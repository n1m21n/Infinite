#pragma once

#include <atomic>
#include <cstdint>

// Global clock. Everything time-based in the graph (modulators, video playback)
// reads from here rather than wall time, so play/pause genuinely freezes the
// whole patch and tempo changes retime every modulator at once.
//
// Beats()/Seconds() are safe to call from the main thread or the real-time
// audio thread, at any time. Whichever clock source is live is decided by
// mAudioSampleRate: >0 means the audio engine is open and AdvanceAudioClock()
// is being fed from real callbacks, so position is computed from the sample
// counter; ==0 means there's no audio device (or it failed to open), so
// Tick()'s frame-driven accumulation is the fallback.
class Transport
{
public:
   static Transport& Instance();

   // Advances the clock by one frame's worth of real time, but only while
   // playing, and only when the audio engine isn't already driving the
   // clock (see mAudioSampleRate). Main thread only.
   void Tick(float deltaSeconds);

   unsigned long long ResetEpoch() const { return mResetEpoch.load(std::memory_order_relaxed); }
   void TriggerReset() { mResetEpoch.fetch_add(1, std::memory_order_relaxed); }

   void SetPlaying(bool playing)
   {
      if (!playing)
      {
         DropFrameClock();
         CancelLaunch(); // nothing is left to launch on a stopped clock
      }
      if (mPlaying && !playing)
         mResetEpoch.fetch_add(1, std::memory_order_relaxed);
      mPlaying = playing;
   }
   bool IsPlaying() const { return mPlaying; }
   void TogglePlay()
   {
      DropFrameClock();
      if (mPlaying)
      {
         mResetEpoch.fetch_add(1, std::memory_order_relaxed);
         CancelLaunch();
      }
      mPlaying = !mPlaying;
   }

   // ---- launch quantize (v0.6 2.38) ---------------------------------------
   // Where a pause or a restart lands while the transport is playing: none
   // (at once, the default), beat, bar or 2 bars (a MusicTime::LaunchQuantize
   // value, stored as an int so this header stays free of MusicTime.h). The
   // Request* calls are what a button, a key or a script presses; the plain
   // TogglePlay / Rewind / Seek above stay immediate for everything else
   // (loading a patch, the Show Set, tests, offline renders).
   //   - Pause while playing, and Rewind while playing, wait for the next grid
   //     line; the clock owner applies them at the first audio block that
   //     contains the line, so the move lands within one block of it (never
   //     later; up to one block early).
   //   - Play from a stopped transport starts at once: a stopped clock has no
   //     bar line coming, and the playhead is wherever it was parked.
   //   - Pressing again while one is waiting cancels it.
   // Every sequencing node (Sequencer, Arpeggiator, Chorder, MIDI File, the
   // generators) follows the transport, so they all start, stop and restart
   // on the grid with no change of their own.
   enum LaunchAction { kLaunchNothing = 0, kLaunchPause, kLaunchRestart };
   void SetLaunchQuantize(int q) { mLaunchQuantize.store(q, std::memory_order_relaxed); if (q <= 0) CancelLaunch(); }
   int LaunchQuantize() const { return mLaunchQuantize.load(std::memory_order_relaxed); }
   // Returns what it did: kLaunchNothing = acted at once (or cancelled a wait),
   // otherwise the action now waiting for `PendingLaunchBeat()`.
   LaunchAction RequestTogglePlay();
   LaunchAction RequestRewind();
   LaunchAction PendingLaunch() const { return (LaunchAction)mPendingLaunch.load(std::memory_order_acquire); }
   double PendingLaunchBeat() const { return mPendingLaunchBeat.load(std::memory_order_relaxed); }
   void CancelLaunch() { mPendingLaunch.store(kLaunchNothing, std::memory_order_release); }

   void Rewind();
   void Seek(double seconds);
   // Beat-addressed seek. The arrangement playhead is Beats(), so every
   // timeline gesture goes through this one; Seek(seconds) stays for the
   // seconds-native callers (video scrub, offline render frame stepping).
   void SeekBeats(double beats);

   // Tempo is *staged*, not applied in place. Applying it here would move
   // Beats() instantly: Beats() is beatsOffset + (Seconds - secOffset) * bpm,
   // so swapping bpm under a live clock jumps the beat position and drags
   // every beat-placed clip with it. The clock owner (AdvanceAudioClock,
   // BeginOfflineAudioBlock, Tick) applies the pending value at a block
   // boundary after rebasing the offsets to the current position, which keeps
   // Beats() continuous across the change. Tempo() reports the staged value
   // so UI and conversions see what was asked for, not a one-block-old bpm.
   void SetTempo(float bpm);
   float Tempo() const
   {
      const float pending = mPendingBpm.load(std::memory_order_relaxed);
      return pending > 0.0f ? pending : mBpm.load(std::memory_order_relaxed);
   }

   // ---- loop (overhaul WP2) ---------------------------------------------
   // Lives here rather than in the arrangement panel's draw: the panel only
   // runs once per UI frame, so a frame-granular wrap overshoots the loop end
   // by however long the frame took (tens of ms, and unbounded during a
   // stall). The wrap now happens wherever the clock is advanced, so the
   // overshoot is at most one audio block.
   void SetLoop(bool enabled, double beatStart, double beatEnd);
   bool LoopEnabled() const { return mLoopEnabled.load(std::memory_order_relaxed); }
   double LoopStartBeats() const { return mLoopStartBeats.load(std::memory_order_relaxed); }
   double LoopEndBeats() const { return mLoopEndBeats.load(std::memory_order_relaxed); }
   // True while an offline take has the loop parked. The user's own loop flag
   // (LoopEnabled) is deliberately left alone across a take, so this is the
   // only way to tell "not looping right now" from "the user turned it off" -
   // INFINITE_ARRANGERENDERTEST asserts the pair (WP7 #3).
   bool LoopSuspended() const { return mLoopSuspended.load(std::memory_order_relaxed); }

   // ---- frame clock (docs/plans/modulation/README.md, Block 2) ----------
   // The live audio clock only moves at block boundaries, so a UI frame
   // reading it lands anywhere up to one block (10.7 ms at 512/48k) away
   // from the frame's real time, and a picture driven by it jitters.
   // BeginFrameClock() latches, for the calling (main) thread only, the
   // position interpolated between audio callbacks by wall time: until
   // the next call, Beats()/Seconds() on this thread return that smooth,
   // frame-consistent value. The audio thread never sees it. A no-op
   // offline (video time is already exact), with no device, or while
   // stopped. Any seek, stop or mode change drops the latch.
   void BeginFrameClock();
   // Engine time: seconds of audio the engine has processed since start,
   // monotonic, advancing whether or not the transport plays. The stamp
   // domain for ParamMailbox ramps. EngineFrameSeconds() is the value
   // BeginFrameClock interpolated (or the current one offline); <0 = none.
   double EngineBlockStartSeconds() const { return mEngineBlockStart.load(std::memory_order_relaxed); }
   double EngineFrameSeconds() const { return mEngineFrameSeconds; }
   double LastBlockSeconds() const { return mLastBlockSeconds.load(std::memory_order_relaxed); }
   // Seconds that only move forward at playing speed, whatever the
   // transport does (stop, seek, loop): free-running modulators step by
   // this. The frame's engine time live, video time offline, the wall
   // clock with no audio engine. Main thread.
   double FreeSeconds() const;
   // Test hook: replaces the steady wall clock (seconds) both threads read.
   static void SetWallClockForTest(double (*now)());

   // Musical position; modulator rates are expressed in beats.
   double Beats() const;
   // Playing time in seconds, also frozen while paused.
   double Seconds() const;

   // Called once, main thread, immediately after AudioEngine::Instance().Start()
   // succeeds. Captures the current frame-driven position as the audio clock's
   // starting offset, so the switch is seamless - no jump at the moment the
   // engine starts producing real callbacks.
   void NotifyAudioEngineStarted(double sampleRate);

   // Called once, main thread, immediately after AudioEngine::Instance().Stop().
   // Freezes mBeats/mSeconds at the audio clock's last live value so Tick()'s
   // fallback path continues from exactly where the audio clock left off.
   void NotifyAudioEngineStopped();

   // Called from the real-time audio thread, once per AudioEngine::Process
   // callback, before any node in that block reads Beats()/Seconds(). Adds
   // numFrames to the sample counter only while playing - this is what makes
   // pause freeze the audio-driven clock the same way it already freezes the
   // frame-driven one.
   void AdvanceAudioClock(int numFrames);

   // The musical position at the START of the block the clock owner most
   // recently advanced over - captured before the advance, so it is the real
   // start even when that block crossed a loop end and Beats() has already
   // wrapped to the loop start. RunTopology derives its per-sample beat axis
   // from this rather than from Beats() - numFrames*rate: on the lap block
   // that subtraction lands on the loop-start axis and misplaces the whole
   // block's envelope, dropping the last few ms before the loop point.
   double BlockStartBeats() const { return mBlockStartBeats.load(std::memory_order_relaxed); }

   // Offline render clock decoupling: keeps visual cooking locked to frame k's
   // exact timestamp (k / fps) while audio synthesis advances along its own
   // sample clock (numFrames / sampleRate), even when lookahead renders audio
   // blocks ahead of the current video frame.
   void SetOfflineMode(bool active, double sampleRate = 0.0);
   bool IsOfflineMode() const { return mOfflineActive.load(std::memory_order_relaxed); }
   double AudioSampleRate() const { return mAudioSampleRate.load(std::memory_order_relaxed); }
   void SetOfflineVideoTime(double seconds);
   void BeginOfflineAudioBlock(int numFrames);
   void EndOfflineAudioBlock();

   // ---- time signature (docs/plans/audio/P3c-P3a2-design.md §0.2) --------
   // Transport had BPM and beats but no bar concept, so nothing could express
   // "1 bar" or "reset every bar". numerator 1-16, denominator restricted to
   // the set a time signature actually uses; an invalid denominator falls
   // back to 4 rather than producing a nonsensical BeatsPerBar().
   void SetTimeSignature(int numerator, int denominator)
   {
      numerator = numerator < 1 ? 1 : (numerator > 99 ? 99 : numerator);
      static const int kValidDenominators[] = { 1, 2, 4, 8, 16 };
      bool ok = false;
      for (int v : kValidDenominators)
         ok = ok || (v == denominator);
      if (!ok)
         denominator = 4;
      mTimeSigNum.store(numerator, std::memory_order_relaxed);
      mTimeSigDen.store(denominator, std::memory_order_relaxed);
   }
   int TimeSigNumerator() const { return mTimeSigNum.load(std::memory_order_relaxed); }
   int TimeSigDenominator() const { return mTimeSigDen.load(std::memory_order_relaxed); }
   // Beats per bar per the current time signature (numerator * 4 / denominator
   // - a quarter note is one beat, so a /4 signature's numerator is beats per
   // bar directly and a /8 signature halves it).
   double BeatsPerBar() const
   {
      const double bpb = (double)TimeSigNumerator() * 4.0 / (double)TimeSigDenominator();
      return bpb > 0.0 ? bpb : 4.0;
   }
   // Musical bar position, derived from Beats() - everything bar-relative
   // (sequencer length, arp retrigger-on-bar, euclidean rotation) reads this.
   double Bars() const { return Beats() / BeatsPerBar(); }

   // ---- global key + scale (docs/plans/audio/P3c-P3a2-design.md §0.3) ----
   // `scale` is a MusicTime::ScaleType, stored as a plain int so this header
   // doesn't have to include MusicTime.h (which itself includes Transport.h
   // for BeatsPerBar() above) - callers cast at both ends, same as every
   // other int-backed enum param in this codebase.
   void SetKey(int root) { mKey.store(((root % 12) + 12) % 12, std::memory_order_relaxed); }
   int Key() const { return mKey.load(std::memory_order_relaxed); }
   void SetScale(int scaleType) { mScale.store(scaleType, std::memory_order_relaxed); }
   int Scale() const { return mScale.load(std::memory_order_relaxed); }

private:
   std::atomic<bool> mPlaying { true };
   std::atomic<float> mBpm { 120.0f };

   // Launch quantize state. The pending action is written by the UI thread
   // (beat first, then the action) and consumed by the clock owner, which
   // clears it with a compare-exchange so a request that lands at the same
   // moment is not lost.
   std::atomic<int> mLaunchQuantize { 0 };
   std::atomic<int> mPendingLaunch { 0 };
   std::atomic<double> mPendingLaunchBeat { 0.0 };
   // Applies a waiting launch whose grid line falls in [b0, b1) (or has
   // passed). Called by the clock owner just before a block advances.
   void ResolveLaunch(double b0, double b1);
   std::atomic<unsigned long long> mResetEpoch { 0 };

   // Fallback (no audio engine) clock state. Only ever touched by Tick(),
   // which only runs on the main thread and only writes these while
   // mAudioSampleRate == 0 - the audio thread never reads or writes them,
   // so they don't need to be atomic.
   double mBeats = 0.0;
   double mSeconds = 0.0;

   // Audio-driven clock state. mAudioSampleRate > 0.0 means the audio engine
   // is open and live; that single flag is "the engine is open and its
   // sample counter is live" - no separate bool to keep in sync with it.
   std::atomic<uint64_t> mAudioSampleCounter { 0 };
   std::atomic<double> mAudioSampleRate { 0.0 };
   std::atomic<double> mAudioBeatsOffset { 0.0 };
   std::atomic<double> mAudioSecondsOffset { 0.0 };
   std::atomic<double> mPendingSeekSeconds { -1.0 };
   std::atomic<double> mBlockStartBeats { 0.0 };
   std::atomic<float> mPendingBpm { -1.0f }; // <= 0 means "nothing staged"

   std::atomic<bool> mLoopEnabled { false };
   std::atomic<double> mLoopStartBeats { 0.0 };
   std::atomic<double> mLoopEndBeats { 0.0 };
   // Offline render walks the timeline once, start to end; a loop would make
   // it render the loop body forever. SetOfflineMode(true) suspends the wrap
   // without clearing the user's loop, and false restores it.
   std::atomic<bool> mLoopSuspended { false };

   // Applies a staged tempo, rebasing the offsets first so Beats() doesn't
   // jump. Called only by the live clock owner, never by UI code.
   void ApplyPendingTempo();
   // Consumes a staged tempo for a seek, which re-seats both offsets anyway
   // and so needs no rebasing. Returns the bpm to use for the seek.
   double CommitTempoForSeek();
   void SeekInternal(double seconds, double beats);
   // Wraps the position back into the loop range if it has run past the end.
   // Called after the clock advances, on whichever thread owns it.
   void WrapLoopIfNeeded();

   std::atomic<int> mTimeSigNum { 4 };
   std::atomic<int> mTimeSigDen { 4 };
   std::atomic<int> mKey { 0 };   // 0 = C
   std::atomic<int> mScale { 0 }; // MusicTime::kMajor

   // Frame clock. mEngine* are written by the clock owner (audio thread, or
   // the offline pump); mBlockSeq brackets each update so the main thread
   // can read a consistent set. The latched pair is main-thread only.
   std::atomic<uint64_t> mEngineSamples { 0 };
   std::atomic<double> mEngineBlockStart { 0.0 };
   std::atomic<double> mLastCallbackWall { -1.0 };
   std::atomic<double> mLastBlockSeconds { 0.0 };
   std::atomic<uint64_t> mBlockSeq { 0 };
   double mEngineFrameSeconds = -1.0;
   double mLatchedSeconds = 0.0;
   double mLatchedBeats = 0.0;
   unsigned long long mLatchedEpoch = 0;
   uint64_t mLatchedSeq = 0;
   // True while some thread holds a frame latch. Checked first so the
   // common unlatched read (audio thread, offline, tests) skips the
   // thread_local lookup.
   std::atomic<bool> mLatchArmed { false };
   bool FrameLatchLive() const;
   void AdvanceEngineClock(int numFrames, double sampleRate);
   void DropFrameClock();

   std::atomic<bool> mOfflineActive { false };
   std::atomic<bool> mOfflineInAudioBlock { false };
   std::atomic<double> mOfflineVideoSeconds { 0.0 };
};

// Points Transport::Instance() at another clock, for the calling thread only,
// for as long as it lives. An offline render of a private graph (Generate
// Samples) runs its sequencers on their own playhead, at their own tempo,
// without moving or reading the live one. Nests; the audio thread never has one.
class ScopedTransport
{
public:
   explicit ScopedTransport(Transport& clock);
   ~ScopedTransport();
   ScopedTransport(const ScopedTransport&) = delete;
   ScopedTransport& operator=(const ScopedTransport&) = delete;

private:
   Transport* mPrev;
};

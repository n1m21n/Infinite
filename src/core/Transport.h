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

   void SetPlaying(bool playing)
   {
      if (!playing)
         CancelLaunch(); // nothing is left to launch on a stopped clock
      mPlaying = playing;
   }
   bool IsPlaying() const { return mPlaying; }
   // Upstream offline render; Turbo has none, so ported nodes always see live time.
   bool IsOfflineMode() const { return false; }
   void TogglePlay()
   {
      if (mPlaying)
         CancelLaunch();
      mPlaying = !mPlaying;
   }

   // ---- launch quantize (Turbo 0.72, upstream v0.6 2.38) -----------------
   // Where a pause or a restart lands while the transport is playing: none
   // (at once, the default), beat, bar or 2 bars (a MusicTime::LaunchQuantize
   // value). The Request* calls are what a button or a key presses; the plain
   // TogglePlay / Rewind / Seek stay immediate for everything else (loading a
   // patch, tests, renders).
   //   - Pause and Rewind while playing wait for the next grid line; the clock
   //     owner applies them at the first audio block that contains the line.
   //   - Play from a stopped transport starts at once.
   //   - Pressing again while one is waiting cancels it.
   enum LaunchAction { kLaunchNothing = 0, kLaunchPause, kLaunchRestart };
   void SetLaunchQuantize(int q) { mLaunchQuantize.store(q, std::memory_order_relaxed); if (q <= 0) CancelLaunch(); }
   int LaunchQuantize() const { return mLaunchQuantize.load(std::memory_order_relaxed); }
   LaunchAction RequestTogglePlay();
   LaunchAction RequestRewind();
   LaunchAction PendingLaunch() const { return (LaunchAction)mPendingLaunch.load(std::memory_order_acquire); }
   double PendingLaunchBeat() const { return mPendingLaunchBeat.load(std::memory_order_relaxed); }
   void CancelLaunch() { mPendingLaunch.store(kLaunchNothing, std::memory_order_release); }

   void Rewind()
   {
      mBeats = 0.0;
      mSeconds = 0.0;
      // These three stores aren't atomic as a group, so a concurrent
      // AdvanceAudioClock() fetch_add from the audio thread can race a
      // Rewind() from the main thread. Worst case is one block's worth
      // (~10ms) of position error on the sample a rewind lands on, which
      // is inaudible - not worth a lock for.
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
      mAudioBeatsOffset.store(0.0, std::memory_order_relaxed);
      mAudioSecondsOffset.store(0.0, std::memory_order_relaxed);
      mPendingSeekBeats.store(-1.0, std::memory_order_relaxed);
      mVisualValid.store(false, std::memory_order_relaxed); // the frame clock snaps to 0 too
      mSeekSerial.fetch_add(1, std::memory_order_release);
   }

   // Turbo (upstream approach): while the audio engine drives the clock,
   // Beats() is offset + elapsed * bpm, so swapping bpm directly re-measures
   // the whole elapsed span and the playhead jumps. The new tempo is staged
   // here and applied by the audio thread at the next block boundary, after
   // re-basing the offsets to "now" (ApplyPendingTempo). Without a live audio
   // clock the fallback clock is incremental and the tempo lands directly.
   void SetTempo(float bpm);
   float Tempo() const
   {
      const float pending = mPendingBpm.load(std::memory_order_relaxed);
      return pending > 0.0f ? pending : mBpm.load(std::memory_order_relaxed);
   }
   // Audio thread (AdvanceAudioClock) or main thread when no audio clock.
   void ApplyPendingTempo();

   // Turbo (Arrangement Timeline): jump the playhead. With a live audio
   // clock the target is staged and applied by the audio thread at the next
   // block boundary (AdvanceAudioClock), so a block never sees a half-moved
   // clock; without one it lands directly. Seconds follow as beats * 60/bpm.
   void SeekBeats(double beats);
   // Loop range in beats: when enabled and playing, reaching `end` jumps to
   // `start` at the block boundary.
   void SetLoop(bool enabled, double beatStart, double beatEnd);
   // Turbo 0.72 (upstream's BlockStartBeats): the beat at the first frame of
   // the block AdvanceAudioClock just opened (after its seek or loop wrap,
   // before the advance). Beats() already reads the block's END there, so
   // anything scheduling inside the block (the timeline mix) must use this or
   // it runs one block early. Without an audio clock it is Beats().
   double BlockStartBeats() const
   {
      return mAudioSampleRate.load(std::memory_order_relaxed) > 0.0 ? mBlockStartBeats.load(std::memory_order_relaxed)
                                                                    : Beats();
   }
   bool LoopEnabled() const { return mLoopEnabled.load(std::memory_order_relaxed); }
   double LoopStartBeats() const { return mLoopStartBeats.load(std::memory_order_relaxed); }
   double LoopEndBeats() const { return mLoopEndBeats.load(std::memory_order_relaxed); }
   // Bumped by every seek / loop wrap the clock actually applied, so a
   // reader (the timeline's retrigger, a video clip) can tell a jump from
   // ordinary playback.
   uint32_t SeekSerial() const { return mSeekSerial.load(std::memory_order_acquire); }

   // Musical position; modulator rates are expressed in beats.
   double Beats() const;
   // Playing time in seconds, also frozen while paused.
   double Seconds() const;
   // Upstream: seconds that keep running whatever the transport does (stop,
   // seek, loop) - free-running modulators step by this. Steady wall clock here.
   double FreeSeconds() const;
   // Upstream: the engine time of the current audio block, the stamp domain
   // ParamMailbox ramps toward once frames are published (PublishFrame). Turbo
   // never publishes, so the mailbox only ever runs its fixed fallback ramp and
   // this value just has to be steady.
   double EngineBlockStartSeconds() const { return 0.0; }

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

   // ---- time signature (docs/plans/audio/P3c-P3a2-design.md §0.2) --------
   // Transport had BPM and beats but no bar concept, so nothing could express
   // "1 bar" or "reset every bar". numerator 1-16, denominator restricted to
   // the set a time signature actually uses; an invalid denominator falls
   // back to 4 rather than producing a nonsensical BeatsPerBar().
   void SetTimeSignature(int numerator, int denominator)
   {
      numerator = numerator < 1 ? 1 : (numerator > 16 ? 16 : numerator);
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
   std::atomic<float> mPendingBpm { -1.0f };

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
   // Turbo 0.59: smooth clock between audio blocks for every reader that is
   // not the thread driving the clock (see Seconds()). The counter only moves
   // once per block (10-20 ms), so a 60 fps frame used to see time stand
   // still on one frame and jump two blocks on the next: a stuttering image.
   // mBlockSeq is odd while a block's counter / frames / wall time change.
   std::atomic<uint32_t> mBlockSeq { 0 };
   std::atomic<uint32_t> mBlockFrames { 0 };
   std::atomic<int64_t> mBlockWallNs { 0 };
   std::atomic<int> mClockThread { 0 }; // the thread that last called AdvanceAudioClock
   // Turbo 0.59: the frame clock the visuals read (see Tick): advances by each
   // frame's real duration and leans gently toward the audio position, so it
   // is smooth even when audio blocks arrive in bunches. Snaps on a jump
   // (seek, loop, rewind) and is only used by the thread that calls Tick.
   std::atomic<double> mVisualSeconds { 0.0 };
   std::atomic<bool> mVisualValid { false };
   std::atomic<int> mTickThread { 0 };
   double LiveSeconds() const; // audio position + time since its last block
   std::atomic<double> mAudioSampleRate { 0.0 };
   std::atomic<double> mAudioBeatsOffset { 0.0 };
   std::atomic<double> mAudioSecondsOffset { 0.0 };

   std::atomic<double> mPendingSeekBeats { -1.0 };
   std::atomic<double> mBlockStartBeats { 0.0 }; // Turbo 0.72: see BlockStartBeats()
   // Turbo 0.72: launch quantize state. The pending action is written by the
   // UI thread (beat first, then the action) and consumed by the clock owner,
   // which clears it with a compare-exchange so a request landing at the same
   // moment is not lost.
   std::atomic<int> mLaunchQuantize { 0 };
   std::atomic<int> mPendingLaunch { 0 };
   std::atomic<double> mPendingLaunchBeat { 0.0 };
   // Applies a waiting launch whose grid line falls in [b0, b1) (or has passed).
   void ResolveLaunch(double b0, double b1);
   std::atomic<bool> mLoopEnabled { false };
   std::atomic<double> mLoopStartBeats { 0.0 };
   std::atomic<double> mLoopEndBeats { 0.0 };
   std::atomic<uint32_t> mSeekSerial { 0 };
   void ApplySeek(double beats);

   std::atomic<int> mTimeSigNum { 4 };
   std::atomic<int> mTimeSigDen { 4 };
   std::atomic<int> mKey { 0 };   // 0 = C
   std::atomic<int> mScale { 0 }; // MusicTime::kMajor
};

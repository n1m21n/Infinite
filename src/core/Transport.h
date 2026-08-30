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

   void SetPlaying(bool playing) { mPlaying = playing; }
   bool IsPlaying() const { return mPlaying; }
   void TogglePlay() { mPlaying = !mPlaying; }

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
   }

   void SetTempo(float bpm) { mBpm = bpm < 1.0f ? 1.0f : bpm; }
   float Tempo() const { return mBpm; }

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

   // Offline render clock decoupling: keeps visual cooking locked to frame k's
   // exact timestamp (k / fps) while audio synthesis advances along its own
   // sample clock (numFrames / sampleRate), even when lookahead renders audio
   // blocks ahead of the current video frame.
   void SetOfflineMode(bool active, double sampleRate = 0.0);
   bool IsOfflineMode() const { return mOfflineActive.load(std::memory_order_relaxed); }
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

   std::atomic<int> mTimeSigNum { 4 };
   std::atomic<int> mTimeSigDen { 4 };
   std::atomic<int> mKey { 0 };   // 0 = C
   std::atomic<int> mScale { 0 }; // MusicTime::kMajor

   std::atomic<bool> mOfflineActive { false };
   std::atomic<bool> mOfflineInAudioBlock { false };
   std::atomic<double> mOfflineVideoSeconds { 0.0 };
};

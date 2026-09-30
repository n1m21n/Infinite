#include "Transport.h"

Transport& Transport::Instance()
{
   static Transport instance;
   return instance;
}

double Transport::Seconds() const
{
   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   if (sr > 0.0)
   {
      return mAudioSecondsOffset.load(std::memory_order_relaxed) +
             (double)mAudioSampleCounter.load(std::memory_order_relaxed) / sr;
   }
   return mSeconds;
}

double Transport::Beats() const
{
   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   if (sr <= 0.0)
      return mBeats;
   const double secOffset = mAudioSecondsOffset.load(std::memory_order_relaxed);
   return mAudioBeatsOffset.load(std::memory_order_relaxed) +
          (Seconds() - secOffset) * (mBpm.load(std::memory_order_relaxed) / 60.0);
}

void Transport::SetTempo(float bpm)
{
   bpm = bpm < 1.0f ? 1.0f : bpm;
   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
      mPendingBpm.store(bpm, std::memory_order_relaxed);
   else
   {
      mPendingBpm.store(-1.0f, std::memory_order_relaxed);
      mBpm.store(bpm, std::memory_order_relaxed);
   }
}

void Transport::ApplyPendingTempo()
{
   const float pending = mPendingBpm.exchange(-1.0f, std::memory_order_relaxed);
   if (pending <= 0.0f || pending == mBpm.load(std::memory_order_relaxed))
      return;
   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
   {
      // Pin the offsets to where the clock is now, under the old bpm, so the
      // new bpm only measures time from this instant on.
      const double beats = Beats();
      const double secs = Seconds();
      mAudioBeatsOffset.store(beats, std::memory_order_relaxed);
      mAudioSecondsOffset.store(secs, std::memory_order_relaxed);
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
   }
   mBpm.store(pending, std::memory_order_relaxed); // publish last
}

void Transport::Tick(float deltaSeconds)
{
   // clamp so a stalled frame (window drag, file dialog) doesn't jump the clock
   if (deltaSeconds > 0.25f)
      deltaSeconds = 0.25f;

   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
      return; // audio-driven: Beats()/Seconds() compute live, nothing to accumulate here

   if (!mPlaying.load(std::memory_order_relaxed))
      return;

   mSeconds += deltaSeconds;
   mBeats += deltaSeconds * (mBpm.load(std::memory_order_relaxed) / 60.0);
}

void Transport::NotifyAudioEngineStarted(double sampleRate)
{
   {
      const float pending = mPendingBpm.exchange(-1.0f, std::memory_order_relaxed);
      if (pending > 0.0f)
         mBpm.store(pending, std::memory_order_relaxed);
   }
   mAudioSecondsOffset.store(mSeconds, std::memory_order_relaxed);
   mAudioBeatsOffset.store(mBeats, std::memory_order_relaxed);
   mAudioSampleCounter.store(0, std::memory_order_relaxed);
   mAudioSampleRate.store(sampleRate, std::memory_order_relaxed); // publish last
}

void Transport::NotifyAudioEngineStopped()
{
   ApplyPendingTempo();
   mSeconds = Seconds(); // read the still-live audio-driven value...
   mBeats = Beats();
   mAudioSampleRate.store(0.0, std::memory_order_relaxed); // ...then switch back to fallback
}

void Transport::AdvanceAudioClock(int numFrames)
{
   ApplyPendingTempo(); // block boundary: rebase and land a staged tempo
   if (mPlaying.load(std::memory_order_relaxed))
      mAudioSampleCounter.fetch_add((uint64_t)numFrames, std::memory_order_relaxed);
}

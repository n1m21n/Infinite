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

void Transport::Tick(float deltaSeconds)
{
   // clamp so a stalled frame (window drag, file dialog) doesn't jump the clock
   if (deltaSeconds > 0.25f)
      deltaSeconds = 0.25f;

   // External clock freshness ages regardless of play state or clock source -
   // a MIDI clock source dropping out (deck unplugged, pause on the mixer)
   // should still be detected while the transport itself is paused.
   if (mExternalSync && mExternalClockFresh)
   {
      mExternalClockAge += deltaSeconds;
      if (mExternalClockAge > kExternalClockTimeoutSeconds)
         mExternalClockFresh = false;
   }

   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
      return; // audio-driven: Beats()/Seconds() compute live, nothing to accumulate here

   if (!mPlaying.load(std::memory_order_relaxed))
      return;

   mSeconds += deltaSeconds;
   mBeats += deltaSeconds * (mBpm.load(std::memory_order_relaxed) / 60.0);
}

void Transport::NotifyAudioEngineStarted(double sampleRate)
{
   mAudioSecondsOffset.store(mSeconds, std::memory_order_relaxed);
   mAudioBeatsOffset.store(mBeats, std::memory_order_relaxed);
   mAudioSampleCounter.store(0, std::memory_order_relaxed);
   mAudioSampleRate.store(sampleRate, std::memory_order_relaxed); // publish last
}

void Transport::NotifyAudioEngineStopped()
{
   mSeconds = Seconds(); // read the still-live audio-driven value...
   mBeats = Beats();
   mAudioSampleRate.store(0.0, std::memory_order_relaxed); // ...then switch back to fallback
}

void Transport::AdvanceAudioClock(int numFrames)
{
   if (mPlaying.load(std::memory_order_relaxed))
      mAudioSampleCounter.fetch_add((uint64_t)numFrames, std::memory_order_relaxed);
}

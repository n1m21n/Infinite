#include "Transport.h"

#include <algorithm>
#include <chrono>

namespace
{
   // A small per-thread id (std::thread::id is not usable in an atomic).
   int ThisThreadTag()
   {
      static std::atomic<int> sNext { 1 };
      thread_local const int tTag = sNext.fetch_add(1, std::memory_order_relaxed);
      return tTag;
   }

   int64_t NowNs()
   {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
   }
}

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
      // The thread driving the clock (the audio callback, or the main thread
      // during an offline render) reads the block-exact position.
      if (!mPlaying.load(std::memory_order_relaxed) ||
          mClockThread.load(std::memory_order_relaxed) == ThisThreadTag())
         return mAudioSecondsOffset.load(std::memory_order_relaxed) +
                (double)mAudioSampleCounter.load(std::memory_order_relaxed) / sr;
      // Turbo 0.59: the frame thread reads the smooth frame clock (Tick).
      if (mVisualValid.load(std::memory_order_relaxed) &&
          mTickThread.load(std::memory_order_relaxed) == ThisThreadTag())
         return mVisualSeconds.load(std::memory_order_relaxed);
      return LiveSeconds();
   }
   return mSeconds;
}

double Transport::LiveSeconds() const
{
   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   if (sr > 0.0)
   {
      // The last block's start plus the real time since it arrived, capped
      // at that block's length - continuous at the next block, never ahead.
      for (int attempt = 0; attempt < 4; attempt++)
      {
         const uint32_t seq = mBlockSeq.load(std::memory_order_acquire);
         if (seq & 1u)
            continue;
         const double offset = mAudioSecondsOffset.load(std::memory_order_relaxed);
         const uint64_t counter = mAudioSampleCounter.load(std::memory_order_relaxed);
         const uint32_t frames = mBlockFrames.load(std::memory_order_relaxed);
         const int64_t wall = mBlockWallNs.load(std::memory_order_relaxed);
         if (mBlockSeq.load(std::memory_order_acquire) != seq)
            continue;
         const double blockSec = (double)frames / sr;
         const double since = wall > 0 ? (double)(NowNs() - wall) * 1e-9 : 0.0;
         return offset + (double)counter / sr + std::clamp(since, 0.0, blockSec);
      }
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

   mTickThread.store(ThisThreadTag(), std::memory_order_relaxed);
   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
   {
      // Audio-driven: nothing to accumulate, but the frame clock moves.
      // Turbo 0.59: by this frame's real duration, corrected by a small share
      // of its distance to the audio position, so blocks that arrive unevenly
      // (two at once, then a gap) never show as a stutter. A big distance is
      // a jump (seek, loop wrap, tempo rebase): follow it at once.
      if (!mPlaying.load(std::memory_order_relaxed))
      {
         mVisualValid.store(false, std::memory_order_relaxed);
         return;
      }
      const double target = LiveSeconds();
      double v = target;
      if (mVisualValid.load(std::memory_order_relaxed))
      {
         v = mVisualSeconds.load(std::memory_order_relaxed) + (double)deltaSeconds;
         const double err = target - v;
         if (err > 0.1 || err < -0.1)
            v = target;
         else
            v += err * 0.08;
      }
      mVisualSeconds.store(v, std::memory_order_relaxed);
      mVisualValid.store(true, std::memory_order_relaxed);
      return;
   }

   if (!mPlaying.load(std::memory_order_relaxed))
      return;

   mSeconds += deltaSeconds;
   mBeats += deltaSeconds * (mBpm.load(std::memory_order_relaxed) / 60.0);
   if (mLoopEnabled.load(std::memory_order_relaxed))
   {
      const double start = mLoopStartBeats.load(std::memory_order_relaxed);
      const double end = mLoopEndBeats.load(std::memory_order_relaxed);
      if (end > start + 1e-6 && mBeats >= end)
         ApplySeek(start);
   }
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
   mVisualValid.store(false, std::memory_order_relaxed);
   mAudioSampleRate.store(sampleRate, std::memory_order_relaxed); // publish last
}

void Transport::NotifyAudioEngineStopped()
{
   ApplyPendingTempo();
   mSeconds = Seconds(); // read the still-live audio-driven value...
   mBeats = Beats();
   mAudioSampleRate.store(0.0, std::memory_order_relaxed); // ...then switch back to fallback
   mVisualValid.store(false, std::memory_order_relaxed);
}

void Transport::AdvanceAudioClock(int numFrames)
{
   ApplyPendingTempo(); // block boundary: rebase and land a staged tempo
   const double seek = mPendingSeekBeats.exchange(-1.0, std::memory_order_relaxed);
   if (seek >= 0.0)
      ApplySeek(seek);
   else if (mPlaying.load(std::memory_order_relaxed) && mLoopEnabled.load(std::memory_order_relaxed))
   {
      // Wrap at the block boundary that reaches the loop end.
      const double start = mLoopStartBeats.load(std::memory_order_relaxed);
      const double end = mLoopEndBeats.load(std::memory_order_relaxed);
      if (end > start + 1e-6 && Beats() >= end)
         ApplySeek(start);
   }
   mClockThread.store(ThisThreadTag(), std::memory_order_relaxed);
   if (mPlaying.load(std::memory_order_relaxed))
   {
      mBlockSeq.fetch_add(1, std::memory_order_acq_rel); // odd: changing
      mAudioSampleCounter.fetch_add((uint64_t)numFrames, std::memory_order_relaxed);
      mBlockFrames.store((uint32_t)std::max(0, numFrames), std::memory_order_relaxed);
      mBlockWallNs.store(NowNs(), std::memory_order_relaxed);
      mBlockSeq.fetch_add(1, std::memory_order_release); // even: consistent
   }
}

void Transport::ApplySeek(double beats)
{
   beats = beats < 0.0 ? 0.0 : beats;
   const double secs = beats * 60.0 / (double)(mBpm.load(std::memory_order_relaxed) > 0.0f ? mBpm.load(std::memory_order_relaxed) : 120.0f);
   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
   {
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
      mAudioSecondsOffset.store(secs, std::memory_order_relaxed);
      mAudioBeatsOffset.store(beats, std::memory_order_relaxed);
   }
   else
   {
      mBeats = beats;
      mSeconds = secs;
   }
   mSeekSerial.fetch_add(1, std::memory_order_release);
}

void Transport::SeekBeats(double beats)
{
   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
      mPendingSeekBeats.store(beats < 0.0 ? 0.0 : beats, std::memory_order_relaxed);
   else
      ApplySeek(beats);
}

void Transport::SetLoop(bool enabled, double beatStart, double beatEnd)
{
   mLoopStartBeats.store(beatStart, std::memory_order_relaxed);
   mLoopEndBeats.store(beatEnd, std::memory_order_relaxed);
   mLoopEnabled.store(enabled && beatEnd > beatStart, std::memory_order_relaxed);
}

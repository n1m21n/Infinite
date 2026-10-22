#include "Transport.h"

#include "audio/MusicTime.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
   // Set by BeginFrameClock on the thread that latched (the main thread);
   // never true on the audio thread, so its reads are untouched.
   thread_local bool tFrameLatched = false;

   double SteadyNow()
   {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
   }
   std::atomic<double (*)()> sWallClock { &SteadyNow };
}

void Transport::SetWallClockForTest(double (*now)())
{
   sWallClock.store(now != nullptr ? now : &SteadyNow, std::memory_order_relaxed);
}

void Transport::DropFrameClock()
{
   tFrameLatched = false;
   mLatchArmed.store(false, std::memory_order_relaxed);
}

// The latch covers one UI frame. It lapses by itself after 8 audio blocks
// so main-thread code that waits on the clock without running frames
// (tests, modal loops) still sees it move.
bool Transport::FrameLatchLive() const
{
   return mLatchArmed.load(std::memory_order_relaxed) && tFrameLatched && mBlockSeq.load(std::memory_order_relaxed) - mLatchedSeq <= 16;
}

void Transport::AdvanceEngineClock(int numFrames, double sampleRate)
{
   if (sampleRate <= 0.0)
      return;
   mBlockSeq.fetch_add(1, std::memory_order_acq_rel); // odd: update in progress
   const uint64_t start = mEngineSamples.load(std::memory_order_relaxed);
   mEngineBlockStart.store((double)start / sampleRate, std::memory_order_relaxed);
   mEngineSamples.store(start + (uint64_t)std::max(0, numFrames), std::memory_order_relaxed);
   mLastBlockSeconds.store((double)numFrames / sampleRate, std::memory_order_relaxed);
   mLastCallbackWall.store(sWallClock.load(std::memory_order_relaxed)(), std::memory_order_relaxed);
   mBlockSeq.fetch_add(1, std::memory_order_acq_rel); // even: consistent
}

double Transport::FreeSeconds() const
{
   if (mOfflineActive.load(std::memory_order_relaxed))
      return mOfflineVideoSeconds.load(std::memory_order_relaxed);
   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   if (sr > 0.0)
      return mEngineFrameSeconds >= 0.0 ? mEngineFrameSeconds
                                        : (double)mEngineSamples.load(std::memory_order_relaxed) / sr;
   return sWallClock.load(std::memory_order_relaxed)();
}

void Transport::BeginFrameClock()
{
   tFrameLatched = false;
   mEngineFrameSeconds = -1.0;
   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   if (sr <= 0.0)
      return;
   if (mOfflineActive.load(std::memory_order_relaxed))
   {
      // Offline the engine clock only moves inside the pump's audio
      // blocks, so "now" is exactly where the next block starts.
      mEngineFrameSeconds = (double)mEngineSamples.load(std::memory_order_relaxed) / sr;
      return;
   }

   // One consistent snapshot of the last callback: elapsed wall time
   // since it began, clamped to the block it covers, says how far
   // through that block the frame is.
   double seconds = 0.0, beats = 0.0, offset = 0.0, engineEnd = 0.0;
   uint64_t seqUsed = 0;
   for (int attempt = 0; attempt < 4; attempt++)
   {
      const uint64_t seq0 = mBlockSeq.load(std::memory_order_acquire);
      if (seq0 & 1)
         continue;
      const double wall0 = mLastCallbackWall.load(std::memory_order_relaxed);
      const double block = mLastBlockSeconds.load(std::memory_order_relaxed);
      engineEnd = (double)mEngineSamples.load(std::memory_order_relaxed) / sr;
      seconds = Seconds();
      beats = Beats();
      if (mBlockSeq.load(std::memory_order_acquire) != seq0)
         continue;
      seqUsed = seq0;
      if (wall0 >= 0.0 && block > 0.0)
         offset = std::clamp(sWallClock.load(std::memory_order_relaxed)() - wall0, 0.0, block) - block;
      break;
   }
   mEngineFrameSeconds = engineEnd + offset;

   if (!mPlaying.load(std::memory_order_relaxed) || mPendingSeekSeconds.load(std::memory_order_relaxed) >= 0.0)
      return;
   const double bpm = mBpm.load(std::memory_order_relaxed);
   double s = seconds + offset;
   double b = beats + offset * (bpm / 60.0);
   // The block that just wrapped the loop: the frame may still sit before
   // the wrap, on the far side of the loop end.
   if (mLoopEnabled.load(std::memory_order_relaxed) && !mLoopSuspended.load(std::memory_order_relaxed))
   {
      const double loopStart = mLoopStartBeats.load(std::memory_order_relaxed);
      const double span = mLoopEndBeats.load(std::memory_order_relaxed) - loopStart;
      if (span > 0.0 && b < loopStart && beats >= loopStart)
      {
         b += span;
         s += span * (60.0 / bpm);
      }
   }
   // Never step backwards within one transport epoch (callback jitter).
   const unsigned long long epoch = mResetEpoch.load(std::memory_order_relaxed);
   if (epoch == mLatchedEpoch && b < mLatchedBeats && mLatchedBeats - b < 0.25)
   {
      b = mLatchedBeats;
      s = mLatchedSeconds;
   }
   mLatchedSeconds = s;
   mLatchedBeats = b;
   mLatchedEpoch = epoch;
   mLatchedSeq = seqUsed;
   tFrameLatched = true;
   mLatchArmed.store(true, std::memory_order_relaxed);
}

Transport& Transport::Instance()
{
   static Transport instance;
   return instance;
}

double Transport::Seconds() const
{
   const double pending = mPendingSeekSeconds.load(std::memory_order_relaxed);
   if (pending >= 0.0)
      return pending;

   if (mOfflineActive.load(std::memory_order_relaxed))
   {
      if (mOfflineInAudioBlock.load(std::memory_order_relaxed))
      {
         const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
         if (sr > 0.0)
         {
            return mAudioSecondsOffset.load(std::memory_order_relaxed) +
                   (double)mAudioSampleCounter.load(std::memory_order_relaxed) / sr;
         }
      }
      return mOfflineVideoSeconds.load(std::memory_order_relaxed);
   }

   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   if (sr > 0.0)
   {
      // Only the live audio clock is ever latched (BeginFrameClock), so
      // offline and fallback reads never pay for the check.
      if (FrameLatchLive())
         return mLatchedSeconds;
      return mAudioSecondsOffset.load(std::memory_order_relaxed) +
             (double)mAudioSampleCounter.load(std::memory_order_relaxed) / sr;
   }
   return mSeconds;
}

double Transport::Beats() const
{
   const double pending = mPendingSeekSeconds.load(std::memory_order_relaxed);
   if (pending >= 0.0)
      return pending * (mBpm.load(std::memory_order_relaxed) / 60.0);

   const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
   const bool offline = mOfflineActive.load(std::memory_order_relaxed);
   if (sr <= 0.0 && !offline)
      return mBeats;
   if (!offline && FrameLatchLive())
      return mLatchedBeats;
   const double secOffset = mAudioSecondsOffset.load(std::memory_order_relaxed);
   return mAudioBeatsOffset.load(std::memory_order_relaxed) +
          (Seconds() - secOffset) * (mBpm.load(std::memory_order_relaxed) / 60.0);
}

void Transport::SetTempo(float bpm)
{
   // A patch or show file is outside input. A NaN would slip past a `< 1`
   // test and ApplyPendingTempo, and a huge bpm overflows the step clocks'
   // tick math (StepClock::Ticks). Hold to headless --bpm's 1..999.
   if (!std::isfinite(bpm))
      return;
   mPendingBpm.store(std::clamp(bpm, 1.0f, 999.0f), std::memory_order_relaxed);
}

void Transport::ApplyPendingTempo()
{
   const float pending = mPendingBpm.exchange(-1.0f, std::memory_order_relaxed);
   if (pending <= 0.0f || pending == mBpm.load(std::memory_order_relaxed))
      return;

   // Rebase first: pin the offsets to where the clock is *now*, under the old
   // bpm, so the new bpm only affects time from this instant forward. Without
   // this the whole elapsed span gets re-measured at the new tempo and the
   // playhead teleports.
   const double beats = Beats();
   const double secs = Seconds();
   mAudioBeatsOffset.store(beats, std::memory_order_relaxed);
   mAudioSecondsOffset.store(secs, std::memory_order_relaxed);
   mAudioSampleCounter.store(0, std::memory_order_relaxed);
   // mBeats/mSeconds are the non-atomic fallback pair, owned by the main
   // thread. Only touch them on the path where no audio clock is live -
   // otherwise this runs on the audio thread and would race Tick().
   if (mAudioSampleRate.load(std::memory_order_relaxed) <= 0.0 &&
       !mOfflineActive.load(std::memory_order_relaxed))
   {
      mBeats = beats;
      mSeconds = secs;
   }
   mBpm.store(pending, std::memory_order_relaxed); // publish last
}

double Transport::CommitTempoForSeek()
{
   // A seek re-seats both offsets from scratch, so there is nothing to rebase
   // - the staged tempo can just land, and the seek is measured with it.
   const float pending = mPendingBpm.exchange(-1.0f, std::memory_order_relaxed);
   if (pending > 0.0f)
      mBpm.store(pending, std::memory_order_relaxed);
   return (double)mBpm.load(std::memory_order_relaxed);
}

void Transport::Tick(float deltaSeconds)
{
   // clamp so a stalled frame (window drag, file dialog) doesn't jump the clock
   if (deltaSeconds > 0.25f)
      deltaSeconds = 0.25f;

   if (mOfflineActive.load(std::memory_order_relaxed))
      return; // offline mode: video frame time is driven explicitly by SetOfflineVideoTime

   if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
      return; // audio-driven: Beats()/Seconds() compute live, nothing to accumulate here

   ApplyPendingTempo(); // applies while paused too - tempo isn't a transport move

   if (!mPlaying.load(std::memory_order_relaxed))
      return;

   ResolveLaunch(mBeats, mBeats + deltaSeconds * (mBpm.load(std::memory_order_relaxed) / 60.0));
   if (!mPlaying.load(std::memory_order_relaxed))
      return;

   mSeconds += deltaSeconds;
   mBeats += deltaSeconds * (mBpm.load(std::memory_order_relaxed) / 60.0);

   WrapLoopIfNeeded();
}

void Transport::SetLoop(bool enabled, double beatStart, double beatEnd)
{
   if (beatStart < 0.0)
      beatStart = 0.0;
   if (beatEnd < beatStart)
      beatEnd = beatStart;
   mLoopStartBeats.store(beatStart, std::memory_order_relaxed);
   mLoopEndBeats.store(beatEnd, std::memory_order_relaxed);
   mLoopEnabled.store(enabled && (beatEnd > beatStart), std::memory_order_relaxed); // publish last
}

void Transport::WrapLoopIfNeeded()
{
   if (!mLoopEnabled.load(std::memory_order_relaxed) ||
       mLoopSuspended.load(std::memory_order_relaxed) ||
       !mPlaying.load(std::memory_order_relaxed))
      return;

   const double loopStart = mLoopStartBeats.load(std::memory_order_relaxed);
   const double loopEnd = mLoopEndBeats.load(std::memory_order_relaxed);
   const double span = loopEnd - loopStart;
   if (!(span > 0.0))
      return;

   const double beats = Beats();
   if (beats < loopEnd)
      return;

   // fmod, not "snap to loopStart": the block that crossed the end carries an
   // overshoot, and dropping it would make every lap a fraction of a block
   // short. fmod also covers the case where the loop was shrunk under a
   // playhead that is now several spans past the end.
   const double wrapped = loopStart + std::fmod(beats - loopStart, span);
   const double bpm = mBpm.load(std::memory_order_relaxed);
   const double seconds = Seconds() - (beats - wrapped) * (60.0 / bpm);

   mAudioBeatsOffset.store(wrapped, std::memory_order_relaxed);
   mAudioSecondsOffset.store(seconds, std::memory_order_relaxed);
   mAudioSampleCounter.store(0, std::memory_order_relaxed);
   if (mAudioSampleRate.load(std::memory_order_relaxed) <= 0.0 &&
       !mOfflineActive.load(std::memory_order_relaxed))
   {
      mBeats = wrapped;
      mSeconds = seconds;
   }
   // Same epoch bump the old panel-side Seek() produced, so anything that
   // retriggers on a transport jump still does on every lap.
   mResetEpoch.fetch_add(1, std::memory_order_relaxed);
}

void Transport::NotifyAudioEngineStarted(double sampleRate)
{
   DropFrameClock();
   mAudioSecondsOffset.store(mSeconds, std::memory_order_relaxed);
   mAudioBeatsOffset.store(mBeats, std::memory_order_relaxed);
   mAudioSampleCounter.store(0, std::memory_order_relaxed);
   mAudioSampleRate.store(sampleRate, std::memory_order_relaxed); // publish last
}

void Transport::NotifyAudioEngineStopped()
{
   DropFrameClock();
   mSeconds = Seconds(); // read the still-live audio-driven value...
   mBeats = Beats();
   mAudioSampleRate.store(0.0, std::memory_order_relaxed); // ...then switch back to fallback
   mPendingSeekSeconds.store(-1.0, std::memory_order_relaxed);
}

void Transport::AdvanceAudioClock(int numFrames)
{
   // A launch waiting on the grid is applied first, so its seek or pause is
   // what this very block renders: Beats() here is still this block's start.
   if (mPendingLaunch.load(std::memory_order_acquire) != kLaunchNothing && mPlaying.load(std::memory_order_relaxed))
   {
      const double sr = mAudioSampleRate.load(std::memory_order_relaxed);
      if (sr > 0.0)
      {
         const double b0 = Beats();
         ResolveLaunch(b0, b0 + (double)numFrames * (Tempo() / (60.0 * sr)));
      }
   }
   const double pending = mPendingSeekSeconds.exchange(-1.0, std::memory_order_relaxed);
   if (pending >= 0.0)
   {
      mAudioSecondsOffset.store(pending, std::memory_order_relaxed);
      mAudioBeatsOffset.store(pending * (mBpm.load(std::memory_order_relaxed) / 60.0), std::memory_order_relaxed);
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
   }
   ApplyPendingTempo(); // after the seek consumed its offsets, before we advance
   mBlockStartBeats.store(Beats(), std::memory_order_relaxed); // before the advance AND before any wrap
   if (mPlaying.load(std::memory_order_relaxed))
      mAudioSampleCounter.fetch_add((uint64_t)numFrames, std::memory_order_relaxed);
   WrapLoopIfNeeded();
   AdvanceEngineClock(numFrames, mAudioSampleRate.load(std::memory_order_relaxed));
}

Transport::LaunchAction Transport::RequestTogglePlay()
{
   // A second press while one waits cancels it, whatever the grid is.
   if (mPendingLaunch.load(std::memory_order_acquire) != kLaunchNothing)
   {
      CancelLaunch();
      return kLaunchNothing;
   }
   const int q = mLaunchQuantize.load(std::memory_order_relaxed);
   if (q <= 0 || !mPlaying.load(std::memory_order_relaxed) || mOfflineActive.load(std::memory_order_relaxed))
   {
      TogglePlay();
      return kLaunchNothing;
   }
   mPendingLaunchBeat.store(MusicTime::NextLaunchBeat(Beats(), q), std::memory_order_relaxed);
   mPendingLaunch.store(kLaunchPause, std::memory_order_release);
   return kLaunchPause;
}

Transport::LaunchAction Transport::RequestRewind()
{
   const int q = mLaunchQuantize.load(std::memory_order_relaxed);
   // Not playing: nothing is running off the bar, so the playhead goes home
   // now. This also drops a restart that was waiting (it would restart a
   // transport the user has since stopped).
   if (q <= 0 || !mPlaying.load(std::memory_order_relaxed) || mOfflineActive.load(std::memory_order_relaxed))
   {
      CancelLaunch();
      Rewind();
      return kLaunchNothing;
   }
   // A pause that was waiting is replaced: the later press wins.
   mPendingLaunchBeat.store(MusicTime::NextLaunchBeat(Beats(), q), std::memory_order_relaxed);
   mPendingLaunch.store(kLaunchRestart, std::memory_order_release);
   return kLaunchRestart;
}

void Transport::ResolveLaunch(double b0, double b1)
{
   int action = mPendingLaunch.load(std::memory_order_acquire);
   if (action == kLaunchNothing)
      return;
   // Applied by the block that contains the grid line (or the first one after
   // it, if the line has already gone by): up to one block early, never late.
   // A target exactly on this block's end belongs to the next block.
   const double target = mPendingLaunchBeat.load(std::memory_order_relaxed);
   if (target >= b1 - 1e-9 && target > b0 + 1e-9)
      return;
   if (!mPendingLaunch.compare_exchange_strong(action, (int)kLaunchNothing, std::memory_order_acq_rel))
      return; // cancelled or replaced between the load and here
   if (action == kLaunchPause)
      SetPlaying(false);
   else if (action == kLaunchRestart)
      Rewind();
}

void Transport::Rewind()
{
   Seek(0.0);
}

void Transport::Seek(double seconds)
{
   if (seconds < 0.0)
      seconds = 0.0;
   const double bpm = CommitTempoForSeek();
   SeekInternal(seconds, seconds * (bpm / 60.0));
}

void Transport::SeekBeats(double beats)
{
   if (beats < 0.0)
      beats = 0.0;
   const double bpm = CommitTempoForSeek();
   SeekInternal(beats * (60.0 / bpm), beats);
}

void Transport::SeekInternal(double seconds, double beats)
{
   DropFrameClock();
   mResetEpoch.fetch_add(1, std::memory_order_relaxed);
   mSeconds = seconds;
   mBeats = beats;
   mOfflineVideoSeconds.store(seconds, std::memory_order_relaxed);
   if (mOfflineActive.load(std::memory_order_relaxed))
   {
      mPendingSeekSeconds.store(-1.0, std::memory_order_relaxed);
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
      mAudioSecondsOffset.store(seconds, std::memory_order_relaxed);
      mAudioBeatsOffset.store(mBeats, std::memory_order_relaxed);
   }
   else if (mAudioSampleRate.load(std::memory_order_relaxed) > 0.0)
   {
      mPendingSeekSeconds.store(seconds, std::memory_order_relaxed);
   }
   else
   {
      mPendingSeekSeconds.store(-1.0, std::memory_order_relaxed);
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
      mAudioSecondsOffset.store(seconds, std::memory_order_relaxed);
      mAudioBeatsOffset.store(mBeats, std::memory_order_relaxed);
   }
}

void Transport::SetOfflineMode(bool active, double sampleRate)
{
   DropFrameClock();
   CancelLaunch(); // an offline take starts where it is told to, not on a live grid
   if (active)
   {
      mPendingSeekSeconds.store(-1.0, std::memory_order_relaxed);
      mAudioSecondsOffset.store(mSeconds, std::memory_order_relaxed);
      mAudioBeatsOffset.store(mBeats, std::memory_order_relaxed);
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
      mAudioSampleRate.store(sampleRate, std::memory_order_relaxed);
      mOfflineVideoSeconds.store(mSeconds, std::memory_order_relaxed);
      mOfflineInAudioBlock.store(false, std::memory_order_relaxed);
      // An offline render walks the range once; leaving the loop live would
      // wrap it back and render the loop body until the frame budget ran out.
      mLoopSuspended.store(true, std::memory_order_relaxed);
      mOfflineActive.store(true, std::memory_order_relaxed);
   }
   else
   {
      mPendingSeekSeconds.store(-1.0, std::memory_order_relaxed);
      mOfflineActive.store(false, std::memory_order_relaxed);
      mOfflineInAudioBlock.store(false, std::memory_order_relaxed);
      mLoopSuspended.store(false, std::memory_order_relaxed); // the user's loop comes back
   }
}

void Transport::SetOfflineVideoTime(double seconds)
{
   mOfflineVideoSeconds.store(seconds, std::memory_order_relaxed);
}

void Transport::BeginOfflineAudioBlock(int numFrames)
{
   const double pending = mPendingSeekSeconds.exchange(-1.0, std::memory_order_relaxed);
   if (pending >= 0.0)
   {
      mAudioSecondsOffset.store(pending, std::memory_order_relaxed);
      mAudioBeatsOffset.store(pending * (mBpm.load(std::memory_order_relaxed) / 60.0), std::memory_order_relaxed);
      mAudioSampleCounter.store(0, std::memory_order_relaxed);
   }
   ApplyPendingTempo();
   // Enter the block BEFORE reading Beats(): outside an offline audio block
   // Seconds() deliberately reports the renderer's video time, not the audio
   // clock, so capturing the block start any earlier would read the frame's
   // timestamp instead of the sample position. See AdvanceAudioClock for why
   // the capture has to happen before the counter advances.
   mOfflineInAudioBlock.store(true, std::memory_order_relaxed);
   mBlockStartBeats.store(Beats(), std::memory_order_relaxed);
   if (mPlaying.load(std::memory_order_relaxed))
      mAudioSampleCounter.fetch_add((uint64_t)numFrames, std::memory_order_relaxed);
   AdvanceEngineClock(numFrames, mAudioSampleRate.load(std::memory_order_relaxed));
}

void Transport::EndOfflineAudioBlock()
{
   mOfflineInAudioBlock.store(false, std::memory_order_relaxed);
}

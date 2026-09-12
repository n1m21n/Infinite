#include "GestureRecorder.h"

#include <algorithm>
#include <iterator>

GestureRecorder& GestureRecorder::Instance()
{
   static GestureRecorder instance;
   return instance;
}

void GestureRecorder::FinalizeSession(const Key& key, double nowSec)
{
   auto it = mSession.find(key);
   if (it == mSession.end())
      return;
   if (it->second.size() >= 2)
   {
      Playback pb;
      pb.samples = std::move(it->second);
      pb.startTime = nowSec;
      pb.recordedMin = pb.recordedMax = pb.samples.front().value;
      for (const Sample& s : pb.samples)
      {
         pb.recordedMin = std::min(pb.recordedMin, s.value);
         pb.recordedMax = std::max(pb.recordedMax, s.value);
      }
      mPlayback[key] = std::move(pb);
   }
   mSession.erase(it);
}

void GestureRecorder::BeginFrame(bool shiftHeld, double nowSec)
{
   const bool wasHeld = mShiftHeld;
   mShiftHeld = shiftHeld;
   if (wasHeld && !mShiftHeld)
   {
      // Session just ended: every param with a real trace (more than one
      // sample - a single touch has no movement to replay) starts looping.
      // Snapshot the keys first - FinalizeSession erases from mSession as it
      // goes, so iterating mSession directly while erasing from it is unsafe.
      std::vector<Key> keys;
      keys.reserve(mSession.size());
      for (const auto& entry : mSession)
         keys.push_back(entry.first);
      for (const Key& key : keys)
         FinalizeSession(key, nowSec);
   }
}

void GestureRecorder::AdvanceClock(double deltaSeconds, bool playing)
{
   if (playing)
      mClockSeconds += deltaSeconds;
}

void GestureRecorder::ArmParam(int nodeIndex, int paramIndex)
{
   const Key key(nodeIndex, paramIndex);
   mArmedParams.insert(key);
   mSession.erase(key);
   mPlayback.erase(key);
}

void GestureRecorder::CancelArm(int nodeIndex, int paramIndex)
{
   const Key key(nodeIndex, paramIndex);
   mArmedParams.erase(key);
   mSession.erase(key);
}

void GestureRecorder::NotifyMovement(int nodeIndex, int paramIndex, float value, double nowSec, bool isNewGrab)
{
   const Key key(nodeIndex, paramIndex);
   if (!mShiftHeld && mArmedParams.find(key) == mArmedParams.end())
      return; // shouldn't happen (callers already gate on Shift/armed) - defensive only
   mSession[key].push_back({ value, nowSec, isNewGrab });
   mPlayback.erase(key); // re-recording replaces whatever was looping before
}

void GestureRecorder::MaybeFinishArmedRecording(int nodeIndex, int paramIndex, double nowSec)
{
   const Key key(nodeIndex, paramIndex);
   if (mArmedParams.find(key) == mArmedParams.end())
      return;
   mArmedParams.erase(key);
   FinalizeSession(key, nowSec);
}

bool GestureRecorder::IsRecording(int nodeIndex, int paramIndex) const
{
   const Key key(nodeIndex, paramIndex);
   if (mArmedParams.find(key) != mArmedParams.end())
      return true;
   if (mShiftHeld && mSession.find(key) != mSession.end())
      return true;
   return mPlayback.find(key) != mPlayback.end();
}

void GestureRecorder::StopPlayback(int nodeIndex, int paramIndex)
{
   mPlayback.erase(Key(nodeIndex, paramIndex));
}

bool GestureRecorder::GetPlaybackValue(int nodeIndex, int paramIndex, double nowSec, float& outValue) const
{
   auto it = mPlayback.find(Key(nodeIndex, paramIndex));
   if (it == mPlayback.end())
      return false;
   const Playback& pb = it->second;
   const std::vector<Sample>& s = pb.samples;
   const double duration = s.back().timeSec - s.front().timeSec;
   double raw;
   if (duration <= 0.0)
   {
      raw = s.back().value;
   }
   else
   {
      const double speed = (double)std::max(0.05f, pb.speed);
      const double elapsed = (nowSec - pb.startTime) * speed;
      double t = std::fmod(elapsed, duration);
      if (t < 0.0)
         t += duration;
      const double target = s.front().timeSec + t;
      raw = s.back().value;
      // Linear scan: recordings are a handful of samples per second of
      // shift-held dragging, never large enough to warrant a binary search.
      for (size_t i = 1; i < s.size(); ++i)
      {
         if (target <= s[i].timeSec)
         {
            if (s[i].startsNewGrab)
            {
               // Checkpoint pattern: this sample began a fresh grab, separate
               // from the drag that produced s[i-1] - so the gap between them
               // is a released hand, not a movement to replay smoothly. Hold
               // the previous checkpoint's value right up to this one, then
               // jump, instead of sliding between the two.
               raw = s[i - 1].value;
            }
            else
            {
               const double span = s[i].timeSec - s[i - 1].timeSec;
               const double frac = (span > 0.0) ? (target - s[i - 1].timeSec) / span : 0.0;
               raw = s[i - 1].value + frac * (s[i].value - s[i - 1].value);
            }
            break;
         }
      }
   }
   if (pb.hasRangeOverride)
   {
      if (pb.recordedMax > pb.recordedMin)
      {
         const double norm = (raw - pb.recordedMin) / (pb.recordedMax - pb.recordedMin);
         outValue = pb.rangeLo + (float)norm * (pb.rangeHi - pb.rangeLo);
      }
      else
      {
         outValue = pb.rangeLo;
      }
   }
   else
   {
      outValue = (float)raw;
   }
   return true;
}

void GestureRecorder::SetPlaybackSpeed(int nodeIndex, int paramIndex, float speed)
{
   auto it = mPlayback.find(Key(nodeIndex, paramIndex));
   if (it != mPlayback.end())
      it->second.speed = std::max(0.05f, speed);
}

float GestureRecorder::PlaybackSpeedFor(int nodeIndex, int paramIndex) const
{
   auto it = mPlayback.find(Key(nodeIndex, paramIndex));
   return it != mPlayback.end() ? it->second.speed : 1.0f;
}

void GestureRecorder::SetPlaybackRange(int nodeIndex, int paramIndex, float lo, float hi)
{
   auto it = mPlayback.find(Key(nodeIndex, paramIndex));
   if (it != mPlayback.end())
   {
      it->second.hasRangeOverride = true;
      it->second.rangeLo = lo;
      it->second.rangeHi = hi;
   }
}

void GestureRecorder::ClearPlaybackRange(int nodeIndex, int paramIndex)
{
   auto it = mPlayback.find(Key(nodeIndex, paramIndex));
   if (it != mPlayback.end())
      it->second.hasRangeOverride = false;
}

bool GestureRecorder::PlaybackRangeFor(int nodeIndex, int paramIndex, float& lo, float& hi) const
{
   auto it = mPlayback.find(Key(nodeIndex, paramIndex));
   if (it == mPlayback.end() || !it->second.hasRangeOverride)
      return false;
   lo = it->second.rangeLo;
   hi = it->second.rangeHi;
   return true;
}

void GestureRecorder::ClearForNode(int nodeIndex)
{
   for (auto it = mPlayback.begin(); it != mPlayback.end();)
      it = (it->first.first == nodeIndex) ? mPlayback.erase(it) : std::next(it);
   for (auto it = mSession.begin(); it != mSession.end();)
      it = (it->first.first == nodeIndex) ? mSession.erase(it) : std::next(it);
   for (auto it = mArmedParams.begin(); it != mArmedParams.end();)
      it = (it->first == nodeIndex) ? mArmedParams.erase(it) : std::next(it);
}

void GestureRecorder::Clear()
{
   mPlayback.clear();
   mSession.clear();
   mArmedParams.clear();
}

void GestureRecorder::Restore(PlaybackMap playbacks, double nowSec)
{
   mPlayback = std::move(playbacks);
   for (auto& entry : mPlayback)
      entry.second.startTime = nowSec;
   mSession.clear();
   mArmedParams.clear();
}

void GestureRecorder::SetPlayback(int nodeIndex, int paramIndex, Playback playback)
{
   if (playback.samples.size() < 2)
      return; // matches FinalizeSession: a trace with no movement is not a loop
   mPlayback[Key(nodeIndex, paramIndex)] = std::move(playback);
}

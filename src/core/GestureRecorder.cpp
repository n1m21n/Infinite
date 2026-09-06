#include "GestureRecorder.h"

GestureRecorder& GestureRecorder::Instance()
{
   static GestureRecorder instance;
   return instance;
}

void GestureRecorder::BeginFrame(bool shiftHeld, double nowSec)
{
   const bool wasHeld = mShiftHeld;
   mShiftHeld = shiftHeld;
   if (wasHeld && !mShiftHeld)
   {
      // Session just ended: every param with a real trace (more than one
      // sample - a single touch has no movement to replay) starts looping.
      for (auto& [key, samples] : mSession)
      {
         if (samples.size() < 2)
            continue;
         Playback pb;
         pb.samples = std::move(samples);
         pb.startTime = nowSec;
         mPlayback[key] = std::move(pb);
      }
      mSession.clear();
   }
}

void GestureRecorder::NotifyMovement(int nodeIndex, int paramIndex, float value, double nowSec)
{
   if (!mShiftHeld)
      return; // shouldn't happen (callers already gate on Shift) - defensive only
   const Key key(nodeIndex, paramIndex);
   mSession[key].push_back({ value, nowSec });
   mPlayback.erase(key); // re-recording replaces whatever was looping before
}

bool GestureRecorder::IsRecording(int nodeIndex, int paramIndex) const
{
   const Key key(nodeIndex, paramIndex);
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
   const std::vector<Sample>& s = it->second.samples;
   const double duration = s.back().timeSec - s.front().timeSec;
   if (duration <= 0.0)
   {
      outValue = s.back().value;
      return true;
   }
   const double elapsed = nowSec - it->second.startTime;
   double t = std::fmod(elapsed, duration);
   if (t < 0.0)
      t += duration;
   const double target = s.front().timeSec + t;
   // Linear scan: recordings are a handful of samples per second of
   // shift-held dragging, never large enough to warrant a binary search.
   for (size_t i = 1; i < s.size(); ++i)
   {
      if (target <= s[i].timeSec)
      {
         const double span = s[i].timeSec - s[i - 1].timeSec;
         const double frac = (span > 0.0) ? (target - s[i - 1].timeSec) / span : 0.0;
         outValue = s[i - 1].value + (float)frac * (s[i].value - s[i - 1].value);
         return true;
      }
   }
   outValue = s.back().value;
   return true;
}

#include "MacroNodes.h"

#include <algorithm>
#include <cmath>

#include "Transport.h"

float MacroKnobNode::Value01()
{
   float v = std::min(1.0f, std::max(0.0f, value));
   if (curve != 1.0f)
      v = std::pow(v, std::max(0.05f, curve));
   return invert ? 1.0f - v : v;
}

void MacroXYNode::StartRecording()
{
   mPath.clear();
   mRecording = true;
   mPlaying = false;
   mStartBeat = Transport::Instance().Beats();
}

void MacroXYNode::StopRecording()
{
   mRecording = false;
}

void MacroXYNode::PlayPath()
{
   if (mPath.size() < 2)
      return;
   mPlaying = true;
   mRecording = false;
   mStartBeat = Transport::Instance().Beats();
}

void MacroXYNode::StopPath()
{
   mPlaying = false;
}

void MacroXYNode::ClearPath()
{
   mPath.clear();
   mPlaying = false;
   mRecording = false;
}

void MacroXYNode::UpdatePath()
{
   const double beat = Transport::Instance().Beats();

   if (mRecording)
   {
      PadPoint point;
      point.x = padX;
      point.y = padY;
      point.beat = beat - mStartBeat;
      if (mPath.empty() ||
          std::fabs(mPath.back().x - point.x) > 0.002f ||
          std::fabs(mPath.back().y - point.y) > 0.002f ||
          point.beat - mPath.back().beat > 0.25)
      {
         mPath.push_back(point);
      }
      return;
   }

   if (!mPlaying || mPath.size() < 2)
      return;

   const double duration = mPath.back().beat;
   if (duration <= 0.0)
      return;

   double t = (beat - mStartBeat) * std::max(0.01f, speed);
   if (t > duration)
   {
      if (!loopPath)
      {
         mPlaying = false;
         return;
      }
      t = std::fmod(t, duration);
   }

   for (size_t i = 1; i < mPath.size(); i++)
   {
      if (mPath[i].beat >= t)
      {
         const PadPoint& a = mPath[i - 1];
         const PadPoint& b = mPath[i];
         const double span = std::max(1e-6, b.beat - a.beat);
         const float f = (float)((t - a.beat) / span);
         padX = a.x + (b.x - a.x) * f;
         padY = a.y + (b.y - a.y) * f;
         return;
      }
   }
}

void MacroXYNode::CookIfNeeded(int frameId)
{
   // Path playback advances once per frame, not once per Value01() call: the
   // pad may be read by many destinations and must give all of them the same
   // value within a frame.
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   UpdatePath();
}

float MacroXYNode::Value01()
{
   return std::min(1.0f, std::max(0.0f, padX));
}

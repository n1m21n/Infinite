#include "GestureRecorder.h"

GestureRecorder& GestureRecorder::Instance()
{
   static GestureRecorder instance;
   return instance;
}

void GestureRecorder::BeginFrame(bool shiftHeld)
{
   mShiftHeld = shiftHeld;
   if (!mShiftHeld)
      mSession.clear();
}

void GestureRecorder::NotifyMovement(int nodeIndex, int paramIndex, float value, double nowSec)
{
   if (!mShiftHeld)
      return; // shouldn't happen (callers already gate on Shift) - defensive only
   mSession[Key(nodeIndex, paramIndex)].push_back({ value, nowSec });
}

bool GestureRecorder::IsRecording(int nodeIndex, int paramIndex) const
{
   return mShiftHeld && mSession.find(Key(nodeIndex, paramIndex)) != mSession.end();
}

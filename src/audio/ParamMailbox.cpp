#include "ParamMailbox.h"

// <cassert>'s assert() is itself a no-op when NDEBUG is defined, so this is
// always included; nothing here needs its own #ifndef NDEBUG.
#include <cassert>

void ParamMailbox::PrepareToPlay(double sampleRate)
{
   mSampleRate = sampleRate;
   for (auto& smoother : mSmoothers)
      smoother.SetTimeConstant(0.005f, sampleRate);
}

void ParamMailbox::Push(int paramId, float value)
{
   // A node that outgrows kMaxParams silently drops the param in release -
   // fail loudly in development instead, since a dropped param otherwise
   // looks identical to "the knob just doesn't do anything yet".
   assert(paramId >= 0 && paramId < kMaxParams);
   if (paramId < 0 || paramId >= kMaxParams)
      return;
   mTarget[paramId].store(value, std::memory_order_release);
}

float ParamMailbox::SmoothedValue(int paramId)
{
   if (paramId < 0 || paramId >= kMaxParams)
      return 0.0f;
   const float target = mTarget[paramId].load(std::memory_order_acquire);
   return mSmoothers[paramId].Process(target);
}

void ParamMailbox::SetImmediate(int paramId, float value)
{
   assert(paramId >= 0 && paramId < kMaxParams);
   if (paramId < 0 || paramId >= kMaxParams)
      return;
   mTarget[paramId].store(value, std::memory_order_release);
   mSmoothers[paramId].SetImmediate(value);
}

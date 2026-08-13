#include "ParamMailbox.h"

void ParamMailbox::PrepareToPlay(double sampleRate)
{
   mSampleRate = sampleRate;
   for (auto& smoother : mSmoothers)
      smoother.SetTimeConstant(0.005f, sampleRate);
}

void ParamMailbox::Push(int paramId, float value)
{
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
   if (paramId < 0 || paramId >= kMaxParams)
      return;
   mTarget[paramId].store(value, std::memory_order_release);
   mSmoothers[paramId].SetImmediate(value);
}

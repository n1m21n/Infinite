#include "ParamMailbox.h"

#include "core/Transport.h"

#include <algorithm>
// <cassert>'s assert() is itself a no-op when NDEBUG is defined, so this is
// always included; nothing here needs its own #ifndef NDEBUG.
#include <cassert>
#include <cmath>

namespace
{
   std::atomic<double> sFrameStamp { -1.0 };
   std::atomic<double> sFrameDelay { 0.0 };
   constexpr double kFallbackRampSeconds = 0.005;
   constexpr double kMaxRampSeconds = 0.25;
   constexpr int kMinRampSamples = 16;
}

void ParamMailbox::PublishFrame(double stampSeconds, double delaySeconds)
{
   sFrameDelay.store(delaySeconds, std::memory_order_relaxed);
   sFrameStamp.store(stampSeconds, std::memory_order_release);
}

void ParamMailbox::PrepareToPlay(double sampleRate)
{
   mSampleRate = sampleRate;
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
   mPushed.store(true, std::memory_order_release);
}

void ParamMailbox::StartRamp(Ramp& r, float value, double startTime, double endTime)
{
   const int samples = (int)std::clamp(std::lround((endTime - startTime) * mSampleRate),
                                       (long)kMinRampSamples, std::lround(kMaxRampSeconds * mSampleRate));
   r.target = value;
   r.remaining = samples;
   r.inc = (value - r.cur) / (float)samples;
   r.endTime = startTime + (double)samples / mSampleRate;
}

void ParamMailbox::OnNewTarget(Ramp& r, float value)
{
   if (!(mSampleRate > 0.0))
   {
      r.cur = r.target = value;
      r.remaining = 0;
      r.hasPending = false;
      return;
   }
   const double now = Transport::Instance().EngineBlockStartSeconds();
   const double stamp = sFrameStamp.load(std::memory_order_acquire);
   if (stamp < 0.0)
   {
      r.hasPending = false;
      StartRamp(r, value, now, now + kFallbackRampSeconds);
      return;
   }
   const double end = stamp + sFrameDelay.load(std::memory_order_relaxed);
   // Still on the way to the previous frame's value: queue behind it, so
   // that value is actually reached before turning toward this one.
   if (r.remaining > 0 && end > r.endTime + 0.5 / mSampleRate)
   {
      r.pendingValue = value;
      r.pendingEnd = end;
      r.hasPending = true;
      return;
   }
   r.hasPending = false;
   StartRamp(r, value, now, end);
}

float ParamMailbox::SmoothedValue(int paramId)
{
   if (paramId < 0 || paramId >= kMaxParams)
      return 0.0f;
   Ramp& r = mRamps[paramId];
   const float target = mTarget[paramId].load(std::memory_order_acquire);
   if (!r.primed && mPushed.load(std::memory_order_acquire))
   {
      r.cur = r.target = r.seen = target;
      r.remaining = 0;
      r.hasPending = false;
      r.primed = true;
   }
   if (target != r.seen)
   {
      r.seen = target;
      OnNewTarget(r, target);
   }
   if (r.remaining > 0)
   {
      r.cur += r.inc;
      if (--r.remaining == 0)
      {
         r.cur = r.target;
         if (r.hasPending)
         {
            r.hasPending = false;
            StartRamp(r, r.pendingValue, r.endTime, r.pendingEnd);
         }
      }
   }
   return r.cur;
}

float ParamMailbox::BlockValue(int paramId, int numFrames)
{
   float v = SmoothedValue(paramId);
   for (int i = 1; i < numFrames; i++)
      v = SmoothedValue(paramId);
   return v;
}

void ParamMailbox::SetImmediate(int paramId, float value)
{
   assert(paramId >= 0 && paramId < kMaxParams);
   if (paramId < 0 || paramId >= kMaxParams)
      return;
   mTarget[paramId].store(value, std::memory_order_release);
   Ramp& r = mRamps[paramId];
   r.cur = r.target = r.seen = value;
   r.primed = true;
   r.remaining = 0;
   r.hasPending = false;
}

void ParamMailbox::SnapToTarget(int paramId)
{
   assert(paramId >= 0 && paramId < kMaxParams);
   if (paramId < 0 || paramId >= kMaxParams)
      return;
   SetImmediate(paramId, mTarget[paramId].load(std::memory_order_acquire));
}

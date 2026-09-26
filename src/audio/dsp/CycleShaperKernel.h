#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/dsp/AnalogPrimitives.h"

// Cycle Shaper: replaces each wavecycle of the input with an idealized
// geometric waveform (Sine, Square, Triangle) of the same period and peak.
class AudioEffectNode;

class CycleShaperKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 2;
   static constexpr int kMaxCycleSamples = 2048;
   static constexpr int kMaxTail = 32;

   struct ChannelState
   {
      float inBuffer[kMaxCycleSamples] = {};
      float outBuffer[kMaxCycleSamples] = {};
      int inPos = 0;
      int outPos = 0;
      int outLen = 0;
      float prevSample = 0.0f;
      float cyclePeak = 0.0f;
      float lastRenderedTail[kMaxTail] = {};
      int lastTailLen = 0;
      float lastCycleLen = 100.0f;
      float lastCyclePeak = 0.5f;

      void Reset()
      {
         inPos = 0;
         outPos = 0;
         outLen = 0;
         prevSample = 0.0f;
         cyclePeak = 0.0f;
         lastTailLen = 0;
         lastCycleLen = 100.0f;
         lastCyclePeak = 0.5f;
         std::memset(inBuffer, 0, sizeof(inBuffer));
         std::memset(outBuffer, 0, sizeof(outBuffer));
         std::memset(lastRenderedTail, 0, sizeof(lastRenderedTail));
      }
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      Reset();
   }

   void Reset() override
   {
      for (auto& ch : mChannels)
         ch.Reset();
   }

   // Discrete / per-cycle threshold parameters legitimately need no per-sample ramp:
   // threshold is a per-cycle gate, waveform and smooth are discrete integer counts.
   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

   float LastCycleLength(int ch) const { return mChannels[ch % kMaxChannels].lastCycleLen; }
   float LastCyclePeak(int ch) const { return mChannels[ch % kMaxChannels].lastCyclePeak; }

private:
   double mSampleRate = 44100.0;
   std::atomic<int> mWaveform { 0 };
   std::atomic<float> mThresholdLinear { 0.0158489f }; // -36 dBFS
   std::atomic<int> mSmoothSamples { 8 };
   std::atomic<int> mAnalog { 0 };

   ChannelState mChannels[kMaxChannels];
};

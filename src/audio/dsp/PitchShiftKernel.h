#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine's Hermite-interpolated fractional read
#include "AnalogPrimitives.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Pitch Shifter's kernel - two-tap crossfaded delay-line pitch shifter with analog mode.
class AudioEffectNode;

class PitchShiftKernel : public IEffectKernel
{
public:
   static constexpr float kMaxGrainMs = 250.0f;

   enum ParamSlot
   {
      kSemitones = 0,
      kGrainMs,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const int maxSamples = (int)std::ceil(kMaxGrainMs * 0.001 * sampleRate) * 2 + 16;
      mLineL.Prepare(maxSamples);
      mLineR.Prepare(maxSamples);
      for (int i = 0; i < 2; i++)
      {
         mFilterL[i].SetSampleRate(sampleRate);
         mFilterL[i].SetCutoff(9000.0f, 0.707f);
         mFilterR[i].SetSampleRate(sampleRate);
         mFilterR[i].SetCutoff(9000.0f, 0.707f);
      }
      Reset();
   }

   void Reset() override
   {
      mLineL.Reset();
      mLineR.Reset();
      mTap0 = 0.0f;
      mTap1 = 0.0f;
      mTapsInitialized = false;
      mJitterPhase = 0.0f;
      for (int i = 0; i < 2; i++)
      {
         mFilterL[i].Reset();
         mFilterR[i].Reset();
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   std::atomic<int> mAnalog { 0 };

   DelayLine mLineL, mLineR;
   float mTap0 = 0.0f, mTap1 = 0.0f;
   bool mTapsInitialized = false;

   // Analog mode components
   float mJitterPhase = 0.0f;
   DspMath::TptSvf mFilterL[2];
   DspMath::TptSvf mFilterR[2];
};

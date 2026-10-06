#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine - Hermite-interpolated fractional read, already proven by Delay
#include "AnalogPrimitives.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Chorus's kernel - modulated delay-line voices per channel with digital and analog modes.
class AudioEffectNode;

class ChorusKernel : public IEffectKernel
{
public:
   static constexpr float kMaxDelayMs = 60.0f;
   static constexpr int kMaxTaps = 3;

   enum ParamSlot
   {
      kDelayMs = 0,
      kSpread,
      kDepthMs,
      kRateHz,
      kFeedback,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const int maxSamples = (int)std::ceil(kMaxDelayMs * 0.001 * sampleRate) + 8;
      mLineL.Prepare(maxSamples);
      mLineR.Prepare(maxSamples);
      for (int i = 0; i < 2; i++)
      {
         mFilterL[i].SetSampleRate(sampleRate);
         mFilterL[i].SetCutoff(7500.0f, 0.707f);
         mFilterR[i].SetSampleRate(sampleRate);
         mFilterR[i].SetCutoff(7500.0f, 0.707f);
      }
      Reset();
   }

   void Reset() override
   {
      mLineL.Reset();
      mLineR.Reset();
      mPhase = 0.0;
      mDriftLfo.Reset();
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

   std::atomic<int> mTaps { 2 };
   std::atomic<int> mSync { 0 };
   std::atomic<int> mRateDiv { MusicTime::kQuarter };
   std::atomic<int> mAnalog { 0 };

   DelayLine mLineL, mLineR;
   double mPhase = 0.0;

   // Analog mode components
   AnalogDsp::DriftLfo mDriftLfo;
   DspMath::TptSvf mFilterL[2];
   DspMath::TptSvf mFilterR[2];
};

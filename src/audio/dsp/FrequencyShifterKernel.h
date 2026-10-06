#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "AnalogPrimitives.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Frequency Shifter's kernel - a true single-sideband (SSB) frequency shifter with analog mode.
class AudioEffectNode;

struct Allpass2ndOrder
{
   float x1 = 0.0f, x2 = 0.0f;
   float y1 = 0.0f, y2 = 0.0f;

   void Reset()
   {
      x1 = x2 = 0.0f;
      y1 = y2 = 0.0f;
   }

   // Recurrence relation for 2nd-order allpass: y[n] = a^2 * (x[n] + y[n-2]) - x[n-2]
   inline float Process(float x, float aSq)
   {
      const float y = aSq * (x + y2) - x2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
   }
};

class FrequencyShifterKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;

   enum ParamSlot
   {
      kShiftHz = 0,
      kFeedback,
      kSpreadHz,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      for (int ch = 0; ch < kMaxChannels; ch++)
         mFbLp[ch].SetCutoff(6000.0f, sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         for (int s = 0; s < 4; s++)
         {
            mStagesA[ch][s].Reset();
            mStagesB[ch][s].Reset();
         }
         mDelayB[ch] = 0.0f;
         mPhase[ch] = 0.0;
         mLastOut[ch] = 0.0f;
         mFbLp[ch].Reset();
      }
      mEnvFollower = 0.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 1; }

private:
   static constexpr float kCoeffsASq[4] = {
      0.4794008343717222f,
      0.8762184935408794f,
      0.9765975895786483f,
      0.9974992560371661f
   };

   static constexpr float kCoeffsBSq[4] = {
      0.1617584983637118f,
      0.7330289323145458f,
      0.9453497003058869f,
      0.9905991568222474f
   };

   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   std::atomic<int> mAnalog { 0 };

   Allpass2ndOrder mStagesA[kMaxChannels][4];
   Allpass2ndOrder mStagesB[kMaxChannels][4];
   float mDelayB[kMaxChannels] = { 0.0f };
   double mPhase[kMaxChannels] = { 0.0 };
   float mLastOut[kMaxChannels] = { 0.0f };

   // Analog mode components
   float mEnvFollower = 0.0f;
   AnalogDsp::OnePoleLP mFbLp[kMaxChannels];
};

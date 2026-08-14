#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Phaser's kernel - a cascade of first-order allpass stages sharing one
// LFO-modulated coefficient, per channel. Primary reference: Zolzer, "DAFX:
// Digital Audio Effects", ch. 2.5 (the standard first-order allpass phaser:
// a(n) = (tan(pi*fc/fs) - 1) / (tan(pi*fc/fs) + 1), y[n] = a*x[n] + x[n-1] -
// a*y[n-1]). `order` (stage count) matches the KHS Audio Phaser module's
// control set directly; `spread` offsets the right channel's LFO phase for
// stereo width, the same construction Chorus/Flanger use.
class AudioEffectNode;

// A single first-order allpass section (Zolzer DAFX ch. 2.5). Coefficient is
// pushed in externally each sample so every stage in a cascade can share one
// tan() call instead of paying for it per stage.
struct AllpassStage
{
   float x1 = 0.0f, y1 = 0.0f;

   void Reset() { x1 = y1 = 0.0f; }

   float Process(float x, float a)
   {
      const float y = a * x + x1 - a * y1;
      x1 = x;
      y1 = y;
      return y;
   }
};

class PhaserKernel : public IEffectKernel
{
public:
   static constexpr int kMaxStages = 8;

   enum ParamSlot
   {
      kCutoffHz = 0,
      kRateHz,
      kDepth,
      kSpread,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      Reset();
   }

   void Reset() override
   {
      for (int i = 0; i < kMaxStages; i++)
      {
         mStagesL[i].Reset();
         mStagesR[i].Reset();
      }
      mPhase = 0.0;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   static float AllpassCoeff(float fc, float sampleRate)
   {
      const float clampedFc = std::clamp(fc, 20.0f, sampleRate * 0.45f);
      const float t = tanf((float)M_PI * clampedFc / sampleRate);
      return (t - 1.0f) / (t + 1.0f);
   }

   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mStageCount { 4 };
   std::atomic<int> mSync { 0 };
   std::atomic<int> mRateDiv { MusicTime::kQuarter };

   AllpassStage mStagesL[kMaxStages];
   AllpassStage mStagesR[kMaxStages];
   double mPhase = 0.0;
};

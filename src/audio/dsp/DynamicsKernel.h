#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "AnalogPrimitives.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Dynamics' kernel - cut down to KHS Audio Compressor's control surface
// (Threshold, Ratio, Attack, Release, Makeup, a Peak/RMS detector switch, a
// Sidechain on/off switch, an Analog mode toggle, a meter) per
// .claude/skills/new-audio-node/SKILL.md's minimalism rule.
//
// Primary reference: Giannoulis, Massberg & Reiss, "Digital Dynamic Range
// Compressor Design - A Tutorial and Analysis" (JAES 2012) - the soft-knee
// gain computer (their eq. 4) and the branching/decoupled peak-detector
// smoothing topology (their §III) both come directly from it. Knee width is
// a fixed internal constant (6 dB).
namespace DynamicsDsp
{
   static constexpr float kKneeDb = 6.0f;

   inline float GainComputerDb(float xG, float thresholdDb, float ratio)
   {
      const float R = std::max(1.0f, ratio);
      const float W = kKneeDb;
      const float delta = xG - thresholdDb;
      if (2.0f * delta < -W)
         return xG;
      if (2.0f * delta > W)
         return thresholdDb + delta / R;
      const float t = delta + W * 0.5f;
      return xG + (1.0f / R - 1.0f) * (t * t) / (2.0f * W);
   }
}

class DynamicsKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr float kRmsWindowMs = 20.0f;

   enum ParamSlot
   {
      kThreshold = 0,
      kRatio,
      kAttackMs,
      kReleaseMs,
      kMakeupDb,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      for (int ch = 0; ch < kMaxChannels; ch++)
         mWarmLp[ch].SetCutoff(20000.0f, sampleRate);
      Reset();
   }

   void Reset() override
   {
      mRmsMeanSq = 0.0f;
      mSmoothedReductionDb = 0.0f;
      mLastInputDb = -100.0f;
      mOptoMemoryDb = 0.0f;
      for (int ch = 0; ch < kMaxChannels; ch++)
         mWarmLp[ch].Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override
   {
      const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
      const bool useRms = mDetectorRms.load(std::memory_order_relaxed) != 0;
      const bool useSidechain = sidechain != nullptr && mSidechainExternal.load(std::memory_order_relaxed) != 0;
      const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

      const float rmsCoeff = expf(-1.0f / (kRmsWindowMs * 0.001f * (float)mSampleRate));

      for (int i = 0; i < out.numFrames; i++)
      {
         const float thresholdDb = mMailbox.SmoothedValue(kThreshold);
         const float ratio = mMailbox.SmoothedValue(kRatio);
         const float attackMs = std::max(0.05f, mMailbox.SmoothedValue(kAttackMs));
         const float releaseMs = std::max(1.0f, mMailbox.SmoothedValue(kReleaseMs));
         const float makeupLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kMakeupDb));
         const float attackCoeff = expf(-1.0f / (attackMs * 0.001f * (float)mSampleRate));
         const float releaseCoeff = expf(-1.0f / (releaseMs * 0.001f * (float)mSampleRate));

         float linkedLevel = 0.0f;
         for (int ch = 0; ch < numChannels; ch++)
         {
            const float s = useSidechain ? sidechain->channels[std::min(ch, sidechain->numChannels - 1)][i]
                                         : in.channels[ch][i];
            linkedLevel = std::max(linkedLevel, std::fabs(s));
         }

         float level = linkedLevel;
         if (useRms)
         {
            mRmsMeanSq = rmsCoeff * mRmsMeanSq + (1.0f - rmsCoeff) * (level * level);
            level = std::sqrt(std::max(0.0f, mRmsMeanSq));
         }

         const float xG = DspMath::LinearToDb(std::max(1.0e-9f, level));
         mLastInputDb = xG;
         const float yG = DynamicsDsp::GainComputerDb(xG, thresholdDb, ratio);
         const float targetReductionDb = std::max(0.0f, xG - yG);

         if (analog)
         {
            if (targetReductionDb > mSmoothedReductionDb)
            {
               mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * attackCoeff;
               mOptoMemoryDb += (mSmoothedReductionDb - mOptoMemoryDb) * 0.005f;
            }
            else
            {
               // Program-dependent dual-stage release: fast transient release (40ms) + slow opto release (400-1500ms)
               const float fastRelCoeff = expf(-1.0f / (0.040f * (float)mSampleRate));
               const float slowRelMs = std::clamp(releaseMs * 1.5f, 400.0f, 1500.0f);
               const float slowRelCoeff = expf(-1.0f / (slowRelMs * 0.001f * (float)mSampleRate));
               const float optoWeight = std::clamp(mOptoMemoryDb / 12.0f, 0.2f, 0.85f);
               const float effRelCoeff = fastRelCoeff * (1.0f - optoWeight) + slowRelCoeff * optoWeight;
               mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * effRelCoeff;
               mOptoMemoryDb += (targetReductionDb - mOptoMemoryDb) * 0.0002f;
            }
            mOptoMemoryDb = DspMath::FlushDenormal(mOptoMemoryDb);

            const float gr = DspMath::DbToLinear(-mSmoothedReductionDb) * makeupLin;
            const float warmFc = std::clamp(20000.0f - std::max(0.0f, mSmoothedReductionDb - 6.0f) * 800.0f, 8000.0f, 20000.0f);
            const float warmCoeff = AnalogDsp::OnePoleLP::CalcCoeff(warmFc, mSampleRate);

            for (int ch = 0; ch < numChannels; ch++)
            {
               float s = in.channels[ch][i];
               s = AnalogDsp::AsymTanh(s, 0.08f); // Input transformer warmth
               s = s * gr;
               s = mWarmLp[ch].Process(s, warmCoeff);
               s = AnalogDsp::AsymTanh(s, 0.05f); // Output transformer warmth
               out.channels[ch][i] = s + 1.0e-20f;
            }
         }
         else
         {
            // Digital exact path
            const float coeff = (targetReductionDb > mSmoothedReductionDb) ? attackCoeff : releaseCoeff;
            mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * coeff;

            const float gr = DspMath::DbToLinear(-mSmoothedReductionDb) * makeupLin;
            for (int ch = 0; ch < numChannels; ch++)
               out.channels[ch][i] = in.channels[ch][i] * gr + 1.0e-20f;
         }
      }

      const float payload[2] = { mLastInputDb, mSmoothedReductionDb };
      mGrMeter.Write(payload, 2);
   }

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mGrMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mDetectorRms { 0 };
   std::atomic<int> mSidechainExternal { 0 };
   std::atomic<int> mAnalog { 0 };

   float mRmsMeanSq = 0.0f;
   float mSmoothedReductionDb = 0.0f;
   float mLastInputDb = -100.0f;
   float mOptoMemoryDb = 0.0f;

   AnalogDsp::OnePoleLP mWarmLp[kMaxChannels];

   MeterRing mGrMeter;
};

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

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
// smoothing topology (their §III) both come directly from it. The knee
// width comes with the style (DynamicsDsp::KneeDb).
namespace DynamicsDsp
{
   // v0.6 2.21: four characters. Each style is a knee width, a detector
   // topology and a release shape; the knobs mean the same thing in all four.
   //   clean - feed-forward, branching peak smoothing (GMR §III), 6 dB knee
   //   bus   - VCA bus glue: the smooth decoupled detector (GMR §III), so the
   //           release starts only once the attack stage lets go; 10 dB knee
   //   opto  - a light cell: attack slows when little is being taken off,
   //           and the release has a fast and a slow stage that the recent
   //           reduction weights; 18 dB knee, ratio grows with level
   //   FET   - feedback: the detector hears the output, so the gain grabs
   //           in fewer samples than the attack knob alone; 2 dB knee
   enum Style
   {
      kClean = 0,
      kBus,
      kOpto,
      kFet,
      kNumStyles
   };

   inline const std::vector<std::string>& StyleList()
   {
      static const std::vector<std::string> kList = { "clean", "bus", "opto", "FET" };
      return kList;
   }

   inline float KneeDb(int style)
   {
      static const float kKnee[kNumStyles] = { 6.0f, 10.0f, 18.0f, 2.0f };
      return kKnee[std::clamp(style, 0, kNumStyles - 1)];
   }

   // dB over the threshold through a quadratic knee of width W (GMR eq. 4).
   inline float OverDb(float delta, float W)
   {
      if (2.0f * delta < -W)
         return 0.0f;
      if (2.0f * delta > W)
         return delta;
      const float t = delta + W * 0.5f;
      return (t * t) / (2.0f * W);
   }

   inline float GainComputerDb(float xG, float thresholdDb, float ratio, float kneeDb = 6.0f)
   {
      const float R = std::max(1.0f, ratio);
      return xG + (1.0f / R - 1.0f) * OverDb(xG - thresholdDb, kneeDb);
   }
}

class DynamicsKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr float kRmsWindowMs = 20.0f;
   // `scHpf`: a 12 dB/oct high-pass on the detector signal only (the audio
   // path is untouched), so bass energy stops driving the gain - the
   // "pumping on the kick" a full-band detector causes. 90 Hz, as on most
   // console and bus compressors' sidechain filter.
   static constexpr float kScHpfHz = 90.0f;

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
      {
         mWarmLp[ch].SetCutoff(20000.0f, sampleRate);
         mScHpf[ch].SetSampleRate(sampleRate);
         mScHpf[ch].SetCutoff(kScHpfHz, 0.707f);
      }
      Reset();
   }

   void Reset() override
   {
      mRmsMeanSq = 0.0f;
      mSmoothedReductionDb = 0.0f;
      mDecoupledDb = 0.0f;
      mLastGain = 1.0f;
      mLastInputDb = -100.0f;
      mOptoMemoryDb = 0.0f;
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         mWarmLp[ch].Reset();
         mScHpf[ch].Reset();
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override
   {
      const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
      const bool useRms = mDetectorRms.load(std::memory_order_relaxed) != 0;
      const bool useSidechain = sidechain != nullptr && mSidechainExternal.load(std::memory_order_relaxed) != 0;
      const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;
      const bool scHpf = mScHpfOn.load(std::memory_order_relaxed) != 0;
      const int style = std::clamp(mStyle.load(std::memory_order_relaxed), 0, DynamicsDsp::kNumStyles - 1);
      const float kneeDb = DynamicsDsp::KneeDb(style);
      const float sr = (float)mSampleRate;

      const float rmsCoeff = expf(-1.0f / (kRmsWindowMs * 0.001f * sr));
      const float fastRelCoeff = expf(-1.0f / (0.040f * sr));

      for (int i = 0; i < out.numFrames; i++)
      {
         const float thresholdDb = mMailbox.SmoothedValue(kThreshold);
         const float ratio = std::max(1.0f, mMailbox.SmoothedValue(kRatio));
         const float attackMs = std::max(0.05f, mMailbox.SmoothedValue(kAttackMs));
         const float releaseMs = std::max(1.0f, mMailbox.SmoothedValue(kReleaseMs));
         const float makeupLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kMakeupDb));
         float attackCoeff = expf(-1.0f / (attackMs * 0.001f * sr));
         const float releaseCoeff = expf(-1.0f / (releaseMs * 0.001f * sr));

         float linkedLevel = 0.0f;
         for (int ch = 0; ch < numChannels; ch++)
         {
            float s = useSidechain ? sidechain->channels[std::min(ch, sidechain->numChannels - 1)][i]
                                   : in.channels[ch][i];
            if (scHpf)
               s = mScHpf[ch].Process(s).high;
            linkedLevel = std::max(linkedLevel, std::fabs(s));
         }
         // FET: the detector hears the output (last sample's gain).
         if (style == DynamicsDsp::kFet && !useSidechain)
            linkedLevel *= mLastGain;

         float level = linkedLevel;
         if (useRms)
         {
            mRmsMeanSq = rmsCoeff * mRmsMeanSq + (1.0f - rmsCoeff) * (level * level);
            level = std::sqrt(std::max(0.0f, mRmsMeanSq));
         }

         const float xG = DspMath::LinearToDb(std::max(1.0e-9f, level));
         mLastInputDb = xG;
         const float overDb = DynamicsDsp::OverDb(xG - thresholdDb, kneeDb);
         float targetReductionDb;
         if (style == DynamicsDsp::kFet && !useSidechain)
         {
            // Over the output, (R - 1) per dB gives the same R:1 as feed-
            // forward. The loop gain caps how fast it may close, or it rings.
            targetReductionDb = overDb * (ratio - 1.0f);
            attackCoeff = std::max(attackCoeff, 1.0f - 1.0f / ratio);
         }
         else
            targetReductionDb = overDb * (1.0f - 1.0f / ratio);

         switch (style)
         {
         case DynamicsDsp::kBus:
         {
            // Smooth decoupled: a release-only peak hold, then the attack.
            mDecoupledDb = std::max(targetReductionDb, releaseCoeff * mDecoupledDb + (1.0f - releaseCoeff) * targetReductionDb);
            mSmoothedReductionDb = mDecoupledDb + (mSmoothedReductionDb - mDecoupledDb) * attackCoeff;
            break;
         }
         case DynamicsDsp::kOpto:
         {
            if (targetReductionDb > mSmoothedReductionDb)
            {
               // A lightly lit cell answers slower.
               const float slow = 1.0f + 1.5f * expf(-mSmoothedReductionDb / 6.0f);
               const float optoAttack = expf(-1.0f / (attackMs * slow * 0.001f * sr));
               mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * optoAttack;
               mOptoMemoryDb += (mSmoothedReductionDb - mOptoMemoryDb) * 0.005f;
            }
            else
            {
               // Fast transient release (40 ms) + slow cell release (400-1500 ms),
               // weighted by how hard it has been working.
               const float slowRelMs = std::clamp(releaseMs * 1.5f, 400.0f, 1500.0f);
               const float slowRelCoeff = expf(-1.0f / (slowRelMs * 0.001f * sr));
               const float optoWeight = std::clamp(mOptoMemoryDb / 12.0f, 0.2f, 0.85f);
               const float effRelCoeff = fastRelCoeff * (1.0f - optoWeight) + slowRelCoeff * optoWeight;
               mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * effRelCoeff;
               mOptoMemoryDb += (targetReductionDb - mOptoMemoryDb) * 0.0002f;
            }
            mOptoMemoryDb = DspMath::FlushDenormal(mOptoMemoryDb);
            break;
         }
         default:
         {
            const float coeff = (targetReductionDb > mSmoothedReductionDb) ? attackCoeff : releaseCoeff;
            mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * coeff;
            break;
         }
         }
         mSmoothedReductionDb = DspMath::FlushDenormal(mSmoothedReductionDb);
         mLastGain = DspMath::DbToLinear(-mSmoothedReductionDb);
         const float gr = mLastGain * makeupLin;

         if (analog)
         {
            // Warm: transformers either side and a top end that closes as
            // the gain comes down.
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
   std::atomic<int> mScHpfOn { 0 };
   std::atomic<int> mStyle { 0 };

   float mRmsMeanSq = 0.0f;
   float mSmoothedReductionDb = 0.0f;
   float mDecoupledDb = 0.0f; // bus style's release stage
   float mLastGain = 1.0f;    // FET style's feedback
   float mLastInputDb = -100.0f;
   float mOptoMemoryDb = 0.0f;

   AnalogDsp::OnePoleLP mWarmLp[kMaxChannels];
   DspMath::TptSvf mScHpf[kMaxChannels]; // detector-only high-pass (kScHpfHz)

   MeterRing mGrMeter;
};

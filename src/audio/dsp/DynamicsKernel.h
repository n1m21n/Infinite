#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Dynamics' kernel - cut down to KHS Audio Compressor's control surface
// (Threshold, Ratio, Attack, Release, Makeup, a Peak/RMS detector switch, a
// Sidechain on/off switch, a meter - 5 knobs + 2 switches, one processing
// mode) per .claude/skills/new-audio-node/SKILL.md's minimalism rule. An
// earlier version had 4 selectable modes (compress/limit/gate/expand) and 17
// params (knee, lookahead, hold, range, stereo link, auto release, sidechain
// HP/audition, ...); all of that is gone, not hidden - this is a compressor,
// full stop.
//
// Primary reference: Giannoulis, Massberg & Reiss, "Digital Dynamic Range
// Compressor Design - A Tutorial and Analysis" (JAES 2012) - the soft-knee
// gain computer (their eq. 4) and the branching/decoupled peak-detector
// smoothing topology (their §III) both come directly from it. Knee width is
// a fixed internal constant (6 dB), not a control - it shapes the transition
// band around threshold, audible but not worth a knob at this size.
namespace DynamicsDsp
{
   static constexpr float kKneeDb = 6.0f;

   // Static gain-computer characteristic, in dB: given an instantaneous
   // input level xG (dBFS), returns the gain-computer's target output level
   // yG (dBFS) - Giannoulis eq. 4. A pure function of its arguments, shared
   // by the kernel's own per-sample path and the DSP fixture's "measured vs
   // analytic" assertion, so there is exactly one definition of "the
   // analytic curve" to compare against.
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

// AudioEffectNode's kernel for the Dynamics node. See IEffectKernel.h for
// the PushParams/ProcessBlock split this obeys.
class DynamicsKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr float kRmsWindowMs = 20.0f; // fixed internal constant, not a control

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
      Reset();
   }

   void Reset() override
   {
      mRmsMeanSq = 0.0f;
      mSmoothedReductionDb = 0.0f;
      mLastInputDb = -100.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override
   {
      const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
      const bool useRms = mDetectorRms.load(std::memory_order_relaxed) != 0;
      const bool useSidechain = sidechain != nullptr && mSidechainExternal.load(std::memory_order_relaxed) != 0;

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

         // Detection signal: the external sidechain if wired and enabled,
         // else the main input, always fully stereo-linked (max across
         // channels) - the simplest, most predictable behavior for a
         // single-knob-per-thing compressor.
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

         // Branching/decoupled smoothing (Giannoulis §III): attack when the
         // target calls for MORE reduction than currently applied, release
         // when it calls for less. Fully linked (one shared reduction value
         // for every channel), the simplest predictable stereo behavior.
         const float coeff = (targetReductionDb > mSmoothedReductionDb) ? attackCoeff : releaseCoeff;
         mSmoothedReductionDb = targetReductionDb + (mSmoothedReductionDb - targetReductionDb) * coeff;

         const float gr = DspMath::DbToLinear(-mSmoothedReductionDb) * makeupLin;
         for (int ch = 0; ch < numChannels; ch++)
            out.channels[ch][i] = in.channels[ch][i] * gr + 1.0e-20f; // tiny bias: denormal guard
      }

      // {last sample's detected input level in dB, gain reduction in dB} -
      // the visualizer's operating-point dot and GR meter.
      const float payload[2] = { mLastInputDb, mSmoothedReductionDb };
      mGrMeter.Write(payload, 2);
   }

   int LatencySamples() const override { return 0; } // no lookahead at this size
   MeterRing* ExtraMeter() override { return &mGrMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mDetectorRms { 0 };
   std::atomic<int> mSidechainExternal { 0 };

   float mRmsMeanSq = 0.0f;
   float mSmoothedReductionDb = 0.0f;
   float mLastInputDb = -100.0f;

   MeterRing mGrMeter;
};

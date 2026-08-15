#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// A brick-wall peak limiter - four knobs (Threshold, Release, In gain, Out
// gain) per .claude/skills/new-audio-node/SKILL.md's minimalism rule: no
// ceiling-vs-threshold split, no true-peak/oversampling, no mode selector,
// no lookahead knob. Lookahead is a fixed internal constant, the same way
// DynamicsKernel fixes its knee width rather than exposing it.
//
// Standard lookahead-limiter topology: delay the audio by a small fixed
// window, and drive the gain from a peak detector that reads *ahead* of the
// delayed output by that same window, so gain reduction is already in place
// by the time the loud sample the detector saw actually arrives at the
// output - the attack is therefore governed by the lookahead time itself
// (see e.g. Zölzer, "DAFX: Digital Audio Effects", the peak-limiter section),
// not a separate attack control. Release remains a knob, same
// attack/release-coupled one-pole smoothing as DynamicsKernel (Giannoulis,
// Massberg & Reiss, JAES 2012, §III) - reduction always jumps toward more
// limiting instantly (attack), and eases back per the release knob.
class LimiterKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr float kLookaheadMs = 1.5f; // fixed internal constant, not a control

   enum ParamSlot
   {
      kThreshold = 0,
      kReleaseMs,
      kInGainDb,
      kOutGainDb,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mLookaheadSamples = std::max(1, (int)std::ceil(kLookaheadMs * 0.001 * sampleRate));
      for (int ch = 0; ch < kMaxChannels; ch++)
         mDelay[ch].assign((size_t)mLookaheadSamples, 0.0f);
      mLevelWindow.assign((size_t)mLookaheadSamples, 0.0f);
      Reset();
   }

   void Reset() override
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
         std::fill(mDelay[ch].begin(), mDelay[ch].end(), 0.0f);
      std::fill(mLevelWindow.begin(), mLevelWindow.end(), 0.0f);
      mWritePos = 0;
      mSmoothedGain = 1.0f;
      mLastGrDb = 0.0f;
      mLastOutPeakDb = -100.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out) override
   {
      const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
      const int window = (int)mLevelWindow.size();
      if (window <= 0)
         return;

      const float attackCoeff = expf(-1.0f / (kLookaheadMs * 0.001f * (float)mSampleRate));

      float outPeak = 0.0f;
      for (int i = 0; i < out.numFrames; i++)
      {
         const float thresholdLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kThreshold));
         const float releaseMs = std::max(1.0f, mMailbox.SmoothedValue(kReleaseMs));
         const float inGainLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kInGainDb));
         const float outGainLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kOutGainDb));
         const float releaseCoeff = expf(-1.0f / (releaseMs * 0.001f * (float)mSampleRate));

         // Post-inGain samples go into the delay line; the window's peak
         // (this new sample plus everything already in flight) drives the
         // gain that will apply once this same sample reaches the output.
         float gained[kMaxChannels];
         float levelAtN = 0.0f;
         for (int ch = 0; ch < numChannels; ch++)
         {
            gained[ch] = in.channels[ch][i] * inGainLin;
            levelAtN = std::max(levelAtN, std::fabs(gained[ch]));
         }

         float delayed[kMaxChannels];
         for (int ch = 0; ch < numChannels; ch++)
         {
            delayed[ch] = mDelay[ch][mWritePos];
            mDelay[ch][mWritePos] = gained[ch];
         }
         mLevelWindow[mWritePos] = levelAtN;
         mWritePos++;
         if (mWritePos >= window)
            mWritePos = 0;

         float windowMax = 0.0f;
         for (int k = 0; k < window; k++)
            windowMax = std::max(windowMax, mLevelWindow[k]);

         const float targetGain = (windowMax > thresholdLin && windowMax > 1.0e-9f) ? thresholdLin / windowMax : 1.0f;
         const float coeff = (targetGain < mSmoothedGain) ? attackCoeff : releaseCoeff;
         mSmoothedGain = targetGain + (mSmoothedGain - targetGain) * coeff;

         for (int ch = 0; ch < numChannels; ch++)
         {
            const float v = delayed[ch] * mSmoothedGain * outGainLin;
            out.channels[ch][i] = v + 1.0e-20f; // denormal guard
            outPeak = std::max(outPeak, std::fabs(v));
         }

         mLastGrDb = -DspMath::LinearToDb(std::max(1.0e-9f, mSmoothedGain));
      }
      mLastOutPeakDb = DspMath::LinearToDb(std::max(1.0e-9f, outPeak));

      // {gain reduction in dB, output peak in dB} - the GR meter's bar and
      // its 0/-10/-20 scale.
      const float payload[2] = { mLastGrDb, mLastOutPeakDb };
      mGrMeter.Write(payload, 2);
   }

   int LatencySamples() const override { return mLookaheadSamples; }
   MeterRing* ExtraMeter() override { return &mGrMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   int mLookaheadSamples = 1;

   std::vector<float> mDelay[kMaxChannels];
   std::vector<float> mLevelWindow;
   int mWritePos = 0;

   float mSmoothedGain = 1.0f;
   float mLastGrDb = 0.0f;
   float mLastOutPeakDb = -100.0f;

   MeterRing mGrMeter;
};

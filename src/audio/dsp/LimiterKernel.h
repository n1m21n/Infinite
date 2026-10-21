#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "PeakLimiter.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// A brick-wall true-peak limiter - four knobs (Threshold, Release, In gain,
// Out gain). Threshold is the ceiling: no output sample, and no inter-sample
// peak the 4x detector can see, goes above it (before Out gain).
//
// The processing is PeakLimiter (see its header for why the ceiling holds by
// construction): a 1.5 ms look-ahead hold, a straight-line attack ramp across
// that window, and a one-pole release on the knob. Look-ahead is a fixed
// internal constant, not a control. All channels share one gain, so the
// stereo image never moves.
class AudioEffectNode;

class LimiterKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = PeakLimiter::kMaxChannels;
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
      mLimiter.Prepare(sampleRate, kLookaheadMs);
      mCachedReleaseMs = -1.0f;
      Reset();
   }

   void Reset() override
   {
      mLimiter.Reset();
      mLastGrDb = 0.0f;
      mLastOutPeakDb = -100.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out) override
   {
      const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));

      float outPeak = 0.0f;
      for (int i = 0; i < out.numFrames; i++)
      {
         const float ceiling = DspMath::DbToLinear(mMailbox.SmoothedValue(kThreshold));
         const float releaseMs = std::max(1.0f, mMailbox.SmoothedValue(kReleaseMs));
         const float inGainLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kInGainDb));
         const float outGainLin = DspMath::DbToLinear(mMailbox.SmoothedValue(kOutGainDb));
         if (releaseMs != mCachedReleaseMs)
         {
            mCachedReleaseMs = releaseMs;
            mReleaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * (float)mSampleRate));
         }

         float frame[kMaxChannels];
         for (int ch = 0; ch < numChannels; ch++)
            frame[ch] = in.channels[ch][i] * inGainLin;

         mLimiter.ProcessFrame(frame, numChannels, ceiling, mReleaseCoeff);

         for (int ch = 0; ch < numChannels; ch++)
         {
            const float v = frame[ch] * outGainLin;
            out.channels[ch][i] = v + 1.0e-20f; // denormal guard
            outPeak = std::max(outPeak, std::fabs(v));
         }
      }
      mLastGrDb = -DspMath::LinearToDb(std::max(1.0e-9f, mLimiter.LastGain()));
      mLastOutPeakDb = DspMath::LinearToDb(std::max(1.0e-9f, outPeak));

      // {gain reduction in dB, output peak in dB} - the GR meter's bar and
      // its 0/-10/-20 scale.
      const float payload[2] = { mLastGrDb, mLastOutPeakDb };
      mGrMeter.Write(payload, 2);
   }

   int LatencySamples() const override { return mLimiter.Latency(); }
   MeterRing* ExtraMeter() override { return &mGrMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0; // written by PrepareToPlay only (audio-thread owned)

   PeakLimiter mLimiter;
   float mCachedReleaseMs = -1.0f;
   float mReleaseCoeff = 0.0f;

   float mLastGrDb = 0.0f;
   float mLastOutPeakDb = -100.0f;

   MeterRing mGrMeter;
};

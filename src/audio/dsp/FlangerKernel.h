#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine
#include "AnalogPrimitives.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Flanger's kernel - modulated delay line per channel with digital and
// analog modes. The feedback tap runs through a fixed ~7kHz damping filter
// (mFbDampL/R) before being written back into the line, in both modes - a
// real BBD/tape flanger loop always loses a little top end on every pass
// (the bucket-brigade compander's own bandwidth limit), and a plain
// unfiltered comb loop reads as harsher/more metallic than that. No new
// param - still the same 5 knobs (delay, depth, rate, feedback, spread).
class AudioEffectNode;

class FlangerKernel : public IEffectKernel
{
public:
   static constexpr float kMaxDelayMs = 20.0f;

   enum ParamSlot
   {
      kDelayMs = 0,
      kDepthMs,
      kRateHz,
      kFeedback,
      kSpread,
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
         mFilterL[i].SetCutoff(9000.0f, 0.707f);
         mFilterR[i].SetSampleRate(sampleRate);
         mFilterR[i].SetCutoff(9000.0f, 0.707f);
      }
      mFbDampL.SetCutoff(7000.0f, sampleRate);
      mFbDampR.SetCutoff(7000.0f, sampleRate);
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
      mFbDampL.Reset();
      mFbDampR.Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mSync { 0 };
   std::atomic<int> mRateDiv { MusicTime::kQuarter };
   std::atomic<int> mAnalog { 0 };

   DelayLine mLineL, mLineR;
   double mPhase = 0.0;

   // Analog mode components
   AnalogDsp::DriftLfo mDriftLfo;
   DspMath::TptSvf mFilterL[2];
   DspMath::TptSvf mFilterR[2];

   // Feedback-path damping - fixed cutoff, always on (both modes).
   AnalogDsp::OnePoleLP mFbDampL, mFbDampR;
};

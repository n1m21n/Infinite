#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine - Hermite-interpolated fractional read, already proven by Delay
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Chorus's kernel - `taps` (2 or 3) modulated delay-line voices per channel,
// summed. Primary reference: Dattorro, "Effect Design Part 2: Delay-Line
// Modulation and Chorus" (JAES 1997) - the standard modulated-delay chorus
// topology (short base delay, sinusoidal LFO depth, multiple detuned voices,
// stereo-decorrelated via LFO phase offset). Matches the KHS Audio Chorus
// module's Delay/Spread/Taps/Depth/Rate/Mix control set directly.
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
      Reset();
   }

   void Reset() override
   {
      mLineL.Reset();
      mLineR.Reset();
      mPhase = 0.0;
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

   DelayLine mLineL, mLineR;
   double mPhase = 0.0; // 0..1, one shared LFO cycle every voice/channel reads at its own offset
};

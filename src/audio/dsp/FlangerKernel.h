#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Flanger's kernel - one short modulated delay line per channel with
// feedback, the classic jet-swoosh comb-filter topology. Primary reference:
// Dattorro's "Effect Design Part 2" (as with Chorus) covers flanging as the
// same modulated-delay-line family at a shorter base delay with feedback
// added - the feedback path is what turns the broad chorus comb into
// flanging's narrow, resonant one. Right channel reads its LFO a fixed
// quarter-cycle ahead of the left for stereo width, hardcoded rather than a
// param (`spread`/`offset`/`motion` in the design doc's KHS reference are
// cut per .claude/skills/new-audio-node/SKILL.md's minimalism rule - the
// knob budget goes to feedback instead, which flanging is not itself without).
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

   std::atomic<int> mSync { 0 };
   std::atomic<int> mRateDiv { MusicTime::kQuarter };

   DelayLine mLineL, mLineR;
   double mPhase = 0.0;
};

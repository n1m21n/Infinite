#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Transient Shaper's kernel - a dual-envelope difference detector (fast
// follower tracks instantaneous peaks, slow follower tracks the sustained
// level; how far the fast one sits above the slow one is "how much
// transient is happening right now") drives a continuous crossfade between
// an `attack` gain and a `sustain` gain. This is the standard transient-
// designer topology described generically in DSP transient-shaping
// literature (e.g. Case, "Sound FX: Unlocking the Creative Potential of
// Recording Studio Effects", ch. 9's transient-shaper section) - envelope-
// difference detection, not a compressor's threshold/ratio gain computer.
// Two knobs (KHS Audio's own Transient Shaper is also just Attack/Sustain)
// plus mix - `pump`/`sidechain` from the design doc's reference image are
// cut per .claude/skills/new-audio-node/SKILL.md's minimalism rule.
class AudioEffectNode;

class TransientShaperKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kAttackDb = 0,
      kSustainDb,
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
      mFastEnv = 0.0f;
      mSlowEnv = 0.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mTransientMeter; } // {transientAmount 0..1}

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   float mFastEnv = 0.0f, mSlowEnv = 0.0f;
   MeterRing mTransientMeter;
};

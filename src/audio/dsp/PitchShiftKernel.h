#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "DelayKernel.h" // reuses DelayLine's Hermite-interpolated fractional read
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Pitch Shifter's kernel - the classic two-tap crossfaded delay-line pitch
// shifter: two read taps into a circular buffer, each tap's delay-from-write
// distance moves at (1 - ratio) samples/sample, so as it walks it reads the
// buffer at an effectively re-clocked rate (transposing pitch by `ratio`).
// Each tap wraps every `grain` samples and is triangle-windowed so the wrap
// (which would otherwise click) crossfades into the other tap instead - the
// two windows are a half-cycle apart, so their sum is constant. Primary
// reference: this exact two-tap delay-line technique is standard in the
// time-domain pitch-shifting literature (e.g. Zolzer, "DAFX: Digital Audio
// Effects", ch. 7's "delay-line" pitch shifter, and Puckette, "Theory and
// Technique of Electronic Music" §7.3's "overlap-add" delay reader) -
// simpler than a phase vocoder, no FFT, and the standard choice for a
// single continuously-variable knob rather than a frozen/offline shift.
class AudioEffectNode;

class PitchShiftKernel : public IEffectKernel
{
public:
   static constexpr float kMaxGrainMs = 250.0f;

   enum ParamSlot
   {
      kSemitones = 0,
      kGrainMs,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const int maxSamples = (int)std::ceil(kMaxGrainMs * 0.001 * sampleRate) * 2 + 16;
      mLineL.Prepare(maxSamples);
      mLineR.Prepare(maxSamples);
      Reset();
   }

   void Reset() override
   {
      mLineL.Reset();
      mLineR.Reset();
      mTap0 = 0.0f;
      mTap1 = 0.0f;
      mTapsInitialized = false;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   // The grain length is the worst-case latency before the wet signal fully
   // reflects a fresh input - not compensated (a pitch shifter's own
   // character includes this smear), reported for hosts that want it.
   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   DelayLine mLineL, mLineR;
   float mTap0 = 0.0f, mTap1 = 0.0f; // shared tap phase (in samples) for both channels
   bool mTapsInitialized = false;
};

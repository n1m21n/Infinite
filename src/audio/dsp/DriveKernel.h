#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Drive's kernel - one saturator (tanh waveshaper with an asymmetry/bias
// control), not the design doc's 6-mode curve dropdown (tanh / hard clip /
// foldback / bitcrush / diode / tube), per
// .claude/skills/new-audio-node/SKILL.md's minimalism rule: a dropdown that
// switches between more than two processing modes is a smell, and tanh alone
// - warm at low drive, hard-saturating at high drive - already covers the
// "overdrive to fuzz" range a single knob is expected to cover. `bias` is
// what makes this more than a one-knob toy: an asymmetric transfer curve
// produces even harmonics, the entire difference between "fuzzy" (symmetric)
// and "warm" (asymmetric) saturation, and it's cheap once tanh exists.
//
// Primary reference: the tanh soft-clipper is the standard closed-form
// saturator in the DSP literature (e.g. Zolzer, "DAFX: Digital Audio
// Effects", ch. 4's nonlinear processing survey); DspMath::FastTanh is this
// codebase's rational approximation of it (see its own comment).
//
// Tier 2 (oversampling, ADAA anti-aliasing, DC-blocker toggle, bit-depth/
// downsample, pre/post filters) is cut entirely, not hidden - a DC blocker
// is the one piece that isn't optional (bias otherwise leaks a constant
// offset straight to the output), so it runs always, hardcoded, with no
// param.
class AudioEffectNode;

// Pure function so the visualizer (main-thread, params only) and the audio
// thread compute the identical curve - the same reason DynamicsDsp::
// GainComputerDb exists. Bias is subtracted back out via its own
// zero-signal shaped value, so a bias offset warps the curve's symmetry
// without visibly sliding the whole curve's DC point on the graph the way
// a raw pre-shape offset would.
namespace DriveDsp
{
   // Blends tanh (color=0, warm) toward a harder arctan saturator (color=1,
   // more aggressive/brighter clipping character) - arctan is the standard
   // alternate closed-form saturator alongside tanh in the same DAFX ch. 4
   // survey, with a harder knee at the same input level, which is exactly
   // the "warm vs. aggressive" character axis a drive unit's color/tone
   // knob is expected to cover (distinct from `tone`'s post-shape spectral
   // tilt, which doesn't touch the curve's own shape at all).
   inline float RawShape(float x, float driveDb, float bias, float color)
   {
      const float driveLin = DspMath::DbToLinear(driveDb);
      const float driven = (x + bias) * driveLin;
      const float soft = DspMath::FastTanh(driven);
      const float hard = (2.0f / (float)M_PI) * atanf(driven * 2.0f);
      return soft * (1.0f - color) + hard * color;
   }

   inline float Shape(float x, float driveDb, float bias, float color)
   {
      const float zeroRef = RawShape(0.0f, driveDb, bias, color);
      return RawShape(x, driveDb, bias, color) - zeroRef;
   }
}

// Simple one-pole DC blocker (y[n] = x[n] - x[n-1] + R*y[n-1]), the standard
// textbook removal for the constant offset an asymmetric waveshaper's bias
// introduces. R close to 1 pushes the cutoff low (~8 Hz at 44.1 kHz here)
// so it has no audible effect on the shaped signal itself.
struct DcBlocker
{
   float prevIn = 0.0f, prevOut = 0.0f;

   void Reset() { prevIn = prevOut = 0.0f; }

   float Process(float x)
   {
      const float y = x - prevIn + 0.995f * prevOut;
      prevIn = x;
      prevOut = y;
      return y;
   }
};

// Bipolar tilt around a fixed 1 kHz pivot, the same construction
// DelayKernel.h's DelayToneFilter uses for its feedback-path tone control -
// negative darkens, positive brightens, y=x exactly at tone=0.
struct DriveToneFilter
{
   DspMath::TptSvf svf;

   void Reset() { svf.Reset(); }

   float Process(float x, float tone, float sampleRate)
   {
      if (tone != 0.0f)
      {
         svf.SetSampleRate(sampleRate);
         svf.SetCutoff(1000.0f, 0.707f);
         const DspMath::TptSvf::Outputs o = svf.Process(x);
         x = x + tone * (o.high - o.low) * 0.5f;
      }
      return x;
   }
};

class DriveKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 8;

   enum ParamSlot
   {
      kDriveDb = 0,
      kBias,
      kTone,
      kColor,
      kOutputDb,
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
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         mDcBlockers[ch].Reset();
         mToneFilters[ch].Reset();
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   DcBlocker mDcBlockers[kMaxChannels];
   DriveToneFilter mToneFilters[kMaxChannels];
};

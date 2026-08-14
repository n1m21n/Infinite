#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Stereo's kernel - replaces stereo/panning/mono per README §3. Cut from the
// design doc's `mode` dropdown (stereo/mono/mid-side/swap) down to a single
// always-on M/S width control, per .claude/skills/new-audio-node/SKILL.md's
// minimalism rule - width=0 already gives mono and width=2 already gives an
// exaggerated swap-adjacent image, so a mode selector adds no reachable
// state. Matches the KHS Audio Stereo module's three knobs (Mid/Width/Pan)
// referenced directly.
//
// Primary reference: mid/side matrixing (M = 0.5(L+R), S = 0.5(L-R), width
// scales S before decoding back) is standard stereo-processing textbook
// technique (e.g. Bosi & Goldberg, "Introduction to Digital Audio Coding and
// Standards" §5's M/S coding); bass mono uses Linkwitz-Riley-style crossover
// via two DspMath::TptSvf low/high taps (Zavalishin's SVF, already shared).
class AudioEffectNode;

class StereoKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kWidth = 0,
      kPan,
      kBassMonoHz,
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
      mSvfL.Reset();
      mSvfR.Reset();
      mCorrSum = mCorrL2 = mCorrR2 = 0.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mCorrMeter; } // {correlation -1..1}

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   DspMath::TptSvf mSvfL, mSvfR;

   float mCorrSum = 0.0f, mCorrL2 = 0.0f, mCorrR2 = 0.0f;
   MeterRing mCorrMeter;
};

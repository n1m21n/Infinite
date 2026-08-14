#pragma once

#include <algorithm>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Formant Filter's kernel - three parallel bandpass resonators (DspMath::
// TptSvf's band output) tuned to a vowel's F1/F2/F3 formant frequencies and
// summed, the standard three-resonator vocal-formant filter topology used
// throughout speech-synthesis/vocoder literature (e.g. the source-filter
// model in Klatt, "Software for a cascade/parallel formant synthesizer",
// JASA 1980 - three parallel formant resonators is exactly Klatt's parallel
// branch). `vowel` sweeps continuously A-E-I-O-U by linearly interpolating
// each formant's table frequency between its two neighbouring vowels, so
// one knob morphs the whole bank rather than a 5-way dropdown - the smooth
// version of the choice a discrete selector would force.
class AudioEffectNode;

namespace FormantDsp
{
   // Peterson & Barney (1952)-style average adult vowel formant table,
   // Hz, [F1, F2, F3] per vowel, ordered A E I O U so `vowel` in [0,4]
   // indexes directly with fractional interpolation between neighbours.
   struct Formants { float f1, f2, f3; };
   inline Formants VowelFormants(float vowel)
   {
      static const Formants kTable[5] = {
         { 730.0f, 1090.0f, 2440.0f }, // A
         { 530.0f, 1840.0f, 2480.0f }, // E
         { 270.0f, 2290.0f, 3010.0f }, // I
         { 570.0f, 840.0f, 2410.0f },  // O
         { 300.0f, 870.0f, 2240.0f },  // U
      };
      const float clamped = std::clamp(vowel, 0.0f, 4.0f);
      const int i0 = std::min(3, (int)clamped);
      const int i1 = i0 + 1;
      const float t = clamped - (float)i0;
      return { kTable[i0].f1 + t * (kTable[i1].f1 - kTable[i0].f1),
               kTable[i0].f2 + t * (kTable[i1].f2 - kTable[i0].f2),
               kTable[i0].f3 + t * (kTable[i1].f3 - kTable[i0].f3) };
   }
}

class FormantFilterKernel : public IEffectKernel
{
public:
   static constexpr int kMaxChannels = 2;

   enum ParamSlot
   {
      kVowel = 0,
      kQ,
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
         for (int f = 0; f < 3; f++)
            mSvf[ch][f].Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   DspMath::TptSvf mSvf[kMaxChannels][3];
};

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/MusicTime.h"
#include "audio/ParamMailbox.h"

// Tremolo's kernel - a unipolar gain envelope traced by a band-limited LFO
// (DspMath::PolyBlepOsc), applied as straight amplitude modulation. This is
// NOT ring modulation: Ring Mod (RingModKernel.h) multiplies by a bipolar
// carrier at audio rate to replace the input's frequencies with sum/
// difference sidebands; Tremolo's LFO runs sub-audio and only scales the
// input's existing level up and down, leaving its frequency content
// untouched - see any classic tremolo-pedal description (e.g. a Fender
// "vibrato channel" bias-tremolo, or Roads, "The Computer Music Tutorial"
// ch. 5's distinction between AM and ring mod). `shape` is the LFO's
// identity, the same carve-out Ring Mod's `waveform` and Audio Filter's
// `type` dropdowns already have, not a processing mode.
class AudioEffectNode;

class TremoloKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kRateHz = 0,
      kDepth,
      kStereoPhaseDeg,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      Reset();
   }

   void Reset() override { mOsc.phase = 0.0; }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

   // Reads the LFO's own waveform enum (DspMath::kWave*) from a tremolo
   // `shape` param (0=sine, 1=triangle, 2=square, 3=ramp-down) - shared by
   // the kernel and the visualizer so the two can never map differently.
   static int ShapeToDspWaveform(int shape)
   {
      switch (shape)
      {
         case 1: return DspMath::kWaveTriangle;
         case 2: return DspMath::kWaveSquare;
         case 3: return DspMath::kWaveSaw; // negated in Generate - ramp-down, not ramp-up
         case 0:
         default: return DspMath::kWaveSine;
      }
   }

   // The unipolar gain envelope for a bipolar LFO sample: depth==0 is exactly
   // unity, depth==1 reaches zero at the trough while the peak stays at
   // unity - never boosts above unity, unlike a naive `1 + depth * lfo`.
   static float GainFromLfo(float lfo, float depth)
   {
      return 1.0f - depth * (0.5f - 0.5f * lfo);
   }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mShape { 0 };
   std::atomic<int> mSync { 0 };
   std::atomic<int> mRateDiv { MusicTime::kEighth };

   DspMath::PolyBlepOsc mOsc;
};

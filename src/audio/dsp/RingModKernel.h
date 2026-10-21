#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"
#include "Oversampler.h"

// Ring Mod's kernel - multiplies the input by a band-limited oscillator
// (DspMath::PolyBlepOsc, already proven by Wavetable/Oscillator-family
// code), the textbook definition of ring modulation (amplitude modulation
// with no DC offset in the carrier, producing sum/difference sidebands
// rather than the original frequencies - see any AM/ring-mod DSP
// reference, e.g. Roads, "The Computer Music Tutorial", ch. 5). `waveform`
// is the modulator's identity, not a processing-mode switch, the same
// carve-out Audio Filter's `type` dropdown already has.
//
// Runs 4x oversampled: input times carrier puts energy at f + fc, which for
// any input above Nyquist - fc (17 kHz at 44.1 kHz with a 5 kHz carrier)
// folds back as inharmonic aliases, and the square/saw carriers' harmonics
// fold the same way. At 4x the sum band stays below the inner Nyquist and
// the decimator removes it; the oscillator runs at the inner rate.
class AudioEffectNode;

class RingModKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kFreqHz = 0,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      mOversampler.Prepare(kOversample, 32);
      Reset();
   }

   void Reset() override
   {
      mOsc.phase = 0.0;
      mEnvFollower = 0.0f;
      mOversampler.Reset();
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return mOversampler.Latency(); }

private:
   static constexpr int kOversample = 4;
   Oversampler mOversampler;
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mWaveform { DspMath::kWaveSine };
   std::atomic<int> mAnalog { 0 };
   DspMath::PolyBlepOsc mOsc;
   float mEnvFollower = 0.0f;
};

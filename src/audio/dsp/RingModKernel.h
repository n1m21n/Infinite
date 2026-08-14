#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Ring Mod's kernel - multiplies the input by a band-limited oscillator
// (DspMath::PolyBlepOsc, already proven by Wavetable/Oscillator-family
// code), the textbook definition of ring modulation (amplitude modulation
// with no DC offset in the carrier, producing sum/difference sidebands
// rather than the original frequencies - see any AM/ring-mod DSP
// reference, e.g. Roads, "The Computer Music Tutorial", ch. 5). `waveform`
// is the modulator's identity, not a processing-mode switch, the same
// carve-out Audio Filter's `type` dropdown already has.
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
      Reset();
   }

   void Reset() override { mOsc.phase = 0.0; }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;

   std::atomic<int> mWaveform { DspMath::kWaveSine };
   DspMath::PolyBlepOsc mOsc;
};

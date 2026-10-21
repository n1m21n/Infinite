#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "IEffectKernel.h"
#include "audio/DspMath.h"
#include "audio/ParamMailbox.h"

// Bitcrush's kernel - sample-rate reduction (zero-order hold) plus bit-depth
// quantization, the two textbook lo-fi degradations (see Zolzer, "DAFX:
// Digital Audio Effects", ch. 4's nonlinear-processing survey, which covers
// both as standard "digital" effects). Cut from the design doc's Quantize/
// Bits/Dither/ADC-Q/DAC-Q five-control panel down to rate + bits + mix per
// .claude/skills/new-audio-node/SKILL.md's minimalism rule - dither and the
// separate ADC/DAC quantizer stages are inaudible refinements on top of the
// two controls that actually define "how crushed", matching the KHS Audio
// Bitcrush module's own big display (downsample rate) plus Bits knob.
class AudioEffectNode;

class BitcrushKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kRateHz = 0,
      kBits,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      for (int i = 0; i < 2; i++)
      {
         mPreFilterL[i].SetSampleRate(sampleRate);
         mPreFilterL[i].SetCutoff(11000.0f, 0.707f);
         mPreFilterR[i].SetSampleRate(sampleRate);
         mPreFilterR[i].SetCutoff(11000.0f, 0.707f);
      }
      Reset();
   }

   void Reset() override
   {
      mPhase = 0.0;
      mHoldL = mHoldR = 0.0f;
      mPrevL = mPrevR = 0.0f;
      for (int i = 0; i < 2; i++)
      {
         mPreFilterL[i].Reset();
         mPreFilterR[i].Reset();
      }
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   std::atomic<int> mAnalog { 0 };
   std::atomic<int> mDither { 0 };

   // TPDF dither source: the difference of two uniform draws is triangular
   // on (-1, 1) LSB - the standard dither, which makes the quantisation error
   // independent of the signal (no harmonic distortion on quiet material) at
   // the cost of a flat noise floor. Seeded, so renders are deterministic.
   uint32_t mRng = 0x9E3779B9u;
   float Tpdf()
   {
      auto next = [this]() {
         mRng ^= mRng << 13;
         mRng ^= mRng >> 17;
         mRng ^= mRng << 5;
         return (float)(mRng >> 8) * (1.0f / 16777216.0f);
      };
      return next() - next();
   }

   // The hold clock, in double so its instants stay on the same times at
   // every device rate over a long render (F14).
   double mPhase = 0.0;
   float mHoldL = 0.0f, mHoldR = 0.0f;
   float mPrevL = 0.0f, mPrevR = 0.0f; // last input sample, for the fractional hold instant

   // Analog mode: 11kHz reconstruction pre-filter stages
   DspMath::TptSvf mPreFilterL[2];
   DspMath::TptSvf mPreFilterR[2];
};

#pragma once

#include <algorithm>
#include <cmath>

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
      mPhase = 0.0f;
      mHoldL = mHoldR = 0.0f;
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

   float mPhase = 0.0f;
   float mHoldL = 0.0f, mHoldR = 0.0f;

   // Analog mode: 11kHz reconstruction pre-filter stages
   DspMath::TptSvf mPreFilterL[2];
   DspMath::TptSvf mPreFilterR[2];
};

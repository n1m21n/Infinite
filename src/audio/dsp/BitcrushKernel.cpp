#include "BitcrushKernel.h"

#include "nodes/AudioEffectNode.h"

void BitcrushKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kBits, node.Param("bits"));
}

namespace
{
   inline float QuantizeToBits(float x, float bits)
   {
      const float levels = powf(2.0f, std::max(1.0f, bits)) - 1.0f;
      return roundf(std::clamp(x, -1.0f, 1.0f) * levels) / levels;
   }
}

void BitcrushKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));

   for (int i = 0; i < out.numFrames; i++)
   {
      const float rateHz = std::clamp(mMailbox.SmoothedValue(kRateHz), 100.0f, (float)mSampleRate);
      const float bits = mMailbox.SmoothedValue(kBits);

      const float phaseInc = rateHz / (float)mSampleRate;
      mPhase += phaseInc;
      const bool tick = mPhase >= 1.0f;
      if (tick)
         mPhase -= floorf(mPhase);

      if (tick)
      {
         mHoldL = in.channels[0][i];
         mHoldR = numChannels >= 2 ? in.channels[1][i] : mHoldL;
      }

      out.channels[0][i] = QuantizeToBits(mHoldL, bits);
      if (numChannels >= 2)
         out.channels[1][i] = QuantizeToBits(mHoldR, bits);
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

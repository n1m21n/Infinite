#include "BitcrushKernel.h"

#include "AnalogPrimitives.h"
#include "nodes/AudioEffectNode.h"

void BitcrushKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kBits, node.Param("bits"));
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
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
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float rateHz = std::clamp(mMailbox.SmoothedValue(kRateHz), 100.0f, (float)mSampleRate);
      const float bits = mMailbox.SmoothedValue(kBits);

      const float phaseInc = rateHz / (float)mSampleRate;
      mPhase += phaseInc;
      const bool tick = mPhase >= 1.0f;
      if (tick)
         mPhase -= floorf(mPhase);

      if (analog)
      {
         float inL = in.channels[0][i];
         float inR = numChannels >= 2 ? in.channels[1][i] : inL;

         // 4th-order 11kHz reconstruction pre-filter
         inL = mPreFilterL[0].Process(inL).low;
         inL = mPreFilterL[1].Process(inL).low;
         inR = mPreFilterR[0].Process(inR).low;
         inR = mPreFilterR[1].Process(inR).low;

         if (tick)
         {
            mHoldL = inL;
            mHoldR = inR;
         }

         float outL = AnalogDsp::MuLawQuantize(mHoldL, bits);
         float outR = AnalogDsp::MuLawQuantize(mHoldR, bits);

         // Output buffer warmth
         outL = AnalogDsp::AsymTanh(outL, 0.08f);
         outR = AnalogDsp::AsymTanh(outR, 0.08f);

         out.channels[0][i] = outL;
         if (numChannels >= 2)
            out.channels[1][i] = outR;
      }
      else
      {
         // Digital exact path
         if (tick)
         {
            mHoldL = in.channels[0][i];
            mHoldR = numChannels >= 2 ? in.channels[1][i] : mHoldL;
         }

         out.channels[0][i] = QuantizeToBits(mHoldL, bits);
         if (numChannels >= 2)
            out.channels[1][i] = QuantizeToBits(mHoldR, bits);
      }

      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

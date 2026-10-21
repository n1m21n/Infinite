#include "BitcrushKernel.h"

#include "AnalogPrimitives.h"
#include "nodes/AudioEffectNode.h"

// The sample rate comes from PrepareToPlay, never from here: writing it
// from the main thread raced the audio thread's read.
void BitcrushKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{
   mMailbox.Push(kRateHz, node.Param("rate"));
   mMailbox.Push(kBits, node.Param("bits"));
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
   mDither.store(node.Param("dither") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

namespace
{
   // An N-bit converter: 2^N levels, step 2 / 2^N across [-1, 1), mid-tread
   // (zero is a level, so silence stays silence). `ditherLsb` is added in
   // units of one step before rounding. (This used to round x * (2^N - 1)
   // across [-1, 1] - 2^(N+1) - 1 levels, about one bit more than the knob
   // said, and three levels at "1 bit".)
   inline float QuantizeToBits(float x, float bits, float ditherLsb)
   {
      const float step = 2.0f / powf(2.0f, std::clamp(bits, 1.0f, 16.0f));
      const float q = roundf(std::clamp(x, -1.0f, 1.0f) / step + ditherLsb) * step;
      return std::clamp(q, -1.0f, 1.0f - step);
   }
}

void BitcrushKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, 2));
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;
   const bool dither = mDither.load(std::memory_order_relaxed) != 0;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float rateHz = std::clamp(mMailbox.SmoothedValue(kRateHz), 100.0f, (float)mSampleRate);
      const float bits = mMailbox.SmoothedValue(kBits);

      // mPhase is the hold clock at this sample (i * rate / SR), advanced
      // after use: advancing first put every hold one host sample early, a
      // different slice of the waveform at each device rate. The hold
      // instant falls between two host samples, `ago` samples before this
      // one, and the input is read there (linear), not at this sample:
      // ticks snapped to the host grid jittered differently at 44.1, 48 and
      // 96 kHz. Together they crushed one tone to levels 5.4 dB apart (F14).
      const double phaseInc = (double)rateHz / mSampleRate;
      const bool tick = mPhase >= 1.0;
      if (tick)
         mPhase -= floor(mPhase);
      const float ago = tick ? (float)std::min(1.0, mPhase / std::max(phaseInc, 1.0e-6)) : 0.0f;

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
            mHoldL = inL + (mPrevL - inL) * ago;
            mHoldR = inR + (mPrevR - inR) * ago;
         }
         mPrevL = inL;
         mPrevR = inR;

         float outL = AnalogDsp::MuLawQuantize(mHoldL, bits, 255.0f, dither ? Tpdf() : 0.0f);
         float outR = AnalogDsp::MuLawQuantize(mHoldR, bits, 255.0f, dither ? Tpdf() : 0.0f);

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
         const float inL = in.channels[0][i];
         const float inR = numChannels >= 2 ? in.channels[1][i] : inL;
         if (tick)
         {
            mHoldL = inL + (mPrevL - inL) * ago;
            mHoldR = inR + (mPrevR - inR) * ago;
         }
         mPrevL = inL;
         mPrevR = inR;

         out.channels[0][i] = QuantizeToBits(mHoldL, bits, dither ? Tpdf() : 0.0f);
         if (numChannels >= 2)
            out.channels[1][i] = QuantizeToBits(mHoldR, bits, dither ? Tpdf() : 0.0f);
      }

      mPhase += phaseInc;
      for (int ch = 2; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}

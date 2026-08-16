#include "FrequencyShifterKernel.h"

#include "nodes/AudioEffectNode.h"

void FrequencyShifterKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kShiftHz, node.Param("shift"));
   mMailbox.Push(kFeedback, node.Param("feedback"));
   mMailbox.Push(kSpreadHz, node.Param("spread"));
}

void FrequencyShifterKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));

   for (int i = 0; i < out.numFrames; i++)
   {
      const float baseShiftHz = mMailbox.SmoothedValue(kShiftHz);
      const float feedback = std::clamp(mMailbox.SmoothedValue(kFeedback), 0.0f, 0.95f);
      const float spreadHz = mMailbox.SmoothedValue(kSpreadHz);

      for (int ch = 0; ch < numChannels; ch++)
      {
         const float inSample = in.channels[ch][i];

         // Soft-clipped feedback path (one-sample delay)
         const float fb = DspMath::FastTanh(mLastOut[ch] * feedback);
         const float x = inSample + fb;

         // Path A: 4 cascaded 2nd-order allpass sections
         float xA = x;
         for (int s = 0; s < 4; s++)
            xA = mStagesA[ch][s].Process(xA, kCoeffsASq[s]);

         // Path B: 1-sample delay, then 4 cascaded 2nd-order allpass sections
         const float inB = mDelayB[ch];
         mDelayB[ch] = x;
         float xB = inB;
         for (int s = 0; s < 4; s++)
            xB = mStagesB[ch][s].Process(xB, kCoeffsBSq[s]);

         // Frequency shift per channel: Right channel offset by +spread
         const float freqHz = (ch == 1) ? (baseShiftHz + spreadHz) : baseShiftHz;

         // Single-sideband modulation: x(t)*cos(phi) - x_hat(t)*sin(phi)
         // Path B is in-phase x(t), Path A is quadrature x_hat(t)
         const float shifted = xB * cosf((float)mPhase[ch]) - xA * sinf((float)mPhase[ch]);
         mLastOut[ch] = shifted;
         out.channels[ch][i] = shifted;

         // Advance phase
         const double phaseInc = 2.0 * M_PI * (double)freqHz / mSampleRate;
         mPhase[ch] += phaseInc;
         while (mPhase[ch] >= 2.0 * M_PI)
            mPhase[ch] -= 2.0 * M_PI;
         while (mPhase[ch] < 0.0)
            mPhase[ch] += 2.0 * M_PI;
      }
   }
}

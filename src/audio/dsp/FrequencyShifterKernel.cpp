#include "FrequencyShifterKernel.h"

#include "nodes/AudioEffectNode.h"

void FrequencyShifterKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kShiftHz, node.Param("shift"));
   mMailbox.Push(kFeedback, node.Param("feedback"));
   mMailbox.Push(kSpreadHz, node.Param("spread"));
   mAnalog.store(node.Param("analog") != 0.0f ? 1 : 0, std::memory_order_relaxed);
}

void FrequencyShifterKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   for (int i = 0; i < out.numFrames; i++)
   {
      const float baseShiftHz = mMailbox.SmoothedValue(kShiftHz);
      const float feedback = std::clamp(mMailbox.SmoothedValue(kFeedback), 0.0f, 0.95f);
      const float spreadHz = mMailbox.SmoothedValue(kSpreadHz);

      if (analog)
      {
         float inMax = 0.0f;
         for (int ch = 0; ch < numChannels; ch++)
            inMax = std::max(inMax, std::fabs(in.channels[ch][i]));
         mEnvFollower += 0.005f * (inMax - mEnvFollower);
         mEnvFollower = DspMath::FlushDenormal(mEnvFollower);
      }

      for (int ch = 0; ch < numChannels; ch++)
      {
         const float inSample = in.channels[ch][i];

         float fb = mLastOut[ch] * feedback;
         if (analog)
         {
            fb = mFbLp[ch].Process(fb);
            fb = AnalogDsp::DiodeDeadZone(fb, 0.08f);
         }
         fb = DspMath::FastTanh(fb);
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
         float shifted = xB * cosf((float)mPhase[ch]) - xA * sinf((float)mPhase[ch]);

         if (analog)
         {
            const float carrierBleed = cosf((float)mPhase[ch]) * 0.008f * std::min(1.0f, mEnvFollower * 4.0f);
            shifted = AnalogDsp::AsymTanh(shifted + carrierBleed, 0.06f);
         }

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

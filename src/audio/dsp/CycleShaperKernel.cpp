#include "CycleShaperKernel.h"

#include "nodes/AudioEffectNode.h"

void CycleShaperKernel::PushParams(const AudioEffectNode& node, double /*sampleRate*/)
{
   const int waveform = std::clamp((int)std::round(node.Param("waveform")), 0, 2);
   const float thresholdDb = std::clamp(node.Param("threshold"), -60.0f, 0.0f);
   const float thresholdLinear = powf(10.0f, thresholdDb / 20.0f);
   const int smooth = std::clamp((int)std::round(node.Param("smooth")), 0, kMaxTail);
   const bool analog = node.Param("analog") > 0.5f;

   mWaveform.store(waveform, std::memory_order_relaxed);
   mThresholdLinear.store(thresholdLinear, std::memory_order_relaxed);
   mSmoothSamples.store(smooth, std::memory_order_relaxed);
   mAnalog.store(analog ? 1 : 0, std::memory_order_relaxed);
}

void CycleShaperKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int waveform = mWaveform.load(std::memory_order_relaxed);
   const float threshLinear = mThresholdLinear.load(std::memory_order_relaxed);
   const int smoothSamples = mSmoothSamples.load(std::memory_order_relaxed);
   const bool analog = mAnalog.load(std::memory_order_relaxed) != 0;

   const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });

   for (int ch = 0; ch < numChannels; ch++)
   {
      ChannelState& state = mChannels[ch];
      const float* inData = in.channels[ch];
      float* outData = out.channels[ch];

      for (int i = 0; i < out.numFrames; i++)
      {
         const float x = inData[i];
         const bool isCrossing = (state.prevSample <= 0.0f && x > 0.0f);

         if (isCrossing && state.inPos >= 4)
         {
            const int L = state.inPos;
            const float A = state.cyclePeak;

            if (A >= threshLinear)
            {
               // Resynthesize geometric waveform of length L and peak A
               const float invL = 1.0f / (float)L;
               const float twoPi = (float)(2.0 * M_PI);

               for (int m = 0; m < L; m++)
               {
                  const float phase = (float)m * invL * twoPi;
                  const float s = sinf(phase);
                  float y = 0.0f;

                  switch (waveform)
                  {
                     case 0: // Sine
                        y = A * s;
                        break;
                     case 1: // Square
                        y = A * (s >= 0.0f ? 1.0f : -1.0f);
                        break;
                     case 2: // Triangle
                     default:
                        y = (2.0f * A / (float)M_PI) * asinf(std::clamp(s, -1.0f, 1.0f));
                        break;
                  }

                  if (analog)
                     y = AnalogDsp::AsymTanh(y);

                  state.outBuffer[m] = y;
               }

               // Cubic-Hermite crossfade of first smoothSamples against tail of previous cycle
               const int crossfadeLen = std::min({ smoothSamples, L, state.lastTailLen });
               for (int m = 0; m < crossfadeLen; m++)
               {
                  const float t = (float)(m + 1) / (float)(crossfadeLen + 1);
                  const float h = 3.0f * t * t - 2.0f * t * t * t;
                  state.outBuffer[m] = (1.0f - h) * state.lastRenderedTail[m] + h * state.outBuffer[m];
               }

               // Cache tail of newly synthesized cycle
               state.lastTailLen = std::min(kMaxTail, L);
               for (int m = 0; m < state.lastTailLen; m++)
                  state.lastRenderedTail[m] = state.outBuffer[L - state.lastTailLen + m];

               state.outLen = L;
               state.outPos = 0;
               state.lastCycleLen = (float)L;
               state.lastCyclePeak = A;
            }
            else
            {
               // Below threshold: pass through input cycle untouched
               for (int m = 0; m < L; m++)
                  state.outBuffer[m] = state.inBuffer[m];
               state.outLen = L;
               state.outPos = 0;
               state.lastTailLen = 0;
            }

            state.inBuffer[0] = x;
            state.inPos = 1;
            state.cyclePeak = std::fabs(x);
         }
         else if (state.inPos >= kMaxCycleSamples)
         {
            // Degenerate: cycle exceeded max buffer without zero crossing (e.g. DC or sub-audio)
            for (int m = 0; m < state.inPos; m++)
               state.outBuffer[m] = state.inBuffer[m];
            state.outLen = state.inPos;
            state.outPos = 0;
            state.lastTailLen = 0;

            state.inBuffer[0] = x;
            state.inPos = 1;
            state.cyclePeak = std::fabs(x);
         }
         else
         {
            state.inBuffer[state.inPos++] = x;
            state.cyclePeak = std::max(state.cyclePeak, std::fabs(x));
         }

         float outSample = 0.0f;
         if (state.outPos < state.outLen)
            outSample = state.outBuffer[state.outPos++];

         outData[i] = DspMath::FlushDenormal(outSample);
         state.prevSample = x;
      }
   }

   for (int ch = numChannels; ch < out.numChannels; ch++)
   {
      for (int i = 0; i < out.numFrames; i++)
         out.channels[ch][i] = 0.0f;
   }
}

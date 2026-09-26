#include "SpecBlurKernel.h"

#include "nodes/AudioEffectNode.h"

void SpecBlurKernel::PrepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
   mSampleRate = sampleRate;
   mFft.Prepare(kLog2Fft);

   mWindow.resize(kFftSize);
   for (int n = 0; n < kFftSize; n++)
   {
      mWindow[n] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)n / (double)kFftSize)));
   }

   mRealScratch.assign(kNumBins, 0.0f);
   mImagScratch.assign(kNumBins, 0.0f);
   mTimeScratch.assign(kFftSize, 0.0f);

   for (int ch = 0; ch < kMaxChannels; ch++)
   {
      mChannels[ch].Init();
      mChannels[ch].rngState = 0x853c49e6u ^ (uint32_t)(ch * 0x1f3d5b79u);
   }

   Reset();
}

void SpecBlurKernel::Reset()
{
   for (int ch = 0; ch < kMaxChannels; ch++)
      mChannels[ch].Reset();
}

void SpecBlurKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;

   const float blurTimeMs = std::clamp(node.Param("blurTime"), 10.0f, 5000.0f);
   const float blurSec = blurTimeMs / 1000.0f;
   const float tilt = std::clamp(node.Param("tilt"), -1.0f, 1.0f);
   const float diffusion = std::clamp(node.Param("diffusion"), 0.0f, 1.0f);
   const bool freeze = node.Param("freeze") > 0.5f;
   const bool analog = node.Param("analog") > 0.5f;

   float tempAlpha[kNumBins + 1];
   for (int k = 0; k <= kNumBins; k++)
   {
      const float f_k = (float)k * (float)sampleRate / (float)kFftSize;
      const float fNorm = std::max(f_k, 20.0f) / 1000.0f;
      float tau_k = blurSec * powf(fNorm, tilt);
      tau_k = std::max(tau_k, 0.0001f);
      float alpha_k = expf(-(float)kHopSize / ((float)sampleRate * tau_k));
      tempAlpha[k] = std::clamp(alpha_k, 0.0f, 0.99999f);
   }

   mSeq.fetch_add(1, std::memory_order_release);
   memcpy(mAlphaTableTarget, tempAlpha, sizeof(tempAlpha));
   mDiffusionTarget = diffusion;
   mFreezeTarget = freeze ? 1 : 0;
   mAnalogTarget = analog ? 1 : 0;
   mSeq.fetch_add(1, std::memory_order_release);
}

void SpecBlurKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   // Seqlock snapshot
   uint32_t s0, s1;
   do {
      s0 = mSeq.load(std::memory_order_acquire);
      if (s0 & 1u) break;
      memcpy(mAlphaTableLocal, mAlphaTableTarget, sizeof(mAlphaTableLocal));
      mDiffusionLocal = mDiffusionTarget;
      mFreezeLocal = mFreezeTarget;
      mAnalogLocal = mAnalogTarget;
      s1 = mSeq.load(std::memory_order_acquire);
   } while (s0 != s1);

   const int numChannels = std::min({ in.numChannels, out.numChannels, kMaxChannels });
   const float twoPi = (float)(2.0 * M_PI);
   const float hopPhaseAdv = twoPi * (float)kHopSize / (float)kFftSize;
   const float normScale = 1.0f / ((float)kFftSize * 1.5f);

   for (int ch = 0; ch < numChannels; ch++)
   {
      ChannelState& state = mChannels[ch];
      const float* inData = in.channels[ch];
      float* outData = out.channels[ch];

      for (int i = 0; i < out.numFrames; i++)
      {
         const float x = inData[i];

         // Pop from output OLA buffer first to establish exact kFftSize (2048) latency
         const float yOut = state.outputOla[0];
         std::memmove(state.outputOla.data(), state.outputOla.data() + 1, (state.outputOla.size() - 1) * sizeof(float));
         state.outputOla.back() = 0.0f;
         outData[i] = DspMath::FlushDenormal(yOut);

         state.inputFifo[state.fifoSamples++] = x;

         if (state.fifoSamples >= kFftSize)
         {
            // Window analysis frame
            for (int n = 0; n < kFftSize; n++)
               mTimeScratch[n] = state.inputFifo[n] * mWindow[n];

            mFft.Forward(mTimeScratch.data(), kLog2Fft, mRealScratch.data(), mImagScratch.data());

            // Halve forward spectrum for standard DFT scaling
            for (int k = 0; k < kNumBins; k++)
            {
               mRealScratch[k] *= 0.5f;
               mImagScratch[k] *= 0.5f;
            }

            if (!mPassthroughOnly)
            {
               // 1. DC (real[0]) and Nyquist (imag[0]) as independent real magnitudes
               const float dcAlpha = mAlphaTableLocal[0];
               const float nyqAlpha = mAlphaTableLocal[kNumBins];
               const float magDc = std::fabs(mRealScratch[0]);
               const float magNyq = std::fabs(mImagScratch[0]);
               const float sgnDc = (mRealScratch[0] >= 0.0f ? 1.0f : -1.0f);
               const float sgnNyq = (mImagScratch[0] >= 0.0f ? 1.0f : -1.0f);

               if (!mFreezeLocal)
               {
                  state.magState[0] = dcAlpha * state.magState[0] + (1.0f - dcAlpha) * magDc;
                  state.magState[kNumBins] = nyqAlpha * state.magState[kNumBins] + (1.0f - nyqAlpha) * magNyq;
               }
               mRealScratch[0] = sgnDc * state.magState[0];
               mImagScratch[0] = sgnNyq * state.magState[kNumBins];

               // 2. Complex bins 1 .. kNumBins - 1
               for (int k = 1; k < kNumBins; k++)
               {
                  const float r = mRealScratch[k];
                  const float im = mImagScratch[k];
                  const float mag = sqrtf(r * r + im * im);
                  const float phase = atan2f(im, r);

                  const float alpha_k = mAlphaTableLocal[k];
                  const float expectedAdv = (float)k * hopPhaseAdv;

                  if (!mFreezeLocal)
                  {
                     state.magState[k] = alpha_k * state.magState[k] + (1.0f - alpha_k) * mag;

                     float phaseDev = phase - state.phasePrev[k] - expectedAdv;
                     while (phaseDev > (float)M_PI) phaseDev -= twoPi;
                     while (phaseDev <= -(float)M_PI) phaseDev += twoPi;

                     state.phasePrev[k] = phase;
                     state.deltaPhiFrozen[k] = expectedAdv + phaseDev;
                  }

                  state.rngState ^= state.rngState << 13;
                  state.rngState ^= state.rngState >> 17;
                  state.rngState ^= state.rngState << 5;
                  const float rngFloat = ((float)(state.rngState & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
                  const float jitter = mDiffusionLocal * rngFloat * (float)M_PI;

                  state.phaseSynth[k] += state.deltaPhiFrozen[k] + jitter;
                  while (state.phaseSynth[k] > (float)M_PI) state.phaseSynth[k] -= twoPi;
                  while (state.phaseSynth[k] <= -(float)M_PI) state.phaseSynth[k] += twoPi;

                  mRealScratch[k] = state.magState[k] * cosf(state.phaseSynth[k]);
                  mImagScratch[k] = state.magState[k] * sinf(state.phaseSynth[k]);
               }
            }

            mFft.Inverse(mRealScratch.data(), mImagScratch.data(), kLog2Fft, mTimeScratch.data());

            for (int n = 0; n < kFftSize; n++)
            {
               float y = mTimeScratch[n] * mWindow[n] * normScale;
               if (mAnalogLocal)
                  y = AnalogDsp::AsymTanh(y);
               state.outputOla[n] += y;
            }

            std::memmove(state.inputFifo.data(), state.inputFifo.data() + kHopSize, (kFftSize - kHopSize) * sizeof(float));
            state.fifoSamples = kFftSize - kHopSize;
         }
      }
   }

   for (int ch = numChannels; ch < out.numChannels; ch++)
   {
      for (int i = 0; i < out.numFrames; i++)
         out.channels[ch][i] = 0.0f;
   }
}

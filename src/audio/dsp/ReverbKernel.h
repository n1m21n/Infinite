#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "IEffectKernel.h"
#include "AnalogPrimitives.h"
#include "audio/ParamMailbox.h"

// Reverb's kernel - algorithmic FDN with pure digital and vintage modulated analog modes.
class AudioEffectNode;

namespace ReverbDsp
{
   constexpr int kNumLines = 8;

   constexpr int kBaseLengths44k[kNumLines] = { 1051, 1163, 1279, 1381, 1499, 1607, 1733, 1867 };

   inline void HadamardMix8(const float in[kNumLines], float out[kNumLines])
   {
      float h4a[4], h4b[4];
      {
         const float a = in[0], b = in[1], c = in[2], d = in[3];
         h4a[0] = a + b + c + d;
         h4a[1] = a - b + c - d;
         h4a[2] = a + b - c - d;
         h4a[3] = a - b - c + d;
      }
      {
         const float a = in[4], b = in[5], c = in[6], d = in[7];
         h4b[0] = a + b + c + d;
         h4b[1] = a - b + c - d;
         h4b[2] = a + b - c - d;
         h4b[3] = a - b - c + d;
      }
      static constexpr float kNorm = 0.35355339059f; // 1/sqrt(8)
      for (int i = 0; i < 4; i++)
      {
         out[i] = (h4a[i] + h4b[i]) * kNorm;
         out[4 + i] = (h4a[i] - h4b[i]) * kNorm;
      }
   }

   inline float FlushDenormal(float x) { return DspMath::FlushDenormal(x); }

   struct FdnLine
   {
      std::vector<float> buf;
      int capacity = 1;
      int writePos = 0;
      float dampState = 0.0f;

      void Prepare(int cap)
      {
         capacity = std::max(8, cap);
         buf.assign((size_t)capacity, 0.0f);
         writePos = 0;
         dampState = 0.0f;
      }

      void Reset()
      {
         std::fill(buf.begin(), buf.end(), 0.0f);
         writePos = 0;
         dampState = 0.0f;
      }

      float SampleAtDelay(int k) const
      {
         int p = writePos - 1 - k;
         p %= capacity;
         if (p < 0)
            p += capacity;
         return buf[(size_t)p];
      }

      float ReadAtDelay(int activeLen) const
      {
         const int len = std::clamp(activeLen, 1, capacity);
         int p = writePos - len;
         p %= capacity;
         if (p < 0)
            p += capacity;
         return buf[(size_t)p];
      }

      // 4-point Hermite cubic fractional read for analog tank modulation
      float Read(float delaySamples) const
      {
         const int iDelay = (int)delaySamples;
         const float frac = delaySamples - (float)iDelay;
         const float xm1 = SampleAtDelay(iDelay - 1);
         const float x0  = SampleAtDelay(iDelay);
         const float x1  = SampleAtDelay(iDelay + 1);
         const float x2  = SampleAtDelay(iDelay + 2);
         const float c0 = x0;
         const float c1 = 0.5f * (x1 - xm1);
         const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
         const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
         return ((c3 * frac + c2) * frac + c1) * frac + c0;
      }

      void Write(float v)
      {
         buf[(size_t)writePos] = v;
         writePos++;
         if (writePos >= capacity)
            writePos = 0;
      }
   };

   struct AllpassDiffuser
   {
      std::vector<float> buf;
      int size = 1;
      int pos = 0;
      float g = 0.7f;

      void Prepare(int samples, float gain)
      {
         size = std::max(4, samples);
         buf.assign((size_t)size, 0.0f);
         pos = 0;
         g = gain;
      }

      void Reset()
      {
         std::fill(buf.begin(), buf.end(), 0.0f);
         pos = 0;
      }

      float Process(float x)
      {
         const float delayed = buf[(size_t)pos];
         const float y = -g * x + delayed;
         buf[(size_t)pos] = FlushDenormal(x + g * y);
         pos++;
         if (pos >= size)
            pos = 0;
         return y;
      }
   };
}

class ReverbKernel : public IEffectKernel
{
public:
   enum ParamSlot
   {
      kSize = 0,
      kDecaySeconds,
      kDamping,
      kPredelayMs,
      kWidth,
      kNumSlots
   };

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate;
      mMailbox.PrepareToPlay(sampleRate);
      const float rateScale = (float)(sampleRate / 44100.0);

      for (int i = 0; i < ReverbDsp::kNumLines; i++)
      {
         const int cap = (int)std::ceil(ReverbDsp::kBaseLengths44k[i] * rateScale) + 64;
         mLines[i].Prepare(cap);
      }

      mDiffuser[0].Prepare((int)std::ceil(347 * rateScale), 0.7f);
      mDiffuser[1].Prepare((int)std::ceil(113 * rateScale), 0.7f);

      const int maxPredelaySamples = (int)std::ceil(0.5 * sampleRate) + 8;
      mPredelay.assign((size_t)std::max(8, maxPredelaySamples), 0.0f);
      mPredelayCapacity = (int)mPredelay.size();

      Reset();
   }

   void Reset() override
   {
      for (auto& line : mLines)
         line.Reset();
      for (auto& lfo : mLfo)
         lfo.Reset();
      mDiffuser[0].Reset();
      mDiffuser[1].Reset();
      std::fill(mPredelay.begin(), mPredelay.end(), 0.0f);
      mPredelayWrite = 0;
      mInputEnv = 0.0f;
   }

   void PushParams(const AudioEffectNode& node, double sampleRate) override;

   void ProcessBlock(const AudioBuffer& in, const AudioBuffer* sidechain, AudioBuffer& out) override;

   int LatencySamples() const override { return 0; }
   MeterRing* ExtraMeter() override { return &mLevelMeter; }

private:
   ParamMailbox mMailbox;
   double mSampleRate = 44100.0;
   std::atomic<int> mAnalog { 0 };

   ReverbDsp::FdnLine mLines[ReverbDsp::kNumLines];
   ReverbDsp::AllpassDiffuser mDiffuser[2];
   AnalogDsp::DriftLfo mLfo[ReverbDsp::kNumLines];
   float mInputEnv = 0.0f;

   std::vector<float> mPredelay;
   int mPredelayCapacity = 1;
   int mPredelayWrite = 0;

   MeterRing mLevelMeter;
};

#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// Measured-HRTF object renderer for one mono source -> two ears. Same per-sample
// shape as Binaural::Voice (the parametric model), so the mixer can hold either.
// Conventions as BinauralKernel.h: azimuth 0 front, +90 right, +-180 back;
// elevation +90 up; distance in metres.
namespace Hrtf
{
   // A grid of impulse responses resampled to the device rate. Built on the main
   // thread (Get), then only read by the audio thread; never mutated afterwards.
   struct Set
   {
      int numEl = 0, numAz = 0, taps = 0;
      float elMin = 0.0f, elStep = 1.0f, azStep = 1.0f;
      double sampleRate = 48000.0;
      std::vector<float> ir;    // [el][az][ear][taps]
      std::vector<float> delay; // [el][az][ear] samples
      const float* Ir(int e, int a, int ear) const { return &ir[(((size_t)e * numAz + a) * 2 + ear) * taps]; }
      float Delay(int e, int a, int ear) const { return delay[((size_t)e * numAz + a) * 2 + ear]; }

      // Main thread only (caches per sample rate). Never null.
      static std::shared_ptr<const Set> Get(double sampleRate);
   };

   class Voice
   {
   public:
      void Prepare(const Set* set, double sampleRate)
      {
         mSet = set;
         mSr = sampleRate;
         mTaps = set != nullptr ? set->taps : 0;
         mGlide = 1.0f - std::exp(-1.0f / (0.008f * (float)sampleRate));
         mHistLen = 1;
         while (mHistLen < mTaps + 1)
            mHistLen <<= 1;
         mHist.assign((size_t)mHistLen * 2, 0.0f);
         for (int e = 0; e < 2; e++)
         {
            mCur[e].assign((size_t)mTaps, 0.0f);
            mNxt[e].assign((size_t)mTaps, 0.0f);
         }
         Reset();
      }

      void Reset()
      {
         std::fill(mHist.begin(), mHist.end(), 0.0f);
         std::fill(std::begin(mEarRing[0]), std::end(mEarRing[0]), 0.0f);
         std::fill(std::begin(mEarRing[1]), std::end(mEarRing[1]), 0.0f);
         mWrite = 0;
         mEarWrite = 0;
         mAirState = 0.0f;
         mHave = false;
         mFadeLeft = 0;
      }

      // Once per block. nframes is the block the IR crossfade is spread over.
      void SetTarget(float azDeg, float elDeg, float distM, int nframes)
      {
         if (mSet == nullptr || mTaps == 0)
            return;
         const float dist = std::max(distM, 0.1f);
         mTgtGain = 1.0f / std::max(dist, 1.0f);
         mTgtAir = std::min(20000.0f / (1.0f + 0.12f * std::max(dist - 1.0f, 0.0f)), (float)(mSr * 0.45));

         const bool moved = !mHave || std::fabs(WrapDeg(azDeg - mAz)) > 0.01f || std::fabs(elDeg - mEl) > 0.01f;
         if (moved)
         {
            mAz = azDeg;
            mEl = elDeg;
            if (!mHave)
            {
               Interpolate(azDeg, elDeg, mCur, mCurDelay);
               mTgtDelay[0] = mDelay[0] = mCurDelay[0];
               mTgtDelay[1] = mDelay[1] = mCurDelay[1];
               mGain = mTgtGain;
               mAir = mTgtAir;
               mHave = true;
               mFadeLeft = 0;
            }
            else
            {
               if (mFadeLeft > 0)
               {
                  // Block shorter than expected: land the previous fade first.
                  std::swap(mCur[0], mNxt[0]);
                  std::swap(mCur[1], mNxt[1]);
                  mCurDelay[0] = mNxtDelay[0];
                  mCurDelay[1] = mNxtDelay[1];
               }
               Interpolate(azDeg, elDeg, mNxt, mNxtDelay);
               mTgtDelay[0] = mNxtDelay[0];
               mTgtDelay[1] = mNxtDelay[1];
               mFadeLen = std::max(nframes, 1);
               mFadeLeft = mFadeLen;
            }
         }
      }

      inline void Process(float in, float& outL, float& outR)
      {
         if (!mHave)
            return;
         // Slow cues glide per sample.
         mGain += mGlide * (mTgtGain - mGain);
         mAir += mGlide * (mTgtAir - mAir);
         for (int e = 0; e < 2; e++)
            mDelay[e] += mGlide * (mTgtDelay[e] - mDelay[e]);
         const float a = 1.0f - std::exp(-6.283185307f * std::clamp(mAir, 20.0f, (float)(mSr * 0.45)) / (float)mSr);
         mAirState += a * (in - mAirState);

         const unsigned w = mWrite & (mHistLen - 1);
         mHist[w] = mHist[w + mHistLen] = mAirState;
         // Newest sample is at w; the FIR reads w, w-1, ... = hist[w + mHistLen - k].
         const float* h = &mHist[w + mHistLen - (mTaps - 1)];
         float y[2];
         if (mFadeLeft > 0)
         {
            const float t = 1.0f - (float)mFadeLeft / (float)mFadeLen;
            for (int e = 0; e < 2; e++)
            {
               const float c = Dot(mCur[e].data(), h, mTaps);
               const float n = Dot(mNxt[e].data(), h, mTaps);
               y[e] = c + (n - c) * t;
            }
            if (--mFadeLeft == 0)
            {
               std::swap(mCur[0], mNxt[0]);
               std::swap(mCur[1], mNxt[1]);
               mCurDelay[0] = mNxtDelay[0];
               mCurDelay[1] = mNxtDelay[1];
            }
         }
         else
         {
            y[0] = Dot(mCur[0].data(), h, mTaps);
            y[1] = Dot(mCur[1].data(), h, mTaps);
         }
         mWrite++;

         for (int e = 0; e < 2; e++)
         {
            mEarRing[e][mEarWrite & (kEarLen - 1)] = y[e];
            // Cubic (Catmull-Rom) fractional read: linear interpolation would
            // low-pass the far ear by a different amount at every azimuth.
            // One sample of base latency keeps the 4-point window in the past.
            const float d = std::clamp(mDelay[e] + 1.0f, 1.0f, (float)(kEarLen - 4));
            const int di = (int)d;
            const float fr = d - (float)di;
            const float* ring = mEarRing[e];
            const float p0 = ring[(mEarWrite - di + 1) & (kEarLen - 1)];
            const float p1 = ring[(mEarWrite - di) & (kEarLen - 1)];
            const float p2 = ring[(mEarWrite - di - 1) & (kEarLen - 1)];
            const float p3 = ring[(mEarWrite - di - 2) & (kEarLen - 1)];
            const float v = 0.5f * (2.0f * p1 + (p2 - p0) * fr + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * fr * fr +
                                    (3.0f * p1 - p0 - 3.0f * p2 + p3) * fr * fr * fr);
            (e == 0 ? outL : outR) += v * mGain;
         }
         mEarWrite++;
      }

   private:
      static constexpr int kEarLen = 256;

      static float WrapDeg(float d)
      {
         d = std::fmod(d + 180.0f, 360.0f);
         if (d < 0.0f)
            d += 360.0f;
         return d - 180.0f;
      }

      // Taps are stored reversed so the dot product walks both arrays forward:
      // ir[j] pairs with the sample (taps-1-j) back from the newest.
      static float Dot(const float* ir, const float* hist, int n)
      {
         float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
         int i = 0;
         for (; i + 4 <= n; i += 4)
         {
            s0 += ir[i] * hist[i];
            s1 += ir[i + 1] * hist[i + 1];
            s2 += ir[i + 2] * hist[i + 2];
            s3 += ir[i + 3] * hist[i + 3];
         }
         for (; i < n; i++)
            s0 += ir[i] * hist[i];
         return (s0 + s1) + (s2 + s3);
      }

      void Interpolate(float azDeg, float elDeg, std::vector<float> out[2], float outDelay[2]) const
      {
         const Set& s = *mSet;
         const float ef = std::clamp((elDeg - s.elMin) / s.elStep, 0.0f, (float)(s.numEl - 1));
         const int e0 = std::min((int)ef, s.numEl - 1), e1 = std::min(e0 + 1, s.numEl - 1);
         const float we = ef - (float)e0;
         float az = std::fmod(azDeg, 360.0f);
         if (az < 0.0f)
            az += 360.0f;
         const float af = az / s.azStep;
         const int a0 = (int)af % s.numAz, a1 = (a0 + 1) % s.numAz;
         const float wa = af - std::floor(af);
         const int ei[4] = { e0, e0, e1, e1 };
         const int ai[4] = { a0, a1, a0, a1 };
         const float wt[4] = { (1 - we) * (1 - wa), (1 - we) * wa, we * (1 - wa), we * wa };
         for (int ear = 0; ear < 2; ear++)
         {
            float* dst = out[ear].data();
            std::fill(dst, dst + mTaps, 0.0f);
            float d = 0.0f;
            for (int k = 0; k < 4; k++)
            {
               const float* src = s.Ir(ei[k], ai[k], ear);
               const float w = wt[k];
               for (int j = 0; j < mTaps; j++)
                  dst[mTaps - 1 - j] += w * src[j]; // reversed
               d += w * s.Delay(ei[k], ai[k], ear);
            }
            outDelay[ear] = d;
         }
      }

      const Set* mSet = nullptr;
      double mSr = 48000.0;
      int mTaps = 0;
      unsigned mHistLen = 1, mWrite = 0, mEarWrite = 0;
      float mGlide = 0.002f;
      std::vector<float> mHist;
      std::vector<float> mCur[2], mNxt[2];
      float mCurDelay[2] = {}, mNxtDelay[2] = {};
      float mEarRing[2][kEarLen] = {};
      float mDelay[2] = {}, mTgtDelay[2] = {};
      float mGain = 1.0f, mTgtGain = 1.0f, mAir = 20000.0f, mTgtAir = 20000.0f, mAirState = 0.0f;
      float mAz = 0.0f, mEl = 0.0f;
      int mFadeLeft = 0, mFadeLen = 1;
      bool mHave = false;
   };
}

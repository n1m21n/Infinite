#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/FpContract.h"

// Linear-phase oversampler for running a nonlinearity at F times the sample
// rate (F = 2, 4 or 8), per channel, built as a cascade of 2x half-band
// stages - the standard efficient structure: only the first stage, at the
// lowest rate, needs a sharp filter; each later stage only has to keep its
// images out of the audible band, so it is short.
//
//   Up():   one base-rate sample -> F samples at F x rate.
//   Down(): F samples at F x rate -> one base-rate sample.
//
// Each stage is a Kaiser-windowed half-band sinc (every other tap is zero,
// so it costs half its length) with an integer group delay gd. A stage's up
// + down round trip delays its outer-rate signal by exactly gd samples, and
// the gd values are chosen so every stage's share is a whole number of base
// samples: total latency is 64, 70 and 73 base samples at 2x, 4x and 8x.
//
//   stage 1 (fs <-> 2fs):  gd 64, ~100 dB stopband, transition about
//                          +-2.2 kHz around fs/2 - flat to 20 kHz at 44.1
//                          and 48 kHz, and what would fold back below
//                          20 kHz is ~100 dB down.
//   stage 2 (2fs <-> 4fs): gd 12, only has to stop above 2fs - 20 kHz.
//   stage 3 (4fs <-> 8fs): gd 12, only has to stop above 4fs - 20 kHz.
//
// Real-time: Prepare() allocates (main thread). Up/Down only touch
// pre-sized buffers.
class Oversampler
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kMaxFactor = 8;

   // `firstStageGroupDelay` trades the first stage's sharpness for cost and
   // latency: 64 (the default) is flat to 20 kHz at 44.1 kHz with ~100 dB of
   // stopband; a synth voice, where folding into 20-24 kHz is acceptable and
   // up to 16 instances run at once, can use 32.
   void Prepare(int factor, int firstStageGroupDelay = 64)
   {
      mFactor = (factor >= 8) ? 8 : (factor >= 4 ? 4 : 2);
      mStages = (mFactor == 8) ? 3 : (mFactor == 4 ? 2 : 1);
      const int kGroupDelay[3] = { std::max(8, firstStageGroupDelay), 12, 12 };
      static constexpr double kBeta[3] = { 10.0, 10.0, 10.0 };
      mLatency = 0;
      for (int s = 0; s < mStages; s++)
      {
         for (int ch = 0; ch < kMaxChannels; ch++)
            mStage[s][ch].Prepare(kGroupDelay[s], kBeta[s]);
         mLatency += kGroupDelay[s] >> s; // gd outer-rate samples = gd / 2^s base samples
      }
      Reset();
   }

   void Reset()
   {
      for (int s = 0; s < 3; s++)
         for (int ch = 0; ch < kMaxChannels; ch++)
            mStage[s][ch].Reset();
   }

   // One channel only - for a signal path that starts up mid-stream and
   // must not resume from stale history.
   void ResetChannel(int ch)
   {
      for (int s = 0; s < 3; s++)
         mStage[s][ch].Reset();
   }

   int Factor() const { return mFactor; }
   int Latency() const { return mLatency; }

   // Writes Factor() samples to `os`.
   void Up(int ch, float x, float* os)
   {
      float a[kMaxFactor], b[kMaxFactor];
      a[0] = x;
      int n = 1;
      for (int s = 0; s < mStages; s++)
      {
         for (int i = 0; i < n; i++)
            mStage[s][ch].Up(a[i], &b[2 * i]);
         n *= 2;
         std::copy(b, b + n, a);
      }
      std::copy(a, a + n, os);
   }

   // Reads Factor() samples from `os`, returns one base-rate sample.
   float Down(int ch, const float* os)
   {
      float a[kMaxFactor];
      std::copy(os, os + mFactor, a);
      int n = mFactor;
      for (int s = mStages - 1; s >= 0; s--)
      {
         n /= 2;
         for (int i = 0; i < n; i++)
            a[i] = mStage[s][ch].Down(&a[2 * i]);
      }
      return a[0];
   }

   // One 2x half-band stage, one channel. History buffers are mirrored
   // (every sample written twice, S apart) so each filter reads one
   // contiguous window - no per-tap index masking, so the loops vectorise.
   // Public so the four-voice lane version (VoiceLanes.h) runs the very
   // same taps.
   class HalfbandStage
   {
   public:
      int GroupDelay() const { return mGd; }
      float Centre() const { return mCentre; }
      const std::vector<float>& OddTaps() const { return mOdd; }
      const std::vector<int>& OddIndex() const { return mOddIndex; }
      const std::vector<float>& UpTaps() const { return mUpOdd; }
      int OddParity() const { return mOddParity; }
      int UpSize() const { return mUpSize; }
      int DownSize() const { return mDownSize; }

      void Prepare(int groupDelay, double beta)
      {
         mGd = groupDelay;
         const int len = 2 * mGd + 1;
         std::vector<double> h((size_t)len);
         double total = 0.0;
         for (int i = 0; i < len; i++)
         {
            const int d = i - mGd;
            const double x = 0.5 * d; // half-band: cutoff at pi/2 of the inner rate
            const double sinc = (d == 0) ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            const double r = (double)d / (double)(mGd + 1);
            const double win = BesselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / BesselI0(beta);
            h[(size_t)i] = ((d % 2) == 0 && d != 0) ? 0.0 : sinc * win; // exact half-band zeros
            total += h[(size_t)i];
         }
         for (double& v : h)
            v /= total;
         mCentre = (float)h[(size_t)mGd];

         // Odd-offset taps, ordered by increasing index i (offsets -gd+1 ..
         // gd-1 step 2 when gd is even, -gd .. gd when gd is odd).
         mOdd.clear();
         mOddIndex.clear();
         for (int i = 0; i < len; i++)
            if (((i - mGd) % 2) != 0)
            {
               mOdd.push_back((float)h[(size_t)i]);
               mOddIndex.push_back(i);
            }

         // Interpolation: inner sample 2n + r takes taps i == r (mod 2) of
         // the zero-stuffed input, gain 2. The phase holding the centre
         // (parity of gd) is just a delay; the other is the odd taps.
         mUpSpan = mOddIndex.back() / 2 + 1;
         mUpOdd.assign((size_t)mUpSpan, 0.0f);
         mOddParity = mOddIndex.front() & 1;
         for (size_t k = 0; k < mOddIndex.size(); k++)
            mUpOdd[(size_t)(mOddIndex[k] / 2)] = 2.0f * mOdd[k];

         mUpSize = std::max(mUpSpan, mGd / 2 + 1) + 1;
         mUpHist.assign((size_t)mUpSize * 2, 0.0f);
         mDownSize = len + 2;
         mDownHist.assign((size_t)mDownSize * 2, 0.0f);
         Reset();
      }

      void Reset()
      {
         std::fill(mUpHist.begin(), mUpHist.end(), 0.0f);
         std::fill(mDownHist.begin(), mDownHist.end(), 0.0f);
         mUpPos = 0;
         mDownPos = 0;
      }

      // out[r] = 2 * sum_j h[r + 2j] * x[n - j]
      void Up(float x, float* out)
      {
         INFINITE_NO_FMA
         mUpHist[(size_t)mUpPos] = x;
         mUpHist[(size_t)(mUpPos + mUpSize)] = x;
         // newest at w[0], older at w[-1], w[-2], ...
         const float* w = &mUpHist[(size_t)(mUpPos + mUpSize)];
         float acc = 0.0f;
         for (int j = 0; j < mUpSpan; j++)
            acc += mUpOdd[(size_t)j] * w[-j];
         const float delayed = 2.0f * mCentre * w[-(mGd / 2)];
         out[mOddParity] = acc;
         out[mOddParity ^ 1] = delayed;
         if (++mUpPos >= mUpSize)
            mUpPos = 0;
      }

      // y = sum_i h[i] * v[2n - i], v[2n] = in[0], v[2n+1] = in[1].
      float Down(const float* in)
      {
         INFINITE_NO_FMA
         // Push in[0] then in[1]; v[2n] then sits one behind the newest.
         Push(in[0]);
         Push(in[1]);
         const float* v0 = &mDownHist[(size_t)(mDownPos + mDownSize - 2)]; // v[2n]
         float acc = mCentre * v0[-mGd];
         for (size_t k = 0; k < mOddIndex.size(); k++)
            acc += mOdd[k] * v0[-mOddIndex[k]];
         return acc;
      }

   private:
      void Push(float v)
      {
         mDownHist[(size_t)mDownPos] = v;
         mDownHist[(size_t)(mDownPos + mDownSize)] = v;
         if (++mDownPos >= mDownSize)
            mDownPos = 0;
      }

      int mGd = 64;
      float mCentre = 0.5f;
      std::vector<float> mOdd;
      std::vector<int> mOddIndex;
      std::vector<float> mUpOdd;
      int mUpSpan = 0;
      int mOddParity = 1;
      std::vector<float> mUpHist, mDownHist;
      int mUpSize = 0, mDownSize = 0;
      int mUpPos = 0, mDownPos = 0;
   };

   int Stages() const { return mStages; }
   const HalfbandStage& Stage(int s) const { return mStage[s][0]; }

private:
   static double BesselI0(double x)
   {
      double sum = 1.0, term = 1.0;
      for (int k = 1; k < 40; k++)
      {
         term *= (x / (2.0 * k)) * (x / (2.0 * k));
         sum += term;
      }
      return sum;
   }

   int mFactor = 4;
   int mStages = 2;
   int mLatency = 70;
   HalfbandStage mStage[3][kMaxChannels];
};

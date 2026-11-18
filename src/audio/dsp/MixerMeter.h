#pragma once
// Master metering for the Mixer panel (internal/plans/mixer-panel B4): ITU-R BS.1770 loudness
// (momentary 400 ms, short-term 3 s, gated integrated) and 4x oversampled true peak.
// Process() runs on one thread (the audio thread for the master strip, the main thread for a
// take); the readouts are atomics, so any thread can read them. No allocation after construction.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

class MixerMeter
{
public:
   static constexpr float kSilent = -120.0f; // readout for "nothing measured yet"

   // Usable before Prepare (a headless engine never calls PrepareToPlay): 48 kHz until told otherwise.
   MixerMeter() { Prepare(48000.0); }

   void Prepare(double sampleRate)
   {
      mRate = sampleRate > 0.0 ? sampleRate : 48000.0;
      mBlockLen = std::max(1, (int)std::lround(mRate * 0.1));
      // K-weighting: a high shelf, then a high-pass (BS.1770 analogue prototypes, bilinear at this rate).
      {
         const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
         const double K = std::tan(kPi * f0 / mRate), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
         const double a0 = 1.0 + K / Q + K * K;
         mShelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                    2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
      }
      {
         const double f0 = 38.13547087602444, Q = 0.5003270373238773;
         const double K = std::tan(kPi * f0 / mRate);
         const double a0 = 1.0 + K / Q + K * K;
         mHigh = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
      }
      // True peak: a 48-tap Kaiser-windowed sinc, split into four phases of 12 taps. Phase 0 is the
      // sample itself, so only phases 1..3 are filtered.
      for (int p = 1; p < kPhases; p++)
      {
         double sum = 0.0;
         for (int k = 0; k < kTaps; k++)
         {
            const double n = (double)(k * kPhases + p) - (double)(kTaps * kPhases) * 0.5;
            const double x = kPi * n / (double)kPhases;
            const double r = n / ((double)(kTaps * kPhases) * 0.5);
            const double w = std::fabs(r) >= 1.0 ? 0.0 : BesselI0(8.0 * std::sqrt(1.0 - r * r)) / BesselI0(8.0);
            mTap[p][k] = (std::fabs(x) < 1e-12 ? 1.0 : std::sin(x) / x) * w;
            sum += mTap[p][k];
         }
         for (int k = 0; k < kTaps; k++)
            mTap[p][k] /= sum; // unity at DC
      }
      Clear();
   }

   // Any thread: the next Process() starts from nothing.
   void Reset() { mResetRequest.store(true, std::memory_order_relaxed); }

   // Planar stereo; r may be null for a mono signal.
   void Process(const float* l, const float* r, int frames)
   {
      if (mResetRequest.exchange(false, std::memory_order_relaxed))
         Clear();
      const float* in[2] = { l, r };
      const int chans = r != nullptr ? 2 : 1;
      for (int i = 0; i < frames; i++)
      {
         for (int c = 0; c < chans; c++)
         {
            const double x = (double)in[c][i];
            // True peak: newest sample at mPos, read backwards.
            double* h = mHist[c];
            h[mPos] = x;
            double pk = std::fabs(x);
            for (int p = 1; p < kPhases; p++)
            {
               double y = 0.0;
               for (int k = 0; k < kTaps; k++)
                  y += mTap[p][k] * h[(mPos - k) & (kHist - 1)];
               pk = std::max(pk, std::fabs(y));
            }
            mPeak = std::max(mPeak, pk);
            // K-weighted energy.
            const double s = mShelfState[c].Run(mShelf, x);
            const double k = mHighState[c].Run(mHigh, s);
            mAcc += k * k;
         }
         mPos = (mPos + 1) & (kHist - 1);
         if (++mFill >= mBlockLen)
            FinishBlock();
      }
      mTruePeakDb.store(mPeak > 1e-6 ? (float)(20.0 * std::log10(mPeak)) : kSilent, std::memory_order_relaxed);
   }

   float Momentary() const { return mMomentary.load(std::memory_order_relaxed); }
   float ShortTerm() const { return mShort.load(std::memory_order_relaxed); }
   float Integrated() const { return mIntegrated.load(std::memory_order_relaxed); }
   float TruePeakDb() const { return mTruePeakDb.load(std::memory_order_relaxed); }

private:
   static constexpr double kPi = 3.14159265358979323846;
   static constexpr int kPhases = 4, kTaps = 12, kHist = 16; // kHist: power of two >= kTaps
   static constexpr int kRing = 30;                          // 3 s of 100 ms blocks
   static constexpr int kBins = 1000;                        // -70 .. +30 LUFS in 0.1 LU steps

   struct Coefs { double b0, b1, b2, a1, a2; };
   struct Biquad
   {
      double z1 = 0.0, z2 = 0.0;
      double Run(const Coefs& c, double x)
      {
         const double y = c.b0 * x + z1;
         z1 = c.b1 * x - c.a1 * y + z2;
         z2 = c.b2 * x - c.a2 * y;
         return y;
      }
   };

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

   static float Lufs(double meanSquare)
   {
      return meanSquare > 1e-12 ? (float)(-0.691 + 10.0 * std::log10(meanSquare)) : kSilent;
   }

   void Clear()
   {
      for (int c = 0; c < 2; c++)
      {
         std::fill(std::begin(mHist[c]), std::end(mHist[c]), 0.0);
         mShelfState[c] = Biquad();
         mHighState[c] = Biquad();
      }
      std::fill(std::begin(mBlock), std::end(mBlock), 0.0);
      std::fill(std::begin(mBinCount), std::end(mBinCount), 0u);
      std::fill(std::begin(mBinEnergy), std::end(mBinEnergy), 0.0);
      mPos = mFill = mBlocks = 0;
      mAcc = mPeak = 0.0;
      mMomentary.store(kSilent, std::memory_order_relaxed);
      mShort.store(kSilent, std::memory_order_relaxed);
      mIntegrated.store(kSilent, std::memory_order_relaxed);
      mTruePeakDb.store(kSilent, std::memory_order_relaxed);
   }

   double MeanOfLast(int n) const
   {
      n = std::min(n, std::min(mBlocks, kRing));
      double sum = 0.0;
      for (int k = 0; k < n; k++)
         sum += mBlock[(mBlocks - 1 - k) % kRing];
      return n > 0 ? sum / (double)n : 0.0;
   }

   // One 100 ms block done: its energy is the channels' mean squares added together.
   void FinishBlock()
   {
      mBlock[mBlocks % kRing] = mAcc / (double)mFill;
      mBlocks++;
      mAcc = 0.0;
      mFill = 0;
      const double m = MeanOfLast(4);
      mMomentary.store(Lufs(m), std::memory_order_relaxed);
      mShort.store(Lufs(MeanOfLast(kRing)), std::memory_order_relaxed);
      if (mBlocks < 4)
         return;
      // Integrated: every 400 ms block (75 % overlap) above -70 LUFS, then those within 10 LU of their mean.
      const float lm = Lufs(m);
      if (lm >= -70.0f)
      {
         const int bin = std::clamp((int)std::floor(((double)lm + 70.0) * 10.0), 0, kBins - 1);
         mBinCount[bin]++;
         mBinEnergy[bin] += m;
      }
      double sum = 0.0;
      unsigned count = 0;
      for (int b = 0; b < kBins; b++)
      {
         sum += mBinEnergy[b];
         count += mBinCount[b];
      }
      if (count == 0)
      {
         mIntegrated.store(kSilent, std::memory_order_relaxed);
         return;
      }
      const double gate = (double)Lufs(sum / (double)count) - 10.0;
      const int first = std::clamp((int)std::floor((gate + 70.0) * 10.0), 0, kBins - 1);
      sum = 0.0;
      count = 0;
      for (int b = first; b < kBins; b++)
      {
         sum += mBinEnergy[b];
         count += mBinCount[b];
      }
      mIntegrated.store(count > 0 ? Lufs(sum / (double)count) : kSilent, std::memory_order_relaxed);
   }

   double mRate = 48000.0;
   int mBlockLen = 4800;
   Coefs mShelf {}, mHigh {};
   double mTap[kPhases][kTaps] = {};
   double mHist[2][kHist] = {};
   Biquad mShelfState[2], mHighState[2];
   double mBlock[kRing] = {};
   unsigned mBinCount[kBins] = {};
   double mBinEnergy[kBins] = {};
   int mPos = 0, mFill = 0, mBlocks = 0;
   double mAcc = 0.0, mPeak = 0.0;
   std::atomic<bool> mResetRequest { false };
   std::atomic<float> mMomentary { kSilent }, mShort { kSilent }, mIntegrated { kSilent }, mTruePeakDb { kSilent };
};

#pragma once

#include <algorithm>
#include <cmath>

// Minimum-phase oversampler for running a nonlinearity at F times the sample
// rate (F = 2, 4 or 8), per channel. Same job and same call shape as
// Oversampler.h, with the other trade: Oversampler is linear phase and costs
// 73 samples of latency at 8x; this one costs 5 or 6 (0.12 ms), and pays
// for it with a delay that is not the same at every frequency (it rises
// towards 20 kHz, by a fraction of a millisecond).
//
//   Up():   one base-rate sample -> F samples at F x rate.
//   Down(): F samples at F x rate -> one base-rate sample.
//
// Each 2x stage is a polyphase half-band built from two chains of
// first-order allpass sections, H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2
// (Valenzuela & Constantinides, "Digital signal processing schemes for
// efficient interpolation and decimation", IEE Proc. G, 1983). The two
// chains are power complementary, so the passband is flat to rounding and
// the stopband is an elliptic one; the coefficients come from the closed-form
// elliptic design for that structure, computed in Prepare() for the actual
// sample rate:
//
//   stage 1 (fs <-> 2fs):  passes 20 kHz, stops above fs - 20 kHz
//   stage 2 (2fs <-> 4fs): stops above 2fs - 20 kHz
//   stage 3 (4fs <-> 8fs): stops above 4fs - 20 kHz
//
// each with at least kStopbandDb of rejection, using as few sections as that
// takes (so a higher sample rate, whose transition bands are wider, costs
// less and delays less).
//
// Latency() is the low-frequency delay of an Up() + Down() round trip,
// rounded to whole base samples - what a parallel path has to be delayed by
// to stay in phase through the low and middle of the spectrum. A dry/wet mix
// inside one effect should not use it: run the dry signal through a second
// instance (Up() straight into Down()) and the two paths match at every
// frequency.
//
// Real-time: no allocation anywhere. Prepare() is main thread (it runs the
// design); Up/Down touch only this object's own state. State is double: the
// sharp first stage has coefficients close to 1.
class MinPhaseOversampler
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kMaxFactor = 8;
   static constexpr int kMaxSections = 16;
   static constexpr double kStopbandDb = 120.0;
   static constexpr double kPassbandHz = 20000.0;

   MinPhaseOversampler() { Prepare(kMaxFactor, 48000.0); }

   void Prepare(int factor, double sampleRate)
   {
      mFactor = (factor >= 8) ? 8 : (factor >= 4 ? 4 : 2);
      mStages = (mFactor == 8) ? 3 : (mFactor == 4 ? 2 : 1);
      double latency = 0.0;
      for (int s = 0; s < mStages; s++)
      {
         // Transition width as a fraction of the stage's outer rate: from
         // kPassbandHz up to (inner rate - kPassbandHz), which is centred on
         // a quarter of the outer rate as a half-band must be.
         const double outerRate = sampleRate * (double)(2 << s);
         const double transition = std::clamp(0.5 - 2.0 * kPassbandHz / outerRate, 0.01, 0.45);
         mStage[s].Design(transition, kStopbandDb);
         // Up delays by `delay` inner-rate samples and so does Down, less the
         // half sample its z^-1 branch already accounts for: the pair it
         // reads is the pair Up wrote.
         latency += (2.0 * mStage[s].delay - 0.5) / (double)(1 << s);
      }
      mLatency = (int)std::lround(latency);
      Reset();
   }

   void Reset()
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
         ResetChannel(ch);
   }

   // One channel only - for a signal path that starts up mid-stream and
   // must not resume from stale history.
   void ResetChannel(int ch)
   {
      for (int s = 0; s < 3; s++)
      {
         mUp[s][ch] = State();
         mDown[s][ch] = State();
      }
   }

   int Factor() const { return mFactor; }
   int Latency() const { return mLatency; }

   // Sections in stage `s` (0 = the sharp one) - for tests and comments.
   int Sections(int s) const { return mStage[s].count; }

   // Writes Factor() samples to `os`.
   void Up(int ch, float x, float* os)
   {
      double a[kMaxFactor], b[kMaxFactor];
      a[0] = x;
      int n = 1;
      for (int s = 0; s < mStages; s++)
      {
         for (int i = 0; i < n; i++)
         {
            b[2 * i] = mStage[s].Chain(mUp[s][ch], 0, a[i]);
            b[2 * i + 1] = mStage[s].Chain(mUp[s][ch], 1, a[i]);
         }
         n *= 2;
         std::copy(b, b + n, a);
      }
      for (int i = 0; i < n; i++)
         os[i] = (float)a[i];
   }

   // Reads Factor() samples from `os`, returns one base-rate sample.
   float Down(int ch, const float* os)
   {
      double a[kMaxFactor];
      std::copy(os, os + mFactor, a);
      int n = mFactor;
      for (int s = mStages - 1; s >= 0; s--)
      {
         n /= 2;
         // The z^-1 branch (odd sections) takes the earlier of the pair.
         for (int i = 0; i < n; i++)
            a[i] = 0.5 * (mStage[s].Chain(mDown[s][ch], 0, a[2 * i + 1]) + mStage[s].Chain(mDown[s][ch], 1, a[2 * i]));
      }
      return (float)a[0];
   }

private:
   struct State
   {
      double x1[kMaxSections] = {};
      double y1[kMaxSections] = {};
   };

   struct Stage
   {
      int count = 1;
      double coef[kMaxSections] = {};
      double delay = 0.5; // low-frequency delay of one direction, inner-rate samples

      // Sections `first`, first + 2, ... in series: each is the allpass
      // (a + z^-1) / (1 + a z^-1) at the inner rate.
      double Chain(State& st, int first, double x) const
      {
         for (int i = first; i < count; i += 2)
         {
            const double y = coef[i] * (x - st.y1[i]) + st.x1[i];
            st.x1[i] = x;
            st.y1[i] = y;
            x = y;
         }
         return x;
      }

      // Closed-form elliptic half-band: `transition` is the width of the
      // transition band as a fraction of the outer rate; the section count
      // is the smallest that reaches `stopbandDb`.
      void Design(double transition, double stopbandDb)
      {
         double k = std::tan((1.0 - 2.0 * transition) * M_PI / 4.0);
         k *= k;
         const double kk = std::sqrt(std::sqrt(1.0 - k * k));
         const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
         const double e4 = e * e * e * e;
         const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4))); // elliptic nome

         // Rejection of an n-section design: -5 (2n + 1) log10(q) - 6.02 dB.
         const double perOrder = -5.0 * std::log10(q);
         count = (int)std::ceil(((stopbandDb + 6.02) / perOrder - 1.0) * 0.5);
         count = std::clamp(count, 1, kMaxSections);

         const int order = 2 * count + 1;
         for (int index = 0; index < count; index++)
         {
            const double c = (double)(index + 1) * M_PI / (double)order;
            double num = 0.0, sign = 1.0;
            for (int i = 0; i < 64; i++)
            {
               const double term = std::pow(q, (double)(i * (i + 1))) * std::sin((double)(2 * i + 1) * c) * sign;
               num += term;
               sign = -sign;
               if (std::fabs(term) < 1.0e-100)
                  break;
            }
            num *= std::sqrt(std::sqrt(q));
            double den = 0.5;
            sign = -1.0;
            for (int i = 1; i < 64; i++)
            {
               const double term = std::pow(q, (double)(i * i)) * std::cos((double)(2 * i) * c) * sign;
               den += term;
               sign = -sign;
               if (std::fabs(term) < 1.0e-100)
                  break;
            }
            const double ww = (num / den) * (num / den);
            const double x = std::sqrt((1.0 - ww * k) * (1.0 - ww / k)) / (1.0 + ww);
            coef[index] = (1.0 - x) / (1.0 + x);
         }

         // Each section delays DC by (1 - a) / (1 + a) inner-rate samples;
         // the z^-1 branch adds half of one. The two branches agree at DC.
         double even = 0.0, odd = 0.5;
         for (int i = 0; i < count; i++)
            ((i & 1) ? odd : even) += (1.0 - coef[i]) / (1.0 + coef[i]);
         delay = 0.5 * (even + odd);
      }
   };

   int mFactor = 8;
   int mStages = 3;
   int mLatency = 5;
   Stage mStage[3];
   State mUp[3][kMaxChannels];
   State mDown[3][kMaxChannels];
};

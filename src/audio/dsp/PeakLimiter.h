#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Linked look-ahead peak limiter with a 4x true-peak detector, for up to
// kMaxChannels channels sharing one gain (so the stereo image never shifts).
//
// The guarantee, and why it holds. Per input sample k the detector gives a
// level L[k] (the sample itself and the inter-sample peaks on either side of
// it), and t[k] = min(1, ceiling / L[k]) is the most gain that sample may
// take. The gain path is
//
//   w[n] = min of the last W t-values        hold
//   e[n] = w[n] if it is lower than e[n-1],  instant attack
//          else e[n-1] eased up toward w[n]  release (never above w[n])
//   g[n] = mean of the last W e-values       box ramp
//
// and the audio is delayed by D = (W - 1) + kTpLag, so the sample leaving at
// time n is exactly the one whose t-value sits inside every one of the W
// hold windows the box average reads. Each averaged e-value is therefore at
// most that sample's t, and so is their mean: the output sample peak never
// exceeds the ceiling (up to float rounding), and the attack is a straight
// ramp across the look-ahead window instead of a click. This is the
// topology SpatialMaster::Limiter already uses on the master bus,
// generalised to N channels and to a polyphase detector.
//
// The old LimiterKernel smoothed its attack with a one-pole whose time
// constant equalled the look-ahead, which only covers 1 - 1/e (63%) of the
// needed reduction by the time the peak is output - a +6 dB transient left
// it about +2.7 dB over its ceiling.
//
// True peak: interpolated at 1/4, 1/2 and 3/4 between samples by a 16-tap
// Kaiser-windowed sinc per phase (the 4x oversampling ITU-R BS.1770-4 Annex 2
// specifies for true-peak metering; the coefficients here are designed in
// Prepare(), not copied). Like any 4x meter it under-reads a full-scale tone
// near Nyquist by up to ~0.5 dB; on programme material the residue is a few
// hundredths of a dB.
//
// Real-time: Prepare() allocates (main thread). ProcessFrame() touches only
// pre-sized buffers - no allocation, no locks.
class PeakLimiter
{
public:
   static constexpr int kMaxChannels = 8;
   static constexpr int kTpTaps = 16;         // taps per interpolation phase
   static constexpr int kTpPhases = 3;        // 1/4, 1/2, 3/4 (phase 0 is the sample)
   static constexpr int kTpLag = kTpTaps / 2; // future samples the interpolator reads

   void Prepare(double sampleRate, float lookaheadMs)
   {
      mWindow = std::max(2, (int)std::ceil(lookaheadMs * 0.001 * sampleRate) + 1);
      mLatency = (mWindow - 1) + kTpLag;
      int size = 1;
      while (size < std::max(mLatency + 1, std::max(kTpTaps, mWindow)))
         size <<= 1;
      mMask = (unsigned)size - 1;
      for (int ch = 0; ch < kMaxChannels; ch++)
         mHistory[ch].assign((size_t)size, 0.0f);
      mHoldIn.assign((size_t)size, 1.0f);
      mEnvelope.assign((size_t)size, 1.0f);
      BuildCoefficients();
      Reset();
   }

   void Reset()
   {
      for (int ch = 0; ch < kMaxChannels; ch++)
      {
         std::fill(mHistory[ch].begin(), mHistory[ch].end(), 0.0f);
         mPrevInterval[ch] = 0.0f;
      }
      std::fill(mHoldIn.begin(), mHoldIn.end(), 1.0f);
      std::fill(mEnvelope.begin(), mEnvelope.end(), 1.0f);
      mEnv = 1.0f;
      mGain = 1.0f;
      mPos = 0;
   }

   // Samples of delay between a frame going in and the same frame coming out.
   int Latency() const { return mLatency; }

   // Gain applied to the frame most recently output (1 = no reduction).
   float LastGain() const { return mGain; }

   // One frame, in place: io[ch] holds the input sample on entry and the
   // limited, delayed sample on return. `releaseCoeff` is the one-pole
   // coefficient exp(-1 / (releaseSeconds * sampleRate)).
   void ProcessFrame(float* io, int numChannels, float ceiling, float releaseCoeff)
   {
      numChannels = std::clamp(numChannels, 0, kMaxChannels);
      const unsigned m = mMask;
      const unsigned pos = mPos;

      float level = 0.0f;
      for (int ch = 0; ch < numChannels; ch++)
      {
         float* hist = mHistory[ch].data();
         hist[pos & m] = io[ch];

         // Interval (k, k + 1), k = pos - kTpLag, from samples k-7 .. k+8.
         float interval = 0.0f;
         for (int p = 0; p < kTpPhases; p++)
         {
            float acc = 0.0f;
            for (int i = 0; i < kTpTaps; i++)
               acc += hist[(pos - (unsigned)(kTpTaps - 1) + (unsigned)i) & m] * mCoef[p][i];
            interval = std::max(interval, std::fabs(acc));
         }
         const float atSample = std::fabs(hist[(pos - (unsigned)kTpLag) & m]);
         level = std::max(level, std::max(atSample, std::max(interval, mPrevInterval[ch])));
         mPrevInterval[ch] = interval;
      }

      mHoldIn[pos & m] = (level > ceiling && level > 1.0e-9f) ? ceiling / level : 1.0f;

      float hold = 1.0f;
      for (int i = 0; i < mWindow; i++)
         hold = std::min(hold, mHoldIn[(pos - (unsigned)i) & m]);

      if (hold < mEnv)
         mEnv = hold;
      else
         mEnv = hold + (mEnv - hold) * releaseCoeff;
      mEnvelope[pos & m] = mEnv;

      // Summed afresh every frame (W adds) rather than as a running sum, so
      // there is no accumulated rounding drift over a long session.
      double sum = 0.0;
      for (int i = 0; i < mWindow; i++)
         sum += mEnvelope[(pos - (unsigned)i) & m];
      mGain = (float)(sum / (double)mWindow);

      const unsigned outPos = (pos - (unsigned)mLatency) & m;
      for (int ch = 0; ch < numChannels; ch++)
         io[ch] = mHistory[ch][outPos] * mGain;

      mPos = pos + 1;
   }

private:
   static double BesselI0(double x)
   {
      double sum = 1.0, term = 1.0;
      for (int k = 1; k < 32; k++)
      {
         term *= (x / (2.0 * k)) * (x / (2.0 * k));
         sum += term;
      }
      return sum;
   }

   void BuildCoefficients()
   {
      constexpr double kBeta = 7.0;    // Kaiser shape: ~ -70 dB stopband
      constexpr double kCutoff = 0.95; // fraction of Nyquist
      const double half = kTpTaps * 0.5;
      for (int p = 0; p < kTpPhases; p++)
      {
         const double frac = (p + 1) * 0.25;
         double total = 0.0;
         for (int i = 0; i < kTpTaps; i++)
         {
            // Tap i is sample k + (i - (kTpLag - 1)); x is its distance from
            // the point being interpolated, k + frac.
            const double x = (double)(i - (kTpLag - 1)) - frac;
            const double arg = M_PI * kCutoff * x;
            const double sinc = (std::fabs(arg) < 1.0e-12) ? 1.0 : std::sin(arg) / arg;
            const double r = x / half;
            const double win = (std::fabs(r) >= 1.0) ? 0.0
                                                      : BesselI0(kBeta * std::sqrt(1.0 - r * r)) / BesselI0(kBeta);
            mCoef[p][i] = (float)(sinc * win);
            total += sinc * win;
         }
         // Unity gain at DC, so a constant reads as itself.
         for (int i = 0; i < kTpTaps; i++)
            mCoef[p][i] = (float)(mCoef[p][i] / total);
      }
   }

   int mWindow = 2;
   int mLatency = 1 + kTpLag;
   unsigned mMask = 0;
   unsigned mPos = 0;

   std::vector<float> mHistory[kMaxChannels]; // input history; also the audio delay line
   std::vector<float> mHoldIn;                // t[k]
   std::vector<float> mEnvelope;              // e[n]
   float mPrevInterval[kMaxChannels] = {};
   float mCoef[kTpPhases][kTpTaps] = {};
   float mEnv = 1.0f;
   float mGain = 1.0f;
};

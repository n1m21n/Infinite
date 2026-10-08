#pragma once

#include <algorithm>
#include <cmath>

// Parametric binaural object renderer for one mono source -> stereo ears.
//
// Interim stage of docs/plans/spatial (Block A): the cues are derived from a
// spherical-head model (Woodworth ITD, Rayleigh-style head shadow as a
// one-pole low-pass, a pinna high cut for sources behind, a mild elevation
// lift, 1/r distance + air absorption). A measured SOFA HRTF replaces
// ComputeTarget()'s mapping later; the smoothing, delay line and block API stay.
//
// Audio-thread safe: fixed-size state, no allocation, no locks. All coordinate
// conventions: azimuth 0 = front, +90 = right, 180 = back (degrees);
// elevation +90 = straight up; distance in metres.
namespace Binaural
{
   constexpr float kHeadRadius = 0.0875f; // m
   constexpr float kSpeedOfSound = 343.0f;
   constexpr int kDelayLen = 256;          // power of two, > max ITD in samples at 192 kHz (~0.7 ms = 135)

   struct EarTarget
   {
      float delaySamples = 0.0f;
      float gain = 1.0f;
      float headCutoff = 20000.0f; // head-shadow / air low-pass
   };

   struct Target
   {
      EarTarget ear[2];
      float backCut = 0.0f; // 0..1 pinna high-cut amount (sources behind)
      float lift = 0.0f;    // elevation lift amount (-1..1)
   };

   inline Target ComputeTarget(float azDeg, float elDeg, float distM, double sr)
   {
      const float d2r = 0.017453292519943295f;
      const float az = azDeg * d2r, el = elDeg * d2r;
      const float dist = std::max(distM, 0.1f);

      // Lateral angle: 0 on the median plane, +-pi/2 at the interaural axis.
      const float s = std::clamp(std::sin(az) * std::cos(el), -1.0f, 1.0f);
      const float lat = std::asin(s);
      const float absLat = std::fabs(lat);

      // Woodworth: ITD = (a / c) * (theta + sin theta).
      const float itd = (kHeadRadius / kSpeedOfSound) * (absLat + std::sin(absLat));

      Target t;
      const float shadow = std::sin(absLat);        // 0 front/back, 1 at the side
      const float airCut = 20000.0f / (1.0f + 0.12f * std::max(dist - 1.0f, 0.0f));
      const float distGain = 1.0f / std::max(dist, 1.0f);

      // Index 0 = left ear, 1 = right ear. Positive lat -> source on the right.
      for (int e = 0; e < 2; e++)
      {
         const bool near = (e == 1) ? (lat >= 0.0f) : (lat <= 0.0f);
         EarTarget& et = t.ear[e];
         et.delaySamples = near ? 0.0f : itd * (float)sr;
         // Far ear: head shadow darkens and attenuates; near ear gets a slight
         // bright boost (head reflection) so the pair spans ~10 dB at the side.
         et.gain = distGain * (near ? (1.0f + 0.2f * shadow) : (1.0f - 0.3f * shadow));
         const float shadowCut = near ? 20000.0f : 20000.0f * std::pow(0.1f, shadow); // ~1.6 kHz at 90 deg
         et.headCutoff = std::min(shadowCut, airCut);
      }
      t.backCut = std::clamp(-std::cos(az) * std::cos(el), 0.0f, 1.0f);
      t.lift = std::sin(el);
      return t;
   }

   class Voice
   {
   public:
      void Prepare(double sampleRate)
      {
         mSr = sampleRate;
         // ~8 ms glide on every derived cue; continuous in derived space so
         // azimuth wrap (179 -> -179) never sweeps through the opposite side.
         mGlide = 1.0f - std::exp(-1.0f / (0.008f * (float)sampleRate));
         mLp3k = OnePoleCoeff(3000.0f);
         mLp5k = OnePoleCoeff(5000.0f);
         Reset();
         mCur = Target();
         mHaveCur = false;
      }

      void Reset()
      {
         for (float& v : mDelay) v = 0.0f;
         mWrite = 0;
         for (int e = 0; e < 2; e++)
            mHead[e] = mBack[e] = mElev[e] = 0.0f;
         mHaveCur = false;
      }

      void SetTarget(const Target& t)
      {
         mTarget = t;
         if (!mHaveCur)
         {
            mCur = t;
            mHaveCur = true;
         }
      }

      // Renders one sample of mono input into the two ears (added to outL/outR).
      inline void Process(float in, float& outL, float& outR)
      {
         Glide();
         mDelay[mWrite & (kDelayLen - 1)] = in;
         for (int e = 0; e < 2; e++)
         {
            const EarTarget& et = mCur.ear[e];
            const float x = ReadDelayed(et.delaySamples);

            // Head shadow / air: one-pole low-pass at the smoothed cutoff.
            const float a = OnePoleCoeff(et.headCutoff);
            mHead[e] += a * (x - mHead[e]);
            float y = mHead[e];

            // Pinna cut for rear sources: blend toward a 3 kHz low-passed copy.
            mBack[e] += mLp3k * (y - mBack[e]);
            y = mBack[e] * (1.0f + 0.35f * mCur.backCut) + (y - mBack[e]) * (1.0f - 0.55f * mCur.backCut);

            // Elevation: lift (up) or dull (down) the band above ~5 kHz.
            mElev[e] += mLp5k * (y - mElev[e]);
            y = y + mCur.lift * 0.5f * (y - mElev[e]);

            (e == 0 ? outL : outR) += y * et.gain; // low band lifted, high band cut above: band-averaged loudness stays flat
         }
         mWrite++;
      }

   private:
      float OnePoleCoeff(float fc) const
      {
         const float f = std::clamp(fc, 20.0f, (float)(mSr * 0.45));
         return 1.0f - std::exp(-6.283185307f * f / (float)mSr);
      }

      void Glide()
      {
         for (int e = 0; e < 2; e++)
         {
            mCur.ear[e].delaySamples += mGlide * (mTarget.ear[e].delaySamples - mCur.ear[e].delaySamples);
            mCur.ear[e].gain += mGlide * (mTarget.ear[e].gain - mCur.ear[e].gain);
            mCur.ear[e].headCutoff += mGlide * (mTarget.ear[e].headCutoff - mCur.ear[e].headCutoff);
         }
         mCur.backCut += mGlide * (mTarget.backCut - mCur.backCut);
         mCur.lift += mGlide * (mTarget.lift - mCur.lift);
      }

      // Linear-interpolated read, `delay` samples behind the newest sample.
      inline float ReadDelayed(float delay) const
      {
         const float d = std::clamp(delay, 0.0f, (float)(kDelayLen - 2));
         const int di = (int)d;
         const float fr = d - (float)di;
         const float a = mDelay[(mWrite - di) & (kDelayLen - 1)];
         const float b = mDelay[(mWrite - di - 1) & (kDelayLen - 1)];
         return a + fr * (b - a);
      }

      double mSr = 48000.0;
      float mGlide = 0.002f, mLp3k = 0.3f, mLp5k = 0.4f;
      float mDelay[kDelayLen] = {};
      unsigned mWrite = 0;
      float mHead[2] = {}, mBack[2] = {}, mElev[2] = {};
      Target mCur, mTarget;
      bool mHaveCur = false;
   };
}

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// =========================================================================
// ColorStats — Online Photographic Color Statistics & Target Model
// (docs/plans/prediction/step-08-predictive-coloring.md)
//
// Maintains 32-bin histograms for R, G, B, Luma Y, Hue H, Saturation S,
// along with sample moments, quantiles/percentiles, and circular statistics.
// Persists the global learned profile across sessions with active-time half-life decay.
// =========================================================================

namespace ColorStats
{
   constexpr int kBins = 32;
   constexpr double kHalfLifeActiveSec = 14.0 * 24.0 * 3600.0; // 2 weeks of active footage

   // Photographic tone & color parameters (13 operators)
   struct ColorGradeParams
   {
      float rgbGainR = 1.0f;     // White balance R gain
      float rgbGainG = 1.0f;     // White balance G gain
      float rgbGainB = 1.0f;     // White balance B gain
      float exposure = 0.0f;     // Exposure in stops (multiplicative: 2^stops)
      float blackPoint = 0.0f;   // Levels black point (0..1)
      float whitePoint = 1.0f;   // Levels white point (0..1)
      float contrast = 1.0f;     // Contrast slope around pivot
      float pivot = 0.5f;        // Contrast pivot (e.g. median luma)
      float highlights = 0.0f;   // Highlights gain (-1..+1)
      float shadows = 0.0f;      // Shadows gain (-1..+1)
      float midtones = 1.0f;     // Midtones gamma (1.0 = neutral)
      float whites = 0.0f;       // Whites endpoint anchor (-1..+1)
      float blacks = 0.0f;       // Blacks endpoint anchor (-1..+1)
      float saturation = 1.0f;   // HSV saturation multiplier
      float vibrance = 0.0f;     // Non-uniform low-saturation boost (-1..+1)
      float hueShift = 0.0f;     // Hue shift in [0, 1) normalized circle
   };

   // A set of 6 histograms representing a frame or time-window of footage
   struct HistogramSet
   {
      uint32_t sampleCount = 0;

      // 32 bins each for R, G, B, Y (Luma), Saturation (S), Hue (H)
      float r[kBins] = { 0 };
      float g[kBins] = { 0 };
      float b[kBins] = { 0 };
      float y[kBins] = { 0 };
      float s[kBins] = { 0 };
      float h[kBins] = { 0 };

      // Moments
      double sumR = 0.0, sumG = 0.0, sumB = 0.0, sumY = 0.0, sumS = 0.0;
      double sumY2 = 0.0, sumS2 = 0.0;

      // Circular Hue components: sum(cos(2pi H)), sum(sin(2pi H))
      double sumCosH = 0.0, sumSinH = 0.0;

      void Clear();
      void AddPixel(float red, float green, float blue);

      float MeanR() const { return sampleCount > 0 ? (float)(sumR / sampleCount) : 0.5f; }
      float MeanG() const { return sampleCount > 0 ? (float)(sumG / sampleCount) : 0.5f; }
      float MeanB() const { return sampleCount > 0 ? (float)(sumB / sampleCount) : 0.5f; }
      float MeanY() const { return sampleCount > 0 ? (float)(sumY / sampleCount) : 0.5f; }
      float MeanS() const { return sampleCount > 0 ? (float)(sumS / sampleCount) : 0.0f; }

      float StdDevY() const
      {
         if (sampleCount < 2) return 0.2f;
         const double my = sumY / sampleCount;
         const double var = std::max(0.0, (sumY2 / sampleCount) - (my * my));
         return (float)std::sqrt(var);
      }

      float StdDevS() const
      {
         if (sampleCount < 2) return 0.1f;
         const double ms = sumS / sampleCount;
         const double var = std::max(0.0, (sumS2 / sampleCount) - (ms * ms));
         return (float)std::sqrt(var);
      }

      // Circular mean of hue in [0, 1)
      float CircularHue() const;

      // Quantile / Percentile query on Luma Y (p in [0, 1])
      float PercentileY(float p) const;
      // Quantile / Percentile on any given 32-bin histogram array
      static float Percentile(const float bins[kBins], float p);
   };

   // Learned target color profile
   class Profile
   {
   public:
      Profile();

      void Reset();
      void ResetToSelfNormalize();

      // Accumulate live evidence with exponential moving average and sample weight.
      // activeDtSec, when > 0, is the real active-time elapsed since the last call
      // and drives kHalfLifeActiveSec decay of mTotalSamples (see .cpp) - without
      // it, a profile fed once and never revisited would keep reporting the same
      // confidence forever regardless of how stale it has gone.
      void Accumulate(const HistogramSet& live, double weight = 1.0, double activeDtSec = 0.0);

      // Evaluate mathematical confidence in [0, 1]
      // Based on: sample mass, dynamic span/variance, and histogram stability
      float Confidence01() const;
      uint64_t TotalSamples() const { return mTotalSamples; }
      double AccumulatedActiveTimeSec() const { return mAccumulatedTimeSec; }

      // Target properties
      float TargetMeanY() const { return mTargetMeanY; }
      float TargetStdY() const { return mTargetStdY; }
      float TargetP01Y() const { return mTargetP01Y; }
      float TargetP50Y() const { return mTargetP50Y; }
      float TargetP99Y() const { return mTargetP99Y; }
      float TargetMeanR() const { return mTargetMeanR; }
      float TargetMeanG() const { return mTargetMeanG; }
      float TargetMeanB() const { return mTargetMeanB; }
      float TargetMeanS() const { return mTargetMeanS; }
      float TargetCircularHue() const { return mTargetCircularHue; }

      // Serialization
      std::vector<uint8_t> Serialize() const;
      bool Deserialize(const uint8_t* data, size_t size);

   private:
      uint64_t mTotalSamples = 0;
      double mAccumulatedTimeSec = 0.0;

      float mTargetMeanY = 0.45f;
      float mTargetStdY = 0.22f;
      float mTargetP01Y = 0.02f;
      float mTargetP50Y = 0.45f;
      float mTargetP99Y = 0.98f;

      float mTargetMeanR = 0.45f;
      float mTargetMeanG = 0.45f;
      float mTargetMeanB = 0.45f;
      float mTargetMeanS = 0.35f;
      float mTargetCircularHue = 0.0f;

      // Smoothed 32-bin histograms
      float mHistY[kBins] = { 0 };
      float mHistS[kBins] = { 0 };
      float mHistH[kBins] = { 0 };
      float mHistR[kBins] = { 0 };
      float mHistG[kBins] = { 0 };
      float mHistB[kBins] = { 0 };
   };

   // Global persistence engine (singleton)
   class Engine
   {
   public:
      static Engine& Instance();

      Profile& GlobalProfile() { return mProfile; }
      const Profile& GlobalProfile() const { return mProfile; }

      void Accumulate(const HistogramSet& live, double dt);

      bool Load(const std::string& directory);
      bool Save(const std::string& directory);

      // True once any Predictive Coloring node has fed this profile real
      // evidence - lets callers (periodic autosave, quit) skip writing a
      // freshly-defaulted file over a real one when nothing has learned yet.
      bool HasLearnedData() const { return mProfile.TotalSamples() > 0; }

   private:
      Engine();
      Profile mProfile;
      std::string mLastDir;
   };

   // Closed-form statistical fit of the 13 photographic operators (step-08 §3.2)
   ColorGradeParams Fit(const HistogramSet& live, const Profile& target, bool selfNormalize);

   // Clamps every field of a ColorGradeParams to a sane operating range (step-10 Task A: the
   // struct had no explicit bounds before the OU wander needed somewhere to reflect against).
   // hueShift wraps mod 1 rather than clamping, since it is a circular quantity.
   void ClampColorGradeParams(ColorGradeParams& p);

   // Advances a small Ornstein-Uhlenbeck offset, mean-reverting to zero, in ColorGradeParams
   // space (step-10 Task A §A1). Added on top of Fit()'s equilibrium so the same footage grades
   // slightly differently frame to frame instead of solving to one exact answer forever. `sigma`
   // gives one noise amplitude per dimension (zero disables wander on that dimension); `theta` is
   // the shared mean-reversion rate. `rng` is xorshift64* state, advanced in place so the caller
   // owns determinism (seed it from a saved param, never from global/time-based entropy).
   void StepColorWander(ColorGradeParams& offset, uint64_t& rng, float theta,
                         const ColorGradeParams& sigma, double dt);

   // File path helper
   std::string StatsPath(const std::string& logDir);
}

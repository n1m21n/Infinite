#include "ColorStats.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ColorStats
{
   namespace
   {
      // ---- RNG for StepColorWander: same splitmix64-seeded xorshift64* + Box-Muller pair as
      // DriftNode's simulation (src/nodes/PredictionNodes.cpp) - deliberately duplicated rather
      // than shared, since it's ~10 lines and pulling it into a shared header would couple this
      // module to the prediction-node layer for no benefit.
      uint64_t NextRaw(uint64_t& s)
      {
         s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
         return s * 0x2545F4914F6CDD1Dull;
      }
      float Uniform01(uint64_t& s)
      {
         return ((float)(NextRaw(s) >> 40) + 0.5f) * (1.0f / 16777216.0f);
      }
      float Gauss(uint64_t& s)
      {
         const float u1 = Uniform01(s), u2 = Uniform01(s);
         return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
      }

      float OUStep(float x, float theta, float sigma, double dt, uint64_t& rng)
      {
         if (theta <= 1e-6f || dt <= 0.0)
            return x;
         const float fdt = (float)dt;
         const float phi = std::exp(-theta * fdt);
         if (sigma <= 0.0f)
            return x * phi; // amplitude gated to zero (cold start / wander == 0): relax to equilibrium, no noise
         const float var = (sigma * sigma) * (1.0f - phi * phi) / (2.0f * theta);
         return x * phi + std::sqrt(std::max(var, 0.0f)) * Gauss(rng);
      }

      void RgbToHsv(float r, float g, float b, float& h, float& s, float& v)
      {
         float cmax = std::max(r, std::max(g, b));
         float cmin = std::min(r, std::min(g, b));
         float diff = cmax - cmin;
         v = cmax;
         s = (cmax > 1e-6f) ? (diff / cmax) : 0.0f;
         if (diff < 1e-6f)
         {
            h = 0.0f;
         }
         else if (cmax == r)
         {
            h = std::fmod((g - b) / diff, 6.0f);
            if (h < 0.0f) h += 6.0f;
            h /= 6.0f;
         }
         else if (cmax == g)
         {
            h = ((b - r) / diff + 2.0f) / 6.0f;
         }
         else
         {
            h = ((r - g) / diff + 4.0f) / 6.0f;
         }
         h = std::clamp(h, 0.0f, 1.0f);
      }
   }

   // --- HistogramSet --------------------------------------------------------

   void HistogramSet::Clear()
   {
      sampleCount = 0;
      std::memset(r, 0, sizeof(r));
      std::memset(g, 0, sizeof(g));
      std::memset(b, 0, sizeof(b));
      std::memset(y, 0, sizeof(y));
      std::memset(s, 0, sizeof(s));
      std::memset(h, 0, sizeof(h));
      sumR = sumG = sumB = sumY = sumS = 0.0;
      sumY2 = sumS2 = 0.0;
      sumCosH = sumSinH = 0.0;
   }

   void HistogramSet::AddPixel(float red, float green, float blue)
   {
      red = std::clamp(red, 0.0f, 1.0f);
      green = std::clamp(green, 0.0f, 1.0f);
      blue = std::clamp(blue, 0.0f, 1.0f);

      // ITU-R BT.709 Luma
      const float luma = 0.2126f * red + 0.7152f * green + 0.0722f * blue;

      float hue = 0.0f, sat = 0.0f, val = 0.0f;
      RgbToHsv(red, green, blue, hue, sat, val);

      sampleCount++;
      sumR += red;
      sumG += green;
      sumB += blue;
      sumY += luma;
      sumS += sat;
      sumY2 += (double)luma * luma;
      sumS2 += (double)sat * sat;

      const double hueAngle = (double)hue * 2.0 * M_PI;
      sumCosH += std::cos(hueAngle);
      sumSinH += std::sin(hueAngle);

      auto binIndex = [](float val01) -> int {
         return std::clamp((int)(val01 * (float)kBins), 0, kBins - 1);
      };

      r[binIndex(red)] += 1.0f;
      g[binIndex(green)] += 1.0f;
      b[binIndex(blue)] += 1.0f;
      y[binIndex(luma)] += 1.0f;
      s[binIndex(sat)] += 1.0f;
      h[binIndex(hue)] += 1.0f;
   }

   float HistogramSet::CircularHue() const
   {
      if (sampleCount == 0) return 0.0f;
      double avgCos = sumCosH / sampleCount;
      double avgSin = sumSinH / sampleCount;
      if (std::abs(avgCos) < 1e-6 && std::abs(avgSin) < 1e-6)
         return 0.0f;
      double angle = std::atan2(avgSin, avgCos);
      if (angle < 0.0) angle += 2.0 * M_PI;
      return (float)(angle / (2.0 * M_PI));
   }

   float HistogramSet::Percentile(const float bins[kBins], float p)
   {
      p = std::clamp(p, 0.0f, 1.0f);
      float total = 0.0f;
      for (int i = 0; i < kBins; i++)
         total += bins[i];
      if (total <= 0.0f)
         return p;

      const float targetMass = p * total;
      float running = 0.0f;
      for (int i = 0; i < kBins; i++)
      {
         const float binMass = bins[i];
         if (running + binMass >= targetMass)
         {
            const float frac = (binMass > 1e-6f) ? (targetMass - running) / binMass : 0.5f;
            return ((float)i + frac) / (float)kBins;
         }
         running += binMass;
      }
      return 1.0f;
   }

   float HistogramSet::PercentileY(float p) const
   {
      return Percentile(y, p);
   }

   // --- Profile -------------------------------------------------------------

   Profile::Profile()
   {
      ResetToSelfNormalize();
   }

   void Profile::Reset()
   {
      mTotalSamples = 0;
      mAccumulatedTimeSec = 0.0;
      ResetToSelfNormalize();
   }

   void Profile::ResetToSelfNormalize()
   {
      mTargetMeanY = 0.45f;
      mTargetStdY = 0.22f;
      mTargetP01Y = 0.02f;
      mTargetP50Y = 0.45f;
      mTargetP99Y = 0.98f;

      mTargetMeanR = 0.45f;
      mTargetMeanG = 0.45f;
      mTargetMeanB = 0.45f;
      mTargetMeanS = 0.35f;
      mTargetCircularHue = 0.0f;

      for (int i = 0; i < kBins; i++)
      {
         mHistY[i] = 1.0f / (float)kBins;
         mHistS[i] = 1.0f / (float)kBins;
         mHistH[i] = 1.0f / (float)kBins;
         mHistR[i] = 1.0f / (float)kBins;
         mHistG[i] = 1.0f / (float)kBins;
         mHistB[i] = 1.0f / (float)kBins;
      }
   }

   void Profile::Accumulate(const HistogramSet& live, double weight, double activeDtSec)
   {
      if (live.sampleCount == 0 || weight <= 0.0)
         return;

      // Active-time half-life: decay the sample-mass count (Confidence01's
      // saturation term) by however much active time passed since the last
      // tick, so a profile that stops seeing fresh footage for kHalfLifeActiveSec
      // worth of active time reports lower confidence again rather than staying
      // saturated forever off one old session.
      if (activeDtSec > 0.0)
      {
         mAccumulatedTimeSec += activeDtSec;
         const double decay = std::pow(2.0, -activeDtSec / kHalfLifeActiveSec);
         mTotalSamples = (uint64_t)((double)mTotalSamples * decay);
      }

      const double alpha = std::clamp(weight, 0.001, 1.0);
      mTotalSamples += live.sampleCount;

      mTargetMeanY = (float)((1.0 - alpha) * mTargetMeanY + alpha * live.MeanY());
      mTargetStdY  = (float)((1.0 - alpha) * mTargetStdY  + alpha * live.StdDevY());
      mTargetP01Y  = (float)((1.0 - alpha) * mTargetP01Y  + alpha * live.PercentileY(0.01f));
      mTargetP50Y  = (float)((1.0 - alpha) * mTargetP50Y  + alpha * live.PercentileY(0.50f));
      mTargetP99Y  = (float)((1.0 - alpha) * mTargetP99Y  + alpha * live.PercentileY(0.99f));

      mTargetMeanR = (float)((1.0 - alpha) * mTargetMeanR + alpha * live.MeanR());
      mTargetMeanG = (float)((1.0 - alpha) * mTargetMeanG + alpha * live.MeanG());
      mTargetMeanB = (float)((1.0 - alpha) * mTargetMeanB + alpha * live.MeanB());
      mTargetMeanS = (float)((1.0 - alpha) * mTargetMeanS + alpha * live.MeanS());

      // Circular EMA for hue
      const double liveHueRad = (double)live.CircularHue() * 2.0 * M_PI;
      const double targetHueRad = (double)mTargetCircularHue * 2.0 * M_PI;
      const double cosBlend = (1.0 - alpha) * std::cos(targetHueRad) + alpha * std::cos(liveHueRad);
      const double sinBlend = (1.0 - alpha) * std::sin(targetHueRad) + alpha * std::sin(liveHueRad);
      double blendedAngle = std::atan2(sinBlend, cosBlend);
      if (blendedAngle < 0.0) blendedAngle += 2.0 * M_PI;
      mTargetCircularHue = (float)(blendedAngle / (2.0 * M_PI));

      // Histogram bins EMA
      const float liveInv = 1.0f / (float)live.sampleCount;
      for (int i = 0; i < kBins; i++)
      {
         mHistY[i] = (float)((1.0 - alpha) * mHistY[i] + alpha * (live.y[i] * liveInv));
         mHistS[i] = (float)((1.0 - alpha) * mHistS[i] + alpha * (live.s[i] * liveInv));
         mHistH[i] = (float)((1.0 - alpha) * mHistH[i] + alpha * (live.h[i] * liveInv));
         mHistR[i] = (float)((1.0 - alpha) * mHistR[i] + alpha * (live.r[i] * liveInv));
         mHistG[i] = (float)((1.0 - alpha) * mHistG[i] + alpha * (live.g[i] * liveInv));
         mHistB[i] = (float)((1.0 - alpha) * mHistB[i] + alpha * (live.b[i] * liveInv));
      }
   }

   float Profile::Confidence01() const
   {
      // Mathematical confidence function:
      // 1. Sample Mass factor: asymptotic curve saturated after ~120 frames (4 sec at 30 fps)
      //    (each frame is 4096 samples at 64x64, so ~500k samples)
      constexpr double kSaturatingSamples = 200000.0;
      const float massFactor = (float)(1.0 - std::exp(-(double)mTotalSamples / kSaturatingSamples));

      // 2. Dynamic Span factor: penalizes flat black/white single-tone inputs
      const float span = std::max(0.0f, mTargetP99Y - mTargetP01Y);
      const float spanFactor = std::clamp(span / 0.5f, 0.0f, 1.0f);

      // 3. Variance factor: ensures non-zero entropy
      const float varFactor = std::clamp(mTargetStdY / 0.12f, 0.0f, 1.0f);

      return std::clamp(massFactor * spanFactor * varFactor, 0.0f, 1.0f);
   }

   // Versioned, field-by-field (de)serialization - deliberately NOT a raw
   // memcpy(this, ...) of the Profile struct. A whole-struct memcpy ties the
   // on-disk format to this build's exact member layout: adding, removing or
   // reordering one field would silently invalidate every saved patch's
   // `profile` param and the global color_stats.bin (sizeof mismatch ->
   // Deserialize bails -> silent fallback to defaults, no error surfaced to
   // the user). Writing/reading each field explicitly means the format only
   // changes when kProfileVersion is bumped on purpose.
   namespace
   {
      constexpr uint32_t kProfileMagic = 0x53434c43; // 'CLCS'
      constexpr uint16_t kProfileVersion = 1;

      template <typename T>
      void Push(std::vector<uint8_t>& buf, const T& v)
      {
         const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
         buf.insert(buf.end(), p, p + sizeof(T));
      }

      template <typename T>
      bool Pull(const uint8_t* data, size_t size, size_t& offset, T& out)
      {
         if (offset + sizeof(T) > size)
            return false;
         std::memcpy(&out, data + offset, sizeof(T));
         offset += sizeof(T);
         return true;
      }

      bool PullBins(const uint8_t* data, size_t size, size_t& offset, float (&bins)[kBins])
      {
         for (int i = 0; i < kBins; i++)
            if (!Pull(data, size, offset, bins[i]))
               return false;
         return true;
      }
   }

   std::vector<uint8_t> Profile::Serialize() const
   {
      std::vector<uint8_t> buf;
      Push(buf, kProfileMagic);
      Push(buf, kProfileVersion);
      Push(buf, mTotalSamples);
      Push(buf, mAccumulatedTimeSec);
      Push(buf, mTargetMeanY);
      Push(buf, mTargetStdY);
      Push(buf, mTargetP01Y);
      Push(buf, mTargetP50Y);
      Push(buf, mTargetP99Y);
      Push(buf, mTargetMeanR);
      Push(buf, mTargetMeanG);
      Push(buf, mTargetMeanB);
      Push(buf, mTargetMeanS);
      Push(buf, mTargetCircularHue);
      for (float v : mHistY) Push(buf, v);
      for (float v : mHistS) Push(buf, v);
      for (float v : mHistH) Push(buf, v);
      for (float v : mHistR) Push(buf, v);
      for (float v : mHistG) Push(buf, v);
      for (float v : mHistB) Push(buf, v);
      return buf;
   }

   bool Profile::Deserialize(const uint8_t* data, size_t size)
   {
      if (!data)
         return false;

      size_t off = 0;
      uint32_t magic = 0;
      uint16_t version = 0;
      if (!Pull(data, size, off, magic) || magic != kProfileMagic)
         return false;
      if (!Pull(data, size, off, version) || version == 0 || version > kProfileVersion)
         return false; // future format this build doesn't understand - refuse rather than misread

      Profile parsed; // decode into a scratch copy so a truncated buffer never half-mutates *this*
      bool ok = true;
      ok = ok && Pull(data, size, off, parsed.mTotalSamples);
      ok = ok && Pull(data, size, off, parsed.mAccumulatedTimeSec);
      ok = ok && Pull(data, size, off, parsed.mTargetMeanY);
      ok = ok && Pull(data, size, off, parsed.mTargetStdY);
      ok = ok && Pull(data, size, off, parsed.mTargetP01Y);
      ok = ok && Pull(data, size, off, parsed.mTargetP50Y);
      ok = ok && Pull(data, size, off, parsed.mTargetP99Y);
      ok = ok && Pull(data, size, off, parsed.mTargetMeanR);
      ok = ok && Pull(data, size, off, parsed.mTargetMeanG);
      ok = ok && Pull(data, size, off, parsed.mTargetMeanB);
      ok = ok && Pull(data, size, off, parsed.mTargetMeanS);
      ok = ok && Pull(data, size, off, parsed.mTargetCircularHue);
      ok = ok && PullBins(data, size, off, parsed.mHistY);
      ok = ok && PullBins(data, size, off, parsed.mHistS);
      ok = ok && PullBins(data, size, off, parsed.mHistH);
      ok = ok && PullBins(data, size, off, parsed.mHistR);
      ok = ok && PullBins(data, size, off, parsed.mHistG);
      ok = ok && PullBins(data, size, off, parsed.mHistB);
      if (!ok)
         return false;

      *this = parsed;
      return true;
   }

   // --- Engine Singleton ----------------------------------------------------

   Engine& Engine::Instance()
   {
      static Engine sEngine;
      return sEngine;
   }

   Engine::Engine()
   {
      mProfile.ResetToSelfNormalize();
   }

   void Engine::Accumulate(const HistogramSet& live, double dt)
   {
      if (live.sampleCount == 0)
         return;

      // Active-time weight: 30 fps frame has ~0.033s dt.
      // Time constant ~ 1.0 second response window during active learning
      const double weight = std::clamp(dt / 1.5, 0.01, 0.5);
      mProfile.Accumulate(live, weight, dt);
   }

   std::string StatsPath(const std::string& logDir)
   {
      std::filesystem::path p(logDir);
      return (p / "color_stats.bin").string();
   }

   bool Engine::Load(const std::string& directory)
   {
      mLastDir = directory;
      const std::string path = StatsPath(directory);
      std::ifstream f(path, std::ios::binary | std::ios::ate);
      if (!f.is_open())
         return false;
      const size_t sz = (size_t)f.tellg();
      f.seekg(0, std::ios::beg);
      std::vector<uint8_t> buf(sz);
      f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)sz);
      return mProfile.Deserialize(buf.data(), sz);
   }

   bool Engine::Save(const std::string& directory)
   {
      mLastDir = directory;
      const std::string path = StatsPath(directory);
      std::error_code ec;
      std::filesystem::create_directories(directory, ec);
      std::ofstream f(path, std::ios::binary | std::ios::trunc);
      if (!f.is_open())
         return false;
      std::vector<uint8_t> buf = mProfile.Serialize();
      f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
      return true;
   }

   // --- Fit Solver (step-08 §3.2) -------------------------------------------

   ColorGradeParams Fit(const HistogramSet& live, const Profile& target, bool selfNormalize)
   {
      ColorGradeParams p;
      if (live.sampleCount == 0)
         return p;

      const float liveMeanY = live.MeanY();
      const float liveStdY  = live.StdDevY();
      const float liveP01Y  = live.PercentileY(0.01f);
      const float liveP50Y  = live.PercentileY(0.50f);
      const float liveP99Y  = live.PercentileY(0.99f);

      const float targetMeanY = selfNormalize ? 0.45f : target.TargetMeanY();
      const float targetStdY  = selfNormalize ? 0.22f : target.TargetStdY();
      const float targetP01Y  = selfNormalize ? 0.02f : target.TargetP01Y();
      const float targetP50Y  = selfNormalize ? 0.45f : target.TargetP50Y();
      const float targetP99Y  = selfNormalize ? 0.98f : target.TargetP99Y();

      // 1. Exposure (stops): log2(targetMeanY / liveMeanY)
      const float expRatio = targetMeanY / std::max(liveMeanY, 0.01f);
      p.exposure = std::clamp((float)(std::log2(expRatio)), -3.0f, 3.0f);

      // 2. Black & White Point (Levels)
      p.blackPoint = std::clamp(liveP01Y - targetP01Y, -0.2f, 0.3f);
      p.whitePoint = std::clamp(1.0f + (liveP99Y - targetP99Y), 0.7f, 1.2f);
      if (p.blackPoint >= p.whitePoint - 0.1f)
      {
         p.blackPoint = 0.0f;
         p.whitePoint = 1.0f;
      }

      // 3. Contrast around pivot
      p.pivot = std::clamp(liveP50Y, 0.2f, 0.8f);
      const float contrastRatio = targetStdY / std::max(liveStdY, 0.02f);
      p.contrast = std::clamp(contrastRatio, 0.3f, 2.5f);

      // 4. Tone Zones: Highlights, Shadows, Whites, Blacks
      // Count mass in lower and upper 30% of bins
      float liveLowMass = 0.0f, liveHighMass = 0.0f;
      const int lowEnd = kBins * 3 / 10;
      const int highStart = kBins * 7 / 10;
      for (int i = 0; i < lowEnd; i++) liveLowMass += live.y[i];
      for (int i = highStart; i < kBins; i++) liveHighMass += live.y[i];
      const float liveTotal = (float)live.sampleCount;
      liveLowMass /= liveTotal;
      liveHighMass /= liveTotal;

      const float targetLowMass = selfNormalize ? 0.25f : 0.30f;
      const float targetHighMass = selfNormalize ? 0.25f : 0.30f;

      p.shadows = std::clamp((targetLowMass - liveLowMass) * 1.2f, -1.0f, 1.0f);
      p.highlights = std::clamp((targetHighMass - liveHighMass) * 1.2f, -1.0f, 1.0f);

      // Midtones
      if (std::abs(liveP50Y - targetP50Y) > 0.02f)
      {
         const float gammaRatio = targetP50Y / std::max(liveP50Y, 0.01f);
         p.midtones = std::clamp(gammaRatio, 0.5f, 2.0f);
      }
      else
      {
         p.midtones = 1.0f;
      }

      // 5. White Balance (Per-channel RGB Gains)
      if (!selfNormalize)
      {
         const float avgGain = (live.MeanR() + live.MeanG() + live.MeanB()) / 3.0f;
         const float targetAvg = (target.TargetMeanR() + target.TargetMeanG() + target.TargetMeanB()) / 3.0f;
         if (avgGain > 0.01f && targetAvg > 0.01f)
         {
            p.rgbGainR = std::clamp((target.TargetMeanR() / targetAvg) / (live.MeanR() / avgGain), 0.5f, 2.0f);
            p.rgbGainG = std::clamp((target.TargetMeanG() / targetAvg) / (live.MeanG() / avgGain), 0.5f, 2.0f);
            p.rgbGainB = std::clamp((target.TargetMeanB() / targetAvg) / (live.MeanB() / avgGain), 0.5f, 2.0f);
         }
      }
      else
      {
         // In self-normalize, balance color casts toward neutral grey
         const float avgRgb = (live.MeanR() + live.MeanG() + live.MeanB()) / 3.0f;
         if (avgRgb > 0.01f)
         {
            p.rgbGainR = std::clamp(avgRgb / std::max(live.MeanR(), 0.01f), 0.7f, 1.4f);
            p.rgbGainG = std::clamp(avgRgb / std::max(live.MeanG(), 0.01f), 0.7f, 1.4f);
            p.rgbGainB = std::clamp(avgRgb / std::max(live.MeanB(), 0.01f), 0.7f, 1.4f);
         }
      }

      // 6. Saturation & Vibrance
      const float liveMeanS = live.MeanS();
      const float targetMeanS = selfNormalize ? std::max(liveMeanS, 0.25f) : target.TargetMeanS();
      if (liveMeanS > 0.02f)
      {
         p.saturation = std::clamp(targetMeanS / liveMeanS, 0.2f, 2.0f);
      }
      else
      {
         p.saturation = 1.0f;
      }
      p.vibrance = std::clamp((targetMeanS - liveMeanS) * 0.8f, -0.8f, 0.8f);

      // 7. Hue Shift
      if (!selfNormalize)
      {
         float hueDiff = target.TargetCircularHue() - live.CircularHue();
         if (hueDiff < -0.5f) hueDiff += 1.0f;
         if (hueDiff > 0.5f) hueDiff -= 1.0f;
         p.hueShift = hueDiff;
      }
      else
      {
         p.hueShift = 0.0f;
      }

      return p;
   }

   void ClampColorGradeParams(ColorGradeParams& p)
   {
      p.rgbGainR = std::clamp(p.rgbGainR, 0.3f, 3.0f);
      p.rgbGainG = std::clamp(p.rgbGainG, 0.3f, 3.0f);
      p.rgbGainB = std::clamp(p.rgbGainB, 0.3f, 3.0f);
      p.exposure = std::clamp(p.exposure, -4.0f, 4.0f);
      p.blackPoint = std::clamp(p.blackPoint, -0.4f, 0.4f);
      p.whitePoint = std::clamp(p.whitePoint, 0.5f, 1.4f);
      p.contrast = std::clamp(p.contrast, 0.15f, 3.0f); // > 0, per A1
      p.pivot = std::clamp(p.pivot, 0.05f, 0.95f);
      p.highlights = std::clamp(p.highlights, -1.0f, 1.0f);
      p.shadows = std::clamp(p.shadows, -1.0f, 1.0f);
      p.midtones = std::clamp(p.midtones, 0.25f, 3.0f);
      p.whites = std::clamp(p.whites, -1.0f, 1.0f);
      p.blacks = std::clamp(p.blacks, -1.0f, 1.0f);
      p.saturation = std::clamp(p.saturation, 0.0f, 3.0f); // >= 0, per A1
      p.vibrance = std::clamp(p.vibrance, -1.0f, 1.0f);
      p.hueShift = p.hueShift - std::floor(p.hueShift); // wraps mod 1, not clamped
   }

   void StepColorWander(ColorGradeParams& o, uint64_t& rng, float theta, const ColorGradeParams& sigma, double dt)
   {
      o.rgbGainR   = OUStep(o.rgbGainR,   theta, sigma.rgbGainR,   dt, rng);
      o.rgbGainG   = OUStep(o.rgbGainG,   theta, sigma.rgbGainG,   dt, rng);
      o.rgbGainB   = OUStep(o.rgbGainB,   theta, sigma.rgbGainB,   dt, rng);
      o.exposure   = OUStep(o.exposure,   theta, sigma.exposure,   dt, rng);
      o.blackPoint = OUStep(o.blackPoint, theta, sigma.blackPoint, dt, rng);
      o.whitePoint = OUStep(o.whitePoint, theta, sigma.whitePoint, dt, rng);
      o.contrast   = OUStep(o.contrast,   theta, sigma.contrast,   dt, rng);
      o.pivot      = OUStep(o.pivot,      theta, sigma.pivot,      dt, rng);
      o.highlights = OUStep(o.highlights, theta, sigma.highlights, dt, rng);
      o.shadows    = OUStep(o.shadows,    theta, sigma.shadows,    dt, rng);
      o.midtones   = OUStep(o.midtones,   theta, sigma.midtones,   dt, rng);
      o.whites     = OUStep(o.whites,     theta, sigma.whites,     dt, rng);
      o.blacks     = OUStep(o.blacks,     theta, sigma.blacks,     dt, rng);
      o.saturation = OUStep(o.saturation, theta, sigma.saturation, dt, rng);
      o.vibrance   = OUStep(o.vibrance,   theta, sigma.vibrance,   dt, rng);
      o.hueShift   = OUStep(o.hueShift,   theta, sigma.hueShift,   dt, rng);
   }
}

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace SpectralAdditiveDsp
{
   constexpr int kMaxPartials = 256;
   constexpr int kDefaultPartials = 128;
   constexpr int kMaxVoices = 8;
   constexpr int kDefaultImageW = 256;
   constexpr int kDefaultImageH = 256;

   enum FrequencyScale
   {
      kScaleLogarithmic = 0, // Musical pitch / exponential (20 Hz -> 16 kHz)
      kScaleLinear,          // Equal Hz spacing
      kScaleHarmonic,        // Integer overtone series (f0, 2f0, 3f0...)
      kScaleMel,             // Perceptual Mel scale
      kScaleChromatic,       // Equal-tempered 12-TET pitch grid
      kScaleCount
   };

   enum ColorMode
   {
      kColorLuminance = 0,   // Pure grayscale luminance -> amplitude (centered pan)
      kColorHuePan,          // Luminance -> amplitude, Hue -> Stereo Pan [-1..+1]
      kColorRGBSplit,        // Red -> Left Pan, Green -> Right Pan, Blue -> Shimmer/Treble Boost
      kColorSpectralBands,   // Red -> Lows, Green -> Mids, Blue -> Highs
      kColorCount
   };

   enum ScanMode
   {
      kScanForwardLoop = 0,  // Continuous 0 -> 1 loop
      kScanPingPong,         // Continuous 0 -> 1 -> 0 bounce
      kScanOneShot,          // Single sweep 0 -> 1 on note-on/trigger
      kScanFreeRunHz,        // Free running at specified Hz speed
      kScanBpmSync,          // Tempo-synced rate division
      kScanManual,           // Manual scrub via position parameter/CV
      kScanCount
   };

   enum FilterType
   {
      kFilterOff = 0,
      kFilterLP12,
      kFilterLP24,
      kFilterHP12,
      kFilterBP12,
      kFilterCount
   };

   // RGBA Spectrogram Pixel Matrix
   struct SpectrogramMatrix
   {
      int width = kDefaultImageW;
      int height = kDefaultImageH;
      std::vector<uint8_t> rgba; // width * height * 4

      SpectrogramMatrix() : rgba(kDefaultImageW * kDefaultImageH * 4, 0)
      {
         GenerateDefaultPattern();
      }

      SpectrogramMatrix(int w, int h) : width(w), height(h), rgba((size_t)w * h * 4, 0)
      {
      }

      void GenerateDefaultPattern();

      // Sample pixel at normalized coordinates (u, v) in [0, 1] x [0, 1] with bilinear filtering
      // u: Time axis (0 = left, 1 = right)
      // v: Frequency axis (0 = bottom/low freq, 1 = top/high freq)
      inline void SampleBilinear(float u, float v, float& r, float& g, float& b, float& a) const
      {
         if (rgba.empty() || width <= 0 || height <= 0)
         {
            r = g = b = a = 0.0f;
            return;
         }

         const float x = std::clamp(u, 0.0f, 1.0f) * (float)(width - 1);
         // Note: v = 0 is bottom (row 0), v = 1 is top (row height - 1)
         const float y = std::clamp(v, 0.0f, 1.0f) * (float)(height - 1);

         const int x0 = (int)x;
         const int y0 = (int)y;
         const int x1 = std::min(x0 + 1, width - 1);
         const int y1 = std::min(y0 + 1, height - 1);

         const float fx = x - (float)x0;
         const float fy = y - (float)y0;

         const auto getPix = [this](int px, int py, float& pr, float& pg, float& pb, float& pa) {
            const size_t idx = ((size_t)py * (size_t)width + (size_t)px) * 4;
            pr = (float)rgba[idx + 0] * (1.0f / 255.0f);
            pg = (float)rgba[idx + 1] * (1.0f / 255.0f);
            pb = (float)rgba[idx + 2] * (1.0f / 255.0f);
            pa = (float)rgba[idx + 3] * (1.0f / 255.0f);
         };

         float r00, g00, b00, a00;
         float r10, g10, b10, a10;
         float r01, g01, b01, a01;
         float r11, g11, b11, a11;

         getPix(x0, y0, r00, g00, b00, a00);
         getPix(x1, y0, r10, g10, b10, a10);
         getPix(x0, y1, r01, g01, b01, a01);
         getPix(x1, y1, r11, g11, b11, a11);

         const float w00 = (1.0f - fx) * (1.0f - fy);
         const float w10 = fx * (1.0f - fy);
         const float w01 = (1.0f - fx) * fy;
         const float w11 = fx * fy;

         r = r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11;
         g = g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11;
         b = b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11;
         a = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
      }
   };

   // Computes partial frequency in Hz for bin index k in [0, numPartials - 1]
   float ComputePartialFrequency(int k, int numPartials, int scale, float minFreq, float maxFreq, float baseFreq);

   // Converts RGB pixel into partial amplitude and stereo pan
   void EvaluatePartialColor(float r, float g, float b, float a,
                             int colorMode, float threshold, float contrast, float brightness, bool invert,
                             float& outAmpL, float& outAmpR);

   // Fast Sine Table for vectorizable high-throughput oscillator evaluation
   struct FastSineTable
   {
      static constexpr int kTableSize = 4096;
      static constexpr int kTableMask = kTableSize - 1;
      float table[kTableSize + 1];

      FastSineTable()
      {
         for (int i = 0; i <= kTableSize; i++)
         {
            const double angle = 2.0 * 3.14159265358979323846 * (double)i / (double)kTableSize;
            table[i] = (float)sin(angle);
         }
      }

      static const FastSineTable& Instance()
      {
         static FastSineTable sInst;
         return sInst;
      }

      inline float Lookup(double phase01) const
      {
         phase01 -= floor(phase01);
         const double p = phase01 * (double)kTableSize;
         const int idx = (int)p & kTableMask;
         const float frac = (float)(p - (double)idx);
         return table[idx] + (table[idx + 1] - table[idx]) * frac;
      }
   };
}

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace WaveTerrainDsp
{
   constexpr int kFrameSize = 1024;
   constexpr int kFrames = 8;
   constexpr int kMipLevels = 10;
   constexpr int kMaxHarmonics = kFrameSize / 2; // 512

   enum OrbitType
   {
      kOrbitCircle = 0,
      kOrbitLissajous,
      kOrbitLemniscate,
      kOrbitSpiral,
      kOrbitScanline,
      kOrbitCount
   };

   enum ChannelMode
   {
      kChanLuminance = 0,
      kChanRed,
      kChanGreen,
      kChanBlue,
      kChanAlpha,
      kChanEdge,
      kChanCount
   };

   struct BankData
   {
      std::vector<float> data;
      BankData() : data(kFrames * kMipLevels * kFrameSize, 0.0f) {}

      inline int Offset(int frame, int mip) const
      {
         return (frame * kMipLevels + mip) * kFrameSize;
      }

      inline const float* Frame(int frame, int mip) const
      {
         const int f = std::clamp(frame, 0, kFrames - 1);
         const int m = std::clamp(mip, 0, kMipLevels - 1);
         return data.data() + Offset(f, m);
      }
   };

   // Fast Cooley-Tukey Radix-2 FFT Engine for N = 1024
   struct Radix2FFT
   {
      static constexpr int N = 1024;
      float cosTable[N / 2];
      float sinTable[N / 2];
      uint16_t bitRev[N];

      Radix2FFT()
      {
         for (int i = 0; i < N / 2; i++)
         {
            const double angle = -2.0 * 3.14159265358979323846 * (double)i / (double)N;
            cosTable[i] = (float)cos(angle);
            sinTable[i] = (float)sin(angle);
         }
         for (int i = 0; i < N; i++)
         {
            int rev = 0;
            int temp = i;
            for (int j = 0; j < 10; j++)
            {
               rev = (rev << 1) | (temp & 1);
               temp >>= 1;
            }
            bitRev[i] = (uint16_t)rev;
         }
      }

      static const Radix2FFT& Instance()
      {
         static Radix2FFT sInstance;
         return sInstance;
      }

      void Forward(float* re, float* im) const
      {
         for (int i = 0; i < N; i++)
         {
            const int j = bitRev[i];
            if (i < j)
            {
               std::swap(re[i], re[j]);
               std::swap(im[i], im[j]);
            }
         }

         for (int len = 2; len <= N; len <<= 1)
         {
            const int half = len >> 1;
            const int step = N / len;
            for (int i = 0; i < N; i += len)
            {
               for (int k = 0; k < half; k++)
               {
                  const int twiddleIdx = k * step;
                  const float uRe = re[i + k];
                  const float uIm = im[i + k];
                  const float vRe = re[i + k + half] * cosTable[twiddleIdx] - im[i + k + half] * sinTable[twiddleIdx];
                  const float vIm = re[i + k + half] * sinTable[twiddleIdx] + im[i + k + half] * cosTable[twiddleIdx];

                  re[i + k] = uRe + vRe;
                  im[i + k] = uIm + vIm;
                  re[i + k + half] = uRe - vRe;
                  im[i + k + half] = uIm - vIm;
               }
            }
         }
      }

      void Inverse(float* re, float* im) const
      {
         for (int i = 0; i < N; i++)
            im[i] = -im[i];

         Forward(re, im);

         const float invN = 1.0f / (float)N;
         for (int i = 0; i < N; i++)
         {
            re[i] *= invN;
            im[i] = -im[i] * invN;
         }
      }
   };

   // Bilinear sampling of downscaled RGBA8 texture data
   inline float SamplePixelChannel(const uint8_t* px, int w, int h, float u, float v, int channel)
   {
      if (!px || w <= 0 || h <= 0)
         return 0.0f;

      u = u - floorf(u);
      v = v - floorf(v);

      const float fx = u * (float)w - 0.5f;
      const float fy = v * (float)h - 0.5f;

      const int x0 = (int)floorf(fx);
      const int y0 = (int)floorf(fy);
      const int x1 = x0 + 1;
      const int y1 = y0 + 1;

      const float tx = fx - (float)x0;
      const float ty = fy - (float)y0;

      auto wrap = [](int val, int maxVal) -> int {
         val = val % maxVal;
         return val < 0 ? val + maxVal : val;
      };

      const int wx0 = wrap(x0, w);
      const int wx1 = wrap(x1, w);
      const int wy0 = wrap(y0, h);
      const int wy1 = wrap(y1, h);

      auto getVal = [px, w, channel](int x, int y) -> float {
         const uint8_t* p = px + (y * w + x) * 4;
         switch (channel)
         {
            case kChanRed:   return (float)p[0] / 255.0f;
            case kChanGreen: return (float)p[1] / 255.0f;
            case kChanBlue:  return (float)p[2] / 255.0f;
            case kChanAlpha: return (float)p[3] / 255.0f;
            case kChanEdge:
            {
               const float lum = (0.299f * (float)p[0] + 0.587f * (float)p[1] + 0.114f * (float)p[2]) / 255.0f;
               return fabsf(lum - 0.5f) * 2.0f;
            }
            case kChanLuminance:
            default:
               return (0.299f * (float)p[0] + 0.587f * (float)p[1] + 0.114f * (float)p[2]) / 255.0f;
         }
      };

      const float c00 = getVal(wx0, wy0);
      const float c10 = getVal(wx1, wy0);
      const float c01 = getVal(wx0, wy1);
      const float c11 = getVal(wx1, wy1);

      const float top = c00 + (c10 - c00) * tx;
      const float bot = c01 + (c11 - c01) * tx;
      return top + (bot - top) * ty;
   }

   // Generates (u, v) on terrain for a given normalized phase t in [0, 1) and frame k in [0, 7]
   inline void EvaluateOrbit(int orbitType, float t, int frame, float cx, float cy, float rx, float ry,
                             float ratioA, float ratioB, float phaseOffset, float rotRad, float& outU, float& outV)
   {
      const float twoPi = 6.28318530717958647692f;
      const float morph = (float)frame / (float)std::max(1, kFrames - 1);
      float localX = 0.0f;
      float localY = 0.0f;

      switch (orbitType)
      {
         case kOrbitCircle:
         {
            const float rSpread = 1.0f + morph * 0.4f;
            const float angle = t * twoPi;
            localX = rx * rSpread * cosf(angle);
            localY = ry * rSpread * sinf(angle + phaseOffset * twoPi);
            break;
         }
         case kOrbitLissajous:
         {
            const float angleA = t * twoPi * std::max(1.0f, roundf(ratioA));
            const float angleB = t * twoPi * std::max(1.0f, roundf(ratioB));
            localX = rx * sinf(angleA + phaseOffset * twoPi + morph * 0.3f);
            localY = ry * sinf(angleB);
            break;
         }
         case kOrbitLemniscate:
         {
            const float angle = t * twoPi;
            const float denom = 1.0f + sinf(angle) * sinf(angle);
            localX = (rx * (1.0f + morph * 0.3f)) * cosf(angle) / denom;
            localY = (ry * (1.0f + morph * 0.3f)) * sinf(angle) * cosf(angle) / denom;
            break;
         }
         case kOrbitSpiral:
         {
            const float rFactor = (0.2f + 0.8f * t) * (1.0f + morph * 0.3f);
            const float turns = 3.0f;
            const float angle = t * twoPi * turns;
            localX = rx * rFactor * cosf(angle);
            localY = ry * rFactor * sinf(angle);
            break;
         }
         case kOrbitScanline:
         default:
         {
            localX = (t - 0.5f) * rx * 2.0f;
            localY = ((float)frame / (float)(kFrames - 1) - 0.5f) * ry * 2.0f;
            break;
         }
      }

      const float cosR = cosf(rotRad);
      const float sinR = sinf(rotRad);
      const float rxRot = localX * cosR - localY * sinR;
      const float ryRot = localX * sinR + localY * cosR;

      // Widen the morph span (item 18): each orbit case above already nudges
      // its own shape per-frame (rSpread/phase/radius terms, up to +-30-40%),
      // but that alone is a small dilation of the same loop, not the large
      // timbral change a terrain's whole point is. Traverse the orbit's
      // center through a real fraction of the image across the 8 frames too,
      // so frame 0 and frame 7 sample genuinely different regions rather
      // than a slightly-bigger version of the same region. Scanline already
      // drives its Y axis directly from `frame` across the full radius (see
      // its case above) - adding this on top would double up that traversal
      // with a second, uncoordinated one, so it's excluded here.
      float morphCy = cy;
      if (orbitType != kOrbitScanline)
      {
         const float morphSpan = 0.6f; // fraction of the image traversed frame 0 -> frame 7
         morphCy = std::clamp(cy + (morph - 0.5f) * morphSpan, 0.0f, 1.0f);
      }

      outU = cx + rxRot;
      outV = morphCy + ryRot;
   }

   // Builds an exact 10-level anti-aliased wavetable bank from raw pixel heightfield via O(N log N) Fast Fourier Transform
   inline void BuildBankFromPixels(BankData& bank, const uint8_t* pixels, int w, int h,
                                   int orbitType, int channel,
                                   float cx, float cy, float rx, float ry,
                                   float ratioA, float ratioB, float phaseOffset, float rotRad)
   {
      const int ceilings[kMipLevels] = { 512, 256, 128, 64, 32, 16, 8, 4, 2, 1 };
      const auto& fft = Radix2FFT::Instance();

      float re[kFrameSize];
      float im[kFrameSize];
      float specRe[kFrameSize];
      float specIm[kFrameSize];

      for (int f = 0; f < kFrames; f++)
      {
         // 1. Sample raw 1024-sample cycle along the orbit trajectory
         double mean = 0.0;
         for (int i = 0; i < kFrameSize; i++)
         {
            const float t = (float)i / (float)kFrameSize;
            float u = 0.0f, v = 0.0f;
            EvaluateOrbit(orbitType, t, f, cx, cy, rx, ry, ratioA, ratioB, phaseOffset, rotRad, u, v);

            const float val = SamplePixelChannel(pixels, w, h, u, v, channel);
            const float s = (val * 2.0f - 1.0f);
            re[i] = s;
            im[i] = 0.0f;
            mean += (double)s;
         }

         mean /= (double)kFrameSize;
         for (int i = 0; i < kFrameSize; i++)
            re[i] -= (float)mean; // Remove DC offset

         // 2. Fast Fourier Transform
         fft.Forward(re, im);
         std::memcpy(specRe, re, sizeof(re));
         std::memcpy(specIm, im, sizeof(im));

         // 3. Synthesize each mip level with its exact harmonic ceiling
         for (int L = 0; L < kMipLevels; L++)
         {
            const int maxH = ceilings[L];
            std::memcpy(re, specRe, sizeof(re));
            std::memcpy(im, specIm, sizeof(im));

            // Zero out frequencies above harmonic ceiling
            for (int k = maxH + 1; k < kFrameSize - maxH; k++)
            {
               re[k] = 0.0f;
               im[k] = 0.0f;
            }

            fft.Inverse(re, im);

            float* dst = bank.data.data() + bank.Offset(f, L);
            std::memcpy(dst, re, sizeof(re));
         }

         // 4. Normalise every mip level by the full-bandwidth (mip 0) peak
         float peak = 0.0f;
         const float* mip0 = bank.data.data() + bank.Offset(f, 0);
         for (int n = 0; n < kFrameSize; n++)
            peak = std::max(peak, fabsf(mip0[n]));

         const float norm = peak > 1e-5f ? (0.95f / peak) : 0.0f;
         for (int L = 0; L < kMipLevels; L++)
         {
            float* dst = bank.data.data() + bank.Offset(f, L);
            for (int n = 0; n < kFrameSize; n++)
               dst[n] *= norm;
         }
      }
   }

   inline int MipForPhaseInc(double phaseInc)
   {
      if (phaseInc <= 0.0)
         return 0;
      const double maxHarmonic = 0.5 / phaseInc;
      int level = 0;
      int ceiling = kMaxHarmonics;
      while (level < kMipLevels - 1 && (double)ceiling > maxHarmonic)
      {
         ceiling >>= 1;
         level++;
      }
      return level;
   }

   inline float SampleBank(const BankData& bank, float position01, double phase01, double phaseInc)
   {
      const int mip = MipForPhaseInc(phaseInc);
      const float framePos = std::clamp(position01, 0.0f, 1.0f) * (float)(kFrames - 1);
      const int f0 = std::clamp((int)framePos, 0, kFrames - 1);
      const int f1 = std::min(f0 + 1, kFrames - 1);
      const float frameFrac = framePos - (float)f0;

      const float* lo = bank.Frame(f0, mip);
      const float* hi = bank.Frame(f1, mip);

      phase01 -= floor(phase01);
      const double x = phase01 * (double)kFrameSize;
      const int i0 = (int)x & (kFrameSize - 1);
      const int i1 = (i0 + 1) & (kFrameSize - 1);
      const float fx = (float)(x - floor(x));

      const float a = lo[i0] + (lo[i1] - lo[i0]) * fx;
      const float b = hi[i0] + (hi[i1] - hi[i0]) * fx;
      return a + (b - a) * frameFrac;
   }
}

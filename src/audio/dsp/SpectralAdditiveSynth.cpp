#include "SpectralAdditiveSynth.h"
#include <cmath>

namespace SpectralAdditiveDsp
{
   void SpectrogramMatrix::GenerateDefaultPattern()
   {
      width = kDefaultImageW;
      height = kDefaultImageH;
      rgba.assign((size_t)width * height * 4, 0);

      // Procedural iconic test spectrogram:
      // - 1. Harmonic fundamental + overtone chord bars (Y = 10% .. 40%)
      // - 2. Rising exponential sweep / chirp (X = 0..1, Y = 0.05 .. 0.95)
      // - 3. Geometric Lissajous / spiral glyph in the center
      // - 4. High-frequency sparkle / noise dots near top (Y = 80% .. 95%)

      for (int y = 0; y < height; y++)
      {
         const float ny = (float)y / (float)(height - 1); // 0 = bass, 1 = treble
         for (int x = 0; x < width; x++)
         {
            const float nx = (float)x / (float)(width - 1); // 0 = start, 1 = end

            float intensity = 0.0f;
            float hue = nx; // hue rotates across time

            // 1. Five parallel harmonic horizontal bars
            for (int h = 1; h <= 6; h++)
            {
               const float barY = 0.08f * (float)h;
               const float distY = fabsf(ny - barY);
               if (distY < 0.012f)
               {
                  const float env = 1.0f - (distY / 0.012f);
                  const float beatPulse = 0.5f + 0.5f * cosf(nx * 3.14159265f * 8.0f);
                  intensity = std::max(intensity, env * 0.7f * beatPulse);
               }
            }

            // 2. Rising exponential sweep / chirp
            const float sweepY = 0.05f + 0.85f * powf(nx, 1.8f);
            const float sweepDist = fabsf(ny - sweepY);
            if (sweepDist < 0.015f)
            {
               const float sweepEnv = 1.0f - (sweepDist / 0.015f);
               intensity = std::max(intensity, sweepEnv * 0.95f);
            }

            // 3. Central glowing circular / spiral motif
            const float cx = 0.5f;
            const float cy = 0.55f;
            const float dx = (nx - cx) * 1.5f;
            const float dy = (ny - cy);
            const float r = sqrtf(dx * dx + dy * dy);
            const float angle = atan2f(dy, dx);
            const float spiralR = 0.18f + 0.04f * sinf(angle * 5.0f + nx * 6.283185f);
            const float spiralDist = fabsf(r - spiralR);
            if (spiralDist < 0.018f)
            {
               const float spiralEnv = 1.0f - (spiralDist / 0.018f);
               intensity = std::max(intensity, spiralEnv * 0.85f);
               hue = 0.15f + 0.7f * (0.5f + 0.5f * sinf(angle * 2.0f));
            }

            // 4. Dual diagonal cross-chords
            const float diag1 = fabsf(ny - (0.2f + 0.6f * nx));
            if (diag1 < 0.008f)
               intensity = std::max(intensity, (1.0f - diag1 / 0.008f) * 0.6f);

            // Convert (intensity, hue) into RGB
            intensity = std::clamp(intensity, 0.0f, 1.0f);
            float rOut = 0.0f, gOut = 0.0f, bOut = 0.0f;
            if (intensity > 0.001f)
            {
               // HSV to RGB conversion
               const float h6 = fmodf(hue * 6.0f, 6.0f);
               const int hInt = (int)h6;
               const float f = h6 - (float)hInt;
               const float v = intensity;
               const float p = v * 0.2f;
               const float q = v * (1.0f - f * 0.8f);
               const float t = v * (1.0f - (1.0f - f) * 0.8f);

               switch (hInt)
               {
                  case 0: rOut = v; gOut = t; bOut = p; break;
                  case 1: rOut = q; gOut = v; bOut = p; break;
                  case 2: rOut = p; gOut = v; bOut = t; break;
                  case 3: rOut = p; gOut = q; bOut = v; break;
                  case 4: rOut = t; gOut = p; bOut = v; break;
                  default: rOut = v; gOut = p; bOut = q; break;
               }
            }

            const size_t idx = ((size_t)y * (size_t)width + (size_t)x) * 4;
            rgba[idx + 0] = (uint8_t)std::clamp((int)(rOut * 255.0f), 0, 255);
            rgba[idx + 1] = (uint8_t)std::clamp((int)(gOut * 255.0f), 0, 255);
            rgba[idx + 2] = (uint8_t)std::clamp((int)(bOut * 255.0f), 0, 255);
            rgba[idx + 3] = 255;
         }
      }
   }

   float ComputePartialFrequency(int k, int numPartials, int scale, float minFreq, float maxFreq, float baseFreq)
   {
      if (numPartials <= 1)
         return minFreq;

      const float normK = (float)k / (float)(numPartials - 1); // 0..1
      const float safeMin = std::max(10.0f, minFreq);
      const float safeMax = std::max(safeMin + 10.0f, maxFreq);

      switch (scale)
      {
         case kScaleLogarithmic:
         default:
         {
            // Logarithmic / Exponential: f(y) = min * (max / min)^y
            return safeMin * powf(safeMax / safeMin, normK);
         }
         case kScaleLinear:
         {
            // Linear equal-Hz spacing
            return safeMin + normK * (safeMax - safeMin);
         }
         case kScaleHarmonic:
         {
            // Harmonic integer series: f0, 2*f0, 3*f0...
            const float f0 = std::max(10.0f, baseFreq > 0.0f ? baseFreq : safeMin);
            return f0 * (float)(k + 1);
         }
         case kScaleMel:
         {
            // Mel scale: Mel = 2595 * log10(1 + f / 700)
            const float melMin = 2595.0f * log10f(1.0f + safeMin / 700.0f);
            const float melMax = 2595.0f * log10f(1.0f + safeMax / 700.0f);
            const float mel = melMin + normK * (melMax - melMin);
            return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
         }
         case kScaleChromatic:
         {
            // 12-TET semitone / microtone grid across octave range
            const float numOctaves = log2f(safeMax / safeMin);
            const float totalSemitones = numOctaves * 12.0f;
            const float st = roundf(normK * totalSemitones);
            return safeMin * powf(2.0f, st / 12.0f);
         }
      }
   }

   void EvaluatePartialColor(float r, float g, float b, float a,
                             int colorMode, float threshold, float contrast, float brightness, bool invert,
                             float& outAmpL, float& outAmpR)
   {
      // Standard perceived luminance
      float luma = 0.299f * r + 0.587f * g + 0.114f * b;
      luma *= a;

      if (invert)
         luma = 1.0f - luma;

      // Threshold noise gating
      if (luma < threshold)
      {
         outAmpL = 0.0f;
         outAmpR = 0.0f;
         return;
      }

      // Re-scale above threshold
      const float activeSpan = std::max(0.001f, 1.0f - threshold);
      luma = (luma - threshold) / activeSpan;

      // Contrast power curve
      const float safeGamma = std::clamp(contrast, 0.1f, 8.0f);
      luma = powf(luma, safeGamma);

      // Brightness / Gain
      luma *= std::max(0.0f, brightness);

      // Determine Pan [-1, +1] based on colorMode
      float pan = 0.0f; // center
      float ampMultiplierL = 1.0f;
      float ampMultiplierR = 1.0f;

      switch (colorMode)
      {
         case kColorLuminance:
         default:
            pan = 0.0f;
            break;

         case kColorHuePan:
         {
            // Compute Hue in [0, 1]
            const float maxC = std::max(r, std::max(g, b));
            const float minC = std::min(r, std::min(g, b));
            const float delta = maxC - minC;

            float hue = 0.0f;
            if (delta > 0.001f)
            {
               if (maxC == r)
                  hue = fmodf((g - b) / delta, 6.0f);
               else if (maxC == g)
                  hue = (b - r) / delta + 2.0f;
               else
                  hue = (r - g) / delta + 4.0f;

               hue /= 6.0f;
               if (hue < 0.0f) hue += 1.0f;
            }

            // Hue mapped to Pan [-1.0 .. +1.0]
            pan = (hue * 2.0f - 1.0f);
            break;
         }

         case kColorRGBSplit:
         {
            // Red strictly left, Green strictly right, Blue stereo center
            const float rVal = (invert ? (1.0f - r) : r) * brightness;
            const float gVal = (invert ? (1.0f - g) : g) * brightness;
            const float bVal = (invert ? (1.0f - b) : b) * brightness;

            const float leftRaw = std::max(0.0f, rVal - threshold) + 0.5f * std::max(0.0f, bVal - threshold);
            const float rightRaw = std::max(0.0f, gVal - threshold) + 0.5f * std::max(0.0f, bVal - threshold);

            outAmpL = powf(leftRaw / activeSpan, safeGamma) * brightness;
            outAmpR = powf(rightRaw / activeSpan, safeGamma) * brightness;
            return;
         }

         case kColorSpectralBands:
         {
            // Equal pan, but color saturation boosts harmonic brightness
            pan = (r - g) * 0.8f;
            break;
         }
      }

      // Equal-power stereo panning
      const float clampedPan = std::clamp(pan, -1.0f, 1.0f);
      const float angle = (clampedPan + 1.0f) * (3.14159265358979323846f / 4.0f);
      outAmpL = luma * cosf(angle) * ampMultiplierL;
      outAmpR = luma * sinf(angle) * ampMultiplierR;
   }
}

#pragma once

#include <algorithm>
#include <cmath>

// sRGB <-> linear <-> Oklab (Bjorn Ottosson), shared by every node that mixes
// colours perceptually. Colours elsewhere in the app are stored the way the
// picker shows them (sRGB-encoded), so anything that averages or interpolates
// has to linearise on the way in and re-encode on the way out.
//
// This is deliberately not a "fix" for the other sRGB conventions in the app
// (see codebase-navigation: exact piecewise, gamma 2.2 and none coexist).
namespace ColorSpace
{
   inline float SrgbToLinear(float c)
   {
      return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
   }

   inline float LinearToSrgb(float c)
   {
      c = std::max(0.0f, std::min(c, 1.0f));
      return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
   }

   inline void LinearToOklab(const float rgb[3], float outLab[3])
   {
      const float l = 0.4122214708f * rgb[0] + 0.5363325363f * rgb[1] + 0.0514459929f * rgb[2];
      const float m = 0.2119034982f * rgb[0] + 0.6806995451f * rgb[1] + 0.1073969566f * rgb[2];
      const float s = 0.0883024619f * rgb[0] + 0.2817188376f * rgb[1] + 0.6299787005f * rgb[2];

      const float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);

      outLab[0] = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
      outLab[1] = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
      outLab[2] = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
   }

   inline void OklabToLinear(const float lab[3], float outRgb[3])
   {
      const float l_ = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
      const float m_ = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
      const float s_ = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];

      const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;

      outRgb[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
      outRgb[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
      outRgb[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
   }

   inline bool InGamut(const float linear[3], float eps = 1e-4f)
   {
      for (int i = 0; i < 3; i++)
         if (linear[i] < -eps || linear[i] > 1.0f + eps)
            return false;
      return true;
   }

   // Oklab -> linear sRGB inside the gamut. An out-of-gamut colour keeps its
   // lightness and hue and loses chroma (largest in-gamut scale found by
   // bisection), instead of each channel clipping on its own, which shifts the
   // hue and flattens saturated colours into the same few primaries.
   inline void OklabToLinearInGamut(const float lab[3], float outRgb[3])
   {
      OklabToLinear(lab, outRgb);
      if (InGamut(outRgb))
         return;

      const float L = std::max(0.0f, std::min(lab[0], 1.0f));
      float lo = 0.0f, hi = 1.0f;
      for (int i = 0; i < 14; i++)
      {
         const float mid = 0.5f * (lo + hi);
         const float scaled[3] = { L, lab[1] * mid, lab[2] * mid };
         float rgb[3];
         OklabToLinear(scaled, rgb);
         if (InGamut(rgb))
            lo = mid;
         else
            hi = mid;
      }
      const float scaled[3] = { L, lab[1] * lo, lab[2] * lo };
      OklabToLinear(scaled, outRgb);
      for (int i = 0; i < 3; i++)
         outRgb[i] = std::max(0.0f, std::min(outRgb[i], 1.0f));
   }

   inline void OklabToDisplay(const float lab[3], float outRgb[3])
   {
      float linear[3];
      OklabToLinearInGamut(lab, linear);
      for (int i = 0; i < 3; i++)
         outRgb[i] = LinearToSrgb(linear[i]);
   }
}

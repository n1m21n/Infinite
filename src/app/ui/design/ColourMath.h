// Colour maths constants in one place (ui-formalisation Block A): standards values, not design decisions.
// UI code calls these instead of typing luma weights next to its design tokens.
#pragma once
#include <cmath>

namespace ColourMath
{
   // ITU-R BT.709 / sRGB luma weights.
   constexpr float kLuma709R = 0.2126f, kLuma709G = 0.7152f, kLuma709B = 0.0722f;
   // ITU-R BT.601 luma weights.
   constexpr float kLuma601R = 0.299f, kLuma601G = 0.587f, kLuma601B = 0.114f;

   constexpr float Luma709(float r, float g, float b) { return kLuma709R * r + kLuma709G * g + kLuma709B * b; }
   constexpr float Luma601(float r, float g, float b) { return kLuma601R * r + kLuma601G * g + kLuma601B * b; }

   // sRGB channel (0..1) to linear, WCAG 2.0 form (threshold 0.03928).
   inline float SrgbToLinear(float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
   // WCAG relative luminance of an sRGB colour (0..1 channels).
   inline float RelativeLuminance(float r, float g, float b) { return Luma709(SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b)); }
}

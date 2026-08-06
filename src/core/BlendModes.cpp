#include "BlendModes.h"

namespace BlendModes
{
   namespace
   {
      // Index must match the blendMode() switch in kBlendGLSL below.
      const std::vector<std::string> kNames = {
         "Normal", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color",
         "Lighten", "Screen", "Color Dodge", "Add", "Lighter Color",
         "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix",
         "Difference", "Exclusion", "Subtract", "Divide",
         "Hue", "Saturation", "Color", "Luminosity",
         "Average", "Negation", "Reflect", "Glow", "Erase"
      };
   }

   const std::vector<std::string>& Names()
   {
      return kNames;
   }

   const char* kBlendGLSL =
      "float Lum(vec3 c) { return dot(c, vec3(0.3, 0.59, 0.11)); }\n"
      "vec3 ClipColor(vec3 c) {\n"
      "   float l = Lum(c);\n"
      "   float n = min(c.r, min(c.g, c.b));\n"
      "   float x = max(c.r, max(c.g, c.b));\n"
      "   if (n < 0.0) c = l + (c - l) * l / (l - n + 1e-6);\n"
      "   if (x > 1.0) c = l + (c - l) * (1.0 - l) / (x - l + 1e-6);\n"
      "   return c;\n"
      "}\n"
      "vec3 SetLum(vec3 c, float l) { return ClipColor(c + vec3(l - Lum(c))); }\n"
      "float Sat(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }\n"
      "vec3 SetSat(vec3 c, float s) {\n"
      "   float cmin = min(c.r, min(c.g, c.b));\n"
      "   float cmax = max(c.r, max(c.g, c.b));\n"
      "   if (cmax > cmin) return (c - cmin) * s / (cmax - cmin);\n"
      "   return vec3(0.0);\n"
      "}\n"
      "\n"
      "vec3 blendMode(int mode, vec3 a, vec3 b) {\n"
      "   if (mode == 0) return b;\n"
      "   if (mode == 1) return min(a, b);\n"
      "   if (mode == 2) return a * b;\n"
      "   if (mode == 3) return 1.0 - min((1.0 - a) / max(b, 1e-4), vec3(1.0));\n"
      "   if (mode == 4) return clamp(a + b - 1.0, 0.0, 1.0);\n"
      "   if (mode == 5) return Lum(a) <= Lum(b) ? a : b;\n"
      "   if (mode == 6) return max(a, b);\n"
      "   if (mode == 7) return 1.0 - (1.0 - a) * (1.0 - b);\n"
      "   if (mode == 8) return min(a / max(1.0 - b, 1e-4), vec3(1.0));\n"
      "   if (mode == 9) return clamp(a + b, 0.0, 1.0);\n"
      "   if (mode == 10) return Lum(a) >= Lum(b) ? a : b;\n"
      "   if (mode == 11) return mix(2.0*a*b, 1.0-2.0*(1.0-a)*(1.0-b), step(0.5, b));\n"
      "   if (mode == 12) {\n"
      "      vec3 d = mix(sqrt(a), ((16.0*a-12.0)*a+4.0)*a, step(0.25, a));\n"
      "      return mix(a - (1.0-2.0*b)*a*(1.0-a), a + (2.0*b-1.0)*(d-a), step(0.5, b));\n"
      "   }\n"
      "   if (mode == 13) return mix(2.0*a*b, 1.0-2.0*(1.0-a)*(1.0-b), step(0.5, a));\n"
      "   if (mode == 14) return mix(1.0-min((1.0-a)/max(2.0*b,1e-4),vec3(1.0)), min(a/max(2.0*(1.0-b),1e-4),vec3(1.0)), step(0.5, b));\n"
      "   if (mode == 15) return clamp(a + 2.0*b - 1.0, 0.0, 1.0);\n"
      "   if (mode == 16) return mix(min(a, 2.0*b), max(a, 2.0*b-1.0), step(0.5, b));\n"
      "   if (mode == 17) return step(0.5, clamp(a + 2.0*b - 1.0, 0.0, 1.0));\n"
      "   if (mode == 18) return abs(a - b);\n"
      "   if (mode == 19) return a + b - 2.0*a*b;\n"
      "   if (mode == 20) return clamp(a - b, 0.0, 1.0);\n"
      "   if (mode == 21) return min(a / max(b, 1e-4), vec3(1.0));\n"
      "   if (mode == 22) return SetLum(SetSat(b, Sat(a)), Lum(a));\n"
      "   if (mode == 23) return SetLum(SetSat(a, Sat(b)), Lum(a));\n"
      "   if (mode == 24) return SetLum(b, Lum(a));\n"
      "   if (mode == 25) return SetLum(a, Lum(b));\n"
      "   if (mode == 26) return (a + b) * 0.5;\n"
      "   if (mode == 27) return 1.0 - abs(1.0 - a - b);\n"
      "   if (mode == 28) return min(a*a / max(1.0-b, 1e-4), vec3(1.0));\n"
      "   if (mode == 29) return min(b*b / max(1.0-a, 1e-4), vec3(1.0));\n"
      "   return a;\n"
      "}\n";
}

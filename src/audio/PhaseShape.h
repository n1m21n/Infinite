#pragma once

#include <cmath>
#include <cstring>

// Phase shaping: functions that take a phase (0..1, wrapped) and return a
// new phase, so any waveform read through them changes shape without a
// filter. One copy of the math, used by Field's builtins (every backend) and
// by the Wavetable engine's warps, so a warp sounds the same in both.
//
//   bend(p, a)     a -1..1   power curve: a > 0 rushes the start, a < 0 lingers
//   pinch(p, a)    a -1..1   S-curve about the middle: a > 0 lingers at the centre
//   mirror(p, a)   a  0..1   fold: at 1 the cycle plays forward then back
//   skew(p, a)     a -1..1   moves the half-way point (phase distortion)
//   formant(p, r)  r  1..64  plays the cycle r times faster, then holds its end
//   sync(p, r)     r  0.25..64  plays the cycle r times per period (hard sync;
//                  below 1 it plays the first r of the cycle and restarts)
//   quantize(p, n) n  1..256 snaps the phase to n steps
//
// Every shape keeps 0 -> 0 and leaves the phase in [0, 1]. Amount 0 (ratio 1
// for formant/sync) is the identity, so a shape at rest costs no timbre.
// Field's builtins and Wavetable's warps (WavetableSynthCore::WarpReadPhase)
// both call Apply.
namespace PhaseShape
{
   enum class Kind
   {
      Bend,
      Pinch,
      Mirror,
      Skew,
      Formant,
      Sync,
      Quantize,
      Count
   };

   inline const char* Name(Kind k)
   {
      switch (k)
      {
         case Kind::Bend: return "bend";
         case Kind::Pinch: return "pinch";
         case Kind::Mirror: return "mirror";
         case Kind::Skew: return "skew";
         case Kind::Formant: return "formant";
         case Kind::Sync: return "sync";
         case Kind::Quantize: return "quantize";
         default: return "";
      }
   }

   // The shapes Field exposes as builtins: bend, pinch, mirror, skew, formant.
   inline bool FieldBuiltin(const char* name, Kind& out)
   {
      for (int i = 0; i <= (int)Kind::Formant; i++)
      {
         if (std::strcmp(name, Name((Kind)i)) == 0)
         {
            out = (Kind)i;
            return true;
         }
      }
      return false;
   }

   template <typename T>
   inline T Clamp(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

   template <typename T>
   inline T Apply(Kind k, T p, T a)
   {
      const T f = p - std::floor(p);
      switch (k)
      {
         case Kind::Bend:
         {
            const T e = std::pow(T(4), -Clamp(a, T(-1), T(1)));
            return std::pow(f, e);
         }
         case Kind::Pinch:
         {
            const T e = std::pow(T(4), Clamp(a, T(-1), T(1)));
            const T x = T(2) * f - T(1);
            const T y = x < T(0) ? -std::pow(-x, e) : std::pow(x, e);
            return T(0.5) * (y + T(1));
         }
         case Kind::Mirror:
         {
            const T m = T(1) - std::fabs(T(2) * f - T(1));
            return f + (m - f) * Clamp(a, T(0), T(1));
         }
         case Kind::Skew:
         {
            const T knee = T(0.5) + T(0.49) * Clamp(a, T(-1), T(1));
            return f < knee ? T(0.5) * f / knee
                            : T(0.5) + T(0.5) * (f - knee) / (T(1) - knee);
         }
         case Kind::Formant:
         {
            const T r = Clamp(a, T(1), T(64));
            const T q = f * r;
            return q < T(1) ? q : T(1);
         }
         case Kind::Sync:
         {
            const T q = f * Clamp(a, T(0.25), T(64));
            return q - std::floor(q);
         }
         case Kind::Quantize:
         {
            const T n = std::floor(Clamp(a, T(1), T(256)));
            return std::floor(f * n) / n;
         }
         default: return f;
      }
   }

   // The same five Field shapes in GLSL, scalar plus component-wise vec2..4,
   // as fld_<name>. Appended to the Field pixel helper prelude.
   constexpr const char* kGlslPrelude =
      "float fld_bend(float p, float a) { return pow((p - floor(p)), pow(4.0, -clamp(a, -1.0, 1.0))); }\n"
      "float fld_pinch(float p, float a) {\n"
      "   float x = 2.0 * (p - floor(p)) - 1.0;\n"
      "   return 0.5 * (sign(x) * pow(abs(x), pow(4.0, clamp(a, -1.0, 1.0))) + 1.0);\n"
      "}\n"
      "float fld_mirror(float p, float a) { float f = (p - floor(p)); return mix(f, 1.0 - abs(2.0 * f - 1.0), clamp(a, 0.0, 1.0)); }\n"
      "float fld_skew(float p, float a) {\n"
      "   float f = (p - floor(p)); float k = 0.5 + 0.49 * clamp(a, -1.0, 1.0);\n"
      "   return f < k ? 0.5 * f / k : 0.5 + 0.5 * (f - k) / (1.0 - k);\n"
      "}\n"
      "float fld_formant(float p, float r) { return min((p - floor(p)) * clamp(r, 1.0, 64.0), 1.0); }\n"
#define PHASESHAPE_GLSL_VEC(fn) \
      "vec2 " fn "(vec2 p, vec2 a) { return vec2(" fn "(p.x, a.x), " fn "(p.y, a.y)); }\n" \
      "vec3 " fn "(vec3 p, vec3 a) { return vec3(" fn "(p.x, a.x), " fn "(p.y, a.y), " fn "(p.z, a.z)); }\n" \
      "vec4 " fn "(vec4 p, vec4 a) { return vec4(" fn "(p.x, a.x), " fn "(p.y, a.y), " fn "(p.z, a.z), " fn "(p.w, a.w)); }\n"
      PHASESHAPE_GLSL_VEC("fld_bend")
      PHASESHAPE_GLSL_VEC("fld_pinch")
      PHASESHAPE_GLSL_VEC("fld_mirror")
      PHASESHAPE_GLSL_VEC("fld_skew")
      PHASESHAPE_GLSL_VEC("fld_formant");
#undef PHASESHAPE_GLSL_VEC
}

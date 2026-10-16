// PointZ operators, tier A: per-point GPU kernels and the simple pass-through
// and conversion operators. Each follows the purpose of the TouchDesigner
// PointZ of the same name (from Derivative's public operator descriptions);
// parameters and GLSL are Infinite's own.

#include "PopOps.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "PopOpsInternal.h"

namespace pop
{
   using namespace popa;

   void RegisterKernelsA1(std::vector<PopDef>& defs)
   {
      // ---- Null / In / Out / Geometry to PointZ -------------------------------------
      {
         PopDef d = Simple("Null PointZ", "Null PointZ", "Passes its input through unchanged: a stable junction to branch a PointZ chain, "
                                                  "and the end of a Particle or Feedback loop - give it a 'tag' and name that "
                                                  "tag in the loop's 'target'.", 1);
         d.params = { T("tag", "tag (loop target)", "") };
         d.cook = [](CookCtx& c) { PassInput(c); return true; };
         defs.push_back(d);
         PopDef in = Simple("In PointZ", "In PointZ", "Marks where a PointZ enters a group or macro; passes it through.", 1);
         in.cook = d.cook;
         defs.push_back(in);
         PopDef out = Simple("Out PointZ", "Out PointZ", "Marks where a PointZ leaves a group or macro; passes it through.", 1);
         out.cook = d.cook;
         defs.push_back(out);
         PopDef sop = Simple("Geometry to PointZ", "Geometry to PointZ",
                             "Turns any Infinite 3D source (Geometry, Model 3D, Text 3D, Particle System, Curve...) into a PointZ, "
                             "with its placement baked in. Every PointZ input already does this; use it to make the step explicit.", 1);
         sop.inputLabels = { "geometry" };
         sop.cook = d.cook;
         defs.push_back(sop);
      }

      // ---- Switch ----------------------------------------------------------
      {
         PopDef d = Simple("Switch PointZ", "Switch PointZ", "Outputs one of up to four inputs, picked by 'index' (modulate it to cut between shapes).", 4);
         d.inputLabels = { "A", "B", "C", "D" };
         d.params = { I("index", "index", 0, 0, 3) };
         d.cook = [](CookCtx& c) {
            const int i = std::max(0, std::min(3, c.I("index")));
            if (const PopData* in = c.Input(i))
               c.out = ShallowCopy(*in);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Transform -------------------------------------------------------
      {
         PopDef d = Simple("Transform PointZ", "Transform PointZ",
                           "Moves, rotates and scales points (and their normals) about a pivot, in a chosen order. "
                           "'also vectors' lists more vector attributes to rotate with them (e.g. PartVel).", 1);
         d.params = { V("t", "translate", 0, 0, 0, -10.0f, 10.0f), V("r", "rotate", 0, 0, 0, -360.0f, 360.0f),
                      V("s", "scale", 1, 1, 1, -5.0f, 5.0f), F("uniform", "uniform scale", 1.0f, -5.0f, 5.0f),
                      V("pivot", "pivot", 0, 0, 0, -10.0f, 10.0f),
                      M("order", "order", { "scale rotate translate", "scale translate rotate", "rotate scale translate",
                                            "rotate translate scale", "translate scale rotate", "translate rotate scale" }, 0),
                      T("vectors", "also vectors", ""), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            float t[3], r[3], s[3], pv[3];
            c.V3("t", t); c.V3("r", r); c.V3("s", s); c.V3("pivot", pv);
            const float u = c.F("uniform");
            const Mat4 S = Mat4::Scale(s[0] * u, s[1] * u, s[2] * u);
            const Mat4 R = Euler(r[0], r[1], r[2]);
            const Mat4 Tm = Mat4::Translation(t[0], t[1], t[2]);
            // Applied right to left: "scale rotate translate" = T * R * S.
            Mat4 M;
            switch (c.I("order"))
            {
               case 1: M = Mat4::Multiply(R, Mat4::Multiply(Tm, S)); break;
               case 2: M = Mat4::Multiply(Tm, Mat4::Multiply(S, R)); break;
               case 3: M = Mat4::Multiply(S, Mat4::Multiply(Tm, R)); break;
               case 4: M = Mat4::Multiply(R, Mat4::Multiply(S, Tm)); break;
               case 5: M = Mat4::Multiply(S, Mat4::Multiply(R, Tm)); break;
               default: M = Mat4::Multiply(Tm, Mat4::Multiply(R, S)); break;
            }
            M = Mat4::Multiply(Mat4::Translation(pv[0], pv[1], pv[2]),
                               Mat4::Multiply(M, Mat4::Translation(-pv[0], -pv[1], -pv[2])));
            float nm[9];
            M.NormalMatrix(nm);
            Mat4 NM;
            NM.m[0] = nm[0]; NM.m[1] = nm[1]; NM.m[2] = nm[2];
            NM.m[4] = nm[3]; NM.m[5] = nm[4]; NM.m[6] = nm[5];
            NM.m[8] = nm[6]; NM.m[9] = nm[7]; NM.m[10] = nm[8];
            KernelSpec k;
            k.reads = { { attr::P, 3 } };
            k.writes = { { attr::P, 3 } };
            k.body = "P = (uM * vec4(P, 1.0)).xyz;\n";
            const PopData* in = c.Input(0);
            if (in->Has(attr::N))
            {
               k.writes.push_back({ attr::N, 3 });
               k.body += "N = normalize(mat3(uNM) * N);\n";
            }
            for (const std::string& v : SplitList(c.S("vectors")))
            {
               if (k.writes.size() >= 4)
               {
                  c.warning = "only the first vector attributes were rotated (4 writes per kernel)";
                  break;
               }
               if (!ValidAttribName(v) || !in->Has(v) || in->CompsOf(v) != 3 || v == attr::P || v == attr::N)
                  continue;
               k.writes.push_back({ v, 3 });
               k.body += v + " = mat3(uM) * " + v + ";\n";
            }
            return Run(c, k, { Mat("uM", M), Mat("uNM", NM) });
         };
         defs.push_back(d);
      }

      // ---- Twist -----------------------------------------------------------
      {
         PopDef d = Simple("Twist PointZ", "Twist PointZ",
                           "Deforms along an axis: twist (rotate around it), bend (curl it into an arc), taper (scale the "
                           "cross-section) or shear (slide it sideways), growing from 'origin' over 'length'.", 1);
         d.params = { M("mode", "operation", { "twist", "bend", "taper", "shear" }, 0), M("axis", "axis", Axes(), 1),
                      F("amount", "amount", 90.0f, -720.0f, 720.0f), F("origin", "origin", 0.0f, -10.0f, 10.0f),
                      F("length", "length", 1.0f, 0.01f, 20.0f), B("clamp", "clamp to length", false), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            KernelSpec k;
            k.reads = { { attr::P, 3 }, { attr::N, 3 } };
            k.writes = { { attr::P, 3 } };
            if (c.Input(0)->Has(attr::N))
               k.writes.push_back({ attr::N, 3 });
            k.body = R"GLSL(
   int ax = p_axis;
   vec3 q = ax == 0 ? P.yzx : (ax == 1 ? P.zxy : P);   // q.z runs along the axis
   vec3 nq = ax == 0 ? N.yzx : (ax == 1 ? N.zxy : N);
   float t = (q.z - p_origin) / p_length;
   if (p_clamp != 0) t = clamp(t, 0.0, 1.0);
   if (p_mode == 0)
   {
      mat3 R = rotZ(radians(p_amount) * t);
      q = vec3((R * vec3(q.xy, 0.0)).xy, q.z);
      nq = R * nq;
   }
   else if (p_mode == 1)
   {
      float ang = radians(p_amount) * t;
      if (abs(ang) > 1e-5)
      {
         float rad = p_length / radians(p_amount);
         float along = q.z - p_origin;
         float a = along / rad;
         float y = q.y;
         q.z = p_origin + sin(a) * (rad - y);
         q.y = rad - cos(a) * (rad - y);
         nq = rotX(-a) * nq;
      }
   }
   else if (p_mode == 2)
   {
      float k = max(0.0, 1.0 + p_amount / 100.0 * t);
      q.xy *= k;
   }
   else
   {
      q.x += p_amount / 100.0 * t;
   }
   P = ax == 0 ? q.zxy : (ax == 1 ? q.yzx : q);
   N = normalize(ax == 0 ? nq.zxy : (ax == 1 ? nq.yzx : nq));
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Noise -----------------------------------------------------------
      {
         PopDef d = Simple("Noise PointZ", "Noise PointZ",
                           "Adds (or sets, or multiplies by) smooth noise on any attribute. Types: gradient, fractal (fbm), "
                           "curl (swirling, divergence-free, good for particles), ridged, turbulence, cells (Worley), value "
                           "and random. With 'translate 4D' on, time and the W offset move through a fourth dimension, so the "
                           "noise evolves in place instead of sliding. Sampled at 'coordinates' (P by default).", 1);
         d.params = { T("attrib", "attribute", "P"),
                      M("type", "type", { "gradient", "fractal", "curl", "ridged", "turbulence", "cells", "value", "random" }, 1),
                      M("mode", "combine", { "add", "set", "multiply" }, 0), F("amp", "amplitude", 0.2f, 0.0f, 5.0f),
                      V("axisAmp", "per axis", 1, 1, 1, 0.0f, 2.0f), F("bias", "offset (added)", 0.0f, -2.0f, 2.0f),
                      F("freq", "frequency", 1.5f, 0.0f, 20.0f), V("scale", "frequency per axis", 1, 1, 1, 0.0f, 10.0f),
                      V("offset", "translate", 0, 0, 0, -100.0f, 100.0f),
                      B("use4d", "translate 4D", true), F("t4d", "translate 4D (W)", 0.0f, -100.0f, 100.0f, "use4d=1"),
                      F("speed", "speed", 0.0f, -5.0f, 5.0f),
                      I("oct", "harmonics", 4, 1, 8, "type=1|3|4|5|6"), F("spread", "harmonic spread", 2.0f, 1.0f, 4.0f, "type=1|3|4|5|6"),
                      F("rough", "roughness (gain)", 0.5f, 0.0f, 1.0f, "type=1|3|4|5|6"),
                      F("expo", "exponent", 1.0f, 0.1f, 4.0f),
                      T("coord", "coordinates", "P"), F("seed", "seed", 0.0f, 0.0f, 100.0f), Mask() };
         d.animKey = "speed";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            std::string coord = c.S("coord");
            if (!ValidAttribName(coord))
               coord = attr::P;
            const int type = std::clamp(c.I("type"), 0, 7);
            const bool use4d = c.Bl("use4d");
            KernelSpec k;
            k.reads = { { t.name, t.comps } };
            AddRead(k, coord, 3);
            k.writes = { { t.name, t.comps } };
            // Type, dimension and channel count are compiled in: the GPU runs
            // only the branch in use (the program is cached per combination).
            static const int kBase[8] = { 0, 0, 0, 0, 0, 2, 1, 3 };   // gradient, value, cells, random
            static const int kLayer[8] = { 0, 1, 0, 2, 3, 1, 1, 0 };  // single, fbm, ridged, turbulence
            k.decls = "\n#define NBASE " + std::to_string(kBase[type]) + "\n#define NLAYER " + std::to_string(kLayer[type]) +
                      "\n#define NCURL " + std::string(type == 2 ? "1" : "0") + "\n#define N4D " + (use4d ? "1" : "0") +
                      "\n#define NCOMPS " + std::to_string(t.comps) + "\n" + R"GLSL(
float nBase(vec4 q, int id)
{
#if NBASE == 3
   return hashi(id, p_seed + floor(q.w) * 7.13) * 2.0 - 1.0;
#elif N4D == 1
#if NBASE == 0
   return gnoise4(q);
#elif NBASE == 1
   return valnoise4(q);
#else
   return cellnoise4(q);
#endif
#else
#if NBASE == 0
   return gnoise(q.xyz);
#elif NBASE == 1
   return valnoise4(vec4(q.xyz, 0.0));
#else
   return cellnoise4(vec4(q.xyz, 0.0));
#endif
#endif
}
float nLayered(vec4 q, int id)
{
#if NLAYER == 0
   return nBase(q, id);
#else
   float sum = 0.0, amp = 1.0, norm = 0.0;
   for (int o = 0; o < 8; o++)
   {
      if (o >= p_oct) break;
      float n = nBase(q, id);
#if NLAYER == 2
      n = 1.0 - abs(n);
      n = n * n * 2.0 - 1.0;
#elif NLAYER == 3
      n = abs(n) * 2.0 - 1.0;
#endif
      sum += n * amp;
      norm += amp;
      amp *= p_rough;
      q = vec4(q.xyz * p_spread + vec3(17.1, 9.7, 3.3), q.w * p_spread + 5.1);
   }
   return norm > 0.0 ? sum / norm : 0.0;
#endif
}
vec3 nPot(vec4 q, int id)
{
   return vec3(nBase(q, id), nBase(q + vec4(31.4, 47.9, 11.3, 7.7), id), nBase(q + vec4(-23.7, 13.1, 61.7, -3.1), id));
}
vec3 nCurl(vec4 q, int id)
{
   const float e = 0.05;
   vec4 dx = vec4(e, 0.0, 0.0, 0.0), dy = vec4(0.0, e, 0.0, 0.0), dz = vec4(0.0, 0.0, e, 0.0);
   vec3 px0 = nPot(q - dx, id), px1 = nPot(q + dx, id);
   vec3 py0 = nPot(q - dy, id), py1 = nPot(q + dy, id);
   vec3 pz0 = nPot(q - dz, id), pz1 = nPot(q + dz, id);
   return vec3((py1.z - py0.z) - (pz1.y - pz0.y), (pz1.x - pz0.x) - (px1.z - px0.z), (px1.y - px0.y) - (py1.x - py0.x)) / (2.0 * e);
}
)GLSL";
            k.body = Defines(t) + "   vec3 q3 = vec3(" + coord + ") * p_freq * p_scale + p_offset + vec3(p_seed * 17.31, p_seed * 3.7, 0.0);\n" + R"GLSL(
#if N4D == 1
   vec4 q = vec4(q3, p_t4d + uTime * p_speed + p_seed * 1.37);
#else
   vec4 q = vec4(q3 + vec3(0.0, 0.0, uTime * p_speed), 0.0);
#endif
   vec4 n = vec4(0.0);
#if NCURL == 1
   n.xyz = nCurl(q, id) * 0.25;
#if NCOMPS > 3
   n.w = nBase(q + vec4(5.2, 1.3, 9.1, 2.9), id);
#endif
#else
   n.x = nLayered(q, id);
#if NCOMPS > 1
   n.y = nLayered(q + vec4(31.4, 47.9, 11.3, 7.7), id + 7919);
#endif
#if NCOMPS > 2
   n.z = nLayered(q + vec4(-23.7, 13.1, 61.7, -3.1), id + 15887);
#endif
#if NCOMPS > 3
   n.w = nLayered(q + vec4(5.2, 1.3, 9.1, 2.9), id + 23873);
#endif
#endif
   if (p_expo != 1.0) n = sign(n) * pow(abs(n), vec4(p_expo));
   n = n * vec4(p_axisAmp, 1.0) * p_amp + vec4(p_bias);
   if (p_mode == 0) A += TA(n.SW);
   else if (p_mode == 1) A = TA(n.SW);
   else A *= TA((vec4(1.0) + n).SW);
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Random ----------------------------------------------------------
      {
         PopDef d = Simple("Random PointZ", "Random PointZ",
                           "Writes a random value per point into an attribute, between 'min' and 'max' (per component), "
                           "uniform or gaussian, optionally whole numbers. Same seed, same values.", 1);
         d.params = { T("attrib", "attribute", "Color"), M("mode", "combine", { "set", "add", "multiply" }, 0),
                      V("lo", "min", 0, 0, 0, -10.0f, 10.0f), V("hi", "max", 1, 1, 1, -10.0f, 10.0f),
                      F("lo4", "min (4th)", 1.0f, -10.0f, 10.0f), F("hi4", "max (4th)", 1.0f, -10.0f, 10.0f),
                      M("dist", "distribution", { "uniform", "gaussian" }, 0), B("integer", "whole numbers", false),
                      F("seed", "seed", 1.0f, 0.0f, 1000.0f), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            KernelSpec k;
            k.reads = { { t.name, t.comps } };
            k.writes = { { t.name, t.comps } };
            k.body = Defines(t) + R"GLSL(
   vec4 h = vec4(hash3i(id, p_seed), hashi(id, p_seed + 17.0));
   if (p_dist == 1)
   {
      vec4 h2 = vec4(hash3i(id, p_seed + 101.0), hashi(id, p_seed + 211.0));
      vec4 g = sqrt(-2.0 * log(max(h, vec4(1e-7)))) * cos(TAU * h2);
      h = clamp(0.5 + g * 0.18, 0.0, 1.0);
   }
   vec4 lo = vec4(p_lo, p_lo4), hi = vec4(p_hi, p_hi4);
   vec4 r = mix(lo, hi, h);
   if (p_integer != 0) r = floor(r + 0.5);
   if (p_mode == 0) A = TA(r.SW);
   else if (p_mode == 1) A += TA(r.SW);
   else A *= TA(r.SW);
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Math ------------------------------------------------------------
      {
         PopDef d = Simple("Math PointZ", "Math PointZ",
                           "Arithmetic on one attribute: (value + pre add) x multiply, through a function, + post add.", 1);
         d.params = { T("attrib", "attribute", "P"), T("dest", "output (empty = same)", ""),
                      F("pre", "pre add", 0.0f, -10.0f, 10.0f), F("mul", "multiply", 1.0f, -10.0f, 10.0f),
                      M("fn", "function", { "none", "abs", "negate", "sqrt", "square", "power", "exp", "log", "sign", "floor",
                                            "ceil", "fract", "round", "1 / x", "1 - x", "clamp 0..1", "length", "normalize",
                                            "sin", "cos" }, 0),
                      F("exp", "exponent", 2.0f, -8.0f, 8.0f, "fn=5"), F("post", "post add", 0.0f, -10.0f, 10.0f), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target src;
            if (!ResolveTarget(c, "attrib", src))
               return false;
            Target dst = src;
            if (!c.S("dest").empty())
            {
               if (!ResolveTarget(c, "dest", dst, src.comps))
                  return false;
            }
            KernelSpec k;
            k.reads = { { src.name, src.comps } };
            k.writes = { { dst.name, dst.comps } };
            k.body = Defines(src) + R"GLSL(
   TA x = (A + TA(p_pre)) * TA(p_mul);
   int f = p_fn;
   if (f == 1) x = abs(x);
   else if (f == 2) x = -x;
   else if (f == 3) x = sqrt(max(x, TA(0.0)));
   else if (f == 4) x = x * x;
   else if (f == 5) x = sign(x) * pow(abs(x), TA(p_exp));
   else if (f == 6) x = exp(x);
   else if (f == 7) x = log(max(x, TA(1e-12)));
   else if (f == 8) x = sign(x);
   else if (f == 9) x = floor(x);
   else if (f == 10) x = ceil(x);
   else if (f == 11) x = fract(x);
   else if (f == 12) x = floor(x + TA(0.5));
   else if (f == 13) x = TA(1.0) / x;
   else if (f == 14) x = TA(1.0) - x;
   else if (f == 15) x = clamp(x, TA(0.0), TA(1.0));
   else if (f == 16) x = TA(length(x));
   else if (f == 17) x = length(x) > 1e-12 ? x / length(x) : x;
   else if (f == 18) x = sin(x);
   else if (f == 19) x = cos(x);
   x += TA(p_post);
)GLSL" + "   " + dst.name + " = x;\n";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- ReRange ---------------------------------------------------------
      {
         PopDef d = Simple("ReRange PointZ", "ReRange PointZ", "Maps an attribute from one range onto another (every component), optionally clamped.", 1);
         d.params = { T("attrib", "attribute", "P"), F("inLo", "from low", 0.0f, -10.0f, 10.0f), F("inHi", "from high", 1.0f, -10.0f, 10.0f),
                      F("outLo", "to low", 0.0f, -10.0f, 10.0f), F("outHi", "to high", 1.0f, -10.0f, 10.0f),
                      B("clamp", "clamp", false), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            KernelSpec k;
            k.reads = { { t.name, t.comps } };
            k.writes = { { t.name, t.comps } };
            k.body = Defines(t) + R"GLSL(
   float span = (p_inHi - p_inLo) == 0.0 ? 1e-9 : (p_inHi - p_inLo);
   TA u = (A - TA(p_inLo)) / span;
   if (p_clamp != 0) u = clamp(u, TA(0.0), TA(1.0));
   A = TA(p_outLo) + u * (p_outHi - p_outLo);
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Quantize --------------------------------------------------------
      {
         PopDef d = Simple("Quantize PointZ", "Quantize PointZ", "Snaps an attribute to a grid of 'step' (per axis for vectors), rounding, flooring or ceiling.", 1);
         d.params = { T("attrib", "attribute", "P"), V("step", "step", 0.1f, 0.1f, 0.1f, 0.0f, 5.0f), F("step4", "step (4th)", 0.1f, 0.0f, 5.0f),
                      V("offset", "offset", 0, 0, 0, -5.0f, 5.0f), M("how", "rounding", { "round", "floor", "ceil" }, 0), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            KernelSpec k;
            k.reads = { { t.name, t.comps } };
            k.writes = { { t.name, t.comps } };
            k.body = Defines(t) + R"GLSL(
   vec4 st = max(vec4(p_step, p_step4), vec4(1e-9));
   vec4 off = vec4(p_offset, 0.0);
   vec4 v = vec4(0.0);
   v.SW = A;
   vec4 g = (v - off) / st;
   g = p_how == 1 ? floor(g) : (p_how == 2 ? ceil(g) : floor(g + 0.5));
   A = TA((g * st + off).SW);
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Trig ------------------------------------------------------------
      {
         PopDef d = Simple("Trig PointZ", "Trig PointZ", "Trigonometric function on an attribute: amplitude x f(value x frequency + phase). 'cycles' units make 1.0 one full turn.", 1);
         d.params = { T("attrib", "attribute", "P"), T("dest", "output (empty = same)", ""),
                      M("fn", "function", { "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh" }, 0),
                      M("units", "units", { "radians", "cycles", "degrees" }, 1), F("freq", "frequency", 1.0f, -20.0f, 20.0f),
                      F("phase", "phase", 0.0f, -10.0f, 10.0f), F("amp", "amplitude", 1.0f, -10.0f, 10.0f),
                      F("speed", "speed", 0.0f, -10.0f, 10.0f), Mask() };
         d.animKey = "speed";
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target src;
            if (!ResolveTarget(c, "attrib", src))
               return false;
            Target dst = src;
            if (!c.S("dest").empty() && !ResolveTarget(c, "dest", dst, src.comps))
               return false;
            KernelSpec k;
            k.reads = { { src.name, src.comps } };
            k.writes = { { dst.name, dst.comps } };
            k.body = Defines(src) + R"GLSL(
   float unit = p_units == 1 ? TAU : (p_units == 2 ? PI / 180.0 : 1.0);
   TA x = (A * p_freq + TA(p_phase + uTime * p_speed)) * unit;
   int f = p_fn;
   TA y;
   if (f == 0) y = sin(x); else if (f == 1) y = cos(x); else if (f == 2) y = tan(x);
   else if (f == 3) y = asin(clamp(x, TA(-1.0), TA(1.0))); else if (f == 4) y = acos(clamp(x, TA(-1.0), TA(1.0)));
   else if (f == 5) y = atan(x); else if (f == 6) y = sinh(x); else if (f == 7) y = cosh(x); else y = tanh(x);
)GLSL" + "   " + dst.name + " = y * p_amp;\n";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Limit -----------------------------------------------------------
      {
         PopDef d = Simple("Limit PointZ", "Limit PointZ", "Keeps an attribute inside a box: clamp to its walls, wrap around (exit one side, enter the other) or mirror back.", 1);
         d.params = { T("attrib", "attribute", "P"), M("mode", "mode", { "clamp", "wrap", "mirror" }, 0),
                      V("lo", "min", -1, -1, -1, -20.0f, 20.0f), V("hi", "max", 1, 1, 1, -20.0f, 20.0f), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            KernelSpec k;
            k.reads = { { t.name, t.comps } };
            k.writes = { { t.name, t.comps } };
            k.body = Defines(t) + R"GLSL(
   vec4 lo = vec4(p_lo, -1e30), hi = vec4(p_hi, 1e30);
   vec4 v = vec4(0.0);
   v.SW = A;
   vec4 span = max(hi - lo, vec4(1e-9));
   if (p_mode == 0) v = clamp(v, lo, hi);
   else if (p_mode == 1) v = lo + mod(v - lo, span);
   else { vec4 m = mod(v - lo, 2.0 * span); v = lo + span - abs(m - span); }
   A = TA(v.SW);
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Normalize -------------------------------------------------------
      {
         PopDef d = Simple("Normalize PointZ", "Normalize PointZ",
                           "Vectors: scales each to 'length' (unit by default). Single values: stretches the attribute's "
                           "own range across all points to 0..1.", 1);
         d.params = { T("attrib", "attribute", "N"), F("length", "length", 1.0f, 0.0f, 10.0f), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            Target t;
            if (!ResolveTarget(c, "attrib", t))
               return false;
            KernelSpec k;
            k.reads = { { t.name, t.comps } };
            k.writes = { { t.name, t.comps } };
            if (t.comps == 1)
            {
               const std::vector<float> v = c.Input(0)->Read(t.name, 1);
               float lo = 1e30f, hi = -1e30f;
               for (float x : v)
                  if (std::isfinite(x)) { lo = std::min(lo, x); hi = std::max(hi, x); }
               if (!(lo <= hi)) { lo = 0.0f; hi = 1.0f; }
               k.body = t.name + " = (" + t.name + " - uLo) / max(uHi - uLo, 1e-9);\n";
               return Run(c, k, { Flt("uLo", lo), Flt("uHi", hi) });
            }
            k.body = Defines(t) + "   float l = length(A);\n   if (l > 1e-12) A = A / l * p_length;\n";
            return Run(c, k);
         };
         defs.push_back(d);
      }

      // ---- Projection ------------------------------------------------------
      {
         PopDef d = Simple("Projection PointZ", "Projection PointZ",
                           "Pushes points onto a shape - a plane, a sphere or a cylinder - by 'amount' (0 leaves them, 1 lands "
                           "them on it). Normals follow when 'set normals' is on.", 1);
         d.params = { M("shape", "shape", { "plane", "sphere", "cylinder" }, 1), M("axis", "axis", Axes(), 1),
                      V("center", "center", 0, 0, 0, -10.0f, 10.0f), F("radius", "radius / offset", 0.5f, -10.0f, 10.0f),
                      F("amount", "amount", 1.0f, 0.0f, 1.0f), B("setN", "set normals", true), Mask() };
         d.cook = [](CookCtx& c) {
            if (!PassInput(c))
               return true;
            KernelSpec k;
            k.reads = { { attr::P, 3 }, { attr::N, 3 } };
            k.writes = { { attr::P, 3 }, { attr::N, 3 } };
            k.body = R"GLSL(
   vec3 ax = p_axis == 0 ? vec3(1, 0, 0) : (p_axis == 1 ? vec3(0, 1, 0) : vec3(0, 0, 1));
   vec3 d = P - p_center;
   vec3 target = P;
   vec3 n = N;
   if (p_shape == 0)
   {
      float h = dot(d, ax) - p_radius;
      target = P - ax * h;
      n = ax;
   }
   else if (p_shape == 1)
   {
      float l = length(d);
      vec3 dir = l > 1e-9 ? d / l : vec3(0, 1, 0);
      target = p_center + dir * p_radius;
      n = dir;
   }
   else
   {
      vec3 r = d - ax * dot(d, ax);
      float l = length(r);
      vec3 dir = l > 1e-9 ? r / l : normalize(cross(ax, vec3(0.31, 0.57, 0.76)));
      target = p_center + ax * dot(d, ax) + dir * p_radius;
      n = dir;
   }
   P = mix(P, target, p_amount);
   if (p_setN != 0) N = normalize(mix(N, n, p_amount));
)GLSL";
            return Run(c, k);
         };
         defs.push_back(d);
      }
   }
}

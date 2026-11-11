#pragma once

// Hair: the Chiang 2016 fibre BSDF (Chiang, Bitterli, Tappan, Burley, "A Practical and Controllable Hair and Fur
// Model for Production Path Tracing"), written from the paper, as Blender's Principled Hair (Chiang) uses it.
// One source compiles as GLSL (forward shader, Geometry3DNodes.cpp) and as MSL (live and settle, MetalDevice.mm):
// the includer defines HAIR_ATAN2(y, x) and HAIR_OUT (an out float parameter), and MSL typedefs vec3 = float3.
//
// The fibre frame: T along the strand (Fur's Tangent normals put it in the vertex normal), X the part of wo across
// the fibre (so phi_o = 0), Y = T x X. h in -1..1 is where the ray meets the fibre's width.
// Lobes p = 0 R, 1 TT, 2 TRT, 3 the rest (TRRT+, one isotropic term). Each is
//   Mp (longitudinal, roughness v_p, cuticle tilt alpha) * Ap (Fresnel and absorption sigma_a) * Np (azimuthal,
//   a trimmed logistic of scale s around the exit angle Phi(p, gamma_o, gamma_t)).
// hairBsdf returns f * |cos theta_i| (the cosine cancels), so a light adds f * radiance, a bounce f / pdf.

#include <algorithm>
#include <cmath>

#define R3D_HAIR_SRC \
   "#ifndef HAIR_MS\n#define HAIR_MS 4.0\n#endif\n" \
   "float hairI0(float x) { float v = 1.0, t = 1.0, q = x * x * 0.25; for (int i = 1; i < 10; i++) { t *= q / float(i * i); v += t; } return v; }\n" \
   "float hairLogI0(float x) { return x > 12.0 ? x + 0.5 * (-log(6.2831853) + log(1.0 / x) + 1.0 / (8.0 * x)) : log(hairI0(x)); }\n" \
   "float hairMp(float cti, float cto, float sti, float sto, float v) {\n" \
   "   float a = cti * cto / v, b = sti * sto / v;\n" \
   "   return v <= 0.1 ? exp(hairLogI0(a) - b - 1.0 / v + 0.6931 + log(1.0 / (2.0 * v))) : exp(-b) * hairI0(a) / (sinh(1.0 / v) * 2.0 * v);\n" \
   "}\n" \
   "float hairCdf(float x, float s) { return 1.0 / (1.0 + exp(-x / s)); }\n" \
   "float hairNp(float dphi, float s) {\n" \
   "   dphi = dphi - 6.2831853 * floor((dphi + 3.14159265) / 6.2831853);\n" \
   "   float e = exp(-abs(dphi) / s);\n" \
   "   return e / (s * (1.0 + e) * (1.0 + e)) / (hairCdf(3.14159265, s) - hairCdf(-3.14159265, s));\n" \
   "}\n" \
   "float hairPhi(float p, float go, float gt) { return 2.0 * p * gt - 2.0 * go + p * 3.14159265; }\n" \
   "float hairFresnel(float ci, float eta) {\n" \
   "   ci = clamp(ci, 0.0, 1.0); float st2 = (1.0 - ci * ci) / (eta * eta);\n" \
   "   if (st2 >= 1.0) return 1.0;\n" \
   "   float ct = sqrt(1.0 - st2), rs = (eta * ci - ct) / (eta * ci + ct), rp = (ci - eta * ct) / (ci + eta * ct);\n" \
   "   return 0.5 * (rs * rs + rp * rp);\n" \
   "}\n" \
   "vec3 hairX(vec3 wo, vec3 T, float sto) {\n" \
   "   vec3 px = wo - T * sto; float pl = length(px);\n" \
   "   return pl > 1e-5 ? px / pl : normalize(abs(T.y) < 0.9 ? cross(T, vec3(0.0, 1.0, 0.0)) : cross(T, vec3(1.0, 0.0, 0.0)));\n" \
   "}\n" \
   "float hairV0(float bm) { float v = 0.726 * bm + 0.812 * bm * bm + 3.7 * pow(bm, 20.0); return max(v * v, 1e-4); }\n" \
   "float hairS(float bn) { return max(0.626657 * (0.265 * bn + 1.194 * bn * bn + 5.372 * pow(bn, 22.0)), 1e-3); }\n" \
   /* f * cos and the pdf hairSample draws wi with (lobe chosen by its share of Ap). */ \
   "vec3 hairBsdf(vec3 wo, vec3 wi, vec3 T, vec3 sa, float bm, float bn, float alpha, float eta, float h, HAIR_OUT pdf) {\n" \
   "   float sto = clamp(dot(wo, T), -1.0, 1.0), cto = sqrt(max(1.0 - sto * sto, 0.0));\n" \
   "   float sti = clamp(dot(wi, T), -1.0, 1.0), cti = sqrt(max(1.0 - sti * sti, 0.0));\n" \
   "   vec3 X = hairX(wo, T, sto), Y = cross(T, X);\n" \
   "   float phi = HAIR_ATAN2(dot(wi, Y), dot(wi, X));\n" \
   "   float stt = sto / eta, ctt = sqrt(max(1.0 - stt * stt, 1e-4));\n" \
   "   float etap = sqrt(max(eta * eta - sto * sto, 0.0)) / max(cto, 1e-4);\n" \
   "   float sgt = clamp(h / etap, -1.0, 1.0), cgt = sqrt(max(1.0 - sgt * sgt, 0.0));\n" \
   "   float go = asin(clamp(h, -1.0, 1.0)), gt = asin(sgt);\n" \
   "   vec3 tr = exp(-sa * (2.0 * cgt / ctt));\n" \
   "   float fr = hairFresnel(cto * sqrt(max(1.0 - h * h, 0.0)), eta);\n" \
   "   vec3 a0 = vec3(fr), a1 = (1.0 - fr) * (1.0 - fr) * tr, a2 = a1 * tr * fr;\n" \
   "   vec3 a3 = a2 * tr * fr / max(vec3(1.0) - tr * fr, vec3(1e-4));\n" \
   "   float v0 = hairV0(bm), s = hairS(bn);\n" \
   "   float s1 = sin(alpha), c1 = cos(alpha), s2 = 2.0 * s1 * c1, c2 = c1 * c1 - s1 * s1, s4 = 2.0 * s2 * c2, c4 = c2 * c2 - s2 * s2;\n" \
   "   float m0 = hairMp(cti, abs(cto * c2 + sto * s2), sti, sto * c2 - cto * s2, v0);\n" \
   "   float m1 = hairMp(cti, abs(cto * c1 - sto * s1), sti, sto * c1 + cto * s1, 0.25 * v0);\n" \
   "   float m2 = hairMp(cti, abs(cto * c4 - sto * s4), sti, sto * c4 + cto * s4, 4.0 * v0);\n" \
   "   float m3 = hairMp(cti, cto, sti, sto, 4.0 * v0);\n" \
   "   float n0 = hairNp(phi - hairPhi(0.0, go, gt), s), n1 = hairNp(phi - hairPhi(1.0, go, gt), s), n2 = hairNp(phi - hairPhi(2.0, go, gt), s);\n" \
   "   const vec3 lw = vec3(0.2126, 0.7152, 0.0722);\n" \
   "   vec4 ap = vec4(dot(a0, lw), dot(a1, lw), dot(a2, lw), dot(a3, lw)); ap /= max(ap.x + ap.y + ap.z + ap.w, 1e-6);\n" \
   "   pdf = m0 * ap.x * n0 + m1 * ap.y * n1 + m2 * ap.z * n2 + m3 * ap.w * 0.15915494;\n" \
   "   return m0 * a0 * n0 + m1 * a1 * n1 + m2 * a2 * n2 + m3 * a3 * 0.15915494;\n" \
   "}\n" \
   /* The colour the strand reads as under diffuse light (ambient, GI): Chiang's fit inverted, as r3dhair::ColourFromSigma. */ \
   "vec3 hairAlbedo(vec3 sa, float bn) {\n" \
   "   float d = 5.969 - 0.215 * bn + 2.532 * bn * bn - 10.73 * pow(bn, 3.0) + 5.574 * pow(bn, 4.0) + 0.245 * pow(bn, 5.0);\n" \
   "   return exp(-sqrt(max(sa, vec3(0.0))) * d);\n" \
   "}\n" \
   /* Live and raster: one evaluation per light, averaged over four places across the fibre, plus the light that */ \
   /* bounced between strands (settle traces it): the albedo, Kajiya's sin(T, wi) diffuse. */ \
   "vec3 hairAvg(vec3 wo, vec3 wi, vec3 T, vec3 sa, float bm, float bn, float alpha, float eta) {\n" \
   "   float pd = 0.0, ti = dot(wi, T);\n" \
   "   return 0.25 * (hairBsdf(wo, wi, T, sa, bm, bn, alpha, eta, -0.75, pd) + hairBsdf(wo, wi, T, sa, bm, bn, alpha, eta, -0.25, pd)\n" \
   "                + hairBsdf(wo, wi, T, sa, bm, bn, alpha, eta, 0.25, pd) + hairBsdf(wo, wi, T, sa, bm, bn, alpha, eta, 0.75, pd))\n" \
   "        + hairAlbedo(sa, bn) * (HAIR_MS * 0.31830989 * sqrt(max(1.0 - ti * ti, 0.0)));\n" \
   "}\n"

namespace r3dhair
{
   // Melanin (0 white .. 1 black, redness = pheomelanin share) to absorption, Blender's mapping: concentration
   // -log(1 - melanin), eumelanin and pheomelanin absorption per unit concentration.
   inline void SigmaFromMelanin(float melanin, float redness, float out[3])
   {
      const float q = -std::log(std::max(1.0f - std::min(std::max(melanin, 0.0f), 1.0f), 1e-4f));
      const float r = std::min(std::max(redness, 0.0f), 1.0f);
      const float eu[3] = { 0.506f, 0.841f, 1.653f }, ph[3] = { 0.343f, 0.733f, 1.924f };
      for (int k = 0; k < 3; k++)
         out[k] = q * (1.0f - r) * eu[k] + q * r * ph[k];
   }
   // Chiang's fit: the absorption whose multiple-scattered colour is c (linear) at radial roughness bn.
   inline float ReflectanceDenom(float bn)
   {
      return 5.969f - 0.215f * bn + 2.532f * bn * bn - 10.73f * std::pow(bn, 3.0f) + 5.574f * std::pow(bn, 4.0f)
             + 0.245f * std::pow(bn, 5.0f);
   }
   inline void SigmaFromColour(const float lin[3], float bn, float out[3])
   {
      const float d = ReflectanceDenom(bn);
      for (int k = 0; k < 3; k++)
      {
         const float l = std::log(std::min(std::max(lin[k], 1e-4f), 1.0f)) / d;
         out[k] = l * l;
      }
   }
   // The inverse: the colour hair of this absorption reads as. Ambient and GI light it as a diffuse albedo.
   inline void ColourFromSigma(const float sa[3], float bn, float out[3])
   {
      const float d = ReflectanceDenom(bn);
      for (int k = 0; k < 3; k++)
         out[k] = std::exp(-std::sqrt(std::max(sa[k], 0.0f)) * d);
   }
   // A Material's hair absorption. mode 1 Melanin (plus the tint as a dye), 2 Colour (the base colour is the hair's).
   // Colours are linear.
   inline void Sigma(int mode, float melanin, float redness, const float linColour[3], const float linTint[3], float bn,
                     float out[3])
   {
      if (mode == 2)
      {
         SigmaFromColour(linColour, bn, out);
         return;
      }
      SigmaFromMelanin(melanin, redness, out);
      float dye[3];
      SigmaFromColour(linTint, bn, dye);
      for (int k = 0; k < 3; k++)
         out[k] += dye[k];
   }
}

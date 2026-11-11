#pragma once

// Thin-film iridescence, one source for the GL forward shader (Geometry3DNodes.cpp) and Metal (MetalDevice.mm,
// where vec3 is float3). The physical film (Airy summation of the two-interface reflections, s and p averaged, three wavelengths per
// channel): air -> film (ior n1, thickness d nm) -> base, whose ior comes from f0. thinFilmGain is that reflectance
// over the bare base's (the same formula at d = 0), so a lobe's own Fresnel times the gain carries the tint and
// a film of zero thickness changes nothing. Belcour and Barla 2017 describe the model; this is the direct sum.
#define R3D_THINFILM_SRC \
   "vec3 thinFilmR(float c0, float n1, float d, vec3 f0) {\n" \
   "   c0 = clamp(c0, 1e-3, 1.0);\n" \
   "   float s2 = 1.0 - c0 * c0;\n" \
   "   float c1 = sqrt(max(1.0 - s2 / (n1 * n1), 0.0));\n" \
   "   vec3 sq = sqrt(clamp(f0, vec3(0.0), vec3(0.98)));\n" \
   "   vec3 n2 = (vec3(1.0) + sq) / (vec3(1.0) - sq);\n" \
   "   vec3 c2 = sqrt(max(vec3(1.0) - vec3(s2) / (n2 * n2), vec3(0.0)));\n" \
   "   float rs01 = (c0 - n1 * c1) / (c0 + n1 * c1), rp01 = (n1 * c0 - c1) / (n1 * c0 + c1);\n" \
   "   vec3 rs12 = (vec3(n1 * c1) - n2 * c2) / (vec3(n1 * c1) + n2 * c2);\n" \
   "   vec3 rp12 = (n2 * c1 - vec3(n1) * c2) / (n2 * c1 + vec3(n1) * c2);\n" \
   "   float opd = 4.0 * 3.14159265 * n1 * d * c1;\n" \
   "   vec3 R = vec3(0.0);\n" \
   "   for (int k = 0; k < 3; k++) {\n" \
   "      vec3 cp = cos(vec3(opd) / (vec3(610.0, 550.0, 465.0) * (0.93 + 0.07 * float(k))));\n" \
   "      R += (rs01 * rs01 + rs12 * rs12 + 2.0 * rs01 * rs12 * cp) / (vec3(1.0) + rs01 * rs01 * rs12 * rs12 + 2.0 * rs01 * rs12 * cp);\n" \
   "      R += (rp01 * rp01 + rp12 * rp12 + 2.0 * rp01 * rp12 * cp) / (vec3(1.0) + rp01 * rp01 * rp12 * rp12 + 2.0 * rp01 * rp12 * cp);\n" \
   "   }\n" \
   "   return R / 6.0;\n" \
   "}\n" \
   "vec3 thinFilmGain(float nV, float n1, float d, vec3 f0, float w) {\n" \
   "   return mix(vec3(1.0), thinFilmR(nV, n1, d, f0) / max(thinFilmR(nV, n1, 0.0, f0), vec3(1e-4)), w);\n" \
   "}\n"

#pragma once

// Render 3D's tonemaps and sRGB curves, one source for every engine: the GL forward shaders
// (HDR off: tonemapped per fragment), the HDR finish (Render3DHdr.cpp), the GL path tracer and
// Metal (live, settle). Written in syntax GLSL and MSL share; the MSL includer typedefs
// vec3 = float3 and mat3 = float3x3 first.
//   toneMap(c, t) - the node's order: 1 ACES (Narkowicz fit), 2 Reinhard, 3 AgX, 4 Neutral.
// Both take and return linear light.
//   agxTone     - AgX (Troy Sobotka's display transform): inset matrix, log2 encoding
//                 over -12.47..+4.03 stops, the contrast curve, outset matrix, and a
//                 Rec.1886 (2.4 power) decode back to linear, as Blender's AgX view ends.
//                 The curve is our own 8th-order fit to Blender 5.2's AgX measured on a
//                 512-step grey ramp (tools/blender-ab, 2026-11-10): within 1.6/255 of it;
//                 the older minimal 6th-order fit lifted shadows up to 1.5x.
//   neutralTone - Khronos PBR Neutral (Khronos Group, 2024): colours below 0.76
//                 pass unchanged, highlights compress toward white.
#define R3D_TONEMAP_SRC \
   "vec3 acesFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n" \
   "vec3 agxTone(vec3 c) {\n" \
   "   const mat3 inset = mat3(vec3(0.842479062253094, 0.0423282422610123, 0.0423756549057051),\n" \
   "                           vec3(0.0784335999999992, 0.878468636469772, 0.0784336),\n" \
   "                           vec3(0.0792237451477643, 0.0791661274605434, 0.879142973793104));\n" \
   "   const mat3 outset = mat3(vec3(1.19687900512017, -0.0528968517574562, -0.0529716355144438),\n" \
   "                            vec3(-0.0980208811401368, 1.15190312990417, -0.0980434501171241),\n" \
   "                            vec3(-0.0990297440797205, -0.0989611768448433, 1.15107367264116));\n" \
   "   vec3 x = clamp((log2(max(inset * c, vec3(1e-10))) + 12.47393) / 16.5, 0.0, 1.0);\n" \
   "   x = clamp(((((((((-246.535442) * x + 984.731367) * x - 1568.41656) * x + 1268.06241) * x - 553.234808) * x + 130.459845) * x - 14.9498445) * x + 0.887512984) * x - 0.00843336545, 0.0, 1.0);\n" \
   "   x = max(outset * x, vec3(0.0));\n" \
   "   return pow(x, vec3(2.4));\n" \
   "}\n" \
   "vec3 neutralTone(vec3 c) {\n" \
   "   float x = min(c.r, min(c.g, c.b));\n" \
   "   c -= (x < 0.08) ? x - 6.25 * x * x : 0.04;\n" \
   "   float peak = max(c.r, max(c.g, c.b));\n" \
   "   if (peak < 0.76) return c;\n" \
   "   float np = 1.0 - 0.0576 / (peak - 0.52);\n" \
   "   c *= np / peak;\n" \
   "   return mix(c, vec3(np), 1.0 - 1.0 / (0.15 * (peak - np) + 1.0));\n" \
   "}\n" \
   "vec3 toneMap(vec3 c, int t) {\n" \
   "   if (t == 1) return acesFilm(c);\n" \
   "   if (t == 2) return c / (c + vec3(1.0));\n" \
   "   if (t == 3) return agxTone(c);\n" \
   "   if (t == 4) return neutralTone(c);\n" \
   "   return c;\n" \
   "}\n"

// sRGB transfer, both ways (shared syntax too).
#define R3D_SRGB_SRC \
   "vec3 toLinear(vec3 c) { c = max(c, vec3(0.0)); return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }\n" \
   "vec3 toSrgb(vec3 c) { c = max(c, vec3(0.0)); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }\n"

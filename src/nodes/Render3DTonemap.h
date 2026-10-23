#pragma once

// GLSL for Render 3D's AgX and Neutral tonemaps, shared by the forward shaders
// (HDR off: tonemapped per fragment) and the HDR finish (Render3DHdr.cpp).
// Both take and return linear light.
//   agxTone     - AgX (Troy Sobotka's display transform) in its widely published
//                 minimal form: inset matrix, log2 encoding over -12.47..+4.03
//                 stops, a 6th-order fit of the contrast curve, outset matrix.
//   neutralTone - Khronos PBR Neutral (Khronos Group, 2024): colours below 0.76
//                 pass unchanged, highlights compress toward white.
#define R3D_TONEMAP_GLSL \
   "vec3 agxTone(vec3 c) {\n" \
   "   const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,\n" \
   "                           0.0784335999999992, 0.878468636469772, 0.0784336,\n" \
   "                           0.0792237451477643, 0.0791661274605434, 0.879142973793104);\n" \
   "   const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,\n" \
   "                            -0.0980208811401368, 1.15190312990417, -0.0980434501171241,\n" \
   "                            -0.0990297440797205, -0.0989611768448433, 1.15107367264116);\n" \
   "   vec3 x = clamp((log2(max(inset * c, vec3(1e-10))) + 12.47393) / 16.5, 0.0, 1.0);\n" \
   "   vec3 x2 = x * x, x4 = x2 * x2;\n" \
   "   x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;\n" \
   "   return pow(max(outset * x, vec3(0.0)), vec3(2.2));\n" \
   "}\n" \
   "vec3 neutralTone(vec3 c) {\n" \
   "   float x = min(c.r, min(c.g, c.b));\n" \
   "   c -= (x < 0.08) ? x - 6.25 * x * x : 0.04;\n" \
   "   float peak = max(c.r, max(c.g, c.b));\n" \
   "   if (peak < 0.76) return c;\n" \
   "   float np = 1.0 - 0.0576 / (peak - 0.52);\n" \
   "   c *= np / peak;\n" \
   "   return mix(c, vec3(np), 1.0 - 1.0 / (0.15 * (peak - np) + 1.0));\n" \
   "}\n"

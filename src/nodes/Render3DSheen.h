#pragma once

// Render 3D's sheen, one source for all four shader sites: R3D_SHEEN_GLSL for the forward shader
// (Geometry3DNodes.cpp) and the GL path tracer (Render3DPathTrace.cpp), R3D_SHEEN_MSL for live and settle
// (MetalDevice.mm). The functions are written in the syntax both languages share.
// Cycles' sheen (4.0+) is a linearly transformed cosine (Zeltner 2022). Its lobe was measured in Blender
// (tools/blender-ab, 2026-11-11: black base, sheen 1, IOR 1, orthographic sphere under ten sun directions)
// and fitted per view angle and roughness. Rows roughness 0.1..1.0 step 0.15, columns cos(view) 0..1 in 11 steps.
//   kSheenE - directional albedo, measured in a white furnace (2048 samples).
//   kSheenA, kSheenB - the transform: w' = (a x + b z, a y, z) in a frame whose x points at the view.
// sheenLtc is the BRDF E a^2 / (pi |w'|^4); the base under the sheen keeps 1 - sheen * sheenE(view).
// Row 5.1, fibre direction: sheenTilt leans the lobe's normal toward the uv tangent (direction 1 = 45 degrees, -1 the
// other way, 0 = isotropic, the normal itself), so a nap brushed two ways catches the light differently.
#define R3D_SHEEN_E "0.1027, 0.0530, 0.0033, 0.0003, 0.0001, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.0000, 0.5498, 0.3736, 0.1973, 0.0804, 0.0304, 0.0117, 0.0049, 0.0023, 0.0012, 0.0007, 0.0004, 0.0003, 0.6310, 0.5032, 0.3755, 0.2530, 0.1616, 0.0997, 0.0601, 0.0357, 0.0214, 0.0129, 0.0078, 0.0052, 0.6578, 0.5674, 0.4771, 0.3802, 0.2955, 0.2242, 0.1681, 0.1236, 0.0898, 0.0648, 0.0459, 0.0330, 0.6782, 0.6124, 0.5467, 0.4733, 0.4033, 0.3394, 0.2834, 0.2353, 0.1939, 0.1593, 0.1296, 0.1068, 0.7049, 0.6549, 0.6049, 0.5473, 0.4907, 0.4377, 0.3893, 0.3464, 0.3080, 0.2738, 0.2433, 0.2191, 0.7385, 0.6970, 0.6555, 0.6077, 0.5618, 0.5185, 0.4800, 0.4453, 0.4144, 0.3874, 0.3636, 0.3441"
#define R3D_SHEEN_A "0.05, 0.05, 0.05, 0.0748, 0.0996, 0.1492, 0.1739, 0.1987, 0.22, 0.24, 0.26, 0.28, 0.1243, 0.1491, 0.1739, 0.1987, 0.2235, 0.2235, 0.2483, 0.2731, 0.2979, 0.2979, 0.3475, 0.3971, 0.2483, 0.2731, 0.2979, 0.3227, 0.3475, 0.3723, 0.3971, 0.3971, 0.4218, 0.4466, 0.4714, 0.4962, 0.3722, 0.397, 0.4218, 0.4466, 0.4962, 0.521, 0.521, 0.5458, 0.5458, 0.5706, 0.5954, 0.5954, 0.521, 0.5458, 0.5706, 0.5954, 0.6202, 0.645, 0.6697, 0.6697, 0.6945, 0.6945, 0.7193, 0.7193, 0.6697, 0.6945, 0.7193, 0.7441, 0.7689, 0.7689, 0.7689, 0.7937, 0.7937, 0.7937, 0.8185, 0.8185, 0.7689, 0.7937, 0.8185, 0.8433, 0.8433, 0.8681, 0.8681, 0.8681, 0.8681, 0.8681, 0.8681, 0.8681"
#define R3D_SHEEN_B "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -0.05, -0.05, -0.05, -0.025, -0.025, -0.025, -0.025, -0.05, -0.05, -0.05, -0.05, -0.025, -0.1, -0.1, -0.075, -0.05, -0.05, -0.05, -0.025, -0.025, -0.025, -0.05, -0.05, -0.025, -0.15, -0.15, -0.125, -0.1, -0.075, -0.075, -0.05, -0.05, -0.05, -0.05, -0.025, -0.025, -0.15, -0.15, -0.125, -0.1, -0.1, -0.075, -0.075, -0.075, -0.05, -0.05, -0.025, -0.025, -0.075, -0.075, -0.075, -0.075, -0.075, -0.05, -0.05, -0.05, -0.05, -0.025, -0.025, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0"
#define R3D_SHEEN_FUNCS \
   "float sheenLut(float mu, float r, int t) {\n" \
   "   float x = clamp(mu, 0.0, 1.0) * 11.0, y = clamp((r - 0.1) / 0.15, 0.0, 6.0);\n" \
   "   int x0 = min(int(x), 10), y0 = min(int(y), 5);\n" \
   "   float fx = x - float(x0), fy = y - float(y0);\n" \
   "   int i = y0 * 12 + x0;\n" \
   "   vec4 q = t == 0 ? vec4(kSheenE[i], kSheenE[i + 1], kSheenE[i + 12], kSheenE[i + 13])\n" \
   "          : t == 1 ? vec4(kSheenA[i], kSheenA[i + 1], kSheenA[i + 12], kSheenA[i + 13])\n" \
   "                   : vec4(kSheenB[i], kSheenB[i + 1], kSheenB[i + 12], kSheenB[i + 13]);\n" \
   "   return mix(mix(q.x, q.y, fx), mix(q.z, q.w, fx), fy);\n" \
   "}\n" \
   "float sheenE(float mu, float r) { return sheenLut(mu, r, 0); }\n" \
   "vec3 sheenTilt(vec3 n, vec3 t, float d) {\n" \
   "   vec3 tt = t - n * dot(n, t);\n" \
   "   return d == 0.0 || dot(tt, tt) < 1e-12 ? n : normalize(n + normalize(tt) * d);\n" \
   "}\n" \
   "float sheenLtc(float nL, float nV, float lv, float r) {\n" \
   "   float a = sheenLut(nV, r, 1), b = sheenLut(nV, r, 2);\n" \
   "   float st = sqrt(max(1.0 - nL * nL, 0.0));\n" \
   "   float x = clamp((lv - nL * nV) / max(sqrt(max(1.0 - nV * nV, 0.0)), 1e-4), -st, st);\n" \
   "   float wx = a * x + b * nL, q = wx * wx + a * a * max(st * st - x * x, 0.0) + nL * nL;\n" \
   "   return sheenE(nV, r) * a * a / (3.14159265 * max(q * q, 1e-8));\n" \
   "}\n"

#define R3D_SHEEN_GLSL \
   "const float kSheenE[84] = float[84](" R3D_SHEEN_E ");\n" \
   "const float kSheenA[84] = float[84](" R3D_SHEEN_A ");\n" \
   "const float kSheenB[84] = float[84](" R3D_SHEEN_B ");\n" \
   R3D_SHEEN_FUNCS

// MSL: the includer typedefs vec4 = float4 first.
#define R3D_SHEEN_MSL \
   "constant float kSheenE[84] = {" R3D_SHEEN_E "};\n" \
   "constant float kSheenA[84] = {" R3D_SHEEN_A "};\n" \
   "constant float kSheenB[84] = {" R3D_SHEEN_B "};\n" \
   R3D_SHEEN_FUNCS

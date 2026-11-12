#pragma once
// OPUS_PLAN row 9.1: shading maths with no API in it, written once for every backend. The `vec3` scheme of
// Render3DThinFilm.h: Metal typedefs vec3 to float3; GLSL (and the Vulkan glue of row 9.2) reads it as is. No `const`
// locals, `select` or `thread &`, so the text is GLSL 410 as well as MSL. Bindings, entry points and intersector
// calls stay in each backend.
#define R3D_SHARED_SRC \
   "float distGGX(float nDotH, float rough) { float a = rough * rough, a2 = a * a; float d = nDotH * nDotH * (a2 - 1.0) + 1.0; return a2 / max(3.14159265 * d * d, 1e-7); }\n" \
   "float geomSchlick(float nDotV, float rough) { float r = rough + 1.0, k = (r * r) / 8.0; return nDotV / (nDotV * (1.0 - k) + k); }\n" \
   "vec3 fresnelRough(float cosT, vec3 f0, float rough) { vec3 ceil = max(vec3(1.0 - rough), f0); return f0 + (ceil - f0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0); }\n" \
   "float lightFalloff(float dist, float k) { if (k >= 0.0) return 1.0 / (1.0 + dist * dist * k); return min(1.0 / max(dist * dist * -k, 1e-4), 20.0); }\n" \
   "uint pcg(uint v) { v = v * 747796405u + 2891336453u; uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u; return (w >> 22u) ^ w; }\n" \
   "vec3 cosineDir(vec3 n, float u1, float u2) {\n" \
   "   float r = sqrt(u1), phi = 6.2831853 * u2;\n" \
   "   vec3 up = abs(n.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);\n" \
   "   vec3 t = normalize(cross(up, n)); vec3 b = cross(n, t);\n" \
   "   return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u1, 0.0)));\n" \
   "}\n" \
   "vec3 ggxSampleH(vec3 n, float a, float u1, float u2) {\n" \
   "   float cosT = sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1));\n" \
   "   float sinT = sqrt(max(1.0 - cosT * cosT, 0.0)), phi = 6.2831853 * u2;\n" \
   "   vec3 up = abs(n.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);\n" \
   "   vec3 t = normalize(cross(up, n)); vec3 b = cross(n, t);\n" \
   "   return normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + n * cosT);\n" \
   "}\n" \
   "float maxc(vec3 c) { return max(c.x, max(c.y, c.z)); }\n" \
   "float hgPhase(float c, float g) { float k = max(1.0 + g * g - 2.0 * g * c, 1e-6); return (1.0 - g * g) / (12.566370614 * k * sqrt(k)); }\n" \
   "float ptS5(float c) { float m = clamp(1.0 - c, 0.0, 1.0), m2 = m * m; return m2 * m2 * m; }\n" \
   "float ptVis(float nV, float nL, float a) {\n" \
   "   float a2 = a * a;\n" \
   "   float gV = nL * sqrt(a2 + (1.0 - a2) * nV * nV), gL = nV * sqrt(a2 + (1.0 - a2) * nL * nL);\n" \
   "   return 0.5 / max(gV + gL, 1e-7);\n" \
   "}\n" \
   "float ptD(float nH, float a) { float a2 = a * a, d = nH * nH * (a2 - 1.0) + 1.0; return a2 / max(3.14159265 * d * d, 1e-7); }\n" \
   "float ptFd(float c, float eta) {\n" \
   "   float s2 = eta * eta * (1.0 - c * c);\n" \
   "   if (s2 >= 1.0) return 1.0;\n" \
   "   float ct = sqrt(1.0 - s2);\n" \
   "   float rs = (eta * c - ct) / (eta * c + ct), rp = (c - eta * ct) / (c + eta * ct);\n" \
   "   return 0.5 * (rs * rs + rp * rp);\n" \
   "}\n" \
   "float ptG1(float x, float a) { return 2.0 * x / (x + sqrt(a * a + (1.0 - a * a) * x * x)); }\n"

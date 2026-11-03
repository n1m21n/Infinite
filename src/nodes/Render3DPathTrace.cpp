#include "Render3DPathTrace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "BenchReport.h"
#include "ColorSpace.h"
#include "Render3DTonemap.h"
#include "gl3.h"

namespace
{
   constexpr float kPi = 3.14159265358979323846f;

   const char* kVert330 =
      "#version 330 core\n"
      "layout(location = 0) in vec2 aPos;\n"
      "layout(location = 1) in vec2 aUv;\n"
      "out vec2 vUv;\n"
      "void main() {\n"
      "   vUv = aUv;\n"
      "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
      "}\n";

   // Fragment shader for progressive path tracing (1 sample per pass into
   // ping-pong RGBA32F accumulation buffer).
   const char* kTraceFrag330 =
      "#version 330 core\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "\n"
      "uniform sampler2D uPrevAccum;\n"
      "uniform samplerBuffer uTlasNodes;\n"
      "uniform samplerBuffer uTlasInstances;\n"
      "uniform samplerBuffer uBlasNodes;\n"
      "uniform samplerBuffer uBlasTris;\n"
      "uniform samplerBuffer uBlasNormals;\n"
      "uniform samplerBuffer uMaterials;\n"
      "\n"
      "uniform int uSampleIndex;\n"
      "uniform int uMaxBounces;\n"
      "uniform int uHasTlas;\n"
      "uniform vec2 uResolution;\n"
      "\n"
      "uniform vec3 uCamEye;\n"
      "uniform vec3 uCamForward;\n"
      "uniform vec3 uCamRight;\n"
      "uniform vec3 uCamUp;\n"
      "uniform float uTanHalfFovY;\n"
      "uniform float uAspect;\n"
      "uniform float uOrthoHalfH;\n"   // > 0: orthographic, the view's half height
      "uniform vec3 uWorldColor;\n"
      // The world a ray that leaves the scene sees: 0 the uniform colour, 1 Render 3D's sky/horizon/ground
      // gradient, 2 an equirect HDRI (Environment node), rotated about Y like Render 3D's dirToEquirect.
      "uniform int uEnvMode;\n"
      "uniform sampler2D uEnvMap;\n"
      "uniform float uEnvRotation;\n"
      "uniform float uEnvIntensity;\n"
      "uniform vec3 uEnvSky;\n"
      "uniform vec3 uEnvHorizon;\n"
      "uniform vec3 uEnvGround;\n"
      // 0: a camera ray that misses shows the background colour instead of the world (coverage 0).
      "uniform int uEnvAsBg;\n"
      "\n"
      "uniform int uLightCount;\n"
      "uniform int uLightType[8];\n"
      "uniform vec3 uLightPos[8];\n"
      "uniform vec3 uLightAim[8];\n"
      "uniform vec3 uLightColor[8];\n"
      "uniform vec2 uLightArea[8];\n"
      "uniform vec3 uLightSpot[8];\n" // x = cosOuter, y = smoothW, z = cutoff
      "\n"
      "const float PI = 3.14159265358979323846;\n"
      "\n"
      "uint pcgHash(inout uint state) {\n"
      "   uint prev = state;\n"
      "   state = prev * 747796405u + 2891336453u;\n"
      "   uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;\n"
      "   return (word >> 22u) ^ word;\n"
      "}\n"
      "\n"
      "float rand(inout uint state) {\n"
      "   return float(pcgHash(state) >> 8u) * (1.0 / 16777216.0);\n"
      "}\n"
      "\n"
      "vec3 safeInvDir(vec3 d) {\n"
      "   vec3 s = vec3(d.x >= 0.0 ? 1e-20 : -1e-20,\n"
      "                 d.y >= 0.0 ? 1e-20 : -1e-20,\n"
      "                 d.z >= 0.0 ? 1e-20 : -1e-20);\n"
      "   return 1.0 / mix(s, d, greaterThan(abs(d), vec3(1e-20)));\n"
      "}\n"
      "\n"
      "bool intersectAabb(vec3 minB, vec3 maxB, vec3 o, vec3 invD, float tMin, float tMax, out float outNear) {\n"
      "   const float eps = 1e-5;\n"
      "   vec3 b0 = (minB - vec3(eps) - o) * invD;\n"
      "   vec3 b1 = (maxB + vec3(eps) - o) * invD;\n"
      "   vec3 nearV = min(b0, b1);\n"
      "   vec3 farV = max(b0, b1);\n"
      "   float t0 = max(max(tMin, nearV.x), max(nearV.y, nearV.z));\n"
      "   float t1 = min(min(tMax, farV.x), min(farV.y, farV.z));\n"
      "   outNear = t0;\n"
      "   return t0 <= t1;\n"
      "}\n"
      "\n"
      "bool intersectTri(vec3 o, vec3 d, float tMin, float tMax, vec3 v0, vec3 e1, vec3 e2,\n"
      "                  out float outT, out float outU, out float outV) {\n"
      "   vec3 h = cross(d, e2);\n"
      "   float det = dot(e1, h);\n"
      "   if (abs(det) < 1e-8) return false;\n"
      "   float invDet = 1.0 / det;\n"
      "   vec3 s = o - v0;\n"
      "   float u = dot(s, h) * invDet;\n"
      "   if (u < 0.0 || u > 1.0) return false;\n"
      "   vec3 q = cross(s, e1);\n"
      "   float v = dot(d, q) * invDet;\n"
      "   if (v < 0.0 || u + v > 1.0) return false;\n"
      "   float t = dot(e2, q) * invDet;\n"
      "   if (t <= tMin || t >= tMax) return false;\n"
      "   outT = t;\n"
      "   outU = u;\n"
      "   outV = v;\n"
      "   return true;\n"
      "}\n"
      "\n"
      "struct HitInfo {\n"
      "   float t;\n"
      "   int triSlot;\n"
      "   int instIdx;\n"
      "   int matId;\n"
      "   float u;\n"
      "   float v;\n"
      "};\n"
      "\n"
      "void intersectBlas(vec3 o, vec3 d, float tMin, int blasNodeBase, int blasTriBase, int instIdx, int instMatId,\n"
      "                   inout HitInfo hit) {\n"
      "   vec3 invD = safeInvDir(d);\n"
      "   vec4 root0 = texelFetch(uBlasNodes, blasNodeBase * 2 + 0);\n"
      "   vec4 root1 = texelFetch(uBlasNodes, blasNodeBase * 2 + 1);\n"
      "   float rootNear = 0.0;\n"
      "   if (!intersectAabb(root0.xyz, root1.xyz, o, invD, tMin, hit.t, rootNear))\n"
      "      return;\n"
      "\n"
      "   int stack[32];\n"
      "   int top = 0;\n"
      "   int cur = blasNodeBase;\n"
      "   for (int iter = 0; iter < 512; iter++) {\n"
      "      vec4 n0 = texelFetch(uBlasNodes, cur * 2 + 0);\n"
      "      vec4 n1 = texelFetch(uBlasNodes, cur * 2 + 1);\n"
      "      int w0 = floatBitsToInt(n0.w);\n"
      "      int w1 = floatBitsToInt(n1.w);\n"
      "      if (w1 > 0) {\n"
      "         for (int k = 0; k < w1; k++) {\n"
      "            int triSlot = blasTriBase + w0 + k;\n"
      "            vec4 t0 = texelFetch(uBlasTris, triSlot * 3 + 0);\n"
      "            vec4 t1 = texelFetch(uBlasTris, triSlot * 3 + 1);\n"
      "            vec4 t2 = texelFetch(uBlasTris, triSlot * 3 + 2);\n"
      "            float t, u, v;\n"
      "            if (intersectTri(o, d, tMin, hit.t, t0.xyz, t1.xyz, t2.xyz, t, u, v)) {\n"
      "               hit.t = t;\n"
      "               hit.triSlot = triSlot;\n"
      "               hit.instIdx = instIdx;\n"
      "               hit.matId = (instMatId >= 0) ? instMatId : floatBitsToInt(t2.w);\n"
      "               hit.u = u;\n"
      "               hit.v = v;\n"
      "            }\n"
      "         }\n"
      "         if (top == 0) break;\n"
      "         cur = stack[--top];\n"
      "      } else {\n"
      "         int leftIdx = blasNodeBase + w0;\n"
      "         int rightIdx = blasNodeBase - w1;\n"
      "         vec4 l0 = texelFetch(uBlasNodes, leftIdx * 2 + 0);\n"
      "         vec4 l1 = texelFetch(uBlasNodes, leftIdx * 2 + 1);\n"
      "         vec4 r0 = texelFetch(uBlasNodes, rightIdx * 2 + 0);\n"
      "         vec4 r1 = texelFetch(uBlasNodes, rightIdx * 2 + 1);\n"
      "         float tNearL = 0.0, tNearR = 0.0;\n"
      "         bool hitL = intersectAabb(l0.xyz, l1.xyz, o, invD, tMin, hit.t, tNearL);\n"
      "         bool hitR = intersectAabb(r0.xyz, r1.xyz, o, invD, tMin, hit.t, tNearR);\n"
      "         if (hitL && hitR) {\n"
      "            if (tNearL <= tNearR) {\n"
      "               if (top < 32) stack[top++] = rightIdx;\n"
      "               cur = leftIdx;\n"
      "            } else {\n"
      "               if (top < 32) stack[top++] = leftIdx;\n"
      "               cur = rightIdx;\n"
      "            }\n"
      "         } else if (hitL) {\n"
      "            cur = leftIdx;\n"
      "         } else if (hitR) {\n"
      "            cur = rightIdx;\n"
      "         } else {\n"
      "            if (top == 0) break;\n"
      "            cur = stack[--top];\n"
      "         }\n"
      "      }\n"
      "   }\n"
      "}\n"
      "\n"
      "bool occludedBlas(vec3 o, vec3 d, float tMin, float tMax, int blasNodeBase, int blasTriBase) {\n"
      "   vec3 invD = safeInvDir(d);\n"
      "   vec4 root0 = texelFetch(uBlasNodes, blasNodeBase * 2 + 0);\n"
      "   vec4 root1 = texelFetch(uBlasNodes, blasNodeBase * 2 + 1);\n"
      "   float rootNear = 0.0;\n"
      "   if (!intersectAabb(root0.xyz, root1.xyz, o, invD, tMin, tMax, rootNear))\n"
      "      return false;\n"
      "\n"
      "   int stack[32];\n"
      "   int top = 0;\n"
      "   int cur = blasNodeBase;\n"
      "   for (int iter = 0; iter < 512; iter++) {\n"
      "      vec4 n0 = texelFetch(uBlasNodes, cur * 2 + 0);\n"
      "      vec4 n1 = texelFetch(uBlasNodes, cur * 2 + 1);\n"
      "      int w0 = floatBitsToInt(n0.w);\n"
      "      int w1 = floatBitsToInt(n1.w);\n"
      "      if (w1 > 0) {\n"
      "         for (int k = 0; k < w1; k++) {\n"
      "            int triSlot = blasTriBase + w0 + k;\n"
      "            vec4 t0 = texelFetch(uBlasTris, triSlot * 3 + 0);\n"
      "            vec4 t1 = texelFetch(uBlasTris, triSlot * 3 + 1);\n"
      "            vec4 t2 = texelFetch(uBlasTris, triSlot * 3 + 2);\n"
      "            float t, u, v;\n"
      "            if (intersectTri(o, d, tMin, tMax, t0.xyz, t1.xyz, t2.xyz, t, u, v))\n"
      "               return true;\n"
      "         }\n"
      "         if (top == 0) break;\n"
      "         cur = stack[--top];\n"
      "      } else {\n"
      "         int leftIdx = blasNodeBase + w0;\n"
      "         int rightIdx = blasNodeBase - w1;\n"
      "         vec4 l0 = texelFetch(uBlasNodes, leftIdx * 2 + 0);\n"
      "         vec4 l1 = texelFetch(uBlasNodes, leftIdx * 2 + 1);\n"
      "         vec4 r0 = texelFetch(uBlasNodes, rightIdx * 2 + 0);\n"
      "         vec4 r1 = texelFetch(uBlasNodes, rightIdx * 2 + 1);\n"
      "         float tNearL = 0.0, tNearR = 0.0;\n"
      "         bool hitL = intersectAabb(l0.xyz, l1.xyz, o, invD, tMin, tMax, tNearL);\n"
      "         bool hitR = intersectAabb(r0.xyz, r1.xyz, o, invD, tMin, tMax, tNearR);\n"
      "         if (hitL && hitR) {\n"
      "            if (tNearL <= tNearR) {\n"
      "               if (top < 32) stack[top++] = rightIdx;\n"
      "               cur = leftIdx;\n"
      "            } else {\n"
      "               if (top < 32) stack[top++] = leftIdx;\n"
      "               cur = rightIdx;\n"
      "            }\n"
      "         } else if (hitL) {\n"
      "            cur = leftIdx;\n"
      "         } else if (hitR) {\n"
      "            cur = rightIdx;\n"
      "         } else {\n"
      "            if (top == 0) break;\n"
      "            cur = stack[--top];\n"
      "         }\n"
      "      }\n"
      "   }\n"
      "   return false;\n"
      "}\n"
      "\n"
      "HitInfo traceClosest(vec3 worldO, vec3 worldD, float tMin, float tMax) {\n"
      "   HitInfo hit;\n"
      "   hit.t = tMax;\n"
      "   hit.triSlot = -1;\n"
      "   hit.instIdx = -1;\n"
      "   hit.matId = 0;\n"
      "   hit.u = 0.0;\n"
      "   hit.v = 0.0;\n"
      "   if (uHasTlas == 0) return hit;\n"
      "\n"
      "   vec3 invD = safeInvDir(worldD);\n"
      "   vec4 root0 = texelFetch(uTlasNodes, 0);\n"
      "   vec4 root1 = texelFetch(uTlasNodes, 1);\n"
      "   float rootNear = 0.0;\n"
      "   if (!intersectAabb(root0.xyz, root1.xyz, worldO, invD, tMin, hit.t, rootNear))\n"
      "      return hit;\n"
      "\n"
      "   int stack[32];\n"
      "   int top = 0;\n"
      "   int cur = 0;\n"
      "   for (int iter = 0; iter < 256; iter++) {\n"
      "      vec4 n0 = texelFetch(uTlasNodes, cur * 2 + 0);\n"
      "      vec4 n1 = texelFetch(uTlasNodes, cur * 2 + 1);\n"
      "      int w0 = floatBitsToInt(n0.w);\n"
      "      int w1 = floatBitsToInt(n1.w);\n"
      "      if (w1 > 0) {\n"
      "         int instIdx = w0;\n"
      "         vec4 m0 = texelFetch(uTlasInstances, instIdx * 4 + 0);\n"
      "         vec4 m1 = texelFetch(uTlasInstances, instIdx * 4 + 1);\n"
      "         vec4 m2 = texelFetch(uTlasInstances, instIdx * 4 + 2);\n"
      "         vec4 meta = texelFetch(uTlasInstances, instIdx * 4 + 3);\n"
      "         vec3 localO = vec3(dot(m0.xyz, worldO) + m0.w,\n"
      "                            dot(m1.xyz, worldO) + m1.w,\n"
      "                            dot(m2.xyz, worldO) + m2.w);\n"
      "         vec3 localD = vec3(dot(m0.xyz, worldD),\n"
      "                            dot(m1.xyz, worldD),\n"
      "                            dot(m2.xyz, worldD));\n"
      "         int blasNodeBase = floatBitsToInt(meta.x);\n"
      "         int blasTriBase = floatBitsToInt(meta.y);\n"
      "         int instMatId = floatBitsToInt(meta.z);\n"
      "         intersectBlas(localO, localD, tMin, blasNodeBase, blasTriBase, instIdx, instMatId, hit);\n"
      "         if (top == 0) break;\n"
      "         cur = stack[--top];\n"
      "      } else {\n"
      "         int leftIdx = w0;\n"
      "         int rightIdx = -w1;\n"
      "         vec4 l0 = texelFetch(uTlasNodes, leftIdx * 2 + 0);\n"
      "         vec4 l1 = texelFetch(uTlasNodes, leftIdx * 2 + 1);\n"
      "         vec4 r0 = texelFetch(uTlasNodes, rightIdx * 2 + 0);\n"
      "         vec4 r1 = texelFetch(uTlasNodes, rightIdx * 2 + 1);\n"
      "         float tNearL = 0.0, tNearR = 0.0;\n"
      "         bool hitL = intersectAabb(l0.xyz, l1.xyz, worldO, invD, tMin, hit.t, tNearL);\n"
      "         bool hitR = intersectAabb(r0.xyz, r1.xyz, worldO, invD, tMin, hit.t, tNearR);\n"
      "         if (hitL && hitR) {\n"
      "            if (tNearL <= tNearR) {\n"
      "               if (top < 32) stack[top++] = rightIdx;\n"
      "               cur = leftIdx;\n"
      "            } else {\n"
      "               if (top < 32) stack[top++] = leftIdx;\n"
      "               cur = rightIdx;\n"
      "            }\n"
      "         } else if (hitL) {\n"
      "            cur = leftIdx;\n"
      "         } else if (hitR) {\n"
      "            cur = rightIdx;\n"
      "         } else {\n"
      "            if (top == 0) break;\n"
      "            cur = stack[--top];\n"
      "         }\n"
      "      }\n"
      "   }\n"
      "   return hit;\n"
      "}\n"
      "\n"
      "bool occludedScene(vec3 worldO, vec3 worldD, float tMin, float tMax) {\n"
      "   if (uHasTlas == 0) return false;\n"
      "   vec3 invD = safeInvDir(worldD);\n"
      "   vec4 root0 = texelFetch(uTlasNodes, 0);\n"
      "   vec4 root1 = texelFetch(uTlasNodes, 1);\n"
      "   float rootNear = 0.0;\n"
      "   if (!intersectAabb(root0.xyz, root1.xyz, worldO, invD, tMin, tMax, rootNear))\n"
      "      return false;\n"
      "\n"
      "   int stack[32];\n"
      "   int top = 0;\n"
      "   int cur = 0;\n"
      "   for (int iter = 0; iter < 256; iter++) {\n"
      "      vec4 n0 = texelFetch(uTlasNodes, cur * 2 + 0);\n"
      "      vec4 n1 = texelFetch(uTlasNodes, cur * 2 + 1);\n"
      "      int w0 = floatBitsToInt(n0.w);\n"
      "      int w1 = floatBitsToInt(n1.w);\n"
      "      if (w1 > 0) {\n"
      "         int instIdx = w0;\n"
      "         vec4 m0 = texelFetch(uTlasInstances, instIdx * 4 + 0);\n"
      "         vec4 m1 = texelFetch(uTlasInstances, instIdx * 4 + 1);\n"
      "         vec4 m2 = texelFetch(uTlasInstances, instIdx * 4 + 2);\n"
      "         vec4 meta = texelFetch(uTlasInstances, instIdx * 4 + 3);\n"
      "         vec3 localO = vec3(dot(m0.xyz, worldO) + m0.w,\n"
      "                            dot(m1.xyz, worldO) + m1.w,\n"
      "                            dot(m2.xyz, worldO) + m2.w);\n"
      "         vec3 localD = vec3(dot(m0.xyz, worldD),\n"
      "                            dot(m1.xyz, worldD),\n"
      "                            dot(m2.xyz, worldD));\n"
      "         int blasNodeBase = floatBitsToInt(meta.x);\n"
      "         int blasTriBase = floatBitsToInt(meta.y);\n"
      "         if (occludedBlas(localO, localD, tMin, tMax, blasNodeBase, blasTriBase))\n"
      "            return true;\n"
      "         if (top == 0) break;\n"
      "         cur = stack[--top];\n"
      "      } else {\n"
      "         int leftIdx = w0;\n"
      "         int rightIdx = -w1;\n"
      "         vec4 l0 = texelFetch(uTlasNodes, leftIdx * 2 + 0);\n"
      "         vec4 l1 = texelFetch(uTlasNodes, leftIdx * 2 + 1);\n"
      "         vec4 r0 = texelFetch(uTlasNodes, rightIdx * 2 + 0);\n"
      "         vec4 r1 = texelFetch(uTlasNodes, rightIdx * 2 + 1);\n"
      "         float tNearL = 0.0, tNearR = 0.0;\n"
      "         bool hitL = intersectAabb(l0.xyz, l1.xyz, worldO, invD, tMin, tMax, tNearL);\n"
      "         bool hitR = intersectAabb(r0.xyz, r1.xyz, worldO, invD, tMin, tMax, tNearR);\n"
      "         if (hitL && hitR) {\n"
      "            if (tNearL <= tNearR) {\n"
      "               if (top < 32) stack[top++] = rightIdx;\n"
      "               cur = leftIdx;\n"
      "            } else {\n"
      "               if (top < 32) stack[top++] = leftIdx;\n"
      "               cur = rightIdx;\n"
      "            }\n"
      "         } else if (hitL) {\n"
      "            cur = leftIdx;\n"
      "         } else if (hitR) {\n"
      "            cur = rightIdx;\n"
      "         } else {\n"
      "            if (top == 0) break;\n"
      "            cur = stack[--top];\n"
      "         }\n"
      "      }\n"
      "   }\n"
      "   return false;\n"
      "}\n"
      "\n"
      "void buildOrthonormalBasis(vec3 n, out vec3 t, out vec3 b) {\n"
      "   float signZ = n.z >= 0.0 ? 1.0 : -1.0;\n"
      "   float a = -1.0 / (signZ + n.z);\n"
      "   float d = n.x * n.y * a;\n"
      "   t = vec3(1.0 + signZ * n.x * n.x * a, signZ * d, -signZ * n.x);\n"
      "   b = vec3(d, signZ + n.y * n.y * a, -n.y);\n"
      "}\n"
      "\n"
      "vec3 sampleCosineHemisphere(vec2 u, vec3 n) {\n"
      "   float phi = 2.0 * PI * u.x;\n"
      "   float cosTheta = sqrt(max(0.0, 1.0 - u.y));\n"
      "   float sinTheta = sqrt(max(0.0, u.y));\n"
      "   vec3 t, b;\n"
      "   buildOrthonormalBasis(n, t, b);\n"
      "   return normalize(t * (cos(phi) * sinTheta) + b * (sin(phi) * sinTheta) + n * cosTheta);\n"
      "}\n"
      "\n"
      "vec3 sampleGGX(vec2 u, vec3 n, float alpha) {\n"
      "   float phi = 2.0 * PI * u.x;\n"
      "   float cosTheta2 = (1.0 - u.y) / max(1.0 + (alpha * alpha - 1.0) * u.y, 1e-7);\n"
      "   float cosTheta = sqrt(clamp(cosTheta2, 0.0, 1.0));\n"
      "   float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));\n"
      "   vec3 t, b;\n"
      "   buildOrthonormalBasis(n, t, b);\n"
      "   return normalize(t * (cos(phi) * sinTheta) + b * (sin(phi) * sinTheta) + n * cosTheta);\n"
      "}\n"
      "\n"
      "// Evaluates f_r(V, L) * cosThetaL and outputs the combined BSDF sampling PDF.\n"
      "vec3 evalBsdfCos(vec3 N, vec3 V, vec3 L, vec3 baseColor, float metallic, float roughness, out float outPdf) {\n"
      "   float nDotL = dot(N, L);\n"
      "   if (nDotL <= 1e-6) {\n"
      "      outPdf = 0.0;\n"
      "      return vec3(0.0);\n"
      "   }\n"
      "   float nDotV = max(dot(N, V), 1e-4);\n"
      "   vec3 H = normalize(V + L);\n"
      "   float nDotH = max(dot(N, H), 0.0);\n"
      "   float vDotH = max(dot(V, H), 1e-4);\n"
      "\n"
      "   float alpha = max(0.002, roughness * roughness);\n"
      "   float a2 = alpha * alpha;\n"
      "   float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;\n"
      "   float D = a2 / max(PI * denom * denom, 1e-7);\n"
      "\n"
      "   float gV = nDotL * sqrt(a2 + (1.0 - a2) * nDotV * nDotV);\n"
      "   float gL = nDotV * sqrt(a2 + (1.0 - a2) * nDotL * nDotL);\n"
      "   float vis = 0.5 / max(gV + gL, 1e-7);\n"
      "\n"
      "   vec3 f0 = mix(vec3(0.04), baseColor, metallic);\n"
      "   vec3 F = f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);\n"
      "\n"
      "   vec3 spec = D * vis * F;\n"
      "   vec3 diff = (vec3(1.0) - F) * (1.0 - metallic) * baseColor * (1.0 / PI);\n"
      "\n"
      "   float f0s = mix(0.04, 1.0, metallic);\n"
      "   float fView = f0s + (1.0 - f0s) * pow(clamp(1.0 - nDotV, 0.0, 1.0), 5.0);\n"
      "   float specProb = clamp(mix(fView, 1.0, metallic), 0.05, 1.0);\n"
      "   float pdfSpec = (D * nDotH) / max(4.0 * vDotH, 1e-6);\n"
      "   float pdfDiff = nDotL * (1.0 / PI);\n"
      "   outPdf = specProb * pdfSpec + (1.0 - specProb) * pdfDiff;\n"
      "   return (diff + spec) * nDotL;\n"
      "}\n"
      "\n"
      "vec3 envRadiance(vec3 d) {\n"
      "   if (uEnvMode == 2) {\n"
      "      float phi = atan(d.z, d.x) + uEnvRotation;\n"
      "      float theta = acos(clamp(d.y, -1.0, 1.0));\n"
      "      return textureLod(uEnvMap, vec2(phi / (2.0 * PI) + 0.5, theta / PI), 0.0).rgb * uEnvIntensity;\n"
      "   }\n"
      "   if (uEnvMode == 1) {\n"
      "      float t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);\n"
      "      vec3 lower = mix(uEnvGround, uEnvHorizon, smoothstep(0.0, 0.5, t));\n"
      "      return mix(lower, uEnvSky, smoothstep(0.5, 1.0, t)) * uEnvIntensity;\n"
      "   }\n"
      "   return uWorldColor;\n"
      "}\n"
      "\n"
      "void areaBasis(vec3 fwd, out vec3 rgt, out vec3 upv) {\n"
      "   rgt = cross(vec3(0.0, 1.0, 0.0), fwd);\n"
      "   rgt = (dot(rgt, rgt) < 1e-4) ? vec3(1.0, 0.0, 0.0) : normalize(rgt);\n"
      "   upv = cross(fwd, rgt);\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   uvec2 pix = uvec2(gl_FragCoord.xy);\n"
      "   uint pixRng = (pix.x * 1973u + pix.y * 9277u + 89173u) | 1u;\n"
      "   pcgHash(pixRng);\n"
      "   vec2 pixRotA = vec2(rand(pixRng), rand(pixRng));\n"
      "   vec2 pixRotB = vec2(rand(pixRng), rand(pixRng));\n"
      "   uint rng = (pix.x * 1973u + pix.y * 9277u + uint(uSampleIndex) * 26699u + 911u) | 1u;\n"
      "   pcgHash(rng);\n"
      "\n"
      "   vec2 jitter = vec2(rand(rng), rand(rng));\n"
      "   vec2 uv = (floor(gl_FragCoord.xy) + jitter) / uResolution;\n"
      "   vec2 ndc = uv * 2.0 - 1.0;\n"
      "   vec3 rayOrigin = uCamEye;\n"
      "   vec3 rayDir = normalize(uCamForward + ndc.x * uAspect * uTanHalfFovY * uCamRight + ndc.y * uTanHalfFovY * uCamUp);\n"
      "   if (uOrthoHalfH > 0.0) {\n"
      "      rayOrigin = uCamEye + (ndc.x * uAspect * uCamRight + ndc.y * uCamUp) * uOrthoHalfH;\n"
      "      rayDir = uCamForward;\n"
      "   }\n"
      "   float coverage = 1.0;\n"
      "\n"
      "   vec3 radiance = vec3(0.0);\n"
      "   vec3 throughput = vec3(1.0);\n"
      "   float prevPdfBsdf = 0.0;\n"
      "\n"
      "   int maxB = clamp(uMaxBounces, 1, 16);\n"
      "   for (int bounce = 0; bounce < maxB; bounce++) {\n"
      "      HitInfo hit = traceClosest(rayOrigin, rayDir, 1e-4, 1e30);\n"
      "\n"
      "      // MIS evaluation when an indirect BSDF ray intersects an area light.\n"
      "      if (bounce > 0 && prevPdfBsdf > 0.0) {\n"
      "         for (int li = 0; li < 8; li++) {\n"
      "            if (li >= uLightCount) break;\n"
      "            if (uLightType[li] != 5) continue;\n"
      "            vec3 lp = uLightPos[li];\n"
      "            vec3 fwd = uLightAim[li];\n"
      "            float cosLight = dot(fwd, -rayDir);\n"
      "            if (cosLight > 1e-5) {\n"
      "               float tLight = dot(lp - rayOrigin, fwd) / dot(rayDir, fwd);\n"
      "               if (tLight > 1e-4 && tLight < hit.t) {\n"
      "                  vec3 rgt, upv;\n"
      "                  areaBasis(fwd, rgt, upv);\n"
      "                  vec2 sz = uLightArea[li];\n"
      "                  vec3 rel = (rayOrigin + rayDir * tLight) - lp;\n"
      "                  if (abs(dot(rel, rgt)) <= 0.5 * sz.x && abs(dot(rel, upv)) <= 0.5 * sz.y) {\n"
      "                     float area = max(sz.x * sz.y, 1e-6);\n"
      "                     float pdfLight = (tLight * tLight) / (area * cosLight);\n"
      "                     float wMis = (prevPdfBsdf * prevPdfBsdf) / max(prevPdfBsdf * prevPdfBsdf + pdfLight * pdfLight, 1e-12);\n"
      "                     vec3 contrib = throughput * (uLightColor[li] / area) * wMis;\n"
      "                     radiance += min(contrib, vec3(10.0));\n"
      "                  }\n"
      "               }\n"
      "            }\n"
      "         }\n"
      "      }\n"
      "\n"
      "      if (hit.triSlot < 0) {\n"
      "         if (bounce == 0 && uEnvAsBg == 0) { coverage = 0.0; break; }\n"
      "         vec3 envContrib = throughput * envRadiance(rayDir);\n"
      "         radiance += (bounce > 0) ? min(envContrib, vec3(10.0)) : envContrib;\n"
      "         break;\n"
      "      }\n"
      "\n"
      "      vec3 hitPos = rayOrigin + rayDir * hit.t;\n"
      "\n"
      "      vec4 t1 = texelFetch(uBlasTris, hit.triSlot * 3 + 1);\n"
      "      vec4 t2 = texelFetch(uBlasTris, hit.triSlot * 3 + 2);\n"
      "      vec3 n0 = texelFetch(uBlasNormals, hit.triSlot * 3 + 0).xyz;\n"
      "      vec3 n1 = texelFetch(uBlasNormals, hit.triSlot * 3 + 1).xyz;\n"
      "      vec3 n2 = texelFetch(uBlasNormals, hit.triSlot * 3 + 2).xyz;\n"
      "      vec3 localN = (1.0 - hit.u - hit.v) * n0 + hit.u * n1 + hit.v * n2;\n"
      "      vec3 localNg = cross(t1.xyz, t2.xyz);\n"
      "\n"
      "      vec4 m0 = texelFetch(uTlasInstances, hit.instIdx * 4 + 0);\n"
      "      vec4 m1 = texelFetch(uTlasInstances, hit.instIdx * 4 + 1);\n"
      "      vec4 m2 = texelFetch(uTlasInstances, hit.instIdx * 4 + 2);\n"
      "      vec3 Ng = normalize(m0.xyz * localNg.x + m1.xyz * localNg.y + m2.xyz * localNg.z);\n"
      "      vec3 N = normalize(m0.xyz * localN.x + m1.xyz * localN.y + m2.xyz * localN.z);\n"
      "      if (dot(Ng, -rayDir) < 0.0) {\n"
      "         Ng = -Ng;\n"
      "         N = -N;\n"
      "      }\n"
      "      if (dot(N, -rayDir) <= 1e-4)\n"
      "         N = Ng;\n"
      "\n"
      "      vec4 mat0 = texelFetch(uMaterials, hit.matId * 2 + 0);\n"
      "      vec4 mat1 = texelFetch(uMaterials, hit.matId * 2 + 1);\n"
      "      vec3 baseColor = max(mat0.rgb, vec3(0.0));\n"
      "      float metallic = clamp(mat0.a, 0.0, 1.0);\n"
      "      float roughness = clamp(mat1.r, 0.045, 1.0);\n"
      "      vec3 emission = max(mat1.gba, vec3(0.0));\n"
      "\n"
      "      radiance += throughput * emission;\n"
      "\n"
      "      vec3 V = -rayDir;\n"
      "      vec3 shadowOrigin = hitPos + Ng * 1e-4;\n"
      "\n"
      "      // Next-Event Estimation (NEE) across scene lights.\n"
      "      for (int li = 0; li < 8; li++) {\n"
      "         if (li >= uLightCount) break;\n"
      "         int ltype = uLightType[li];\n"
      "         float cutoff = uLightSpot[li].z;\n"
      "         if (ltype == 5) {\n"
      "            vec3 lp = uLightPos[li];\n"
      "            vec3 fwd = uLightAim[li];\n"
      "            vec3 rgt, upv;\n"
      "            areaBasis(fwd, rgt, upv);\n"
      "            vec2 sz = uLightArea[li];\n"
      "            vec2 xi = (bounce == 0) ? (fract(pixRotA + float(uSampleIndex) * vec2(0.754877666, 0.569840291)) - 0.5) : vec2(rand(rng) - 0.5, rand(rng) - 0.5);\n"
      "            vec3 lightPt = lp + rgt * (xi.x * sz.x) + upv * (xi.y * sz.y);\n"
      "            vec3 toLight = lightPt - hitPos;\n"
      "            float dist2 = max(dot(toLight, toLight), 1e-6);\n"
      "            float dist = sqrt(dist2);\n"
      "            if (cutoff > 0.0 && dist > cutoff) continue;\n"
      "            vec3 L = toLight / dist;\n"
      "            float cosLight = dot(fwd, -L);\n"
      "            if (cosLight > 1e-5 && dot(N, L) > 1e-5 && dot(Ng, L) > 0.0) {\n"
      "               if (!occludedScene(shadowOrigin, L, 1e-4, dist - 2e-4)) {\n"
      "                  float area = max(sz.x * sz.y, 1e-6);\n"
      "                  float pdfLight = dist2 / (area * cosLight);\n"
      "                  float pdfBsdf = 0.0;\n"
      "                  vec3 bsdfCos = evalBsdfCos(N, V, L, baseColor, metallic, roughness, pdfBsdf);\n"
      "                  float wMis = (pdfLight * pdfLight) / max(pdfLight * pdfLight + pdfBsdf * pdfBsdf, 1e-12);\n"
      "                  vec3 contrib = throughput * bsdfCos * uLightColor[li] * (cosLight / dist2) * wMis;\n"
      "                  radiance += (bounce > 0) ? min(contrib, vec3(10.0)) : contrib;\n"
      "               }\n"
      "            }\n"
      "         } else if (ltype == 1 || ltype == 4) {\n"
      "            vec3 toLight = uLightPos[li] - hitPos;\n"
      "            float dist2 = max(dot(toLight, toLight), 1e-6);\n"
      "            float dist = sqrt(dist2);\n"
      "            if (cutoff > 0.0 && dist > cutoff) continue;\n"
      "            vec3 L = toLight / dist;\n"
      "            if (dot(N, L) > 1e-5 && dot(Ng, L) > 0.0) {\n"
      "               float atten = 1.0 / max(dist2, 1e-4);\n"
      "               if (ltype == 4) {\n"
      "                  float cosCur = dot(-L, uLightAim[li]);\n"
      "                  float cosOuter = uLightSpot[li].x;\n"
      "                  float smoothW = uLightSpot[li].y;\n"
      "                  float spotAtten = cosCur > cosOuter ? cosCur : 0.0;\n"
      "                  if (smoothW > 0.0) spotAtten *= smoothstep(0.0, smoothW, cosCur - cosOuter);\n"
      "                  atten *= spotAtten;\n"
      "               }\n"
      "               if (atten > 0.0 && !occludedScene(shadowOrigin, L, 1e-4, dist - 2e-4)) {\n"
      "                  float pdfBsdf = 0.0;\n"
      "                  vec3 bsdfCos = evalBsdfCos(N, V, L, baseColor, metallic, roughness, pdfBsdf);\n"
      "                  vec3 contrib = throughput * bsdfCos * uLightColor[li] * atten;\n"
      "                  radiance += (bounce > 0) ? min(contrib, vec3(10.0)) : contrib;\n"
      "               }\n"
      "            }\n"
      "         } else if (ltype == 0 || ltype == 2) {\n"
      "            vec3 L = normalize(uLightPos[li]);\n"
      "            if (dot(N, L) > 1e-5 && dot(Ng, L) > 0.0) {\n"
      "               if (!occludedScene(shadowOrigin, L, 1e-4, 1e30)) {\n"
      "                  float pdfBsdf = 0.0;\n"
      "                  vec3 bsdfCos = evalBsdfCos(N, V, L, baseColor, metallic, roughness, pdfBsdf);\n"
      "                  vec3 contrib = throughput * bsdfCos * uLightColor[li];\n"
      "                  radiance += (bounce > 0) ? min(contrib, vec3(10.0)) : contrib;\n"
      "               }\n"
      "            }\n"
      "         }\n"
      "      }\n"
      "\n"
      "      // Sample BSDF for next bounce.\n"
      "      float f0s = mix(0.04, 1.0, metallic);\n"
      "      float fView = f0s + (1.0 - f0s) * pow(clamp(1.0 - max(dot(N, V), 1e-4), 0.0, 1.0), 5.0);\n"
      "      float specProb = clamp(mix(fView, 1.0, metallic), 0.05, 1.0);\n"
      "      vec2 uBsdf = (bounce == 0) ? fract(pixRotB + float(uSampleIndex) * vec2(0.569840291, 0.754877666)) : vec2(rand(rng), rand(rng));\n"
      "      vec3 L;\n"
      "      if (rand(rng) < specProb) {\n"
      "         vec3 H = sampleGGX(uBsdf, N, max(0.002, roughness * roughness));\n"
      "         L = reflect(-V, H);\n"
      "      } else {\n"
      "         L = sampleCosineHemisphere(uBsdf, N);\n"
      "      }\n"
      "      if (dot(N, L) <= 1e-5 || dot(Ng, L) <= 0.0)\n"
      "         break;\n"
      "\n"
      "      float pdfBsdf = 0.0;\n"
      "      vec3 bsdfCos = evalBsdfCos(N, V, L, baseColor, metallic, roughness, pdfBsdf);\n"
      "      if (pdfBsdf <= 1e-6)\n"
      "         break;\n"
      "\n"
      "      throughput *= bsdfCos / pdfBsdf;\n"
      "      prevPdfBsdf = pdfBsdf;\n"
      "\n"
      "      // Russian roulette after bounce 2.\n"
      "      if (bounce >= 2) {\n"
      "         float pSurvive = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);\n"
      "         if (rand(rng) > pSurvive)\n"
      "            break;\n"
      "         throughput /= pSurvive;\n"
      "      }\n"
      "\n"
      "      rayOrigin = hitPos + Ng * 1e-4;\n"
      "      rayDir = L;\n"
      "   }\n"
      "\n"
      "   if (any(isnan(radiance)) || any(isinf(radiance)))\n"
      "      radiance = vec3(0.0);\n"
      "\n"
      // Alpha keeps the share of samples that hit something (or the world shown as background).
      "   if (uSampleIndex <= 0) {\n"
      "      fragColor = vec4(radiance, coverage);\n"
      "   } else {\n"
      "      vec4 prev = texelFetch(uPrevAccum, ivec2(gl_FragCoord.xy), 0);\n"
      "      fragColor = mix(prev, vec4(radiance, coverage), 1.0 / float(uSampleIndex + 1));\n"
      "   }\n"
      "}\n";

   // Display resolve shader: applies exposure, tonemap, and the exact IEC
   // 61966-2-1 linear-to-sRGB curve used by Geometry3DNodes.cpp.
   const char* kDisplayFrag330 =
      "#version 330 core\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "\n"
      "uniform sampler2D uAccum;\n"
      "uniform float uExposure;\n"
      "uniform int uTonemap;\n"
      "uniform vec3 uBgColor;\n"   // sRGB, shown as is where no sample hit, like Render 3D's clear colour
      "uniform float uBgOpacity;\n"
      "\n"
      "vec3 toLinear(vec3 c) { c = max(c, vec3(0.0)); return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }\n"
      "vec3 toSrgb(vec3 c) { c = max(c, vec3(0.0)); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }\n"
      "\n"
      "vec3 acesFilm(vec3 x) {\n"
      "   return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n"
      "}\n"
      R3D_TONEMAP_GLSL
      "\n"
      "vec3 toneDisplay(vec3 c) {\n"
      "   if (uTonemap == 1) return acesFilm(c);\n"
      "   if (uTonemap == 2) return c / (c + vec3(1.0));\n"
      "   if (uTonemap == 3) return agxTone(c);\n"
      "   if (uTonemap == 4) return neutralTone(c);\n"
      "   return c;\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   vec4 acc = texelFetch(uAccum, ivec2(gl_FragCoord.xy), 0);\n"
      "   float cov = clamp(acc.a, 0.0, 1.0);\n"
      "   vec3 lin = acc.rgb / max(cov, 1e-4);\n"
      "   vec3 srgb = clamp(toSrgb(toneDisplay(lin * uExposure)), 0.0, 1.0);\n"
      "   fragColor = vec4(mix(uBgColor, srgb, cov), mix(uBgOpacity, 1.0, cov));\n"
      "}\n";

   unsigned int CompileStage330(GLenum stage, const char* src)
   {
      unsigned int sh = glCreateShader(stage);
      glShaderSource(sh, 1, &src, nullptr);
      glCompileShader(sh);
      GLint ok = 0;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (ok)
         return sh;
      char log[2048];
      glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
      std::fprintf(stderr, "Render3DPathTrace shader compile error:\n%s\n", log);
      glDeleteShader(sh);
      return 0;
   }

   unsigned int CompileProgram330(const char* fragSrc)
   {
      const unsigned int vs = CompileStage330(GL_VERTEX_SHADER, kVert330);
      if (vs == 0)
         return 0;
      const unsigned int fs = CompileStage330(GL_FRAGMENT_SHADER, fragSrc);
      if (fs == 0)
      {
         glDeleteShader(vs);
         return 0;
      }
      const unsigned int prog = glCreateProgram();
      glBindAttribLocation(prog, 0, "aPos");
      glBindAttribLocation(prog, 1, "aUv");
      glAttachShader(prog, vs);
      glAttachShader(prog, fs);
      glLinkProgram(prog);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GLint linked = 0;
      glGetProgramiv(prog, GL_LINK_STATUS, &linked);
      if (!linked)
      {
         char log[2048];
         glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
         std::fprintf(stderr, "Render3DPathTrace link error:\n%s\n", log);
         glDeleteProgram(prog);
         return 0;
      }
      return prog;
   }
}

Render3DPathTrace::PtMaterial Render3DPathTrace::PtMaterial::FromLinear(
   float r, float g, float b, float rough, float metal, float emR, float emG, float emB)
{
   PtMaterial m;
   m.baseColor[0] = r;
   m.baseColor[1] = g;
   m.baseColor[2] = b;
   m.roughness = rough;
   m.metallic = metal;
   m.emission[0] = emR;
   m.emission[1] = emG;
   m.emission[2] = emB;
   return m;
}

Render3DPathTrace::PtMaterial Render3DPathTrace::PtMaterial::FromMaterial(const Material& mat)
{
   PtMaterial m;
   for (int k = 0; k < 3; k++)
   {
      m.baseColor[k] = ColorSpace::SrgbToLinear(std::max(0.0f, mat.color[k]));
      m.emission[k] = ColorSpace::SrgbToLinear(std::max(0.0f, mat.emissionColor[k])) * std::max(0.0f, mat.emission);
   }
   m.metallic = std::clamp(mat.metallic, 0.0f, 1.0f);
   m.roughness = std::clamp(mat.roughness, 0.045f, 1.0f);
   return m;
}

Render3DPathTrace::PtLight Render3DPathTrace::PtLight::FromRigLight(const Render3DNode::RigLight& rl)
{
   PtLight l;
   l.type = rl.type;
   std::memcpy(l.posOrDir, rl.vec, sizeof(l.posOrDir));
   std::memcpy(l.aim, rl.aim, sizeof(l.aim));
   l.areaW = std::max(0.001f, rl.areaW);
   l.areaH = std::max(0.001f, rl.areaH);
   const float cosOuter = std::cos(rl.spotAngle * kPi / 180.0f);
   l.spotCosOuter = cosOuter;
   l.spotSmoothW = (1.0f - cosOuter) * rl.spotPenumbra;
   l.cutoff = rl.cutoff;

   // RigLight::intensity follows Geometry3DNodes.cpp's conventions:
   // - Point/Spot/Area with hard inverse-square falloff (lightFalloff = 4 / d^2)
   //   multiply intensity by 4 in the forward shader.
   // - Sun/Directional use intensity directly as irradiance (W/m^2).
   const float scale = (rl.type == 1 || rl.type == 4 || rl.type == 5) ? (4.0f * rl.intensity) : rl.intensity;
   for (int k = 0; k < 3; k++)
      l.color[k] = ColorSpace::SrgbToLinear(std::max(0.0f, rl.color[k])) * scale;
   return l;
}

Render3DPathTrace::PtLight Render3DPathTrace::PtLight::FromLightNode(const LightNode& n)
{
   Render3DNode::RigLight rl;
   rl.From(n);
   n.ComputeVector(rl.vec);
   if (n.type == 4 || n.type == 5)
   {
      const float len = std::sqrt(rl.vec[0] * rl.vec[0] + rl.vec[1] * rl.vec[1] + rl.vec[2] * rl.vec[2]);
      if (len > 1e-6f)
      {
         rl.aim[0] = -rl.vec[0] / len;
         rl.aim[1] = -rl.vec[1] / len;
         rl.aim[2] = -rl.vec[2] / len;
      }
   }
   return FromRigLight(rl);
}

Render3DPathTrace::PtLight Render3DPathTrace::PtLight::AreaWatts(
   const float pos[3], const float aimDir[3], float width, float height, float powerWatts, const float linearRgb[3])
{
   PtLight l;
   l.type = 5;
   std::memcpy(l.posOrDir, pos, sizeof(l.posOrDir));
   float ax = aimDir[0], ay = aimDir[1], az = aimDir[2];
   const float len = std::sqrt(ax * ax + ay * ay + az * az);
   if (len > 1e-6f) { ax /= len; ay /= len; az /= len; }
   else { ax = 0.0f; ay = -1.0f; az = 0.0f; }
   l.aim[0] = ax; l.aim[1] = ay; l.aim[2] = az;
   l.areaW = std::max(0.001f, width);
   l.areaH = std::max(0.001f, height);
   // Cycles normalized area light of power P Watts has L_e * A = P / pi.
   const float fluxOverPi = powerWatts / kPi;
   for (int k = 0; k < 3; k++)
      l.color[k] = (linearRgb ? linearRgb[k] : 1.0f) * fluxOverPi;
   return l;
}

Render3DPathTrace::PtLight Render3DPathTrace::PtLight::PointWatts(
   const float pos[3], float powerWatts, const float linearRgb[3])
{
   PtLight l;
   l.type = 1;
   std::memcpy(l.posOrDir, pos, sizeof(l.posOrDir));
   const float intensitySr = powerWatts / (4.0f * kPi);
   for (int k = 0; k < 3; k++)
      l.color[k] = (linearRgb ? linearRgb[k] : 1.0f) * intensitySr;
   return l;
}

Render3DPathTrace::~Render3DPathTrace()
{
   Release();
}

void Render3DPathTrace::DestroyBufferTex(BufferTex& bt)
{
   if (bt.tex != 0)
   {
      glDeleteTextures(1, &bt.tex);
      bt.tex = 0;
   }
   if (bt.vbo != 0)
   {
      Bench::GpuMem::ReleaseBuffer(bt.vbo);
      glDeleteBuffers(1, &bt.vbo);
      bt.vbo = 0;
   }
   bt.floatCount = 0;
}

void Render3DPathTrace::UploadBufferTex(BufferTex& bt, const std::vector<float>& data, const char* label)
{
   static const float kFallback[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
   const float* ptr = data.empty() ? kFallback : data.data();
   const size_t floats = data.empty() ? 4u : data.size();
   const size_t bytes = floats * sizeof(float);

   if (bt.vbo == 0)
      glGenBuffers(1, &bt.vbo);
   glBindBuffer(GL_TEXTURE_BUFFER, bt.vbo);
   glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)bytes, ptr, GL_STATIC_DRAW);
   glBindBuffer(GL_TEXTURE_BUFFER, 0);
   Bench::GpuMem::RecordBuffer(bt.vbo, Bench::GpuMemCategory::MeshBuffers, bytes, label);

   if (bt.tex == 0)
      glGenTextures(1, &bt.tex);
   glBindTexture(GL_TEXTURE_BUFFER, bt.tex);
   glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, bt.vbo);
   glBindTexture(GL_TEXTURE_BUFFER, 0);
   bt.floatCount = floats;
}

void Render3DPathTrace::Release()
{
   DestroyBufferTex(mTlasNodesBuf);
   DestroyBufferTex(mTlasInstBuf);
   DestroyBufferTex(mBlasNodesBuf);
   DestroyBufferTex(mBlasTrisBuf);
   DestroyBufferTex(mBlasNormalsBuf);
   DestroyBufferTex(mMaterialsBuf);

   GLUtil::DestroyFbo(mAccum[0]);
   GLUtil::DestroyFbo(mAccum[1]);
   GLUtil::DestroyFbo(mDisplay);

   if (mProgTrace != 0)
   {
      glDeleteProgram(mProgTrace);
      mProgTrace = 0;
   }
   if (mProgDisplay != 0)
   {
      glDeleteProgram(mProgDisplay);
      mProgDisplay = 0;
   }
   mShadersTried = false;
   mSceneUploaded = false;
   mSampleIndex = 0;
}

bool Render3DPathTrace::EnsureShaders()
{
   if (mShadersTried)
      return mProgTrace != 0 && mProgDisplay != 0;
   mShadersTried = true;
   mProgTrace = CompileProgram330(kTraceFrag330);
   mProgDisplay = CompileProgram330(kDisplayFrag330);
   return mProgTrace != 0 && mProgDisplay != 0;
}

bool Render3DPathTrace::EnsureTargets(int w, int h)
{
   if (w <= 0 || h <= 0)
      return false;
   if (mWidth != w || mHeight != h)
   {
      mWidth = w;
      mHeight = h;
      mSampleIndex = 0;
      mAccumReadIdx = 0;
   }
   return GLUtil::EnsureFbo(mAccum[0], w, h, GL_RGBA32F, "PtAccum0") &&
          GLUtil::EnsureFbo(mAccum[1], w, h, GL_RGBA32F, "PtAccum1") &&
          GLUtil::EnsureFbo(mDisplay, w, h, GL_RGBA8, "PtDisplay");
}

bool Render3DPathTrace::UploadScene(const std::vector<Mesh>& meshes,
                                    const std::vector<Bvh::Instance>& instances,
                                    const std::vector<PtMaterial>& materials)
{
   std::vector<Bvh::Blas> blases(meshes.size());
   std::vector<int> blasNodeOffset(meshes.size(), 0);
   std::vector<int> blasTriOffset(meshes.size(), 0);

   std::vector<float> allBlasNodes;
   std::vector<float> allBlasTris;
   std::vector<float> allBlasNormals;

   for (size_t m = 0; m < meshes.size(); m++)
   {
      const Mesh& mesh = meshes[m];
      blases[m].Build(mesh, 0);
      blasNodeOffset[m] = (int)(allBlasNodes.size() / 8);
      blasTriOffset[m] = (int)(allBlasTris.size() / 12);

      allBlasNodes.insert(allBlasNodes.end(), blases[m].nodeTexels.begin(), blases[m].nodeTexels.end());
      allBlasTris.insert(allBlasTris.end(), blases[m].triTexels.begin(), blases[m].triTexels.end());

      const int triCount = blases[m].TriCount();
      for (int i = 0; i < triCount; i++)
      {
         const int origTri = Bvh::FloatAsInt(blases[m].triTexels[(size_t)i * 12 + 3]);
         const Vertex& v0 = mesh.vertices[mesh.indices[(size_t)origTri * 3 + 0]];
         const Vertex& v1 = mesh.vertices[mesh.indices[(size_t)origTri * 3 + 1]];
         const Vertex& v2 = mesh.vertices[mesh.indices[(size_t)origTri * 3 + 2]];

         float fnx = 0.0f, fny = 1.0f, fnz = 0.0f;
         {
            const float e1x = v1.px - v0.px, e1y = v1.py - v0.py, e1z = v1.pz - v0.pz;
            const float e2x = v2.px - v0.px, e2y = v2.py - v0.py, e2z = v2.pz - v0.pz;
            fnx = e1y * e2z - e1z * e2y;
            fny = e1z * e2x - e1x * e2z;
            fnz = e1x * e2y - e1y * e2x;
            const float len = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
            if (len > 1e-8f) { fnx /= len; fny /= len; fnz /= len; }
         }

         auto pushNormal = [&](const Vertex& v)
         {
            const float nlen2 = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
            if (nlen2 > 1e-6f)
            {
               allBlasNormals.push_back(v.nx);
               allBlasNormals.push_back(v.ny);
               allBlasNormals.push_back(v.nz);
               allBlasNormals.push_back(0.0f);
            }
            else
            {
               allBlasNormals.push_back(fnx);
               allBlasNormals.push_back(fny);
               allBlasNormals.push_back(fnz);
               allBlasNormals.push_back(0.0f);
            }
         };
         pushNormal(v0);
         pushNormal(v1);
         pushNormal(v2);
      }
   }

   Bvh::Tlas tlas;
   tlas.Build(instances, blases);

   // Pack 4 vec4 texels (16 floats) per instance:
   //   0..2: row-major 3x4 world-to-local invTransform
   //   3:    (IntAsFloat(blasNodeBase), IntAsFloat(blasTriBase), IntAsFloat(materialId), 0)
   std::vector<float> instTexels(tlas.instances.size() * 16, 0.0f);
   for (size_t i = 0; i < tlas.instances.size(); i++)
   {
      const Bvh::Instance& inst = tlas.instances[i];
      float* dst = &instTexels[i * 16];
      std::memcpy(dst, inst.invTransform, 12 * sizeof(float));
      const int bIdx = (inst.blasIndex >= 0 && inst.blasIndex < (int)meshes.size()) ? inst.blasIndex : 0;
      const int nodeBase = (bIdx < (int)blasNodeOffset.size() ? blasNodeOffset[(size_t)bIdx] : 0) + inst.blasRootIndex;
      const int triBase = bIdx < (int)blasTriOffset.size() ? blasTriOffset[(size_t)bIdx] : 0;
      dst[12] = Bvh::IntAsFloat(nodeBase);
      dst[13] = Bvh::IntAsFloat(triBase);
      dst[14] = Bvh::IntAsFloat(inst.materialId);
      dst[15] = 0.0f;
   }

   std::vector<float> matTexels(std::max<size_t>(1, materials.size()) * 8, 0.0f);
   if (materials.empty())
   {
      matTexels[0] = 0.8f; matTexels[1] = 0.8f; matTexels[2] = 0.8f; matTexels[3] = 0.0f;
      matTexels[4] = 0.5f; matTexels[5] = 0.0f; matTexels[6] = 0.0f; matTexels[7] = 0.0f;
   }
   else
   {
      for (size_t i = 0; i < materials.size(); i++)
      {
         const PtMaterial& m = materials[i];
         float* dst = &matTexels[i * 8];
         dst[0] = m.baseColor[0];
         dst[1] = m.baseColor[1];
         dst[2] = m.baseColor[2];
         dst[3] = m.metallic;
         dst[4] = m.roughness;
         dst[5] = m.emission[0];
         dst[6] = m.emission[1];
         dst[7] = m.emission[2];
      }
   }

   UploadBufferTex(mTlasNodesBuf, tlas.nodeTexels, "PtTlasNodes");
   UploadBufferTex(mTlasInstBuf, instTexels, "PtTlasInstances");
   UploadBufferTex(mBlasNodesBuf, allBlasNodes, "PtBlasNodes");
   UploadBufferTex(mBlasTrisBuf, allBlasTris, "PtBlasTris");
   UploadBufferTex(mBlasNormalsBuf, allBlasNormals, "PtBlasNormals");
   UploadBufferTex(mMaterialsBuf, matTexels, "PtMaterials");

   mSceneUploaded = !tlas.Empty();
   ResetAccumulation();
   return true;
}

void Render3DPathTrace::SetLights(const std::vector<PtLight>& lights)
{
   mLights = lights;
   if ((int)mLights.size() > Render3DNode::kMaxLights)
      mLights.resize((size_t)Render3DNode::kMaxLights);
   ResetAccumulation();
}

void Render3DPathTrace::SetWorldColorLinear(float r, float g, float b)
{
   mWorldColor[0] = r;
   mWorldColor[1] = g;
   mWorldColor[2] = b;
   mEnvTex = 0;
   mEnvGradient = false;
   ResetAccumulation();
}

void Render3DPathTrace::SetCameraLookAt(const float eye[3], const float target[3], const float up[3],
                                        float fovDegrees, float aspect)
{
   std::memcpy(mCamEye, eye, sizeof(mCamEye));
   float fx = target[0] - eye[0];
   float fy = target[1] - eye[1];
   float fz = target[2] - eye[2];
   const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
   if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }
   else { fx = 0.0f; fy = 0.0f; fz = -1.0f; }

   float rx = fy * up[2] - fz * up[1];
   float ry = fz * up[0] - fx * up[2];
   float rz = fx * up[1] - fy * up[0];
   const float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
   if (rlen > 1e-6f) { rx /= rlen; ry /= rlen; rz /= rlen; }
   else { rx = 1.0f; ry = 0.0f; rz = 0.0f; }

   const float ux = ry * fz - rz * fy;
   const float uy = rz * fx - rx * fz;
   const float uz = rx * fy - ry * fx;

   mCamForward[0] = fx; mCamForward[1] = fy; mCamForward[2] = fz;
   mCamRight[0] = rx;   mCamRight[1] = ry;   mCamRight[2] = rz;
   mCamUp[0] = ux;      mCamUp[1] = uy;      mCamUp[2] = uz;

   mAspect = std::max(1e-3f, aspect);
   mTanHalfFovY = std::tan(fovDegrees * 0.5f * kPi / 180.0f);
   mOrthoHalfH = 0.0f;
   ResetAccumulation();
}

void Render3DPathTrace::SetCameraBasis(const float eye[3], const float right[3], const float up[3],
                                       const float forward[3], float tanHalfFovY, float aspect, float orthoHalfH)
{
   std::memcpy(mCamEye, eye, sizeof(mCamEye));
   std::memcpy(mCamRight, right, sizeof(mCamRight));
   std::memcpy(mCamUp, up, sizeof(mCamUp));
   std::memcpy(mCamForward, forward, sizeof(mCamForward));
   mTanHalfFovY = tanHalfFovY;
   mAspect = std::max(1e-3f, aspect);
   mOrthoHalfH = std::max(0.0f, orthoHalfH);
   ResetAccumulation();
}

void Render3DPathTrace::SetEnvironment(unsigned int equirectTex, float rotationRad, float intensity,
                                       const float skyLin[3], const float horizonLin[3], const float groundLin[3])
{
   mEnvTex = equirectTex;
   mEnvGradient = true;
   mEnvRotation = rotationRad;
   mEnvIntensity = intensity;
   std::memcpy(mEnvSky, skyLin, sizeof(mEnvSky));
   std::memcpy(mEnvHorizon, horizonLin, sizeof(mEnvHorizon));
   std::memcpy(mEnvGround, groundLin, sizeof(mEnvGround));
   ResetAccumulation();
}

void Render3DPathTrace::SetBackground(bool worldAsBackground, const float srgb[3], float opacity)
{
   mEnvAsBg = worldAsBackground;
   std::memcpy(mBgColor, srgb, sizeof(mBgColor));
   mBgOpacity = opacity;
}

void Render3DPathTrace::SetCameraFromNode(const CameraNode& cam, float aspect)
{
   float eye[3];
   cam.ComputeEye(eye);
   const float target[3] = { cam.targetX, cam.targetY, cam.targetZ };
   const float rollRad = cam.roll * kPi / 180.0f;
   const float up[3] = { std::sin(rollRad), std::cos(rollRad), 0.0f };
   SetCameraLookAt(eye, target, up, cam.fov, aspect);
}

void Render3DPathTrace::ResetAccumulation()
{
   mSampleIndex = 0;
   mAccumReadIdx = 0;
}

bool Render3DPathTrace::StepSamples(int width, int height, int numSamples, bool resolveDisplay)
{
   if (numSamples <= 0)
      return true;
   if (!EnsureShaders() || !EnsureTargets(width, height))
      return false;

   const int lightCount = std::min((int)mLights.size(), (int)Render3DNode::kMaxLights);
   int lTypes[Render3DNode::kMaxLights] = {};
   float lPos[Render3DNode::kMaxLights * 3] = {};
   float lAim[Render3DNode::kMaxLights * 3] = {};
   float lCol[Render3DNode::kMaxLights * 3] = {};
   float lArea[Render3DNode::kMaxLights * 2] = {};
   float lSpot[Render3DNode::kMaxLights * 3] = {};
   for (int i = 0; i < lightCount; i++)
   {
      const PtLight& l = mLights[(size_t)i];
      lTypes[i] = l.type;
      std::memcpy(&lPos[i * 3], l.posOrDir, 3 * sizeof(float));
      std::memcpy(&lAim[i * 3], l.aim, 3 * sizeof(float));
      std::memcpy(&lCol[i * 3], l.color, 3 * sizeof(float));
      lArea[i * 2 + 0] = l.areaW;
      lArea[i * 2 + 1] = l.areaH;
      lSpot[i * 3 + 0] = l.spotCosOuter;
      lSpot[i * 3 + 1] = l.spotSmoothW;
      lSpot[i * 3 + 2] = l.cutoff;
   }

   for (int s = 0; s < numSamples; s++)
   {
      const int writeIdx = (mAccumReadIdx + 1) & 1;
      const unsigned int prevTex = mAccum[mAccumReadIdx].tex;
      const int curSample = mSampleIndex;

      GLUtil::RunShaderPass(mAccum[writeIdx], mProgTrace, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, prevTex);
         glUniform1i(glGetUniformLocation(mProgTrace, "uPrevAccum"), 0);

         auto bindBuf = [&](int unit, unsigned int tex, const char* name)
         {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_BUFFER, tex);
            glUniform1i(glGetUniformLocation(mProgTrace, name), unit);
         };
         bindBuf(1, mTlasNodesBuf.tex, "uTlasNodes");
         bindBuf(2, mTlasInstBuf.tex, "uTlasInstances");
         bindBuf(3, mBlasNodesBuf.tex, "uBlasNodes");
         bindBuf(4, mBlasTrisBuf.tex, "uBlasTris");
         bindBuf(5, mBlasNormalsBuf.tex, "uBlasNormals");
         bindBuf(6, mMaterialsBuf.tex, "uMaterials");

         glUniform1i(glGetUniformLocation(mProgTrace, "uSampleIndex"), curSample);
         glUniform1i(glGetUniformLocation(mProgTrace, "uMaxBounces"), maxBounces);
         glUniform1i(glGetUniformLocation(mProgTrace, "uHasTlas"), mSceneUploaded ? 1 : 0);
         glUniform2f(glGetUniformLocation(mProgTrace, "uResolution"), (float)width, (float)height);

         glUniform3fv(glGetUniformLocation(mProgTrace, "uCamEye"), 1, mCamEye);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uCamForward"), 1, mCamForward);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uCamRight"), 1, mCamRight);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uCamUp"), 1, mCamUp);
         glUniform1f(glGetUniformLocation(mProgTrace, "uTanHalfFovY"), mTanHalfFovY);
         glUniform1f(glGetUniformLocation(mProgTrace, "uAspect"), mAspect);
         glUniform1f(glGetUniformLocation(mProgTrace, "uOrthoHalfH"), mOrthoHalfH);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uWorldColor"), 1, mWorldColor);
         glActiveTexture(GL_TEXTURE7);
         glBindTexture(GL_TEXTURE_2D, mEnvTex);
         glUniform1i(glGetUniformLocation(mProgTrace, "uEnvMap"), 7);
         glUniform1i(glGetUniformLocation(mProgTrace, "uEnvMode"), mEnvTex != 0 ? 2 : mEnvGradient ? 1 : 0);
         glUniform1f(glGetUniformLocation(mProgTrace, "uEnvRotation"), mEnvRotation);
         glUniform1f(glGetUniformLocation(mProgTrace, "uEnvIntensity"), mEnvIntensity);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uEnvSky"), 1, mEnvSky);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uEnvHorizon"), 1, mEnvHorizon);
         glUniform3fv(glGetUniformLocation(mProgTrace, "uEnvGround"), 1, mEnvGround);
         glUniform1i(glGetUniformLocation(mProgTrace, "uEnvAsBg"), mEnvAsBg ? 1 : 0);

         glUniform1i(glGetUniformLocation(mProgTrace, "uLightCount"), lightCount);
         if (lightCount > 0)
         {
            glUniform1iv(glGetUniformLocation(mProgTrace, "uLightType"), lightCount, lTypes);
            glUniform3fv(glGetUniformLocation(mProgTrace, "uLightPos"), lightCount, lPos);
            glUniform3fv(glGetUniformLocation(mProgTrace, "uLightAim"), lightCount, lAim);
            glUniform3fv(glGetUniformLocation(mProgTrace, "uLightColor"), lightCount, lCol);
            glUniform2fv(glGetUniformLocation(mProgTrace, "uLightArea"), lightCount, lArea);
            glUniform3fv(glGetUniformLocation(mProgTrace, "uLightSpot"), lightCount, lSpot);
         }
      });

      mAccumReadIdx = writeIdx;
      mSampleIndex++;
   }

   glActiveTexture(GL_TEXTURE7);
   glBindTexture(GL_TEXTURE_2D, 0);
   for (int u = 6; u >= 1; u--)
   {
      glActiveTexture(GL_TEXTURE0 + u);
      glBindTexture(GL_TEXTURE_BUFFER, 0);
   }
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, 0);

   if (resolveDisplay)
      ResolveDisplay();
   return true;
}

int Render3DPathTrace::StepFrame(int width, int height, int targetSpp)
{
   if (targetSpp > 0 && mSampleIndex >= targetSpp)
      return 0;
   const int budgetSamples = std::max(1, sampleBudgetPerFrame);
   const int maxToRun = (targetSpp > 0) ? std::min(budgetSamples, targetSpp - mSampleIndex) : budgetSamples;
   const auto tStart = std::chrono::steady_clock::now();
   int done = 0;
   for (int i = 0; i < maxToRun; i++)
   {
      if (!StepSamples(width, height, 1, false))
         break;
      done++;
      if (i + 1 < maxToRun)
      {
         glFlush();
         const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tStart).count();
         if (elapsedMs >= (double)frameBudgetMs)
            break;
      }
   }
   if (done > 0)
      ResolveDisplay();
   return done;
}

bool Render3DPathTrace::RenderToSpp(int width, int height, int totalSpp)
{
   ResetAccumulation();
   return StepSamples(width, height, std::max(1, totalSpp), true);
}

void Render3DPathTrace::ResolveDisplay()
{
   if (mProgDisplay == 0 || mDisplay.fbo == 0)
      return;
   const unsigned int accumTex = mAccum[mAccumReadIdx].tex;
   GLUtil::RunShaderPass(mDisplay, mProgDisplay, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, accumTex);
      glUniform1i(glGetUniformLocation(mProgDisplay, "uAccum"), 0);
      glUniform1f(glGetUniformLocation(mProgDisplay, "uExposure"), exposure);
      glUniform1i(glGetUniformLocation(mProgDisplay, "uTonemap"), tonemap);
      glUniform3fv(glGetUniformLocation(mProgDisplay, "uBgColor"), 1, mBgColor);
      glUniform1f(glGetUniformLocation(mProgDisplay, "uBgOpacity"), mBgOpacity);
   });
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, 0);
}

bool Render3DPathTrace::ReadDisplayRGBA8(std::vector<unsigned char>& outRgba, int& outW, int& outH) const
{
   if (mDisplay.fbo == 0 || mWidth <= 0 || mHeight <= 0)
      return false;
   outW = mWidth;
   outH = mHeight;
   std::vector<unsigned char> raw((size_t)mWidth * (size_t)mHeight * 4u);

   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, mDisplay.fbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

   // Flip bottom-to-top OpenGL readback into top-to-bottom image order.
   outRgba.resize(raw.size());
   const size_t rowBytes = (size_t)mWidth * 4u;
   for (int y = 0; y < mHeight; y++)
   {
      std::memcpy(&outRgba[(size_t)y * rowBytes],
                  &raw[(size_t)(mHeight - 1 - y) * rowBytes],
                  rowBytes);
   }
   return true;
}

bool Render3DPathTrace::ReadLinearRGBA32F(std::vector<float>& outRgba, int& outW, int& outH) const
{
   const GLUtil::Fbo& acc = mAccum[mAccumReadIdx];
   if (acc.fbo == 0 || mWidth <= 0 || mHeight <= 0)
      return false;
   outW = mWidth;
   outH = mHeight;
   std::vector<float> raw((size_t)mWidth * (size_t)mHeight * 4u);

   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glBindFramebuffer(GL_FRAMEBUFFER, acc.fbo);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(0, 0, mWidth, mHeight, GL_RGBA, GL_FLOAT, raw.data());
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);

   outRgba.resize(raw.size());
   const size_t rowFloats = (size_t)mWidth * 4u;
   for (int y = 0; y < mHeight; y++)
   {
      std::memcpy(&outRgba[(size_t)y * rowFloats],
                  &raw[(size_t)(mHeight - 1 - y) * rowFloats],
                  rowFloats * sizeof(float));
   }
   return true;
}

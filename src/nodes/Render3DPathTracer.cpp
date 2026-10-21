#include "Render3DPathTracer.h"

#include "core/GLUtil.h"
#include "platform/OpenGLHeaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
   const char* kTraceSrc = R"GLSL(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform samplerBuffer uNodes;  // 2 texels per node: (min, left|first), (max, count)
uniform samplerBuffer uTriPos; // 3 texels per triangle: v0 + material, v1, v2
uniform samplerBuffer uTriAtt; // 4 texels: n0 + u0, n1 + u1, n2 + u2, (v0, v1, v2)
uniform samplerBuffer uMats;   // 3 texels: (base, metal), (emission, rough), (ior, transmission, opacity, layer)
uniform sampler2DArray uTex;
uniform sampler2D uPrev;
uniform sampler2D uEnvMap;
uniform int uHasEnv;
uniform float uEnvRotation, uEnvIntensity;
uniform vec3 uSky, uHorizon, uGround, uAmbient;
uniform mat4 uInvViewProj;
uniform vec2 uRes;
uniform int uSamples, uSpp, uBounces;
uniform uint uSeed;
uniform float uExposure;
uniform int uLightCount;
uniform int uLightType[4];
uniform vec3 uLightDir[4], uLightColor[4];
uniform float uLightIntensity[4], uLightSpot[4];
uniform int uPanorama;     // 0 camera, 1 equirect, 2 cubemap 3x2 (Render3DPost::PanoGlsl)
uniform mat3 uInvViewRot;  // camera space -> world, for the panorama's rays
uniform vec3 uEye;

const float PI = 3.14159265359;
//PANO_GLSL
uint rngState;
uint pcg(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float rnd() { rngState = pcg(rngState); return float(rngState) * (1.0 / 4294967296.0); }
vec3 toLin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

vec3 envColor(vec3 d) {
   if (uHasEnv == 1) {
      float phi = atan(d.z, d.x) + uEnvRotation;
      float theta = acos(clamp(d.y, -1.0, 1.0));
      return textureLod(uEnvMap, vec2(phi / (2.0 * PI) + 0.5, 1.0 - theta / PI), 0.0).rgb * uEnvIntensity;
   }
   float t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);
   vec3 lower = mix(toLin(uGround), toLin(uHorizon), smoothstep(0.0, 0.5, t));
   return mix(lower, toLin(uSky), smoothstep(0.5, 1.0, t)) * uEnvIntensity;
}

struct Hit { float t; int tri; float u, v; };

bool hitBox(vec3 ro, vec3 inv, vec3 bmin, vec3 bmax, float tmax) {
   vec3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
   vec3 lo = min(t0, t1), hi = max(t0, t1);
   float tn = max(max(lo.x, lo.y), max(lo.z, 0.0));
   float tf = min(min(hi.x, hi.y), min(hi.z, tmax));
   return tn <= tf;
}

Hit trace(vec3 ro, vec3 rd, float tmax, bool anyHit) {
   Hit h; h.t = tmax; h.tri = -1; h.u = 0.0; h.v = 0.0;
   vec3 inv = vec3(1.0) / mix(rd, vec3(1e-12), lessThan(abs(rd), vec3(1e-12)));
   int stack[32]; int sp = 0; stack[sp++] = 0;
   while (sp > 0) {
      int ni = stack[--sp];
      vec4 a = texelFetch(uNodes, ni * 2), b = texelFetch(uNodes, ni * 2 + 1);
      if (!hitBox(ro, inv, a.xyz, b.xyz, h.t)) continue;
      int first = floatBitsToInt(a.w), count = floatBitsToInt(b.w);
      if (count == 0) {
         if (sp < 30) { stack[sp++] = first; stack[sp++] = first + 1; }
         continue;
      }
      for (int i = 0; i < count; i++) {
         int ti = first + i;
         vec3 v0 = texelFetch(uTriPos, ti * 3).xyz, v1 = texelFetch(uTriPos, ti * 3 + 1).xyz, v2 = texelFetch(uTriPos, ti * 3 + 2).xyz;
         vec3 e1 = v1 - v0, e2 = v2 - v0, p = cross(rd, e2);
         float det = dot(e1, p);
         if (abs(det) < 1e-10) continue;
         float id = 1.0 / det;
         vec3 s = ro - v0;
         float u = dot(s, p) * id;
         if (u < 0.0 || u > 1.0) continue;
         vec3 q = cross(s, e1);
         float v = dot(rd, q) * id;
         if (v < 0.0 || u + v > 1.0) continue;
         float t = dot(e2, q) * id;
         if (t > 1e-4 && t < h.t) { h.t = t; h.tri = ti; h.u = u; h.v = v; if (anyHit) return h; }
      }
   }
   return h;
}

float ggxD(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-7); }
float smithG(float nv, float nl, float a) { float k = a * 0.5; return (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k)); }
vec3 fres(float c, vec3 f0) { return f0 + (1.0 - f0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0); }
void basis(vec3 n, out vec3 t, out vec3 b) {
   vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
   t = normalize(cross(up, n)); b = cross(n, t);
}
vec3 sampleGgxH(vec3 n, float a) {
   float r1 = rnd(), r2 = rnd();
   float phi = 2.0 * PI * r1;
   float ct = sqrt((1.0 - r2) / (1.0 + (a * a - 1.0) * r2)), st = sqrt(max(1.0 - ct * ct, 0.0));
   vec3 t, b; basis(n, t, b);
   return normalize(t * (cos(phi) * st) + b * (sin(phi) * st) + n * ct);
}

// Diffuse + GGX specular toward one light direction (no transmission lobe).
vec3 evalBsdf(vec3 n, vec3 v, vec3 l, vec3 base, float metal, float a, vec3 f0) {
   float nl = dot(n, l), nv = max(dot(n, v), 1e-4);
   if (nl <= 0.0) return vec3(0.0);
   vec3 h = normalize(v + l);
   vec3 F = fres(max(dot(v, h), 0.0), f0);
   vec3 spec = F * ggxD(max(dot(n, h), 0.0), a) * smithG(nv, nl, a) / (4.0 * nv * nl + 1e-5);
   vec3 diff = (1.0 - F) * (1.0 - metal) * base / PI;
   return (diff + spec) * nl;
}

vec3 radiance(vec3 ro, vec3 rd, out float primaryMiss) {
   vec3 L = vec3(0.0), thr = vec3(1.0);
   primaryMiss = 0.0;
   int passes = 0;
   for (int bounce = 0; bounce <= uBounces; bounce++) {
      Hit h = trace(ro, rd, 1e30, false);
      if (h.tri < 0) {
         if (bounce == 0) primaryMiss = 1.0;
         L += thr * envColor(rd);
         break;
      }
      vec4 p0 = texelFetch(uTriPos, h.tri * 3);
      int mi = int(p0.w);
      vec4 n0 = texelFetch(uTriAtt, h.tri * 4), n1 = texelFetch(uTriAtt, h.tri * 4 + 1), n2 = texelFetch(uTriAtt, h.tri * 4 + 2);
      vec4 vv = texelFetch(uTriAtt, h.tri * 4 + 3);
      float w = 1.0 - h.u - h.v;
      vec3 n = normalize(n0.xyz * w + n1.xyz * h.u + n2.xyz * h.v);
      vec2 uv = vec2(n0.w * w + n1.w * h.u + n2.w * h.v, vv.x * w + vv.y * h.u + vv.z * h.v);
      vec4 m0 = texelFetch(uMats, mi * 3), m1 = texelFetch(uMats, mi * 3 + 1), m2 = texelFetch(uMats, mi * 3 + 2);
      vec3 base = m0.rgb;
      if (m2.w >= 0.0) base *= toLin(texture(uTex, vec3(uv, m2.w)).rgb);
      float metal = m0.a, rough = clamp(m1.a, 0.03, 1.0), a = rough * rough;
      float ior = max(m2.x, 1.0), trans = m2.y, opacity = m2.z;
      vec3 pos = ro + rd * h.t;
      // Cut-out opacity: pass straight through with the matching probability.
      if (opacity < 1.0 && rnd() > opacity && passes < 8) { ro = pos + rd * 1e-3; bounce--; passes++; continue; }
      bool inside = dot(n, rd) > 0.0;
      vec3 nf = inside ? -n : n;
      vec3 v = -rd;
      L += thr * m1.rgb; // emission
      vec3 f0 = mix(vec3(0.04), base, metal);

      // Next-event estimation: one shadow ray per light (glass blocks it).
      for (int i = 0; i < 4; i++) {
         if (i >= uLightCount) break;
         int ty = uLightType[i];
         vec3 lc = toLin(uLightColor[i]) * uLightIntensity[i];
         if (ty == 3) { L += thr * base * (1.0 - metal) * lc * (1.0 - trans); continue; }
         vec3 ldir; float dist = 1e30, att = 1.0;
         if (ty == 1 || ty == 4) {
            vec3 tl = uLightDir[i] - pos; dist = length(tl); ldir = tl / max(dist, 1e-4);
            att = 1.0 / (1.0 + dist * dist * 0.25);
            if (ty == 4) {
               float c = dot(-ldir, normalize(-uLightDir[i]));
               att *= smoothstep(cos(radians(uLightSpot[i])), cos(radians(uLightSpot[i] * 0.8)), c);
            }
         } else {
            ldir = normalize(uLightDir[i]);
         }
         if (dot(nf, ldir) <= 0.0 || att <= 0.0) continue;
         Hit s = trace(pos + nf * 1e-3, ldir, dist - 2e-3, true);
         if (s.tri >= 0) continue;
         L += thr * evalBsdf(nf, v, ldir, base, metal, a, f0) * lc * att * (1.0 - trans * (1.0 - metal));
      }

      // Sample the next direction.
      vec3 nd;
      if (trans > 0.0 && rnd() < trans * (1.0 - metal)) {
         vec3 hm = sampleGgxH(nf, a);
         float eta = inside ? ior : 1.0 / ior;
         float c = clamp(dot(v, hm), 0.0, 1.0);
         float r0 = (1.0 - ior) / (1.0 + ior); r0 *= r0;
         float F = r0 + (1.0 - r0) * pow(1.0 - c, 5.0);
         vec3 refr = refract(rd, hm, eta);
         if (rnd() < F || dot(refr, refr) < 1e-6) { nd = reflect(rd, hm); ro = pos + nf * 1e-3; }
         else { nd = normalize(refr); ro = pos - nf * 1e-3; thr *= base; }
      } else {
         float nv = max(dot(nf, v), 1e-4);
         vec3 fv = fres(nv, f0);
         float pSpec = metal > 0.99 ? 1.0 : clamp(max(fv.r, max(fv.g, fv.b)) + metal, 0.1, 0.9);
         if (rnd() < pSpec) {
            vec3 hm = sampleGgxH(nf, a);
            nd = reflect(rd, hm);
            float nl = dot(nf, nd);
            if (nl <= 0.0) break;
            float vh = max(dot(v, hm), 1e-4), nh = max(dot(nf, hm), 1e-4);
            thr *= fres(vh, f0) * smithG(nv, nl, a) * vh / (nh * nv) / pSpec;
         } else {
            float r1 = rnd(), r2 = rnd(), r = sqrt(r1), phi = 2.0 * PI * r2;
            vec3 t, b; basis(nf, t, b);
            nd = normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + nf * sqrt(max(1.0 - r1, 0.0)));
            thr *= (1.0 - fv) * (1.0 - metal) * base / (1.0 - pSpec);
         }
         ro = pos + nf * 1e-3;
      }
      rd = nd;
      // Russian roulette past the third bounce.
      if (bounce >= 3) {
         float q = clamp(max(thr.r, max(thr.g, thr.b)), 0.05, 0.95);
         if (rnd() > q) break;
         thr /= q;
      }
      if (any(isnan(thr))) break;
   }
   return L;
}

void main() {
   ivec2 px = ivec2(gl_FragCoord.xy);
   rngState = uint(px.x) * 1973u + uint(px.y) * 9277u + uSeed * 26699u;
   vec3 sum = vec3(0.0); float miss = 0.0;
   for (int s = 0; s < uSpp; s++) {
      vec2 jit = vec2(rnd(), rnd());
      vec2 ndc = ((gl_FragCoord.xy - 0.5 + jit) / uRes) * 2.0 - 1.0;
      vec3 ro, rd;
      if (uPanorama > 0) {
         ro = uEye;
         rd = normalize(uInvViewRot * panoDir((gl_FragCoord.xy - 0.5 + jit) / uRes, uPanorama));
      } else {
         vec4 p0 = uInvViewProj * vec4(ndc, -1.0, 1.0), p1 = uInvViewProj * vec4(ndc, 1.0, 1.0);
         ro = p0.xyz / p0.w; rd = normalize(p1.xyz / p1.w - ro);
      }
      float m;
      vec3 c = radiance(ro, rd, m);
      if (!any(isnan(c)) && !any(isinf(c))) sum += min(c, vec3(64.0)); // firefly clamp
      miss += m;
   }
   // Alpha = coverage (1 - primary miss), so the finish can key the background.
   vec4 cur = vec4(sum / float(uSpp) * uExposure, 1.0 - miss / float(uSpp));
   vec4 prev = texelFetch(uPrev, px, 0);
   float n = float(uSamples), k = float(uSpp);
   fragColor = uSamples == 0 ? cur : (prev * n + cur * k) / (n + k);
}
)GLSL";

   // --- BVH (binned SAH) -------------------------------------------------
   struct Tri
   {
      float v[3][3];
      float n[3][3];
      float uv[3][2];
      int mat;
      float c[3]; // centroid
      float lo[3], hi[3];
   };

   struct Node
   {
      float lo[3], hi[3];
      int first = 0, count = 0; // count 0 = interior, children at first, first + 1
   };

   void Grow(float lo[3], float hi[3], const float a[3], const float b[3])
   {
      for (int k = 0; k < 3; k++)
      {
         lo[k] = std::min(lo[k], a[k]);
         hi[k] = std::max(hi[k], b[k]);
      }
   }

   float Area(const float lo[3], const float hi[3])
   {
      const float x = std::max(0.0f, hi[0] - lo[0]), y = std::max(0.0f, hi[1] - lo[1]), z = std::max(0.0f, hi[2] - lo[2]);
      return 2.0f * (x * y + y * z + z * x);
   }

   void BuildBvh(std::vector<Tri>& tris, std::vector<Node>& nodes)
   {
      constexpr int kBins = 12;
      nodes.clear();
      nodes.reserve(tris.size() * 2);
      nodes.push_back(Node());
      struct Item { int node, first, count; };
      std::vector<Item> work{ { 0, 0, (int)tris.size() } };
      while (!work.empty())
      {
         const Item it = work.back();
         work.pop_back();
         float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
         float clo[3] = { 1e30f, 1e30f, 1e30f }, chi[3] = { -1e30f, -1e30f, -1e30f };
         for (int i = it.first; i < it.first + it.count; i++)
         {
            Grow(lo, hi, tris[i].lo, tris[i].hi);
            Grow(clo, chi, tris[i].c, tris[i].c);
         }
         for (int k = 0; k < 3; k++)
         {
            nodes[it.node].lo[k] = lo[k];
            nodes[it.node].hi[k] = hi[k];
         }
         if (it.count <= 4)
         {
            nodes[it.node].first = it.first;
            nodes[it.node].count = it.count;
            continue;
         }
         int bestAxis = -1, bestSplit = 0;
         float bestCost = Area(lo, hi) * (float)it.count;
         for (int axis = 0; axis < 3; axis++)
         {
            const float extent = chi[axis] - clo[axis];
            if (extent <= 1e-9f)
               continue;
            int binCount[kBins] = {};
            float binLo[kBins][3], binHi[kBins][3];
            for (int b = 0; b < kBins; b++)
               for (int k = 0; k < 3; k++) { binLo[b][k] = 1e30f; binHi[b][k] = -1e30f; }
            for (int i = it.first; i < it.first + it.count; i++)
            {
               const int b = std::min(kBins - 1, (int)((tris[i].c[axis] - clo[axis]) / extent * kBins));
               binCount[b]++;
               Grow(binLo[b], binHi[b], tris[i].lo, tris[i].hi);
            }
            for (int s = 1; s < kBins; s++)
            {
               float llo[3] = { 1e30f, 1e30f, 1e30f }, lhi[3] = { -1e30f, -1e30f, -1e30f };
               float rlo[3] = { 1e30f, 1e30f, 1e30f }, rhi[3] = { -1e30f, -1e30f, -1e30f };
               int lc = 0, rc = 0;
               for (int b = 0; b < s; b++) if (binCount[b]) { Grow(llo, lhi, binLo[b], binHi[b]); lc += binCount[b]; }
               for (int b = s; b < kBins; b++) if (binCount[b]) { Grow(rlo, rhi, binLo[b], binHi[b]); rc += binCount[b]; }
               if (lc == 0 || rc == 0)
                  continue;
               const float cost = 0.5f + Area(llo, lhi) * (float)lc + Area(rlo, rhi) * (float)rc;
               if (cost < bestCost)
               {
                  bestCost = cost;
                  bestAxis = axis;
                  bestSplit = s;
               }
            }
         }
         int mid;
         if (bestAxis < 0)
         {
            if (it.count <= 32)
            {
               nodes[it.node].first = it.first;
               nodes[it.node].count = it.count;
               continue;
            }
            mid = it.first + it.count / 2; // degenerate centroids: halve so no leaf gets huge
         }
         else
         {
            const float extent = chi[bestAxis] - clo[bestAxis];
            auto pivot = std::partition(tris.begin() + it.first, tris.begin() + it.first + it.count, [&](const Tri& t) {
               return std::min(kBins - 1, (int)((t.c[bestAxis] - clo[bestAxis]) / extent * kBins)) < bestSplit;
            });
            mid = (int)(pivot - tris.begin());
            if (mid == it.first || mid == it.first + it.count)
               mid = it.first + it.count / 2;
         }
         const int left = (int)nodes.size();
         nodes.push_back(Node());
         nodes.push_back(Node());
         nodes[it.node].first = left;
         nodes[it.node].count = 0;
         work.push_back({ left, it.first, mid - it.first });
         work.push_back({ left + 1, mid, it.first + it.count - mid });
      }
   }

   float IntBits(int i)
   {
      float f;
      std::memcpy(&f, &i, 4);
      return f;
   }

   float ToLinear(float c) { return std::pow(std::max(c, 0.0f), 2.2f); }

   void UploadTbo(unsigned int& buf, unsigned int& tbo, const std::vector<float>& data)
   {
      if (buf == 0) glGenBuffers(1, &buf);
      if (tbo == 0) glGenTextures(1, &tbo);
      glBindBuffer(GL_TEXTURE_BUFFER, buf);
      glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)std::max<size_t>(16, data.size() * sizeof(float)),
                   data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
      glBindTexture(GL_TEXTURE_BUFFER, tbo);
      glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buf);
      glBindTexture(GL_TEXTURE_BUFFER, 0);
      glBindBuffer(GL_TEXTURE_BUFFER, 0);
   }

   constexpr size_t kMaxTriangles = 4000000;
   constexpr int kTexSize = 1024;
   constexpr int kMaxTexLayers = 16;
}

Render3DPathTracer::~Render3DPathTracer()
{
   ReleaseScene();
   if (mProgram != 0) glDeleteProgram(mProgram);
   for (int i = 0; i < 2; i++)
   {
      if (mAccum[i] != 0) glDeleteTextures(1, &mAccum[i]);
      if (mAccumFbo[i] != 0) glDeleteFramebuffers(1, &mAccumFbo[i]);
   }
}

void Render3DPathTracer::ReleaseScene()
{
   for (int i = 0; i < 4; i++)
   {
      if (mTbo[i] != 0) { glDeleteTextures(1, &mTbo[i]); mTbo[i] = 0; }
      if (mBuf[i] != 0) { glDeleteBuffers(1, &mBuf[i]); mBuf[i] = 0; }
   }
   if (mTexArray != 0) { glDeleteTextures(1, &mTexArray); mTexArray = 0; }
   mTriCount = 0;
}

bool Render3DPathTracer::EnsureProgram()
{
   if (mProgram != 0)
      return true;
   if (mProgramFailed)
      return false;
   std::string src = kTraceSrc;
   const std::string marker = "//PANO_GLSL";
   src.replace(src.find(marker), marker.size(), Render3DPost::PanoGlsl());
   mProgram = GLUtil::CompileProgram(src.c_str());
   if (mProgram == 0)
   {
      fprintf(stderr, "Render3DPathTracer: shader compile failed\n");
      mProgramFailed = true;
      return false;
   }
   return true;
}

bool Render3DPathTracer::SetScene(const std::vector<Source>& sources, uint64_t geometryKey)
{
   if (geometryKey == mGeometryKey && mTriCount > 0)
      return true;
   mGeometryKey = geometryKey;
   mSamples = 0;

   std::vector<Tri> tris;
   std::vector<float> mats;
   std::vector<unsigned int> texSources;
   for (const Source& s : sources)
   {
      if (s.mesh == nullptr || s.mesh->indices.size() < 3)
         continue;
      const Material& m = s.material;
      int layer = -1;
      if (s.surfaceTex != 0)
      {
         auto found = std::find(texSources.begin(), texSources.end(), s.surfaceTex);
         if (found != texSources.end())
            layer = (int)(found - texSources.begin());
         else if ((int)texSources.size() < kMaxTexLayers)
         {
            layer = (int)texSources.size();
            texSources.push_back(s.surfaceTex);
         }
      }
      const int matId = (int)(mats.size() / 12);
      const float em = std::max(0.0f, m.emission);
      const float mv[12] = { ToLinear(m.color[0]), ToLinear(m.color[1]), ToLinear(m.color[2]), m.metallic,
                             ToLinear(m.emissionColor[0]) * em, ToLinear(m.emissionColor[1]) * em,
                             ToLinear(m.emissionColor[2]) * em, m.roughness,
                             m.ior, m.transmission, m.opacity, (float)layer };
      mats.insert(mats.end(), mv, mv + 12);

      std::vector<Mat4> worlds = s.instances;
      if (worlds.empty())
         worlds.push_back(s.model);
      const Mesh& mesh = *s.mesh;
      for (const Mat4& M : worlds)
      {
         if (tris.size() + mesh.indices.size() / 3 > kMaxTriangles)
            break;
         const Mat4 inv = Mat4::Inverse(M);
         for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
         {
            Tri t;
            t.mat = matId;
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++)
            {
               const unsigned int idx = mesh.indices[i + k];
               if (idx >= mesh.vertices.size())
               {
                  ok = false;
                  break;
               }
               const Vertex& vx = mesh.vertices[idx];
               const float p[3] = { vx.px, vx.py, vx.pz }, nn[3] = { vx.nx, vx.ny, vx.nz };
               for (int r = 0; r < 3; r++)
               {
                  t.v[k][r] = M.m[0 + r] * p[0] + M.m[4 + r] * p[1] + M.m[8 + r] * p[2] + M.m[12 + r];
                  // Inverse-transpose of a column-major matrix: row r of inv^T = column r of inv.
                  t.n[k][r] = inv.m[r * 4 + 0] * nn[0] + inv.m[r * 4 + 1] * nn[1] + inv.m[r * 4 + 2] * nn[2];
               }
               const float len = std::sqrt(t.n[k][0] * t.n[k][0] + t.n[k][1] * t.n[k][1] + t.n[k][2] * t.n[k][2]);
               for (int r = 0; r < 3; r++)
                  t.n[k][r] = len > 1e-12f ? t.n[k][r] / len : (r == 1 ? 1.0f : 0.0f);
               t.uv[k][0] = vx.u;
               t.uv[k][1] = vx.v;
            }
            if (!ok)
               continue;
            for (int r = 0; r < 3; r++)
            {
               t.lo[r] = std::min(t.v[0][r], std::min(t.v[1][r], t.v[2][r]));
               t.hi[r] = std::max(t.v[0][r], std::max(t.v[1][r], t.v[2][r]));
               t.c[r] = (t.lo[r] + t.hi[r]) * 0.5f;
            }
            if (!std::isfinite(t.c[0]) || !std::isfinite(t.c[1]) || !std::isfinite(t.c[2]))
               continue;
            tris.push_back(t);
         }
      }
   }

   ReleaseScene();
   if (tris.empty())
      return false;

   std::vector<Node> nodes;
   BuildBvh(tris, nodes);

   std::vector<float> nodeData, posData, attData;
   nodeData.reserve(nodes.size() * 8);
   for (const Node& n : nodes)
   {
      const float d[8] = { n.lo[0], n.lo[1], n.lo[2], IntBits(n.first), n.hi[0], n.hi[1], n.hi[2], IntBits(n.count) };
      nodeData.insert(nodeData.end(), d, d + 8);
   }
   posData.reserve(tris.size() * 12);
   attData.reserve(tris.size() * 16);
   for (const Tri& t : tris)
   {
      const float p[12] = { t.v[0][0], t.v[0][1], t.v[0][2], (float)t.mat, t.v[1][0], t.v[1][1], t.v[1][2], 0.0f,
                            t.v[2][0], t.v[2][1], t.v[2][2], 0.0f };
      posData.insert(posData.end(), p, p + 12);
      const float a[16] = { t.n[0][0], t.n[0][1], t.n[0][2], t.uv[0][0], t.n[1][0], t.n[1][1], t.n[1][2], t.uv[1][0],
                            t.n[2][0], t.n[2][1], t.n[2][2], t.uv[2][0], t.uv[0][1], t.uv[1][1], t.uv[2][1], 0.0f };
      attData.insert(attData.end(), a, a + 16);
   }
   UploadTbo(mBuf[0], mTbo[0], nodeData);
   UploadTbo(mBuf[1], mTbo[1], posData);
   UploadTbo(mBuf[2], mTbo[2], attData);
   UploadTbo(mBuf[3], mTbo[3], mats);
   mTriCount = (int)tris.size();

   // Surface textures into one array, a layer each, resampled to 1024 x 1024.
   mTexLayers = std::max(1, (int)texSources.size());
   glGenTextures(1, &mTexArray);
   glBindTexture(GL_TEXTURE_2D_ARRAY, mTexArray);
   glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, kTexSize, kTexSize, mTexLayers);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
   if (!texSources.empty())
   {
      GLint prevRead = 0, prevDraw = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
      const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
      glDisable(GL_SCISSOR_TEST);
      unsigned int fbo[2];
      glGenFramebuffers(2, fbo);
      for (size_t i = 0; i < texSources.size(); i++)
      {
         GLint sw = 0, sh = 0;
         glBindTexture(GL_TEXTURE_2D, texSources[i]);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &sw);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sh);
         glBindTexture(GL_TEXTURE_2D, 0);
         if (sw <= 0 || sh <= 0)
            continue;
         glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
         glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texSources[i], 0);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
         glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mTexArray, 0, (int)i);
         glBlitFramebuffer(0, 0, sw, sh, 0, 0, kTexSize, kTexSize, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      }
      glDeleteFramebuffers(2, fbo);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
      if (prevScissor) glEnable(GL_SCISSOR_TEST);
   }
   return true;
}

void Render3DPathTracer::EnsureTargets(int w, int h)
{
   if (mAccum[0] != 0 && w == mWidth && h == mHeight)
      return;
   GLint prev = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
   for (int i = 0; i < 2; i++)
   {
      if (mAccum[i] != 0) glDeleteTextures(1, &mAccum[i]);
      if (mAccumFbo[i] != 0) glDeleteFramebuffers(1, &mAccumFbo[i]);
      glGenTextures(1, &mAccum[i]);
      glBindTexture(GL_TEXTURE_2D, mAccum[i]);
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, w, h);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glGenFramebuffers(1, &mAccumFbo[i]);
      glBindFramebuffer(GL_FRAMEBUFFER, mAccumFbo[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mAccum[i], 0);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, prev);
   glBindTexture(GL_TEXTURE_2D, 0);
   mWidth = w;
   mHeight = h;
   mSamples = 0;
}

unsigned int Render3DPathTracer::Trace(int w, int h, const Mat4& viewProj, const float eye[3],
                                       const std::vector<Light>& lights, const Env& env, float exposure, int bounces,
                                       int maxSamples, int spp, int panorama, const Mat4& view)
{
   if (mTriCount == 0 || !EnsureProgram())
      return 0;
   EnsureTargets(w, h);
   if (mSamples >= maxSamples)
      return mAccum[mFront];
   spp = std::max(1, std::min(spp, maxSamples - mSamples));

   GLint prevFbo = 0, vp[4];
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, vp);
   const GLboolean blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE),
                   scissor = glIsEnabled(GL_SCISSOR_TEST);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_SCISSOR_TEST);

   const int back = 1 - mFront;
   glBindFramebuffer(GL_FRAMEBUFFER, mAccumFbo[back]);
   glViewport(0, 0, w, h);
   glUseProgram(mProgram);
   const char* tboNames[4] = { "uNodes", "uTriPos", "uTriAtt", "uMats" };
   for (int i = 0; i < 4; i++)
   {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_BUFFER, mTbo[i]);
      glUniform1i(glGetUniformLocation(mProgram, tboNames[i]), i);
   }
   glActiveTexture(GL_TEXTURE4);
   glBindTexture(GL_TEXTURE_2D_ARRAY, mTexArray);
   glUniform1i(glGetUniformLocation(mProgram, "uTex"), 4);
   glActiveTexture(GL_TEXTURE5);
   glBindTexture(GL_TEXTURE_2D, mAccum[mFront]);
   glUniform1i(glGetUniformLocation(mProgram, "uPrev"), 5);
   glActiveTexture(GL_TEXTURE6);
   glBindTexture(GL_TEXTURE_2D, env.tex);
   glUniform1i(glGetUniformLocation(mProgram, "uEnvMap"), 6);
   glUniform1i(glGetUniformLocation(mProgram, "uHasEnv"), env.tex != 0 ? 1 : 0);
   glUniform1f(glGetUniformLocation(mProgram, "uEnvRotation"), env.rotation);
   glUniform1f(glGetUniformLocation(mProgram, "uEnvIntensity"), env.intensity);
   glUniform3fv(glGetUniformLocation(mProgram, "uSky"), 1, env.sky);
   glUniform3fv(glGetUniformLocation(mProgram, "uHorizon"), 1, env.horizon);
   glUniform3fv(glGetUniformLocation(mProgram, "uGround"), 1, env.ground);
   glUniform3fv(glGetUniformLocation(mProgram, "uAmbient"), 1, env.ambient);
   const Mat4 inv = Mat4::Inverse(viewProj);
   glUniformMatrix4fv(glGetUniformLocation(mProgram, "uInvViewProj"), 1, GL_FALSE, inv.m);
   glUniform2f(glGetUniformLocation(mProgram, "uRes"), (float)w, (float)h);
   glUniform1i(glGetUniformLocation(mProgram, "uSamples"), mSamples);
   glUniform1i(glGetUniformLocation(mProgram, "uSpp"), spp);
   glUniform1i(glGetUniformLocation(mProgram, "uBounces"), std::clamp(bounces, 1, 16));
   glUniform1ui(glGetUniformLocation(mProgram, "uSeed"), mFrameSeed++);
   glUniform1f(glGetUniformLocation(mProgram, "uExposure"), exposure);
   // Camera space -> world is the transpose of the view's rotation (rows of
   // the column-major view matrix are the camera axes in world space).
   const float invRot[9] = { view.m[0], view.m[4], view.m[8], view.m[1], view.m[5], view.m[9],
                             view.m[2], view.m[6], view.m[10] };
   glUniform1i(glGetUniformLocation(mProgram, "uPanorama"), panorama);
   glUniformMatrix3fv(glGetUniformLocation(mProgram, "uInvViewRot"), 1, GL_FALSE, invRot);
   glUniform3fv(glGetUniformLocation(mProgram, "uEye"), 1, eye);
   int types[4] = {}, count = 0;
   float dirs[12] = {}, cols[12] = {}, ints[4] = {}, spots[4] = {};
   for (const Light& l : lights)
   {
      if (count >= 4)
         break;
      types[count] = l.type;
      for (int k = 0; k < 3; k++)
      {
         dirs[count * 3 + k] = l.dir[k];
         cols[count * 3 + k] = l.color[k];
      }
      ints[count] = l.intensity;
      spots[count] = l.spotAngle;
      count++;
   }
   glUniform1i(glGetUniformLocation(mProgram, "uLightCount"), count);
   glUniform1iv(glGetUniformLocation(mProgram, "uLightType"), 4, types);
   glUniform3fv(glGetUniformLocation(mProgram, "uLightDir"), 4, dirs);
   glUniform3fv(glGetUniformLocation(mProgram, "uLightColor"), 4, cols);
   glUniform1fv(glGetUniformLocation(mProgram, "uLightIntensity"), 4, ints);
   glUniform1fv(glGetUniformLocation(mProgram, "uLightSpot"), 4, spots);
   GLUtil::DrawFullscreenQuad();

   for (int i = 0; i < 7; i++)
   {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_BUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
   }
   glActiveTexture(GL_TEXTURE0);
   glUseProgram(0);
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glViewport(vp[0], vp[1], vp[2], vp[3]);
   if (blend) glEnable(GL_BLEND);
   if (depth) glEnable(GL_DEPTH_TEST);
   if (cull) glEnable(GL_CULL_FACE);
   if (scissor) glEnable(GL_SCISSOR_TEST);

   mFront = back;
   mSamples += spp;
   return mAccum[mFront];
}

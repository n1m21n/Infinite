// Render 3D through the Metal engine (src/engine): the same scene the GL raster draws, lit with ray-traced
// shadows and reflections and live GI. Opt-in per node (liveEngine). Instanced copies, glass, emission and the
// Environment node's HDRI are carried; textures and per-copy colours are not drawn yet.
// Returns false (GL draws instead) only when the engine itself is unavailable.
#include "Geometry3DNodes.h"
#include "ColorSpace.h"
#include "EnvironmentNode.h"
#include "GeometryOpNodes.h"
#include "Render3DHair.h"
#include "SceneNodes.h"
#include "Transport.h"
#include "field/MaterialRegistry.h"
#include "MeshWear.h"
#include "UtilityNodes.h"

#include "gl3.h"
#include "engine/Scene.h"
#include "engine/rhi/Rhi.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

struct Render3DNode::EngineState
{
   std::unique_ptr<rhi::Device> dev;
   std::unique_ptr<rhi::RasterRenderer> raster;
   std::unique_ptr<rhi::SharedImage> img;
   std::unique_ptr<rhi::SharedImage> imgLo; // row 8.4: the live picture at the upscale percentage
   bool upReset = true;                     // the upscaler's history starts over on the next frame
   std::vector<std::pair<const void*, unsigned long long>> meshSig;
   unsigned frame = 0;
   unsigned int readFbo = 0;
   bool failed = false;
   unsigned long long sceneHash = 0;
   int settled = 0;   // path-traced samples since the scene last changed
   bool exportWarm = false; // the live history is already converged by an earlier exported frame
   double offlineAt = -1.0; // the transport time of the last offline cook (a repeat cook at it reuses the picture)
   double profWarmGpu = 0.0; // INFINITE_ENGINE_PROF: GPU ms of this frame's warm-up renders
   // Row 6.1: each plain source's model on the last cooked frame, for settle's motion blur (as the GL path's mPrevModel).
   std::unordered_map<const void*, Mat4> prevModel, curModel;
   double motionAt = -1.0; // the transport time of curModel; a cook at a new time moves it to prevModel
   std::vector<float> envPixels;   // the HDRI read back from GL, one mip at most 2048 wide
   int envW = 0, envH = 0, envLod = 0;
   unsigned int envTex = 0;
   unsigned long long envRev = ~0ull, envSerial = 0;
   // Material maps read back from GL, by (texture, the source's texture revision); dropped when a frame stops using them.
   struct Map { unsigned int tex = 0; unsigned long long rev = 0, key = 0; int w = 0, h = 0; std::vector<uint8_t> px; bool used = false; };
   std::vector<Map> maps;
   unsigned long long mapSerial = 0;
   // Row 8.6: the map with its sun taken out, for pure live frames (LiveSun).
   std::vector<float> envLive;
   unsigned long long envLiveSerial = ~0ull;
   bool envLiveSun = false;
   float envLiveDir[3] = {}, envLiveLight[3] = {};
};

void Render3DNode::ReleaseEngine()
{
   if (mEng == nullptr)
      return;
   if (mEng->readFbo != 0)
      glDeleteFramebuffers(1, &mEng->readFbo);
   delete mEng;
   mEng = nullptr;
}

#if defined(__APPLE__) || defined(INFINITE_VULKAN) // the engine: Metal on macOS, Vulkan where it is built in (row 9.5)
namespace
{
   engine::MeshData ToMeshData(const Mesh& m)
   {
      engine::MeshData d;
      d.posNormal.reserve(m.vertices.size() * 6);
      for (const Vertex& v : m.vertices)
         d.posNormal.insert(d.posNormal.end(), { v.px, v.py, v.pz, v.nx, v.ny, v.nz });
      d.indices.assign(m.indices.begin(), m.indices.end());
      d.uv.reserve(m.vertices.size() * 2);
      for (const Vertex& v : m.vertices)
         d.uv.insert(d.uv.end(), { v.u, v.v });
      return d;
   }
}

namespace
{
   // A material map for the engine: the GL texture's level 0 read back once per (texture, revision), at most
   // engine::kMaxTextures distinct maps a frame (past that the material draws plain, as before maps were carried).
   template <class State> // Render3DNode::EngineState (private, so deduced, not named)
   auto CacheMap(State& E, unsigned int tex, unsigned long long rev) -> decltype(E.maps.data())
   {
      decltype(E.maps.data()) m = nullptr;
      for (auto& c : E.maps)
         if (c.tex == tex) { m = &c; break; }
      if (m == nullptr)
      {
         E.maps.emplace_back();
         m = &E.maps.back();
         m->tex = tex;
         m->key = 0;
      }
      if (m->key == 0 || m->rev != rev)
      {
         glBindTexture(GL_TEXTURE_2D, tex);
         int tw = 0, th = 0;
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
         if (tw <= 0 || th <= 0)
            return nullptr;
         m->px.assign((size_t)tw * th * 4, 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, m->px.data());
         glPixelStorei(GL_PACK_ALIGNMENT, 4);
         glBindTexture(GL_TEXTURE_2D, 0);
         m->w = tw; m->h = th; m->rev = rev;
         m->key = ++E.mapSerial;
      }
      m->used = true;
      return m;
   }
   template <class State>
   int MapIndex(State& E, engine::Frame& fr, unsigned int tex, unsigned long long rev)
   {
      if (tex == 0)
         return -1;
      auto* m = CacheMap(E, tex, rev);
      if (m == nullptr)
         return -1;
      for (size_t i = 0; i < fr.textures.size(); i++)
         if (fr.textures[i].key == m->key)
            return (int)i;
      if ((int)fr.textures.size() >= engine::kMaxTextures)
         return -1;
      engine::TextureData t;
      t.rgba = m->px.data(); t.w = m->w; t.h = m->h; t.key = m->key;
      fr.textures.push_back(t);
      return (int)fr.textures.size() - 1;
   }
}

namespace
{
   // Row 4.3: a bump (grey height) map as real relief for the rays. The map's grey is read where Render 3D's raster
   // reads it (uv * mapping scale + translate), 1 = the surface, 0 = 4 x strength texels below it: the same depth
   // parallax occlusion (row 4.2) marches, so the raster and the rays agree. Each triangle is cut into n x n by
   // barycentric steps, n from the longest edge in texels, all triangles of a mesh alike so no edge cracks; capped
   // by the machine's memory (2 KB of RAM a triangle: 8 GB -> 4 M). Raster keeps the base mesh.
   bool WantsDisplace(IGeometrySource* src)
   {
      const Material mat = src->GetMaterial();
      if (!mat.normalBump || mat.hair || src->GetMaterialTexture(kMapNormal) == 0 || mat.normalStrength <= 0.0f)
         return false;
      if (mat.emission > 0.0f) // emissive triangles are sampled by primitive id from the drawn mesh
         return false;
      const MappingTransform mt = src->GetMappingTransform();
      return mt.space == kMapSpaceUv && mt.breakup <= 0.001f && mt.rotate[0] == 0.0f && mt.rotate[1] == 0.0f && mt.rotate[2] == 0.0f;
   }
   // Rows 4.4 / 3.3: the per-vertex stream is built for a mesh whose material program reads curvature, cavity or attr.
   const Field::MaterialProgramEntry* WearProgram(IGeometrySource* src)
   {
      const Field::MaterialProgramEntry* e = Field::FindMaterialProgram(src->GetMaterial().fieldProgram);
      return e != nullptr && (e->usesWear || e->usesAttr) ? e : nullptr;
   }
   bool WantsWear(IGeometrySource* src) { return WearProgram(src) != nullptr; }
   size_t TriangleCap()
   {
      unsigned long long mem = 8ull << 30;
#if defined(__APPLE__)
      size_t len = sizeof(mem);
      sysctlbyname("hw.memsize", &mem, &len, nullptr, 0);
#endif
      return (size_t)(mem / 2048);
   }
   template <class MapT>
   void Displace(engine::MeshData& d, const MapT& map, const Material& mat, const MappingTransform& mt, int wrap, size_t cap)
   {
      const size_t nv = d.posNormal.size() / 6, nt = d.indices.size() / 3;
      if (nt == 0 || d.uv.size() != nv * 2 || map.w <= 0 || map.h <= 0)
         return;
      const float su = mt.scale[0], sv = mt.scale[1];
      // World length per uv unit over the whole mesh (object space), so the depth is in the mesh's own units.
      double aw = 0.0, au = 0.0; float maxTx = 0.0f;
      for (size_t t = 0; t < nt; t++)
      {
         const uint32_t i[3] = { d.indices[t * 3], d.indices[t * 3 + 1], d.indices[t * 3 + 2] };
         const float* p[3] = { &d.posNormal[i[0] * 6], &d.posNormal[i[1] * 6], &d.posNormal[i[2] * 6] };
         const float* u[3] = { &d.uv[i[0] * 2], &d.uv[i[1] * 2], &d.uv[i[2] * 2] };
         const float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] }, e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
         const float cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
         aw += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
         au += 0.5 * std::fabs((u[1][0] - u[0][0]) * (u[2][1] - u[0][1]) - (u[2][0] - u[0][0]) * (u[1][1] - u[0][1]));
         for (int k = 0; k < 3; k++)
         {
            const float* a = u[k]; const float* b = u[(k + 1) % 3];
            maxTx = std::max(maxTx, std::max(std::fabs(b[0] - a[0]) * su * map.w, std::fabs(b[1] - a[1]) * sv * map.h));
         }
      }
      if (au <= 1e-12 || aw <= 1e-12)
         return;
      const float depth = 4.0f * mat.normalStrength / (float)map.w / std::max(su, 1e-6f) * (float)std::sqrt(aw / au);
      int n = (int)std::ceil(maxTx);
      n = std::min(n, (int)std::sqrt((double)cap / (double)nt));
      if (n < 1)
         return;
      auto texel = [&](int x, int y) {
         auto fix = [&](int c, int sz) {
            if (wrap == 1) return ((c % sz) + sz) % sz;
            if (wrap == 2) { const int p = ((c % (2 * sz)) + 2 * sz) % (2 * sz); return p < sz ? p : 2 * sz - 1 - p; }
            return std::clamp(c, 0, sz - 1);
         };
         const uint8_t* q = &map.px[((size_t)fix(y, map.h) * map.w + fix(x, map.w)) * 4];
         return (0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2]) / 255.0f;
      };
      auto height = [&](float u, float v) { // bilinear at texel centres, as the GPU reads level 0
         const float x = (u * su + mt.translate[0]) * map.w - 0.5f, y = (v * sv + mt.translate[1]) * map.h - 0.5f;
         const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
         const float fx = x - x0, fy = y - y0;
         return (texel(x0, y0) * (1 - fx) + texel(x0 + 1, y0) * fx) * (1 - fy) + (texel(x0, y0 + 1) * (1 - fx) + texel(x0 + 1, y0 + 1) * fx) * fy;
      };
      const size_t per = (size_t)(n + 1) * (n + 2) / 2;
      d.tracePosNormal.reserve(nt * per * 6); d.traceUv.reserve(nt * per * 2); d.traceIndices.reserve(nt * (size_t)n * n * 3);
      const bool wear = d.wear.size() == nv * 3;
      if (wear) d.traceWear.reserve(nt * per * 3);
      for (size_t t = 0; t < nt; t++)
      {
         const uint32_t vi[3] = { d.indices[t * 3], d.indices[t * 3 + 1], d.indices[t * 3 + 2] };
         const uint32_t base = (uint32_t)(d.tracePosNormal.size() / 6);
         int ks[3] = { 0, 1, 2 };
         std::sort(ks, ks + 3, [&](int x, int y) { return vi[x] < vi[y]; });
         for (int a = 0; a <= n; a++)
            for (int b = 0; b <= n - a; b++)
            {
               const int w[3] = { n - a - b, a, b };
               // On an edge the two ends are taken in vertex-index order, so both triangles get the same bits.
               float att[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
               for (int kk = 0; kk < 3; kk++)
               {
                  const int k = ks[kk];
                  if (w[k] == 0) continue;
                  const float f = (float)w[k] / (float)n;
                  for (int c = 0; c < 6; c++) att[c] += d.posNormal[vi[k] * 6 + c] * f;
                  for (int c = 0; c < 2; c++) att[6 + c] += d.uv[vi[k] * 2 + c] * f;
                  if (wear) for (int c = 0; c < 3; c++) att[8 + c] += d.wear[vi[k] * 3 + c] * f;
               }
               float nl = std::sqrt(att[3] * att[3] + att[4] * att[4] + att[5] * att[5]);
               if (nl > 1e-12f) { att[3] /= nl; att[4] /= nl; att[5] /= nl; }
               const float off = (height(att[6], att[7]) - 1.0f) * depth;
               for (int c = 0; c < 3; c++) att[c] += att[3 + c] * off;
               d.tracePosNormal.insert(d.tracePosNormal.end(), att, att + 6);
               d.traceUv.insert(d.traceUv.end(), att + 6, att + 8);
               if (wear) d.traceWear.insert(d.traceWear.end(), att + 8, att + 11);
            }
         auto id = [&](int a, int b) { return base + (uint32_t)(a * (n + 1) - a * (a - 1) / 2 + b); }; // row a holds n + 1 - a
         for (int a = 0; a < n; a++)
            for (int b = 0; b < n - a; b++)
            {
               d.traceIndices.insert(d.traceIndices.end(), { id(a, b), id(a + 1, b), id(a, b + 1) });
               if (b + 1 < n - a)
                  d.traceIndices.insert(d.traceIndices.end(), { id(a + 1, b), id(a + 1, b + 1), id(a, b + 1) });
            }
      }
      // Normals of the relief: area-weighted faces, welded by position and the base normal (hard edges stay hard).
      const size_t tv = d.tracePosNormal.size() / 6;
      std::vector<float> acc(tv * 3, 0.0f);
      for (size_t t = 0; t + 2 < d.traceIndices.size(); t += 3)
      {
         const float* p0 = &d.tracePosNormal[d.traceIndices[t] * 6]; const float* p1 = &d.tracePosNormal[d.traceIndices[t + 1] * 6]; const float* p2 = &d.tracePosNormal[d.traceIndices[t + 2] * 6];
         const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
         const float c[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
         for (int k = 0; k < 3; k++)
            for (int j = 0; j < 3; j++) acc[d.traceIndices[t + k] * 3 + j] += c[j];
      }
      const float q = 1.0f / std::max(depth * 1e-3f, 1e-7f);
      std::unordered_map<unsigned long long, size_t> weld;
      weld.reserve(tv);
      std::vector<size_t> rep(tv);
      for (size_t v = 0; v < tv; v++)
      {
         const float* a = &d.tracePosNormal[v * 6];
         auto mix = [](unsigned long long x) { // splitmix64: small integer keys fold together under xor-multiply
            x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull; return x ^ (x >> 31);
         };
         unsigned long long h = 0;
         for (int c = 0; c < 3; c++) h = mix(h ^ mix((unsigned long long)(long long)std::llround(a[c] * q)));
         for (int c = 3; c < 6; c++) h = mix(h ^ mix((unsigned long long)(long long)std::llround(a[c] * 64.0f)));
         auto it = weld.emplace(h, v).first;
         rep[v] = it->second;
         if (it->second != v)
            for (int j = 0; j < 3; j++) acc[it->second * 3 + j] += acc[v * 3 + j];
      }
      for (size_t v = 0; v < tv; v++)
      {
         const float* c = &acc[rep[v] * 3];
         const float l = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
         if (l > 1e-20f)
            for (int j = 0; j < 3; j++) d.tracePosNormal[v * 6 + 3 + j] = c[j] / l;
      }
   }
}

// General 4x4 inverse (cofactors); false when singular.
static bool Invert4(const float* m, float* o)
{
   float inv[16];
   inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
   inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
   inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
   inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
   inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
   inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
   inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
   inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
   inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
   inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
   inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
   inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
   inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
   inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
   inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
   inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
   const float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
   if (std::fabs(det) < 1e-12f) return false;
   for (int i = 0; i < 16; i++) o[i] = inv[i] / det;
   return true;
}

bool Render3DNode::LiveEngineAvailable() { return true; }
namespace
{
// Row 8.6: EnvironmentNode's sun extraction (threshold 10) on the live engine's own copy of the map, once per map.
bool LiveSun(Render3DNode::EngineState& E, float dir[3], float light[3])
{
   if (E.envLiveSerial != E.envSerial)
   {
      E.envLiveSerial = E.envSerial;
      E.envLive = E.envPixels;
      E.envLiveSun = false;
      const int w = E.envW, h = E.envH;
      const float kT = 10.0f;
      const double kPi = 3.14159265358979;
      double L[3] = { 0, 0, 0 }, D[3] = { 0, 0, 0 };
      for (int r = 0; r < h; r++)
      {
         const double theta = kPi * (r + 0.5) / h, omega = (2.0 * kPi / w) * (kPi / h) * std::sin(theta);
         for (int x = 0; x < w; x++)
         {
            float* p = &E.envLive[((size_t)r * w + x) * 3];
            const float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            if (lum <= kT)
               continue;
            const float keep = kT / lum;
            const double phi = 2.0 * kPi * ((x + 0.5) / w - 0.5), wt = (lum - kT) * omega;
            for (int k = 0; k < 3; k++) { L[k] += p[k] * (1.0f - keep) * omega; p[k] *= keep; }
            D[0] += std::sin(theta) * std::cos(phi) * wt; D[1] += std::cos(theta) * wt; D[2] += std::sin(theta) * std::sin(phi) * wt;
         }
      }
      const double len = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
      if (len > 0.0 && L[0] + L[1] + L[2] > 0.0)
      {
         E.envLiveSun = true;
         for (int k = 0; k < 3; k++) { E.envLiveDir[k] = (float)(D[k] / len); E.envLiveLight[k] = (float)L[k]; }
      }
   }
   if (!E.envLiveSun)
      return false;
   for (int k = 0; k < 3; k++) { dir[k] = E.envLiveDir[k]; light[k] = E.envLiveLight[k]; }
   return true;
}
}

bool Render3DNode::CookEngine(int frameId)
{
   if (mEng == nullptr)
      mEng = new EngineState();
   EngineState& E = *mEng;
   // Whatever path this cook takes, the node shows the first shader or pipeline that failed (DESIGN.md L2).
   struct ErrorOut { std::string& s; ~ErrorOut() { s = rhi::LastError(); } } errorOut{ mEngineError };
   if (E.failed)
      return false;
   if (!E.dev)
   {
      E.dev = rhi::CreateDevice();
      E.raster = E.dev ? E.dev->CreateRasterRenderer() : nullptr;
      if (!E.raster)
      {
         E.failed = true;
         return false;
      }
   }
   const int w = GLUtil::TargetSide(width, 16);
   const int h = GLUtil::TargetSide(height, 16);
   if (!EnsureResources(w, h, 1))
      return false;
   // Row 8.4: interactive live frames render at 67 % and MetalFX's temporal scaler brings them to w x h (boat: 83 -> 53
   // ms a frame; 8 cases within 0.005 FLIP of native). Export stays native. INFINITE_ENGINE_UPSCALE=<percent> sets
   // both (0 = off).
   static const int sUpEnv = [] { const char* e = std::getenv("INFINITE_ENGINE_UPSCALE"); return e ? std::atoi(e) : -1; }();
   const int sUp = sUpEnv >= 0 ? sUpEnv : (Transport::Instance().IsOfflineMode() ? 0 : 67);
   const bool up = sUp > 0 && sUp < 100;
   const int lw = up ? std::max(16, w * sUp / 100) : w, lh = up ? std::max(16, h * sUp / 100) : h;
   if (up && (!E.imgLo || E.imgLo->Width() != lw || E.imgLo->Height() != lh))
   {
      E.imgLo = E.dev->CreateSharedImage(lw, lh, rhi::Format::RGBA8);
      E.upReset = true;
   }
   if (!E.img || E.img->Width() != w || E.img->Height() != h)
   {
      E.img = E.dev->CreateSharedImage(w, h, rhi::Format::RGBA8);
      if (!E.img || E.img->GlTexture() == 0)
      {
         E.failed = true;
         E.img.reset();
         return false;
      }
   }

   const auto tpA = std::chrono::steady_clock::now();
   CollectDraw();
   for (IGeometrySource* src : mDraw)
      if (auto* node = dynamic_cast<INode*>(src))
         node->CookIfNeeded(frameId);

   // Meshes: a changed source re-uploads only its own mesh (and BLAS); a changed set of sources, all of them.
   std::vector<std::pair<const void*, unsigned long long>> sig;
   int displaced = 0;
   for (IGeometrySource* src : mDraw)
   {
      unsigned long long rev = src->MeshRevision();
      if (WantsDisplace(src))
      {
         // The relief follows the map, its strength and its mapping, so they are part of the mesh's key.
         const Material mat = src->GetMaterial();
         const MappingTransform mt = src->GetMappingTransform();
         unsigned long long k = (unsigned long long)src->GetMaterialTexture(kMapNormal) * 0x9E3779B97F4A7C15ull ^ src->SurfaceTextureRevision();
         const float f[5] = { mat.normalStrength, mt.scale[0], mt.scale[1], mt.translate[0], mt.translate[1] };
         for (float x : f) { unsigned u; std::memcpy(&u, &x, 4); k = (k ^ u) * 1099511628211ull; }
         rev ^= k | 1ull;
         displaced++;
      }
      if (WantsWear(src))
         rev = (rev ^ 0x5745415257454152ull) * 1099511628211ull; // the bake joins the mesh's key
      sig.emplace_back((const void*)src, rev);
   }
   const size_t triCap = TriangleCap() / (size_t)std::max(displaced, 1);
   auto meshFor = [&](IGeometrySource* src) {
      engine::MeshData d = ToMeshData(src->GetMesh());
      if (const Field::MaterialProgramEntry* wp = WearProgram(src))
         d.wear = MeshWear::Stream(d.posNormal.data(), 6, d.posNormal.size() / 6, d.indices.data(), d.indices.size(), wp->usesWear,
                                   src->GetMesh().vertexMask);
      if (WantsDisplace(src))
         if (auto* m = CacheMap(E, src->GetMaterialTexture(kMapNormal), src->SurfaceTextureRevision()))
         {
            int wrap = 0;
            if (auto* mn = dynamic_cast<MaterialNode*>(src)) wrap = mn->wrapMode;
            Displace(d, *m, src->GetMaterial(), src->GetMappingTransform(), wrap, triCap);
         }
      return d;
   };
   static const bool sProf = std::getenv("INFINITE_ENGINE_PROF") != nullptr;
   const auto tp0 = std::chrono::steady_clock::now();
   int changedMeshes = 0;
   size_t changedTris = 0;
   const bool meshesChanged = sig != E.meshSig;
   if (meshesChanged)
   {
      for (size_t i = 0; i < sig.size() && i < E.meshSig.size(); i++)
         if (sProf && sig[i] != E.meshSig[i]) { changedMeshes++; changedTris += mDraw[i]->GetMesh().indices.size() / 3; }
      bool partial = sig.size() == E.meshSig.size() && !sig.empty();
      for (size_t i = 0; partial && i < sig.size(); i++)
         partial = sig[i].first == E.meshSig[i].first;
      for (size_t i = 0; partial && i < sig.size(); i++)
         if (sig[i].second != E.meshSig[i].second)
            partial = E.raster->UpdateMesh((int)i, meshFor(mDraw[i]));
      if (!partial)
      {
         std::vector<engine::MeshData> meshes;
         for (IGeometrySource* src : mDraw)
            meshes.push_back(meshFor(src));
         if (!E.raster->UploadMeshes(meshes))
            return false;
      }
      E.meshSig = sig;
   }

   const auto tp1 = std::chrono::steady_clock::now();
   engine::Frame fr;
   // Motion blur: the last earlier moment cooked gives each source's motion per frame (one step of the transport, as
   // export and playback step it; several cooks at one moment share it); after a jump back or a long gap there is none.
   const bool motion = MotionActive();
   if (motion)
   {
      const double now = Transport::Instance().Seconds();
      if (now != E.motionAt)
      {
         const bool step = E.motionAt >= 0.0 && now > E.motionAt && now - E.motionAt < 0.25 && !E.curModel.empty();
         E.prevModel.swap(E.curModel);
         if (!step) E.prevModel.clear();
         E.curModel.clear();
         E.motionAt = now;
      }
   }
   const bool hasPrev = motion && !E.prevModel.empty();
   const float shutS = std::clamp(motionShutter, 0.0f, 2.0f);
   const float shutOpen = motionPosition == 0 ? 0.0f : motionPosition == 2 ? -shutS : -0.5f * shutS;
   auto place = [&](engine::Instance& in, const Mat4& m)
   {
      std::memcpy(in.model, m.m, 64);
      float n3[9];
      m.NormalMatrix(n3);
      for (int c = 0; c < 3; c++)
         for (int r = 0; r < 3; r++)
            in.normalMatrix[c * 4 + r] = n3[c * 3 + r];
   };
   for (size_t i = 0; i < mDraw.size(); i++)
   {
      IGeometrySource* src = mDraw[i];
      engine::Instance in;
      in.mesh = (int)i;
      in.castShadow = src->CastsShadow();
      const Material mat = src->GetMaterial();
      for (int k = 0; k < 3; k++)
         in.baseSrgb[k] = mat.color[k];
      in.metallic = mat.metallic;
      in.roughness = mat.roughness;
      in.transmission = mat.transmission;
      in.ior = mat.ior;
      for (int k = 0; k < 3; k++)
         in.emission[k] = ColorSpace::SrgbToLinear(mat.emissionColor[k]) * mat.emission; // GL: toLinear(colour) * strength
      in.normalStrength = mat.normalStrength;
      in.normalBump = mat.normalBump;
      in.sheen = mat.sheen;
      std::memcpy(in.sheenColor, mat.sheenColor, 12);
      in.sheenRoughness = mat.sheenRoughness;
      in.sheenDir = mat.sheenDirection;
      if (auto* mn = dynamic_cast<MaterialNode*>(src))
         in.wrap = mn->wrapMode;
      {
         const int kinds[10] = { kMapAlbedo, kMapRoughness, kMapMetallic, kMapNormal, kMapAmbientOcclusion,
                                 kMapEmission, kMapSheen, kMapOpacity, kMapClearcoat, kMapTransmission };
         int* slots[10] = { &in.mapAlbedo, &in.mapRoughness, &in.mapMetallic, &in.mapNormal, &in.mapAo,
                            &in.mapEmission, &in.mapSheen, &in.mapOpacity, &in.mapClearcoat, &in.mapTransmission };
         for (int k = 0; k < 10; k++)
            *slots[k] = MapIndex(E, fr, src->GetMaterialTexture(kinds[k]), src->SurfaceTextureRevision());
      }
      in.clearcoat = mat.clearcoat;
      in.clearcoatRoughness = mat.clearcoatRoughness;
      in.specular = mat.specular;
      in.opacity = mat.opacity;
      in.alphaCutoff = mat.alphaCutoff;
      in.fieldProgram = mat.fieldProgram;
      for (int k = 0; k < 4; k++)
         in.fieldImage[k] = MapIndex(E, fr, mat.fieldImage[k], mat.fieldImageRev[k]);
      in.fieldSeeThrough = mat.fieldProgram != 0 && Field::MaterialProgramSeeThrough(mat.fieldProgram);
      std::memcpy(in.fieldParams, mat.fieldParams, sizeof(in.fieldParams));
      in.detailScale = mat.detailScale;
      in.detailStrength = mat.detailStrength;
      in.subsurface = std::clamp(mat.subsurface, 0.0f, 1.0f);
      for (int k = 0; k < 3; k++)
         in.sssRadius[k] = std::max(mat.subsurfaceColor[k], 0.0f) * 0.2f * std::max(mat.subsurfaceRadius, 0.0f);
      in.translucency = std::clamp(mat.translucency, 0.0f, 1.0f);
      in.iridescence = mat.iridescence; in.iridescenceIor = mat.iridescenceIor; in.iridescenceThickness = mat.iridescenceThickness;
      { const Mesh& sm = src->GetMesh(); in.strandFrom = sm.twoSided ? (int)(sm.ribbonStart / 3) : -1; }
      if (mat.hair)
      {
         float lc[3], lt[3];
         for (int k = 0; k < 3; k++)
         {
            lc[k] = ColorSpace::SrgbToLinear(mat.color[k]);
            lt[k] = ColorSpace::SrgbToLinear(mat.hairTint[k]);
         }
         in.hairRadial = std::clamp(mat.radialRoughness, 0.02f, 1.0f);
         r3dhair::Sigma(mat.hair, mat.melanin, mat.melaninRedness, lc, lt, in.hairRadial, in.hairSigma);
         in.hairShift = mat.hairShift * 0.017453293f;
         const Mesh& hm = src->GetMesh();
         in.hairFrom = hm.twoSided ? (int)(hm.ribbonStart / 3) : 0;
      }
      {
         const MappingTransform mt = src->GetMappingTransform();
         in.mapSpace = mt.space;
         std::memcpy(in.mapScale, mt.scale, 12);
         std::memcpy(in.mapRotate, mt.rotate, 12);
         std::memcpy(in.mapTranslate, mt.translate, 12);
         in.triplanar = mt.triplanarBlend;
         in.breakup = mt.breakup;
         if (mt.space == kMapSpaceGenerated) // generated coordinates span the mesh's own box
         {
            const Mesh& mesh = src->GetMesh();
            if (!mesh.vertices.empty())
            {
               for (int a = 0; a < 3; a++) { in.objLo[a] = 1e30f; in.objHi[a] = -1e30f; }
               for (const Vertex& v : mesh.vertices)
               {
                  const float q[3] = { v.px, v.py, v.pz };
                  for (int a = 0; a < 3; a++) { in.objLo[a] = std::min(in.objLo[a], q[a]); in.objHi[a] = std::max(in.objHi[a], q[a]); }
               }
            }
         }
      }
      // Instanced sources: one engine instance per copy, placed as the GL draw places it (group matrix * copy).
      if (InstanceOnPointsNode* instancer = FindInstancer(src))
      {
         const std::vector<Mat4>& xforms = ResolveInstanceTransforms(src, instancer);
         const std::vector<unsigned char>* sel = ResolveInstanceSelection(src);
         const bool useSel = sel != nullptr && sel->size() == xforms.size();
         const Mat4 group = src->GetInstanceGroupMatrix();
         in.copyGroup = (int)i; // row 7.2: the backend merges a large run of copies for the rays
         fr.instances.reserve(fr.instances.size() + xforms.size());
         for (size_t k = 0; k < xforms.size(); k++)
         {
            if (useSel && (*sel)[k] == 0)
               continue;
            place(in, Mat4::Multiply(group, xforms[k]));
            in.objRand = engine::CopyRandom((uint32_t)k);
            fr.instances.push_back(in);
         }
         continue;
      }
      const Mat4 model = src->GetModelMatrix();
      place(in, model);
      if (motion)
      {
         // A Transform op bakes its move into the mesh, so the motion is the model matrix times every whole-mesh
         // Transform up the chain (E); the mesh drawn already carries this frame's Transforms (B), so the shutter
         // places it at E(time) * B^-1.
         Mat4 bake = Mat4::Identity();
         for (IGeometrySource* g = src; g; g = g->PassthroughSource())
            if (auto* op = dynamic_cast<GeometryOpNode*>(g))
               if (op->op == GeometryOpNode::kTransform && !op->bypassed && !op->selectionOnly && !FindInstancer(op->input))
                  bake = Mat4::Multiply(bake, op->TransformMatrix());
         const Mat4 eff = Mat4::Multiply(model, bake);
         E.curModel[src] = eff;
         auto pv = E.prevModel.find(src);
         float inv[16];
         if (hasPrev && pv != E.prevModel.end() && std::memcmp(pv->second.m, eff.m, 64) != 0 && Invert4(bake.m, inv))
         {
            // linear in the matrix, per frame: open/close = E + (E - prev) * time (frames)
            in.moving = true;
            Mat4 a, b, binv;
            std::memcpy(binv.m, inv, 64);
            for (int k = 0; k < 16; k++)
            {
               const float d = eff.m[k] - pv->second.m[k];
               a.m[k] = eff.m[k] + d * shutOpen;
               b.m[k] = eff.m[k] + d * (shutOpen + shutS);
            }
            std::memcpy(in.shut0, Mat4::Multiply(a, binv).m, 64);
            std::memcpy(in.shut1, Mat4::Multiply(b, binv).m, 64);
         }
      }
      fr.instances.push_back(in);
   }

   // Material programs (row 1.3): one library for the set of ids drawn; the backend relinks when the key changes.
   bool fieldUsesTime = false;
   {
      std::vector<int> ids;
      for (const engine::Instance& in : fr.instances)
         if (in.fieldProgram != 0)
         {
            ids.push_back(in.fieldProgram);
            if (const Field::MaterialProgramEntry* e = Field::FindMaterialProgram(in.fieldProgram))
               fieldUsesTime = fieldUsesTime || e->usesTime;
         }
      fr.fieldMsl = Field::BuildMaterialLibrary(ids, Field::MaterialLang::Msl, fr.fieldMslKey);
      fr.fieldTime = (float)Transport::Instance().Seconds();
   }

   // Camera, same precedence as the GL cook.
   const float aspect = (float)w / (float)h;
   float eye[3];
   Mat4 view, proj;
   if (camera != nullptr)
   {
      camera->ComputeEye(eye);
      view = camera->ViewMatrix();
      proj = camera->ProjectionMatrix(aspect);
   }
   else
   {
      const float az = camAzimuth * (float)M_PI / 180.0f, el = camElevation * (float)M_PI / 180.0f;
      const float ce = std::cos(el);
      eye[0] = targetX + camDistance * ce * std::cos(az);
      eye[1] = targetY + camDistance * std::sin(el);
      eye[2] = targetZ + camDistance * ce * std::sin(az);
      const float target[3] = { targetX, targetY, targetZ }, up[3] = { 0, 1, 0 };
      view = Mat4::LookAt(eye, target, up);
      proj = projection == 1 ? Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane)
                             : Mat4::Perspective(fov * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
   }
   if (rig != nullptr && rig->hasView)
   {
      view = rig->view;
      eye[0] = rig->eye[0]; eye[1] = rig->eye[1]; eye[2] = rig->eye[2];
      if (rig->hasProj)
         proj = rig->proj;
      else if (rig->lens != nullptr)
         proj = rig->lens->ProjectionMatrix(aspect);
   }
   mLastCamera.view = view; mLastCamera.proj = proj;
   mLastCamera.eye[0] = eye[0]; mLastCamera.eye[1] = eye[1]; mLastCamera.eye[2] = eye[2];
   mLastCamera.valid = true;
   if (mJitterIndex >= 0)
   {
      // Supersample pass: the same Halton(2,3) sub-pixel offset as the raster's (CookOnce).
      auto halton = [](int i, int base) { float f = 1.0f, r = 0.0f; while (i > 0) { f /= (float)base; r += f * (float)(i % base); i /= base; } return r; };
      const float jx = (halton(mJitterIndex + 1, 2) - 0.5f) * 2.0f / (float)w;
      const float jy = (halton(mJitterIndex + 1, 3) - 0.5f) * 2.0f / (float)h;
      if (proj.m[11] != 0.0f) { proj.m[8] += jx; proj.m[9] += jy; }
      else { proj.m[12] += jx; proj.m[13] += jy; }
   }
   const Mat4 vp = Mat4::Multiply(proj, view);
   std::memcpy(fr.viewProj, vp.m, 64);
   std::memcpy(fr.eye, eye, 12);
   {
      // Depth of field as Render 3D's path tracer lens (Render3DPathTraceCook): a circle dofAperture x 24 px across
      // (diameter) in a 1080 frame at |d - focus| / d, so the aperture's diameter is that share of the focus plane's
      // height and its radius half of it.
      const float r3[3] = { view.m[0], view.m[4], view.m[8] }, u3[3] = { view.m[1], view.m[5], view.m[9] },
                  f3[3] = { -view.m[2], -view.m[6], -view.m[10] };
      std::memcpy(fr.camRight, r3, 12); std::memcpy(fr.camUp, u3, 12); std::memcpy(fr.camFwd, f3, 12);
      const bool ortho = proj.m[11] == 0.0f;
      const float invFy = 1.0f / std::max(1e-6f, std::fabs(proj.m[5]));
      fr.lensRadius = ortho || dofAperture <= 0.0f ? 0.0f : dofAperture * (24.0f / 1080.0f) * focusDistance * invFy;
      fr.focusDist = std::max(1e-3f, focusDistance);
      // A new ray starts this far off the surface. Scaled to the focus distance (under a pixel's footprint there), so
      // a glass wall or a water drop a few millimetres thick is not stepped over; never more than the old fixed 3 mm.
      fr.rayBias = std::clamp(fr.focusDist * 1e-4f, 1e-5f, 0.003f);
      fr.dofAperture = ortho ? 0.0f : std::max(0.0f, dofAperture);
      fr.fogDensity = std::max(0.0f, fogDensity); fr.fogAnisotropy = std::clamp(fogAnisotropy, -0.95f, 0.95f);
      for (int k = 0; k < 3; k++) { fr.fogColor[k] = ColorSpace::SrgbToLinear(std::clamp(fogColor[k], 0.0f, 1.0f)); fr.fogMin[k] = fogMin[k]; fr.fogMax[k] = fogMax[k]; }
      fr.bokeh[0] = dofBlades >= 3 ? (float)std::min(dofBlades, 16) : 0.0f;
      fr.bokeh[1] = dofRotation * 3.14159265f / 180.0f;
      fr.bokeh[2] = std::clamp(dofRatio, 0.01f, 2.0f);
      fr.lensCatEye = fr.lensRadius > 0.0f ? std::clamp(dofCatEye, 0.0f, 1.0f) : 0.0f;
      fr.lensDistortion = ortho ? 0.0f : std::clamp(lensDistortion, -0.5f, 0.5f);
      fr.lensCA = ortho ? 0.0f : std::clamp(lensCA, 0.0f, 0.05f);
      fr.bloom = std::clamp(bloom, 0.0f, 1.0f); fr.bloomThreshold = bloomThreshold; fr.bloomRadius = bloomRadius;
   }

   // Lights: rig, then light nodes, then the built-in one.
   auto addLight = [&](const RigLight& l)
   {
      if ((int)fr.lights.size() >= engine::kMaxLights)
         return;
      engine::Light e;
      e.type = (engine::LightType)std::max(0, std::min(l.type, 5));
      std::memcpy(e.vec, l.vec, 12); std::memcpy(e.aim, l.aim, 12); std::memcpy(e.colorSrgb, l.color, 12);
      e.intensity = l.intensity; e.falloff = l.falloff; e.spotAngleDeg = l.spotAngle; e.spotPenumbra = l.spotPenumbra;
      e.diffuse = l.diffuse; e.specular = l.specular; e.cutoff = l.cutoff;
      e.castShadow = l.castShadow;
      e.areaW = std::max(0.001f, l.areaW); e.areaH = std::max(0.001f, l.areaH);
      // A sun's disc from the light's shadow softness, as GL sizes its shadow (0.526 degrees, the real sun, at 1).
      if (e.type == engine::LightType::Directional || e.type == engine::LightType::Sun) e.sunAngleDeg = 0.526f * std::max(0.0f, l.shadowSoft);
      fr.lights.push_back(e);
   };
   if (rig != nullptr && rig->hasLights)
   {
      for (const RigLight& l : rig->lights)
         addLight(l);
   }
   else
   {
      for (int i = 0; i < kLightSlots; i++)
      {
         if (lights[i] == nullptr)
            continue;
         RigLight l;
         l.From(*lights[i]);
         float vec[3];
         lights[i]->ComputeVector(vec);
         const float len = std::max(1e-4f, std::sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]));
         for (int k = 0; k < 3; k++) { l.vec[k] = vec[k]; l.aim[k] = -vec[k] / len; }
         addLight(l);
      }
   }
   if (fr.lights.empty())
   {
      const float az = lightAzimuth * (float)M_PI / 180.0f, el = lightElevation * (float)M_PI / 180.0f;
      RigLight l;
      l.type = 0;
      l.vec[0] = std::cos(el) * std::cos(az); l.vec[1] = std::sin(el); l.vec[2] = std::cos(el) * std::sin(az);
      for (int k = 0; k < 3; k++) l.color[k] = lightColor[k];
      l.intensity = lightIntensity;
      addLight(l);
   }

   float ambient[3] = { ambientColor[0], ambientColor[1], ambientColor[2] };
   if (rig != nullptr)
      for (int k = 0; k < 3; k++) ambient[k] += rig->ambient[k];
   std::memcpy(fr.ambientSrgb, ambient, 12);
   std::memcpy(fr.envSkySrgb, envSky, 12);
   std::memcpy(fr.envHorizonSrgb, envHorizon, 12);
   std::memcpy(fr.envGroundSrgb, envGround, 12);
   fr.envIntensity = envIntensity;
   std::memcpy(fr.clearSrgb, bgColor, 12);

   // The Environment node's HDRI, read back once per change from the GL texture Render 3D samples. A mip at most
   // 2048 wide is enough for lighting and a blurred background; envBaseLod keeps roughness on GL's mip scale.
   if (auto* env = dynamic_cast<EnvironmentNode*>(envInput.IsConnected() ? envInput.Resolved() : nullptr))
   {
      env->CookIfNeeded(frameId);
      const unsigned int tex = env->GetEnvironmentTexture();
      if (tex != 0 && (tex != E.envTex || env->TextureRevision() != E.envRev))
      {
         glBindTexture(GL_TEXTURE_2D, tex);
         int tw = 0, th = 0, lod = 0;
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
         while ((tw >> lod) > 2048 && lod < (int)env->MaxLod())
            lod++;
         glGetTexLevelParameteriv(GL_TEXTURE_2D, lod, GL_TEXTURE_WIDTH, &tw);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, lod, GL_TEXTURE_HEIGHT, &th);
         if (tw > 0 && th > 0)
         {
            E.envPixels.assign((size_t)tw * th * 3, 0.0f);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGetTexImage(GL_TEXTURE_2D, lod, GL_RGB, GL_FLOAT, E.envPixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            E.envW = tw; E.envH = th; E.envLod = lod;
            E.envSerial++;
         }
         glBindTexture(GL_TEXTURE_2D, 0);
         E.envTex = tex;
         E.envRev = env->TextureRevision();
      }
      if (tex != 0 && !E.envPixels.empty())
      {
         // Rows as GL holds them: GL's v and Metal's v both start at row 0, so the same lookup reads the same texel.
         fr.envPixels = E.envPixels.data();
         fr.envW = E.envW; fr.envH = E.envH;
         fr.envRev = E.envSerial;
         fr.envRotationRad = env->rotation * (float)M_PI / 180.0f;
         fr.envMaxLod = env->MaxLod();
         fr.envBaseLod = (float)E.envLod;
         fr.envIntensity = envIntensity * env->intensity;
         fr.envBackground = envAsBackground;
         float sunDir[3], sunLight[3];
         if ((int)fr.lights.size() < engine::kMaxLights && env->Sun(sunDir, sunLight))
         {
            // The extracted sun, as GL adds it: rotated with the map, colour normalised, power = peak * intensity.
            const float c = std::cos(fr.envRotationRad), sn = std::sin(fr.envRotationRad);
            engine::Light l;
            l.type = engine::LightType::Directional;
            l.vec[0] = sunDir[0] * c + sunDir[2] * sn; l.vec[1] = sunDir[1]; l.vec[2] = -sunDir[0] * sn + sunDir[2] * c;
            const float peak = std::max({ sunLight[0], sunLight[1], sunLight[2], 1e-6f });
            for (int k = 0; k < 3; k++)
               l.colorSrgb[k] = ColorSpace::LinearToSrgb(sunLight[k] / peak);
            l.intensity = peak * env->intensity;
            l.sunAngleDeg = std::max(0.0f, env->sunAngle);
            fr.lights.push_back(l);
         }
         else if ((liveEngineFeatures & 8) == 0 && (int)fr.lights.size() < engine::kMaxLights && !E.envPixels.empty() && LiveSun(E, sunDir, sunLight))
         {
            // Row 8.6: the sun left in the map (threshold 0) is a patch the live engine's prefiltered lighting blurs
            // away and its shadows never see; pure live takes it out of its own copy of the map, as a threshold of
            // 10 would, and adds it as the sun. Settle keeps the whole map and samples it.
            fr.envPixels = E.envLive.data();
            fr.envRev = E.envSerial | (1ull << 62);
            const float c = std::cos(fr.envRotationRad), sn = std::sin(fr.envRotationRad);
            engine::Light l;
            l.type = engine::LightType::Directional;
            l.vec[0] = sunDir[0] * c + sunDir[2] * sn; l.vec[1] = sunDir[1]; l.vec[2] = -sunDir[0] * sn + sunDir[2] * c;
            const float peak = std::max({ sunLight[0], sunLight[1], sunLight[2], 1e-6f });
            for (int k = 0; k < 3; k++)
               l.colorSrgb[k] = ColorSpace::LinearToSrgb(sunLight[k] / peak);
            l.intensity = peak * env->intensity;
            l.sunAngleDeg = std::max(0.0f, env->sunAngle);
            fr.lights.push_back(l);
         }
      }
   }
   // Maps no instance used this frame are let go (their pixels can be large).
   E.maps.erase(std::remove_if(E.maps.begin(), E.maps.end(), [](const auto& m) { return !m.used; }), E.maps.end());
   for (auto& m : E.maps)
      m.used = false;
   fr.exposure = exposure;
   fr.tonemap = tonemap;                // 0 none, 1 ACES, 2 Reinhard, 3 AgX, 4 Neutral (MetalDevice toneMap)
   fr.rim = rimIntensity;
   fr.shadow.enabled = shadowsEnabled;
   fr.shadow.bias = shadowBias; fr.shadow.strength = shadowStrength; fr.shadow.softness = shadowSoftness;
   fr.rtShadows = shadowsEnabled && (liveEngineFeatures & 1) != 0;
   fr.rtReflections = (liveEngineFeatures & 2) != 0;
   fr.gi = (liveEngineFeatures & 4) != 0;
   fr.frameIndex = ++E.frame;

   // Settle: once nothing has changed since the last frame, refine with the path tracer up to ptSamples.
   unsigned long long hsh = 1469598103934665603ull;
   auto mix = [&](const void* d, size_t n)
   {
      const unsigned char* b = (const unsigned char*)d;
      for (size_t i = 0; i < n; i++) { hsh ^= b[i]; hsh *= 1099511628211ull; }
   };
   mix(fr.viewProj, 64); mix(&fr.lensRadius, 8); mix(fr.bokeh, 12); mix(&fr.lensCatEye, 12); mix(&fr.bloom, 12); mix(&fr.exposure, 4); mix(fr.ambientSrgb, 12); mix(fr.clearSrgb, 12); mix(&fr.envIntensity, 4);
   mix(&fr.envRev, 8); mix(&fr.envRotationRad, 4); mix(&fr.envBackground, 1);
   mix(&fr.fogDensity, 8); mix(fr.fogColor, 36);
   for (size_t ii = 0; ii < fr.instances.size(); ii++)
   {
      const engine::Instance& in = fr.instances[ii];
      // row 7.2: a scatter's later copies differ from its first only in placement
      if (ii > 0 && in.copyGroup >= 0 && fr.instances[ii - 1].copyGroup == in.copyGroup) { mix(in.model, 64); continue; }
      mix(in.model, 64); mix(in.baseSrgb, 12); mix(&in.metallic, 16);
      mix(&in.mapAlbedo, 16); mix(&in.fieldImage, 16); mix(&in.wrap, 4); mix(&in.normalStrength, 4); mix(&in.normalBump, 4);
      mix(&in.sheen, 4); mix(in.sheenColor, 12); mix(&in.sheenRoughness, 4); mix(&in.sheenDir, 4);
      mix(&in.mapAo, 24); mix(&in.clearcoat, 4 * 7); mix(&in.mapSpace, 4); mix(in.mapScale, 4 * 9); mix(&in.triplanar, 8); mix(in.objLo, 24);
      mix(in.hairSigma, 20); mix(&in.hairFrom, 4); mix(&in.strandFrom, 4); mix(&in.subsurface, 20); mix(&in.iridescence, 12);
      { const int cs = in.castShadow ? 1 : 0; mix(&cs, 4); }
      mix(&in.fieldProgram, 4); mix(in.fieldParams, 64);
   }
   if (fieldUsesTime) mix(&fr.fieldTime, 4); // a program that reads t never settles
   for (const engine::TextureData& t : fr.textures) mix(&t.key, 8);
   for (const engine::Light& l : fr.lights) { mix(l.vec, 12); mix(l.colorSrgb, 12); mix(&l.intensity, 4); mix(l.aim, 12); const int cs = l.castShadow ? 1 : 0; mix(&cs, 4); }
   const bool changed = hsh != E.sceneHash;
   E.sceneHash = hsh;
   if (changed || meshesChanged) E.upReset = true;
   // One live frame: native, or (row 8.4) at lw x lh with a Halton sub-pixel jitter, then upscaled into E.img.
   float vpBase[16];
   std::memcpy(vpBase, fr.viewProj, 64);
   auto liveRender = [&](double* g) -> bool
   {
      if (!up || !E.imgLo)
         return E.raster->Render(*E.img, fr, g);
      auto halton = [](unsigned i, unsigned base) { float f = 1.0f, r = 0.0f; while (i > 0) { f /= (float)base; r += f * (float)(i % base); i /= base; } return r; };
      const unsigned ji = (E.frame % 32u) + 1u;
      const float jx = halton(ji, 2) - 0.5f, jy = halton(ji, 3) - 0.5f; // pixels of the low-res picture
      const float dx = jx * 2.0f / (float)lw, dy = jy * 2.0f / (float)lh;
      for (int c = 0; c < 4; c++) { fr.viewProj[c * 4 + 0] = vpBase[c * 4 + 0] + dx * vpBase[c * 4 + 3]; fr.viewProj[c * 4 + 1] = vpBase[c * 4 + 1] + dy * vpBase[c * 4 + 3]; }
      fr.keepDepth = true;
      const bool ok = E.raster->Render(*E.imgLo, fr, g);
      std::memcpy(fr.viewProj, vpBase, 64);
      fr.keepDepth = false;
      if (ok && E.raster->Upscale(*E.imgLo, *E.img, jx, jy, E.upReset))
      {
         E.upReset = false;
         return true;
      }
      E.upReset = true;
      return E.raster->Render(*E.img, fr, g); // no scaler on this device: native
   };
   // SPEED_PLAN S0.1: one line per scene build (geometry cook, mesh upload and BVH, frame build), so warm timing can
   // keep the build apart from the samples.
   if (sProf && meshesChanged)
   {
      auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
      const auto tpS = std::chrono::steady_clock::now();
      std::fprintf(stderr, "ENGINE_PROF scene sources=%.2f meshes=%.2f frame=%.2f total=%.2f\n", ms(tpA, tp0), ms(tp0, tp1),
                   ms(tp1, tpS), ms(tpA, tpS));
   }
   bool traced = false;
   const bool offline = Transport::Instance().IsOfflineMode();
   if (!offline) E.exportWarm = false;
   // --frame cooks the graph a few times at one moment before it exports (probe and warm ticks). A repeat cook of an
   // unchanged scene at the same time keeps the converged picture instead of warming or tracing it again.
   const double offlineNow = offline ? Transport::Instance().Seconds() : -1.0;
   const bool offlineRepeat = offline && !changed && !meshesChanged && offlineNow == E.offlineAt;
   E.offlineAt = offlineNow;
   if (offline && (liveEngineFeatures & 8) != 0 && offlineRepeat && E.settled > 0)
      traced = true;
   else if (offline && (liveEngineFeatures & 8) != 0)
   {
      // Export / --frame: every frame is the settled picture, traced in full before it is handed on.
      const int target = std::clamp(ptSamples, 1, 4096), bounces = std::clamp(ptBounces, 1, 16);
      // Adaptive stop: a frame whose 95th-percentile relative error is already under the limit takes no more
      // samples (the denoiser's own variance says so). Default 0.05: the largest limit whose picture stays under
      // 0.01 FLIP of the full count on the 8 reference cases (0.06 and 0.08 do not). INFINITE_ENGINE_STOP overrides;
      // 0 traces them all.
      static const float sStop = [] { const char* e = std::getenv("INFINITE_ENGINE_STOP"); return e ? (float)std::atof(e) : 0.05f; }();
      traced = true;
      int done = 0;
      // Row 6.1, motion blur: the shutter is cut into time slices, two samples each, at radical-inverse times (any
      // prefix spans the shutter); each slice places the moving instances at its time, and the TLAS follows.
      bool anyMoving = false;
      for (const engine::Instance& in : fr.instances) anyMoving = anyMoving || in.moving;
      unsigned slice = 0;
      while (done < target && traced)
      {
         const int n = std::min(anyMoving ? 2 : 16, target - done);
         if (anyMoving)
         {
            unsigned b = ++slice; float tau = 0.0f, w = 0.5f;
            for (; b; b >>= 1, w *= 0.5f) if (b & 1) tau += w;
            for (engine::Instance& in : fr.instances)
               if (in.moving)
               {
                  Mat4 m;
                  for (int k = 0; k < 16; k++) m.m[k] = in.shut0[k] + (in.shut1[k] - in.shut0[k]) * tau;
                  place(in, m);
               }
         }
         const auto tpP = std::chrono::steady_clock::now();
         double passGpu = 0.0;
         traced = E.raster->PathTrace(*E.img, fr, n, bounces, done == 0, sProf ? &passGpu : nullptr);
         done += n;
         const float noise = E.raster->PathTraceNoise();
         if (sProf)
            std::fprintf(stderr, "ENGINE_PROF settle spp=%d noise95=%.4f n=%d ms=%.2f gpu=%.2f\n", done, noise, n,
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tpP).count(), passGpu);
         if (!anyMoving && sStop > 0.0f && noise >= 0.0f && noise < sStop)
            break;
      }
      E.settled = traced ? target : 0;
   }
   else if (offline)
   {
      // Live look on export: let the temporal GI / reflection history converge as it would on a held frame.
      // The first exported frame converges from nothing; later ones carry the history over (it follows motion, as
      // in playback), so a short top-up does. Later supersample passes reuse it too.
      int warm = (fr.gi || fr.rtReflections) && mJitterIndex <= 0 && !(offlineRepeat && E.exportWarm) ? (E.exportWarm ? 24 : 48) : 0;
      if (up && !offlineRepeat) warm = std::max(warm, E.exportWarm ? 4 : 8); // the upscaler's history fills over jittered frames
      E.exportWarm = true;
      for (int i = 0; i < warm; i++)
      {
         double g = 0.0;
         const auto tpW = std::chrono::steady_clock::now();
         if (!liveRender(sProf ? &g : nullptr))
            return false;
         E.profWarmGpu += g;
         if (sProf) // S0.1: each warm-up is one live frame of a held view; frames 2.. are the warm frame rate
            std::fprintf(stderr, "ENGINE_PROF live frame=%d ms=%.2f gpu=%.2f\n", i + 1,
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tpW).count(), g);
         fr.frameIndex = ++E.frame;
         fr.reuseShadowMask = true; // one camera, scene and light set: the warm-ups and the final render share one mask
      }
   }
   else if ((liveEngineFeatures & 8) != 0 && !changed)
   {
      const int target = std::clamp(ptSamples, 1, 4096);
      if (E.settled >= target)
         traced = true;   // converged: the picture in the image is final
      else
      {
         const int step = std::min(4, target - E.settled);
         traced = E.raster->PathTrace(*E.img, fr, step, std::clamp(ptBounces, 1, 16), E.settled == 0, nullptr);
         if (traced) E.settled += step;
      }
   }
   const auto tp2 = std::chrono::steady_clock::now();
   if (traced) E.upReset = true; // the path tracer wrote E.img itself
   if (!traced)
   {
      E.settled = 0;
      if (!liveRender(nullptr))
         return false;
   }
   // Export steps the counter by the warm-up count, so print every exported frame there.
   const bool profNow = sProf && ((E.frame % 30) == 0 || offline);
   if (profNow)
   {
      const auto tp3 = std::chrono::steady_clock::now();
      auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
      std::fprintf(stderr, "ENGINE_PROF warmGpu=%.1f meshes=%.2f (changed %d, %zu tris) build=%.2f render=%.2f inst=%zu env=%dx%d lod=%.0f bg=%d conn=%d\n",
                   E.profWarmGpu, ms(tp0, tp1), changedMeshes, changedTris, ms(tp1, tp2), ms(tp2, tp3), fr.instances.size(), fr.envW, fr.envH,
                   fr.envBaseLod, (int)fr.envBackground, (int)envInput.IsConnected());
      E.profWarmGpu = 0.0;
   }

   // GL reads the engine's picture from a rectangle texture; the node's output is an ordinary 2D texture.
   if (E.readFbo == 0)
      glGenFramebuffers(1, &E.readFbo);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, E.readFbo);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, E.img->GlTexture(), 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   mRevision = NextTextureRevision();
   mHasSceneBuilt = false;
   if (profNow)
   {
      const auto tpZ = std::chrono::steady_clock::now();
      std::fprintf(stderr, "ENGINE_PROF2 sources=%.2f total=%.2f\n", std::chrono::duration<double, std::milli>(tp0 - tpA).count(),
                   std::chrono::duration<double, std::milli>(tpZ - tpA).count());
   }
   return true;
}
#else
bool Render3DNode::CookEngine(int) { return false; }
bool Render3DNode::LiveEngineAvailable() { return false; }
#endif

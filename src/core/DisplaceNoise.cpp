#include "DisplaceNoise.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DisplaceNoise
{
   namespace
   {
      // Every line here has a twin in kGlsl below, in the same order of
      // operations, so the two paths round alike.
      constexpr float kCurlEps = 0.01f;
      constexpr float kCurlGain = 0.35f; // brings the curl to roughly the -1..1 of the scalars

      inline uint32_t Hash(int x, int y, int z, uint32_t s)
      {
         uint32_t h = s ^ ((uint32_t)x * 0x8da6b343u) ^ ((uint32_t)y * 0xd8163841u) ^ ((uint32_t)z * 0xcb1ab31fu);
         h ^= h >> 16;
         h *= 0x7feb352du;
         h ^= h >> 15;
         h *= 0x846ca68bu;
         h ^= h >> 16;
         return h;
      }

      inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
      inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
      inline float Unit(uint32_t bits) { return (float)(bits & 1023u) * (1.0f / 1023.0f); }

      inline float Corner(int x, int y, int z, uint32_t s, float dx, float dy, float dz)
      {
         const uint32_t h = Hash(x, y, z, s);
         const float gx = Unit(h) * 2.0f - 1.0f;
         const float gy = Unit(h >> 10) * 2.0f - 1.0f;
         const float gz = Unit(h >> 20) * 2.0f - 1.0f;
         return gx * dx + gy * dy + gz * dz;
      }

      float Gradient(float x, float y, float z, uint32_t s)
      {
         const float fx0 = std::floor(x), fy0 = std::floor(y), fz0 = std::floor(z);
         const int ix = (int)fx0, iy = (int)fy0, iz = (int)fz0;
         const float fx = x - fx0, fy = y - fy0, fz = z - fz0;
         const float ux = Fade(fx), uy = Fade(fy), uz = Fade(fz);
         const float n000 = Corner(ix, iy, iz, s, fx, fy, fz);
         const float n100 = Corner(ix + 1, iy, iz, s, fx - 1.0f, fy, fz);
         const float n010 = Corner(ix, iy + 1, iz, s, fx, fy - 1.0f, fz);
         const float n110 = Corner(ix + 1, iy + 1, iz, s, fx - 1.0f, fy - 1.0f, fz);
         const float n001 = Corner(ix, iy, iz + 1, s, fx, fy, fz - 1.0f);
         const float n101 = Corner(ix + 1, iy, iz + 1, s, fx - 1.0f, fy, fz - 1.0f);
         const float n011 = Corner(ix, iy + 1, iz + 1, s, fx, fy - 1.0f, fz - 1.0f);
         const float n111 = Corner(ix + 1, iy + 1, iz + 1, s, fx - 1.0f, fy - 1.0f, fz - 1.0f);
         const float a = Lerp(Lerp(n000, n100, ux), Lerp(n010, n110, ux), uy);
         const float b = Lerp(Lerp(n001, n101, ux), Lerp(n011, n111, ux), uy);
         return Lerp(a, b, uz) * 1.5f;
      }

      // One step of `w` per layer; the two layers either side fade across.
      float Layer(float x, float y, float z, uint32_t s, float w)
      {
         const float fw = std::floor(w);
         const int iw = (int)fw;
         const float u = Fade(w - fw);
         return Lerp(Gradient(x, y, z, Hash(iw, 45, 31, s)), Gradient(x, y, z, Hash(iw + 1, 45, 31, s)), u);
      }

      // Distance to the nearest of one jittered point per cell; the points
      // circle their cell centres as `w` moves.
      float Cells(float x, float y, float z, uint32_t s, float w)
      {
         const float fx0 = std::floor(x), fy0 = std::floor(y), fz0 = std::floor(z);
         const int ix = (int)fx0, iy = (int)fy0, iz = (int)fz0;
         float best = 8.0f;
         for (int cz = -1; cz <= 1; cz++)
            for (int cy = -1; cy <= 1; cy++)
               for (int cx = -1; cx <= 1; cx++)
               {
                  const uint32_t h = Hash(ix + cx, iy + cy, iz + cz, s);
                  const float px = (float)(ix + cx) + 0.5f + 0.45f * std::sin(6.2831853f * (Unit(h) + w));
                  const float py = (float)(iy + cy) + 0.5f + 0.45f * std::sin(6.2831853f * (Unit(h >> 10) + w));
                  const float pz = (float)(iz + cz) + 0.5f + 0.45f * std::sin(6.2831853f * (Unit(h >> 20) + w));
                  const float dx = px - x, dy = py - y, dz = pz - z;
                  best = std::min(best, dx * dx + dy * dy + dz * dz);
               }
         return std::min(std::sqrt(best), 1.0f) * 2.0f - 1.0f;
      }

      float Octaves(const Params& p, float x, float y, float z, uint32_t s, bool ridged)
      {
         const int octaves = std::max(1, std::min(8, p.octaves));
         float sum = 0.0f, norm = 0.0f, amp = 1.0f, f = 1.0f;
         for (int o = 0; o < octaves; o++)
         {
            float n = Layer(x * f, y * f, z * f, s + (uint32_t)o * 1013u, p.w);
            if (ridged)
            {
               n = 1.0f - std::fabs(n);
               n = n * n;
            }
            sum += amp * n;
            norm += amp;
            amp *= p.roughness;
            f *= 2.0f;
         }
         const float v = sum / std::max(norm, 1e-6f);
         return ridged ? v * 2.0f - 1.0f : v;
      }

      float ScalarAt(const Params& p, float x, float y, float z, uint32_t s)
      {
         switch (p.type)
         {
            case kFractal: return Octaves(p, x, y, z, s, false);
            case kRidged: return Octaves(p, x, y, z, s, true);
            case kCells: return Cells(x, y, z, s, p.w);
            default: return Layer(x, y, z, s, p.w);
         }
      }

      void Curl(const Params& p, float x, float y, float z, float out[3])
      {
         const uint32_t sx = p.seed + 11u, sy = p.seed + 23u, sz = p.seed + 37u;
         const float e = kCurlEps;
         const float dAzdy = Layer(x, y + e, z, sz, p.w) - Layer(x, y - e, z, sz, p.w);
         const float dAydz = Layer(x, y, z + e, sy, p.w) - Layer(x, y, z - e, sy, p.w);
         const float dAxdz = Layer(x, y, z + e, sx, p.w) - Layer(x, y, z - e, sx, p.w);
         const float dAzdx = Layer(x + e, y, z, sz, p.w) - Layer(x - e, y, z, sz, p.w);
         const float dAydx = Layer(x + e, y, z, sy, p.w) - Layer(x - e, y, z, sy, p.w);
         const float dAxdy = Layer(x, y + e, z, sx, p.w) - Layer(x, y - e, z, sx, p.w);
         const float k = kCurlGain / (2.0f * e);
         out[0] = (dAzdy - dAydz) * k;
         out[1] = (dAxdz - dAzdx) * k;
         out[2] = (dAydx - dAxdy) * k;
      }

      inline bool UsesVector(const Params& p, int mode) { return mode == 1 || p.type == kCurl; }
   }

   float Scalar(const Params& p, float x, float y, float z)
   {
      x *= p.frequency; y *= p.frequency; z *= p.frequency;
      if (p.type == kCurl)
      {
         float v[3];
         Curl(p, x, y, z, v);
         return v[0];
      }
      return ScalarAt(p, x, y, z, p.seed);
   }

   void Vector(const Params& p, float x, float y, float z, float out[3])
   {
      x *= p.frequency; y *= p.frequency; z *= p.frequency;
      if (p.type == kCurl)
      {
         Curl(p, x, y, z, out);
         return;
      }
      out[0] = ScalarAt(p, x, y, z, p.seed);
      out[1] = ScalarAt(p, x, y, z, p.seed + 7919u);
      out[2] = ScalarAt(p, x, y, z, p.seed + 15887u);
   }

   Mesh DisplaceMesh(const Mesh& in, const Params& p, int mode, float strength, bool flat, bool flip,
                     const std::vector<unsigned char>* vertexMask)
   {
      Mesh out = in;
      const size_t n = out.vertices.size();
      if (n == 0)
         return out;
      // Same seam rule as MeshOps::Displace: offsets average over vertices
      // that share a start position.
      const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(out);
      std::vector<float> accum(n * 3, 0.0f);
      std::vector<int> count(n, 0);
      const bool vec = UsesVector(p, mode);
      for (size_t i = 0; i < n; i++)
      {
         if (vertexMask != nullptr && (i >= vertexMask->size() || !(*vertexMask)[i]))
            continue;
         const Vertex& v = out.vertices[i];
         float d[3];
         if (vec)
         {
            Vector(p, v.px, v.py, v.pz, d);
            d[0] *= strength; d[1] *= strength; d[2] *= strength;
         }
         else
         {
            const float s = Scalar(p, v.px, v.py, v.pz) * strength;
            d[0] = v.nx * s; d[1] = v.ny * s; d[2] = v.nz * s;
         }
         const unsigned int w = weld[i];
         accum[w * 3 + 0] += d[0]; accum[w * 3 + 1] += d[1]; accum[w * 3 + 2] += d[2];
         count[w]++;
      }
      for (size_t i = 0; i < n; i++)
      {
         if (vertexMask != nullptr && (i >= vertexMask->size() || !(*vertexMask)[i]))
            continue;
         const unsigned int w = weld[i];
         const float c = (float)std::max(1, count[w]);
         out.vertices[i].px += accum[w * 3 + 0] / c;
         out.vertices[i].py += accum[w * 3 + 1] / c;
         out.vertices[i].pz += accum[w * 3 + 2] / c;
      }
      return MeshOps::RecalculateNormals(out, flat, flip);
   }

   void DisplacePoints(std::vector<Particle>& points, const Params& p, int mode, float strength, int affects)
   {
      const bool vec = UsesVector(p, mode);
      for (Particle& q : points)
      {
         if (affects == kSize)
         {
            q.scale *= std::max(0.0f, 1.0f + Scalar(p, q.px, q.py, q.pz) * strength);
            continue;
         }
         if (vec)
         {
            float d[3];
            Vector(p, q.px, q.py, q.pz, d);
            if (affects == kColour)
            {
               q.r = std::max(0.0f, q.r + d[0] * strength * 0.5f);
               q.g = std::max(0.0f, q.g + d[1] * strength * 0.5f);
               q.b = std::max(0.0f, q.b + d[2] * strength * 0.5f);
               q.hasColor = true;
            }
            else
            {
               q.px += d[0] * strength; q.py += d[1] * strength; q.pz += d[2] * strength;
            }
            continue;
         }
         const float s = Scalar(p, q.px, q.py, q.pz);
         if (affects == kColour)
         {
            const float k = std::max(0.0f, 1.0f + s * strength);
            q.r *= k; q.g *= k; q.b *= k;
            q.hasColor = true;
         }
         else
            q.pz += s * strength;
      }
   }

   const char* const kGlsl = R"GLSL(
uniform int uDnType;
uniform float uDnFreq;
uniform float uDnW;
uniform int uDnOctaves;
uniform float uDnRough;
uniform uint uDnSeed;
uniform int uDnMode;
uniform float uDnStrength;
uniform int uDnAffects;
uint DnHash(int x, int y, int z, uint s) {
   uint h = s ^ (uint(x) * 0x8da6b343u) ^ (uint(y) * 0xd8163841u) ^ (uint(z) * 0xcb1ab31fu);
   h ^= h >> 16u;
   h *= 0x7feb352du;
   h ^= h >> 15u;
   h *= 0x846ca68bu;
   h ^= h >> 16u;
   return h;
}
float DnLerp(float a, float b, float t) { return a + (b - a) * t; }
float DnFade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
float DnUnit(uint bits) { return float(bits & 1023u) * (1.0 / 1023.0); }
float DnCorner(int x, int y, int z, uint s, float dx, float dy, float dz) {
   uint h = DnHash(x, y, z, s);
   float gx = DnUnit(h) * 2.0 - 1.0;
   float gy = DnUnit(h >> 10u) * 2.0 - 1.0;
   float gz = DnUnit(h >> 20u) * 2.0 - 1.0;
   return gx * dx + gy * dy + gz * dz;
}
float DnGradient(float x, float y, float z, uint s) {
   float fx0 = floor(x), fy0 = floor(y), fz0 = floor(z);
   int ix = int(fx0), iy = int(fy0), iz = int(fz0);
   float fx = x - fx0, fy = y - fy0, fz = z - fz0;
   float ux = DnFade(fx), uy = DnFade(fy), uz = DnFade(fz);
   float n000 = DnCorner(ix, iy, iz, s, fx, fy, fz);
   float n100 = DnCorner(ix + 1, iy, iz, s, fx - 1.0, fy, fz);
   float n010 = DnCorner(ix, iy + 1, iz, s, fx, fy - 1.0, fz);
   float n110 = DnCorner(ix + 1, iy + 1, iz, s, fx - 1.0, fy - 1.0, fz);
   float n001 = DnCorner(ix, iy, iz + 1, s, fx, fy, fz - 1.0);
   float n101 = DnCorner(ix + 1, iy, iz + 1, s, fx - 1.0, fy, fz - 1.0);
   float n011 = DnCorner(ix, iy + 1, iz + 1, s, fx, fy - 1.0, fz - 1.0);
   float n111 = DnCorner(ix + 1, iy + 1, iz + 1, s, fx - 1.0, fy - 1.0, fz - 1.0);
   float a = DnLerp(DnLerp(n000, n100, ux), DnLerp(n010, n110, ux), uy);
   float b = DnLerp(DnLerp(n001, n101, ux), DnLerp(n011, n111, ux), uy);
   return DnLerp(a, b, uz) * 1.5;
}
float DnLayer(float x, float y, float z, uint s, float w) {
   float fw = floor(w);
   int iw = int(fw);
   float u = DnFade(w - fw);
   return DnLerp(DnGradient(x, y, z, DnHash(iw, 45, 31, s)), DnGradient(x, y, z, DnHash(iw + 1, 45, 31, s)), u);
}
float DnCells(float x, float y, float z, uint s, float w) {
   float fx0 = floor(x), fy0 = floor(y), fz0 = floor(z);
   int ix = int(fx0), iy = int(fy0), iz = int(fz0);
   float best = 8.0;
   for (int cz = -1; cz <= 1; cz++)
      for (int cy = -1; cy <= 1; cy++)
         for (int cx = -1; cx <= 1; cx++) {
            uint h = DnHash(ix + cx, iy + cy, iz + cz, s);
            float px = float(ix + cx) + 0.5 + 0.45 * sin(6.2831853 * (DnUnit(h) + w));
            float py = float(iy + cy) + 0.5 + 0.45 * sin(6.2831853 * (DnUnit(h >> 10u) + w));
            float pz = float(iz + cz) + 0.5 + 0.45 * sin(6.2831853 * (DnUnit(h >> 20u) + w));
            float dx = px - x, dy = py - y, dz = pz - z;
            best = min(best, dx * dx + dy * dy + dz * dz);
         }
   return min(sqrt(best), 1.0) * 2.0 - 1.0;
}
float DnOctaves(float x, float y, float z, uint s, bool ridged) {
   int octaves = max(1, min(8, uDnOctaves));
   float sum = 0.0, norm = 0.0, amp = 1.0, f = 1.0;
   for (int o = 0; o < octaves; o++) {
      float n = DnLayer(x * f, y * f, z * f, s + uint(o) * 1013u, uDnW);
      if (ridged) {
         n = 1.0 - abs(n);
         n = n * n;
      }
      sum += amp * n;
      norm += amp;
      amp *= uDnRough;
      f *= 2.0;
   }
   float v = sum / max(norm, 1e-6);
   return ridged ? v * 2.0 - 1.0 : v;
}
float DnScalarAt(float x, float y, float z, uint s) {
   if (uDnType == 1) return DnOctaves(x, y, z, s, false);
   if (uDnType == 3) return DnOctaves(x, y, z, s, true);
   if (uDnType == 4) return DnCells(x, y, z, s, uDnW);
   return DnLayer(x, y, z, s, uDnW);
}
vec3 DnCurl(float x, float y, float z) {
   uint sx = uDnSeed + 11u, sy = uDnSeed + 23u, sz = uDnSeed + 37u;
   float e = 0.01;
   float dAzdy = DnLayer(x, y + e, z, sz, uDnW) - DnLayer(x, y - e, z, sz, uDnW);
   float dAydz = DnLayer(x, y, z + e, sy, uDnW) - DnLayer(x, y, z - e, sy, uDnW);
   float dAxdz = DnLayer(x, y, z + e, sx, uDnW) - DnLayer(x, y, z - e, sx, uDnW);
   float dAzdx = DnLayer(x + e, y, z, sz, uDnW) - DnLayer(x - e, y, z, sz, uDnW);
   float dAydx = DnLayer(x + e, y, z, sy, uDnW) - DnLayer(x - e, y, z, sy, uDnW);
   float dAxdy = DnLayer(x, y + e, z, sx, uDnW) - DnLayer(x, y - e, z, sx, uDnW);
   float k = 0.35 / (2.0 * e);
   return vec3((dAzdy - dAydz) * k, (dAxdz - dAzdx) * k, (dAydx - dAxdy) * k);
}
float DnScalar(vec3 P) {
   float x = P.x * uDnFreq, y = P.y * uDnFreq, z = P.z * uDnFreq;
   if (uDnType == 2) return DnCurl(x, y, z).x;
   return DnScalarAt(x, y, z, uDnSeed);
}
vec3 DnVector(vec3 P) {
   float x = P.x * uDnFreq, y = P.y * uDnFreq, z = P.z * uDnFreq;
   if (uDnType == 2) return DnCurl(x, y, z);
   return vec3(DnScalarAt(x, y, z, uDnSeed), DnScalarAt(x, y, z, uDnSeed + 7919u),
               DnScalarAt(x, y, z, uDnSeed + 15887u));
}
void DnApply(inout vec3 P, inout vec3 Cd, inout float W) {
   if (uDnAffects == 2) {
      W *= max(0.0, 1.0 + DnScalar(P) * uDnStrength);
      return;
   }
   if (uDnMode == 1 || uDnType == 2) {
      vec3 d = DnVector(P);
      if (uDnAffects == 1) Cd = max(vec3(0.0), Cd + d * uDnStrength * 0.5);
      else P += d * uDnStrength;
      return;
   }
   float s = DnScalar(P);
   if (uDnAffects == 1) Cd *= max(0.0, 1.0 + s * uDnStrength);
   else P.z += s * uDnStrength;
}
)GLSL";
}

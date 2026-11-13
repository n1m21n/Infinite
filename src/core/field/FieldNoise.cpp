#include "FieldNoise.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

// The C++ below and the GLSL text at the end are the same functions line for line: change both together.

namespace Field
{
   namespace
   {
      struct U3 { uint32_t x, y, z; };
      struct U4 { uint32_t x, y, z, w; };

      U3 Pcg3d(U3 v)
      {
         v.x = v.x * 1664525u + 1013904223u; v.y = v.y * 1664525u + 1013904223u; v.z = v.z * 1664525u + 1013904223u;
         v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
         v.x ^= v.x >> 16u; v.y ^= v.y >> 16u; v.z ^= v.z >> 16u;
         v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
         return v;
      }
      U4 Pcg4d(U4 v)
      {
         v.x = v.x * 1664525u + 1013904223u; v.y = v.y * 1664525u + 1013904223u;
         v.z = v.z * 1664525u + 1013904223u; v.w = v.w * 1664525u + 1013904223u;
         v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
         v.x ^= v.x >> 16u; v.y ^= v.y >> 16u; v.z ^= v.z >> 16u; v.w ^= v.w >> 16u;
         v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
         return v;
      }
      uint32_t U(float c) { return (uint32_t)(int32_t)c; } // lattice coordinate (already integral) as GLSL uint(int(c))
      float F24(uint32_t h) { return (float)(h >> 8u) * (1.0f / 16777216.0f); }

      // cell -> 3 floats in [0, 1)
      void H3(float cx, float cy, float cz, float o[3])
      {
         const U3 h = Pcg3d({ U(cx), U(cy), U(cz) });
         o[0] = F24(h.x); o[1] = F24(h.y); o[2] = F24(h.z);
      }
      float Fade(float f) { return f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f); }
      float Mix(float a, float b, float t) { return a + (b - a) * t; } // GLSL mix: a * (1 - t) + b * t, equal to rounding
      float Gdot3(float cx, float cy, float cz, float fx, float fy, float fz)
      {
         float g[3];
         H3(cx, cy, cz, g);
         return (g[0] * 2.0f - 1.0f) * fx + (g[1] * 2.0f - 1.0f) * fy + (g[2] * 2.0f - 1.0f) * fz;
      }
      float Noise3(float px, float py, float pz)
      {
         const float ix = std::floor(px), iy = std::floor(py), iz = std::floor(pz);
         const float fx = px - ix, fy = py - iy, fz = pz - iz;
         const float ux = Fade(fx), uy = Fade(fy), uz = Fade(fz);
         float n[8];
         for (int k = 0; k < 8; k++)
         {
            const float ox = (float)(k & 1), oy = (float)((k >> 1) & 1), oz = (float)((k >> 2) & 1);
            n[k] = Gdot3(ix + ox, iy + oy, iz + oz, fx - ox, fy - oy, fz - oz);
         }
         const float z0 = Mix(Mix(n[0], n[1], ux), Mix(n[2], n[3], ux), uy);
         const float z1 = Mix(Mix(n[4], n[5], ux), Mix(n[6], n[7], ux), uy);
         return Mix(z0, z1, uz);
      }
      float Noise4(float px, float py, float pz, float pw)
      {
         const float i[4] = { std::floor(px), std::floor(py), std::floor(pz), std::floor(pw) };
         const float f[4] = { px - i[0], py - i[1], pz - i[2], pw - i[3] };
         const float u[4] = { Fade(f[0]), Fade(f[1]), Fade(f[2]), Fade(f[3]) };
         float s[2];
         for (int w = 0; w < 2; w++)
         {
            float n[8];
            for (int k = 0; k < 8; k++)
            {
               const float o[4] = { (float)(k & 1), (float)((k >> 1) & 1), (float)((k >> 2) & 1), (float)w };
               const U4 h = Pcg4d({ U(i[0] + o[0]), U(i[1] + o[1]), U(i[2] + o[2]), U(i[3] + o[3]) });
               n[k] = (F24(h.x) * 2.0f - 1.0f) * (f[0] - o[0]) + (F24(h.y) * 2.0f - 1.0f) * (f[1] - o[1]) +
                      (F24(h.z) * 2.0f - 1.0f) * (f[2] - o[2]) + (F24(h.w) * 2.0f - 1.0f) * (f[3] - o[3]);
            }
            const float z0 = Mix(Mix(n[0], n[1], u[0]), Mix(n[2], n[3], u[0]), u[1]);
            const float z1 = Mix(Mix(n[4], n[5], u[0]), Mix(n[6], n[7], u[0]), u[1]);
            s[w] = Mix(z0, z1, u[2]);
         }
         return Mix(s[0], s[1], u[3]);
      }
      float Fbm(float px, float py, float pz, float octaves, float lacunarity, float gain)
      {
         const int n = (int)std::min(std::max(std::floor(octaves), 1.0f), 16.0f);
         float s = 0.0f, a = 1.0f, norm = 0.0f;
         for (int k = 0; k < n; k++)
         {
            s += a * Noise3(px, py, pz);
            norm += a;
            a *= gain;
            px *= lacunarity; py *= lacunarity; pz *= lacunarity;
         }
         return s / std::max(norm, 1e-6f);
      }
      void Voronoi3(float px, float py, float pz, double out[3])
      {
         const float ix = std::floor(px), iy = std::floor(py), iz = std::floor(pz);
         float f1 = 1e9f, f2 = 1e9f, bx = ix, by = iy, bz = iz;
         for (int z = -1; z <= 1; z++)
            for (int y = -1; y <= 1; y++)
               for (int x = -1; x <= 1; x++)
               {
                  const float cx = ix + (float)x, cy = iy + (float)y, cz = iz + (float)z;
                  float j[3];
                  H3(cx, cy, cz, j);
                  const float dx = cx + j[0] - px, dy = cy + j[1] - py, dz = cz + j[2] - pz;
                  const float dd = dx * dx + dy * dy + dz * dz;
                  if (dd < f1) { f2 = f1; f1 = dd; bx = cx; by = cy; bz = cz; }
                  else if (dd < f2) f2 = dd;
               }
         const U3 h = Pcg3d({ U(bx) ^ 0x5bd1e995u, U(by) ^ 0x5bd1e995u, U(bz) ^ 0x5bd1e995u });
         out[0] = std::sqrt(f1); out[1] = std::sqrt(f2); out[2] = F24(h.y);
      }
      float Brick(float px, float py, float mortar)
      {
         const float row = std::floor(py * 2.0f);
         const float x = px + 0.5f * (row - 2.0f * std::floor(row * 0.5f));
         const float fx = x - std::floor(x), fy = py * 2.0f - row;
         const float m = std::min(std::min(fx, 1.0f - fx), std::min(fy, 1.0f - fy) * 0.5f);
         return m < mortar * 0.5f ? 0.0f : 1.0f; // GLSL step(edge, x)
      }
   }

   bool IsNoiseBuiltin(const std::string& name, int* arity, int* resultLanes)
   {
      int a = 0, r = 1;
      if (name == "noise3" || name == "voronoi3" || name == "wave" || name == "checker" || name == "gradient" || name == "white")
         a = 1;
      else if (name == "noise4" || name == "brick")
         a = 2;
      else if (name == "fbm")
         a = 4;
      else
         return false;
      if (name == "voronoi3") r = 3;
      if (arity) *arity = a;
      if (resultLanes) *resultLanes = r;
      return true;
   }

   bool EvalNoiseBuiltin(const std::string& name, const double* v, int count, double out[3])
   {
      int arity = 0;
      if (!IsNoiseBuiltin(name, &arity) || count < 2 + arity) return false;
      const float x = (float)v[0], y = (float)v[1], z = (float)v[2];
      auto s = [&](int k) { return (float)v[3 + k]; };
      if (name == "noise3") out[0] = Noise3(x, y, z);
      else if (name == "noise4") out[0] = Noise4(x, y, z, s(0));
      else if (name == "fbm") out[0] = Fbm(x, y, z, s(0), s(1), s(2));
      else if (name == "voronoi3") Voronoi3(x, y, z, out);
      else if (name == "wave") out[0] = 0.5f - 0.5f * std::cos(6.28318530718f * x);
      else if (name == "brick") out[0] = Brick(x, y, s(0));
      else if (name == "checker")
      {
         const float c = std::floor(x) + std::floor(y) + std::floor(z);
         out[0] = c - 2.0f * std::floor(c * 0.5f);
      }
      else if (name == "gradient") out[0] = std::min(std::max(x, 0.0f), 1.0f);
      else if (name == "white")
      {
         float h[3];
         H3(std::floor(x), std::floor(y), std::floor(z), h);
         out[0] = h[0];
      }
      return true;
   }

   const char* NoiseGlsl()
   {
      return R"(
// ---- noise built-ins (FieldNoise.cpp, row 2.1): PCG3D / PCG4D hash, integer only ----
uvec3 fld_pcg3d(uvec3 v)
{
   v = v * 1664525u + 1013904223u;
   v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
   v = v ^ (v >> 16u);
   v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
   return v;
}
uvec4 fld_pcg4d(uvec4 v)
{
   v = v * 1664525u + 1013904223u;
   v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
   v = v ^ (v >> 16u);
   v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
   return v;
}
vec3 fld_hash3(vec3 c) { return vec3(fld_pcg3d(uvec3(ivec3(c))) >> 8u) * (1.0 / 16777216.0); }
vec3 fld_fade3(vec3 f) { return f * f * f * (f * (f * 6.0 - 15.0) + 10.0); }
float fld_gdot3(vec3 c, vec3 f) { return dot(fld_hash3(c) * 2.0 - 1.0, f); }
float fld_noise3(vec3 p)
{
   vec3 i = floor(p); vec3 f = p - i; vec3 u = fld_fade3(f);
   float n0 = fld_gdot3(i, f);
   float n1 = fld_gdot3(i + vec3(1.0, 0.0, 0.0), f - vec3(1.0, 0.0, 0.0));
   float n2 = fld_gdot3(i + vec3(0.0, 1.0, 0.0), f - vec3(0.0, 1.0, 0.0));
   float n3 = fld_gdot3(i + vec3(1.0, 1.0, 0.0), f - vec3(1.0, 1.0, 0.0));
   float n4 = fld_gdot3(i + vec3(0.0, 0.0, 1.0), f - vec3(0.0, 0.0, 1.0));
   float n5 = fld_gdot3(i + vec3(1.0, 0.0, 1.0), f - vec3(1.0, 0.0, 1.0));
   float n6 = fld_gdot3(i + vec3(0.0, 1.0, 1.0), f - vec3(0.0, 1.0, 1.0));
   float n7 = fld_gdot3(i + vec3(1.0, 1.0, 1.0), f - vec3(1.0, 1.0, 1.0));
   float z0 = mix(mix(n0, n1, u.x), mix(n2, n3, u.x), u.y);
   float z1 = mix(mix(n4, n5, u.x), mix(n6, n7, u.x), u.y);
   return mix(z0, z1, u.z);
}
float fld_gdot4(vec4 c, vec4 f) { return dot(vec4(fld_pcg4d(uvec4(ivec4(c))) >> 8u) * (1.0 / 16777216.0) * 2.0 - 1.0, f); }
float fld_noise4slice(vec4 i, vec4 f, vec4 u, float w)
{
   vec4 o = vec4(0.0, 0.0, 0.0, w);
   float n0 = fld_gdot4(i + o, f - o);
   o = vec4(1.0, 0.0, 0.0, w); float n1 = fld_gdot4(i + o, f - o);
   o = vec4(0.0, 1.0, 0.0, w); float n2 = fld_gdot4(i + o, f - o);
   o = vec4(1.0, 1.0, 0.0, w); float n3 = fld_gdot4(i + o, f - o);
   o = vec4(0.0, 0.0, 1.0, w); float n4 = fld_gdot4(i + o, f - o);
   o = vec4(1.0, 0.0, 1.0, w); float n5 = fld_gdot4(i + o, f - o);
   o = vec4(0.0, 1.0, 1.0, w); float n6 = fld_gdot4(i + o, f - o);
   o = vec4(1.0, 1.0, 1.0, w); float n7 = fld_gdot4(i + o, f - o);
   float z0 = mix(mix(n0, n1, u.x), mix(n2, n3, u.x), u.y);
   float z1 = mix(mix(n4, n5, u.x), mix(n6, n7, u.x), u.y);
   return mix(z0, z1, u.z);
}
float fld_noise4(vec3 p3, float pw)
{
   vec4 p = vec4(p3, pw); vec4 i = floor(p); vec4 f = p - i;
   vec4 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
   return mix(fld_noise4slice(i, f, u, 0.0), fld_noise4slice(i, f, u, 1.0), u.w);
}
float fld_fbm(vec3 p, float octaves, float lacunarity, float gain)
{
   int n = int(min(max(floor(octaves), 1.0), 16.0));
   float s = 0.0; float a = 1.0; float norm = 0.0;
   for (int k = 0; k < 16; k++)
   {
      if (k >= n) break;
      s += a * fld_noise3(p);
      norm += a;
      a *= gain;
      p *= lacunarity;
   }
   return s / max(norm, 1e-6);
}
vec3 fld_voronoi3(vec3 p)
{
   vec3 i = floor(p);
   float f1 = 1e9; float f2 = 1e9; vec3 b = i;
   for (int z = -1; z <= 1; z++)
      for (int y = -1; y <= 1; y++)
         for (int x = -1; x <= 1; x++)
         {
            vec3 c = i + vec3(float(x), float(y), float(z));
            vec3 d = c + fld_hash3(c) - p;
            float dd = dot(d, d);
            if (dd < f1) { f2 = f1; f1 = dd; b = c; }
            else if (dd < f2) { f2 = dd; }
         }
   uvec3 h = fld_pcg3d(uvec3(ivec3(b)) ^ uvec3(0x5bd1e995u));
   return vec3(sqrt(f1), sqrt(f2), float(h.y >> 8u) * (1.0 / 16777216.0));
}
float fld_wave(vec3 p) { return 0.5 - 0.5 * cos(6.28318530718 * p.x); }
float fld_brick(vec3 p, float mortar)
{
   float row = floor(p.y * 2.0);
   float x = p.x + 0.5 * (row - 2.0 * floor(row * 0.5));
   float fx = x - floor(x); float fy = p.y * 2.0 - row;
   float m = min(min(fx, 1.0 - fx), min(fy, 1.0 - fy) * 0.5);
   return step(mortar * 0.5, m);
}
float fld_checker(vec3 p) { float c = floor(p.x) + floor(p.y) + floor(p.z); return c - 2.0 * floor(c * 0.5); }
float fld_gradient(vec3 p) { return clamp(p.x, 0.0, 1.0); }
float fld_white(vec3 p) { return fld_hash3(floor(p)).x; }
)";
   }

   std::string NoiseMsl()
   {
      // GLSL -> MSL: (u|i)vecN -> (uint|int)N, vecN -> floatN; every helper static (one per library).
      const std::string g = NoiseGlsl();
      std::string out;
      out.reserve(g.size() + 512);
      for (size_t i = 0; i < g.size(); i++)
      {
         const bool wordStart = i == 0 || !(std::isalnum((unsigned char)g[i - 1]) || g[i - 1] == '_');
         auto isVec = [&](size_t at) {
            return g.compare(at, 3, "vec") == 0 && at + 3 < g.size() && g[at + 3] >= '2' && g[at + 3] <= '4' &&
                   (at + 4 >= g.size() || !(std::isalnum((unsigned char)g[at + 4]) || g[at + 4] == '_'));
         };
         if (wordStart && (g[i] == 'u' || g[i] == 'i') && isVec(i + 1))
         {
            out += g[i] == 'u' ? "uint" : "int";
            out += g[i + 4];
            i += 4;
            continue;
         }
         if (wordStart && isVec(i))
         {
            out += "float";
            out += g[i + 3];
            i += 3;
            continue;
         }
         // a definition at line start ("uvec3 fld_..." / "float fld_..."): make it static
         if ((i == 0 || g[i - 1] == '\n') && std::isalpha((unsigned char)g[i]))
         {
            const size_t sp = g.find(' ', i);
            if (sp != std::string::npos && g.compare(sp + 1, 4, "fld_") == 0)
               out += "static ";
         }
         out += g[i];
      }
      return out;
   }
}

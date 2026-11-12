#include "MeshWear.h"
#include "TriBvh.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace MeshWear
{
   using namespace tribvh;

   // splitmix64: the weld keys are small integers, which a plain xor-multiply hash folds onto each other.
   inline unsigned long long Mix(unsigned long long x)
   {
      x += 0x9E3779B97F4A7C15ull;
      x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
      x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
      return x ^ (x >> 31);
   }

   std::vector<float> Bake(const float* pos, size_t stride, size_t nv, const unsigned int* idx, size_t ni)
   {
      std::vector<float> out;
      const size_t nt = ni / 3;
      if (nv == 0 || nt == 0)
         return out;
      auto P = [&](size_t i) { const float* q = pos + i * stride; return V3{ q[0], q[1], q[2] }; };
      V3 lo = P(0), hi = lo;
      for (size_t i = 1; i < nv; i++)
      {
         const V3 p = P(i);
         lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
         hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
      }
      // Bounding radius: the farthest vertex from the box centre (a sphere's own radius).
      const V3 mid = (lo + hi) * 0.5f;
      float r2 = 0.0f;
      for (size_t i = 0; i < nv; i++)
      {
         const V3 d = P(i) - mid;
         r2 = std::max(r2, Dot(d, d));
      }
      const float radius = std::sqrt(r2);
      if (!(radius > 1e-12f))
         return std::vector<float>(nv * 2, 0.0f);

      // Weld by position (a grid 1e-5 of the radius).
      const float q = 1.0f / (radius * 1e-5f);
      std::unordered_map<unsigned long long, unsigned> ids;
      ids.reserve(nv);
      std::vector<unsigned> weld(nv);
      for (size_t i = 0; i < nv; i++)
      {
         const V3 p = P(i);
         unsigned long long h = 0;
         for (float c : { p.x, p.y, p.z }) h = Mix(h ^ Mix((unsigned long long)(long long)std::llround(c * q)));
         weld[i] = ids.emplace(h, (unsigned)ids.size()).first->second;
      }
      const size_t w = ids.size();
      std::vector<V3> wp(w), lap(w), wn(w);
      std::vector<float> area(w, 0.0f);
      for (size_t i = 0; i < nv; i++)
         wp[weld[i]] = P(i);
      // Cotangent Laplacian, barycentric area: K_i = 1/(2A_i) sum (cot a + cot b)(x_i - x_j) = 2 H n.
      for (size_t t = 0; t < nt; t++)
      {
         const unsigned v[3] = { weld[idx[t * 3]], weld[idx[t * 3 + 1]], weld[idx[t * 3 + 2]] };
         if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
            continue;
         const V3 c = Cross(wp[v[1]] - wp[v[0]], wp[v[2]] - wp[v[0]]);
         const float a2 = std::sqrt(Dot(c, c)); // twice the area
         if (a2 < 1e-20f)
            continue;
         for (int k = 0; k < 3; k++)
         {
            area[v[k]] += a2 / 6.0f;
            wn[v[k]] = wn[v[k]] + c; // area-weighted face normal
            // The angle at k faces edge (k+1, k+2): its cotangent weights that edge.
            const unsigned o = v[k], i = v[(k + 1) % 3], j = v[(k + 2) % 3];
            const V3 ea = wp[i] - wp[o], eb = wp[j] - wp[o];
            const float cot = Dot(ea, eb) / a2;
            const V3 d = (wp[i] - wp[j]) * cot;
            lap[i] = lap[i] + d;
            lap[j] = lap[j] - d;
         }
      }
      std::vector<float> curv(w, 0.0f), cav(w, 0.0f);
      for (size_t i = 0; i < w; i++)
      {
         wn[i] = Norm(wn[i]);
         if (area[i] > 1e-20f)
            curv[i] = Dot(lap[i], wn[i]) / (4.0f * area[i]) * radius; // H = K.n / 2, K = lap / (2A)
      }

      // Cavity: 16 cosine rays per welded vertex, short (5 % of the radius), any hit counts.
      Bvh bvh;
      bvh.Build(pos, stride, idx, ni);
      const float reach = 0.05f * radius, eps = radius * 1e-4f;
      constexpr int kRays = 16;
      auto shade = [&](size_t a, size_t b) {
         for (size_t i = a; i < b; i++)
         {
            const float turn = (float)(((unsigned)i * 2654435769u) >> 8) / 16777216.0f;
            int hit = 0;
            for (int s = 0; s < kRays; s++)
               if (bvh.Cast(wp[i] + wn[i] * eps, HemiDir(wn[i], s, kRays, turn), reach, true) < reach)
                  hit++;
            cav[i] = (float)hit / kRays;
         }
      };
      const int workers = w < 2048 ? 1 : std::clamp((int)std::thread::hardware_concurrency(), 1, 8);
      std::vector<std::thread> pool;
      for (int k = 1; k < workers; k++)
         pool.emplace_back(shade, w * k / workers, w * (k + 1) / workers);
      shade(0, w / workers);
      for (std::thread& th : pool)
         th.join();

      out.resize(nv * 2);
      for (size_t i = 0; i < nv; i++)
      {
         out[i * 2] = curv[weld[i]];
         out[i * 2 + 1] = cav[weld[i]];
      }
      return out;
   }
}

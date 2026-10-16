#include "Delaunay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
   struct Tri
   {
      int a, b, c;
      double cx, cy, r2;
      bool dead;
   };

   void Circle(const std::vector<double>& p, Tri& t)
   {
      const double ax = p[t.a * 2], ay = p[t.a * 2 + 1];
      const double bx = p[t.b * 2] - ax, by = p[t.b * 2 + 1] - ay;
      const double cx = p[t.c * 2] - ax, cy = p[t.c * 2 + 1] - ay;
      const double d = 2.0 * (bx * cy - by * cx);
      if (std::fabs(d) < 1e-300)
      {
         t.cx = ax; t.cy = ay; t.r2 = 1e300;
         return;
      }
      const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
      const double ux = (cy * b2 - by * c2) / d;
      const double uy = (bx * c2 - cx * b2) / d;
      t.cx = ax + ux; t.cy = ay + uy;
      t.r2 = ux * ux + uy * uy;
   }
}

std::vector<unsigned int> Delaunay::Triangulate(const std::vector<double>& xy)
{
   std::vector<unsigned int> out;
   const int n = (int)(xy.size() / 2);
   if (n < 3)
      return out;

   double minX = xy[0], maxX = xy[0], minY = xy[1], maxY = xy[1];
   for (int i = 1; i < n; ++i)
   {
      minX = std::min(minX, xy[i * 2]);     maxX = std::max(maxX, xy[i * 2]);
      minY = std::min(minY, xy[i * 2 + 1]); maxY = std::max(maxY, xy[i * 2 + 1]);
   }
   const double span = std::max(std::max(maxX - minX, maxY - minY), 1e-9);
   const double mx = (minX + maxX) * 0.5, my = (minY + maxY) * 0.5;

   // Working copy with three super-triangle vertices appended.
   std::vector<double> p(xy);
   const double big = span * 40.0;
   p.push_back(mx - big); p.push_back(my - big);
   p.push_back(mx + big); p.push_back(my - big);
   p.push_back(mx);       p.push_back(my + big);
   const int s0 = n, s1 = n + 1, s2 = n + 2;

   // Insert in spatial (grid-snake) order so the bad-triangle set stays local.
   std::vector<int> order(n);
   for (int i = 0; i < n; ++i) order[i] = i;
   const int cells = std::max(1, (int)std::sqrt((double)n * 0.5));
   auto key = [&](int i)
   {
      const int gx = std::min(cells - 1, (int)((xy[i * 2] - minX) / span * cells));
      const int gy = std::min(cells - 1, (int)((xy[i * 2 + 1] - minY) / span * cells));
      return gy * cells * 2 + ((gy & 1) ? (cells - 1 - gx) : gx);
   };
   std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return key(a) < key(b); });

   std::vector<Tri> tris;
   tris.reserve((size_t)n * 2 + 8);
   { Tri t{ s0, s1, s2, 0, 0, 0, false }; Circle(p, t); tris.push_back(t); }

   struct Edge { int a, b; };
   std::vector<Edge> poly;
   std::vector<int> bad;
   const double eps = 1e-12 * span * span;

   for (int idx : order)
   {
      const double px = p[idx * 2], py = p[idx * 2 + 1];
      bad.clear();
      bool dup = false;
      for (int ti = 0; ti < (int)tris.size(); ++ti)
      {
         const Tri& t = tris[ti];
         if (t.dead) continue;
         const double dx = px - t.cx, dy = py - t.cy;
         if (dx * dx + dy * dy < t.r2 * (1.0 - 1e-12))
         {
            bad.push_back(ti);
            for (int v : { t.a, t.b, t.c })
            {
               const double ex = p[v * 2] - px, ey = p[v * 2 + 1] - py;
               if (ex * ex + ey * ey <= eps) dup = true;
            }
         }
      }
      if (dup || bad.empty())
         continue;

      // Cavity boundary: edges that belong to exactly one bad triangle.
      poly.clear();
      for (int ti : bad)
      {
         const Tri& t = tris[ti];
         const int e[3][2] = { { t.a, t.b }, { t.b, t.c }, { t.c, t.a } };
         for (auto& ed : e)
         {
            bool shared = false;
            for (int tj : bad)
            {
               if (tj == ti) continue;
               const Tri& u = tris[tj];
               const int f[3][2] = { { u.a, u.b }, { u.b, u.c }, { u.c, u.a } };
               for (auto& fd : f)
                  if ((fd[0] == ed[0] && fd[1] == ed[1]) || (fd[0] == ed[1] && fd[1] == ed[0]))
                     shared = true;
               if (shared) break;
            }
            if (!shared) poly.push_back({ ed[0], ed[1] });
         }
      }
      for (int ti : bad) tris[ti].dead = true;

      for (const Edge& e : poly)
      {
         Tri t{ e.a, e.b, idx, 0, 0, 0, false };
         const double o = (p[e.b * 2] - p[e.a * 2]) * (py - p[e.a * 2 + 1]) -
                          (p[e.b * 2 + 1] - p[e.a * 2 + 1]) * (px - p[e.a * 2]);
         if (std::fabs(o) < 1e-300) continue; // sliver on a collinear run
         Circle(p, t);
         tris.push_back(t);
      }

      // Compact now and then so the linear scan does not wade through corpses.
      if (tris.size() > 4096 && bad.size() * 2 < tris.size())
      {
         size_t w = 0;
         for (size_t r = 0; r < tris.size(); ++r)
            if (!tris[r].dead) tris[w++] = tris[r];
         tris.resize(w);
      }
   }

   for (const Tri& t : tris)
   {
      if (t.dead) continue;
      if (t.a >= n || t.b >= n || t.c >= n) continue;
      const double o = (p[t.b * 2] - p[t.a * 2]) * (p[t.c * 2 + 1] - p[t.a * 2 + 1]) -
                       (p[t.b * 2 + 1] - p[t.a * 2 + 1]) * (p[t.c * 2] - p[t.a * 2]);
      if (o == 0.0) continue;
      out.push_back((unsigned)t.a);
      if (o > 0.0) { out.push_back((unsigned)t.b); out.push_back((unsigned)t.c); }
      else         { out.push_back((unsigned)t.c); out.push_back((unsigned)t.b); }
   }
   return out;
}

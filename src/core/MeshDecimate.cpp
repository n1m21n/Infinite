// MeshOps::Decimate - quadric error metric edge collapse (Garland & Heckbert,
// SIGGRAPH 1997), own implementation. Topology is computed on position-welded
// "groups" so a UV seam or a flat-shaded duplicate vertex never reads as a
// border; each face keeps its own original vertices (and so its UVs, colours
// and normals) and only their positions move.
#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <tuple>

namespace
{
   struct Quadric
   {
      double q[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // a2 ab ac ad b2 bc bd c2 cd d2

      void AddPlane(double a, double b, double c, double d, double w)
      {
         q[0] += w * a * a; q[1] += w * a * b; q[2] += w * a * c; q[3] += w * a * d;
         q[4] += w * b * b; q[5] += w * b * c; q[6] += w * b * d;
         q[7] += w * c * c; q[8] += w * c * d; q[9] += w * d * d;
      }
      void Add(const Quadric& o) { for (int i = 0; i < 10; i++) q[i] += o.q[i]; }
      double Eval(const double p[3]) const
      {
         const double x = p[0], y = p[1], z = p[2];
         return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x +
                q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y +
                q[7] * z * z + 2 * q[8] * z + q[9];
      }
   };

   struct Cand
   {
      double cost;
      int a, b;
      unsigned va, vb;
      double p[3];
      bool operator<(const Cand& o) const { return cost > o.cost; } // min-heap
   };
}

Mesh MeshOps::Decimate(const Mesh& in, float ratio, bool lockBorder)
{
   const size_t nv = in.vertices.size();
   const size_t nf = in.indices.size() / 3;
   ratio = std::min(std::max(ratio, 0.01f), 1.0f);
   if (nf < 4 || ratio >= 0.999f)
      return in;

   // ---- weld positions into groups
   double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
   for (const Vertex& v : in.vertices)
   {
      const double p[3] = { v.px, v.py, v.pz };
      for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
   }
   const double diag = std::max(std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                           (hi[2] - lo[2]) * (hi[2] - lo[2])), 1e-9);
   const double cell = diag * 1e-6;
   std::map<std::tuple<long long, long long, long long>, int> weld;
   std::vector<int> grp(nv);
   std::vector<std::array<double, 3>> pos;
   for (size_t i = 0; i < nv; i++)
   {
      const Vertex& v = in.vertices[i];
      const auto key = std::make_tuple((long long)std::llround(v.px / cell), (long long)std::llround(v.py / cell),
                                        (long long)std::llround(v.pz / cell));
      auto it = weld.find(key);
      if (it == weld.end())
      {
         it = weld.emplace(key, (int)pos.size()).first;
         pos.push_back({ v.px, v.py, v.pz });
      }
      grp[i] = it->second;
   }
   const int ng = (int)pos.size();

   // ---- faces: original vertex ids, group ids
   std::vector<std::array<unsigned, 3>> fv;
   std::vector<std::array<int, 3>> fg;
   for (size_t f = 0; f < nf; f++)
   {
      const unsigned a = in.indices[f * 3], b = in.indices[f * 3 + 1], c = in.indices[f * 3 + 2];
      if (a >= nv || b >= nv || c >= nv) continue;
      if (grp[a] == grp[b] || grp[b] == grp[c] || grp[a] == grp[c]) continue;
      fv.push_back({ a, b, c });
      fg.push_back({ grp[a], grp[b], grp[c] });
   }
   std::vector<char> alive(fv.size(), 1);
   size_t aliveCount = fv.size();
   const size_t target = std::max<size_t>(4, (size_t)std::llround((double)aliveCount * ratio));
   if (aliveCount <= target)
      return in;

   std::vector<std::vector<int>> vf(ng);
   for (size_t f = 0; f < fg.size(); f++)
      for (int k = 0; k < 3; k++) vf[fg[f][k]].push_back((int)f);

   auto triNormal = [&](int a, int b, int c, double n[3])
   {
      const auto &p = pos[a], &q = pos[b], &r = pos[c];
      const double u[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
      const double w[3] = { r[0] - p[0], r[1] - p[1], r[2] - p[2] };
      n[0] = u[1] * w[2] - u[2] * w[1];
      n[1] = u[2] * w[0] - u[0] * w[2];
      n[2] = u[0] * w[1] - u[1] * w[0];
   };

   // ---- border groups and quadrics
   std::map<std::pair<int, int>, int> edgeUse;
   for (const auto& g : fg)
      for (int k = 0; k < 3; k++)
         edgeUse[{ std::min(g[k], g[(k + 1) % 3]), std::max(g[k], g[(k + 1) % 3]) }]++;
   std::vector<char> border(ng, 0);
   std::vector<Quadric> Q(ng);
   for (size_t f = 0; f < fg.size(); f++)
   {
      double n[3];
      triNormal(fg[f][0], fg[f][1], fg[f][2], n);
      const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (len < 1e-18) continue;
      const double area = len * 0.5;
      const double nx = n[0] / len, ny = n[1] / len, nz = n[2] / len;
      const auto& p0 = pos[fg[f][0]];
      const double d = -(nx * p0[0] + ny * p0[1] + nz * p0[2]);
      for (int k = 0; k < 3; k++)
      {
         Q[fg[f][k]].AddPlane(nx, ny, nz, d, area);
         const int a = fg[f][k], b = fg[f][(k + 1) % 3];
         if (edgeUse[{ std::min(a, b), std::max(a, b) }] == 1)
         {
            border[a] = border[b] = 1;
            // Plane through the border edge, perpendicular to the face: keeps
            // an unlocked border from sliding inward.
            const double e[3] = { pos[b][0] - pos[a][0], pos[b][1] - pos[a][1], pos[b][2] - pos[a][2] };
            double m[3] = { e[1] * nz - e[2] * ny, e[2] * nx - e[0] * nz, e[0] * ny - e[1] * nx };
            const double ml = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            if (ml > 1e-18)
            {
               m[0] /= ml; m[1] /= ml; m[2] /= ml;
               const double dd = -(m[0] * pos[a][0] + m[1] * pos[a][1] + m[2] * pos[a][2]);
               const double w = 100.0 * (ml * ml);
               Q[a].AddPlane(m[0], m[1], m[2], dd, w);
               Q[b].AddPlane(m[0], m[1], m[2], dd, w);
            }
         }
      }
   }

   std::vector<char> gone(ng, 0);
   std::vector<unsigned> ver(ng, 0);
   std::vector<int> parent(ng);
   for (int i = 0; i < ng; i++) parent[i] = i;

   auto neighbours = [&](int g, std::vector<int>& out)
   {
      out.clear();
      for (int f : vf[g])
      {
         if (!alive[f]) continue;
         for (int k = 0; k < 3; k++)
            if (fg[f][k] != g) out.push_back(fg[f][k]);
      }
      std::sort(out.begin(), out.end());
      out.erase(std::unique(out.begin(), out.end()), out.end());
   };

   std::priority_queue<Cand> heap;
   auto push = [&](int a, int b)
   {
      if (lockBorder && (border[a] || border[b])) return;
      Quadric s = Q[a];
      s.Add(Q[b]);
      Cand c;
      c.a = a; c.b = b; c.va = ver[a]; c.vb = ver[b];
      const double mid[3] = { (pos[a][0] + pos[b][0]) * 0.5, (pos[a][1] + pos[b][1]) * 0.5, (pos[a][2] + pos[b][2]) * 0.5 };
      const double pa[3] = { pos[a][0], pos[a][1], pos[a][2] }, pb[3] = { pos[b][0], pos[b][1], pos[b][2] };
      const double ca = s.Eval(pa), cb = s.Eval(pb), cm = s.Eval(mid);
      const double* best = pa; c.cost = ca;
      if (cb < c.cost) { best = pb; c.cost = cb; }
      if (cm < c.cost) { best = mid; c.cost = cm; }
      for (int k = 0; k < 3; k++) c.p[k] = best[k];
      heap.push(c);
   };
   for (const auto& e : edgeUse) push(e.first.first, e.first.second);

   std::vector<int> na, nb;
   while (aliveCount > target && !heap.empty())
   {
      const Cand c = heap.top();
      heap.pop();
      if (gone[c.a] || gone[c.b] || ver[c.a] != c.va || ver[c.b] != c.vb) continue;

      // Link condition: the shared neighbours must be exactly the apexes of the
      // faces on the edge, or the collapse pinches the surface non-manifold.
      neighbours(c.a, na);
      neighbours(c.b, nb);
      int common = 0;
      for (int x : na) if (std::binary_search(nb.begin(), nb.end(), x)) common++;
      int sharedFaces = 0;
      for (int f : vf[c.a])
         if (alive[f] && (fg[f][0] == c.b || fg[f][1] == c.b || fg[f][2] == c.b)) sharedFaces++;
      if (common != sharedFaces) continue;

      // Reject collapses that fold a surviving face over or squash it flat.
      bool bad = false;
      auto checkSide = [&](int from)
      {
         for (int f : vf[from])
         {
            if (!alive[f]) continue;
            const auto& g = fg[f];
            if (g[0] == c.a || g[1] == c.a || g[2] == c.a)
               if (g[0] == c.b || g[1] == c.b || g[2] == c.b) continue; // dies
            double before[3], after[3];
            triNormal(g[0], g[1], g[2], before);
            const auto sa = pos[c.a], sb = pos[c.b];
            pos[c.a] = { c.p[0], c.p[1], c.p[2] };
            pos[c.b] = { c.p[0], c.p[1], c.p[2] };
            triNormal(g[0], g[1], g[2], after);
            pos[c.a] = sa; pos[c.b] = sb;
            const double lb = std::sqrt(before[0] * before[0] + before[1] * before[1] + before[2] * before[2]);
            const double la = std::sqrt(after[0] * after[0] + after[1] * after[1] + after[2] * after[2]);
            if (lb < 1e-18) continue;
            if (la < lb * 1e-3 ||
                (before[0] * after[0] + before[1] * after[1] + before[2] * after[2]) < 0.2 * lb * la)
            { bad = true; return; }
         }
      };
      checkSide(c.a);
      if (!bad) checkSide(c.b);
      if (bad) continue;

      // Collapse a into b at the chosen point.
      for (int f : vf[c.a])
      {
         if (!alive[f]) continue;
         auto& g = fg[f];
         const bool hasB = g[0] == c.b || g[1] == c.b || g[2] == c.b;
         if (hasB) { alive[f] = 0; aliveCount--; continue; }
         for (int k = 0; k < 3; k++) if (g[k] == c.a) g[k] = c.b;
         vf[c.b].push_back(f);
      }
      pos[c.b] = { c.p[0], c.p[1], c.p[2] };
      Q[c.b].Add(Q[c.a]);
      border[c.b] = border[c.a] || border[c.b];
      gone[c.a] = 1;
      parent[c.a] = c.b;
      ver[c.b]++;
      vf[c.a].clear();

      neighbours(c.b, nb);
      for (int x : nb) push(std::min(c.b, x), std::max(c.b, x));
   }

   auto root = [&](int g)
   {
      while (parent[g] != g) g = parent[g];
      return g;
   };

   // ---- emit: original vertices, moved to their group's final position
   Mesh out;
   std::vector<int> remap(nv, -1);
   const bool hasColor = in.vertexColor.size() == nv * 3;
   for (size_t f = 0; f < fv.size(); f++)
   {
      if (!alive[f]) continue;
      for (int k = 0; k < 3; k++)
      {
         const unsigned ov = fv[f][k];
         if (remap[ov] < 0)
         {
            Vertex v = in.vertices[ov];
            const auto& p = pos[root(grp[ov])];
            v.px = (float)p[0]; v.py = (float)p[1]; v.pz = (float)p[2];
            remap[ov] = (int)out.vertices.size();
            out.vertices.push_back(v);
            if (hasColor)
            {
               out.vertexColor.push_back(in.vertexColor[ov * 3]);
               out.vertexColor.push_back(in.vertexColor[ov * 3 + 1]);
               out.vertexColor.push_back(in.vertexColor[ov * 3 + 2]);
            }
         }
         out.indices.push_back((unsigned)remap[ov]);
      }
   }
   return out;
}

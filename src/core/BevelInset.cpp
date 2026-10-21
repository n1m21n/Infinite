#include "Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

// Edge bevel and face inset (v0.6 3.7).
//
// Both work on "regions": groups of triangles joined across edges that are not sharp, so a
// flat face made of two triangles, or a smooth patch, is one unit. A bevel pulls every region
// away from the sharp edges that bound it by `width` (in the region's own plane, mitred at
// corners), then fills the gap: a strip of `segments` quads along each sharp edge (the cross
// section is a quadratic curve through the old corner, so 1 segment is a chamfer and more
// segments round it) and a patch at each vertex where three or more regions meet. An inset
// does the same to the boundary of a selected region only, and lifts it by `depth`.
//
// Topology is decided on vertices welded by position, because a flat-shaded primitive keeps
// separate vertices per face. Vertices the result needs are new ones, so normals can be flat on
// a chamfer and smooth on a round.

namespace
{
   struct V3
   {
      double x = 0, y = 0, z = 0;
   };

   V3 operator+(const V3& a, const V3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
   V3 operator-(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
   V3 operator*(const V3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
   double Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   V3 Cross(const V3& a, const V3& b)
   {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
   }
   double Length(const V3& a) { return std::sqrt(Dot(a, a)); }
   V3 Normalized(const V3& a, const V3& fallback)
   {
      const double l = Length(a);
      return l > 1e-12 ? a * (1.0 / l) : fallback;
   }

   V3 Pos(const Vertex& v) { return { v.px, v.py, v.pz }; }

   struct Topo
   {
      const Mesh* in = nullptr;
      size_t tris = 0;
      std::vector<int> weld;                 // vertex -> welded id
      std::vector<V3> faceN;                 // unit face normal per triangle
      std::map<std::pair<int, int>, std::vector<int>> edgeTris;   // welded (lo, hi) -> triangles
   };

   std::pair<int, int> EdgeKey(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

   void BuildTopo(const Mesh& in, Topo& t)
   {
      t.in = &in;
      t.tris = in.indices.size() / 3;
      double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
      for (const Vertex& v : in.vertices)
      {
         lo[0] = std::min<double>(lo[0], v.px); hi[0] = std::max<double>(hi[0], v.px);
         lo[1] = std::min<double>(lo[1], v.py); hi[1] = std::max<double>(hi[1], v.py);
         lo[2] = std::min<double>(lo[2], v.pz); hi[2] = std::max<double>(hi[2], v.pz);
      }
      const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                    (hi[2] - lo[2]) * (hi[2] - lo[2]));
      const double q = 1.0 / std::max(1e-9, diag * 1e-5);
      std::map<std::array<long long, 3>, int> ids;
      t.weld.resize(in.vertices.size());
      for (size_t i = 0; i < in.vertices.size(); i++)
      {
         const Vertex& v = in.vertices[i];
         const std::array<long long, 3> key = { std::llround(v.px * q), std::llround(v.py * q),
                                                 std::llround(v.pz * q) };
         auto it = ids.find(key);
         if (it == ids.end())
            it = ids.emplace(key, (int)ids.size()).first;
         t.weld[i] = it->second;
      }
      t.faceN.resize(t.tris);
      for (size_t f = 0; f < t.tris; f++)
      {
         const unsigned* ix = &in.indices[f * 3];
         const V3 a = Pos(in.vertices[ix[0]]), b = Pos(in.vertices[ix[1]]), c = Pos(in.vertices[ix[2]]);
         t.faceN[f] = Normalized(Cross(b - a, c - a), { 0, 1, 0 });
         const int w[3] = { t.weld[ix[0]], t.weld[ix[1]], t.weld[ix[2]] };
         if (w[0] == w[1] || w[1] == w[2] || w[0] == w[2])
            continue;   // collapsed triangle: no edges
         for (int e = 0; e < 3; e++)
            t.edgeTris[EdgeKey(w[e], w[(e + 1) % 3])].push_back((int)f);
      }
   }

   struct DSU
   {
      std::vector<int> p;
      explicit DSU(size_t n) : p(n) { for (size_t i = 0; i < n; i++) p[i] = (int)i; }
      int Find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
      void Join(int a, int b) { p[Find(a)] = Find(b); }
   };

   // Region id (dense, 0..) for each triangle, -1 for a triangle not allowed in any region.
   // Two allowed triangles share a region when they meet across a manifold edge whose
   // dihedral angle is within `cosLimit`.
   int BuildRegions(const Topo& t, const std::vector<char>& allowed, double cosLimit, std::vector<int>& rid)
   {
      DSU dsu(t.tris);
      for (const auto& kv : t.edgeTris)
      {
         if (kv.second.size() != 2)
            continue;
         const int a = kv.second[0], b = kv.second[1];
         if (!allowed[a] || !allowed[b])
            continue;
         if (Dot(t.faceN[a], t.faceN[b]) >= cosLimit)
            dsu.Join(a, b);
      }
      rid.assign(t.tris, -1);
      std::map<int, int> dense;
      for (size_t f = 0; f < t.tris; f++)
      {
         if (!allowed[f])
            continue;
         const int r = dsu.Find((int)f);
         auto it = dense.find(r);
         if (it == dense.end())
            it = dense.emplace(r, (int)dense.size()).first;
         rid[f] = it->second;
      }
      return (int)dense.size();
   }

   // The edge p -> q as triangle f winds it, in welded ids and original vertex indices.
   struct DirEdge
   {
      int wp, wq;
      unsigned ip, iq;
   };

   bool FindDirEdge(const Topo& t, int f, int wa, int wb, DirEdge& out)
   {
      const unsigned* ix = &t.in->indices[(size_t)f * 3];
      for (int e = 0; e < 3; e++)
      {
         const unsigned i0 = ix[e], i1 = ix[(e + 1) % 3];
         const int w0 = t.weld[i0], w1 = t.weld[i1];
         if ((w0 == wa && w1 == wb) || (w0 == wb && w1 == wa))
         {
            out = { w0, w1, i0, i1 };
            return true;
         }
      }
      return false;
   }

   // Mitred in-plane shift for a region vertex from the inward directions of the boundary
   // edges meeting there, so each adjacent boundary edge moves inward by exactly `w`.
   V3 MitreShift(const std::vector<V3>& dirs, double w)
   {
      if (dirs.empty())
         return {};
      const V3 d1 = dirs[0];
      for (size_t i = 1; i < dirs.size(); i++)
      {
         const V3 d2 = dirs[i];
         const double c = Dot(d1, d2);
         if (c > 0.999 || c < -0.999)
            continue;
         V3 s = (d1 + d2) * (w / (1.0 + c));
         const double len = Length(s);
         if (len > 3.0 * w)
            s = s * (3.0 * w / len);
         return s;
      }
      return d1 * w;
   }

   unsigned AddVertex(Mesh& out, const Mesh& in, unsigned srcIdx, const V3& p, const V3& n)
   {
      Vertex v = in.vertices[srcIdx];
      v.px = (float)p.x; v.py = (float)p.y; v.pz = (float)p.z;
      v.nx = (float)n.x; v.ny = (float)n.y; v.nz = (float)n.z;
      out.vertices.push_back(v);
      if (!in.vertexColor.empty())
      {
         out.vertexColor.push_back(in.vertexColor[srcIdx * 3]);
         out.vertexColor.push_back(in.vertexColor[srcIdx * 3 + 1]);
         out.vertexColor.push_back(in.vertexColor[srcIdx * 3 + 2]);
      }
      return (unsigned)out.vertices.size() - 1;
   }

   void AddTri(Mesh& out, unsigned a, unsigned b, unsigned c)
   {
      out.indices.push_back(a);
      out.indices.push_back(b);
      out.indices.push_back(c);
   }

   Mesh CopyShell(const Mesh& in)
   {
      Mesh out;
      out.vertices = in.vertices;
      out.vertexColor = in.vertexColor;
      return out;
   }
}

namespace MeshOps
{
   Mesh BevelEdges(const Mesh& in, float width, int segments, float angleDeg)
   {
      segments = std::max(1, std::min(32, segments));
      if (in.Empty() || !(width > 1e-7f) || !std::isfinite(width))
         return in;

      Topo t;
      BuildTopo(in, t);
      std::vector<char> allowed(t.tris, 1);
      std::vector<int> rid;
      const double cosLimit = std::cos(std::max(1.0f, std::min(179.0f, angleDeg)) * 3.14159265358979 / 180.0);
      BuildRegions(t, allowed, cosLimit, rid);

      // Sharp edges: manifold edges that separate two different regions.
      struct Sharp { int wa, wb, t1, t2; };
      std::vector<Sharp> sharp;
      double minLen = 1e30;
      for (const auto& kv : t.edgeTris)
      {
         if (kv.second.size() != 2)
            continue;
         const int a = kv.second[0], b = kv.second[1];
         if (rid[a] == rid[b])
            continue;
         sharp.push_back({ kv.first.first, kv.first.second, a, b });
         // length from any vertex pair carrying these welded ids
         DirEdge de;
         if (FindDirEdge(t, a, kv.first.first, kv.first.second, de))
            minLen = std::min(minLen, Length(Pos(in.vertices[de.ip]) - Pos(in.vertices[de.iq])));
      }
      if (sharp.empty() || minLen < 1e-9)
         return in;
      const double w = std::min<double>(width, 0.45 * minLen);

      // Inward in-plane direction of each sharp edge, per (region, welded vertex).
      std::map<std::pair<int, int>, std::vector<V3>> inward;
      for (const Sharp& s : sharp)
      {
         for (int side = 0; side < 2; side++)
         {
            const int f = side == 0 ? s.t1 : s.t2;
            DirEdge de;
            if (!FindDirEdge(t, f, s.wa, s.wb, de))
               continue;
            const V3 e = Normalized(Pos(in.vertices[de.iq]) - Pos(in.vertices[de.ip]), { 1, 0, 0 });
            const V3 d = Normalized(Cross(t.faceN[f], e), { 0, 0, 0 });
            inward[{ rid[f], s.wa }].push_back(d);
            inward[{ rid[f], s.wb }].push_back(d);
         }
      }
      std::map<std::pair<int, int>, V3> shift;
      for (const auto& kv : inward)
         shift[kv.first] = MitreShift(kv.second, w);

      Mesh out;
      // Region triangles on their own (possibly shifted) copies of the vertices.
      std::map<std::pair<int, unsigned>, unsigned> regionVert;
      std::map<std::pair<int, int>, unsigned> repVert;   // (region, welded) -> a vertex there
      for (size_t f = 0; f < t.tris; f++)
      {
         unsigned tri[3];
         for (int c = 0; c < 3; c++)
         {
            const unsigned oi = in.indices[f * 3 + c];
            const std::pair<int, unsigned> key = { rid[f], oi };
            auto it = regionVert.find(key);
            if (it == regionVert.end())
            {
               V3 p = Pos(in.vertices[oi]);
               auto sh = shift.find({ rid[f], t.weld[oi] });
               if (sh != shift.end())
                  p = p + sh->second;
               const Vertex& ov = in.vertices[oi];
               const unsigned ni = AddVertex(out, in, oi, p, { ov.nx, ov.ny, ov.nz });
               it = regionVert.emplace(key, ni).first;
               repVert.emplace(std::make_pair(rid[f], t.weld[oi]), ni);
            }
            tri[c] = it->second;
         }
         AddTri(out, tri[0], tri[1], tri[2]);
      }

      auto regionNormal = [&](int r, int wid) -> V3 {
         const Vertex& v = out.vertices[repVert[{ r, wid }]];
         return Normalized({ v.nx, v.ny, v.nz }, { 0, 1, 0 });
      };

      // One curve per (vertex, region pair), stored from the lower region id to the higher.
      std::map<std::array<int, 3>, std::vector<unsigned>> arcs;
      auto getArc = [&](int wid, int from, int to) -> std::vector<unsigned> {
         const bool flip = from > to;
         const int ra = flip ? to : from, rb = flip ? from : to;
         const std::array<int, 3> key = { wid, ra, rb };
         auto it = arcs.find(key);
         if (it == arcs.end())
         {
            const unsigned ia = repVert[{ ra, wid }], ib = repVert[{ rb, wid }];
            const V3 a = Pos(out.vertices[ia]), b = Pos(out.vertices[ib]);
            const Vertex& ov = out.vertices[ia];
            // the old corner, back from either side's shift
            V3 corner = a;
            {
               auto sa = shift.find({ ra, wid });
               if (sa != shift.end())
                  corner = a - sa->second;
            }
            const V3 na = regionNormal(ra, wid), nb = regionNormal(rb, wid);
            std::vector<unsigned> pts;
            for (int k = 0; k <= segments; k++)
            {
               const double u = (double)k / segments;
               const V3 p = a * ((1 - u) * (1 - u)) + corner * (2 * (1 - u) * u) + b * (u * u);
               V3 n;
               if (segments == 1)
                  n = Normalized(na + nb, na);
               else
                  n = Normalized(na * (1 - u) + nb * u, na);
               // attributes (uv) from the nearer region vertex
               const unsigned src = u < 0.5 ? ia : ib;
               (void)ov;
               pts.push_back(AddVertex(out, out, src, p, n));
            }
            it = arcs.emplace(key, std::move(pts)).first;
         }
         std::vector<unsigned> r = it->second;
         if (flip)
            std::reverse(r.begin(), r.end());
         return r;
      };

      // Strips along every sharp edge.
      std::map<int, std::vector<std::pair<int, int>>> cornerPairs;   // welded vertex -> region pairs
      for (const Sharp& s : sharp)
      {
         DirEdge de;
         if (!FindDirEdge(t, s.t1, s.wa, s.wb, de))
            continue;
         const int r1 = rid[s.t1], r2 = rid[s.t2];
         const std::vector<unsigned> P = getArc(de.wp, r1, r2);
         const std::vector<unsigned> Q = getArc(de.wq, r1, r2);
         for (int k = 0; k < segments; k++)
         {
            AddTri(out, Q[k], P[k], P[k + 1]);
            AddTri(out, Q[k], P[k + 1], Q[k + 1]);
         }
         for (int wv : { s.wa, s.wb })
            cornerPairs[wv].push_back({ std::min(r1, r2), std::max(r1, r2) });
      }

      // Patches where three or more regions meet at a vertex.
      for (auto& kv : cornerPairs)
      {
         const int wid = kv.first;
         std::vector<std::pair<int, int>>& pairs = kv.second;
         std::sort(pairs.begin(), pairs.end());
         pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
         std::map<int, std::vector<int>> adj;
         for (const auto& pr : pairs)
         {
            adj[pr.first].push_back(pr.second);
            adj[pr.second].push_back(pr.first);
         }
         if (adj.size() < 3)
            continue;
         bool ok = true;
         for (const auto& a : adj)
            if (a.second.size() != 2)
               ok = false;
         if (!ok)
            continue;
         std::vector<int> cycle;
         int prev = -1, cur = adj.begin()->first;
         for (size_t guard = 0; guard <= adj.size(); guard++)
         {
            cycle.push_back(cur);
            const std::vector<int>& nb = adj[cur];
            const int nxt = (nb[0] != prev) ? nb[0] : nb[1];
            prev = cur;
            cur = nxt;
            if (cur == cycle[0])
               break;
         }
         if (cycle.size() != adj.size() || cur != cycle[0])
            continue;
         std::vector<unsigned> ring;
         for (size_t i = 0; i < cycle.size(); i++)
         {
            const std::vector<unsigned> arc = getArc(wid, cycle[i], cycle[(i + 1) % cycle.size()]);
            for (size_t k = 0; k + 1 < arc.size(); k++)
               ring.push_back(arc[k]);
         }
         // Winding: the patch faces the way the regions around it do.
         V3 nsum, newell;
         for (int r : cycle)
            nsum = nsum + regionNormal(r, wid);
         for (size_t i = 0; i < ring.size(); i++)
         {
            const V3 a = Pos(out.vertices[ring[i]]), b = Pos(out.vertices[ring[(i + 1) % ring.size()]]);
            newell = newell + Cross(a, b);
         }
         if (Dot(newell, nsum) < 0)
            std::reverse(ring.begin(), ring.end());
         if (ring.size() == 3)
         {
            AddTri(out, ring[0], ring[1], ring[2]);
            continue;
         }
         V3 centre;
         for (unsigned i : ring)
            centre = centre + Pos(out.vertices[i]);
         centre = centre * (1.0 / ring.size());
         const unsigned ci = AddVertex(out, out, ring[0], centre, Normalized(nsum, { 0, 1, 0 }));
         for (size_t i = 0; i < ring.size(); i++)
            AddTri(out, ci, ring[i], ring[(i + 1) % ring.size()]);
      }
      return out;
   }

   Mesh InsetFaces(const Mesh& in, float width, float depth, float angleDeg)
   {
      if (in.Empty() || !(width > 1e-7f) || !std::isfinite(width) || !std::isfinite(depth))
         return in;
      Topo t;
      BuildTopo(in, t);
      std::vector<char> allowed(t.tris, 1);
      if (!in.faceMask.empty())
         for (size_t f = 0; f < t.tris; f++)
            allowed[f] = f < in.faceMask.size() ? (in.faceMask[f] != 0) : 0;
      std::vector<int> rid;
      const double cosLimit = std::cos(std::max(1.0f, std::min(179.0f, angleDeg)) * 3.14159265358979 / 180.0);
      const int regions = BuildRegions(t, allowed, cosLimit, rid);
      if (regions == 0)
         return in;

      // Boundary edges of each region: an edge on one of its triangles whose other side is not
      // the same region (another face, a different region, or nothing).
      struct Bound { int f; int wa, wb; };
      std::vector<Bound> bounds;
      double minLen = 1e30;
      for (const auto& kv : t.edgeTris)
      {
         const std::vector<int>& tr = kv.second;
         for (int f : tr)
         {
            if (rid[f] < 0)
               continue;
            bool inside = false;
            if (tr.size() == 2)
            {
               const int o = tr[0] == f ? tr[1] : tr[0];
               inside = rid[o] == rid[f];
            }
            if (inside)
               continue;
            bounds.push_back({ f, kv.first.first, kv.first.second });
            DirEdge de;
            if (FindDirEdge(t, f, kv.first.first, kv.first.second, de))
               minLen = std::min(minLen, Length(Pos(in.vertices[de.ip]) - Pos(in.vertices[de.iq])));
         }
      }
      if (bounds.empty() || minLen < 1e-9)
         return in;
      const double w = std::min<double>(width, 0.45 * minLen);

      std::map<std::pair<int, int>, std::vector<V3>> inward;
      std::vector<V3> regionN(regions);
      for (size_t f = 0; f < t.tris; f++)
         if (rid[f] >= 0)
            regionN[rid[f]] = regionN[rid[f]] + t.faceN[f];
      for (V3& n : regionN)
         n = Normalized(n, { 0, 1, 0 });
      for (const Bound& b : bounds)
      {
         DirEdge de;
         if (!FindDirEdge(t, b.f, b.wa, b.wb, de))
            continue;
         const V3 e = Normalized(Pos(in.vertices[de.iq]) - Pos(in.vertices[de.ip]), { 1, 0, 0 });
         const V3 d = Normalized(Cross(t.faceN[b.f], e), { 0, 0, 0 });
         inward[{ rid[b.f], b.wa }].push_back(d);
         inward[{ rid[b.f], b.wb }].push_back(d);
      }
      std::map<std::pair<int, int>, V3> shift;
      for (const auto& kv : inward)
         shift[kv.first] = MitreShift(kv.second, w);

      Mesh out = CopyShell(in);
      const bool hadMask = !in.faceMask.empty();
      // Unselected and unregioned triangles stay as they are.
      std::vector<unsigned char> mask;
      for (size_t f = 0; f < t.tris; f++)
      {
         if (rid[f] >= 0)
            continue;
         AddTri(out, in.indices[f * 3], in.indices[f * 3 + 1], in.indices[f * 3 + 2]);
         mask.push_back(0);
      }
      std::map<std::pair<int, unsigned>, unsigned> inner;
      for (size_t f = 0; f < t.tris; f++)
      {
         if (rid[f] < 0)
            continue;
         unsigned tri[3];
         for (int c = 0; c < 3; c++)
         {
            const unsigned oi = in.indices[f * 3 + c];
            const std::pair<int, unsigned> key = { rid[f], oi };
            auto it = inner.find(key);
            if (it == inner.end())
            {
               V3 p = Pos(in.vertices[oi]);
               auto sh = shift.find({ rid[f], t.weld[oi] });
               if (sh != shift.end())
                  p = p + sh->second;
               p = p + regionN[rid[f]] * (double)depth;
               const Vertex& ov = in.vertices[oi];
               it = inner.emplace(key, AddVertex(out, in, oi, p, { ov.nx, ov.ny, ov.nz })).first;
            }
            tri[c] = it->second;
         }
         AddTri(out, tri[0], tri[1], tri[2]);
         mask.push_back(1);
      }
      // Wall between the old outline and the inset one.
      for (const Bound& b : bounds)
      {
         DirEdge de;
         if (!FindDirEdge(t, b.f, b.wa, b.wb, de))
            continue;
         const unsigned ip2 = inner[{ rid[b.f], de.ip }], iq2 = inner[{ rid[b.f], de.iq }];
         const V3 po = Pos(in.vertices[de.ip]), qo = Pos(in.vertices[de.iq]);
         const V3 pi = Pos(out.vertices[ip2]), qi = Pos(out.vertices[iq2]);
         V3 n = Normalized(Cross(po - pi, qi - pi), t.faceN[b.f]);
         const unsigned a = AddVertex(out, in, de.ip, po, n);
         const unsigned b2 = AddVertex(out, in, de.iq, qo, n);
         const unsigned c = AddVertex(out, in, de.ip, pi, n);
         const unsigned d = AddVertex(out, in, de.iq, qi, n);
         // wall reverses the inner edge p' -> q'
         AddTri(out, d, c, a);
         AddTri(out, d, a, b2);
         mask.push_back(0);
         mask.push_back(0);
      }
      if (hadMask)
         out.faceMask = std::move(mask);
      return out;
   }
}

#include "Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "Base64.h"

namespace Sculpt
{
   namespace
   {
      // Smooth falloff from the brush centre (1) to its rim (0).
      float Falloff(float t)
      {
         const float f = std::clamp(1.0f - t, 0.0f, 1.0f);
         return f * f * (3.0f - 2.0f * f);
      }

      void Normalize(float v[3])
      {
         const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
         if (l > 1e-12f)
            for (int k = 0; k < 3; k++)
               v[k] /= l;
      }

      // The symmetry passes a dab runs: every mix of the enabled axes.
      std::vector<int> Passes(int symmetry)
      {
         std::vector<int> out;
         for (int m = 0; m < 8; m++)
            if ((m & ~symmetry) == 0)
               out.push_back(m);
         return out;
      }

      void Flip(const float in[3], int m, float out[3])
      {
         for (int k = 0; k < 3; k++)
            out[k] = (m >> k & 1) ? -in[k] : in[k];
      }

      // The brush image's brightness under point p for a dab centred at c
      // (symmetry pass m: the offset is mirrored back first), 1 without one.
      float AlphaAt(const Dab& d, const float p[3], const float c[3], int m)
      {
         if (d.alpha == nullptr || d.alpha->lum.empty())
            return 1.0f;
         float off[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
         Flip(off, m, off);
         const float x = off[0] * d.right[0] + off[1] * d.right[1] + off[2] * d.right[2];
         const float y = off[0] * d.up[0] + off[1] * d.up[1] + off[2] * d.up[2];
         return d.alpha->Sample(0.5f + 0.5f * x / d.radius, 0.5f + 0.5f * y / d.radius);
      }

      // The points under a brush, with how strongly each is touched, and the
      // surface's average normal and centre there (Blender's "area" plane).
      struct Area
      {
         std::vector<unsigned int> idx;
         std::vector<float> f;
         float normal[3] = { 0, 0, 0 };
         float center[3] = { 0, 0, 0 };
      };

      uint64_t CellKey(int x, int y, int z)
      {
         return (uint64_t)(x + (1 << 20)) & 0x1FFFFF | ((uint64_t)(y + (1 << 20)) & 0x1FFFFF) << 21 |
                ((uint64_t)(z + (1 << 20)) & 0x1FFFFF) << 42;
      }
      int CellOf(float v, float cell) { return (int)std::floor(v / cell); }

      // Calls f(point) for every point that can be within r of c: the grid's
      // cells around c, or every point when there is no grid (or r spans
      // too many cells).
      template <class F>
      void ForNear(const Grid* g, const float c[3], float r, int points, F f)
      {
         if (g != nullptr && g->cell > 0.0f)
         {
            const float reach = r;
            int lo[3], hi[3];
            long count = 1;
            for (int k = 0; k < 3; k++)
            {
               lo[k] = CellOf(c[k] - reach, g->cell);
               hi[k] = CellOf(c[k] + reach, g->cell);
               count *= hi[k] - lo[k] + 1;
            }
            if (count <= 4096)
            {
               for (int x = lo[0]; x <= hi[0]; x++)
                  for (int y = lo[1]; y <= hi[1]; y++)
                     for (int z = lo[2]; z <= hi[2]; z++)
                     {
                        auto it = g->cells.find(CellKey(x, y, z));
                        if (it != g->cells.end())
                           for (unsigned int i : it->second)
                              f(i);
                     }
               return;
            }
         }
         for (int i = 0; i < points; i++)
            f((unsigned int)i);
      }

      Area Gather(const float c[3], float radius, const std::vector<float>& pos, const std::vector<float>& nrm,
                  const Topology& t, bool sharp, const Grid* grid, const std::vector<float>* areaNrm = nullptr)
      {
         const std::vector<float>& an = areaNrm != nullptr && areaNrm->size() == nrm.size() ? *areaNrm : nrm;
         Area a;
         float wsum = 0.0f;
         const float r2 = radius * radius;
         ForNear(grid, c, radius, t.points, [&](unsigned int i)
         {
            const float* p = &pos[(size_t)i * 3];
            const float d[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
            const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (dd >= r2)
               return;
            float f = Falloff(std::sqrt(dd) / radius);
            if (sharp)
               f *= f;
            for (int k = 0; k < 3; k++)
            {
               a.normal[k] += an[(size_t)i * 3 + k] * f;
               a.center[k] += p[k] * f;
            }
            wsum += f;
            if (!t.mask.empty() && t.mask[i] == 0)
               return;
            a.idx.push_back(i);
            a.f.push_back(f);
         });
         Normalize(a.normal);
         if (wsum > 0.0f)
            for (int k = 0; k < 3; k++)
               a.center[k] /= wsum;
         return a;
      }
   }

   float Alpha::Sample(float u, float v) const
   {
      if (w <= 0 || h <= 0 || u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f)
         return 0.0f;
      const float x = std::clamp(u * w - 0.5f, 0.0f, (float)(w - 1)), y = std::clamp(v * h - 0.5f, 0.0f, (float)(h - 1));
      const int x0 = (int)x, y0 = (int)y, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
      const float fx = x - x0, fy = y - y0;
      const float a = lum[(size_t)y0 * w + x0] * (1 - fx) + lum[(size_t)y0 * w + x1] * fx;
      const float b = lum[(size_t)y1 * w + x0] * (1 - fx) + lum[(size_t)y1 * w + x1] * fx;
      return a * (1 - fy) + b * fy;
   }

   const char* BrushName(int brush)
   {
      static const char* kNames[] = { "Draw", "Smooth", "Grab", "Inflate", "Pinch", "Flatten", "Crease" };
      return brush >= 0 && brush < kBrushCount ? kNames[brush] : "";
   }

   void Build(const Mesh& base, Topology& t)
   {
      if (t.vertexCount == base.vertices.size() && t.indices == base.indices)
      {
         // Same triangles: only the mask can have moved.
         t.mask.assign(t.points, base.faceMask.empty() ? 1 : 0);
         if (!base.faceMask.empty())
            for (size_t f = 0; f < base.FaceCount(); f++)
               if (base.FaceSelected(f))
                  for (int c = 0; c < 3; c++)
                     t.mask[t.point[base.indices[f * 3 + c]]] = 1;
         return;
      }
      t = Topology();
      t.indices = base.indices;
      t.vertexCount = base.vertices.size();
      const size_t nv = base.vertices.size();

      // Weld by position, quantised against the mesh's size.
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      for (const Vertex& v : base.vertices)
      {
         const float p[3] = { v.px, v.py, v.pz };
         for (int k = 0; k < 3; k++)
         {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
         }
      }
      const float size = std::max({ hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-6f });
      const float q = 1e5f / size;
      std::unordered_map<uint64_t, unsigned int> seen;
      seen.reserve(nv);
      t.point.resize(nv);
      for (size_t i = 0; i < nv; i++)
      {
         const Vertex& v = base.vertices[i];
         const uint64_t x = (uint64_t)(int64_t)std::llround((v.px - lo[0]) * q) & 0x1FFFFF;
         const uint64_t y = (uint64_t)(int64_t)std::llround((v.py - lo[1]) * q) & 0x1FFFFF;
         const uint64_t z = (uint64_t)(int64_t)std::llround((v.pz - lo[2]) * q) & 0x1FFFFF;
         const uint64_t key = x | y << 21 | z << 42;
         auto it = seen.emplace(key, (unsigned int)t.points);
         if (it.second)
            t.points++;
         t.point[i] = it.first->second;
      }

      // Per point: its vertices, and the faces with a corner there (CSR).
      auto prefix = [](std::vector<unsigned int>& start) {
         for (size_t p = 1; p < start.size(); p++)
            start[p] += start[p - 1];
      };
      t.pointVertStart.assign(t.points + 1, 0);
      for (size_t i = 0; i < nv; i++)
         t.pointVertStart[t.point[i] + 1]++;
      prefix(t.pointVertStart);
      t.pointVert.resize(nv);
      std::vector<unsigned int> fill(t.pointVertStart.begin(), t.pointVertStart.end() - 1);
      for (size_t i = 0; i < nv; i++)
         t.pointVert[fill[t.point[i]]++] = (unsigned int)i;
      const size_t nf = base.FaceCount();
      auto valid = [&](size_t f) {
         const unsigned int* tri = &base.indices[f * 3];
         return tri[0] < nv && tri[1] < nv && tri[2] < nv;
      };
      t.pointFaceStart.assign(t.points + 1, 0);
      for (size_t f = 0; f < nf; f++)
         if (valid(f))
            for (int c = 0; c < 3; c++)
               t.pointFaceStart[t.point[base.indices[f * 3 + c]] + 1]++;
      prefix(t.pointFaceStart);
      t.pointFace.resize(t.pointFaceStart.back());
      std::vector<unsigned int> cornerVert(t.pointFace.size());
      // The other two corners of each (point, face), in the same order, so
      // the neighbours below sort in place instead of chasing faces.
      std::vector<unsigned int> other(t.pointFace.size() * 2);
      fill.assign(t.pointFaceStart.begin(), t.pointFaceStart.end() - 1);
      for (size_t f = 0; f < nf; f++)
         if (valid(f))
         {
            const unsigned int fp[3] = { t.point[base.indices[f * 3]], t.point[base.indices[f * 3 + 1]],
                                         t.point[base.indices[f * 3 + 2]] };
            for (int c = 0; c < 3; c++)
            {
               const unsigned int at = fill[fp[c]]++;
               t.pointFace[at] = (unsigned int)f;
               cornerVert[at] = base.indices[f * 3 + c];
               other[(size_t)at * 2] = fp[(c + 1) % 3];
               other[(size_t)at * 2 + 1] = fp[(c + 2) % 3];
            }
         }

      // Neighbours: the other corners of a point's faces.
      t.nbrStart.assign(t.points + 1, 0);
      t.nbr.reserve(other.size() / 2);
      for (int p = 0; p < t.points; p++)
      {
         const auto first = other.begin() + (size_t)t.pointFaceStart[p] * 2;
         const auto last = other.begin() + (size_t)t.pointFaceStart[p + 1] * 2;
         std::sort(first, last);
         for (auto it = first; it != last; ++it)
            if (*it != (unsigned int)p && (it == first || *it != *(it - 1)))
               t.nbr.push_back(*it);
         t.nbrStart[p + 1] = (unsigned int)t.nbr.size();
      }

      // Each vertex shades from the faces at its point whose corner normal
      // agrees with its own.
      t.shadeStart.assign(nv + 1, 0);
      t.shadeFace.reserve(t.pointFace.size());
      for (size_t i = 0; i < nv; i++)
      {
         const Vertex& v = base.vertices[i];
         const unsigned int p = t.point[i];
         for (unsigned int s = t.pointFaceStart[p]; s < t.pointFaceStart[p + 1]; s++)
         {
            const Vertex& w = base.vertices[cornerVert[s]];
            if (cornerVert[s] == i || v.nx * w.nx + v.ny * w.ny + v.nz * w.nz > 0.5f)
               t.shadeFace.push_back(t.pointFace[s]);
         }
         t.shadeStart[i + 1] = (unsigned int)t.shadeFace.size();
      }
      t.mask.assign(t.points, base.faceMask.empty() ? 1 : 0);
      if (!base.faceMask.empty())
         for (size_t f = 0; f < nf; f++)
            if (base.FaceSelected(f))
               for (int c = 0; c < 3; c++)
                  t.mask[t.point[base.indices[f * 3 + c]]] = 1;
   }

   namespace
   {
      void FaceNormal(const Mesh& m, size_t f, float n[3])
      {
         const unsigned int* tri = &m.indices[f * 3];
         const Vertex &a = m.vertices[tri[0]], &b = m.vertices[tri[1]], &c = m.vertices[tri[2]];
         const float e1[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz };
         const float e2[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         n[0] = e1[1] * e2[2] - e1[2] * e2[1];
         n[1] = e1[2] * e2[0] - e1[0] * e2[2];
         n[2] = e1[0] * e2[1] - e1[1] * e2[0];
      }

      // A vertex's normal from its shading faces' (area-weighted) normals,
      // kept on the side its base normal faces.
      void SetNormal(const Mesh& base, size_t i, float n[3], Mesh& out)
      {
         const Vertex& b = base.vertices[i];
         if (n[0] * b.nx + n[1] * b.ny + n[2] * b.nz < 0.0f && (b.nx != 0.0f || b.ny != 0.0f || b.nz != 0.0f))
            for (int k = 0; k < 3; k++)
               n[k] = -n[k];
         const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
         if (l > 1e-20f)
         {
            out.vertices[i].nx = n[0] / l;
            out.vertices[i].ny = n[1] / l;
            out.vertices[i].nz = n[2] / l;
         }
      }

      // Marks that reset in O(1): a value is marked when it holds the epoch.
      struct Marks
      {
         std::vector<unsigned int> v;
         unsigned int epoch = 0;
         void Next(size_t n)
         {
            if (v.size() != n || ++epoch == 0)
            {
               v.assign(n, 0);
               epoch = 1;
            }
         }
         bool Mark(size_t i)
         {
            if (v[i] == epoch)
               return false;
            v[i] = epoch;
            return true;
         }
      };
   }

   void Apply(const Mesh& base, const Topology& t, const std::vector<float>& offsets, float amount, Mesh& out)
   {
      out = base;
      if (amount == 0.0f || offsets.size() != (size_t)t.points * 3 || t.point.size() != base.vertices.size())
         return;
      if (std::all_of(offsets.begin(), offsets.end(), [](float v) { return v == 0.0f; }))
         return;
      for (size_t i = 0; i < out.vertices.size(); i++)
      {
         const float* o = &offsets[(size_t)t.point[i] * 3];
         out.vertices[i].px += o[0] * amount;
         out.vertices[i].py += o[1] * amount;
         out.vertices[i].pz += o[2] * amount;
      }
      const size_t nf = out.FaceCount();
      std::vector<float> fn(nf * 3, 0.0f);
      for (size_t f = 0; f < nf; f++)
      {
         const unsigned int* tri = &out.indices[f * 3];
         if (tri[0] >= out.vertices.size() || tri[1] >= out.vertices.size() || tri[2] >= out.vertices.size())
            continue;
         const Vertex &a = out.vertices[tri[0]], &b = out.vertices[tri[1]], &c = out.vertices[tri[2]];
         const float e1[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz };
         const float e2[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         fn[f * 3] = e1[1] * e2[2] - e1[2] * e2[1];
         fn[f * 3 + 1] = e1[2] * e2[0] - e1[0] * e2[2];
         fn[f * 3 + 2] = e1[0] * e2[1] - e1[1] * e2[0];
      }
      for (size_t i = 0; i < out.vertices.size(); i++)
      {
         float n[3] = { 0, 0, 0 };
         for (unsigned int s = t.shadeStart[i]; s < t.shadeStart[i + 1]; s++)
            for (int k = 0; k < 3; k++)
               n[k] += fn[(size_t)t.shadeFace[s] * 3 + k];
         SetNormal(base, i, n, out);
      }
   }

   bool ApplyPoints(const Mesh& base, const Topology& t, const std::vector<float>& offsets, float amount,
                    const std::vector<unsigned int>& points, Mesh& out, std::vector<unsigned int>& changed)
   {
      changed.clear();
      if (out.vertices.size() != base.vertices.size() || out.indices.size() != base.indices.size() ||
          offsets.size() != (size_t)t.points * 3 || t.point.size() != base.vertices.size() ||
          t.pointVertStart.size() != (size_t)t.points + 1)
         return false;
      thread_local Marks pointMarks, faceMarks;
      thread_local std::vector<unsigned int> faces;
      faces.clear();
      pointMarks.Next(t.points);
      faceMarks.Next(base.FaceCount());
      for (unsigned int p : points)
      {
         if (p >= (unsigned int)t.points || !pointMarks.Mark(p))
            continue;
         const float* o = &offsets[(size_t)p * 3];
         for (unsigned int s = t.pointVertStart[p]; s < t.pointVertStart[p + 1]; s++)
         {
            const unsigned int i = t.pointVert[s];
            out.vertices[i].px = base.vertices[i].px + o[0] * amount;
            out.vertices[i].py = base.vertices[i].py + o[1] * amount;
            out.vertices[i].pz = base.vertices[i].pz + o[2] * amount;
         }
         for (unsigned int s = t.pointFaceStart[p]; s < t.pointFaceStart[p + 1]; s++)
            if (faceMarks.Mark(t.pointFace[s]))
               faces.push_back(t.pointFace[s]);
      }
      // Every corner of a moved face shades from it.
      pointMarks.Next(t.points);
      for (unsigned int f : faces)
         for (int c = 0; c < 3; c++)
         {
            const unsigned int p = t.point[base.indices[(size_t)f * 3 + c]];
            if (pointMarks.Mark(p))
               changed.push_back(p);
         }
      for (unsigned int p : changed)
         for (unsigned int s = t.pointVertStart[p]; s < t.pointVertStart[p + 1]; s++)
         {
            const unsigned int i = t.pointVert[s];
            float n[3] = { 0, 0, 0 };
            for (unsigned int e = t.shadeStart[i]; e < t.shadeStart[i + 1]; e++)
            {
               float fn[3];
               FaceNormal(out, t.shadeFace[e], fn);
               for (int k = 0; k < 3; k++)
                  n[k] += fn[k];
            }
            SetNormal(base, i, n, out);
         }
      return true;
   }

   void PointFrameAt(const Mesh& shaped, const Topology& t, const std::vector<unsigned int>& points,
                     std::vector<float>& pos, std::vector<float>& nrm)
   {
      if (pos.size() != (size_t)t.points * 3 || nrm.size() != pos.size() || t.point.size() != shaped.vertices.size())
         return;
      for (unsigned int p : points)
      {
         float* q = &pos[(size_t)p * 3];
         float* n = &nrm[(size_t)p * 3];
         n[0] = n[1] = n[2] = 0.0f;
         for (unsigned int s = t.pointVertStart[p]; s < t.pointVertStart[p + 1]; s++)
         {
            const Vertex& v = shaped.vertices[t.pointVert[s]];
            q[0] = v.px;
            q[1] = v.py;
            q[2] = v.pz;
            n[0] += v.nx;
            n[1] += v.ny;
            n[2] += v.nz;
         }
         Normalize(n);
      }
   }

   void BuildGrid(const std::vector<float>& pos, float cell, Grid& g)
   {
      g = Grid();
      g.cell = std::max(cell, 1e-6f);
      g.at = pos;
      g.cells.reserve(pos.size() / 12 + 1);
      for (int k = 0; k < 3; k++)
      {
         g.lo[k] = 1e30f;
         g.hi[k] = -1e30f;
      }
      for (size_t i = 0; i < pos.size(); i++)
      {
         g.lo[i % 3] = std::min(g.lo[i % 3], pos[i] - g.cell);
         g.hi[i % 3] = std::max(g.hi[i % 3], pos[i] + g.cell);
      }
      for (size_t i = 0; i + 2 < pos.size(); i += 3)
         g.cells[CellKey(CellOf(pos[i], g.cell), CellOf(pos[i + 1], g.cell), CellOf(pos[i + 2], g.cell))].push_back(
            (unsigned int)(i / 3));
   }

   void MoveInGrid(const std::vector<float>& pos, const std::vector<unsigned int>& points, Grid& g)
   {
      if (g.cell <= 0.0f || g.at.size() != pos.size())
         return;
      for (unsigned int p : points)
      {
         const float* q = &pos[(size_t)p * 3];
         float* was = &g.at[(size_t)p * 3];
         const uint64_t from = CellKey(CellOf(was[0], g.cell), CellOf(was[1], g.cell), CellOf(was[2], g.cell));
         const uint64_t to = CellKey(CellOf(q[0], g.cell), CellOf(q[1], g.cell), CellOf(q[2], g.cell));
         for (int k = 0; k < 3; k++)
         {
            was[k] = q[k];
            g.lo[k] = std::min(g.lo[k], q[k] - g.cell);
            g.hi[k] = std::max(g.hi[k], q[k] + g.cell);
         }
         if (from == to)
            continue;
         auto it = g.cells.find(from);
         if (it != g.cells.end())
         {
            std::vector<unsigned int>& v = it->second;
            auto e = std::find(v.begin(), v.end(), p);
            if (e != v.end())
            {
               *e = v.back();
               v.pop_back();
            }
         }
         g.cells[to].push_back(p);
      }
   }

   namespace
   {
      // Möller-Trumbore; true with t when the ray hits face f ahead of o.
      bool HitFace(const Mesh& m, size_t f, const float o[3], const float d[3], float& t)
      {
         const unsigned int* tri = &m.indices[f * 3];
         const Vertex &a = m.vertices[tri[0]], &b = m.vertices[tri[1]], &c = m.vertices[tri[2]];
         const float e1[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz }, e2[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         const float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
         const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
         if (std::fabs(det) < 1e-12f)
            return false;
         const float inv = 1.0f / det;
         const float s[3] = { o[0] - a.px, o[1] - a.py, o[2] - a.pz };
         const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
         // A little slack, so a ray through a shared corner or edge hits one of its faces.
         const float e = 1e-5f;
         if (u < -e || u > 1.0f + e)
            return false;
         const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
         const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
         if (v < -e || u + v > 1.0f + e)
            return false;
         t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
         return t > 0.0f;
      }
   }

   bool Raycast(const Mesh& m, const float o[3], const float d[3], float& best, const Topology* topo, const Grid* grid)
   {
      best = 1e30f;
      const size_t nv = m.vertices.size();
      const bool walk = topo != nullptr && grid != nullptr && grid->cell > 0.0f && topo->point.size() == nv &&
                        topo->pointFaceStart.size() == (size_t)topo->points + 1;
      if (!walk)
      {
         float t;
         for (size_t f = 0; f * 3 + 2 < m.indices.size(); f++)
            if (m.indices[f * 3] < nv && m.indices[f * 3 + 1] < nv && m.indices[f * 3 + 2] < nv && HitFace(m, f, o, d, t) &&
                t < best)
               best = t;
         return best < 1e30f;
      }
      // March the ray through the grid in half cells, testing the faces of
      // the points in the cells around each step (the grid's cells are at
      // least two edges wide, so a face hit at a step has its corners
      // there). The first hit is final once the march has passed it.
      float lo[3], hi[3];
      for (int a = 0; a < 3; a++)
      {
         lo[a] = grid->lo[a];
         hi[a] = grid->hi[a];
      }
      float t0 = 0.0f, t1 = 1e30f;
      for (int a = 0; a < 3; a++)
      {
         if (std::fabs(d[a]) < 1e-20f)
         {
            if (o[a] < lo[a] || o[a] > hi[a])
               return false;
            continue;
         }
         float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
         if (ta > tb)
            std::swap(ta, tb);
         t0 = std::max(t0, ta);
         t1 = std::min(t1, tb);
      }
      if (t0 > t1)
         return false;
      const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      const float step = grid->cell * 0.5f / std::max(len, 1e-20f);
      thread_local Marks faceMarks;
      faceMarks.Next(m.FaceCount());
      std::unordered_map<uint64_t, bool> seen;
      for (float t = t0; t <= t1 + step; t += step)
      {
         if (best < t - step)
            break;
         const float p[3] = { o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t };
         const int c[3] = { CellOf(p[0], grid->cell), CellOf(p[1], grid->cell), CellOf(p[2], grid->cell) };
         for (int x = c[0] - 1; x <= c[0] + 1; x++)
            for (int y = c[1] - 1; y <= c[1] + 1; y++)
               for (int z = c[2] - 1; z <= c[2] + 1; z++)
               {
                  const uint64_t key = CellKey(x, y, z);
                  if (!seen.emplace(key, true).second)
                     continue;
                  auto it = grid->cells.find(key);
                  if (it == grid->cells.end())
                     continue;
                  for (unsigned int q : it->second)
                     for (unsigned int s = topo->pointFaceStart[q]; s < topo->pointFaceStart[q + 1]; s++)
                     {
                        const unsigned int f = topo->pointFace[s];
                        float th;
                        if (faceMarks.Mark(f) && HitFace(m, f, o, d, th) && th < best)
                           best = th;
                     }
               }
      }
      return best < 1e30f;
   }

   void PointFrame(const Mesh& shaped, const Topology& t, std::vector<float>& pos, std::vector<float>& nrm)
   {
      pos.assign((size_t)t.points * 3, 0.0f);
      nrm.assign((size_t)t.points * 3, 0.0f);
      if (t.point.size() != shaped.vertices.size())
         return;
      for (size_t i = 0; i < shaped.vertices.size(); i++)
      {
         const Vertex& v = shaped.vertices[i];
         const size_t p = (size_t)t.point[i] * 3;
         pos[p] = v.px;
         pos[p + 1] = v.py;
         pos[p + 2] = v.pz;
         nrm[p] += v.nx;
         nrm[p + 1] += v.ny;
         nrm[p + 2] += v.nz;
      }
      for (int p = 0; p < t.points; p++)
         Normalize(&nrm[(size_t)p * 3]);
   }

   void Stroke(const Dab& d, const std::vector<float>& pos, const std::vector<float>& nrm, const Topology& t,
               float amount, std::vector<float>& offsets, std::vector<unsigned int>* touched, const Grid* grid)
   {
      if (amount < 0.01f || d.radius <= 0.0f || pos.size() != (size_t)t.points * 3 || offsets.size() != pos.size())
         return;
      const float s = std::clamp(d.strength, 0.0f, 1.0f) / amount;
      const float sign = d.invert ? -1.0f : 1.0f;
      const float st = std::clamp(d.strength, 0.0f, 1.0f);
      for (int m : Passes(d.symmetry))
      {
         float c[3];
         Flip(d.center, m, c);
         Area a = Gather(c, d.radius, pos, nrm, t, false, grid, d.areaNormals);
         // Pinch gathers towards the line along the stroke: the pull is across it (and along the normal).
         float across[3] = { 0, 0, 0 };
         if (d.brush == kPinch)
         {
            float dir[3];
            Flip(d.dir, m, dir);
            across[0] = a.normal[1] * dir[2] - a.normal[2] * dir[1];
            across[1] = a.normal[2] * dir[0] - a.normal[0] * dir[2];
            across[2] = a.normal[0] * dir[1] - a.normal[1] * dir[0];
            const float len = std::sqrt(across[0] * across[0] + across[1] * across[1] + across[2] * across[2]);
            if (len < 1e-9f)
               continue;
            for (int k = 0; k < 3; k++)
               across[k] /= len;
         }
         if (d.alpha != nullptr)
            for (size_t e = 0; e < a.idx.size(); e++)
               a.f[e] *= AlphaAt(d, &pos[(size_t)a.idx[e] * 3], c, m);
         if (touched != nullptr)
            touched->insert(touched->end(), a.idx.begin(), a.idx.end());
         for (size_t e = 0; e < a.idx.size(); e++)
         {
            const unsigned int i = a.idx[e];
            const float f = a.f[e];
            const float* p = &pos[(size_t)i * 3];
            const float* n = &nrm[(size_t)i * 3];
            float* o = &offsets[(size_t)i * 3];
            float move[3] = { 0, 0, 0 };
            switch (d.brush)
            {
               case kDraw:
                  // Blender: radius * strength squared along the area normal.
                  for (int k = 0; k < 3; k++)
                     move[k] = a.normal[k] * d.radius * st * st * f * sign;
                  break;
               case kInflate:
                  // Blender: a quarter of draw's, and half that again when deflating.
                  for (int k = 0; k < 3; k++)
                     move[k] = n[k] * d.radius * 0.25f * st * st * f * sign * (sign < 0.0f ? 0.5f : 1.0f);
                  break;
               case kSmooth:
               {
                  const unsigned int b0 = t.nbrStart[i], b1 = t.nbrStart[i + 1];
                  if (b1 == b0)
                     break;
                  float avg[3] = { 0, 0, 0 };
                  for (unsigned int b = b0; b < b1; b++)
                     for (int k = 0; k < 3; k++)
                        avg[k] += pos[(size_t)t.nbr[b] * 3 + k];
                  const float w = std::min(1.0f, f * std::clamp(d.strength, 0.0f, 1.0f) * 3.3f);
                  for (int k = 0; k < 3; k++)
                     move[k] = (avg[k] / (float)(b1 - b0) - p[k]) * w;
                  break;
               }
               case kPinch:
               {
                  const float to[3] = { c[0] - p[0], c[1] - p[1], c[2] - p[2] };
                  const float x = to[0] * across[0] + to[1] * across[1] + to[2] * across[2];
                  const float z = to[0] * a.normal[0] + to[1] * a.normal[1] + to[2] * a.normal[2];
                  for (int k = 0; k < 3; k++)
                     move[k] = (across[k] * x + a.normal[k] * z) * d.strength * f * sign;
                  break;
               }
               case kCrease:
               {
                  float to[3] = { c[0] - p[0], c[1] - p[1], c[2] - p[2] };
                  const float along = to[0] * a.normal[0] + to[1] * a.normal[1] + to[2] * a.normal[2];
                  for (int k = 0; k < 3; k++)
                     to[k] -= a.normal[k] * along;
                  // Blender: the pinch is the brush's pinch factor squared whatever the strength, the dig
                  // is radius * strength squared. Both are in final units, so Stroke does not scale them.
                  for (int k = 0; k < 3; k++)
                     move[k] = (to[k] * 0.25f - a.normal[k] * d.radius * st * st * sign) * f;
                  break;
               }
               case kFlatten:
               {
                  const float h = (p[0] - a.center[0]) * a.normal[0] + (p[1] - a.center[1]) * a.normal[1] +
                                  (p[2] - a.center[2]) * a.normal[2];
                  for (int k = 0; k < 3; k++)
                     move[k] = -a.normal[k] * h * (0.64f * st) * f * sign;
                  break;
               }
               default:
                  break;
            }
            // Smooth already scales by strength above; the rest scale here.
            const float scale = d.brush == kSmooth || d.brush == kCrease || d.brush == kDraw || d.brush == kInflate || d.brush == kFlatten ? 1.0f / amount : s;
            for (int k = 0; k < 3; k++)
               o[k] += move[k] * scale;
         }
      }
   }

   void GrabBegin(const Dab& d, const std::vector<float>& pos, const Topology& t, const std::vector<float>& offsets,
                  GrabHold& hold)
   {
      hold = GrabHold();
      hold.start = offsets;
      if (pos.size() != (size_t)t.points * 3)
         return;
      const float r2 = d.radius * d.radius;
      for (int m : Passes(d.symmetry))
      {
         float c[3];
         Flip(d.center, m, c);
         for (int i = 0; i < t.points; i++)
         {
            if (!t.mask.empty() && t.mask[i] == 0)
               continue;
            const float* p = &pos[(size_t)i * 3];
            const float dd = (p[0] - c[0]) * (p[0] - c[0]) + (p[1] - c[1]) * (p[1] - c[1]) + (p[2] - c[2]) * (p[2] - c[2]);
            if (dd >= r2)
               continue;
            hold.idx.push_back((unsigned int)i);
            hold.weight.push_back(Falloff(std::sqrt(dd) / d.radius) * std::clamp(d.strength * 2.0f, 0.0f, 1.0f) *
                                  AlphaAt(d, p, c, m));
            hold.flip.push_back(m);
         }
      }
   }

   void GrabMove(const GrabHold& hold, const float move[3], float amount, std::vector<float>& offsets)
   {
      if (amount < 0.01f || hold.start.size() != offsets.size())
         return;
      offsets = hold.start;
      for (size_t e = 0; e < hold.idx.size(); e++)
      {
         float mv[3];
         Flip(move, hold.flip[e], mv);
         float* o = &offsets[(size_t)hold.idx[e] * 3];
         for (int k = 0; k < 3; k++)
            o[k] += mv[k] * hold.weight[e] / amount;
      }
   }

   // Layout: "S1", points (u32), scale (f32), count (u32), then per moved
   // point its index (u32) and xyz as int16 times scale.
   std::string Encode(const std::vector<float>& offsets, int points)
   {
      float big = 0.0f;
      uint32_t moved = 0;
      for (int i = 0; i < points && (size_t)i * 3 + 2 < offsets.size(); i++)
      {
         const float* o = &offsets[(size_t)i * 3];
         const float m = std::max({ std::fabs(o[0]), std::fabs(o[1]), std::fabs(o[2]) });
         big = std::max(big, m);
         moved += m > 0.0f ? 1 : 0;
      }
      if (moved == 0)
         return std::string();
      const float scale = big / 32767.0f;
      std::vector<uint8_t> b;
      auto put = [&](const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
      put("S1", 2);
      const uint32_t np = (uint32_t)points;
      put(&np, 4);
      put(&scale, 4);
      const size_t countAt = b.size();
      put(&moved, 4);
      uint32_t kept = 0;
      for (int i = 0; i < points && (size_t)i * 3 + 2 < offsets.size(); i++)
      {
         const float* o = &offsets[(size_t)i * 3];
         int16_t q[3];
         for (int k = 0; k < 3; k++)
            q[k] = (int16_t)std::clamp(std::lround(o[k] / scale), -32767L, 32767L);
         if (q[0] == 0 && q[1] == 0 && q[2] == 0)
            continue;
         const uint32_t idx = (uint32_t)i;
         put(&idx, 4);
         put(q, 6);
         kept++;
      }
      std::memcpy(&b[countAt], &kept, 4);
      return Base64::Encode(b.data(), b.size());
   }

   bool Decode(const std::string& s, std::vector<float>& offsets, int& points)
   {
      offsets.clear();
      points = 0;
      if (s.empty())
         return true;
      std::vector<uint8_t> b;
      if (!Base64::Decode(s, b) || b.size() < 14 || b[0] != 'S' || b[1] != '1')
         return false;
      uint32_t np, count;
      float scale;
      std::memcpy(&np, &b[2], 4);
      std::memcpy(&scale, &b[6], 4);
      std::memcpy(&count, &b[10], 4);
      if (np > 50000000u || b.size() < 14 + (size_t)count * 10)
         return false;
      offsets.assign((size_t)np * 3, 0.0f);
      for (uint32_t e = 0; e < count; e++)
      {
         const uint8_t* r = &b[14 + (size_t)e * 10];
         uint32_t idx;
         int16_t q[3];
         std::memcpy(&idx, r, 4);
         std::memcpy(q, r + 4, 6);
         if (idx >= np)
            return false;
         for (int k = 0; k < 3; k++)
            offsets[(size_t)idx * 3 + k] = q[k] * scale;
      }
      points = (int)np;
      return true;
   }
}

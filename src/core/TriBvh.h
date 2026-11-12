#pragma once

// A plain triangle BVH and the hemisphere directions the bakes shoot through it: shared by the Bake node (texture
// bakes) and the per-vertex wear bake the 3D engines read (MeshWear.h).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace tribvh
{
   struct V3
   {
      float x = 0, y = 0, z = 0;
   };
   inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
   inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
   inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
   inline V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
   inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   inline V3 Norm(V3 a)
   {
      const float l = std::sqrt(Dot(a, a));
      return l > 1e-12f ? a * (1.0f / l) : V3{ 0, 1, 0 };
   }

   // A plain median-split BVH over triangles: enough for the few thousand
   // to few hundred thousand triangles a bake sees.
   struct Bvh
   {
      struct Node
      {
         V3 lo, hi;
         int left = -1, right = -1, first = 0, count = 0;
      };
      std::vector<Node> nodes;
      std::vector<int> tris;
      std::vector<V3> a, e1, e2;

      // Triangles from a flat position list (3 floats a vertex, stride floats apart) and an index list.
      void Build(const float* pos, size_t stride, const unsigned int* idx, size_t indexCount)
      {
         const int n = (int)(indexCount / 3);
         a.resize(n); e1.resize(n); e2.resize(n); tris.resize(n);
         std::vector<V3> centre(n);
         auto P = [&](unsigned int i) { const float* q = pos + (size_t)i * stride; return V3{ q[0], q[1], q[2] }; };
         for (int f = 0; f < n; f++)
         {
            const V3 p0 = P(idx[f * 3]), p1 = P(idx[f * 3 + 1]), p2 = P(idx[f * 3 + 2]);
            a[f] = p0; e1[f] = p1 - p0; e2[f] = p2 - p0;
            centre[f] = (p0 + p1 + p2) * (1.0f / 3.0f);
            tris[f] = f;
         }
         nodes.clear();
         nodes.reserve(n * 2 + 1);
         if (n > 0)
            Split(0, n, centre);
      }

      // The split axis comes from the centroids' extent; a node's box is its leaves' triangles or its children's
      // union, so each triangle's corners are read once.
      int Split(int first, int count, const std::vector<V3>& centre)
      {
         const int index = (int)nodes.size();
         nodes.push_back(Node{});
         V3 lo = { 1e30f, 1e30f, 1e30f }, hi = { -1e30f, -1e30f, -1e30f };
         auto grow = [&](V3 p) {
            lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
            hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
         };
         if (count <= 4)
         {
            for (int i = first; i < first + count; i++)
            {
               const int f = tris[i];
               grow(a[f]); grow(a[f] + e1[f]); grow(a[f] + e2[f]);
            }
            nodes[index].lo = lo; nodes[index].hi = hi;
            nodes[index].first = first;
            nodes[index].count = count;
            return index;
         }
         for (int i = first; i < first + count; i++)
            grow(centre[tris[i]]);
         const V3 ext = hi - lo;
         const int axis = (ext.x > ext.y && ext.x > ext.z) ? 0 : (ext.y > ext.z ? 1 : 2);
         auto key = [&](int f) { return axis == 0 ? centre[f].x : axis == 1 ? centre[f].y : centre[f].z; };
         const int mid = first + count / 2;
         std::nth_element(tris.begin() + first, tris.begin() + mid, tris.begin() + first + count,
                          [&](int l, int r) { return key(l) < key(r); });
         const int left = Split(first, mid - first, centre);
         const int right = Split(mid, first + count - mid, centre);
         const Node &l = nodes[left], &r = nodes[right];
         nodes[index].lo = { std::min(l.lo.x, r.lo.x), std::min(l.lo.y, r.lo.y), std::min(l.lo.z, r.lo.z) };
         nodes[index].hi = { std::max(l.hi.x, r.hi.x), std::max(l.hi.y, r.hi.y), std::max(l.hi.z, r.hi.z) };
         nodes[index].left = left;
         nodes[index].right = right;
         return index;
      }

      static bool HitBox(const Node& n, V3 o, V3 inv, float tMax)
      {
         float t0 = 0.0f, t1 = tMax;
         const float lo[3] = { n.lo.x, n.lo.y, n.lo.z }, hi[3] = { n.hi.x, n.hi.y, n.hi.z };
         const float oo[3] = { o.x, o.y, o.z }, ii[3] = { inv.x, inv.y, inv.z };
         for (int k = 0; k < 3; k++)
         {
            float ta = (lo[k] - oo[k]) * ii[k], tb = (hi[k] - oo[k]) * ii[k];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1)
               return false;
         }
         return true;
      }

      // Nearest hit distance below tMax, or tMax. anyHit stops at the first.
      float Cast(V3 o, V3 d, float tMax, bool anyHit) const
      {
         if (nodes.empty())
            return tMax;
         const V3 inv = { 1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f),
                          1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f),
                          1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f) };
         int stack[64];
         int sp = 0;
         stack[sp++] = 0;
         float best = tMax;
         while (sp > 0)
         {
            const Node& n = nodes[stack[--sp]];
            if (!HitBox(n, o, inv, best))
               continue;
            if (n.left < 0)
            {
               for (int i = n.first; i < n.first + n.count; i++)
               {
                  const int f = tris[i];
                  // Moller-Trumbore, both sides.
                  const V3 p = Cross(d, e2[f]);
                  const float det = Dot(e1[f], p);
                  if (std::fabs(det) < 1e-12f)
                     continue;
                  const float id = 1.0f / det;
                  const V3 s = o - a[f];
                  const float u = Dot(s, p) * id;
                  if (u < 0.0f || u > 1.0f)
                     continue;
                  const V3 q = Cross(s, e1[f]);
                  const float v = Dot(d, q) * id;
                  if (v < 0.0f || u + v > 1.0f)
                     continue;
                  const float t = Dot(e2[f], q) * id;
                  if (t > 0.0f && t < best)
                  {
                     best = t;
                     if (anyHit)
                        return best;
                  }
               }
            }
            else if (sp < 62)
            {
               stack[sp++] = n.left;
               stack[sp++] = n.right;
            }
         }
         return best;
      }
   };

   // Low-discrepancy cosine-weighted directions about n, turned per texel so
   // neighbouring texels do not band.
   inline V3 HemiDir(V3 n, int i, int count, float turn)
   {
      const float u1 = ((float)i + 0.5f) / (float)count;
      unsigned b = (unsigned)i;
      b = (b << 16u) | (b >> 16u);
      b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
      b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
      b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
      b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
      const float u2 = std::fmod((float)b * 2.3283064e-10f + turn, 1.0f);
      const float r = std::sqrt(u1), phi = 6.2831853f * u2;
      const V3 up = std::fabs(n.y) < 0.99f ? V3{ 0, 1, 0 } : V3{ 1, 0, 0 };
      const V3 t = Norm(Cross(up, n)), bt = Cross(n, t);
      return Norm(t * (r * std::cos(phi)) + bt * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - u1)));
   }

}

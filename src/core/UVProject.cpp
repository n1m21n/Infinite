// UV Project (MeshOps::UVProject): writes real UVs from a projection. Every
// corner of every triangle gets a UV, then vertices are split wherever two
// corners that shared one now disagree, so seams are true seams.
//
// Smart Project follows Blender's Smart UV Project, written fresh: pick
// projection directions from the face normals (largest faces first, a new
// direction when no existing one is within the angle limit), give each face
// the nearest direction, split into islands of edge-connected faces that share
// a direction, flatten each island onto its plane and shelf-pack the islands
// into the unit square.

#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

namespace
{
   struct V3
   {
      float x = 0, y = 0, z = 0;
   };
   V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
   V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
   float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   float Len(V3 a) { return std::sqrt(Dot(a, a)); }
   V3 Norm(V3 a)
   {
      const float l = Len(a);
      return l > 1e-12f ? V3{ a.x / l, a.y / l, a.z / l } : V3{ 0, 1, 0 };
   }
   V3 Pos(const Vertex& v) { return { v.px, v.py, v.pz }; }

   // A plane basis for a projection direction, so u and v run along the two
   // axes perpendicular to it (world up kept as v where it can be).
   void Basis(V3 n, V3& t, V3& b)
   {
      const V3 up = std::fabs(n.y) < 0.99f ? V3{ 0, 1, 0 } : V3{ 0, 0, 1 };
      t = Norm(Cross(up, n));
      b = Cross(n, t);
   }

   // Positions welded by value, so faces split for flat shading still count
   // as edge neighbours.
   std::vector<unsigned> WeldIds(const Mesh& m)
   {
      std::map<std::array<long long, 3>, unsigned> ids;
      std::vector<unsigned> out(m.vertices.size());
      for (size_t i = 0; i < m.vertices.size(); i++)
      {
         const Vertex& v = m.vertices[i];
         const std::array<long long, 3> k = { std::llround(v.px * 1e5), std::llround(v.py * 1e5),
                                              std::llround(v.pz * 1e5) };
         out[i] = ids.emplace(k, (unsigned)ids.size()).first->second;
      }
      return out;
   }
}

namespace MeshOps
{
   Mesh UVProject(const Mesh& in, int mode, float angleDeg, float margin, float scale, std::vector<unsigned>* from)
   {
      if (from != nullptr)
         from->clear();
      if (in.Empty())
         return in;
      const size_t faces = in.FaceCount();
      std::vector<float> uv(faces * 6, 0.0f);
      std::vector<V3> fn(faces);
      std::vector<float> area(faces);
      for (size_t f = 0; f < faces; f++)
      {
         const V3 a = Pos(in.vertices[in.indices[f * 3]]);
         const V3 b = Pos(in.vertices[in.indices[f * 3 + 1]]);
         const V3 c = Pos(in.vertices[in.indices[f * 3 + 2]]);
         const V3 cr = Cross(Sub(b, a), Sub(c, a));
         area[f] = Len(cr) * 0.5f;
         fn[f] = Norm(cr);
      }
      V3 lo = Pos(in.vertices[0]), hi = lo;
      for (const Vertex& v : in.vertices)
      {
         lo = { std::min(lo.x, v.px), std::min(lo.y, v.py), std::min(lo.z, v.pz) };
         hi = { std::max(hi.x, v.px), std::max(hi.y, v.py), std::max(hi.z, v.pz) };
      }
      const V3 mid = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
      const float size = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f });
      const float kPi = 3.14159265f;

      if (mode == kUVCube)
      {
         // Each face onto the box side its normal points at, 1 UV unit per box size.
         for (size_t f = 0; f < faces; f++)
         {
            const V3 n = fn[f];
            const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
            for (int k = 0; k < 3; k++)
            {
               const V3 p = Sub(Pos(in.vertices[in.indices[f * 3 + k]]), lo);
               float u, v;
               // Mirrored sides flip within the box, so every UV stays in 0-1.
               const V3 e = Sub(hi, lo);
               if (ax >= ay && ax >= az) { u = n.x > 0 ? e.z - p.z : p.z; v = p.y; }
               else if (ay >= az)        { u = p.x; v = n.y > 0 ? e.z - p.z : p.z; }
               else                      { u = n.z > 0 ? p.x : e.x - p.x; v = p.y; }
               uv[f * 6 + k * 2] = u / size;
               uv[f * 6 + k * 2 + 1] = v / size;
            }
         }
      }
      else if (mode == kUVCylinder || mode == kUVSphere)
      {
         // Around Y through the bounds centre; a face that straddles the back
         // seam has its small u values lifted by 1 so it does not smear.
         for (size_t f = 0; f < faces; f++)
         {
            float us[3];
            for (int k = 0; k < 3; k++)
            {
               const V3 p = Sub(Pos(in.vertices[in.indices[f * 3 + k]]), mid);
               us[k] = std::atan2(p.x, p.z) / (2.0f * kPi) + 0.5f;
               float v;
               if (mode == kUVCylinder)
                  v = (p.y + (hi.y - lo.y) * 0.5f) / std::max(hi.y - lo.y, 1e-6f);
               else
                  v = std::asin(std::clamp(p.y / std::max(Len(p), 1e-6f), -1.0f, 1.0f)) / kPi + 0.5f;
               uv[f * 6 + k * 2 + 1] = v;
            }
            const float maxU = std::max({ us[0], us[1], us[2] });
            for (int k = 0; k < 3; k++)
               uv[f * 6 + k * 2] = (maxU - us[k] > 0.5f) ? us[k] + 1.0f : us[k];
            // At a pole every u is right; use the other two corners' mean.
            if (mode == kUVSphere)
               for (int k = 0; k < 3; k++)
               {
                  const V3 p = Sub(Pos(in.vertices[in.indices[f * 3 + k]]), mid);
                  if (std::fabs(p.x) + std::fabs(p.z) < 1e-5f * size)
                     uv[f * 6 + k * 2] = (uv[f * 6 + ((k + 1) % 3) * 2] + uv[f * 6 + ((k + 2) % 3) * 2]) * 0.5f;
               }
         }
      }
      else
      {
         // Smart: projection directions, largest faces first.
         const float cosLimit = std::cos(std::clamp(angleDeg, 1.0f, 89.0f) * kPi / 180.0f);
         std::vector<size_t> order(faces);
         for (size_t f = 0; f < faces; f++)
            order[f] = f;
         std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return area[a] > area[b]; });
         std::vector<V3> dirs;
         std::vector<int> faceDir(faces, -1);
         for (size_t f : order)
         {
            if (area[f] <= 0.0f)
               continue;
            int best = -1;
            float bestDot = cosLimit;
            for (size_t d = 0; d < dirs.size(); d++)
            {
               const float c = Dot(dirs[d], fn[f]);
               if (c >= bestDot) { bestDot = c; best = (int)d; }
            }
            if (best < 0)
            {
               best = (int)dirs.size();
               dirs.push_back(fn[f]);
            }
            faceDir[f] = best;
         }
         for (size_t f = 0; f < faces; f++)
            if (faceDir[f] < 0)
               faceDir[f] = 0;
         if (dirs.empty())
            dirs.push_back({ 0, 1, 0 });

         // Islands: faces that share a welded edge and a direction.
         const std::vector<unsigned> weld = WeldIds(in);
         std::vector<int> parent(faces);
         for (size_t f = 0; f < faces; f++)
            parent[f] = (int)f;
         auto find = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
         std::unordered_map<unsigned long long, size_t> edgeFace;
         for (size_t f = 0; f < faces; f++)
            for (int k = 0; k < 3; k++)
            {
               unsigned a = weld[in.indices[f * 3 + k]], b = weld[in.indices[f * 3 + (k + 1) % 3]];
               if (a > b) std::swap(a, b);
               const unsigned long long key = ((unsigned long long)a << 32) | b;
               auto it = edgeFace.find(key);
               if (it == edgeFace.end())
                  edgeFace.emplace(key, f);
               else if (faceDir[it->second] == faceDir[f])
                  parent[find((int)f)] = find((int)it->second);
            }

         struct Island
         {
            std::vector<size_t> faces;
            float minU = 1e30f, minV = 1e30f, maxU = -1e30f, maxV = -1e30f;
            float x = 0, y = 0;
         };
         std::unordered_map<int, size_t> islandOf;
         std::vector<Island> islands;
         for (size_t f = 0; f < faces; f++)
         {
            const int r = find((int)f);
            auto it = islandOf.find(r);
            if (it == islandOf.end())
            {
               it = islandOf.emplace(r, islands.size()).first;
               islands.emplace_back();
            }
            islands[it->second].faces.push_back(f);
         }
         for (Island& is : islands)
         {
            V3 t, b;
            Basis(dirs[faceDir[is.faces[0]]], t, b);
            for (size_t f : is.faces)
               for (int k = 0; k < 3; k++)
               {
                  const V3 p = Pos(in.vertices[in.indices[f * 3 + k]]);
                  const float u = Dot(p, t), v = Dot(p, b);
                  uv[f * 6 + k * 2] = u;
                  uv[f * 6 + k * 2 + 1] = v;
                  is.minU = std::min(is.minU, u); is.maxU = std::max(is.maxU, u);
                  is.minV = std::min(is.minV, v); is.maxV = std::max(is.maxV, v);
               }
         }

         // Shelf pack, tallest first, into a square whose side is the root of
         // the total island area (margins included), then scale into 0-1.
         const float gap = std::clamp(margin, 0.0f, 0.2f) * size * 0.25f;
         double total = 0.0;
         float widest = 0.0f;
         for (const Island& is : islands)
         {
            total += (double)(is.maxU - is.minU + gap) * (is.maxV - is.minV + gap);
            widest = std::max(widest, is.maxU - is.minU + gap);
         }
         const float side = std::max((float)std::sqrt(total) * 1.1f, widest);
         std::vector<size_t> byHeight(islands.size());
         for (size_t i = 0; i < islands.size(); i++)
            byHeight[i] = i;
         std::stable_sort(byHeight.begin(), byHeight.end(), [&](size_t a, size_t b)
                          { return islands[a].maxV - islands[a].minV > islands[b].maxV - islands[b].minV; });
         float cx = 0.0f, cy = 0.0f, rowH = 0.0f, usedW = 0.0f;
         for (size_t i : byHeight)
         {
            Island& is = islands[i];
            const float w = is.maxU - is.minU + gap, h = is.maxV - is.minV + gap;
            if (cx > 0.0f && cx + w > side)
            {
               cx = 0.0f;
               cy += rowH;
               rowH = 0.0f;
            }
            is.x = cx + gap * 0.5f;
            is.y = cy + gap * 0.5f;
            cx += w;
            usedW = std::max(usedW, cx);
            rowH = std::max(rowH, h);
         }
         const float extent = std::max({ usedW, cy + rowH, 1e-6f });
         for (const Island& is : islands)
            for (size_t f : is.faces)
               for (int k = 0; k < 3; k++)
               {
                  uv[f * 6 + k * 2] = (uv[f * 6 + k * 2] - is.minU + is.x) / extent;
                  uv[f * 6 + k * 2 + 1] = (uv[f * 6 + k * 2 + 1] - is.minV + is.y) / extent;
               }
      }

      // One vertex per (source vertex, UV): split only where corners disagree.
      Mesh out;
      out.faceMask = in.faceMask;
      out.selectionGroup = in.selectionGroup;
      const bool colour = in.HasVertexColor();
      out.indices.resize(in.indices.size());
      std::map<std::tuple<unsigned, int, int>, unsigned> made;
      for (size_t c = 0; c < in.indices.size(); c++)
      {
         const unsigned src = in.indices[c];
         const float u = uv[c * 2] * scale, v = uv[c * 2 + 1] * scale;
         const auto key = std::make_tuple(src, (int)std::lround(u * 1e5f), (int)std::lround(v * 1e5f));
         auto it = made.find(key);
         if (it == made.end())
         {
            Vertex nv = in.vertices[src];
            nv.u = u;
            nv.v = v;
            it = made.emplace(key, (unsigned)out.vertices.size()).first;
            out.vertices.push_back(nv);
            if (from != nullptr)
               from->push_back(src);
            if (colour)
               for (int k = 0; k < 3; k++)
                  out.vertexColor.push_back(in.vertexColor[src * 3 + k]);
         }
         out.indices[c] = it->second;
      }
      return out;
   }
}

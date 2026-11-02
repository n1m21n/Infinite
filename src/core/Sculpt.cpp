#include "Sculpt.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

      // The points under a brush, with how strongly each is touched, and the
      // surface's average normal and centre there (Blender's "area" plane).
      struct Area
      {
         std::vector<unsigned int> idx;
         std::vector<float> f;
         float normal[3] = { 0, 0, 0 };
         float center[3] = { 0, 0, 0 };
      };

      Area Gather(const float c[3], float radius, const std::vector<float>& pos, const std::vector<float>& nrm,
                  const Topology& t, bool sharp)
      {
         Area a;
         float wsum = 0.0f;
         const float r2 = radius * radius;
         for (int i = 0; i < t.points; i++)
         {
            const float* p = &pos[(size_t)i * 3];
            const float d[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
            const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (dd >= r2)
               continue;
            float f = Falloff(std::sqrt(dd) / radius);
            if (sharp)
               f *= f;
            for (int k = 0; k < 3; k++)
            {
               a.normal[k] += nrm[(size_t)i * 3 + k] * f;
               a.center[k] += p[k] * f;
            }
            wsum += f;
            if (!t.mask.empty() && t.mask[i] == 0)
               continue;
            a.idx.push_back((unsigned int)i);
            a.f.push_back(f);
         }
         Normalize(a.normal);
         if (wsum > 0.0f)
            for (int k = 0; k < 3; k++)
               a.center[k] /= wsum;
         return a;
      }
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

      // Neighbours over welded edges.
      std::vector<std::vector<unsigned int>> nb(t.points);
      std::vector<std::vector<std::pair<unsigned int, unsigned int>>> corners(t.points);   // (face, vertex)
      const size_t nf = base.FaceCount();
      for (size_t f = 0; f < nf; f++)
      {
         const unsigned int* tri = &base.indices[f * 3];
         if (tri[0] >= nv || tri[1] >= nv || tri[2] >= nv)
            continue;
         for (int c = 0; c < 3; c++)
         {
            const unsigned int a = t.point[tri[c]], b = t.point[tri[(c + 1) % 3]];
            corners[a].push_back({ (unsigned int)f, tri[c] });
            if (a == b)
               continue;
            nb[a].push_back(b);
            nb[b].push_back(a);
         }
      }
      t.nbrStart.assign(t.points + 1, 0);
      for (int p = 0; p < t.points; p++)
      {
         std::sort(nb[p].begin(), nb[p].end());
         nb[p].erase(std::unique(nb[p].begin(), nb[p].end()), nb[p].end());
         t.nbrStart[p + 1] = t.nbrStart[p] + (unsigned int)nb[p].size();
         t.nbr.insert(t.nbr.end(), nb[p].begin(), nb[p].end());
      }

      // Each vertex shades from the faces at its point whose corner normal
      // agrees with its own.
      t.shadeStart.assign(nv + 1, 0);
      for (size_t i = 0; i < nv; i++)
      {
         const Vertex& v = base.vertices[i];
         for (const auto& fc : corners[t.point[i]])
         {
            const Vertex& w = base.vertices[fc.second];
            if (fc.second == i || v.nx * w.nx + v.ny * w.ny + v.nz * w.nz > 0.5f)
               t.shadeFace.push_back(fc.first);
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
         // Base normals point the way the surface faces; keep that side.
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
               float amount, std::vector<float>& offsets)
   {
      if (amount < 0.01f || d.radius <= 0.0f || pos.size() != (size_t)t.points * 3 || offsets.size() != pos.size())
         return;
      const float s = std::clamp(d.strength, 0.0f, 1.0f) / amount;
      const float sign = d.invert ? -1.0f : 1.0f;
      for (int m : Passes(d.symmetry))
      {
         float c[3];
         Flip(d.center, m, c);
         const Area a = Gather(c, d.radius, pos, nrm, t, d.brush == kCrease);
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
                  for (int k = 0; k < 3; k++)
                     move[k] = a.normal[k] * d.radius * 0.15f * f * sign;
                  break;
               case kInflate:
                  for (int k = 0; k < 3; k++)
                     move[k] = n[k] * d.radius * 0.15f * f * sign;
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
                  const float w = std::min(1.0f, f * std::clamp(d.strength, 0.0f, 1.0f));
                  for (int k = 0; k < 3; k++)
                     move[k] = (avg[k] / (float)(b1 - b0) - p[k]) * w;
                  break;
               }
               case kPinch:
               case kCrease:
               {
                  float to[3] = { c[0] - p[0], c[1] - p[1], c[2] - p[2] };
                  const float along = to[0] * a.normal[0] + to[1] * a.normal[1] + to[2] * a.normal[2];
                  for (int k = 0; k < 3; k++)
                     to[k] -= a.normal[k] * along;
                  const float pull = d.brush == kPinch ? 0.2f : 0.25f;
                  for (int k = 0; k < 3; k++)
                     move[k] = to[k] * pull * f * (d.brush == kPinch ? sign : 1.0f);
                  if (d.brush == kCrease)
                     for (int k = 0; k < 3; k++)
                        move[k] -= a.normal[k] * d.radius * 0.15f * f * sign;
                  break;
               }
               case kFlatten:
               {
                  const float h = (p[0] - a.center[0]) * a.normal[0] + (p[1] - a.center[1]) * a.normal[1] +
                                  (p[2] - a.center[2]) * a.normal[2];
                  for (int k = 0; k < 3; k++)
                     move[k] = -a.normal[k] * h * 0.3f * f * sign;
                  break;
               }
               default:
                  break;
            }
            // Smooth already scales by strength above; the rest scale here.
            const float scale = d.brush == kSmooth ? 1.0f / amount : s;
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
            hold.weight.push_back(Falloff(std::sqrt(dd) / d.radius) * std::clamp(d.strength * 2.0f, 0.0f, 1.0f));
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

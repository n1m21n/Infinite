// Fur and hair strands (docs/plans/fur). See MeshOps::Fur in Mesh.h.
#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
   struct V3
   {
      float x = 0, y = 0, z = 0;
   };
   V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
   V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
   V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
   float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
   float Len(V3 a) { return std::sqrt(Dot(a, a)); }
   V3 Norm(V3 a, V3 fallback = { 0, 1, 0 })
   {
      const float l = Len(a);
      return l > 1e-12f ? a * (1.0f / l) : fallback;
   }
   V3 Lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }

   // Small, fast and stable across platforms (no std:: distributions, whose output differs between libraries).
   struct Rng
   {
      uint32_t s;
      explicit Rng(uint32_t seed) : s(seed * 747796405u + 2891336453u) {}
      float Next()
      {
         s = s * 747796405u + 2891336453u;
         uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
         w = (w >> 22u) ^ w;
         return (float)(w >> 8) * (1.0f / 16777216.0f);
      }
   };

   // Rotation taking unit `a` onto unit `b` (Rodrigues), applied to v.
   V3 RotateOnto(V3 v, V3 a, V3 b)
   {
      const V3 k = Cross(a, b);
      const float s = Len(k), c = Dot(a, b);
      if (s < 1e-6f)
      {
         if (c > 0.0f)
            return v;
         // Opposite: half turn about any axis perpendicular to a.
         const V3 ax = Norm(std::fabs(a.y) < 0.9f ? Cross(a, { 0, 1, 0 }) : Cross(a, { 1, 0, 0 }));
         return ax * (2.0f * Dot(ax, v)) - v;
      }
      const V3 kn = k * (1.0f / s);
      return v * c + Cross(kn, v) * s + kn * (Dot(kn, v) * (1.0f - c));
   }

   struct Root
   {
      V3 p, n;
      float u = 0, v = 0;
      float c[3] = { 1, 1, 1 };
   };
}

Mesh MeshOps::Fur(const Mesh& in, const FurParams& prm, const Polyline* guide)
{
   Mesh out = in;
   const size_t nv = in.vertices.size(), nf = in.indices.size() / 3;
   if (nf == 0 || !std::isfinite(prm.length) || prm.length <= 0.0f || !std::isfinite(prm.density) || prm.density <= 0.0f)
      return out;
   const bool inColour = in.HasVertexColor();
   if (!inColour)
      out.vertexColor.assign(nv * 3, 1.0f);

   // Area table over the faces fur may grow on.
   std::vector<double> cum(nf, 0.0);
   double total = 0.0;
   for (size_t f = 0; f < nf; f++)
   {
      if (in.FaceSelected(f))
      {
         const Vertex& a = in.vertices[in.indices[f * 3]];
         const Vertex& b = in.vertices[in.indices[f * 3 + 1]];
         const Vertex& c = in.vertices[in.indices[f * 3 + 2]];
         const V3 e1 = { b.px - a.px, b.py - a.py, b.pz - a.pz }, e2 = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         total += 0.5 * Len(Cross(e1, e2));
      }
      cum[f] = total;
   }
   if (total <= 0.0)
      return out;

   const int kClump = 8;
   const int maxStrands = std::clamp(prm.maxStrands, 1, 400000);
   const double want = std::min<double>(prm.density * total * 1e4, (double)maxStrands);
   const int clumps = std::max(1, (int)std::ceil(want / kClump));
   const int segs = std::clamp(prm.segments, 1, 32);
   const int pts = segs + 1;
   const float clumpRadius = prm.length * 0.35f;

   auto sampleRoot = [&](float r0, float r1, float r2) {
      const double target = r0 * total;
      size_t f = (size_t)(std::lower_bound(cum.begin(), cum.end(), target) - cum.begin());
      f = std::min(f, nf - 1);
      while (f < nf - 1 && !in.FaceSelected(f)) f++;
      float b1 = r1, b2 = r2;
      if (b1 + b2 > 1.0f) { b1 = 1.0f - b1; b2 = 1.0f - b2; }
      const float b0 = 1.0f - b1 - b2;
      const unsigned ia = in.indices[f * 3], ib = in.indices[f * 3 + 1], ic = in.indices[f * 3 + 2];
      const Vertex& a = in.vertices[ia];
      const Vertex& b = in.vertices[ib];
      const Vertex& c = in.vertices[ic];
      Root r;
      r.p = { a.px * b0 + b.px * b1 + c.px * b2, a.py * b0 + b.py * b1 + c.py * b2, a.pz * b0 + b.pz * b1 + c.pz * b2 };
      const V3 fn = Norm(Cross({ b.px - a.px, b.py - a.py, b.pz - a.pz }, { c.px - a.px, c.py - a.py, c.pz - a.pz }));
      r.n = Norm({ a.nx * b0 + b.nx * b1 + c.nx * b2, a.ny * b0 + b.ny * b1 + c.ny * b2, a.nz * b0 + b.nz * b1 + c.nz * b2 }, fn);
      r.u = a.u * b0 + b.u * b1 + c.u * b2;
      r.v = a.v * b0 + b.v * b1 + c.v * b2;
      if (inColour)
         for (int k = 0; k < 3; k++)
            r.c[k] = in.vertexColor[ia * 3 + k] * b0 + in.vertexColor[ib * 3 + k] * b1 + in.vertexColor[ic * 3 + k] * b2;
      return r;
   };

   // The guide, resampled by arc length to `pts` points, as offsets from its first point in units of its length.
   std::vector<V3> shape;
   V3 shapeDir = { 0, 1, 0 };
   if (guide && guide->Count() >= 2)
   {
      float len = 0.0f;
      for (size_t i = 1; i < guide->Count(); i++)
      {
         const float* a = &guide->points[(i - 1) * 3];
         const float* b = &guide->points[i * 3];
         len += Len({ b[0] - a[0], b[1] - a[1], b[2] - a[2] });
      }
      if (len > 1e-6f)
      {
         float p0[3], t0[3];
         MeshOps::SamplePolyline(*guide, 0.0f, p0, t0);
         for (int j = 0; j < pts; j++)
         {
            float q[3], t[3];
            MeshOps::SamplePolyline(*guide, (float)j / segs, q, t);
            shape.push_back(V3 { q[0] - p0[0], q[1] - p0[1], q[2] - p0[2] } * (1.0f / len));
         }
         shapeDir = Norm(shape[1] - shape[0]);
      }
   }

   const V3 comb = { prm.comb[0], prm.comb[1], prm.comb[2] };
   const V3 down = { 0, -1, 0 };
   Rng rng((uint32_t)prm.seed * 2654435761u + 17u);
   const size_t strands = (size_t)clumps * kClump;
   out.vertices.reserve(nv + strands * pts * 2);
   out.indices.reserve(in.indices.size() + strands * segs * 6);
   out.vertexColor.reserve((nv + strands * pts * 2) * 3);
   if (!in.faceMask.empty())
      out.faceMask.reserve(nf + strands * segs * 2);
   if (!in.selectionGroup.empty())
      out.selectionGroup.reserve(nf + strands * segs * 2);

   std::vector<V3> lead(pts), path(pts);
   // The path a strand rooted at r with length L takes, before clumping.
   auto grow = [&](const Root& r, float L, std::vector<V3>& o) {
      const V3 d0 = Norm(r.n + comb, r.n);
      for (int j = 0; j < pts; j++)
      {
         const float t = (float)j / segs;
         // Grown straight (or along the guide), then drooped by gravity * L * t^2: the same law the Blender A/B
         // rebuilds with a Set Position, so both renders hold identical strands.
         o[j] = r.p + (shape.empty() ? d0 * t : RotateOnto(shape[j], shapeDir, d0)) * L + down * (prm.gravity * L * t * t);
      }
   };

   for (int k = 0; k < clumps; k++)
   {
      // Stratified over the area table so strands cover the surface evenly.
      const Root lr = prm.clump > 0.0f ? sampleRoot((k + rng.Next()) / clumps, rng.Next(), rng.Next())
                                         : sampleRoot((k * kClump + rng.Next()) / (float)strands, rng.Next(), rng.Next());
      const float leadLen = prm.length * (1.0f - prm.lengthJitter * rng.Next());
      grow(lr, leadLen, lead);
      const V3 t1 = Norm(std::fabs(lr.n.y) < 0.9f ? Cross(lr.n, { 0, 1, 0 }) : Cross(lr.n, { 1, 0, 0 }));
      const V3 t2 = Cross(lr.n, t1);
      for (int m = 0; m < kClump; m++)
      {
         Root r = lr;
         if (prm.clump > 0.0f)
         {
            const float ang = 6.2831853f * rng.Next(), rad = clumpRadius * std::sqrt(rng.Next());
            r.p = lr.p + (t1 * std::cos(ang) + t2 * std::sin(ang)) * rad;
         }
         else if (m > 0) // no clumps: every strand its own root, spread evenly over the area table
            r = sampleRoot((k * kClump + m + rng.Next()) / (float)strands, rng.Next(), rng.Next());
         const float L = prm.length * (1.0f - prm.lengthJitter * rng.Next());
         grow(r, L, path);
         // Clump: tips meet the lead strand's path (shifted to share this root), more toward the tip.
         for (int j = 1; j < pts; j++)
         {
            const float t = (float)j / segs;
            const V3 onLead = lead[std::min(j, pts - 1)] + (r.p - lr.p) * (1.0f - t);
            path[j] = Lerp(path[j], onLead, prm.clump * t);
         }
         // Curl: a helix around the strand's own axis, opening toward the tip.
         const float twist = 6.2831853f * rng.Next();
         if (prm.curl > 0.0f)
         {
            const V3 ax = Norm(path[pts - 1] - path[0], r.n);
            const V3 c1 = Norm(std::fabs(ax.y) < 0.9f ? Cross(ax, { 0, 1, 0 }) : Cross(ax, { 1, 0, 0 }));
            const V3 c2 = Cross(ax, c1);
            const float cr = 0.12f * L * prm.curl, turns = 1.0f + 3.0f * prm.curl;
            for (int j = 1; j < pts; j++)
            {
               const float t = (float)j / segs, a = twist + 6.2831853f * turns * t;
               path[j] = path[j] + (c1 * std::cos(a) + c2 * std::sin(a)) * (cr * t);
            }
         }

         const unsigned base = (unsigned)out.vertices.size();
         for (int j = 0; j < pts; j++)
         {
            const float t = (float)j / segs;
            const V3 T = Norm(j + 1 < pts ? path[j + 1] - path[j] : path[j] - path[j - 1], r.n);
            const V3 b1 = Norm(Cross(T, r.n), t1);
            const V3 b2 = Cross(T, b1);
            const V3 side = b1 * std::cos(twist) + b2 * std::sin(twist);
            // Width is the fibre's diameter. A flat ribbon at a random twist covers 2/pi of a round fibre's width on
            // average, so it is widened by pi/2; with its own face normal (two-sided) its normals then spread over
            // the visible half circle the way a round fibre's do, so it shades like one. (A normal rounded across the
            // width measured worse against Cycles in both live and settle.)
            const float w = 0.5f * 1.5707963f * prm.width * (1.0f - 0.8f * t);
            const V3 f = Norm(Cross(side, T), r.n);
            for (int s = 0; s < 2; s++)
            {
               const V3 q = path[j] + side * (s == 0 ? -w : w);
               const V3 n = prm.normals == 1 ? T : f;
               Vertex v;
               v.px = q.x; v.py = q.y; v.pz = q.z;
               v.nx = n.x; v.ny = n.y; v.nz = n.z;
               v.u = r.u; v.v = r.v;
               out.vertices.push_back(v);
               for (int c = 0; c < 3; c++)
                  out.vertexColor.push_back(r.c[c]);
            }
         }
         for (int j = 0; j < segs; j++)
         {
            const unsigned a = base + j * 2;
            const unsigned idx[6] = { a, a + 1, a + 3, a, a + 3, a + 2 };
            out.indices.insert(out.indices.end(), idx, idx + 6);
            if (!in.faceMask.empty()) { out.faceMask.push_back(1); out.faceMask.push_back(1); }
            if (!in.selectionGroup.empty())
            {
               out.selectionGroup.push_back(0xFFFFFFF0u);
               out.selectionGroup.push_back(0xFFFFFFF0u);
            }
         }
      }
   }
   out.twoSided = true;
   out.ribbonStart = (unsigned int)in.indices.size();
   return out;
}

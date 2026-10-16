#include "CurveOps.h"

#include <algorithm>
#include <cmath>

namespace
{
   struct P { double x, y, z; };

   P At(const Polyline& l, size_t i) { return { l.points[i * 3], l.points[i * 3 + 1], l.points[i * 3 + 2] }; }

   void Push(Polyline& l, const P& p)
   {
      l.points.push_back((float)p.x); l.points.push_back((float)p.y); l.points.push_back((float)p.z);
   }

   double Len(const P& a, const P& b)
   {
      return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
   }

   double SegDist(const P& p, const P& a, const P& b)
   {
      const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
      const double l2 = dx * dx + dy * dy + dz * dz;
      double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy + (p.z - a.z) * dz) / l2 : 0.0;
      t = std::min(std::max(t, 0.0), 1.0);
      return Len(p, { a.x + dx * t, a.y + dy * t, a.z + dz * t });
   }
}

Polyline CurveOps::Resample(const Polyline& in, int count)
{
   if (in.Count() < 2 || count < 2)
      return in;
   const size_t n = in.Count();
   const size_t segs = in.closed ? n : n - 1;
   std::vector<double> cum(segs + 1, 0.0);
   for (size_t i = 0; i < segs; i++)
      cum[i + 1] = cum[i] + Len(At(in, i), At(in, (i + 1) % n));
   const double total = cum[segs];
   if (total <= 1e-12)
      return in;

   Polyline out;
   out.closed = in.closed;
   const int steps = in.closed ? count : count - 1; // closed: last point would repeat the first
   size_t seg = 0;
   for (int k = 0; k < count; k++)
   {
      const double d = total * (double)k / (double)steps;
      while (seg + 1 < segs && cum[seg + 1] < d) seg++;
      const double span = cum[seg + 1] - cum[seg];
      const double t = span > 0 ? std::min(std::max((d - cum[seg]) / span, 0.0), 1.0) : 0.0;
      const P a = At(in, seg), b = At(in, (seg + 1) % n);
      Push(out, { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t });
   }
   return out;
}

Polyline CurveOps::Simplify(const Polyline& in, float tolerance)
{
   const size_t n = in.Count();
   if (n < 3 || tolerance <= 0.0f)
      return in;
   std::vector<char> keep(n, 0);
   keep[0] = keep[n - 1] = 1;
   // A closed line is split at the point farthest from the start so both halves
   // have a chord to measure against.
   size_t split = n - 1;
   if (in.closed)
   {
      double best = -1;
      for (size_t i = 1; i < n; i++)
      {
         const double d = Len(At(in, 0), At(in, i));
         if (d > best) { best = d; split = i; }
      }
      keep[split] = 1;
   }
   struct Range { size_t a, b; };
   std::vector<Range> stack;
   if (in.closed) { stack.push_back({ 0, split }); stack.push_back({ split, n - 1 }); }
   else stack.push_back({ 0, n - 1 });
   while (!stack.empty())
   {
      const Range r = stack.back();
      stack.pop_back();
      if (r.b <= r.a + 1) continue;
      double worst = -1;
      size_t at = r.a;
      for (size_t i = r.a + 1; i < r.b; i++)
      {
         const double d = SegDist(At(in, i), At(in, r.a), At(in, r.b));
         if (d > worst) { worst = d; at = i; }
      }
      if (worst > tolerance)
      {
         keep[at] = 1;
         stack.push_back({ r.a, at });
         stack.push_back({ at, r.b });
      }
   }
   Polyline out;
   out.closed = in.closed;
   for (size_t i = 0; i < n; i++)
      if (keep[i]) Push(out, At(in, i));
   // A closed ring needs at least three points to stay a ring.
   if (out.Count() < (in.closed ? 3u : 2u))
      return in;
   return out;
}

Polyline CurveOps::Smooth(const Polyline& in, int iterations, float strength)
{
   const size_t n = in.Count();
   if (n < 3 || iterations <= 0)
      return in;
   const double s = std::min(std::max((double)strength, 0.0), 1.0);
   std::vector<P> cur(n), nxt(n);
   for (size_t i = 0; i < n; i++) cur[i] = At(in, i);
   for (int it = 0; it < iterations; it++)
   {
      for (size_t i = 0; i < n; i++)
      {
         const bool end = !in.closed && (i == 0 || i == n - 1);
         if (end) { nxt[i] = cur[i]; continue; }
         const P& a = cur[(i + n - 1) % n];
         const P& b = cur[(i + 1) % n];
         const P& c = cur[i];
         nxt[i] = { c.x + ((a.x + b.x) * 0.5 - c.x) * s, c.y + ((a.y + b.y) * 0.5 - c.y) * s,
                    c.z + ((a.z + b.z) * 0.5 - c.z) * s };
      }
      cur.swap(nxt);
   }
   Polyline out;
   out.closed = in.closed;
   for (const P& p : cur) Push(out, p);
   return out;
}

Polyline CurveOps::Offset(const Polyline& in, float distance, int arcSteps)
{
   // Drop repeated points so every edge has a direction.
   std::vector<P> v;
   for (size_t i = 0; i < in.Count(); i++)
   {
      const P p = At(in, i);
      if (v.empty() || Len(v.back(), p) > 1e-9) v.push_back(p);
   }
   if (in.closed && v.size() > 1 && Len(v.front(), v.back()) <= 1e-9) v.pop_back();
   const size_t n = v.size();
   if (n < 2 || distance == 0.0f)
      return in;

   const double d = distance;
   const size_t edges = in.closed ? n : n - 1;
   std::vector<P> nrm(edges); // unit left-hand normal per edge
   for (size_t e = 0; e < edges; e++)
   {
      const P& a = v[e];
      const P& b = v[(e + 1) % n];
      const double dx = b.x - a.x, dy = b.y - a.y;
      const double l = std::sqrt(dx * dx + dy * dy);
      nrm[e] = l > 1e-12 ? P{ -dy / l, dx / l, 0.0 } : P{ 0, 1, 0 };
   }

   Polyline out;
   out.closed = in.closed;
   const int arc = std::max(1, arcSteps);
   for (size_t i = 0; i < n; i++)
   {
      const bool first = !in.closed && i == 0, last = !in.closed && i == n - 1;
      if (first) { Push(out, { v[i].x + nrm[0].x * d, v[i].y + nrm[0].y * d, v[i].z }); continue; }
      if (last)  { Push(out, { v[i].x + nrm[edges - 1].x * d, v[i].y + nrm[edges - 1].y * d, v[i].z }); continue; }
      const P& n0 = nrm[(i + edges - 1) % edges];
      const P& n1 = nrm[i % edges];
      const double cross = n0.x * n1.y - n0.y * n1.x;
      const double dot = n0.x * n1.x + n0.y * n1.y;
      // The corner is on the outside of the offset when the turn runs away
      // from the side we are moving to.
      const bool outer = (cross * d) < 0.0;
      if (outer && dot < 0.9999)
      {
         const double a0 = std::atan2(n0.y, n0.x);
         double sweep = std::atan2(cross, dot);
         for (int k = 0; k <= arc; k++)
         {
            const double a = a0 + sweep * (double)k / (double)arc;
            Push(out, { v[i].x + std::cos(a) * d, v[i].y + std::sin(a) * d, v[i].z });
         }
      }
      else
      {
         // Mitre on the inside, clamped so a hairpin cannot throw the point far away.
         double mx = n0.x + n1.x, my = n0.y + n1.y;
         const double ml2 = mx * mx + my * my;
         if (ml2 < 1e-12) { mx = n0.x; my = n0.y; }
         else
         {
            const double scale = std::min(2.0 / ml2, 4.0);
            mx *= scale; my *= scale;
         }
         Push(out, { v[i].x + mx * d, v[i].y + my * d, v[i].z });
      }
   }
   return out;
}

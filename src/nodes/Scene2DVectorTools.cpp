// Scene 2D, vector tools: curve fitting (Simplify), Shape Snap, vector region fill and Image Trace.
// All CPU maths on point lists and masks; the results are ordinary vector items, editable by hand.
//
// Curve fitting follows Schneider, "An Algorithm for Automatically Fitting Digitized Curves" (Graphics Gems,
// 1990). Image Trace follows the pipeline VTracer describes (github.com/visioncortex/vtracer, MIT): colour
// clusters, connected regions, outline tracing, staircase removal, curve fit, stacked so no gaps show.

#include "Scene2DNode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <numeric>
#include <cctype>
#include <sstream>

namespace
{
   constexpr double kPiD = 3.14159265358979323846;

   struct V2
   {
      double x = 0, y = 0;
   };
   V2 operator+(V2 a, V2 b) { return { a.x + b.x, a.y + b.y }; }
   V2 operator-(V2 a, V2 b) { return { a.x - b.x, a.y - b.y }; }
   V2 operator*(V2 a, double k) { return { a.x * k, a.y * k }; }
   double Dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
   double Len(V2 a) { return std::hypot(a.x, a.y); }
   V2 Unit(V2 a)
   {
      const double l = Len(a);
      return l > 1e-12 ? V2{ a.x / l, a.y / l } : V2{ 0, 0 };
   }

   using Bez = std::array<V2, 4>;

   V2 BezAt(const Bez& b, double t)
   {
      const double s = 1 - t;
      return b[0] * (s * s * s) + b[1] * (3 * s * s * t) + b[2] * (3 * s * t * t) + b[3] * (t * t * t);
   }
   V2 BezD1(const Bez& b, double t)
   {
      const double s = 1 - t;
      return (b[1] - b[0]) * (3 * s * s) + (b[2] - b[1]) * (6 * s * t) + (b[3] - b[2]) * (3 * t * t);
   }
   V2 BezD2(const Bez& b, double t)
   {
      return (b[2] - b[1] * 2 + b[0]) * (6 * (1 - t)) + (b[3] - b[2] * 2 + b[1]) * (6 * t);
   }

   // ---- Schneider fit -----------------------------------------------------------------------------------

   struct Fitter
   {
      const std::vector<V2>& p;
      double err2;
      std::vector<Bez> out;

      std::vector<double> Chord(int a, int b) const
      {
         std::vector<double> u(b - a + 1, 0.0);
         for (int i = a + 1; i <= b; i++)
            u[i - a] = u[i - a - 1] + Len(p[i] - p[i - 1]);
         const double total = u.back();
         for (double& v : u)
            v = total > 0 ? v / total : 0;
         return u;
      }

      Bez Generate(int a, int b, const std::vector<double>& u, V2 t1, V2 t2) const
      {
         double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
         const V2 p0 = p[a], p3 = p[b];
         for (int i = a; i <= b; i++)
         {
            const double t = u[i - a], s = 1 - t;
            const double b0 = s * s * s, b1 = 3 * s * s * t, b2 = 3 * s * t * t, b3 = t * t * t;
            const V2 a1 = t1 * b1, a2 = t2 * b2;
            c00 += Dot(a1, a1);
            c01 += Dot(a1, a2);
            c11 += Dot(a2, a2);
            const V2 tmp = p[i] - (p0 * (b0 + b1) + p3 * (b2 + b3));
            x0 += Dot(a1, tmp);
            x1 += Dot(a2, tmp);
         }
         const double det = c00 * c11 - c01 * c01;
         const double seg = Len(p3 - p0);
         double al = 0, ar = 0;
         if (std::fabs(det) > 1e-12)
         {
            al = (x0 * c11 - x1 * c01) / det;
            ar = (c00 * x1 - c01 * x0) / det;
         }
         // Wild or backwards handles: Wu/Barsky's heuristic, a third of the chord.
         if (!(al > 1e-6 * seg) || !(ar > 1e-6 * seg) || al > seg * 4 || ar > seg * 4)
            al = ar = seg / 3.0;
         return { p0, p0 + t1 * al, p3 + t2 * ar, p3 };
      }

      double MaxError(int a, int b, const Bez& bz, const std::vector<double>& u, int& split) const
      {
         double worst = 0;
         split = (a + b) / 2;
         for (int i = a + 1; i < b; i++)
         {
            const V2 d = BezAt(bz, u[i - a]) - p[i];
            const double e = Dot(d, d);
            if (e >= worst)
            {
               worst = e;
               split = i;
            }
         }
         return worst;
      }

      void Reparam(int a, int b, const Bez& bz, std::vector<double>& u) const
      {
         for (int i = a; i <= b; i++)
         {
            const double t = u[i - a];
            const V2 q = BezAt(bz, t) - p[i], d1 = BezD1(bz, t), d2 = BezD2(bz, t);
            const double num = Dot(q, d1), den = Dot(d1, d1) + Dot(q, d2);
            if (std::fabs(den) > 1e-12)
               u[i - a] = std::clamp(t - num / den, 0.0, 1.0);
         }
      }

      void Fit(int a, int b, V2 t1, V2 t2, int depth)
      {
         if (b - a == 1 || depth > 40)
         {
            const double d = Len(p[b] - p[a]) / 3.0;
            out.push_back({ p[a], p[a] + t1 * d, p[b] + t2 * d, p[b] });
            return;
         }
         std::vector<double> u = Chord(a, b);
         Bez bz = Generate(a, b, u, t1, t2);
         int split = 0;
         double e = MaxError(a, b, bz, u, split);
         if (e < err2)
         {
            out.push_back(bz);
            return;
         }
         if (e < err2 * 4)
            for (int it = 0; it < 4; it++)
            {
               Reparam(a, b, bz, u);
               bz = Generate(a, b, u, t1, t2);
               e = MaxError(a, b, bz, u, split);
               if (e < err2)
               {
                  out.push_back(bz);
                  return;
               }
            }
         split = std::clamp(split, a + 1, b - 1);
         V2 tc = Unit(p[split - 1] - p[split + 1]);
         if (Len(tc) == 0)
            tc = Unit(p[a] - p[b]);
         Fit(a, split, t1, tc, depth + 1);
         Fit(split, b, tc * -1.0, t2, depth + 1);
      }
   };

   // The point an arc length `win` away from i along the polyline (dir +1 forward, -1 back), wrapping when closed.
   V2 Reach(const std::vector<V2>& p, int i, int dir, double win, bool closed)
   {
      const int n = (int)p.size();
      double acc = 0;
      int j = i;
      for (int k = 0; k < n; k++)
      {
         int nj = j + dir;
         if (closed)
            nj = (nj % n + n) % n;
         else if (nj < 0 || nj >= n)
            return p[j];
         acc += Len(p[nj] - p[j]);
         j = nj;
         if (acc >= win)
            break;
      }
      return p[j];
   }

   // Indices where the line turns sharper than cornerDeg, measured over `win` px each side, one per turn.
   std::vector<int> FindCorners(const std::vector<V2>& p, bool closed, double cornerDeg, double win)
   {
      const int n = (int)p.size();
      std::vector<double> turn(n, 0.0);
      for (int i = 0; i < n; i++)
      {
         if (!closed && (i == 0 || i == n - 1))
            continue;
         const V2 in = Unit(p[i] - Reach(p, i, -1, win, closed)), out = Unit(Reach(p, i, 1, win, closed) - p[i]);
         if (Len(in) == 0 || Len(out) == 0)
            continue;
         turn[i] = std::acos(std::clamp(Dot(in, out), -1.0, 1.0)) * 180.0 / kPiD;
      }
      std::vector<int> c;
      for (int i = 0; i < n; i++)
      {
         if (turn[i] <= cornerDeg)
            continue;
         // Non-maximum suppression: the sharpest sample within the window wins.
         bool best = true;
         for (int d : { -1, 1 })
         {
            double acc = 0;
            int j = i;
            for (int k = 0; k < n && best; k++)
            {
               int nj = j + d;
               if (closed)
                  nj = (nj % n + n) % n;
               else if (nj < 0 || nj >= n)
                  break;
               acc += Len(p[nj] - p[j]);
               j = nj;
               if (acc > win)
                  break;
               if (turn[j] > turn[i] || (turn[j] == turn[i] && j < i))
                  best = false;
            }
         }
         if (best)
            c.push_back(i);
      }
      return c;
   }

   // Bezier segments to anchors (x y inX inY outX outY), the shape's own format.
   void ToAnchors(const std::vector<Bez>& segs, bool closed, std::vector<float>& pts)
   {
      pts.clear();
      const int n = (int)segs.size();
      if (n == 0)
         return;
      auto push = [&](V2 a, V2 in, V2 out) {
         for (double v : { a.x, a.y, in.x, in.y, out.x, out.y })
            pts.push_back((float)v);
      };
      for (int i = 0; i < n; i++)
      {
         const V2 in = i > 0 ? segs[i - 1][2] : (closed ? segs[n - 1][2] : segs[0][0]);
         push(segs[i][0], in, segs[i][1]);
      }
      if (!closed)
         push(segs[n - 1][3], segs[n - 1][2], segs[n - 1][3]);
   }

   std::vector<V2> Clean(const std::vector<float>& xy, bool closed)
   {
      std::vector<V2> p;
      for (size_t i = 0; i + 1 < xy.size(); i += 2)
      {
         const V2 v{ xy[i], xy[i + 1] };
         if (!std::isfinite(v.x) || !std::isfinite(v.y))
            continue;
         if (p.empty() || Len(v - p.back()) > 1e-6)
            p.push_back(v);
      }
      if (closed && p.size() > 2 && Len(p.front() - p.back()) < 1e-6)
         p.pop_back();
      return p;
   }

   // Even arc-length samples of a polyline (n of them; closed adds the closing edge).
   std::vector<V2> Resample(const std::vector<V2>& p, bool closed, int n)
   {
      std::vector<V2> q = p;
      if (closed && !p.empty())
         q.push_back(p.front());
      std::vector<double> acc(q.size(), 0.0);
      for (size_t i = 1; i < q.size(); i++)
         acc[i] = acc[i - 1] + Len(q[i] - q[i - 1]);
      std::vector<V2> out;
      if (q.size() < 2 || acc.back() <= 0)
         return p;
      const double total = acc.back();
      const int count = closed ? n : n;
      size_t j = 1;
      for (int k = 0; k < count; k++)
      {
         const double s = closed ? total * k / count : total * k / std::max(1, count - 1);
         while (j < q.size() - 1 && acc[j] < s)
            j++;
         const double seg = acc[j] - acc[j - 1];
         const double f = seg > 0 ? (s - acc[j - 1]) / seg : 0;
         out.push_back(q[j - 1] + (q[j] - q[j - 1]) * std::clamp(f, 0.0, 1.0));
      }
      return out;
   }

   double SegDist(V2 p, V2 a, V2 b)
   {
      const V2 ab = b - a;
      const double l2 = Dot(ab, ab);
      const double t = l2 > 0 ? std::clamp(Dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
      return Len(p - (a + ab * t));
   }

   // Douglas-Peucker on a closed loop, starting from the point farthest from the centroid.
   std::vector<V2> ClosedDp(const std::vector<V2>& p, double tol)
   {
      const int n = (int)p.size();
      if (n < 4)
         return p;
      V2 c{ 0, 0 };
      for (const V2& v : p)
         c = c + v * (1.0 / n);
      int s = 0;
      for (int i = 1; i < n; i++)
         if (Len(p[i] - c) > Len(p[s] - c))
            s = i;
      // The point farthest from s splits the loop in two open runs.
      int f = s;
      for (int i = 0; i < n; i++)
         if (Len(p[i] - p[s]) > Len(p[f] - p[s]))
            f = i;
      std::vector<char> keep(n, 0);
      keep[s] = keep[f] = 1;
      std::vector<std::pair<int, int>> stack{ { s, f }, { f, s } };
      while (!stack.empty())
      {
         const auto [a, b] = stack.back();
         stack.pop_back();
         double worst = 0;
         int wi = -1;
         for (int i = (a + 1) % n; i != b; i = (i + 1) % n)
         {
            const double d = SegDist(p[i], p[a], p[b]);
            if (d > worst)
            {
               worst = d;
               wi = i;
            }
         }
         if (wi >= 0 && worst > tol)
         {
            keep[wi] = 1;
            stack.push_back({ a, wi });
            stack.push_back({ wi, b });
         }
      }
      std::vector<V2> out;
      for (int k = 0; k < n; k++)
         if (keep[(s + k) % n])
            out.push_back(p[(s + k) % n]);
      return out;
   }

   double PolyError(const std::vector<V2>& p, const std::vector<V2>& poly, bool closed)
   {
      double sum = 0;
      const int m = (int)poly.size();
      for (const V2& v : p)
      {
         double best = 1e300;
         for (int i = 0; i + (closed ? 0 : 1) < m; i++)
            best = std::min(best, SegDist(v, poly[i], poly[(i + 1) % m]));
         sum += best;
      }
      return p.empty() ? 0 : sum / p.size();
   }

   void PathFromPoly(const std::vector<V2>& poly, bool closed, Scene2DNode::Shape& s)
   {
      s.type = Scene2DNode::Shape::kPath;
      s.pts.clear();
      for (const V2& v : poly)
         for (double c : { v.x, v.y, v.x, v.y, v.x, v.y })
            s.pts.push_back((float)c);
      s.closed = closed;
   }

   // An elliptical arc as cubics of at most 90 degrees each, in a frame turned by `rot` radians.
   void ArcPath(V2 c, double a, double b, double rot, double t0, double sweep, bool closed, Scene2DNode::Shape& s)
   {
      s.type = Scene2DNode::Shape::kPath;
      s.pts.clear();
      s.closed = closed;
      const int segs = std::max(1, (int)std::ceil(std::fabs(sweep) / (kPiD / 2) - 1e-9));
      const double step = sweep / segs, k = 4.0 / 3.0 * std::tan(step / 4);
      const double cr = std::cos(rot), sr = std::sin(rot);
      auto at = [&](double t) { const V2 e{ a * std::cos(t), b * std::sin(t) }; return V2{ c.x + e.x * cr - e.y * sr, c.y + e.x * sr + e.y * cr }; };
      auto tan = [&](double t) { const V2 e{ -a * std::sin(t), b * std::cos(t) }; return V2{ e.x * cr - e.y * sr, e.x * sr + e.y * cr }; };
      const int anchors = closed ? segs : segs + 1;
      for (int i = 0; i < anchors; i++)
      {
         const double t = t0 + step * i;
         const V2 p = at(t), d = tan(t) * k;
         const bool first = !closed && i == 0, last = !closed && i == segs;
         const V2 in = first ? p : p - d, out = last ? p : p + d;
         for (double v : { p.x, p.y, in.x, in.y, out.x, out.y })
            s.pts.push_back((float)v);
      }
   }
}

// ---- Curve fit and Simplify ------------------------------------------------------------------------------

void Scene2DNode::FitPath(const std::vector<float>& xy, bool closed, float tol, float cornerDeg, std::vector<float>& pts, float window)
{
   pts.clear();
   std::vector<V2> p = Clean(xy, closed);
   const int n = (int)p.size();
   if (n == 0)
      return;
   if (n == 1)
   {
      pts = { (float)p[0].x, (float)p[0].y, (float)p[0].x, (float)p[0].y, (float)p[0].x, (float)p[0].y };
      return;
   }
   const double t = std::max(0.05, (double)tol);
   const double win = window > 0 ? (double)window : std::max(2.0 * t, 2.0);
   std::vector<int> br = FindCorners(p, closed, cornerDeg, win);
   Fitter f{ p, t * t, {} };
   // Runs between breaks; each run's end tangents come from `win` px along it, or across a smooth join.
   auto tanAt = [&](int i, int dir, bool corner) {
      if (corner || !closed)
         return Unit(Reach(p, i, dir, win, closed) - p[i]);
      return Unit((Reach(p, i, dir, win, closed) - Reach(p, i, -dir, win, closed)));
   };
   if (!closed)
   {
      std::vector<int> cuts{ 0 };
      for (int c : br)
         if (c > 0 && c < n - 1)
            cuts.push_back(c);
      cuts.push_back(n - 1);
      for (size_t k = 0; k + 1 < cuts.size(); k++)
      {
         const int a = cuts[k], b = cuts[k + 1];
         f.Fit(a, b, tanAt(a, 1, true), tanAt(b, -1, true), 0);
      }
      ToAnchors(f.out, false, pts);
      return;
   }
   // Closed: unroll the loop from the first break (or 0) so every run is a plain index range.
   const int start = br.empty() ? 0 : br[0];
   std::vector<V2> q;
   for (int k = 0; k <= n; k++)
      q.push_back(p[(start + k) % n]);
   std::vector<int> cuts{ 0 };
   for (int c : br)
   {
      const int r = ((c - start) % n + n) % n;
      if (r > 0)
         cuts.push_back(r);
   }
   cuts.push_back(n);
   std::sort(cuts.begin(), cuts.end());
   cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
   Fitter g{ q, t * t, {} };
   const bool smoothStart = br.empty();
   for (size_t k = 0; k + 1 < cuts.size(); k++)
   {
      const int a = cuts[k], b = cuts[k + 1];
      const int ia = (start + a) % n, ib = (start + b) % n;
      const bool ca = !(smoothStart && a == 0), cb = !(smoothStart && b == n);
      g.Fit(a, b, tanAt(ia, 1, ca), tanAt(ib, -1, cb), 0);
   }
   ToAnchors(g.out, true, pts);
}

bool Scene2DNode::SimplifyShape(Shape& s, float tol)
{
   if (s.type != Shape::kPath || s.Anchors() < 3)
      return false;
   std::vector<std::vector<float>> lines;
   Outline(s, lines);
   if (lines.empty() || lines[0].size() < 4)
      return false;
   std::vector<float> pts;
   FitPath(lines[0], s.closed, tol, kSimplifyCornerDeg, pts);
   if (pts.size() < 6 || pts.size() >= s.pts.size())
      return false;
   s.pts = std::move(pts);
   return true;
}

float Scene2DNode::OutlineDistance(const Shape& s, const std::vector<float>& xy)
{
   std::vector<std::vector<float>> lines;
   Outline(s, lines);
   double worst = 0;
   for (size_t i = 0; i + 1 < xy.size(); i += 2)
   {
      const V2 v{ xy[i], xy[i + 1] };
      double best = 1e300;
      for (const auto& l : lines)
      {
         for (size_t k = 0; k + 3 < l.size(); k += 2)
            best = std::min(best, SegDist(v, { l[k], l[k + 1] }, { l[k + 2], l[k + 3] }));
         if (s.closed && l.size() >= 4)
            best = std::min(best, SegDist(v, { l[l.size() - 2], l[l.size() - 1] }, { l[0], l[1] }));
      }
      worst = std::max(worst, best);
   }
   return (float)worst;
}

// ---- Shape Snap ------------------------------------------------------------------------------------------

int Scene2DNode::SnapStroke(const std::vector<float>& xy, bool exact, Shape& out)
{
   std::vector<V2> raw = Clean(xy, false);
   if (raw.size() < 3)
      return kSnapNone;
   double length = 0;
   for (size_t i = 1; i < raw.size(); i++)
      length += Len(raw[i] - raw[i - 1]);
   V2 lo = raw[0], hi = raw[0];
   for (const V2& v : raw)
   {
      lo = { std::min(lo.x, v.x), std::min(lo.y, v.y) };
      hi = { std::max(hi.x, v.x), std::max(hi.y, v.y) };
   }
   const double diag = Len(hi - lo);
   if (length < 4 || diag < 4)
      return kSnapNone;
   const bool closed = Len(raw.back() - raw.front()) < 0.15 * length;
   out = Shape();
   if (!closed)
   {
      const std::vector<V2> p = Resample(raw, false, 64);
      const V2 a = p.front(), b = p.back();
      double dev = 0;
      for (const V2& v : p)
         dev = std::max(dev, SegDist(v, a, b));
      const double chord = Len(b - a);
      if (chord > 0 && dev / chord < 0.06)
      {
         V2 e = b;
         if (exact)
         {
            // Exact: the nearest 15 degrees, same length.
            const double ang = std::round(std::atan2(b.y - a.y, b.x - a.x) / (kPiD / 12)) * (kPiD / 12);
            e = a + V2{ std::cos(ang), std::sin(ang) } * chord;
         }
         out.type = Shape::kLine;
         out.x0 = (float)a.x, out.y0 = (float)a.y, out.x1 = (float)e.x, out.y1 = (float)e.y;
         return kSnapLine;
      }
      // Arc: algebraic circle fit (Kasa), then the stroke's own sweep.
      double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
      const double m = (double)p.size();
      for (const V2& v : p)
      {
         const double z = v.x * v.x + v.y * v.y;
         sx += v.x, sy += v.y, sxx += v.x * v.x, syy += v.y * v.y, sxy += v.x * v.y, sxz += v.x * z, syz += v.y * z, sz += z;
      }
      // [sxx sxy sx; sxy syy sy; sx sy m] [D E F] = -[sxz syz sz]
      const double A[3][3] = { { sxx, sxy, sx }, { sxy, syy, sy }, { sx, sy, m } };
      const double R[3] = { -sxz, -syz, -sz };
      const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                         A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
      if (std::fabs(det) < 1e-9)
         return kSnapNone;
      auto solve = [&](int col) {
         double M[3][3];
         std::memcpy(M, A, sizeof(M));
         for (int r = 0; r < 3; r++)
            M[r][col] = R[r];
         return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                 M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
      };
      const double D = solve(0), E = solve(1), F = solve(2);
      const V2 c{ -D / 2, -E / 2 };
      const double r2 = c.x * c.x + c.y * c.y - F;
      if (!(r2 > 0))
         return kSnapNone;
      const double r = std::sqrt(r2);
      double res = 0;
      for (const V2& v : p)
         res += (Len(v - c) - r) * (Len(v - c) - r);
      res = std::sqrt(res / m);
      if (res / r > 0.05 || r > diag * 4)
         return kSnapNone;
      double sweep = 0, prev = std::atan2(p[0].y - c.y, p[0].x - c.x);
      const double t0 = prev;
      for (size_t i = 1; i < p.size(); i++)
      {
         const double a2 = std::atan2(p[i].y - c.y, p[i].x - c.x);
         sweep += std::remainder(a2 - prev, 2 * kPiD);
         prev = a2;
      }
      if (std::fabs(sweep) < kPiD / 12)
         return kSnapNone;
      ArcPath(c, r, r, 0, t0, sweep, false, out);
      return kSnapArc;
   }
   // Closed: an ellipse against a polygon, whichever sits closer to what was drawn.
   const std::vector<V2> p = Resample(raw, true, 96);
   const double m = (double)p.size();
   V2 c{ 0, 0 };
   for (const V2& v : p)
      c = c + v * (1.0 / m);
   double cxx = 0, cyy = 0, cxy = 0;
   for (const V2& v : p)
   {
      const V2 d = v - c;
      cxx += d.x * d.x / m, cyy += d.y * d.y / m, cxy += d.x * d.y / m;
   }
   const double rot = 0.5 * std::atan2(2 * cxy, cxx - cyy);
   const double cr = std::cos(rot), sr = std::sin(rot);
   double ulo = 1e300, uhi = -1e300, vlo = 1e300, vhi = -1e300;
   for (const V2& v : p)
   {
      const V2 d = v - c;
      const double u = d.x * cr + d.y * sr, w = -d.x * sr + d.y * cr;
      ulo = std::min(ulo, u), uhi = std::max(uhi, u), vlo = std::min(vlo, w), vhi = std::max(vhi, w);
   }
   const double ea = (uhi - ulo) / 2, eb = (vhi - vlo) / 2;
   const V2 ec = c + V2{ cr, sr } * ((uhi + ulo) / 2) + V2{ -sr, cr } * ((vhi + vlo) / 2);
   double eErr = 1e300;
   if (ea > 1 && eb > 1)
   {
      eErr = 0;
      for (const V2& v : p)
      {
         const V2 d = v - ec;
         const double u = d.x * cr + d.y * sr, w = -d.x * sr + d.y * cr;
         eErr += std::fabs(std::hypot(u / ea, w / eb) - 1.0) * (ea + eb) / 2;
      }
      eErr /= m;
   }
   std::vector<V2> poly = ClosedDp(p, 0.05 * diag);
   const int k = (int)poly.size();
   const double pErr = k >= 3 ? PolyError(p, poly, true) : 1e300;
   const bool polyOk = k >= 3 && k <= 12 && pErr < 0.04 * diag;
   const bool ellOk = eErr < 0.08 * (ea + eb) / 2;
   if (ellOk && (!polyOk || eErr * 0.6 < pErr || k > 8))
   {
      double a = ea, b = eb, ro = rot;
      if (exact)
         a = b = (ea + eb) / 2, ro = 0;
      // Upright (or round): a live ellipse; turned: a path of four quarter arcs.
      const double tilt = std::fabs(std::remainder(ro, kPiD / 2));
      if (tilt < 3 * kPiD / 180 || std::fabs(a - b) < 0.5)
      {
         const bool swap = std::fabs(std::remainder(ro, kPiD)) > kPiD / 4;
         const double hw = swap ? b : a, hh = swap ? a : b;
         out.type = Shape::kEllipse;
         out.x0 = (float)(ec.x - hw), out.y0 = (float)(ec.y - hh), out.x1 = (float)(ec.x + hw), out.y1 = (float)(ec.y + hh);
      }
      else
         ArcPath(ec, a, b, ro, 0, 2 * kPiD, true, out);
      return kSnapEllipse;
   }
   if (!polyOk)
      return kSnapNone;
   // Rotation of the polygon: its longest edge.
   int le = 0;
   for (int i = 0; i < k; i++)
      if (Len(poly[(i + 1) % k] - poly[i]) > Len(poly[(le + 1) % k] - poly[le]))
         le = i;
   const double edgeAng = std::atan2(poly[(le + 1) % k].y - poly[le].y, poly[(le + 1) % k].x - poly[le].x);
   if (k == 4)
   {
      bool square = true;
      for (int i = 0; i < 4; i++)
      {
         const V2 a = Unit(poly[(i + 1) % 4] - poly[i]), b = Unit(poly[(i + 2) % 4] - poly[(i + 1) % 4]);
         square = square && std::fabs(Dot(a, b)) < std::sin(15 * kPiD / 180);
      }
      if (square || exact)
      {
         // The rectangle in the frame of its longest edge.
         const double ang = exact ? 0.0 : std::remainder(edgeAng, kPiD / 2);
         const double ca = std::cos(ang), sa = std::sin(ang);
         double a0 = 1e300, a1 = -1e300, b0 = 1e300, b1 = -1e300;
         for (const V2& v : p)
         {
            const V2 d = v - c;
            const double u = d.x * ca + d.y * sa, w = -d.x * sa + d.y * ca;
            a0 = std::min(a0, u), a1 = std::max(a1, u), b0 = std::min(b0, w), b1 = std::max(b1, w);
         }
         if (exact && std::fabs((a1 - a0) - (b1 - b0)) < 0.15 * std::max(a1 - a0, b1 - b0))
         {
            const double s2 = ((a1 - a0) + (b1 - b0)) / 4, mu = (a0 + a1) / 2, mv = (b0 + b1) / 2;
            a0 = mu - s2, a1 = mu + s2, b0 = mv - s2, b1 = mv + s2;
         }
         if (std::fabs(ang) < 3 * kPiD / 180)
         {
            out.type = Shape::kRect;
            out.x0 = (float)(c.x + a0), out.y0 = (float)(c.y + b0), out.x1 = (float)(c.x + a1), out.y1 = (float)(c.y + b1);
         }
         else
         {
            std::vector<V2> q;
            for (const auto& uv : { V2{ a0, b0 }, V2{ a1, b0 }, V2{ a1, b1 }, V2{ a0, b1 } })
               q.push_back(c + V2{ uv.x * ca - uv.y * sa, uv.x * sa + uv.y * ca });
            PathFromPoly(q, true, out);
         }
         return kSnapRect;
      }
   }
   // Regular when the sides agree (or asked to be exact): centred on the mean, one vertex where the first was.
   std::vector<double> rad(k), side(k);
   for (int i = 0; i < k; i++)
      rad[i] = Len(poly[i] - c), side[i] = Len(poly[(i + 1) % k] - poly[i]);
   auto cv = [](const std::vector<double>& v, int from, int step) {
      double s = 0, s2 = 0;
      int cnt = 0;
      for (size_t i = from; i < v.size(); i += step)
         s += v[i], s2 += v[i] * v[i], cnt++;
      const double mean = s / std::max(1, cnt);
      return mean > 0 ? std::sqrt(std::max(0.0, s2 / cnt - mean * mean)) / mean : 1.0;
   };
   // A star: every other corner pulled in.
   if (k >= 8 && k % 2 == 0)
   {
      double ro = 0, ri = 0;
      for (int i = 0; i < k; i++)
         (i % 2 == 0 ? ro : ri) += rad[i] / (k / 2);
      if (ri > ro)
         std::swap(ri, ro);
      if (ri < 0.75 * ro)
      {
         const int outer = rad[0] > rad[1] ? 0 : 1;
         const double a0 = std::atan2(poly[outer].y - c.y, poly[outer].x - c.x);
         std::vector<V2> q;
         for (int i = 0; i < k; i++)
         {
            const double r = i % 2 == 0 ? ro : ri, a = a0 + 2 * kPiD * i / k;
            q.push_back(c + V2{ std::cos(a), std::sin(a) } * r);
         }
         PathFromPoly(exact ? q : poly, true, out);
         return kSnapStar;
      }
   }
   const bool regular = exact || (cv(side, 0, 1) < 0.15 && cv(rad, 0, 1) < 0.15);
   std::vector<V2> q = poly;
   if (regular)
   {
      double r = 0;
      for (double v : rad)
         r += v / k;
      const double a0 = std::atan2(poly[0].y - c.y, poly[0].x - c.x);
      q.clear();
      for (int i = 0; i < k; i++)
      {
         const double a = a0 + 2 * kPiD * i / k;
         q.push_back(c + V2{ std::cos(a), std::sin(a) } * r);
      }
   }
   PathFromPoly(q, true, out);
   return k == 3 ? kSnapTriangle : kSnapPolygon;
}

// ---- Masks to outlines (shared by vector fill and trace) -----------------------------------------------------

namespace
{
   // The outer boundary of the 4-connected region `id` in `lab` (w x h) starting at its first pixel in raster
   // order, as the midpoints of its unit edges (which removes the pixel staircase before the fit).
   std::vector<float> TraceOuter(const std::vector<int>& lab, int w, int h, int id, int sx, int sy)
   {
      auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && lab[(size_t)y * w + x] == id; };
      static const int kMove[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
      // For heading d at corner (x, y): the pixel ahead on the right, and ahead on the left.
      static const int kRight[4][2] = { { 0, 0 }, { -1, 0 }, { -1, -1 }, { 0, -1 } };
      static const int kLeft[4][2] = { { 0, -1 }, { 0, 0 }, { -1, 0 }, { -1, -1 } };
      std::vector<float> mid;
      int x = sx, y = sy, d = 0;
      const size_t cap = (size_t)4 * w * h + 8;
      for (size_t step = 0; step < cap; step++)
      {
         const bool r = in(x + kRight[d][0], y + kRight[d][1]), l = in(x + kLeft[d][0], y + kLeft[d][1]);
         if (!r)
            d = (d + 1) % 4;
         else if (l)
            d = (d + 3) % 4;
         mid.push_back(x + kMove[d][0] * 0.5f);
         mid.push_back(y + kMove[d][1] * 0.5f);
         x += kMove[d][0];
         y += kMove[d][1];
         if (x == sx && y == sy && d == 0)
            break;
         if (x == sx && y == sy)
         {
            // Back at the start heading another way: one more decision closes it.
            const bool r2 = in(x + kRight[d][0], y + kRight[d][1]), l2 = in(x + kLeft[d][0], y + kLeft[d][1]);
            int nd = d;
            if (!r2)
               nd = (d + 1) % 4;
            else if (l2)
               nd = (d + 3) % 4;
            if (nd == 0)
               break;
         }
      }
      return mid;
   }

   // Connected regions (4-neighbour) of equal value in `cls`; returns the count, fills `lab` with 0..count-1.
   int LabelRegions(const std::vector<int>& cls, int w, int h, std::vector<int>& lab, std::vector<int>& area, std::vector<int>& first)
   {
      lab.assign((size_t)w * h, -1);
      area.clear();
      first.clear();
      std::vector<int> stack;
      int count = 0;
      for (int i = 0; i < w * h; i++)
      {
         if (lab[i] >= 0)
            continue;
         const int v = cls[i];
         lab[i] = count;
         stack.assign(1, i);
         int a = 0;
         while (!stack.empty())
         {
            const int q = stack.back();
            stack.pop_back();
            a++;
            const int qx = q % w, qy = q / w;
            const int nb[4] = { qx > 0 ? q - 1 : -1, qx + 1 < w ? q + 1 : -1, qy > 0 ? q - w : -1, qy + 1 < h ? q + w : -1 };
            for (int nq : nb)
               if (nq >= 0 && lab[nq] < 0 && cls[nq] == v)
               {
                  lab[nq] = count;
                  stack.push_back(nq);
               }
         }
         area.push_back(a);
         first.push_back(i);
         count++;
      }
      return count;
   }

   // The area a closed polyline encloses (shoelace).
   double PolyArea(const std::vector<float>& xy)
   {
      double a = 0;
      const size_t n = xy.size() / 2;
      for (size_t i = 0; i < n; i++)
      {
         const size_t j = (i + 1) % n;
         a += (double)xy[i * 2] * xy[j * 2 + 1] - (double)xy[j * 2] * xy[i * 2 + 1];
      }
      return std::fabs(a) * 0.5;
   }
}

namespace
{
   // A traced outline (edge midpoints on a pixel lattice) relaxed once with 1/4 1/2 1/4: the steps left on shallow
   // diagonals go, a corner moves by under half a pixel. Then fitted with a corner window wide enough to see past steps.
   constexpr float kTraceWindow = 4.0f;
   void Relax(std::vector<float>& xy)
   {
      const size_t n = xy.size() / 2;
      if (n < 4)
         return;
      std::vector<float> o(xy.size());
      for (size_t i = 0; i < n; i++)
      {
         const size_t a = (i + n - 1) % n, b = (i + 1) % n;
         o[i * 2] = 0.25f * xy[a * 2] + 0.5f * xy[i * 2] + 0.25f * xy[b * 2];
         o[i * 2 + 1] = 0.25f * xy[a * 2 + 1] + 0.5f * xy[i * 2 + 1] + 0.25f * xy[b * 2 + 1];
      }
      xy.swap(o);
   }
}

// ---- Vector region fill (Live Paint style) ----------------------------------------------------------------

bool Scene2DNode::VectorFill(int id, float x, float y, float gap, const float rgba[4])
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Vector || l->shapes.empty())
      return false;
   float bx = 0, by = 0;
   DocToBox(*l, x, y, bx, by);
   // Every item's outline in box px, and the region that holds them.
   std::vector<std::pair<std::vector<float>, float>> lines;
   float lo[2] = { bx, by }, hi[2] = { bx, by };
   for (const Shape& s : l->shapes)
   {
      std::vector<std::vector<float>> o;
      Outline(s, o);
      const float wdt = s.strokeOn ? s.width : 1.0f;
      for (auto& line : o)
      {
         if (s.closed || s.type != Shape::kPath)
            if (line.size() >= 4 && (line[0] != line[line.size() - 2] || line[1] != line[line.size() - 1]) && s.type != Shape::kLine)
            {
               line.push_back(line[0]);
               line.push_back(line[1]);
            }
         for (size_t i = 0; i + 1 < line.size(); i += 2)
         {
            lo[0] = std::min(lo[0], line[i]), lo[1] = std::min(lo[1], line[i + 1]);
            hi[0] = std::max(hi[0], line[i]), hi[1] = std::max(hi[1], line[i + 1]);
         }
         lines.push_back({ std::move(line), wdt });
      }
   }
   // The fill's edge hides under the lines, so it may wander by half the thinnest one.
   float thin = 1e30f;
   for (const auto& ln : lines)
      thin = std::min(thin, ln.second);
   const float pad = gap + 4.0f;
   lo[0] -= pad, lo[1] -= pad, hi[0] += pad, hi[1] += pad;
   // One cell per box px, coarser when the drawing is huge.
   const float span = std::max(hi[0] - lo[0], hi[1] - lo[1]);
   const float cell = std::max(1.0f, span / kVectorFillGridMax);
   const int w = std::max(2, (int)std::ceil((hi[0] - lo[0]) / cell)), h = std::max(2, (int)std::ceil((hi[1] - lo[1]) / cell));
   std::vector<int> wall((size_t)w * h, 0);
   // Each line widened by half its stroke plus half the gap to close: what the fill must not cross.
   for (const auto& [line, wdt] : lines)
   {
      const float r = (wdt * 0.5f + gap * 0.5f) / cell + 0.5f;
      for (size_t i = 0; i + 3 < line.size(); i += 2)
      {
         const float ax = (line[i] - lo[0]) / cell, ay = (line[i + 1] - lo[1]) / cell;
         const float cx = (line[i + 2] - lo[0]) / cell, cy = (line[i + 3] - lo[1]) / cell;
         const int x0 = std::max(0, (int)std::floor(std::min(ax, cx) - r)), x1 = std::min(w - 1, (int)std::ceil(std::max(ax, cx) + r));
         const int y0 = std::max(0, (int)std::floor(std::min(ay, cy) - r)), y1 = std::min(h - 1, (int)std::ceil(std::max(ay, cy) + r));
         for (int yy = y0; yy <= y1; yy++)
            for (int xx = x0; xx <= x1; xx++)
               if (SegDist({ xx + 0.5, yy + 0.5 }, { ax, ay }, { cx, cy }) <= r)
                  wall[(size_t)yy * w + xx] = 1;
      }
   }
   const int sx = (int)((bx - lo[0]) / cell), sy = (int)((by - lo[1]) / cell);
   if (sx < 0 || sy < 0 || sx >= w || sy >= h || wall[(size_t)sy * w + sx])
      return false;
   // Flood from the click; reaching the border means the click was outside every closed region.
   std::vector<int> cls((size_t)w * h, 0);
   std::vector<int> stack{ sy * w + sx };
   cls[(size_t)sy * w + sx] = 1;
   while (!stack.empty())
   {
      const int q = stack.back();
      stack.pop_back();
      const int qx = q % w, qy = q / w;
      if (qx == 0 || qy == 0 || qx == w - 1 || qy == h - 1)
         return false;
      for (int nq : { q - 1, q + 1, q - w, q + w })
         if (!cls[nq] && !wall[nq])
         {
            cls[nq] = 1;
            stack.push_back(nq);
         }
   }
   // Grow the region back under the lines (by what the walls took), so the fill meets the strokes' centres.
   const int grow = (int)std::lround(gap * 0.5f / cell + 1.0f);
   for (int g = 0; g < grow; g++)
   {
      std::vector<int> next = cls;
      for (int q = 0; q < w * h; q++)
         if (!cls[q])
         {
            const int qx = q % w, qy = q / w;
            if ((qx > 0 && cls[q - 1]) || (qx + 1 < w && cls[q + 1]) || (qy > 0 && cls[q - w]) || (qy + 1 < h && cls[q + w]))
               next[q] = 1;
         }
      cls.swap(next);
   }
   int first = -1;
   for (int q = 0; q < w * h && first < 0; q++)
      if (cls[q])
         first = q;
   if (first < 0)
      return false;
   std::vector<float> outline = TraceOuter(cls, w, h, 1, first % w, first / w);
   for (size_t i = 0; i + 1 < outline.size(); i += 2)
   {
      outline[i] = lo[0] + outline[i] * cell;
      outline[i + 1] = lo[1] + outline[i + 1] * cell;
   }
   Relax(outline);
   Shape fill;
   FitPath(outline, true, std::max({ 0.75f, cell, thin * 0.5f + 0.5f }), kTraceCornerDeg, fill.pts, kTraceWindow * cell);
   if (fill.Anchors() < 2)
      return false;
   fill.type = Shape::kPath;
   fill.closed = true;
   fill.fillOn = true;
   std::copy(rgba, rgba + 4, fill.fillC);
   fill.strokeOn = false;
   // Under the strokes, as a painted region sits under its lines.
   l->shapes.insert(l->shapes.begin(), fill);
   Commit();
   return true;
}

// ---- Image Trace -------------------------------------------------------------------------------------------

int Scene2DNode::TracePixels(const uint8_t* rgba, int w, int h, const TraceOpts& o, std::vector<Shape>& out)
{
   out.clear();
   if (rgba == nullptr || w <= 0 || h <= 0)
      return 0;
   const int n = w * h;
   // Transparent pixels are class -1 (never traced).
   std::vector<int> cls(n, -1);
   std::vector<std::array<float, 4>> palette;
   const int k = o.mode == kTraceColour ? std::clamp(o.colours, 2, kTraceColoursMax) : 2;
   if (o.mode != kTraceColour)
   {
      // B&W and line art: dark ink on what is left; only the ink is traced.
      for (int i = 0; i < n; i++)
      {
         const uint8_t* p = rgba + (size_t)i * 4;
         if (p[3] < 128)
            continue;
         const float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
         cls[i] = lum < o.threshold ? 0 : 1;
      }
      palette = { { 0, 0, 0, 1 }, { 1, 1, 1, 1 } };
   }
   else
   {
      // k-means in RGB on a sample, seeded by farthest points (deterministic).
      std::vector<std::array<float, 3>> samp;
      const int stride = std::max(1, (int)std::sqrt((double)n / 65536.0));
      for (int y = 0; y < h; y += stride)
         for (int x = 0; x < w; x += stride)
         {
            const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
            if (p[3] >= 128)
               samp.push_back({ (float)p[0], (float)p[1], (float)p[2] });
         }
      if (samp.empty())
         return 0;
      std::vector<std::array<float, 3>> cent{ samp[0] };
      std::vector<float> dist(samp.size(), 1e30f);
      while ((int)cent.size() < k)
      {
         size_t far = 0;
         for (size_t i = 0; i < samp.size(); i++)
         {
            const auto& c = cent.back();
            const float d = (samp[i][0] - c[0]) * (samp[i][0] - c[0]) + (samp[i][1] - c[1]) * (samp[i][1] - c[1]) + (samp[i][2] - c[2]) * (samp[i][2] - c[2]);
            dist[i] = std::min(dist[i], d);
            if (dist[i] > dist[far])
               far = i;
         }
         if (dist[far] <= 1.0f)
            break;
         cent.push_back(samp[far]);
      }
      std::vector<int> asg(samp.size(), 0);
      auto nearest = [&](float r, float g, float b) {
         int best = 0;
         float bd = 1e30f;
         for (size_t c = 0; c < cent.size(); c++)
         {
            const float d = (r - cent[c][0]) * (r - cent[c][0]) + (g - cent[c][1]) * (g - cent[c][1]) + (b - cent[c][2]) * (b - cent[c][2]);
            if (d < bd)
               bd = d, best = (int)c;
         }
         return best;
      };
      for (int it = 0; it < 12; it++)
      {
         std::vector<std::array<double, 4>> acc(cent.size(), { 0, 0, 0, 0 });
         for (size_t i = 0; i < samp.size(); i++)
         {
            asg[i] = nearest(samp[i][0], samp[i][1], samp[i][2]);
            auto& a = acc[asg[i]];
            a[0] += samp[i][0], a[1] += samp[i][1], a[2] += samp[i][2], a[3] += 1;
         }
         for (size_t c = 0; c < cent.size(); c++)
            if (acc[c][3] > 0)
               cent[c] = { (float)(acc[c][0] / acc[c][3]), (float)(acc[c][1] / acc[c][3]), (float)(acc[c][2] / acc[c][3]) };
      }
      for (int i = 0; i < n; i++)
      {
         const uint8_t* p = rgba + (size_t)i * 4;
         if (p[3] >= 128)
            cls[i] = nearest(p[0], p[1], p[2]);
      }
      for (const auto& c : cent)
         palette.push_back({ c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f });
   }
   std::vector<int> lab, area, first;
   const int regions = LabelRegions(cls, w, h, lab, area, first);
   // Each region's outer outline, filled whole (holes ignored): a region inside another encloses less, so it is
   // drawn later, on top, and the stack shows no gaps (VTracer's stacked mode).
   struct Piece { double filled; std::vector<float> line; int cls; };
   std::vector<Piece> pieces;
   for (int r = 0; r < regions; r++)
   {
      const int c = cls[first[r]];
      if (c < 0 || area[r] < std::max(1, o.speckle))
         continue;
      if (o.mode != kTraceColour && c != 0)
         continue;
      std::vector<float> line = TraceOuter(lab, w, h, r, first[r] % w, first[r] / w);
      if (line.size() < 6)
         continue;
      const double filled = PolyArea(line);
      pieces.push_back({ filled, std::move(line), c });
      if ((int)pieces.size() >= kTraceShapesMax)
         break;
   }
   std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.filled > b.filled; });
   for (const Piece& pc : pieces)
   {
      Shape s;
      s.type = Shape::kPath;
      s.closed = true;
      std::vector<float> line = pc.line;
      Relax(line);
      FitPath(line, true, std::max(0.2f, o.tol), o.corner, s.pts, kTraceWindow);
      if (s.Anchors() < 2)
         continue;
      s.fillOn = true;
      std::copy(palette[pc.cls].begin(), palette[pc.cls].end(), s.fillC);
      s.strokeOn = false;
      out.push_back(std::move(s));
   }
   // Holes in B&W ink would fill solid: cut them back out with the paper's regions inside the ink.
   if (o.mode != kTraceColour)
   {
      std::vector<Shape> holes;
      for (int r = 0; r < regions; r++)
      {
         const int c = cls[first[r]];
         if (c != 1 || area[r] < std::max(1, o.speckle))
            continue;
         const int fx = first[r] % w, fy = first[r] / w;
         // Paper touching the edge is the background, not a hole.
         bool edge = false;
         for (int i = 0; i < n && !edge; i++)
            if (lab[i] == r)
            {
               const int x = i % w, y = i / w;
               edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
            }
         if (edge)
            continue;
         Shape s;
         s.type = Shape::kPath;
         s.closed = true;
         std::vector<float> line = TraceOuter(lab, w, h, r, fx, fy);
         Relax(line);
         FitPath(line, true, std::max(0.2f, o.tol), o.corner, s.pts, kTraceWindow);
         if (s.Anchors() < 2)
            continue;
         s.fillOn = true;
         s.op = Shape::kSubtract;
         holes.push_back(std::move(s));
      }
      for (Shape& s : holes)
         out.push_back(std::move(s));
   }
   return (int)out.size();
}

int Scene2DNode::TraceLayer(int id, int frameId, const TraceOpts& o)
{
   Layer* src = FindLayer(id);
   if (src == nullptr)
      return 0;
   std::vector<uint8_t> top;
   if (!RenderLayerAlone(id, frameId, top) || top.size() != (size_t)mDoc.w * mDoc.h * 4)
      return 0;
   std::vector<Shape> shapes;
   if (TracePixels(top.data(), mDoc.w, mDoc.h, o, shapes) == 0)
      return 0;
   const std::string name = (src->name.empty() ? std::string("Layer") : src->name) + " trace";
   const int vid = AddVectorLayer(id);
   Layer* dst = FindLayer(vid);
   if (dst == nullptr)
      return 0;
   // The new layer's box is the document: map doc px into it in case its transform differs.
   for (Shape& s : shapes)
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         float bx = 0, by = 0;
         DocToBox(*dst, s.pts[i], s.pts[i + 1], bx, by);
         s.pts[i] = bx, s.pts[i + 1] = by;
      }
   dst->shapes = std::move(shapes);
   dst->name = name;
   Commit();
   return vid;
}

// ---- Paths (Paths panel) -----------------------------------------------------------------------------------

const Scene2DNode::DocPath* Scene2DNode::FindPath(const std::string& name) const
{
   for (const DocPath& p : mPaths)
      if (p.name == name)
         return &p;
   return nullptr;
}

namespace
{
   bool PathInRange(const std::vector<Scene2DNode::Shape>& subs)
   {
      if (subs.empty())
         return false;
      for (const Scene2DNode::Shape& s : subs)
      {
         if (s.type != Scene2DNode::Shape::kPath || s.pts.size() < 12 || s.pts.size() % 6 != 0)
            return false;
         for (float v : s.pts)
            if (!std::isfinite(v) || std::fabs(v) > Scene2DNode::kPathCoordMax)
               return false;
      }
      return true;
   }
}

bool Scene2DNode::SetWorkPath(std::vector<Shape> subs)
{
   Layers();
   if (!PathInRange(subs))
      return false;
   auto it = std::find_if(mPaths.begin(), mPaths.end(), [](const DocPath& p) { return p.name == kWorkPath; });
   if (it == mPaths.end())
      it = mPaths.insert(mPaths.end(), DocPath{ kWorkPath, {} });
   it->subs = std::move(subs);
   Commit();
   return true;
}

bool Scene2DNode::RenamePath(const std::string& from, const std::string& to)
{
   Layers();
   if (to.empty() || to == kWorkPath || from == to || FindPath(to) != nullptr)
      return false;
   auto it = std::find_if(mPaths.begin(), mPaths.end(), [&](const DocPath& p) { return p.name == from; });
   if (it == mPaths.end())
      return false;
   it->name = to;
   if (mClipPath == from)
      mClipPath = to;
   Commit();
   return true;
}

bool Scene2DNode::DeletePath(const std::string& name)
{
   Layers();
   auto it = std::find_if(mPaths.begin(), mPaths.end(), [&](const DocPath& p) { return p.name == name; });
   if (it == mPaths.end())
      return false;
   mPaths.erase(it);
   if (mClipPath == name)
      mClipPath.clear();
   Commit();
   return true;
}

bool Scene2DNode::SetClippingPath(const std::string& name)
{
   Layers();
   if (!name.empty() && (name == kWorkPath || FindPath(name) == nullptr))
      return false;
   mClipPath = name;
   Commit();
   return true;
}

void Scene2DNode::PathPolylines(const DocPath& p, std::vector<std::vector<float>>& lines, std::vector<int>& ops)
{
   lines.clear();
   ops.clear();
   for (const Shape& s : p.subs)
   {
      std::vector<std::vector<float>> o;
      Outline(s, o);
      for (auto& line : o)
         if (line.size() >= 4)
         {
            lines.push_back(std::move(line));
            ops.push_back(s.op);
         }
   }
}

bool Scene2DNode::PathMask(const DocPath& p, std::vector<uint8_t>& out, float feather, bool aa)
{
   const int W = mDoc.w, H = mDoc.h;
   out.assign((size_t)W * H, 0);
   std::vector<std::vector<float>> lines;
   std::vector<int> ops;
   PathPolylines(p, lines, ops);
   std::vector<uint8_t> t;
   bool first = true;
   for (size_t i = 0; i < lines.size(); i++)
   {
      if (lines[i].size() < 6)
         continue;
      PolygonMask(lines[i], W, H, t, aa);
      const int op = first && ops[i] != Shape::kSubtract ? Shape::kAdd : ops[i];
      first = false;
      for (size_t k = 0; k < out.size(); k++)
      {
         const int a = out[k], b = t[k];
         if (op == Shape::kAdd) out[k] = (uint8_t)std::max(a, b);
         else if (op == Shape::kSubtract) out[k] = (uint8_t)(a * (255 - b) / 255);
         else if (op == Shape::kIntersect) out[k] = (uint8_t)std::min(a, b);
         else out[k] = (uint8_t)std::abs(a - b);
      }
   }
   if (feather > 0.5f)
      FeatherMask(out, W, H, feather);
   return std::any_of(out.begin(), out.end(), [](uint8_t v) { return v != 0; });
}

bool Scene2DNode::PathToSelection(const std::string& name, int op, float feather, bool aa)
{
   Layers();
   const DocPath* p = FindPath(name);
   std::vector<uint8_t> m;
   if (p == nullptr || !PathMask(*p, m, feather, aa))
      return false;
   CombineSelection(m, op);
   Commit();
   return true;
}

bool Scene2DNode::MakeWorkPathFromSelection(float tol)
{
   Layers();
   const int W = mDoc.w, H = mDoc.h;
   if (!mSelAny || mSel.size() != (size_t)W * H)
      return false;
   // Half covered and up is inside (Photoshop's Make Work Path reads the selection at 50%).
   std::vector<int> cls((size_t)W * H);
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
         cls[(size_t)y * W + x] = mSel[(size_t)(H - 1 - y) * W + x] >= 128 ? 1 : 0;
   std::vector<int> lab, area, first;
   const int regions = LabelRegions(cls, W, H, lab, area, first);
   std::vector<char> edge(regions, 0);
   for (int x = 0; x < W; x++)
      edge[lab[x]] = edge[lab[(size_t)(H - 1) * W + x]] = 1;
   for (int y = 0; y < H; y++)
      edge[lab[(size_t)y * W]] = edge[lab[(size_t)y * W + W - 1]] = 1;
   // Outlines of the selected regions and the holes inside them, biggest first, so each cut follows what it cuts.
   struct Piece { double filled; std::vector<float> line; bool hole; };
   std::vector<Piece> pieces;
   for (int r = 0; r < regions; r++)
   {
      const bool in = cls[first[r]] == 1;
      if (!in && edge[r])
         continue;
      std::vector<float> line = TraceOuter(lab, W, H, r, first[r] % W, first[r] / W);
      if (line.size() < 6)
         continue;
      const double filled = PolyArea(line);
      pieces.push_back({ filled, std::move(line), !in });
      if ((int)pieces.size() >= kTraceShapesMax)
         break;
   }
   std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.filled > b.filled; });
   std::vector<Shape> subs;
   for (Piece& pc : pieces)
   {
      Shape s;
      s.type = Shape::kPath;
      s.closed = true;
      Relax(pc.line);
      FitPath(pc.line, true, std::max(0.5f, tol), kTraceCornerDeg, s.pts, kTraceWindow);
      if (s.Anchors() < 2)
         continue;
      s.op = pc.hole ? Shape::kSubtract : Shape::kAdd;
      subs.push_back(std::move(s));
   }
   return SetWorkPath(std::move(subs));
}

bool Scene2DNode::MakeWorkPathFromLayer(int id)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Vector || l->shapes.empty())
      return false;
   std::vector<Shape> subs;
   for (const Shape& s : l->shapes)
   {
      Shape p;
      p.type = Shape::kPath;
      p.op = s.op;
      if (s.type == Shape::kPath)
      {
         p.pts = s.pts;
         p.closed = s.closed;
      }
      else
      {
         // Primitives as their outline, fitted with corners kept (a rectangle is four straight sides).
         std::vector<std::vector<float>> o;
         Outline(s, o);
         if (o.empty() || o[0].size() < 4)
            continue;
         p.closed = s.type != Shape::kLine;
         FitPath(o[0], p.closed, 0.25f, kTraceCornerDeg, p.pts);
      }
      if (p.Anchors() < 2)
         continue;
      for (size_t i = 0; i + 1 < p.pts.size(); i += 2)
      {
         float x = 0, y = 0;
         BoxToDoc(*l, p.pts[i], p.pts[i + 1], x, y);
         p.pts[i] = x, p.pts[i + 1] = y;
      }
      subs.push_back(std::move(p));
   }
   return SetWorkPath(std::move(subs));
}

bool Scene2DNode::TransformPath(const std::string& name, const float m[6])
{
   Layers();
   auto it = std::find_if(mPaths.begin(), mPaths.end(), [&](const DocPath& p) { return p.name == name; });
   if (it == mPaths.end())
      return false;
   DocPath moved = *it;
   for (Shape& s : moved.subs)
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         const float x = s.pts[i], y = s.pts[i + 1];
         s.pts[i] = m[0] * x + m[1] * y + m[2];
         s.pts[i + 1] = m[3] * x + m[4] * y + m[5];
      }
   if (!PathInRange(moved.subs))
      return false;
   *it = std::move(moved);
   Commit();
   return true;
}

bool Scene2DNode::FillPath(const std::string& name, int id, const float rgb[3], float opacity, float feather)
{
   Layers();
   const DocPath* p = FindPath(name);
   Layer* l = FindLayer(id);
   std::vector<uint8_t> m;
   if (p == nullptr || l == nullptr || l->kind != Kind::Paint || !PathMask(*p, m, feather, true))
      return false;
   // Inside the selection too: the fill runs through the selection, so the path stands in for it for one fill.
   if (mSelAny && mSel.size() == m.size())
      for (size_t i = 0; i < m.size(); i++)
         m[i] = (uint8_t)std::min(m[i], mSel[i]);
   if (std::none_of(m.begin(), m.end(), [](uint8_t v) { return v != 0; }))
      return false;
   std::vector<uint8_t> keepSel = mSel;
   const bool keepAny = mSelAny;
   const SelApron keepApron = mSelApron;
   mSel = std::move(m);
   mSelAny = true;
   mSelDirty = true;
   const bool ok = FillPixels(id, rgb, opacity);
   mSel = std::move(keepSel);
   mSelAny = keepAny;
   mSelApron = keepApron;
   mSelDirty = true;
   Commit();
   return ok;
}

bool Scene2DNode::StrokePath(const std::string& name, int id, const BrushEngine::Brush& b, bool simulatePressure)
{
   Layers();
   const DocPath* p = FindPath(name);
   Layer* l = FindLayer(id);
   if (p == nullptr || l == nullptr || l->kind != Kind::Paint || Painting())
      return false;
   std::vector<std::vector<float>> lines;
   std::vector<int> ops;
   PathPolylines(*p, lines, ops);
   bool any = false;
   for (size_t li = 0; li < lines.size(); li++)
   {
      std::vector<float> line = lines[li];
      if (p->subs.size() == lines.size() && p->subs[li].closed && line.size() >= 4 &&
          (line[0] != line[line.size() - 2] || line[1] != line[line.size() - 1]))
      {
         line.push_back(line[0]);
         line.push_back(line[1]);
      }
      const size_t n = line.size() / 2;
      if (n < 2)
         continue;
      // Simulate Pressure: thin at both ends, full in the middle, by distance along the line.
      std::vector<float> along(n, 0.0f);
      for (size_t i = 1; i < n; i++)
         along[i] = along[i - 1] + (float)std::hypot(line[i * 2] - line[i * 2 - 2], line[i * 2 + 1] - line[i * 2 - 1]);
      const float len = std::max(along[n - 1], 1e-3f);
      auto pressure = [&](size_t i) { return simulatePressure ? (float)std::sin(kPiD * along[i] / len) : 1.0f; };
      BrushEngine::Brush bb = b;
      if (simulatePressure)
         bb.pressureSize = true;
      if (!BeginPaint(id, 0, bb, line[0], line[1], pressure(0)))
         continue;
      for (size_t i = 1; i < n; i++)
         PaintTo(line[i * 2], line[i * 2 + 1], pressure(i));
      EndPaint();
      any = true;
   }
   return any;
}

int Scene2DNode::PathToShapeLayer(const std::string& name, int above, const float rgba[4])
{
   Layers();
   const DocPath* found = FindPath(name);
   if (found == nullptr)
      return 0;
   const DocPath p = *found;
   const int vid = ShapesToShapeLayer(p.subs, above, rgba, false);
   if (Layer* dst = FindLayer(vid))
      dst->name = p.name == kWorkPath ? std::string("Shape") : p.name;
   return vid;
}

int Scene2DNode::ShapesToShapeLayer(const std::vector<Shape>& subs, int above, const float rgba[4], bool consumeWorkPath)
{
   Layers();
   if (subs.empty())
      return 0;
   for (const Shape& s : subs)
      if (s.Anchors() < 2)
      {
         fprintf(stderr, "PROBE [2d] path to shape refused: a subpath has %d anchors\n", s.Anchors());
         return 0;
      }
   const int vid = AddVectorLayer(above);
   Layer* dst = FindLayer(vid);
   if (dst == nullptr)
      return 0;
   for (Shape s : subs)
   {
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         float bx = 0, by = 0;
         DocToBox(*dst, s.pts[i], s.pts[i + 1], bx, by);
         s.pts[i] = bx, s.pts[i + 1] = by;
      }
      s.fillOn = true;
      std::copy(rgba, rgba + 4, s.fillC);
      s.strokeOn = false;
      dst->shapes.push_back(std::move(s));
   }
   dst->name = "Shape";
   if (consumeWorkPath)
      DeletePath(kWorkPath);
   fprintf(stderr, "PROBE [2d] path to shape: layer %d from %zu subpaths, consume %d\n", vid, subs.size(), consumeWorkPath ? 1 : 0);
   Commit();
   return vid;
}

// ---- Edit > Find and Replace Text, Layer Style scaling, Global Light, Paths to Illustrator ------------------------

int Scene2DNode::FindReplaceText(const std::string& find, const std::string& with, bool matchCase, const std::vector<int>& ids)
{
   Layers();
   if (find.empty())
      return 0;
   auto lower = [](std::string s) {
      for (char& c : s)
         c = (char)std::tolower((unsigned char)c);
      return s;
   };
   const std::string key = matchCase ? find : lower(find);
   std::vector<int> todo = ids;
   if (todo.empty())
      for (const Layer& l : mLayers)
         if (l.kind == Kind::Text)
            todo.push_back(l.id);
   int n = 0;
   for (int id : todo)
   {
      const Layer* l = FindConst(id);
      if (l == nullptr || l->kind != Kind::Text)
         continue;
      const std::string hay = matchCase ? l->text : lower(l->text);
      std::vector<size_t> at;
      for (size_t p = hay.find(key); p != std::string::npos; p = hay.find(key, p + key.size()))
         at.push_back(p);
      // From the end, so earlier offsets stay right.
      for (auto it = at.rbegin(); it != at.rend(); ++it)
         if (ReplaceText(id, *it, *it + find.size(), with))
            n++;
   }
   std::fprintf(stderr, "PROBE find and replace text: %d replaced\n", n);
   return n;
}

bool Scene2DNode::ScaleEffects(int id, float pct)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || !std::isfinite(pct) || pct <= 0.0f || !l->AnyFx())
      return false;
   const float k = pct / 100.0f;
   auto scale = [&](Fx& f) {
      f.size *= k;
      f.dist *= k;
   };
   for (Fx& f : l->fx)
      scale(f);
   for (auto& m : l->fxMore)
      scale(m.second);
   Commit();
   return true;
}

void Scene2DNode::SetGlobalLight(float angle)
{
   Layers();
   if (!std::isfinite(angle))
      return;
   const float a = std::remainder(angle, 360.0f);
   auto lit = [](int type) { return type == kFxShadow || type == kFxInner || type == kFxBevel || type == kFxSatin; };
   for (Layer& l : mLayers)
   {
      for (int k = 0; k < kFxCount; k++)
         if (lit(k))
            l.fx[k].angle = a;
      for (auto& m : l.fxMore)
         if (lit(m.first))
            m.second.angle = a;
   }
   Commit();
}

std::string Scene2DNode::PathsSvg()
{
   if (mPaths.empty())
      return "";
   std::ostringstream o;
   o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << mDoc.w << "\" height=\"" << mDoc.h << "\" viewBox=\"0 0 "
     << mDoc.w << " " << mDoc.h << "\">\n";
   for (const DocPath& p : mPaths)
   {
      std::vector<std::vector<float>> lines;
      std::vector<int> ops;
      PathPolylines(p, lines, ops);
      std::string d;
      for (const std::vector<float>& ln : lines)
      {
         for (size_t i = 0; i + 1 < ln.size(); i += 2)
            d += (i == 0 ? "M" : " L") + std::to_string(ln[i]) + " " + std::to_string(ln[i + 1]);
         if (!ln.empty())
            d += " Z ";
      }
      std::string id = p.name;
      for (char& c : id)
         if (!std::isalnum((unsigned char)c))
            c = '_';
      o << "  <path id=\"" << id << "\" d=\"" << d << "\" fill=\"none\" stroke=\"black\"/>\n";
   }
   o << "</svg>\n";
   return o.str();
}

bool Scene2DNode::VectorMaskFromPath(int id, const std::string& name)
{
   Layers();
   const DocPath* found = FindPath(name);
   Layer* l = FindLayer(id);
   if (found == nullptr || l == nullptr || found->subs.empty())
      return false;
   std::vector<Shape> mask;
   for (Shape s : found->subs)
   {
      for (size_t i = 0; i + 1 < s.pts.size(); i += 2)
      {
         float bx = 0, by = 0;
         DocToBox(*l, s.pts[i], s.pts[i + 1], bx, by);
         s.pts[i] = bx, s.pts[i + 1] = by;
      }
      s.fillOn = true;
      s.strokeOn = false;
      mask.push_back(std::move(s));
   }
   l->vmask = std::move(mask);
   l->hasVMask = true;
   l->vmaskOn = true;
   fprintf(stderr, "PROBE vector mask from path: layer %d, %zu subpaths\n", id, l->vmask.size());
   Commit();
   return true;
}

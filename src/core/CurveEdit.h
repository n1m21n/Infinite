#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/ModShapes.h"

// The Curve Editor's edits (docs/plans/curve-editor/README.md §8.4-§8.5) as
// pure functions on an MSEG point list: sorted by x, first point at x = 0,
// last at x = 1, every x and y in 0..1. The panel calls them on the node's
// points and then MSEGNode::Normalize(); nothing here touches a node, so the
// agent API and the tests use the same maths. Main thread only (allocates).
namespace CurveEdit
{
   using Point = ModShapes::MsegPoint;
   using Points = std::vector<Point>;

   inline void Invert(Points& pts)
   {
      for (Point& p : pts) { p.y = 1.0f - p.y; p.ly = -p.ly; p.ry = -p.ry; }
   }

   inline void Reverse(Points& pts)
   {
      const Points before = pts;
      for (size_t i = 0; i < pts.size(); ++i)
      {
         const size_t j = pts.size() - 1 - i;
         Point p = before[j]; p.x = 1.0f - p.x;
         p.lx = -before[j].rx; p.ly = before[j].ry;
         p.rx = -before[j].lx; p.ry = before[j].ly;
         if (j > 0)
         {
            p.interp = before[j-1].interp;
            p.curve = -before[j-1].curve;
            p.easing = ModShapes::MsegEaseReversed(before[j-1].easing);
         }
         pts[i] = p;
      }
   }

   inline void Normalise(Points& pts)
   {
      if (pts.empty()) return;
      float lo = 1.0f, hi = 0.0f;
      for (const Point& p : pts) { lo = std::min(lo,p.y); hi = std::max(hi,p.y); }
      if (hi-lo < 1.0e-6f) return;
      for (Point& p : pts) { p.y = (p.y-lo)/(hi-lo); p.ly /= hi-lo; p.ry /= hi-lo; }
   }

   inline void Smooth(Points& pts, const std::vector<int>& selected, float strength = 0.5f)
   {
      const Points before = pts;
      for (int i : selected)
         if (i > 0 && i+1 < (int)pts.size())
         {
            const float dx = before[i+1].x-before[i-1].x;
            const float t = dx > 1e-6f ? (before[i].x-before[i-1].x)/dx : .5f;
            const float target = before[i-1].y*(1-t)+before[i+1].y*t;
            pts[i].y += (target-before[i].y)*strength;
         }
   }

   struct XY
   {
      float x = 0.0f, y = 0.0f;
   };

   // x rounded to the nearest multiple of `grid` (a fraction of the length);
   // grid <= 0 leaves it alone.
   inline float SnapX(float x, float grid)
   {
      if (!(grid > 0.0f))
         return x;
      return std::clamp(std::round(x / grid) * grid, 0.0f, 1.0f);
   }

   // Ramer-Douglas-Peucker: the fewest of `in` that stay within `tolerance`
   // (shape units, measured vertically, since a curve is read along x) of
   // the stroke. Keeps the first and last.
   inline std::vector<XY> Simplify(const std::vector<XY>& in, float tolerance)
   {
      const int n = (int)in.size();
      if (n <= 2)
         return in;
      std::vector<char> keep(n, 0);
      keep[0] = keep[n - 1] = 1;
      std::vector<std::pair<int, int>> stack { { 0, n - 1 } };
      while (!stack.empty())
      {
         const auto [a, b] = stack.back();
         stack.pop_back();
         float worst = -1.0f;
         int at = -1;
         const float dx = in[b].x - in[a].x;
         for (int i = a + 1; i < b; i++)
         {
            const float t = dx > 1.0e-9f ? (in[i].x - in[a].x) / dx : 0.5f;
            const float d = std::fabs(in[i].y - (in[a].y + (in[b].y - in[a].y) * t));
            if (d > worst)
            {
               worst = d;
               at = i;
            }
         }
         if (at >= 0 && worst > tolerance)
         {
            keep[at] = 1;
            stack.push_back({ a, at });
            stack.push_back({ at, b });
         }
      }
      std::vector<XY> out;
      for (int i = 0; i < n; i++)
         if (keep[i])
            out.push_back(in[i]);
      return out;
   }

   // A freehand stroke as it was drawn (any order, any x) to the points a
   // curve can hold: x sorted, one y per x (a later pass over the same x
   // wins), clamped to 0..1.
   inline std::vector<XY> CleanStroke(std::vector<XY> s)
   {
      for (XY& p : s)
      {
         p.x = std::clamp(p.x, 0.0f, 1.0f);
         p.y = std::clamp(p.y, 0.0f, 1.0f);
      }
      std::stable_sort(s.begin(), s.end(), [](const XY& a, const XY& b) { return a.x < b.x; });
      std::vector<XY> out;
      for (const XY& p : s)
      {
         if (!out.empty() && std::fabs(p.x - out.back().x) < 1.0e-6f)
            out.back() = p;
         else
            out.push_back(p);
      }
      return out;
   }

   // Remove the inner points with x inside [x0, x1] (the two end points stay
   // and take the new value when the range reaches them). Returns how many
   // went.
   inline int EraseRange(Points& pts, float x0, float x1)
   {
      if (x1 < x0)
         std::swap(x0, x1);
      const size_t before = pts.size();
      if (pts.size() <= 2)
         return 0;
      pts.erase(std::remove_if(pts.begin() + 1, pts.end() - 1, [&](const Point& p) { return p.x >= x0 && p.x <= x1; }), pts.end() - 1);
      return (int)(before - pts.size());
   }

   // Replace everything inside [first.x, last.x] of `with` by `with` (sorted
   // by x): the Line tool's ramp and the Draw tool's stroke. Points that land
   // on an end (x 0 or 1) become that end. New points take `interp`. Keeps
   // at most `maxPoints`, dropping from the new run's middle first.
   inline void ReplaceRange(Points& pts, const std::vector<XY>& with, int interp, int maxPoints)
   {
      if (with.empty())
         return;
      const float x0 = with.front().x, x1 = with.back().x;
      EraseRange(pts, x0, x1);
      std::vector<XY> run = with;
      const int room = std::max(0, maxPoints - (int)pts.size());
      while ((int)run.size() > room + 2 && run.size() > 2) // the ends may land on the curve's own ends
         run.erase(run.begin() + (long)run.size() / 2);
      for (const XY& q : run)
      {
         Point p;
         p.x = std::clamp(q.x, 0.0f, 1.0f);
         p.y = std::clamp(q.y, 0.0f, 1.0f);
         p.interp = (uint8_t)interp;
         if (p.x <= 0.0f && !pts.empty() && pts.front().x <= 0.0f)
         {
            pts.front().y = p.y;
            pts.front().interp = p.interp;
            continue;
         }
         if (p.x >= 1.0f && !pts.empty() && pts.back().x >= 1.0f)
         {
            pts.back().y = p.y;
            continue;
         }
         if ((int)pts.size() >= maxPoints)
            continue;
         const auto at = std::upper_bound(pts.begin(), pts.end(), p.x, [](float x, const Point& e) { return x < e.x; });
         pts.insert(at, p);
      }
   }

   // Move the x of the points in `sel` (indices) to the grid; the ends stay
   // pinned and a point never passes a neighbour.
   inline void QuantiseX(Points& pts, const std::vector<int>& sel, float grid)
   {
      if (!(grid > 0.0f))
         return;
      for (int i : sel)
      {
         if (i <= 0 || i >= (int)pts.size() - 1)
            continue;
         pts[i].x = std::clamp(SnapX(pts[i].x, grid), pts[i - 1].x, pts[i + 1].x);
      }
   }

   // Keep only the part between a and b (0..1) and stretch it to the whole
   // length: the cut ends get points carrying the curve's value there. A cut
   // bend stays the same shape (part of e^(kt) is e^(k't) with k scaled by
   // the part kept); handles scale with the stretch. False (and no change)
   // for a part shorter than 1/1000 of the length.
   inline bool Crop(Points& pts, float a, float b)
   {
      if (b < a)
         std::swap(a, b);
      a = std::clamp(a, 0.0f, 1.0f);
      b = std::clamp(b, 0.0f, 1.0f);
      const int n = (int)pts.size();
      if (b - a < 1.0e-3f || n < 2)
         return false;
      const int ia = ModShapes::MsegFindSegment(pts.data(), n, a, -1);
      const int ib = ModShapes::MsegFindSegment(pts.data(), n, b, -1);
      auto spanOf = [&](int i) { return std::max(1.0e-9f, pts[i + 1].x - pts[i].x); };
      Point first = pts[ia];
      first.x = a;
      first.y = ModShapes::MsegEvaluate(pts.data(), n, a);
      first.lx = first.ly = 0.0f;
      const float firstEnd = ia == ib ? b : pts[ia + 1].x;
      first.curve = pts[ia].curve * (firstEnd - a) / spanOf(ia);
      if (a > pts[ia].x + 1.0e-6f)
         first.rx *= (firstEnd - a) / spanOf(ia);
      Point last;
      last.x = b;
      last.y = ModShapes::MsegEvaluate(pts.data(), n, b);
      Points out { first };
      for (int i = 0; i < n; i++)
      {
         if (!(pts[i].x > a + 1.0e-6f && pts[i].x < b - 1.0e-6f))
            continue;
         Point p = pts[i];
         if (i == ib) // the segment the cut at b ends
         {
            p.curve *= (b - pts[i].x) / spanOf(i);
            p.rx *= (b - pts[i].x) / spanOf(i);
         }
         out.push_back(p);
      }
      if (b >= pts[ib + 1].x - 1.0e-6f) // b on a point: keep it whole
         last = pts[ib + 1], last.x = b;
      last.rx = last.ry = 0.0f;
      out.push_back(last);
      const float span = b - a;
      for (Point& p : out)
      {
         p.x = (p.x - a) / span;
         p.lx /= span;
         p.rx /= span;
      }
      out.front().x = 0.0f;
      out.back().x = 1.0f;
      pts = out;
      return true;
   }
}

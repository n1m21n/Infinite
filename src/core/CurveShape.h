#pragma once

#include <string>
#include <vector>

// A monotonic-in-x, Catmull-Rom-interpolated point curve, shared by CurvesNode
// (the Photoshop-style per-channel tone curve) and ModCurveNode (a modulator
// transfer curve). Points are kept sorted by x; the two endpoints are pinned
// to x=0 and x=1 and cannot be removed.
struct CurveShape
{
   struct Point
   {
      float x = 0.0f;
      float y = 0.0f;
   };

   std::vector<Point> points;

   // Bumped by any edit that actually changes `points` (Reset, AddPoint,
   // MovePoint when it moves, RemovePoint, Decode when the decoded points
   // differ). Lets a consumer that caches a derived result (e.g. CurvesNode's
   // LUT texture) detect a change without every mutation site having to
   // remember to say so explicitly.
   unsigned long long version = 0;

   // false: the original Catmull-Rom with duplicated ends, which bends even a
   // straight two-point line (kept for ModCurveNode and for Curves patches
   // saved before the fix). true: cubic Hermite with slopes taken over the
   // real x spacing (secant slopes at the ends), so collinear points give an
   // exact straight line. Change it through SetExactLine so caches rebuild.
   bool exactLine = false;
   void SetExactLine(bool on)
   {
      if (on != exactLine)
      {
         exactLine = on;
         version++;
      }
   }

   CurveShape() { Reset(); }

   void Reset();

   // Editing. Returns the index of the inserted point.
   int AddPoint(float x, float y);
   void MovePoint(int index, float x, float y);
   void RemovePoint(int index);
   // Replaces every point (an editor's result); bumps version only when something differs.
   void SetPoints(const std::vector<Point>& p)
   {
      bool same = p.size() == points.size();
      for (size_t i = 0; same && i < p.size(); i++)
         same = p[i].x == points[i].x && p[i].y == points[i].y;
      if (!same && p.size() >= 2)
      {
         points = p;
         version++;
      }
   }

   // Curve value at x (0..1).
   float Evaluate(float x) const;

   // Round-tripped as one "x0,y0;x1,y1;..." string.
   std::string Encode() const;

   // No-op if fewer than 2 points decode out of s.
   void Decode(const std::string& s);
};

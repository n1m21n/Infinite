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

   CurveShape() { Reset(); }

   void Reset();

   // Editing. Returns the index of the inserted point.
   int AddPoint(float x, float y);
   void MovePoint(int index, float x, float y);
   void RemovePoint(int index);

   // Curve value at x (0..1).
   float Evaluate(float x) const;

   // Round-tripped as one "x0,y0;x1,y1;..." string.
   std::string Encode() const;

   // No-op if fewer than 2 points decode out of s.
   void Decode(const std::string& s);
};

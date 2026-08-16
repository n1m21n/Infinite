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

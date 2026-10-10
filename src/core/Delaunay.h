#pragma once

#include <vector>

// 2D Delaunay triangulation (Bowyer-Watson, double precision). Own
// implementation; no third-party source. Coincident points are skipped and
// collinear input yields no triangles. Cost is O(n^2) worst case, so callers
// cap the point count.
namespace Delaunay
{
   // xy: interleaved x,y pairs. Returns triangle index triples into the
   // point list, each wound counter-clockwise. Points dropped as duplicates
   // simply never appear.
   std::vector<unsigned int> Triangulate(const std::vector<double>& xy);
}

#pragma once

#include "Mesh.h"

// Polyline editing: resample, simplify, smooth, offset. Own implementations.
namespace CurveOps
{
   // `count` points evenly spaced by arc length (closed lines wrap).
   Polyline Resample(const Polyline& in, int count);
   // Ramer-Douglas-Peucker in 3D: drops points closer than `tolerance` to the
   // chord that replaces them. Endpoints of an open line are always kept.
   Polyline Simplify(const Polyline& in, float tolerance);
   // Laplacian relaxation; the ends of an open line stay put.
   Polyline Smooth(const Polyline& in, int iterations, float strength);
   // Parallel curve at signed `distance` (left of travel is positive) in the
   // XY plane; z is carried through. Convex corners get round joins, concave
   // ones a mitre clamped to the offset distance. Self-intersections that a
   // large offset creates are not trimmed.
   Polyline Offset(const Polyline& in, float distance, int arcSteps);
}

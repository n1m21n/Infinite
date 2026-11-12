#pragma once

// Row 4.4 (realtime-engine OPUS_PLAN): curvature and cavity per vertex, the inputs material programs read as
// `curvature` and `cavity` (Field G.z / G.w) and Shade Geometry outputs.
//   curvature: signed mean curvature from the cotangent Laplacian (Meyer et al. 2003), times the mesh's bounding
//              radius (farthest vertex from the box centre), so a sphere reads 1 at any size; positive on ridges, negative in creases, 0 on flats.
//   cavity:    the share of 16 cosine-weighted rays from the vertex that hit the mesh within 5 % of that radius:
//              0 open, 1 buried (1 - AO at that distance).
// Vertices at one position (seams, hard edges) are welded first and share both values.

#include <cstddef>
#include <vector>

namespace MeshWear
{
   // pos: 3 floats a vertex, `stride` floats apart. Returns 2 floats a vertex (curvature, cavity), empty for no faces.
   std::vector<float> Bake(const float* pos, size_t stride, size_t vertexCount, const unsigned int* idx, size_t indexCount);
}

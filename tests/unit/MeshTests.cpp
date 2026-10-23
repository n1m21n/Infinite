// Turbo 0.71 (S1): every closed primitive is wound counter-clockwise seen from
// outside, the way its vertex normals point. Render 3D and Render 3D HQ cull
// GL_BACK with GL_FRONT_FACE CCW, so a primitive wound the other way shows the
// inside of its far half: right silhouette, inverted lighting and reflections
// (found on the HQ reference scene A's spheres).

#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "Mesh.h"

namespace
{
   // Share of triangles whose geometric normal (from the winding) agrees with
   // the average of its three vertex normals.
   double AgreeingShare(const Mesh& m)
   {
      size_t agree = 0, total = 0;
      for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
      {
         const Vertex& a = m.vertices[m.indices[i]];
         const Vertex& b = m.vertices[m.indices[i + 1]];
         const Vertex& c = m.vertices[m.indices[i + 2]];
         const float e1[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz };
         const float e2[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         const float g[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                              e1[0] * e2[1] - e1[1] * e2[0] };
         const float n[3] = { a.nx + b.nx + c.nx, a.ny + b.ny + c.ny, a.nz + b.nz + c.nz };
         const float d = g[0] * n[0] + g[1] * n[1] + g[2] * n[2];
         if (g[0] * g[0] + g[1] * g[1] + g[2] * g[2] < 1e-14f)
            continue; // degenerate (pole caps)
         total++;
         if (d > 0.0f)
            agree++;
      }
      return total == 0 ? 0.0 : (double)agree / (double)total;
   }
}

// Sphere, Cylinder, Cone, TorusKnot, Helix (0%) and Pyramid (37.5%) failed this
// until 2026-10-23 (fixed in Mesh.cpp, Turbo 0.71).
TEST_CASE("closed primitives are wound counter-clockwise from outside")
{
   const std::vector<std::pair<std::string, Mesh>> shapes = {
      { "Cube", Primitives::Cube(2) },
      { "Sphere", Primitives::Sphere(16, 32) },
      { "Icosphere", Primitives::Icosphere(2) },
      { "Torus", Primitives::Torus(16, 16, 0.3f) },
      { "Cylinder", Primitives::Cylinder(16, 2, 1.0f) },
      { "Cone", Primitives::Cone(16, 2) },
      { "TorusKnot", Primitives::TorusKnot(64, 8, 0.2f, 2, 3) },
      { "Capsule", Primitives::Capsule(8, 16, 0.5f) },
      { "Tube", Primitives::Tube(16, 2, 0.5f) },
      { "Pyramid", Primitives::Pyramid(4) },
      { "Prism", Primitives::Prism(6, 2) },
      { "Helix", Primitives::Helix(64, 8, 0.2f, 2.0f, 1.0f) },
      { "Tetrahedron", Primitives::Tetrahedron() },
      { "Octahedron", Primitives::Octahedron() },
      { "Dodecahedron", Primitives::Dodecahedron() },
      { "RoundedCube", Primitives::RoundedCube(4, 0.1f) },
      { "Gear", Primitives::Gear(12, 0.2f, 0.1f, 0.2f) },
      { "Star", Primitives::Star(5, 0.5f, 0.2f) },
      { "Arrow", Primitives::Arrow(16, 0.1f, 0.3f) },
      { "Supershape", Primitives::Supershape(16, 32, 4.0f, 1.0f, 1.0f, 1.0f, 4.0f, 1.0f, 1.0f, 1.0f) },
      { "TubeAlong", [] {
           Polyline line; // an open spiral, so the frame turns
           for (int i = 0; i <= 64; i++)
           {
              const float t = (float)i / 64.0f * 6.2831853f * 2.0f;
              line.points.insert(line.points.end(), { 0.5f * std::cos(t), 0.05f * t, 0.5f * std::sin(t) });
           }
           return MeshOps::TubeAlong(line, 0.05f, 12, 0.0f);
        }() },
   };
   for (const auto& shape : shapes)
   {
      const double share = AgreeingShare(shape.second);
      printf("  winding %-13s %5.1f%% of triangles agree with their normals\n", shape.first.c_str(), share * 100.0);
      CHECK_MESSAGE(share > 0.99, shape.first);
   }
}

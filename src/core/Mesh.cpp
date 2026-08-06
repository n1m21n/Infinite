#include "Mesh.h"

#include <algorithm>
#include <map>

namespace
{
   const float kPi = 3.14159265358979f;

   void PushVertex(Mesh& mesh, float px, float py, float pz,
                   float nx, float ny, float nz, float u, float v)
   {
      Vertex vert;
      vert.px = px; vert.py = py; vert.pz = pz;
      vert.nx = nx; vert.ny = ny; vert.nz = nz;
      vert.u = u; vert.v = v;
      mesh.vertices.push_back(vert);
   }

   void PushQuad(Mesh& mesh, unsigned int a, unsigned int b, unsigned int c, unsigned int d)
   {
      mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c);
      mesh.indices.push_back(a); mesh.indices.push_back(c); mesh.indices.push_back(d);
   }

   // Builds one face of a cube from an origin plus two edge vectors.
   void PushGrid(Mesh& mesh, int segments,
                 const float origin[3], const float du[3], const float dv[3],
                 const float normal[3])
   {
      const unsigned int base = (unsigned int)mesh.vertices.size();
      const int n = std::max(1, segments);
      for (int j = 0; j <= n; j++)
      {
         for (int i = 0; i <= n; i++)
         {
            const float fu = (float)i / n;
            const float fv = (float)j / n;
            PushVertex(mesh,
                       origin[0] + du[0] * fu + dv[0] * fv,
                       origin[1] + du[1] * fu + dv[1] * fv,
                       origin[2] + du[2] * fu + dv[2] * fv,
                       normal[0], normal[1], normal[2], fu, fv);
         }
      }
      for (int j = 0; j < n; j++)
      {
         for (int i = 0; i < n; i++)
         {
            const unsigned int row = base + (unsigned int)(j * (n + 1) + i);
            PushQuad(mesh, row, row + 1, row + n + 2, row + n + 1);
         }
      }
   }
}

namespace Primitives
{
   Mesh Plane(int segments)
   {
      Mesh mesh;
      const float origin[3] = { -0.5f, -0.5f, 0.0f };
      const float du[3] = { 1.0f, 0.0f, 0.0f };
      const float dv[3] = { 0.0f, 1.0f, 0.0f };
      const float n[3] = { 0.0f, 0.0f, 1.0f };
      PushGrid(mesh, segments, origin, du, dv, n);
      return mesh;
   }

   Mesh Cube(int segments)
   {
      Mesh mesh;
      const float h = 0.5f;
      struct Face { float o[3], du[3], dv[3], n[3]; };
      const Face faces[6] = {
         { { -h, -h,  h }, { 1, 0, 0 }, { 0, 1, 0 }, {  0,  0,  1 } }, // front
         { {  h, -h, -h }, { -1, 0, 0 }, { 0, 1, 0 }, { 0,  0, -1 } }, // back
         { { -h, -h, -h }, { 0, 0, 1 }, { 0, 1, 0 }, { -1,  0,  0 } }, // left
         { {  h, -h,  h }, { 0, 0, -1 }, { 0, 1, 0 }, { 1,  0,  0 } }, // right
         { { -h,  h,  h }, { 1, 0, 0 }, { 0, 0, -1 }, { 0,  1,  0 } }, // top
         { { -h, -h, -h }, { 1, 0, 0 }, { 0, 0, 1 }, {  0, -1,  0 } }, // bottom
      };
      for (const Face& f : faces)
         PushGrid(mesh, segments, f.o, f.du, f.dv, f.n);
      return mesh;
   }

   Mesh Sphere(int rings, int sectors)
   {
      Mesh mesh;
      const int r = std::max(3, rings);
      const int s = std::max(3, sectors);
      for (int i = 0; i <= r; i++)
      {
         const float phi = kPi * (float)i / r;          // 0..pi, pole to pole
         const float y = std::cos(phi);
         const float ringRadius = std::sin(phi);
         for (int j = 0; j <= s; j++)
         {
            const float theta = 2.0f * kPi * (float)j / s;
            const float x = ringRadius * std::cos(theta);
            const float z = ringRadius * std::sin(theta);
            PushVertex(mesh, x * 0.5f, y * 0.5f, z * 0.5f, x, y, z,
                       (float)j / s, 1.0f - (float)i / r);
         }
      }
      for (int i = 0; i < r; i++)
      {
         for (int j = 0; j < s; j++)
         {
            const unsigned int a = (unsigned int)(i * (s + 1) + j);
            const unsigned int b = a + (unsigned int)s + 1;
            PushQuad(mesh, a, b, b + 1, a + 1);
         }
      }
      return mesh;
   }

   Mesh Torus(int rings, int sides, float tubeRadius)
   {
      Mesh mesh;
      const int r = std::max(3, rings);
      const int s = std::max(3, sides);
      const float major = 0.5f - tubeRadius * 0.5f;
      const float minor = std::max(0.01f, tubeRadius * 0.5f);
      for (int i = 0; i <= r; i++)
      {
         const float u = 2.0f * kPi * (float)i / r;
         const float cu = std::cos(u), su = std::sin(u);
         for (int j = 0; j <= s; j++)
         {
            const float v = 2.0f * kPi * (float)j / s;
            const float cv = std::cos(v), sv = std::sin(v);
            const float x = (major + minor * cv) * cu;
            const float y = minor * sv;
            const float z = (major + minor * cv) * su;
            PushVertex(mesh, x, y, z, cv * cu, sv, cv * su, (float)i / r, (float)j / s);
         }
      }
      for (int i = 0; i < r; i++)
         for (int j = 0; j < s; j++)
         {
            const unsigned int a = (unsigned int)(i * (s + 1) + j);
            const unsigned int b = a + (unsigned int)s + 1;
            PushQuad(mesh, a, a + 1, b + 1, b);
         }
      return mesh;
   }

   Mesh Cylinder(int sides, int rings, float topRadius)
   {
      Mesh mesh;
      const int s = std::max(3, sides);
      const int r = std::max(1, rings);
      // side wall
      for (int i = 0; i <= r; i++)
      {
         const float t = (float)i / r;
         const float y = 0.5f - t;
         const float radius = 0.5f * (1.0f - t) + (0.5f * topRadius) * t;
         for (int j = 0; j <= s; j++)
         {
            const float theta = 2.0f * kPi * (float)j / s;
            const float cx = std::cos(theta), cz = std::sin(theta);
            PushVertex(mesh, cx * radius, y, cz * radius, cx, 0.0f, cz, (float)j / s, 1.0f - t);
         }
      }
      for (int i = 0; i < r; i++)
         for (int j = 0; j < s; j++)
         {
            const unsigned int a = (unsigned int)(i * (s + 1) + j);
            const unsigned int b = a + (unsigned int)s + 1;
            PushQuad(mesh, a, b, b + 1, a + 1);
         }

      // caps
      for (int cap = 0; cap < 2; cap++)
      {
         const bool top = (cap == 0);
         const float y = top ? 0.5f : -0.5f;
         const float radius = top ? 0.5f * topRadius : 0.5f;
         if (radius <= 0.0f)
            continue;
         const unsigned int center = (unsigned int)mesh.vertices.size();
         PushVertex(mesh, 0.0f, y, 0.0f, 0.0f, top ? 1.0f : -1.0f, 0.0f, 0.5f, 0.5f);
         for (int j = 0; j <= s; j++)
         {
            const float theta = 2.0f * kPi * (float)j / s;
            const float cx = std::cos(theta), cz = std::sin(theta);
            PushVertex(mesh, cx * radius, y, cz * radius, 0.0f, top ? 1.0f : -1.0f, 0.0f,
                       0.5f + cx * 0.5f, 0.5f + cz * 0.5f);
         }
         for (int j = 0; j < s; j++)
         {
            const unsigned int a = center + 1 + (unsigned int)j;
            if (top)
            {
               mesh.indices.push_back(center); mesh.indices.push_back(a); mesh.indices.push_back(a + 1);
            }
            else
            {
               mesh.indices.push_back(center); mesh.indices.push_back(a + 1); mesh.indices.push_back(a);
            }
         }
      }
      return mesh;
   }

   Mesh Cone(int sides, int rings)
   {
      return Cylinder(sides, rings, 0.0f);
   }

   Mesh Icosphere(int subdivisions)
   {
      // Start from an icosahedron and subdivide: gives far more even triangles
      // than a lat/long sphere, which bunches badly at the poles.
      const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
      std::vector<std::array<float, 3>> pts = {
         {{-1,  t,  0}}, {{ 1,  t,  0}}, {{-1, -t,  0}}, {{ 1, -t,  0}},
         {{ 0, -1,  t}}, {{ 0,  1,  t}}, {{ 0, -1, -t}}, {{ 0,  1, -t}},
         {{ t,  0, -1}}, {{ t,  0,  1}}, {{-t,  0, -1}}, {{-t,  0,  1}},
      };
      std::vector<std::array<unsigned int, 3>> tris = {
         {{0,11,5}}, {{0,5,1}}, {{0,1,7}}, {{0,7,10}}, {{0,10,11}},
         {{1,5,9}}, {{5,11,4}}, {{11,10,2}}, {{10,7,6}}, {{7,1,8}},
         {{3,9,4}}, {{3,4,2}}, {{3,2,6}}, {{3,6,8}}, {{3,8,9}},
         {{4,9,5}}, {{2,4,11}}, {{6,2,10}}, {{8,6,7}}, {{9,8,1}},
      };

      const int levels = std::max(0, std::min(subdivisions, 4));
      for (int level = 0; level < levels; level++)
      {
         std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpoints;
         std::vector<std::array<unsigned int, 3>> next;
         auto midpoint = [&](unsigned int a, unsigned int b) {
            auto key = std::minmax(a, b);
            auto it = midpoints.find({ key.first, key.second });
            if (it != midpoints.end())
               return it->second;
            std::array<float, 3> m = {{ (pts[a][0] + pts[b][0]) * 0.5f,
                                        (pts[a][1] + pts[b][1]) * 0.5f,
                                        (pts[a][2] + pts[b][2]) * 0.5f }};
            pts.push_back(m);
            const unsigned int index = (unsigned int)pts.size() - 1;
            midpoints[{ key.first, key.second }] = index;
            return index;
         };
         for (const auto& tri : tris)
         {
            const unsigned int a = midpoint(tri[0], tri[1]);
            const unsigned int b = midpoint(tri[1], tri[2]);
            const unsigned int c = midpoint(tri[2], tri[0]);
            next.push_back({{ tri[0], a, c }});
            next.push_back({{ tri[1], b, a }});
            next.push_back({{ tri[2], c, b }});
            next.push_back({{ a, b, c }});
         }
         tris.swap(next);
      }

      Mesh mesh;
      for (auto& p : pts)
      {
         const float len = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
         const float nx = p[0] / len, ny = p[1] / len, nz = p[2] / len;
         PushVertex(mesh, nx * 0.5f, ny * 0.5f, nz * 0.5f, nx, ny, nz,
                    0.5f + std::atan2(nz, nx) / (2.0f * kPi),
                    0.5f - std::asin(ny) / kPi);
      }
      for (const auto& tri : tris)
      {
         mesh.indices.push_back(tri[0]);
         mesh.indices.push_back(tri[1]);
         mesh.indices.push_back(tri[2]);
      }
      return mesh;
   }

   Mesh TorusKnot(int segments, int sides, float tubeRadius, int p, int q)
   {
      Mesh mesh;
      const int seg = std::max(8, segments);
      const int sid = std::max(3, sides);
      const float tube = std::max(0.005f, tubeRadius * 0.25f);
      const int pp = std::max(1, p);
      const int qq = std::max(1, q);

      auto curve = [&](float t, float out[3]) {
         const float u = t * 2.0f * kPi * pp;
         const float v = t * 2.0f * kPi * qq;
         const float r = 0.28f * (2.0f + std::cos(v));
         out[0] = r * std::cos(u);
         out[1] = r * std::sin(u);
         out[2] = 0.28f * std::sin(v);
      };

      for (int i = 0; i <= seg; i++)
      {
         const float t = (float)i / seg;
         float cur[3], nxt[3];
         curve(t, cur);
         curve(t + 1.0f / seg, nxt);

         float tangent[3] = { nxt[0] - cur[0], nxt[1] - cur[1], nxt[2] - cur[2] };
         float len = std::sqrt(tangent[0]*tangent[0] + tangent[1]*tangent[1] + tangent[2]*tangent[2]);
         if (len < 1e-6f) len = 1.0f;
         tangent[0] /= len; tangent[1] /= len; tangent[2] /= len;

         // Any vector not parallel to the tangent works as a reference up.
         float up[3] = { 0.0f, 0.0f, 1.0f };
         if (std::fabs(tangent[2]) > 0.9f) { up[0] = 1.0f; up[2] = 0.0f; }
         float normal[3] = {
            tangent[1]*up[2] - tangent[2]*up[1],
            tangent[2]*up[0] - tangent[0]*up[2],
            tangent[0]*up[1] - tangent[1]*up[0] };
         len = std::sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
         if (len < 1e-6f) len = 1.0f;
         normal[0] /= len; normal[1] /= len; normal[2] /= len;
         const float binormal[3] = {
            tangent[1]*normal[2] - tangent[2]*normal[1],
            tangent[2]*normal[0] - tangent[0]*normal[2],
            tangent[0]*normal[1] - tangent[1]*normal[0] };

         for (int j = 0; j <= sid; j++)
         {
            const float a = 2.0f * kPi * (float)j / sid;
            const float ca = std::cos(a), sa = std::sin(a);
            const float nx = normal[0]*ca + binormal[0]*sa;
            const float ny = normal[1]*ca + binormal[1]*sa;
            const float nz = normal[2]*ca + binormal[2]*sa;
            PushVertex(mesh, cur[0] + nx*tube, cur[1] + ny*tube, cur[2] + nz*tube,
                       nx, ny, nz, t, (float)j / sid);
         }
      }
      for (int i = 0; i < seg; i++)
         for (int j = 0; j < sid; j++)
         {
            const unsigned int a = (unsigned int)(i * (sid + 1) + j);
            const unsigned int b = a + (unsigned int)sid + 1;
            PushQuad(mesh, a, b, b + 1, a + 1);
         }
      return mesh;
   }
}

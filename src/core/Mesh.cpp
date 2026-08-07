#include "Mesh.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>

// Zero is reserved as "nothing uploaded yet", so the first real stamp is 1.
unsigned long long NextMeshRevision()
{
   static unsigned long long sCounter = 0;
   return ++sCounter;
}

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

// ================================================================ MeshOps

namespace
{
   float RandFrom(float seed, int index)
   {
      const float x = std::sin((seed + 1.0f) * (float)(index + 1) * 12.9898f) * 43758.5453f;
      return x - std::floor(x);
   }

   void TransformPoint(const Mat4& m, float x, float y, float z, float out[3])
   {
      out[0] = m.m[0]*x + m.m[4]*y + m.m[8]*z  + m.m[12];
      out[1] = m.m[1]*x + m.m[5]*y + m.m[9]*z  + m.m[13];
      out[2] = m.m[2]*x + m.m[6]*y + m.m[10]*z + m.m[14];
   }
}

namespace MeshOps
{
   Mesh Transform(const Mesh& in, const Mat4& m)
   {
      Mesh out = in;
      float normalMatrix[9];
      m.NormalMatrix(normalMatrix);
      for (Vertex& v : out.vertices)
      {
         float p[3];
         TransformPoint(m, v.px, v.py, v.pz, p);
         v.px = p[0]; v.py = p[1]; v.pz = p[2];
         const float nx = normalMatrix[0]*v.nx + normalMatrix[3]*v.ny + normalMatrix[6]*v.nz;
         const float ny = normalMatrix[1]*v.nx + normalMatrix[4]*v.ny + normalMatrix[7]*v.nz;
         const float nz = normalMatrix[2]*v.nx + normalMatrix[5]*v.ny + normalMatrix[8]*v.nz;
         const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
         if (len > 1e-6f) { v.nx = nx/len; v.ny = ny/len; v.nz = nz/len; }
      }
      return out;
   }

   Mesh Array(const Mesh& in, int count, float dx, float dy, float dz,
              float rotStep, float scaleStep, bool radial, float radius)
   {
      Mesh out;
      const int n = std::max(1, std::min(count, 256));
      out.vertices.reserve(in.vertices.size() * n);
      out.indices.reserve(in.indices.size() * n);

      for (int i = 0; i < n; i++)
      {
         Mat4 m;
         if (radial)
         {
            const float a = 6.28318530f * (float)i / (float)n;
            m = Mat4::Multiply(Mat4::RotationY(a),
                               Mat4::Translation(radius, 0.0f, 0.0f));
            m = Mat4::Multiply(m, Mat4::RotationZ(rotStep * i));
         }
         else
         {
            m = Mat4::Translation(dx * i, dy * i, dz * i);
            m = Mat4::Multiply(m, Mat4::RotationY(rotStep * i));
         }
         const float s = std::pow(std::max(0.01f, scaleStep), (float)i);
         m = Mat4::Multiply(m, Mat4::Scale(s, s, s));

         const unsigned int base = (unsigned int)out.vertices.size();
         const Mesh copy = Transform(in, m);
         out.vertices.insert(out.vertices.end(), copy.vertices.begin(), copy.vertices.end());
         for (unsigned int idx : copy.indices)
            out.indices.push_back(base + idx);
      }
      return out;
   }

   // Primitives here duplicate vertices wherever UVs or normals split - the
   // sphere's date line, every edge of a flat-shaded cube. Any operator that
   // reasons about connectivity has to weld those back together first or it
   // will treat one surface as several and tear it open along the seams.
   //
   // Returns, for each vertex, the index of the first vertex sharing its
   // position. Positions are quantised before hashing so that values that
   // differ only in the last float bit still land together.
   std::vector<unsigned int> BuildWeldMap(const Mesh& in)
   {
      std::map<std::array<long long, 3>, unsigned int> unique;
      std::vector<unsigned int> weld(in.vertices.size());
      const double kQuantum = 1e5; // ~0.00001 units

      for (size_t i = 0; i < in.vertices.size(); i++)
      {
         const Vertex& v = in.vertices[i];
         const std::array<long long, 3> key = {
            (long long)std::llround((double)v.px * kQuantum),
            (long long)std::llround((double)v.py * kQuantum),
            (long long)std::llround((double)v.pz * kQuantum)
         };
         auto it = unique.find(key);
         if (it == unique.end())
         {
            unique[key] = (unsigned int)i;
            weld[i] = (unsigned int)i;
         }
         else
         {
            weld[i] = it->second;
         }
      }
      return weld;
   }

   // Neighbour sets over welded indices, built from triangle edges.
   std::map<unsigned int, std::set<unsigned int>> BuildAdjacency(
      const Mesh& in, const std::vector<unsigned int>& weld)
   {
      std::map<unsigned int, std::set<unsigned int>> adjacency;
      for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
      {
         const unsigned int v[3] = { weld[in.indices[t]], weld[in.indices[t + 1]],
                                     weld[in.indices[t + 2]] };
         for (int e = 0; e < 3; e++)
         {
            const unsigned int a = v[e], b = v[(e + 1) % 3];
            if (a == b)
               continue;
            adjacency[a].insert(b);
            adjacency[b].insert(a);
         }
      }
      return adjacency;
   }

   // Loop subdivision. Each pass splits every triangle into four and then moves
   // both the new edge points and the original vertices onto the limit surface
   // using Loop's weights.
   //
   // This replaces an earlier version that split the triangles and then shoved
   // each midpoint along its own normal by a twelfth of the edge length. That
   // rounded a sphere convincingly enough, but it was not subdivision: the
   // displacement depended only on the edge, so flat regions bulged, creases
   // rounded off at the same rate as everything else, and the result drifted
   // further from the true surface with every level rather than converging.
   //
   // `smooth` blends between plain 4:1 tessellation at 0 and the full limit
   // surface at 1, so the parameter still means roughly what it used to.
   Mesh Subdivide(const Mesh& in, int levels, float smooth)
   {
      Mesh current = in;
      const int passes = std::max(0, std::min(levels, 3));
      const float blend = std::max(0.0f, std::min(smooth, 1.0f));

      for (int pass = 0; pass < passes; pass++)
      {
         // Guard against a runaway: each pass quadruples the triangle count.
         if (current.indices.size() / 3 > 200000)
            break;

         // Connectivity has to be computed on welded vertices, or the sphere's
         // seam and every cube edge read as a boundary and stay creased.
         const std::vector<unsigned int> weld = BuildWeldMap(current);
         const std::map<unsigned int, std::set<unsigned int>> adjacency =
            BuildAdjacency(current, weld);

         // For each welded edge: how many triangles use it, and the opposite
         // vertex of each. Loop's interior edge point is 3/8 of the two endpoints
         // plus 1/8 of the two opposite vertices.
         using Edge = std::pair<unsigned int, unsigned int>;
         std::map<Edge, int> edgeCount;
         std::map<Edge, std::vector<unsigned int>> edgeOpposite;
         for (size_t t = 0; t + 2 < current.indices.size(); t += 3)
         {
            const unsigned int w[3] = { weld[current.indices[t]], weld[current.indices[t + 1]],
                                        weld[current.indices[t + 2]] };
            for (int e = 0; e < 3; e++)
            {
               const auto key = std::minmax(w[e], w[(e + 1) % 3]);
               if (key.first == key.second)
                  continue;
               edgeCount[{ key.first, key.second }]++;
               edgeOpposite[{ key.first, key.second }].push_back(w[(e + 2) % 3]);
            }
         }

         auto position = [&](unsigned int i, float out[3]) {
            out[0] = current.vertices[i].px;
            out[1] = current.vertices[i].py;
            out[2] = current.vertices[i].pz;
         };

         // --- repositioned original vertices ---
         std::map<unsigned int, std::array<float, 3>> movedVertex;
         for (const auto& entry : adjacency)
         {
            const unsigned int v = entry.first;
            const std::set<unsigned int>& neighbours = entry.second;
            const int n = (int)neighbours.size();

            float original[3];
            position(v, original);

            // A vertex is on a boundary when any of its edges is used by only
            // one triangle. Those follow the 1/8, 3/4, 1/8 curve rule so the
            // border stays put instead of shrinking inward.
            std::vector<unsigned int> boundaryNeighbours;
            for (unsigned int nb : neighbours)
            {
               const auto key = std::minmax(v, nb);
               auto it = edgeCount.find({ key.first, key.second });
               if (it != edgeCount.end() && it->second == 1)
                  boundaryNeighbours.push_back(nb);
            }

            std::array<float, 3> moved = { original[0], original[1], original[2] };
            if (boundaryNeighbours.size() == 2)
            {
               float a[3], b[3];
               position(boundaryNeighbours[0], a);
               position(boundaryNeighbours[1], b);
               for (int k = 0; k < 3; k++)
                  moved[k] = 0.75f * original[k] + 0.125f * (a[k] + b[k]);
            }
            else if (n > 2)
            {
               // Loop's beta. At valence 3 this is 3/16; the general form
               // converges to 3/(8n) for larger valences.
               const float t = 0.375f + 0.25f * std::cos(2.0f * kPi / (float)n);
               const float beta = (0.625f - t * t) / (float)n;
               float sum[3] = { 0, 0, 0 };
               for (unsigned int nb : neighbours)
               {
                  float p[3];
                  position(nb, p);
                  sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
               }
               for (int k = 0; k < 3; k++)
                  moved[k] = original[k] * (1.0f - (float)n * beta) + sum[k] * beta;
            }

            for (int k = 0; k < 3; k++)
               moved[k] = original[k] + (moved[k] - original[k]) * blend;
            movedVertex[v] = moved;
         }

         Mesh next;
         next.vertices = current.vertices;
         for (size_t i = 0; i < next.vertices.size(); i++)
         {
            auto it = movedVertex.find(weld[i]);
            if (it == movedVertex.end())
               continue;
            next.vertices[i].px = it->second[0];
            next.vertices[i].py = it->second[1];
            next.vertices[i].pz = it->second[2];
         }

         // --- edge points ---
         // Cached on the welded pair so the two triangles either side of an edge
         // agree on one vertex, rather than each making its own and cracking.
         std::map<Edge, unsigned int> edgePoint;
         auto splitEdge = [&](unsigned int ia, unsigned int ib) -> unsigned int {
            const auto key = std::minmax(weld[ia], weld[ib]);
            const Edge e = { key.first, key.second };
            auto cached = edgePoint.find(e);
            if (cached != edgePoint.end())
               return cached->second;

            const Vertex& va = current.vertices[ia];
            const Vertex& vb = current.vertices[ib];
            Vertex m = va;
            // UVs and normals stay at the plain midpoint: interpolating them
            // with Loop's weights would drag them across seams.
            m.u = (va.u + vb.u) * 0.5f;
            m.v = (va.v + vb.v) * 0.5f;
            m.nx = (va.nx + vb.nx) * 0.5f;
            m.ny = (va.ny + vb.ny) * 0.5f;
            m.nz = (va.nz + vb.nz) * 0.5f;
            const float len = std::sqrt(m.nx*m.nx + m.ny*m.ny + m.nz*m.nz);
            if (len > 1e-6f) { m.nx /= len; m.ny /= len; m.nz /= len; }

            float pa[3], pb[3];
            position(ia, pa);
            position(ib, pb);
            float mid[3] = { (pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f,
                             (pa[2] + pb[2]) * 0.5f };
            float limit[3] = { mid[0], mid[1], mid[2] };

            auto opposites = edgeOpposite.find(e);
            auto count = edgeCount.find(e);
            const bool interior = count != edgeCount.end() && count->second == 2 &&
                                  opposites != edgeOpposite.end() &&
                                  opposites->second.size() >= 2;
            if (interior)
            {
               float c[3], d[3];
               position(opposites->second[0], c);
               position(opposites->second[1], d);
               for (int k = 0; k < 3; k++)
                  limit[k] = 0.375f * (pa[k] + pb[k]) + 0.125f * (c[k] + d[k]);
            }

            m.px = mid[0] + (limit[0] - mid[0]) * blend;
            m.py = mid[1] + (limit[1] - mid[1]) * blend;
            m.pz = mid[2] + (limit[2] - mid[2]) * blend;

            next.vertices.push_back(m);
            const unsigned int index = (unsigned int)next.vertices.size() - 1;
            edgePoint[e] = index;
            return index;
         };

         for (size_t t = 0; t + 2 < current.indices.size(); t += 3)
         {
            const unsigned int a = current.indices[t];
            const unsigned int b = current.indices[t + 1];
            const unsigned int c = current.indices[t + 2];
            const unsigned int ab = splitEdge(a, b);
            const unsigned int bc = splitEdge(b, c);
            const unsigned int ca = splitEdge(c, a);
            const unsigned int tri[4][3] = { { a, ab, ca }, { b, bc, ab }, { c, ca, bc }, { ab, bc, ca } };
            for (auto& x : tri)
            {
               next.indices.push_back(x[0]);
               next.indices.push_back(x[1]);
               next.indices.push_back(x[2]);
            }
         }
         current = std::move(next);
      }
      return RecalculateNormals(current, false, false);
   }

   Mesh Smooth(const Mesh& in, int iterations, float strength)
   {
      Mesh out = in;
      if (in.Empty() || iterations <= 0 || strength <= 0.0f)
         return out;

      const std::vector<unsigned int> weld = BuildWeldMap(in);
      const std::map<unsigned int, std::set<unsigned int>> adjacency = BuildAdjacency(in, weld);

      // Taubin: a positive Laplacian pass that relaxes the surface, then a
      // slightly larger negative one that pushes back out. Plain Laplacian
      // smoothing shrinks a closed mesh toward its centroid a little more with
      // every iteration, and a sphere run enough times collapses to a point;
      // the alternating signs cancel that while still removing the high
      // frequencies.
      const float lambda = 0.5f * std::max(0.0f, std::min(strength, 1.0f));
      const float mu = -lambda * 1.04f;

      std::vector<float> px(out.vertices.size()), py(out.vertices.size()), pz(out.vertices.size());
      for (size_t i = 0; i < out.vertices.size(); i++)
      {
         px[i] = out.vertices[i].px; py[i] = out.vertices[i].py; pz[i] = out.vertices[i].pz;
      }

      const int passes = std::max(0, std::min(iterations, 20));
      for (int pass = 0; pass < passes * 2; pass++)
      {
         const float step = (pass % 2 == 0) ? lambda : mu;
         std::vector<float> nx = px, ny = py, nz = pz;

         for (const auto& entry : adjacency)
         {
            const unsigned int v = entry.first;
            if (entry.second.empty())
               continue;
            float sx = 0, sy = 0, sz = 0;
            for (unsigned int n : entry.second)
            {
               sx += px[n]; sy += py[n]; sz += pz[n];
            }
            const float inv = 1.0f / (float)entry.second.size();
            nx[v] = px[v] + step * (sx * inv - px[v]);
            ny[v] = py[v] + step * (sy * inv - py[v]);
            nz[v] = pz[v] + step * (sz * inv - pz[v]);
         }
         px.swap(nx); py.swap(ny); pz.swap(nz);
      }

      // Written back through the weld map so split vertices move together and
      // the seams stay closed.
      for (size_t i = 0; i < out.vertices.size(); i++)
      {
         const unsigned int rep = weld[i];
         out.vertices[i].px = px[rep];
         out.vertices[i].py = py[rep];
         out.vertices[i].pz = pz[rep];
      }
      return RecalculateNormals(out, false, false);
   }

   Mesh Mirror(const Mesh& in, int axis, float offset, bool weldSeam, bool keepOriginal)
   {
      Mesh out;
      if (in.Empty())
         return out;

      const int a = std::max(0, std::min(axis, 2));
      auto component = [](Vertex& v, int i) -> float& {
         return (i == 0) ? v.px : (i == 1) ? v.py : v.pz;
      };
      auto normalComponent = [](Vertex& v, int i) -> float& {
         return (i == 0) ? v.nx : (i == 1) ? v.ny : v.nz;
      };

      if (keepOriginal)
         out = in;

      const unsigned int base = (unsigned int)out.vertices.size();
      for (Vertex v : in.vertices)
      {
         // Reflect about the plane at `offset`, and flip the matching normal
         // component so the mirrored half is not lit inside out.
         float& p = component(v, a);
         p = 2.0f * offset - p;
         normalComponent(v, a) = -normalComponent(v, a);
         out.vertices.push_back(v);
      }

      // Reflection reverses handedness, so the winding has to be reversed too
      // or every mirrored triangle faces backwards and backface culling eats it.
      for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
      {
         out.indices.push_back(base + in.indices[t]);
         out.indices.push_back(base + in.indices[t + 2]);
         out.indices.push_back(base + in.indices[t + 1]);
      }

      if (weldSeam && keepOriginal)
      {
         // Vertices sitting on the mirror plane are now duplicated. Snap the
         // near-plane ones exactly onto it so the halves meet without a crack;
         // BuildWeldMap then sees them as one.
         const float tolerance = 1e-4f;
         for (Vertex& v : out.vertices)
         {
            float& p = component(v, a);
            if (std::fabs(p - offset) < tolerance)
               p = offset;
         }
      }
      return out;
   }

   Mesh Screw(const Mesh& in, int steps, float turns, float rise, float radiusOffset, int axis)
   {
      Mesh out;
      if (in.Empty())
         return out;

      // Blender's Screw revolves a profile, not a solid. The profile here is the
      // input's boundary: edges used by exactly one triangle. For an open mesh
      // like a Plane that is its outline, which is what makes this behave like a
      // lathe rather than smearing the whole surface around the axis.
      const std::vector<unsigned int> weld = BuildWeldMap(in);
      std::map<std::pair<unsigned int, unsigned int>, int> edgeUse;
      for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
      {
         const unsigned int v[3] = { weld[in.indices[t]], weld[in.indices[t + 1]],
                                     weld[in.indices[t + 2]] };
         for (int e = 0; e < 3; e++)
         {
            const auto key = std::minmax(v[e], v[(e + 1) % 3]);
            if (key.first != key.second)
               edgeUse[{ key.first, key.second }]++;
         }
      }

      std::vector<std::pair<unsigned int, unsigned int>> profile;
      for (const auto& e : edgeUse)
         if (e.second == 1)
            profile.push_back(e.first);

      // A closed mesh has no boundary at all. Falling back to every edge would
      // produce an unreadable tangle, so revolve the silhouette-ish set of edges
      // that face away from the axis instead of returning nothing.
      if (profile.empty())
      {
         for (const auto& e : edgeUse)
            profile.push_back(e.first);
         if (profile.size() > 4000)
            profile.resize(4000);
      }

      const int segments = std::max(2, std::min(steps, 512));
      const float totalAngle = turns * 6.28318530718f;
      const int a = std::max(0, std::min(axis, 2));

      auto rotateAbout = [a](float p[3], float angle) {
         const float s = std::sin(angle), c = std::cos(angle);
         float r[3] = { p[0], p[1], p[2] };
         if (a == 1)      { r[0] = p[0] * c + p[2] * s; r[2] = -p[0] * s + p[2] * c; }
         else if (a == 0) { r[1] = p[1] * c - p[2] * s; r[2] = p[1] * s + p[2] * c; }
         else             { r[0] = p[0] * c - p[1] * s; r[1] = p[0] * s + p[1] * c; }
         p[0] = r[0]; p[1] = r[1]; p[2] = r[2];
      };

      for (const auto& edge : profile)
      {
         const Vertex& v0 = in.vertices[edge.first];
         const Vertex& v1 = in.vertices[edge.second];
         const unsigned int base = (unsigned int)out.vertices.size();

         for (int s = 0; s <= segments; s++)
         {
            const float t = (float)s / (float)segments;
            const float angle = totalAngle * t;
            const float lift = rise * turns * t;

            for (int end = 0; end < 2; end++)
            {
               const Vertex& src = (end == 0) ? v0 : v1;
               float p[3] = { src.px, src.py, src.pz };
               // Push away from the axis before rotating, so a profile sitting
               // on the axis opens into a tube instead of a degenerate sliver.
               if (radiusOffset != 0.0f)
               {
                  if (a == 1) { const float len = std::sqrt(p[0]*p[0] + p[2]*p[2]);
                                if (len > 1e-6f) { p[0] += p[0]/len * radiusOffset; p[2] += p[2]/len * radiusOffset; }
                                else p[0] += radiusOffset; }
                  else if (a == 0) { const float len = std::sqrt(p[1]*p[1] + p[2]*p[2]);
                                if (len > 1e-6f) { p[1] += p[1]/len * radiusOffset; p[2] += p[2]/len * radiusOffset; }
                                else p[1] += radiusOffset; }
                  else { const float len = std::sqrt(p[0]*p[0] + p[1]*p[1]);
                                if (len > 1e-6f) { p[0] += p[0]/len * radiusOffset; p[1] += p[1]/len * radiusOffset; }
                                else p[0] += radiusOffset; }
               }
               rotateAbout(p, angle);
               p[a] += lift;

               Vertex nv = src;
               nv.px = p[0]; nv.py = p[1]; nv.pz = p[2];
               nv.u = t;
               nv.v = (float)end;
               out.vertices.push_back(nv);
            }
         }

         for (int s = 0; s < segments; s++)
         {
            const unsigned int q = base + (unsigned int)s * 2;
            PushQuad(out, q, q + 2, q + 3, q + 1);
         }
      }
      return RecalculateNormals(out, false, false);
   }

   namespace
   {
      struct P2 { float x = 0, y = 0; };

      float SignedArea(const std::vector<P2>& poly)
      {
         float area = 0.0f;
         for (size_t i = 0, n = poly.size(); i < n; i++)
         {
            const P2& a = poly[i];
            const P2& b = poly[(i + 1) % n];
            area += a.x * b.y - b.x * a.y;
         }
         return area * 0.5f;
      }

      // Strictly inside: a point exactly on an edge does not block an ear.
      //
      // This has to be strict because hole bridging deliberately duplicates two
      // vertices to make a zero-width seam. With a boundary-inclusive test the
      // duplicate always sits on the candidate ear's edge, every ear is
      // rejected, and the whole face silently triangulates to nothing.
      bool PointInTriangle(const P2& p, const P2& a, const P2& b, const P2& c)
      {
         const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
         const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
         const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
         const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
         const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
         if (hasNeg && hasPos)
            return false;
         // All same sign, but a zero means it is on the boundary, not within.
         return d1 != 0.0f && d2 != 0.0f && d3 != 0.0f;
      }

      bool PointInPolygon(const P2& p, const std::vector<P2>& poly)
      {
         bool inside = false;
         for (size_t i = 0, n = poly.size(), j = n - 1; i < n; j = i++)
         {
            const P2& a = poly[i];
            const P2& b = poly[j];
            if (((a.y > p.y) != (b.y > p.y)) &&
                (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
               inside = !inside;
         }
         return inside;
      }

      // Ear clipping over a simple anticlockwise polygon. O(n^2), which is fine
      // for glyph outlines - a few hundred points at most after flattening.
      void EarClip(const std::vector<P2>& poly, const std::vector<unsigned int>& ids,
                   std::vector<unsigned int>& outIndices)
      {
         const size_t n = poly.size();
         if (n < 3)
            return;

         std::vector<size_t> remaining(n);
         for (size_t i = 0; i < n; i++)
            remaining[i] = i;

         // Bounded so a self-intersecting contour cannot spin here forever;
         // a partially triangulated glyph beats a hung UI.
         size_t guard = n * n + 16;
         while (remaining.size() > 3 && guard-- > 0)
         {
            bool clipped = false;
            const size_t count = remaining.size();
            for (size_t i = 0; i < count; i++)
            {
               const size_t ia = remaining[(i + count - 1) % count];
               const size_t ib = remaining[i];
               const size_t ic = remaining[(i + 1) % count];
               const P2& a = poly[ia];
               const P2& b = poly[ib];
               const P2& c = poly[ic];

               // Convex in an anticlockwise polygon means a positive cross.
               const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
               if (cross <= 0.0f)
                  continue;

               bool contains = false;
               for (size_t j = 0; j < count && !contains; j++)
               {
                  const size_t idx = remaining[j];
                  if (idx == ia || idx == ib || idx == ic)
                     continue;
                  contains = PointInTriangle(poly[idx], a, b, c);
               }
               if (contains)
                  continue;

               outIndices.push_back(ids[ia]);
               outIndices.push_back(ids[ib]);
               outIndices.push_back(ids[ic]);
               remaining.erase(remaining.begin() + (long)i);
               clipped = true;
               break;
            }
            if (!clipped)
               break; // no ear found: the contour is degenerate, stop cleanly
         }

         if (remaining.size() == 3)
         {
            outIndices.push_back(ids[remaining[0]]);
            outIndices.push_back(ids[remaining[1]]);
            outIndices.push_back(ids[remaining[2]]);
         }
      }

      // Cuts each hole into the outline with a bridge, producing one simple
      // polygon that ear clipping can handle. The bridge runs from the hole's
      // rightmost vertex to a visible outline vertex, and both endpoints are
      // duplicated so the seam has zero width.
      std::vector<size_t> MergeHoles(std::vector<P2>& poly,
                                     const std::vector<std::vector<P2>>& holes)
      {
         std::vector<size_t> dummy;
         for (const std::vector<P2>& hole : holes)
         {
            if (hole.size() < 3)
               continue;

            size_t holeStart = 0;
            for (size_t i = 1; i < hole.size(); i++)
               if (hole[i].x > hole[holeStart].x)
                  holeStart = i;

            // Nearest outline vertex to the right of the hole. Not a full
            // visibility test, but glyph counters are convex enough that the
            // closest candidate is reliably reachable.
            size_t bridge = 0;
            float best = 1e30f;
            bool found = false;
            for (size_t i = 0; i < poly.size(); i++)
            {
               if (poly[i].x < hole[holeStart].x)
                  continue;
               const float dx = poly[i].x - hole[holeStart].x;
               const float dy = poly[i].y - hole[holeStart].y;
               const float d = dx * dx + dy * dy;
               if (d < best)
               {
                  best = d;
                  bridge = i;
                  found = true;
               }
            }
            if (!found)
            {
               for (size_t i = 0; i < poly.size(); i++)
               {
                  const float dx = poly[i].x - hole[holeStart].x;
                  const float dy = poly[i].y - hole[holeStart].y;
                  const float d = dx * dx + dy * dy;
                  if (d < best) { best = d; bridge = i; }
               }
            }

            std::vector<P2> merged;
            merged.reserve(poly.size() + hole.size() + 2);
            for (size_t i = 0; i <= bridge; i++)
               merged.push_back(poly[i]);
            for (size_t i = 0; i < hole.size(); i++)
               merged.push_back(hole[(holeStart + i) % hole.size()]);
            merged.push_back(hole[holeStart]);
            for (size_t i = bridge; i < poly.size(); i++)
               merged.push_back(poly[i]);
            poly.swap(merged);
         }
         return dummy;
      }
   }

   Mesh ExtrudeContours(const std::vector<Contour2D>& contours, float depth, float bevel)
   {
      Mesh out;
      if (contours.empty())
         return out;

      std::vector<std::vector<P2>> polys;
      for (const Contour2D& c : contours)
      {
         std::vector<P2> poly;
         poly.reserve(c.points.size() / 2);
         for (size_t i = 0; i + 1 < c.points.size(); i += 2)
            poly.push_back({ c.points[i], c.points[i + 1] });
         if (poly.size() >= 3)
            polys.push_back(std::move(poly));
      }
      if (polys.empty())
         return out;

      // Outline or hole is decided by nesting depth, not by winding direction.
      // Winding is not portable here: TrueType fonts wind outer contours
      // clockwise and CFF/PostScript ones anticlockwise, so keying off the sign
      // of the area classified every glyph in a TrueType face as a hole.
      // Counting how many other contours enclose each one works for both, and
      // also handles a genuinely nested shape like the inner ring of an 'O'
      // inside a surrounding box.
      std::vector<std::vector<P2>> outlines, holes;
      for (size_t i = 0; i < polys.size(); i++)
      {
         int depth = 0;
         for (size_t j = 0; j < polys.size(); j++)
         {
            if (i == j)
               continue;
            if (PointInPolygon(polys[i][0], polys[j]))
               depth++;
         }

         // Winding is then forced to match the role, since ear clipping assumes
         // an anticlockwise outline and hole bridging assumes the reverse.
         std::vector<P2> poly = polys[i];
         const float area = SignedArea(poly);
         const bool isHole = (depth % 2) == 1;
         if ((isHole && area > 0.0f) || (!isHole && area < 0.0f))
            std::reverse(poly.begin(), poly.end());

         if (isHole)
            holes.push_back(std::move(poly));
         else
            outlines.push_back(std::move(poly));
      }
      if (outlines.empty())
         return out;

      const float half = depth * 0.5f;
      // The bevel is applied as an inset on the back face rather than as extra
      // geometry: a real chamfer would need offset curves, and this reads the
      // same at the sizes text is used at.
      const float shrink = std::max(0.0f, std::min(bevel, 0.45f));

      for (std::vector<P2>& outline : outlines)
      {
         // Each hole goes to the outline that actually contains it, so two
         // adjacent letters do not steal each other's counters.
         std::vector<std::vector<P2>> mine;
         for (size_t h = 0; h < holes.size(); h++)
         {
            if (holes[h].empty())
               continue;
            if (PointInPolygon(holes[h][0], outline))
            {
               mine.push_back(holes[h]);
               holes[h].clear();
            }
         }

         std::vector<P2> merged = outline;
         MergeHoles(merged, mine);

         const unsigned int base = (unsigned int)out.vertices.size();
         const size_t count = merged.size();

         float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
         for (const P2& p : merged)
         {
            lo[0] = std::min(lo[0], p.x); hi[0] = std::max(hi[0], p.x);
            lo[1] = std::min(lo[1], p.y); hi[1] = std::max(hi[1], p.y);
         }
         const float spanX = std::max(1e-5f, hi[0] - lo[0]);
         const float spanY = std::max(1e-5f, hi[1] - lo[1]);

         // Front face, then back face, then the wall between them.
         for (int side = 0; side < 2; side++)
         {
            const float z = (side == 0) ? half : -half;
            const float nz = (side == 0) ? 1.0f : -1.0f;
            const float inset = (side == 1) ? shrink * 0.02f : 0.0f;
            for (const P2& p : merged)
            {
               Vertex v;
               v.px = p.x - (p.x > 0 ? inset : -inset);
               v.py = p.y - (p.y > 0 ? inset : -inset);
               v.pz = z;
               v.nx = 0; v.ny = 0; v.nz = nz;
               v.u = (p.x - lo[0]) / spanX;
               v.v = (p.y - lo[1]) / spanY;
               out.vertices.push_back(v);
            }
         }

         std::vector<unsigned int> ids(count);
         for (size_t i = 0; i < count; i++)
            ids[i] = (unsigned int)i;
         std::vector<unsigned int> faceIndices;
         EarClip(merged, ids, faceIndices);

         for (size_t i = 0; i + 2 < faceIndices.size(); i += 3)
         {
            // Front, as clipped.
            out.indices.push_back(base + faceIndices[i]);
            out.indices.push_back(base + faceIndices[i + 1]);
            out.indices.push_back(base + faceIndices[i + 2]);
            // Back, reversed so it faces the other way.
            const unsigned int backBase = base + (unsigned int)count;
            out.indices.push_back(backBase + faceIndices[i + 2]);
            out.indices.push_back(backBase + faceIndices[i + 1]);
            out.indices.push_back(backBase + faceIndices[i]);
         }

         // Side wall, one quad per contour edge, joining front to back.
         for (size_t i = 0; i < count; i++)
         {
            const size_t next = (i + 1) % count;
            const unsigned int f0 = base + (unsigned int)i;
            const unsigned int f1 = base + (unsigned int)next;
            const unsigned int b0 = base + (unsigned int)count + (unsigned int)i;
            const unsigned int b1 = base + (unsigned int)count + (unsigned int)next;
            PushQuad(out, f0, b0, b1, f1);
         }
      }

      if (out.vertices.empty())
         return out;
      // Flat shading: the wall and the faces meet at a hard 90 degrees, and
      // averaged normals would smear that corner into a soft gradient.
      return RecalculateNormals(out, true, false);
   }

   Mesh Ocean(int resolution, float size, float amplitude, float wavelength,
              float steepness, float direction, float choppiness, int octaves, float time)
   {
      Mesh out;
      const int n = std::max(2, std::min(resolution, 400));
      const int waveCount = std::max(1, std::min(octaves, 8));

      out.vertices.reserve((size_t)(n + 1) * (size_t)(n + 1));

      // Each octave is a shorter, steeper, slightly rotated wave train. The
      // golden-angle turn between them stops the crests lining up into an
      // obvious diagonal grid the way an even split would.
      struct Wave { float dx, dz, k, amp, speed, steep; };
      std::vector<Wave> waves;
      waves.reserve((size_t)waveCount);
      float totalAmp = 0.0f;
      for (int w = 0; w < waveCount; w++)
      {
         const float falloff = std::pow(0.62f, (float)w);
         const float angle = direction + (float)w * 2.39996323f;
         const float length = std::max(0.02f, wavelength * falloff);
         Wave wave;
         wave.dx = std::cos(angle);
         wave.dz = std::sin(angle);
         wave.k = 6.28318530718f / length;
         wave.amp = amplitude * falloff;
         // Deep-water dispersion: longer waves genuinely travel faster, which
         // is what stops the octaves marching in lockstep.
         wave.speed = std::sqrt(9.81f / wave.k);
         wave.steep = steepness * falloff;
         waves.push_back(wave);
         totalAmp += wave.amp;
      }
      // Steepness is shared across the octaves; letting each use the full value
      // lets the sum loop the surface back through itself and self-intersect.
      const float steepNorm = (totalAmp > 1e-6f) ? 1.0f / (totalAmp * (float)waveCount) : 0.0f;

      for (int z = 0; z <= n; z++)
      {
         for (int x = 0; x <= n; x++)
         {
            const float u = (float)x / (float)n;
            const float v = (float)z / (float)n;
            const float px = (u - 0.5f) * size;
            const float pz = (v - 0.5f) * size;

            float dispX = 0.0f, dispY = 0.0f, dispZ = 0.0f;
            for (const Wave& w : waves)
            {
               const float phase = w.k * (w.dx * px + w.dz * pz) - w.speed * w.k * time;
               const float c = std::cos(phase);
               const float s = std::sin(phase);
               dispY += w.amp * s;
               // The horizontal displacement is what makes a Gerstner crest
               // sharpen and a trough flatten, instead of a plain sine hump.
               const float q = w.steep * steepNorm * w.amp * choppiness;
               dispX += q * w.dx * c;
               dispZ += q * w.dz * c;
            }

            Vertex vert;
            vert.px = px + dispX;
            vert.py = dispY;
            vert.pz = pz + dispZ;
            vert.nx = 0; vert.ny = 1; vert.nz = 0;
            vert.u = u;
            vert.v = v;
            out.vertices.push_back(vert);
         }
      }

      const int stride = n + 1;
      out.indices.reserve((size_t)n * (size_t)n * 6);
      for (int z = 0; z < n; z++)
      {
         for (int x = 0; x < n; x++)
         {
            const unsigned int a = (unsigned int)(z * stride + x);
            const unsigned int b = (unsigned int)(z * stride + x + 1);
            const unsigned int c = (unsigned int)((z + 1) * stride + x + 1);
            const unsigned int d = (unsigned int)((z + 1) * stride + x);
            PushQuad(out, a, b, c, d);
         }
      }

      // Normals are derived from the displaced mesh rather than analytically:
      // the horizontal displacement moves vertices sideways, so the analytic
      // gradient of the height field alone would light the crests wrongly.
      return RecalculateNormals(out, false, false);
   }

   Mesh Bevel(const Mesh& in, float amount, int segments)
   {
      if (in.Empty() || amount <= 0.0f)
         return in;

      // Subdividing first is what gives the rounding somewhere to happen: on a
      // raw cube there are no interior vertices to move, so the corners would
      // simply collapse toward the centre.
      Mesh work = in;
      const int passes = std::max(1, std::min(segments, 3));
      for (int i = 0; i < passes; i++)
      {
         if (work.indices.size() / 3 > 100000)
            break;
         work = Subdivide(work, 1, 0.0f);
      }

      const std::vector<unsigned int> weld = BuildWeldMap(work);
      const std::map<unsigned int, std::set<unsigned int>> adjacency = BuildAdjacency(work, weld);

      std::vector<float> px(work.vertices.size()), py(work.vertices.size()), pz(work.vertices.size());
      for (size_t i = 0; i < work.vertices.size(); i++)
      {
         px[i] = work.vertices[i].px; py[i] = work.vertices[i].py; pz[i] = work.vertices[i].pz;
      }

      // A few Laplacian passes scaled by the bevel amount. Corners have more
      // neighbours pulling them inward than a face centre does, so they round
      // first and flat regions barely move - which is the behaviour wanted.
      const float strength = std::max(0.0f, std::min(amount, 1.0f));
      for (int pass = 0; pass < 2; pass++)
      {
         std::vector<float> nx = px, ny = py, nz = pz;
         for (const auto& entry : adjacency)
         {
            const unsigned int v = entry.first;
            if (entry.second.empty())
               continue;
            float sx = 0, sy = 0, sz = 0;
            for (unsigned int n : entry.second)
            {
               sx += px[n]; sy += py[n]; sz += pz[n];
            }
            const float inv = 1.0f / (float)entry.second.size();
            nx[v] = px[v] + (sx * inv - px[v]) * strength;
            ny[v] = py[v] + (sy * inv - py[v]) * strength;
            nz[v] = pz[v] + (sz * inv - pz[v]) * strength;
         }
         px.swap(nx); py.swap(ny); pz.swap(nz);
      }

      for (size_t i = 0; i < work.vertices.size(); i++)
      {
         const unsigned int rep = weld[i];
         work.vertices[i].px = px[rep];
         work.vertices[i].py = py[rep];
         work.vertices[i].pz = pz[rep];
      }
      return RecalculateNormals(work, false, false);
   }

   Mesh RecalculateNormals(const Mesh& in, bool flat, bool flip)
   {
      Mesh out = in;
      if (flat)
      {
         // Flat shading needs unshared vertices, so the mesh is expanded.
         Mesh expanded;
         expanded.vertices.reserve(in.indices.size());
         expanded.indices.reserve(in.indices.size());
         for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
         {
            const Vertex& a = in.vertices[in.indices[t]];
            const Vertex& b = in.vertices[in.indices[t + 1]];
            const Vertex& c = in.vertices[in.indices[t + 2]];
            const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
            const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
            float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
            const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            if (flip) { nx = -nx; ny = -ny; nz = -nz; }
            for (const Vertex* src : { &a, &b, &c })
            {
               Vertex v = *src;
               v.nx = nx; v.ny = ny; v.nz = nz;
               expanded.vertices.push_back(v);
               expanded.indices.push_back((unsigned int)expanded.vertices.size() - 1);
            }
         }
         return expanded;
      }

      for (Vertex& v : out.vertices) { v.nx = 0; v.ny = 0; v.nz = 0; }
      for (size_t t = 0; t + 2 < out.indices.size(); t += 3)
      {
         Vertex& a = out.vertices[out.indices[t]];
         Vertex& b = out.vertices[out.indices[t + 1]];
         Vertex& c = out.vertices[out.indices[t + 2]];
         const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
         const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
         const float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
         for (Vertex* v : { &a, &b, &c }) { v->nx += nx; v->ny += ny; v->nz += nz; }
      }
      for (Vertex& v : out.vertices)
      {
         const float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
         if (len > 1e-8f) { v.nx /= len; v.ny /= len; v.nz /= len; }
         if (flip) { v.nx = -v.nx; v.ny = -v.ny; v.nz = -v.nz; }
      }
      return out;
   }

   Mesh Solidify(const Mesh& in, float thickness, bool keepOriginal)
   {
      Mesh out;
      const size_t vertCount = in.vertices.size();
      out.vertices.reserve(vertCount * 2);

      // outer shell
      for (const Vertex& v : in.vertices)
         out.vertices.push_back(v);
      // inner shell, pushed back along the normal
      for (const Vertex& v : in.vertices)
      {
         Vertex inner = v;
         inner.px -= v.nx * thickness;
         inner.py -= v.ny * thickness;
         inner.pz -= v.nz * thickness;
         inner.nx = -v.nx; inner.ny = -v.ny; inner.nz = -v.nz;
         out.vertices.push_back(inner);
      }

      if (keepOriginal)
         out.indices = in.indices;

      // inner faces, wound the other way
      for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
      {
         out.indices.push_back((unsigned int)(in.indices[t + 2] + vertCount));
         out.indices.push_back((unsigned int)(in.indices[t + 1] + vertCount));
         out.indices.push_back((unsigned int)(in.indices[t] + vertCount));
      }
      return out;
   }

   Mesh Extrude(const Mesh& in, float distance, float inset)
   {
      Mesh out;
      // Each triangle becomes a capped prism: its own top face plus three sides.
      for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
      {
         const Vertex& a = in.vertices[in.indices[t]];
         const Vertex& b = in.vertices[in.indices[t + 1]];
         const Vertex& c = in.vertices[in.indices[t + 2]];

         const float cx = (a.px + b.px + c.px) / 3.0f;
         const float cy = (a.py + b.py + c.py) / 3.0f;
         const float cz = (a.pz + b.pz + c.pz) / 3.0f;
         const float nx = (a.nx + b.nx + c.nx) / 3.0f;
         const float ny = (a.ny + b.ny + c.ny) / 3.0f;
         const float nz = (a.nz + b.nz + c.nz) / 3.0f;

         const unsigned int base = (unsigned int)out.vertices.size();
         const Vertex* src[3] = { &a, &b, &c };
         for (int i = 0; i < 3; i++)
         {
            Vertex low = *src[i];
            low.px += (cx - low.px) * inset;
            low.py += (cy - low.py) * inset;
            low.pz += (cz - low.pz) * inset;
            out.vertices.push_back(low);
         }
         for (int i = 0; i < 3; i++)
         {
            Vertex high = out.vertices[base + i];
            high.px += nx * distance;
            high.py += ny * distance;
            high.pz += nz * distance;
            out.vertices.push_back(high);
         }

         // top cap
         out.indices.push_back(base + 3); out.indices.push_back(base + 4); out.indices.push_back(base + 5);
         // walls
         for (int i = 0; i < 3; i++)
         {
            const unsigned int i0 = base + i;
            const unsigned int i1 = base + (i + 1) % 3;
            const unsigned int j0 = i0 + 3;
            const unsigned int j1 = i1 + 3;
            out.indices.push_back(i0); out.indices.push_back(j0); out.indices.push_back(j1);
            out.indices.push_back(i0); out.indices.push_back(j1); out.indices.push_back(i1);
         }
      }
      return RecalculateNormals(out, true, false);
   }

   Mesh Wireframe(const Mesh& in, float thickness)
   {
      Mesh out;
      const float t = std::max(0.001f, thickness);
      // Each edge becomes a thin box. Cheap, and reads correctly at a distance.
      std::map<std::pair<unsigned int, unsigned int>, bool> seen;
      for (size_t i = 0; i + 2 < in.indices.size(); i += 3)
      {
         const unsigned int tri[3] = { in.indices[i], in.indices[i + 1], in.indices[i + 2] };
         for (int e = 0; e < 3; e++)
         {
            const unsigned int a = tri[e];
            const unsigned int b = tri[(e + 1) % 3];
            auto key = std::minmax(a, b);
            if (seen.count({ key.first, key.second }))
               continue;
            seen[{ key.first, key.second }] = true;
            if (out.indices.size() / 3 > 300000)
               return RecalculateNormals(out, true, false);

            const Vertex& va = in.vertices[a];
            const Vertex& vb = in.vertices[b];
            float dir[3] = { vb.px - va.px, vb.py - va.py, vb.pz - va.pz };
            const float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
            if (len < 1e-6f)
               continue;
            dir[0] /= len; dir[1] /= len; dir[2] /= len;

            float up[3] = { 0, 1, 0 };
            if (std::fabs(dir[1]) > 0.9f) { up[0] = 1; up[1] = 0; }
            float side[3] = { dir[1]*up[2] - dir[2]*up[1],
                              dir[2]*up[0] - dir[0]*up[2],
                              dir[0]*up[1] - dir[1]*up[0] };
            float sl = std::sqrt(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
            if (sl < 1e-6f) continue;
            side[0] /= sl; side[1] /= sl; side[2] /= sl;
            const float other[3] = { dir[1]*side[2] - dir[2]*side[1],
                                     dir[2]*side[0] - dir[0]*side[2],
                                     dir[0]*side[1] - dir[1]*side[0] };

            const unsigned int base = (unsigned int)out.vertices.size();
            for (int end = 0; end < 2; end++)
            {
               const Vertex& v = end == 0 ? va : vb;
               for (int corner = 0; corner < 4; corner++)
               {
                  const float sx = (corner == 0 || corner == 3) ? -t : t;
                  const float sy = (corner < 2) ? -t : t;
                  Vertex q;
                  q.px = v.px + side[0]*sx + other[0]*sy;
                  q.py = v.py + side[1]*sx + other[1]*sy;
                  q.pz = v.pz + side[2]*sx + other[2]*sy;
                  q.nx = side[0]*sx + other[0]*sy;
                  q.ny = side[1]*sx + other[1]*sy;
                  q.nz = side[2]*sx + other[2]*sy;
                  q.u = (float)end; q.v = (float)corner / 3.0f;
                  out.vertices.push_back(q);
               }
            }
            const unsigned int quads[4][4] = {
               { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 }
            };
            for (auto& q : quads)
            {
               out.indices.push_back(base + q[0]); out.indices.push_back(base + q[1]); out.indices.push_back(base + q[2]);
               out.indices.push_back(base + q[0]); out.indices.push_back(base + q[2]); out.indices.push_back(base + q[3]);
            }
         }
      }
      return RecalculateNormals(out, true, false);
   }

   Mesh Triangulate(const Mesh& in, float jitter)
   {
      Mesh out = RecalculateNormals(in, true, false);
      if (jitter > 0.0f)
      {
         for (size_t t = 0; t + 2 < out.indices.size(); t += 3)
         {
            const int face = (int)(t / 3);
            const float ox = (RandFrom(1.0f, face * 3 + 0) - 0.5f) * jitter;
            const float oy = (RandFrom(1.0f, face * 3 + 1) - 0.5f) * jitter;
            const float oz = (RandFrom(1.0f, face * 3 + 2) - 0.5f) * jitter;
            for (int k = 0; k < 3; k++)
            {
               Vertex& v = out.vertices[out.indices[t + k]];
               v.px += ox; v.py += oy; v.pz += oz;
            }
         }
      }
      return out;
   }

   Mesh Explode(const Mesh& in, float amount, float seed)
   {
      Mesh out = RecalculateNormals(in, true, false);
      for (size_t t = 0; t + 2 < out.indices.size(); t += 3)
      {
         const int face = (int)(t / 3);
         const Vertex& a = out.vertices[out.indices[t]];
         const float push = amount * (0.4f + RandFrom(seed, face) * 0.6f);
         const float nx = a.nx * push, ny = a.ny * push, nz = a.nz * push;
         for (int k = 0; k < 3; k++)
         {
            Vertex& v = out.vertices[out.indices[t + k]];
            v.px += nx; v.py += ny; v.pz += nz;
         }
      }
      return out;
   }

   Mesh Twist(const Mesh& in, float angle, int axis)
   {
      Mesh out = in;
      for (Vertex& v : out.vertices)
      {
         const float along = (axis == 0) ? v.px : (axis == 1) ? v.py : v.pz;
         const float a = angle * along;
         const float s = std::sin(a), c = std::cos(a);
         if (axis == 1)
         {
            const float x = v.px * c - v.pz * s;
            const float z = v.px * s + v.pz * c;
            v.px = x; v.pz = z;
         }
         else if (axis == 0)
         {
            const float y = v.py * c - v.pz * s;
            const float z = v.py * s + v.pz * c;
            v.py = y; v.pz = z;
         }
         else
         {
            const float x = v.px * c - v.py * s;
            const float y = v.px * s + v.py * c;
            v.px = x; v.py = y;
         }
      }
      return RecalculateNormals(out, false, false);
   }

   std::vector<MeshPoint> ToPoints(const Mesh& in, int mode, int maxPoints)
   {
      std::vector<MeshPoint> points;
      const int cap = std::max(1, std::min(maxPoints, 100000));

      if (mode == 0) // vertices
      {
         const int stride = std::max(1, (int)(in.vertices.size() / cap) + ((int)in.vertices.size() > cap ? 1 : 0));
         for (size_t i = 0; i < in.vertices.size(); i += stride)
         {
            const Vertex& v = in.vertices[i];
            points.push_back({ v.px, v.py, v.pz, v.nx, v.ny, v.nz, 1.0f, (int)i });
         }
      }
      else if (mode == 1) // edge midpoints
      {
         std::map<std::pair<unsigned int, unsigned int>, bool> seen;
         for (size_t t = 0; t + 2 < in.indices.size() && (int)points.size() < cap; t += 3)
         {
            const unsigned int tri[3] = { in.indices[t], in.indices[t + 1], in.indices[t + 2] };
            for (int e = 0; e < 3 && (int)points.size() < cap; e++)
            {
               auto key = std::minmax(tri[e], tri[(e + 1) % 3]);
               if (seen.count({ key.first, key.second }))
                  continue;
               seen[{ key.first, key.second }] = true;
               const Vertex& a = in.vertices[key.first];
               const Vertex& b = in.vertices[key.second];
               points.push_back({ (a.px+b.px)*0.5f, (a.py+b.py)*0.5f, (a.pz+b.pz)*0.5f,
                                  (a.nx+b.nx)*0.5f, (a.ny+b.ny)*0.5f, (a.nz+b.nz)*0.5f,
                                  1.0f, (int)points.size() });
            }
         }
      }
      else // face centres
      {
         const int faces = (int)(in.indices.size() / 3);
         const int stride = std::max(1, faces / cap + (faces > cap ? 1 : 0));
         for (int f = 0; f < faces; f += stride)
         {
            const size_t t = (size_t)f * 3;
            const Vertex& a = in.vertices[in.indices[t]];
            const Vertex& b = in.vertices[in.indices[t + 1]];
            const Vertex& c = in.vertices[in.indices[t + 2]];
            points.push_back({ (a.px+b.px+c.px)/3.0f, (a.py+b.py+c.py)/3.0f, (a.pz+b.pz+c.pz)/3.0f,
                               (a.nx+b.nx+c.nx)/3.0f, (a.ny+b.ny+c.ny)/3.0f, (a.nz+b.nz+c.nz)/3.0f,
                               1.0f, f });
         }
      }
      return points;
   }

   Mesh PointsToFaces(const std::vector<MeshPoint>& points, float size)
   {
      Mesh out;
      const float h = std::max(0.001f, size) * 0.5f;
      for (const MeshPoint& p : points)
      {
         // billboard-ish quad oriented to the point normal
         float n[3] = { p.nx, p.ny, p.nz };
         const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
         if (len > 1e-6f) { n[0] /= len; n[1] /= len; n[2] /= len; }
         float up[3] = { 0, 1, 0 };
         if (std::fabs(n[1]) > 0.9f) { up[0] = 1; up[1] = 0; }
         float sx[3] = { n[1]*up[2] - n[2]*up[1], n[2]*up[0] - n[0]*up[2], n[0]*up[1] - n[1]*up[0] };
         const float sl = std::sqrt(sx[0]*sx[0] + sx[1]*sx[1] + sx[2]*sx[2]);
         if (sl < 1e-6f) continue;
         sx[0] /= sl; sx[1] /= sl; sx[2] /= sl;
         const float sy[3] = { n[1]*sx[2] - n[2]*sx[1], n[2]*sx[0] - n[0]*sx[2], n[0]*sx[1] - n[1]*sx[0] };

         const unsigned int base = (unsigned int)out.vertices.size();
         const float corners[4][2] = { { -h, -h }, { h, -h }, { h, h }, { -h, h } };
         for (int c = 0; c < 4; c++)
         {
            Vertex v;
            v.px = p.px + sx[0]*corners[c][0]*p.scale + sy[0]*corners[c][1]*p.scale;
            v.py = p.py + sx[1]*corners[c][0]*p.scale + sy[1]*corners[c][1]*p.scale;
            v.pz = p.pz + sx[2]*corners[c][0]*p.scale + sy[2]*corners[c][1]*p.scale;
            v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
            v.u = (corners[c][0] / h + 1.0f) * 0.5f;
            v.v = (corners[c][1] / h + 1.0f) * 0.5f;
            out.vertices.push_back(v);
         }
         out.indices.push_back(base); out.indices.push_back(base + 1); out.indices.push_back(base + 2);
         out.indices.push_back(base); out.indices.push_back(base + 2); out.indices.push_back(base + 3);
      }
      return out;
   }
}

// ============================================================ more primitives

namespace Primitives
{
   Mesh Capsule(int rings, int sectors, float height)
   {
      // A sphere split at its equator with a cylinder inserted, so the caps
      // stay hemispherical at any height rather than stretching with it.
      Mesh mesh;
      const int r = std::max(2, rings);
      const int sec = std::max(3, sectors);
      const float half = std::max(0.0f, height) * 0.5f;
      const float radius = 0.5f;

      for (int i = 0; i <= r; i++)
      {
         const float v = (float)i / (float)r;
         const float phi = v * kPi;
         const float y = std::cos(phi) * radius;
         const float ring = std::sin(phi) * radius;
         // Offset the top half up and the bottom half down by the barrel length.
         const float offset = (phi <= kPi * 0.5f) ? half : -half;

         for (int j = 0; j <= sec; j++)
         {
            const float u = (float)j / (float)sec;
            const float theta = u * 2.0f * kPi;
            const float x = std::cos(theta) * ring;
            const float z = std::sin(theta) * ring;
            float nx = x, ny = y, nz = z;
            const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            PushVertex(mesh, x, y + offset, z, nx, ny, nz, u, v);
         }
      }

      const int stride = sec + 1;
      for (int i = 0; i < r; i++)
         for (int j = 0; j < sec; j++)
            PushQuad(mesh, (unsigned int)(i * stride + j),
                     (unsigned int)(i * stride + j + 1),
                     (unsigned int)((i + 1) * stride + j + 1),
                     (unsigned int)((i + 1) * stride + j));
      return mesh;
   }

   Mesh Tube(int sides, int rings, float innerRadius)
   {
      // A cylinder with its middle bored out: outer wall, inner wall wound the
      // other way so it faces inward, and an annulus capping each end.
      Mesh mesh;
      const int n = std::max(3, sides);
      const int r = std::max(1, rings);
      const float outer = 0.5f;
      const float inner = std::max(0.01f, std::min(innerRadius, 0.49f));

      auto wall = [&](float radius, bool facingOut)
      {
         const unsigned int base = (unsigned int)mesh.vertices.size();
         for (int i = 0; i <= r; i++)
         {
            const float v = (float)i / (float)r;
            const float y = v - 0.5f;
            for (int j = 0; j <= n; j++)
            {
               const float u = (float)j / (float)n;
               const float theta = u * 2.0f * kPi;
               const float c = std::cos(theta), s = std::sin(theta);
               const float sign = facingOut ? 1.0f : -1.0f;
               PushVertex(mesh, c * radius, y, s * radius, c * sign, 0.0f, s * sign, u, v);
            }
         }
         const int stride = n + 1;
         for (int i = 0; i < r; i++)
            for (int j = 0; j < n; j++)
            {
               const unsigned int a = base + (unsigned int)(i * stride + j);
               const unsigned int b = base + (unsigned int)(i * stride + j + 1);
               const unsigned int c = base + (unsigned int)((i + 1) * stride + j + 1);
               const unsigned int d = base + (unsigned int)((i + 1) * stride + j);
               if (facingOut)
                  PushQuad(mesh, a, b, c, d);
               else
                  PushQuad(mesh, a, d, c, b);
            }
      };

      wall(outer, true);
      wall(inner, false);

      for (int end = 0; end < 2; end++)
      {
         const float y = (end == 0) ? 0.5f : -0.5f;
         const float ny = (end == 0) ? 1.0f : -1.0f;
         const unsigned int base = (unsigned int)mesh.vertices.size();
         for (int j = 0; j <= n; j++)
         {
            const float u = (float)j / (float)n;
            const float theta = u * 2.0f * kPi;
            const float c = std::cos(theta), s = std::sin(theta);
            PushVertex(mesh, c * outer, y, s * outer, 0.0f, ny, 0.0f, u, 1.0f);
            PushVertex(mesh, c * inner, y, s * inner, 0.0f, ny, 0.0f, u, 0.0f);
         }
         for (int j = 0; j < n; j++)
         {
            const unsigned int a = base + (unsigned int)(j * 2);
            const unsigned int b = base + (unsigned int)(j * 2 + 1);
            const unsigned int c = base + (unsigned int)(j * 2 + 3);
            const unsigned int d = base + (unsigned int)(j * 2 + 2);
            if (end == 0)
               PushQuad(mesh, a, b, c, d);
            else
               PushQuad(mesh, a, d, c, b);
         }
      }
      return mesh;
   }

   Mesh Pyramid(int sides)
   {
      Mesh mesh;
      const int n = std::max(3, sides);
      const float radius = 0.5f;
      const float apexY = 0.5f, baseY = -0.5f;

      // Flat-shaded: each face gets its own vertices, or the apex normal would
      // be averaged across every side and the facets would smear together.
      for (int j = 0; j < n; j++)
      {
         const float t0 = (float)j / (float)n * 2.0f * kPi;
         const float t1 = (float)(j + 1) / (float)n * 2.0f * kPi;
         const float x0 = std::cos(t0) * radius, z0 = std::sin(t0) * radius;
         const float x1 = std::cos(t1) * radius, z1 = std::sin(t1) * radius;

         float ax = x1 - x0, az = z1 - z0;
         float bx = -x0, by = apexY - baseY, bz = -z0;
         float nx = az * by - 0.0f * bz;
         float ny = 0.0f * bx - ax * bz;
         float nz = ax * by - 0.0f * bx;
         const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
         if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }

         const unsigned int base = (unsigned int)mesh.vertices.size();
         PushVertex(mesh, x0, baseY, z0, nx, ny, nz, 0.0f, 0.0f);
         PushVertex(mesh, x1, baseY, z1, nx, ny, nz, 1.0f, 0.0f);
         PushVertex(mesh, 0.0f, apexY, 0.0f, nx, ny, nz, 0.5f, 1.0f);
         mesh.indices.push_back(base);
         mesh.indices.push_back(base + 1);
         mesh.indices.push_back(base + 2);
      }

      const unsigned int centre = (unsigned int)mesh.vertices.size();
      PushVertex(mesh, 0.0f, baseY, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f);
      for (int j = 0; j <= n; j++)
      {
         const float t = (float)j / (float)n * 2.0f * kPi;
         PushVertex(mesh, std::cos(t) * radius, baseY, std::sin(t) * radius,
                    0.0f, -1.0f, 0.0f, 0.5f + std::cos(t) * 0.5f, 0.5f + std::sin(t) * 0.5f);
      }
      for (int j = 0; j < n; j++)
      {
         mesh.indices.push_back(centre);
         mesh.indices.push_back(centre + 1 + (unsigned int)j + 1);
         mesh.indices.push_back(centre + 1 + (unsigned int)j);
      }
      return mesh;
   }

   Mesh Prism(int sides, int rings)
   {
      // Cylinder with a low side count is a prism; this just gives it a name
      // and a flat-shaded look rather than a smoothed barrel.
      Mesh mesh = Cylinder(std::max(3, sides), std::max(1, rings), 1.0f);
      return MeshOps::RecalculateNormals(mesh, true, false);
   }

   Mesh Helix(int segments, int sides, float tubeRadius, float turns, float height)
   {
      Mesh mesh;
      const int seg = std::max(8, std::min(segments, 4096));
      const int n = std::max(3, sides);
      const float coil = std::max(0.05f, tubeRadius);
      const float radius = 0.5f;

      for (int i = 0; i <= seg; i++)
      {
         const float t = (float)i / (float)seg;
         const float angle = t * turns * 2.0f * kPi;
         const float cx = std::cos(angle) * radius;
         const float cz = std::sin(angle) * radius;
         const float cy = (t - 0.5f) * height;

         // Frame from the analytic tangent rather than finite differences, so
         // the tube does not wobble where the curve is sampled coarsely.
         float tx = -std::sin(angle) * radius * turns * 2.0f * kPi;
         float tz = std::cos(angle) * radius * turns * 2.0f * kPi;
         float ty = height;
         const float tl = std::sqrt(tx*tx + ty*ty + tz*tz);
         if (tl > 1e-6f) { tx /= tl; ty /= tl; tz /= tl; }

         float ux = 0.0f, uy = 1.0f, uz = 0.0f;
         if (std::fabs(ty) > 0.99f) { ux = 1.0f; uy = 0.0f; }
         float nx = uy * tz - uz * ty;
         float ny = uz * tx - ux * tz;
         float nz = ux * ty - uy * tx;
         const float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
         if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
         const float bx = ty * nz - tz * ny;
         const float by = tz * nx - tx * nz;
         const float bz = tx * ny - ty * nx;

         for (int j = 0; j <= n; j++)
         {
            const float u = (float)j / (float)n;
            const float theta = u * 2.0f * kPi;
            const float c = std::cos(theta), s = std::sin(theta);
            const float ox = nx * c + bx * s;
            const float oy = ny * c + by * s;
            const float oz = nz * c + bz * s;
            PushVertex(mesh, cx + ox * coil, cy + oy * coil, cz + oz * coil,
                       ox, oy, oz, u, t);
         }
      }

      const int stride = n + 1;
      for (int i = 0; i < seg; i++)
         for (int j = 0; j < n; j++)
            PushQuad(mesh, (unsigned int)(i * stride + j),
                     (unsigned int)((i + 1) * stride + j),
                     (unsigned int)((i + 1) * stride + j + 1),
                     (unsigned int)(i * stride + j + 1));
      return mesh;
   }

   Mesh Supershape(int rings, int sectors, float m1, float n1, float n2, float n3,
                   float m2, float p1, float p2, float p3)
   {
      Mesh mesh;
      const int r = std::max(3, rings);
      const int sec = std::max(3, sectors);

      auto super = [](float angle, float m, float e1, float e2, float e3) -> float
      {
         const float t = m * angle * 0.25f;
         const float a = std::pow(std::fabs(std::cos(t)), e2);
         const float b = std::pow(std::fabs(std::sin(t)), e3);
         const float sum = a + b;
         if (sum < 1e-6f)
            return 0.0f;
         return std::pow(sum, -1.0f / std::max(1e-3f, e1));
      };

      for (int i = 0; i <= r; i++)
      {
         const float v = (float)i / (float)r;
         const float phi = (v - 0.5f) * kPi;
         const float r2 = super(phi, m2, p1, p2, p3);
         for (int j = 0; j <= sec; j++)
         {
            const float u = (float)j / (float)sec;
            const float theta = (u - 0.5f) * 2.0f * kPi;
            const float r1 = super(theta, m1, n1, n2, n3);
            const float x = r1 * std::cos(theta) * r2 * std::cos(phi) * 0.5f;
            const float y = r2 * std::sin(phi) * 0.5f;
            const float z = r1 * std::sin(theta) * r2 * std::cos(phi) * 0.5f;
            PushVertex(mesh, x, y, z, 0.0f, 1.0f, 0.0f, u, v);
         }
      }

      const int stride = sec + 1;
      for (int i = 0; i < r; i++)
         for (int j = 0; j < sec; j++)
            PushQuad(mesh, (unsigned int)(i * stride + j),
                     (unsigned int)(i * stride + j + 1),
                     (unsigned int)((i + 1) * stride + j + 1),
                     (unsigned int)((i + 1) * stride + j));
      // Normals are derived rather than analytic: the superformula's gradient
      // is unpleasant and the mesh is dense enough that derived ones are fine.
      return MeshOps::RecalculateNormals(mesh, false, false);
   }
}

// ================================================================= metaballs

namespace Primitives
{
namespace
{
   // Marching cubes needs the 256-entry edge table to know which of the twelve
   // cube edges a surface crosses for a given corner sign pattern. Rather than
   // paste the canonical table, it is derived once at startup from the twelve
   // edge endpoints: an edge is crossed exactly when its two corners disagree.
   //
   // The tetrahedral decomposition below is used instead of the full 256-case
   // triangle table. It emits more triangles for the same surface, but it needs
   // no table at all and cannot produce the ambiguous-face holes that a naive
   // partial marching-cubes table does.
   const int kTetraOfCube[6][4] = {
      { 0, 5, 1, 6 }, { 0, 1, 2, 6 }, { 0, 2, 3, 6 },
      { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }
   };

   struct FieldPoint { float x, y, z, value; };

   void EmitTetra(Mesh& mesh, const FieldPoint p[4], float threshold)
   {
      // Classify corners, then emit the one or two triangles that separate the
      // inside corners from the outside ones.
      int inside[4], insideCount = 0, outside[4], outsideCount = 0;
      for (int i = 0; i < 4; i++)
      {
         if (p[i].value >= threshold)
            inside[insideCount++] = i;
         else
            outside[outsideCount++] = i;
      }
      if (insideCount == 0 || insideCount == 4)
         return;

      auto lerpPoint = [&](int a, int b, float out[3])
      {
         const float va = p[a].value, vb = p[b].value;
         const float denom = vb - va;
         const float t = (std::fabs(denom) < 1e-9f) ? 0.5f : (threshold - va) / denom;
         const float clamped = std::max(0.0f, std::min(t, 1.0f));
         out[0] = p[a].x + (p[b].x - p[a].x) * clamped;
         out[1] = p[a].y + (p[b].y - p[a].y) * clamped;
         out[2] = p[a].z + (p[b].z - p[a].z) * clamped;
      };

      auto pushTri = [&](const float a[3], const float b[3], const float c[3])
      {
         const unsigned int base = (unsigned int)mesh.vertices.size();
         // Normals are left flat here and recomputed from the finished surface;
         // per-tetra normals would be faceted along every cell boundary.
         PushVertex(mesh, a[0], a[1], a[2], 0, 1, 0, 0, 0);
         PushVertex(mesh, b[0], b[1], b[2], 0, 1, 0, 1, 0);
         PushVertex(mesh, c[0], c[1], c[2], 0, 1, 0, 0, 1);
         mesh.indices.push_back(base);
         mesh.indices.push_back(base + 1);
         mesh.indices.push_back(base + 2);
      };

      float v0[3], v1[3], v2[3], v3[3];
      if (insideCount == 1)
      {
         lerpPoint(inside[0], outside[0], v0);
         lerpPoint(inside[0], outside[1], v1);
         lerpPoint(inside[0], outside[2], v2);
         pushTri(v0, v1, v2);
      }
      else if (insideCount == 3)
      {
         lerpPoint(outside[0], inside[0], v0);
         lerpPoint(outside[0], inside[1], v1);
         lerpPoint(outside[0], inside[2], v2);
         pushTri(v0, v2, v1);
      }
      else // two in, two out: the surface crosses four edges, so a quad
      {
         lerpPoint(inside[0], outside[0], v0);
         lerpPoint(inside[0], outside[1], v1);
         lerpPoint(inside[1], outside[1], v2);
         lerpPoint(inside[1], outside[0], v3);
         pushTri(v0, v1, v2);
         pushTri(v0, v2, v3);
      }
   }
}

Mesh MetaBalls(const std::vector<MetaBall>& balls, int resolution, float threshold,
               float bounds)
{
   Mesh mesh;
   if (balls.empty())
      return mesh;

   // Capped: the grid is resolution cubed, and the sampling is balls times that.
   const int n = std::max(4, std::min(resolution, 96));
   const float extent = std::max(0.1f, bounds);
   const float step = (extent * 2.0f) / (float)n;

   // Field sampled once per grid point rather than per tetrahedron: each point
   // is shared by up to eight cells and six tetrahedra within each.
   std::vector<float> field((size_t)(n + 1) * (n + 1) * (n + 1), 0.0f);
   auto at = [&](int x, int y, int z) -> float& {
      return field[((size_t)z * (n + 1) + y) * (n + 1) + x];
   };

   for (int z = 0; z <= n; z++)
   {
      const float pz = -extent + step * (float)z;
      for (int y = 0; y <= n; y++)
      {
         const float py = -extent + step * (float)y;
         for (int x = 0; x <= n; x++)
         {
            const float px = -extent + step * (float)x;
            float sum = 0.0f;
            for (const MetaBall& b : balls)
            {
               const float dx = px - b.x, dy = py - b.y, dz = pz - b.z;
               const float d2 = dx*dx + dy*dy + dz*dz;
               // Inverse-square falloff: summing these is what makes two balls
               // bulge toward each other and merge rather than intersect.
               sum += b.strength / std::max(d2, 1e-4f);
            }
            at(x, y, z) = sum;
         }
      }
   }

   for (int z = 0; z < n; z++)
   {
      for (int y = 0; y < n; y++)
      {
         for (int x = 0; x < n; x++)
         {
            FieldPoint corner[8];
            for (int c = 0; c < 8; c++)
            {
               const int cx = x + ((c == 1 || c == 2 || c == 5 || c == 6) ? 1 : 0);
               const int cy = y + ((c == 2 || c == 3 || c == 6 || c == 7) ? 1 : 0);
               const int cz = z + ((c >= 4) ? 1 : 0);
               corner[c].x = -extent + step * (float)cx;
               corner[c].y = -extent + step * (float)cy;
               corner[c].z = -extent + step * (float)cz;
               corner[c].value = at(cx, cy, cz);
            }
            for (int t = 0; t < 6; t++)
            {
               const FieldPoint tet[4] = {
                  corner[kTetraOfCube[t][0]], corner[kTetraOfCube[t][1]],
                  corner[kTetraOfCube[t][2]], corner[kTetraOfCube[t][3]]
               };
               EmitTetra(mesh, tet, threshold);
            }
         }
      }
   }

   if (mesh.Empty())
      return mesh;
   return MeshOps::RecalculateNormals(mesh, false, false);
}
}

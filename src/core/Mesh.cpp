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

   Mesh Subdivide(const Mesh& in, int levels, float smooth)
   {
      Mesh current = in;
      const int passes = std::max(0, std::min(levels, 3));
      for (int pass = 0; pass < passes; pass++)
      {
         // Guard against a runaway: each pass quadruples the triangle count.
         if (current.indices.size() / 3 > 200000)
            break;

         Mesh next;
         std::map<std::pair<unsigned int, unsigned int>, unsigned int> edgeCache;
         next.vertices = current.vertices;

         auto midpoint = [&](unsigned int a, unsigned int b) -> unsigned int {
            auto key = std::minmax(a, b);
            auto it = edgeCache.find({ key.first, key.second });
            if (it != edgeCache.end())
               return it->second;
            const Vertex& va = current.vertices[a];
            const Vertex& vb = current.vertices[b];
            Vertex m;
            m.px = (va.px + vb.px) * 0.5f;
            m.py = (va.py + vb.py) * 0.5f;
            m.pz = (va.pz + vb.pz) * 0.5f;
            m.nx = (va.nx + vb.nx) * 0.5f;
            m.ny = (va.ny + vb.ny) * 0.5f;
            m.nz = (va.nz + vb.nz) * 0.5f;
            const float len = std::sqrt(m.nx*m.nx + m.ny*m.ny + m.nz*m.nz);
            if (len > 1e-6f) { m.nx /= len; m.ny /= len; m.nz /= len; }
            // Nudging the midpoint along its normal rounds the surface off,
            // which is the cheap stand-in for a real Catmull-Clark limit surface.
            if (smooth > 0.0f)
            {
               const float edgeLen = std::sqrt((va.px-vb.px)*(va.px-vb.px) +
                                               (va.py-vb.py)*(va.py-vb.py) +
                                               (va.pz-vb.pz)*(va.pz-vb.pz));
               const float push = edgeLen * 0.12f * smooth;
               m.px += m.nx * push; m.py += m.ny * push; m.pz += m.nz * push;
            }
            m.u = (va.u + vb.u) * 0.5f;
            m.v = (va.v + vb.v) * 0.5f;
            next.vertices.push_back(m);
            const unsigned int index = (unsigned int)next.vertices.size() - 1;
            edgeCache[{ key.first, key.second }] = index;
            return index;
         };

         for (size_t t = 0; t + 2 < current.indices.size(); t += 3)
         {
            const unsigned int a = current.indices[t];
            const unsigned int b = current.indices[t + 1];
            const unsigned int c = current.indices[t + 2];
            const unsigned int ab = midpoint(a, b);
            const unsigned int bc = midpoint(b, c);
            const unsigned int ca = midpoint(c, a);
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
      return current;
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

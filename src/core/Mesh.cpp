#include "Mesh.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>

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

   // Bilinear sample of an RGBA float buffer (row 0 = bottom, matching
   // glReadPixels/GL_TEXTURE_2D convention) with wrapped UVs - GL_REPEAT, not
   // clamp, so a displacement texture tiled across an Array still samples the
   // pattern instead of smearing the edge texel past UV 1.
   void SampleBilinearRGBA(const std::vector<float>& rgba, int w, int h, float u, float v, float out[4])
   {
      if (rgba.empty() || w <= 0 || h <= 0)
      {
         out[0] = out[1] = out[2] = out[3] = 0.0f;
         return;
      }
      auto wrap01 = [](float x) { x = x - std::floor(x); return x; };
      u = wrap01(u);
      v = wrap01(v);
      const float fx = u * w - 0.5f;
      const float fy = v * h - 0.5f;
      int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
      const float tx = fx - (float)x0, ty = fy - (float)y0;
      auto wrapI = [](int x, int n) { x %= n; return x < 0 ? x + n : x; };
      const int x1 = wrapI(x0 + 1, w), y1 = wrapI(y0 + 1, h);
      x0 = wrapI(x0, w);
      y0 = wrapI(y0, h);
      auto at = [&](int x, int y, int c) { return rgba[((size_t)y * w + x) * 4 + c]; };
      for (int c = 0; c < 4; c++)
      {
         const float a = at(x0, y0, c) * (1 - tx) + at(x1, y0, c) * tx;
         const float b = at(x0, y1, c) * (1 - tx) + at(x1, y1, c) * tx;
         out[c] = a * (1 - ty) + b * ty;
      }
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
         // t runs top (y=+0.5) to bottom (y=-0.5), so topRadius scales t=0.
         // The caps below key off the same end; if the two ever disagree a
         // cone tapers to a point at one end and gets capped at the other.
         const float radius = (0.5f * topRadius) * (1.0f - t) + 0.5f * t;
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
         // Each repetition is a straight copy of `in`, in the same order -
         // Transform() carries copy.vertexColor through unchanged - so the
         // repetitions' colour blocks just concatenate.
         if (!copy.vertexColor.empty())
            out.vertexColor.insert(out.vertexColor.end(),
                                   copy.vertexColor.begin(), copy.vertexColor.end());
      }
      return out;
   }

   Mesh RealizeInstances(const Mesh& stamp,
                         const std::vector<Mat4>& xforms,
                         const Mat4& groupMatrix,
                         const std::vector<float>* instanceColors,
                         int maxInstances)
   {
      Mesh out;
      if (stamp.vertices.empty() || xforms.empty())
         return out;

      const int n = std::max(0, std::min((int)xforms.size(), maxInstances));
      if (n == 0)
         return out;

      out.vertices.reserve(stamp.vertices.size() * (size_t)n);
      out.indices.reserve(stamp.indices.size() * (size_t)n);
      if (!stamp.faceMask.empty())
         out.faceMask.reserve(stamp.faceMask.size() * (size_t)n);

      const bool isGroupIdent = groupMatrix == Mat4::Identity();
      for (int i = 0; i < n; i++)
      {
         const Mat4 m = isGroupIdent ? xforms[i] : Mat4::Multiply(groupMatrix, xforms[i]);
         Mesh copy = Transform(stamp, m);

         if (instanceColors && (size_t)i * 3 + 2 < instanceColors->size())
         {
            const float cr = (*instanceColors)[(size_t)i * 3 + 0];
            const float cg = (*instanceColors)[(size_t)i * 3 + 1];
            const float cb = (*instanceColors)[(size_t)i * 3 + 2];
            if (copy.vertexColor.empty())
            {
               copy.vertexColor.reserve(copy.vertices.size() * 3);
               for (size_t v = 0; v < copy.vertices.size(); v++)
               {
                  copy.vertexColor.push_back(cr);
                  copy.vertexColor.push_back(cg);
                  copy.vertexColor.push_back(cb);
               }
            }
            else
            {
               for (size_t v = 0; v < copy.vertices.size(); v++)
               {
                  copy.vertexColor[v * 3 + 0] *= cr;
                  copy.vertexColor[v * 3 + 1] *= cg;
                  copy.vertexColor[v * 3 + 2] *= cb;
               }
            }
         }

         const unsigned int base = (unsigned int)out.vertices.size();
         out.vertices.insert(out.vertices.end(), copy.vertices.begin(), copy.vertices.end());
         for (unsigned int idx : copy.indices)
            out.indices.push_back(base + idx);
         if (!copy.vertexColor.empty())
            out.vertexColor.insert(out.vertexColor.end(),
                                   copy.vertexColor.begin(), copy.vertexColor.end());
         if (!copy.faceMask.empty())
            out.faceMask.insert(out.faceMask.end(),
                                copy.faceMask.begin(), copy.faceMask.end());
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

   // Same idea, but the snap distance is a caller-supplied threshold instead
   // of the hardcoded near-exact quantum above. threshold <= 0 is a no-op
   // (every vertex keeps its own index), matching "Merge by Distance" at zero
   // doing nothing.
   std::vector<unsigned int> BuildWeldMap(const Mesh& in, float threshold)
   {
      std::vector<unsigned int> weld(in.vertices.size());
      for (size_t i = 0; i < weld.size(); i++)
         weld[i] = (unsigned int)i;
      if (threshold <= 0.0f)
         return weld;

      std::map<std::array<long long, 3>, unsigned int> unique;
      const double quantum = 1.0 / (double)threshold;

      for (size_t i = 0; i < in.vertices.size(); i++)
      {
         const Vertex& v = in.vertices[i];
         const std::array<long long, 3> key = {
            (long long)std::llround((double)v.px * quantum),
            (long long)std::llround((double)v.py * quantum),
            (long long)std::llround((double)v.pz * quantum)
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

   Mesh MergeByDistance(const Mesh& in, float threshold)
   {
      if (threshold <= 0.0f || in.vertices.empty())
         return in;

      const std::vector<unsigned int> weldMap = BuildWeldMap(in, threshold);

      // Compact to one vertex per weld group, in first-encountered order, so
      // downstream index buffers still read front-to-back sensibly.
      std::vector<unsigned int> oldToNew(in.vertices.size());
      std::vector<unsigned int> newToOld;
      newToOld.reserve(in.vertices.size());
      for (size_t i = 0; i < in.vertices.size(); i++)
      {
         if (weldMap[i] == i)
         {
            oldToNew[i] = (unsigned int)newToOld.size();
            newToOld.push_back((unsigned int)i);
         }
      }
      for (size_t i = 0; i < in.vertices.size(); i++)
         if (weldMap[i] != i)
            oldToNew[i] = oldToNew[weldMap[i]];

      Mesh out;
      out.vertices.reserve(newToOld.size());
      for (unsigned int oldIdx : newToOld)
         out.vertices.push_back(in.vertices[oldIdx]);

      const bool hasSelection = !in.faceMask.empty();
      const bool hasGroups = !in.selectionGroup.empty();
      const size_t faceCount = in.indices.size() / 3;
      out.indices.reserve(in.indices.size());
      for (size_t f = 0; f < faceCount; f++)
      {
         const size_t t = f * 3;
         const unsigned int a = oldToNew[in.indices[t]];
         const unsigned int b = oldToNew[in.indices[t + 1]];
         const unsigned int c = oldToNew[in.indices[t + 2]];
         if (a == b || b == c || a == c)
            continue; // degenerate once its corners land on the same weld group
         out.indices.push_back(a);
         out.indices.push_back(b);
         out.indices.push_back(c);
         if (hasSelection)
            out.faceMask.push_back(f < in.faceMask.size() ? in.faceMask[f] : (unsigned char)1);
         if (hasGroups)
            out.selectionGroup.push_back(f < in.selectionGroup.size() ? in.selectionGroup[f] : 0u);
      }
      out.vertexColor = RemapVertexColor(in.vertexColor, newToOld, out.vertices.size());
      return out;
   }

   std::vector<float> RemapVertexColor(const std::vector<float>& srcColor,
                                        const std::vector<unsigned int>& mapping,
                                        size_t outVertexCount)
   {
      if (srcColor.empty())
         return {};
      const size_t srcVertexCount = srcColor.size() / 3;
      std::vector<float> out(outVertexCount * 3, 1.0f);
      for (size_t i = 0; i < outVertexCount && i < mapping.size(); i++)
      {
         const unsigned int src = mapping[i];
         if (src >= srcVertexCount)
            return {}; // out-of-range mapping: treat as absent rather than read garbage
         out[i * 3 + 0] = srcColor[src * 3 + 0];
         out[i * 3 + 1] = srcColor[src * 3 + 1];
         out[i * 3 + 2] = srcColor[src * 3 + 2];
      }
      return out;
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
         // Repositioned original vertices only move - they keep whatever
         // colour they entered the pass with, so the array carries straight
         // across before any edge points are appended below.
         next.vertexColor = current.vertexColor;
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
            // Plain midpoint, same reasoning as the UV/normal average just
            // above - and it has to run every time a vertex is pushed here,
            // or next.vertexColor falls out of parallel with next.vertices.
            if (current.HasVertexColor())
            {
               for (int k = 0; k < 3; k++)
                  next.vertexColor.push_back(
                     (current.vertexColor[ia * 3 + k] + current.vertexColor[ib * 3 + k]) * 0.5f);
            }
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

      // The mirrored block is `in`'s vertices in the same order, so its colour
      // is just `in.vertexColor` again - appended after the kept original when
      // keepOriginal already carried it via `out = in` above, or set outright
      // when there is no original block ahead of it.
      if (!in.vertexColor.empty())
      {
         if (keepOriginal)
            out.vertexColor.insert(out.vertexColor.end(),
                                   in.vertexColor.begin(), in.vertexColor.end());
         else
            out.vertexColor = in.vertexColor;
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
      // Every pushed vertex derives from exactly one profile-edge endpoint,
      // tracked here so vertexColor can be remapped afterward.
      std::vector<unsigned int> mapping;

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
               mapping.push_back((end == 0) ? edge.first : edge.second);
            }
         }

         for (int s = 0; s < segments; s++)
         {
            const unsigned int q = base + (unsigned int)s * 2;
            PushQuad(out, q, q + 2, q + 3, q + 1);
         }
      }
      out.vertexColor = RemapVertexColor(in.vertexColor, mapping, out.vertices.size());
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
         // Each new vertex derives from exactly one original vertex - tracked
         // here so vertexColor can be remapped the same way, rather than
         // silently dropped by the expansion.
         std::vector<unsigned int> mapping;
         mapping.reserve(in.indices.size());
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
            const unsigned int srcIndex[3] = { in.indices[t], in.indices[t + 1], in.indices[t + 2] };
            int k = 0;
            for (const Vertex* src : { &a, &b, &c })
            {
               Vertex v = *src;
               v.nx = nx; v.ny = ny; v.nz = nz;
               expanded.vertices.push_back(v);
               expanded.indices.push_back((unsigned int)expanded.vertices.size() - 1);
               mapping.push_back(srcIndex[k++]);
            }
         }
         expanded.vertexColor = RemapVertexColor(in.vertexColor, mapping, expanded.vertices.size());
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
      const size_t faces = in.FaceCount();
      if (faces == 0)
         return out;

      // Per-triangle face normal, used to tell where the mesh was actually
      // triangulated from an n-gon (coplanar neighbours) versus a real edge.
      std::vector<std::array<float, 3>> faceNormals(faces);
      for (size_t f = 0; f < faces; f++)
      {
         const Vertex& a = in.vertices[in.indices[f * 3]];
         const Vertex& b = in.vertices[in.indices[f * 3 + 1]];
         const Vertex& c = in.vertices[in.indices[f * 3 + 2]];
         const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
         const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
         float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
         const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
         if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
         faceNormals[f] = { nx, ny, nz };
      }

      // Union-find over selected triangles: two triangles sharing an edge
      // and facing the same way are the same source face, so no wall goes
      // between them (e.g. the diagonal inside a triangulated quad).
      std::vector<int> parent(faces);
      for (size_t f = 0; f < faces; f++) parent[f] = (int)f;
      auto find = [&](int x) {
         while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
         return x;
      };
      auto unite = [&](int a, int b) {
         a = find(a); b = find(b);
         if (a != b) parent[a] = b;
      };

      std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>> edgeTris;
      for (size_t f = 0; f < faces; f++)
      {
         if (!in.FaceSelected(f))
            continue;
         for (int k = 0; k < 3; k++)
         {
            const unsigned int a = in.indices[f * 3 + k];
            const unsigned int b = in.indices[f * 3 + (k + 1) % 3];
            const auto key = std::minmax(a, b);
            edgeTris[{ key.first, key.second }].push_back((unsigned int)f);
         }
      }
      for (auto& entry : edgeTris)
      {
         const auto& tris = entry.second;
         if (tris.size() != 2)
            continue;
         const auto& n0 = faceNormals[tris[0]];
         const auto& n1 = faceNormals[tris[1]];
         const float dot = n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2];
         if (dot > 0.9998f)
            unite((int)tris[0], (int)tris[1]);
      }

      // Region normal + centroid, averaged from every triangle merged into it.
      std::map<int, std::array<float, 3>> regionNormal;
      std::map<int, std::array<double, 3>> regionCentroidSum;
      std::map<int, int> regionCentroidCount;
      for (size_t f = 0; f < faces; f++)
      {
         if (!in.FaceSelected(f))
            continue;
         const int r = find((int)f);
         auto& rn = regionNormal[r];
         rn[0] += faceNormals[f][0]; rn[1] += faceNormals[f][1]; rn[2] += faceNormals[f][2];
         auto& cs = regionCentroidSum[r];
         for (int k = 0; k < 3; k++)
         {
            const Vertex& v = in.vertices[in.indices[f * 3 + k]];
            cs[0] += v.px; cs[1] += v.py; cs[2] += v.pz;
            regionCentroidCount[r]++;
         }
      }
      for (auto& entry : regionNormal)
      {
         auto& n = entry.second;
         const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
         if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; }
      }

      // Unselected faces (when a selection exists) pass through unchanged.
      std::vector<int> remap(in.vertices.size(), -1);
      auto copyVertex = [&](unsigned int index) {
         if (remap[index] < 0)
         {
            remap[index] = (int)out.vertices.size();
            out.vertices.push_back(in.vertices[index]);
         }
         return (unsigned int)remap[index];
      };
      for (size_t f = 0; f < faces; f++)
      {
         if (in.FaceSelected(f))
            continue;
         for (int k = 0; k < 3; k++)
            out.indices.push_back(copyVertex(in.indices[f * 3 + k]));
      }

      // Cap vertices (inset toward the region centroid, lifted along the
      // region normal) and rim vertices (original position), both keyed
      // per-region so neighbouring regions don't bleed into each other.
      std::map<std::pair<int, unsigned int>, unsigned int> regionCap;
      auto capVertex = [&](int region, unsigned int srcIndex) {
         const auto key = std::make_pair(region, srcIndex);
         const auto it = regionCap.find(key);
         if (it != regionCap.end())
            return it->second;
         const auto& n = regionNormal[region];
         const auto& cs = regionCentroidSum[region];
         const int count = regionCentroidCount[region];
         const float cx = (float)(cs[0] / count), cy = (float)(cs[1] / count), cz = (float)(cs[2] / count);
         Vertex v = in.vertices[srcIndex];
         v.px += (cx - v.px) * inset + n[0] * distance;
         v.py += (cy - v.py) * inset + n[1] * distance;
         v.pz += (cz - v.pz) * inset + n[2] * distance;
         const unsigned int idx = (unsigned int)out.vertices.size();
         out.vertices.push_back(v);
         regionCap[key] = idx;
         return idx;
      };
      std::map<std::pair<int, unsigned int>, unsigned int> regionRim;
      auto rimVertex = [&](int region, unsigned int srcIndex) {
         const auto key = std::make_pair(region, srcIndex);
         const auto it = regionRim.find(key);
         if (it != regionRim.end())
            return it->second;
         const unsigned int idx = (unsigned int)out.vertices.size();
         out.vertices.push_back(in.vertices[srcIndex]);
         regionRim[key] = idx;
         return idx;
      };

      // Top caps, using each region's own triangulation.
      for (size_t f = 0; f < faces; f++)
      {
         if (!in.FaceSelected(f))
            continue;
         const int region = find((int)f);
         const unsigned int cap0 = capVertex(region, in.indices[f * 3]);
         const unsigned int cap1 = capVertex(region, in.indices[f * 3 + 1]);
         const unsigned int cap2 = capVertex(region, in.indices[f * 3 + 2]);
         out.indices.push_back(cap0); out.indices.push_back(cap1); out.indices.push_back(cap2);
      }

      // Walls, only on edges that bound a region (true mesh boundary, or the
      // seam between two differently-facing regions) — never on the interior
      // diagonals introduced by triangulating an n-gon.
      for (size_t f = 0; f < faces; f++)
      {
         if (!in.FaceSelected(f))
            continue;
         const int region = find((int)f);
         for (int k = 0; k < 3; k++)
         {
            const unsigned int a = in.indices[f * 3 + k];
            const unsigned int b = in.indices[f * 3 + (k + 1) % 3];
            const auto key = std::minmax(a, b);
            const auto& tris = edgeTris[{ key.first, key.second }];
            int sameRegionCount = 0;
            for (unsigned int t : tris)
               if (find((int)t) == region)
                  sameRegionCount++;
            if (sameRegionCount != 1)
               continue;

            const unsigned int r0 = rimVertex(region, a);
            const unsigned int r1 = rimVertex(region, b);
            const unsigned int c0 = capVertex(region, a);
            const unsigned int c1 = capVertex(region, b);
            out.indices.push_back(r0); out.indices.push_back(c0); out.indices.push_back(c1);
            out.indices.push_back(r0); out.indices.push_back(c1); out.indices.push_back(r1);
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

   Mesh Twist(const Mesh& in, float angle, int axis, const std::vector<unsigned char>* vertexMask)
   {
      Mesh out = in;
      for (size_t i = 0; i < out.vertices.size(); i++)
      {
         if (vertexMask != nullptr && (i >= vertexMask->size() || !(*vertexMask)[i]))
            continue;
         Vertex& v = out.vertices[i];
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

   Mesh Displace(const Mesh& in, const std::vector<float>& texRGBA, int texW, int texH,
                 int mode, float strength, float midlevel, bool flat, bool flip,
                 const std::vector<unsigned char>* vertexMask)
   {
      Mesh out = in;
      if (!texRGBA.empty() && texW > 0 && texH > 0 && !out.vertices.empty())
      {
         // A hard-shaded primitive (Cube, Icosphere's poles, anything with a
         // UV seam) duplicates one corner into several vertices at the same
         // position but with different face normals and UVs. Displacing each
         // along its own normal/sample pulls those duplicates apart and tears
         // the surface open at exactly the edges that used to be closed - so
         // the offset is computed per vertex but then averaged across every
         // vertex sharing a start position (the same weld map Subdivide/Smooth
         // use) and applied uniformly to the group, keeping seams closed.
         const std::vector<unsigned int> weld = BuildWeldMap(out);
         const size_t n = out.vertices.size();
         std::vector<float> accum(n * 3, 0.0f);
         std::vector<int> count(n, 0);

         for (size_t i = 0; i < n; i++)
         {
            if (vertexMask != nullptr && (i >= vertexMask->size() || !(*vertexMask)[i]))
               continue;
            const Vertex& v = out.vertices[i];
            float s[4];
            SampleBilinearRGBA(texRGBA, texW, texH, v.u, v.v, s);
            float dx, dy, dz;
            if (mode == 1) // vector: RGB is an XYZ offset in the mesh's own local space
            {
               dx = (s[0] - 0.5f) * 2.0f * strength;
               dy = (s[1] - 0.5f) * 2.0f * strength;
               dz = (s[2] - 0.5f) * 2.0f * strength;
            }
            else // scalar: luminance pushes the vertex along its own normal
            {
               const float height = 0.299f * s[0] + 0.587f * s[1] + 0.114f * s[2];
               const float d = (height - midlevel) * strength;
               dx = v.nx * d; dy = v.ny * d; dz = v.nz * d;
            }
            const unsigned int w = weld[i];
            accum[w * 3 + 0] += dx; accum[w * 3 + 1] += dy; accum[w * 3 + 2] += dz;
            count[w]++;
         }

         for (size_t i = 0; i < n; i++)
         {
            if (vertexMask != nullptr && (i >= vertexMask->size() || !(*vertexMask)[i]))
               continue;
            const unsigned int w = weld[i];
            const float c = (float)std::max(1, count[w]);
            out.vertices[i].px += accum[w * 3 + 0] / c;
            out.vertices[i].py += accum[w * 3 + 1] / c;
            out.vertices[i].pz += accum[w * 3 + 2] / c;
         }
      }
      return RecalculateNormals(out, flat, flip);
   }

   // Averages three (or two) component-wise normals and renormalises, falling
   // back to the un-normalised sum (or +Y) when opposed normals cancel out.
   static void NormalizeOrFallback(float& nx, float& ny, float& nz)
   {
      const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
      if (len > 1e-6f)
      {
         nx /= len; ny /= len; nz /= len;
      }
      else if (nx == 0.0f && ny == 0.0f && nz == 0.0f)
      {
         ny = 1.0f;
      }
   }

   std::vector<MeshPoint> ToPoints(const Mesh& in, int mode, int maxPoints,
                                    bool weld, float dissolveAngleDegrees)
   {
      std::vector<MeshPoint> points;
      const int cap = std::max(1, std::min(maxPoints, 100000));

      // An empty faceMask means "everything selected" (Mesh's own convention),
      // so an unselected mesh takes the fast unfiltered path below and a
      // selected one is narrowed to just the chosen faces - a Select feeding
      // this node, directly or through Instance on Points, actually changes
      // what gets sampled instead of the selection being silently ignored.
      const bool hasSelection = !in.faceMask.empty();

      // Primitives duplicate vertices at every UV seam and flat-shaded edge
      // (PushGrid gives Cube(1) 24 vertices for 8 corners), so sampling the
      // raw index space over-counts every seam. Weld first so vertex/edge
      // sampling reasons about topology, not the duplicated index space.
      std::vector<unsigned int> weldMap;
      if (weld)
         weldMap = BuildWeldMap(in);
      else
      {
         weldMap.resize(in.vertices.size());
         for (size_t i = 0; i < weldMap.size(); i++)
            weldMap[i] = (unsigned int)i;
      }

      if (mode == 0) // vertices
      {
         if (!hasSelection)
         {
            // The representative of each weld group is the first vertex that
            // claimed its position, i.e. the indices where weldMap[i] == i.
            std::vector<size_t> reps;
            for (size_t i = 0; i < in.vertices.size(); i++)
               if (weldMap[i] == i)
                  reps.push_back(i);

            const int n = (int)reps.size();
            const int stride = std::max(1, n / cap + (n > cap ? 1 : 0));
            for (int i = 0; i < n; i += stride)
            {
               const size_t idx = reps[(size_t)i];
               const Vertex& v = in.vertices[idx];
               points.push_back({ v.px, v.py, v.pz, v.nx, v.ny, v.nz, 1.0f, (int)idx });
               if (in.HasVertexColor())
               {
                  MeshPoint& mp = points.back();
                  mp.r = in.vertexColor[idx * 3 + 0]; mp.g = in.vertexColor[idx * 3 + 1];
                  mp.b = in.vertexColor[idx * 3 + 2];
               }
            }
         }
         else
         {
            // A vertex counts as selected if any face touching it is selected,
            // the same "touches the selection" rule a face-select tool implies
            // for its corners.
            std::vector<char> touched(in.vertices.size(), 0);
            const int faces = (int)(in.indices.size() / 3);
            for (int f = 0; f < faces; f++)
            {
               if (!in.FaceSelected((size_t)f))
                  continue;
               const size_t t = (size_t)f * 3;
               touched[in.indices[t]] = 1;
               touched[in.indices[t + 1]] = 1;
               touched[in.indices[t + 2]] = 1;
            }

            std::vector<char> repSelected(in.vertices.size(), 0);
            for (size_t i = 0; i < touched.size(); i++)
               if (touched[i])
                  repSelected[weldMap[i]] = 1;

            std::vector<size_t> selected;
            for (size_t i = 0; i < in.vertices.size(); i++)
               if (weldMap[i] == i && repSelected[i])
                  selected.push_back(i);

            const int n = (int)selected.size();
            const int stride = std::max(1, n / cap + (n > cap ? 1 : 0));
            for (int i = 0; i < n; i += stride)
            {
               const size_t idx = selected[(size_t)i];
               const Vertex& v = in.vertices[idx];
               points.push_back({ v.px, v.py, v.pz, v.nx, v.ny, v.nz, 1.0f, (int)idx });
               if (in.HasVertexColor())
               {
                  MeshPoint& mp = points.back();
                  mp.r = in.vertexColor[idx * 3 + 0]; mp.g = in.vertexColor[idx * 3 + 1];
                  mp.b = in.vertexColor[idx * 3 + 2];
               }
            }
         }
      }
      else if (mode == 1) // edge midpoints
      {
         // Collect every unique edge first, then stride-sample across all of
         // them - the same even-coverage scheme vertices/faces use below,
         // rather than stopping as soon as the cap is hit, which would leave
         // the sample clustered wherever face traversal happened to be.
         //
         // The dedup key is built on welded indices, or the two triangles of
         // every quad face (the only ones that actually share raw indices)
         // are the only "duplicates" ever found, while every adjoining face -
         // which has entirely separate raw indices - is never recognised as
         // sharing the edge.
         struct EdgeInfo
         {
            unsigned int a, b;
            int faces[2] = { -1, -1 };
            int faceCount = 0;
         };
         std::map<std::pair<unsigned int, unsigned int>, size_t> edgeIndex;
         std::vector<EdgeInfo> edgeList;
         const int faces = (int)(in.indices.size() / 3);
         for (int f = 0; f < faces; f++)
         {
            if (!in.FaceSelected((size_t)f))
               continue;
            const size_t t = (size_t)f * 3;
            const unsigned int rawTri[3] = { in.indices[t], in.indices[t + 1], in.indices[t + 2] };
            const unsigned int tri[3] = { weldMap[rawTri[0]], weldMap[rawTri[1]], weldMap[rawTri[2]] };
            for (int e = 0; e < 3; e++)
            {
               auto key = std::minmax(tri[e], tri[(e + 1) % 3]);
               auto it = edgeIndex.find(key);
               size_t idx;
               if (it == edgeIndex.end())
               {
                  idx = edgeList.size();
                  edgeIndex[key] = idx;
                  edgeList.push_back({ key.first, key.second });
               }
               else
                  idx = it->second;

               EdgeInfo& info = edgeList[idx];
               if (info.faceCount < 2)
                  info.faces[info.faceCount++] = f;
            }
         }

         // Of the welded edges, some are triangulation diagonals rather than
         // real edges - after welding there is no data left to tell them
         // apart except that a diagonal's two adjacent triangles are
         // (near-)coplanar, unlike a real edge's. `dissolveAngleDegrees == 0`
         // disables this filter entirely; a boundary edge (one adjacent
         // triangle) is never dissolved. NOTE: this degrades on dense smooth
         // meshes - adjacent triangle normals on a ~200-segment sphere differ
         // by under a degree, so a low threshold there would start dissolving
         // real edges, not just diagonals. Do not raise the default to make a
         // cube look better; that trades a real artifact on dense meshes for
         // a cosmetic win on primitives.
         auto faceNormal = [&](int f) {
            const size_t t = (size_t)f * 3;
            const Vertex& a = in.vertices[in.indices[t]];
            const Vertex& b = in.vertices[in.indices[t + 1]];
            const Vertex& c = in.vertices[in.indices[t + 2]];
            const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
            const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
            float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
            const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-9f) { nx /= len; ny /= len; nz /= len; }
            return std::array<float, 3>{ nx, ny, nz };
         };

         std::vector<std::pair<unsigned int, unsigned int>> edges;
         edges.reserve(edgeList.size());
         for (const EdgeInfo& info : edgeList)
         {
            bool keep = true;
            if (dissolveAngleDegrees > 0.0f && info.faceCount == 2)
            {
               const auto n0 = faceNormal(info.faces[0]);
               const auto n1 = faceNormal(info.faces[1]);
               const float dot = std::max(-1.0f, std::min(1.0f,
                  n0[0]*n1[0] + n0[1]*n1[1] + n0[2]*n1[2]));
               const float angleDeg = std::acos(dot) * (180.0f / 3.14159265f);
               if (angleDeg < dissolveAngleDegrees)
                  keep = false;
            }
            if (keep)
               edges.push_back({ info.a, info.b });
         }

         const int n = (int)edges.size();
         const int stride = std::max(1, n / cap + (n > cap ? 1 : 0));
         for (int i = 0; i < n; i += stride)
         {
            const auto& key = edges[(size_t)i];
            const Vertex& a = in.vertices[key.first];
            const Vertex& b = in.vertices[key.second];
            float nx = (a.nx+b.nx)*0.5f, ny = (a.ny+b.ny)*0.5f, nz = (a.nz+b.nz)*0.5f;
            NormalizeOrFallback(nx, ny, nz);
            points.push_back({ (a.px+b.px)*0.5f, (a.py+b.py)*0.5f, (a.pz+b.pz)*0.5f,
                               nx, ny, nz, 1.0f, i });
            if (in.HasVertexColor())
            {
               MeshPoint& mp = points.back();
               mp.r = (in.vertexColor[key.first * 3 + 0] + in.vertexColor[key.second * 3 + 0]) * 0.5f;
               mp.g = (in.vertexColor[key.first * 3 + 1] + in.vertexColor[key.second * 3 + 1]) * 0.5f;
               mp.b = (in.vertexColor[key.first * 3 + 2] + in.vertexColor[key.second * 3 + 2]) * 0.5f;
            }
         }
      }
      else // face centres
      {
         const int faces = (int)(in.indices.size() / 3);
         std::vector<int> selectedFaces;
         if (!hasSelection)
         {
            selectedFaces.reserve(faces);
            for (int f = 0; f < faces; f++)
               selectedFaces.push_back(f);
         }
         else
         {
            for (int f = 0; f < faces; f++)
               if (in.FaceSelected((size_t)f))
                  selectedFaces.push_back(f);
         }

         const int n = (int)selectedFaces.size();
         const int stride = std::max(1, n / cap + (n > cap ? 1 : 0));
         for (int i = 0; i < n; i += stride)
         {
            const int f = selectedFaces[(size_t)i];
            const size_t t = (size_t)f * 3;
            const Vertex& a = in.vertices[in.indices[t]];
            const Vertex& b = in.vertices[in.indices[t + 1]];
            const Vertex& c = in.vertices[in.indices[t + 2]];
            float nx = (a.nx+b.nx+c.nx)/3.0f, ny = (a.ny+b.ny+c.ny)/3.0f, nz = (a.nz+b.nz+c.nz)/3.0f;
            NormalizeOrFallback(nx, ny, nz);
            points.push_back({ (a.px+b.px+c.px)/3.0f, (a.py+b.py+c.py)/3.0f, (a.pz+b.pz+c.pz)/3.0f,
                               nx, ny, nz, 1.0f, f });
            if (in.HasVertexColor())
            {
               const unsigned int ia = in.indices[t], ib = in.indices[t + 1], ic = in.indices[t + 2];
               MeshPoint& mp = points.back();
               mp.r = (in.vertexColor[ia * 3 + 0] + in.vertexColor[ib * 3 + 0] + in.vertexColor[ic * 3 + 0]) / 3.0f;
               mp.g = (in.vertexColor[ia * 3 + 1] + in.vertexColor[ib * 3 + 1] + in.vertexColor[ic * 3 + 1]) / 3.0f;
               mp.b = (in.vertexColor[ia * 3 + 2] + in.vertexColor[ib * 3 + 2] + in.vertexColor[ic * 3 + 2]) / 3.0f;
            }
         }
      }
      return points;
   }

   Mesh PointsToFaces(const std::vector<MeshPoint>& points, float size)
   {
      Mesh out;
      const float h = std::max(0.001f, size) * 0.5f;
      unsigned int pointIdx = 0;
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
            // Every corner of a point's billboard shares that point's colour -
            // 1,1,1 when the point has none, so this is a true no-op unless a
            // Set Color upstream actually wrote something.
            out.vertexColor.push_back(p.r);
            out.vertexColor.push_back(p.g);
            out.vertexColor.push_back(p.b);
         }
         out.indices.push_back(base); out.indices.push_back(base + 1); out.indices.push_back(base + 2);
         out.indices.push_back(base); out.indices.push_back(base + 2); out.indices.push_back(base + 3);
         out.selectionGroup.push_back(pointIdx);
         out.selectionGroup.push_back(pointIdx);
         pointIdx++;
      }
      return out;
   }

   // Deterministic per-candidate hash, same shape as MeshOps::Select's
   // SelectHash but with a channel argument so one point can draw several
   // independent values (which triangle, then where inside it) without them
   // correlating. Same seed + same index always gives the same draw, which is
   // what lets a reopened patch scatter identically.
   static float DistributeHash(float seed, size_t index, int channel)
   {
      const float x = std::sin((seed + 1.0f) * ((float)(index + 1) * 12.9898f +
                                                  (float)(channel + 1) * 78.233f)) * 43758.5453f;
      return x - std::floor(x);
   }

   std::vector<MeshPoint> DistributeOnFaces(const Mesh& in, float density, float seed,
                                             int method, float minDistance)
   {
      std::vector<MeshPoint> points;
      const int faceCount = (int)(in.indices.size() / 3);
      if (faceCount == 0)
         return points;

      const bool hasSelection = !in.faceMask.empty();
      std::vector<int> faces;
      faces.reserve(faceCount);
      for (int f = 0; f < faceCount; f++)
         if (!hasSelection || in.FaceSelected((size_t)f))
            faces.push_back(f);
      if (faces.empty())
         return points;

      // Prefix sum of triangle areas - picking a triangle by binary search on
      // a uniform draw over [0, totalArea) samples each triangle in
      // proportion to its area, which is what makes coverage uniform in
      // world space regardless of how densely the mesh is tessellated.
      std::vector<double> prefixArea(faces.size());
      double totalArea = 0.0;
      for (size_t i = 0; i < faces.size(); i++)
      {
         const size_t t = (size_t)faces[i] * 3;
         const Vertex& a = in.vertices[in.indices[t]];
         const Vertex& b = in.vertices[in.indices[t + 1]];
         const Vertex& c = in.vertices[in.indices[t + 2]];
         const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
         const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
         const float cx = uy*vz - uz*vy, cy = uz*vx - ux*vz, cz = ux*vy - uy*vx;
         totalArea += 0.5 * std::sqrt((double)cx*cx + (double)cy*cy + (double)cz*cz);
         prefixArea[i] = totalArea;
      }
      if (totalArea <= 1e-12)
         return points;

      const int wanted = (int)std::lround((double)std::max(0.0f, density) * totalArea);
      const int cap = std::min(wanted, 200000);
      if (cap <= 0)
         return points;

      auto pickFace = [&](float r) -> size_t {
         const double target = (double)r * totalArea;
         size_t lo = 0, hi = prefixArea.size() - 1;
         while (lo < hi)
         {
            const size_t mid = lo + (hi - lo) / 2;
            if (prefixArea[mid] < target)
               lo = mid + 1;
            else
               hi = mid;
         }
         return lo;
      };

      // Standard sqrt barycentric warp: uniform (r1, r2) over the unit square
      // maps to a uniform point inside the triangle.
      auto samplePoint = [&](size_t faceIdx, float r1, float r2) -> MeshPoint {
         const int f = faces[faceIdx];
         const size_t t = (size_t)f * 3;
         const unsigned int ia = in.indices[t], ib = in.indices[t + 1], ic = in.indices[t + 2];
         const Vertex& a = in.vertices[ia];
         const Vertex& b = in.vertices[ib];
         const Vertex& c = in.vertices[ic];
         const float sq = std::sqrt(std::max(0.0f, r1));
         const float wa = 1.0f - sq, wb = (1.0f - r2) * sq, wc = r2 * sq;

         MeshPoint p;
         p.px = wa*a.px + wb*b.px + wc*c.px;
         p.py = wa*a.py + wb*b.py + wc*c.py;
         p.pz = wa*a.pz + wb*b.pz + wc*c.pz;
         // Interpolated from the three vertex normals, not the face normal -
         // otherwise a smooth-shaded surface scatters with faceted
         // orientation instead of following the surface it was sampled from.
         float nx = wa*a.nx + wb*b.nx + wc*c.nx;
         float ny = wa*a.ny + wb*b.ny + wc*c.ny;
         float nz = wa*a.nz + wb*b.nz + wc*c.nz;
         NormalizeOrFallback(nx, ny, nz);
         p.nx = nx; p.ny = ny; p.nz = nz;
         p.scale = 1.0f;
         p.index = f;
         if (in.HasVertexColor())
         {
            p.r = wa*in.vertexColor[ia*3+0] + wb*in.vertexColor[ib*3+0] + wc*in.vertexColor[ic*3+0];
            p.g = wa*in.vertexColor[ia*3+1] + wb*in.vertexColor[ib*3+1] + wc*in.vertexColor[ic*3+1];
            p.b = wa*in.vertexColor[ia*3+2] + wb*in.vertexColor[ib*3+2] + wc*in.vertexColor[ic*3+2];
         }
         return p;
      };

      if (method != kDistributePoisson)
      {
         points.reserve((size_t)cap);
         for (int i = 0; i < cap; i++)
         {
            const size_t face = pickFace(DistributeHash(seed, (size_t)i, 0));
            points.push_back(samplePoint(face, DistributeHash(seed, (size_t)i, 1),
                                          DistributeHash(seed, (size_t)i, 2)));
         }
         return points;
      }

      // Poisson disk: generate candidates the same way as random mode, but
      // reject any candidate within minDistance of an already-accepted point.
      // A spatial hash grid (cell size == minDistance) keeps the rejection
      // check O(1) per candidate rather than O(n) against every prior point.
      //
      // The grid is a flat open-addressed table: `gridKeys`/`gridHead` map a
      // cell hash to the most recently accepted point in that cell, and
      // `cellNext` chains earlier points in the same cell by index. This
      // replaces a std::unordered_map<long long, std::vector<int>>, which
      // cost a heap allocation per newly-seen cell and a hash + pointer chase
      // per neighbour lookup - the dominant cost of a saturated scatter.
      // Both arrays are sized off `cap` (the accepted-point budget), not the
      // candidate budget, since only accepted points are ever inserted.
      const float cell = std::max(minDistance, 1e-5f);
      auto cellCoord = [&](float v) { return (long long)std::floor(v / cell); };
      auto cellHash = [](long long cx, long long cy, long long cz) {
         return (cx * 73856093LL) ^ (cy * 19349663LL) ^ (cz * 83492791LL);
      };

      const long long kEmptyGridKey = std::numeric_limits<long long>::min();
      size_t gridCapacity = 64;
      while (gridCapacity < (size_t)cap * 2 + 16)
         gridCapacity <<= 1;
      std::vector<long long> gridKeys(gridCapacity, kEmptyGridKey);
      std::vector<int> gridHead(gridCapacity, -1);
      std::vector<int> cellNext((size_t)cap, -1);

      auto gridSlot = [&](long long key, bool insert) -> size_t {
         size_t h = (size_t)(key * 0x9E3779B97F4A7C15ULL);
         h ^= h >> 32;
         size_t idx = h & (gridCapacity - 1);
         while (gridKeys[idx] != kEmptyGridKey && gridKeys[idx] != key)
            idx = (idx + 1) & (gridCapacity - 1);
         if (insert && gridKeys[idx] == kEmptyGridKey)
            gridKeys[idx] = key;
         return idx;
      };

      const float minDistSq = minDistance * minDistance;
      // A generous candidate budget: Poisson rejection needs far more draws
      // than accepted points once packing tightens. `saturationLimit` is a
      // fixed number of consecutive rejects (spacing has nowhere left to add
      // a point), not scaled by `cap` - scaling it by the requested count is
      // what let a high-density scatter burn tens of millions of wasted
      // candidate draws before this fix. `maxCandidates` never drops below
      // 4000, so saturationLimit stays a hard backstop below it.
      const int maxCandidates = std::min(std::max(cap * 15, 4000), 300000);
      const int saturationLimit = 150;
      int consecutiveRejects = 0;

      for (int i = 0; i < maxCandidates && (int)points.size() < cap; i++)
      {
         const size_t face = pickFace(DistributeHash(seed, (size_t)i, 0));
         const MeshPoint p = samplePoint(face, DistributeHash(seed, (size_t)i, 1),
                                          DistributeHash(seed, (size_t)i, 2));

         const long long cx = cellCoord(p.px), cy = cellCoord(p.py), cz = cellCoord(p.pz);
         bool ok = true;
         for (long long dz = -1; dz <= 1 && ok; dz++)
         for (long long dy = -1; dy <= 1 && ok; dy++)
         for (long long dx = -1; dx <= 1 && ok; dx++)
         {
            const size_t slot = gridSlot(cellHash(cx+dx, cy+dy, cz+dz), false);
            for (int idx = gridHead[slot]; idx != -1 && ok; idx = cellNext[(size_t)idx])
            {
               const MeshPoint& q = points[(size_t)idx];
               const float ddx = q.px-p.px, ddy = q.py-p.py, ddz = q.pz-p.pz;
               if (ddx*ddx + ddy*ddy + ddz*ddz < minDistSq)
                  ok = false;
            }
         }

         if (ok)
         {
            points.push_back(p);
            const int newIdx = (int)points.size() - 1;
            const size_t slot = gridSlot(cellHash(cx, cy, cz), true);
            cellNext[(size_t)newIdx] = gridHead[slot];
            gridHead[slot] = newIdx;
            consecutiveRejects = 0;
         }
         else if (++consecutiveRejects > saturationLimit)
         {
            break;
         }
      }
      return points;
   }

   // Closest point on triangle (abc) to point p, clamping barycentric
   // coordinates to the triangle's interior/edges/corners - Ericson's
   // Real-Time Collision Detection algorithm.
   static void ClosestPointOnTriangle(const float p[3], const float a[3], const float b[3], const float c[3],
                                       float out[3])
   {
      auto sub = [](const float* x, const float* y, float* r) { r[0]=x[0]-y[0]; r[1]=x[1]-y[1]; r[2]=x[2]-y[2]; };
      auto dot = [](const float* x, const float* y) { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; };

      float ab[3], ac[3], ap[3];
      sub(b, a, ab); sub(c, a, ac); sub(p, a, ap);
      const float d1 = dot(ab, ap), d2 = dot(ac, ap);
      if (d1 <= 0.0f && d2 <= 0.0f) { out[0]=a[0]; out[1]=a[1]; out[2]=a[2]; return; }

      float bp[3]; sub(p, b, bp);
      const float d3 = dot(ab, bp), d4 = dot(ac, bp);
      if (d3 >= 0.0f && d4 <= d3) { out[0]=b[0]; out[1]=b[1]; out[2]=b[2]; return; }

      const float vc = d1*d4 - d3*d2;
      if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
      {
         const float v = d1 / (d1 - d3);
         out[0] = a[0] + ab[0]*v; out[1] = a[1] + ab[1]*v; out[2] = a[2] + ab[2]*v;
         return;
      }

      float cp[3]; sub(p, c, cp);
      const float d5 = dot(ab, cp), d6 = dot(ac, cp);
      if (d6 >= 0.0f && d5 <= d6) { out[0]=c[0]; out[1]=c[1]; out[2]=c[2]; return; }

      const float vb = d5*d2 - d1*d6;
      if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
      {
         const float w = d2 / (d2 - d6);
         out[0] = a[0] + ac[0]*w; out[1] = a[1] + ac[1]*w; out[2] = a[2] + ac[2]*w;
         return;
      }

      const float va = d3*d6 - d5*d4;
      if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
      {
         const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
         out[0] = b[0] + (c[0]-b[0])*w; out[1] = b[1] + (c[1]-b[1])*w; out[2] = b[2] + (c[2]-b[2])*w;
         return;
      }

      const float denom = 1.0f / (va + vb + vc);
      const float v = vb * denom, w = vc * denom;
      out[0] = a[0] + ab[0]*v + ac[0]*w;
      out[1] = a[1] + ab[1]*v + ac[1]*w;
      out[2] = a[2] + ab[2]*v + ac[2]*w;
   }

   // (bend, horizontal, depth) component index per axis. One table instead of
   // three copies of the same trigonometry: for axis 1 (the default, text
   // around the equator) that is (Y, X, Z), and the other two are its cyclic
   // rotations.
   static const int kWrapAxes[3][3] = { { 0, 1, 2 }, { 1, 0, 2 }, { 2, 0, 1 } };

   // Target centre and radius from its world bounding box. The radius is the
   // mean of the two half-extents perpendicular to the bend axis, so a sphere
   // gives exactly its radius and a cube its half-width.
   static void WrapBounds(const Mesh& worldTarget, int axis, float centre[3], float& radius)
   {
      centre[0] = centre[1] = centre[2] = 0.0f;
      radius = 0.0f;
      if (worldTarget.Empty())
         return;
      if (axis < 0 || axis > 2) axis = 1;
      const int hor = kWrapAxes[axis][1], dep = kWrapAxes[axis][2];
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      for (const Vertex& v : worldTarget.vertices)
      {
         const float p[3] = { v.px, v.py, v.pz };
         for (int i = 0; i < 3; i++) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
      }
      for (int i = 0; i < 3; i++) centre[i] = (lo[i] + hi[i]) * 0.5f;
      radius = 0.5f * ((hi[hor] - lo[hor]) * 0.5f + (hi[dep] - lo[dep]) * 0.5f);
   }

   float WrapRadius(const Mesh& target, const Mat4& targetModel, int axis)
   {
      if (target.Empty())
         return 0.0f;
      float centre[3], radius;
      WrapBounds(Transform(target, targetModel), axis, centre, radius);
      return radius;
   }

   Mesh Wrap(const Mesh& source, const Mat4& sourceModel, const Mesh& target, const Mat4& targetModel,
             int mode, float offset, float blend, float radiusOverride, float radiusScale, int axis,
             bool fitAround, bool flatShade, bool flipNormals)
   {
      const Mesh worldSource = Transform(source, sourceModel);
      if (worldSource.Empty())
         return worldSource;
      // Nearest-surface has nothing to snap to without a target. The bend
      // modes can still run off `radiusOverride` alone.
      if (target.Empty() && (mode == kWrapNearest || radiusOverride <= 0.0f))
         return worldSource;

      const Mesh worldTarget = Transform(target, targetModel);

      if (mode == kWrapCylindrical || mode == kWrapSpherical)
      {
         if (axis < 0 || axis > 2) axis = 1;
         const int up = kWrapAxes[axis][0], hor = kWrapAxes[axis][1], dep = kWrapAxes[axis][2];

         float centre[3] = { 0.0f, 0.0f, 0.0f };
         float radius = 0.0f;
         WrapBounds(worldTarget, axis, centre, radius);
         if (!worldTarget.Empty())
         {
            // With a target the derived radius is only ever *scaled*, never
            // replaced, so scaling the target always moves the source.
            radius *= std::max(radiusScale, 1e-4f);
         }
         else
         {
            // No target: nothing to derive from, so the override is the radius.
            radius = radiusOverride;
         }

         const float reff = radius + offset;
         if (!(reff > 1e-6f))
            return worldSource;   // nothing sane to bend around

         // 1.0 is what makes the bend arc-length preserving: one radian per
         // `reff` units travelled, so the source's own metric is untouched.
         float arcScale = 1.0f;
         if (fitAround)
         {
            float wLo = 1e30f, wHi = -1e30f;
            for (const Vertex& v : worldSource.vertices)
            {
               const float p[3] = { v.px, v.py, v.pz };
               wLo = std::min(wLo, p[hor]); wHi = std::max(wHi, p[hor]);
            }
            const float width = wHi - wLo;
            if (width > 1e-6f)
               arcScale = (2.0f * 3.14159265358979f * reff) / width;
         }

         const float kHalfPi = 1.5707963267948966f;
         Mesh out = worldSource;
         for (Vertex& v : out.vertices)
         {
            const float orig[3] = { v.px, v.py, v.pz };
            const float q[3] = { orig[0] - centre[0], orig[1] - centre[1], orig[2] - centre[2] };

            const float theta = (q[hor] / reff) * arcScale;
            const float r = reff + q[dep];   // extrusion depth becomes radial thickness

            float bent[3];
            if (mode == kWrapSpherical)
            {
               // Clamped so text longer than the circumference stops at the
               // pole rather than folding back through itself.
               float phi = (q[up] / reff) * arcScale;
               phi = std::max(-kHalfPi, std::min(kHalfPi, phi));
               const float rHoriz = r * std::cos(phi);
               bent[up]  = r * std::sin(phi);
               bent[hor] = rHoriz * std::sin(theta);
               bent[dep] = rHoriz * std::cos(theta);
            }
            else
            {
               bent[up]  = q[up];           // height preserved exactly
               bent[hor] = r * std::sin(theta);
               bent[dep] = r * std::cos(theta);
            }

            float res[3];
            for (int i = 0; i < 3; i++)
            {
               const float t = bent[i] + centre[i];
               res[i] = orig[i] + (t - orig[i]) * blend;
            }
            v.px = res[0]; v.py = res[1]; v.pz = res[2];
         }
         return RecalculateNormals(out, flatShade, flipNormals);
      }

      const size_t triCount = worldTarget.FaceCount();

      Mesh out = worldSource;
      for (Vertex& v : out.vertices)
      {
         const float p[3] = { v.px, v.py, v.pz };
         float bestPoint[3] = { p[0], p[1], p[2] };
         float bestNormal[3] = { v.nx, v.ny, v.nz };
         float bestDist = -1.0f;

         for (size_t f = 0; f < triCount; f++)
         {
            const Vertex& va = worldTarget.vertices[worldTarget.indices[f * 3 + 0]];
            const Vertex& vb = worldTarget.vertices[worldTarget.indices[f * 3 + 1]];
            const Vertex& vc = worldTarget.vertices[worldTarget.indices[f * 3 + 2]];
            const float a[3] = { va.px, va.py, va.pz };
            const float b[3] = { vb.px, vb.py, vb.pz };
            const float c[3] = { vc.px, vc.py, vc.pz };

            float cp[3];
            ClosestPointOnTriangle(p, a, b, c, cp);
            const float dx = cp[0]-p[0], dy = cp[1]-p[1], dz = cp[2]-p[2];
            const float dist = dx*dx + dy*dy + dz*dz;
            if (bestDist < 0.0f || dist < bestDist)
            {
               bestDist = dist;
               bestPoint[0] = cp[0]; bestPoint[1] = cp[1]; bestPoint[2] = cp[2];

               float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
               float e2[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
               float n[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
               const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
               if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; }
               bestNormal[0] = n[0]; bestNormal[1] = n[1]; bestNormal[2] = n[2];
            }
         }

         const float target_[3] = {
            bestPoint[0] + bestNormal[0] * offset,
            bestPoint[1] + bestNormal[1] * offset,
            bestPoint[2] + bestNormal[2] * offset
         };
         v.px = p[0] + (target_[0] - p[0]) * blend;
         v.py = p[1] + (target_[1] - p[1]) * blend;
         v.pz = p[2] + (target_[2] - p[2]) * blend;
      }

      return RecalculateNormals(out, flatShade, flipNormals);
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
               // (a,b,c,d) walks the ring the same way the angle does, which
               // winds the triangles clockwise seen from outside - backface
               // culling then eats the near wall and leaves the far wall's
               // inside showing through, notching the rim. Reverse it so the
               // winding matches the outward normals, as the end caps already
               // do.
               if (facingOut)
                  PushQuad(mesh, a, d, c, b);
               else
                  PushQuad(mesh, a, b, c, d);
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

   namespace
   {
      // Flat-shaded convex polygon face. The winding is decided by testing the
      // computed normal against the face centroid rather than trusting the
      // caller's point order - every solid here is centred on the origin, so
      // "points away from the centre" is exactly the outward test, and no
      // hand-transcribed face table can get it backwards.
      void PushHullFace(Mesh& mesh, std::vector<std::array<float, 3>> loop)
      {
         if (loop.size() < 3)
            return;

         float cx = 0, cy = 0, cz = 0;
         for (const auto& p : loop) { cx += p[0]; cy += p[1]; cz += p[2]; }
         cx /= (float)loop.size(); cy /= (float)loop.size(); cz /= (float)loop.size();

         const float ax = loop[1][0] - loop[0][0], ay = loop[1][1] - loop[0][1], az = loop[1][2] - loop[0][2];
         const float bx = loop[2][0] - loop[0][0], by = loop[2][1] - loop[0][1], bz = loop[2][2] - loop[0][2];
         float nx = ay * bz - az * by;
         float ny = az * bx - ax * bz;
         float nz = ax * by - ay * bx;
         const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
         if (len < 1e-9f)
            return;
         nx /= len; ny /= len; nz /= len;

         if (nx * cx + ny * cy + nz * cz < 0.0f)
         {
            nx = -nx; ny = -ny; nz = -nz;
            std::reverse(loop.begin(), loop.end());
         }

         // Fan from the centroid so pentagons and larger stay convex-safe and
         // every triangle shares the one true face normal.
         const unsigned int centre = (unsigned int)mesh.vertices.size();
         PushVertex(mesh, cx, cy, cz, nx, ny, nz, 0.5f, 0.5f);
         for (size_t i = 0; i < loop.size(); i++)
         {
            const float u = 0.5f + 0.5f * std::cos((float)i / (float)loop.size() * 2.0f * kPi);
            const float v = 0.5f + 0.5f * std::sin((float)i / (float)loop.size() * 2.0f * kPi);
            PushVertex(mesh, loop[i][0], loop[i][1], loop[i][2], nx, ny, nz, u, v);
         }
         for (size_t i = 0; i < loop.size(); i++)
         {
            mesh.indices.push_back(centre);
            mesh.indices.push_back(centre + 1 + (unsigned int)i);
            mesh.indices.push_back(centre + 1 + (unsigned int)((i + 1) % loop.size()));
         }
      }

      // Sorts the points of one planar face into ring order around its normal,
      // so a face can be described as an unordered point set.
      void SortAroundNormal(std::vector<std::array<float, 3>>& pts, const float n[3])
      {
         if (pts.size() < 3)
            return;
         float cx = 0, cy = 0, cz = 0;
         for (const auto& p : pts) { cx += p[0]; cy += p[1]; cz += p[2]; }
         cx /= (float)pts.size(); cy /= (float)pts.size(); cz /= (float)pts.size();

         // Any vector perpendicular to n works as the zero-angle reference.
         float ux = pts[0][0] - cx, uy = pts[0][1] - cy, uz = pts[0][2] - cz;
         const float proj = ux * n[0] + uy * n[1] + uz * n[2];
         ux -= n[0] * proj; uy -= n[1] * proj; uz -= n[2] * proj;
         const float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
         if (ul < 1e-9f)
            return;
         ux /= ul; uy /= ul; uz /= ul;
         const float vx = n[1] * uz - n[2] * uy;
         const float vy = n[2] * ux - n[0] * uz;
         const float vz = n[0] * uy - n[1] * ux;

         std::sort(pts.begin(), pts.end(),
                   [&](const std::array<float, 3>& a, const std::array<float, 3>& b)
         {
            const float aa = std::atan2((a[0] - cx) * vx + (a[1] - cy) * vy + (a[2] - cz) * vz,
                                        (a[0] - cx) * ux + (a[1] - cy) * uy + (a[2] - cz) * uz);
            const float ba = std::atan2((b[0] - cx) * vx + (b[1] - cy) * vy + (b[2] - cz) * vz,
                                        (b[0] - cx) * ux + (b[1] - cy) * uy + (b[2] - cz) * uz);
            return aa < ba;
         });
      }

      // Builds a flat 2D outline with an optional concentric hole and extrudes
      // it. Shared by Gear and Star, which differ only in their radius curve.
      Mesh ExtrudeRadialProfile(const std::vector<float>& radii, float depth, float holeRadius)
      {
         MeshOps::Contour2D outer;
         const size_t n = radii.size();
         for (size_t i = 0; i < n; i++)
         {
            const float a = (float)i / (float)n * 2.0f * kPi;
            outer.points.push_back(std::cos(a) * radii[i]);
            outer.points.push_back(std::sin(a) * radii[i]);
         }

         std::vector<MeshOps::Contour2D> contours;
         contours.push_back(outer);
         if (holeRadius > 1e-4f)
         {
            MeshOps::Contour2D hole;
            const int steps = 48;
            for (int i = 0; i < steps; i++)
            {
               const float a = (float)i / (float)steps * 2.0f * kPi;
               hole.points.push_back(std::cos(a) * holeRadius);
               hole.points.push_back(std::sin(a) * holeRadius);
            }
            contours.push_back(hole);
         }
         return MeshOps::ExtrudeContours(contours, depth, 0.0f);
      }
   }

   Mesh Tetrahedron()
   {
      // The four alternating corners of a cube, scaled so the solid fits the
      // same unit box every other primitive here uses.
      const float k = 0.5f / 1.7320508f;
      const std::array<std::array<float, 3>, 4> v = { {
         { { k, k, k } }, { { k, -k, -k } }, { { -k, k, -k } }, { { -k, -k, k } }
      } };
      Mesh mesh;
      const int faces[4][3] = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 } };
      for (const auto& f : faces)
         PushHullFace(mesh, { v[f[0]], v[f[1]], v[f[2]] });
      return mesh;
   }

   Mesh Octahedron()
   {
      const float r = 0.5f;
      Mesh mesh;
      for (int sx = -1; sx <= 1; sx += 2)
         for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
               PushHullFace(mesh, { { { (float)sx * r, 0, 0 } },
                                    { { 0, (float)sy * r, 0 } },
                                    { { 0, 0, (float)sz * r } } });
      return mesh;
   }

   Mesh Dodecahedron()
   {
      const float phi = 1.61803399f;
      const float inv = 1.0f / phi;
      std::vector<std::array<float, 3>> pts;
      for (int sx = -1; sx <= 1; sx += 2)
         for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
               pts.push_back({ { (float)sx, (float)sy, (float)sz } });
      for (int a = -1; a <= 1; a += 2)
         for (int b = -1; b <= 1; b += 2)
         {
            pts.push_back({ { 0.0f, (float)a * inv, (float)b * phi } });
            pts.push_back({ { (float)a * inv, (float)b * phi, 0.0f } });
            pts.push_back({ { (float)a * phi, 0.0f, (float)b * inv } });
         }

      // Scale so the circumscribed sphere has radius 0.5, matching Sphere/Cube.
      const float scale = 0.5f / std::sqrt(3.0f);
      for (auto& p : pts) { p[0] *= scale; p[1] *= scale; p[2] *= scale; }

      // The twelve face normals are the icosahedron's vertex directions - the
      // dual relationship. Collecting the points that touch each supporting
      // plane beats transcribing a twelve-pentagon face table by hand.
      //
      // Note the phi sits on the *first* named axis of each triple. Swapping it
      // with the 1 gives the other icosahedron orientation, whose directions are
      // face *corners* of this dodecahedron rather than face centres - each
      // supporting plane then touches a single vertex and every face comes back
      // empty.
      std::vector<std::array<float, 3>> normals;
      for (int a = -1; a <= 1; a += 2)
         for (int b = -1; b <= 1; b += 2)
         {
            normals.push_back({ { 0.0f, (float)a * phi, (float)b } });
            normals.push_back({ { (float)a * phi, (float)b, 0.0f } });
            normals.push_back({ { (float)a, 0.0f, (float)b * phi } });
         }

      Mesh mesh;
      for (auto& n : normals)
      {
         const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
         n[0] /= nl; n[1] /= nl; n[2] /= nl;

         float best = -1e9f;
         for (const auto& p : pts)
            best = std::max(best, p[0] * n[0] + p[1] * n[1] + p[2] * n[2]);

         std::vector<std::array<float, 3>> face;
         for (const auto& p : pts)
            if (p[0] * n[0] + p[1] * n[1] + p[2] * n[2] > best - 1e-4f)
               face.push_back(p);

         SortAroundNormal(face, n.data());
         PushHullFace(mesh, face);
      }
      return mesh;
   }

   Mesh RoundedCube(int segments, float radius)
   {
      Mesh mesh = Cube(std::max(1, std::min(segments, 128)));
      const float r = std::max(0.0f, std::min(radius, 0.4999f));
      const float b = 0.5f - r;

      // Every cube vertex is already on the box surface, so clamping it into
      // the inner box and pushing back out by r lands it exactly on the rounded
      // box - and the push direction *is* the surface normal, for free.
      for (Vertex& vert : mesh.vertices)
      {
         const float qx = std::max(-b, std::min(vert.px, b));
         const float qy = std::max(-b, std::min(vert.py, b));
         const float qz = std::max(-b, std::min(vert.pz, b));
         float dx = vert.px - qx, dy = vert.py - qy, dz = vert.pz - qz;
         const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
         if (len < 1e-6f)
            continue;
         dx /= len; dy /= len; dz /= len;
         vert.px = qx + dx * r;
         vert.py = qy + dy * r;
         vert.pz = qz + dz * r;
         vert.nx = dx; vert.ny = dy; vert.nz = dz;
      }
      return mesh;
   }

   Mesh MobiusStrip(int segments, int widthSegments, float width)
   {
      Mesh mesh;
      const int seg = std::max(8, std::min(segments, 2048));
      const int wseg = std::max(1, std::min(widthSegments, 256));
      const float w = std::max(0.01f, width);
      const int stride = wseg + 1;

      for (int i = 0; i <= seg; i++)
      {
         const float u = (float)i / (float)seg * 2.0f * kPi;
         const float half = u * 0.5f;
         for (int j = 0; j <= wseg; j++)
         {
            const float v = ((float)j / (float)wseg - 0.5f) * w;
            const float rad = 0.4f + v * std::cos(half);
            PushVertex(mesh, rad * std::cos(u), v * std::sin(half), rad * std::sin(u),
                       0.0f, 1.0f, 0.0f,
                       (float)i / (float)seg, (float)j / (float)wseg);
         }
      }

      for (int i = 0; i < seg; i++)
         for (int j = 0; j < wseg; j++)
            PushQuad(mesh, (unsigned int)(i * stride + j),
                     (unsigned int)((i + 1) * stride + j),
                     (unsigned int)((i + 1) * stride + j + 1),
                     (unsigned int)(i * stride + j + 1));

      mesh = MeshOps::RecalculateNormals(mesh, false, false);

      // A Mobius strip has one side, so half of it always faces away. Emitting
      // the mirrored winding as well makes it visible from everywhere instead
      // of half-disappearing under backface culling.
      const size_t vertCount = mesh.vertices.size();
      const size_t indexCount = mesh.indices.size();
      for (size_t i = 0; i < vertCount; i++)
      {
         Vertex back = mesh.vertices[i];
         back.nx = -back.nx; back.ny = -back.ny; back.nz = -back.nz;
         mesh.vertices.push_back(back);
      }
      for (size_t i = 0; i + 2 < indexCount; i += 3)
      {
         mesh.indices.push_back(mesh.indices[i] + (unsigned int)vertCount);
         mesh.indices.push_back(mesh.indices[i + 2] + (unsigned int)vertCount);
         mesh.indices.push_back(mesh.indices[i + 1] + (unsigned int)vertCount);
      }
      return mesh;
   }

   Mesh KleinBottle(int uSegments, int vSegments)
   {
      // Figure-8 immersion: closed with no boundary and no self-intersecting
      // neck to tessellate around, unlike the classic bottle shape.
      Mesh mesh;
      const int us = std::max(8, std::min(uSegments, 512));
      const int vs = std::max(8, std::min(vSegments, 512));
      const float r = 0.3f;

      // No duplicated seam rows: both wraps are handled by the index mapping
      // below, which is the only way to close this surface at all.
      for (int i = 0; i < us; i++)
      {
         const float u = (float)i / (float)us * 2.0f * kPi;
         const float half = u * 0.5f;
         for (int j = 0; j < vs; j++)
         {
            const float v = (float)j / (float)vs * 2.0f * kPi;
            const float t = 0.18f * (std::cos(half) * std::sin(v) - std::sin(half) * std::sin(2.0f * v));
            const float y = 0.18f * (std::sin(half) * std::sin(v) + std::cos(half) * std::sin(2.0f * v));
            PushVertex(mesh, (r + t) * std::cos(u), y, (r + t) * std::sin(u),
                       0.0f, 1.0f, 0.0f,
                       (float)i / (float)us, (float)j / (float)vs);
         }
      }

      // Going once around u advances the half-angle by pi, which negates the
      // whole cross-section: the point at (u + 2pi, v) is the point at (u, -v),
      // not (u, v). That reversal *is* the Klein bottle. Wrapping u naively
      // leaves the two ends of the tube misaligned and tears the seam open.
      auto at = [&](int i, int j) -> unsigned int
      {
         if (i >= us)
         {
            i -= us;
            j = (vs - j) % vs;
         }
         return (unsigned int)(i * vs + (j % vs));
      };

      for (int i = 0; i < us; i++)
         for (int j = 0; j < vs; j++)
            PushQuad(mesh, at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
      return MeshOps::RecalculateNormals(mesh, false, false);
   }

   Mesh Gear(int teeth, float depth, float toothDepth, float hubRadius)
   {
      const int n = std::max(3, std::min(teeth, 200));
      const float outer = 0.5f;
      const float root = outer * (1.0f - std::max(0.02f, std::min(toothDepth, 0.6f)));

      // Four samples per tooth - root, rising flank, tip, falling flank - which
      // is the minimum that gives a trapezoidal tooth rather than a sine wave.
      std::vector<float> radii;
      for (int i = 0; i < n; i++)
      {
         radii.push_back(root);
         radii.push_back(outer);
         radii.push_back(outer);
         radii.push_back(root);
      }
      return ExtrudeRadialProfile(radii, std::max(0.01f, depth),
                                  std::max(0.0f, std::min(hubRadius, root * 0.9f)));
   }

   Mesh Star(int points, float innerRatio, float depth)
   {
      const int n = std::max(3, std::min(points, 200));
      const float outer = 0.5f;
      const float inner = outer * std::max(0.05f, std::min(innerRatio, 0.95f));
      std::vector<float> radii;
      for (int i = 0; i < n; i++)
      {
         radii.push_back(outer);
         radii.push_back(inner);
      }
      return ExtrudeRadialProfile(radii, std::max(0.01f, depth), 0.0f);
   }

   Mesh Disc(int sides, float innerRadius)
   {
      const int n = std::max(3, std::min(sides, 4096));
      const float outer = 0.5f;
      const float inner = std::max(0.0f, std::min(innerRadius, 0.49f));
      Mesh mesh;

      // Two-sided: a disc with one face is invisible from below, which is never
      // what anyone wants from a ground plane or a cap.
      for (int side = 0; side < 2; side++)
      {
         const float ny = (side == 0) ? 1.0f : -1.0f;
         const unsigned int base = (unsigned int)mesh.vertices.size();
         for (int i = 0; i <= n; i++)
         {
            const float a = (float)i / (float)n * 2.0f * kPi;
            const float c = std::cos(a), s = std::sin(a);
            PushVertex(mesh, c * inner, 0.0f, s * inner, 0.0f, ny,
                       0.0f, 0.5f + c * inner, 0.5f + s * inner);
            PushVertex(mesh, c * outer, 0.0f, s * outer, 0.0f, ny,
                       0.0f, 0.5f + c * outer, 0.5f + s * outer);
         }
         for (int i = 0; i < n; i++)
         {
            const unsigned int a = base + (unsigned int)i * 2;
            if (side == 0)
               PushQuad(mesh, a, a + 1, a + 3, a + 2);
            else
               PushQuad(mesh, a, a + 2, a + 3, a + 1);
         }
      }
      return mesh;
   }

   Mesh Arrow(int sides, float shaftRadius, float headLength)
   {
      const int n = std::max(3, std::min(sides, 512));
      const float head = std::max(0.05f, std::min(headLength, 0.9f));
      const float shaftLen = 1.0f - head;
      const float sr = std::max(0.01f, std::min(shaftRadius, 0.24f));

      Mesh mesh = Cylinder(n, 1, 1.0f);
      // Cylinder is unit-height about the origin; squash and slide it so the
      // whole arrow still spans -0.5..0.5 with the tip at +Y.
      for (Vertex& v : mesh.vertices)
      {
         v.px *= sr * 2.0f;
         v.pz *= sr * 2.0f;
         v.py = v.py * shaftLen - head * 0.5f;
      }

      Mesh cone = Cone(n, 1);
      for (Vertex& v : cone.vertices)
      {
         v.py = v.py * head + shaftLen * 0.5f;
      }

      const unsigned int offset = (unsigned int)mesh.vertices.size();
      mesh.vertices.insert(mesh.vertices.end(), cone.vertices.begin(), cone.vertices.end());
      for (unsigned int i : cone.indices)
         mesh.indices.push_back(i + offset);
      // Both halves were scaled non-uniformly, which leaves their inherited
      // normals pointing the wrong way - a normal does not transform like a
      // position under anisotropic scale.
      return MeshOps::RecalculateNormals(mesh, true, false);
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

      // Outward is from the inside corners toward the outside ones - the
      // direction the field is falling.
      float insideCentre[3] = { 0, 0, 0 }, outsideCentre[3] = { 0, 0, 0 };
      for (int i = 0; i < insideCount; i++)
      {
         insideCentre[0] += p[inside[i]].x; insideCentre[1] += p[inside[i]].y;
         insideCentre[2] += p[inside[i]].z;
      }
      for (int i = 0; i < outsideCount; i++)
      {
         outsideCentre[0] += p[outside[i]].x; outsideCentre[1] += p[outside[i]].y;
         outsideCentre[2] += p[outside[i]].z;
      }
      const float outward[3] = {
         outsideCentre[0] / (float)outsideCount - insideCentre[0] / (float)insideCount,
         outsideCentre[1] / (float)outsideCount - insideCentre[1] / (float)insideCount,
         outsideCentre[2] / (float)outsideCount - insideCentre[2] / (float)insideCount
      };

      auto pushTri = [&](const float a[3], const float b[3], const float c[3])
      {
         // Winding has to be decided per triangle, not assumed. Which corners
         // land in inside[] versus outside[] depends on the order they were
         // tested in, so a fixed vertex order gives each tetrahedron whichever
         // facing it happens to get - and with backface culling on, half the
         // surface simply vanishes, which is what made the blobs look shattered.
         const float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
         const float e2[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
         const float normal[3] = {
            e1[1]*e2[2] - e1[2]*e2[1],
            e1[2]*e2[0] - e1[0]*e2[2],
            e1[0]*e2[1] - e1[1]*e2[0]
         };
         const bool flip = (normal[0]*outward[0] + normal[1]*outward[1] +
                            normal[2]*outward[2]) < 0.0f;

         const unsigned int base = (unsigned int)mesh.vertices.size();
         // Normals are left flat here and recomputed from the finished surface;
         // per-tetra normals would be faceted along every cell boundary.
         PushVertex(mesh, a[0], a[1], a[2], 0, 1, 0, 0, 0);
         PushVertex(mesh, b[0], b[1], b[2], 0, 1, 0, 1, 0);
         PushVertex(mesh, c[0], c[1], c[2], 0, 1, 0, 0, 1);
         mesh.indices.push_back(base);
         mesh.indices.push_back(base + (flip ? 2 : 1));
         mesh.indices.push_back(base + (flip ? 1 : 2));
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

// =================================================================== curves

namespace MeshOps
{
   void SamplePolyline(const Polyline& line, float t, float outPos[3], float outTangent[3])
   {
      outPos[0] = outPos[1] = outPos[2] = 0.0f;
      outTangent[0] = 0.0f; outTangent[1] = 0.0f; outTangent[2] = 1.0f;
      const size_t count = line.Count();
      if (count == 0)
         return;
      if (count == 1)
      {
         outPos[0] = line.points[0]; outPos[1] = line.points[1]; outPos[2] = line.points[2];
         return;
      }

      // Cumulative arc length, so t is distance along the curve rather than an
      // index. Without this a point sweeping at constant t speeds up wherever
      // the control points are far apart and crawls where they bunch.
      const size_t segments = line.closed ? count : count - 1;
      std::vector<float> lengths(segments + 1, 0.0f);
      for (size_t i = 0; i < segments; i++)
      {
         const size_t a = i * 3;
         const size_t b = ((i + 1) % count) * 3;
         const float dx = line.points[b] - line.points[a];
         const float dy = line.points[b+1] - line.points[a+1];
         const float dz = line.points[b+2] - line.points[a+2];
         lengths[i + 1] = lengths[i] + std::sqrt(dx*dx + dy*dy + dz*dz);
      }
      const float total = lengths[segments];
      if (total < 1e-6f)
      {
         outPos[0] = line.points[0]; outPos[1] = line.points[1]; outPos[2] = line.points[2];
         return;
      }

      float clamped = t;
      if (line.closed)
         clamped = clamped - std::floor(clamped);
      else
         clamped = std::max(0.0f, std::min(clamped, 1.0f));
      const float target = clamped * total;

      size_t seg = 0;
      while (seg + 1 < segments && lengths[seg + 1] < target)
         seg++;
      const float spanLength = std::max(1e-6f, lengths[seg + 1] - lengths[seg]);
      const float local = (target - lengths[seg]) / spanLength;

      const size_t a = seg * 3;
      const size_t b = ((seg + 1) % count) * 3;
      for (int k = 0; k < 3; k++)
         outPos[k] = line.points[a + k] + (line.points[b + k] - line.points[a + k]) * local;

      float dir[3] = { line.points[b] - line.points[a],
                       line.points[b+1] - line.points[a+1],
                       line.points[b+2] - line.points[a+2] };
      const float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
      if (len > 1e-6f)
      {
         outTangent[0] = dir[0] / len;
         outTangent[1] = dir[1] / len;
         outTangent[2] = dir[2] / len;
      }
   }

   Polyline BuildCurve(const std::vector<float>& controlPoints, int kind, int segments,
                       bool closed)
   {
      Polyline out;
      const size_t count = controlPoints.size() / 3;
      if (count < 2)
      {
         out.points = controlPoints;
         out.closed = closed;
         return out;
      }

      const int steps = std::max(1, std::min(segments, 64));
      out.closed = closed;

      auto point = [&](long long i, float p[3])
      {
         long long index = i;
         const long long n = (long long)count;
         if (closed)
            index = ((index % n) + n) % n;
         else
            index = std::max((long long)0, std::min(index, n - 1));
         p[0] = controlPoints[index * 3];
         p[1] = controlPoints[index * 3 + 1];
         p[2] = controlPoints[index * 3 + 2];
      };

      if (kind == kCurveLinear)
      {
         out.points = controlPoints;
         return out;
      }

      const size_t spans = closed ? count : count - 1;
      for (size_t i = 0; i < spans; i++)
      {
         float p0[3], p1[3], p2[3], p3[3];
         if (kind == kCurveBezier)
         {
            // Control points read as anchor, handle, handle, anchor, so a
            // four-point run is one cubic segment and they chain end to end.
            point((long long)i * 3 + 0, p0);
            point((long long)i * 3 + 1, p1);
            point((long long)i * 3 + 2, p2);
            point((long long)i * 3 + 3, p3);
            if ((size_t)(i * 3) >= count)
               break;
         }
         else
         {
            point((long long)i - 1, p0);
            point((long long)i, p1);
            point((long long)i + 1, p2);
            point((long long)i + 2, p3);
         }

         for (int s = 0; s < steps; s++)
         {
            const float u = (float)s / (float)steps;
            float v[3];
            for (int k = 0; k < 3; k++)
            {
               if (kind == kCurveBezier)
               {
                  const float m = 1.0f - u;
                  v[k] = m*m*m*p0[k] + 3*m*m*u*p1[k] + 3*m*u*u*p2[k] + u*u*u*p3[k];
               }
               else if (kind == kCurveBSpline)
               {
                  // Uniform cubic B-spline basis: smoother than Catmull-Rom but
                  // it does not pass through its control points.
                  const float u2 = u * u, u3 = u2 * u;
                  v[k] = ((-u3 + 3*u2 - 3*u + 1) * p0[k] +
                          (3*u3 - 6*u2 + 4) * p1[k] +
                          (-3*u3 + 3*u2 + 3*u + 1) * p2[k] +
                          u3 * p3[k]) / 6.0f;
               }
               else
               {
                  const float u2 = u * u, u3 = u2 * u;
                  v[k] = 0.5f * ((2*p1[k]) +
                                 (-p0[k] + p2[k]) * u +
                                 (2*p0[k] - 5*p1[k] + 4*p2[k] - p3[k]) * u2 +
                                 (-p0[k] + 3*p1[k] - 3*p2[k] + p3[k]) * u3);
               }
            }
            out.points.push_back(v[0]);
            out.points.push_back(v[1]);
            out.points.push_back(v[2]);
         }
      }

      if (!closed && count >= 2)
      {
         out.points.push_back(controlPoints[(count - 1) * 3]);
         out.points.push_back(controlPoints[(count - 1) * 3 + 1]);
         out.points.push_back(controlPoints[(count - 1) * 3 + 2]);
      }
      return out;
   }

   Mesh TubeAlong(const Polyline& line, float radius, int sides, float taper)
   {
      Mesh mesh;
      const size_t count = line.Count();
      if (count < 2 || radius <= 0.0f)
         return mesh;

      const int n = std::max(3, std::min(sides, 64));
      const size_t rings = line.closed ? count + 1 : count;

      // A parallel-transport frame rather than a fresh one per ring: computing
      // each frame independently makes the tube spin about its own axis wherever
      // the curve's curvature flips, which reads as a twist that is not there.
      float normal[3] = { 0.0f, 1.0f, 0.0f };

      for (size_t i = 0; i < rings; i++)
      {
         const size_t index = i % count;
         const size_t nextIndex = (index + 1) % count;
         const size_t prevIndex = (index + count - 1) % count;

         float tangent[3];
         for (int k = 0; k < 3; k++)
            tangent[k] = line.points[nextIndex * 3 + k] - line.points[prevIndex * 3 + k];
         float tl = std::sqrt(tangent[0]*tangent[0] + tangent[1]*tangent[1] + tangent[2]*tangent[2]);
         if (tl < 1e-6f)
         {
            for (int k = 0; k < 3; k++)
               tangent[k] = line.points[nextIndex * 3 + k] - line.points[index * 3 + k];
            tl = std::sqrt(tangent[0]*tangent[0] + tangent[1]*tangent[1] + tangent[2]*tangent[2]);
         }
         if (tl > 1e-6f) { tangent[0] /= tl; tangent[1] /= tl; tangent[2] /= tl; }
         else { tangent[0] = 0; tangent[1] = 0; tangent[2] = 1; }

         // Re-orthogonalise the carried normal against the new tangent.
         const float dot = normal[0]*tangent[0] + normal[1]*tangent[1] + normal[2]*tangent[2];
         float n0[3] = { normal[0] - tangent[0]*dot,
                         normal[1] - tangent[1]*dot,
                         normal[2] - tangent[2]*dot };
         float nl = std::sqrt(n0[0]*n0[0] + n0[1]*n0[1] + n0[2]*n0[2]);
         if (nl < 1e-5f)
         {
            n0[0] = std::fabs(tangent[1]) > 0.9f ? 1.0f : 0.0f;
            n0[1] = std::fabs(tangent[1]) > 0.9f ? 0.0f : 1.0f;
            n0[2] = 0.0f;
            const float d2 = n0[0]*tangent[0] + n0[1]*tangent[1] + n0[2]*tangent[2];
            for (int k = 0; k < 3; k++)
               n0[k] -= tangent[k] * d2;
            nl = std::sqrt(n0[0]*n0[0] + n0[1]*n0[1] + n0[2]*n0[2]);
         }
         if (nl > 1e-6f) { n0[0] /= nl; n0[1] /= nl; n0[2] /= nl; }
         normal[0] = n0[0]; normal[1] = n0[1]; normal[2] = n0[2];

         const float binormal[3] = {
            tangent[1]*normal[2] - tangent[2]*normal[1],
            tangent[2]*normal[0] - tangent[0]*normal[2],
            tangent[0]*normal[1] - tangent[1]*normal[0]
         };

         const float along = (rings > 1) ? (float)i / (float)(rings - 1) : 0.0f;
         const float r = radius * (1.0f - taper * along);

         for (int j = 0; j <= n; j++)
         {
            const float u = (float)j / (float)n;
            const float theta = u * 2.0f * kPi;
            const float c = std::cos(theta), s = std::sin(theta);
            const float ox = normal[0]*c + binormal[0]*s;
            const float oy = normal[1]*c + binormal[1]*s;
            const float oz = normal[2]*c + binormal[2]*s;
            PushVertex(mesh,
                       line.points[index*3] + ox * r,
                       line.points[index*3+1] + oy * r,
                       line.points[index*3+2] + oz * r,
                       ox, oy, oz, u, along);
         }
      }

      const int stride = n + 1;
      for (size_t i = 0; i + 1 < rings; i++)
         for (int j = 0; j < n; j++)
            PushQuad(mesh, (unsigned int)(i * stride + j),
                     (unsigned int)((i + 1) * stride + j),
                     (unsigned int)((i + 1) * stride + j + 1),
                     (unsigned int)(i * stride + j + 1));
      return mesh;
   }
}

namespace MeshOps
{
namespace
{
   // Chains loose edges into ordered runs. Both boundary extraction and plane
   // slicing produce an unordered soup of segments, and a follower needs them
   // in travel order.
   std::vector<Polyline> ChainSegments(const std::vector<std::array<float, 6>>& segments)
   {
      std::vector<Polyline> loops;
      if (segments.empty())
         return loops;

      // Endpoints are quantised before matching: a slice computes the same
      // shared vertex twice from two different triangles, and the results differ
      // in the last float bit.
      const double kQuantum = 1e4;
      auto key = [&](const float* p) {
         return std::array<long long, 3>{
            (long long)std::llround((double)p[0] * kQuantum),
            (long long)std::llround((double)p[1] * kQuantum),
            (long long)std::llround((double)p[2] * kQuantum) };
      };

      std::map<std::array<long long, 3>, std::vector<size_t>> byEndpoint;
      for (size_t i = 0; i < segments.size(); i++)
      {
         byEndpoint[key(&segments[i][0])].push_back(i);
         byEndpoint[key(&segments[i][3])].push_back(i);
      }

      std::vector<bool> used(segments.size(), false);
      for (size_t start = 0; start < segments.size(); start++)
      {
         if (used[start])
            continue;
         used[start] = true;

         Polyline line;
         line.points.insert(line.points.end(), segments[start].begin(), segments[start].begin() + 3);
         line.points.insert(line.points.end(), segments[start].begin() + 3, segments[start].end());

         // Walk forward from the tail until nothing connects.
         bool extended = true;
         size_t guard = segments.size() + 1;
         while (extended && guard-- > 0)
         {
            extended = false;
            const float* tail = &line.points[line.points.size() - 3];
            auto it = byEndpoint.find(key(tail));
            if (it == byEndpoint.end())
               break;
            for (size_t candidate : it->second)
            {
               if (used[candidate])
                  continue;
               const float* a = &segments[candidate][0];
               const float* b = &segments[candidate][3];
               const bool forward = key(a) == key(tail);
               const float* next = forward ? b : a;
               line.points.push_back(next[0]);
               line.points.push_back(next[1]);
               line.points.push_back(next[2]);
               used[candidate] = true;
               extended = true;
               break;
            }
         }

         // A run whose ends meet is a loop; drop the duplicated final point.
         if (line.Count() > 2 && key(&line.points[0]) == key(&line.points[line.points.size() - 3]))
         {
            line.points.resize(line.points.size() - 3);
            line.closed = true;
         }
         if (!line.Empty())
            loops.push_back(std::move(line));
      }

      // Longest first: a follower almost always wants the main outline, not a
      // stray two-segment fragment.
      std::sort(loops.begin(), loops.end(), [](const Polyline& a, const Polyline& b) {
         return a.Count() > b.Count();
      });
      return loops;
   }
}

std::vector<Polyline> BoundaryLoops(const Mesh& in)
{
   const std::vector<unsigned int> weld = BuildWeldMap(in);
   std::map<std::pair<unsigned int, unsigned int>, int> edgeUse;
   for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
   {
      const unsigned int v[3] = { weld[in.indices[t]], weld[in.indices[t+1]], weld[in.indices[t+2]] };
      for (int e = 0; e < 3; e++)
      {
         const auto k = std::minmax(v[e], v[(e + 1) % 3]);
         if (k.first != k.second)
            edgeUse[{ k.first, k.second }]++;
      }
   }

   std::vector<std::array<float, 6>> segments;
   for (const auto& entry : edgeUse)
   {
      if (entry.second != 1)
         continue;
      const Vertex& a = in.vertices[entry.first.first];
      const Vertex& b = in.vertices[entry.first.second];
      segments.push_back({ a.px, a.py, a.pz, b.px, b.py, b.pz });
   }
   return ChainSegments(segments);
}

std::vector<Polyline> SliceContours(const Mesh& in, int axis, float position)
{
   const int k = std::max(0, std::min(axis, 2));
   auto component = [k](const Vertex& v) {
      return (k == 0) ? v.px : (k == 1) ? v.py : v.pz;
   };

   std::vector<std::array<float, 6>> segments;
   for (size_t t = 0; t + 2 < in.indices.size(); t += 3)
   {
      const Vertex& a = in.vertices[in.indices[t]];
      const Vertex& b = in.vertices[in.indices[t+1]];
      const Vertex& c = in.vertices[in.indices[t+2]];
      const Vertex* tri[3] = { &a, &b, &c };

      // A triangle crossing the plane does so along exactly two of its edges.
      float crossings[2][3];
      int found = 0;
      for (int e = 0; e < 3 && found < 2; e++)
      {
         const Vertex& p = *tri[e];
         const Vertex& q = *tri[(e + 1) % 3];
         const float dp = component(p) - position;
         const float dq = component(q) - position;
         if ((dp > 0.0f) == (dq > 0.0f))
            continue;
         const float denom = dq - dp;
         const float u = (std::fabs(denom) < 1e-9f) ? 0.5f : (-dp / denom);
         crossings[found][0] = p.px + (q.px - p.px) * u;
         crossings[found][1] = p.py + (q.py - p.py) * u;
         crossings[found][2] = p.pz + (q.pz - p.pz) * u;
         found++;
      }
      if (found == 2)
         segments.push_back({ crossings[0][0], crossings[0][1], crossings[0][2],
                              crossings[1][0], crossings[1][1], crossings[1][2] });
   }
   return ChainSegments(segments);
}
}

// ====================================================== constructive solid geometry

namespace MeshOps
{
namespace csg
{
   // Classic BSP-tree CSG, in the shape Evan Wallace's csg.js popularised: a
   // polygon soup is partitioned by planes, then each tree is used to clip the
   // other's polygons, and what survives is reassembled.
   const float kEpsilon = 1e-5f;

   struct Vec3 { float x = 0, y = 0, z = 0; };

   inline Vec3 Sub(const Vec3& a, const Vec3& b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
   inline Vec3 Cross(const Vec3& a, const Vec3& b)
   {
      return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
   }
   inline float Dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
   inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t)
   {
      return { a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t };
   }

   struct CsgVertex
   {
      Vec3 pos;
      Vec3 normal;
      float u = 0, v = 0;
   };

   CsgVertex LerpVertex(const CsgVertex& a, const CsgVertex& b, float t)
   {
      CsgVertex out;
      out.pos = Lerp(a.pos, b.pos, t);
      out.normal = Lerp(a.normal, b.normal, t);
      out.u = a.u + (b.u - a.u) * t;
      out.v = a.v + (b.v - a.v) * t;
      return out;
   }

   struct Plane
   {
      Vec3 normal;
      float w = 0.0f;
      bool valid = false;
   };

   struct Polygon
   {
      std::vector<CsgVertex> vertices;
      Plane plane;
   };

   Plane PlaneFrom(const CsgVertex& a, const CsgVertex& b, const CsgVertex& c)
   {
      Plane p;
      Vec3 n = Cross(Sub(b.pos, a.pos), Sub(c.pos, a.pos));
      const float len = std::sqrt(Dot(n, n));
      if (len < 1e-12f)
         return p; // degenerate triangle: no plane, and no contribution
      n.x /= len; n.y /= len; n.z /= len;
      p.normal = n;
      p.w = Dot(n, a.pos);
      p.valid = true;
      return p;
   }

   // Splits a polygon against a plane, appending each piece to whichever list
   // it belongs in. Coplanar polygons are sorted by facing, which is what makes
   // union and intersection differ on shared surfaces.
   void SplitPolygon(const Plane& plane, const Polygon& poly,
                     std::vector<Polygon>& coplanarFront, std::vector<Polygon>& coplanarBack,
                     std::vector<Polygon>& front, std::vector<Polygon>& back)
   {
      enum { kCoplanar = 0, kFront = 1, kBack = 2, kSpanning = 3 };

      int polygonType = 0;
      std::vector<int> types(poly.vertices.size());
      for (size_t i = 0; i < poly.vertices.size(); i++)
      {
         const float t = Dot(plane.normal, poly.vertices[i].pos) - plane.w;
         const int type = (t < -kEpsilon) ? kBack : ((t > kEpsilon) ? kFront : kCoplanar);
         polygonType |= type;
         types[i] = type;
      }

      switch (polygonType)
      {
         case kCoplanar:
            (Dot(plane.normal, poly.plane.normal) > 0 ? coplanarFront : coplanarBack)
               .push_back(poly);
            break;
         case kFront:
            front.push_back(poly);
            break;
         case kBack:
            back.push_back(poly);
            break;
         default:
         {
            Polygon f, b;
            f.plane = poly.plane;
            b.plane = poly.plane;
            for (size_t i = 0; i < poly.vertices.size(); i++)
            {
               const size_t j = (i + 1) % poly.vertices.size();
               const int ti = types[i], tj = types[j];
               const CsgVertex& vi = poly.vertices[i];
               const CsgVertex& vj = poly.vertices[j];
               if (ti != kBack) f.vertices.push_back(vi);
               if (ti != kFront) b.vertices.push_back(vi);
               if ((ti | tj) == kSpanning)
               {
                  const float denom = Dot(plane.normal, Sub(vj.pos, vi.pos));
                  const float t = (std::fabs(denom) < 1e-12f) ? 0.5f
                     : (plane.w - Dot(plane.normal, vi.pos)) / denom;
                  const CsgVertex mid = LerpVertex(vi, vj, t);
                  f.vertices.push_back(mid);
                  b.vertices.push_back(mid);
               }
            }
            if (f.vertices.size() >= 3) front.push_back(f);
            if (b.vertices.size() >= 3) back.push_back(b);
            break;
         }
      }
   }

   struct Node
   {
      Plane plane;
      std::unique_ptr<Node> front;
      std::unique_ptr<Node> back;
      std::vector<Polygon> polygons;
   };

   void Build(Node& node, const std::vector<Polygon>& polygons, int depth = 0)
   {
      if (polygons.empty())
         return;
      // Depth cap: a pathological input can otherwise recurse until the stack
      // gives out, and a slightly wrong result beats a crash.
      if (depth > 256)
      {
         node.polygons.insert(node.polygons.end(), polygons.begin(), polygons.end());
         return;
      }

      size_t start = 0;
      if (!node.plane.valid)
      {
         while (start < polygons.size() && !polygons[start].plane.valid)
            start++;
         if (start >= polygons.size())
            return;
         node.plane = polygons[start].plane;
      }

      std::vector<Polygon> frontList, backList;
      for (size_t i = 0; i < polygons.size(); i++)
      {
         if (!polygons[i].plane.valid)
            continue;
         SplitPolygon(node.plane, polygons[i], node.polygons, node.polygons,
                      frontList, backList);
      }

      if (!frontList.empty())
      {
         if (!node.front) node.front.reset(new Node());
         Build(*node.front, frontList, depth + 1);
      }
      if (!backList.empty())
      {
         if (!node.back) node.back.reset(new Node());
         Build(*node.back, backList, depth + 1);
      }
   }

   std::vector<Polygon> ClipPolygons(const Node& node, const std::vector<Polygon>& polygons)
   {
      if (!node.plane.valid)
         return polygons;

      std::vector<Polygon> frontList, backList;
      for (const Polygon& p : polygons)
         SplitPolygon(node.plane, p, frontList, backList, frontList, backList);

      if (node.front)
         frontList = ClipPolygons(*node.front, frontList);
      // Nothing behind the plane means the space back there is solid, so what
      // is behind it is inside the solid and gets dropped.
      if (node.back)
         backList = ClipPolygons(*node.back, backList);
      else
         backList.clear();

      frontList.insert(frontList.end(), backList.begin(), backList.end());
      return frontList;
   }

   void ClipTo(Node& node, const Node& other)
   {
      node.polygons = ClipPolygons(other, node.polygons);
      if (node.front) ClipTo(*node.front, other);
      if (node.back) ClipTo(*node.back, other);
   }

   void Invert(Node& node)
   {
      for (Polygon& p : node.polygons)
      {
         std::reverse(p.vertices.begin(), p.vertices.end());
         for (CsgVertex& v : p.vertices)
         {
            v.normal.x = -v.normal.x; v.normal.y = -v.normal.y; v.normal.z = -v.normal.z;
         }
         p.plane.normal.x = -p.plane.normal.x;
         p.plane.normal.y = -p.plane.normal.y;
         p.plane.normal.z = -p.plane.normal.z;
         p.plane.w = -p.plane.w;
      }
      // The node's own splitting plane has to flip too, not just the planes of
      // the polygons stored on it. Without this the tree still partitions space
      // the old way while claiming the opposite sense of solid, so every later
      // clip against it decides inside-versus-out backwards. Union and
      // difference survived it; intersection came back empty.
      if (node.plane.valid)
      {
         node.plane.normal.x = -node.plane.normal.x;
         node.plane.normal.y = -node.plane.normal.y;
         node.plane.normal.z = -node.plane.normal.z;
         node.plane.w = -node.plane.w;
      }
      node.front.swap(node.back);
      if (node.front) Invert(*node.front);
      if (node.back) Invert(*node.back);
   }

   void AllPolygons(const Node& node, std::vector<Polygon>& out)
   {
      out.insert(out.end(), node.polygons.begin(), node.polygons.end());
      if (node.front) AllPolygons(*node.front, out);
      if (node.back) AllPolygons(*node.back, out);
   }

   std::vector<Polygon> FromMesh(const Mesh& mesh)
   {
      std::vector<Polygon> polygons;
      polygons.reserve(mesh.indices.size() / 3);
      for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
      {
         Polygon p;
         p.vertices.resize(3);
         for (int k = 0; k < 3; k++)
         {
            const Vertex& v = mesh.vertices[mesh.indices[t + k]];
            p.vertices[k].pos = { v.px, v.py, v.pz };
            p.vertices[k].normal = { v.nx, v.ny, v.nz };
            p.vertices[k].u = v.u;
            p.vertices[k].v = v.v;
         }
         p.plane = PlaneFrom(p.vertices[0], p.vertices[1], p.vertices[2]);
         if (p.plane.valid)
            polygons.push_back(std::move(p));
      }
      return polygons;
   }

   Mesh ToMesh(const std::vector<Polygon>& polygons)
   {
      Mesh mesh;
      for (const Polygon& p : polygons)
      {
         if (p.vertices.size() < 3)
            continue;
         // Fan-triangulated: every polygon here came from a triangle split by
         // planes, so it stays convex and a fan is safe.
         const unsigned int base = (unsigned int)mesh.vertices.size();
         for (const CsgVertex& v : p.vertices)
            PushVertex(mesh, v.pos.x, v.pos.y, v.pos.z,
                       v.normal.x, v.normal.y, v.normal.z, v.u, v.v);
         for (size_t i = 1; i + 1 < p.vertices.size(); i++)
         {
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + (unsigned int)i);
            mesh.indices.push_back(base + (unsigned int)i + 1);
         }
      }
      return mesh;
   }
}

Mesh Boolean(const Mesh& a, const Mesh& b, int op)
{
   if (a.Empty())
      return (op == kBooleanIntersect || op == kBooleanDifference) ? Mesh() : b;
   if (b.Empty())
      return (op == kBooleanIntersect) ? Mesh() : a;

   csg::Node treeA, treeB;
   csg::Build(treeA, csg::FromMesh(a));
   csg::Build(treeB, csg::FromMesh(b));

   // The three operations are the same clipping sequence with inversions in
   // different places - inverting a tree swaps its notion of inside and out,
   // so intersection is a union of complements and difference is a union with
   // one side complemented.
   //
   // Each ends by building what survives of B into A's tree and reading A. That
   // final step is not optional: for intersection the result is inverted
   // afterwards, and inverting A while B's polygons sit outside it leaves the
   // two halves disagreeing about which way is solid. Collecting from both
   // trees instead happens to give the right answer for union and difference,
   // and gives an empty mesh for intersection.
   switch (op)
   {
      case kBooleanIntersect:
      {
         csg::Invert(treeA);
         csg::ClipTo(treeB, treeA);
         csg::Invert(treeB);
         csg::ClipTo(treeA, treeB);
         csg::ClipTo(treeB, treeA);
         std::vector<csg::Polygon> fromB;
         csg::AllPolygons(treeB, fromB);
         csg::Build(treeA, fromB);
         csg::Invert(treeA);
         break;
      }
      case kBooleanDifference:
      {
         csg::Invert(treeA);
         csg::ClipTo(treeA, treeB);
         csg::ClipTo(treeB, treeA);
         csg::Invert(treeB);
         csg::ClipTo(treeB, treeA);
         csg::Invert(treeB);
         std::vector<csg::Polygon> fromB;
         csg::AllPolygons(treeB, fromB);
         csg::Build(treeA, fromB);
         csg::Invert(treeA);
         break;
      }
      case kBooleanUnion:
      default:
      {
         csg::ClipTo(treeA, treeB);
         csg::ClipTo(treeB, treeA);
         csg::Invert(treeB);
         csg::ClipTo(treeB, treeA);
         csg::Invert(treeB);
         std::vector<csg::Polygon> fromB;
         csg::AllPolygons(treeB, fromB);
         csg::Build(treeA, fromB);
         break;
      }
   }

   std::vector<csg::Polygon> merged;
   csg::AllPolygons(treeA, merged);
   return csg::ToMesh(merged);
}
}

// =================================================================== selection

namespace MeshOps
{
namespace
{
   void FaceCentre(const Mesh& m, size_t face, float out[3])
   {
      const Vertex& a = m.vertices[m.indices[face * 3]];
      const Vertex& b = m.vertices[m.indices[face * 3 + 1]];
      const Vertex& c = m.vertices[m.indices[face * 3 + 2]];
      out[0] = (a.px + b.px + c.px) / 3.0f;
      out[1] = (a.py + b.py + c.py) / 3.0f;
      out[2] = (a.pz + b.pz + c.pz) / 3.0f;
   }

   void FaceNormal(const Mesh& m, size_t face, float out[3])
   {
      const Vertex& a = m.vertices[m.indices[face * 3]];
      const Vertex& b = m.vertices[m.indices[face * 3 + 1]];
      const Vertex& c = m.vertices[m.indices[face * 3 + 2]];
      const float e1[3] = { b.px-a.px, b.py-a.py, b.pz-a.pz };
      const float e2[3] = { c.px-a.px, c.py-a.py, c.pz-a.pz };
      out[0] = e1[1]*e2[2] - e1[2]*e2[1];
      out[1] = e1[2]*e2[0] - e1[0]*e2[2];
      out[2] = e1[0]*e2[1] - e1[1]*e2[0];
      const float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
      if (len > 1e-9f) { out[0] /= len; out[1] /= len; out[2] /= len; }
   }

   float SelectHash(float seed, size_t index)
   {
      const float x = std::sin((seed + 1.0f) * (float)(index + 1) * 12.9898f) * 43758.5453f;
      return x - std::floor(x);
   }
}

Mesh Select(const Mesh& in, int mode, float a, float b, float c, int axis,
            float seed, bool invert, bool append)
{
   Mesh out = in;
   const size_t faces = out.FaceCount();
   if (faces == 0)
      return out;

   const std::vector<unsigned char> previous = in.faceMask;
   out.faceMask.assign(faces, 0);
   const int k = std::max(0, std::min(axis, 2));

   // Faces carrying the same selectionGroup tag (e.g. the two triangles of a
   // Mesh to Points billboard quad) are one atomic selection unit: the test
   // below runs once on the group's first face and the result is broadcast to
   // every face sharing that tag, so a quad is never torn into one selected
   // and one unselected triangle.
   const bool grouped = out.selectionGroup.size() == faces;
   std::unordered_map<unsigned int, bool> groupHit;

   for (size_t f = 0; f < faces; f++)
   {
      const unsigned int groupId = grouped ? out.selectionGroup[f] : (unsigned int)f;
      bool hit;
      auto cached = grouped ? groupHit.find(groupId) : groupHit.end();
      if (grouped && cached != groupHit.end())
      {
         hit = cached->second;
      }
      else
      {
         switch (mode)
         {
            case kSelectIndex:
            {
               // Start, count and stride, so "every third face from face 10" is
               // expressible without a second node.
               const long long start = (long long)a;
               const long long count = (long long)b;
               const long long stride = std::max(1LL, (long long)c);
               const long long rel = (long long)f - start;
               hit = rel >= 0 && (count <= 0 || rel < count * stride) && (rel % stride) == 0;
               break;
            }
            case kSelectAxis:
            {
               float centre[3];
               FaceCentre(out, f, centre);
               hit = centre[k] >= a && centre[k] <= b;
               break;
            }
            case kSelectNormal:
            {
               // Faces whose normal points within a threshold of an axis - which
               // is how "the top of the cube" gets said.
               float normal[3];
               FaceNormal(out, f, normal);
               const float sign = (c >= 0.0f) ? 1.0f : -1.0f;
               hit = (normal[k] * sign) >= a;
               break;
            }
            case kSelectRandom:
               hit = SelectHash(seed, groupId) < a;
               break;
            case kSelectRadius:
            {
               float centre[3];
               FaceCentre(out, f, centre);
               const float dx = centre[0] - a, dy = centre[1] - b, dz = centre[2] - c;
               hit = std::sqrt(dx*dx + dy*dy + dz*dz) <= seed;
               break;
            }
            case kSelectAll:
            default:
               hit = true;
               break;
         }
         if (grouped)
            groupHit.emplace(groupId, hit);
      }

      if (invert)
         hit = !hit;
      if (append && !previous.empty() && f < previous.size() && previous[f])
         hit = true;
      out.faceMask[f] = hit ? 1 : 0;
   }
   return out;
}

SelectionSplit SplitBySelection(const Mesh& in)
{
   SelectionSplit r;
   const size_t faces = in.FaceCount();
   std::vector<int> selRemap(in.vertices.size(), -1), unselRemap(in.vertices.size(), -1);
   std::vector<unsigned int> selMapping, unselMapping;
   for (size_t f = 0; f < faces; f++)
   {
      const bool sel = in.FaceSelected(f);
      Mesh& out = sel ? r.selected : r.unselected;
      std::vector<int>& remap = sel ? selRemap : unselRemap;
      std::vector<unsigned int>& mapping = sel ? selMapping : unselMapping;
      for (int k = 0; k < 3; k++)
      {
         const unsigned int index = in.indices[f * 3 + k];
         if (remap[index] < 0)
         {
            remap[index] = (int)out.vertices.size();
            out.vertices.push_back(in.vertices[index]);
            mapping.push_back(index);
         }
         out.indices.push_back((unsigned int)remap[index]);
      }
   }
   r.selected.vertexColor = RemapVertexColor(in.vertexColor, selMapping, r.selected.vertices.size());
   r.unselected.vertexColor = RemapVertexColor(in.vertexColor, unselMapping, r.unselected.vertices.size());
   return r;
}

void AppendMesh(Mesh& a, const Mesh& b)
{
   if (b.vertices.empty())
      return;
   const unsigned int base = (unsigned int)a.vertices.size();
   const bool hadColorA = a.HasVertexColor();
   const bool hadColorB = b.HasVertexColor();
   a.vertices.insert(a.vertices.end(), b.vertices.begin(), b.vertices.end());
   for (unsigned int idx : b.indices)
      a.indices.push_back(base + idx);
   if (hadColorA || hadColorB)
   {
      if (!hadColorA)
         a.vertexColor.assign((size_t)base * 3, 1.0f);
      if (hadColorB)
         a.vertexColor.insert(a.vertexColor.end(), b.vertexColor.begin(), b.vertexColor.end());
      else
         a.vertexColor.insert(a.vertexColor.end(), b.vertices.size() * 3, 1.0f);
   }
}

Mesh ClearSelection(const Mesh& in)
{
   Mesh out = in;
   out.faceMask.clear();
   return out;
}

std::vector<unsigned char> VertexSelectionFromFaces(const Mesh& in)
{
   std::vector<unsigned char> touched(in.vertices.size(), 0);
   const size_t faces = in.FaceCount();
   for (size_t f = 0; f < faces; f++)
   {
      if (!in.FaceSelected(f))
         continue;
      touched[in.indices[f * 3 + 0]] = 1;
      touched[in.indices[f * 3 + 1]] = 1;
      touched[in.indices[f * 3 + 2]] = 1;
   }
   return touched;
}

Mesh DeleteSelected(const Mesh& in, bool keepSelected)
{
   Mesh out;
   const size_t faces = in.FaceCount();
   if (faces == 0)
      return out;

   // Vertices are remapped rather than copied wholesale, so deleting most of a
   // mesh actually frees the vertices too rather than leaving them orphaned.
   std::vector<int> remap(in.vertices.size(), -1);
   // Inverse of `remap`, built alongside it, so vertexColor can be remapped
   // the same way the vertices themselves were.
   std::vector<unsigned int> mapping;
   for (size_t f = 0; f < faces; f++)
   {
      // Named for what is kept, not what is dropped. The flag previously read
      // as "keep unselected" while the comparison did the opposite, so the
      // default deleted everything except the selection.
      const bool selected = in.FaceSelected(f);
      if (selected != keepSelected)
         continue;
      for (int k = 0; k < 3; k++)
      {
         const unsigned int index = in.indices[f * 3 + k];
         if (remap[index] < 0)
         {
            remap[index] = (int)out.vertices.size();
            out.vertices.push_back(in.vertices[index]);
            mapping.push_back(index);
         }
         out.indices.push_back((unsigned int)remap[index]);
      }
   }
   out.vertexColor = RemapVertexColor(in.vertexColor, mapping, out.vertices.size());
   return out;
}

Mesh TransformSelected(const Mesh& in, const Mat4& m, bool alongNormals, float normalAmount)
{
   Mesh out = in;
   const size_t faces = in.FaceCount();
   if (faces == 0)
      return out;

   // Only vertices belonging to a selected face move. A vertex shared with an
   // unselected face still moves, which stretches the neighbour rather than
   // tearing the mesh - splitting instead would leave a visible crack.
   std::vector<unsigned char> moved(out.vertices.size(), 0);
   for (size_t f = 0; f < faces; f++)
   {
      if (!in.FaceSelected(f))
         continue;
      float normal[3];
      FaceNormal(in, f, normal);
      for (int k = 0; k < 3; k++)
      {
         const unsigned int index = in.indices[f * 3 + k];
         if (moved[index])
            continue;
         moved[index] = 1;
         Vertex& v = out.vertices[index];
         if (alongNormals)
         {
            v.px += normal[0] * normalAmount;
            v.py += normal[1] * normalAmount;
            v.pz += normal[2] * normalAmount;
         }
         const float x = v.px, y = v.py, z = v.pz;
         v.px = m.m[0]*x + m.m[4]*y + m.m[8]*z + m.m[12];
         v.py = m.m[1]*x + m.m[5]*y + m.m[9]*z + m.m[13];
         v.pz = m.m[2]*x + m.m[6]*y + m.m[10]*z + m.m[14];
      }
   }
   return RecalculateNormals(out, false, false);
}

Mesh ExtrudeSelected(const Mesh& in, float distance, float inset)
{
   Mesh out;
   const size_t faces = in.FaceCount();
   if (faces == 0)
      return out;

   // Unselected faces pass through unchanged; selected ones are lifted along
   // their own normal and walled back to where they were.
   std::vector<int> remap(in.vertices.size(), -1);
   auto copyVertex = [&](unsigned int index) {
      if (remap[index] < 0)
      {
         remap[index] = (int)out.vertices.size();
         out.vertices.push_back(in.vertices[index]);
      }
      return (unsigned int)remap[index];
   };

   for (size_t f = 0; f < faces; f++)
   {
      if (in.FaceSelected(f))
         continue;
      for (int k = 0; k < 3; k++)
         out.indices.push_back(copyVertex(in.indices[f * 3 + k]));
   }

   for (size_t f = 0; f < faces; f++)
   {
      if (!in.FaceSelected(f))
         continue;

      float normal[3], centre[3];
      FaceNormal(in, f, normal);
      FaceCentre(in, f, centre);

      const unsigned int base = (unsigned int)out.vertices.size();
      // Original rim, then the lifted cap, so the wall can be stitched between.
      for (int k = 0; k < 3; k++)
         out.vertices.push_back(in.vertices[in.indices[f * 3 + k]]);
      for (int k = 0; k < 3; k++)
      {
         Vertex v = in.vertices[in.indices[f * 3 + k]];
         v.px += (centre[0] - v.px) * inset + normal[0] * distance;
         v.py += (centre[1] - v.py) * inset + normal[1] * distance;
         v.pz += (centre[2] - v.pz) * inset + normal[2] * distance;
         out.vertices.push_back(v);
      }

      out.indices.push_back(base + 3);
      out.indices.push_back(base + 4);
      out.indices.push_back(base + 5);

      for (int k = 0; k < 3; k++)
      {
         const unsigned int a0 = base + (unsigned int)k;
         const unsigned int a1 = base + (unsigned int)((k + 1) % 3);
         const unsigned int b0 = base + 3 + (unsigned int)k;
         const unsigned int b1 = base + 3 + (unsigned int)((k + 1) % 3);
         PushQuad(out, a0, a1, b1, b0);
      }
   }
   return RecalculateNormals(out, false, false);
}
}

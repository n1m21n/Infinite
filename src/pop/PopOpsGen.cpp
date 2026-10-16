// PointZ operators, tier A: generators (CPU-built, uploaded once per change).
// Each entry follows a TouchDesigner POP of the same name in purpose
// (behaviour written from Derivative's public operator descriptions, not
// from their code); parameter pages are Infinite's own.

#include "PopOps.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Mesh.h"

namespace pop
{
   namespace
   {
      const float kPiA = 3.14159265358979f;

      // ---- generator helpers ---------------------------------------------
      struct Builder
      {
         std::vector<float> P, N, T;
         std::vector<uint32_t> tris;
         uint32_t Add(float x, float y, float z, float nx, float ny, float nz, float u, float v)
         {
            P.insert(P.end(), { x, y, z });
            N.insert(N.end(), { nx, ny, nz });
            T.insert(T.end(), { u, v, 0.0f });
            return (uint32_t)(P.size() / 3 - 1);
         }
         void Tri(uint32_t a, uint32_t b, uint32_t c) { tris.insert(tris.end(), { a, b, c }); }
         void Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { Tri(a, b, c); Tri(a, c, d); }
         std::shared_ptr<PopData> Done(int rows = 0, int cols = 0)
         {
            auto out = std::make_shared<PopData>();
            out->pointCount = (int)(P.size() / 3);
            out->Set(attr::P, MakeBuffer(std::move(P), 3));
            out->Set(attr::N, MakeBuffer(std::move(N), 3));
            out->Set(attr::Tex, MakeBuffer(std::move(T), 3));
            auto topo = NewTopo();
            topo->tris = std::move(tris);
            out->topo = topo;
            out->dims[0] = rows;
            out->dims[1] = cols;
            return out;
         }
      };

      // Planar orientation: 0 XY (facing +Z), 1 XZ (ground, facing +Y), 2 YZ (facing +X).
      void Orient(int o, float a, float b, float& x, float& y, float& z)
      {
         if (o == 1) { x = a; y = 0.0f; z = -b; }
         else if (o == 2) { x = 0.0f; y = b; z = -a; }
         else { x = a; y = b; z = 0.0f; }
      }
      void OrientN(int o, float& nx, float& ny, float& nz)
      {
         nx = (o == 2) ? 1.0f : 0.0f;
         ny = (o == 1) ? 1.0f : 0.0f;
         nz = (o == 0) ? 1.0f : 0.0f;
      }

      std::vector<std::string> Orients() { return { "XY", "XZ (ground)", "YZ" }; }

      void Offset(PopData& d, const float c[3])
      {
         if (c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f)
            return;
         std::vector<float> P = d.Read(attr::P, 3);
         for (size_t i = 0; i + 2 < P.size(); i += 3)
         {
            P[i] += c[0]; P[i + 1] += c[1]; P[i + 2] += c[2];
         }
         d.Set(attr::P, MakeBuffer(std::move(P), 3));
      }

      // Output kind shared by the 2D generators.
      std::vector<std::string> SurfaceKinds() { return { "polygons", "lines", "points" }; }

      // Turn a builder grid (rows x cols points) into lines or points instead of faces.
      void ApplyKind(PopData& d, int kind, int rows, int cols, bool closeRows)
      {
         if (kind == 0)
            return;
         auto topo = NewTopo();
         if (kind == 1)
         {
            for (int r = 0; r < rows; r++)
            {
               std::vector<uint32_t> s;
               for (int c = 0; c < cols; c++)
                  s.push_back((uint32_t)(r * cols + c));
               topo->AddStrip(s, closeRows);
            }
         }
         else
            topo->pointPrims = true;
         d.topo = topo;
      }
   }

   void RegisterGenerators(std::vector<PopDef>& defs)
   {
      // ---- Box -------------------------------------------------------------
      {
         PopDef d;
         d.name = "Box PointZ"; d.tdName = "Box PointZ"; d.inputs = 0;
         d.help = "A box of points and faces: size, centre and divisions per side. Outputs P, N and Tex.";
         d.params = { V("size", "size", 1, 1, 1, 0.0f, 10.0f), V("center", "center", 0, 0, 0, -10.0f, 10.0f),
                      I("div", "divisions", 1, 1, 64) };
         d.cook = [](CookCtx& c) {
            float s[3], o[3];
            c.V3("size", s);
            c.V3("center", o);
            const int n = std::max(1, c.I("div"));
            Builder b;
            // Six faces, each an n x n grid with its own normal (hard edges).
            static const float kFace[6][9] = {
               { 1, 0, 0, 0, 0, -1, 0, 1, 0 }, { -1, 0, 0, 0, 0, 1, 0, 1, 0 },
               { 0, 1, 0, 1, 0, 0, 0, 0, -1 }, { 0, -1, 0, 1, 0, 0, 0, 0, 1 },
               { 0, 0, 1, 1, 0, 0, 0, 1, 0 }, { 0, 0, -1, -1, 0, 0, 0, 1, 0 },
            };
            for (int f = 0; f < 6; f++)
            {
               const float* nrm = kFace[f];
               const float* du = kFace[f] + 3;
               const float* dv = kFace[f] + 6;
               const uint32_t base = (uint32_t)(b.P.size() / 3);
               for (int j = 0; j <= n; j++)
                  for (int i = 0; i <= n; i++)
                  {
                     const float u = (float)i / n - 0.5f, v = (float)j / n - 0.5f;
                     const float x = (nrm[0] * 0.5f + du[0] * u + dv[0] * v) * s[0] + o[0];
                     const float y = (nrm[1] * 0.5f + du[1] * u + dv[1] * v) * s[1] + o[1];
                     const float z = (nrm[2] * 0.5f + du[2] * u + dv[2] * v) * s[2] + o[2];
                     b.Add(x, y, z, nrm[0], nrm[1], nrm[2], u + 0.5f, v + 0.5f);
                  }
               for (int j = 0; j < n; j++)
                  for (int i = 0; i < n; i++)
                  {
                     const uint32_t a = base + (uint32_t)(j * (n + 1) + i);
                     b.Quad(a, a + 1, a + (uint32_t)(n + 2), a + (uint32_t)(n + 1));
                  }
            }
            c.out = b.Done();
            return true;
         };
         defs.push_back(d);
      }

      // ---- Sphere ----------------------------------------------------------
      {
         PopDef d;
         d.name = "Sphere PointZ"; d.tdName = "Sphere PointZ"; d.inputs = 0;
         d.help = "A sphere: UV (rows and columns, poles at +-Y) or geodesic (icosphere, even triangles).";
         d.params = { M("type", "type", { "UV", "geodesic" }, 0), F("radius", "radius", 0.5f, 0.0f, 10.0f),
                      V("center", "center", 0, 0, 0, -10.0f, 10.0f), I("rows", "rows", 24, 3, 512, "type=0"),
                      I("cols", "columns", 48, 3, 1024, "type=0"), I("freq", "frequency", 3, 0, 7, "type=1") };
         d.cook = [](CookCtx& c) {
            const float r = c.F("radius");
            float o[3];
            c.V3("center", o);
            if (c.I("type") == 1)
            {
               Mesh m = Primitives::Icosphere(std::max(0, std::min(7, c.I("freq"))));
               for (Vertex& v : m.vertices)
               {
                  const float len = std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz);
                  const float k = len > 1e-9f ? 1.0f / len : 0.0f;
                  v.nx = v.px * k; v.ny = v.py * k; v.nz = v.pz * k;
                  v.px = v.nx * r; v.py = v.ny * r; v.pz = v.nz * r;
               }
               c.out = FromMesh(m);
               Offset(*c.out, o);
               return true;
            }
            const int rows = std::max(3, c.I("rows")), cols = std::max(3, c.I("cols"));
            Builder b;
            for (int j = 0; j <= rows; j++)
            {
               const float phi = kPiA * (float)j / rows;
               for (int i = 0; i <= cols; i++)
               {
                  const float th = 2.0f * kPiA * (float)i / cols;
                  const float nx = std::sin(phi) * std::cos(th), ny = std::cos(phi), nz = -std::sin(phi) * std::sin(th);
                  b.Add(nx * r + o[0], ny * r + o[1], nz * r + o[2], nx, ny, nz, (float)i / cols, 1.0f - (float)j / rows);
               }
            }
            for (int j = 0; j < rows; j++)
               for (int i = 0; i < cols; i++)
               {
                  const uint32_t a = (uint32_t)(j * (cols + 1) + i), bb = a + (uint32_t)(cols + 1);
                  if (j > 0) b.Tri(a, bb, a + 1);
                  if (j < rows - 1) b.Tri(a + 1, bb, bb + 1);
               }
            c.out = b.Done(rows + 1, cols + 1);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Torus -----------------------------------------------------------
      {
         PopDef d;
         d.name = "Torus PointZ"; d.tdName = "Torus PointZ"; d.inputs = 0;
         d.help = "A torus around the Y axis: major and minor radius, rows around the ring, columns around the tube.";
         d.params = { F("major", "major radius", 0.5f, 0.0f, 10.0f), F("minor", "minor radius", 0.15f, 0.0f, 5.0f),
                      I("rows", "rows", 48, 3, 1024), I("cols", "columns", 24, 3, 512),
                      V("center", "center", 0, 0, 0, -10.0f, 10.0f) };
         d.cook = [](CookCtx& c) {
            const float R = c.F("major"), r = c.F("minor");
            const int rows = std::max(3, c.I("rows")), cols = std::max(3, c.I("cols"));
            float o[3];
            c.V3("center", o);
            Builder b;
            for (int j = 0; j <= rows; j++)
            {
               const float u = 2.0f * kPiA * (float)j / rows;
               for (int i = 0; i <= cols; i++)
               {
                  const float v = 2.0f * kPiA * (float)i / cols;
                  const float nx = std::cos(v) * std::cos(u), ny = std::sin(v), nz = std::cos(v) * std::sin(u);
                  const float x = (R + r * std::cos(v)) * std::cos(u), y = r * std::sin(v), z = (R + r * std::cos(v)) * std::sin(u);
                  b.Add(x + o[0], y + o[1], z + o[2], nx, ny, nz, (float)j / rows, (float)i / cols);
               }
            }
            for (int j = 0; j < rows; j++)
               for (int i = 0; i < cols; i++)
               {
                  const uint32_t a = (uint32_t)(j * (cols + 1) + i), bb = a + (uint32_t)(cols + 1);
                  b.Quad(a, a + 1, bb + 1, bb);
               }
            c.out = b.Done(rows + 1, cols + 1);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Tube ------------------------------------------------------------
      {
         PopDef d;
         d.name = "Tube PointZ"; d.tdName = "Tube PointZ"; d.inputs = 0;
         d.help = "A tube along Y: bottom and top radius (a cone when one is 0), height, rows and columns, optional caps.";
         d.params = { F("rbottom", "bottom radius", 0.5f, 0.0f, 10.0f), F("rtop", "top radius", 0.5f, 0.0f, 10.0f),
                      F("height", "height", 1.0f, 0.0f, 20.0f), I("rows", "rows", 2, 2, 512), I("cols", "columns", 32, 3, 1024),
                      B("caps", "end caps", true), V("center", "center", 0, 0, 0, -10.0f, 10.0f) };
         d.cook = [](CookCtx& c) {
            const float rb = c.F("rbottom"), rt = c.F("rtop"), h = c.F("height");
            const int rows = std::max(2, c.I("rows")), cols = std::max(3, c.I("cols"));
            float o[3];
            c.V3("center", o);
            Builder b;
            const float slope = (rb - rt) / std::max(1e-6f, h);
            for (int j = 0; j < rows; j++)
            {
               const float t = (float)j / (rows - 1);
               const float y = -0.5f * h + t * h, rad = rb + (rt - rb) * t;
               for (int i = 0; i <= cols; i++)
               {
                  const float th = 2.0f * kPiA * (float)i / cols;
                  float nx = std::cos(th), nz = -std::sin(th), ny = slope;
                  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                  b.Add(rad * std::cos(th) + o[0], y + o[1], -rad * std::sin(th) + o[2], nx / len, ny / len, nz / len,
                        (float)i / cols, t);
               }
            }
            for (int j = 0; j + 1 < rows; j++)
               for (int i = 0; i < cols; i++)
               {
                  const uint32_t a = (uint32_t)(j * (cols + 1) + i), bb = a + (uint32_t)(cols + 1);
                  b.Quad(a, a + 1, bb + 1, bb);
               }
            if (c.Bl("caps"))
            {
               for (int end = 0; end < 2; end++)
               {
                  const float y = end == 0 ? -0.5f * h : 0.5f * h, rad = end == 0 ? rb : rt, ny = end == 0 ? -1.0f : 1.0f;
                  if (rad <= 0.0f)
                     continue;
                  const uint32_t centre = b.Add(o[0], y + o[1], o[2], 0, ny, 0, 0.5f, 0.5f);
                  const uint32_t first = (uint32_t)(b.P.size() / 3);
                  for (int i = 0; i <= cols; i++)
                  {
                     const float th = 2.0f * kPiA * (float)i / cols;
                     b.Add(rad * std::cos(th) + o[0], y + o[1], -rad * std::sin(th) + o[2], 0, ny, 0,
                           0.5f + 0.5f * std::cos(th), 0.5f + 0.5f * std::sin(th));
                  }
                  for (int i = 0; i < cols; i++)
                  {
                     if (end == 0) b.Tri(centre, first + (uint32_t)i + 1, first + (uint32_t)i);
                     else b.Tri(centre, first + (uint32_t)i, first + (uint32_t)i + 1);
                  }
               }
            }
            c.out = b.Done(rows, cols + 1);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Grid / Plane ----------------------------------------------------
      for (int which = 0; which < 2; which++)
      {
         PopDef d;
         const bool grid = which == 0;
         d.name = grid ? "Grid PointZ" : "Plane PointZ";
         d.tdName = d.name;
         d.inputs = 0;
         d.help = grid ? "A grid of rows x columns points (its dimension is kept for grid-aware PointZ nodes), as polygons, row lines or points."
                       : "A flat rectangle split into rows x columns, as polygons, row lines or points.";
         d.params = { V("size", "size", grid ? 2.0f : 1.0f, grid ? 2.0f : 1.0f, 0.0f, 0.0f, 20.0f),
                      I("rows", "rows", grid ? 32 : 1, 1, 2048), I("cols", "columns", grid ? 32 : 1, 1, 2048),
                      M("orient", "orientation", Orients(), grid ? 1 : 0), M("kind", "output", SurfaceKinds(), 0),
                      V("center", "center", 0, 0, 0, -10.0f, 10.0f) };
         d.cook = [grid](CookCtx& c) {
            float s[3], o[3];
            c.V3("size", s);
            c.V3("center", o);
            // A grid counts points per side; a plane counts faces per side.
            int rows = std::max(1, c.I("rows")), cols = std::max(1, c.I("cols"));
            if (!grid) { rows += 1; cols += 1; }
            rows = std::max(2, rows); cols = std::max(2, cols);
            const int orient = c.I("orient");
            float nx, ny, nz;
            OrientN(orient, nx, ny, nz);
            Builder b;
            for (int j = 0; j < rows; j++)
               for (int i = 0; i < cols; i++)
               {
                  const float u = (float)i / (cols - 1), v = (float)j / (rows - 1);
                  float x, y, z;
                  Orient(orient, (u - 0.5f) * s[0], (v - 0.5f) * s[1], x, y, z);
                  b.Add(x + o[0], y + o[1], z + o[2], nx, ny, nz, u, v);
               }
            for (int j = 0; j + 1 < rows; j++)
               for (int i = 0; i + 1 < cols; i++)
               {
                  const uint32_t a = (uint32_t)(j * cols + i);
                  b.Quad(a, a + 1, a + (uint32_t)cols + 1, a + (uint32_t)cols);
               }
            c.out = b.Done(rows, cols);
            ApplyKind(*c.out, c.I("kind"), rows, cols, false);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Circle / Rectangle ---------------------------------------------
      {
         PopDef d;
         d.name = "Circle PointZ"; d.tdName = "Circle PointZ"; d.inputs = 0;
         d.help = "A circle or arc: a closed line, a filled polygon (triangle fan) or points.";
         d.params = { F("radius", "radius", 0.5f, 0.0f, 10.0f), I("div", "divisions", 64, 3, 4096),
                      F("arcStart", "arc start", 0.0f, 0.0f, 360.0f), F("arcEnd", "arc end", 360.0f, 0.0f, 360.0f),
                      M("kind", "output", SurfaceKinds(), 1), M("orient", "orientation", Orients(), 0),
                      V("center", "center", 0, 0, 0, -10.0f, 10.0f) };
         d.cook = [](CookCtx& c) {
            const float r = c.F("radius");
            const int n = std::max(3, c.I("div"));
            const float a0 = c.F("arcStart") * kPiA / 180.0f, a1 = c.F("arcEnd") * kPiA / 180.0f;
            const bool full = std::fabs(a1 - a0) >= 2.0f * kPiA - 1e-4f;
            const int orient = c.I("orient"), kind = c.I("kind");
            float o[3];
            c.V3("center", o);
            float nx, ny, nz;
            OrientN(orient, nx, ny, nz);
            Builder b;
            const int pts = full ? n : n + 1;
            for (int i = 0; i < pts; i++)
            {
               const float a = a0 + (a1 - a0) * (float)i / n;
               float x, y, z;
               Orient(orient, r * std::cos(a), r * std::sin(a), x, y, z);
               b.Add(x + o[0], y + o[1], z + o[2], nx, ny, nz, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
            }
            if (kind == 0)
            {
               float x, y, z;
               Orient(orient, 0, 0, x, y, z);
               const uint32_t centre = b.Add(x + o[0], y + o[1], z + o[2], nx, ny, nz, 0.5f, 0.5f);
               for (int i = 0; i + 1 < pts; i++)
                  b.Tri(centre, (uint32_t)i, (uint32_t)i + 1);
               if (full)
                  b.Tri(centre, (uint32_t)(pts - 1), 0);
               c.out = b.Done();
            }
            else
            {
               c.out = b.Done(1, pts);
               ApplyKind(*c.out, kind, 1, pts, full);
            }
            return true;
         };
         defs.push_back(d);
      }
      {
         PopDef d;
         d.name = "Rectangle PointZ"; d.tdName = "Rectangle PointZ"; d.inputs = 0;
         d.help = "A rectangle: a closed line of four corners, a filled quad or its corner points.";
         d.params = { V("size", "size", 1, 1, 0, 0.0f, 20.0f), M("kind", "output", SurfaceKinds(), 1),
                      M("orient", "orientation", Orients(), 0), V("center", "center", 0, 0, 0, -10.0f, 10.0f) };
         d.cook = [](CookCtx& c) {
            float s[3], o[3];
            c.V3("size", s);
            c.V3("center", o);
            const int orient = c.I("orient"), kind = c.I("kind");
            float nx, ny, nz;
            OrientN(orient, nx, ny, nz);
            Builder b;
            const float cu[4] = { -0.5f, 0.5f, 0.5f, -0.5f }, cv[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
            for (int i = 0; i < 4; i++)
            {
               float x, y, z;
               Orient(orient, cu[i] * s[0], cv[i] * s[1], x, y, z);
               b.Add(x + o[0], y + o[1], z + o[2], nx, ny, nz, cu[i] + 0.5f, cv[i] + 0.5f);
            }
            if (kind == 0)
               b.Quad(0, 1, 2, 3);
            c.out = b.Done();
            if (kind != 0)
               ApplyKind(*c.out, kind, 1, 4, true);
            return true;
         };
         defs.push_back(d);
      }

      // ---- Line ------------------------------------------------------------
      {
         PopDef d;
         d.name = "Line PointZ"; d.tdName = "Line PointZ"; d.inputs = 0;
         d.help = "A straight line from point A to point B with evenly spaced points.";
         d.params = { V("a", "point A", -0.5f, 0, 0, -20.0f, 20.0f), V("b", "point B", 0.5f, 0, 0, -20.0f, 20.0f),
                      I("points", "points", 32, 2, 100000) };
         d.cook = [](CookCtx& c) {
            float a[3], bb[3];
            c.V3("a", a);
            c.V3("b", bb);
            const int n = std::max(2, c.I("points"));
            std::vector<float> P((size_t)n * 3), T((size_t)n * 3, 0.0f);
            std::vector<uint32_t> strip((size_t)n);
            for (int i = 0; i < n; i++)
            {
               const float t = (float)i / (n - 1);
               for (int k = 0; k < 3; k++)
                  P[(size_t)i * 3 + k] = a[k] + (bb[k] - a[k]) * t;
               T[(size_t)i * 3] = t;
               strip[(size_t)i] = (uint32_t)i;
            }
            c.out = FromPoints(std::move(P), false);
            c.out->Set(attr::Tex, MakeBuffer(std::move(T), 3));
            auto topo = NewTopo();
            topo->AddStrip(strip, false);
            c.out->topo = topo;
            c.out->dims[0] = 1;
            c.out->dims[1] = n;
            return true;
         };
         defs.push_back(d);
      }

      // ---- Point Generator -------------------------------------------------
      {
         PopDef d;
         d.name = "Point Generator PointZ"; d.tdName = "Point Generator PointZ"; d.inputs = 0;
         d.help = "Scatters a number of points in a shape (box, sphere, sphere surface, disc, line), uniformly at random "
                  "or as an even golden-ratio spiral. Outputs point primitives with P and an Id.";
         d.params = { I("count", "points", 1000, 1, 4000000), M("shape", "shape", { "box", "sphere", "sphere surface", "disc", "line" }, 0),
                      V("size", "size", 1, 1, 1, 0.0f, 50.0f), V("center", "center", 0, 0, 0, -50.0f, 50.0f),
                      M("dist", "distribution", { "random", "even spiral" }, 0), F("seed", "seed", 1.0f, 0.0f, 1000.0f) };
         d.cook = [](CookCtx& c) {
            const int n = std::max(1, c.I("count"));
            const int shape = c.I("shape");
            float s[3], o[3];
            c.V3("size", s);
            c.V3("center", o);
            const bool even = c.I("dist") == 1;
            uint32_t state = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
            auto rnd = [&]() {
               state ^= state << 13; state ^= state >> 17; state ^= state << 5;
               return (float)(state & 0xffffff) / 16777215.0f;
            };
            std::vector<float> P((size_t)n * 3), I((size_t)n);
            const float golden = kPiA * (3.0f - std::sqrt(5.0f));
            for (int i = 0; i < n; i++)
            {
               float x = 0, y = 0, z = 0;
               const float t = (i + 0.5f) / n;
               switch (shape)
               {
                  case 0:
                     x = rnd() - 0.5f; y = rnd() - 0.5f; z = rnd() - 0.5f;
                     break;
                  case 1:
                  case 2:
                  {
                     float u, ang;
                     if (even) { u = 1.0f - 2.0f * t; ang = golden * i; }
                     else { u = rnd() * 2.0f - 1.0f; ang = rnd() * 2.0f * kPiA; }
                     const float rr = std::sqrt(std::max(0.0f, 1.0f - u * u));
                     const float rad = shape == 2 ? 0.5f : 0.5f * std::cbrt(even ? t : rnd());
                     x = rr * std::cos(ang) * rad; y = u * rad; z = rr * std::sin(ang) * rad;
                     break;
                  }
                  case 3:
                  {
                     const float rad = 0.5f * std::sqrt(even ? t : rnd());
                     const float ang = even ? golden * i : rnd() * 2.0f * kPiA;
                     x = rad * std::cos(ang); z = rad * std::sin(ang);
                     break;
                  }
                  default:
                     x = (even ? t : rnd()) - 0.5f;
                     break;
               }
               P[(size_t)i * 3] = x * s[0] + o[0];
               P[(size_t)i * 3 + 1] = y * s[1] + o[1];
               P[(size_t)i * 3 + 2] = z * s[2] + o[2];
               I[(size_t)i] = (float)i;
            }
            c.out = FromPoints(std::move(P), true);
            c.out->Set(attr::PartId, MakeBuffer(std::move(I), 1));
            return true;
         };
         defs.push_back(d);
      }

      // ---- Pattern ---------------------------------------------------------
      {
         PopDef d;
         d.name = "Pattern PointZ"; d.tdName = "Pattern PointZ"; d.inputs = 0;
         d.help = "N points along X carrying a waveform in an attribute (sine, triangle, square, ramp, gaussian, random) - "
                  "a channel of values to look up, displace or instance with. The value also lifts Y when 'plot' is on.";
         d.params = { I("count", "points", 128, 1, 1000000), M("wave", "waveform", { "sine", "triangle", "square", "ramp", "gaussian", "random" }, 0),
                      F("cycles", "cycles", 1.0f, 0.0f, 64.0f), F("phase", "phase", 0.0f, 0.0f, 1.0f),
                      F("amp", "amplitude", 1.0f, 0.0f, 10.0f), F("offset", "offset", 0.0f, -10.0f, 10.0f),
                      F("width", "width", 2.0f, 0.0f, 50.0f), T("name", "attribute", "Pattern"), B("plot", "plot in Y", true),
                      F("seed", "seed", 1.0f, 0.0f, 1000.0f) };
         d.cook = [](CookCtx& c) {
            const int n = std::max(1, c.I("count"));
            const int wave = c.I("wave");
            const float cycles = c.F("cycles"), phase = c.F("phase"), amp = c.F("amp"), off = c.F("offset");
            std::vector<float> P((size_t)n * 3, 0.0f), V((size_t)n);
            uint32_t state = (uint32_t)(c.F("seed") * 7919.0f) * 2654435761u + 1u;
            const float width = c.F("width");
            for (int i = 0; i < n; i++)
            {
               const float u = n > 1 ? (float)i / (n - 1) : 0.0f;
               float x = u * cycles + phase;
               x -= std::floor(x);
               float v = 0.0f;
               switch (wave)
               {
                  case 0: v = std::sin(x * 2.0f * kPiA); break;
                  case 1: v = 1.0f - 4.0f * std::fabs(x - 0.5f); break;
                  case 2: v = x < 0.5f ? 1.0f : -1.0f; break;
                  case 3: v = x * 2.0f - 1.0f; break;
                  case 4: { const float g = (u - 0.5f - phase) * 6.0f; v = std::exp(-g * g * 0.5f); break; }
                  default:
                     state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                     v = (float)(state & 0xffffff) / 16777215.0f * 2.0f - 1.0f;
                     break;
               }
               V[(size_t)i] = v * amp + off;
               P[(size_t)i * 3] = (u - 0.5f) * width;
               if (c.Bl("plot"))
                  P[(size_t)i * 3 + 1] = V[(size_t)i];
            }
            std::string name = c.S("name");
            if (!ValidAttribName(name))
            {
               c.warning = "attribute name '" + name + "' is not usable; wrote 'Pattern'";
               name = "Pattern";
            }
            c.out = FromPoints(std::move(P), false);
            c.out->Set(name, MakeBuffer(std::move(V), 1));
            std::vector<uint32_t> strip((size_t)n);
            for (int i = 0; i < n; i++)
               strip[(size_t)i] = (uint32_t)i;
            auto topo = NewTopo();
            if (n >= 2)
               topo->AddStrip(strip, false);
            else
               topo->pointPrims = true;
            c.out->topo = topo;
            return true;
         };
         defs.push_back(d);
      }
   }
}

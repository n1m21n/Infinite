// Standalone checks for the Sketch 3D engine (no GL, no app): primitive winding vs normals,
// determinism, transforms, limits, abort, errors.
#include "Sketch3DEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>

static int gFail = 0;
static void Check(bool ok, const char* what)
{
   std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
   if (!ok) ++gFail;
}

// Fraction of triangles whose geometric normal (from winding) agrees with the vertex normal.
static float WindingAgreement(const Mesh& m)
{
   size_t good = 0, n = m.indices.size() / 3;
   for (size_t t = 0; t < n; ++t)
   {
      const Vertex &a = m.vertices[m.indices[t * 3]], &b = m.vertices[m.indices[t * 3 + 1]],
                   &c = m.vertices[m.indices[t * 3 + 2]];
      const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
      const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
      const float gx = uy * vz - uz * vy, gy = uz * vx - ux * vz, gz = ux * vy - uy * vx;
      const float d = gx * (a.nx + b.nx + c.nx) + gy * (a.ny + b.ny + c.ny) + gz * (a.nz + b.nz + c.nz);
      const float len2 = gx * gx + gy * gy + gz * gz;
      if (len2 < 1e-8f || d > 0) ++good;   // degenerate (pole) triangles don't count against
   }
   return n ? (float)good / n : 0;
}

static bool Build(const char* code, Mesh& m, SketchError& err)
{
   Sketch3DEngine e;
   if (!e.Compile(code, err)) return false;
   Sketch3DEngine::Frame f;
   return e.Run(f, m, err);
}

int main()
{
   SketchError err;
   Mesh m;
   struct { const char* name; const char* code; } prims[] = {
      {"box", "function draw(t){ box(1,2,3); }"},
      {"sphere", "function draw(t){ sphere(0.5, 16); }"},
      {"cylinder", "function draw(t){ cylinder(0.5, 1, 16); }"},
      {"cone", "function draw(t){ cone(0.5, 1, 16); }"},
      {"torus", "function draw(t){ torus(0.5, 0.2, 16); }"},
      {"plane", "function draw(t){ plane(1,1); }"},
      {"tube", "function draw(t){ tube(0,0,0, 1,1,0, 0.1, 8); }"},
      {"box mirrored", "function draw(t){ scale(-1,1,1); box(1); }"},
      {"sphere non-uniform scale", "function draw(t){ scale(1,3,1); rotateX(0.7); sphere(0.5, 16); }"},
   };
   for (auto& p : prims)
   {
      char msg[96];
      const bool ok = Build(p.code, m, err);
      std::snprintf(msg, sizeof msg, "%s builds (%zu tris)", p.name, m.indices.size() / 3);
      Check(ok && !m.indices.empty(), msg);
      std::snprintf(msg, sizeof msg, "%s winding matches normals (%.3f)", p.name, WindingAgreement(m));
      Check(ok && WindingAgreement(m) > 0.99f, msg);
      for (unsigned i : m.indices) if (i >= m.vertices.size()) { Check(false, "index in range"); break; }
   }

   // Transform order: translate then box lands at x=5.
   Build("function draw(t){ translate(5,0,0); box(1); }", m, err);
   float lo = 1e9f, hi = -1e9f;
   for (auto& v : m.vertices) { lo = std::min(lo, v.px); hi = std::max(hi, v.px); }
   Check(std::fabs(lo - 4.5f) < 1e-4f && std::fabs(hi - 5.5f) < 1e-4f, "translate moves the box");
   Build("function draw(t){ push(); translate(5,0,0); pop(); box(1); }", m, err);
   lo = 1e9f; hi = -1e9f;
   for (auto& v : m.vertices) { lo = std::min(lo, v.px); hi = std::max(hi, v.px); }
   Check(std::fabs(lo + 0.5f) < 1e-4f && std::fabs(hi - 0.5f) < 1e-4f, "push/pop restores transform");

   // Fill: colour only when fill() was called.
   Build("function draw(t){ box(1); }", m, err);
   Check(!m.HasVertexColor(), "no fill() -> no vertex colour");
   Build("function draw(t){ fill(1,0,0); box(1); }", m, err);
   Check(m.HasVertexColor() && m.vertexColor[0] == 1.f && m.vertexColor[1] == 0.f, "fill() colours vertices");

   // Custom shape.
   Build("function draw(t){ beginShape(TRIANGLES); vertex(0,0,0); vertex(1,0,0); vertex(0,1,0); endShape(); }", m, err);
   Check(m.indices.size() == 3 && std::fabs(m.vertices[0].nz - 1) < 1e-4f, "beginShape triangle, flat normal +z");

   // Determinism incl. random().
   const char* rnd = "function draw(t){ for(let i=0;i<20;i++){ push(); translate(random(-1,1),random(-1,1),random(-1,1)); box(0.1); pop(); } }";
   Sketch3DEngine e;
   Check(e.Compile(rnd, err), "random sketch compiles");
   Sketch3DEngine::Frame f; f.seed = 7; f.frame = 3;
   Mesh a, b, c;
   e.Run(f, a, err); e.Run(f, b, err);
   f.frame = 4; e.Run(f, c, err);
   Check(a.vertices.size() == b.vertices.size() && a.vertices[5].px == b.vertices[5].px, "same (seed, frame) -> same mesh");
   Check(a.vertices[5].px != c.vertices[5].px, "different frame -> different mesh");

   // Params.
   Sketch3DEngine pe;
   Check(pe.Compile("param('n',4,1,8); function draw(t){ for(let i=0;i<n;i++) box(0.1); }", err) &&
         pe.Params().size() == 1, "param() declares a knob");
   Sketch3DEngine::Frame pf; std::map<std::string, float> pm{{"n", 2}}; pf.params = &pm;
   pe.Run(pf, m, err);
   Check(m.indices.size() == 2 * 12 * 3, "param value drives the mesh");

   // Limits and errors.
   Check(!Build("function draw(t){ sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); sphere(1,128); }", m, err) &&
         err.message.find("too large") != std::string::npos, "vertex cap throws a RangeError");
   auto t0 = std::chrono::steady_clock::now();
   const bool ab = Build("function draw(t){ while(true){} }", m, err);
   const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   Check(!ab && ms < 300 && err.message.find("too long") != std::string::npos, "runaway loop aborted");
   Check(!Build("function draw(t){ box( }", m, err) && err.line >= 1, "syntax error has a line");
   Check(!Build("var x = 1;", m, err) && err.message.find("draw") != std::string::npos, "missing draw() reported");
   Check(!Build("function draw(t){ vertex(0,0,0); }", m, err), "vertex outside beginShape throws");

   // Keep-last-working: a failed Compile leaves the old program.
   Sketch3DEngine k;
   k.Compile("function draw(t){ box(1); }", err);
   Check(!k.Compile("function draw(t){ box( }", err) && k.HasProgram(), "failed compile keeps the last program");

   std::printf("%s\n", gFail ? "FAILED" : "all passed");
   return gFail ? 1 : 0;
}

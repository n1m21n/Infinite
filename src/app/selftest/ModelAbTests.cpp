// Modelling A/B (tools/blender-ab/modeling.py): runs the case file's mesh operations and writes
// each result as an OBJ for the Blender comparison.
//
// INFINITE_MODELAB=<case file> INFINITE_MODELAB_OUT=<dir>. The file is a list of cases:
//   case NAME
//   prim cube N | sphere RINGS SECTORS | cylinder SIDES RINGS TOP | torus RINGS SIDES TUBE
//   edit <ops joined with ;>      EditMesh::Apply (src/core/EditMesh.h)
//   subdivide LEVELS SMOOTH | smooth ITERS STRENGTH | twist DEG AXIS | bevel WIDTH SEGS ANGLE
//   revolve AXIS DEG SEGS CAPS x y z ... | sweep SCALE0 SCALE1 TWIST CAPS CLOSEDPATH PROFILECOUNT x y z ... (profile then path)
//   end
// Writes DIR/NAME.base.obj (before the operations) and DIR/NAME.obj. Positions are Blender's Z-up
// (x, -z, y) from Infinite's Y-up.
#include "app/AppShared.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

#include "core/EditMesh.h"
#include "core/Mesh.h"

namespace app
{
namespace
{
   void WriteObj(const Mesh& m, const std::string& path)
   {
      std::ofstream f(path);
      for (const Vertex& v : m.vertices)
         f << "v " << v.px << " " << -v.pz << " " << v.py << "\n";
      for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
         f << "f " << m.indices[i] + 1 << " " << m.indices[i + 1] + 1 << " " << m.indices[i + 2] + 1 << "\n";
   }

   Polyline Line(std::istringstream& in, size_t count, bool closed)
   {
      Polyline p;
      p.closed = closed;
      for (size_t i = 0; i < count * 3; i++)
      {
         float x = 0;
         in >> x;
         p.points.push_back(x);
      }
      return p;
   }
}

bool RunModelAbTest()
{
   const char* file = getenv("INFINITE_MODELAB");
   const char* out = getenv("INFINITE_MODELAB_OUT");
   if (!file || !out)
      return false;
   std::ifstream in(file);
   std::string line, name;
   Mesh cur;
   int done = 0;
   while (std::getline(in, line))
   {
      std::istringstream ls(line);
      std::string op;
      ls >> op;
      if (op == "case")
      {
         ls >> name;
         cur = Mesh();
      }
      else if (op == "prim")
      {
         std::string k;
         ls >> k;
         int a = 0, b = 0;
         float c = 0;
         if (k == "cube") { ls >> a; cur = Primitives::Cube(a); }
         else if (k == "sphere") { ls >> a >> b; cur = Primitives::Sphere(a, b); }
         else if (k == "cylinder") { ls >> a >> b >> c; cur = Primitives::Cylinder(a, b, c); }
         else if (k == "torus") { ls >> a >> b >> c; cur = Primitives::Torus(a, b, c); }
         WriteObj(cur, std::string(out) + "/" + name + ".base.obj");
      }
      else if (op == "edit")
      {
         std::string ops((std::istreambuf_iterator<char>(ls)), std::istreambuf_iterator<char>());
         for (char& ch : ops)
            if (ch == ';')
               ch = '\n';
         cur = EditMesh::Apply(cur, ops);
      }
      else if (op == "subdivide") { int l; float s; ls >> l >> s; cur = MeshOps::Subdivide(cur, l, s); }
      else if (op == "smooth") { int l; float s; ls >> l >> s; cur = MeshOps::Smooth(cur, l, s); }
      else if (op == "twist") { float d; int ax; ls >> d >> ax; cur = MeshOps::Twist(cur, d, ax); }
      else if (op == "bevel") { float w, ang; int s; ls >> w >> s >> ang; cur = MeshOps::BevelEdges(cur, w, s, ang); }
      else if (op == "revolve")
      {
         int axis, segs, caps;
         float deg;
         ls >> axis >> deg >> segs >> caps;
         std::vector<float> pts((std::istream_iterator<float>(ls)), std::istream_iterator<float>());
         Polyline p;
         p.points = pts;
         cur = MeshOps::Revolve(p, axis, deg, segs, caps != 0);
      }
      else if (op == "sweep")
      {
         float s0, s1, tw;
         int caps, closed;
         size_t n;
         ls >> s0 >> s1 >> tw >> caps >> closed >> n;
         Polyline prof = Line(ls, n, true);
         std::vector<float> rest((std::istream_iterator<float>(ls)), std::istream_iterator<float>());
         Polyline path;
         path.points = rest;
         path.closed = closed != 0;
         cur = MeshOps::Sweep(prof, path, s0, s1, tw, caps != 0);
      }
      else if (op == "end")
      {
         WriteObj(cur, std::string(out) + "/" + name + ".obj");
         done++;
      }
   }
   printf("MODELAB %d cases\n", done);
   return done > 0;
}
}

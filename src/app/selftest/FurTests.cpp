// Geometry Op Fur (INFINITE_FURTEST): the strand cap, determinism, growth on selected faces only, a guide's shape,
// Tangent normals, and a patch save/load round trip of the node's params.
#include "app/AppShared.h"
#include "core/Mesh.h"
#include "nodes/GeometryOpNodes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace app
{
namespace
{
   bool sOk = true;

   void Check(const char* label, bool pass)
   {
      printf("  [%s] %s\n", pass ? "pass" : "FAIL", label);
      if (!pass)
         sOk = false;
   }

   struct P3
   {
      float x, y, z;
   };
   P3 Mid(const Mesh& m, size_t v) // a ribbon's centre at a strand point: the mean of its two edge vertices
   {
      const Vertex &a = m.vertices[v], &b = m.vertices[v + 1];
      return { (a.px + b.px) * 0.5f, (a.py + b.py) * 0.5f, (a.pz + b.pz) * 0.5f };
   }
   float Len(P3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

   size_t Strands(const Mesh& in, const Mesh& out, int segs)
   {
      return (out.FaceCount() - in.FaceCount()) / (size_t)(segs * 2);
   }
}

void RunFurTest(int f)
{
   if (f != 3)
      return;
   const Mesh ball = Primitives::Sphere(24, 32);
   MeshOps::FurParams p;
   p.density = 50.0f;      // far more than the cap allows on this ball
   p.maxStrands = 2000;
   p.length = 0.05f;
   p.segments = 4;
   p.gravity = 0.0f;
   p.clump = 0.0f;
   const int pts = p.segments + 1;
   const Mesh a = MeshOps::Fur(ball, p);
   const size_t n = Strands(ball, a, p.segments);
   Check("cap: Max Strands holds (rounded up to a clump of eight)", n > 0 && n <= (size_t)p.maxStrands + 7);
   Check("the base mesh is kept and the strands are marked two-sided",
         a.twoSided && a.ribbonStart == ball.indices.size() && a.vertices.size() == ball.vertices.size() + n * pts * 2);

   const Mesh b = MeshOps::Fur(ball, p);
   bool same = a.vertices.size() == b.vertices.size();
   for (size_t i = 0; same && i < a.vertices.size(); i++)
      same = a.vertices[i].px == b.vertices[i].px && a.vertices[i].py == b.vertices[i].py && a.vertices[i].pz == b.vertices[i].pz;
   Check("deterministic: the same params give the same strands", same);
   MeshOps::FurParams p2 = p;
   p2.seed = 7;
   const Mesh c = MeshOps::Fur(ball, p2);
   bool differ = false;
   for (size_t i = ball.vertices.size(); !differ && i < std::min(a.vertices.size(), c.vertices.size()); i++)
      differ = a.vertices[i].px != c.vertices[i].px;
   Check("another seed grows other strands", differ);

   // Straight strands (no guide, no gravity) leave the surface along it: the tip sits a strand length out.
   const size_t nv = ball.vertices.size();
   int along = 0;
   for (size_t s = 0; s < n; s++)
   {
      const P3 r = Mid(a, nv + s * pts * 2), t = Mid(a, nv + s * pts * 2 + (pts - 1) * 2);
      along += Len(t) - Len(r) > 0.6f * p.length;
   }
   Check("without a guide strands grow out from the surface", along > (int)n * 9 / 10);

   // Selection: fur only on the faces wholly above the equator.
   Mesh top = ball;
   top.faceMask.assign(ball.FaceCount(), 0);
   for (size_t k = 0; k < ball.FaceCount(); k++)
   {
      bool up = true;
      for (int j = 0; j < 3; j++)
         up = up && ball.vertices[ball.indices[k * 3 + j]].py > 0.0f;
      top.faceMask[k] = up;
   }
   const Mesh sel = MeshOps::Fur(top, p);
   const size_t ns = Strands(top, sel, p.segments);
   int below = 0;
   for (size_t s = 0; s < ns; s++)
      below += Mid(sel, nv + s * pts * 2).y < -1e-4f;
   Check("selection: roots only on the selected faces", ns > 0 && below == 0);

   // A guide: an arc bends every strand the same way; a straight guide keeps them straight.
   Polyline arc;
   for (int k = 0; k <= 8; k++)
   {
      const float t = k / 8.0f;
      arc.points.insert(arc.points.end(), { (t - 0.5f) * 1.0f, std::sin(3.14159265f * t), 0.0f });
   }
   const Mesh g = MeshOps::Fur(ball, p, &arc);
   int bent = 0;
   for (size_t s = 0; s < n; s++)
   {
      const P3 r = Mid(g, nv + s * pts * 2), m = Mid(g, nv + s * pts * 2 + 2), t = Mid(g, nv + s * pts * 2 + (pts - 1) * 2);
      const P3 d0 = { m.x - r.x, m.y - r.y, m.z - r.z }, d1 = { t.x - r.x, t.y - r.y, t.z - r.z };
      const float cosA = (d0.x * d1.x + d0.y * d1.y + d0.z * d1.z) / std::max(Len(d0) * Len(d1), 1e-9f);
      bent += cosA < 0.9f;
   }
   Check("guide: an arc bends the strands", bent > (int)n * 9 / 10);

   // Tangent normals: the vertex normal runs along the strand (the Hair material's fibre axis).
   MeshOps::FurParams pt = p;
   pt.normals = 1;
   const Mesh tn = MeshOps::Fur(ball, pt);
   int axial = 0;
   for (size_t s = 0; s < n; s++)
   {
      const size_t v0 = nv + s * pts * 2;
      const P3 r = Mid(tn, v0), q = Mid(tn, v0 + 2);
      const P3 d = { q.x - r.x, q.y - r.y, q.z - r.z };
      const Vertex& v = tn.vertices[v0 + 2];
      axial += std::fabs(d.x * v.nx + d.y * v.ny + d.z * v.nz) / std::max(Len(d), 1e-9f) > 0.95f;
   }
   Check("Tangent normals point along the strand", axial > (int)n * 9 / 10);

   // The node's params survive a save and a load.
   NewPatch();
   GraphNode* gn = SpawnNode("Fur", "3D", 0, 0);
   auto* fur = gn ? dynamic_cast<GeometryOpNode*>(gn->node.get()) : nullptr;
   Check("Fur spawns as Geometry Op in Fur mode", fur && fur->op == GeometryOpNode::kFur);
   if (fur)
   {
      fur->furDensity = 12.5f;
      fur->furMaxStrands = 3456;
      fur->furSegments = 7;
      fur->furNormals = 1;
      const std::string path = (std::filesystem::temp_directory_path() / "infinite_furtest.inf").string();
      SavePatchTo(path);
      NewPatch();
      const bool loaded = LoadPatchFrom(path);
      GeometryOpNode* back = nullptr;
      for (auto& node : gNodes)
         if (auto* o = dynamic_cast<GeometryOpNode*>(node.node.get()))
            back = o;
      Check("save/load keeps op, density, cap, segments and normals",
            loaded && back && back->op == GeometryOpNode::kFur && back->furDensity == 12.5f && back->furMaxStrands == 3456 &&
                back->furSegments == 7 && back->furNormals == 1);
      std::error_code ec;
      std::filesystem::remove(path, ec);
   }
   printf("FURTEST %s\n", sOk ? "OK" : "FAIL");
}
} // namespace app

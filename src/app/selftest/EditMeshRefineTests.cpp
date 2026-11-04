// Edit Mode ops survive an upstream refinement (INFINITE_EDITMESHREFINETEST): a face or point picked on Cube
// detail 4 means the same footprint on detail 16 and 96, so the replay equals editing that detail directly.
#include "app/AppShared.h"
#include "core/EditMesh.h"
#include "core/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

   void Bounds(const Mesh& m, float lo[3], float hi[3])
   {
      for (int k = 0; k < 3; k++)
      {
         lo[k] = 1e30f;
         hi[k] = -1e30f;
      }
      for (const Vertex& v : m.vertices)
      {
         const float p[3] = { v.px, v.py, v.pz };
         for (int k = 0; k < 3; k++)
         {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
         }
      }
   }

   bool SameBounds(const Mesh& a, const Mesh& b)
   {
      float la[3], ha[3], lb[3], hb[3];
      Bounds(a, la, ha);
      Bounds(b, lb, hb);
      for (int k = 0; k < 3; k++)
         if (std::fabs(la[k] - lb[k]) > 1e-3f || std::fabs(ha[k] - hb[k]) > 1e-3f)
            return false;
      return true;
   }

   // The faces whose normal points along +Y (the cube's top).
   std::vector<unsigned char> Top(const Mesh& m)
   {
      std::vector<unsigned char> out(m.FaceCount(), 0);
      for (size_t f = 0; f < out.size(); f++)
      {
         const Vertex &a = m.vertices[m.indices[f * 3]], &b = m.vertices[m.indices[f * 3 + 1]], &c = m.vertices[m.indices[f * 3 + 2]];
         const float u[3] = { b.px - a.px, b.py - a.py, b.pz - a.pz }, w[3] = { c.px - a.px, c.py - a.py, c.pz - a.pz };
         const float ny = u[2] * w[0] - u[0] * w[2];
         const float len = std::sqrt(std::pow(u[1] * w[2] - u[2] * w[1], 2) + ny * ny + std::pow(u[0] * w[1] - u[1] * w[0], 2));
         out[f] = len > 0.0f && ny / len > 0.9f;
      }
      return out;
   }

   int Moved(const Mesh& before, const Mesh& after)
   {
      int n = 0;
      for (size_t i = 0; i < before.vertices.size() && i < after.vertices.size(); i++)
         n += std::fabs(before.vertices[i].py - after.vertices[i].py) > 1e-5f;
      return n;
   }
}

void RunEditMeshRefineTest(int f)
{
   if (f != 3)
      return;
   const std::string tail = "\nextrude 0.3\nmove 0.2 0 0";
   const Mesh coarse = Primitives::Cube(4);
   const std::string ops = "sel f" + EditMesh::FaceSelectors(coarse, Top(coarse)) + tail;
   const Mesh slab4 = EditMesh::Apply(coarse, ops);
   Check("detail 4: the top face is extruded and moved", slab4.vertices.size() > coarse.vertices.size());
   Check("the sel line carries the footprints (t)", ops.find(" t ") != std::string::npos);

   for (int detail : { 16, 96 })
   {
      char label[96];
      const Mesh fine = Primitives::Cube(detail);
      int missed = 0;
      const Mesh replay = EditMesh::Apply(fine, ops, &missed);
      // The truth: the same edit made directly on that detail.
      const Mesh direct = EditMesh::Apply(fine, "sel f" + EditMesh::FaceSelectors(fine, Top(fine)) + tail);
      snprintf(label, sizeof(label), "detail %d: replay equals editing that detail directly (vertices, faces)", detail);
      Check(label, replay.vertices.size() == direct.vertices.size() && replay.FaceCount() == direct.FaceCount());
      snprintf(label, sizeof(label), "detail %d: same silhouette as detail 4 (one slab, no pillars)", detail);
      Check(label, SameBounds(replay, slab4) && SameBounds(replay, direct));
      snprintf(label, sizeof(label), "detail %d: nothing missed", detail);
      Check(label, missed == 0);
      snprintf(label, sizeof(label), "detail %d: vertex count grew with the detail", detail);
      Check(label, replay.vertices.size() > slab4.vertices.size());
   }

   // The same line without the footprint data replays by nearest match on an unchanged mesh, as it always did.
   const std::string plain = "sel f" + EditMesh::FaceSelectors(coarse, Top(coarse)).substr(0, EditMesh::FaceSelectors(coarse, Top(coarse)).find(" t ")) + tail;
   const Mesh a = EditMesh::Apply(coarse, ops), b = EditMesh::Apply(coarse, plain);
   bool same = a.vertices.size() == b.vertices.size();
   for (size_t i = 0; same && i < a.vertices.size(); i++)
      same = std::fabs(a.vertices[i].px - b.vertices[i].px) < 1e-4f && std::fabs(a.vertices[i].py - b.vertices[i].py) < 1e-4f &&
             std::fabs(a.vertices[i].pz - b.vertices[i].pz) < 1e-4f;
   Check("unchanged topology: footprint line equals the plain line", same);

   // A moved point is a bump, never a spike.
   size_t pick = 0;
   float bestD = 1e30f;
   for (size_t i = 0; i < coarse.vertices.size(); i++)
   {
      const Vertex& v = coarse.vertices[i];
      const float d = (v.px - 0.25f) * (v.px - 0.25f) + (v.py - 0.5f) * (v.py - 0.5f) + (v.pz - 0.25f) * (v.pz - 0.25f);
      if (d < bestD)
      {
         bestD = d;
         pick = i;
      }
   }
   const std::string pointOps = "sel v" + EditMesh::PointSelectors(coarse, { (unsigned int)pick }) + "\nmove 0 0.2 0";
   const Mesh bump4 = EditMesh::Apply(coarse, pointOps);
   Check("detail 4: the point moved on its own", Moved(coarse, bump4) >= 1 && Moved(coarse, bump4) <= 3);
   const Mesh fine = Primitives::Cube(96);
   const Mesh bump96 = EditMesh::Apply(fine, pointOps);
   float lo[3], hi[3], lo4[3], hi4[3];
   Bounds(bump96, lo, hi);
   Bounds(bump4, lo4, hi4);
   Check("detail 96: the point's reach moved as a bump, not one spike vertex", Moved(fine, bump96) > 20);
   Check("detail 96: the peak is the same height", std::fabs(hi[1] - hi4[1]) < 1e-3f);
   Check("detail 96: nothing moved past the picked offset", hi[1] <= 0.5f + 0.2f + 1e-3f);

   printf("EDITMESHREFINETEST %s\n", sOk ? "OK" : "FAIL");
}
} // namespace app

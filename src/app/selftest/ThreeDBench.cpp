// 3D bench (INFINITE_3DBENCH): times the CPU side of the 3D basics at the
// sizes people bring, one JSON line per row so runs compare. Budgets live in
// docs/plans/3d-next/README.md. INFINITE_3DBENCH_FOX / _CESIUM name the glbs.
#include "app/AppShared.h"

#include <algorithm>
#include <chrono>

#include "nodes/BakeNode.h"
#include "nodes/ModelSourceNode.h"
#include "nodes/SculptNode.h"

namespace app
{
namespace
{
   double Ms(std::chrono::steady_clock::time_point a)
   {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
   }
   double Median(std::vector<double> v)
   {
      std::sort(v.begin(), v.end());
      return v.empty() ? 0.0 : v[v.size() / 2];
   }
   void Row(const char* name, long size, double ms, double budget)
   {
      printf("{\"bench\":\"%s\",\"size\":%ld,\"ms\":%.3f,\"budget_ms\":%.1f,\"ok\":%s}\n", name, size, ms, budget,
             ms <= budget ? "true" : "false");
   }

   // A stroke as Sculpt mode runs it, one frame at a time: the point frame,
   // one dab, then the shaped mesh.
   void SculptRows(IGeometrySource* src, int level, const char* tag, double frameBudget, double buildBudget)
   {
      SculptNode n;
      n.input = src;
      n.level = level;
      auto t0 = std::chrono::steady_clock::now();
      n.CookIfNeeded(1);
      const double build = Ms(t0);
      const long points = n.Topology().points;
      const std::vector<float>& pos = n.PointPos();
      float lo = 1e30f, hi = -1e30f, front = -1e30f;
      for (size_t i = 1; i < pos.size(); i += 3)
      {
         lo = std::min(lo, pos[i]);
         hi = std::max(hi, pos[i]);
         front = std::max(front, pos[i + 1]);
      }
      // A stroke across the front, through the middle: each dab casts the
      // mouse ray, then paints. (Not over a UV sphere's pole, where every
      // ring's points crowd into one dab.)
      std::vector<double> frame, ray, dab, apply;
      double under = 0.0;
      std::vector<unsigned int> touched;
      const char* framesArg = getenv("INFINITE_3DBENCH_FRAMES");
      const int frames = framesArg ? std::max(1, atoi(framesArg)) : 30;
      for (int f = 0; f < frames; f++)
      {
         auto a = std::chrono::steady_clock::now();
         const float o[3] = { (hi - lo) * (-0.2f + 0.4f * (float)(f % 30) / 30.0f), (lo + hi) * 0.5f + 0.01f,
                              front + (hi - lo) };
         const float dir[3] = { 0, 0, -1 };
         float t = 0.0f;
         const bool hit = n.Raycast(o, dir, t);
         ray.push_back(Ms(a));
         auto b = std::chrono::steady_clock::now();
         Sculpt::Dab d;
         d.brush = f % 2 ? Sculpt::kSmooth : Sculpt::kDraw;
         d.center[0] = o[0];
         d.center[1] = o[1];
         d.center[2] = hit ? o[2] - t : front;
         d.radius = (hi - lo) * 0.1f;
         d.symmetry = 1;
         touched.clear();
         const Sculpt::Grid* grid = n.PointGrid(d.radius);
         Sculpt::Stroke(d, n.PointPos(), n.PointNrm(), n.Topology(), 1.0f, n.Offsets(), &touched, grid);
         dab.push_back(Ms(b));
         under += (double)touched.size() / frames;
         auto c = std::chrono::steady_clock::now();
         n.OffsetsChanged(&touched);
         n.GetMesh();
         apply.push_back(Ms(c));
         frame.push_back(Ms(a));
      }
      if (getenv("INFINITE_3DBENCH_VERBOSE"))
         for (size_t f = 0; f < frame.size(); f++)
            printf("    frame %zu: ray %.2f dab %.2f apply %.2f\n", f, ray[f], dab[f], apply[f]);
      // Where the build goes: the Level's subdivision, then the topology.
      auto s0 = std::chrono::steady_clock::now();
      const Mesh sub = level > 0 ? MeshOps::Subdivide(src->GetMesh(), level, 1.0f) : src->GetMesh();
      const double subMs = Ms(s0);
      Sculpt::Topology topo;
      auto s1 = std::chrono::steady_clock::now();
      Sculpt::Build(sub, topo);
      const double topoMs = Ms(s1);
      char name[64];
      snprintf(name, sizeof(name), "sculpt_build_%s", tag);
      Row(name, points, build, buildBudget);
      unsigned long long sum = 1469598103934665603ull;
      auto mix = [&](const void* data, size_t bytes)
      {
         for (size_t i = 0; i < bytes; i++)
            sum = (sum ^ ((const unsigned char*)data)[i]) * 1099511628211ull;
      };
      mix(sub.vertices.data(), sub.vertices.size() * sizeof(Vertex));
      mix(sub.indices.data(), sub.indices.size() * sizeof(unsigned int));
      mix(sub.vertexColor.data(), sub.vertexColor.size() * sizeof(float));
      printf("  (subdivide %.0f, topology %.0f ms, mesh %016llx)\n", subMs, topoMs, sum);
      snprintf(name, sizeof(name), "sculpt_frame_%s", tag);
      Row(name, points, Median(frame), frameBudget);
      printf("  (ray %.2f, dab %.2f, apply %.2f ms; %.0f points under the brush)\n", Median(ray), Median(dab),
             Median(apply), under);
   }
}

// ======================================================== INFINITE_3DBENCH
bool RunThreeDBench()
{
   printf("[3D BENCH]\n");
   // Sculpt on spheres subdivided by the node's Level: 16k, 65k, 260k points
   // (Level 3 is the top, and Subdivide holds the sphere there).
   struct Size { int detail, level; };
   const Size sizes[] = { { 128, 1 }, { 128, 2 }, { 128, 3 } };
   for (const Size& z : sizes)
   {
      GeometryNode sphere;
      sphere.shape = 2;
      sphere.detail = z.detail;
      sphere.CookIfNeeded(1);
      char tag[32];
      snprintf(tag, sizeof(tag), "sphere%d_L%d", z.detail, z.level);
      SculptRows(&sphere, z.level, tag, 8.0, 300.0);
   }

   if (const char* fox = getenv("INFINITE_3DBENCH_FOX"))
   {
      ModelSourceNode model;
      if (model.Load(fox))
      {
         // Skinning: a new phase every frame forces a re-skin.
         model.clipSpeed = 0.0f;
         std::vector<double> t;
         for (int f = 0; f < 30; f++)
         {
            model.clipPhase = (float)f / 30.0f;
            auto a = std::chrono::steady_clock::now();
            model.GetMesh();
            t.push_back(Ms(a));
         }
         const long verts = (long)model.GetMesh().vertices.size();
         Row("skin_fox", verts, Median(t), 2.0 * std::max(1.0, verts / 10000.0));
         // The Fox at sculpt Level 3.
         SculptRows(&model, 3, "fox_L3", 8.0, 300.0);
         BakeNode::Request r;
         r.mesh = MeshOps::UVProject(model.GetMesh(), MeshOps::kUVSmart, 66.0f, 0.02f, 1.0f);
         r.mode = BakeNode::kAO;
         r.res = 512;
         auto a = std::chrono::steady_clock::now();
         BakeNode::Bake(r);
         Row("bake_ao512_fox", verts, Ms(a), 1500.0);
      }
   }
   if (const char* cesium = getenv("INFINITE_3DBENCH_CESIUM"))
   {
      ModelSourceNode model;
      if (model.Load(cesium))
      {
         model.clipSpeed = 0.0f;
         std::vector<double> t;
         for (int f = 0; f < 30; f++)
         {
            model.clipPhase = (float)f / 30.0f;
            auto a = std::chrono::steady_clock::now();
            model.GetMesh();
            t.push_back(Ms(a));
         }
         const long verts = (long)model.GetMesh().vertices.size();
         Row("skin_cesiumman", verts, Median(t), 2.0 * std::max(1.0, verts / 10000.0));
      }
   }
   printf("3D BENCH: done\n");
   return true;
}
}

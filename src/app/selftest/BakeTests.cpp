// Textures self-test: UV Project writes UVs that stay in the unit square and
// split only at seams; Bake fills a cube's islands with the values the shape
// implies (open AO, convex curvature, bounds-normalised position).
#include "app/AppShared.h"

#include <cmath>

#include "nodes/BakeNode.h"

namespace app
{
// ======================================================== INFINITE_BAKETEST
bool RunBakeTest()
{
   printf("[BAKE TEST] UV Project and Bake\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };

   const Mesh cube = Primitives::Cube(1);
   const Mesh torus = Primitives::Torus(24, 12, 0.3f);
   for (int mode = MeshOps::kUVSmart; mode <= MeshOps::kUVSphere; mode++)
   {
      const Mesh& src = mode == MeshOps::kUVSmart || mode == MeshOps::kUVCube ? cube : torus;
      const Mesh m = MeshOps::UVProject(src, mode, 66.0f, 0.02f, 1.0f);
      float lo = 1e9f, hi = -1e9f;
      for (const Vertex& v : m.vertices)
      {
         lo = std::min({ lo, v.u, v.v });
         hi = std::max({ hi, v.u, v.v });
      }
      char what[96];
      snprintf(what, sizeof(what), "mode %d keeps every face", mode);
      check(m.indices.size() == src.indices.size(), what, (double)m.FaceCount(), (double)src.FaceCount());
      snprintf(what, sizeof(what), "mode %d UVs inside 0-1 (seam lift may reach 1.5)", mode);
      check(lo >= -1e-4f && hi <= (mode >= MeshOps::kUVCylinder ? 1.5f : 1.0001f), what, lo, hi);
   }
   // Smart on a cube: six islands, so every corner splits into three.
   const Mesh smart = MeshOps::UVProject(cube, MeshOps::kUVSmart, 66.0f, 0.02f, 1.0f);
   check(smart.vertices.size() == 24, "smart cube: 24 vertices (one per face corner)", (double)smart.vertices.size(), 24);

   auto bake = [&](int mode)
   {
      BakeNode::Request r;
      r.mesh = smart;
      r.mode = mode;
      r.res = 128;
      r.samples = 16;
      return BakeNode::Bake(r);
   };
   auto covered = [](const std::vector<float>& px, int channel, double& mean, float& lo, float& hi)
   {
      double sum = 0.0;
      int n = 0;
      lo = 1e9f; hi = -1e9f;
      for (size_t i = 0; i < px.size() / 4; i++)
         if (px[i * 4 + 3] > 0.5f)
         {
            sum += px[i * 4 + channel];
            lo = std::min(lo, px[i * 4 + channel]);
            hi = std::max(hi, px[i * 4 + channel]);
            n++;
         }
      mean = n ? sum / n : 0.0;
      return n;
   };
   double mean;
   float lo, hi;
   const int n = covered(bake(BakeNode::kAO), 0, mean, lo, hi);
   check(n > 128 * 128 / 3, "AO covers the packed islands", n, 128 * 128 / 3);
   check(mean > 0.97, "AO: a lone cube is open everywhere", mean, 0.97);
   covered(bake(BakeNode::kCurvature), 0, mean, lo, hi);
   check(mean >= 0.5 && lo >= 0.49f, "curvature: a cube is convex or flat, never concave", mean, lo);
   covered(bake(BakeNode::kPosition), 1, mean, lo, hi);
   check(lo >= -0.01f && hi <= 1.01f && hi - lo > 0.9f, "position y spans the bounds", lo, hi);
   covered(bake(BakeNode::kThickness), 0, mean, lo, hi);
   check(mean > 0.0 && hi <= 1.0001f, "thickness inside 0-1", mean, hi);

   printf("BAKE TEST: %s (%d failure%s)\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
   return failures == 0;
}
}

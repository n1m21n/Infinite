// Render 3D prototype path-tracer self-test (INFINITE_PTTEST) and benchmark
// (INFINITE_PTBENCH) for Section G.3 of the 3D Realism Specification.
#include "app/AppShared.h"
#include "core/GltfImport.h"
#include "core/Mesh.h"
#include "nodes/Render3DPathTrace.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace app
{
namespace
{
   std::string ResolveOutDir()
   {
      if (const char* envOut = getenv("INFINITE_PTTEST_OUT"))
         if (envOut[0] != '\0')
            return envOut;
      if (const char* envDir = getenv("INFINITE_PTTEST_DIR"))
         if (envDir[0] != '\0')
            return envDir;
      return TmpPath("infinite-pt-test");
   }

   Mesh MakeBoxMesh(float sx, float sy, float sz, float tx, float ty, float tz)
   {
      Mesh m = Primitives::Cube(1);
      for (Vertex& v : m.vertices)
      {
         v.px = v.px * (2.0f * sx) + tx;
         v.py = v.py * (2.0f * sy) + ty;
         v.pz = v.pz * (2.0f * sz) + tz;
      }
      return m;
   }

   Mesh MakeSmoothSphereMesh(float radius, float tx, float ty, float tz)
   {
      Mesh m = Primitives::Sphere(32, 64);
      for (Vertex& v : m.vertices)
      {
         const float x = v.px;
         const float y = v.py;
         const float z = v.pz;
         const float len = std::sqrt(x * x + y * y + z * z);
         if (len > 1e-8f)
         {
            v.nx = x / len;
            v.ny = y / len;
            v.nz = z / len;
         }
         v.px = v.nx * radius + tx;
         v.py = v.ny * radius + ty;
         v.pz = v.nz * radius + tz;
      }
      return m;
   }

   bool LoadGlbAsMesh(const std::string& path, Mesh& out)
   {
      std::string err;
      const GltfImport::GltfDecodePackage* loaded = GltfImport::DecodeCached(path, err);
      if (loaded == nullptr || loaded->vertices.empty() || loaded->indices.empty())
         return false;
      out = Mesh{};
      out.vertices.resize(loaded->vertices.size());
      for (size_t i = 0; i < loaded->vertices.size(); i++)
      {
         const Platform::ModelVertex& sv = loaded->vertices[i];
         Vertex& dv = out.vertices[i];
         dv.px = sv.px;
         dv.py = sv.py;
         dv.pz = sv.pz;
         dv.nx = sv.nx;
         dv.ny = sv.ny;
         dv.nz = sv.nz;
         dv.u = sv.u;
         dv.v = sv.v;
      }
      out.indices = loaded->indices;
      return true;
   }

   void BuildCornellScene(const std::string& outDir,
                          std::vector<Mesh>& meshes,
                          std::vector<Bvh::Instance>& instances,
                          std::vector<Render3DPathTrace::PtMaterial>& materials)
   {
      meshes.resize(7);
      const std::string dir = outDir + "/cornell";
      bool loadedAll = true;
      for (int i = 0; i < 7; i++)
      {
         const std::string p = dir + "/obj_" + std::to_string(i) + ".glb";
         if (!LoadGlbAsMesh(p, meshes[i]))
         {
            loadedAll = false;
            break;
         }
      }
      if (!loadedAll)
      {
         // Exact analytical match to tools/blender-ab/cases.py _cornell().
         meshes[0] = MakeBoxMesh(1.0f, 0.02f, 1.0f, 0.0f, -0.02f, 0.0f); // floor
         meshes[1] = MakeBoxMesh(1.0f, 0.02f, 1.0f, 0.0f, 2.02f, 0.0f);  // ceiling
         meshes[2] = MakeBoxMesh(1.0f, 1.0f, 0.02f, 0.0f, 1.0f, -1.02f); // back wall
         meshes[3] = MakeBoxMesh(0.02f, 1.0f, 1.0f, -1.02f, 1.0f, 0.0f); // left wall (red)
         meshes[4] = MakeBoxMesh(0.02f, 1.0f, 1.0f, 1.02f, 1.0f, 0.0f);  // right wall (green)
         meshes[5] = MakeSmoothSphereMesh(0.35f, -0.42f, 0.35f, -0.20f); // metal sphere
         meshes[6] = MakeSmoothSphereMesh(0.35f, 0.42f, 0.35f, 0.15f);   // white sphere
      }

      materials.clear();
      materials.push_back(Render3DPathTrace::PtMaterial::FromLinear(0.80f, 0.80f, 0.80f, 0.80f, 0.0f)); // 0: white
      materials.push_back(Render3DPathTrace::PtMaterial::FromLinear(0.80f, 0.08f, 0.08f, 0.80f, 0.0f)); // 1: red
      materials.push_back(Render3DPathTrace::PtMaterial::FromLinear(0.08f, 0.80f, 0.08f, 0.80f, 0.0f)); // 2: green
      materials.push_back(Render3DPathTrace::PtMaterial::FromLinear(0.90f, 0.90f, 0.90f, 0.15f, 1.0f)); // 3: metal
      materials.push_back(Render3DPathTrace::PtMaterial::FromLinear(0.85f, 0.85f, 0.85f, 0.70f, 0.0f)); // 4: white_sphere

      const int matForObj[7] = { 0, 0, 0, 1, 2, 3, 4 };
      instances.clear();
      for (int i = 0; i < 7; i++)
      {
         Bvh::Instance inst;
         inst.blasIndex = i;
         inst.materialId = matForObj[i];
         inst.transform = Mat4::Identity();
         instances.push_back(inst);
      }
   }

   int TotalTriangles(const std::vector<Mesh>& meshes)
   {
      int total = 0;
      for (const Mesh& m : meshes)
         total += (int)(m.indices.size() / 3);
      return total;
   }

   double BenchmarkMsPerSample(Render3DPathTrace& pt, int w, int h, int warmupSpp, int timedSpp)
   {
      pt.ResetAccumulation();
      pt.StepSamples(w, h, warmupSpp, false);
      glFinish();
      const auto t0 = std::chrono::steady_clock::now();
      pt.StepSamples(w, h, timedSpp, true);
      glFinish();
      const auto t1 = std::chrono::steady_clock::now();
      const double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
      return totalMs / (double)std::max(1, timedSpp);
   }

   std::string ShellQuote(const std::string& s)
   {
      std::string out = "'";
      for (char c : s)
      {
         if (c == '\'')
            out += "'\\''";
         else
            out += c;
      }
      out += "'";
      return out;
   }
} // namespace

bool RunPathTraceTest()
{
   const bool benchOnly = getenv("INFINITE_PTBENCH") != nullptr && getenv("INFINITE_PTTEST") == nullptr;
   const std::string outDir = ResolveOutDir();

   Render3DPathTrace pt;
   std::vector<Mesh> cornellMeshes;
   std::vector<Bvh::Instance> cornellInstances;
   std::vector<Render3DPathTrace::PtMaterial> cornellMats;
   BuildCornellScene(outDir, cornellMeshes, cornellInstances, cornellMats);

   if (!pt.UploadScene(cornellMeshes, cornellInstances, cornellMats))
   {
      std::printf("[PT TEST] FAIL: could not build Cornell scene BVH\n");
      return false;
   }

   const float camEye[3] = { 0.0f, 1.0f, 3.8f };
   const float camTarget[3] = { 0.0f, 1.0f, 0.0f };
   const float camUp[3] = { 0.0f, 1.0f, 0.0f };
   pt.SetCameraLookAt(camEye, camTarget, camUp, 40.0f, 1.0f);

   // Cornell ceiling area light from tools/blender-ab/cases.py _cornell():
   // power = 40 W, position = (0, 1.95, 0), aim = (0, -1, 0), size = 0.5 x 0.5.
   const float lightPos[3] = { 0.0f, 1.95f, 0.0f };
   const float lightAim[3] = { 0.0f, -1.0f, 0.0f };
   const float lightColor[3] = { 1.0f, 1.0f, 1.0f };
   std::vector<Render3DPathTrace::PtLight> lights;
   lights.push_back(Render3DPathTrace::PtLight::AreaWatts(lightPos, lightAim, 0.5f, 0.5f, 40.0f, lightColor));
   pt.SetLights(lights);
   pt.SetWorldColorLinear(0.0f, 0.0f, 0.0f);
   pt.SetTonemap(0);
   pt.SetExposure(1.0f);
   pt.maxBounces = 4;

   bool allPassed = true;

   if (!benchOnly)
   {
      std::printf("[PT TEST] Rendering Cornell box at 512x512, 256 spp...\n");
      const auto tRender0 = std::chrono::steady_clock::now();
      if (!pt.RenderToSpp(512, 512, 256))
      {
         std::printf("[PT TEST] FAIL: RenderToSpp failed\n");
         return false;
      }
      glFinish();
      const double renderSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - tRender0).count();

      std::vector<unsigned char> rgba;
      int w = 0, h = 0;
      if (!pt.ReadDisplayRGBA8(rgba, w, h) || w != 512 || h != 512)
      {
         std::printf("[PT TEST] FAIL: ReadDisplayRGBA8 failed\n");
         return false;
      }

      std::error_code ec;
      const std::string caseDir = outDir + "/cornell";
      std::filesystem::create_directories(caseDir, ec);
      const std::string ptPngPath = caseDir + "/pt.png";
      const std::string boardPngPath = outDir + "/cornell_pt_board.png";
      const std::string metricsPath = caseDir + "/pt_metrics.txt";
      std::filesystem::remove(metricsPath, ec);
      if (!stbi_write_png(ptPngPath.c_str(), w, h, 4, rgba.data(), w * 4))
      {
         std::printf("[PT TEST] FAIL: could not write %s\n", ptPngPath.c_str());
         return false;
      }

      // Check colour bleed from red (left) and green (right) walls onto the white sphere.
      // White sphere projects around (cx=337, cy=381) with radius ~44 px at 512x512.
      auto sampleRegionRGB = [&](int x0, int x1, int y0, int y1, double outRgb[3])
      {
         outRgb[0] = outRgb[1] = outRgb[2] = 0.0;
         int count = 0;
         for (int y = y0; y <= y1; y++)
         {
            for (int x = x0; x <= x1; x++)
            {
               const size_t idx = ((size_t)y * (size_t)w + (size_t)x) * 4u;
               outRgb[0] += (double)rgba[idx + 0] / 255.0;
               outRgb[1] += (double)rgba[idx + 1] / 255.0;
               outRgb[2] += (double)rgba[idx + 2] / 255.0;
               count++;
            }
         }
         if (count > 0)
         {
            outRgb[0] /= (double)count;
            outRgb[1] /= (double)count;
            outRgb[2] /= (double)count;
         }
      };

      double leftSphere[3] = {}, rightSphere[3] = {}, ceilingPatch[3] = {};
      sampleRegionRGB(305, 320, 374, 388, leftSphere);
      sampleRegionRGB(354, 369, 374, 388, rightSphere);
      sampleRegionRGB(236, 276, 80, 100, ceilingPatch);

      const bool redBleedOk = (leftSphere[0] > leftSphere[1] + 0.03) && (leftSphere[0] > leftSphere[2] + 0.03);
      const bool greenBleedOk = (rightSphere[1] > rightSphere[0] + 0.05) && (rightSphere[1] > rightSphere[2] + 0.05);
      const bool ceilingBounceOk = (ceilingPatch[0] > 0.25) && (ceilingPatch[1] > 0.25);

      std::printf("  [%s] white sphere left (red bleed): sRGB=(%.3f, %.3f, %.3f)\n",
                  redBleedOk ? "PASS" : "FAIL", leftSphere[0], leftSphere[1], leftSphere[2]);
      std::printf("  [%s] white sphere right (green bleed): sRGB=(%.3f, %.3f, %.3f)\n",
                  greenBleedOk ? "PASS" : "FAIL", rightSphere[0], rightSphere[1], rightSphere[2]);
      std::printf("  [%s] ceiling indirect bounce: sRGB=(%.3f, %.3f, %.3f)\n",
                  ceilingBounceOk ? "PASS" : "FAIL", ceilingPatch[0], ceilingPatch[1], ceilingPatch[2]);
      if (!redBleedOk || !greenBleedOk || !ceilingBounceOk)
         allPassed = false;

      // Score against Cycles truth via committed tools/blender-ab/score_pt.py.
      const std::filesystem::path repoRoot =
         std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
      const std::string scoreScript = (repoRoot / "tools" / "blender-ab" / "score_pt.py").string();
      const std::string pyCmd =
         "python3 " + ShellQuote(scoreScript) +
         " --out " + ShellQuote(outDir) +
         " --case cornell"
         " --image " + ShellQuote(ptPngPath) +
         " --board " + ShellQuote(boardPngPath) +
         " --metrics " + ShellQuote(metricsPath);
      const int pyRc = std::system(pyCmd.c_str());

      std::ifstream mf(metricsPath);
      std::string statusTag;
      mf >> statusTag;
      if (statusTag == "SKIP")
      {
         std::printf("  [SKIP] cornell FLIP comparison: skipped (blender not found) (pt PNG: %s)\n", ptPngPath.c_str());
      }
      else if (pyRc == 0 && statusTag == "OK")
      {
         double flipScore = 1.0, brightnessRatio = 0.0, lumaErr = 1.0, rasterFlip = 0.583;
         mf >> flipScore >> brightnessRatio >> lumaErr >> rasterFlip;
         const bool flipOk = flipScore < rasterFlip;
         std::printf("  [%s] cornell FLIP=%.3f vs raster=%.3f | brightness_ratio=%.2fx | luma_err=%.3f | render=%.2fs\n",
                     flipOk ? "PASS" : "FAIL", flipScore, rasterFlip, brightnessRatio, lumaErr, renderSec);
         std::printf("  board PNG: %s (pt PNG: %s)\n", boardPngPath.c_str(), ptPngPath.c_str());
         if (!flipOk)
            allPassed = false;
      }
      else
      {
         std::printf("  [FAIL] cornell FLIP script failed (rc=%d)\n", pyRc);
         allPassed = false;
      }
   }

   // Live strips trace the same samples a whole frame does: Render 3D's preview refines in
   // budget-sized strips, and the image it settles on must be the export's, pixel for pixel.
   if (!benchOnly)
   {
      std::vector<float> whole, strips;
      int ww = 0, wh = 0, sw = 0, sh = 0;
      const bool wholeOk = pt.RenderToSpp(256, 256, 8) && pt.ReadLinearRGBA32F(whole, ww, wh);
      pt.ResetAccumulation();
      pt.frameBudgetMs = 0.5f;   // forces many short strips
      pt.sampleBudgetPerFrame = 1;
      int calls = 0;
      while (pt.StepFrame(256, 256, 8) != 0 && calls < 10000)
         calls++;
      pt.frameBudgetMs = 12.0f;
      pt.sampleBudgetPerFrame = 4;
      const bool stripOk = pt.ReadLinearRGBA32F(strips, sw, sh) && pt.SampleCount() == 8;
      double maxDiff = (wholeOk && stripOk && whole.size() == strips.size()) ? 0.0 : 1e9;
      for (size_t i = 0; maxDiff < 1e9 && i < whole.size(); i++)
         maxDiff = std::max(maxDiff, (double)std::fabs(whole[i] - strips[i]));
      const bool same = maxDiff < 1e-5;
      std::printf("  [%s] live strips match a whole render: %d calls, max diff %.2e\n", same ? "PASS" : "FAIL",
                  calls, maxDiff);
      allPassed = allPassed && same;
   }

   // Benchmark ms_per_sample at 512x512 for cornell and Fox.
   const double cornellMs = BenchmarkMsPerSample(pt, 512, 512, 4, 32);
   std::printf("{\"scene\":\"cornell\",\"resolution\":\"512x512\",\"tris\":%d,\"ms_per_sample\":%.3f}\n",
               TotalTriangles(cornellMeshes), cornellMs);

   const char* foxEnv = getenv("INFINITE_3DBENCH_FOX");
   if (foxEnv == nullptr || foxEnv[0] == '\0')
   {
      std::printf("fox: skipped (INFINITE_3DBENCH_FOX unset)\n");
   }
   else
   {
      Mesh foxMesh;
      if (!LoadGlbAsMesh(foxEnv, foxMesh))
      {
         std::printf("fox: skipped (failed to load %s)\n", foxEnv);
      }
      else
      {
         float lo[3] = { 1e30f, 1e30f, 1e30f };
         float hi[3] = { -1e30f, -1e30f, -1e30f };
         for (const Vertex& v : foxMesh.vertices)
         {
            lo[0] = std::min(lo[0], v.px);
            lo[1] = std::min(lo[1], v.py);
            lo[2] = std::min(lo[2], v.pz);
            hi[0] = std::max(hi[0], v.px);
            hi[1] = std::max(hi[1], v.py);
            hi[2] = std::max(hi[2], v.pz);
         }
         const float cx = 0.5f * (lo[0] + hi[0]);
         const float cy = 0.5f * (lo[1] + hi[1]);
         const float cz = 0.5f * (lo[2] + hi[2]);
         const float extent = std::max({ hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1.0f });

         Render3DPathTrace foxPt;
         const Render3DPathTrace::PtMaterial foxMat =
            Render3DPathTrace::PtMaterial::FromLinear(0.8f, 0.6f, 0.4f, 0.5f, 0.0f);
         Bvh::Instance foxInst;
         foxInst.blasIndex = 0;
         foxInst.materialId = 0;
         foxInst.transform = Mat4::Identity();
         std::vector<Mesh> foxMeshes = { foxMesh };
         if (foxPt.UploadScene(foxMeshes, { foxInst }, { foxMat }))
         {
            const float foxEye[3] = { cx + extent * 1.2f, cy + extent * 0.4f, cz + extent * 1.2f };
            const float foxTarget[3] = { cx, cy, cz };
            const float foxUp[3] = { 0.0f, 1.0f, 0.0f };
            foxPt.SetCameraLookAt(foxEye, foxTarget, foxUp, 40.0f, 1.0f);
            foxPt.SetLights(lights);
            foxPt.SetWorldColorLinear(0.05f, 0.05f, 0.06f);
            foxPt.SetTonemap(0);
            foxPt.SetExposure(1.0f);
            foxPt.maxBounces = 4;
            const double foxMs = BenchmarkMsPerSample(foxPt, 512, 512, 4, 32);
            std::printf("{\"scene\":\"fox\",\"resolution\":\"512x512\",\"tris\":%d,\"ms_per_sample\":%.3f}\n",
                        TotalTriangles(foxMeshes), foxMs);
         }
      }
   }

   if (!benchOnly)
      std::printf("PTTEST %s\n", allPassed ? "PASS" : "FAIL");
   return allPassed;
}
} // namespace app

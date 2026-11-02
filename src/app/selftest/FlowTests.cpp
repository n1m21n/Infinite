// 3D flow self-test (INFINITE_3DFLOWTEST=a.glb,b.glb,...): the chain an
// artist runs, on real models, as one patch: Model 3D -> Sculpt -> UV Project
// -> Bake AO -> Material -> Render 3D -> Output. Every stage must pass
// something on; a skinned model is posed and the sculpt must follow; the
// patch is saved, reopened and must come back the same. The saved patches
// (INFINITE_3DFLOWTEST_OUT, a folder) are left for `--frame` to render
// headless in a fresh process.
#include "app/AppShared.h"

#include <chrono>
#include <cmath>
#include <thread>

#include "nodes/BakeNode.h"
#include "nodes/ModelSourceNode.h"
#include "nodes/SculptNode.h"

namespace app
{
namespace
{
   // Mean of a texture's RGB over its texels with alpha, and how many.
   double TextureMean(unsigned int tex, int w, int h, int& covered)
   {
      covered = 0;
      if (tex == 0 || w <= 0 || h <= 0)
         return 0.0;
      std::vector<float> px((size_t)w * h * 4);
      glBindTexture(GL_TEXTURE_2D, tex);
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      double sum = 0.0;
      for (size_t i = 0; i < px.size(); i += 4)
         if (px[i + 3] > 0.5f)
         {
            sum += (px[i] + px[i + 1] + px[i + 2]) / 3.0;
            covered++;
         }
      return covered ? sum / covered : 0.0;
   }

   template <class T>
   T* Find(const char* type)
   {
      for (GraphNode& g : gNodes)
         if (g.typeName == type)
            return dynamic_cast<T*>(g.node.get());
      return nullptr;
   }

   // Cooks the chain in order; Bake runs on a worker, so wait it out.
   void Cook(int& frame)
   {
      for (int tries = 0; tries < 400; tries++)
      {
         frame++;
         for (GraphNode& g : gNodes)
            g.node->CookIfNeeded(frame);
         BakeNode* bake = Find<BakeNode>("Bake");
         if (bake == nullptr || !bake->Baking())
            break;
         std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      frame++;
      for (GraphNode& g : gNodes)
         g.node->CookIfNeeded(frame);
   }

   // A few Draw dabs across the front, as Sculpt mode lays them.
   void Sculpt3(SculptNode* n)
   {
      const std::vector<float>& pos = n->PointPos();
      float lo = 1e30f, hi = -1e30f, front = -1e30f;
      for (size_t i = 0; i + 2 < pos.size(); i += 3)
      {
         lo = std::min(lo, pos[i + 1]);
         hi = std::max(hi, pos[i + 1]);
         front = std::max(front, pos[i + 2]);
      }
      std::vector<unsigned int> touched;
      for (int k = 0; k < 3; k++)
      {
         const float o[3] = { (hi - lo) * (-0.1f + 0.1f * k), (lo + hi) * 0.5f, front + (hi - lo) };
         const float d[3] = { 0, 0, -1 };
         float t;
         if (!n->Raycast(o, d, t))
            continue;
         Sculpt::Dab dab;
         dab.center[0] = o[0];
         dab.center[1] = o[1];
         dab.center[2] = o[2] - t;
         dab.radius = (hi - lo) * 0.15f;
         dab.strength = 1.0f;
         touched.clear();
         Sculpt::Stroke(dab, n->PointPos(), n->PointNrm(), n->Topology(), 1.0f, n->Offsets(), &touched,
                        n->PointGrid(dab.radius));
         n->OffsetsChanged(&touched);
      }
   }

   float MaxMove(const Mesh& a, const Mesh& b)
   {
      if (a.vertices.size() != b.vertices.size())
         return -1.0f;
      float m = 0.0f;
      for (size_t i = 0; i < a.vertices.size(); i++)
         m = std::max({ m, std::fabs(a.vertices[i].px - b.vertices[i].px), std::fabs(a.vertices[i].py - b.vertices[i].py),
                        std::fabs(a.vertices[i].pz - b.vertices[i].pz) });
      return m;
   }
}

// ======================================================== INFINITE_3DFLOWTEST
bool RunThreeDFlowTest()
{
   printf("[3D FLOW TEST] Model 3D -> Sculpt -> UV Project -> Bake -> Material -> Render 3D\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };
   const char* outDir = getenv("INFINITE_3DFLOWTEST_OUT");
   const std::string list = getenv("INFINITE_3DFLOWTEST");
   size_t start = 0;
   int frame = 1000;
   while (start < list.size())
   {
      const size_t comma = list.find(',', start);
      const std::string path = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      start = comma == std::string::npos ? list.size() : comma + 1;
      if (path.empty() || path == "1")
         continue;
      const size_t slash = path.find_last_of('/');
      const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
      printf(" %s\n", name.c_str());

      NewPatch();
      const int m = SpawnNode("Model 3D", "3D", 0, 0)->index;
      const int s = SpawnNode("Sculpt", "3D", 320, 0)->index;
      const int uv = SpawnNode("UV Project", "3D", 640, 0)->index;
      const int b = SpawnNode("Bake", "3D", 960, 260)->index;
      const int mat = SpawnNode("Material", "3D", 960, 0)->index;
      const int r = SpawnNode("Render 3D", "3D", 1280, 0)->index;
      const int out = SpawnNode("Output", "Utility", 1600, 0)->index;
      std::string error;
      const bool wired = ConnectNodes(m, 0, s, 0, error) && ConnectNodes(s, 0, uv, 0, error) &&
                         ConnectNodes(uv, 0, b, 0, error) && ConnectNodes(uv, 0, mat, 0, error) &&
                         ConnectNodes(b, 0, mat, 5, error) && ConnectNodes(mat, 0, r, 0, error) &&
                         ConnectNodes(r, 0, out, 0, error);
      check(wired, ("wired " + error).c_str(), wired, 1);
      auto* model = Find<ModelSourceNode>("Model 3D");
      auto* sculpt = Find<SculptNode>("Sculpt");
      auto* bake = Find<BakeNode>("Bake");
      auto* render = Find<Render3DNode>("Render 3D");
      auto* uvGeo = Find<IGeometrySource>("UV Project");
      auto* matGeo = Find<IGeometrySource>("Material");
      const bool typed = model && sculpt && bake && render && uvGeo && matGeo;
      check(typed, "every node is the type it should be", (double)(!!model + !!sculpt + !!bake + !!render),
            (double)(!!uvGeo + !!matGeo));
      if (!wired || !typed)
         continue;
      check(model->Load(path), "loads", 1, 1);
      model->clipSpeed = 0.0f;   // a still pose, so frames compare
      sculpt->level = 1;
      Cook(frame);
      const size_t modelTris = model->GetMesh().FaceCount();
      check(modelTris > 0, "Model 3D has triangles", (double)modelTris, 0);

      Sculpt3(sculpt);
      Cook(frame);
      check(sculpt->HasSculpt(), "Sculpt holds the strokes", sculpt->HasSculpt(), 1);
      const Mesh sculpted = sculpt->GetMesh();
      check(sculpted.FaceCount() == modelTris * 4, "Sculpt level 1 makes 4x the faces", (double)sculpted.FaceCount(),
            (double)modelTris * 4);

      const Mesh& uvMesh = uvGeo->GetMesh();
      float ulo = 1e9f, uhi = -1e9f;
      for (const Vertex& v : uvMesh.vertices)
      {
         ulo = std::min({ ulo, v.u, v.v });
         uhi = std::max({ uhi, v.u, v.v });
      }
      check(uvMesh.FaceCount() == sculpted.FaceCount() && uhi - ulo > 0.5f, "UV Project spreads UVs over the sculpt",
            uhi - ulo, (double)uvMesh.FaceCount());
      int covered = 0;
      const double ao = TextureMean(bake->GetOutputTexture(), bake->GetOutputWidth(), bake->GetOutputHeight(), covered);
      check(covered > 1000 && ao > 0.2, "Bake AO is not black", ao, covered);
      check(matGeo->GetMesh().FaceCount() == uvMesh.FaceCount(), "Material passes the mesh on",
            (double)matGeo->GetMesh().FaceCount(), (double)uvMesh.FaceCount());
      const double luma = TextureMean(render->GetOutputTexture(), render->GetOutputWidth(), render->GetOutputHeight(),
                                      covered);
      check(render->LastTriangleCount() >= uvMesh.FaceCount() && luma > 0.02, "Render 3D draws it, not black", luma,
            (double)render->LastTriangleCount());

      // A skinned model in a pose: the sculpt rides the posed mesh.
      if (model->Poseable())
      {
         const Mesh before = sculpt->GetMesh();
         model->pose.poseJoint[0] = model->JointCount() / 2;
         model->pose.poseRot[0][0] = 40.0f;
         Cook(frame);
         // One frame's cook, as the app runs it: Bake re-bakes on a worker
         // and is not waited on.
         model->pose.poseRot[0][0] = 41.0f;
         frame++;
         double posedMs = 0.0;
         // Geometry is pulled lazily, so time each stage's mesh in chain
         // order; the cook below then shows what is left.
         IGeometrySource* stages[] = { model, sculpt, uvGeo, matGeo };
         const char* names[] = { "Model 3D", "Sculpt", "UV Project", "Material" };
         for (int k = 0; k < 4; k++)
         {
            const auto t0 = std::chrono::steady_clock::now();
            stages[k]->GetMesh();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            posedMs += ms;
            printf("    %s mesh %.1f ms\n", names[k], ms);
         }
         for (GraphNode& g : gNodes)
         {
            const auto t0 = std::chrono::steady_clock::now();
            g.node->CookIfNeeded(frame);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            posedMs += ms;
            if (ms > 1.0)
               printf("    %s %.1f ms\n", g.typeName.c_str(), ms);
         }
         Cook(frame);
         const Mesh& after = sculpt->GetMesh();
         check(after.vertices.size() == before.vertices.size() && MaxMove(before, after) > 1e-3f,
               "pose moves the sculpted mesh, same points", MaxMove(before, after), (double)after.vertices.size());
         check(sculpt->HasSculpt() && sculpt->CookWarning().empty(), "the sculpt survives the pose", sculpt->HasSculpt(),
               0);
         check(posedMs < 25.0, "a pose change re-cooks the chain in < 25 ms", posedMs, 25);
      }

      // Several materials (parts): another part flows through the same chain.
      // The sculpt no longer fits it, so Sculpt says so and passes it on.
      if (model->PartCount() > 1)
      {
         const int first = model->part;
         const size_t firstDrawn = render->LastTriangleCount();
         model->part = model->PartCount() - 1;
         Cook(frame);
         const size_t other = render->LastTriangleCount();
         check(other > 0 && other != firstDrawn && !sculpt->CookWarning().empty(),
               "another part flows through, the sculpt warns it does not fit", (double)other,
               (double)model->PartCount());
         model->part = first;
         Cook(frame);
      }

      // Save, reopen: the same patch comes back.
      const std::string saved = std::string(outDir ? outDir : "/tmp") + "/" + name + ".inf";
      check(SavePatchTo(saved), "saves", 1, 1);
      const size_t sculptTris = sculpt->GetMesh().FaceCount();
      const std::vector<float> offsets = sculpt->Offsets();
      const size_t drawn = render->LastTriangleCount();
      NewPatch();
      check(LoadPatchFrom(saved), "reopens", 1, 1);
      Cook(frame);
      auto* sculpt2 = Find<SculptNode>("Sculpt");
      auto* render2 = Find<Render3DNode>("Render 3D");
      float drift = -1.0f;
      if (sculpt2 && sculpt2->Offsets().size() == offsets.size())
      {
         drift = 0.0f;
         for (size_t i = 0; i < offsets.size(); i++)
            drift = std::max(drift, std::fabs(offsets[i] - sculpt2->Offsets()[i]));
      }
      check(drift >= 0.0f && drift < 1e-3f, "reopened sculpt matches", drift, (double)offsets.size());
      check(sculpt2 && sculpt2->GetMesh().FaceCount() == sculptTris, "reopened mesh has the same faces",
            sculpt2 ? (double)sculpt2->GetMesh().FaceCount() : 0, (double)sculptTris);
      check(render2 && render2->LastTriangleCount() == drawn, "reopened render draws the same",
            render2 ? (double)render2->LastTriangleCount() : 0, (double)drawn);
      printf("  saved %s\n", saved.c_str());
   }
   NewPatch();
   printf("3D FLOW TEST: %s (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
   return failures == 0;
}
}

// Rigging self-test: a two-bone skinned glTF (tools/make-skinned-gltf.py)
// posed through Model 3D, vertices checked against hand-worked positions.
#include "app/AppShared.h"

#include <cmath>

#include "core/Skinning.h"
#include "nodes/ArmatureNode.h"
#include "nodes/ModelSourceNode.h"

namespace app
{
// ======================================================== INFINITE_RIGTEST
// Asserts: the file loads with 2 joints, 2 clips and a morph; the rest pose
// skins back onto itself (inverse binds right); half of "bend" turns the top
// ring 45 degrees about the tip joint; half of "twist" turns the base about
// Y; the "bulge" morph moves only the top ring; a paused transport does not
// re-pose; a pose slot turns a joint on top of the clip (Pose mode's
// store) and joint matrices come out in fitted mesh space; Euler <-> quaternion
// round-trips. The unit-box fit is centre (0, 1, 0), scale 0.5.
bool RunRigTest()
{
   printf("[RIG TEST] skinned glTF: joints, clips, morphs\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };

   const char* env = getenv("INFINITE_RIGTEST_FILE");
   // The app's working directory is not the checkout, so the default is
   // found from this source file's own path.
   std::string path = env != nullptr ? env : "";
#ifdef INFINITE_SOURCE_DIR
   // __FILE__ is relative to the build dir under some generators ("../src/..."), so CMake passes the root.
   if (path.empty())
      path = std::string(INFINITE_SOURCE_DIR) + "/tests/render3d/skinned_two_bone.glb";
#endif
   if (path.empty())
   {
      const std::string self = __FILE__;
      const size_t at = self.rfind("src/app/selftest/");
      path = (at == std::string::npos ? std::string() : self.substr(0, at)) + "tests/render3d/skinned_two_bone.glb";
   }
   ModelSourceNode n;
   const bool loaded = n.Load(path);
   check(loaded, n.Status().c_str(), loaded, 1);
   if (!loaded)
   {
      printf("RIGTEST FAIL\n");
      return false;
   }
   check(n.JointCount() == 3 && n.ClipNames().size() == 3 && n.MorphCount() == 1,
         "3 joints, 2 clips, 1 morph", n.JointCount(), (double)n.ClipNames().size() - 1);
   check(n.clip == 1, "a new file starts on its first clip", n.clip, 1);

   auto near3 = [](const Vertex& v, float x, float y, float z)
   { return std::fabs(v.px - x) < 1e-3f && std::fabs(v.py - y) < 1e-3f && std::fabs(v.pz - z) < 1e-3f; };
   const int kTop = 16, kBase = 0;

   n.clip = 0;
   const Mesh* m = &n.GetMesh();
   check(near3(m->vertices[kTop], 0.1f, 0.5f, 0.1f) && near3(m->vertices[kBase], 0.1f, -0.5f, 0.1f),
         "rest pose skins onto itself", m->vertices[kTop].px, m->vertices[kTop].py);

   // Speed 0 holds the clip at its phase whatever the transport does.
   n.clip = 1; n.clipSpeed = 0.0f; n.clipPhase = 0.5f;
   m = &n.GetMesh();
   const float c = std::sqrt(0.5f);
   check(near3(m->vertices[kTop], -0.8f * c * 0.5f, 1.2f * c * 0.5f, 0.1f) && near3(m->vertices[kBase], 0.1f, -0.5f, 0.1f),
         "bend at half turns the top ring 45 degrees", m->vertices[kTop].px, m->vertices[kTop].py);

   n.clip = 2;
   m = &n.GetMesh();
   check(near3(m->vertices[kBase], 0.4f * c * 0.5f, -0.5f, 0.0f), "twist at half turns the base about Y",
         m->vertices[kBase].px, m->vertices[kBase].pz);

   n.clip = 0; n.clipPhase = 0.0f; n.morph[0] = 1.0f;
   m = &n.GetMesh();
   check(near3(m->vertices[kTop], 0.15f, 0.5f, 0.15f) && near3(m->vertices[kTop - 4], 0.1f, 0.25f, 0.1f),
         "bulge moves the top ring only", m->vertices[kTop].px, m->vertices[kTop - 4].px);

   n.morph[0] = 0.0f;
   n.clip = 0;
   const int slot = n.pose.Slot(1, true);
   n.pose.poseRot[slot][2] = 90.0f;
   m = &n.GetMesh();
   check(slot == 0 && near3(m->vertices[kTop], -0.5f, 0.1f, 0.1f), "a pose slot turns the tip 90 degrees",
         m->vertices[kTop].px, m->vertices[kTop].py);
   float jm[16];
   check(n.JointMatrix(1, jm) && std::fabs(jm[12]) < 1e-4f && std::fabs(jm[13]) < 1e-4f && std::fabs(jm[1] - 0.5f) < 1e-3f,
         "tip joint sits at the fitted origin, turned and scaled", jm[13], jm[1]);
   n.clip = 1; n.clipPhase = 0.5f; n.pose.poseRot[slot][2] = 45.0f;
   m = &n.GetMesh();
   check(near3(m->vertices[kTop], -0.5f, 0.1f, 0.1f), "pose adds on top of the clip (45 + 45)",
         m->vertices[kTop].px, m->vertices[kTop].py);
   n.pose.poseJoint[slot] = -1; n.pose.poseRot[slot][2] = 0.0f; n.clipPhase = 0.0f;
   {
      const float e[3] = { 10.0f, -35.0f, 120.0f };
      float q[4], back[3];
      Skinning::QuatFromEuler(e, q);
      Skinning::EulerFromQuat(q, back);
      check(std::fabs(back[0] - e[0]) < 1e-2f && std::fabs(back[1] - e[1]) < 1e-2f && std::fabs(back[2] - e[2]) < 1e-2f,
            "Euler round-trips through a quaternion", back[1], back[2]);
   }

   // IK. Fitted space halves the file and moves y = 1 to 0, so root is at
   // (0, -0.5, 0), tip at the origin and end at (0, 0.5, 0); bones are 0.5.
   n.clip = 0;
   auto head = [&](int j, float x, float y, float z)
   {
      float w[16];
      n.GetMesh();
      return n.JointMatrix(j, w) && std::fabs(w[12] - x) < 2e-3f && std::fabs(w[13] - y) < 2e-3f && std::fabs(w[14] - z) < 2e-3f;
   };
   n.pose.ikOn = true; n.pose.ikJoint = 1; n.pose.ikChain = 1;
   n.pose.ikTarget[0] = 0.5f; n.pose.ikTarget[1] = -0.5f; n.pose.ikTarget[2] = 0.0f;
   m = &n.GetMesh();
   check(head(1, 0.5f, -0.5f, 0.0f) && near3(m->vertices[kTop], 1.0f, -0.6f, 0.1f),
         "one-bone IK swings the column to the target", m->vertices[kTop].px, m->vertices[kTop].py);
   n.pose.ikTarget[0] = 3.0f;
   check(head(1, 0.5f, -0.5f, 0.0f), "out of reach, the bone points at the target", 0, 0);
   n.pose.ikJoint = 2; n.pose.ikChain = 2;
   n.pose.ikTarget[0] = 0.5f; n.pose.ikTarget[1] = 0.0f;
   float tipX[2] = { 0, 0 };
   for (int side = 0; side < 2; side++)
   {
      n.pose.ikPoleOn = true;
      n.pose.ikPole[0] = side == 0 ? 2.0f : -2.0f; n.pose.ikPole[1] = 0.0f; n.pose.ikPole[2] = 0.0f;
      n.GetMesh();
      float w[16];
      n.JointMatrix(1, w);
      tipX[side] = w[12];
      check(head(2, 0.5f, 0.0f, 0.0f), side == 0 ? "two-bone IK reaches (pole +X)" : "two-bone IK reaches (pole -X)",
            w[12], w[13]);
   }
   check(tipX[0] > 0.05f && tipX[1] < tipX[0] - 0.05f, "the pole picks the elbow's side", tipX[0], tipX[1]);
   n.pose.ikPoleOn = false;
   n.pose.ikWeight = 0.0f;
   check(head(2, 0.0f, 0.5f, 0.0f), "IK weight 0 leaves the pose alone", 0, 0);
   n.pose.ikOn = false; n.pose.ikWeight = 1.0f;

   n.clip = 1; n.clipSpeed = 1.0f;
   Transport::Instance().SetPlaying(false);
   const unsigned long long r0 = n.MeshRevision();
   const unsigned long long r1 = n.MeshRevision();
   check(r0 == r1, "a paused transport does not re-pose", (double)r0, (double)r1);

   printf(failures == 0 ? "RIGTEST OK\n" : "RIGTEST FAIL (%d)\n", failures);
   return failures == 0;
}
// ======================================================= INFINITE_ARMTEST
// Armature on a 2-unit column (x = +-0.1, y = -1..1) with 4 bones of 0.5.
// Asserts: 5 joints (4 bones + tip); no curl leaves the mesh alone; curl 90
// turns each inner joint 30 degrees so the top lands on the arc; the base
// stays; a cloud's points and velocities turn too and a curve bends;
// a path curve lays the bones along it; IK takes the tip to a target.
namespace
{
   struct ArmTestSource : IGeometrySource
   {
      Mesh mesh;
      std::vector<Particle> cloud;
      Polyline curve;
      bool hasCloud = false, hasCurve = false;
      const Mesh& GetMesh() override { return mesh; }
      unsigned long long MeshRevision() override { return 1; }
      Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
      Material GetMaterial() const override { return Material(); }
      const std::vector<Particle>* GetPointCloud() override { return hasCloud ? &cloud : nullptr; }
      unsigned long long PointCloudRevision() override { return hasCloud ? 1 : 0; }
      const Polyline* GetCurve() override { return hasCurve ? &curve : nullptr; }
      unsigned long long CurveStamp() override { return hasCurve ? 1 : 0; }
   };
}

bool RunArmatureTest()
{
   printf("[ARM TEST] Armature: bones, envelope weights, curl, IK\n");
   int failures = 0;
   auto check = [&](bool ok, const char* what, double a, double b)
   {
      printf("  [%s] %s (%.4f / %.4f)\n", ok ? "PASS" : "FAIL", what, a, b);
      if (!ok)
         failures++;
   };
   auto near = [](float a, float b, float tol = 1e-3f) { return std::fabs(a - b) < tol; };

   ArmTestSource col;
   for (int i = 0; i <= 8; i++)
      for (int side = 0; side < 2; side++)
      {
         Vertex v;
         v.px = side == 0 ? 0.1f : -0.1f;
         v.py = -1.0f + 0.25f * (float)i;
         v.nx = side == 0 ? 1.0f : -1.0f; v.nz = 0.0f;
         col.mesh.vertices.push_back(v);
      }
   const int kTop = 16, kBase = 0;
   ArmatureNode n;
   n.input = &col;
   n.bones = 4;
   const Mesh* m = &n.GetMesh();
   check(n.JointCount() == 5 && n.JointName(4) == "tip", "4 bones and a tip", n.JointCount(), 5);
   bool same = m->vertices.size() == col.mesh.vertices.size();
   for (size_t i = 0; same && i < m->vertices.size(); i++)
      same = near(m->vertices[i].px, col.mesh.vertices[i].px) && near(m->vertices[i].py, col.mesh.vertices[i].py) &&
             near(m->vertices[i].pz, col.mesh.vertices[i].pz);
   check(same, "no curl leaves the mesh alone", m->vertices[kTop].py, 1.0);

   n.curl = 90.0f;
   m = &n.GetMesh();
   const float d = 3.14159265f / 180.0f;
   const float topY = -0.5f + 0.5f * (std::cos(30 * d) + std::cos(60 * d) + std::cos(90 * d));
   const float topZ = 0.5f * (std::sin(30 * d) + std::sin(60 * d) + std::sin(90 * d));
   check(near(m->vertices[kTop].px, 0.1f) && near(m->vertices[kTop].py, topY) && near(m->vertices[kTop].pz, topZ),
         "curl 90 puts the top on the arc", m->vertices[kTop].py, m->vertices[kTop].pz);
   check(near(m->vertices[kBase].py, -1.0f) && near(m->vertices[kBase].pz, 0.0f), "the base stays", m->vertices[kBase].py, -1.0);
   check(near(m->vertices[kTop].nx, 1.0f), "normals across the bend axis keep", m->vertices[kTop].nx, 1.0);

   n.curl = 0.0f; n.waveAmount = 10.0f; n.waveSpeed = 0.0f; n.wavePhase = 0.25f;
   m = &n.GetMesh();
   check(std::fabs(m->vertices[kTop].pz) > 0.05f, "a held wave bends the column", m->vertices[kTop].pz, 0.05);
   n.waveAmount = 0.0f; n.wavePhase = 0.0f;

   // A cloud and a curve beside the mesh: all three bend together.
   col.hasCloud = true;
   Particle top;
   top.px = 0.1f; top.py = 1.0f; top.vy = 1.0f; top.ny = 1.0f;
   col.cloud = { top };
   col.hasCurve = true;
   col.curve.points = { 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
   n.curl = 90.0f;
   const std::vector<Particle>* cloud = n.GetPointCloud();
   check(cloud != nullptr && near((*cloud)[0].py, topY) && near((*cloud)[0].vz, 1.0f) && near((*cloud)[0].nz, 1.0f),
         "a cloud's points, velocities and normals turn", cloud ? (*cloud)[0].vz : 0, 1.0);
   const Polyline* line = n.GetCurve();
   check(line != nullptr && near(line->points[4], topY) && near(line->points[5], topZ) && near(line->points[1], -1.0f),
         "a curve bends", line ? line->points[5] : 0, topZ);
   col.hasCloud = col.hasCurve = false;
   n.curl = 0.0f;

   // Along a path: the bones lie on the curve whatever shape comes in.
   ArmTestSource route;
   route.hasCurve = true;
   route.curve.points = { -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
   n.path = &route;
   float jm[16];
   check(n.JointMatrix(1, jm) && near(jm[12], -0.5f) && near(jm[13], 0.0f), "a path lays the bones along it", jm[12], jm[13]);
   m = &n.GetMesh();
   check(near(m->vertices[kTop].py, 1.0f), "no curl on a path still leaves the mesh alone", m->vertices[kTop].py, 1.0);
   n.path = nullptr;

   // IK: the tip reaches (1, 0, 0) with the whole chain (2 long, 1.41 away).
   n.pose.ikOn = true; n.pose.ikJoint = 4; n.pose.ikChain = 4;
   n.pose.ikTarget[0] = 1.0f; n.pose.ikTarget[1] = 0.0f; n.pose.ikTarget[2] = 0.0f;
   n.GetMesh();
   check(n.JointMatrix(4, jm) && near(jm[12], 1.0f, 1e-2f) && near(jm[13], 0.0f, 1e-2f), "IK takes the tip to the target",
         jm[12], jm[13]);
   n.pose.ikOn = false;

   const int slot = n.pose.Slot(2, true);
   n.pose.poseRot[slot][0] = 90.0f;
   m = &n.GetMesh();
   check(near(m->vertices[kTop].pz, 1.0f) && near(m->vertices[kTop].py, 0.0f), "a pose slot turns a bone",
         m->vertices[kTop].py, m->vertices[kTop].pz);

   // Weight paint: the base points belong to bone 1 only; painting bone 3
   // onto them makes them follow its turn, and the paint saves and loads.
   const float baseZ = m->vertices[kBase].pz;
   std::vector<std::pair<unsigned int, float>> hits = { { (unsigned)kBase, 1.0f }, { (unsigned)kBase + 1, 1.0f } };
   n.Paint(hits, 2, 1.0f);
   m = &n.GetMesh();
   check(n.HasPaint() && std::fabs(m->vertices[kBase].pz - baseZ) > 0.1f, "painted points follow the painted bone",
         m->vertices[kBase].pz, baseZ);
   const float paintedZ = m->vertices[kBase].pz;
   n.paintBone = 2;
   m = &n.GetMesh();
   check(m->vertexColor.size() == m->vertices.size() * 3 && m->vertexColor[kBase * 3 + 1] > 0.9f &&
         m->vertexColor[kTop * 3 + 2] > 0.5f, "the tint shows the bone's weight", m->vertexColor.empty() ? 0 : m->vertexColor[kBase * 3], 0);
   n.paintBone = -1;
   ParamSnapshot saved;
   n.VisitParams(saved);
   struct Load : ParamVisitor
   {
      std::string weights;
      void Float(const char*, float&) override {}
      void Int(const char*, int&) override {}
      void Bool(const char*, bool&) override {}
      void Color(const char*, float*) override {}
      void Text(const char* name, std::string& v) override { if (std::string(name) == "weights") v = weights; }
   } load;
   load.weights = saved.texts.empty() ? std::string() : saved.texts.back();
   ArmatureNode copy;
   copy.input = &col;
   copy.bones = 4;
   copy.pose = n.pose;
   copy.VisitParams(load);
   const Mesh& cm = copy.GetMesh();
   check(!load.weights.empty() && near(cm.vertices[kBase].pz, paintedZ, 1e-3f), "the paint saves and loads",
         cm.vertices[kBase].pz, paintedZ);
   n.bones = 5;
   n.GetMesh();
   check(!n.Paintable() && n.CookWarning().rfind("weight paint", 0) == 0, "another bone count warns and paints nothing",
         n.Paintable(), 0);
   n.bones = 4;
   n.ClearPaint();
   m = &n.GetMesh();
   check(!n.HasPaint() && near(m->vertices[kBase].pz, baseZ) && n.CookWarning().empty(), "clearing brings the envelope back",
         m->vertices[kBase].pz, baseZ);

   printf(failures == 0 ? "ARMTEST OK\n" : "ARMTEST FAIL (%d)\n", failures);
   return failures == 0;
}
}

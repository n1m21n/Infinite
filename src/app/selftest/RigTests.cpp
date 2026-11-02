// Rigging self-test: a two-bone skinned glTF (tools/make-skinned-gltf.py)
// posed through Model 3D, vertices checked against hand-worked positions.
#include "app/AppShared.h"

#include <cmath>

#include "nodes/ModelSourceNode.h"

namespace app
{
// ======================================================== INFINITE_RIGTEST
// Asserts: the file loads with 2 joints, 2 clips and a morph; the rest pose
// skins back onto itself (inverse binds right); half of "bend" turns the top
// ring 45 degrees about the tip joint; half of "twist" turns the base about
// Y; the "bulge" morph moves only the top ring; a paused transport does not
// re-pose. The unit-box fit is centre (0, 1, 0), scale 0.5.
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
   check(n.JointCount() == 2 && n.ClipNames().size() == 3 && n.MorphCount() == 1,
         "2 joints, 2 clips, 1 morph", n.JointCount(), (double)n.ClipNames().size() - 1);
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

   n.morph[0] = 0.0f; n.clip = 1; n.clipSpeed = 1.0f;
   Transport::Instance().SetPlaying(false);
   const unsigned long long r0 = n.MeshRevision();
   const unsigned long long r1 = n.MeshRevision();
   check(r0 == r1, "a paused transport does not re-pose", (double)r0, (double)r1);

   printf(failures == 0 ? "RIGTEST OK\n" : "RIGTEST FAIL (%d)\n", failures);
   return failures == 0;
}
}

// Render 3D's path trace engine: feeds the scene Render 3D already gathered (its draw list, the camera
// and lights of its last raster cook, the environment) into Render3DPathTrace and puts the traced image
// in mColorTex. Raster keeps the frame while anything moves; the tracer refines once the view is still.
#include "Geometry3DNodes.h"
#include "Render3DPathTrace.h"
#include "ColorSpace.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "Transport.h"
#include "GeometryOpNodes.h"
#include "EnvironmentNode.h"
#include "UtilityNodes.h"

namespace
{
   // One stashed light: type, vec[3], aim[3], colour (sRGB)[3], power, spot angle, penumbra,
   // area w, area h, falloff k (< 0 hard), cutoff.
   constexpr int kPtLightFloats = 19;
}

namespace
{
   // The source's material with its maps, mapping, wrap and own box, as Render 3D binds them.
   Render3DPathTrace::PtMaterial PtMaterialWithMaps(IGeometrySource* source, const Mesh& mesh)
   {
      using PtMaterial = Render3DPathTrace::PtMaterial;
      const Material mat = source->GetMaterial();
      PtMaterial m = PtMaterial::FromMaterial(mat);
      static const int kSlotMap[PtMaterial::kMapSlots] = { kMapAlbedo,   kMapRoughness, kMapMetallic, kMapNormal,
                                                          kMapEmission, kMapClearcoat, kMapSheen,    kMapTransmission, kMapOpacity };
      for (int k = 0; k < PtMaterial::kMapSlots; k++)
         m.maps[k] = source->GetMaterialTexture(kSlotMap[k]);
      if (auto* matNode = dynamic_cast<MaterialNode*>(source))
         m.wrap = matNode->wrapMode;
      m.normalStrength = mat.normalStrength;
      m.normalBump = mat.normalBump;
      const MappingTransform mapping = source->GetMappingTransform();
      m.mapSpace = mapping.space;
      std::memcpy(m.mapScale, mapping.scale, sizeof(m.mapScale));
      std::memcpy(m.mapRotate, mapping.rotate, sizeof(m.mapRotate));
      std::memcpy(m.mapTranslate, mapping.translate, sizeof(m.mapTranslate));
      if (!mesh.vertices.empty())
      {
         for (int a = 0; a < 3; a++)
         {
            m.objLo[a] = 1e30f;
            m.objHi[a] = -1e30f;
         }
         for (const Vertex& v : mesh.vertices)
         {
            const float p[3] = { v.px, v.py, v.pz };
            for (int a = 0; a < 3; a++)
            {
               m.objLo[a] = std::min(m.objLo[a], p[a]);
               m.objHi[a] = std::max(m.objHi[a], p[a]);
            }
         }
      }
      return m;
   }
}

// What the BVH is built from: the drawn sources, their meshes, placement and materials.
bool Render3DNode::PtSameGeometry(const SceneSignature& a, const SceneSignature& b)
{
   if (a.hasGeom.size() != b.hasGeom.size())
      return false;
   for (size_t i = 0; i < a.hasGeom.size(); i++)
   {
      if (a.hasGeom[i] != b.hasGeom[i] || a.meshRev[i] != b.meshRev[i] || a.cloudRev[i] != b.cloudRev[i] ||
          a.curveRev[i] != b.curveRev[i] || a.instanceRev[i] != b.instanceRev[i] ||
          a.instanceCount[i] != b.instanceCount[i])
         return false;
      if (!a.hasGeom[i])
         continue;
      if (a.surfaceTexRev[i] != b.surfaceTexRev[i] ||
          std::memcmp(&a.mapping[i], &b.mapping[i], sizeof(MappingTransform)) != 0)
         return false;
      if (std::memcmp(&a.material[i], &b.material[i], sizeof(Material)) != 0 ||
          !(a.modelMatrix[i] == b.modelMatrix[i]) || !(a.instanceGroupMatrix[i] == b.instanceGroupMatrix[i]))
         return false;
   }
   return true;
}

// Everything else that changes a traced pixel: Render 3D's own params (camera, background, engine),
// a Camera node, Light nodes, a rig, the environment. A beat-synced orbit never holds still.
bool Render3DNode::PtSameView(const SceneSignature& a, const SceneSignature& b)
{
   if (a.animated || b.animated || a.rigRevision != b.rigRevision)
      return false;
   if (a.own != b.own || a.hasCamera != b.hasCamera || a.camera != b.camera)
      return false;
   for (int i = 0; i < kLightSlots; i++)
      if (a.hasLight[i] != b.hasLight[i] || a.light[i] != b.light[i])
         return false;
   return a.envConnected == b.envConnected && a.envRev == b.envRev && a.envRotation == b.envRotation &&
          a.envIntensity == b.envIntensity && a.envSun == b.envSun;
}

void Render3DNode::StashPathTraceLights(int count, const int* types, const float* dirs, const float* aims,
                                        const float* colsSrgb, const float* power, const float* spotAngle,
                                        const float* spotPenumbra, const float* area, const float* terms,
                                        const float* cutoff)
{
   mPtLights.assign((size_t)count * kPtLightFloats, 0.0f);
   for (int i = 0; i < count; i++)
   {
      float* d = &mPtLights[(size_t)i * kPtLightFloats];
      d[0] = (float)types[i];
      for (int k = 0; k < 3; k++)
      {
         d[1 + k] = dirs[i * 3 + k];
         d[4 + k] = aims[i * 3 + k];
         d[7 + k] = colsSrgb[i * 3 + k];
      }
      d[10] = power[i];
      d[11] = spotAngle[i];
      d[12] = spotPenumbra[i];
      d[13] = area[i * 2 + 0];
      d[14] = area[i * 2 + 1];
      d[15] = terms[i * 4 + 3];
      d[16] = cutoff[i];
      d[17] = terms[i * 4 + 0];
      d[18] = terms[i * 4 + 1];
   }
}

void Render3DNode::ReleasePathTrace()
{
   delete mPathTrace;
   mPathTrace = nullptr;
   mPtSceneBuilt = false;
   mPtHasLastSig = false;
   mPtStill = false;
   mHasSceneBuilt = false;   // mColorTex may hold a traced frame; raster draws its own next
}

void Render3DNode::UploadPtScene(Render3DPathTrace& pt)
{
   std::vector<Mesh> meshes;
   std::vector<Bvh::Instance> instances;
   std::vector<Render3DPathTrace::PtMaterial> materials;
   for (IGeometrySource* source : mDraw)
   {
      if (source == nullptr || source->GetGpuPoints() != nullptr || source->GetPointCloud() != nullptr)
         continue;   // points and sprites stay raster-only
      const Mesh& mesh = source->GetMesh();
      if (mesh.indices.size() < 3)
         continue;
      const int blas = (int)meshes.size();
      const int mat = (int)materials.size();
      meshes.push_back(mesh);
      materials.push_back(PtMaterialWithMaps(source, mesh));
      Bvh::Instance inst;
      inst.blasIndex = blas;
      inst.materialId = mat;
      if (auto* instancer = FindInstancer(source))
      {
         const Mat4 group = source->GetInstanceGroupMatrix();
         for (const Mat4& x : ResolveInstanceTransforms(source, instancer))
         {
            inst.transform = Mat4::Multiply(group, x);
            instances.push_back(inst);
         }
      }
      else
      {
         inst.transform = source->GetModelMatrix();
         instances.push_back(inst);
      }
   }
   pt.SetFieldTime((float)Transport::Instance().Seconds());
   pt.UploadScene(meshes, instances, materials);
}

void Render3DNode::SetPtWorld(Render3DPathTrace& pt)
{
   EnvironmentNode* env = envInput.IsConnected() ? dynamic_cast<EnvironmentNode*>(envInput.Resolved()) : nullptr;
   // Lights, in raster's units: radiance = linear colour x power x raster's own falloff, per hit.
   std::vector<Render3DPathTrace::PtLight> lights;
   for (size_t i = 0; i + kPtLightFloats <= mPtLights.size(); i += kPtLightFloats)
   {
      const float* d = &mPtLights[i];
      Render3DPathTrace::PtLight l;
      l.type = (int)d[0];
      std::memcpy(l.posOrDir, d + 1, sizeof(l.posOrDir));
      std::memcpy(l.aim, d + 4, sizeof(l.aim));
      const float scale = d[10];
      l.falloff = d[15];
      l.diffuse = d[17];
      l.specular = d[18];
      for (int k = 0; k < 3; k++)
         l.color[k] = ColorSpace::SrgbToLinear(std::max(0.0f, d[7 + k])) * scale;
      const float cosOuter = std::cos(d[11] * 3.14159265f / 180.0f);
      l.spotCosOuter = cosOuter;
      l.spotSmoothW = (1.0f - cosOuter) * d[12];
      l.areaW = std::max(0.001f, d[13]);
      l.areaH = std::max(0.001f, d[14]);
      l.cutoff = d[16];
      lights.push_back(l);
   }
   pt.SetLights(lights);

   // The world: the Environment node's HDRI, else Render 3D's gradient, at the intensities raster uses.
   unsigned int envTex = env != nullptr ? env->GetEnvironmentTexture() : 0;
   float sky[3], horizon[3], ground[3];
   for (int k = 0; k < 3; k++)
   {
      sky[k] = ColorSpace::SrgbToLinear(envSky[k]);
      horizon[k] = ColorSpace::SrgbToLinear(envHorizon[k]);
      ground[k] = ColorSpace::SrgbToLinear(envGround[k]);
   }
   const float hdri = env != nullptr ? env->intensity : 1.0f;
   const float rot = env != nullptr ? env->rotation * 3.14159265f / 180.0f : 0.0f;
   pt.SetEnvironment(envTex, rot, envIntensity * hdri, sky, horizon, ground);
   pt.SetBackground(envTex != 0 && envAsBackground, bgColor, bgOpacity);
}

bool Render3DNode::CookPathTrace(int frameId)
{
   const int w = GLUtil::TargetSide(width, 16);
   const int h = GLUtil::TargetSide(height, 16);
   const bool offline = Transport::Instance().IsOfflineMode();

   // The same gather raster does, so the signature sees this frame's sources.
   CollectDraw();
   for (IGeometrySource* source : mDraw)
      if (auto* node = dynamic_cast<INode*>(source))
         node->CookIfNeeded(frameId);
   EnvironmentNode* env = envInput.IsConnected() ? dynamic_cast<EnvironmentNode*>(envInput.Resolved()) : nullptr;
   if (env != nullptr)
      env->CookIfNeeded(frameId);

   SceneSignature sig = BuildSceneSignature();
   const bool moved = !mPtHasLastSig || !PtSameView(sig, mPtLastSig) || !PtSameGeometry(sig, mPtLastSig);
   mPtLastSig = sig;
   mPtHasLastSig = true;
   if (offline)
   {
      // An export frame is traced in full. The raster cook first sets this frame's camera and lights.
      CookOnce(frameId);
   }
   else if (moved)
   {
      mPtStill = false;
      return false;
   }
   if (mFbo == 0 || mColorTex == 0 || mWidth != w || mHeight != h || !mLastCamera.valid)
      return false;

   if (mPathTrace == nullptr)
      mPathTrace = new Render3DPathTrace();
   Render3DPathTrace& pt = *mPathTrace;

   if (offline || !mPtStill)
   {
      if (!mPtSceneBuilt || !PtSameGeometry(sig, mPtBuiltSig))
      {
         UploadPtScene(pt);
         mPtBuiltSig = sig;
         mPtSceneBuilt = true;
      }

      // Camera: the basis of the view raster drew, its rows (Mat4::LookAt), and the lens from the projection.
      const Mat4& view = mLastCamera.view;
      const Mat4& proj = mLastCamera.proj;
      const float right[3] = { view.m[0], view.m[4], view.m[8] };
      const float up[3] = { view.m[1], view.m[5], view.m[9] };
      const float fwd[3] = { -view.m[2], -view.m[6], -view.m[10] };
      const bool ortho = proj.m[11] == 0.0f;
      const float invFy = 1.0f / std::max(1e-6f, std::fabs(proj.m[5]));
      pt.SetCameraBasis(mLastCamera.eye, right, up, fwd, invFy, (float)w / (float)h, ortho ? invFy : 0.0f);
      // Depth of field as a real lens, the same blur raster's pass gives: its circle is dofAperture x 24 px
      // across a 1080 frame at |d - focus| / d, so the aperture radius is that share of the focus plane's
      // full height, 2 x focus x tan(fov / 2).
      // (24 px is the circle's diameter, Scene3DNode's thin-lens f-stop formula, so the radius is half the share.)
      const float lens = ortho || dofAperture <= 0.0f ? 0.0f : dofAperture * (24.0f / 1080.0f) * focusDistance * invFy;
      pt.SetLens(lens, focusDistance, dofBlades, dofRotation * 3.14159265f / 180.0f, dofRatio);

      SetPtWorld(pt);
      pt.SetExposure(exposure);
      pt.SetTonemap(tonemap);
      pt.maxBounces = std::clamp(ptBounces, 1, 16);
      pt.ResetAccumulation();
      mPtStill = true;
   }

   const int target = PathTraceSampleCount();
   if (offline)
      pt.RenderToSpp(w, h, target);
   else
   {
      pt.sampleBudgetPerFrame = 4;   // a small view fits several whole samples in the budget
      if (pt.StepFrame(w, h, target) == 0)
         return true;   // done: mColorTex already holds the finished image
   }

   GLint prevRead = 0, prevDraw = 0;
   glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
   glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
   const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
   glDisable(GL_SCISSOR_TEST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, pt.GetDisplayFbo());
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
   // Until the first sample is whole, only its traced rows go up: raster's frame stays under the rest.
   const int rows = pt.CoveredRows();
   glBlitFramebuffer(0, 0, w, rows, 0, 0, w, rows, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
   if (scissor)
      glEnable(GL_SCISSOR_TEST);
   mHasSceneBuilt = false;   // raster's cache no longer matches mColorTex
   mRevision = NextTextureRevision();
   return true;
}

void Render3DNode::ReleaseProbes()
{
   delete mProbeTrace;
   mProbeTrace = nullptr;
   if (mProbeTex != 0)
      glDeleteTextures(1, &mProbeTex);
   if (mProbeDepthTex != 0)
      glDeleteTextures(1, &mProbeDepthTex);
   mProbeTex = mProbeDepthTex = 0;
   mProbeSceneBuilt = false;
   mProbeValid = false;
   mProbeHasLastSig = false;
   mProbeKey.clear();
}

void Render3DNode::UpdateProbes(const SceneSignature& sig)
{
   const bool offline = Transport::Instance().IsOfflineMode();
   // What the probes see besides geometry: the lights and the world.
   std::vector<float> key = mPtLights;
   EnvironmentNode* env = envInput.IsConnected() ? dynamic_cast<EnvironmentNode*>(envInput.Resolved()) : nullptr;
   key.push_back(env != nullptr ? (float)env->GetEnvironmentTexture() : -1.0f);
   key.push_back(env != nullptr ? env->intensity : 1.0f);
   key.push_back(env != nullptr ? env->rotation : 0.0f);
   key.push_back(envIntensity);
   for (int k = 0; k < 3; k++)
   {
      key.push_back(envSky[k]);
      key.push_back(envHorizon[k]);
      key.push_back(envGround[k]);
   }
   const bool sameGeo = mProbeSceneBuilt && PtSameGeometry(sig, mProbeBuiltSig);
   if (mProbeValid && sameGeo && key == mProbeKey)
      return;
   // Live, wait until the scene holds still for a frame; keep the old probes meanwhile.
   const bool still = mProbeHasLastSig && PtSameGeometry(sig, mProbeLastSig);
   mProbeLastSig = sig;
   mProbeHasLastSig = true;
   if (!offline && !still && mProbeValid)
      return;

   // Bounds: every mesh's world box. A mesh far larger than the rest (a ground plane, a sky dome)
   // is clipped to the others' union grown by a quarter, so the grid stays on the subject.
   struct Box { float lo[3], hi[3]; };
   std::vector<Box> boxes;
   for (IGeometrySource* source : mDraw)
   {
      if (source == nullptr || source->GetGpuPoints() != nullptr || source->GetPointCloud() != nullptr)
         continue;
      const Mesh& mesh = source->GetMesh();
      if (mesh.indices.size() < 3 || mesh.vertices.empty())
         continue;
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      for (const Vertex& v : mesh.vertices)
      {
         const float p[3] = { v.px, v.py, v.pz };
         for (int a = 0; a < 3; a++)
         {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
         }
      }
      std::vector<Mat4> xs;
      if (auto* instancer = FindInstancer(source))
      {
         const Mat4 group = source->GetInstanceGroupMatrix();
         for (const Mat4& x : ResolveInstanceTransforms(source, instancer))
            xs.push_back(Mat4::Multiply(group, x));
      }
      else
         xs.push_back(source->GetModelMatrix());
      Box b = { { 1e30f, 1e30f, 1e30f }, { -1e30f, -1e30f, -1e30f } };
      for (const Mat4& M : xs)
         for (int c = 0; c < 8; c++)
         {
            const float p[3] = { (c & 1) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1], (c & 4) ? hi[2] : lo[2] };
            for (int a = 0; a < 3; a++)
            {
               const float q = M.m[a] * p[0] + M.m[4 + a] * p[1] + M.m[8 + a] * p[2] + M.m[12 + a];
               b.lo[a] = std::min(b.lo[a], q);
               b.hi[a] = std::max(b.hi[a], q);
            }
         }
      if (!xs.empty())
         boxes.push_back(b);
   }
   if (boxes.empty())
   {
      mProbeValid = false;
      return;
   }
   auto diag = [](const Box& b) {
      float s = 0.0f;
      for (int a = 0; a < 3; a++)
         s += (b.hi[a] - b.lo[a]) * (b.hi[a] - b.lo[a]);
      return std::sqrt(s);
   };
   std::vector<float> diags;
   for (const Box& b : boxes)
      diags.push_back(diag(b));
   std::vector<float> sorted = diags;
   std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
   const float median = sorted[sorted.size() / 2];
   Box core = { { 1e30f, 1e30f, 1e30f }, { -1e30f, -1e30f, -1e30f } }, all = core;
   bool haveCore = false;
   for (size_t i = 0; i < boxes.size(); i++)
      for (int a = 0; a < 3; a++)
      {
         all.lo[a] = std::min(all.lo[a], boxes[i].lo[a]);
         all.hi[a] = std::max(all.hi[a], boxes[i].hi[a]);
         if (diags[i] <= 3.0f * median)
         {
            core.lo[a] = std::min(core.lo[a], boxes[i].lo[a]);
            core.hi[a] = std::max(core.hi[a], boxes[i].hi[a]);
            haveCore = true;
         }
      }
   Box g = all;
   if (haveCore)
      for (int a = 0; a < 3; a++)
      {
         const float grow = 0.25f * (core.hi[a] - core.lo[a]) + 1e-3f;
         g.lo[a] = std::max(all.lo[a], core.lo[a] - grow);
         g.hi[a] = std::min(all.hi[a], core.hi[a] + grow);
      }

   // The grid reaches one cell past the subject on every side. Flush with the surface, the fade at the
   // grid's edge (probeIrr) lands on the object itself and shows as a lighter inset on every face.
   {
      float lg = 0.0f;
      for (int a = 0; a < 3; a++)
         lg = std::max(lg, g.hi[a] - g.lo[a]);
      const float pad = std::max(lg, 1e-3f) / 14.0f;
      for (int a = 0; a < 3; a++)
      {
         g.lo[a] -= pad;
         g.hi[a] += pad;
      }
   }
   // 16 probes along the longest side, at least 2 on each, at most 4096 in all.
   float ext[3], longest = 0.0f;
   for (int a = 0; a < 3; a++)
   {
      ext[a] = std::max(g.hi[a] - g.lo[a], 1e-3f);
      longest = std::max(longest, ext[a]);
   }
   float cell = longest / 15.0f;
   int dims[3];
   for (;;)
   {
      for (int a = 0; a < 3; a++)
         dims[a] = std::max(2, (int)std::ceil(ext[a] / cell) + 1);
      if (dims[0] * dims[1] * dims[2] <= 4096)
         break;
      cell *= 1.1f;
   }
   float step[3];
   for (int a = 0; a < 3; a++)
      step[a] = ext[a] / (float)(dims[a] - 1);

   if (mProbeTrace == nullptr)
      mProbeTrace = new Render3DPathTrace();
   Render3DPathTrace& pt = *mProbeTrace;
   if (!sameGeo)
   {
      UploadPtScene(pt);
      mProbeBuiltSig = sig;
      mProbeSceneBuilt = true;
   }
   SetPtWorld(pt);
   pt.SetBackground(false, bgColor, 1.0f);
   const float eye[3] = { 0, 0, 0 }, tgt[3] = { 0, 0, -1 }, up[3] = { 0, 1, 0 };
   pt.SetCameraLookAt(eye, tgt, up, 45.0f, 1.0f);
   pt.SetExposure(1.0f);
   pt.maxBounces = 3;

   GLint prevFbo = 0, prevVp[4], prevProg = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevVp);
   glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
   const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
   constexpr int kDirs = 128, kSpp = 4;
   std::vector<float> raw;
   const bool ok = pt.TraceProbes(g.lo, step, dims, kDirs, kSpp, raw);
   glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
   glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
   glUseProgram((GLuint)prevProg);
   if (scissor)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);
   mProbeKey = key;
   if (!ok)
   {
      mProbeValid = false;
      return;
   }

   // Each probe's radiance folded into six cosine-weighted axis faces (+x -x +y -y +z -z), as E / pi:
   // a uniform world of radiance L gives L on every face. Probes that see mostly back faces sit
   // inside a solid and fade out.
   const int count = dims[0] * dims[1] * dims[2];
   float dirs[kDirs][3];
   for (int d = 0; d < kDirs; d++)
   {
      const float z = 1.0f - 2.0f * ((float)d + 0.5f) / (float)kDirs;
      const float ph = 2.39996323f * ((float)d + 0.5f);
      const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
      dirs[d][0] = r * std::cos(ph);
      dirs[d][1] = r * std::sin(ph);
      dirs[d][2] = z;
   }
   const float wDir = 4.0f / (float)kDirs;   // (4 pi / N) / pi
   std::vector<uint16_t> tex((size_t)count * 6u * 4u), depth((size_t)count * 6u * 2u);
   auto half = [](float f) -> uint16_t {
      uint32_t x;
      std::memcpy(&x, &f, 4);
      const uint32_t sign = (x >> 16) & 0x8000u;
      int e = (int)((x >> 23) & 0xff) - 127 + 15;
      uint32_t mant = x & 0x7fffffu;
      if (e <= 0)
         return (uint16_t)sign;
      if (e >= 31)
         return (uint16_t)(sign | 0x7bffu);
      return (uint16_t)(sign | ((uint32_t)e << 10) | (mant >> 13));
   };
   // Depth stays below 200 so its square fits a half float.
   float diagLen = 0.0f;
   for (int a = 0; a < 3; a++)
      diagLen += ext[a] * ext[a];
   const float tCap = std::min(200.0f, 2.0f * std::sqrt(diagLen));
   for (int p = 0; p < count; p++)
   {
      float face[6][3] = {};
      float dm[6] = {}, dm2[6] = {}, dw[6] = {};
      float back = 0.0f;
      for (int d = 0; d < kDirs; d++)
      {
         const float* L = &raw[((size_t)d * (size_t)count + (size_t)p) * 4u];
         // Alpha = back-face share x 1e5 + mean first-hit distance; the share comes in steps of 1 / spp.
         const float bf = std::round(L[3] / 1e5f * (float)kSpp) / (float)kSpp;
         const float t = std::min(std::max(L[3] - bf * 1e5f, 0.0f), tCap);
         back += bf;
         for (int a = 0; a < 3; a++)
         {
            const float c = dirs[d][a];
            const int fi = a * 2 + (c >= 0.0f ? 0 : 1);
            const float w = std::fabs(c) * wDir;
            for (int k = 0; k < 3; k++)
               face[fi][k] += L[k] * w;
            const float c2 = c * c, wd = c2 * c2;   // a narrower lobe for depth
            dm[fi] += t * wd;
            dm2[fi] += t * t * wd;
            dw[fi] += wd;
         }
      }
      back /= (float)kDirs;
      const float valid = std::clamp((0.3f - back) / 0.2f, 0.0f, 1.0f);
      const int x = p % dims[0], y = (p / dims[0]) % dims[1], z = p / (dims[0] * dims[1]);
      for (int f = 0; f < 6; f++)
      {
         const size_t texel = ((size_t)(f * dims[2] + z) * (size_t)dims[1] + (size_t)y) * (size_t)dims[0] + (size_t)x;
         for (int k = 0; k < 3; k++)
            tex[texel * 4 + k] = half(face[f][k]);
         tex[texel * 4 + 3] = half(valid);
         const float iw = dw[f] > 0.0f ? 1.0f / dw[f] : 0.0f;
         depth[texel * 2 + 0] = half(dm[f] * iw);
         depth[texel * 2 + 1] = half(dm2[f] * iw);
      }
   }
   if (mProbeDepthTex == 0)
      glGenTextures(1, &mProbeDepthTex);
   GLint prevTex3 = 0;
   glGetIntegerv(GL_TEXTURE_BINDING_3D, &prevTex3);
   glBindTexture(GL_TEXTURE_3D, mProbeDepthTex);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage3D(GL_TEXTURE_3D, 0, GL_RG16F, dims[0], dims[1], dims[2] * 6, 0, GL_RG, GL_HALF_FLOAT, depth.data());
   glBindTexture(GL_TEXTURE_3D, (GLuint)prevTex3);
   if (mProbeTex == 0)
      glGenTextures(1, &mProbeTex);
   GLint prevTex = 0;
   glGetIntegerv(GL_TEXTURE_BINDING_3D, &prevTex);
   glBindTexture(GL_TEXTURE_3D, mProbeTex);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, dims[0], dims[1], dims[2] * 6, 0, GL_RGBA, GL_HALF_FLOAT, tex.data());
   glBindTexture(GL_TEXTURE_3D, (GLuint)prevTex);
   std::memcpy(mProbeLo, g.lo, sizeof(mProbeLo));
   std::memcpy(mProbeStep, step, sizeof(mProbeStep));
   std::memcpy(mProbeDims, dims, sizeof(mProbeDims));
   mProbeValid = true;
}

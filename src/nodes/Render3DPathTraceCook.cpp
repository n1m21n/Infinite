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
         pt.UploadScene(meshes, instances, materials);
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
      const float lens = ortho || dofAperture <= 0.0f ? 0.0f : dofAperture * (24.0f / 1080.0f) * 2.0f * focusDistance * invFy;
      pt.SetLens(lens, focusDistance, dofBlades, dofRotation * 3.14159265f / 180.0f, dofRatio);

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

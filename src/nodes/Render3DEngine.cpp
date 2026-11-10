// Render 3D through the Metal engine (src/engine): the same scene the GL raster draws, lit with ray-traced
// shadows and reflections and live GI. Opt-in per node (liveEngine); anything the engine cannot draw yet
// (HDRI maps, textures, glass, instancing, non-ACES tonemaps) falls back to the GL render by returning false
// only when the engine itself is unavailable - the unsupported features are simply not drawn.
#include "Geometry3DNodes.h"
#include "SceneNodes.h"

#include "gl3.h"
#include "engine/Scene.h"
#include "engine/rhi/Rhi.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

struct Render3DNode::EngineState
{
   std::unique_ptr<rhi::Device> dev;
   std::unique_ptr<rhi::RasterRenderer> raster;
   std::unique_ptr<rhi::SharedImage> img;
   std::vector<std::pair<const void*, unsigned long long>> meshSig;
   unsigned frame = 0;
   unsigned int readFbo = 0;
   bool failed = false;
   unsigned long long sceneHash = 0;
   int settled = 0;   // path-traced samples since the scene last changed
};

void Render3DNode::ReleaseEngine()
{
   if (mEng == nullptr)
      return;
   if (mEng->readFbo != 0)
      glDeleteFramebuffers(1, &mEng->readFbo);
   delete mEng;
   mEng = nullptr;
}

#if defined(__APPLE__)
namespace
{
   engine::MeshData ToMeshData(const Mesh& m)
   {
      engine::MeshData d;
      d.posNormal.reserve(m.vertices.size() * 6);
      for (const Vertex& v : m.vertices)
         d.posNormal.insert(d.posNormal.end(), { v.px, v.py, v.pz, v.nx, v.ny, v.nz });
      d.indices.assign(m.indices.begin(), m.indices.end());
      return d;
   }
}

bool Render3DNode::CookEngine(int frameId)
{
   if (mEng == nullptr)
      mEng = new EngineState();
   EngineState& E = *mEng;
   if (E.failed)
      return false;
   if (!E.dev)
   {
      E.dev = rhi::CreateDevice();
      E.raster = E.dev ? E.dev->CreateRasterRenderer() : nullptr;
      if (!E.raster)
      {
         E.failed = true;
         return false;
      }
   }
   const int w = GLUtil::TargetSide(width, 16);
   const int h = GLUtil::TargetSide(height, 16);
   if (!EnsureResources(w, h, 1))
      return false;
   if (!E.img || E.img->Width() != w || E.img->Height() != h)
   {
      E.img = E.dev->CreateSharedImage(w, h, rhi::Format::RGBA8);
      if (!E.img || E.img->GlTexture() == 0)
      {
         E.failed = true;
         E.img.reset();
         return false;
      }
   }

   CollectDraw();
   for (IGeometrySource* src : mDraw)
      if (auto* node = dynamic_cast<INode*>(src))
         node->CookIfNeeded(frameId);

   // Meshes: re-upload everything when any source's geometry changed.
   std::vector<std::pair<const void*, unsigned long long>> sig;
   for (IGeometrySource* src : mDraw)
      sig.emplace_back((const void*)src, src->MeshRevision());
   if (sig != E.meshSig)
   {
      std::vector<engine::MeshData> meshes;
      for (IGeometrySource* src : mDraw)
         meshes.push_back(ToMeshData(src->GetMesh()));
      if (!E.raster->UploadMeshes(meshes))
         return false;
      E.meshSig = sig;
   }

   engine::Frame fr;
   for (size_t i = 0; i < mDraw.size(); i++)
   {
      IGeometrySource* src = mDraw[i];
      engine::Instance in;
      in.mesh = (int)i;
      const Mat4 m = src->GetModelMatrix();
      std::memcpy(in.model, m.m, 64);
      float n3[9];
      m.NormalMatrix(n3);
      for (int c = 0; c < 3; c++)
         for (int r = 0; r < 3; r++)
            in.normalMatrix[c * 4 + r] = n3[c * 3 + r];
      const Material mat = src->GetMaterial();
      for (int k = 0; k < 3; k++)
         in.baseSrgb[k] = mat.color[k];
      in.metallic = mat.metallic;
      in.roughness = mat.roughness;
      fr.instances.push_back(in);
   }

   // Camera, same precedence as the GL cook.
   const float aspect = (float)w / (float)h;
   float eye[3];
   Mat4 view, proj;
   if (camera != nullptr)
   {
      camera->ComputeEye(eye);
      view = camera->ViewMatrix();
      proj = camera->ProjectionMatrix(aspect);
   }
   else
   {
      const float az = camAzimuth * (float)M_PI / 180.0f, el = camElevation * (float)M_PI / 180.0f;
      const float ce = std::cos(el);
      eye[0] = targetX + camDistance * ce * std::cos(az);
      eye[1] = targetY + camDistance * std::sin(el);
      eye[2] = targetZ + camDistance * ce * std::sin(az);
      const float target[3] = { targetX, targetY, targetZ }, up[3] = { 0, 1, 0 };
      view = Mat4::LookAt(eye, target, up);
      proj = projection == 1 ? Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane)
                             : Mat4::Perspective(fov * 3.14159265f / 180.0f, aspect, nearPlane, farPlane);
   }
   if (rig != nullptr && rig->hasView)
   {
      view = rig->view;
      eye[0] = rig->eye[0]; eye[1] = rig->eye[1]; eye[2] = rig->eye[2];
      if (rig->hasProj)
         proj = rig->proj;
      else if (rig->lens != nullptr)
         proj = rig->lens->ProjectionMatrix(aspect);
   }
   mLastCamera.view = view; mLastCamera.proj = proj;
   mLastCamera.eye[0] = eye[0]; mLastCamera.eye[1] = eye[1]; mLastCamera.eye[2] = eye[2];
   mLastCamera.valid = true;
   const Mat4 vp = Mat4::Multiply(proj, view);
   std::memcpy(fr.viewProj, vp.m, 64);
   std::memcpy(fr.eye, eye, 12);

   // Lights: rig, then light nodes, then the built-in one.
   auto addLight = [&](const RigLight& l)
   {
      if ((int)fr.lights.size() >= engine::kMaxLights)
         return;
      engine::Light e;
      e.type = (engine::LightType)std::max(0, std::min(l.type, 4));
      std::memcpy(e.vec, l.vec, 12); std::memcpy(e.aim, l.aim, 12); std::memcpy(e.colorSrgb, l.color, 12);
      e.intensity = l.intensity; e.falloff = l.falloff; e.spotAngleDeg = l.spotAngle; e.spotPenumbra = l.spotPenumbra;
      e.diffuse = l.diffuse; e.specular = l.specular; e.cutoff = l.cutoff;
      fr.lights.push_back(e);
   };
   if (rig != nullptr && rig->hasLights)
   {
      for (const RigLight& l : rig->lights)
         addLight(l);
   }
   else
   {
      for (int i = 0; i < kLightSlots; i++)
      {
         if (lights[i] == nullptr)
            continue;
         RigLight l;
         l.From(*lights[i]);
         float vec[3];
         lights[i]->ComputeVector(vec);
         const float len = std::max(1e-4f, std::sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]));
         for (int k = 0; k < 3; k++) { l.vec[k] = vec[k]; l.aim[k] = -vec[k] / len; }
         addLight(l);
      }
   }
   if (fr.lights.empty())
   {
      const float az = lightAzimuth * (float)M_PI / 180.0f, el = lightElevation * (float)M_PI / 180.0f;
      RigLight l;
      l.type = 0;
      l.vec[0] = std::cos(el) * std::cos(az); l.vec[1] = std::sin(el); l.vec[2] = std::cos(el) * std::sin(az);
      for (int k = 0; k < 3; k++) l.color[k] = lightColor[k];
      l.intensity = lightIntensity;
      addLight(l);
   }

   float ambient[3] = { ambientColor[0], ambientColor[1], ambientColor[2] };
   if (rig != nullptr)
      for (int k = 0; k < 3; k++) ambient[k] += rig->ambient[k];
   std::memcpy(fr.ambientSrgb, ambient, 12);
   std::memcpy(fr.envSkySrgb, envSky, 12);
   std::memcpy(fr.envHorizonSrgb, envHorizon, 12);
   std::memcpy(fr.envGroundSrgb, envGround, 12);
   fr.envIntensity = envIntensity;
   std::memcpy(fr.clearSrgb, bgColor, 12);
   fr.exposure = exposure;
   fr.tonemap = tonemap == 0 ? 0 : 1;   // none or ACES; the other curves are not in the engine yet
   fr.rim = rimIntensity;
   fr.shadow.enabled = shadowsEnabled;
   fr.shadow.bias = shadowBias; fr.shadow.strength = shadowStrength; fr.shadow.softness = shadowSoftness;
   fr.rtShadows = shadowsEnabled && (liveEngineFeatures & 1) != 0;
   fr.rtReflections = (liveEngineFeatures & 2) != 0;
   fr.gi = (liveEngineFeatures & 4) != 0;
   fr.frameIndex = ++E.frame;

   // Settle: once nothing has changed since the last frame, refine with the path tracer up to ptSamples.
   unsigned long long hsh = 1469598103934665603ull;
   auto mix = [&](const void* d, size_t n)
   {
      const unsigned char* b = (const unsigned char*)d;
      for (size_t i = 0; i < n; i++) { hsh ^= b[i]; hsh *= 1099511628211ull; }
   };
   mix(fr.viewProj, 64); mix(&fr.exposure, 4); mix(fr.ambientSrgb, 12); mix(fr.clearSrgb, 12); mix(&fr.envIntensity, 4);
   for (const engine::Instance& in : fr.instances) { mix(in.model, 64); mix(in.baseSrgb, 12); mix(&in.metallic, 8); }
   for (const engine::Light& l : fr.lights) { mix(l.vec, 12); mix(l.colorSrgb, 12); mix(&l.intensity, 4); mix(l.aim, 12); }
   const bool changed = hsh != E.sceneHash;
   E.sceneHash = hsh;
   bool traced = false;
   if ((liveEngineFeatures & 8) != 0 && !changed)
   {
      const int target = std::clamp(ptSamples, 1, 4096);
      if (E.settled >= target)
         traced = true;   // converged: the picture in the image is final
      else
      {
         const int step = std::min(4, target - E.settled);
         traced = E.raster->PathTrace(*E.img, fr, step, std::clamp(ptBounces, 1, 16), E.settled == 0, nullptr);
         if (traced) E.settled += step;
      }
   }
   if (!traced)
   {
      E.settled = 0;
      if (!E.raster->Render(*E.img, fr, nullptr))
         return false;
   }

   // GL reads the engine's picture from a rectangle texture; the node's output is an ordinary 2D texture.
   if (E.readFbo == 0)
      glGenFramebuffers(1, &E.readFbo);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, E.readFbo);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, E.img->GlTexture(), 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFbo);
   glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   mRevision = NextTextureRevision();
   mHasSceneBuilt = false;
   return true;
}
#else
bool Render3DNode::CookEngine(int) { return false; }
#endif

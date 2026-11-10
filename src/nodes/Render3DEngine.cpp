// Render 3D through the Metal engine (src/engine): the same scene the GL raster draws, lit with ray-traced
// shadows and reflections and live GI. Opt-in per node (liveEngine). Instanced copies, glass and the
// Environment node's HDRI are carried; textures, per-copy colours and non-ACES tonemaps are not drawn yet.
// Returns false (GL draws instead) only when the engine itself is unavailable.
#include "Geometry3DNodes.h"
#include "ColorSpace.h"
#include "EnvironmentNode.h"
#include "GeometryOpNodes.h"
#include "SceneNodes.h"
#include "Transport.h"

#include "gl3.h"
#include "engine/Scene.h"
#include "engine/rhi/Rhi.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
   std::vector<float> envPixels;   // the HDRI read back from GL, one mip at most 2048 wide
   int envW = 0, envH = 0, envLod = 0;
   unsigned int envTex = 0;
   unsigned long long envRev = ~0ull, envSerial = 0;
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

   const auto tpA = std::chrono::steady_clock::now();
   CollectDraw();
   for (IGeometrySource* src : mDraw)
      if (auto* node = dynamic_cast<INode*>(src))
         node->CookIfNeeded(frameId);

   // Meshes: a changed source re-uploads only its own mesh (and BLAS); a changed set of sources, all of them.
   std::vector<std::pair<const void*, unsigned long long>> sig;
   for (IGeometrySource* src : mDraw)
      sig.emplace_back((const void*)src, src->MeshRevision());
   static const bool sProf = std::getenv("INFINITE_ENGINE_PROF") != nullptr;
   const auto tp0 = std::chrono::steady_clock::now();
   int changedMeshes = 0;
   size_t changedTris = 0;
   if (sig != E.meshSig)
   {
      for (size_t i = 0; i < sig.size() && i < E.meshSig.size(); i++)
         if (sProf && sig[i] != E.meshSig[i]) { changedMeshes++; changedTris += mDraw[i]->GetMesh().indices.size() / 3; }
      bool partial = sig.size() == E.meshSig.size() && !sig.empty();
      for (size_t i = 0; partial && i < sig.size(); i++)
         partial = sig[i].first == E.meshSig[i].first;
      for (size_t i = 0; partial && i < sig.size(); i++)
         if (sig[i].second != E.meshSig[i].second)
            partial = E.raster->UpdateMesh((int)i, ToMeshData(mDraw[i]->GetMesh()));
      if (!partial)
      {
         std::vector<engine::MeshData> meshes;
         for (IGeometrySource* src : mDraw)
            meshes.push_back(ToMeshData(src->GetMesh()));
         if (!E.raster->UploadMeshes(meshes))
            return false;
      }
      E.meshSig = sig;
   }

   const auto tp1 = std::chrono::steady_clock::now();
   engine::Frame fr;
   auto place = [&](engine::Instance& in, const Mat4& m)
   {
      std::memcpy(in.model, m.m, 64);
      float n3[9];
      m.NormalMatrix(n3);
      for (int c = 0; c < 3; c++)
         for (int r = 0; r < 3; r++)
            in.normalMatrix[c * 4 + r] = n3[c * 3 + r];
   };
   for (size_t i = 0; i < mDraw.size(); i++)
   {
      IGeometrySource* src = mDraw[i];
      engine::Instance in;
      in.mesh = (int)i;
      const Material mat = src->GetMaterial();
      for (int k = 0; k < 3; k++)
         in.baseSrgb[k] = mat.color[k];
      in.metallic = mat.metallic;
      in.roughness = mat.roughness;
      in.transmission = mat.transmission;
      in.ior = mat.ior;
      // Instanced sources: one engine instance per copy, placed as the GL draw places it (group matrix * copy).
      if (InstanceOnPointsNode* instancer = FindInstancer(src))
      {
         const std::vector<Mat4>& xforms = ResolveInstanceTransforms(src, instancer);
         const std::vector<unsigned char>* sel = ResolveInstanceSelection(src);
         const bool useSel = sel != nullptr && sel->size() == xforms.size();
         const Mat4 group = src->GetInstanceGroupMatrix();
         for (size_t k = 0; k < xforms.size(); k++)
         {
            if (useSel && (*sel)[k] == 0)
               continue;
            place(in, Mat4::Multiply(group, xforms[k]));
            fr.instances.push_back(in);
         }
         continue;
      }
      place(in, src->GetModelMatrix());
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
      e.type = (engine::LightType)std::max(0, std::min(l.type, 5));
      std::memcpy(e.vec, l.vec, 12); std::memcpy(e.aim, l.aim, 12); std::memcpy(e.colorSrgb, l.color, 12);
      e.intensity = l.intensity; e.falloff = l.falloff; e.spotAngleDeg = l.spotAngle; e.spotPenumbra = l.spotPenumbra;
      e.diffuse = l.diffuse; e.specular = l.specular; e.cutoff = l.cutoff;
      e.areaW = std::max(0.001f, l.areaW); e.areaH = std::max(0.001f, l.areaH);
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

   // The Environment node's HDRI, read back once per change from the GL texture Render 3D samples. A mip at most
   // 2048 wide is enough for lighting and a blurred background; envBaseLod keeps roughness on GL's mip scale.
   if (auto* env = dynamic_cast<EnvironmentNode*>(envInput.IsConnected() ? envInput.Resolved() : nullptr))
   {
      env->CookIfNeeded(frameId);
      const unsigned int tex = env->GetEnvironmentTexture();
      if (tex != 0 && (tex != E.envTex || env->TextureRevision() != E.envRev))
      {
         glBindTexture(GL_TEXTURE_2D, tex);
         int tw = 0, th = 0, lod = 0;
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
         while ((tw >> lod) > 2048 && lod < (int)env->MaxLod())
            lod++;
         glGetTexLevelParameteriv(GL_TEXTURE_2D, lod, GL_TEXTURE_WIDTH, &tw);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, lod, GL_TEXTURE_HEIGHT, &th);
         if (tw > 0 && th > 0)
         {
            E.envPixels.assign((size_t)tw * th * 3, 0.0f);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGetTexImage(GL_TEXTURE_2D, lod, GL_RGB, GL_FLOAT, E.envPixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            E.envW = tw; E.envH = th; E.envLod = lod;
            E.envSerial++;
         }
         glBindTexture(GL_TEXTURE_2D, 0);
         E.envTex = tex;
         E.envRev = env->TextureRevision();
      }
      if (tex != 0 && !E.envPixels.empty())
      {
         // Rows as GL holds them: GL's v and Metal's v both start at row 0, so the same lookup reads the same texel.
         fr.envPixels = E.envPixels.data();
         fr.envW = E.envW; fr.envH = E.envH;
         fr.envRev = E.envSerial;
         fr.envRotationRad = env->rotation * (float)M_PI / 180.0f;
         fr.envMaxLod = env->MaxLod();
         fr.envBaseLod = (float)E.envLod;
         fr.envIntensity = envIntensity * env->intensity;
         fr.envBackground = envAsBackground;
         float sunDir[3], sunLight[3];
         if ((int)fr.lights.size() < engine::kMaxLights && env->Sun(sunDir, sunLight))
         {
            // The extracted sun, as GL adds it: rotated with the map, colour normalised, power = peak * intensity.
            const float c = std::cos(fr.envRotationRad), sn = std::sin(fr.envRotationRad);
            engine::Light l;
            l.type = engine::LightType::Directional;
            l.vec[0] = sunDir[0] * c + sunDir[2] * sn; l.vec[1] = sunDir[1]; l.vec[2] = -sunDir[0] * sn + sunDir[2] * c;
            const float peak = std::max({ sunLight[0], sunLight[1], sunLight[2], 1e-6f });
            for (int k = 0; k < 3; k++)
               l.colorSrgb[k] = ColorSpace::LinearToSrgb(sunLight[k] / peak);
            l.intensity = peak * env->intensity;
            fr.lights.push_back(l);
         }
      }
   }
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
   mix(&fr.envRev, 8); mix(&fr.envRotationRad, 4); mix(&fr.envBackground, 1);
   for (const engine::Instance& in : fr.instances) { mix(in.model, 64); mix(in.baseSrgb, 12); mix(&in.metallic, 16); }
   for (const engine::Light& l : fr.lights) { mix(l.vec, 12); mix(l.colorSrgb, 12); mix(&l.intensity, 4); mix(l.aim, 12); }
   const bool changed = hsh != E.sceneHash;
   E.sceneHash = hsh;
   bool traced = false;
   const bool offline = Transport::Instance().IsOfflineMode();
   if (offline && (liveEngineFeatures & 8) != 0)
   {
      // Export / --frame: every frame is the settled picture, traced in full before it is handed on.
      const int target = std::clamp(ptSamples, 1, 4096), bounces = std::clamp(ptBounces, 1, 16);
      traced = true;
      for (int done = 0; done < target && traced; done += 16)
         traced = E.raster->PathTrace(*E.img, fr, std::min(16, target - done), bounces, done == 0, nullptr);
      E.settled = traced ? target : 0;
   }
   else if (offline)
   {
      // Live look on export: let the temporal GI / reflection history converge as it would on a held frame.
      const int warm = (fr.gi || fr.rtReflections) ? 48 : 0;
      for (int i = 0; i < warm; i++)
      {
         if (!E.raster->Render(*E.img, fr, nullptr))
            return false;
         fr.frameIndex = ++E.frame;
      }
   }
   else if ((liveEngineFeatures & 8) != 0 && !changed)
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
   const auto tp2 = std::chrono::steady_clock::now();
   if (!traced)
   {
      E.settled = 0;
      if (!E.raster->Render(*E.img, fr, nullptr))
         return false;
   }
   if (sProf && (E.frame % 30) == 0)
   {
      const auto tp3 = std::chrono::steady_clock::now();
      auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
      std::fprintf(stderr, "ENGINE_PROF meshes=%.2f (changed %d, %zu tris) build=%.2f render=%.2f inst=%zu env=%dx%d lod=%.0f bg=%d conn=%d\n",
                   ms(tp0, tp1), changedMeshes, changedTris, ms(tp1, tp2), ms(tp2, tp3), fr.instances.size(), fr.envW, fr.envH,
                   fr.envBaseLod, (int)fr.envBackground, (int)envInput.IsConnected());
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
   if (sProf && (E.frame % 30) == 0)
   {
      const auto tpZ = std::chrono::steady_clock::now();
      std::fprintf(stderr, "ENGINE_PROF2 sources=%.2f total=%.2f\n", std::chrono::duration<double, std::milli>(tp0 - tpA).count(),
                   std::chrono::duration<double, std::milli>(tpZ - tpA).count());
   }
   return true;
}
#else
bool Render3DNode::CookEngine(int) { return false; }
#endif

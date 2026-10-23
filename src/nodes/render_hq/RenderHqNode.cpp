// Turbo 0.71 (S1): Render 3D HQ, the node side - gathers the scene the same
// way Render 3D does (patched camera / lights win over the built-in ones) and
// hands it to HqRenderer. See RenderHqNode.h.

#include "RenderHqNode.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "nodes/EnvironmentNode.h"
#include "nodes/SceneNodes.h"

// Its cook time (inclusive of the sources it pulls first), for
// INFINITE_FRAMEPROF next to Render 3D's.
double gRenderHqCookSeconds = 0.0;

RenderHqNode::RenderHqNode()
{
   // A 1080p frame by default: HQ is for the finished picture.
   width = 1920.0f;
   height = 1080.0f;
   // S4: shadows on, 2048 map, hard (PCF) by default.
   shadowsEnabled = true;
   shadowQuality = 1;
   shadowSoftness = 0.0f;
   // S6: TAA on (the HQ has no MSAA: the TAA is its anti-aliasing).
   taa = true;
}

RenderHqNode::~RenderHqNode() = default;

unsigned int RenderHqNode::GetOutputTexture()
{
   return mRenderer ? mOut : Render3DNode::GetOutputTexture();
}

int RenderHqNode::GetOutputWidth() const
{
   return mRenderer ? mOutW : Render3DNode::GetOutputWidth();
}

int RenderHqNode::GetOutputHeight() const
{
   return mRenderer ? mOutH : Render3DNode::GetOutputHeight();
}

unsigned long long RenderHqNode::TextureRevision() const
{
   return mRenderer ? mRevision : Render3DNode::TextureRevision();
}

void RenderHqNode::CookIfNeeded(int frameId)
{
   if (!mTried)
   {
      mTried = true;
      mRenderer = CreateHqRenderer(mWhy);
   }
   if (!mRenderer)
   {
      Render3DNode::CookIfNeeded(frameId);
      return;
   }
   if (mLastFrame == frameId)
      return;
   mLastFrame = frameId;
   struct CookTimer
   {
      std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
      ~CookTimer() { gRenderHqCookSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
   } cookTimer;
   GLUtil::DebugScope debugGroup("Render 3D HQ");

   HqFrame f;
   f.frameId = frameId;
   f.width = std::max(16, (int)width);
   f.height = std::max(16, (int)height);

   // The sources first: a texture or PointZ upstream cooks into this frame.
   const std::vector<IGeometrySource*>& draw = CollectDraw();
   for (IGeometrySource* s : draw)
      if (auto* node = dynamic_cast<INode*>(s))
         node->CookIfNeeded(frameId);
   f.draw = &draw;

   if (envInput.IsConnected())
      if (auto* env = dynamic_cast<EnvironmentNode*>(envInput.Resolved()))
      {
         env->CookIfNeeded(frameId);
         f.envTex = env->GetEnvironmentTexture();
         f.envRotation = env->rotation * (float)M_PI / 180.0f;
         f.envIntensity = envIntensity * env->intensity;
         f.envMaxLod = env->MaxLod();
         f.envRevision = env->TextureRevision();
      }
   if (f.envTex == 0)
      f.envIntensity = envIntensity;
   f.envAsBackground = envAsBackground;
   std::copy(envSky, envSky + 3, f.envSky);
   std::copy(envHorizon, envHorizon + 3, f.envHorizon);
   std::copy(envGround, envGround + 3, f.envGround);
   std::copy(bgColor, bgColor + 3, f.bg);

   const float aspect = (float)f.width / (float)f.height;
   if (camera != nullptr)
   {
      camera->ComputeEye(f.eye);
      f.view = camera->ViewMatrix();
      f.proj = camera->ProjectionMatrix(aspect);
   }
   else
   {
      const float az = camAzimuth * (float)M_PI / 180.0f;
      const float el = camElevation * (float)M_PI / 180.0f;
      f.eye[0] = targetX + camDistance * std::cos(el) * std::cos(az);
      f.eye[1] = targetY + camDistance * std::sin(el);
      f.eye[2] = targetZ + camDistance * std::cos(el) * std::sin(az);
      const float target[3] = { targetX, targetY, targetZ };
      const float up[3] = { 0.0f, 1.0f, 0.0f };
      f.view = Mat4::LookAt(f.eye, target, up);
      f.proj = (projection == 1) ? Mat4::Orthographic(orthoHeight, aspect, nearPlane, farPlane)
                                 : Mat4::Perspective(fov * (float)M_PI / 180.0f, aspect, nearPlane, farPlane);
   }

   for (int i = 0; i < kLightSlots && f.lightCount < HqFrame::kMaxLights; i++)
   {
      if (lights[i] == nullptr)
         continue;
      HqLight& l = f.lights[f.lightCount++];
      lights[i]->ComputeVector(l.vec);
      std::copy(lights[i]->color, lights[i]->color + 3, l.color);
      l.intensity = lights[i]->intensity;
      l.type = lights[i]->type;
      l.spotAngle = lights[i]->spotAngle;
      l.spotPenumbra = lights[i]->spotPenumbra;
   }
   if (f.lightCount == 0)
   {
      HqLight& l = f.lights[f.lightCount++];
      const float az = lightAzimuth * (float)M_PI / 180.0f;
      const float el = lightElevation * (float)M_PI / 180.0f;
      l.vec[0] = std::cos(el) * std::cos(az);
      l.vec[1] = std::sin(el);
      l.vec[2] = std::cos(el) * std::sin(az);
      std::copy(lightColor, lightColor + 3, l.color);
      l.intensity = lightIntensity;
   }

   f.exposure = exposure;
   f.tonemap = tonemap;
   f.bloom = bloom;
   f.bloomThreshold = bloomThreshold;
   f.bloomRadius = bloomRadius;
   f.shadows = shadowsEnabled;
   f.shadowSize = 1024 << std::clamp(shadowQuality, 0, 2);
   f.shadowSoftness = std::max(0.0f, shadowSoftness);
   f.contactShadows = contactShadows;
   f.ao = ao;
   f.aoRadius = std::max(0.001f, aoRadius);
   f.aoIntensity = std::max(0.0f, aoIntensity);
   f.aoShow = aoShow;
   f.taa = taa;
   // Seconds since the last frame, for particle velocity (PartVel x dt).
   const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
   f.dt = mLastCookSeconds < 0.0 ? 1.0f / 60.0f : (float)std::clamp(now - mLastCookSeconds, 0.0, 0.1);
   mLastCookSeconds = now;
   f.reuseStill = reuseStill;

   mOut = mRenderer->Render(f);
   mOutW = f.width;
   mOutH = f.height;
   if (!mRenderer->LastReused()) // S7: a reused still frame is the same image
      mRevision++;
   mWhy = mOut == 0 ? mRenderer->Error() : std::string();
}

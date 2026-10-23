#pragma once
// Turbo 0.71 (S1): Render 3D HQ - a second 3D renderer next to Render 3D,
// with techniques from Google Filament (PBR, spherical particle impostors,
// shadows, GTAO, TAA; docs/RENDER-3D-HQ-FILAMENT.md). Same pins as Render 3D
// (geometry A-D, camera, three lights, HDRI) and the same built-in camera and
// light params, so it derives from Render3DNode: cabling, saving, the preview
// orbit and every place that asks "is this a 3D render?" treat it as one. The
// drawing is its own (HqRenderer, an optional module); Render 3D's pipeline
// is never run for it unless the module is missing from the build, in which
// case it draws as Render 3D and Status() says why.

#include "nodes/Geometry3DNodes.h"
#include "nodes/render_hq/HqRenderer.h"

class RenderHqNode : public Render3DNode
{
public:
   static INode* Create() { return new RenderHqNode(); }
   RenderHqNode();
   ~RenderHqNode() override;

   unsigned int GetOutputTexture() override;
   int GetOutputWidth() const override;
   int GetOutputHeight() const override;
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override;

   // Only what the HQ reads (Render 3D's other params are not saved for it).
   void VisitParams(ParamVisitor& v) override
   {
      v.Float("width", width); v.Float("height", height);
      v.Int("projection", projection); v.Float("fov", fov);
      v.Float("orthoHeight", orthoHeight); v.Float("camDistance", camDistance);
      v.Float("camAzimuth", camAzimuth); v.Float("camElevation", camElevation);
      v.Float("targetX", targetX); v.Float("targetY", targetY); v.Float("targetZ", targetZ);
      v.Float("near", nearPlane); v.Float("far", farPlane);
      v.Float("lightAzimuth", lightAzimuth); v.Float("lightElevation", lightElevation);
      v.Color("lightColor", lightColor); v.Float("lightIntensity", lightIntensity);
      v.Color("envSky", envSky); v.Color("envHorizon", envHorizon);
      v.Color("envGround", envGround); v.Float("envIntensity", envIntensity);
      v.Bool("envAsBackground", envAsBackground);
      v.Color("bg", bgColor);
      v.Int("tonemap", tonemap); v.Float("exposure", exposure);
      v.Float("bloom", bloom); v.Float("bloomThreshold", bloomThreshold); v.Float("bloomRadius", bloomRadius);
      // S4 (Render 3D's keys where it has the same setting).
      v.Bool("shadows", shadowsEnabled); v.Int("shadowQuality", shadowQuality);
      v.Float("shadowSoftness", shadowSoftness); v.Bool("contactShadows", contactShadows);
      // S5.
      v.Bool("ao", ao); v.Float("aoRadius", aoRadius); v.Float("aoIntensity", aoIntensity);
      v.Bool("aoShow", aoShow);
      // S6 (Render 3D's key; on by default for the HQ, set in the constructor).
      v.Bool("taa", taa);
   }

   // S5: GTAO on the indirect light, on by default (the finished picture);
   // radius in world units, intensity 0..1; aoShow draws it alone. Radius 1,
   // not Filament's 0.3 m: Infinite scenes are built around 1-unit objects
   // (the default camera sits 3 away), and with Filament's falloff 0.3 only
   // reached a thin line at each contact.
   bool ao = true;
   float aoRadius = 1.0f;
   float aoIntensity = 1.0f;
   bool aoShow = false;

   // S4: screen-space contact shadows for the shadowed light. Off by default,
   // as in Filament (~2 ms at 1080p over a million-particle surface).
   bool contactShadows = false;

   // True while the HQ renderer draws (false: module missing, drawing as Render 3D).
   bool HqActive() const { return mRenderer != nullptr; }
   const std::string& Status() const { return mWhy; }
   size_t HqTriangles() const { return mRenderer ? mRenderer->LastTriangles() : 0; }
   size_t HqDrawCalls() const { return mRenderer ? mRenderer->LastDrawCalls() : 0; }
   size_t HqSpheres() const { return mRenderer ? mRenderer->LastSpheres() : 0; }
   size_t HqSkipped() const { return mRenderer ? mRenderer->LastSkipped() : 0; }

private:
   std::unique_ptr<HqRenderer> mRenderer;
   bool mTried = false;
   std::string mWhy;
   unsigned int mOut = 0;
   int mOutW = 0, mOutH = 0;
   int mLastFrame = -1;
   unsigned long long mRevision = 0;
   double mLastCookSeconds = -1.0; // S6: dt for particle velocity
};

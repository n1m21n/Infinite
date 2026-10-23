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

class Render3DPost;

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
      // S8: render scale and sharpening (Render 3D's keys), GTAO resolution.
      v.Int("upscale", upscale); v.Float("sharpness", sharpness);
      v.Bool("aoFullRes", aoFullRes);
      // S9: FXAA when TAA is off; 360 output (Render 3D's keys and modes).
      v.Bool("fxaa", fxaa);
      v.Int("panorama", panorama); v.Float("domeAngle", domeAngle); v.Float("domeTilt", domeTilt);
   }

   // S8: the HQ's tonemaps - Render 3D's five, then Filament's ACES legacy
   // (Filament's default) and ACES.
   static const std::vector<std::string>& TonemapNames();
   // S8: quality presets - each sets the real params (shadow map size, GTAO
   // on / off and resolution, TAA, render scale); the params stay editable
   // and the preset shown is the one they match ("Custom" when none does).
   enum Preset { kPresetPerformance = 0, kPresetBalanced = 1, kPresetHigh = 2, kPresetCustom = 3 };
   static const std::vector<std::string>& PresetNames();
   void ApplyPreset(int preset);
   int MatchingPreset() const;

   // S8: GTAO at the full frame size (Filament's resolution 1.0; the High
   // preset) instead of half.
   bool aoFullRes = false;
   // S9: Filament's FXAA on the finished image whenever the frame has no TAA
   // (TAA off - e.g. a texture that slides over its surface, which the TAA
   // history smears - or a 360 face). On by default: without it the HQ has no
   // anti-aliasing at all when TAA is off.
   bool fxaa = true;

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

   // S7: not a param - false makes every cook draw (the self-test's timings).
   bool reuseStill = true;
   bool HqReused() const { return mRenderer && mRenderer->LastReused(); }

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
   // S9: 360 - the six faces (RGBA8 array) and Render 3D's reprojection.
   void Cook360(HqFrame& f);
   std::unique_ptr<Render3DPost> mPano;
   unsigned int mFaceArray = 0;
   int mFaceSize = 0;
};

#pragma once
// Turbo 0.71 (S1): the Render 3D HQ renderer, an optional module
// (INFINITE_ENABLE_RENDER_HQ). RenderHqNode gathers the scene (draw list,
// camera, lights, HDRI, settings) into an HqFrame and hands it here; the
// renderer keeps its own GPU copies of the geometry and its own targets, so
// nothing in Render 3D is shared or changed. Without the module, HqStub.cpp
// answers and the node draws through Render 3D instead (Status() says why).
//
// Techniques are ported from Google Filament (Apache 2.0), see
// docs/RENDER-3D-HQ-FILAMENT.md and docs/filament-notes.md.

#include <memory>
#include <string>
#include <vector>

#include "Mesh.h"

class IGeometrySource;

struct HqLight
{
   float vec[3] = { 0.0f, 1.0f, 0.0f }; // direction to the light, or its position (point, spot)
   float color[3] = { 1.0f, 1.0f, 1.0f }; // sRGB, as the Light node shows it
   float intensity = 1.0f;
   int type = 0; // LightNode: 0 directional, 1 point, 2 sun, 3 ambient, 4 spot
   float spotAngle = 45.0f;
   float spotPenumbra = 0.2f;
};

struct HqFrame
{
   static const int kMaxLights = 3;
   // Sources to draw, Group 3D already expanded, each cooked for this frame.
   const std::vector<IGeometrySource*>* draw = nullptr;
   int frameId = 0;
   int width = 0, height = 0;
   Mat4 view, proj;
   float eye[3] = { 0.0f, 0.0f, 0.0f };
   HqLight lights[kMaxLights];
   int lightCount = 0;
   // HDRI (0 = none): equirectangular, mips 0..envMaxLod.
   unsigned int envTex = 0;
   float envRotation = 0.0f; // radians
   float envIntensity = 1.0f;
   float envMaxLod = 0.0f;
   unsigned long long envRevision = 0;
   bool envAsBackground = true;
   // No HDRI: a three-stop gradient for the ambient light (sRGB).
   float envSky[3] = { 0.42f, 0.53f, 0.75f };
   float envHorizon[3] = { 0.55f, 0.56f, 0.60f };
   float envGround[3] = { 0.16f, 0.14f, 0.13f };
   float bg[3] = { 0.04f, 0.04f, 0.06f }; // sRGB, written as is where nothing is drawn
   float exposure = 1.0f;
   // Render 3D's list (none / ACES / Reinhard / AgX / Neutral), then S8's
   // Filament ACES legacy (Filament's default) and Filament ACES.
   int tonemap = 1;
   float bloom = 0.0f, bloomThreshold = 1.0f, bloomRadius = 0.6f;
   // S4: the first directional / sun light (or the built-in one) casts.
   bool shadows = true;
   int shadowSize = 2048;
   float shadowSoftness = 0.0f; // 0 = PCF; > 0 = soft (PCSS-lite), the light's size
   bool contactShadows = false;
   // S5: GTAO on the indirect light (Filament's AmbientOcclusionOptions:
   // radius in world units, intensity 0..1); aoShow draws the occlusion alone.
   bool ao = true;
   float aoRadius = 1.0f;
   float aoIntensity = 1.0f;
   bool aoShow = false;
   // S6: temporal anti-aliasing (Filament's TAA: jittered frames blended
   // into a history); dt: seconds since the last frame (particle velocity).
   bool taa = true;
   float dt = 1.0f / 60.0f;
   // S7: a still scene may return the last image (false: draw every frame -
   // the self-test's timings).
   bool reuseStill = true;
   // S8: Filament's bloom (HqPost) with bloomLevels levels; render scale -
   // the frame is drawn at width x height and FSR 1 brings it to outWidth x
   // outHeight (0 = no upscale); sharpness: RCAS after the TAA (0 = none at
   // native size; with the upscale, Render 3D's (1 - sharpness) x 2 stops);
   // GTAO at the full frame size instead of Filament's half.
   int bloomLevels = 6;
   int outWidth = 0, outHeight = 0;
   float sharpness = 0.0f;
   bool aoFullRes = false;
   // S9: FXAA on the finished image when the frame has no TAA (TAA off, or a
   // 360 face) - without it nothing smooths the edges.
   bool fxaa = true;
};

class HqRenderer
{
public:
   virtual ~HqRenderer() {}
   // Draws the frame and returns the finished RGBA8 texture (width x height,
   // or outWidth x outHeight when S8's render scale upscales it), 0 when it
   // could not (Error() says why). Leaves the GL state as it found it.
   virtual unsigned int Render(const HqFrame& frame) = 0;
   // The frame's depth (hardware 0..1), for nodes drawn after it.
   virtual unsigned int DepthTexture() const = 0;
   virtual size_t LastTriangles() const = 0;
   virtual size_t LastDrawCalls() const = 0;
   // PointZ points drawn as sphere impostors (S3).
   virtual size_t LastSpheres() const = 0;
   // Sources this version cannot draw yet (PointZ line strips, Instance PointZ: S7).
   virtual size_t LastSkipped() const = 0;
   // S7: the last Render() returned the previous image untouched (a still
   // scene whose TAA had converged): its texture revision need not move.
   virtual bool LastReused() const = 0;
   virtual const std::string& Error() const = 0;
};

// nullptr (and why) when the module is not in this build.
std::unique_ptr<HqRenderer> CreateHqRenderer(std::string& why);
bool HqModuleBuilt();

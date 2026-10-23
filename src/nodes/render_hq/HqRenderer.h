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
   int tonemap = 1; // Render 3D's list: none / ACES / Reinhard / AgX / Neutral
   float bloom = 0.0f, bloomThreshold = 1.0f, bloomRadius = 0.6f;
   // S4: the first directional / sun light (or the built-in one) casts.
   bool shadows = true;
   int shadowSize = 2048;
   float shadowSoftness = 0.0f; // 0 = PCF; > 0 = soft (PCSS-lite), the light's size
   bool contactShadows = false;
};

class HqRenderer
{
public:
   virtual ~HqRenderer() {}
   // Draws the frame and returns the finished RGBA8 texture (width x height),
   // 0 when it could not (Error() says why). Leaves the GL state as it found it.
   virtual unsigned int Render(const HqFrame& frame) = 0;
   // The frame's depth (hardware 0..1), for nodes drawn after it.
   virtual unsigned int DepthTexture() const = 0;
   virtual size_t LastTriangles() const = 0;
   virtual size_t LastDrawCalls() const = 0;
   // PointZ points drawn as sphere impostors (S3).
   virtual size_t LastSpheres() const = 0;
   // Sources this version cannot draw yet (PointZ line strips, Instance PointZ: S7).
   virtual size_t LastSkipped() const = 0;
   virtual const std::string& Error() const = 0;
};

// nullptr (and why) when the module is not in this build.
std::unique_ptr<HqRenderer> CreateHqRenderer(std::string& why);
bool HqModuleBuilt();

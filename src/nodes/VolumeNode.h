#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

class Render3DNode;

// --- Volume -------------------------------------------------------------
// Fog, smoke or a cloud drawn over a picture by ray marching a density field
// in the fragment shader. The density comes from layered noise shaped by the
// chosen form (a fog layer, a rising plume, a lumpy mass) and drifts with the
// wind over time.
//
// When the image comes from a Render 3D node the volume sits in that scene: it
// uses that render's camera, and solid objects hide the haze behind them (the
// render's depth buffer stops each ray at the first surface). With any other
// image it draws from its own camera and nothing hides it.
//
// `steps` is the quality dial: few while designing so it stays real time, and
// `render steps` whenever the transport is rendering offline.
class VolumeNode : public INode
{
public:
   enum Shape { kFog = 0, kSmoke, kCloud, kShapeCount };

   static INode* Create() { return new VolumeNode(); }
   static const std::vector<std::string>& ShapeNames();

   ~VolumeNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int) const override { return "image"; }

   // True when the last cook took its camera and depth from an upstream Render 3D.
   bool UsedSceneCamera() const { return mUsedScene; }

   int shape = kFog;
   float density = 1.0f;          // extinction per world unit at full thickness
   float size = 1.5f;             // fog thickness, plume width, cloud radius
   float height = 3.0f;           // fog depth below its top, plume height
   float centerX = 0.0f, centerY = -1.0f, centerZ = 0.0f;
   float noiseScale = 1.2f;       // noise features per world unit
   float noiseAmount = 0.7f;      // 0 smooth, 1 fully broken up
   float seed = 0.0f;
   float windX = 0.2f, windY = 0.1f, windZ = 0.0f;
   float speed = 1.0f;            // how fast the wind moves the noise
   float color[3] = { 0.85f, 0.88f, 0.95f };
   float lightColor[3] = { 1.0f, 0.98f, 0.94f };
   float lightAzimuth = 51.5662f;
   float lightElevation = 51.5662f;
   float lightIntensity = 1.2f;
   float ambient = 0.35f;
   float scatter = 0.8f;          // 0 only darkens, 1 reflects all it receives
   float anisotropy = 0.3f;       // 0 even, toward 1 bright toward the light
   int lightSteps = 4;            // samples toward the light for self shadow; 0 off
   int steps = 32;                // while designing
   int renderSteps = 96;          // in an offline render
   float maxDistance = 30.0f;
   // Camera used when the image does not come from a Render 3D.
   float camAzimuth = 34.3775f, camElevation = 22.9183f, camDistance = 3.0f, fov = 45.0f;
   float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shape", shape); v.Float("density", density); v.Float("size", size); v.Float("height", height);
      v.Float("centerX", centerX); v.Float("centerY", centerY); v.Float("centerZ", centerZ);
      v.Float("noiseScale", noiseScale); v.Float("noiseAmount", noiseAmount); v.Float("seed", seed);
      v.Float("windX", windX); v.Float("windY", windY); v.Float("windZ", windZ); v.Float("speed", speed);
      v.Color("color", color); v.Color("lightColor", lightColor);
      v.Float("lightAzimuth", lightAzimuth); v.Float("lightElevation", lightElevation);
      v.Float("lightIntensity", lightIntensity); v.Float("ambient", ambient);
      v.Float("scatter", scatter); v.Float("anisotropy", anisotropy);
      v.Int("lightSteps", lightSteps); v.Int("steps", steps); v.Int("renderSteps", renderSteps);
      v.Float("maxDistance", maxDistance);
      v.Float("camAzimuth", camAzimuth); v.Float("camElevation", camElevation);
      v.Float("camDistance", camDistance); v.Float("fov", fov);
      v.Float("targetX", targetX); v.Float("targetY", targetY); v.Float("targetZ", targetZ);
   }

private:
   bool EnsureShader();

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProg = 0;
   bool mShaderTried = false;
   bool mUsedScene = false;
   int mLastCookFrame = -1;
};

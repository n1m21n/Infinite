#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "GLUtil.h"

// Procedural noise source: value / Perlin-style fBm / Voronoi / ridged, with
// domain warping. Animates off the Transport clock so it freezes with Pause.
class NoiseNode : public INode
{
public:
   static INode* Create() { return new NoiseNode(); }
   static const std::vector<std::string>& TypeNames();

   ~NoiseNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   int noiseType = 1;
   float width = 1024.0f;
   float height = 1024.0f;
   float scale = 6.0f;
   float octaves = 4.0f;
   float lacunarity = 2.0f;
   float gain = 0.5f;
   float warp = 0.0f;
   float speed = 0.2f;
   float contrast = 1.0f;
   float brightness = 0.0f;
   float seed = 0.0f;
   bool colorNoise = false;
   float lowColor[3] = { 0.0f, 0.0f, 0.0f };
   float highColor[3] = { 1.0f, 1.0f, 1.0f };

private:
   bool EnsureShader();

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

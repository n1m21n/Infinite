#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "GLUtil.h"

// Gradient generator. The most-reached-for source in any node compositor:
// backgrounds, masks, and the input side of a Lookup.
class RampNode : public INode
{
public:
   static const int kStops = 5;

   static INode* Create() { return new RampNode(); }
   static const std::vector<std::string>& TypeNames();
   static const std::vector<std::string>& RepeatNames();

   ~RampNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   int type = 0;   // linear / radial / angular / diamond / spiral
   int repeat = 0; // clamp / repeat / mirror
   float width = 1024.0f;
   float height = 1024.0f;
   float angle = 0.0f;
   float centerX = 0.5f;
   float centerY = 0.5f;
   float scale = 1.0f;
   float offset = 0.0f;
   float gamma = 1.0f;
   float dither = 0.0f;
   int stopCount = 2;
   float stopPos[kStops] = { 0.0f, 1.0f, 0.5f, 0.75f, 0.9f };
   float stopColor[kStops][3] = {
      { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f },
      { 1.0f, 0.3f, 0.2f }, { 0.2f, 0.5f, 1.0f }, { 1.0f, 0.9f, 0.3f }
   };

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("type", type); v.Int("repeat", repeat);
      v.Float("width", width); v.Float("height", height);
      v.Float("angle", angle);
      v.Float("centerX", centerX); v.Float("centerY", centerY);
      v.Float("scale", scale); v.Float("offset", offset);
      v.Float("gamma", gamma); v.Float("dither", dither);
      v.Int("stopCount", stopCount);
      static const char* kPosKeys[kStops] = { "stop0", "stop1", "stop2", "stop3", "stop4" };
      static const char* kColKeys[kStops] = {
         "stopColor0", "stopColor1", "stopColor2", "stopColor3", "stopColor4"
      };
      for (int i = 0; i < kStops; i++)
      {
         v.Float(kPosKeys[i], stopPos[i]);
         v.Color(kColKeys[i], stopColor[i]);
      }
   }

private:
   bool EnsureShader();

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

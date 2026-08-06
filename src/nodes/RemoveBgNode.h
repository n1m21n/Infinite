#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Background removal using the OS's own on-device segmentation - no model
// download, no network, no API key.
//
// Masking costs a GPU readback plus a Vision pass, which is far too slow to run
// every frame at video rates, so the mask is computed on demand (or at a capped
// interval for moving footage) and cached. The mask is then applied on the GPU
// each frame, which is cheap.
class RemoveBgNode : public INode
{
public:
   static INode* Create() { return new RemoveBgNode(); }
   static const std::vector<std::string>& ModeNames();
   static const std::vector<std::string>& OutputModeNames();

   ~RemoveBgNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   void RequestMask() { mNeedsMask = true; }
   const std::string& Status() const { return mStatus; }
   bool HasMask() const { return mMaskTex != 0; }

   int mode = 0;         // 0 = subject, 1 = person
   int outputMode = 0;   // 0 = cutout, 1 = mask only, 2 = background only
   float feather = 0.0f;
   float threshold = 0.5f;
   float contrast = 1.0f;
   bool autoRefresh = false;      // recompute periodically, for video
   float refreshBeats = 1.0f;
   float bgColor[3] = { 0.0f, 0.0f, 0.0f };
   float bgOpacity = 0.0f;

private:
   bool EnsureShader();
   void ComputeMask(unsigned int srcTex, int w, int h);

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   unsigned int mMaskTex = 0;
   bool mShaderTried = false;
   bool mNeedsMask = false;
   int mLastCookFrame = -1;
   double mLastMaskBeat = -1000.0;
   std::string mStatus = "press Remove Background";
};

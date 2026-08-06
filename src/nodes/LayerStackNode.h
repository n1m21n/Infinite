#pragma once

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Multi-input compositing node: four image cables stacked bottom-to-top, each
// with its own blend mode and opacity, resolved in one shader pass. Blend
// handles the two-input case; this is the "many cables into one node" case.
class LayerStackNode : public INode
{
public:
   static const int kSlots = 4;

   static INode* Create() { return new LayerStackNode(); }

   ~LayerStackNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input(int slot) { return mInputs[slot]; }

   int modes[kSlots] = { 0, 0, 0, 0 };
   float opacities[kSlots] = { 1.0f, 1.0f, 1.0f, 1.0f };

private:
   bool EnsureShader();

   ImageCable mInputs[kSlots];
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

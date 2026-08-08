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
   INode* BypassSource() override
   {
      for (int i = 0; i < kSlots; i++)
         if (mInputs[i].IsConnected())
            return mInputs[i].GetSource();
      return nullptr;
   }

   // Swaps two layers wholesale - cable, mode and opacity - so reordering in
   // the UI moves the whole layer rather than just its settings.
   void SwapLayers(int a, int b);

   int modes[kSlots] = { 0, 0, 0, 0 };
   float opacities[kSlots] = { 1.0f, 1.0f, 1.0f, 1.0f };

   void VisitParams(ParamVisitor& v) override
   {
      static const char* kModeKeys[kSlots] = { "mode0", "mode1", "mode2", "mode3" };
      static const char* kOpKeys[kSlots] = { "opacity0", "opacity1", "opacity2", "opacity3" };
      for (int i = 0; i < kSlots; i++)
      {
         v.Int(kModeKeys[i], modes[i]);
         v.Float(kOpKeys[i], opacities[i]);
      }
   }

private:
   bool EnsureShader();

   ImageCable mInputs[kSlots];
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Resolution adaptor, in the spirit of TouchDesigner's Fit TOP. Everything
// downstream of a Fit runs at the resolution chosen here, which is how you get
// two differently-sized sources to composite predictably.
class FitNode : public INode
{
public:
   enum class Mode
   {
      Fit,     // letterbox: whole source visible, aspect preserved
      Fill,    // crop: fills the frame, aspect preserved
      Stretch, // ignore aspect
      Native   // pass through untouched, centred
   };

   static INode* Create() { return new FitNode(); }
   static const std::vector<std::string>& ModeNames();

   ~FitNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   int mode = 0;
   float width = 1024.0f;   // float so modulators can drive it
   float height = 1024.0f;
   bool matchInput = false; // adopt the source's own resolution
   float bgColor[3] = { 0.0f, 0.0f, 0.0f };
   float bgOpacity = 0.0f;

private:
   bool EnsureShader();

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

#pragma once

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// --- Time Machine -------------------------------------------------------
// Keeps the last `length` frames of its input in a ring and shows, at every pixel, the
// frame that was that many frames ago as the grey of the Delay map says: black = now,
// white = `length - 1` frames back, in between is blended between the two nearest frames.
// A left-to-right ramp makes a slit-scan; a radial map makes a ripple through time.
// With nothing in the Delay map the node uses a left-to-right ramp.
//
// The ring is one 2D texture array of `length` layers at the working size (the input
// divided by `resolution`), 8 bits per channel, so 60 frames at half 1080p is 124 MB.
// The ring is filled with the first frame (and again after a size or length change), so
// the picture never shows empty history.
class TimeMachineNode : public INode
{
public:
   static INode* Create() { return new TimeMachineNode(); }
   static constexpr int kMaxLength = 60;

   ~TimeMachineNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   ImageCable& DelayMap() { return mMap; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "image" : "delay map"; }
   void Clear() { mFilled = false; }

   int length = 60;       // frames kept, 2..kMaxLength
   int resolution = 1;    // 0 full, 1 half, 2 quarter (ring memory scales with it)
   bool invert = false;   // white = now, black = oldest

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("length", length); v.Int("resolution", resolution); v.Bool("invert", invert);
   }

private:
   bool EnsureShaders();
   bool EnsureRing(int w, int h, int layers);
   void DestroyRing();

   ImageCable mInput, mMap;
   unsigned int mRingTex = 0;                 // GL_TEXTURE_2D_ARRAY
   unsigned int mLayerFbo[kMaxLength] = {};   // one framebuffer per layer
   int mRingW = 0, mRingH = 0, mRingLayers = 0;
   int mHead = 0;                             // layer the next frame is written to
   bool mFilled = false;
   GLUtil::Fbo mOut;
   unsigned int mProgWrite = 0, mProgRead = 0;
   bool mShadersTried = false;
   int mLastCookFrame = -1;
};

#pragma once

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// --- Optical Flow -------------------------------------------------------
// How far every part of the picture moved since the last frame, as a vector per
// pixel. Pyramidal Lucas-Kanade on the GPU (Bouguet 2001): the image is halved
// `levels` times, the motion is found on the smallest copy and refined on each
// larger one, so a move of many pixels is still found. At each level the window
// sums of the image gradients are separable box filters, so the cost grows with
// the window radius, not its area.
//
// Output (view = Raw): R,G = motion in pixels of the INPUT image per frame
// (+x right, +y up, the UV direction), times `gain`; B = speed, A = 1. The
// texture is the working size (the input divided by `resolution`), 16-bit float.
// View = Colour draws direction as hue and speed as brightness. The first frame,
// and the frame after a size change, is zero: there is nothing to compare with.
//
// The node compares this cook's input with the previous cook's, so a source that
// does not change gives zero, and a patch that is paused gives zero.
class OpticalFlowNode : public INode
{
public:
   static INode* Create() { return new OpticalFlowNode(); }
   static constexpr int kMaxLevels = 6;

   ~OpticalFlowNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int) const override { return "image"; }
   void Clear() { mHavePrev = false; }

   int resolution = 2;   // 0 full, 1 half, 2 quarter (quarter is the one inside 2 ms at 1080p)
   int levels = 3;       // pyramid levels, 1..kMaxLevels (fewer when the image is small)
   int window = 4;       // box radius in pixels at every level, 1..6
   int iterations = 2;   // refinements per level, 1..6
   float gain = 1.0f;    // multiplies the output vectors
   int view = 0;         // 0 raw vectors, 1 colour

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("resolution", resolution); v.Int("levels", levels); v.Int("window", window);
      v.Int("iterations", iterations); v.Float("gain", gain); v.Int("view", view);
   }

private:
   bool EnsureShaders();

   ImageCable mInput;
   // Luma pyramids of the previous and the current frame ([which][level]).
   GLUtil::Fbo mLuma[2][kMaxLevels];
   int mCur = 0;
   // Per level: the window-summed gradient matrix, two work targets, and the flow
   // (ping-ponged between refinements).
   GLUtil::Fbo mTensor[kMaxLevels], mWorkX[kMaxLevels], mWorkY[kMaxLevels];
   GLUtil::Fbo mFlow[kMaxLevels][2];
   GLUtil::Fbo mOut;

   unsigned int mProgLuma = 0, mProgDown = 0, mProgGrad = 0, mProgBox = 0, mProgResid = 0,
                mProgSolve = 0, mProgOut = 0, mProgZero = 0;
   bool mShadersTried = false;
   bool mHavePrev = false;
   int mLastCookFrame = -1;
   int mWorkW = 0, mWorkH = 0, mUsedLevels = 0;
};

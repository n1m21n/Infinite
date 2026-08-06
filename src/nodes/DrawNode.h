#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// A paintable canvas. Strokes accumulate into a persistent FBO, so painting is
// destructive-to-the-buffer but the node itself stays a normal graph citizen -
// it can be fed by an input (paint over an image) and feeds anything downstream.
//
// Brush tips are generated procedurally rather than shipped as image packs:
// downloadable brush sets are almost always licensed, and a distance-field tip
// scales to any size without resampling artefacts.
class DrawNode : public INode
{
public:
   static INode* Create() { return new DrawNode(); }
   static const std::vector<std::string>& BrushNames();

   ~DrawNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mComposite); }
   int GetOutputWidth() const override { return mComposite.w; }
   int GetOutputHeight() const override { return mComposite.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }

   // Called by the editor while the user drags on the node's preview. Coordinates
   // are normalised 0..1 with the origin bottom-left, matching texture space.
   void BeginStroke(float x, float y);
   void ContinueStroke(float x, float y);
   void EndStroke();
   void ClearCanvas() { mNeedsClear = true; }

   bool HasStrokes() const { return mStrokeCount > 0; }

   int brush = 0;
   float brushSize = 0.05f;
   float opacity = 0.8f;
   float hardness = 0.5f;
   float spacing = 0.25f;   // as a fraction of the brush size
   float jitter = 0.0f;
   bool eraser = false;
   float color[3] = { 1.0f, 1.0f, 1.0f };
   float canvasWidth = 1024.0f;
   float canvasHeight = 1024.0f;

private:
   struct Stamp
   {
      float x = 0.0f;
      float y = 0.0f;
      float seed = 0.0f;
   };

   bool EnsureShaders();
   void FlushStamps();

   ImageCable mInput;
   GLUtil::Fbo mCanvas;     // the painted layer alone
   GLUtil::Fbo mScratch;    // ping-pong target while stamping
   GLUtil::Fbo mComposite;  // input with the painted layer over it
   unsigned int mStampProgram = 0;
   unsigned int mCompositeProgram = 0;
   bool mShaderTried = false;
   bool mNeedsClear = true;
   int mLastCookFrame = -1;

   std::vector<Stamp> mPending;
   bool mStrokeActive = false;
   float mLastX = 0.0f;
   float mLastY = 0.0f;
   int mStrokeCount = 0;
   float mSeedCounter = 0.0f;
};

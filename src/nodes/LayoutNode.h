#pragma once

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Infinite-Turbo: a pixel-exact composition canvas, in the spirit of
// TouchDesigner's Layout TOP.
//
// The output is a canvasW x canvasH image. Up to kSlots image inputs are
// placed on it as rectangles measured in canvas pixels: x/y is the layer's
// top-left corner (y grows downwards, like every design tool), and its size
// is the input's REAL pixel size times `scale` - or an explicit width/height
// in "custom size" mode. Layers stack in input order (1 at the bottom) with
// their own opacity; the background is a flat colour (alpha 0 = transparent).
class LayoutNode : public INode
{
public:
   static const int kSlots = 8;
   enum SizeMode { kSizeReal = 0, kSizeCustom = 1 };

   static INode* Create() { return new LayoutNode(); }
   LayoutNode();
   ~LayoutNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   ImageCable& Input(int slot) { return mInputs[slot]; }
   const char* InputLabel(int slot) const override
   {
      static const char* kLabels[kSlots] = { "1", "2", "3", "4", "5", "6", "7", "8" };
      return (slot >= 0 && slot < kSlots) ? kLabels[slot] : nullptr;
   }
   INode* BypassSource() override
   {
      for (int i = 0; i < kSlots; i++)
         if (mInputs[i].IsConnected())
            return mInputs[i].GetSource();
      return nullptr;
   }

   // Real pixel size of what is wired into `slot` (0 x 0 when nothing).
   int SourceWidth(int slot) const;
   int SourceHeight(int slot) const;
   // The layer's rectangle on the canvas, in canvas pixels (top-left origin).
   // x/y place the layer at scale 1; scale is anchored at the layer's centre.
   void LayerRect(int slot, float& x, float& y, float& w, float& h) const;
   // Size at scale 1: the source's real pixel size, or the custom size.
   void BaseSize(int slot, float& w, float& h) const;

   // Quick placements (main thread, UI buttons).
   void PlaceRealSize(int slot);   // scale 1, custom off
   void PlaceCenter(int slot);     // keep size, centre on the canvas
   void PlaceFit(int slot);        // scale so it fits, aspect kept, centred
   void PlaceFill(int slot);       // scale so it covers the canvas, centred

   void VisitParams(ParamVisitor& v) override;

   int canvasW = 1920;
   int canvasH = 1080;
   float bgColor[3] = { 0.0f, 0.0f, 0.0f };
   float bgAlpha = 1.0f;

   float x[kSlots];
   float y[kSlots];
   float scale[kSlots];
   float opacity[kSlots];
   int sizeMode[kSlots];
   int customW[kSlots];
   int customH[kSlots];
   bool visible[kSlots];

   int selectedLayer = 0; // UI only

private:
   bool EnsureShader();

   ImageCable mInputs[kSlots];
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   unsigned long long mRevision = 1;
   int mSrcW[kSlots] = {};
   int mSrcH[kSlots] = {};
};

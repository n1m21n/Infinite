#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Generic scalar-to-color remap: takes any 0-1 luminance/grayscale input (a
// Noise or Texture node's output, an alpha mask, whatever) and recolors it
// through user-authored stops. Unlike RampNode, which welds stop authoring to
// a specific gradient *shape*, this node has no shape of its own - the shape
// comes from upstream, this only supplies the palette.
class ColorRampNode : public INode
{
public:
   static const int kMaxStops = 32;

   enum Interp
   {
      kLinear = 0,
      kConstant = 1
   };

   static INode* Create() { return new ColorRampNode(); }
   static const std::vector<std::string>& InterpNames();

   ColorRampNode();
   ~ColorRampNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   int interpMode = kLinear;
   float mix = 1.0f;
   int stopCount = 3;
   float stopPos[kMaxStops] = { 0.0f, 0.5f, 1.0f };
   float stopColor[kMaxStops][3] = {
      { 0.0f, 0.0f, 0.0f }, { 0.9f, 0.35f, 0.1f }, { 1.0f, 1.0f, 0.85f }
   };

   // Stop editing, mirroring RampNode's array-of-slots approach but with a
   // generous cap instead of a hard 5. New stops append at stopCount; removal
   // compacts by swapping the last active slot into the removed one, so a
   // stop's array index (and therefore its ColorSwatch pin) only ever changes
   // for the stop that got swapped in - not for every stop after it.
   int AddStop(float x, const float rgb[3]);
   void RemoveStop(int index);
   void MoveStop(int index, float x);

   // Color at t (0..1), sorting the stops by position first so callers don't
   // have to keep the array in order while dragging.
   void Evaluate(float t, float outRgb[3]) const;

   void MarkDirty() { mLutDirty = true; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("interpMode", interpMode);
      v.Float("mix", mix);
      v.Int("stopCount", stopCount);
      char posKey[16];
      char colKey[16];
      for (int i = 0; i < kMaxStops; i++)
      {
         snprintf(posKey, sizeof(posKey), "stop%d", i);
         snprintf(colKey, sizeof(colKey), "stopColor%d", i);
         v.Float(posKey, stopPos[i]);
         v.Color(colKey, stopColor[i]);
      }
      mLutDirty = true;
   }

private:
   bool EnsureShader();
   void RebuildLut();
   void SortedOrder(int order[kMaxStops], int count) const;

   bool mLutDirty = true;
   unsigned int mLutTex = 0;

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "core/CurveShape.h"

// Photoshop-style curves: draggable control points per channel, evaluated into
// a 256-entry lookup texture the shader samples. The curve itself is the UI -
// sliders can't express an arbitrary tone response.
class CurvesNode : public INode
{
public:
   enum Channel
   {
      kRGB = 0,
      kRed,
      kGreen,
      kBlue,
      kChannelCount
   };

   static INode* Create() { return new CurvesNode(); }
   static const std::vector<std::string>& ChannelNames();

   ~CurvesNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }

   using Point = CurveShape::Point;

   CurveShape& Shape(int channel) { return mShapes[channel]; }

   // Curve editing. Points are kept sorted by x; the two endpoints cannot be
   // removed, and interior points cannot cross their neighbours.
   int AddPoint(int channel, float x, float y);
   void MovePoint(int channel, int index, float x, float y);
   void RemovePoint(int channel, int index);
   void ResetChannel(int channel);

   // Curve value at x (0..1), used by both the shader LUT and the widget.
   float Evaluate(int channel, float x) const;

   void MarkDirty() { mLutDirty = true; }

   int activeChannel = kRGB;
   float mix = 1.0f;

   // Points are round-tripped as one "x0,y0;x1,y1;..." string per channel
   // rather than adding array support to ParamVisitor for a shape used by only
   // this node. Decoding runs on both save and load - on save the string was
   // just encoded from mPoints, so decoding it back is a harmless no-op.
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("activeChannel", activeChannel);
      v.Float("mix", mix);
      static const char* kKeys[kChannelCount] = {
         "curveRGB", "curveR", "curveG", "curveB"
      };
      for (int c = 0; c < kChannelCount; c++)
      {
         std::string encoded = mShapes[c].Encode();
         v.Text(kKeys[c], encoded);
         mShapes[c].Decode(encoded);
         mLutDirty = true;
      }
   }

private:
   bool EnsureShader();
   void RebuildLut();

   CurveShape mShapes[kChannelCount];
   bool mLutDirty = true;
   unsigned int mLutTex = 0;

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

public:
   CurvesNode();
};

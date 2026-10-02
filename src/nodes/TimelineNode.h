#pragma once

#include "INode.h"

// Infinite-Turbo 0.43: the Arrangement Timeline's video, as a node. Its
// output is the composite of every video lane at the playhead (blend modes,
// opacity, fades), so the timeline can feed an Output / projection window,
// a filter chain or a recording like any other image source.
//
// The compositing itself lives in main.cpp (it needs the graph and the
// arrangement model); this node only asks for it once per frame through
// sComposite and hands the texture on.
class TimelineNode : public INode
{
public:
   static INode* Create() { return new TimelineNode(); }

   // frameId, width, height -> texture (0 = nothing). Set by main.cpp.
   using CompositeFn = unsigned int (*)(int frameId, int width, int height);
   static CompositeFn sComposite;

   unsigned int GetOutputTexture() override { return mTex; }
   int GetOutputWidth() const override { return mTex != 0 ? width : 0; }
   int GetOutputHeight() const override { return mTex != 0 ? height : 0; }
   void CookIfNeeded(int frameId) override
   {
      if (frameId == mLastFrame)
         return;
      mLastFrame = frameId;
      width = width < 16 ? 16 : (width > 8192 ? 8192 : width);
      height = height < 16 ? 16 : (height > 8192 ? 8192 : height);
      mTex = sComposite != nullptr ? sComposite(frameId, width, height) : 0;
   }

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("width", width);
      v.Int("height", height);
   }

   int width = 1920;
   int height = 1080;

private:
   unsigned int mTex = 0;
   int mLastFrame = -1;
};

#pragma once

#include <string>
#include <vector>

#include "INode.h"

// Text/typography node. Renders via macOS CoreText/CoreGraphics into an
// offscreen bitmap, then uploads to a GL texture - a leaf node like
// ImageSourceNode, just with a native-font-rendered source instead of a
// procedural shader. AvailableFonts() enumerates installed system fonts;
// dropping .ttf/.otf files into a future fonts/ folder is the natural next
// step for the "huge open-source font collection" ask without needing to
// vendor gigabytes of font assets up front.
class TextNode : public INode
{
public:
   static INode* Create() { return new TextNode(); }
   ~TextNode() override;

   unsigned int GetOutputTexture() override { return mTex; }
   int GetOutputWidth() const override { return mWidth; }
   int GetOutputHeight() const override { return mHeight; }
   void CookIfNeeded(int frameId) override;

   static const std::vector<std::string>& AvailableFonts();

   // Public - the ImGui params panel writes into these directly (InputText, sliders, etc).
   std::string text = "Text";
   std::string fontName;
   float fontSize = 48.0f;
   float color[3] = { 1.0f, 1.0f, 1.0f };
   float tracking = 0.0f;
   float posX = 0.5f; // normalized, 0..1
   float posY = 0.5f;
   int align = 1; // 0=left, 1=center, 2=right
   float width = 1024.0f;
   float height = 1024.0f;

private:
   unsigned int mTex = 0;
   int mWidth = 512;
   int mHeight = 512;
   int mLastCookFrame = -1;
};

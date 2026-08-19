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
   // Only bumped when CookIfNeeded actually re-rasterizes, so downstream
   // FilterNode chains can skip re-rendering while the text is idle.
   unsigned long long TextureRevision() const override { return mRevision; }

   static const std::vector<std::string>& AvailableFonts();

   // Public - the ImGui params panel writes into these directly (InputText, sliders, etc).
   std::string text = "Text";
   std::string fontName;
   float fontSize = 48.0f;
   float color[3] = { 1.0f, 1.0f, 1.0f };
   float tracking = 0.0f;
   float posX = 0.5f; // normalized, 0..1
   float posY = 0.5f;
   int align = 1; // 0=left, 1=center, 2=right, 3=justified
   float scaleX = 1.0f;
   float scaleY = 1.0f;
   bool wordWrap = false;
   float wrapWidth = 0.9f;    // fraction of the frame width
   float wrapHeight = 0.9f;   // fraction of the frame height
   bool fitToBox = true;      // shrink the type until every line fits the box
   float lineSpacing = 1.0f;
   float outlineWidth = 0.0f;
   float outlineColor[3] = { 0.0f, 0.0f, 0.0f };
   bool outlineOnly = false;
   float width = 1024.0f;
   float height = 1024.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("text", text); v.Text("fontName", fontName);
      v.Float("fontSize", fontSize); v.Color("color", color);
      v.Float("tracking", tracking);
      v.Float("posX", posX); v.Float("posY", posY);
      v.Int("align", align);
      v.Float("scaleX", scaleX); v.Float("scaleY", scaleY);
      v.Bool("wordWrap", wordWrap);
      v.Float("wrapWidth", wrapWidth); v.Float("wrapHeight", wrapHeight);
      v.Bool("fitToBox", fitToBox);
      v.Float("lineSpacing", lineSpacing);
      v.Float("outlineWidth", outlineWidth); v.Color("outlineColor", outlineColor);
      v.Bool("outlineOnly", outlineOnly);
      v.Float("width", width); v.Float("height", height);
   }

private:
   unsigned int mTex = 0;
   int mWidth = 512;
   int mHeight = 512;
   int mLastCookFrame = -1;
   unsigned long long mRevision = 0;

   // Scratch pixel buffers, reused across cooks instead of reallocated.
   std::vector<unsigned char> mPixels;
   std::vector<unsigned char> mFlipped;
   int mUploadedWidth = 0;
   int mUploadedHeight = 0;

   // Params actually rasterized into mTex - the cache key CookIfNeeded compares
   // against to decide whether it needs to redo any work at all. Must cover
   // every field in VisitParams() above (values, not "did the UI touch this"),
   // since modulators can drive these with no UI interaction.
   std::string mBuiltText;
   std::string mBuiltFontName;
   float mBuiltFontSize = 0.0f;
   float mBuiltColor[3] = { -1.0f, -1.0f, -1.0f };
   float mBuiltTracking = 0.0f;
   float mBuiltPosX = 0.0f;
   float mBuiltPosY = 0.0f;
   int mBuiltAlign = -1;
   float mBuiltScaleX = 0.0f;
   float mBuiltScaleY = 0.0f;
   bool mBuiltWordWrap = false;
   float mBuiltWrapWidth = 0.0f;
   float mBuiltWrapHeight = 0.0f;
   bool mBuiltFitToBox = false;
   float mBuiltLineSpacing = 0.0f;
   float mBuiltOutlineWidth = -1.0f;
   float mBuiltOutlineColor[3] = { -1.0f, -1.0f, -1.0f };
   bool mBuiltOutlineOnly = false;
   float mBuiltWidth = 0.0f;
   float mBuiltHeight = 0.0f;
   bool mHasBuilt = false;

public:
   // Point size actually used after fitting, so the UI can show what happened.
   float FittedSize() const { return mFittedSize; }

private:
   float mFittedSize = 0.0f;
};

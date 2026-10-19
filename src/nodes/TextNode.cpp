#include "TextNode.h"

#include "core/gl3.h"
#include "../platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
   // Samples the text sheet for one output pixel. The sheet's anchor sits at
   // (canvas w, canvas h) in sheet pixels; the text's anchor in the output is
   // (w * posX, h * posY), both measured from the top. uShift is the sheet
   // pixel under the output's top-left corner, minus the uploaded box's
   // corner, so uv lands straight in the cropped texture (top row first).
   const char* kMoveFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSheet;\n"
      "uniform vec2 uCanvas;\n" // output size in pixels
      "uniform vec2 uShift;\n"
      "uniform vec2 uBox;\n"    // cropped sheet size in pixels
      "void main() {\n"
      "   vec2 p = vec2(vUv.x, 1.0 - vUv.y) * uCanvas + uShift;\n"
      "   vec2 uv = p / uBox;\n"
      "   if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
      "      fragColor = vec4(0.0);\n"
      "      return;\n"
      "   }\n"
      "   fragColor = texture(uSheet, uv);\n"
      "}\n";
}

TextNode::~TextNode()
{
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
   if (mSheetTex != 0)
      glDeleteTextures(1, &mSheetTex);
   GLUtil::DestroyFbo(mMoved);
   if (mMoveProgram != 0)
      glDeleteProgram(mMoveProgram);
}

const std::vector<std::string>& TextNode::AvailableFonts()
{
   return Platform::AvailableFontFamilies();
}

// Every param except posX/posY - the ones a moved sheet cannot follow.
bool TextNode::StyleMatchesBuilt() const
{
   return mHasBuilt &&
      text == mBuiltText && fontName == mBuiltFontName &&
      fontSize == mBuiltFontSize &&
      color[0] == mBuiltColor[0] && color[1] == mBuiltColor[1] && color[2] == mBuiltColor[2] &&
      tracking == mBuiltTracking &&
      align == mBuiltAlign &&
      scaleX == mBuiltScaleX && scaleY == mBuiltScaleY &&
      wordWrap == mBuiltWordWrap &&
      wrapWidth == mBuiltWrapWidth && wrapHeight == mBuiltWrapHeight &&
      fitToBox == mBuiltFitToBox &&
      lineSpacing == mBuiltLineSpacing &&
      outlineWidth == mBuiltOutlineWidth &&
      outlineColor[0] == mBuiltOutlineColor[0] && outlineColor[1] == mBuiltOutlineColor[1] && outlineColor[2] == mBuiltOutlineColor[2] &&
      outlineOnly == mBuiltOutlineOnly &&
      width == mBuiltWidth && height == mBuiltHeight;
}

// The sheet reaches one canvas width/height either side of the anchor, which
// covers every visible pixel while the anchor stays on the canvas. The sheet
// is laid out with the wrap box halved (on a canvas twice the size, so the
// box comes out the same); every rasterizer clamps the box fraction at 0.05,
// so a box under 0.1 would lay out differently and is drawn exactly instead.
bool TextNode::SheetCovers() const
{
   if (posX < 0.0f || posX > 1.0f || posY < 0.0f || posY > 1.0f)
      return false;
   if (wordWrap && (wrapWidth < 0.1f || wrapHeight < 0.1f))
      return false;
   return true;
}

void TextNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const bool posAtBuilt = posX == mBuiltPosX && posY == mBuiltPosY;
   const bool posStill = posX == mPrevPosX && posY == mPrevPosY;
   mPrevPosX = posX;
   mPrevPosY = posY;
   mStillFrames = posStill ? mStillFrames + 1 : 0;

   if (!StyleMatchesBuilt())
   {
      mSheetValid = false;
      Rasterize();
      return;
   }
   if (posAtBuilt && !mShowingMoved)
      return;

   // Only the position differs from what mTex holds. While it keeps moving,
   // shift the sheet; once it has held still for kSettleFrames, draw it
   // exactly again. Not on the first still frame: gesture playback and
   // stepped modulation can repeat a value for a frame or two mid-move.
   if (mStillFrames >= kSettleFrames || !SheetCovers() || !EnsureSheet())
   {
      Rasterize();
      return;
   }
   if (posStill && mShowingMoved)
      return;
   DrawMoved();
}

void TextNode::Rasterize()
{
   mBuiltText = text;
   mBuiltFontName = fontName;
   mBuiltFontSize = fontSize;
   mBuiltColor[0] = color[0]; mBuiltColor[1] = color[1]; mBuiltColor[2] = color[2];
   mBuiltTracking = tracking;
   mBuiltPosX = posX; mBuiltPosY = posY;
   mBuiltAlign = align;
   mBuiltScaleX = scaleX; mBuiltScaleY = scaleY;
   mBuiltWordWrap = wordWrap;
   mBuiltWrapWidth = wrapWidth; mBuiltWrapHeight = wrapHeight;
   mBuiltFitToBox = fitToBox;
   mBuiltLineSpacing = lineSpacing;
   mBuiltOutlineWidth = outlineWidth;
   mBuiltOutlineColor[0] = outlineColor[0]; mBuiltOutlineColor[1] = outlineColor[1]; mBuiltOutlineColor[2] = outlineColor[2];
   mBuiltOutlineOnly = outlineOnly;
   mBuiltWidth = width; mBuiltHeight = height;
   mHasBuilt = true;
   mShowingMoved = false;

   mWidth = std::max(16, (int)width);
   mHeight = std::max(16, (int)height);

   mPixels.assign((size_t)mWidth * mHeight * 4, 0);

   if (fontName.empty())
      fontName = AvailableFonts().front();
   // The style key must see the font actually drawn, or the next cook would
   // read the filled-in name as a style change.
   mBuiltFontName = fontName;

   Platform::TextRasterRequest req;
   req.text = text;
   req.fontName = fontName;
   req.fontSize = fontSize;
   req.color[0] = color[0]; req.color[1] = color[1]; req.color[2] = color[2];
   req.tracking = tracking;
   req.posX = posX;
   req.posY = posY;
   req.align = align;
   req.wordWrap = wordWrap;
   req.fitToBox = fitToBox;
   req.wrapWidth = wrapWidth;
   req.wrapHeight = wrapHeight;
   req.lineSpacing = lineSpacing;
   req.outlineWidth = outlineWidth;
   req.outlineColor[0] = outlineColor[0]; req.outlineColor[1] = outlineColor[1]; req.outlineColor[2] = outlineColor[2];
   req.outlineOnly = outlineOnly;
   req.scaleX = scaleX;
   req.scaleY = scaleY;

   Platform::RasterizeText(req, mWidth, mHeight, mPixels.data(), mFittedSize);
   ++mRasterCount;

   // The rasterizers write their top row first; GL treats row 0 as the bottom.
   // Reverse the rows so text matches the orientation of every FBO-backed node.
   const int stride = mWidth * 4;
   mFlipped.resize(mPixels.size());
   for (int y = 0; y < mHeight; y++)
      memcpy(&mFlipped[y * stride], &mPixels[(mHeight - 1 - y) * stride], stride);

   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   if (mUploadedWidth == mWidth && mUploadedHeight == mHeight)
   {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE, mFlipped.data());
   }
   else
   {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mWidth, mHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, mFlipped.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      mUploadedWidth = mWidth;
      mUploadedHeight = mHeight;
   }
   glBindTexture(GL_TEXTURE_2D, 0);

   mRevision = NextTextureRevision();
}

// Draws the current style once onto a 2w x 2h sheet with its anchor at the
// centre, then uploads only the box the text covers. Every rasterizer places
// text as anchor = canvas * pos with the wrap box = canvas * wrap / scale, so
// pos 0.5 and half the wrap fraction on the doubled canvas give the same
// layout around the anchor as the real canvas does.
bool TextNode::EnsureSheet()
{
   if (mSheetValid)
      return true;
   if (!mMoveShaderTried)
   {
      mMoveShaderTried = true;
      mMoveProgram = GLUtil::CompileProgram(kMoveFragSrc);
   }
   if (mMoveProgram == 0)
      return false;

   const int sheetW = mWidth * 2;
   const int sheetH = mHeight * 2;
   std::vector<unsigned char> sheet((size_t)sheetW * sheetH * 4, 0);

   Platform::TextRasterRequest req;
   req.text = text;
   req.fontName = fontName;
   req.fontSize = fontSize;
   req.color[0] = color[0]; req.color[1] = color[1]; req.color[2] = color[2];
   req.tracking = tracking;
   req.posX = 0.5f;
   req.posY = 0.5f;
   req.align = align;
   req.wordWrap = wordWrap;
   req.fitToBox = fitToBox;
   req.wrapWidth = wrapWidth * 0.5f;
   req.wrapHeight = wrapHeight * 0.5f;
   req.lineSpacing = lineSpacing;
   req.outlineWidth = outlineWidth;
   req.outlineColor[0] = outlineColor[0]; req.outlineColor[1] = outlineColor[1]; req.outlineColor[2] = outlineColor[2];
   req.outlineOnly = outlineOnly;
   req.scaleX = scaleX;
   req.scaleY = scaleY;
   float fitted = 0.0f;
   Platform::RasterizeText(req, sheetW, sheetH, sheet.data(), fitted);
   ++mSheetCount;

   int x0 = sheetW, y0 = sheetH, x1 = -1, y1 = -1;
   for (int y = 0; y < sheetH; y++)
   {
      const unsigned char* row = &sheet[(size_t)y * sheetW * 4];
      for (int x = 0; x < sheetW; x++)
         if (row[x * 4 + 3] != 0)
         {
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
         }
   }

   mSheetValid = true;
   mSheetEmpty = x1 < 0;
   if (mSheetEmpty)
      return true;

   // One clear pixel round the box, so linear filtering fades to nothing at
   // its edge instead of smearing the outermost row outwards.
   x0 = std::max(0, x0 - 1);
   y0 = std::max(0, y0 - 1);
   x1 = std::min(sheetW - 1, x1 + 1);
   y1 = std::min(sheetH - 1, y1 + 1);
   mSheetBoxX = x0;
   mSheetBoxY = y0;
   mSheetBoxW = x1 - x0 + 1;
   mSheetBoxH = y1 - y0 + 1;

   if (mSheetTex == 0)
      glGenTextures(1, &mSheetTex);
   glBindTexture(GL_TEXTURE_2D, mSheetTex);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, sheetW);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mSheetBoxW, mSheetBoxH, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                &sheet[((size_t)y0 * sheetW + x0) * 4]);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);
   return true;
}

void TextNode::DrawMoved()
{
   if (!GLUtil::EnsureFbo(mMoved, mWidth, mHeight, GL_RGBA8, "Text"))
   {
      Rasterize();
      return;
   }
   const float w = (float)mWidth;
   const float h = (float)mHeight;
   // Sheet pixel under the output's top-left: the sheet anchor (w, h) minus
   // the output anchor (w * posX, h * posY).
   const float shiftX = w * (1.0f - posX) - (float)mSheetBoxX;
   const float shiftY = h * (1.0f - posY) - (float)mSheetBoxY;
   const bool empty = mSheetEmpty;
   const unsigned int sheetTex = mSheetTex;
   const float boxW = (float)std::max(1, mSheetBoxW);
   const float boxH = (float)std::max(1, mSheetBoxH);
   const unsigned int program = mMoveProgram;
   GLUtil::RunShaderPass(mMoved, program, [=]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, sheetTex);
      glUniform1i(glGetUniformLocation(program, "uSheet"), 0);
      glUniform2f(glGetUniformLocation(program, "uCanvas"), w, h);
      // An empty sheet shifts everything off the box: the pass writes clear.
      glUniform2f(glGetUniformLocation(program, "uShift"), empty ? -1.0e6f : shiftX, empty ? -1.0e6f : shiftY);
      glUniform2f(glGetUniformLocation(program, "uBox"), boxW, boxH);
   });
   mShowingMoved = true;
   mRevision = NextTextureRevision();
}

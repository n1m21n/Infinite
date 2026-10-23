#include "TextNode.h"

#include "core/gl3.h"
#include "core/GLUtil.h"
#include "../platform/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

TextNode::~TextNode()
{
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

const std::vector<std::string>& TextNode::AvailableFonts()
{
   return Platform::AvailableFontFamilies();
}

void TextNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const bool paramsUnchanged = mHasBuilt &&
      text == mBuiltText && fontName == mBuiltFontName &&
      fontSize == mBuiltFontSize &&
      color[0] == mBuiltColor[0] && color[1] == mBuiltColor[1] && color[2] == mBuiltColor[2] &&
      tracking == mBuiltTracking &&
      posX == mBuiltPosX && posY == mBuiltPosY &&
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
   if (paramsUnchanged)
      return;

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

   mWidth = GLUtil::TargetSide(width, 16);
   mHeight = GLUtil::TargetSide(height, 16);

   mPixels.assign((size_t)mWidth * mHeight * 4, 0);

   if (fontName.empty())
      fontName = AvailableFonts().front();

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

#include "TextNode.h"

#include <OpenGL/gl3.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
   CFStringRef MakeCFString(const std::string& s)
   {
      return CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
   }
}

TextNode::~TextNode()
{
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

const std::vector<std::string>& TextNode::AvailableFonts()
{
   static std::vector<std::string> sFonts;
   if (!sFonts.empty())
      return sFonts;

   CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
   if (families != nullptr)
   {
      CFIndex count = CFArrayGetCount(families);
      for (CFIndex i = 0; i < count; i++)
      {
         CFStringRef name = (CFStringRef)CFArrayGetValueAtIndex(families, i);
         char buf[256];
         if (CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8))
            sFonts.push_back(buf);
      }
      CFRelease(families);
   }

   std::sort(sFonts.begin(), sFonts.end());
   if (sFonts.empty())
      sFonts.push_back("Helvetica");
   return sFonts;
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

   mWidth = std::max(16, (int)width);
   mHeight = std::max(16, (int)height);

   mPixels.assign((size_t)mWidth * mHeight * 4, 0);

   CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
   CGContextRef ctx = CGBitmapContextCreate(
      mPixels.data(), mWidth, mHeight, 8, mWidth * 4, colorSpace,
      kCGImageAlphaPremultipliedLast);

   if (ctx != nullptr)
   {
      if (fontName.empty())
         fontName = AvailableFonts().front();

      CGColorRef fillColor = CGColorCreateGenericRGB(color[0], color[1], color[2], 1.0);
      CGColorRef strokeColor = CGColorCreateGenericRGB(outlineColor[0], outlineColor[1], outlineColor[2], 1.0);

      // CoreText encodes "stroke as well as fill" as a negative stroke width and
      // "stroke only" as a positive one, both as a percentage of the font size.
      float strokeSetting = 0.0f;
      if (outlineWidth > 0.0f)
         strokeSetting = outlineOnly ? outlineWidth : -outlineWidth;

      CTTextAlignment ctAlign = kCTTextAlignmentCenter;
      if (align == 0) ctAlign = kCTTextAlignmentLeft;
      else if (align == 2) ctAlign = kCTTextAlignmentRight;
      else if (align == 3) ctAlign = kCTTextAlignmentJustified;

      CGFloat lineSpacingMultiple = std::max(0.1f, lineSpacing);
      CTParagraphStyleSetting paragraphSettings[] = {
         { kCTParagraphStyleSpecifierAlignment, sizeof(ctAlign), &ctAlign },
         { kCTParagraphStyleSpecifierLineHeightMultiple, sizeof(lineSpacingMultiple), &lineSpacingMultiple },
      };
      CTParagraphStyleRef paragraphStyle = CTParagraphStyleCreate(paragraphSettings, 2);

      CFStringRef cfText = MakeCFString(text);
      CFNumberRef kern = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &tracking);
      CFNumberRef strokeNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &strokeSetting);

      // Builds the attributed string at a given point size. Fitting has to
      // rebuild it per trial size because the font is baked into the attributes.
      auto buildAttrString = [&](float pointSize) -> CFAttributedStringRef
      {
         CFStringRef nameRef = MakeCFString(fontName);
         CTFontRef trialFont = CTFontCreateWithName(nameRef, pointSize, nullptr);
         CFRelease(nameRef);

         CFStringRef keys[] = {
            kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName,
            kCTStrokeWidthAttributeName, kCTStrokeColorAttributeName, kCTParagraphStyleAttributeName
         };
         CFTypeRef values[] = { trialFont, fillColor, kern, strokeNum, strokeColor, paragraphStyle };
         CFDictionaryRef attrs = CFDictionaryCreate(
            kCFAllocatorDefault, (const void**)keys, (const void**)values, 6,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
         CFAttributedStringRef result = CFAttributedStringCreate(kCFAllocatorDefault, cfText, attrs);
         CFRelease(attrs);
         CFRelease(trialFont);
         return result;
      };

      // Non-uniform scale is applied as a transform about the anchor point, so
      // the fit has to be measured in pre-scale space or the scaled result spills.
      const float sx = std::max(0.01f, scaleX);
      const float sy = std::max(0.01f, scaleY);
      CGContextSaveGState(ctx);
      const double anchorX = mWidth * posX;
      const double anchorY = mHeight * (1.0 - posY);
      CGContextTranslateCTM(ctx, anchorX, anchorY);
      CGContextScaleCTM(ctx, sx, sy);
      CGContextTranslateCTM(ctx, -anchorX, -anchorY);

      if (wordWrap)
      {
         const CGFloat boxW = std::max(8.0, (double)mWidth * std::max(0.05f, wrapWidth) / sx);
         const CGFloat boxH = std::max(8.0, (double)mHeight * std::max(0.05f, wrapHeight) / sy);

         float usedSize = fontSize;
         CFAttributedStringRef finalString = nullptr;
         CGSize finalFit = CGSizeMake(0, 0);

         if (fitToBox)
         {
            // Largest size that fits both axes. Binary search rather than a
            // linear walk so a 400pt request still resolves in ~8 layouts.
            float lo = 4.0f;
            float hi = std::max(5.0f, fontSize);
            for (int i = 0; i < 9; i++)
            {
               const float mid = (lo + hi) * 0.5f;
               CFAttributedStringRef trial = buildAttrString(mid);
               CTFramesetterRef trialSetter = CTFramesetterCreateWithAttributedString(trial);
               CGSize fit = CTFramesetterSuggestFrameSizeWithConstraints(
                  trialSetter, CFRangeMake(0, 0), nullptr, CGSizeMake(boxW, CGFLOAT_MAX), nullptr);
               CFRelease(trialSetter);
               CFRelease(trial);

               if (fit.height <= boxH && fit.width <= boxW)
                  lo = mid;
               else
                  hi = mid;
            }
            usedSize = lo;
         }

         finalString = buildAttrString(usedSize);
         CTFramesetterRef setter = CTFramesetterCreateWithAttributedString(finalString);
         finalFit = CTFramesetterSuggestFrameSizeWithConstraints(
            setter, CFRangeMake(0, 0), nullptr, CGSizeMake(boxW, CGFLOAT_MAX), nullptr);

         // Give the frame the full measured height, never less, or CoreText
         // silently drops the lines that do not fit.
         const CGFloat frameH = std::max(finalFit.height, (CGFloat)1.0);
         CGRect box = CGRectMake(anchorX - boxW * 0.5, anchorY - frameH * 0.5, boxW, frameH);
         CGPathRef path = CGPathCreateWithRect(box, nullptr);
         CTFrameRef frame = CTFramesetterCreateFrame(setter, CFRangeMake(0, 0), path, nullptr);
         CTFrameDraw(frame, ctx);
         CFRelease(frame);
         CGPathRelease(path);
         CFRelease(setter);
         CFRelease(finalString);
         mFittedSize = usedSize;
      }
      else
      {
         CFAttributedStringRef single = buildAttrString(fontSize);
         CTLineRef line = CTLineCreateWithAttributedString(single);
         CGRect bounds = CTLineGetImageBounds(line, ctx);
         double originX = anchorX;
         double originY = anchorY - bounds.size.height * 0.5;
         if (align == 1 || align == 3)
            originX -= bounds.size.width * 0.5;
         else if (align == 2)
            originX -= bounds.size.width;

         CGContextSetTextPosition(ctx, originX, originY);
         CTLineDraw(line, ctx);
         CFRelease(line);
         CFRelease(single);
         mFittedSize = fontSize;
      }

      CGContextRestoreGState(ctx);

      CFRelease(strokeNum);
      CFRelease(kern);
      CFRelease(cfText);
      CFRelease(paragraphStyle);
      CGColorRelease(strokeColor);
      CGColorRelease(fillColor);
      CGContextRelease(ctx);
   }

   CGColorSpaceRelease(colorSpace);

   // CGBitmapContext writes its top row first; GL treats row 0 as the bottom.
   // Reverse the rows so text matches the orientation of every FBO-backed node.
   // (Flipping the CTM instead would avoid this memcpy, but that changes glyph
   // orientation handling in ways not obviously correct - keep the flip.)
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

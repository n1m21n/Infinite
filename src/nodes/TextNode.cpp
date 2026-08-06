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

   mWidth = std::max(16, (int)width);
   mHeight = std::max(16, (int)height);

   std::vector<unsigned char> pixels(mWidth * mHeight * 4, 0);

   CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
   CGContextRef ctx = CGBitmapContextCreate(
      pixels.data(), mWidth, mHeight, 8, mWidth * 4, colorSpace,
      kCGImageAlphaPremultipliedLast);

   if (ctx != nullptr)
   {
      if (fontName.empty())
         fontName = AvailableFonts().front();

      CFStringRef cfFontName = MakeCFString(fontName);
      CTFontRef font = CTFontCreateWithName(cfFontName, fontSize, nullptr);
      CFRelease(cfFontName);

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
      CFStringRef keys[] = {
         kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName,
         kCTStrokeWidthAttributeName, kCTStrokeColorAttributeName, kCTParagraphStyleAttributeName
      };
      CFNumberRef kern = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &tracking);
      CFNumberRef strokeNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &strokeSetting);
      CFTypeRef values[] = { font, fillColor, kern, strokeNum, strokeColor, paragraphStyle };
      CFDictionaryRef attrs = CFDictionaryCreate(
         kCFAllocatorDefault, (const void**)keys, (const void**)values, 6,
         &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

      CFAttributedStringRef attrString = CFAttributedStringCreate(kCFAllocatorDefault, cfText, attrs);

      // Non-uniform scale is applied as a transform about the anchor point so it
      // stretches the rendered glyphs rather than changing the layout metrics.
      CGContextSaveGState(ctx);
      const double anchorX = mWidth * posX;
      const double anchorY = mHeight * (1.0 - posY);
      CGContextTranslateCTM(ctx, anchorX, anchorY);
      CGContextScaleCTM(ctx, std::max(0.01f, scaleX), std::max(0.01f, scaleY));
      CGContextTranslateCTM(ctx, -anchorX, -anchorY);

      if (wordWrap)
      {
         // Framesetter handles wrapping, multi-line layout and justification.
         CTFramesetterRef setter = CTFramesetterCreateWithAttributedString(attrString);
         const CGFloat boxW = std::max(16.0f, mWidth * std::max(0.05f, wrapWidth));
         CGSize suggested = CTFramesetterSuggestFrameSizeWithConstraints(
            setter, CFRangeMake(0, 0), nullptr, CGSizeMake(boxW, CGFLOAT_MAX), nullptr);

         CGRect box = CGRectMake(anchorX - boxW * 0.5, anchorY - suggested.height * 0.5,
                                 boxW, suggested.height);
         CGPathRef path = CGPathCreateWithRect(box, nullptr);
         CTFrameRef frame = CTFramesetterCreateFrame(setter, CFRangeMake(0, 0), path, nullptr);
         CTFrameDraw(frame, ctx);
         CFRelease(frame);
         CGPathRelease(path);
         CFRelease(setter);
      }
      else
      {
         CTLineRef line = CTLineCreateWithAttributedString(attrString);
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
      }

      CGContextRestoreGState(ctx);

      CFRelease(attrString);
      CFRelease(attrs);
      CFRelease(strokeNum);
      CFRelease(kern);
      CFRelease(cfText);
      CFRelease(paragraphStyle);
      CGColorRelease(strokeColor);
      CGColorRelease(fillColor);
      CFRelease(font);
      CGContextRelease(ctx);
   }

   CGColorSpaceRelease(colorSpace);

   // CGBitmapContext writes its top row first; GL treats row 0 as the bottom.
   // Reverse the rows so text matches the orientation of every FBO-backed node.
   const int stride = mWidth * 4;
   std::vector<unsigned char> flipped(pixels.size());
   for (int y = 0; y < mHeight; y++)
      memcpy(&flipped[y * stride], &pixels[(mHeight - 1 - y) * stride], stride);

   if (mTex == 0)
      glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mWidth, mHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);
}

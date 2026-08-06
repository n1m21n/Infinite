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

      CGColorRef cgColor = CGColorCreateGenericRGB(color[0], color[1], color[2], 1.0);

      CFStringRef cfText = MakeCFString(text);
      CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName };
      CFNumberRef kern = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &tracking);
      CFTypeRef values[] = { font, cgColor, kern };
      CFDictionaryRef attrs = CFDictionaryCreate(
         kCFAllocatorDefault, (const void**)keys, (const void**)values, 3,
         &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

      CFAttributedStringRef attrString = CFAttributedStringCreate(kCFAllocatorDefault, cfText, attrs);
      CTLineRef line = CTLineCreateWithAttributedString(attrString);

      CGRect bounds = CTLineGetImageBounds(line, ctx);
      double originX = mWidth * posX;
      double originY = mHeight * (1.0 - posY);
      if (align == 1)
         originX -= bounds.size.width * 0.5;
      else if (align == 2)
         originX -= bounds.size.width;
      originY -= bounds.size.height * 0.5;

      CGContextSetTextPosition(ctx, originX, originY);
      CTLineDraw(line, ctx);

      CFRelease(line);
      CFRelease(attrString);
      CFRelease(attrs);
      CFRelease(kern);
      CFRelease(cfText);
      CGColorRelease(cgColor);
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

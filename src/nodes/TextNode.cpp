#include "TextNode.h"

#include "gl3.h"

#if defined(__APPLE__)
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#else
// Windows rasterization uses GDI+ rather than raw GDI: BeginPath/FillPath
// outlines are unantialiased, which reads as visibly jagged at text sizes.
// GDI+ GraphicsPath::AddString gives antialiased fills AND lets us stroke the
// same path, which is exactly the fill/stroke/stroke-only triad CoreText's
// negative/positive kCTStrokeWidthAttributeName encodes.
#ifndef NOMINMAX
   #define NOMINMAX
#endif
#include <windows.h>
// WIN32_LEAN_AND_MEAN keeps windows.h from pulling the COM/RPC plumbing the
// GDI+ headers assume (IStream, MIDL_INTERFACE via rpcndr.h) - objidl.h
// (through unknwn.h) supplies all of it.
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "../platform/win/WinCommon.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Platform::AvailableFontFamilies backs the Windows branch of AvailableFonts.
#include "../platform/Platform.h"

namespace
{
   // Everything the platform rasterizer needs. Both backends consume exactly
   // this struct; CookIfNeeded stays shared below.
   struct RasterRequest
   {
      std::string text;
      std::string fontName;
      float fontSize = 24.0f;
      float color[3] = { 1, 1, 1 };
      float tracking = 0.0f;
      float posX = 0.5f;
      float posY = 0.5f;
      int align = 1; // 0 left, 1 center, 2 right, 3 justified
      bool wordWrap = false;
      bool fitToBox = false;
      float wrapWidth = 0.9f;  // fraction of canvas width
      float wrapHeight = 0.9f; // fraction of canvas height
      float lineSpacing = 1.0f;
      float outlineWidth = 0.0f;
      float outlineColor[3] = { 0, 0, 0 };
      bool outlineOnly = false;
      float scaleX = 1.0f;
      float scaleY = 1.0f;
   };

#if defined(__APPLE__)

   CFStringRef MakeCFString(const std::string& s)
   {
      return CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
   }

   void Rasterize(const RasterRequest& req, int width, int height,
                  unsigned char* pixels, float& outFittedSize)
   {
      CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
      CGContextRef ctx = CGBitmapContextCreate(
         pixels, width, height, 8, width * 4, colorSpace, kCGImageAlphaPremultipliedLast);

      if (ctx != nullptr)
      {
         CGColorRef fillColor =
            CGColorCreateGenericRGB(req.color[0], req.color[1], req.color[2], 1.0);
         CGColorRef strokeColor = CGColorCreateGenericRGB(
            req.outlineColor[0], req.outlineColor[1], req.outlineColor[2], 1.0);

         // CoreText encodes "stroke as well as fill" as a negative stroke width
         // and "stroke only" as a positive one, both as a percentage of the
         // font size.
         float strokeSetting = 0.0f;
         if (req.outlineWidth > 0.0f)
            strokeSetting = req.outlineOnly ? req.outlineWidth : -req.outlineWidth;

         CTTextAlignment ctAlign = kCTTextAlignmentCenter;
         if (req.align == 0) ctAlign = kCTTextAlignmentLeft;
         else if (req.align == 2) ctAlign = kCTTextAlignmentRight;
         else if (req.align == 3) ctAlign = kCTTextAlignmentJustified;

         CGFloat lineSpacingMultiple = std::max(0.1f, req.lineSpacing);
         CTParagraphStyleSetting paragraphSettings[] = {
            { kCTParagraphStyleSpecifierAlignment, sizeof(ctAlign), &ctAlign },
            { kCTParagraphStyleSpecifierLineHeightMultiple,
              sizeof(lineSpacingMultiple), &lineSpacingMultiple },
         };
         CTParagraphStyleRef paragraphStyle =
            CTParagraphStyleCreate(paragraphSettings, 2);

         CFStringRef cfText = MakeCFString(req.text);
         CFNumberRef kern =
            CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &req.tracking);
         CFNumberRef strokeNum =
            CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &strokeSetting);

         // Builds the attributed string at a given point size. Fitting has to
         // rebuild it per trial size because the font is baked into the
         // attributes.
         auto buildAttrString = [&](float pointSize) -> CFAttributedStringRef
         {
            CFStringRef nameRef = MakeCFString(req.fontName);
            CTFontRef trialFont = CTFontCreateWithName(nameRef, pointSize, nullptr);
            CFRelease(nameRef);

            CFStringRef keys[] = {
               kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName,
               kCTStrokeWidthAttributeName, kCTStrokeColorAttributeName,
               kCTParagraphStyleAttributeName
            };
            CFTypeRef values[] = { trialFont, fillColor, kern, strokeNum, strokeColor,
                                   paragraphStyle };
            CFDictionaryRef attrs = CFDictionaryCreate(
               kCFAllocatorDefault, (const void**)keys, (const void**)values, 6,
               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFAttributedStringRef result =
               CFAttributedStringCreate(kCFAllocatorDefault, cfText, attrs);
            CFRelease(attrs);
            CFRelease(trialFont);
            return result;
         };

         // Non-uniform scale is applied as a transform about the anchor point,
         // so the fit has to be measured in pre-scale space or the scaled
         // result spills.
         const double sx = std::max(0.01f, req.scaleX);
         const double sy = std::max(0.01f, req.scaleY);
         CGContextSaveGState(ctx);
         const double anchorX = width * req.posX;
         const double anchorY = height * (1.0 - req.posY);
         CGContextTranslateCTM(ctx, anchorX, anchorY);
         CGContextScaleCTM(ctx, sx, sy);
         CGContextTranslateCTM(ctx, -anchorX, -anchorY);

         if (req.wordWrap)
         {
            const CGFloat boxW = std::max(8.0, (double)width * std::max(0.05f, req.wrapWidth) / sx);
            const CGFloat boxH = std::max(8.0, (double)height * std::max(0.05f, req.wrapHeight) / sy);

            float usedSize = req.fontSize;
            if (req.fitToBox)
            {
               // Largest size that fits both axes. Binary search rather than a
               // linear walk so a 400pt request still resolves in ~8 layouts.
               float lo = 4.0f;
               float hi = std::max(5.0f, req.fontSize);
               for (int i = 0; i < 9; i++)
               {
                  const float mid = (lo + hi) * 0.5f;
                  CFAttributedStringRef trial = buildAttrString(mid);
                  CTFramesetterRef trialSetter =
                     CTFramesetterCreateWithAttributedString(trial);
                  CGSize fit = CTFramesetterSuggestFrameSizeWithConstraints(
                     trialSetter, CFRangeMake(0, 0), nullptr, CGSizeMake(boxW, CGFLOAT_MAX),
                     nullptr);
                  CFRelease(trialSetter);
                  CFRelease(trial);

                  if (fit.height <= boxH && fit.width <= boxW)
                     lo = mid;
                  else
                     hi = mid;
               }
               usedSize = lo;
            }

            CFAttributedStringRef finalString = buildAttrString(usedSize);
            CTFramesetterRef setter = CTFramesetterCreateWithAttributedString(finalString);
            CGSize finalFit = CTFramesetterSuggestFrameSizeWithConstraints(
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
            outFittedSize = usedSize;
         }
         else
         {
            CFAttributedStringRef single = buildAttrString(req.fontSize);
            CTLineRef line = CTLineCreateWithAttributedString(single);
            CGRect bounds = CTLineGetImageBounds(line, ctx);
            double originX = anchorX;
            double originY = anchorY - bounds.size.height * 0.5;
            if (req.align == 1 || req.align == 3)
               originX -= bounds.size.width * 0.5;
            else if (req.align == 2)
               originX -= bounds.size.width;

            CGContextSetTextPosition(ctx, originX, originY);
            CTLineDraw(line, ctx);
            CFRelease(line);
            CFRelease(single);
            outFittedSize = req.fontSize;
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
   }

#else // Windows

   ULONG_PTR sGdiplusToken = 0;

   void EnsureGdiplus()
   {
      if (sGdiplusToken == 0)
      {
         Gdiplus::GdiplusStartupInput input;
         Gdiplus::GdiplusStartup(&sGdiplusToken, &input, nullptr);
      }
   }

   Gdiplus::FontFamily* LoadFamily(const std::wstring& name, Gdiplus::FontFamily& fallback)
   {
      auto* family = new Gdiplus::FontFamily(name.c_str());
      if (family->GetLastStatus() != Gdiplus::Ok)
      {
         delete family;
         family = new Gdiplus::FontFamily(L"Segoe UI");
         if (family->GetLastStatus() != Gdiplus::Ok)
         {
            delete family;
            family = &fallback; // last resort, caller must not free
         }
      }
      return family;
   }

   // Ink bounds of a string laid out at `size`, measured through the same
   // path machinery the draw uses so measure == render.
   bool MeasureString(Gdiplus::Graphics& graphics, const Gdiplus::FontFamily& family,
                      const std::wstring& text, float size, Gdiplus::RectF& outBounds)
   {
      Gdiplus::GraphicsPath path;
      Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
      const Gdiplus::PointF origin(0.0f, 0.0f);
      path.AddString(text.c_str(), (int)text.size(), &family, Gdiplus::FontStyleRegular,
                     size, origin, &fmt);
      if (path.GetLastStatus() != Gdiplus::Ok)
         return false;
      if (path.GetBounds(&outBounds, nullptr, nullptr) != Gdiplus::Ok)
         return false;
      return true; // empty strings give an empty rect; callers handle zero sizes
   }

   void AppendStringToPath(Gdiplus::GraphicsPath& path, const Gdiplus::FontFamily& family,
                           const std::wstring& text, float size,
                           const Gdiplus::PointF& origin)
   {
      Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
      path.AddString(text.c_str(), (int)text.size(), &family, Gdiplus::FontStyleRegular,
                     size, origin, &fmt);
   }

   void Rasterize(const RasterRequest& req, int width, int height,
                  unsigned char* pixels, float& outFittedSize)
   {
      EnsureGdiplus();
      // The bitmap wraps the caller's buffer in place. Note GDI+ 32bppARGB
      // stores bytes as B,G,R,A - the shared upload tail below swaps to RGBA
      // for GL on this branch.
      // NOTE: PixelFormat32bppARGB is a #define (expands to a parenthesized
      // expression), so it must NOT be qualified with Gdiplus::.
      Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB,
                             (BYTE*)pixels);
      Gdiplus::Graphics* graphics = Gdiplus::Graphics::FromImage(&bitmap);
      if (graphics == nullptr || graphics->GetLastStatus() != Gdiplus::Ok)
      {
         delete graphics;
         return;
      }
      graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
      graphics->SetPageUnit(Gdiplus::UnitPixel); // em sizes and origins are plain pixels

      Gdiplus::FontFamily fallback(L"Segoe UI");
      Gdiplus::FontFamily* family = LoadFamily(WinCommon::Utf8ToWide(req.fontName), fallback);

      // GDI+ y-axis points down, matching the top-down buffer; posY measures
      // from the top directly (the GL flip happens after rasterization).
      const double sx = std::max(0.01f, req.scaleX);
      const double sy = std::max(0.01f, req.scaleY);
      const double anchorX = width * req.posX;
      const double anchorY = height * req.posY;
      // Scale about the anchor point: T(a)*S*T(-a) folded into one matrix.
      Gdiplus::Matrix transform((Gdiplus::REAL)sx, 0.0f, 0.0f, (Gdiplus::REAL)sy,
                                (Gdiplus::REAL)(anchorX * (1.0 - sx)),
                                (Gdiplus::REAL)(anchorY * (1.0 - sy)));
      graphics->SetTransform(&transform);

      const Gdiplus::Color fillColor(
         255, (BYTE)(req.color[0] * 255.0f), (BYTE)(req.color[1] * 255.0f),
         (BYTE)(req.color[2] * 255.0f));
      const Gdiplus::Color strokeColor(
         255, (BYTE)(req.outlineColor[0] * 255.0f), (BYTE)(req.outlineColor[1] * 255.0f),
         (BYTE)(req.outlineColor[2] * 255.0f));

      // Stroke width is a percentage of font size, same units CoreText's
      // kCTStrokeWidthAttributeName uses. Justified alignment falls back to
      // center: GDI+ has no inter-word stretch primitive.
      const int drawAlign = req.align == 3 ? 1 : req.align;

      auto drawLine = [&](const std::wstring& line, float size, double x, double yTop)
      {
         Gdiplus::GraphicsPath path;
         AppendStringToPath(path, *family, line, size,
                            Gdiplus::PointF((Gdiplus::REAL)x, (Gdiplus::REAL)yTop));
         if (path.GetLastStatus() != Gdiplus::Ok || path.GetPointCount() <= 0)
            return;
         if (!req.outlineOnly)
         {
            Gdiplus::SolidBrush brush(fillColor);
            graphics->FillPath(&brush, &path);
         }
         if (req.outlineWidth > 0.0f)
         {
            Gdiplus::Pen pen(strokeColor,
                             std::max(1.0f, size * req.outlineWidth / 100.0f));
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            graphics->DrawPath(&pen, &path);
         }
      };

      // Tracking adds fixed pixels between characters (CoreText's kern
      // attribute). GDI+ has no letter-spacing, so tracked lines draw
      // character-by-character along accumulated advances.
      auto drawTrackedLine = [&](const std::wstring& line, float size, double xStart,
                                 double yTop)
      {
         const double extra = (double)req.tracking;
         double x = xStart;
         for (wchar_t ch : line)
         {
            wchar_t one[2] = { ch, L'\0' };
            Gdiplus::RectF bounds;
            if (MeasureString(*graphics, *family, one, size, bounds))
               drawLine(one, size, x, yTop);
            x += bounds.Width + extra;
         }
      };

      auto lineBoxWidth = [&](const std::wstring& line, float size) -> double
      {
         if (std::abs(req.tracking) > 0.001f)
         {
            double total = 0.0;
            const double extra = (double)req.tracking;
            for (wchar_t ch : line)
            {
               wchar_t one[2] = { ch, L'\0' };
               Gdiplus::RectF b;
               if (MeasureString(*graphics, *family, one, size, b))
                  total += (double)b.Width + extra;
            }
            return total > 0.0 ? total - extra : total;
         }
         Gdiplus::RectF bounds;
         if (!MeasureString(*graphics, *family, line, size, bounds))
            return 0.0;
         return (double)bounds.Width;
      };

      // Greedy word wrap against maxW, honoring explicit newlines. Returns
      // wrapped lines; long words break by character.
      auto wrapLines = [&](const std::wstring& textWide, float size, double maxW)
         -> std::vector<std::wstring>
      {
         std::vector<std::wstring> lines;
         size_t paraStart = 0;
         for (;;)
         {
            const size_t nl = textWide.find(L'\n', paraStart);
            const std::wstring paragraph = textWide.substr(
               paraStart, nl == std::wstring::npos ? std::wstring::npos : nl - paraStart);

            std::wstring current;
            size_t wordStart = 0;
            for (;;)
            {
               const size_t sp = paragraph.find(L' ', wordStart);
               const std::wstring word = paragraph.substr(
                  wordStart, sp == std::wstring::npos ? std::wstring::npos : sp - wordStart);

               if (!word.empty())
               {
                  const std::wstring candidate =
                     current.empty() ? word : current + L" " + word;
                  if (lineBoxWidth(candidate, size) <= maxW || current.empty())
                     current = candidate;
                  else
                  {
                     lines.push_back(current);
                     current = word;
                  }
                  // A single word wider than the box breaks by character.
                  while (lineBoxWidth(current, size) > maxW && current.size() > 1)
                  {
                     size_t cut = current.size() - 1;
                     while (cut > 1 && lineBoxWidth(current.substr(0, cut), size) > maxW)
                        cut--;
                     lines.push_back(current.substr(0, cut));
                     current = current.substr(cut);
                  }
               }
               if (sp == std::wstring::npos)
                  break;
               wordStart = sp + 1;
            }
            lines.push_back(current);
            if (nl == std::wstring::npos)
               break;
            paraStart = nl + 1;
         }
         return lines;
      };

      if (req.wordWrap)
      {
         const double boxW = std::max(8.0, (double)width * std::max(0.05f, req.wrapWidth) / sx);
         const double boxH = std::max(8.0, (double)height * std::max(0.05f, req.wrapHeight) / sy);

         float usedSize = req.fontSize;
         float fittedLo = req.fontSize; // survives the fitToBox scope below
         std::vector<std::wstring> lines;

         if (req.fitToBox)
         {
            // Largest size whose wrapped block fits both axes. Same binary
            // search budget as the CoreText path (~9 layouts).
            float lo = 4.0f;
            float hi = std::max(5.0f, req.fontSize);
            for (int i = 0; i < 9; i++)
            {
               const float mid = (lo + hi) * 0.5f;
               lines = wrapLines(WinCommon::Utf8ToWide(req.text), mid, boxW);
               const double lineH = std::max(1.0, (double)mid * 1.25 *
                                                     std::max(0.1f, req.lineSpacing));
               const double blockH = (double)lines.size() * lineH;
               double widest = 0.0;
               for (const std::wstring& line : lines)
                  widest = std::max(widest, lineBoxWidth(line, mid));
               if (blockH <= boxH && widest <= boxW)
                  lo = mid;
               else
                  hi = mid;
            }
            fittedLo = lo;
         }

         usedSize = req.fitToBox ? fittedLo : req.fontSize;
         lines = wrapLines(WinCommon::Utf8ToWide(req.text), usedSize, boxW);
         const double lineH =
            std::max(1.0, (double)usedSize * 1.25 * std::max(0.1f, req.lineSpacing));
         const double blockH = (double)lines.size() * lineH;

         // The block is centered on the anchor, like the CoreText frame box.
         const double top = anchorY - blockH * 0.5;
         for (size_t i = 0; i < lines.size(); i++)
         {
            const double w = lineBoxWidth(lines[i], usedSize);
            double x = anchorX - boxW * 0.5; // left
            if (drawAlign == 1)
               x = anchorX - w * 0.5;
            else if (drawAlign == 2)
               x = anchorX + boxW * 0.5 - w;
            const double slotTop = top + (double)i * lineH;
            const double yTop = slotTop + std::max(0.0, (lineH - (double)usedSize) * 0.5);

            if (std::abs(req.tracking) > 0.001f)
               drawTrackedLine(lines[i], usedSize, x, yTop);
            else
               drawLine(lines[i], usedSize, x, yTop);
         }
         outFittedSize = usedSize;
      }
      else
      {
         const std::wstring line = WinCommon::Utf8ToWide(req.text);
         Gdiplus::RectF bounds;
         MeasureString(*graphics, *family, line, req.fontSize, bounds);

         double x = anchorX;
         if (drawAlign == 1)
            x -= bounds.Width * 0.5;
         else if (drawAlign == 2)
            x -= bounds.Width;
         const double yTop = anchorY - (double)req.fontSize * 0.5;

         if (std::abs(req.tracking) > 0.001f)
            drawTrackedLine(line, req.fontSize, x, yTop);
         else
            drawLine(line, req.fontSize, x, yTop);
         outFittedSize = req.fontSize;
      }

      graphics->ResetTransform(); // paths above already consumed the matrix
      if (family != &fallback)
         delete family;
      delete graphics;
   }

#endif
}

TextNode::~TextNode()
{
   if (mTex != 0)
      glDeleteTextures(1, &mTex);
}

const std::vector<std::string>& TextNode::AvailableFonts()
{
#if defined(__APPLE__)
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
#else
   return Platform::AvailableFontFamilies(); // already sorted, 'Arial' fallback
#endif
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

   if (fontName.empty())
      fontName = AvailableFonts().front();

   RasterRequest req;
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

   Rasterize(req, mWidth, mHeight, mPixels.data(), mFittedSize);

   // The rasterizers write their top row first; GL treats row 0 as the bottom.
   // Reverse the rows so text matches the orientation of every FBO-backed
   // node. (Flipping inside each rasterizer instead would avoid this memcpy,
   // but that changes glyph orientation handling in ways not obviously
   // correct - keep the flip.) On Windows the GDI+ buffer is BGRA, so the
   // same pass swaps to RGBA for the GL_RGBA upload.
   const int stride = mWidth * 4;
   mFlipped.resize(mPixels.size());
#if defined(__APPLE__)
   for (int y = 0; y < mHeight; y++)
      memcpy(&mFlipped[y * stride], &mPixels[(mHeight - 1 - y) * stride], stride);
#else
   for (int y = 0; y < mHeight; y++)
   {
      const unsigned char* src = &mPixels[(mHeight - 1 - y) * stride];
      unsigned char* dst = &mFlipped[y * stride];
      for (int x = 0; x < mWidth; x++)
      {
         dst[x * 4 + 0] = src[x * 4 + 2];
         dst[x * 4 + 1] = src[x * 4 + 1];
         dst[x * 4 + 2] = src[x * 4 + 0];
         dst[x * 4 + 3] = src[x * 4 + 3];
      }
   }
#endif

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

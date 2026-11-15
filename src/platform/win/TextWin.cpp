#include "Platform.h"
#include "WinCommon.h"

#ifndef NOMINMAX
   #define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Platform
{
   namespace
   {
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

      thread_local int tStyle = Gdiplus::FontStyleRegular;   // set per RasterizeText call from the request

      bool MeasureString(Gdiplus::Graphics& graphics, const Gdiplus::FontFamily& family,
                         const std::wstring& text, float size, Gdiplus::RectF& outBounds)
      {
         Gdiplus::GraphicsPath path;
         Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
         const Gdiplus::PointF origin(0.0f, 0.0f);
         path.AddString(text.c_str(), (int)text.size(), &family, tStyle,
                        size, origin, &fmt);
         if (path.GetLastStatus() != Gdiplus::Ok)
            return false;
         if (path.GetBounds(&outBounds, nullptr, nullptr) != Gdiplus::Ok)
            return false;
         return true;
      }

      void AppendStringToPath(Gdiplus::GraphicsPath& path, const Gdiplus::FontFamily& family,
                              const std::wstring& text, float size,
                              const Gdiplus::PointF& origin)
      {
         Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
         path.AddString(text.c_str(), (int)text.size(), &family, tStyle,
                        size, origin, &fmt);
      }
   }

   void RasterizeText(const TextRasterRequest& req, int width, int height,
                      unsigned char* outPixelsRGBA, float& outFittedSize)
   {
      EnsureGdiplus();
      tStyle = (req.bold ? Gdiplus::FontStyleBold : 0) | (req.italic ? Gdiplus::FontStyleItalic : 0) |
               (req.underline ? Gdiplus::FontStyleUnderline : 0);

      Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB,
                             (BYTE*)outPixelsRGBA);
      Gdiplus::Graphics* graphics = Gdiplus::Graphics::FromImage(&bitmap);
      if (graphics == nullptr || graphics->GetLastStatus() != Gdiplus::Ok)
      {
         delete graphics;
         return;
      }
      graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
      graphics->SetPageUnit(Gdiplus::UnitPixel);

      Gdiplus::FontFamily fallback(L"Segoe UI");
      Gdiplus::FontFamily* family = LoadFamily(WinCommon::Utf8ToWide(req.fontName), fallback);

      const double sx = std::max(0.01f, req.scaleX);
      const double sy = std::max(0.01f, req.scaleY);
      const double anchorX = width * req.posX;
      const double anchorY = height * req.posY;
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

      auto drawTrackedLine = [&](const std::wstring& line, float size, double x, double yTop)
      {
         double curX = x;
         for (size_t c = 0; c < line.size(); c++)
         {
            const std::wstring ch(1, line[c]);
            drawLine(ch, size, curX, yTop);
            Gdiplus::RectF bounds;
            MeasureString(*graphics, *family, ch, size, bounds);
            curX += (double)bounds.Width + (double)req.tracking;
         }
      };

      if (req.wordWrap)
      {
         const double boxW = std::max(8.0, (double)width * std::max(0.05f, req.wrapWidth) / sx);
         const double boxH = std::max(8.0, (double)height * std::max(0.05f, req.wrapHeight) / sy);

         std::vector<std::wstring> words;
         {
            const std::wstring full = WinCommon::Utf8ToWide(req.text);
            size_t start = 0;
            while (start < full.size())
            {
               while (start < full.size() && (full[start] == L' ' || full[start] == L'\t' || full[start] == L'\n'))
                  start++;
               if (start >= full.size())
                  break;
               size_t end = start;
               while (end < full.size() && full[end] != L' ' && full[end] != L'\t' && full[end] != L'\n')
                  end++;
               words.push_back(full.substr(start, end - start));
               start = end;
            }
         }

         auto wrapLinesAt = [&](float size, std::vector<std::wstring>& outLines,
                                std::vector<float>& outLineWidths)
         {
            outLines.clear();
            outLineWidths.clear();
            if (words.empty())
               return;

            Gdiplus::RectF spaceBounds;
            MeasureString(*graphics, *family, L" ", size, spaceBounds);
            const float spaceW = spaceBounds.Width + req.tracking;

            std::wstring curLine;
            float curW = 0.0f;

            for (const auto& w : words)
            {
               Gdiplus::RectF wBounds;
               MeasureString(*graphics, *family, w, size, wBounds);
               const float wW = wBounds.Width + (w.empty() ? 0.0f : (float)(w.size() - 1) * req.tracking);

               if (curLine.empty())
               {
                  curLine = w;
                  curW = wW;
               }
               else if (curW + spaceW + wW <= (float)boxW)
               {
                  curLine += L" " + w;
                  curW += spaceW + wW;
               }
               else
               {
                  outLines.push_back(curLine);
                  outLineWidths.push_back(curW);
                  curLine = w;
                  curW = wW;
               }
            }
            if (!curLine.empty())
            {
               outLines.push_back(curLine);
               outLineWidths.push_back(curW);
            }
         };

         float usedSize = req.fontSize;
         std::vector<std::wstring> lines;
         std::vector<float> lineWidths;

         if (req.fitToBox)
         {
            float lo = 4.0f;
            float hi = std::max(5.0f, req.fontSize);
            for (int iter = 0; iter < 9; iter++)
            {
               const float mid = (lo + hi) * 0.5f;
               wrapLinesAt(mid, lines, lineWidths);
               const double lineH = (double)mid * std::max(0.1f, req.lineSpacing);
               const double totalH = (double)lines.size() * lineH;
               float maxW = 0.0f;
               for (float w : lineWidths)
                  if (w > maxW) maxW = w;
               if (totalH <= boxH && (double)maxW <= boxW)
                  lo = mid;
               else
                  hi = mid;
            }
            usedSize = lo;
         }

         wrapLinesAt(usedSize, lines, lineWidths);
         const double lineH = (double)usedSize * std::max(0.1f, req.lineSpacing);
         const double totalH = (double)lines.size() * lineH;
         const double top = anchorY - totalH * 0.5;

         for (size_t i = 0; i < lines.size(); i++)
         {
            double x = anchorX;
            if (drawAlign == 0)
               x -= boxW * 0.5;
            else if (drawAlign == 1)
               x -= (double)lineWidths[i] * 0.5;
            else if (drawAlign == 2)
               x += boxW * 0.5 - (double)lineWidths[i];

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

      graphics->ResetTransform();
      if (family != &fallback)
         delete family;
      delete graphics;

      // GDI+ 32bppARGB is BGRA in memory; swap B and R so outPixelsRGBA is RGBA.
      for (size_t i = 0; i < (size_t)width * height; ++i)
      {
         std::swap(outPixelsRGBA[i * 4 + 0], outPixelsRGBA[i * 4 + 2]);
      }
   }
}

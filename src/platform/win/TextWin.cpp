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
               (req.underline ? Gdiplus::FontStyleUnderline : 0) | (req.strike ? Gdiplus::FontStyleStrikeout : 0);

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

      // One line: the highlight band behind it, then the letters (moved up by baseShift).
      auto drawAny = [&](const std::wstring& line, float size, double x, double yTop, double w, double bandTop, double bandH)
      {
         if (req.highlight && w > 0.0)
         {
            const float* h = req.highlightColor;
            Gdiplus::SolidBrush band(Gdiplus::Color((BYTE)(std::clamp(h[3], 0.0f, 1.0f) * 255.0f), (BYTE)(h[0] * 255.0f),
                                                    (BYTE)(h[1] * 255.0f), (BYTE)(h[2] * 255.0f)));
            graphics->FillRectangle(&band, (Gdiplus::REAL)x, (Gdiplus::REAL)bandTop, (Gdiplus::REAL)w, (Gdiplus::REAL)bandH);
         }
         if (std::abs(req.tracking) > 0.001f)
            drawTrackedLine(line, size, x, yTop - req.baseShift);
         else
            drawLine(line, size, x, yTop - req.baseShift);
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
               {
                  if (full[start] == L'\n')
                     words.push_back(L"\n");   // a paragraph break
                  start++;
               }
               if (start >= full.size())
                  break;
               size_t end = start;
               while (end < full.size() && full[end] != L' ' && full[end] != L'\t' && full[end] != L'\n')
                  end++;
               words.push_back(full.substr(start, end - start));
               start = end;
            }
         }

         // Per line: does it start a paragraph, and does it end one.
         std::vector<char> paraStart, paraEnd;
         auto lineRoom = [&](bool first) { return std::max(8.0, boxW - req.indentL - req.indentR - (first ? req.indentFirst : 0.0f)); };
         auto wrapLinesAt = [&](float size, std::vector<std::wstring>& outLines,
                                std::vector<float>& outLineWidths)
         {
            outLines.clear();
            outLineWidths.clear();
            paraStart.clear();
            paraEnd.clear();
            if (words.empty())
               return;
            bool first = true;

            Gdiplus::RectF spaceBounds;
            MeasureString(*graphics, *family, L" ", size, spaceBounds);
            const float spaceW = spaceBounds.Width + req.tracking;

            std::wstring curLine;
            float curW = 0.0f;

            auto flush = [&](bool endsPara)
            {
               outLines.push_back(curLine);
               outLineWidths.push_back(curW);
               paraStart.push_back(first ? 1 : 0);
               paraEnd.push_back(endsPara ? 1 : 0);
               first = endsPara;
               curLine.clear();
               curW = 0.0f;
            };
            for (const auto& w : words)
            {
               if (w == L"\n")
               {
                  flush(true);
                  continue;
               }
               Gdiplus::RectF wBounds;
               MeasureString(*graphics, *family, w, size, wBounds);
               const float wW = wBounds.Width + (w.empty() ? 0.0f : (float)(w.size() - 1) * req.tracking);

               if (curLine.empty())
               {
                  curLine = w;
                  curW = wW;
               }
               else if (curW + spaceW + wW <= (float)lineRoom(first))
               {
                  curLine += L" " + w;
                  curW += spaceW + wW;
               }
               else
               {
                  flush(false);
                  curLine = w;
                  curW = wW;
               }
            }
            if (!curLine.empty() || first)
               flush(true);
            else if (!paraEnd.empty())
               paraEnd.back() = 1;
         };
         // Each line's top, with paragraph spacing; returns the total height.
         std::vector<double> lineTops;
         auto layoutTops = [&](double lineH)
         {
            lineTops.assign(paraStart.size(), 0.0);
            double y = 0.0;
            for (size_t i = 0; i < paraStart.size(); i++)
            {
               if (paraStart[i] && i > 0)
                  y += req.spaceBefore;
               lineTops[i] = y;
               y += lineH;
               if (paraEnd[i] && i + 1 < paraStart.size())
                  y += req.spaceAfter;
            }
            return y;
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
               const double totalH = layoutTops(lineH);
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
         const double totalH = layoutTops(lineH);
         const double top = anchorY - totalH * 0.5;

         for (size_t i = 0; i < lines.size(); i++)
         {
            // The line's room: the box less the indents (the first line of a paragraph adds its own).
            const double left = anchorX - boxW * 0.5 + req.indentL + (paraStart[i] ? req.indentFirst : 0.0f);
            const double room = lineRoom(paraStart[i] != 0);
            double x = left;
            if (drawAlign == 1)
               x += (room - (double)lineWidths[i]) * 0.5;
            else if (drawAlign == 2)
               x += room - (double)lineWidths[i];

            const double slotTop = top + lineTops[i];
            const double yTop = slotTop + std::max(0.0, (lineH - (double)usedSize) * 0.5);
            drawAny(lines[i], usedSize, x, yTop, lineWidths[i], yTop, (double)usedSize * 1.2);
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
         drawAny(line, req.fontSize, x, yTop, bounds.Width, yTop, (double)req.fontSize * 1.2);
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

// Linux text: FreeType for glyphs, fontconfig for family -> file resolution.
//
// KNOWN LIMITATION - no complex-script shaping.
//
// Both entry points here map each code point to a glyph with FT_Get_Char_Index
// and advance by that glyph's own advance, with an optional FT_Get_Kerning pair
// adjustment. That is correct for Latin, Greek, Cyrillic and CJK, and it is
// what the Windows GDI path already does, so the two ports agree. It is NOT
// correct for Arabic, Devanagari, Thai or any script that needs contextual
// joining, reordering or mandatory ligatures: those render as isolated,
// unjoined base forms. macOS gets shaping for free from Core Text, so a patch
// authored there with Arabic text will not round-trip faithfully to Linux or
// Windows.
//
// The fix is HarfBuzz (hb_shape over an hb_font_t wrapping the same FT_Face,
// then iterate the glyph infos/positions instead of the code points), which
// adds a dependency in both the Linux and Windows builds. Deliberately out of
// scope for the phase-01 desktop port - it is a shared Linux+Windows gap, not
// a Linux regression, and should be closed on both platforms at once.

#include "platform/Platform.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
   FT_Library gFtLib = nullptr;
   std::mutex gFtMutex;

   bool EnsureFreeType()
   {
      if (!gFtLib)
      {
         if (FT_Init_FreeType(&gFtLib) != 0)
         {
            gFtLib = nullptr;
            return false;
         }
      }
      return true;
   }

   // One fontconfig config for the whole process.
   //
   // FcInitLoadConfigAndFonts() parses /etc/fonts/**, every user config, and
   // validates the font cache - tens of milliseconds on a normal desktop, more
   // on a cold cache. The original code called it once per ResolveFontPath, and
   // ResolveFontPath runs up to three times (requested family -> DejaVu Sans ->
   // fontconfig default) per RasterizeText, which itself re-runs whenever any
   // Text node parameter moves. Never destroyed: owned for the process
   // lifetime. Callers hold gFtMutex, so there is no locking here.
   FcConfig* SharedFcConfig()
   {
      static FcConfig* sConfig = FcInitLoadConfigAndFonts();
      return sConfig;
   }

   std::string ResolveFontPathUncached(const std::string& family, bool bold, bool italic)
   {
      std::string result;
      FcConfig* config = SharedFcConfig();
      if (!config) return result;

      std::string queryFamily = family.empty() ? "sans-serif" : family;
      FcPattern* pat = FcPatternCreate();
      FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)queryFamily.c_str());
      FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_NORMAL);
      FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

      FcConfigSubstitute(config, pat, FcMatchPattern);
      FcDefaultSubstitute(pat);

      FcResult matchResult;
      FcPattern* match = FcFontMatch(config, pat, &matchResult);
      if (match)
      {
         FcChar8* file = nullptr;
         if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file)
         {
            result = (const char*)file;
         }
         FcPatternDestroy(match);
      }
      FcPatternDestroy(pat);
      return result;
   }

   // family+style -> font file. The answer only changes when the user installs
   // or removes a font, which already needs a restart to appear in the font
   // picker (AvailableFontFamilies is likewise load-once).
   std::string ResolveFontPath(const std::string& family, bool bold = false, bool italic = false)
   {
      static std::unordered_map<std::string, std::string> sCache;
      std::string key = family;
      key += bold ? "|b" : "|-";
      key += italic ? "i" : "-";
      auto it = sCache.find(key);
      if (it != sCache.end()) return it->second;
      std::string path = ResolveFontPathUncached(family, bold, italic);
      sCache.emplace(key, path);
      return path;
   }

   // path -> face, kept alive for the process.
   //
   // FT_New_Face mmaps and parses the font file; the previous code paired one
   // with a matching teardown on every single rasterize. Both entry points call
   // FT_Set_Pixel_Sizes before they measure or draw, so a reused face carries no
   // size state between calls. Callers hold gFtMutex, and an FT_Face is not
   // thread-safe, so that lock is what makes sharing one safe. A path that fails
   // to load is cached as nullptr so a broken font file is not re-parsed every
   // frame.
   FT_Face AcquireFace(const std::string& path)
   {
      static std::unordered_map<std::string, FT_Face> sFaces;
      auto it = sFaces.find(path);
      if (it != sFaces.end()) return it->second;

      FT_Face face = nullptr;
      if (FT_New_Face(gFtLib, path.c_str(), 0, &face) != 0)
         face = nullptr;
      sFaces.emplace(path, face);
      return face;
   }

   std::vector<uint32_t> Utf8ToCodepoints(const std::string& str)
   {
      std::vector<uint32_t> cps;
      size_t i = 0;
      while (i < str.size())
      {
         uint8_t c = (uint8_t)str[i];
         if (c < 0x80)
         {
            cps.push_back(c);
            i += 1;
         }
         else if ((c & 0xE0) == 0xC0)
         {
            if (i + 1 < str.size())
            {
               uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)str[i + 1] & 0x3F);
               cps.push_back(cp);
            }
            i += 2;
         }
         else if ((c & 0xF0) == 0xE0)
         {
            if (i + 2 < str.size())
            {
               uint32_t cp = ((c & 0x0F) << 12) | (((uint8_t)str[i + 1] & 0x3F) << 6) | ((uint8_t)str[i + 2] & 0x3F);
               cps.push_back(cp);
            }
            i += 3;
         }
         else if ((c & 0xF8) == 0xF0)
         {
            if (i + 3 < str.size())
            {
               uint32_t cp = ((c & 0x07) << 18) | (((uint8_t)str[i + 1] & 0x3F) << 12) |
                             (((uint8_t)str[i + 2] & 0x3F) << 6) | ((uint8_t)str[i + 3] & 0x3F);
               cps.push_back(cp);
            }
            i += 4;
         }
         else
         {
            i += 1;
         }
      }
      return cps;
   }

   struct OutlineDecomposeContext
   {
      std::vector<Platform::TextContour>* contours = nullptr;
      Platform::TextContour current;
      float lastX = 0.0f;
      float lastY = 0.0f;
      float offsetX = 0.0f;
      float offsetY = 0.0f;
      float scale = 1.0f;

      void Push(float x, float y)
      {
         current.points.push_back((x + offsetX) * scale);
         current.points.push_back((y + offsetY) * scale);
         lastX = x;
         lastY = y;
      }

      void Close()
      {
         if (current.points.size() >= 6)
            contours->push_back(current);
         current.points.clear();
      }
   };

   const int kCurveSteps = 12;

   int OutlineMoveTo(const FT_Vector* to, void* user)
   {
      auto* ctx = (OutlineDecomposeContext*)user;
      ctx->Close();
      ctx->Push((float)to->x, (float)to->y);
      return 0;
   }

   int OutlineLineTo(const FT_Vector* to, void* user)
   {
      auto* ctx = (OutlineDecomposeContext*)user;
      ctx->Push((float)to->x, (float)to->y);
      return 0;
   }

   int OutlineConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
   {
      auto* ctx = (OutlineDecomposeContext*)user;
      const float x0 = ctx->lastX, y0 = ctx->lastY;
      const float x1 = (float)control->x, y1 = (float)control->y;
      const float x2 = (float)to->x, y2 = (float)to->y;
      for (int i = 1; i <= kCurveSteps; i++)
      {
         const float t = (float)i / (float)kCurveSteps;
         const float u = 1.0f - t;
         const float x = u * u * x0 + 2.0f * u * t * x1 + t * t * x2;
         const float y = u * u * y0 + 2.0f * u * t * y1 + t * t * y2;
         ctx->Push(x, y);
      }
      return 0;
   }

   int OutlineCubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user)
   {
      auto* ctx = (OutlineDecomposeContext*)user;
      const float x0 = ctx->lastX, y0 = ctx->lastY;
      const float x1 = (float)control1->x, y1 = (float)control1->y;
      const float x2 = (float)control2->x, y2 = (float)control2->y;
      const float x3 = (float)to->x, y3 = (float)to->y;
      for (int i = 1; i <= kCurveSteps; i++)
      {
         const float t = (float)i / (float)kCurveSteps;
         const float u = 1.0f - t;
         const float x = u * u * u * x0 + 3.0f * u * u * t * x1 + 3.0f * u * t * t * x2 + t * t * t * x3;
         const float y = u * u * u * y0 + 3.0f * u * u * t * y1 + 3.0f * u * t * t * y2 + t * t * t * y3;
         ctx->Push(x, y);
      }
      return 0;
   }
}

namespace Platform
{
   std::string FontFileForFamily(const std::string& family)
   {
      std::string out;
      std::lock_guard<std::mutex> lock(gFtMutex);
      FcConfig* config = SharedFcConfig();
      if (config == nullptr)
         return out;
      FcPattern* pat = FcPatternCreate();
      FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family.c_str());
      FcConfigSubstitute(config, pat, FcMatchPattern);
      FcDefaultSubstitute(pat);
      FcResult res = FcResultNoMatch;
      if (FcPattern* m = FcFontMatch(config, pat, &res))
      {
         FcChar8* file = nullptr;
         if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch && file)
            out = (const char*)file;
         FcPatternDestroy(m);
      }
      FcPatternDestroy(pat);
      return out;
   }

   const std::vector<std::string>& AvailableFontFamilies()
   {
      static std::vector<std::string> sFamilies;
      static bool sLoaded = false;
      if (sLoaded) return sFamilies;
      sLoaded = true;

      std::lock_guard<std::mutex> lock(gFtMutex);
      FcConfig* config = SharedFcConfig();
      if (config)
      {
         FcPattern* pat = FcPatternCreate();
         FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, nullptr);
         FcFontSet* fs = FcFontList(config, pat, os);
         if (fs)
         {
            for (int i = 0; i < fs->nfont; i++)
            {
               FcChar8* family = nullptr;
               if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family)
               {
                  sFamilies.push_back((const char*)family);
               }
            }
            FcFontSetDestroy(fs);
         }
         FcObjectSetDestroy(os);
         FcPatternDestroy(pat);
      }

      std::sort(sFamilies.begin(), sFamilies.end());
      sFamilies.erase(std::unique(sFamilies.begin(), sFamilies.end()), sFamilies.end());
      if (sFamilies.empty())
         sFamilies.push_back("DejaVu Sans");
      return sFamilies;
   }

   bool GetTextOutlines(const std::string& text, const std::string& fontName,
                        float letterSpacing, std::vector<TextContour>& outContours,
                        std::string& outError)
   {
      outContours.clear();
      if (text.empty())
      {
         outError = "no text";
         return false;
      }

      std::lock_guard<std::mutex> lock(gFtMutex);
      if (!EnsureFreeType())
      {
         outError = "failed to initialize FreeType";
         return false;
      }

      std::string path = ResolveFontPath(fontName);
      if (path.empty())
         path = ResolveFontPath("DejaVu Sans");
      if (path.empty())
         path = ResolveFontPath("");
      if (path.empty())
      {
         outError = "could not resolve font path";
         return false;
      }

      FT_Face face = AcquireFace(path);
      if (!face)
      {
         outError = "failed to load font face";
         return false;
      }

      // Large point size to stay away from integer hinting
      const int kPointSize = 256;
      FT_Set_Pixel_Sizes(face, 0, kPointSize);

      // Measure cap height from character 'H' (using FT_Get_Char_Index)
      float capHeight = 0.0f;
      FT_UInt hIdx = FT_Get_Char_Index(face, 'H');
      if (hIdx != 0 && FT_Load_Glyph(face, hIdx, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0)
      {
         capHeight = (float)face->glyph->metrics.height;
      }
      if (capHeight <= 0.0f && face->units_per_EM > 0)
      {
         capHeight = (float)face->units_per_EM * 0.7f;
      }
      if (capHeight <= 0.0f)
      {
         capHeight = (float)kPointSize * 64.0f * 0.7f;
      }

      const float scale = 1.0f / capHeight;

      OutlineDecomposeContext ctx;
      ctx.contours = &outContours;
      ctx.scale = scale;

      FT_Outline_Funcs funcs{};
      funcs.move_to = OutlineMoveTo;
      funcs.line_to = OutlineLineTo;
      funcs.conic_to = OutlineConicTo;
      funcs.cubic_to = OutlineCubicTo;

      const std::vector<uint32_t> codepoints = Utf8ToCodepoints(text);
      float penX = 0.0f;
      FT_UInt prevGlyph = 0;
      const bool hasKerning = FT_HAS_KERNING(face);

      for (size_t i = 0; i < codepoints.size(); i++)
      {
         uint32_t cp = codepoints[i];
         if (cp == '\r' || cp == '\n') continue;

         FT_UInt gIdx = FT_Get_Char_Index(face, cp);
         if (hasKerning && prevGlyph && gIdx)
         {
            FT_Vector delta;
            FT_Get_Kerning(face, prevGlyph, gIdx, FT_KERNING_UNSCALED, &delta);
            penX += (float)delta.x;
         }

         if (FT_Load_Glyph(face, gIdx, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0)
         {
            if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
            {
               ctx.offsetX = penX;
               ctx.offsetY = 0.0f;
               FT_Outline_Decompose(&face->glyph->outline, &funcs, &ctx);
               ctx.Close();
            }
            penX += (float)face->glyph->metrics.horiAdvance + letterSpacing * (float)face->units_per_EM;
         }
         prevGlyph = gIdx;
      }

      if (outContours.empty())
      {
         outError = "text produced no outlines";
         return false;
      }

      // Center horizontally and vertically on the origin (matching macOS Platform.mm)
      float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
      for (const TextContour& c : outContours)
      {
         for (size_t i = 0; i + 1 < c.points.size(); i += 2)
         {
            lo[0] = std::min(lo[0], c.points[i]);
            hi[0] = std::max(hi[0], c.points[i]);
            lo[1] = std::min(lo[1], c.points[i + 1]);
            hi[1] = std::max(hi[1], c.points[i + 1]);
         }
      }
      const float cx = (lo[0] + hi[0]) * 0.5f;
      const float cy = (lo[1] + hi[1]) * 0.5f;
      for (TextContour& c : outContours)
      {
         for (size_t i = 0; i + 1 < c.points.size(); i += 2)
         {
            c.points[i] -= cx;
            c.points[i + 1] -= cy;
         }
      }

      return true;
   }

   void RasterizeText(const TextRasterRequest& req, int width, int height,
                      unsigned char* outPixelsRGBA, float& outFittedSize)
   {
      outFittedSize = req.fontSize;
      if (!outPixelsRGBA || width <= 0 || height <= 0) return;
      std::memset(outPixelsRGBA, 0, (size_t)width * height * 4);

      std::lock_guard<std::mutex> lock(gFtMutex);
      if (!EnsureFreeType()) return;

      std::string path = ResolveFontPath(req.fontName);
      if (path.empty()) path = ResolveFontPath("DejaVu Sans");
      if (path.empty()) path = ResolveFontPath("");
      if (path.empty()) return;

      FT_Face face = AcquireFace(path);
      if (!face)
         return;

      const double sx = std::max(0.01f, req.scaleX);
      const double sy = std::max(0.01f, req.scaleY);
      const double anchorX = width * req.posX;
      const double anchorY = height * req.posY;
      const int drawAlign = (req.align == 3) ? 1 : req.align;

      auto measureLine = [&](const std::string& line, float pxSize) -> float
      {
         FT_Set_Pixel_Sizes(face, 0, (FT_UInt)std::max(1.0f, pxSize));
         float w = 0.0f;
         std::vector<uint32_t> cps = Utf8ToCodepoints(line);
         FT_UInt prev = 0;
         bool hasKerning = FT_HAS_KERNING(face);
         for (uint32_t cp : cps)
         {
            FT_UInt gIdx = FT_Get_Char_Index(face, cp);
            if (hasKerning && prev && gIdx)
            {
               FT_Vector delta;
               FT_Get_Kerning(face, prev, gIdx, FT_KERNING_DEFAULT, &delta);
               w += (float)delta.x / 64.0f;
            }
            if (FT_Load_Glyph(face, gIdx, FT_LOAD_DEFAULT) == 0)
            {
               w += (float)face->glyph->advance.x / 64.0f + req.tracking;
            }
            prev = gIdx;
         }
         return w;
      };

      std::vector<std::string> words;
      {
         size_t start = 0;
         while (start < req.text.size())
         {
            while (start < req.text.size() && (req.text[start] == ' ' || req.text[start] == '\t' || req.text[start] == '\n'))
               start++;
            if (start >= req.text.size()) break;
            size_t end = start;
            while (end < req.text.size() && req.text[end] != ' ' && req.text[end] != '\t' && req.text[end] != '\n')
               end++;
            words.push_back(req.text.substr(start, end - start));
            start = end;
         }
      }

      std::vector<std::string> lines;
      std::vector<float> lineWidths;

      if (req.wordWrap)
      {
         const double boxW = std::max(8.0, (double)width * std::max(0.05f, req.wrapWidth) / sx);
         const double boxH = std::max(8.0, (double)height * std::max(0.05f, req.wrapHeight) / sy);

         auto wrapLinesAt = [&](float size, std::vector<std::string>& outLines, std::vector<float>& outWidths)
         {
            outLines.clear();
            outWidths.clear();
            if (words.empty()) return;

            const float spaceW = measureLine(" ", size);
            std::string curLine;
            float curW = 0.0f;

            for (const auto& w : words)
            {
               float wW = measureLine(w, size);
               if (curLine.empty())
               {
                  curLine = w;
                  curW = wW;
               }
               else if (curW + spaceW + wW <= (float)boxW)
               {
                  curLine += " " + w;
                  curW += spaceW + wW;
               }
               else
               {
                  outLines.push_back(curLine);
                  outWidths.push_back(curW);
                  curLine = w;
                  curW = wW;
               }
            }
            if (!curLine.empty())
            {
               outLines.push_back(curLine);
               outWidths.push_back(curW);
            }
         };

         float usedSize = req.fontSize;
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
               for (float w : lineWidths) if (w > maxW) maxW = w;
               if (totalH <= boxH && (double)maxW <= boxW)
                  lo = mid;
               else
                  hi = mid;
            }
            usedSize = lo;
         }

         wrapLinesAt(usedSize, lines, lineWidths);
         outFittedSize = usedSize;
      }
      else
      {
         lines.push_back(req.text);
         lineWidths.push_back(measureLine(req.text, req.fontSize));
         outFittedSize = req.fontSize;
      }

      const float renderSize = outFittedSize;
      FT_Set_Pixel_Sizes(face, 0, (FT_UInt)std::max(1.0f, renderSize));

      const double lineH = (double)renderSize * std::max(0.1f, req.lineSpacing);
      const double totalH = (double)lines.size() * lineH;
      const double top = anchorY - totalH * 0.5;

      const uint8_t fillR = (uint8_t)std::clamp(req.color[0] * 255.0f, 0.0f, 255.0f);
      const uint8_t fillG = (uint8_t)std::clamp(req.color[1] * 255.0f, 0.0f, 255.0f);
      const uint8_t fillB = (uint8_t)std::clamp(req.color[2] * 255.0f, 0.0f, 255.0f);

      const uint8_t strokeR = (uint8_t)std::clamp(req.outlineColor[0] * 255.0f, 0.0f, 255.0f);
      const uint8_t strokeG = (uint8_t)std::clamp(req.outlineColor[1] * 255.0f, 0.0f, 255.0f);
      const uint8_t strokeB = (uint8_t)std::clamp(req.outlineColor[2] * 255.0f, 0.0f, 255.0f);

      FT_Stroker stroker = nullptr;
      if (req.outlineWidth > 0.0f)
      {
         FT_Stroker_New(gFtLib, &stroker);
         FT_Fixed strokeRadius = (FT_Fixed)(std::max(1.0f, renderSize * req.outlineWidth / 100.0f) * 32.0f);
         FT_Stroker_Set(stroker, strokeRadius, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
      }

      // outPixelsRGBA is PREMULTIPLIED alpha by contract - Platform.mm fills the
      // same buffer through a CGBitmapContext with kCGImageAlphaPremultipliedLast,
      // and every consumer downstream assumes that. So source-over here is
      // dst*(1-a) + src*a on the colour channels, with no divide by alpha. It
      // reads like the straight-alpha bug this codebase has hit before; it isn't.
      // Don't "fix" it without changing Platform.mm's CG flags to match.
      auto blendPixel = [&](int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha)
      {
         if (px < 0 || px >= width || py < 0 || py >= height || alpha == 0) return;
         size_t idx = ((size_t)py * width + px) * 4;
         float a = (float)alpha / 255.0f;
         float invA = 1.0f - a;
         outPixelsRGBA[idx + 0] = (uint8_t)std::clamp((float)outPixelsRGBA[idx + 0] * invA + (float)r * a, 0.0f, 255.0f);
         outPixelsRGBA[idx + 1] = (uint8_t)std::clamp((float)outPixelsRGBA[idx + 1] * invA + (float)g * a, 0.0f, 255.0f);
         outPixelsRGBA[idx + 2] = (uint8_t)std::clamp((float)outPixelsRGBA[idx + 2] * invA + (float)b * a, 0.0f, 255.0f);
         outPixelsRGBA[idx + 3] = (uint8_t)std::clamp((float)outPixelsRGBA[idx + 3] * invA + 255.0f * a, 0.0f, 255.0f);
      };

      for (size_t l = 0; l < lines.size(); l++)
      {
         double curX = anchorX;
         if (drawAlign == 1)
            curX -= (double)lineWidths[l] * 0.5 * sx;
         else if (drawAlign == 2)
            curX -= (double)lineWidths[l] * sx;

         const double slotTop = top + (double)l * lineH * sy;
         const double baselineY = slotTop + (double)face->size->metrics.ascender / 64.0 * sy;

         std::vector<uint32_t> cps = Utf8ToCodepoints(lines[l]);
         FT_UInt prev = 0;
         bool hasKerning = FT_HAS_KERNING(face);

         for (uint32_t cp : cps)
         {
            FT_UInt gIdx = FT_Get_Char_Index(face, cp);
            if (hasKerning && prev && gIdx)
            {
               FT_Vector delta;
               FT_Get_Kerning(face, prev, gIdx, FT_KERNING_DEFAULT, &delta);
               curX += (double)delta.x / 64.0 * sx;
            }

            // Stroke pass
            if (stroker)
            {
               if (FT_Load_Glyph(face, gIdx, FT_LOAD_NO_BITMAP) == 0 && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
               {
                  FT_Glyph glyph;
                  if (FT_Get_Glyph(face->glyph, &glyph) == 0)
                  {
                     FT_Glyph_StrokeBorder(&glyph, stroker, 0, 1);
                     if (FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, 1) == 0)
                     {
                        auto* bmpGlyph = (FT_BitmapGlyph)glyph;
                        for (int r = 0; r < (int)bmpGlyph->bitmap.rows; r++)
                        {
                           for (int c = 0; c < (int)bmpGlyph->bitmap.width; c++)
                           {
                              uint8_t a = bmpGlyph->bitmap.buffer[r * bmpGlyph->bitmap.pitch + c];
                              if (a > 0)
                              {
                                 int px = (int)(curX + (bmpGlyph->left + c) * sx);
                                 int py = (int)(baselineY - (bmpGlyph->top - r) * sy);
                                 blendPixel(px, py, strokeR, strokeG, strokeB, a);
                              }
                           }
                        }
                     }
                     FT_Done_Glyph(glyph);
                  }
               }
            }

            // Fill pass
            if (!req.outlineOnly)
            {
               if (FT_Load_Glyph(face, gIdx, FT_LOAD_RENDER) == 0)
               {
                  FT_GlyphSlot slot = face->glyph;
                  for (int r = 0; r < (int)slot->bitmap.rows; r++)
                  {
                     for (int c = 0; c < (int)slot->bitmap.width; c++)
                     {
                        uint8_t a = slot->bitmap.buffer[r * slot->bitmap.pitch + c];
                        if (a > 0)
                        {
                           int px = (int)(curX + (slot->bitmap_left + c) * sx);
                           int py = (int)(baselineY - (slot->bitmap_top - r) * sy);
                           blendPixel(px, py, fillR, fillG, fillB, a);
                        }
                     }
                  }
               }
            }

            curX += ((double)face->glyph->advance.x / 64.0 + (double)req.tracking) * sx;
            prev = gIdx;
         }
      }

      if (stroker)
         FT_Stroker_Done(stroker);
   }
}

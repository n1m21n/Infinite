#include "TextNode.h"


#include "core/RuntimeLog.h"
#include "platform/OpenGLHeaders.h"
#include "platform/Platform.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

namespace
{
   std::wstring Utf8ToWide(const std::string& text)
   {
      if (text.empty()) return {};
      const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             text.data(), (int)text.size(), nullptr, 0);
      if (count <= 0) return {};
      std::wstring result((size_t)count, L'\0');
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                          text.data(), (int)text.size(), result.data(), count);
      return result;
   }

   BYTE ToByte(float value)
   {
      return (BYTE)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
   }

   double MillisecondsSince(const std::chrono::steady_clock::time_point& start)
   {
      return std::chrono::duration<double, std::milli>(
         std::chrono::steady_clock::now() - start).count();
   }
}

struct TextWindowsRasterState
{
   struct Request
   {
      unsigned long long serial = 0;
      std::string text;
      std::string fontName;
      float fontSize = 48.0f;
      float color[3] {1.0f, 1.0f, 1.0f};
      float tracking = 0.0f;
      float posX = 0.5f;
      float posY = 0.5f;
      int align = 1;
      float scaleX = 1.0f;
      float scaleY = 1.0f;
      bool wordWrap = false;
      float wrapWidth = 0.9f;
      float wrapHeight = 0.9f;
      bool fitToBox = true;
      float lineSpacing = 1.0f;
      float outlineWidth = 0.0f;
      float outlineColor[3] {0.0f, 0.0f, 0.0f};
      bool outlineOnly = false;
      int width = 1024;
      int height = 1024;
   };

   struct Result
   {
      unsigned long long serial = 0;
      std::vector<unsigned char> rgbaBottomUp;
      int width = 0;
      int height = 0;
      float fittedSize = 0.0f;
      bool visible = false;
      double renderMs = 0.0;
      std::string fontUsed;
      std::string error;
   };

   std::thread worker;
   std::mutex mutex;
   std::condition_variable ready;
   std::optional<Request> pending;
   std::optional<Result> completed;
   bool stop = false;
   unsigned long long submittedSerial = 0;
};

namespace
{
   Gdiplus::StringAlignment AlignmentFor(int align)
   {
      if (align == 0 || align == 3) return Gdiplus::StringAlignmentNear;
      if (align == 2) return Gdiplus::StringAlignmentFar;
      return Gdiplus::StringAlignmentCenter;
   }

   bool BuildTextPath(const TextWindowsRasterState::Request& request,
                      const Gdiplus::FontFamily* family, float pointSize,
                      Gdiplus::GraphicsPath& path)
   {
      const std::wstring wide = Utf8ToWide(request.text);
      if (wide.empty()) return true;

      const float boxW = std::max(8.0f, request.width * std::max(0.05f, request.wrapWidth));
      const float boxH = std::max(8.0f, request.height * std::max(0.05f, request.wrapHeight));
      const float anchorX = request.width * request.posX;
      const float anchorY = request.height * request.posY;
      const Gdiplus::RectF box(anchorX - boxW * 0.5f, anchorY - boxH * 0.5f, boxW, boxH);

      Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
      format.SetAlignment(AlignmentFor(request.align));
      format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      format.SetTrimming(Gdiplus::StringTrimmingNone);
      INT flags = Gdiplus::StringFormatFlagsMeasureTrailingSpaces;
      if (!request.wordWrap) flags |= Gdiplus::StringFormatFlagsNoWrap;
      format.SetFormatFlags(flags);

      if (path.AddString(wide.c_str(), (INT)wide.size(), family,
                         Gdiplus::FontStyleRegular, pointSize, box, &format) != Gdiplus::Ok)
         return false;

      // GDI+ does the shaping, Unicode fallback and wrapping. Apply Infinite's
      // typography controls to the completed vector path, never to the bitmap.
      const float horizontal = std::max(0.01f, request.scaleX) *
         std::max(0.05f, 1.0f + request.tracking / std::max(4.0f, pointSize));
      const float vertical = std::max(0.01f, request.scaleY) *
         std::max(0.05f, request.lineSpacing);
      if (std::abs(horizontal - 1.0f) > 0.0001f || std::abs(vertical - 1.0f) > 0.0001f)
      {
         Gdiplus::Matrix transform(horizontal, 0.0f, 0.0f, vertical,
                                   anchorX * (1.0f - horizontal),
                                   anchorY * (1.0f - vertical));
         path.Transform(&transform);
      }
      return true;
   }

   TextWindowsRasterState::Result RasterText(
      const TextWindowsRasterState::Request& request)
   {
      const auto started = std::chrono::steady_clock::now();
      TextWindowsRasterState::Result result;
      result.serial = request.serial;
      result.width = request.width;
      result.height = request.height;

      std::wstring requestedFamily = Utf8ToWide(request.fontName);
      if (requestedFamily.empty()) requestedFamily = L"Segoe UI";
      Gdiplus::FontFamily selected(requestedFamily.c_str());
      const Gdiplus::FontFamily* family = &selected;
      if (selected.GetLastStatus() != Gdiplus::Ok)
      {
         family = Gdiplus::FontFamily::GenericSansSerif();
         result.fontUsed = "GDI+ Generic Sans Serif";
      }
      else
      {
         result.fontUsed = request.fontName.empty() ? "Segoe UI" : request.fontName;
      }

      float usedSize = std::max(4.0f, request.fontSize);
      const float boxW = std::max(8.0f, request.width * std::max(0.05f, request.wrapWidth));
      const float boxH = std::max(8.0f, request.height * std::max(0.05f, request.wrapHeight));
      if (request.fitToBox && !request.text.empty())
      {
         float low = 4.0f;
         float high = usedSize;
         for (int i = 0; i < 10; ++i)
         {
            const float candidate = (low + high) * 0.5f;
            Gdiplus::GraphicsPath test;
            if (!BuildTextPath(request, family, candidate, test)) break;
            Gdiplus::RectF bounds;
            test.GetBounds(&bounds);
            if (bounds.Width <= boxW && bounds.Height <= boxH) low = candidate;
            else high = candidate;
         }
         usedSize = low;
      }
      result.fittedSize = usedSize;

      Gdiplus::Bitmap bitmap(request.width, request.height, PixelFormat32bppARGB);
      if (bitmap.GetLastStatus() != Gdiplus::Ok)
      {
         result.error = "GDI+ could not allocate the text bitmap";
         return result;
      }

      Gdiplus::Graphics graphics(&bitmap);
      graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
      graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
      graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
      graphics.SetPageUnit(Gdiplus::UnitPixel);
      graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
      graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

      Gdiplus::GraphicsPath path;
      if (!BuildTextPath(request, family, usedSize, path))
      {
         result.error = "GDI+ could not shape the selected text/font";
         return result;
      }

      const Gdiplus::Color fill(255, ToByte(request.color[0]),
                                ToByte(request.color[1]), ToByte(request.color[2]));
      const Gdiplus::Color outline(255, ToByte(request.outlineColor[0]),
                                   ToByte(request.outlineColor[1]),
                                   ToByte(request.outlineColor[2]));
      if (!request.outlineOnly && !request.text.empty())
      {
         Gdiplus::SolidBrush brush(fill);
         graphics.FillPath(&brush, &path);
      }
      if (request.outlineWidth > 0.0f && !request.text.empty())
      {
         Gdiplus::Pen pen(outline, std::max(0.5f, request.outlineWidth));
         pen.SetLineJoin(Gdiplus::LineJoinRound);
         graphics.DrawPath(&pen, &path);
      }

      Gdiplus::Rect lockRect(0, 0, request.width, request.height);
      Gdiplus::BitmapData data {};
      if (bitmap.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                          PixelFormat32bppARGB, &data) != Gdiplus::Ok)
      {
         result.error = "GDI+ could not read the rendered text bitmap";
         return result;
      }

      result.rgbaBottomUp.resize((size_t)request.width * request.height * 4);
      size_t visiblePixels = 0;
      for (int y = 0; y < request.height; ++y)
      {
         const auto* source = static_cast<const unsigned char*>(data.Scan0) +
                              (ptrdiff_t)y * data.Stride;
         auto* destination = result.rgbaBottomUp.data() +
            (size_t)(request.height - 1 - y) * request.width * 4;
         for (int x = 0; x < request.width; ++x)
         {
            destination[x * 4 + 0] = source[x * 4 + 2];
            destination[x * 4 + 1] = source[x * 4 + 1];
            destination[x * 4 + 2] = source[x * 4 + 0];
            destination[x * 4 + 3] = source[x * 4 + 3];
            visiblePixels += source[x * 4 + 3] != 0 ? 1u : 0u;
         }
      }
      bitmap.UnlockBits(&data);

      result.visible = visiblePixels != 0;
      if (!request.text.empty() && !result.visible)
         result.error = "GDI+ produced no visible text pixels";
      result.renderMs = MillisecondsSince(started);
      return result;
   }

   void TextRasterWorker(TextWindowsRasterState* state)
   {
      Gdiplus::GdiplusStartupInput input;
      ULONG_PTR token = 0;
      const Gdiplus::Status startup = Gdiplus::GdiplusStartup(&token, &input, nullptr);

      for (;;)
      {
         TextWindowsRasterState::Request request;
         {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->ready.wait(lock, [state] { return state->stop || state->pending.has_value(); });
            if (state->stop) break;
            request = std::move(*state->pending);
            state->pending.reset();
         }

         TextWindowsRasterState::Result result;
         if (startup == Gdiplus::Ok) result = RasterText(request);
         else
         {
            result.serial = request.serial;
            result.width = request.width;
            result.height = request.height;
            result.error = "GDI+ failed to initialise";
         }

         {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->completed = std::move(result);
         }
      }

      if (token != 0) Gdiplus::GdiplusShutdown(token);
   }
}

// Keep construction out of TextNode.h. MSVC otherwise instantiates the
// unique_ptr exception-cleanup path while TextWindowsRasterState is still an
// incomplete forward declaration and rejects default_delete<T>.
TextNode::TextNode() = default;

TextNode::~TextNode()
{
   if (mWindowsRaster)
   {
      {
         std::lock_guard<std::mutex> lock(mWindowsRaster->mutex);
         mWindowsRaster->stop = true;
         mWindowsRaster->pending.reset();
      }
      mWindowsRaster->ready.notify_all();
      if (mWindowsRaster->worker.joinable()) mWindowsRaster->worker.join();
      mWindowsRaster.reset();
   }
   if (mTex != 0) glDeleteTextures(1, &mTex);
}

const std::vector<std::string>& TextNode::AvailableFonts()
{
   return Platform::AvailableFontFamilies();
}

void TextNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;
   mLastCookFrame = frameId;

   if (!mWindowsRaster)
   {
      mWindowsRaster = std::make_unique<TextWindowsRasterState>();
      mWindowsRaster->worker = std::thread(TextRasterWorker, mWindowsRaster.get());
   }

   const bool unchanged = mHasBuilt && text == mBuiltText && fontName == mBuiltFontName &&
      fontSize == mBuiltFontSize && color[0] == mBuiltColor[0] &&
      color[1] == mBuiltColor[1] && color[2] == mBuiltColor[2] &&
      tracking == mBuiltTracking && posX == mBuiltPosX && posY == mBuiltPosY &&
      align == mBuiltAlign && scaleX == mBuiltScaleX && scaleY == mBuiltScaleY &&
      wordWrap == mBuiltWordWrap && wrapWidth == mBuiltWrapWidth &&
      wrapHeight == mBuiltWrapHeight && fitToBox == mBuiltFitToBox &&
      lineSpacing == mBuiltLineSpacing && outlineWidth == mBuiltOutlineWidth &&
      outlineColor[0] == mBuiltOutlineColor[0] &&
      outlineColor[1] == mBuiltOutlineColor[1] &&
      outlineColor[2] == mBuiltOutlineColor[2] &&
      outlineOnly == mBuiltOutlineOnly && width == mBuiltWidth && height == mBuiltHeight;

   if (!unchanged)
   {
      TextWindowsRasterState::Request request;
      request.text = text;
      request.fontName = fontName;
      request.fontSize = fontSize;
      std::copy(color, color + 3, request.color);
      request.tracking = tracking;
      request.posX = posX;
      request.posY = posY;
      request.align = align;
      request.scaleX = scaleX;
      request.scaleY = scaleY;
      request.wordWrap = wordWrap;
      request.wrapWidth = wrapWidth;
      request.wrapHeight = wrapHeight;
      request.fitToBox = fitToBox;
      request.lineSpacing = lineSpacing;
      request.outlineWidth = outlineWidth;
      std::copy(outlineColor, outlineColor + 3, request.outlineColor);
      request.outlineOnly = outlineOnly;
      request.width = std::max(16, (int)width);
      request.height = std::max(16, (int)height);
      {
         std::lock_guard<std::mutex> lock(mWindowsRaster->mutex);
         request.serial = ++mWindowsRaster->submittedSerial;
         // Latest-value-wins: slider drags and typing never build a backlog.
         mWindowsRaster->pending = std::move(request);
      }
      mWindowsRaster->ready.notify_one();

      // Commit the submitted snapshot immediately. A later parameter change
      // replaces the pending work, while an idle frame only polls completion.
      mBuiltText = text;
      mBuiltFontName = fontName;
      mBuiltFontSize = fontSize;
      std::copy(color, color + 3, mBuiltColor);
      mBuiltTracking = tracking;
      mBuiltPosX = posX;
      mBuiltPosY = posY;
      mBuiltAlign = align;
      mBuiltScaleX = scaleX;
      mBuiltScaleY = scaleY;
      mBuiltWordWrap = wordWrap;
      mBuiltWrapWidth = wrapWidth;
      mBuiltWrapHeight = wrapHeight;
      mBuiltFitToBox = fitToBox;
      mBuiltLineSpacing = lineSpacing;
      mBuiltOutlineWidth = outlineWidth;
      std::copy(outlineColor, outlineColor + 3, mBuiltOutlineColor);
      mBuiltOutlineOnly = outlineOnly;
      mBuiltWidth = width;
      mBuiltHeight = height;
      mHasBuilt = true;
   }

   std::optional<TextWindowsRasterState::Result> completed;
   unsigned long long newestSerial = 0;
   {
      std::lock_guard<std::mutex> lock(mWindowsRaster->mutex);
      newestSerial = mWindowsRaster->submittedSerial;
      if (mWindowsRaster->completed)
      {
         completed = std::move(mWindowsRaster->completed);
         mWindowsRaster->completed.reset();
      }
   }
   if (!completed || completed->serial != newestSerial) return;

   if (!completed->error.empty())
   {
      RuntimeLog::Write("TEXT raster failed: backend='GDI+ worker' error='%s' canvas=%dx%d",
                        completed->error.c_str(), completed->width, completed->height);
      return;
   }

   mWidth = completed->width;
   mHeight = completed->height;
   mFittedSize = completed->fittedSize;
   if (mTex == 0) glGenTextures(1, &mTex);
   glBindTexture(GL_TEXTURE_2D, mTex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   if (mUploadedWidth == mWidth && mUploadedHeight == mHeight)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mWidth, mHeight, GL_RGBA,
                      GL_UNSIGNED_BYTE, completed->rgbaBottomUp.data());
   else
   {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mWidth, mHeight, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, completed->rgbaBottomUp.data());
      mUploadedWidth = mWidth;
      mUploadedHeight = mHeight;
   }
   const GLenum uploadError = glGetError();
   glBindTexture(GL_TEXTURE_2D, 0);

   if (uploadError != GL_NO_ERROR)
      RuntimeLog::Write("TEXT OpenGL upload failed: error=0x%04X canvas=%dx%d",
                        (unsigned)uploadError, mWidth, mHeight);
   else
   {
      ++mRevision;
      if (!mReportedRasterSuccess && completed->visible)
      {
         RuntimeLog::Write("TEXT ready: backend='GDI+ software worker' font='%s' size=%.1f canvas=%dx%d render=%.2fms texture=%u",
                           completed->fontUsed.c_str(), completed->fittedSize,
                           mWidth, mHeight, completed->renderMs, mTex);
         mReportedRasterSuccess = true;
      }
   }
}


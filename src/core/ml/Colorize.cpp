#include "Colorize.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>

namespace Ml
{
   namespace Colorize
   {
      namespace
      {
         constexpr int kSide = 256;
      } // namespace

      bool Available() { return ModelRegistry::Available("colorize_clean"); }
      std::string Missing() { return ModelRegistry::Missing("colorize_clean"); }

      void Fallback(const std::vector<uint8_t>& rgba, int w, int h,
                    std::vector<uint8_t>& outRgba)
      {
         outRgba.assign(static_cast<size_t>(std::max(0, w)) * std::max(0, h) * 4, 255);
         if (w < 1 || h < 1 || rgba.size() < static_cast<size_t>(w) * h * 4)
            return;
         for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; i++)
         {
            const uint8_t* p = &rgba[i * 4];
            const float y = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            uint8_t* dst = &outRgba[i * 4];
            dst[0] = static_cast<uint8_t>(std::clamp(y * 1.08f, 0.0f, 255.0f));
            dst[1] = static_cast<uint8_t>(std::clamp(y * 0.96f, 0.0f, 255.0f));
            dst[2] = static_cast<uint8_t>(std::clamp(y * 0.80f, 0.0f, 255.0f));
            dst[3] = p[3];
         }
      }

      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<uint8_t>& outRgba, std::string& err)
      {
         if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4)
         {
            err = "empty picture";
            return false;
         }
         const size_t plane = static_cast<size_t>(kSide) * kSide;
         std::vector<float> lumaIn(plane, 0.0f);
         for (int y = 0; y < kSide; y++)
         {
            const float sy = std::clamp((y + 0.5f) * h / kSide - 0.5f, 0.0f, static_cast<float>(h - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, h - 1);
            const float fy = sy - y0;
            for (int x = 0; x < kSide; x++)
            {
               const float sx = std::clamp((x + 0.5f) * w / kSide - 0.5f, 0.0f, static_cast<float>(w - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, w - 1);
               const float fx = sx - x0;
               auto lum = [&](int px, int py) {
                  const uint8_t* p = &rgba[(static_cast<size_t>(py) * w + px) * 4];
                  return (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
               };
               const float top = lum(x0, y0) + (lum(x1, y0) - lum(x0, y0)) * fx;
               const float bot = lum(x0, y1) + (lum(x1, y1) - lum(x0, y1)) * fx;
               lumaIn[static_cast<size_t>(y) * kSide + x] = top + (bot - top) * fy;
            }
         }

         std::vector<Tracking::OrtRuntime::Tensor> outs;
         const bool ok = ModelRegistry::WithModel("colorize_clean", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            Tracking::OrtRuntime::Input in;
            in.name = "luma";
            in.shape = { 1, 1, kSide, kSide };
            in.f32 = lumaIn.data();
            return rt.RunNamed(m, { in }, outs, e);
         }, err);
         if (!ok)
            return false;
         if (outs.empty() || outs[0].data.size() < plane * 2)
         {
            err = "unexpected Colorize output";
            return false;
         }

         const float* cbPlane = outs[0].data.data();
         const float* crPlane = cbPlane + plane;
         outRgba.assign(static_cast<size_t>(w) * h * 4, 255);
         for (int y = 0; y < h; y++)
         {
            const float sy = std::clamp((y + 0.5f) * kSide / h - 0.5f, 0.0f, static_cast<float>(kSide - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, kSide - 1);
            const float fy = sy - y0;
            for (int x = 0; x < w; x++)
            {
               const float sx = std::clamp((x + 0.5f) * kSide / w - 0.5f, 0.0f, static_cast<float>(kSide - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, kSide - 1);
               const float fx = sx - x0;
               auto sample = [&](const float* p) {
                  const float a = p[static_cast<size_t>(y0) * kSide + x0];
                  const float b = p[static_cast<size_t>(y0) * kSide + x1];
                  const float c = p[static_cast<size_t>(y1) * kSide + x0];
                  const float d = p[static_cast<size_t>(y1) * kSide + x1];
                  return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
               };
               const float cb = sample(cbPlane);
               const float cr = sample(crPlane);
               const uint8_t* src = &rgba[(static_cast<size_t>(y) * w + x) * 4];
               const float luma = (0.299f * src[0] + 0.587f * src[1] + 0.114f * src[2]) / 255.0f;
               const float r = std::clamp(luma + 1.402f * cr, 0.0f, 1.0f);
               const float g = std::clamp(luma - 0.344136f * cb - 0.714136f * cr, 0.0f, 1.0f);
               const float b = std::clamp(luma + 1.772f * cb, 0.0f, 1.0f);
               uint8_t* dst = &outRgba[(static_cast<size_t>(y) * w + x) * 4];
               dst[0] = static_cast<uint8_t>(std::lround(r * 255.0f));
               dst[1] = static_cast<uint8_t>(std::lround(g * 255.0f));
               dst[2] = static_cast<uint8_t>(std::lround(b * 255.0f));
               dst[3] = src[3];
            }
         }
         return true;
      }
   } // namespace Colorize
} // namespace Ml

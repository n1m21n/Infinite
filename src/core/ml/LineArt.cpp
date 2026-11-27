#include "LineArt.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>

namespace Ml
{
   namespace LineArt
   {
      namespace
      {
         constexpr int kSide = 512;
      } // namespace

      bool Available() { return ModelRegistry::Available("lineart_clean"); }
      std::string Missing() { return ModelRegistry::Missing("lineart_clean"); }

      void Fallback(const std::vector<uint8_t>& rgba, int w, int h,
                    std::vector<uint8_t>& outRgba)
      {
         outRgba.assign(static_cast<size_t>(std::max(0, w)) * std::max(0, h) * 4, 255);
         if (w < 3 || h < 3 || rgba.size() < static_cast<size_t>(w) * h * 4)
            return;
         auto lum = [&](int x, int y) -> float {
            const uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
         };
         for (int y = 1; y < h - 1; y++)
         {
            for (int x = 1; x < w - 1; x++)
            {
               const float gx = lum(x + 1, y) - lum(x - 1, y);
               const float gy = lum(x, y + 1) - lum(x, y - 1);
               const float mag = std::sqrt(gx * gx + gy * gy);
               const uint8_t ink = static_cast<uint8_t>(std::lround(std::clamp(255.0f - mag * 1.4f, 0.0f, 255.0f)));
               uint8_t* dst = &outRgba[(static_cast<size_t>(y) * w + x) * 4];
               dst[0] = dst[1] = dst[2] = ink;
               dst[3] = rgba[(static_cast<size_t>(y) * w + x) * 4 + 3];
            }
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
         std::vector<float> chw(plane * 3, 0.0f);
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
               const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
               const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
               const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
               const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const float top = a[k] + (b[k] - a[k]) * fx;
                  const float bot = c[k] + (d[k] - c[k]) * fx;
                  chw[k * plane + static_cast<size_t>(y) * kSide + x] = (top + (bot - top) * fy) / 255.0f;
               }
            }
         }

         std::vector<Tracking::OrtRuntime::Tensor> outs;
         const bool ok = ModelRegistry::WithModel("lineart_clean", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            Tracking::OrtRuntime::Input in;
            in.name = "image";
            in.shape = { 1, 3, kSide, kSide };
            in.f32 = chw.data();
            return rt.RunNamed(m, { in }, outs, e);
         }, err);
         if (!ok)
            return false;
         if (outs.empty() || outs[0].data.size() < plane)
         {
            err = "unexpected LineArt output";
            return false;
         }

         const float* ink = outs[0].data.data();
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
               const float a = ink[static_cast<size_t>(y0) * kSide + x0];
               const float b = ink[static_cast<size_t>(y0) * kSide + x1];
               const float c = ink[static_cast<size_t>(y1) * kSide + x0];
               const float d = ink[static_cast<size_t>(y1) * kSide + x1];
               const float v = (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
               const uint8_t g = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
               uint8_t* dst = &outRgba[(static_cast<size_t>(y) * w + x) * 4];
               dst[0] = dst[1] = dst[2] = g;
               dst[3] = rgba[(static_cast<size_t>(y) * w + x) * 4 + 3];
            }
         }
         return true;
      }
   } // namespace LineArt
} // namespace Ml

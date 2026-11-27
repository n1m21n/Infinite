#include "Upscale.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>

namespace Ml
{
   namespace Upscale
   {
      namespace
      {
         constexpr int kTile = 256;
         constexpr int kPad = 16;
         constexpr int kStep = kTile - kPad * 2; // 224 px core per tile
      } // namespace

      bool Available() { return ModelRegistry::Available("span_2x_clean"); }
      std::string Missing() { return ModelRegistry::Missing("span_2x_clean"); }

      void Fallback2x(const std::vector<uint8_t>& rgba, int w, int h,
                      std::vector<uint8_t>& outRgba, int& outW, int& outH)
      {
         outW = w * 2;
         outH = h * 2;
         outRgba.assign(static_cast<size_t>(std::max(0, outW)) * std::max(0, outH) * 4, 0);
         if (w < 1 || h < 1 || rgba.size() < static_cast<size_t>(w) * h * 4)
            return;
         for (int y = 0; y < outH; y++)
         {
            const float sy = std::clamp((y + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<float>(h - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, h - 1);
            const float fy = sy - y0;
            for (int x = 0; x < outW; x++)
            {
               const float sx = std::clamp((x + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<float>(w - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, w - 1);
               const float fx = sx - x0;
               const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
               const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
               const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
               const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
               uint8_t* dst = &outRgba[(static_cast<size_t>(y) * outW + x) * 4];
               for (int k = 0; k < 4; k++)
               {
                  const float top = a[k] + (b[k] - a[k]) * fx;
                  const float bot = c[k] + (d[k] - c[k]) * fx;
                  dst[k] = static_cast<uint8_t>(std::lround(std::clamp(top + (bot - top) * fy, 0.0f, 255.0f)));
               }
            }
         }
      }

      bool Run2xNow(const std::vector<uint8_t>& rgba, int w, int h,
                    std::vector<uint8_t>& outRgba, int& outW, int& outH,
                    std::string& err)
      {
         if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4)
         {
            err = "empty picture";
            return false;
         }
         // Seed alpha (and fallback pixels) via 2x bilinear.
         Fallback2x(rgba, w, h, outRgba, outW, outH);

         const size_t tPlane = static_cast<size_t>(kTile) * kTile;
         const int outTile = kTile * 2;
         const size_t oPlane = static_cast<size_t>(outTile) * outTile;
         std::vector<float> chw(tPlane * 3, 0.0f);

         const bool ok = ModelRegistry::WithModel("span_2x_clean", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            for (int ty = 0; ty < h; ty += kStep)
            {
               for (int tx = 0; tx < w; tx += kStep)
               {
                  const int x0 = tx - kPad;
                  const int y0 = ty - kPad;
                  for (int y = 0; y < kTile; y++)
                  {
                     const int sy = std::clamp(y0 + y, 0, h - 1);
                     for (int x = 0; x < kTile; x++)
                     {
                        const int sx = std::clamp(x0 + x, 0, w - 1);
                        const uint8_t* p = &rgba[(static_cast<size_t>(sy) * w + sx) * 4];
                        for (int k = 0; k < 3; k++)
                           chw[k * tPlane + static_cast<size_t>(y) * kTile + x] = p[k] / 255.0f;
                     }
                  }
                  Tracking::OrtRuntime::Input in;
                  in.name = "input";
                  in.shape = { 1, 3, kTile, kTile };
                  in.f32 = chw.data();
                  std::vector<Tracking::OrtRuntime::Tensor> outs;
                  if (!rt.RunNamed(m, { in }, outs, e))
                     return false;
                  if (outs.empty() || outs[0].data.size() < oPlane * 3)
                  {
                     e = "unexpected SPAN 2x output";
                     return false;
                  }
                  const float* pred = outs[0].data.data();
                  const int coreY0 = (ty == 0) ? 0 : kPad * 2;
                  const int coreX0 = (tx == 0) ? 0 : kPad * 2;
                  const int coreY1 = std::min(outTile, (h - y0) * 2);
                  const int coreX1 = std::min(outTile, (w - x0) * 2);
                  for (int y = coreY0; y < coreY1; y++)
                  {
                     const int dy = y0 * 2 + y;
                     if (dy < 0 || dy >= outH) continue;
                     for (int x = coreX0; x < coreX1; x++)
                     {
                        const int dx = x0 * 2 + x;
                        if (dx < 0 || dx >= outW) continue;
                        uint8_t* dst = &outRgba[(static_cast<size_t>(dy) * outW + dx) * 4];
                        for (int k = 0; k < 3; k++)
                        {
                           const float v = pred[k * oPlane + static_cast<size_t>(y) * outTile + x];
                           dst[k] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                        }
                     }
                  }
               }
            }
            return true;
         }, err);
         return ok;
      }
   } // namespace Upscale
} // namespace Ml

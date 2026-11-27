#include "CleanInpaint.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>

namespace Ml
{
   namespace CleanInpaint
   {
      namespace
      {
         constexpr int kSide = 512;
      } // namespace

      bool Available() { return ModelRegistry::Available("migan_clean_512"); }
      std::string Missing() { return ModelRegistry::Missing("migan_clean_512"); }

      bool RunNow(const std::vector<uint8_t>& rgba, const std::vector<uint8_t>& mask,
                  int w, int h, std::vector<uint8_t>& outRgba, std::string& err)
      {
         if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4 || mask.size() < static_cast<size_t>(w) * h)
         {
            err = "empty picture";
            return false;
         }
         const size_t plane = static_cast<size_t>(kSide) * kSide;
         std::vector<float> imgIn(plane * 3, 0.0f), maskIn(plane, 0.0f);
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
                  imgIn[k * plane + static_cast<size_t>(y) * kSide + x] = (top + (bot - top) * fy) / 255.0f;
               }
               const int mx = std::clamp(x * w / kSide, 0, w - 1);
               const int my = std::clamp(y * h / kSide, 0, h - 1);
               maskIn[static_cast<size_t>(y) * kSide + x] = (mask[static_cast<size_t>(my) * w + mx] > 127) ? 1.0f : 0.0f;
            }
         }

         std::vector<Tracking::OrtRuntime::Tensor> outs;
         const bool ok = ModelRegistry::WithModel("migan_clean_512", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            std::vector<Tracking::OrtRuntime::Input> ins(2);
            ins[0].name = "image"; ins[0].shape = { 1, 3, kSide, kSide }; ins[0].f32 = imgIn.data();
            ins[1].name = "mask";  ins[1].shape = { 1, 1, kSide, kSide }; ins[1].f32 = maskIn.data();
            return rt.RunNamed(m, ins, outs, e);
         }, err);
         if (!ok)
            return false;
         if (outs.empty() || outs[0].data.size() < plane * 3)
         {
            err = "unexpected CleanInpaint output";
            return false;
         }

         const float* pred = outs[0].data.data();
         outRgba = rgba;
         for (int y = 0; y < h; y++)
         {
            const float sy = std::clamp((y + 0.5f) * kSide / h - 0.5f, 0.0f, static_cast<float>(kSide - 1));
            const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, kSide - 1);
            const float fy = sy - y0;
            for (int x = 0; x < w; x++)
            {
               if (mask[static_cast<size_t>(y) * w + x] <= 127)
                  continue;
               const float sx = std::clamp((x + 0.5f) * kSide / w - 0.5f, 0.0f, static_cast<float>(kSide - 1));
               const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, kSide - 1);
               const float fx = sx - x0;
               uint8_t* dst = &outRgba[(static_cast<size_t>(y) * w + x) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const float a = pred[k * plane + static_cast<size_t>(y0) * kSide + x0];
                  const float b = pred[k * plane + static_cast<size_t>(y0) * kSide + x1];
                  const float c = pred[k * plane + static_cast<size_t>(y1) * kSide + x0];
                  const float d = pred[k * plane + static_cast<size_t>(y1) * kSide + x1];
                  const float v = (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
                  dst[k] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
               }
            }
         }
         return true;
      }
   } // namespace CleanInpaint
} // namespace Ml

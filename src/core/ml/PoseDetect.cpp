#include "PoseDetect.h"

#include "ModelRegistry.h"

#include <algorithm>
#include <cmath>

namespace Ml
{
   namespace PoseDetect
   {
      namespace
      {
         constexpr int kInW = 192;
         constexpr int kInH = 256;
         constexpr int kHmW = 48;
         constexpr int kHmH = 64;
         constexpr int kKpts = 17;

         const char* kCocoNames[kKpts] = {
            "nose", "left_eye", "right_eye", "left_ear", "right_ear",
            "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
            "left_wrist", "right_wrist", "left_hip", "right_hip",
            "left_knee", "right_knee", "left_ankle", "right_ankle"
         };
      } // namespace

      bool Available() { return ModelRegistry::Available("rtmpose_s_coco_clean"); }
      std::string Missing() { return ModelRegistry::Missing("rtmpose_s_coco_clean"); }

      bool DetectNow(const std::vector<uint8_t>& rgba, int w, int h,
                     int cropX, int cropY, int cropW, int cropH,
                     std::vector<Keypoint>& outKeypoints, std::string& err)
      {
         outKeypoints.clear();
         if (w < 2 || h < 2 || rgba.size() < static_cast<size_t>(w) * h * 4)
         {
            err = "empty picture";
            return false;
         }
         if (cropW <= 0 || cropH <= 0)
         {
            cropX = 0;
            cropY = 0;
            cropW = w;
            cropH = h;
         }
         const int x0c = std::clamp(cropX, 0, w - 1);
         const int y0c = std::clamp(cropY, 0, h - 1);
         const int x1c = std::clamp(cropX + cropW, x0c + 1, w);
         const int y1c = std::clamp(cropY + cropH, y0c + 1, h);
         const int cw = x1c - x0c;
         const int ch = y1c - y0c;

         const size_t plane = static_cast<size_t>(kInW) * kInH;
         std::vector<float> imgIn(plane * 3, 0.0f);
         for (int y = 0; y < kInH; y++)
         {
            const float sy = y0c + std::clamp((y + 0.5f) * ch / kInH - 0.5f, 0.0f, static_cast<float>(ch - 1));
            const int y0 = std::clamp(static_cast<int>(sy), 0, h - 1);
            const int y1 = std::min(y0 + 1, h - 1);
            const float fy = sy - static_cast<int>(sy);
            for (int x = 0; x < kInW; x++)
            {
               const float sx = x0c + std::clamp((x + 0.5f) * cw / kInW - 0.5f, 0.0f, static_cast<float>(cw - 1));
               const int x0 = std::clamp(static_cast<int>(sx), 0, w - 1);
               const int x1 = std::min(x0 + 1, w - 1);
               const float fx = sx - static_cast<int>(sx);
               const uint8_t* a = &rgba[(static_cast<size_t>(y0) * w + x0) * 4];
               const uint8_t* b = &rgba[(static_cast<size_t>(y0) * w + x1) * 4];
               const uint8_t* c = &rgba[(static_cast<size_t>(y1) * w + x0) * 4];
               const uint8_t* d = &rgba[(static_cast<size_t>(y1) * w + x1) * 4];
               for (int k = 0; k < 3; k++)
               {
                  const float top = a[k] + (b[k] - a[k]) * fx;
                  const float bot = c[k] + (d[k] - c[k]) * fx;
                  imgIn[k * plane + static_cast<size_t>(y) * kInW + x] = (top + (bot - top) * fy) / 255.0f;
               }
            }
         }

         std::vector<Tracking::OrtRuntime::Tensor> outs;
         const bool ok = ModelRegistry::WithModel("rtmpose_s_coco_clean", [&](Tracking::OrtRuntime& rt, Tracking::OrtModel* m, std::string& e) {
            std::vector<Tracking::OrtRuntime::Input> ins(1);
            ins[0].name = "input";
            ins[0].shape = { 1, 3, kInH, kInW };
            ins[0].f32 = imgIn.data();
            return rt.RunNamed(m, ins, outs, e);
         }, err);
         if (!ok || outs.empty() || outs[0].data.size() < static_cast<size_t>(kKpts) * kHmH * kHmW)
         {
            if (err.empty()) err = "pose inference failed";
            return false;
         }

         const float* hm = outs[0].data.data();
         const size_t hmPlane = static_cast<size_t>(kHmH) * kHmW;
         outKeypoints.resize(kKpts);
         for (int k = 0; k < kKpts; k++)
         {
            const float* p = hm + k * hmPlane;
            int bestIdx = 0;
            float bestVal = p[0];
            for (int i = 1; i < static_cast<int>(hmPlane); i++)
            {
               if (p[i] > bestVal)
               {
                  bestVal = p[i];
                  bestIdx = i;
               }
            }
            const int py = bestIdx / kHmW;
            const int px = bestIdx % kHmW;
            outKeypoints[k] = {
               x0c + (px + 0.5f) / static_cast<float>(kHmW) * cw,
               y0c + (py + 0.5f) / static_cast<float>(kHmH) * ch,
               bestVal,
               kCocoNames[k]
            };
         }
         return true;
      }
   } // namespace PoseDetect
} // namespace Ml

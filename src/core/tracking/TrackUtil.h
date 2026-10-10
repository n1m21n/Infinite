#pragma once

// Small image and geometry helpers shared by the hand, face and pose trackers.

#include <algorithm>
#include <cmath>
#include <vector>

namespace Tracking
{
   static constexpr float kPi = 3.14159265358979f;

   // SSD anchors of the MediaPipe detectors (strides 8,16,16,16, fixed size) for a square input.
   // The palm model (192) gives 2016 anchors, the face model (128) 896.
   inline void BuildSsdAnchors(int input, std::vector<float>& a)
   {
      a.clear();
      const int strides[4] = {8, 16, 16, 16};
      int layer = 0;
      while (layer < 4)
      {
         int last = layer;
         while (last < 4 && strides[last] == strides[layer]) ++last;
         const int fm = (input + strides[layer] - 1) / strides[layer];
         const int per = 2 * (last - layer);
         for (int y = 0; y < fm; ++y)
            for (int x = 0; x < fm; ++x)
               for (int k = 0; k < per; ++k)
               {
                  a.push_back((x + 0.5f) / fm);
                  a.push_back((y + 0.5f) / fm);
               }
         layer = last;
      }
   }

   inline void SampleBilinear(const unsigned char* rgb, int w, int h, float x, float y, float* out)
   {
      if (x < 0 || y < 0 || x > w - 1 || y > h - 1)
      {
         out[0] = out[1] = out[2] = 0.f;
         return;
      }
      const int x0 = (int)x, y0 = (int)y;
      const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
      const float fx = x - x0, fy = y - y0;
      for (int c = 0; c < 3; ++c)
      {
         const float a = rgb[(y0 * w + x0) * 3 + c], b = rgb[(y0 * w + x1) * 3 + c];
         const float d = rgb[(y1 * w + x0) * 3 + c], e = rgb[(y1 * w + x1) * 3 + c];
         out[c] = ((a + (b - a) * fx) * (1 - fy) + (d + (e - d) * fx) * fy) * (1.f / 255.f);
      }
   }

   // Fills dst (n*n*3 floats) with the rotated square ROI. Source point for crop
   // pixel (u,v) = center + R * ((u/n - .5)*size, (v/n - .5)*size).
   inline void CropRoi(const unsigned char* rgb, int w, int h, const float roi[4], int n, float* dst)
   {
      const float c = std::cos(roi[3]), s = std::sin(roi[3]);
      for (int v = 0; v < n; ++v)
         for (int u = 0; u < n; ++u)
         {
            const float dx = ((u + 0.5f) / n - 0.5f) * roi[2];
            const float dy = ((v + 0.5f) / n - 0.5f) * roi[2];
            SampleBilinear(rgb, w, h, roi[0] + c * dx - s * dy - 0.5f, roi[1] + s * dx + c * dy - 0.5f,
                           dst + (v * n + u) * 3);
         }
   }

   inline float Sigmoid(float x) { return 1.f / (1.f + std::exp(-x)); }

   inline float NormalizeAngle(float a)
   {
      while (a > kPi) a -= 2 * kPi;
      while (a < -kPi) a += 2 * kPi;
      return a;
   }


   // Letterbox the frame into an n*n float square (black bars); pad and scale are reported so detector
   // coordinates can be mapped back to source pixels.
   inline void Letterbox(const unsigned char* rgb, int w, int h, int n, std::vector<float>& in,
                         float& sc, float& padX, float& padY)
   {
      sc = (float)n / (float)std::max(w, h);
      padX = (n - w * sc) * 0.5f;
      padY = (n - h * sc) * 0.5f;
      in.assign((size_t)n * n * 3, 0.f);
      for (int y = 0; y < n; ++y)
         for (int x = 0; x < n; ++x)
         {
            const float sx = (x + 0.5f - padX) / sc - 0.5f, sy = (y + 0.5f - padY) / sc - 0.5f;
            SampleBilinear(rgb, w, h, sx, sy, &in[((size_t)y * n + x) * 3]);
         }
   }
}

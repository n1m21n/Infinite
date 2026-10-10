#include "HandTracker.h"

#include <algorithm>
#include <cmath>

namespace Tracking
{
   static constexpr int kDet = 192;
   static constexpr int kLm = 224;
   static constexpr float kPi = 3.14159265358979f;

   static void BuildAnchors(std::vector<float>& a)
   {
      // SSD anchors of the palm model: strides 8,16,16,16 on a 192 input, two
      // per cell, fixed size.
      a.clear();
      const int strides[4] = {8, 16, 16, 16};
      int layer = 0;
      while (layer < 4)
      {
         int last = layer;
         while (last < 4 && strides[last] == strides[layer]) ++last;
         const int fm = (kDet + strides[layer] - 1) / strides[layer];
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

   bool HandTracker::Open(OrtRuntime& rt, const std::string& packDir, bool preferGpu, std::string& err)
   {
      Close();
      m_rt = &rt;
      m_det = rt.OpenModel(packDir + "/models/hand_detector.onnx", false, m_gpuDet, err);
      if (!m_det) return false;
      m_lm = rt.OpenModel(packDir + "/models/hand_landmarks_detector.onnx", preferGpu, m_gpuLm, err);
      if (!m_lm) { Close(); return false; }
      BuildAnchors(m_anchors);
      m_haveRoi = false;
      return true;
   }

   void HandTracker::Close()
   {
      if (m_rt) { m_rt->CloseModel(m_det); m_rt->CloseModel(m_lm); }
      m_det = m_lm = nullptr;
      m_rt = nullptr;
   }

   static void SampleBilinear(const unsigned char* rgb, int w, int h, float x, float y, float* out)
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
   static void CropRoi(const unsigned char* rgb, int w, int h, const float roi[4], int n, float* dst)
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

   static float Sigmoid(float x) { return 1.f / (1.f + std::exp(-x)); }

   static float NormalizeAngle(float a)
   {
      while (a > kPi) a -= 2 * kPi;
      while (a < -kPi) a += 2 * kPi;
      return a;
   }

   bool HandTracker::Process(const unsigned char* rgb, int w, int h, HandResult& out, std::string& err)
   {
      out = HandResult{};
      if (!m_det || !m_lm) { err = "not open"; return false; }
      std::vector<float> in;
      std::vector<OrtRuntime::Tensor> t;
      std::vector<std::string> names;

      if (!m_haveRoi)
      {
         // Letterbox into the square detector input.
         const float sc = (float)kDet / (float)std::max(w, h);
         const float padX = (kDet - w * sc) * 0.5f, padY = (kDet - h * sc) * 0.5f;
         in.assign((size_t)kDet * kDet * 3, 0.f);
         for (int y = 0; y < kDet; ++y)
            for (int x = 0; x < kDet; ++x)
            {
               const float sx = (x + 0.5f - padX) / sc - 0.5f, sy = (y + 0.5f - padY) / sc - 0.5f;
               SampleBilinear(rgb, w, h, sx, sy, &in[((size_t)y * kDet + x) * 3]);
            }
         if (!m_rt->Run(m_det, in.data(), {1, kDet, kDet, 3}, t, names, err)) return false;
         const OrtRuntime::Tensor* boxes = nullptr;
         const OrtRuntime::Tensor* scores = nullptr;
         for (auto& x : t)
         {
            if (x.shape.size() == 3 && x.shape[2] == 18) boxes = &x;
            else if (x.shape.size() == 3 && x.shape[2] == 1) scores = &x;
         }
         if (!boxes || !scores || (size_t)boxes->shape[1] != m_anchors.size() / 2) { err = "unexpected detector outputs"; return false; }
         int best = -1;
         float bestS = 0.5f;
         const int na = (int)boxes->shape[1];
         for (int i = 0; i < na; ++i)
         {
            const float s = Sigmoid(scores->data[i]);
            if (s > bestS) { bestS = s; best = i; }
         }
         if (best < 0) return true;  // no hand
         const float* r = &boxes->data[(size_t)best * 18];
         const float ax = m_anchors[best * 2], ay = m_anchors[best * 2 + 1];
         const float bw = r[2] / kDet, bh = r[3] / kDet;
         const float cx = r[0] / kDet + ax, cy = r[1] / kDet + ay;
         const float k0x = r[4] / kDet + ax, k0y = r[5] / kDet + ay;
         const float k2x = r[8] / kDet + ax, k2y = r[9] / kDet + ay;
         // detector-space (0..1 of the letterboxed square) -> source pixels
         auto toX = [&](float v) { return (v * kDet - padX) / sc; };
         auto toY = [&](float v) { return (v * kDet - padY) / sc; };
         const float rot = NormalizeAngle(kPi * 0.5f - std::atan2(-(toY(k2y) - toY(k0y)), toX(k2x) - toX(k0x)));
         const float pw = bw * kDet / sc, ph = bh * kDet / sc;
         const float pcx = toX(cx), pcy = toY(cy);
         const float shiftY = -0.5f;
         const float cx2 = pcx - ph * shiftY * std::sin(rot);
         const float cy2 = pcy + ph * shiftY * std::cos(rot);
         m_roi[0] = cx2; m_roi[1] = cy2;
         m_roi[2] = std::max(pw, ph) * 2.6f;
         m_roi[3] = rot;
         out.redetected = true;
         out.score = bestS;
      }

      in.assign((size_t)kLm * kLm * 3, 0.f);
      CropRoi(rgb, w, h, m_roi, kLm, in.data());
      if (!m_rt->Run(m_lm, in.data(), {1, kLm, kLm, 3}, t, names, err)) return false;
      const OrtRuntime::Tensor *lm = nullptr, *pres = nullptr;
      for (size_t i = 0; i < t.size(); ++i)
      {
         if (names[i] == "Identity") lm = &t[i];
         else if (names[i] == "Identity_1") pres = &t[i];
      }
      if (!lm || lm->data.size() < 63 || !pres || pres->data.empty()) { err = "unexpected landmark outputs"; return false; }
      const float presence = pres->data[0];
      if (!out.redetected) out.score = presence;
      if (presence < 0.5f) { m_haveRoi = false; return true; }

      const float c = std::cos(m_roi[3]), s = std::sin(m_roi[3]);
      for (int i = 0; i < 21; ++i)
      {
         const float dx = (lm->data[i * 3] / kLm - 0.5f) * m_roi[2];
         const float dy = (lm->data[i * 3 + 1] / kLm - 0.5f) * m_roi[2];
         out.lm[i][0] = m_roi[0] + c * dx - s * dy;
         out.lm[i][1] = m_roi[1] + s * dx + c * dy;
         out.lm[i][2] = lm->data[i * 3 + 2];
      }
      out.found = true;
      for (int i = 0; i < 4; ++i) out.roi[i] = m_roi[i];

      // Next frame's ROI from these landmarks: wrist (0) and middle MCP (9) give
      // the rotation, the landmark bounding box gives the size.
      const float rot = NormalizeAngle(kPi * 0.5f - std::atan2(-(out.lm[9][1] - out.lm[0][1]), out.lm[9][0] - out.lm[0][0]));
      const float rc = std::cos(rot), rs = std::sin(rot);
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      for (int i = 0; i < 21; ++i)
      {
         // box in the rotated frame, as MediaPipe does
         const float rx = rc * out.lm[i][0] + rs * out.lm[i][1];
         const float ry = -rs * out.lm[i][0] + rc * out.lm[i][1];
         minX = std::min(minX, rx); maxX = std::max(maxX, rx);
         minY = std::min(minY, ry); maxY = std::max(maxY, ry);
      }
      const float mx = (minX + maxX) * 0.5f, my = (minY + maxY) * 0.5f;
      const float bw = maxX - minX, bh = maxY - minY;
      const float myShift = my + bh * -0.1f;
      m_roi[0] = rc * mx - rs * myShift;
      m_roi[1] = rs * mx + rc * myShift;
      m_roi[2] = std::max(bw, bh) * 2.0f;
      m_roi[3] = rot;
      m_haveRoi = true;
      return true;
   }
}

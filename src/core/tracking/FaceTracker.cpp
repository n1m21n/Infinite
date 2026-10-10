#include "FaceTracker.h"

#include "TrackUtil.h"

namespace Tracking
{
   static constexpr int kDet = 128;
   static constexpr int kLm = 256;
   static constexpr int kPoints = 478;

   bool FaceTracker::Open(OrtRuntime& rt, const std::string& packDir, bool preferGpu, std::string& err)
   {
      Close();
      m_rt = &rt;
      m_det = rt.OpenModel(packDir + "/models/face_detector.onnx", false, m_gpuDet, err);
      if (!m_det) return false;
      m_lm = rt.OpenModel(packDir + "/models/face_landmarks_detector.onnx", preferGpu, m_gpuLm, err);
      if (!m_lm) { Close(); return false; }
      BuildSsdAnchors(kDet, m_anchors);
      m_haveRoi = false;
      return true;
   }

   void FaceTracker::Close()
   {
      if (m_rt) { m_rt->CloseModel(m_det); m_rt->CloseModel(m_lm); }
      m_det = m_lm = nullptr;
      m_rt = nullptr;
   }

   bool FaceTracker::Process(const unsigned char* rgb, int w, int h, FaceResult& out, std::string& err)
   {
      out = FaceResult{};
      if (!m_det || !m_lm) { err = "not open"; return false; }
      std::vector<float> in;
      std::vector<OrtRuntime::Tensor> t;
      std::vector<std::string> names;

      if (!m_haveRoi)
      {
         float sc, padX, padY;
         Letterbox(rgb, w, h, kDet, in, sc, padX, padY);
         if (!m_rt->Run(m_det, in.data(), {1, kDet, kDet, 3}, t, names, err)) return false;
         const OrtRuntime::Tensor* boxes = nullptr;
         const OrtRuntime::Tensor* scores = nullptr;
         for (auto& x : t)
         {
            if (x.shape.size() == 3 && x.shape[2] == 16) boxes = &x;
            else if (x.shape.size() == 3 && x.shape[2] == 1) scores = &x;
         }
         if (!boxes || !scores || (size_t)boxes->shape[1] != m_anchors.size() / 2) { err = "unexpected face detector outputs"; return false; }
         int best = -1;
         float bestS = 0.5f;
         const int na = (int)boxes->shape[1];
         for (int i = 0; i < na; ++i)
         {
            const float s = Sigmoid(scores->data[i]);
            if (s > bestS) { bestS = s; best = i; }
         }
         if (best < 0) return true;  // no face
         const float* r = &boxes->data[(size_t)best * 16];
         const float ax = m_anchors[best * 2], ay = m_anchors[best * 2 + 1];
         auto toX = [&](float v) { return (v * kDet - padX) / sc; };
         auto toY = [&](float v) { return (v * kDet - padY) / sc; };
         const float cx = toX(r[0] / kDet + ax), cy = toY(r[1] / kDet + ay);
         const float bw = r[2] / sc, bh = r[3] / sc;
         // keypoints 0 and 1 are the eyes: the line between them sets the rotation
         const float ex0 = toX(r[4] / kDet + ax), ey0 = toY(r[5] / kDet + ay);
         const float ex1 = toX(r[6] / kDet + ax), ey1 = toY(r[7] / kDet + ay);
         m_roi[0] = cx; m_roi[1] = cy;
         m_roi[2] = std::max(bw, bh) * 1.5f;
         m_roi[3] = NormalizeAngle(-std::atan2(-(ey1 - ey0), ex1 - ex0));
         out.redetected = true;
         out.score = bestS;
      }

      in.assign((size_t)kLm * kLm * 3, 0.f);
      CropRoi(rgb, w, h, m_roi, kLm, in.data());
      if (!m_rt->Run(m_lm, in.data(), {1, kLm, kLm, 3}, t, names, err)) return false;
      const OrtRuntime::Tensor *lm = nullptr, *flag = nullptr;
      for (size_t i = 0; i < t.size(); ++i)
      {
         if (t[i].data.size() == (size_t)kPoints * 3) lm = &t[i];
         else if (names[i] == "Identity_1" && t[i].data.size() == 1) flag = &t[i];
      }
      if (!lm || !flag) { err = "unexpected face landmark outputs"; return false; }
      const float presence = Sigmoid(flag->data[0]);
      if (!out.redetected) out.score = presence;
      if (presence < 0.5f) { m_haveRoi = false; return true; }

      const float c = std::cos(m_roi[3]), s = std::sin(m_roi[3]);
      for (int i = 0; i < kPoints; ++i)
      {
         const float dx = (lm->data[i * 3] / kLm - 0.5f) * m_roi[2];
         const float dy = (lm->data[i * 3 + 1] / kLm - 0.5f) * m_roi[2];
         out.lm[i][0] = m_roi[0] + c * dx - s * dy;
         out.lm[i][1] = m_roi[1] + s * dx + c * dy;
         out.lm[i][2] = lm->data[i * 3 + 2];
      }
      out.found = true;

      // Next ROI: rotation from the outer eye corners (33, 263), size from the landmark box in that frame.
      const float rot = NormalizeAngle(-std::atan2(-(out.lm[263][1] - out.lm[33][1]), out.lm[263][0] - out.lm[33][0]));
      const float rc = std::cos(rot), rs = std::sin(rot);
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      for (int i = 0; i < 468; ++i)
      {
         const float rx = rc * out.lm[i][0] + rs * out.lm[i][1];
         const float ry = -rs * out.lm[i][0] + rc * out.lm[i][1];
         minX = std::min(minX, rx); maxX = std::max(maxX, rx);
         minY = std::min(minY, ry); maxY = std::max(maxY, ry);
      }
      const float mx = (minX + maxX) * 0.5f, my = (minY + maxY) * 0.5f;
      m_roi[0] = rc * mx - rs * my;
      m_roi[1] = rs * mx + rc * my;
      m_roi[2] = std::max(maxX - minX, maxY - minY) * 1.5f;
      m_roi[3] = rot;
      m_haveRoi = true;
      return true;
   }
}

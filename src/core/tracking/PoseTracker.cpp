#include "PoseTracker.h"

#include "TrackUtil.h"

namespace Tracking
{
   static constexpr int kLm = 256;
   static constexpr int kStride = 5;     // x, y, z, visibility, presence per landmark
   static constexpr int kPoints = 33;    // + 2 alignment points (33 centre, 34 scale)

   static float Sig(float x) { return Sigmoid(x); }

   bool PoseTracker::Open(OrtRuntime& rt, const std::string& packDir, bool preferGpu, std::string& err)
   {
      Close();
      m_rt = &rt;
      m_lm = rt.OpenModel(packDir + "/models/pose_landmarks_detector.onnx", preferGpu, m_gpu, err);
      if (!m_lm) { Close(); return false; }
      m_haveRoi = false;
      return true;
   }

   void PoseTracker::Close()
   {
      if (m_rt) m_rt->CloseModel(m_lm);
      m_lm = nullptr;
      m_rt = nullptr;
   }

   bool PoseTracker::Process(const unsigned char* rgb, int w, int h, PoseResult& out, std::string& err)
   {
      out = PoseResult{};
      if (!m_lm) { err = "not open"; return false; }
      if (!m_haveRoi)
      {
         m_roi[0] = w * 0.5f; m_roi[1] = h * 0.5f;
         m_roi[2] = (float)std::max(w, h);
         m_roi[3] = 0.f;
      }
      std::vector<float> in((size_t)kLm * kLm * 3, 0.f);
      CropRoi(rgb, w, h, m_roi, kLm, in.data());
      std::vector<OrtRuntime::Tensor> t;
      std::vector<std::string> names;
      if (!m_rt->Run(m_lm, in.data(), {1, kLm, kLm, 3}, t, names, err)) return false;
      const OrtRuntime::Tensor *lm = nullptr, *flag = nullptr;
      for (size_t i = 0; i < t.size(); ++i)
      {
         if (names[i] == "Identity" && t[i].data.size() >= (size_t)(kPoints + 2) * kStride) lm = &t[i];
         else if (names[i] == "Identity_1" && t[i].data.size() == 1) flag = &t[i];
      }
      if (!lm || !flag) { err = "unexpected pose outputs"; return false; }
      out.score = flag->data[0];
      if (out.score < 0.5f) { m_haveRoi = false; return true; }

      const float c = std::cos(m_roi[3]), s = std::sin(m_roi[3]);
      auto toImage = [&](int i, float& x, float& y)
      {
         const float dx = (lm->data[i * kStride] / kLm - 0.5f) * m_roi[2];
         const float dy = (lm->data[i * kStride + 1] / kLm - 0.5f) * m_roi[2];
         x = m_roi[0] + c * dx - s * dy;
         y = m_roi[1] + s * dx + c * dy;
      };
      for (int i = 0; i < kPoints; ++i)
      {
         toImage(i, out.lm[i][0], out.lm[i][1]);
         out.lm[i][2] = lm->data[i * kStride + 2];
         out.vis[i] = Sig(lm->data[i * kStride + 3]);
      }
      out.found = true;

      // Next ROI from the alignment points: 33 is the body centre, 34 a point straight above it.
      float cx, cy, tx, ty;
      toImage(33, cx, cy);
      toImage(34, tx, ty);
      const float radius = std::hypot(tx - cx, ty - cy);
      if (radius < 4.f) { m_haveRoi = false; return true; }
      m_roi[0] = cx; m_roi[1] = cy;
      m_roi[2] = radius * 2.f * 1.25f;
      m_roi[3] = NormalizeAngle(kPi * 0.5f - std::atan2(-(ty - cy), tx - cx));
      m_haveRoi = true;
      return true;
   }
}

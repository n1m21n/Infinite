#pragma once

#include "OrtRuntime.h"

#include <string>
#include <vector>

namespace Tracking
{
   struct FaceResult
   {
      bool found = false;
      float score = 0.f;
      float lm[478][3] = {};   // image pixels (x, y); z in landmark-crop pixels (468..477 are the two irises)
      bool redetected = false;
   };

   // MediaPipe face pipeline on ONNX models: BlazeFace detector -> rotated ROI -> 478 landmarks. Later
   // frames take the ROI from the previous landmarks and skip the detector.
   class FaceTracker
   {
   public:
      bool Open(OrtRuntime& rt, const std::string& packDir, bool preferGpu, std::string& err);
      void Close();
      bool Process(const unsigned char* rgb, int w, int h, FaceResult& out, std::string& err);
      void Reset() { m_haveRoi = false; }

   private:
      OrtRuntime* m_rt = nullptr;
      OrtModel* m_det = nullptr;
      OrtModel* m_lm = nullptr;
      bool m_gpuDet = false, m_gpuLm = false;
      bool m_haveRoi = false;
      float m_roi[4] = {};  // cx, cy, size, rot
      std::vector<float> m_anchors;
   };
}

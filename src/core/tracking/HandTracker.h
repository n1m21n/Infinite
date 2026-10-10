#pragma once

#include "OrtRuntime.h"

#include <string>
#include <vector>

namespace Tracking
{
   struct HandResult
   {
      bool found = false;
      float score = 0.f;        // palm score on detection frames, presence on tracking frames
      float lm[21][3] = {};     // image pixels (x, y), z in landmark-crop pixels
      float roi[5] = {};        // cx, cy, size, rotation (rad), valid
      bool redetected = false;  // true when the palm detector ran this frame
   };

   // MediaPipe hand pipeline on ONNX models: palm detector -> rotated ROI ->
   // 21 landmarks; later frames reuse the previous landmarks as the ROI and skip
   // the detector. Input is RGB8, row-major, stride = w*3.
   class HandTracker
   {
   public:
      bool Open(OrtRuntime& rt, const std::string& packDir, bool preferGpu, std::string& err);
      void Close();
      bool Process(const unsigned char* rgb, int w, int h, HandResult& out, std::string& err);
      void Reset() { m_haveRoi = false; }
      bool UsedGpuLandmark() const { return m_gpuLm; }
      bool UsedGpuDetector() const { return m_gpuDet; }

   private:
      OrtRuntime* m_rt = nullptr;
      OrtModel* m_det = nullptr;
      OrtModel* m_lm = nullptr;
      bool m_gpuDet = false, m_gpuLm = false;
      bool m_haveRoi = false;
      float m_roi[4] = {};  // cx, cy, size, rot
      std::vector<float> m_anchors;  // x,y per anchor
   };
}

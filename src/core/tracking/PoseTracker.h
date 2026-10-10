#pragma once

#include "OrtRuntime.h"

#include <string>

namespace Tracking
{
   struct PoseResult
   {
      bool found = false;
      float score = 0.f;
      float lm[33][3] = {};   // image pixels (x, y), z in landmark-crop pixels
      float vis[33] = {};     // 0..1 visibility
   };

   // MediaPipe BlazePose landmark model on ONNX. There is no person detector in the pack: while no
   // person is tracked the model searches the whole frame (a square around the centre), once found the
   // ROI follows the body from the model's own alignment points. Works best with the torso in view.
   class PoseTracker
   {
   public:
      bool Open(OrtRuntime& rt, const std::string& packDir, bool preferGpu, std::string& err);
      void Close();
      bool Process(const unsigned char* rgb, int w, int h, PoseResult& out, std::string& err);
      void Reset() { m_haveRoi = false; }

   private:
      OrtRuntime* m_rt = nullptr;
      OrtModel* m_lm = nullptr;
      bool m_gpu = false;
      bool m_haveRoi = false;
      float m_roi[4] = {};
   };
}

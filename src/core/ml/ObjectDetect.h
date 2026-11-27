#pragma once

// D-FINE-S COCO-only object detector (Apache-2.0; Peterande/D-FINE, dfine_s_coco.onnx).
// Runs at 640x640 FP32 (~41.5 MB, ~150 ms p50 on M2 CPU, +123 MB RSS, 0.90 recall@0.5 on S1).
// Powers Object Selection hover boxes and "Select All Objects" -> SAM 2.1 auto-prompting.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ml
{
   namespace ObjectDetect
   {
      struct Box
      {
         float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f; // image pixels, top-down
         float score = 0.0f;
         int classId = -1;
         std::string label;
      };

      struct Picture
      {
         int w = 0, h = 0;
         std::vector<uint8_t> rgba; // top-down RGBA
      };

      bool Available();
      std::string Missing();

      // Async latest-only detection for Object Selection hover preview.
      void Request(std::shared_ptr<const Picture> pic, uint64_t key);
      bool Ready(uint64_t key);
      bool Busy();
      bool GetBoxes(uint64_t key, std::vector<Box>& outBoxes);
      std::string LastError();

      // Blocking detection on the caller's thread (tests, Select All Objects, eval hook).
      bool DetectNow(const std::vector<uint8_t>& rgba, int w, int h,
                     std::vector<Box>& outBoxes, std::string& err,
                     float scoreThreshold = 0.35f);

      const char* CocoLabel(int classId);
      void ClearCache();
   } // namespace ObjectDetect
} // namespace Ml

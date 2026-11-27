#pragma once

// Clean-room 17-keypoint Pure-COCO Pose Estimator (Apache-2.0 MobileNetV4-Small + heatmap head).
// Trained strictly on MS COCO val2017 person keypoints (zero AIC / zero MPII).
// Input: 256x192 RGB [0, 1]; Output: 17 COCO keypoints in image pixel coordinates.

#include <cstdint>
#include <string>
#include <vector>

namespace Ml
{
   namespace PoseDetect
   {
      struct Keypoint
      {
         float x = 0.0f;
         float y = 0.0f;
         float score = 0.0f;
         const char* name = "";
      };

      bool Available();
      std::string Missing();

      // Estimates 17 COCO keypoints inside [cropX, cropY, cropW, cropH] (or full image if cropW/cropH <= 0).
      bool DetectNow(const std::vector<uint8_t>& rgba, int w, int h,
                     int cropX, int cropY, int cropW, int cropH,
                     std::vector<Keypoint>& outKeypoints, std::string& err);
   } // namespace PoseDetect
} // namespace Ml

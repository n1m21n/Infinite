#pragma once

// Clean-room 512x512 Inpainting (MIT, zero Places2 / zero ADE20K / zero FFT ops).
// Trained on PD12M (CC0) + Infinite headless renders with LPIPS (BSD-2) perceptual loss.
// 4.07 MB ONNX, +16.0 MB RSS, 287 ms p50 @ 512x512 on M2 CPU.

#include <cstdint>
#include <string>
#include <vector>

namespace Ml
{
   namespace CleanInpaint
   {
      bool Available();
      std::string Missing();

      // Fills masked hole (mask > 127) in a top-down RGBA picture (alpha preserved).
      bool RunNow(const std::vector<uint8_t>& rgba, const std::vector<uint8_t>& mask,
                  int w, int h, std::vector<uint8_t>& outRgba, std::string& err);
   } // namespace CleanInpaint
} // namespace Ml

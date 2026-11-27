#pragma once

// Clean-room Photo-to-Lineart (MIT, trained on Infinite G-buffer normal+depth edges + PD12M CC0).
// 4.75 MB ONNX, +17.5 MB RSS, ~243 ms p50 @ 512x512 on M2 CPU.
// Includes a Sobel luminance edge offline fallback when the "art" pack is absent.

#include <cstdint>
#include <string>
#include <vector>

namespace Ml
{
   namespace LineArt
   {
      bool Available();
      std::string Missing();

      // Converts top-down RGBA into a black-ink-on-white-paper top-down RGBA layer (w x h).
      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<uint8_t>& outRgba, std::string& err);

      // Non-ML offline fallback (M5): Sobel edge sketch on white paper.
      void Fallback(const std::vector<uint8_t>& rgba, int w, int h,
                    std::vector<uint8_t>& outRgba);
   } // namespace LineArt
} // namespace Ml

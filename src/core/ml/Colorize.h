#pragma once

// Clean-room Auto-Colorize (Apache-2.0, MobileNetV4-Small BT.601 YCbCr trained on PD12M CC0).
// 6.43 MB ONNX, +12.2 MB RSS, 14.4 ms p50 @ 256x256 on M2 CPU (22.08 dB PSNR).
// Includes a warm sepia-tone offline fallback when the "art" pack is absent.

#include <cstdint>
#include <string>
#include <vector>

namespace Ml
{
   namespace Colorize
   {
      bool Available();
      std::string Missing();

      // Colorizes a top-down RGBA picture (preserving full-res luma & alpha) into outRgba.
      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<uint8_t>& outRgba, std::string& err);

      // Non-ML offline fallback (M5): warm sepia tint preserving luma & alpha.
      void Fallback(const std::vector<uint8_t>& rgba, int w, int h,
                    std::vector<uint8_t>& outRgba);
   } // namespace Colorize
} // namespace Ml

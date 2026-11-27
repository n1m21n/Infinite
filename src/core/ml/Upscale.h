#pragma once

// Clean-room SPAN 2x Super-Resolution & DeJPEG (Apache-2.0, trained on PD12M CC0 + Infinite renders,
// zero DIV2K taint). 1.62 MB ONNX, +10.1 MB RSS, ~214 ms per 256->512 tile on M2 CPU.
// Includes a clean bilinear/bicubic 2x offline fallback when the "upscale" pack is absent.

#include <cstdint>
#include <string>
#include <vector>

namespace Ml
{
   namespace Upscale
   {
      bool Available();
      std::string Missing();

      // Upscales a top-down RGBA picture 2x using tiled 256->512 Clean SPAN 2x.
      // Preserves alpha via bilinear 2x upsampling.
      bool Run2xNow(const std::vector<uint8_t>& rgba, int w, int h,
                    std::vector<uint8_t>& outRgba, int& outW, int& outH,
                    std::string& err);

      // Offline non-ML fallback (M5): smooth 2x bilinear upscale when the "upscale" pack is absent.
      void Fallback2x(const std::vector<uint8_t>& rgba, int w, int h,
                      std::vector<uint8_t>& outRgba, int& outW, int& outH);
   } // namespace Upscale
} // namespace Ml

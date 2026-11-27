#pragma once

// PP-OCRv4 mobile text detection + recognition (Apache-2.0; PaddleOCR / RapidOCR).
// Powers 2D View "Select Text" mask and "Extract Text to Type Layer" (~54 ms p50 on M2 CPU,
// +11.4 MB RSS). Includes a non-ML high-edge horizontal band fallback when the "ocr" pack is absent.

#include <cstdint>
#include <string>
#include <vector>

namespace Ml
{
   namespace TextOcr
   {
      struct TextLine
      {
         int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // image pixels, top-down
         std::string text;
      };

      bool Available();
      std::string Missing();

      // Runs PP-OCRv4 detector + recognizer on top-down RGBA.
      // Writes detected lines (sorted top-to-bottom) and a w x h text mask (0..255).
      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<TextLine>& outLines, std::vector<uint8_t>& outMask,
                  std::string& err);

      // Non-ML offline fallback (M5): selects high-contrast horizontal text-like bands
      // via Sobel edge density so Select Text remains usable without the "ocr" pack.
      void FallbackTextMask(const std::vector<uint8_t>& rgba, int w, int h,
                            std::vector<uint8_t>& outMask);
   } // namespace TextOcr
} // namespace Ml

#pragma once

// MODNet live portrait/subject matte preview (Apache-2.0 code & weights; ZHKKKe/MODNet).
// Runs at 512x512 FP16 ONNX (~13 MB, ~158 ms p50 on M2 CPU, +36.5 MB RSS) through
// Ml::ModelRegistry (1 model resident at a time, 60 s idle unload) and Ml::LatestWorker
// (low-priority latest-only slot, never on UI/GL/audio threads).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ml
{
   namespace ModNet
   {
      struct Picture
      {
         int w = 0, h = 0;
         std::vector<uint8_t> rgba; // top-down RGBA
      };

      bool Available();
      std::string Missing();

      // Async latest-only matte preview request. Returns immediately.
      void Request(std::shared_ptr<const Picture> pic, uint64_t key);
      bool Ready(uint64_t key);
      bool Busy();
      bool GetMatte(uint64_t key, std::vector<uint8_t>& outAlpha, int& w, int& h);
      std::string LastError();

      // Blocking inference on the caller's thread (tests & eval hook only).
      // outAlpha: w x h, top-down, 0..255 soft alpha matte.
      bool RunNow(const std::vector<uint8_t>& rgba, int w, int h,
                  std::vector<uint8_t>& outAlpha, std::string& err);

      void ClearCache();
   } // namespace ModNet
} // namespace Ml

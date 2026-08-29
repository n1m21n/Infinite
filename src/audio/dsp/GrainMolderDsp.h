#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// Grain Molder: time-domain grain sorting and continuous scrambling engine.
// Slices audio into overlapping grains, calculates per-grain metric (Level,
// Brightness, Random), and rearranges them based on a continuous blend between
// original temporal position and metric rank.
namespace GrainMolderDsp
{
   struct Params
   {
      float grainMs = 100.0f; // 10..500 ms
      float amount = 0.0f;    // 0..1 (0 = original order, 1 = fully sorted)
      int key = 0;            // 0 = Level, 1 = Bright, 2 = Random
      bool descending = false;
      uint32_t seed = 1;
   };

   // Worker thread only. Allocates freely. Honours `abort`.
   // If stereo input is provided (rightIn != nullptr and outR != nullptr),
   // mono is used for metric analysis, but the permutation is applied to both channels.
   void Process(const float* mono, int len, double sr, const Params& p,
                std::vector<float>& outL, std::vector<float>* outR = nullptr,
                const float* rightIn = nullptr,
                const std::atomic<bool>* abort = nullptr);
}

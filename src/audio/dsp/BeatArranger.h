#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DrumClassifier.h"

// Pure DSP groove generator for BeatArrangerNode.
// Takes a pool of classified sample slices and arranges them into a multi-bar
// rhythm pattern based on role templates, Euclidean distributions, and
// deterministic seeded variation.
//
// Clean-room implementation based on primary literature:
//   - Euclidean rhythm distribution algorithm:
//     Toussaint, G., "The Euclidean Algorithm Generates Traditional Musical Rhythms",
//     Proc. BRIDGES: Mathematical Connections in Art, Music, and Science, 2005.
//
// Pure math: no INode, no ImGui, no GL, no threads of its own.

namespace BeatArranger
{
   struct SliceInfo
   {
      int sample = 0;              // 0..7 strip index
      int slice = 0;               // slice index within sample
      DrumClassifier::DrumClass cls = DrumClassifier::DrumClass::Perc;
      float confidence = 1.0f;
      float lenSec = 0.0f;
   };

   struct ArrangedHit
   {
      int step = 0;                // 0..(16 * bars - 1)
      int sample = 0;              // 0..7 strip index
      int slice = 0;               // slice index
      float velocity = 1.0f;       // 0..1
      float pitchOffsetSemis = 0.0f; // semitone offset fixed per hit
   };

   struct ArrangeParams
   {
      int bars = 2;                // 1, 2, or 4 (default 2)
      float randPitch = 0.0f;      // 0..1
      float swing = 0.0f;          // 0..1
   };

   // Deterministic arrangement: given the same pool, params, and seed, returns
   // byte-identical hit lists. seed+1 rolls a new variation.
   std::vector<ArrangedHit> Arrange(const std::vector<SliceInfo>& pool,
                                    const ArrangeParams& p,
                                    uint32_t seed);

   // Serialization helpers for saving/loading arranged hit blobs in patches
   std::string SerializeHits(const std::vector<ArrangedHit>& hits);
   std::vector<ArrangedHit> DeserializeHits(const std::string& blob);
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DrumClassifier.h"

// Deterministic seeded groove generator for BeatArranger (single-source
// model, v2). Pure math: no INode, no ImGui, no threads of its own.
//
// Clean-room references:
//   - Euclidean rhythm distribution: Toussaint, G., "The Euclidean Algorithm
//     Generates Traditional Musical Rhythms", Proc. BRIDGES, 2005.
namespace BeatArranger
{
   // One analyzed slice of the single source sample.
   struct SliceInfo
   {
      int slice = 0;
      DrumClassifier::DrumClass cls = DrumClassifier::DrumClass::Perc;
      float confidence = 0.0f;
      float lenSec = 0.0f;
      float centroid = 0.0f;
      float decaySec = 0.0f;
      float f0 = 0.0f;
   };

   // One arranged hit. Pitch randomization is NOT baked in - `pitchRand` is
   // a per-hit seeded ±1 value applied LIVE at play time, scaled by the
   // node's randPitch param, so turning randPitch up/down doesn't require
   // regenerating the groove.
   struct ArrangedHit
   {
      int step = 0;
      int slice = 0;
      float velocity = 1.0f;
      float pitchRand = 0.0f; // -1..1, seeded
   };

   struct ArrangeParams
   {
      int stepsPerBar = 16;
      int bars = 2; // fixed at 2 for v2
      int strongBeatMask = 0b1000100010001000; // which of stepsPerBar*bars steps are "strong"
   };

   // xorshift32, deterministic across platforms.
   struct Rng
   {
      uint32_t state;
      explicit Rng(uint32_t seed) : state(seed ? seed : 1u) {}
      uint32_t Next()
      {
         state ^= state << 13;
         state ^= state >> 17;
         state ^= state << 5;
         return state;
      }
      float Next01() { return (float)(Next() & 0xFFFFFF) / (float)0x1000000; }
      float NextBipolar() { return Next01() * 2.0f - 1.0f; }
   };

   // Generates a groove from a set of analyzed slices, deterministic given
   // (slices, params, seed). Returns hits sorted by (step, slice) - fix A5,
   // no duplicate (step, slice) pairs - fix A2.
   std::vector<ArrangedHit> Generate(const std::vector<SliceInfo>& slices, const ArrangeParams& params, uint32_t seed);

   // Serialization: "step:slice:velocity:pitchRand;..." - %.9g float
   // precision for exact round-trip (fix A6). No sample/strip field.
   std::string SerializeHits(const std::vector<ArrangedHit>& hits);
   std::vector<ArrangedHit> DeserializeHits(const std::string& blob);
}

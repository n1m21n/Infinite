#pragma once
// fork: lazy per-node-type port descriptors for the "drop a cable on empty canvas" suggestions.
// Building the suggestion list used to construct one throwaway node per registered type on every drop
// (~960 ms for 115 hits in a Debug build). A type's ports never change after construction, so the answer
// is computed once per type and kept: which kinds of source cable its inputs accept, and what its outputs carry.
#include <cstdint>
#include <string>
#include <vector>

struct GraphNode;

namespace app
{
   // What an output pin carries: the 9 facts IsInputSlotCompatible takes, packed into a bitmask.
   enum SrcKind : uint16_t
   {
      kSrcModulator = 1u << 0, kSrcPalette = 1u << 1, kSrcGeometry = 1u << 2, kSrcCamera = 1u << 3,
      kSrcLight = 1u << 4, kSrcEnvironment = 1u << 5, kSrcAudio = 1u << 6, kSrcNote = 1u << 7,
      kSrcPredictor = 1u << 8,
   };

   // The same call IsInputSlotCompatible makes, from a packed kind (pointers there are only null-tested).
   bool SlotAcceptsKind(GraphNode* dst, int slot, uint16_t kind);
   // Kind of output `out` of a live node.
   uint16_t SrcKindOfOutput(GraphNode* src, int out);
   // True if some input slot of node type `name` accepts a cable of `kind`. Builds the type's entry on first use.
   bool TypeAcceptsKind(const std::string& name, uint16_t kind);
   // The kinds carried by each output of type `name` (built on first use).
   const std::vector<uint16_t>& TypeOutputKinds(const std::string& name);
   // Build entries for not-yet-cached types until `budgetMs` has passed; call once a frame at startup.
   void WarmPortProbeCache(double budgetMs);
}

// Art History Brush: oriented dabs that sample a history state, posed along the stroke, and stamp.
// Ported from PhotoCraft's crates/algo/src/art_history.rs (Apache License 2.0, Copyright the PhotoCraft authors).
// A dab reads its rotated source straight from the history state, so a dab that crosses a tile edge paints the same
// as an untiled pass. Scatter is seeded, so the same stroke is replayable.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ArtHistory
{
   // Mixed with the history state so two documents with different snapshots scatter differently.
   constexpr uint32_t kSeed = 0xA7B157E0u;
   constexpr size_t kMaxDabs = 4096;                       // most dabs one stroke may emit
   constexpr uint64_t kMaxCoverage = 4096ull * 4096ull;    // largest rectangle, in pixels, a stroke may touch

   enum class Error { None, Empty, Mismatched, TooLarge };

   // Photoshop's Art History styles, in its menu order.
   enum class Style { TightShort, TightMedium, TightLong, LooseMedium, LooseLong, Dab, TightCurl, LooseCurl };
   constexpr int kStyleCount = 8;
   const char* StyleId(Style s);        // tightShort, ... (stable, saved)
   const char* StyleName(Style s);      // Tight Short, ... (shown)
   bool ParseStyle(const std::string& id, Style& out);

   struct Dab
   {
      float x = 0, y = 0, theta = 0, radius = 0, pressure = 1;
      bool operator==(const Dab& o) const { return x == o.x && y == o.y && theta == o.theta && radius == o.radius && pressure == o.pressure; }
   };

   // Spacing along the stroke in pixels; false for a size that is not a positive number.
   bool SpacingPx(float size, float area, Style style, float& out);
   // Reach of a dab around its stroke point (half the dab, its scatter, a margin).
   bool HaloRadius(float size, Style style, int& out);
   // Side of the rotated source square.
   bool SampleSide(float size, Style style, int& out);

   // Walks the stroke `pts` (x, y pairs) and poses its dabs. `seed` is kSeed ^ the history state.
   Error PlaceDabs(const std::vector<float>& pts, float size, float area, Style style, uint32_t seed, std::vector<Dab>& out);

   struct StampParams
   {
      float* dest = nullptr;
      int destW = 0, destH = 0, destX = 0, destY = 0;   // the buffer and where its first pixel sits
      const float* src = nullptr;
      int srcW = 0, srcH = 0, srcX = 0, srcY = 0;
      int ch = 4;
      const Dab* dabs = nullptr;
      size_t dabCount = 0;
      float hardness = 1, opacity = 1, flow = 1;
      float tolerance = 0;          // mean change (0..1) a dab must see before it paints
      int tile = 0;                 // destination tile edge; 0 treats the buffer as one tile
      const float* selection = nullptr;   // destW x destH, 0..1
      bool lockTransparent = false;
      int alpha = -1;               // the alpha channel, or -1 for none
   };

   // Stamps the posed dabs in order (dab n sees dab n-1). `written`: destination pixels written; dabs the tolerance
   // skipped write none.
   Error Stamp(const StampParams& p, uint32_t& written);

   // PhotoCraft's art_history tests, ported: (test name, passed).
   std::vector<std::pair<std::string, bool>> SelfTests();
}

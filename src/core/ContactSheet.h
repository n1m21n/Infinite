#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// =========================================================================
// ContactSheet - several frames on one image, each with its timestamp burnt
// in, for `Infinite --frame ... --contact-sheet <sheet.png>`
// (docs/fix-briefs/headless-engine.md 2.2). Pixels in, pixels out: the PNG
// encode stays with the caller, so this has no GL or image-library dependency.
// =========================================================================

namespace ContactSheet
{
   // Every cell is this wide whatever the frame size, so a reader (or a
   // vision model) always gets enough pixels per frame to judge it by.
   constexpr int kCellWidth = 480;

   struct Image
   {
      int width = 0;
      int height = 0;
      std::vector<uint8_t> rgba; // top row first, opaque
   };

   class Builder
   {
   public:
      // RGBA8 pixels of one frame. `bottomUp` for rows as glReadPixels hands
      // them over. Alpha is dropped: the sheet shows the colour channels.
      void Add(const uint8_t* rgba, int width, int height, bool bottomUp, const std::string& label);
      int Count() const { return (int)mCells.size(); }
      // Cells left to right, top to bottom, in the order they were added.
      Image Build() const;

   private:
      struct Cell
      {
         int height = 0;
         std::vector<uint8_t> rgb; // kCellWidth x height
         std::string label;
      };
      std::vector<Cell> mCells;
   };

   // A PNG with an sRGB chunk added after its header, so a viewer does not
   // have to guess the colour space. Returns the input unchanged if it is
   // not a PNG or already carries one.
   std::vector<uint8_t> TagSrgb(const std::vector<uint8_t>& png);
}

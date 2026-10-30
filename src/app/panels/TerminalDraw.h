// Cell-exact shapes for the Terminal panel: box drawing, blocks and braille join
// across the taller line height, which font glyphs would not (D16).
#pragma once
#include "imgui.h"
#include <cstdint>

namespace TerminalDraw
{
   bool IsShape(uint32_t cp);
   void Shape(ImDrawList* dl, uint32_t cp, float x, float y, float w, float h, ImU32 color);
}

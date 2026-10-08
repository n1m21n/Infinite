// Draws one Infinite Glyph (Glyphs.gen.h) centred on a point, at an exact pixel size.
#pragma once
#include "app/ui/design/Glyphs.gen.h"
#include "imgui.h"

namespace glyph
{
   // `size` is the 20-unit canvas edge in pixels; the font's 1000-unit em equals the canvas, so the text size is the same number.
   inline void Draw(ImDrawList* dl, ImVec2 center, float size, ImU32 col, const char* g)
   {
      ImFont* font = ImGui::GetFont();
      const ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0.0f, g);
      dl->AddText(font, size, ImVec2(center.x - ext.x * 0.5f, center.y - size * 0.5f - (ext.y - size) * 0.5f), col, g);
   }
}

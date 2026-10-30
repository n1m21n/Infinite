#pragma once
// The pointer for a tool, shared by the Timeline, Sample Studio and Piano Roll: the tool's glyph replaces the system
// arrow, so the pointer and the tool read as one thing. Call while the pointer is over the tool's canvas.
#include "app/ui/design/GlyphDraw.h"
#include "imgui.h"

namespace ToolCursor
{
   inline void Badge(const char* glyph)
   {
      if (glyph == nullptr)
         return;
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      const ImVec2 m = ImGui::GetIO().MousePos;
      glyph::Draw(dl, ImVec2(m.x + 1.0f, m.y + 1.0f), 20.0f, IM_COL32(0, 0, 0, 200), glyph);
      glyph::Draw(dl, m, 20.0f, IM_COL32(255, 255, 255, 255), glyph);
   }
}

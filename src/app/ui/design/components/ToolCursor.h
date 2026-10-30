#pragma once
// The pointer for a tool, shared by the Timeline, Sample Studio and Piano Roll: the system arrow with the tool's glyph
// beside it, so the active tool reads at the pointer. Call while the pointer is over the tool's canvas.
#include "app/ui/design/GlyphDraw.h"
#include "imgui.h"

namespace ToolCursor
{
   inline void Badge(const char* glyph)
   {
      if (glyph == nullptr)
         return;
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      const ImVec2 m = ImGui::GetIO().MousePos;
      const ImVec2 c(m.x + 17.0f, m.y + 19.0f);
      dl->AddCircleFilled(c, 11.0f, IM_COL32(12, 14, 24, 215), 24);
      glyph::Draw(dl, c, 16.0f, IM_COL32(255, 255, 255, 255), glyph);
   }
}

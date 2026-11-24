#pragma once
// The pointer for a tool, shared by the Timeline, Sample Studio and Piano Roll: the tool's glyph replaces the system
// arrow, so the pointer and the tool read as one thing. Call while the pointer is over the tool's canvas.
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/TokenColors.h"
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
      glyph::Draw(dl, ImVec2(m.x + 1.0f, m.y + 1.0f), tok::icon_lg, IM_COL32(0, 0, 0, tok::alpha8_cursor_shadow), glyph);
      glyph::Draw(dl, m, tok::icon_lg, tok::U32(tok::tool_cursor_badge_col), glyph);
   }
}

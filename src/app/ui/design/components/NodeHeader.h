#pragma once
#include "imgui.h"
#include "app/ui/design/Tokens.gen.h"

// Node header: title row carries the category to the right of the title, dimmed.
namespace NodeHeader
{
   inline void Category(const char* text, const ImVec4& col)
   {
      ImGui::SameLine(0.0f, tok::space_2);
      ImGui::PushStyleColor(ImGuiCol_Text, col);
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
   }
}

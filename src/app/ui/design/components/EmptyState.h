// EmptyState: what a list or panel says when it has nothing to list. One dimmed Body line, an optional Caption
// hint under it, centred in the rectangle it is given. Drawn, not laid out, so it can sit over table filler rows.
#pragma once
#include "imgui.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/Tokens.gen.h"

namespace EmptyState
{
   inline void Draw(const ImVec2& min, const ImVec2& max, const char* message, const char* hint = nullptr)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const float cx = (min.x + max.x) * 0.5f;
      float y = (min.y + max.y) * 0.5f - (hint != nullptr ? tok::space_3 : tok::space_2);
      {
         UiType::Scope body(UiType::Size::Body, UiType::Weight::Medium);
         const ImVec2 sz = ImGui::CalcTextSize(message);
         dl->AddText(ImVec2(cx - sz.x * 0.5f, y), dim, message);
         y += sz.y + tok::space_1;
      }
      if (hint != nullptr)
      {
         UiType::Scope cap(UiType::Size::Caption);
         const ImVec2 sz = ImGui::CalcTextSize(hint);
         dl->AddText(ImVec2(cx - sz.x * 0.5f, y), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f), hint);
      }
   }

   // Convenience: centred in the current window's content area.
   inline void DrawInWindow(const char* message, const char* hint = nullptr)
   {
      const ImVec2 p = ImGui::GetWindowPos();
      Draw(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetWindowWidth(), p.y + ImGui::GetWindowHeight()), message, hint);
   }
}

// DropdownField: the closed face of a node dropdown (mode / scale / rate selectors in node bodies): the field well
// (same recess as Settings combos) with the current value; deliberately no chevron (owner decision). Drawing and hit target only; the
// caller opens the option list. Text colour is whatever ImGuiCol_Text is (the modulated amber is pushed by the caller).
#pragma once
#include <algorithm>
#include "app/ui/design/components/FieldWell.h"

namespace DropdownField
{
   // `caption` uses ImGui's "text##id" form. Returns true when clicked. `size.x` 0 = fit the text.
   inline bool Draw(const char* caption, ImVec2 size)
   {
      const float h = ImGui::GetFrameHeight();
      const float pad = tok::space_2;
      
      const char* end = ImGui::FindRenderedTextEnd(caption);
      const ImVec2 ts = ImGui::CalcTextSize(caption, end);
      const float w = size.x > 0.0f ? size.x : ts.x + 2.0f * pad;
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 mx(mn.x + w, mn.y + h);
      const bool clicked = ImGui::InvisibleButton(caption, ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, hovered, ImGui::IsItemActive());
      const float textRight = mx.x - pad;
      dl->PushClipRect(mn, ImVec2(textRight, mx.y), true);
      dl->AddText(ImVec2(mn.x + pad, mn.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), caption, end);
      dl->PopClipRect();
      return clicked;
   }
}

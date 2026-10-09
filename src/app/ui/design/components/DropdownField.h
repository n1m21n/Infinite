// DropdownField: the closed face of a node dropdown (mode / scale / rate selectors in node bodies): the field well
// (same recess as Settings combos) with the current value; deliberately no chevron (owner decision). Drawing and hit target only; the
// caller opens the option list. Text colour is whatever ImGuiCol_Text is (the modulated amber is pushed by the caller).
#pragma once
#include <algorithm>
#include "app/ui/design/components/FieldWell.h"

namespace DropdownField
{
   // `caption` uses ImGui's "text##id" form. Returns true when clicked. `size.x` 0 = fit the text.
   // `label` (optional, plain text): the dim parameter name drawn inside the face on the left, value on the right -
   // the same grammar as the slider faces; it yields space to the value, never the other way round.
   inline bool Draw(const char* caption, ImVec2 size, const char* label = nullptr)
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
      const bool hasLabel = label != nullptr && label[0] != '\0';
      const float labelW = hasLabel ? ImGui::CalcTextSize(label).x : 0.0f;
      const float avail = textRight - (mn.x + pad);
      // Value keeps up to everything but a 24px label sliver; right-aligned when a label shares the face.
      float valueX = mn.x + pad;
      if (hasLabel)
      {
         const float valueW = std::min(ts.x, std::max(0.0f, avail - std::min(labelW + pad, 24.0f)));
         valueX = textRight - valueW;
         ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         dim.w *= 0.55f;
         dl->PushClipRect(mn, ImVec2(std::max(mn.x + pad, valueX - pad), mx.y), true);
         dl->AddText(ImVec2(mn.x + pad, mn.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(dim), label);
         dl->PopClipRect();
      }
      dl->PushClipRect(ImVec2(hasLabel ? valueX : mn.x, mn.y), ImVec2(textRight, mx.y), true);
      dl->AddText(ImVec2(valueX, mn.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), caption, end);
      dl->PopClipRect();
      return clicked;
   }
}

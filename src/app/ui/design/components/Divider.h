// Divider: a hairline between sections (plan section 0). Neutral: 15% of the text colour, 1 pt.
#pragma once
#include <cmath>
#include "app/ui/design/UiLayout.h"
#include "imgui.h"

namespace Divider
{
   // Vertical line centred in `r` (use inside a Row cell), full height minus `inset` top and bottom.
   inline void Vertical(const UiLayout::Rect& r, float inset = 0.0f)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const float x = std::round(r.CenterX());
      ImGui::GetWindowDrawList()->AddLine(ImVec2(x, r.y + inset), ImVec2(x, r.Bottom() - inset),
                                          ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.15f)), 1.0f);
   }
   inline void Horizontal(const UiLayout::Rect& r, float inset = 0.0f)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const float y = std::round(r.CenterY());
      ImGui::GetWindowDrawList()->AddLine(ImVec2(r.x + inset, y), ImVec2(r.Right() - inset, y),
                                          ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.15f)), 1.0f);
   }
}

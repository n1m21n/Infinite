// ChipButton: the small toggle/action chip used in toolbars and table cells. Same construction as the Library
// chips and mode tabs: a 6% well (accent when on), eased hover, centred label. Label may carry a "###key" suffix.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"

namespace ChipButton
{
   constexpr float kHeight = 22.0f;

   // Returns true when clicked. `h` is the chip height, `minW` an optional width floor (0 = hug the label).
   inline bool Draw(const char* label, bool on, float h = kHeight, float minW = 0.0f, bool soft = false)
   {
      const char* key = std::strstr(label, "##");
      const std::string shown = key != nullptr ? std::string(label, key) : std::string(label);
      const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
      const ImVec2 sz(std::max(minW, ts.x + 2.0f * tok::space_2), h);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(label, sz);
      const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 a = app::AccentEmphasisSelected();
      // soft: a quiet selected state (text-tinted well, full-strength label) for secondary groups next to an accent chip
      const bool acc = on && !soft;
      const ImVec4 bg = acc ? a : ImVec4(t.x, t.y, t.z, (on ? 0.16f : 0.06f) + 0.05f * hv);
      dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(bg), tok::radius_tile);
      dl->AddText(ImVec2(std::round(p.x + (sz.x - ts.x) * 0.5f), std::round(p.y + (sz.y - ts.y) * 0.5f)),
                  ImGui::GetColorU32(acc ? ImVec4(1, 1, 1, 1) : ImVec4(t.x, t.y, t.z, on ? 1.0f : 0.8f)), shown.c_str());
      return clicked;
   }
}

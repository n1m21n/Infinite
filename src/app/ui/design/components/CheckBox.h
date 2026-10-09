// CheckBox: the app's own checkbox (C4). A 16 pt rounded box in the field-well tint; on = accent fill with a tick that
// draws itself in. Drawing and hit target only: the label and row layout belong to the caller (FormParts, MenuParts).
#pragma once
#include <algorithm>
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"

namespace CheckBox
{
   constexpr float kSize = 16.0f;

   // Box at `p`; `onv` and `hv` are the eased on / hover values in 0..1.
   inline void Draw(ImDrawList* dl, ImVec2 p, float onv, float hv)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec2 q(p.x + kSize, p.y + kSize);
      dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.08f + 0.06f * hv)), tok::radius_field);
      dl->AddRect(p, q, ImGui::GetColorU32(CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.10f) : ImVec4(1, 1, 1, 0.08f)),
                  tok::radius_field);
      if (onv <= 0.001f)
         return;
      ImVec4 a = app::AccentEmphasisSelected();
      a.w *= onv;
      dl->AddRectFilled(p, q, ImGui::GetColorU32(a), tok::radius_field);
      // Tick: two strokes, drawn along their length by `onv`.
      const ImVec2 a0(p.x + kSize * 0.24f, p.y + kSize * 0.52f);
      const ImVec2 a1(p.x + kSize * 0.43f, p.y + kSize * 0.71f);
      const ImVec2 a2(p.x + kSize * 0.77f, p.y + kSize * 0.31f);
      const float l1 = 0.27f, l2 = 0.53f;                   // stroke lengths, in box units
      const float d = onv * (l1 + l2);
      const ImU32 col = ImGui::GetColorU32(ImVec4(1, 1, 1, std::min(1.0f, onv * 1.4f)));
      const float k1 = std::min(1.0f, d / l1);
      dl->PathLineTo(a0);
      dl->PathLineTo(ImVec2(a0.x + (a1.x - a0.x) * k1, a0.y + (a1.y - a0.y) * k1));
      if (d > l1)
      {
         const float k2 = std::min(1.0f, (d - l1) / l2);
         dl->PathLineTo(ImVec2(a1.x + (a2.x - a1.x) * k2, a1.y + (a2.y - a1.y) * k2));
      }
      dl->PathStroke(col, 0, 1.8f);
   }

   // A box on its own line position; returns true when toggled. `id` must be unique in the window.
   inline bool Box(const char* id, bool* v)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(kSize, kSize));
      if (clicked)
         *v = !*v;
      const ImGuiID iid = ImGui::GetItemID();
      const float hv = UiAnim::Hover(iid, ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const float onv = UiAnim::Hover(iid ^ 0x5bd1e995u, *v, tok::motion_on, tok::motion_off);
      Draw(ImGui::GetWindowDrawList(), p, onv, hv);
      if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
         ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + kSize + 2, p.y + kSize + 2),
                                             ImGui::GetColorU32(ImGuiCol_NavHighlight), tok::radius_field + 2.0f, 0, 1.5f);
      return clicked;
   }
}

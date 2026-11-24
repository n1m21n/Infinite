// CheckBox: the app's own checkbox (C4). A 16 pt rounded box in the field-well tint; on = accent fill with a tick that
// draws itself in. Drawing and hit target only: the label and row layout belong to the caller (FormParts, MenuParts).
#pragma once
#include "app/selftest/UiAudit.h"
#include "app/ui/design/TokenColors.h"
#include <algorithm>
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiDensity.h"

namespace CheckBox
{
   // The box edge at the current density (16 regular, smaller in a compact inspector).
   inline float Size() { return UiDensity::M().check; }

   // Box at `p`; `onv` and `hv` are the eased on / hover values in 0..1.
   // `tint` (optional) replaces the accent fill, e.g. the amber of a modulated box.
   inline void Draw(ImDrawList* dl, ImVec2 p, float onv, float hv, const ImVec4* tint = nullptr)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec2 q(p.x + Size(), p.y + Size());
      UiAudit::NoteFill(ImVec4(t.x, t.y, t.z, tok::alpha_fill_rest));
      dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, tok::alpha_fill_rest + tok::alpha_fill_rest * hv)), tok::radius_control);
      dl->AddRect(p, q, ImGui::GetColorU32(tok::InkEdge(CategoryColors::IsThemeLight(), tok::alpha_mark_edge_light, tok::alpha_mark_edge_dark)),
                  tok::radius_control);
      if (onv <= 0.001f)
         return;
      ImVec4 a = tint ? *tint : app::AccentEmphasisSelected();
      a.w *= onv;
      dl->AddRectFilled(p, q, ImGui::GetColorU32(a), tok::radius_control);
      // Tick: two strokes, drawn along their length by `onv`.
      const ImVec2 a0(p.x + Size() * tok::check_box_a0_scale_sm, p.y + Size() * tok::check_box_a0_scale_lg);
      const ImVec2 a1(p.x + Size() * tok::check_box_a1_scale_sm, p.y + Size() * tok::check_box_a1_scale_lg);
      const ImVec2 a2(p.x + Size() * tok::check_box_a2_scale_lg, p.y + Size() * tok::check_box_a2_scale_sm);
      const float l1 = tok::check_box_l1, l2 = tok::check_box_l2;                   // stroke lengths, in box units
      const float d = onv * (l1 + l2);
      const ImU32 col = ImGui::GetColorU32(ImVec4(1, 1, 1, std::min(1.0f, onv * tok::check_box_max)));
      const float k1 = std::min(1.0f, d / l1);
      dl->PathLineTo(a0);
      dl->PathLineTo(ImVec2(a0.x + (a1.x - a0.x) * k1, a0.y + (a1.y - a0.y) * k1));
      if (d > l1)
      {
         const float k2 = std::min(1.0f, (d - l1) / l2);
         dl->PathLineTo(ImVec2(a1.x + (a2.x - a1.x) * k2, a1.y + (a2.y - a1.y) * k2));
      }
      dl->PathStroke(col, 0, tok::stroke_mark);
   }

   // A box on its own line position; returns true when toggled. `id` must be unique in the window.
   inline bool Box(const char* id, bool* v)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      // G16: hit area 24x24 around the 16 box; layout keeps the 16 box (Dummy below).
      const float kHit = std::max(UiDensity::M().row, Size() + tok::space_1);
      ImGui::SetCursorScreenPos(ImVec2(p.x - (kHit - Size()) * 0.5f, p.y - (kHit - Size()) * 0.5f));
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(kHit, kHit));
      const bool hovered = ImGui::IsItemHovered();
      const bool focused = ImGui::IsItemFocused();
      const ImGuiID iid = ImGui::GetItemID();
      UiAudit::NoteKind("checkbox", id);
      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(ImVec2(Size(), Size()));
      if (clicked)
         *v = !*v;
      const float hv = UiAnim::Hover(iid, hovered, tok::motion_hover_in, tok::motion_hover_out);
      const float onv = UiAnim::Hover(iid ^ 0x5bd1e995u, *v, tok::motion_on, tok::motion_off);  // ui-audit-ok: hash salt for the hover anim id
      Draw(ImGui::GetWindowDrawList(), p, onv, hv);
      if (focused && ImGui::GetIO().NavVisible)
         ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x - tok::space_0_5, p.y - tok::space_0_5), ImVec2(p.x + Size() + tok::space_0_5, p.y + Size() + tok::space_0_5),
                                             ImGui::GetColorU32(ImGuiCol_NavHighlight), tok::radius_control + tok::space_0_5, 0, tok::check_box_stroke);
      return clicked;
   }
}

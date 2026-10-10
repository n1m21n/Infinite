// TextButton: a labelled button in a rect. Quiet by default (neutral hover/press overlay); Primary fills
// with the accent. Label is Body/Medium. Hover/press come from UiInteract + UiAnim, so it eases like IconTile.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/UiType.h"

namespace TextButton
{
   enum class Kind { Quiet, Primary };

   // `key` is the stable id ("topbar.search"); `label` is what people read. Returns true on click.
   inline bool Draw(const char* key, const UiLayout::Rect& r, const char* label, Kind kind = Kind::Quiet, bool enabled = true)
   {
      const UiInteract::State s = UiInteract::Item(key, r, UiInteract::Role::Button, label, enabled);
      const float hv = UiAnim::Hover(s.id, s.hovered, tok::motion_hover_in, tok::motion_hover_out);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec2 p0(r.x, r.y), p1(r.Right(), r.Bottom());
      ImVec4 label4 = text;
      if (kind == Kind::Primary)
      {
         ImVec4 a = s.down ? app::AccentEmphasisPressed() : app::AccentEmphasisSelected();
         const float lift = 0.04f * hv;
         a = ImVec4(a.x + (1.0f - a.x) * lift, a.y + (1.0f - a.y) * lift, a.z + (1.0f - a.z) * lift, 1.0f);
         dl->AddRectFilled(p0, p1, ImGui::GetColorU32(a), tok::radius_tile);
      }
      else
      {
         const float o = s.down ? 0.12f : 0.06f * hv;
         if (o > 0.001f)
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, o)), tok::radius_tile);
      }
      if (!enabled)
         label4.w *= 0.35f;
      if (s.focused)
         dl->AddRect(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2),
                     ImGui::GetColorU32(ImGuiCol_NavHighlight), tok::radius_tile + 2.0f, 0, 2.0f);
      UiType::Scope ts(UiType::Size::Body, UiType::Weight::Medium);
      const ImVec2 sz = ImGui::CalcTextSize(label);
      dl->AddText(ImVec2(std::round(r.CenterX() - sz.x * 0.5f), std::round(r.CenterY() - sz.y * 0.5f)),
                  ImGui::GetColorU32(label4), label);
      return s.clicked;
   }

   // Width that fits `label` with token padding, for a Fixed() cell.
   inline float WidthFor(const char* label)
   {
      UiType::Scope ts(UiType::Size::Body, UiType::Weight::Medium);
      return std::ceil(ImGui::CalcTextSize(label).x) + tok::space_3 * 2.0f;
   }
}

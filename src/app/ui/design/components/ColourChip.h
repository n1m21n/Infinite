// ColourChip: the colour field of a colour param (C10). A rounded swatch of the colour with a hairline edge in the
// text tone; hover brightens the edge. Click returns true; the picker/binding logic stays with the caller.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/components/FieldWell.h"

namespace ColourChip
{
   inline bool Draw(const char* id, const float* rgb, ImVec2 size = ImVec2(38.0f, 0.0f))
   {
      const float h = size.y > 0.0f ? size.y : ImGui::GetFrameHeight();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(size.x, h));
      const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 q(p.x + size.x, p.y + h);
      dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)), tok::radius_tile);
      dl->AddRect(p, q, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.18f + 0.45f * hv)), tok::radius_tile, 0, 1.0f);
      return clicked;
   }

   // A colour param row: the field well with the caption inside on the left and a fixed-size rounded swatch on the
   // right (same grammar as the slider and dropdown faces). One hit item over the whole face. `labelTint` (optional)
   // recolours the caption, used for a palette-bound colour.
   inline bool DrawRow(const char* id, const char* caption, const float* rgb, float width, const ImVec4* labelTint = nullptr)
   {
      const float h = ImGui::GetFrameHeight();
      const float pad = tok::space_2;
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 mx(mn.x + width, mn.y + h);
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, hovered, ImGui::IsItemActive());
      const float sw = 30.0f, inset = 3.0f;
      const ImVec2 sMin(mx.x - inset - sw, mn.y + inset), sMax(mx.x - inset, mx.y - inset);
      dl->AddRectFilled(sMin, sMax, ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)), tok::radius_tile);
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      dl->AddRect(sMin, sMax, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.18f)), tok::radius_tile, 0, 1.0f);
      ImVec4 dim = labelTint != nullptr ? *labelTint : ImVec4(t.x, t.y, t.z, t.w * 0.55f);
      dl->PushClipRect(mn, ImVec2(sMin.x - pad, mx.y), true);
      dl->AddText(ImVec2(mn.x + pad, mn.y + (h - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(dim), caption);
      dl->PopClipRect();
      return clicked;
   }
}

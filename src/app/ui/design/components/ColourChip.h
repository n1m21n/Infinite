// ColourChip: the colour field of a colour param (C10). A rounded swatch of the colour with a hairline edge in the
// text tone; hover brightens the edge. Click returns true; the picker/binding logic stays with the caller.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"

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
}

// TopBarParts: the two pieces of the top bar that are not a plain control (plan section 0, top bar).
// MenuTile = a File/Edit/Menu title as a 28 pt rounded tile; SectionBreak = 24 pt of air with a hairline.
#pragma once
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiLayout.h"
#include "app/ui/design/components/Divider.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace TopBarParts
{
   // Spacing is pushed only for the BeginMenu call so the popup keeps the normal metrics.
   inline bool MenuTile(const char* label)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, tok::tile - ImGui::GetFontSize()));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));   // the press flash is the tile wash below, never the accent
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_3, 10.0f));
      const bool open = ImGui::BeginMenu(label);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar();
      const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImGuiID aid = ImGui::GetItemID();
      const float hv = UiAnim::Hover(aid, ImGui::IsItemHovered() || open, tok::motion_hover_in, tok::motion_hover_out);
      if (hv > 0.001f)
         ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(ImVec4(tx.x, tx.y, tx.z, (open ? 0.10f : 0.06f) * hv)), tok::radius_tile);
      return open;
   }

   // Puts the next items on the bar's centre line (tile tall, bar_h bar), after 12 pt, a hairline, 12 pt.
   inline void SectionBreak()
   {
      ImGui::SameLine(0.0f, tok::space_5 * 0.5f);
      ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX(), (tok::bar_h - tok::tile) * 0.5f));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(1.0f, tok::tile));
      Divider::Vertical(UiLayout::Rect { p.x, p.y, 1.0f, tok::tile }, 4.0f);
      // Menu-bar lines do not carry a SetCursorPos y into the next SameLine; pin the line here.
      ImGui::GetCurrentWindow()->DC.CursorPosPrevLine.y = p.y;
      ImGui::SameLine(0.0f, tok::space_5 * 0.5f);
   }
}

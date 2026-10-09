// MenuParts: rows for the File / Edit / Menu popups. Every row is 28 pt tall (tok::tile) with a rounded
// wash inset 4 pt from the popup edge and text inset 8 pt from the wash; separators are a hairline in a
// 8 pt gap. ImGui still lays out and activates the rows (shortcut column, arrows, keyboard nav); only the
// fill is ours, so hover reads the same as the top bar tiles.
#pragma once
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiAnim.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace MenuParts
{
   constexpr float kInset = tok::space_1;   // wash distance from the popup edge
   constexpr float kTextInset = tok::space_2; // text distance from the wash edge
   constexpr float kPadY = kInset + 6.0f;     // popup padding: the row wash starts 6 pt above its text

   // Pushed around a BeginMenu / BeginPopup call: the popup's padding is read when it opens.
   inline void PushPopupPad()
   {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kInset + kTextInset, kPadY));
   }

   // Inside a popup body, after BeginMenu returned true: row pitch 28, ImGui's own fills hidden.
   inline void BeginContent()
   {
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, tok::tile - ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
   }

   inline void EndContent()
   {
      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar();
   }

   inline void Wash(bool hot, bool down, bool open)
   {
      const ImGuiID id = ImGui::GetItemID();
      const float hv = UiAnim::Hover(id, hot || open, tok::motion_hover_in, tok::motion_hover_out);
      if (hv <= 0.001f)
         return;
      ImGuiWindow* w = ImGui::GetCurrentWindow();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const float a = (down ? 0.12f : (open ? 0.10f : 0.07f)) * hv;
      w->DrawList->AddRectFilled(ImVec2(w->Pos.x + kInset, ImGui::GetItemRectMin().y + 1.0f),
                                 ImVec2(w->Pos.x + w->Size.x - kInset, ImGui::GetItemRectMax().y - 1.0f),
                                 ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, a)), tok::radius_tile);
   }

   inline bool Item(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true)
   {
      const bool r = ImGui::MenuItem(label, shortcut, selected, enabled);
      Wash(enabled && ImGui::IsItemHovered(), enabled && ImGui::IsItemActive(), false);
      return r;
   }

   // Toggle row: label left, the app checkbox at the right edge; the menu stays open.
   inline bool Check(const char* label, bool* v)
   {
      const bool r = ImGui::Selectable(label, false, ImGuiSelectableFlags_DontClosePopups);
      if (r)
         *v = !*v;
      const ImGuiID id = ImGui::GetItemID();
      Wash(ImGui::IsItemHovered(), ImGui::IsItemActive(), false);
      ImGuiWindow* w = ImGui::GetCurrentWindow();
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      const float hv = UiAnim::Hover(id, ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const float onv = UiAnim::Hover(id ^ 0x5bd1e995u, *v, tok::motion_on, tok::motion_off);
      CheckBox::Draw(w->DrawList, ImVec2(w->Pos.x + w->Size.x - kInset - kTextInset - CheckBox::kSize,
                                         std::floor((mn.y + mx.y - CheckBox::kSize) * 0.5f)), onv, hv);
      return r;
   }

   inline bool SubMenu(const char* label, bool enabled = true)
   {
      PushPopupPad();
      const bool open = ImGui::BeginMenu(label, enabled);
      ImGui::PopStyleVar();
      // Item rect is the parent row; the wash is drawn on top of its text at low alpha, same as the bar tiles.
      Wash(enabled && ImGui::IsItemHovered(), false, open);
      return open;
   }

   inline void Separator()
   {
      ImGuiWindow* w = ImGui::GetCurrentWindow();
      const float y = ImGui::GetCursorScreenPos().y - (tok::tile - ImGui::GetTextLineHeight()) * 0.5f + 4.0f;
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      w->DrawList->AddLine(ImVec2(w->Pos.x + kInset + kTextInset, std::floor(y)),
                           ImVec2(w->Pos.x + w->Size.x - kInset - kTextInset, std::floor(y)),
                           ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.12f)), 1.0f);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
   }
}

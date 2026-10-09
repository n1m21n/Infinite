// MenuParts: rows for the File / Edit / Menu popups. Every row is 24 pt tall (kRow) with a rounded
// wash inset 4 pt from the popup edge and text inset 8 pt from the wash; separators are a hairline in a
// 8 pt gap. ImGui still lays out and activates the rows (shortcut column, arrows, keyboard nav); only the
// fill is ours, so hover reads the same as the top bar tiles.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiAnim.h"
#include "app/AppShared.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace MenuParts
{
   constexpr float kInset = tok::space_1;   // wash distance from the popup edge
   constexpr float kTextInset = tok::space_2; // text distance from the wash edge
   constexpr float kRow = 24.0f;              // row pitch of every menu / dropdown / list popup (was tok::tile, 28)
   constexpr float kPadY = kInset + 4.0f;     // popup padding: the row wash starts 4 pt above its text

   // A popup born from a control inside a zoomed-out node canvas shrinks with it (clamped, so it stays readable):
   // font, padding and row pitch all follow. Set by ZoomScope around the Begin call; 1 everywhere else.
   inline float& Zoom() { static float z = 1.0f; return z; }
   struct ZoomScope
   {
      float prev;
      explicit ZoomScope(float z) : prev(Zoom()) { Zoom() = z; }
      ~ZoomScope() { Zoom() = prev; }
   };

   // Pushed around a BeginMenu / BeginPopup call: the popup's padding is read when it opens.
   inline void PushPopupPad()
   {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((kInset + kTextInset) * Zoom(), kPadY * Zoom()));
   }

   // Inside a popup body, after BeginMenu returned true: row pitch 24, ImGui's own fills hidden.
   inline void BeginContent()
   {
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kRow * Zoom() - ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
   }

   inline void EndContent()
   {
      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar();
   }

   inline void Wash(bool hot, bool down, bool open, ImGuiWindow* w = nullptr)
   {
      const ImGuiID id = ImGui::GetItemID();
      const float hv = UiAnim::Hover(id, hot || open, tok::motion_hover_in, tok::motion_hover_out);
      if (hv <= 0.001f)
         return;
      if (w == nullptr)
         w = ImGui::GetCurrentWindow();
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

   // Toggle row: the app checkbox at the left edge, label after it; the menu stays open.
   inline bool Check(const char* label, bool* v)
   {
      const float boxW = CheckBox::kSize + tok::space_2;
      const float rowW = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(label, nullptr, true).x + boxW + tok::space_4);
      const std::string id = std::string("##chk") + label;
      const bool r = ImGui::Selectable(id.c_str(), false, ImGuiSelectableFlags_DontClosePopups, ImVec2(rowW, 0));
      if (r)
         *v = !*v;
      const ImGuiID gid = ImGui::GetItemID();
      Wash(ImGui::IsItemHovered(), ImGui::IsItemActive(), false);
      ImGuiWindow* w = ImGui::GetCurrentWindow();
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      const float hv = UiAnim::Hover(gid, ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const float onv = UiAnim::Hover(gid ^ 0x5bd1e995u, *v, tok::motion_on, tok::motion_off);
      CheckBox::Draw(w->DrawList, ImVec2(mn.x, std::floor((mn.y + mx.y - CheckBox::kSize) * 0.5f)), onv, hv);
      const char* end = std::strstr(label, "##");
      w->DrawList->AddText(ImVec2(mn.x + boxW, std::floor((mn.y + mx.y - ImGui::GetTextLineHeight()) * 0.5f)),
                           ImGui::GetColorU32(ImGuiCol_Text), label, end);
      return r;
   }

   // Dropdown-list row: same 24 pt row and wash as a menu item; the current choice reads in the accent colour.
   inline bool Choice(const char* label, bool selected)
   {
      if (selected)
         ImGui::PushStyleColor(ImGuiCol_Text, app::AccentEmphasisSelected());
      const bool r = ImGui::Selectable(label, false);
      if (selected)
         ImGui::PopStyleColor();
      Wash(ImGui::IsItemHovered(), ImGui::IsItemActive(), false);
      return r;
   }

   inline bool SubMenu(const char* label, bool enabled = true)
   {
      ImGuiWindow* parent = ImGui::GetCurrentWindow();
      PushPopupPad();
      const bool open = ImGui::BeginMenu(label, enabled);
      ImGui::PopStyleVar();
      // Item rect is the parent row. An open submenu has become the current window, so the wash goes to the parent's draw list.
      Wash(enabled && (open || ImGui::IsItemHovered()), false, open, parent);
      return open;
   }

   inline void Separator()
   {
      ImGuiWindow* w = ImGui::GetCurrentWindow();
      const float y = ImGui::GetCursorScreenPos().y - (kRow - ImGui::GetTextLineHeight()) * 0.5f + 4.0f;
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      w->DrawList->AddLine(ImVec2(w->Pos.x + kInset + kTextInset, std::floor(y)),
                           ImVec2(w->Pos.x + w->Size.x - kInset - kTextInset, std::floor(y)),
                           ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.12f)), 1.0f);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
   }

   // Popups opened from the canvas / panels: same padding, row pitch and wash as the menu bar's popups.
   inline bool BeginPopup(const char* id, ImGuiWindowFlags flags = 0)
   {
      PushPopupPad();
      const bool open = ImGui::BeginPopup(id, flags);
      ImGui::PopStyleVar();
      if (open)
      {
         if (Zoom() != 1.0f)
            ImGui::SetWindowFontScale(Zoom());
         BeginContent();
      }
      return open;
   }

   inline bool BeginContextItem(const char* id, ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight)
   {
      PushPopupPad();
      const bool open = ImGui::BeginPopupContextItem(id, flags);
      ImGui::PopStyleVar();
      if (open)
         BeginContent();
      return open;
   }

   inline bool BeginContextWindow(const char* id, ImGuiPopupFlags flags)
   {
      PushPopupPad();
      const bool open = ImGui::BeginPopupContextWindow(id, flags);
      ImGui::PopStyleVar();
      if (open)
         BeginContent();
      return open;
   }

   inline void EndPopup()
   {
      EndContent();
      ImGui::EndPopup();
   }

   // A combo whose list is a menu: same padding, row pitch and wash. Pair a true result with EndCombo().
   inline bool BeginCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0)
   {
      PushPopupPad();
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tok::space_3, ImGui::GetStyle().FramePadding.y));
      const bool open = ImGui::BeginCombo(id, preview, flags);
      ImGui::PopStyleVar(2);
      if (open)
         BeginContent();
      return open;
   }

   inline void EndCombo()
   {
      EndContent();
      ImGui::EndCombo();
   }
}

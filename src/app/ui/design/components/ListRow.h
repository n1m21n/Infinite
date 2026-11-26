// ListRow: the one list row and table (owner 2026-11-24, ui-control-catalogue.md section 4). Selectable rows,
// tables and list boxes go through here so row height, hover and the selected wash have one home.
// Today each is the call it replaced, argument for argument (EndTable / EndListBox stay ImGui's).
#pragma once
#include <algorithm>
#include <utility>
#include "imgui.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiInteract.h"

namespace ListRow
{
   template <class... A> inline bool Selectable(A&&... a) { return ImGui::Selectable(std::forward<A>(a)...); }
   template <class... A> inline bool BeginTable(A&&... a) { return ImGui::BeginTable(std::forward<A>(a)...); }
   template <class... A> inline bool BeginListBox(A&&... a) { return ImGui::BeginListBox(std::forward<A>(a)...); }

   // A full-width text row as the History panel draws them: hover wash, `current` washed, `dim` greyed. Returns true
   // when clicked; a context menu can follow it (MenuParts::BeginContextItem reads this row).
   inline bool Row(const char* id, const char* text, bool current, bool dim, float rowH)
   {
      const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = UiInteract::Hit(id, ImVec2(w, rowH));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (current)
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_Header, tok::history_color_alpha_mul), tok::radius_control);
      else if (hovered)
         dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_FrameBgHovered), tok::radius_control);
      dl->PushClipRect(p, ImVec2(p.x + w, p.y + rowH), true);
      dl->AddText(ImVec2(p.x + tok::space_2, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
                  ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text), text);
      dl->PopClipRect();
      if (hovered)
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      return clicked;
   }
}

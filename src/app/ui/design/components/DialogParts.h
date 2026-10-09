// DialogParts: the one modal dialog layout. No title bar; a medium-weight title, dimmed message lines, then a
// right-aligned button row where the last button is the primary (accent) action. Same elevated panel as Settings.
#pragma once
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include "app/AppShared.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FormParts.h"

namespace DialogParts
{
   constexpr float kButtonMinW = 72.0f;   // buttons hug their label, never stretch
   constexpr float kButtonH = 24.0f;

   // Call after OpenPopup; pair a true result with End().
   inline bool Begin(const char* name)
   {
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(480.0f, 4000.0f));
      app::PushElevatedPanelStyle(false);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_4, tok::space_4));
      const bool open = ImGui::BeginPopupModal(name, nullptr,
         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
      ImGui::PopStyleVar();
      if (!open)
         app::PopElevatedPanelStyle();
      return open;
   }

   inline void Title(const char* text)
   {
      UiType::Scope s(UiType::Size::Title, UiType::Weight::Medium);
      ImGui::TextUnformatted(text);
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
   }

   inline void Message(const char* text)
   {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
   }

   // Right-aligned row; returns the clicked index or -1. The last label is the primary action.
   inline int Buttons(std::initializer_list<const char*> labels)
   {
      ImGui::Dummy(ImVec2(0.0f, tok::space_2));
      const int n = (int)labels.size();
      // Labels may carry a "##id" suffix (L()); measure only what is shown.
      const auto widthOf = [](const char* l)
      {
         const char* end = std::strstr(l, "##");
         return std::max(kButtonMinW, ImGui::CalcTextSize(l, end).x + 2.0f * tok::space_3);
      };
      float total = tok::space_2 * (n - 1);
      for (const char* l : labels)
         total += widthOf(l);
      const float avail = ImGui::GetContentRegionAvail().x;
      if (avail > total)
         ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total);
      int clicked = -1, i = 0;
      for (const char* l : labels)
      {
         if (i > 0)
            ImGui::SameLine(0.0f, tok::space_2);
         if (ChipButton::Draw(l, i == n - 1, kButtonH, widthOf(l)))
            clicked = i;
         ++i;
      }
      return clicked;
   }

   inline void End()
   {
      // A modal floats over a same-coloured canvas, so its edge is stronger than a docked surface's hairline.
      {
         const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
         const ImVec4 e = CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.16f) : ImVec4(1, 1, 1, 0.08f);
         ImGui::GetWindowDrawList()->AddRect(ImVec2(wp.x + 0.5f, wp.y + 0.5f), ImVec2(wp.x + ws.x - 0.5f, wp.y + ws.y - 0.5f),
                                             ImGui::GetColorU32(e), 12.0f);
      }
      ImGui::EndPopup();
      app::PopElevatedPanelStyle();
   }
}

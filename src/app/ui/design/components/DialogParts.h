// DialogParts: the one modal dialog layout. No title bar; a medium-weight title, dimmed message lines, then a
// right-aligned button row where the last button is the primary (accent) action. Same elevated panel as Settings.
#pragma once
#include <initializer_list>
#include "app/AppShared.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FormParts.h"

namespace DialogParts
{
   constexpr float kButtonW = 104.0f;

   // Call after OpenPopup; pair a true result with End().
   inline bool Begin(const char* name)
   {
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f), ImVec2(560.0f, 4000.0f));
      app::PushElevatedPanelStyle(false);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_5, tok::space_5));
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
      ImGui::Dummy(ImVec2(0.0f, tok::space_4));
      const int n = (int)labels.size();
      const float total = kButtonW * n + tok::space_2 * (n - 1);
      const float avail = ImGui::GetContentRegionAvail().x;
      if (avail > total)
         ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - total);
      int clicked = -1, i = 0;
      for (const char* l : labels)
      {
         if (i > 0)
            ImGui::SameLine(0.0f, tok::space_2);
         if (ChipButton::Draw(l, i == n - 1, FormParts::kRowH, kButtonW))
            clicked = i;
         ++i;
      }
      return clicked;
   }

   inline void End()
   {
      FormParts::WindowEdge();
      ImGui::EndPopup();
      app::PopElevatedPanelStyle();
   }
}

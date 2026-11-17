// ModeBanner: the one floating strip for a mode the app is waiting in (pick a node on the canvas, MIDI learn).
// Same anatomy as the Perf panel's listening strip: popup well, hairline, a dim semibold head ("Pick"), the
// bright target ("Piano Roll"), one Cancel chip. Hugs its text, one control row tall, no accent colour.
#pragma once
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/ChipButton.h"

namespace ModeBanner
{
   // Draws a small window whose top edge is centred on `topCentre`. Returns true when Cancel is clicked.
   // `hovered` (optional) reports whether the pointer is over the strip, so the caller can ignore clicks under it.
   inline bool Float(const char* id, ImVec2 topCentre, const char* head, const char* target, bool* hovered = nullptr)
   {
      const bool light = CategoryColors::IsThemeLight();
      const ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::SetNextWindowPos(topCentre, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_3, tok::space_1));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tok::radius_container);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(tok::space_2, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
      ImGui::PushStyleColor(ImGuiCol_Border, light ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f));
      bool cancel = false;
      const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
      if (ImGui::Begin(id, nullptr, flags))
      {
         const float h = ChipButton::Height();
         auto text = [&](const char* s, float alpha, UiType::Weight w) {
            UiType::Scope ts(UiType::Size::Body, w);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 sz = ImGui::CalcTextSize(s);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, std::round(p.y + (h - sz.y) * 0.5f)),
                                                ImGui::GetColorU32(ImVec4(ink.x, ink.y, ink.z, alpha)), s);
            ImGui::Dummy(ImVec2(sz.x, h));
            ImGui::SameLine();
         };
         text(head, 0.6f, UiType::Weight::Semibold);
         if (target != nullptr && target[0] != '\0')
            text(target, 0.9f, UiType::Weight::Regular);
         ImGui::Dummy(ImVec2(tok::space_1, h));
         ImGui::SameLine();
         cancel = ChipButton::Draw(I18n::L("Cancel##modebanner"), false, h);
         if (hovered != nullptr)
            *hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      }
      ImGui::End();
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(4);
      return cancel;
   }
}

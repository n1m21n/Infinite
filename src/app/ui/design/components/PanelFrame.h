// PanelFrame: the floating card every docked side panel sits in. The child itself is transparent; the rounded
// surface and its soft shadow are drawn on the parent list underneath, inset by kGap so the card never touches the
// window edge, the top bar or the canvas. Pair BeginCard with EndCard. Content gets kGap + space_2 padding.
#pragma once
#include "app/AppShared.h"

namespace PanelFrame
{
   constexpr float kGap = tok::space_2;

   // Draw the card surface for the rect starting at the cursor with the given outer size.
   inline void DrawSurface(ImVec2 size)
   {
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const ImVec2 c0(p0.x + kGap, p0.y + kGap);
      const ImVec2 c1(p0.x + size.x - kGap, p0.y + size.y - kGap);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (int i = 6; i >= 1; --i)   // soft shadow: stacked, widening, fainter rings
         dl->AddRectFilled(ImVec2(c0.x - i, c0.y - i + 2.0f), ImVec2(c1.x + i, c1.y + i + 2.0f),
                           ImGui::GetColorU32(ImVec4(0, 0, 0, 7 / 255.0f)), tok::radius_group + i);
      dl->AddRectFilled(c0, c1, ImGui::GetColorU32(ImGuiCol_ChildBg), tok::radius_group);
   }

   // Call after PushDockedPanelStyle(true) so ChildBg carries the panel fill the surface samples.
   inline void BeginCard(const char* id, ImVec2 size)
   {
      DrawSurface(size);
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kGap + tok::space_2, kGap + tok::space_2));
      ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
   }
   inline void EndCard() { ImGui::EndChild(); }
}

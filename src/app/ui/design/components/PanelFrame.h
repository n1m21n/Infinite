// PanelFrame: the floating card every docked side panel sits in. The child itself is transparent; the rounded
// surface and its soft shadow are drawn on the parent list underneath, inset by kGap so the card never touches the
// window edge, the top bar or the canvas. Pair BeginCard with EndCard. Content gets kGap + space_2 padding.
#pragma once
#include "app/AppShared.h"

namespace PanelFrame
{
   constexpr float kGap = tok::space_2;

   // Gap kept free on each side of the card. A docked panel's resize grip already supplies the gap on its own
   // side, so that side is 0 there and the four gaps still read equal.
   struct Insets
   {
      float l = kGap, t = kGap, r = kGap, b = kGap;
   };

   // Style for a small floating window over the canvas (find bar, quick pickers): opaque panel fill, hairline border,
   // group radius. Pair with PopFloatingStyle() after End().
   inline void PushFloatingStyle()
   {
      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tok::radius_group);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.panelBg.r, t.panelBg.g, t.panelBg.b, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(t.border.r, t.border.g, t.border.b, 0.9f));
   }
   inline void PopFloatingStyle()
   {
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(3);
   }

   // Draw the card surface for the rect starting at the cursor with the given outer size.
   inline void DrawSurface(ImVec2 size, Insets in = {})
   {
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const ImVec2 c0(p0.x + in.l, p0.y + in.t);
      const ImVec2 c1(p0.x + size.x - in.r, p0.y + size.y - in.b);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (int i = 6; i >= 1; --i)   // soft shadow: stacked, widening, fainter rings
         dl->AddRectFilled(ImVec2(c0.x - i, c0.y - i + 2.0f), ImVec2(c1.x + i, c1.y + i + 2.0f),
                           ImGui::GetColorU32(ImVec4(0, 0, 0, 7 / 255.0f)), tok::radius_group + i);
      dl->AddRectFilled(c0, c1, ImGui::GetColorU32(ImGuiCol_ChildBg), tok::radius_group);
   }

   // Call after PushDockedPanelStyle(true) so ChildBg carries the panel fill the surface samples. `size` is the
   // outer size (both axes > 0); the child is the card rect itself, so content sits space_2 inside the card edge
   // on every side, whichever insets are 0.
   inline void BeginCard(const char* id, ImVec2 size, Insets in = {})
   {
      DrawSurface(size, in);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(p0.x + in.l, p0.y + in.t));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
      ImGui::BeginChild(id, ImVec2(size.x - in.l - in.r, size.y - in.t - in.b), ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
   }
   inline void EndCard() { ImGui::EndChild(); }
}

// FieldWell: the recessed well behind a numeric/combo field (same 6% text tint as ChipButton) and its value fill.
#pragma once
#include <algorithm>
#include "app/AppShared.h"

namespace FieldWell
{
   // Well background; hover and active deepen the tint.
   inline void Draw(ImDrawList* dl, ImVec2 mn, ImVec2 mx, bool hovered, bool active)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const float a = active ? 0.12f : hovered ? 0.10f : 0.06f;
      dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, a)), tok::radius_tile);
   }

   // Accent fill from the left edge to `fillX`, clipped inside the rounded well.
   inline void Fill(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float fillX)
   {
      fillX = std::clamp(fillX, mn.x, mx.x);
      if (fillX <= mn.x) return;
      ImVec4 ac = app::AccentEmphasisSelected();
      ac.w = 0.45f;
      dl->PushClipRect(mn, ImVec2(fillX, mx.y), true);
      dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ac), tok::radius_tile);
      dl->PopClipRect();
   }

   // For ImGui's own frames (DragFloat, combo): the same well colours and rounding; the arrow button is transparent.
   inline void PushStyle()
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(t.x, t.y, t.z, 0.06f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(t.x, t.y, t.z, 0.10f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(t.x, t.y, t.z, 0.12f));
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_tile);
   }
   inline void PopStyle()
   {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(5);
   }
}

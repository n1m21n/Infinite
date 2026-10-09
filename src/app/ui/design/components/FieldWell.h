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
      dl->AddRect(mn, mx, ImGui::GetColorU32(CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f)),
                  tok::radius_tile);
   }

   // Accent fill from the left edge to `fillX`, clipped inside the rounded well.
   inline void Fill(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float fillX)
   {
      fillX = std::clamp(fillX, mn.x, mx.x);
      if (fillX <= mn.x) return;
      ImVec4 ac = app::AccentEmphasisSelected();
      ac.w = 0.32f;
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
      ImGui::PushStyleColor(ImGuiCol_Border, CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_tile);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
   }
   inline void PopStyle()
   {
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(6);
   }

   // Text input in the same well (rename fields).
   inline bool InputText(const char* label, char* buf, size_t size, ImGuiInputTextFlags flags = 0)
   {
      PushStyle();
      const bool r = ImGui::InputText(label, buf, size, flags);
      PopStyle();
      return r;
   }

   // Row of round colour dots, `n` entries, `cols[i]` as ImU32. `selected` is the index with a ring (-1 = none).
   // Returns the clicked index or -1. `names[i]` feeds the tooltip.
   inline int SwatchRow(const ImU32* cols, const char* const* names, int n, int selected, float d = 16.0f)
   {
      int clicked = -1;
      const float gap = 4.0f;
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      for (int i = 0; i < n; ++i)
      {
         if (i > 0) ImGui::SameLine(0.0f, gap);
         ImGui::PushID(i);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         if (ImGui::InvisibleButton("##sw", ImVec2(d, d))) clicked = i;
         const bool hov = ImGui::IsItemHovered();
         if (hov && names != nullptr) ImGui::SetTooltip("%s", names[i]);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
         dl->AddCircleFilled(c, d * 0.5f - (hov ? 0.0f : 1.0f), cols[i]);
         if (i == selected)
            dl->AddCircle(c, d * 0.5f + 2.0f, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.9f)), 0, 1.5f);
         ImGui::PopID();
      }
      return clicked;
   }
}

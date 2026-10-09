// FieldWell: the recessed well behind a numeric/combo field (same 6% text tint as ChipButton) and its value fill.
#pragma once
#include <algorithm>
#include "app/ui/design/UiAnim.h"
#include <type_traits>
#include "imgui_internal.h"
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
   // With an item `id` (the id the widget will get) the hover tint eases in and out like the app's own
   // controls; the rect test uses the next item's cursor position and width, so call it right before the widget.
   inline void PushStyle(ImGuiID id = 0)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      float rest = 0.06f, hover = 0.10f;
      if (id != 0)
      {
         const ImVec2 mn = ImGui::GetCursorScreenPos();
         const bool hot = ImGui::IsMouseHoveringRect(mn, ImVec2(mn.x + ImGui::CalcItemWidth(), mn.y + ImGui::GetFrameHeight())) &&
                          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
         rest = hover = 0.06f + 0.04f * UiAnim::Hover(id, hot, tok::motion_hover_in, tok::motion_hover_out);
      }
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(t.x, t.y, t.z, rest));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(t.x, t.y, t.z, hover));
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

   // Slider in the well: accent fill drawn behind the value text; double-click or Ctrl+click types a number.
   // The first click of a double-click would otherwise snap the value to the click position, so it is undone
   // when the second click opens the text field (Esc then leaves the original value).
   template <class T>
   inline bool Slider(const char* label, T* v, T lo, T hi, const char* fmt, ImGuiSliderFlags fl = 0)
   {
      constexpr ImGuiDataType dt = std::is_same<T, int>::value ? ImGuiDataType_S32 : ImGuiDataType_Float;
      const ImGuiID id = ImGui::GetID(label);
      const float w = ImGui::CalcItemWidth();
      const ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + ImGui::GetFrameHeight());
      static ImGuiID sId = 0;
      static T sPrev = T();
      bool restored = false;
      const bool editing = ImGui::TempInputIsActive(id);
      if (!editing && ImGui::IsMouseHoveringRect(mn, mx) && ImGui::IsMouseClicked(0))
      {
         if (ImGui::GetIO().MouseClickedCount[0] == 1) { sId = id; sPrev = *v; }
         else if (sId == id && *v != sPrev) { *v = sPrev; restored = true; }
      }
      // Hover + start typing a number: opens the field next frame; the first character is replayed into it.
      static ImGuiID sTypeId = 0;
      static ImWchar sTypeCh = 0;
      if (editing && sTypeId == id && sTypeCh != 0)
      {
         ImGui::GetIO().AddInputCharacter(sTypeCh);
         sTypeId = 0;
         sTypeCh = 0;
      }
      else if (!editing && ImGui::GetCurrentContext()->ActiveId == 0 && ImGui::IsMouseHoveringRect(mn, mx) &&
               ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
      {
         for (const ImWchar c : ImGui::GetIO().InputQueueCharacters)
            if ((c >= '0' && c <= '9') || c == '-' || c == '.')
            {
               sTypeId = id;
               sTypeCh = c;
               ImGuiContext& g = *ImGui::GetCurrentContext();
               g.NavNextActivateId = id;
               g.NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
               break;
            }
      }
      if (!editing && hi != lo)
         Fill(ImGui::GetWindowDrawList(), mn, mx, mn.x + w * std::clamp((float)(*v - lo) / (float)(hi - lo), 0.0f, 1.0f));
      PushStyle(id ^ 0x9e3779b9u);
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
      // The text field gets a quiet accent edge instead of ImGui's bright blue nav ring.
      const bool typing = editing || sTypeId == id;
      if (typing)
      {
         ImVec4 ac = app::AccentEmphasisSelected();
         ac.w = 0.6f;
         ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_Border, ac);
      }
      const bool r = ImGui::SliderScalar(label, dt, v, &lo, &hi, fmt, fl | ImGuiSliderFlags_AlwaysClamp);
      if (typing)
         ImGui::PopStyleColor(2);
      ImGui::PopStyleColor(2);
      PopStyle();
      return r || restored;
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

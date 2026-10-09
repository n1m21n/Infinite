// FormParts: the settings-form row grammar. Label in a fixed left column (dimmed), the control in a fixed right
// column; every control is a recessed well with the same chevron / accent-fill construction as the inspector.
// Signatures mirror the ImGui calls they replace (label first, "##" or "###" suffix tolerated), so a call site only
// changes its namespace.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "app/AppShared.h"
#include "core/TablerIcons.h"
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/SectionCard.h"

namespace FormParts
{
   constexpr float kLabelW = 200.0f;
   constexpr float kControlW = 220.0f;
   constexpr float kRowH = 28.0f;

   // The hairline edge every surface carries (same value the Library wells use).
   inline ImU32 EdgeCol()
   {
      return ImGui::GetColorU32(CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f));
   }

   // Round colour dot (same shape as the Clip Settings swatches); a click opens a picker. True when edited.
   inline bool ColorDot(const char* id, float* rgb, float d = 18.0f)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool click = ImGui::InvisibleButton(id, ImVec2(d, d));
      const bool hov = ImGui::IsItemHovered();
      const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddCircleFilled(c, d * 0.5f - (hov ? 0.0f : 1.0f), ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)));
      dl->AddCircle(c, d * 0.5f - (hov ? 0.0f : 1.0f), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.18f)), 0, 1.0f);
      if (click)
         ImGui::OpenPopup(id);
      bool changed = false;
      if (ImGui::BeginPopup(id))
      {
         changed = ImGui::ColorPicker3("##pick", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
         ImGui::EndPopup();
      }
      return changed;
   }

   inline std::string Visible(const char* label)
   {
      const char* k = std::strstr(label, "##");
      return k != nullptr ? std::string(label, k) : std::string(label);
   }

   // Dimmed label in the left column; the control follows on the same line at kLabelW.
   inline void Label(const char* label)
   {
      const std::string v = Visible(label);
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(t.x, t.y, t.z, 0.78f));
      ImGui::TextUnformatted(v.c_str());
      ImGui::PopStyleColor();
      ImGui::SameLine(kLabelW);
   }

   inline std::string Id(const char* label) { return std::string("##") + label; }

   inline void Section(const char* title)
   {
      SectionCard::Begin(title);
   }

   inline bool Checkbox(const char* label, bool* v)
   {
      Label(label);
      // Row height matches the other controls so rows keep one rhythm.
      const float pad = (ImGui::GetFrameHeight() - CheckBox::kSize) * 0.5f;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
      const bool r = CheckBox::Box(Id(label).c_str(), v);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
      return r;
   }

   inline bool BeginCombo(const char* label, const char* preview)
   {
      Label(label);
      ImGui::SetNextItemWidth(kControlW);
      FieldWell::PushStyle();
      const bool open = ImGui::BeginCombo(Id(label).c_str(), preview, ImGuiComboFlags_NoArrowButton);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      glyph::DrawChevronDown(ImGui::GetWindowDrawList(), ImVec2(bmax.x - 12.0f, (bmin.y + bmax.y) * 0.5f), 9.0f,
                             ImGui::GetColorU32(ImGuiCol_TextDisabled));
      FieldWell::PopStyle();
      return open;
   }
   inline void EndCombo() { ImGui::EndCombo(); }

   // `items` is the zero-separated list ImGui::Combo takes.
   inline bool Combo(const char* label, int* current, const char* items)
   {
      std::vector<const char*> list;
      for (const char* it = items; *it != '\0'; it += std::strlen(it) + 1)
         list.push_back(it);
      const char* cur = (*current >= 0 && *current < (int)list.size()) ? list[*current] : "";
      bool changed = false;
      if (BeginCombo(label, cur))
      {
         for (int i = 0; i < (int)list.size(); ++i)
            if (ImGui::Selectable(list[i], *current == i))
            {
               *current = i;
               changed = true;
            }
         ImGui::EndCombo();
      }
      return changed;
   }

   inline void FillSlider(float frac)
   {
      const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
      FieldWell::Fill(ImGui::GetWindowDrawList(), mn, mx, mn.x + (mx.x - mn.x) * std::clamp(frac, 0.0f, 1.0f));
   }

   inline bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt = "%.3f", ImGuiSliderFlags fl = 0)
   {
      Label(label);
      ImGui::SetNextItemWidth(kControlW);
      FieldWell::PushStyle();
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
      const bool r = ImGui::SliderFloat(Id(label).c_str(), v, lo, hi, fmt, fl);
      ImGui::PopStyleColor(2);
      FieldWell::PopStyle();
      FillSlider((*v - lo) / (hi - lo));
      return r;
   }

   inline bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d", ImGuiSliderFlags fl = 0)
   {
      Label(label);
      ImGui::SetNextItemWidth(kControlW);
      FieldWell::PushStyle();
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
      const bool r = ImGui::SliderInt(Id(label).c_str(), v, lo, hi, fmt, fl);
      ImGui::PopStyleColor(2);
      FieldWell::PopStyle();
      FillSlider((float)(*v - lo) / (float)(hi - lo));
      return r;
   }

   // Action button: a chip, 28 pt tall. A size's x is a width floor.
   inline bool Button(const char* label, ImVec2 size = ImVec2(0, 0))
   {
      return ChipButton::Draw(label, false, kRowH, size.x);
   }

   // Frame tint for a tab bar so the tabs read as chips: quiet at rest, a text-tinted well when selected.
   inline void PushTabStyle()
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(t.x, t.y, t.z, 0.08f));
      ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(t.x, t.y, t.z, 0.14f));
      ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_TabDimmed, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, ImVec4(t.x, t.y, t.z, 0.14f));
      ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, tok::radius_tile);
      ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, 0.0f);
   }
   inline void PopTabStyle()
   {
      ImGui::PopStyleVar(3);
      ImGui::PopStyleColor(7);
   }
}

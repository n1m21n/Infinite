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
#include "app/ui/design/GlyphDraw.h"
#include "core/TablerIcons.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/CheckBox.h"
#include "app/ui/design/components/MenuParts.h"
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
      FieldWell::PushStyle(ImGui::GetID(Id(label).c_str()) ^ 0x9e3779b9u);
      MenuParts::PushPopupPad();
      const bool open = ImGui::BeginCombo(Id(label).c_str(), preview, ImGuiComboFlags_NoArrowButton);
      ImGui::PopStyleVar();
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      glyph::DrawChevronDown(ImGui::GetWindowDrawList(), ImVec2(bmax.x - 12.0f, (bmin.y + bmax.y) * 0.5f), 9.0f,
                             ImGui::GetColorU32(ImGuiCol_TextDisabled));
      FieldWell::PopStyle();
      if (open)
         MenuParts::BeginContent();
      return open;
   }
   inline void EndCombo()
   {
      MenuParts::EndContent();
      ImGui::EndCombo();
   }

   // Unlabelled well combo for toolbars: `w` wide, items as a zero-separated list.
   inline bool BareCombo(const char* id, int* current, const char* const* arr, int n, float w)
   {
      std::vector<const char*> list(arr, arr + n);
      const char* cur = (*current >= 0 && *current < (int)list.size()) ? list[*current] : "";
      ImGui::SetNextItemWidth(w);
      FieldWell::PushStyle(ImGui::GetID(id) ^ 0x9e3779b9u);
      MenuParts::PushPopupPad();
      const bool open = ImGui::BeginCombo(id, cur, ImGuiComboFlags_NoArrowButton);
      ImGui::PopStyleVar();
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      glyph::DrawChevronDown(ImGui::GetWindowDrawList(), ImVec2(bmax.x - 12.0f, (bmin.y + bmax.y) * 0.5f), 9.0f,
                             ImGui::GetColorU32(ImGuiCol_TextDisabled));
      FieldWell::PopStyle();
      bool changed = false;
      if (open)
      {
         MenuParts::BeginContent();
         for (int i = 0; i < (int)list.size(); ++i)
            if (MenuParts::Choice(list[i], *current == i))
            {
               *current = i;
               changed = true;
            }
         MenuParts::EndContent();
         ImGui::EndCombo();
      }
      return changed;
   }

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
            if (MenuParts::Choice(list[i], *current == i))
            {
               *current = i;
               changed = true;
            }
         EndCombo();
      }
      return changed;
   }

   inline bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt = "%.3f", ImGuiSliderFlags fl = 0)
   {
      Label(label);
      ImGui::SetNextItemWidth(kControlW);
      return FieldWell::Slider(Id(label).c_str(), v, lo, hi, fmt, fl);
   }

   inline bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d", ImGuiSliderFlags fl = 0)
   {
      Label(label);
      ImGui::SetNextItemWidth(kControlW);
      return FieldWell::Slider(Id(label).c_str(), v, lo, hi, fmt, fl);
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

   // Hairline outline on a floating window; call just before ImGui::End().
   inline void WindowEdge(float rounding = 12.0f)
   {
      const ImVec2 p = ImGui::GetWindowPos(), z = ImGui::GetWindowSize();
      ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(p.x + z.x - 0.5f, p.y + z.y - 0.5f),
                                          EdgeCol(), rounding);
   }

   // Muted section title for long reference text (no card): Body size, medium.
   inline void Heading(const char* text)
   {
      ImGui::Dummy(ImVec2(0.0f, tok::space_1));
      {
         UiType::Scope s(UiType::Size::Body, UiType::Weight::Medium);
         ImGui::TextDisabled("%s", text);
      }
      ImGui::Dummy(ImVec2(0.0f, tok::space_1 * 0.5f));
   }

   // Tables, collapsing headers and bullets in reference windows: hairline rules, quiet banding, rounded headers.
   inline void PushReferenceStyle()
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec4 edge = CategoryColors::IsThemeLight() ? ImVec4(0, 0, 0, 0.07f) : ImVec4(1, 1, 1, 0.06f);
      ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, edge);
      ImGui::PushStyleColor(ImGuiCol_TableBorderLight, edge);
      ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(t.x, t.y, t.z, 0.035f));
      ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(t.x, t.y, t.z, 0.06f));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(t.x, t.y, t.z, 0.09f));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(t.x, t.y, t.z, 0.12f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_tile);
   }
   inline void PopReferenceStyle()
   {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(8);
   }

   // Search / filter field in the well style.
   inline bool SearchInput(const char* id, const char* hint, char* buf, size_t cap, float w)
   {
      ImGui::SetNextItemWidth(w);
      FieldWell::PushStyle(ImGui::GetID(id) ^ 0x9e3779b9u);
      const bool r = ImGui::InputTextWithHint(id, hint, buf, cap);
      FieldWell::PopStyle();
      return r;
   }
}

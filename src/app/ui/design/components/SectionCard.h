// SectionCard: the grouping used by inspectors and dialogs. A soft well (5% text tint, radius_container) sits behind a
// muted title and its rows; Begin() closes the previous card and opens the next, so a caller only marks where a
// section starts. Cards reach kPad into the window padding, so the window needs at least that much of its own.
// Also here: the label column (RowLabel), the dialog title, and a thin progress bar.
#pragma once
#include <algorithm>
#include "app/ui/design/TokenColors.h"
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/DeskParts.h"

namespace SectionCard
{
   constexpr float kPad = tok::space_2;     // card edge to content, and how far a card reaches into the window padding
   constexpr float kLabelW = tok::label_w;  // field label column
   // The same two at the current density (UiDensity): use these inside a panel's settings.
   inline float Pad() { return UiDensity::M().pad; }
   inline float LabelW() { return UiDensity::M().label; }

   struct State
   {
      ImDrawListSplitter split;
      bool open = false;
      float top = 0.0f;
      bool desk = false;   // a panel with the desk look sets this: cards draw as graphite faces
      ImU32 deskCap = 0;
      bool header = false;   // a card with a header row (collapsed or switched off) keeps its well
   };
   inline State& S()
   {
      static State s;
      return s;
   }

   // The soft well itself (5% text tint, hairline edge, radius_container), on the current window's draw list.
   inline void Well(ImVec2 a, ImVec2 b)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (S().desk)
      {
         DeskParts::Face(dl, a, b, S().deskCap, false);
         return;
      }
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, tok::alpha_card)), tok::radius_container);
      dl->AddRect(a, b, ImGui::GetColorU32(tok::Hairline(CategoryColors::IsThemeLight())),
                  tok::radius_container);
   }

   // Call right after the window / child begins (and before its first card).
   inline void BeginWindow()
   {
      S().open = false;
      S().split.Split(ImGui::GetWindowDrawList(), 2);
      S().split.SetCurrentChannel(ImGui::GetWindowDrawList(), 1);
   }
   // Call before the window / child ends.
   inline void End();
   inline void EndWindow()
   {
      End();
      S().split.Merge(ImGui::GetWindowDrawList());
   }

   inline void End()
   {
      State& s = S();
      if (!s.open)
         return;
      s.open = false;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 wp = ImGui::GetWindowPos();
      const float x0 = wp.x + ImGui::GetWindowContentRegionMin().x - Pad();
      const float x1 = wp.x + ImGui::GetWindowContentRegionMax().x + Pad();
      const float y1 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + Pad();
      const bool header = s.header;
      s.header = false;
      if (header || y1 - s.top >= 4.0f * Pad())   // nothing drawn in it: no empty sliver; a header alone is still a card
      {
         s.split.SetCurrentChannel(dl, 0);
         Well(ImVec2(x0, s.top - Pad()), ImVec2(x1, y1));
         s.split.SetCurrentChannel(dl, 1);
      }
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, y1 - ImGui::GetStyle().ItemSpacing.y));
      ImGui::Dummy(ImVec2(0.0f, 0.0f));   // commits the cursor move (ImGui asks for an item after SetCursorPos)
   }

   // A card with no title.
   inline void Begin()
   {
      End();
      S().header = false;
      ImGui::Dummy(ImVec2(0.0f, Pad()));
      S().top = ImGui::GetCursorScreenPos().y;
      S().open = true;
   }

   // The card just begun has a collapsible header: it keeps its well when closed or switched off (Blender panels).
   inline void MarkHeader() { S().header = true; }

   // A card with a muted title (Body size, medium).
   inline void Begin(const char* title)
   {
      Begin();
      UiType::Scope s(UiType::Size::Body, UiType::Weight::Medium);
      ImGui::TextDisabled("%s", title);
      ImGui::Dummy(ImVec2(0.0f, tok::space_1 * 0.5f));
   }

   // Field label in the fixed left column; the field follows on the same line.
   inline void RowLabel(const char* text)
   {
      ImGui::AlignTextToFramePadding();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(t.x, t.y, t.z, tok::alpha_label));
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
      ImGui::SameLine(LabelW());
   }

   // Window title: Title size, semibold.
   inline void Title(const char* text)
   {
      UiType::Scope s(UiType::Size::Body, UiType::Weight::Semibold);
      ImGui::TextUnformatted(text);
   }

   // Thin rounded progress bar. frac < 0 = indeterminate (a bar sweeping across).
   inline void Progress(float frac, float w)
   {
      const float h = tok::progress_h;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(w, h));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.10f)), h * 0.5f);
      ImVec4 a = app::AccentEmphasisSelected();
      float x0 = p.x, x1 = p.x;
      if (frac >= 0.0f)
         x1 = p.x + w * std::clamp(frac, 0.0f, 1.0f);
      else
      {
         const float ph = UiAnim::ReduceMotion() ? 0.7f : std::fmod((float)ImGui::GetTime() * 0.8f, 1.0f);   // still bar under reduce-motion
         x0 = p.x + w * std::max(0.0f, ph * 1.4f - 0.4f);
         x1 = p.x + w * std::min(1.0f, ph * 1.4f);
      }
      if (x1 - x0 > 0.5f)
         dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), ImGui::GetColorU32(a), h * 0.5f);
   }
}

// SectionCard: the grouping used by inspectors and dialogs. A soft well (5% text tint, radius_group) sits behind a
// muted title and its rows; Begin() closes the previous card and opens the next, so a caller only marks where a
// section starts. Cards reach kPad into the window padding, so the window needs at least that much of its own.
// Also here: the label column (RowLabel), the dialog title, and a thin progress bar.
#pragma once
#include <algorithm>
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/ChipButton.h"

namespace SectionCard
{
   constexpr float kPad = tok::space_2;     // card edge to content, and how far a card reaches into the window padding
   constexpr float kLabelW = 72.0f;         // field label column

   struct State
   {
      ImDrawListSplitter split;
      bool open = false;
      float top = 0.0f;
   };
   inline State& S()
   {
      static State s;
      return s;
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
      const float x0 = wp.x + ImGui::GetWindowContentRegionMin().x - kPad;
      const float x1 = wp.x + ImGui::GetWindowContentRegionMax().x + kPad;
      const float y1 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + kPad;
      if (y1 - s.top >= 4.0f * kPad)   // nothing drawn in it: no empty sliver
      {
         s.split.SetCurrentChannel(dl, 0);
         const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         dl->AddRectFilled(ImVec2(x0, s.top - kPad), ImVec2(x1, y1), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.05f)),
                           tok::radius_group);
         s.split.SetCurrentChannel(dl, 1);
      }
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, y1 - ImGui::GetStyle().ItemSpacing.y));
      ImGui::Dummy(ImVec2(0.0f, 0.0f));   // commits the cursor move (ImGui asks for an item after SetCursorPos)
   }

   // A card with no title.
   inline void Begin()
   {
      End();
      ImGui::Dummy(ImVec2(0.0f, kPad));
      S().top = ImGui::GetCursorScreenPos().y;
      S().open = true;
   }

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
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(t.x, t.y, t.z, 0.72f));
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
      ImGui::SameLine(kLabelW);
   }

   // Window title: Title size, semibold.
   inline void Title(const char* text)
   {
      UiType::Scope s(UiType::Size::Title, UiType::Weight::Semibold);
      ImGui::TextUnformatted(text);
   }

   // Thin rounded progress bar. frac < 0 = indeterminate (a bar sweeping across).
   inline void Progress(float frac, float w)
   {
      const float h = 6.0f;
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
         const float ph = std::fmod((float)ImGui::GetTime() * 0.8f, 1.0f);
         x0 = p.x + w * std::max(0.0f, ph * 1.4f - 0.4f);
         x1 = p.x + w * std::min(1.0f, ph * 1.4f);
      }
      if (x1 - x0 > 0.5f)
         dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), ImGui::GetColorU32(a), h * 0.5f);
   }
}

// BusyLine: the one "something is happening" row. A calm spinner (a still ring under Reduce motion), what is
// happening in words, and either a progress bar (progress >= 0) or a count. Cancel appears only where the task
// can really stop. Never blocks: it is drawn each frame while a worker or a staged job runs.
#pragma once
#include <cmath>
#include "imgui.h"
#include "core/CategoryColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiType.h"

namespace BusyLine
{
   constexpr float kSpinner = 12.0f;

   inline void Spinner(float size = kSpinner)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float h = ImGui::GetTextLineHeight();
      const ImVec2 c(p.x + size * 0.5f, p.y + h * 0.5f);
      const float r = size * 0.5f - 1.0f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 track = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
      const ImU32 head = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      dl->AddCircle(c, r, track, 24, 1.5f);
      const bool still = CategoryColors::GetReduceMotion();
      const float a0 = still ? -1.5708f : (float)ImGui::GetTime() * 4.0f;
      dl->PathClear();
      for (int i = 0; i <= 8; ++i)
      {
         const float a = a0 + 2.0f * (float)i / 8.0f;
         dl->PathLineTo(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r));
      }
      dl->PathStroke(head, ImDrawFlags_None, 1.5f);
      ImGui::Dummy(ImVec2(size, h));
   }

   // `progress` 0..1, or < 0 for "no total known". `count` >= 0 appends "(n)". Returns true when Cancel is clicked.
   inline bool Draw(const char* what, float progress = -1.0f, int count = -1, bool cancellable = false)
   {
      Spinner();
      ImGui::SameLine(0.0f, tok::space_2);
      if (count >= 0)
         ImGui::TextDisabled("%s (%d)", what, count);
      else
         ImGui::TextDisabled("%s", what);
      bool cancelled = false;
      if (cancellable)
      {
         ImGui::SameLine(0.0f, tok::space_2);
         cancelled = ImGui::SmallButton("Cancel");
      }
      if (progress >= 0.0f)
         ImGui::ProgressBar(progress > 1.0f ? 1.0f : progress, ImVec2(-1.0f, 4.0f), "");
      return cancelled;
   }
}

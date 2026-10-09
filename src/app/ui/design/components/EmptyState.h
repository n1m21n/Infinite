// EmptyState: what a list or panel says when it has nothing to list. One dimmed Body line, an optional Caption
// hint under it, centred in the rectangle it is given. Drawn, not laid out, so it can sit over table filler rows.
#pragma once
#include "imgui.h"
#include <algorithm>
#include "app/ui/design/UiType.h"
#include "app/ui/design/Tokens.gen.h"

namespace EmptyState
{
   // One centred line, or several when it is wider than the rectangle: words wrap, every line stays centred.
   inline float DrawWrapped(ImDrawList* dl, float cx, float y, float maxW, ImU32 col, const char* text)
   {
      const float lineH = ImGui::GetTextLineHeight();
      const char* p = text;
      while (*p != '\0')
      {
         const char* end = p;
         const char* lastBreak = nullptr;
         while (*end != '\0')
         {
            if (*end == ' ')
               lastBreak = end;
            if (ImGui::CalcTextSize(p, end + 1).x > maxW && lastBreak != nullptr)
            {
               end = lastBreak;
               break;
            }
            ++end;
         }
         const ImVec2 sz = ImGui::CalcTextSize(p, end);
         dl->AddText(ImVec2(cx - sz.x * 0.5f, y), col, p, end);
         y += lineH;
         p = (*end == ' ') ? end + 1 : end;
      }
      return y;
   }

   inline void Draw(const ImVec2& min, const ImVec2& max, const char* message, const char* hint = nullptr)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const float cx = (min.x + max.x) * 0.5f;
      const float maxW = std::max(40.0f, max.x - min.x - 2.0f * tok::space_3);
      float y = (min.y + max.y) * 0.5f - (hint != nullptr ? tok::space_3 : tok::space_2);
      {
         UiType::Scope body(UiType::Size::Body, UiType::Weight::Medium);
         y = DrawWrapped(dl, cx, y, maxW, dim, message) + tok::space_1;
      }
      if (hint != nullptr)
      {
         UiType::Scope cap(UiType::Size::Caption);
         DrawWrapped(dl, cx, y, maxW, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f), hint);
      }
   }

   // The node-body variant: one centred Caption-size line (previews and scopes are small, so no Body weight).
   inline void DrawCaption(const ImVec2& min, const ImVec2& max, const char* message)
   {
      UiType::Scope cap(UiType::Size::Caption);
      const ImVec2 ts = ImGui::CalcTextSize(message);
      const float cx = (min.x + max.x) * 0.5f;
      const float maxW = std::max(40.0f, max.x - min.x - 2.0f * tok::space_2);
      if (ts.x <= maxW)
      {
         // A quiet scrim so a baseline or checker behind the caption never strikes through it.
         const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         const bool darkTheme = (t.x + t.y + t.z) > 1.5f;
         const float y0 = (min.y + max.y) * 0.5f - ts.y * 0.5f;
         ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cx - ts.x * 0.5f - 4.0f, y0 - 1.0f), ImVec2(cx + ts.x * 0.5f + 4.0f, y0 + ts.y + 1.0f),
                                                   darkTheme ? IM_COL32(0, 0, 0, 120) : IM_COL32(255, 255, 255, 150), 3.0f);
      }
      DrawWrapped(ImGui::GetWindowDrawList(), cx, (min.y + max.y) * 0.5f - ts.y * 0.5f, maxW, ImGui::GetColorU32(ImGuiCol_TextDisabled), message);
   }

   // Convenience: centred in the current window's content area.
   inline void DrawInWindow(const char* message, const char* hint = nullptr)
   {
      const ImVec2 p = ImGui::GetWindowPos();
      Draw(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetWindowWidth(), p.y + ImGui::GetWindowHeight()), message, hint);
   }
}

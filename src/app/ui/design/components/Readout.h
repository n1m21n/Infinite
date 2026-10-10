// Readout: a value shown as text with tabular digits (every 0-9 takes the same width, so a changing number
// never jitters its neighbours). Non-interactive; records a Readout node in the semantic tree.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/UiType.h"
#include <cstring>

namespace Readout
{
   enum class Align { Left, Center, Right };

   inline float DigitWidth()
   {
      float w = 0.0f;
      for (char c = '0'; c <= '9'; ++c)
      {
         const char s[2] = { c, 0 };
         w = std::max(w, ImGui::CalcTextSize(s).x);
      }
      return w;
   }

   // Width of `text` with tabular digits, in the current font.
   inline float Measure(const char* text)
   {
      const float dw = DigitWidth();
      float w = 0.0f;
      for (const char* c = text; *c != 0; ++c)
      {
         const char s[2] = { *c, 0 };
         w += (*c >= '0' && *c <= '9') ? dw : ImGui::CalcTextSize(s).x;
      }
      return w;
   }

   inline void Draw(const char* key, const UiLayout::Rect& r, const char* text, const char* spokenLabel,
                    UiType::Size size = UiType::Size::Body, UiType::Weight weight = UiType::Weight::Regular,
                    Align align = Align::Right, float alpha = 1.0f)
   {
      UiType::Scope ts(size, weight);
      const float dw = DigitWidth();
      const float total = Measure(text);
      float x = align == Align::Left ? r.x : align == Align::Center ? r.CenterX() - total * 0.5f : r.Right() - total;
      const float y = std::round(r.CenterY() - ImGui::GetTextLineHeight() * 0.5f);
      ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      col.w *= alpha;
      const ImU32 c32 = ImGui::GetColorU32(col);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (const char* c = text; *c != 0; ++c)
      {
         const char s[2] = { *c, 0 };
         const bool digit = *c >= '0' && *c <= '9';
         const float cw = digit ? dw : ImGui::CalcTextSize(s).x;
         const float gx = digit ? x + (dw - ImGui::CalcTextSize(s).x) * 0.5f : x;
         dl->AddText(ImVec2(std::round(gx), y), c32, s);
         x += cw;
      }
      UiInteract::Note(key, r, UiInteract::Role::Readout, spokenLabel, text);
   }
}

// TransportDisplay: the tempo / signature / key readout in the top bar. One rounded well holds cells
// (a tabular value), cells split by hairlines; each editable value is its own hover target.
// Drawing only: the transport logic stays with the caller.
#pragma once
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/Readout.h"

namespace TransportDisplay
{
   constexpr float kHeight = 32.0f;
   constexpr float kValueH = 18.0f;
   constexpr float kValueY = (kHeight - kValueH) * 0.5f;  // value line top, from the well's top edge: centred

   inline ImVec4 Text(float a)
   {
      ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      t.w *= a;
      return t;
   }

   inline void Well(ImVec2 mn, ImVec2 mx)
   {
      ImGui::GetWindowDrawList()->AddRectFilled(mn, mx, ImGui::GetColorU32(Text(0.06f)), tok::radius_group);
   }

   inline void Divider(float x, float top)
   {
      ImGui::GetWindowDrawList()->AddLine(ImVec2(std::round(x), top + 6.0f), ImVec2(std::round(x), top + kHeight - 6.0f),
                                          ImGui::GetColorU32(Text(0.12f)), 1.0f);
   }

   inline float ValueWidth(const char* widest)
   {
      UiType::Scope ts(UiType::Size::Title, UiType::Weight::Medium);
      return Readout::Measure(widest);
   }

   inline void Value(const char* key, float x, float w, float top, const char* text, float alpha = 1.0f)
   {
      Readout::Draw(key, UiLayout::Rect { x, top + kValueY, w, kValueH }, text, text, UiType::Size::Title,
                    UiType::Weight::Medium, Readout::Align::Center, alpha);
   }

   // Hover/press wash over one editable value (the well's own 6% stays underneath).
   inline void Wash(ImVec2 mn, ImVec2 mx, float hover, bool down)
   {
      const float a = down ? 0.12f : 0.07f * hover;
      if (a > 0.001f)
         ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x + 1, mn.y + 2), ImVec2(mx.x - 1, mx.y - 2),
                                                   ImGui::GetColorU32(Text(a)), tok::radius_tile);
   }
}

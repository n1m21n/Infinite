// VFader: drawing of the vertical fader (C3). A 6 pt rounded rail in the field-well tint, accent fill from the
// bottom, tick marks at the given positions, and a pill handle with a centre line. One path for both themes: every
// tone is the text colour at an alpha. Hit-testing and value mapping stay with the caller.
#pragma once
#include <algorithm>
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiAnim.h"

namespace VFader
{
   constexpr float kHandleW = 18.0f;
   constexpr float kHandleH = 12.0f;

   inline ImU32 PinDotAlpha(ImU32 c, float a)
   {
      return (c & 0x00FFFFFFu) | ((ImU32)(a * (float)((c >> 24) & 0xFF)) << 24);
   }

   // `ticks` = y positions (screen) of the tick marks; `rangeLo/Hi` = y of an optional modulation band.
   inline void Draw(ImDrawList* dl, float cx, float top, float bottom, float capY, bool hasFill, ImU32 fill,
                    const float* ticks, int nTicks, bool hasRange, float rangeYLo, float rangeYHi,
                    bool hovered, bool active, bool readOnly, bool isLight)
   {
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      auto tone = [&](float a) { return ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, a)); };
      dl->AddRectFilled(ImVec2(cx - 3.0f, top - 4.0f), ImVec2(cx + 3.0f, bottom + 4.0f), tone(0.10f), 3.0f);
      if (hasRange)
         dl->AddRectFilled(ImVec2(cx - 5.0f, std::min(rangeYLo, rangeYHi)), ImVec2(cx + 5.0f, std::max(rangeYLo, rangeYHi)),
                           PinDotAlpha(tok::U32(tok::pin_mod, isLight), 0.45f));
      if (hasFill)
         dl->AddRectFilled(ImVec2(cx - 2.0f, capY), ImVec2(cx + 2.0f, bottom + 4.0f), fill, 2.0f);
      for (int i = 0; i < nTicks; ++i)
      {
         dl->AddLine(ImVec2(cx + 6.0f, ticks[i]), ImVec2(cx + 9.0f, ticks[i]), tone(0.18f), 1.0f);
         dl->AddLine(ImVec2(cx - 9.0f, ticks[i]), ImVec2(cx - 6.0f, ticks[i]), tone(0.18f), 1.0f);
      }
      const ImVec2 a(cx - kHandleW * 0.5f, capY - kHandleH * 0.5f), b(cx + kHandleW * 0.5f, capY + kHandleH * 0.5f);
      const float lift = readOnly ? 0.0f : (hovered || active ? 0.06f : 0.0f);
      dl->AddRectFilled(a, b, tone(readOnly ? 0.14f : 0.22f + lift), kHandleH * 0.5f);
      dl->AddLine(ImVec2(cx - 4.0f, capY), ImVec2(cx + 4.0f, capY), tone(readOnly ? 0.35f : 0.8f), 1.6f);
      if (active && !readOnly)
      {
         ImVec4 acc = app::AccentEmphasisSelected();
         acc.w = 0.8f;
         dl->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), ImGui::GetColorU32(acc), kHandleH * 0.5f + 1.0f, 0, 2.0f);
      }
   }
}

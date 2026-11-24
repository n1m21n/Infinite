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
   constexpr float kHandleW = tok::fader_handle_w;
   constexpr float kHandleH = tok::fader_handle_h;

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
      dl->AddRectFilled(ImVec2(cx - tok::space_0_75, top - tok::space_1), ImVec2(cx + tok::space_0_75, bottom + tok::space_1), tone(tok::v_fader_tone_alpha_xs), tok::v_fader_fill_radius);
      if (hasRange)
         dl->AddRectFilled(ImVec2(cx - tok::space_1_25, std::min(rangeYLo, rangeYHi)), ImVec2(cx + tok::space_1_25, std::max(rangeYLo, rangeYHi)),
                           PinDotAlpha(tok::U32(tok::pin_mod, isLight), tok::v_fader_pin_alpha));
      if (hasFill)
         dl->AddRectFilled(ImVec2(cx - tok::space_0_5, capY), ImVec2(cx + tok::space_0_5, bottom + tok::space_1), fill, tok::radius_cell);
      for (int i = 0; i < nTicks; ++i)
      {
         dl->AddLine(ImVec2(cx + tok::space_1_5, ticks[i]), ImVec2(cx + tok::v_fader_line_x, ticks[i]), tone(tok::v_fader_tone_alpha_md), tok::v_fader_line_stroke_sm);
         dl->AddLine(ImVec2(cx - tok::v_fader_line_x, ticks[i]), ImVec2(cx - tok::space_1_5, ticks[i]), tone(tok::v_fader_tone_alpha_md), tok::v_fader_line_stroke_sm);
      }
      const ImVec2 a(cx - kHandleW * 0.5f, capY - kHandleH * 0.5f), b(cx + kHandleW * 0.5f, capY + kHandleH * 0.5f);
      const float lift = readOnly ? 0.0f : (hovered || active ? 0.06f : 0.0f);
      dl->AddRectFilled(a, b, tone(readOnly ? tok::v_fader_tone_alpha_sm : tok::v_fader_tone_inset + lift), kHandleH * 0.5f);
      dl->AddLine(ImVec2(cx - tok::space_1, capY), ImVec2(cx + tok::space_1, capY), tone(readOnly ? tok::v_fader_tone_alpha_lg : tok::v_fader_tone_alpha_xl), tok::v_fader_line_stroke_lg);
      if (active && !readOnly)
      {
         ImVec4 acc = app::AccentEmphasisSelected();
         acc.w = tok::v_fader_w;
         dl->AddRect(ImVec2(a.x - tok::space_0_25, a.y - tok::space_0_25), ImVec2(b.x + tok::space_0_25, b.y + tok::space_0_25), ImGui::GetColorU32(acc), kHandleH * 0.5f + tok::space_0_25, 0, tok::v_fader_outline_stroke);
      }
   }
}

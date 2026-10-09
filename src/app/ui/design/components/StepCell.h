// StepCell: the one look for a gate/step cell in every step grid (Arpeggiator gates, Stutter gates, ...).
// A recess with a border; "on" fills it with the accent tint, and the playhead cell goes bright with a
// heavier border. Interaction (click, paint-drag) stays with the caller, which passes `hovered`.
#pragma once
#include <imgui.h>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"

namespace StepCell
{
   inline void Draw(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, bool on, bool playhead = false,
                    bool hovered = false)
   {
      const bool light = app::IsThemeLight();
      const float r = tok::radius_field;
      dl->AddRectFilled(mn, mx, light ? tok::U32(tok::pal::c_DCE1EBFF) : tok::U32(tok::pal::c_0B0C10FF), r);
      if (on)
         dl->AddRectFilled(mn, mx,
                           playhead ? (light ? tok::U32(tok::pal::c_FFFFFFFF) : tok::U32(tok::pal::c_E1F2FFFF))
                                    : (light ? tok::U32(tok::pal::c_2878EBF0) : tok::U32(tok::pal::c_96D6FFC8)),
                           r);
      const ImU32 edge = playhead ? (light ? tok::U32(tok::pal::c_1E64E6FF) : tok::U32(tok::pal::c_FFFFFFFF))
                                  : (hovered ? (light ? tok::U32(tok::pal::c_1E64E6FF) : tok::U32(tok::pal::c_96D6FFC8))
                                             : (light ? tok::U32(tok::pal::c_B4BCCCFF) : tok::U32(tok::pal::c_404454FF)));
      dl->AddRect(mn, mx, edge, r, 0, playhead ? 2.0f : 1.0f);
   }
}

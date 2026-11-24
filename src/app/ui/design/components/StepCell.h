// StepCell: the one look for a gate/step cell in every step grid (Arpeggiator gates, Stutter gates, ...).
// A recess with a border; "on" fills it with the accent tint, and the playhead cell goes bright with a
// heavier border. Interaction (click, paint-drag) stays with the caller, which passes `hovered`.
#pragma once
#include <imgui.h>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"

namespace StepCell
{
   // Playhead outline for column-style step displays (bars, piano-roll columns): same edge as a gate cell's playhead.
   inline void PlayheadOutline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float thickness = tok::stroke_ring)
   {
      const bool light = app::IsThemeLight();
      dl->AddRect(mn, mx, light ? tok::Base(tok::col::state_warn, tok::step_cell_playhead_outline_state_warn_alpha) : tok::Base(tok::col::role_modulation, tok::step_cell_playhead_outline_role_modulation_alpha), tok::radius_control, 0,
                  thickness);
   }

   inline void Draw(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, bool on, bool playhead = false,
                    bool hovered = false)
   {
      const bool light = app::IsThemeLight();
      const float r = tok::radius_control;
      dl->AddRectFilled(mn, mx, light ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::surface_deep), r);
      if (on)
         dl->AddRectFilled(mn, mx,
                           playhead ? (light ? tok::Base(tok::col::text_warn) : tok::Base(tok::col::role_modulation))
                                    : (light ? tok::Base(tok::col::accent, tok::step_cell_alpha) : tok::Base(tok::col::accent_alt, tok::step_cell_edge_alpha_sm)),
                           r);
      const ImU32 edge = playhead ? (light ? tok::Base(tok::col::state_warn, tok::step_cell_edge_alpha_lg) : tok::Base(tok::col::role_modulation, tok::step_cell_edge_alpha_md))
                                  : (hovered ? (light ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::step_cell_edge_alpha_sm))
                                             : (light ? tok::Base(tok::col::edge_mid_light_fixed) : tok::Base(tok::col::edge_lo_light)));
      dl->AddRect(mn, mx, edge, r, 0, playhead ? tok::step_cell_outline_stroke_lg : tok::step_cell_outline_stroke_sm);
   }
}

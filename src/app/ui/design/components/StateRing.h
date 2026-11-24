// StateRing: the one outline used to mark a control or card that is selected, listening for MIDI, or a drop target.
// Same outset (2 pt), same weight (1.5 pt) everywhere; only the colour role and the pulse differ.
#pragma once
#include <cmath>
#include "imgui.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/Tokens.gen.h"

namespace StateRing
{
   enum class Kind { Select, Learn, Target };

   inline void Draw(ImDrawList* dl, ImVec2 a, ImVec2 b, Kind kind, bool isLight, float radius = tok::radius_control)
   {
      constexpr float kOutset = tok::state_ring_outset;
      constexpr float kWeight = tok::stroke_ring;
      ImU32 col = kind == Kind::Learn ? (isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation))
                                      : (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_xlo));
      if (kind == Kind::Learn)
      {
         const float pulse = UiAnim::ReduceMotion() ? 1.0f : 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
         col = (col & 0x00FFFFFF) | ((ImU32)((tok::state_ring_inset + tok::state_ring_scale * pulse) * 255.0f) << 24);
      }
      const ImVec2 ra(a.x - kOutset, a.y - kOutset), rb(b.x + kOutset, b.y + kOutset);
      if (kind == Kind::Target)
         dl->AddRectFilled(ra, rb, (col & 0x00FFFFFF) | ((ImU32)tok::state_ring_target_alpha << 24), radius + kOutset);
      dl->AddRect(ra, rb, col, radius + kOutset, 0, kWeight);
   }

   // Same ring around a round control (knob).
   inline void DrawCircle(ImDrawList* dl, ImVec2 c, float r, Kind kind, bool isLight)
   {
      ImU32 col = kind == Kind::Learn ? (isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation))
                                      : (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_xlo));
      if (kind == Kind::Learn)
      {
         const float pulse = UiAnim::ReduceMotion() ? 1.0f : 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
         col = (col & 0x00FFFFFF) | ((ImU32)((tok::state_ring_circle_inset + tok::state_ring_circle_ratio * pulse) * 255.0f) << 24);
      }
      dl->AddCircle(c, r + tok::space_0_5, col, 0, tok::state_ring_circle_stroke);
   }
}

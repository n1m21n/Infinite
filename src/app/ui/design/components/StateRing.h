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

   inline void Draw(ImDrawList* dl, ImVec2 a, ImVec2 b, Kind kind, bool isLight, float radius = tok::radius_tile)
   {
      constexpr float kOutset = 2.0f;
      constexpr float kWeight = 1.5f;
      ImU32 col = kind == Kind::Learn ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF))
                                      : (isLight ? tok::U32(tok::pal::c_1E6EDCFF) : tok::U32(tok::pal::c_5FA5FFFF));
      if (kind == Kind::Learn)
      {
         const float pulse = UiAnim::ReduceMotion() ? 1.0f : 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
         col = (col & 0x00FFFFFF) | ((ImU32)((0.55f + 0.45f * pulse) * 255.0f) << 24);
      }
      const ImVec2 ra(a.x - kOutset, a.y - kOutset), rb(b.x + kOutset, b.y + kOutset);
      if (kind == Kind::Target)
         dl->AddRectFilled(ra, rb, (col & 0x00FFFFFF) | (0x22u << 24), radius + kOutset);
      dl->AddRect(ra, rb, col, radius + kOutset, 0, kWeight);
   }

   // Same ring around a round control (knob).
   inline void DrawCircle(ImDrawList* dl, ImVec2 c, float r, Kind kind, bool isLight)
   {
      ImU32 col = kind == Kind::Learn ? (isLight ? tok::U32(tok::pal::c_D77D14FF) : tok::U32(tok::pal::c_FFBE5AFF))
                                      : (isLight ? tok::U32(tok::pal::c_1E6EDCFF) : tok::U32(tok::pal::c_5FA5FFFF));
      if (kind == Kind::Learn)
      {
         const float pulse = UiAnim::ReduceMotion() ? 1.0f : 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
         col = (col & 0x00FFFFFF) | ((ImU32)((0.55f + 0.45f * pulse) * 255.0f) << 24);
      }
      dl->AddCircle(c, r + 2.0f, col, 0, 1.5f);
   }
}

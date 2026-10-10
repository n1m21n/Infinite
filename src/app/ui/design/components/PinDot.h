// PinDot: the one drawing of every pin. Three families, one size table (tokens.json "pin"), one colour rule.
//   Cable   - the round patch pin on a node edge or in a body (data = blue, prediction = green).
//   Param   - the ring beside a control that a cable or expression can drive. Idle = grey ring; driven = filled
//             centre in the source colour (modulation amber, expression violet, prediction green) that pulses gently.
//   Swatch  - the round palette pin beside a colour field (filled, where the param pin is a ring: it accepts a palette cable).
// Call sites pass state, never a colour, radius or duration.
#pragma once
#include <cmath>
#include "imgui.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiAnim.h"

namespace PinDot
{
   enum class State { Idle, Modulated, Expression, Prediction };

   inline bool Driven(State s) { return s != State::Idle; }

   inline ImU32 Colour(State s, bool isLight)
   {
      switch (s)
      {
      case State::Modulated: return tok::U32(tok::pin_mod, isLight);
      case State::Expression: return tok::U32(tok::pin_expr, isLight);
      case State::Prediction: return tok::U32(tok::pin_pred, isLight);
      default: return tok::U32(tok::pin_idle, isLight);
      }
   }

   // 0.72..1.0 slow sine while driven; steady at rest and under reduce-motion.
   inline float Pulse(State s)
   {
      if (!Driven(s) || UiAnim::ReduceMotion())
         return 1.0f;
      return 0.86f + 0.14f * std::sin((float)ImGui::GetTime() * 2.6f);
   }

   inline ImU32 WithAlpha(ImU32 c, float a)
   {
      const ImU32 base = (c >> 24) & 0xFF;
      return (c & 0x00FFFFFFu) | ((ImU32)(a * (float)base) << 24);
   }

   // Param pin centred on `c`. Sits in a tok::pin_box square.
   inline void Param(ImDrawList* dl, ImVec2 c, State s, bool isLight)
   {
      const bool on = Driven(s);
      const ImU32 col = WithAlpha(Colour(s, isLight), Pulse(s));
      dl->AddCircleFilled(c, tok::pin_r_param_on, tok::U32(tok::pin_well, isLight));
      dl->AddCircle(c, on ? tok::pin_r_param_on : tok::pin_r_param, col, 12, 2.0f);
      if (on)
         dl->AddCircleFilled(c, tok::pin_r_param_on * 0.5f, col);
   }

   // Patch pin. `small` is the in-body lane/output pin.
   inline void Cable(ImDrawList* dl, ImVec2 c, bool prediction, bool isLight, bool small = false)
   {
      const float r = small ? tok::pin_r_cable_sm : tok::pin_r_cable;
      dl->AddCircleFilled(c, r, tok::U32(prediction ? tok::pin_cable_pred : tok::pin_cable, isLight));
      dl->AddCircle(c, r, tok::U32(tok::pin_edge, isLight), 0, 1.5f);
   }

   // Palette pin: a small filled dot.
   inline void Swatch(ImDrawList* dl, ImVec2 c, bool bound, bool isLight)
   {
      const float r = tok::pin_r_colour;
      dl->AddCircleFilled(c, r + 0.5f, tok::U32(bound ? tok::pin_colour_bound : tok::pin_colour, isLight));
   }
}

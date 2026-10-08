// Knob: the one painter behind every rotary control (KnobFloat, BipolarKnobFloat). A quiet text-tinted disc, a muted
// track arc, an accent value arc and a thin pointer. Colours come from the theme text colour, so light and dark share
// one construction. Hit-testing, drag and captions stay with the callers.
#pragma once
#include <algorithm>
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"

namespace Knob
{
   constexpr float kTwoPi = 6.28318530717958647692f;
   constexpr float kAMin = 0.75f * kTwoPi * 0.5f;  // 135 deg, start of the 270 deg sweep
   constexpr float kAMax = 2.25f * kTwoPi * 0.5f;  // 405 deg
   constexpr float kAMid = 1.50f * kTwoPi * 0.5f;  // 12 o'clock

   inline ImU32 Text(float a)
   {
      ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      t.w *= a;
      return ImGui::GetColorU32(t);
   }

   inline ImU32 CaptionColor(bool readOnly) { return Text(readOnly ? 0.5f : 0.85f); }

   struct Look
   {
      float t = 0.0f;            // 0..1 position
      bool bipolar = false;      // value arc runs from 12 o'clock instead of from the minimum
      ImU32 fill = 0;            // value arc colour
      bool readOnly = false;
      bool activeTint = false;   // recording: track turns the recording colour
      bool hovered = false;
      bool hasRange = false;     // modulation band, drawn under the value arc
      float angleLo = 0.0f, angleHi = 0.0f;
      ImU32 rangeFill = 0;
   };

   inline void Paint(ImDrawList* dl, ImVec2 c, float r, const Look& k)
   {
      const float angle = kAMin + k.t * (kAMax - kAMin);
      const float arcR = r + 2.5f;
      dl->AddCircleFilled(c, r, Text(0.07f), 32);
      dl->PathArcTo(c, arcR, kAMin, kAMax, 32);
      dl->PathStroke(k.activeTint ? k.fill : Text(0.16f), 0, 3.0f);
      if (k.hasRange && std::fabs(k.angleHi - k.angleLo) > 1e-4f)
      {
         dl->PathArcTo(c, arcR, std::min(k.angleLo, k.angleHi), std::max(k.angleLo, k.angleHi), 32);
         dl->PathStroke(k.rangeFill, 0, 5.0f);
      }
      if (k.bipolar)
      {
         dl->AddLine(ImVec2(c.x, c.y - r - 5.0f), ImVec2(c.x, c.y - r), Text(0.4f), 1.5f);
         if (angle != kAMid)
         {
            dl->PathArcTo(c, arcR, std::min(angle, kAMid), std::max(angle, kAMid), 32);
            dl->PathStroke(k.fill, 0, 3.0f);
         }
      }
      else if (k.t > 0.0f)
      {
         dl->PathArcTo(c, arcR, kAMin, angle, 32);
         dl->PathStroke(k.fill, 0, 3.0f);
      }
      const float ca = std::cos(angle), sa = std::sin(angle);
      dl->AddLine(ImVec2(c.x + ca * r * 0.4f, c.y + sa * r * 0.4f), ImVec2(c.x + ca * (r - 3.0f), c.y + sa * (r - 3.0f)),
                  Text(k.readOnly ? 0.4f : 0.9f), 1.75f);
      if (k.hovered && !k.readOnly)
         dl->AddCircle(c, arcR, Text(0.14f), 32, 3.0f);
   }
}

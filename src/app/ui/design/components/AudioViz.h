// AudioViz: the one frame every audio-effect visualizer draws inside (scope-background field, clipped
// contents, border). Contents are drawn by the caller between Begin and End; the frame never moves a
// sibling's edge, so every visualizer in an audio node body shares one rectangle, corner radius and
// border role. A visualizer that is also a control passes `hot` to End to raise the border.
#pragma once
#include <imgui.h>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"

namespace AudioViz
{
   struct Frame
   {
      ImDrawList* dl = nullptr;
      ImVec2 origin;
      ImVec2 br;
   };

   // Fills the field at the cursor (or at `origin`) and clips to it.
   inline Frame Begin(const ImVec2& origin, float w, float h)
   {
      Frame f;
      f.dl = ImGui::GetWindowDrawList();
      f.origin = origin;
      f.br = ImVec2(origin.x + w, origin.y + h);
      f.dl->AddRectFilled(f.origin, f.br, app::ScopeBgCol(), tok::radius_field);
      f.dl->PushClipRect(f.origin, f.br, true);
      return f;
   }
   inline Frame Begin(float w, float h) { return Begin(ImGui::GetCursorScreenPos(), w, h); }

   // Ends the clip and draws the border; `hotBorder` (0 = none) replaces the border colour while hovered.
   inline void End(const Frame& f, ImU32 hotBorder = 0)
   {
      f.dl->PopClipRect();
      f.dl->AddRect(f.origin, f.br, hotBorder ? hotBorder : app::ScopeBorderCol(), tok::radius_field);
   }

   // The at-rest marker for an analyzer with nothing to analyze: a dim caption centred in the frame, so a
   // flat trace reads as "waiting for input" and never as a broken visualizer. Call before End (inside the clip).
   inline void IdleLabel(const Frame& f, const char* text = "no input")
   {
      const ImVec2 ts = ImGui::CalcTextSize(text);
      f.dl->AddText(ImVec2(0.5f * (f.origin.x + f.br.x - ts.x), 0.5f * (f.origin.y + f.br.y - ts.y)),
                    app::ScopeTextCol(), text);
   }
}

// AudioViz: the one frame every audio-effect visualizer draws inside (scope-background field, clipped
// contents, border). Contents are drawn by the caller between Begin and End; the frame never moves a
// sibling's edge, so every visualizer in an audio node body shares one rectangle, corner radius and
// border role. A visualizer that is also a control passes `hot` to End to raise the border.
#pragma once
#include "app/ui/design/components/EmptyState.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include "app/AppShared.h"
#include "app/ui/design/Tokens.gen.h"
#include "audio/WavePeaks.h"

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

   // The two halves of the frame for visualizers that manage their own clip (viewers with several layers).
   inline void Fill(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx)
   {
      dl->AddRectFilled(mn, mx, app::ScopeBgCol(), tok::radius_field);
   }
   inline void Border(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 hotBorder = 0)
   {
      dl->AddRect(mn, mx, hotBorder ? hotBorder : app::ScopeBorderCol(), tok::radius_field);
   }

   // Ends the clip and draws the border; `hotBorder` (0 = none) replaces the border colour while hovered.
   inline void End(const Frame& f, ImU32 hotBorder = 0)
   {
      f.dl->PopClipRect();
      f.dl->AddRect(f.origin, f.br, hotBorder ? hotBorder : app::ScopeBorderCol(), tok::radius_field);
   }

   // A sample's envelope over the visible fraction [a, b) of it, one column per screen pixel: the
   // min..max outline dim, the RMS core bright, mirrored about the mid line at 0.45 of the height.
   // `lit` draws it in the on-accent colour (a pad that is playing).
   inline void Peaks(ImDrawList* dl, const WavePeaks& peaks, const ImVec2& origin, float w, float h,
                     double a = 0.0, double b = 1.0, bool lit = false)
   {
      if (peaks.Empty() || w < 1.0f || b <= a)
         return;
      const bool light = app::IsThemeLight();
      const ImU32 edge = lit ? tok::U32(tok::pal::c_FFFFFF8C)
                             : (light ? tok::U32(tok::pal::c_1E6EE66E) : tok::U32(tok::pal::c_96D6FF6E));
      const ImU32 core = lit ? tok::U32(tok::pal::c_FFFFFFE6)
                             : (light ? tok::U32(tok::pal::c_1E6EE6DC) : tok::U32(tok::pal::c_96D6FFDC));
      const float midY = origin.y + h * 0.5f;
      const float amp = h * 0.45f;
      const int cols = (int)std::ceil(w);
      const double perCol = (b - a) / (double)cols;
      for (int c = 0; c < cols; c++)
      {
         float mn, mx, rms;
         peaks.RangeFrac(a + perCol * c, a + perCol * (c + 1), mn, mx, rms);
         const float x = origin.x + (float)c;
         // Half a pixel either side of the mid line, so silence still reads as a line.
         dl->AddRectFilled(ImVec2(x, std::min(midY - mx * amp, midY - 0.5f)),
                           ImVec2(x + 1.0f, std::max(midY - mn * amp, midY + 0.5f)), edge);
         const float r = std::min(rms, std::max(mx, -mn));
         if (r * amp >= 0.5f)
            dl->AddRectFilled(ImVec2(x, midY - r * amp), ImVec2(x + 1.0f, midY + r * amp), core);
      }
   }

   // The min..max envelope alone in one colour, over the whole sample (a node with its own waveform tint).
   inline void Peaks(ImDrawList* dl, const WavePeaks& peaks, const ImVec2& origin, float w, float h, float scale,
                     ImU32 col)
   {
      if (peaks.Empty() || w < 1.0f)
         return;
      const float midY = origin.y + h * 0.5f;
      const float amp = h * scale;
      const int cols = (int)std::ceil(w);
      for (int c = 0; c < cols; c++)
      {
         float mn, mx, rms;
         peaks.RangeFrac((double)c / cols, (double)(c + 1) / cols, mn, mx, rms);
         dl->AddRectFilled(ImVec2(origin.x + (float)c, std::min(midY - mx * amp, midY - 0.5f)),
                           ImVec2(origin.x + (float)c + 1.0f, std::max(midY - mn * amp, midY + 0.5f)), col);
      }
   }

   // The at-rest marker for an analyzer with nothing to analyze: a dim caption centred in the frame, so a
   // flat trace reads as "waiting for input" and never as a broken visualizer. Call before End (inside the clip).
   inline void IdleLabel(const Frame& f, const char* text = "no input")
   {
      EmptyState::DrawCaption(f.origin, f.br, text);
   }
}

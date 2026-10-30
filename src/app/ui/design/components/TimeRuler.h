// TimeRuler: the one time ruler and playhead of the Timeline, the Piano Roll and the Sample Studio.
// A beat axis, bar numbers from 0 with the beats between them as zoom allows ("0", "0.3", "1"),
// the red playhead with its cap in the ruler, and the loop band. How a ruler is used is the same
// everywhere: click or drag places the playhead, Shift+drag carves the loop, loop edges drag.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/PillGroup.h"

namespace TimeRuler
{
   inline constexpr float kHeight = 16.0f;
   inline constexpr float kEdgeHit = 7.0f; // px either side of a loop edge that grabs it
   inline constexpr float kCap = 5.0f;     // half width of the playhead's cap
   inline constexpr double kMinLabelPx = 50.0;

   struct Axis
   {
      float x0 = 0.0f, x1 = 0.0f;
      double startBeat = 0.0, pxPerBeat = 1.0, beatsPerBar = 4.0;
      float X(double beat) const { return x0 + (float)((beat - startBeat) * pxPerBeat); }
      double Beat(float x) const { return startBeat + (double)(x - x0) / pxPerBeat; }
   };

   inline ImU32 TickColor() { return ImGui::GetColorU32(ImGuiCol_Text, 0.38f); }
   inline ImU32 LabelColor() { return ImGui::GetColorU32(ImGuiCol_Text, 0.62f); }

   inline void Background(ImDrawList* dl, const Axis& a, float y0)
   {
      dl->AddRectFilled(ImVec2(a.x0, y0), ImVec2(a.x1, y0 + kHeight), ImGui::GetColorU32(ImGuiCol_Text, 0.04f));
      dl->AddLine(ImVec2(a.x0, y0 + kHeight), ImVec2(a.x1, y0 + kHeight), ImGui::GetColorU32(ImGuiCol_Text, 0.08f));
   }

   // "4" on a bar, "4.3" on its third beat, "4.2.5" between beats.
   inline std::string BarLabel(double beat, double beatsPerBar)
   {
      const int bar = (int)std::floor(beat / beatsPerBar + 1e-9);
      const double inBar = beat - bar * beatsPerBar;
      char label[24];
      if (std::fabs(inBar) < 1e-6)
         std::snprintf(label, sizeof label, "%d", bar);
      else if (std::fabs(inBar - std::round(inBar)) < 1e-6)
         std::snprintf(label, sizeof label, "%d.%d", bar, (int)std::round(inBar) + 1);
      else
         std::snprintf(label, sizeof label, "%d.%.3g", bar, inBar + 1.0);
      return label;
   }

   // Ticks from `first` by `step` axis units of `beatsPerUnit` beats; `major` picks the tall ticks.
   template <class IsMajor, class Label>
   inline void Ticks(ImDrawList* dl, const Axis& a, float y0, double beatsPerUnit, double step, IsMajor major, Label label)
   {
      UiType::Scope type(UiType::Size::Caption, UiType::Weight::Medium);
      const float y1 = y0 + kHeight, fontY = y0 + (kHeight - ImGui::GetFontSize()) * 0.5f;
      const double stepPx = step * beatsPerUnit * a.pxPerBeat;
      const int subs = stepPx / 4.0 >= 8.0 ? 4 : stepPx / 2.0 >= 8.0 ? 2 : 1;
      const double first = std::max(0.0, std::floor(a.startBeat / beatsPerUnit / step) * step);
      const long long count = (long long)std::ceil((a.Beat(a.x1) / beatsPerUnit - first) / step) + 1;
      dl->PushClipRect(ImVec2(a.x0, y0), ImVec2(a.x1, y1), true);
      for (long long k = 0; k <= count; k++)
      {
         const double u = first + (double)k * step;
         const float x = a.X(u * beatsPerUnit);
         dl->AddLine(ImVec2(x, y1 - (major(u) ? 6.0f : 4.0f)), ImVec2(x, y1), TickColor());
         for (int m = 1; m < subs; m++)
         {
            const float mx = a.X((u + step * m / subs) * beatsPerUnit);
            dl->AddLine(ImVec2(mx, y1 - 2.0f), ImVec2(mx, y1), TickColor());
         }
         dl->AddText(ImVec2(x + 3.0f, fontY), LabelColor(), label(u, step).c_str());
      }
      dl->PopClipRect();
   }

   // Bars and beats: the widest step of whole bars or their halves that keeps labels legibly apart.
   inline void DrawBars(ImDrawList* dl, const Axis& a, float y0)
   {
      double step = a.beatsPerBar;
      while (step > 0.25 && step * 0.5 * a.pxPerBeat >= kMinLabelPx)
         step *= 0.5;
      while (step * a.pxPerBeat < kMinLabelPx && step < a.beatsPerBar * 4096.0)
         step *= 2.0;
      Ticks(dl, a, y0, 1.0, step,
            [&](double b) { return std::fabs(std::fmod(b, a.beatsPerBar)) < 1e-6; },
            [&](double b, double) { return BarLabel(b, a.beatsPerBar); });
   }

   // Any other unit (seconds, samples): a 1-2-5 step, labelled by `format(value, step)`.
   template <class Format>
   inline void DrawUnits(ImDrawList* dl, const Axis& a, float y0, double unitsPerBeat, double minStep, Format format)
   {
      const double want = std::max(minStep, kMinLabelPx * 1.4 * unitsPerBeat / a.pxPerBeat);
      double step = std::pow(10.0, std::floor(std::log10(std::max(1e-9, want))));
      for (double m : { 1.0, 2.0, 5.0, 10.0 })
         if (step * m >= want)
         {
            step *= m;
            break;
         }
      Ticks(dl, a, y0, 1.0 / unitsPerBeat, step, [](double) { return true; }, format);
   }

   // From the ruler's top down through the panel. A ghost shows where a drag will land.
   inline void Playhead(ImDrawList* dl, float x, float top, float bottom, bool ghost = false)
   {
      const ImU32 col = tok::U32(ghost ? tok::pal::c_EF444478 : tok::pal::c_EF4444FF);
      dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), col, ghost ? 1.5f : 1.25f);
      dl->AddTriangleFilled(ImVec2(x - kCap, top), ImVec2(x + kCap, top), ImVec2(x, top + kCap * 1.6f), col);
   }

   // The armed loop, as brackets on the ruler and a band down the panel. `drag`: 0 none, 1 start, 2 end, 3 move.
   inline void LoopBand(ImDrawList* dl, float bx0, float bx1, float top, float bottom, bool carving, int drag)
   {
      const ImU32 border = tok::U32(tok::pal::c_FACC15B4);
      dl->AddRectFilled(ImVec2(bx0, top), ImVec2(bx1, bottom), tok::U32(carving ? tok::pal::c_FACC152D : tok::pal::c_FACC151E));
      dl->AddRectFilled(ImVec2(bx0, top), ImVec2(bx1, top + 4.0f), tok::U32(drag == 3 ? tok::pal::c_FFEB3BFF : tok::pal::c_FACC15E6), 2.0f);
      const auto edge = [&](float x, float inward, bool held)
      {
         const ImU32 col = held ? tok::U32(tok::pal::c_FFFFFFFF) : border;
         dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), col, held ? 2.5f : 1.5f);
         dl->AddLine(ImVec2(x, top), ImVec2(x, top + 8.0f), col, 3.0f);
         dl->AddLine(ImVec2(x, top + 8.0f), ImVec2(x + inward, top + 8.0f), col, 2.0f);
      };
      edge(bx0, 5.0f, drag == 1);
      edge(bx1, -5.0f, drag == 2);
   }

   // The Bars | Time | Samples unit toggle the three toolbars share. Returns the picked index, or -1.
   inline int UnitPills(const char* id, const PillGroup::Segment* segs, int count, int selected)
   {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const UiLayout::Rect r{ at.x, at.y, count > 2 ? 186.0f : 104.0f, tok::tile }; // "Samples" needs the room
      const int picked = PillGroup::Draw(id, r, segs, count, selected);
      ImGui::SetCursorScreenPos(at);
      ImGui::Dummy(ImVec2(r.w, r.h));
      return picked;
   }
}

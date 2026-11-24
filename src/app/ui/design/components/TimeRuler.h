// TimeRuler: the one time ruler and playhead of the Timeline, the Piano Roll and the Sample Studio.
// A beat axis, bar numbers from 1 as in every DAW, with the beats between them as zoom allows ("1", "1.3", "2"),
// the red playhead with its cap in the ruler, and the loop band. How a ruler is used is the same
// everywhere: click or drag places the playhead, Shift+drag carves the loop, loop edges drag.
#pragma once
#include <algorithm>
#include "app/ui/design/UiFmt.h"
#include <cmath>
#include <cstdio>
#include <string>
#include "app/AppShared.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/PillGroup.h"

namespace TimeRuler
{
   inline constexpr float kHeight = tok::ruler_h;
   inline constexpr float kEdgeHit = tok::ruler_edge_hit; // px either side of a loop edge that grabs it
   inline constexpr float kCap = tok::ruler_cap;     // half width of the playhead's cap
   inline constexpr double kMinLabelPx = tok::ruler_min_label_px;

   struct Axis
   {
      float x0 = 0.0f, x1 = 0.0f;
      double startBeat = 0.0, pxPerBeat = 1.0, beatsPerBar = 4.0;
      float X(double beat) const { return x0 + (float)((beat - startBeat) * pxPerBeat); }
      double Beat(float x) const { return startBeat + (double)(x - x0) / pxPerBeat; }
   };

   inline ImU32 TickColor() { return ImGui::GetColorU32(ImGuiCol_Text, tok::time_ruler_tick_color_im_gui_col_text_alpha); }
   inline ImU32 LabelColor() { return ImGui::GetColorU32(ImGuiCol_Text, tok::time_ruler_label_color_im_gui_col_text_alpha); }

   inline void Background(ImDrawList* dl, const Axis& a, float y0)
   {
      dl->AddRectFilled(ImVec2(a.x0, y0), ImVec2(a.x1, y0 + kHeight), ImGui::GetColorU32(ImGuiCol_Text, tok::time_ruler_background_im_gui_col_text_alpha));
      dl->AddLine(ImVec2(a.x0, y0 + kHeight), ImVec2(a.x1, y0 + kHeight), ImGui::GetColorU32(ImGuiCol_Text, tok::time_ruler_color_alpha_mul));
   }

   // Bars count from 1: beat 0 is "1", "4.3" is bar 4's third beat, "4.2.5" between beats.
   inline std::string BarLabel(double beat, double beatsPerBar)
   {
      const int bar0 = (int)std::floor(beat / beatsPerBar + 1e-9);
      const double inBar = beat - bar0 * beatsPerBar;
      const int bar = bar0 + 1;
      char label[24];
      if (std::fabs(inBar) < 1e-6)
         std::snprintf(label, sizeof label, "%d", bar);
      else if (std::fabs(inBar - std::round(inBar)) < 1e-6)
         std::snprintf(label, sizeof label, "%d.%d", bar, (int)std::round(inBar) + 1);
      else
         std::snprintf(label, sizeof label, "%d.%s", bar, UiFmt::G3(inBar + 1.0).c);
      return label;
   }

   // Ticks from `first` by `step` axis units of `beatsPerUnit` beats; `major` picks the tall ticks.
   template <class IsMajor, class Label>
   inline void Ticks(ImDrawList* dl, const Axis& a, float y0, double beatsPerUnit, double step, IsMajor major, Label label)
   {
      UiType::Scope type(UiType::Size::Caption, UiType::Weight::Medium);
      const float y1 = y0 + kHeight, fontY = y0 + (kHeight - ImGui::GetFontSize()) * 0.5f;
      const double stepPx = step * beatsPerUnit * a.pxPerBeat;
      const int subs = stepPx / tok::time_ruler_subs_ratio >= 8.0 ? 4 : stepPx / tok::time_ruler_subs_scale >= 8.0 ? 2 : 1;
      const double first = std::max(0.0, std::floor(a.startBeat / beatsPerUnit / step) * step);
      const long long count = (long long)std::ceil((a.Beat(a.x1) / beatsPerUnit - first) / step) + 1;
      dl->PushClipRect(ImVec2(a.x0, y0), ImVec2(a.x1, y1), true);
      for (long long k = 0; k <= count; k++)
      {
         const double u = first + (double)k * step;
         const float x = a.X(u * beatsPerUnit);
         dl->AddLine(ImVec2(x, y1 - (major(u) ? tok::time_ruler_ticks_lg : tok::time_ruler_ticks_sm)), ImVec2(x, y1), TickColor());
         for (int m = 1; m < subs; m++)
         {
            const float mx = a.X((u + step * m / subs) * beatsPerUnit);
            dl->AddLine(ImVec2(mx, y1 - tok::space_0_5), ImVec2(mx, y1), TickColor());
         }
         dl->AddText(ImVec2(x + tok::space_0_75, fontY), LabelColor(), label(u, step).c_str());
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
      const double want = std::max(minStep, kMinLabelPx * tok::time_ruler_units_ratio * unitsPerBeat / a.pxPerBeat);
      double step = std::pow(10.0, std::floor(std::log10(std::max(1e-9, want))));
      for (double m : { 1.0, 2.0, 5.0, 10.0 })
         if (step * m >= want)
         {
            step *= m;
            break;
         }
      Ticks(dl, a, y0, 1.0 / unitsPerBeat, step, [](double) { return true; }, format);
   }

   // From the ruler's top down through the panel, only while it is inside the view [vx0, vx1].
   // A ghost shows where a drag will land.
   inline void Playhead(ImDrawList* dl, float x, float top, float bottom, float vx0, float vx1, bool ghost = false)
   {
      if (x < vx0 - 0.5f || x > vx1 + 0.5f)
         return;
      const ImU32 col = ghost ? tok::Role(tok::col::transport_playhead, tok::time_ruler_playhead_ghost_alpha) : tok::Role(tok::col::transport_playhead);
      dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), col, ghost ? tok::time_ruler_playhead_stroke : tok::time_ruler_line_stroke_xs);
      dl->PushClipRect(ImVec2(vx0, top), ImVec2(vx1, bottom), true); // the cap at bar 1 stays off the keys
      dl->AddTriangleFilled(ImVec2(x - kCap, top), ImVec2(x + kCap, top), ImVec2(x, top + kCap * tok::time_ruler_playhead_ratio), col);
      dl->PopClipRect();
   }

   // The armed loop, as brackets on the ruler and a band down the panel, clipped to the view [vx0, vx1]
   // so a loop that starts or ends off-screen never paints over the keys or the track headers.
   // Pass the true loop edges, not clamped ones: an edge off-screen is not drawn at the view's border.
   // `drag`: 0 none, 1 start, 2 end, 3 move.
   inline void LoopBand(ImDrawList* dl, float bx0, float bx1, float top, float bottom, float vx0, float vx1, bool carving, int drag)
   {
      if (bx1 < vx0 || bx0 > vx1 || vx1 <= vx0)
         return;
      dl->PushClipRect(ImVec2(vx0 - tok::time_ruler_loop_band_gap, top), ImVec2(vx1 + tok::time_ruler_loop_band_gap, bottom), true); // room for an edge sitting on the border
      const ImU32 border = tok::Role(tok::col::role_modulation, tok::time_ruler_loop_band_role_modulation_alpha);
      dl->AddRectFilled(ImVec2(bx0, top), ImVec2(bx1, bottom), tok::Role(tok::col::transport_loop, carving ? tok::time_ruler_loop_carving_alpha : tok::time_ruler_loop_band_alpha));
      dl->AddRectFilled(ImVec2(bx0, top), ImVec2(bx1, top + tok::space_1), drag == 3 ? tok::Role(tok::col::role_solo_pressed) : tok::Role(tok::col::transport_loop, tok::time_ruler_loop_edge_alpha), tok::radius_cell);  // ui-audit-ok: model value: drag mode 3 is a move
      const auto edge = [&](float x, float inward, bool held)
      {
         const ImU32 col = held ? tok::Role(tok::col::ink_white) : border;
         dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), col, held ? tok::time_ruler_loop_band_stroke : tok::time_ruler_line_stroke_sm);
         dl->AddLine(ImVec2(x, top), ImVec2(x, top + tok::space_2), col, tok::time_ruler_line_stroke_lg);
         dl->AddLine(ImVec2(x, top + tok::space_2), ImVec2(x + inward, top + tok::space_2), col, tok::time_ruler_line_stroke_md);
      };
      edge(bx0, 5.0f, drag == 1);
      edge(bx1, -5.0f, drag == 2);
      dl->PopClipRect();
   }

   // The Bars | Time | Samples unit toggle the three toolbars share. Returns the picked index, or -1.
   inline int UnitPills(const char* id, const PillGroup::Segment* segs, int count, int selected)
   {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const UiLayout::Rect r{ at.x, at.y, count > 2 ? tok::ruler_units_w : tok::ruler_units_w_narrow, tok::control_h }; // "Samples" needs the room
      const int picked = PillGroup::Draw(id, r, segs, count, selected);
      ImGui::SetCursorScreenPos(at);
      ImGui::Dummy(ImVec2(r.w, r.h));
      return picked;
   }
}

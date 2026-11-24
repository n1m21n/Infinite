// DeskParts: the hardware-desk pieces of the Mixer panel's console look (graphite brushed faces, a colour cap,
// raised keys with an LED bar, pots with a knurled skirt, screws), as drawing helpers any panel can share.
// Reference: SSL 4000 G. Tones are the desk_* roles: a graphite desk in dark mode, a warm light desk in light mode.
// LEDs, readouts and pots keep their colour in both.
// Drawing only unless noted; the caller owns the model and the undo step.
#pragma once
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "app/AppShared.h"

namespace DeskParts
{
   inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
   // Desk palette. Hi/Lo are bevel light and shadow (white and black at an opacity, right on either desk);
   // Ink is text and hairlines on the desk face (light on the dark desk, dark on the light one).
   inline ImU32 Role(const tok::Pair8& p) { return tok::U32(p, app::IsThemeLight()); }
   inline ImU32 Role(const tok::Pair8& p, int a) { return tok::U32(p, app::IsThemeLight(), a); }
   inline ImU32 Hi(int a) { return tok::U32(tok::desk_light, a); }
   inline ImU32 Lo(int a) { return tok::U32(tok::desk_shadow, a); }
   inline ImU32 Ink(int a = tok::desk_a) { return DeskParts::Role(tok::desk_legend, a); }
   inline ImU32 Tint(ImU32 c, float k, int a = tok::desk_a) // k < 1 darkens, k > 1 lightens
   {
      const float r = std::min(tok::desk_max, (float)(c & 0xFF) * k), g = std::min(255.0f, (float)((c >> 8) & 0xFF) * k),
                  b = std::min(255.0f, (float)((c >> 16) & 0xFF) * k);
      return IM_COL32((int)r, (int)g, (int)b, a);
   }

   inline void Screw(ImDrawList* dl, ImVec2 c)
   {
      dl->AddCircleFilled(ImVec2(c.x, c.y + tok::desk_screw_gap), tok::desk_screw_r, Lo(tok::desk_screw_lo_alpha), tok::desk_screw_segments);
      dl->AddCircleFilled(c, tok::desk_dot_r_lg, tok::U32(tok::desk_screw), tok::desk_screw_segments);
      dl->AddCircleFilled(ImVec2(c.x - tok::px_align, c.y - tok::px_align), tok::desk_dot_r_sm, Tint(tok::U32(tok::desk_screw), tok::desk_screw_tint_shade), tok::desk_dot_segments_sm);
      dl->AddLine(ImVec2(c.x - tok::desk_line_x, c.y + tok::space_0_25), ImVec2(c.x + tok::desk_line_x, c.y - tok::space_0_25), Tint(tok::U32(tok::desk_screw), tok::desk_line_alpha), tok::desk_screw_stroke);
   }

   // The console chassis behind the faces: a dark steel bed with a lit top rail.
   inline void Chassis(ImDrawList* dl, ImVec2 p, ImVec2 q)
   {
      dl->AddRectFilled(p, q, DeskParts::Role(tok::desk_chassis), tok::desk_chassis_radius);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + tok::desk_chassis_gap), Hi(tok::desk_chassis_hi_alpha), Hi(tok::desk_chassis_hi_alpha), Hi(0), Hi(0));
      dl->AddRect(p, q, Lo(tok::desk_chassis_lo_alpha), tok::desk_chassis_radius, 0, tok::desk_chassis_stroke);
   }

   // A face: graphite plate, fine vertical brushing (a fixed hash, so nothing swims), a bevelled edge and a colour
   // cap on top (6 px). `lit` outlines it in the check colour.
   inline void Face(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit)
   {
      const float r = tok::desk_r;
      dl->AddRectFilledMultiColor(p, q, DeskParts::Role(tok::desk_strip), DeskParts::Role(tok::desk_strip), DeskParts::Role(tok::desk_strip_edge), DeskParts::Role(tok::desk_strip_edge));
      dl->PushClipRect(p, q, true);
      for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
      {
         const unsigned hsh = (unsigned)((int)(x * tok::desk_face_ratio) * tok::desk_hsh_scale) >> 24;
         dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Hi(tok::desk_face_hi_offset + (int)(hsh % 7)) : Lo(tok::desk_face_lo_offset + (int)(hsh % 9)), tok::desk_face_stroke);  // ui-audit-ok: hash bits pick the grain shade
      }
      dl->PopClipRect();
      dl->AddRect(ImVec2(p.x + tok::space_0_25, p.y + tok::space_0_25), ImVec2(q.x - tok::space_0_25, q.y - tok::space_0_25), Hi(tok::desk_face_hi_alpha), r, 0, tok::desk_face_stroke);
      dl->AddRect(p, q, Lo(tok::desk_face_lo_alpha), r, 0, tok::desk_face_stroke);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + tok::space_1_5), cap, r, ImDrawFlags_RoundCornersTop);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + tok::space_1_5), Hi(tok::desk_hi_alpha_lg), Hi(tok::desk_hi_alpha_lg), Lo(tok::desk_face_lo_alpha_sm), Lo(tok::desk_face_lo_alpha_sm));
      dl->AddLine(ImVec2(p.x + tok::space_0_25, p.y + tok::desk_face_gap), ImVec2(q.x - tok::space_0_25, p.y + tok::desk_face_gap), Lo(tok::desk_face_lo_alpha_md), tok::desk_face_stroke);
      if (lit)
         dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), r, 0, tok::desk_outline_stroke_lg);
   }

   // A key cap, drawn only: a raised cap with an LED bar above the legend. `ledLit` 0..1 is how bright the bar is.
   inline void KeyCap(ImDrawList* dl, ImVec2 p, ImVec2 q, bool hovered, bool down, float ledLit, ImU32 led, const char* legend = nullptr,
                      bool small = false, float lamp = 0.0f, ImU32 cap = 0)
   {
      const float ledH = small ? tok::desk_led_h : tok::desk_key_cap_led_h;
      dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(q.x, q.y + 1), Lo(tok::desk_key_cap_lo_alpha), tok::desk_key_cap_radius);
      const float dip = down ? 1.0f : 0.0f;
      const ImVec2 a(p.x, p.y + dip), b(q.x, q.y - 1.0f + dip);
      // cap != 0: a coloured key plastic (TR step keys), lit from above: lighter top, darker foot, a bevel line under the face.
      const ImU32 capHi = cap != 0 ? Tint(cap, tok::desk_key_cap_tint_shade) : DeskParts::Role(tok::desk_cap_hi);
      const ImU32 capLo = cap != 0 ? Tint(cap, tok::desk_cap_lo_alpha) : DeskParts::Role(tok::desk_cap);
      const ImU32 top = hovered ? Tint(capHi, tok::desk_cap_hi_alpha) : capHi;
      dl->AddRectFilledMultiColor(a, b, top, top, capLo, capLo);
      if (cap != 0)
         dl->AddLine(ImVec2(a.x + tok::space_0_5, b.y - tok::desk_key_cap_gap), ImVec2(b.x - tok::space_0_5, b.y - tok::desk_key_cap_gap), Lo(tok::desk_key_cap_lo_alpha_sm), tok::desk_key_cap_stroke);
      dl->AddRect(a, b, Lo(tok::desk_key_cap_lo_alpha_lg), tok::desk_key_cap_radius, 0, tok::desk_outline_stroke_sm);
      dl->AddLine(ImVec2(a.x + tok::space_0_5, a.y + tok::desk_line_y), ImVec2(b.x - tok::space_0_5, a.y + tok::desk_line_y), Hi(tok::desk_key_cap_hi_alpha), tok::desk_outline_stroke_sm);
      if (lamp > 0.0f) // the whole cap lights in the key colour, brightness = lamp (0..1)
      {
         lamp = std::clamp(lamp, 0.0f, 1.0f);
         const int al = (int)(120.0f + 135.0f * lamp);
         const ImU32 t = Tint(led, tok::desk_key_cap_tint_offset + tok::desk_key_cap_tint_scale * lamp, al), bt = Tint(led, tok::desk_led_alpha + 0.5f * lamp, al);
         dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), t, t, bt, bt);
         dl->AddRect(ImVec2(a.x + tok::space_0_25, a.y + tok::space_0_25), ImVec2(b.x - tok::space_0_25, b.y - tok::space_0_25), Hi((int)(tok::desk_key_cap_ratio * lamp)), tok::desk_key_cap_radius, 0, tok::desk_outline_stroke_sm);
      }
      const ImVec2 la(a.x + tok::space_1, a.y + tok::space_0_75), lb(b.x - tok::space_1, a.y + tok::space_0_75 + ledH);
      ledLit = std::clamp(ledLit, 0.0f, 1.0f);
      dl->AddRectFilled(la, lb, ledLit > 0.0f ? Tint(led, tok::desk_fill_alpha_sm + tok::desk_fill_alpha_lg * ledLit) : Tint(led, tok::desk_fill_alpha_sm), 1.0f);
      if (ledLit > 0.05f)
         dl->AddRectFilled(ImVec2(la.x - 1, la.y - 1), ImVec2(lb.x + 1, lb.y + 1), (led & 0x00FFFFFF) | ((ImU32)(tok::desk_key_cap_scale * ledLit) << 24), tok::radius_cell);
      if (legend != nullptr)
      {
         const float fs = ImGui::GetFontSize() * tok::desk_legend_em;
         const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, legend);
         dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + ((b.x - a.x) - ts.x) * 0.5f, a.y + tok::space_1_5 + (b.y - a.y - tok::space_1_5 - ts.y) * 0.5f),
                     ledLit > 0.0f ? Ink() : Ink(tok::desk_key_cap_ink_alpha), legend);
      }
   }

   // A desk key with a legend. Returns true on click.
   inline bool Key(const char* id, const char* legend, bool on, ImU32 led, ImVec2 size)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(id, size);
      KeyCap(ImGui::GetWindowDrawList(), p, ImVec2(p.x + size.x, p.y + size.y), ImGui::IsItemHovered(), ImGui::IsItemActive(), on ? 1.0f : 0.0f, led,
             legend);
      return clicked;
   }

   constexpr float kPotD = tok::desk_pot_d;
   constexpr float kPotRowH = kPotD + tok::space_3;
   constexpr float kPlateH = tok::desk_plate_h;

   // The same face turned sideways: the colour cap runs down the left edge (a horizontal channel strip).
   inline void FaceSide(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit)
   {
      const float r = tok::desk_r;
      dl->AddRectFilledMultiColor(p, q, DeskParts::Role(tok::desk_strip), DeskParts::Role(tok::desk_strip), DeskParts::Role(tok::desk_strip_edge), DeskParts::Role(tok::desk_strip_edge));
      dl->PushClipRect(p, q, true);
      for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
      {
         const unsigned hsh = (unsigned)((int)(x * tok::desk_face_side_ratio) * tok::desk_hsh_scale) >> 24;
         dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Hi(tok::desk_face_side_hi_offset + (int)(hsh % 7)) : Lo(tok::desk_face_side_lo_offset + (int)(hsh % 9)), tok::desk_face_side_stroke);  // ui-audit-ok: hash bits pick the grain shade
      }
      dl->PopClipRect();
      dl->AddRect(ImVec2(p.x + tok::space_0_25, p.y + tok::space_0_25), ImVec2(q.x - tok::space_0_25, q.y - tok::space_0_25), Hi(tok::desk_face_side_hi_alpha), r, 0, tok::desk_face_side_stroke);
      dl->AddRect(p, q, Lo(tok::desk_face_side_lo_alpha), r, 0, tok::desk_face_side_stroke);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + tok::space_1_5, q.y - 1), cap, r, ImDrawFlags_RoundCornersLeft);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + tok::space_1_5, q.y - 1), Hi(tok::desk_hi_alpha_lg), Lo(tok::desk_face_side_lo_alpha_sm), Lo(tok::desk_face_side_lo_alpha_sm), Hi(tok::desk_hi_alpha_lg));
      dl->AddLine(ImVec2(p.x + tok::desk_face_side_gap, p.y + tok::space_0_25), ImVec2(p.x + tok::desk_face_side_gap, q.y - tok::space_0_25), Lo(tok::desk_face_side_lo_alpha_md), tok::desk_face_side_stroke);
      if (lit)
         dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), r, 0, tok::desk_outline_stroke_lg);
   }

   // A rotary pot: knurled skirt, domed cap, a pointer, and a value arc in `col`. Drag up or down, Shift is fine.
   // `tag` is the caption under it. Returns true while the value changes; the caller owns the undo step.
   inline bool Pot(const char* id, float* v, float lo, float hi, bool bipolar, float cellW, ImU32 col, const char* tag)
   {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton(id, ImVec2(cellW, kPotRowH));
      const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
      bool changed = false;
      if (active && ImGui::GetIO().MouseDelta.y != 0.0f)
      {
         const float speed = ImGui::GetIO().KeyShift ? 0.002f : 0.007f;
         const float nv = std::clamp(*v - ImGui::GetIO().MouseDelta.y * speed * (hi - lo), lo, hi);
         changed = nv != *v;
         *v = nv;
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float R = kPotD * 0.5f;
      const ImVec2 c(p.x + cellW * 0.5f, p.y + R + 1.0f);
      const float a0 = tok::desk_a0_ratio * IM_PI, a1 = tok::desk_a1_ratio * IM_PI;
      const float t = hi > lo ? (*v - lo) / (hi - lo) : 0.0f;
      const float ang = Lerp(a0, a1, t);
      const float from = bipolar ? Lerp(a0, a1, 0.5f) : a0;  // ui-audit-ok: model value: a bipolar knob fills from its centre value
      dl->PathArcTo(c, R + tok::desk_pot_gap, a0, a1, tok::desk_pot_segments);
      dl->PathStroke(Lo(tok::desk_pot_lo_alpha), 0, tok::desk_pot_stroke);
      if (std::fabs(ang - from) > 0.01f)
      {
         dl->PathArcTo(c, R + tok::desk_pot_gap, std::min(from, ang), std::max(from, ang), tok::desk_pot_segments);
         dl->PathStroke(col, 0, tok::desk_pot_stroke);
      }
      dl->AddCircleFilled(ImVec2(c.x, c.y + tok::desk_dot_y), R, Lo(tok::desk_pot_lo_alpha_sm), tok::desk_dot_segments_lg);
      dl->AddCircleFilled(c, R, DeskParts::Role(tok::desk_knurl), tok::desk_dot_segments_lg);
      for (int i = 0; i < 24; i++)
      {
         const float a = (float)i / tok::desk_a_ratio * tok::desk_pot_scale * IM_PI;
         dl->AddLine(ImVec2(c.x + cosf(a) * (R - tok::desk_pot_inset_lg), c.y + sinf(a) * (R - tok::desk_pot_inset_lg)), ImVec2(c.x + cosf(a) * (R - tok::desk_pot_inset_sm), c.y + sinf(a) * (R - tok::desk_pot_inset_sm)),
                     Hi(hovered || active ? tok::desk_pot_hi_alpha : tok::desk_hi_alpha_md), tok::desk_line_stroke_sm);
      }
      const float cr = R - 4.0f;
      const ImU32 pot = tok::U32(tok::desk_pot);
      dl->AddCircleFilled(c, cr, pot, tok::desk_pot_segments);
      dl->AddCircleFilled(ImVec2(c.x - 1.0f, c.y - tok::desk_dot_y), cr - tok::desk_dot_y, Tint(pot, tok::desk_pot_tint_shade), tok::desk_pot_segments);
      dl->AddCircleFilled(ImVec2(c.x - tok::desk_dot_y, c.y - tok::desk_pot_gap), cr - tok::space_1, Tint(pot, tok::desk_dot_alpha), tok::desk_dot_segments_md);
      dl->AddCircle(c, cr, Lo(tok::desk_pot_lo_alpha_lg), tok::desk_pot_segments, 1.0f);
      dl->AddLine(ImVec2(c.x + cosf(ang) * tok::desk_pot_ratio, c.y + sinf(ang) * tok::desk_pot_ratio), ImVec2(c.x + cosf(ang) * (cr - tok::desk_pot_inset_sm), c.y + sinf(ang) * (cr - tok::desk_pot_inset_sm)), col, tok::desk_line_stroke_lg);
      const float fs = ImGui::GetFontSize() * tok::desk_tag_em;
      const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag).x;
      dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (cellW - tw) * 0.5f, p.y + kPotD + tok::space_0_75), Ink(tok::desk_pot_ink_alpha), tag);
      return changed;
   }

   // The readout plate: dark glass with the value in console green.
   inline void Plate(ImDrawList* dl, ImVec2 p, float w, const char* text, ImU32 ink = tok::U32(tok::desk_readout))
   {
      const ImVec2 q(p.x + w, p.y + kPlateH);
      dl->AddRectFilled(p, q, DeskParts::Role(tok::desk_well), tok::radius_cell);
      dl->AddRect(p, q, Hi(tok::desk_plate_hi_alpha), tok::radius_cell);
      const float fs = ImGui::GetFontSize() * tok::desk_plate_em;
      const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
      dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (kPlateH - ts.y) * 0.5f), ink, text);
   }

   // The scribble inset, horizontal: dark glass, the number in the strip colour, then the name. Drawing only.
   inline void ScribbleRow(ImDrawList* dl, ImVec2 p, ImVec2 q, const char* name, int number, ImU32 col, bool dim)
   {
      dl->AddRectFilled(p, q, DeskParts::Role(tok::desk_scribble_bg), tok::desk_scribble_row_radius);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + tok::space_1_25, q.y - 1), Tint(col, tok::desk_scribble_row_tint_shade, tok::desk_scribble_row_tint_alpha), tok::desk_fill_radius, ImDrawFlags_RoundCornersLeft);
      dl->AddRect(p, q, Ink(tok::desk_scribble_row_ink_alpha), tok::desk_scribble_row_radius);
      char num[16];
      snprintf(num, sizeof(num), "%d", number);
      const float nfs = ImGui::GetFontSize() * tok::desk_number_em;
      const float cy = (p.y + q.y) * 0.5f;
      dl->AddText(ImGui::GetFont(), nfs, ImVec2(p.x + tok::desk_scribble_row_gap, cy - nfs * 0.5f), Tint(col, app::IsThemeLight() ? tok::desk_text_alpha_sm : tok::desk_text_alpha_lg), num);
      dl->PushClipRect(ImVec2(p.x + tok::space_0_75, p.y), ImVec2(q.x - tok::space_0_75, q.y), true);
      dl->AddText(ImVec2(p.x + tok::desk_text_x, cy - ImGui::GetTextLineHeight() * 0.5f), DeskParts::Role(tok::desk_scribble_text, dim ? tok::desk_scribble_row_scribble_text_alpha : tok::desk_text_alpha), name);
      dl->PopClipRect();
   }

   // The Mixer's segmented LED meter (green / amber / red segments, held peak, dB ticks). level is linear 0..1;
   // call between PushID/PopID so each meter keeps its own peak. ticks: dB marks left of the meter (the Mixer).
   inline void ConsoleMeter(float x, float y, float w, float h, float level, bool ticks = false)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 tl(x, y), br(x + w, y + h);
      dl->AddRectFilled(tl, br, DeskParts::Role(tok::desk_meter_off), tok::radius_cell);
      const float db = level > 1e-4f ? 20.0f * log10f(level) : -120.0f;
      auto frac = [](float d) { return std::clamp((d + 60.0f) / 60.0f, 0.0f, 1.0f); };
      const float seg = tok::desk_seg, gap = 1.0f, step = seg + gap;
      const int n = std::max(1, (int)((h - tok::space_0_5) / step));
      const float lit = frac(db) * (float)n;
      ImGuiStorage* st = ImGui::GetStateStorage();
      const ImGuiID id = ImGui::GetID("##mpk");
      float peak = st->GetFloat(id, 0.0f);
      const float dt = ImGui::GetIO().DeltaTime;
      peak = std::max(lit, peak - tok::motion_rate_peak_fall * dt * (float)n / 30.0f);
      st->SetFloat(id, peak);
      for (int i = 0; i < n; i++)
      {
         const float topY = br.y - 1.0f - (float)(i + 1) * step + gap;
         const float segDb = -60.0f + 60.0f * ((float)i + 0.5f) / (float)n;
         const bool on = (float)i + 0.5f <= lit;
         const ImU32 base = tok::U32(segDb > -tok::desk_seg_db_shade_sm ? tok::desk_led_red : segDb > -tok::desk_seg_db_shade_lg ? tok::desk_led_amber : tok::desk_led_green);
         dl->AddRectFilled(ImVec2(x + tok::desk_console_meter_gap, topY), ImVec2(br.x - tok::desk_console_meter_gap, topY + seg), on ? base : (base & 0x00FFFFFF) | (tok::desk_console_meter << 24), tok::desk_console_meter_radius);
      }
      const int pi = std::clamp((int)peak, 0, n - 1);
      if (peak > 1.0f)
      {
         const float topY = br.y - 1.0f - (float)(pi + 1) * step + gap;
         dl->AddRectFilled(ImVec2(x + tok::desk_console_meter_gap, topY), ImVec2(br.x - tok::desk_console_meter_gap, topY + seg), Hi(tok::desk_console_meter_hi_alpha), tok::desk_console_meter_radius);
      }
      if (ticks)
      {
         static const float kTicks[] = { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
         for (float t : kTicks)
            dl->AddLine(ImVec2(x - tok::space_0_75, br.y - tok::space_0_25 - (h - tok::space_0_5) * frac(t)), ImVec2(x - tok::space_0_25, br.y - tok::space_0_25 - (h - tok::space_0_5) * frac(t)),
                        Ink(tok::desk_console_meter_ink_alpha), tok::desk_console_meter_stroke);
      }
      dl->AddRect(tl, br, Hi(tok::desk_hi_alpha_sm), tok::radius_cell);
   }
}

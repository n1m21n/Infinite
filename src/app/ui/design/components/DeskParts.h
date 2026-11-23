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
   inline ImU32 Ink(int a = 255) { return Role(tok::desk_legend, a); }
   inline ImU32 Tint(ImU32 c, float k, int a = 255) // k < 1 darkens, k > 1 lightens
   {
      const float r = std::min(255.0f, (float)(c & 0xFF) * k), g = std::min(255.0f, (float)((c >> 8) & 0xFF) * k),
                  b = std::min(255.0f, (float)((c >> 16) & 0xFF) * k);
      return IM_COL32((int)r, (int)g, (int)b, a);
   }

   inline void Screw(ImDrawList* dl, ImVec2 c)
   {
      dl->AddCircleFilled(ImVec2(c.x, c.y + 0.6f), 3.0f, Lo(120), 10);
      dl->AddCircleFilled(c, 2.6f, tok::U32(tok::desk_screw), 10);
      dl->AddCircleFilled(ImVec2(c.x - 0.5f, c.y - 0.5f), 1.6f, Tint(tok::U32(tok::desk_screw), 1.5f), 8);
      dl->AddLine(ImVec2(c.x - 1.6f, c.y + 1.0f), ImVec2(c.x + 1.6f, c.y - 1.0f), Tint(tok::U32(tok::desk_screw), 0.33f), 1.0f);
   }

   // The console chassis behind the faces: a dark steel bed with a lit top rail.
   inline void Chassis(ImDrawList* dl, ImVec2 p, ImVec2 q)
   {
      dl->AddRectFilled(p, q, Role(tok::desk_chassis), 5.0f);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 9), Hi(22), Hi(22), Hi(0), Hi(0));
      dl->AddRect(p, q, Lo(200), 5.0f, 0, 1.0f);
   }

   // A face: graphite plate, fine vertical brushing (a fixed hash, so nothing swims), a bevelled edge and a colour
   // cap on top (6 px). `lit` outlines it in the check colour.
   inline void Face(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit)
   {
      const float r = 3.0f;
      dl->AddRectFilledMultiColor(p, q, Role(tok::desk_strip), Role(tok::desk_strip), Role(tok::desk_strip_edge), Role(tok::desk_strip_edge));
      dl->PushClipRect(p, q, true);
      for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
      {
         const unsigned hsh = (unsigned)((int)(x * 7.0f) * 2654435761u) >> 24;
         dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Hi(5 + (int)(hsh % 7)) : Lo(6 + (int)(hsh % 9)), 1.0f);
      }
      dl->PopClipRect();
      dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, q.y - 1), Hi(28), r, 0, 1.0f);
      dl->AddRect(p, q, Lo(220), r, 0, 1.0f);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 6), cap, r, ImDrawFlags_RoundCornersTop);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 6), Hi(70), Hi(70), Lo(50), Lo(50));
      dl->AddLine(ImVec2(p.x + 1, p.y + 6.5f), ImVec2(q.x - 1, p.y + 6.5f), Lo(160), 1.0f);
      if (lit)
         dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), r, 0, 1.5f);
   }

   // A key cap, drawn only: a raised cap with an LED bar above the legend. `ledLit` 0..1 is how bright the bar is.
   inline void KeyCap(ImDrawList* dl, ImVec2 p, ImVec2 q, bool hovered, bool down, float ledLit, ImU32 led, const char* legend = nullptr,
                      bool small = false, float lamp = 0.0f, ImU32 cap = 0)
   {
      const float ledH = small ? 2.0f : 2.5f;
      dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(q.x, q.y + 1), Lo(150), 3.0f);
      const float dip = down ? 1.0f : 0.0f;
      const ImVec2 a(p.x, p.y + dip), b(q.x, q.y - 1.0f + dip);
      // cap != 0: a coloured key plastic (TR step keys), lit from above: lighter top, darker foot, a bevel line under the face.
      const ImU32 capHi = cap != 0 ? Tint(cap, 1.08f) : Role(tok::desk_cap_hi);
      const ImU32 capLo = cap != 0 ? Tint(cap, 0.72f) : Role(tok::desk_cap);
      const ImU32 top = hovered ? Tint(capHi, 1.14f) : capHi;
      dl->AddRectFilledMultiColor(a, b, top, top, capLo, capLo);
      if (cap != 0)
         dl->AddLine(ImVec2(a.x + 2, b.y - 2.5f), ImVec2(b.x - 2, b.y - 2.5f), Lo(70), 1.5f);
      dl->AddRect(a, b, Lo(220), 3.0f, 0, 1.0f);
      dl->AddLine(ImVec2(a.x + 2, a.y + 1.5f), ImVec2(b.x - 2, a.y + 1.5f), Hi(60), 1.0f);
      if (lamp > 0.0f) // the whole cap lights in the key colour, brightness = lamp (0..1)
      {
         lamp = std::clamp(lamp, 0.0f, 1.0f);
         const int al = (int)(120.0f + 135.0f * lamp);
         const ImU32 t = Tint(led, 0.55f + 0.75f * lamp, al), bt = Tint(led, 0.35f + 0.5f * lamp, al);
         dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), t, t, bt, bt);
         dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), Hi((int)(70.0f * lamp)), 3.0f, 0, 1.0f);
      }
      const ImVec2 la(a.x + 4.0f, a.y + 3.0f), lb(b.x - 4.0f, a.y + 3.0f + ledH);
      ledLit = std::clamp(ledLit, 0.0f, 1.0f);
      dl->AddRectFilled(la, lb, ledLit > 0.0f ? Tint(led, 0.22f + 0.78f * ledLit) : Tint(led, 0.22f), 1.0f);
      if (ledLit > 0.05f)
         dl->AddRectFilled(ImVec2(la.x - 1, la.y - 1), ImVec2(lb.x + 1, lb.y + 1), (led & 0x00FFFFFF) | ((ImU32)(60.0f * ledLit) << 24), tok::radius_cell);
      if (legend != nullptr)
      {
         const float fs = ImGui::GetFontSize() * tok::desk_legend_em;
         const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, legend);
         dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + ((b.x - a.x) - ts.x) * 0.5f, a.y + 6.0f + (b.y - a.y - 6.0f - ts.y) * 0.5f),
                     ledLit > 0.0f ? Ink() : Ink(190), legend);
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

   constexpr float kPotD = 30.0f;
   constexpr float kPotRowH = kPotD + 12.0f;
   constexpr float kPlateH = 16.0f;

   // The same face turned sideways: the colour cap runs down the left edge (a horizontal channel strip).
   inline void FaceSide(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit)
   {
      const float r = 3.0f;
      dl->AddRectFilledMultiColor(p, q, Role(tok::desk_strip), Role(tok::desk_strip), Role(tok::desk_strip_edge), Role(tok::desk_strip_edge));
      dl->PushClipRect(p, q, true);
      for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
      {
         const unsigned hsh = (unsigned)((int)(x * 7.0f) * 2654435761u) >> 24;
         dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Hi(5 + (int)(hsh % 7)) : Lo(6 + (int)(hsh % 9)), 1.0f);
      }
      dl->PopClipRect();
      dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, q.y - 1), Hi(28), r, 0, 1.0f);
      dl->AddRect(p, q, Lo(220), r, 0, 1.0f);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + 6, q.y - 1), cap, r, ImDrawFlags_RoundCornersLeft);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + 6, q.y - 1), Hi(70), Lo(50), Lo(50), Hi(70));
      dl->AddLine(ImVec2(p.x + 6.5f, p.y + 1), ImVec2(p.x + 6.5f, q.y - 1), Lo(160), 1.0f);
      if (lit)
         dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), r, 0, 1.5f);
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
      const float a0 = 0.75f * IM_PI, a1 = 2.25f * IM_PI;
      const float t = hi > lo ? (*v - lo) / (hi - lo) : 0.0f;
      const float ang = Lerp(a0, a1, t);
      const float from = bipolar ? Lerp(a0, a1, 0.5f) : a0;
      dl->PathArcTo(c, R + 2.5f, a0, a1, 24);
      dl->PathStroke(Lo(150), 0, 2.5f);
      if (std::fabs(ang - from) > 0.01f)
      {
         dl->PathArcTo(c, R + 2.5f, std::min(from, ang), std::max(from, ang), 24);
         dl->PathStroke(col, 0, 2.5f);
      }
      dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f), R, Lo(130), 28);
      dl->AddCircleFilled(c, R, Role(tok::desk_knurl), 28);
      for (int i = 0; i < 24; i++)
      {
         const float a = (float)i / 24.0f * 2.0f * IM_PI;
         dl->AddLine(ImVec2(c.x + cosf(a) * (R - 3.0f), c.y + sinf(a) * (R - 3.0f)), ImVec2(c.x + cosf(a) * (R - 0.5f), c.y + sinf(a) * (R - 0.5f)),
                     Hi(hovered || active ? 70 : 46), 1.0f);
      }
      const float cr = R - 4.0f;
      const ImU32 pot = tok::U32(tok::desk_pot);
      dl->AddCircleFilled(c, cr, pot, 24);
      dl->AddCircleFilled(ImVec2(c.x - 1.0f, c.y - 1.5f), cr - 1.5f, Tint(pot, 1.4f), 24);
      dl->AddCircleFilled(ImVec2(c.x - 1.5f, c.y - 2.5f), cr - 4.0f, Tint(pot, 1.6f), 20);
      dl->AddCircle(c, cr, Lo(170), 24, 1.0f);
      dl->AddLine(ImVec2(c.x + cosf(ang) * 2.0f, c.y + sinf(ang) * 2.0f), ImVec2(c.x + cosf(ang) * (cr - 0.5f), c.y + sinf(ang) * (cr - 0.5f)), col, 2.2f);
      const float fs = ImGui::GetFontSize() * tok::desk_tag_em;
      const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag).x;
      dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (cellW - tw) * 0.5f, p.y + kPotD + 3.0f), Ink(140), tag);
      return changed;
   }

   // The readout plate: dark glass with the value in console green.
   inline void Plate(ImDrawList* dl, ImVec2 p, float w, const char* text, ImU32 ink = tok::U32(tok::desk_readout))
   {
      const ImVec2 q(p.x + w, p.y + kPlateH);
      dl->AddRectFilled(p, q, Role(tok::desk_well), tok::radius_cell);
      dl->AddRect(p, q, Hi(30), tok::radius_cell);
      const float fs = ImGui::GetFontSize() * tok::desk_plate_em;
      const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
      dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (kPlateH - ts.y) * 0.5f), ink, text);
   }

   // The scribble inset, horizontal: dark glass, the number in the strip colour, then the name. Drawing only.
   inline void ScribbleRow(ImDrawList* dl, ImVec2 p, ImVec2 q, const char* name, int number, ImU32 col, bool dim)
   {
      dl->AddRectFilled(p, q, Role(tok::desk_scribble_bg), 3.0f);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + 5, q.y - 1), Tint(col, 0.95f, 255), 2.0f, ImDrawFlags_RoundCornersLeft);
      dl->AddRect(p, q, Ink(34), 3.0f);
      char num[16];
      snprintf(num, sizeof(num), "%d", number);
      const float nfs = ImGui::GetFontSize() * tok::desk_number_em;
      const float cy = (p.y + q.y) * 0.5f;
      dl->AddText(ImGui::GetFont(), nfs, ImVec2(p.x + 9.0f, cy - nfs * 0.5f), Tint(col, app::IsThemeLight() ? 0.7f : 1.25f), num);
      dl->PushClipRect(ImVec2(p.x + 3.0f, p.y), ImVec2(q.x - 3.0f, q.y), true);
      dl->AddText(ImVec2(p.x + 21.0f, cy - ImGui::GetTextLineHeight() * 0.5f), Role(tok::desk_scribble_text, dim ? 110 : 255), name);
      dl->PopClipRect();
   }

   // The Mixer's segmented LED meter (green / amber / red segments, held peak, dB ticks). level is linear 0..1;
   // call between PushID/PopID so each meter keeps its own peak. ticks: dB marks left of the meter (the Mixer).
   inline void ConsoleMeter(float x, float y, float w, float h, float level, bool ticks = false)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 tl(x, y), br(x + w, y + h);
      dl->AddRectFilled(tl, br, Role(tok::desk_meter_off), tok::radius_cell);
      const float db = level > 1e-4f ? 20.0f * log10f(level) : -120.0f;
      auto frac = [](float d) { return std::clamp((d + 60.0f) / 60.0f, 0.0f, 1.0f); };
      const float seg = 3.0f, gap = 1.0f, step = seg + gap;
      const int n = std::max(1, (int)((h - 2.0f) / step));
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
         const ImU32 base = tok::U32(segDb > -3.0f ? tok::desk_led_red : segDb > -12.0f ? tok::desk_led_amber : tok::desk_led_green);
         dl->AddRectFilled(ImVec2(x + 1.5f, topY), ImVec2(br.x - 1.5f, topY + seg), on ? base : (base & 0x00FFFFFF) | (38u << 24), 0.5f);
      }
      const int pi = std::clamp((int)peak, 0, n - 1);
      if (peak > 1.0f)
      {
         const float topY = br.y - 1.0f - (float)(pi + 1) * step + gap;
         dl->AddRectFilled(ImVec2(x + 1.5f, topY), ImVec2(br.x - 1.5f, topY + seg), Hi(200), 0.5f);
      }
      if (ticks)
      {
         static const float kTicks[] = { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
         for (float t : kTicks)
            dl->AddLine(ImVec2(x - 3.0f, br.y - 1.0f - (h - 2.0f) * frac(t)), ImVec2(x - 1.0f, br.y - 1.0f - (h - 2.0f) * frac(t)),
                        Ink(70), 1.0f);
      }
      dl->AddRect(tl, br, Hi(26), tok::radius_cell);
   }
}

// DeskParts: the hardware-desk pieces of the Mixer panel's console look (graphite brushed faces, a colour cap,
// raised keys with an LED bar, pots with a knurled skirt, screws), as drawing helpers any panel can share.
// Reference: SSL 4000 G. A desk is dark in both themes, so these tones are fixed on purpose.
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
   inline ImU32 Grey(int v, int a = 255) { return IM_COL32(v, v, v, a); }
   inline ImU32 Tint(ImU32 c, float k, int a = 255) // k < 1 darkens, k > 1 lightens
   {
      const float r = std::min(255.0f, (float)(c & 0xFF) * k), g = std::min(255.0f, (float)((c >> 8) & 0xFF) * k),
                  b = std::min(255.0f, (float)((c >> 16) & 0xFF) * k);
      return IM_COL32((int)r, (int)g, (int)b, a);
   }

   inline void Screw(ImDrawList* dl, ImVec2 c)
   {
      dl->AddCircleFilled(ImVec2(c.x, c.y + 0.6f), 3.0f, Grey(0, 120), 10);
      dl->AddCircleFilled(c, 2.6f, Grey(92), 10);
      dl->AddCircleFilled(ImVec2(c.x - 0.5f, c.y - 0.5f), 1.6f, Grey(138), 8);
      dl->AddLine(ImVec2(c.x - 1.6f, c.y + 1.0f), ImVec2(c.x + 1.6f, c.y - 1.0f), Grey(30), 1.0f);
   }

   // The console chassis behind the faces: a dark steel bed with a lit top rail.
   inline void Chassis(ImDrawList* dl, ImVec2 p, ImVec2 q)
   {
      dl->AddRectFilled(p, q, IM_COL32(18, 19, 22, 255), 5.0f);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 9), Grey(255, 22), Grey(255, 22), Grey(255, 0), Grey(255, 0));
      dl->AddRect(p, q, Grey(0, 200), 5.0f, 0, 1.0f);
   }

   // A face: graphite plate, fine vertical brushing (a fixed hash, so nothing swims), a bevelled edge and a colour
   // cap on top (6 px). `lit` outlines it in the check colour.
   inline void Face(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 cap, bool lit)
   {
      const float r = 3.0f;
      dl->AddRectFilledMultiColor(p, q, IM_COL32(58, 61, 68, 255), IM_COL32(58, 61, 68, 255), IM_COL32(36, 38, 43, 255), IM_COL32(36, 38, 43, 255));
      dl->PushClipRect(p, q, true);
      for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
      {
         const unsigned hsh = (unsigned)((int)(x * 7.0f) * 2654435761u) >> 24;
         dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Grey(255, 5 + (int)(hsh % 7)) : Grey(0, 6 + (int)(hsh % 9)), 1.0f);
      }
      dl->PopClipRect();
      dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, q.y - 1), Grey(255, 28), r, 0, 1.0f);
      dl->AddRect(p, q, Grey(0, 220), r, 0, 1.0f);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 6), cap, r, ImDrawFlags_RoundCornersTop);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, p.y + 6), Grey(255, 70), Grey(255, 70), Grey(0, 50), Grey(0, 50));
      dl->AddLine(ImVec2(p.x + 1, p.y + 6.5f), ImVec2(q.x - 1, p.y + 6.5f), Grey(0, 160), 1.0f);
      if (lit)
         dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), r, 0, 1.5f);
   }

   // A key cap, drawn only: a raised cap with an LED bar above the legend. `ledLit` 0..1 is how bright the bar is.
   inline void KeyCap(ImDrawList* dl, ImVec2 p, ImVec2 q, bool hovered, bool down, float ledLit, ImU32 led, const char* legend = nullptr,
                      bool small = false, float lamp = 0.0f)
   {
      const float ledH = small ? 2.0f : 2.5f;
      dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(q.x, q.y + 1), Grey(0, 150), 3.0f);
      const float dip = down ? 1.0f : 0.0f;
      const ImVec2 a(p.x, p.y + dip), b(q.x, q.y - 1.0f + dip);
      const int top = hovered ? 96 : 84;
      dl->AddRectFilledMultiColor(a, b, Grey(top), Grey(top), Grey(54), Grey(54));
      dl->AddRect(a, b, Grey(0, 220), 3.0f, 0, 1.0f);
      dl->AddLine(ImVec2(a.x + 2, a.y + 1.5f), ImVec2(b.x - 2, a.y + 1.5f), Grey(255, 60), 1.0f);
      if (lamp > 0.0f) // the whole cap lights in the key colour, brightness = lamp (0..1)
      {
         lamp = std::clamp(lamp, 0.0f, 1.0f);
         const int al = (int)(120.0f + 135.0f * lamp);
         const ImU32 t = Tint(led, 0.55f + 0.75f * lamp, al), bt = Tint(led, 0.35f + 0.5f * lamp, al);
         dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), t, t, bt, bt);
         dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), Grey(255, (int)(70.0f * lamp)), 3.0f, 0, 1.0f);
      }
      const ImVec2 la(a.x + 4.0f, a.y + 3.0f), lb(b.x - 4.0f, a.y + 3.0f + ledH);
      ledLit = std::clamp(ledLit, 0.0f, 1.0f);
      dl->AddRectFilled(la, lb, ledLit > 0.0f ? Tint(led, 0.22f + 0.78f * ledLit) : Tint(led, 0.22f), 1.0f);
      if (ledLit > 0.05f)
         dl->AddRectFilled(ImVec2(la.x - 1, la.y - 1), ImVec2(lb.x + 1, lb.y + 1), (led & 0x00FFFFFF) | ((ImU32)(60.0f * ledLit) << 24), 2.0f);
      if (legend != nullptr)
      {
         const float fs = ImGui::GetFontSize() * 0.92f;
         const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, legend);
         dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + ((b.x - a.x) - ts.x) * 0.5f, a.y + 6.0f + (b.y - a.y - 6.0f - ts.y) * 0.5f),
                     ledLit > 0.0f ? Grey(255) : Grey(255, 190), legend);
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
      dl->AddRectFilledMultiColor(p, q, IM_COL32(58, 61, 68, 255), IM_COL32(58, 61, 68, 255), IM_COL32(36, 38, 43, 255), IM_COL32(36, 38, 43, 255));
      dl->PushClipRect(p, q, true);
      for (float x = p.x + 1.0f; x < q.x; x += 2.0f)
      {
         const unsigned hsh = (unsigned)((int)(x * 7.0f) * 2654435761u) >> 24;
         dl->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), hsh & 1 ? Grey(255, 5 + (int)(hsh % 7)) : Grey(0, 6 + (int)(hsh % 9)), 1.0f);
      }
      dl->PopClipRect();
      dl->AddRect(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, q.y - 1), Grey(255, 28), r, 0, 1.0f);
      dl->AddRect(p, q, Grey(0, 220), r, 0, 1.0f);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + 6, q.y - 1), cap, r, ImDrawFlags_RoundCornersLeft);
      dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + 6, q.y - 1), Grey(255, 70), Grey(0, 50), Grey(0, 50), Grey(255, 70));
      dl->AddLine(ImVec2(p.x + 6.5f, p.y + 1), ImVec2(p.x + 6.5f, q.y - 1), Grey(0, 160), 1.0f);
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
      dl->PathStroke(Grey(0, 150), 0, 2.5f);
      if (std::fabs(ang - from) > 0.01f)
      {
         dl->PathArcTo(c, R + 2.5f, std::min(from, ang), std::max(from, ang), 24);
         dl->PathStroke(col, 0, 2.5f);
      }
      dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f), R, Grey(0, 130), 28);
      dl->AddCircleFilled(c, R, Grey(22), 28);
      for (int i = 0; i < 24; i++)
      {
         const float a = (float)i / 24.0f * 2.0f * IM_PI;
         dl->AddLine(ImVec2(c.x + cosf(a) * (R - 3.0f), c.y + sinf(a) * (R - 3.0f)), ImVec2(c.x + cosf(a) * (R - 0.5f), c.y + sinf(a) * (R - 0.5f)),
                     Grey(255, hovered || active ? 70 : 46), 1.0f);
      }
      const float cr = R - 4.0f;
      dl->AddCircleFilled(c, cr, IM_COL32(70, 73, 80, 255), 24);
      dl->AddCircleFilled(ImVec2(c.x - 1.0f, c.y - 1.5f), cr - 1.5f, IM_COL32(98, 102, 111, 255), 24);
      dl->AddCircleFilled(ImVec2(c.x - 1.5f, c.y - 2.5f), cr - 4.0f, IM_COL32(112, 117, 126, 255), 20);
      dl->AddCircle(c, cr, Grey(0, 170), 24, 1.0f);
      dl->AddLine(ImVec2(c.x + cosf(ang) * 2.0f, c.y + sinf(ang) * 2.0f), ImVec2(c.x + cosf(ang) * (cr - 0.5f), c.y + sinf(ang) * (cr - 0.5f)), col, 2.2f);
      const float fs = ImGui::GetFontSize() * 0.72f;
      const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag).x;
      dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (cellW - tw) * 0.5f, p.y + kPotD + 3.0f), Grey(255, 140), tag);
      return changed;
   }

   // The readout plate: dark glass with the value in console green.
   inline void Plate(ImDrawList* dl, ImVec2 p, float w, const char* text, ImU32 ink = IM_COL32(120, 255, 160, 255))
   {
      const ImVec2 q(p.x + w, p.y + kPlateH);
      dl->AddRectFilled(p, q, IM_COL32(10, 14, 12, 255), 2.0f);
      dl->AddRect(p, q, Grey(255, 30), 2.0f);
      const float fs = ImGui::GetFontSize() * 0.86f;
      const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
      dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (kPlateH - ts.y) * 0.5f), ink, text);
   }

   // The scribble inset, horizontal: dark glass, the number in the strip colour, then the name. Drawing only.
   inline void ScribbleRow(ImDrawList* dl, ImVec2 p, ImVec2 q, const char* name, int number, ImU32 col, bool dim)
   {
      dl->AddRectFilled(p, q, IM_COL32(12, 13, 15, 255), 3.0f);
      dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p.x + 5, q.y - 1), Tint(col, 0.95f, 255), 2.0f, ImDrawFlags_RoundCornersLeft);
      dl->AddRect(p, q, Grey(255, 34), 3.0f);
      char num[8];
      snprintf(num, sizeof(num), "%d", number);
      const float nfs = ImGui::GetFontSize() * 0.78f;
      const float cy = (p.y + q.y) * 0.5f;
      dl->AddText(ImGui::GetFont(), nfs, ImVec2(p.x + 9.0f, cy - nfs * 0.5f), Tint(col, 1.25f), num);
      dl->PushClipRect(ImVec2(p.x + 3.0f, p.y), ImVec2(q.x - 3.0f, q.y), true);
      dl->AddText(ImVec2(p.x + 21.0f, cy - ImGui::GetTextLineHeight() * 0.5f), dim ? Grey(255, 110) : Grey(250), name);
      dl->PopClipRect();
   }

   // The Mixer's segmented LED meter (green / amber / red segments, held peak, dB ticks). level is linear 0..1;
   // call between PushID/PopID so each meter keeps its own peak.
   inline void ConsoleMeter(float x, float y, float w, float h, float level)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 tl(x, y), br(x + w, y + h);
      dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, 120), 2.0f);
      const float db = level > 1e-4f ? 20.0f * log10f(level) : -120.0f;
      auto frac = [](float d) { return std::clamp((d + 60.0f) / 60.0f, 0.0f, 1.0f); };
      const float seg = 3.0f, gap = 1.0f, step = seg + gap;
      const int n = std::max(1, (int)((h - 2.0f) / step));
      const float lit = frac(db) * (float)n;
      ImGuiStorage* st = ImGui::GetStateStorage();
      const ImGuiID id = ImGui::GetID("##mpk");
      float peak = st->GetFloat(id, 0.0f);
      const float dt = ImGui::GetIO().DeltaTime;
      peak = std::max(lit, peak - 14.0f * dt * (float)n / 30.0f);
      st->SetFloat(id, peak);
      for (int i = 0; i < n; i++)
      {
         const float topY = br.y - 1.0f - (float)(i + 1) * step + gap;
         const float segDb = -60.0f + 60.0f * ((float)i + 0.5f) / (float)n;
         const bool on = (float)i + 0.5f <= lit;
         const ImU32 base = segDb > -3.0f ? IM_COL32(255, 86, 80, 255) : segDb > -12.0f ? IM_COL32(236, 190, 70, 255) : IM_COL32(84, 214, 140, 255);
         dl->AddRectFilled(ImVec2(x + 1.5f, topY), ImVec2(br.x - 1.5f, topY + seg), on ? base : (base & 0x00FFFFFF) | (38u << 24), 0.5f);
      }
      const int pi = std::clamp((int)peak, 0, n - 1);
      if (peak > 1.0f)
      {
         const float topY = br.y - 1.0f - (float)(pi + 1) * step + gap;
         dl->AddRectFilled(ImVec2(x + 1.5f, topY), ImVec2(br.x - 1.5f, topY + seg), IM_COL32(255, 255, 255, 200), 0.5f);
      }
      dl->AddRect(tl, br, IM_COL32(255, 255, 255, 26), 2.0f);
   }
}

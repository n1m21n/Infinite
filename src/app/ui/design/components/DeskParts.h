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
}

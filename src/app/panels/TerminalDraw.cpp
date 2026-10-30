#include "app/panels/TerminalDraw.h"

#include <cmath>

namespace TerminalDraw
{
namespace
{
   // U+2500..257F: arm weights left, right, up, down. 0 none, 1 light, 2 heavy, 3 double.
   const char kBox[] =
      "1100220000110022" "1100220000110022" "1100220000110022" "0101020101020202"
      "1001200110022002" "0110021001200220" "1010201010202020" "0111021101210112"
      "0122022102120222" "1011201110211012" "1022202120122022" "1101210112012201"
      "1102210212022202" "1110211012102210" "1120212012202220" "1111211112112211"
      "1121111211222121" "1221211212122221" "2212212212222222" "1100220000110022"
      "3300003303010103" "0303300110033003" "0310013003303010" "1030303003110133"
      "0333301110333033" "3301110333033310" "1130333033111133" "3333010110011010"
      "0110000000000000" "1000001001000001" "2000002002000002" "1200001221000021";

   inline float Snap(float v) { return std::floor(v + 0.5f); }

   void Rect(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 c)
   {
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), c);
   }

   void Box(ImDrawList* dl, int index, float x, float y, float w, float h, ImU32 c)
   {
      const char* arms = kBox + index * 4;
      const int l = arms[0] - '0', r = arms[1] - '0', u = arms[2] - '0', d = arms[3] - '0';
      const float x1 = x + w, y1 = y + h;
      const float light = std::fmax(1.0f, Snap(h / 16.0f));
      const float heavy = light * 2.0f;
      const float mx = Snap(x + w * 0.5f - light * 0.5f), my = Snap(y + h * 0.5f - light * 0.5f);
      const bool anyDouble = l == 3 || r == 3 || u == 3 || d == 3;

      // Rounded corners ╭╮╯╰ share the arms of their square twins.
      if (index >= 0x6D && index <= 0x70)
      {
         const float cx = mx + light * 0.5f, cy = my + light * 0.5f;
         const float radius = std::fmin(w, h) * 0.5f;
         const float ax = l ? x : x1, ay = cy, bx = cx, by = u ? y : y1;
         const float sx = l ? -1.0f : 1.0f, sy = u ? -1.0f : 1.0f;
         dl->PathLineTo(ImVec2(ax, ay));
         dl->PathLineTo(ImVec2(cx + sx * radius, cy));
         dl->PathBezierQuadraticCurveTo(ImVec2(cx, cy), ImVec2(cx, cy + sy * radius), 8);
         dl->PathLineTo(ImVec2(bx, by));
         dl->PathStroke(c, 0, light);
         return;
      }
      if (index >= 0x71 && index <= 0x73)
      {
         if (index != 0x71) dl->AddLine(ImVec2(x, y), ImVec2(x1, y1), c, light);
         if (index != 0x72) dl->AddLine(ImVec2(x1, y), ImVec2(x, y1), c, light);
         return;
      }
      const auto thick = [&](int weight) { return weight == 2 ? heavy : light; };
      const float gap = light;  // between the strokes of a double line
      const auto hArm = [&](float x0, float x2, int weight)
      {
         if (weight == 3)
         {
            Rect(dl, x0, my - gap, x2, my - gap + light, c);
            Rect(dl, x0, my + gap, x2, my + gap + light, c);
         }
         else
         {
            const float t = thick(weight);
            Rect(dl, x0, my + (light - t) * 0.5f, x2, my + (light - t) * 0.5f + t, c);
         }
      };
      const auto vArm = [&](float y0, float y2, int weight)
      {
         if (weight == 3)
         {
            Rect(dl, mx - gap, y0, mx - gap + light, y2, c);
            Rect(dl, mx + gap, y0, mx + gap + light, y2, c);
         }
         else
         {
            const float t = thick(weight);
            Rect(dl, mx + (light - t) * 0.5f, y0, mx + (light - t) * 0.5f + t, y2, c);
         }
      };
      // Each arm runs past the centre far enough to close the corner.
      const float over = anyDouble ? gap + light : (heavy + light) * 0.5f;
      const float hL = (u || d) ? mx + light + over - light : mx + light;
      const float hR = (u || d) ? mx - over : mx;
      const float vT = (l || r) ? my + light + over - light : my + light;
      const float vB = (l || r) ? my - over : my;
      if (l) hArm(x, hL, l);
      if (r) hArm(hR, x1, r);
      if (u) vArm(y, vT, u);
      if (d) vArm(vB, y1, d);
   }

   void Block(ImDrawList* dl, int index, float x, float y, float w, float h, ImU32 c)
   {
      const float x1 = x + w, y1 = y + h, midX = Snap(x + w * 0.5f), midY = Snap(y + h * 0.5f);
      if (index == 0) Rect(dl, x, y, x1, midY, c);
      else if (index <= 8) Rect(dl, x, Snap(y1 - h * (float)index / 8.0f), x1, y1, c);
      else if (index <= 15) Rect(dl, x, y, Snap(x + w * (float)(16 - index) / 8.0f), y1, c);
      else if (index == 16) Rect(dl, midX, y, x1, y1, c);
      else if (index <= 19)
      {
         const ImU32 alpha = (c >> 24) * (ImU32)(index - 16) / 4;
         Rect(dl, x, y, x1, y1, (c & 0x00FFFFFFu) | (alpha << 24));
      }
      else if (index == 20) Rect(dl, x, y, x1, Snap(y + h / 8.0f), c);
      else if (index == 21) Rect(dl, Snap(x1 - w / 8.0f), y, x1, y1, c);
      else
      {
         // Quadrants ▖▗▘▙▚▛▜▝▞▟: bit 1 upper left, 2 upper right, 4 lower left, 8 lower right.
         static const unsigned char kQuad[] = { 4, 8, 1, 13, 9, 7, 11, 2, 6, 14 };
         const unsigned char bits = kQuad[index - 22];
         if (bits & 1) Rect(dl, x, y, midX, midY, c);
         if (bits & 2) Rect(dl, midX, y, x1, midY, c);
         if (bits & 4) Rect(dl, x, midY, midX, y1, c);
         if (bits & 8) Rect(dl, midX, midY, x1, y1, c);
      }
   }

   void Braille(ImDrawList* dl, int bits, float x, float y, float w, float h, ImU32 c)
   {
      static const unsigned char kCol[] = { 0, 0, 0, 1, 1, 1, 0, 1 };
      static const unsigned char kRow[] = { 0, 1, 2, 0, 1, 2, 3, 3 };
      const float dot = std::fmax(1.0f, Snap(w * 0.2f));
      for (int i = 0; i < 8; ++i)
         if (bits & (1 << i))
         {
            const float cx = x + w * (kCol[i] == 0 ? 0.3f : 0.7f), cy = y + h * (0.14f + 0.24f * (float)kRow[i]);
            Rect(dl, Snap(cx - dot * 0.5f), Snap(cy - dot * 0.5f), Snap(cx - dot * 0.5f) + dot, Snap(cy - dot * 0.5f) + dot, c);
         }
   }
}

bool IsShape(uint32_t cp)
{
   return (cp >= 0x2500 && cp <= 0x259F) || (cp >= 0x2801 && cp <= 0x28FF);
}

void Shape(ImDrawList* dl, uint32_t cp, float x, float y, float w, float h, ImU32 color)
{
   if (cp < 0x2580) Box(dl, (int)(cp - 0x2500), x, y, w, h, color);
   else if (cp < 0x2800) Block(dl, (int)(cp - 0x2580), x, y, w, h, color);
   else Braille(dl, (int)(cp - 0x2800), x, y, w, h, color);
}
}

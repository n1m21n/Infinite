// Layout engine (plan L3): pure rect math, no ImGui. A surface asks for a Row or Column, adds cells
// (fixed size or flex weight), and gets back one Rect per cell; gaps and padding come from tokens.
// Components then draw into their Rect, so sections line up without per-call-site cursor arithmetic.
#pragma once
#include "app/ui/design/Tokens.gen.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace UiLayout
{
   struct Rect
   {
      float x = 0, y = 0, w = 0, h = 0;
      float Right() const { return x + w; }
      float Bottom() const { return y + h; }
      float CenterX() const { return x + w * 0.5f; }
      float CenterY() const { return y + h * 0.5f; }
      Rect Inset(float l, float t, float r, float b) const
      {
         return { x + l, y + t, std::max(0.0f, w - l - r), std::max(0.0f, h - t - b) };
      }
      Rect Inset(float all) const { return Inset(all, all, all, all); }
   };

   enum class Axis { Horizontal, Vertical };
   enum class Align { Start, Center, End, Fill };

   struct Cell
   {
      float fixed = 0;   // main-axis size when flex == 0
      float flex = 0;    // share of the leftover main-axis space
      float minSize = 0; // flex cells never shrink below this
   };

   inline Cell Fixed(float size) { return { size, 0, 0 }; }
   inline Cell Flex(float weight = 1.0f, float minSize = 0) { return { 0, weight, minSize }; }

   // Splits `area` along `axis` into one Rect per cell. Leftover space (after fixed cells, min sizes
   // and gaps) is shared by flex weight. If the fixed content overflows, flex cells get their min size
   // and the result overflows rather than shrinking fixed cells. `cross` places each cell on the other
   // axis: Fill uses the whole extent, others need crossSize.
   inline std::vector<Rect> Split(const Rect& area, Axis axis, const std::vector<Cell>& cells,
                                  float gap = tok::space_2, Align cross = Align::Fill, float crossSize = 0)
   {
      const bool horiz = axis == Axis::Horizontal;
      const float mainTotal = horiz ? area.w : area.h;
      const float crossTotal = horiz ? area.h : area.w;
      float used = cells.empty() ? 0.0f : gap * static_cast<float>(cells.size() - 1);
      float flexSum = 0;
      for (const Cell& c : cells)
      {
         if (c.flex > 0) { used += c.minSize; flexSum += c.flex; }
         else used += c.fixed;
      }
      const float spare = std::max(0.0f, mainTotal - used);
      std::vector<Rect> out;
      out.reserve(cells.size());
      float pos = horiz ? area.x : area.y;
      for (const Cell& c : cells)
      {
         float size = c.fixed;
         if (c.flex > 0)
            size = c.minSize + (flexSum > 0 ? spare * c.flex / flexSum : 0.0f);
         float cs = crossTotal, co = 0;
         if (cross != Align::Fill)
         {
            cs = crossSize > 0 ? crossSize : crossTotal;
            co = cross == Align::Center ? (crossTotal - cs) * 0.5f : cross == Align::End ? crossTotal - cs : 0.0f;
         }
         out.push_back(horiz ? Rect{ pos, area.y + co, size, cs } : Rect{ area.x + co, pos, cs, size });
         pos += size + gap;
      }
      return out;
   }

   inline std::vector<Rect> Row(const Rect& area, const std::vector<Cell>& cells, float gap = tok::space_2,
                                Align cross = Align::Fill, float crossSize = 0)
   {
      return Split(area, Axis::Horizontal, cells, gap, cross, crossSize);
   }
   inline std::vector<Rect> Column(const Rect& area, const std::vector<Cell>& cells, float gap = tok::space_2,
                                   Align cross = Align::Fill, float crossSize = 0)
   {
      return Split(area, Axis::Vertical, cells, gap, cross, crossSize);
   }

   // Snap a rect to whole points so 1 px strokes and tile edges stay crisp at integer UI scales.
   inline Rect Snap(const Rect& r)
   {
      const float x0 = std::round(r.x), y0 = std::round(r.y);
      return { x0, y0, std::round(r.Right()) - x0, std::round(r.Bottom()) - y0 };
   }

   // Decision table as data; returns failing case count (0 = OK). Run by INFINITE_UILAYOUTTEST.
   inline int SelfCheck(const char** firstFailure)
   {
      int failures = 0;
      auto near = [](float a, float b) { return std::fabs(a - b) < 1.0e-3f; };
      auto fail = [&](const char* n) { if (failures++ == 0 && firstFailure != nullptr) *firstFailure = n; };
      const Rect area { 10, 20, 200, 28 };
      {
         auto r = Row(area, { Fixed(28), Flex(), Fixed(28) }, 8);
         if (r.size() != 3 || !near(r[0].x, 10) || !near(r[1].x, 46) || !near(r[1].w, 200 - 56 - 16) ||
             !near(r[2].Right(), 210) || !near(r[1].h, 28))
            fail("row fixed-flex-fixed");
      }
      {
         auto r = Row(area, { Flex(1), Flex(3) }, 0);
         if (!near(r[0].w, 50) || !near(r[1].w, 150) || !near(r[1].x, 60))
            fail("flex weights");
      }
      {
         auto r = Row(area, { Fixed(150), Fixed(150), Flex(1, 20) }, 8);
         if (!near(r[2].w, 20) || !near(r[0].w, 150))
            fail("overflow keeps fixed, flex at min");
      }
      {
         auto r = Column(area, { Fixed(10), Flex() }, 4, Align::Center, 100);
         if (!near(r[0].x, 60) || !near(r[0].w, 100) || !near(r[1].y, 34) || !near(r[1].h, 14))
            fail("column cross centre");
      }
      {
         if (!Row(area, {}, 8).empty()) fail("empty row");
         const Rect s = Snap({ 10.4f, 20.6f, 10.4f, 10.2f });
         if (!near(s.x, 10) || !near(s.y, 21) || !near(s.w, 11) || !near(s.h, 10)) fail("snap");
      }
      return failures;
   }
}

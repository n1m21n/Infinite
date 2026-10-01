#pragma once

#include "imgui.h"
#include <cmath>

namespace Tabler
{
   // Helper to convert 24x24 Tabler coordinate space to screen space
   inline ImVec2 Point24(ImVec2 center, float size, float x, float y)
   {
      const float s = size / 24.0f;
      return ImVec2(center.x + (x - 12.0f) * s, center.y + (y - 12.0f) * s);
   }

   // Tabler: player-play (smooth filled/outlined triangle)
   inline void DrawPlayerPlay(ImDrawList* dl, ImVec2 center, float size, ImU32 col, bool filled = true)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = ImMax(1.2f, 1.8f * s);

      // Tabler player-play vertices in 24x24 box, optically centered
      const ImVec2 p0 = Point24(center, size, 7.5f, 5.5f);
      const ImVec2 p1 = Point24(center, size, 7.5f, 18.5f);
      const ImVec2 p2 = Point24(center, size, 18.8f, 12.0f);

      if (filled)
      {
         dl->AddTriangleFilled(p0, p1, p2, col);
         const ImVec2 pts[3] = { p0, p1, p2 };
         dl->AddPolyline(pts, 3, col, ImDrawFlags_Closed, stroke);
      }
      else
      {
         const ImVec2 pts[3] = { p0, p1, p2 };
         dl->AddPolyline(pts, 3, col, ImDrawFlags_Closed, stroke);
      }
   }

   // Tabler: player-pause (two vertical rounded pills)
   inline void DrawPlayerPause(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float barW = 3.5f * s;
      const float barH = 13.0f * s;
      const float gap = 2.5f * s;
      const float rounding = barW * 0.45f;

      const ImVec2 leftMin(center.x - gap - barW, center.y - barH * 0.5f);
      const ImVec2 leftMax(center.x - gap, center.y + barH * 0.5f);
      dl->AddRectFilled(leftMin, leftMax, col, rounding);

      const ImVec2 rightMin(center.x + gap, center.y - barH * 0.5f);
      const ImVec2 rightMax(center.x + gap + barW, center.y + barH * 0.5f);
      dl->AddRectFilled(rightMin, rightMax, col, rounding);
   }

   // Tabler: player-stop (rounded square)
   inline void DrawPlayerStop(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float half = 6.0f * s;
      const float rounding = 2.0f * s;
      dl->AddRectFilled(ImVec2(center.x - half, center.y - half),
                        ImVec2(center.x + half, center.y + half), col, rounding);
   }

   // Tabler: player-track-prev / rewind (smooth rounded vertical bar and filled triangle)
   inline void DrawPlayerRewind(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = ImMax(1.2f, 1.8f * s);

      // Vertical rounded pill bar matching Pause style
      const float barW = 2.4f * s;
      const float barH = 13.0f * s;
      const float barRounding = barW * 0.45f;
      const ImVec2 barMin = Point24(center, size, 4.8f, 5.5f);
      const ImVec2 barMax(barMin.x + barW, barMin.y + barH);
      dl->AddRectFilled(barMin, barMax, col, barRounding);

      // Triangle pointing left, optically balanced and matching Play height
      const ImVec2 p0 = Point24(center, size, 18.2f, 5.5f);
      const ImVec2 p1 = Point24(center, size, 18.2f, 18.5f);
      const ImVec2 p2 = Point24(center, size, 8.6f, 12.0f);
      dl->AddTriangleFilled(p0, p1, p2, col);
      const ImVec2 pts[3] = { p0, p1, p2 };
      dl->AddPolyline(pts, 3, col, ImDrawFlags_Closed, stroke);
   }

   // Tabler: refresh (dual smooth circular arcs with arrowheads)
   inline void DrawRefresh(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float r = 6.8f * s;
      const float arrowLen = 4.2f * s;

      // Arc 1: Top / Right arc
      const float a1_start = -2.75f;
      const float a1_end = -0.15f;
      dl->PathClear();
      dl->PathArcTo(center, r, a1_start, a1_end, 16);
      dl->PathStroke(col, 0, stroke);

      // Arrowhead 1 at a1_end
      const ImVec2 tip1(center.x + r * cosf(a1_end), center.y + r * sinf(a1_end));
      const ImVec2 t1(-sinf(a1_end), cosf(a1_end));
      const ImVec2 n1(cosf(a1_end), sinf(a1_end));
      dl->AddLine(tip1, ImVec2(tip1.x - t1.x * arrowLen - n1.x * arrowLen * 0.85f,
                               tip1.y - t1.y * arrowLen - n1.y * arrowLen * 0.85f), col, stroke);
      dl->AddLine(tip1, ImVec2(tip1.x - t1.x * arrowLen + n1.x * arrowLen * 0.85f,
                               tip1.y - t1.y * arrowLen + n1.y * arrowLen * 0.85f), col, stroke);

      // Arc 2: Bottom / Left arc
      const float a2_start = 0.39f;
      const float a2_end = 2.99f;
      dl->PathClear();
      dl->PathArcTo(center, r, a2_start, a2_end, 16);
      dl->PathStroke(col, 0, stroke);

      // Arrowhead 2 at a2_end
      const ImVec2 tip2(center.x + r * cosf(a2_end), center.y + r * sinf(a2_end));
      const ImVec2 t2(-sinf(a2_end), cosf(a2_end));
      const ImVec2 n2(cosf(a2_end), sinf(a2_end));
      dl->AddLine(tip2, ImVec2(tip2.x - t2.x * arrowLen - n2.x * arrowLen * 0.85f,
                               tip2.y - t2.y * arrowLen - n2.y * arrowLen * 0.85f), col, stroke);
      dl->AddLine(tip2, ImVec2(tip2.x - t2.x * arrowLen + n2.x * arrowLen * 0.85f,
                               tip2.y - t2.y * arrowLen + n2.y * arrowLen * 0.85f), col, stroke);
   }

   // Tabler: x (clean diagonal cross with rounded stroke)
   inline void DrawX(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float d = 5.2f * s;

      dl->AddLine(ImVec2(center.x - d, center.y - d), ImVec2(center.x + d, center.y + d), col, stroke);
      dl->AddLine(ImVec2(center.x + d, center.y - d), ImVec2(center.x - d, center.y + d), col, stroke);
   }

   // Tabler: plus (cross)
   inline void DrawPlus(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float d = 5.8f * s;

      dl->AddLine(ImVec2(center.x, center.y - d), ImVec2(center.x, center.y + d), col, stroke);
      dl->AddLine(ImVec2(center.x - d, center.y), ImVec2(center.x + d, center.y), col, stroke);
   }

   // Tabler: chevron-down (smooth rounded stroke V)
   inline void DrawChevronDown(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float w = 5.2f * s;
      const float h = 3.2f * s;

      dl->PathClear();
      dl->PathLineTo(ImVec2(center.x - w, center.y - h * 0.5f));
      dl->PathLineTo(ImVec2(center.x, center.y + h * 0.5f));
      dl->PathLineTo(ImVec2(center.x + w, center.y - h * 0.5f));
      dl->PathStroke(col, 0, stroke);
   }

   // Tabler: chevron-up
   inline void DrawChevronUp(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float w = 5.2f * s;
      const float h = 3.2f * s;

      dl->PathClear();
      dl->PathLineTo(ImVec2(center.x - w, center.y + h * 0.5f));
      dl->PathLineTo(ImVec2(center.x, center.y - h * 0.5f));
      dl->PathLineTo(ImVec2(center.x + w, center.y + h * 0.5f));
      dl->PathStroke(col, 0, stroke);
   }

   // Tabler: chevron-right
   inline void DrawChevronRight(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float w = 3.2f * s;
      const float h = 5.2f * s;

      dl->PathClear();
      dl->PathLineTo(ImVec2(center.x - w * 0.5f, center.y - h));
      dl->PathLineTo(ImVec2(center.x + w * 0.5f, center.y));
      dl->PathLineTo(ImVec2(center.x - w * 0.5f, center.y + h));
      dl->PathStroke(col, 0, stroke);
   }

   // Tabler: search (magnifying glass)
   inline void DrawSearch(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      const float r = 5.5f * s;
      const ImVec2 cPos = Point24(center, size, 10.0f, 10.0f);

      dl->AddCircle(cPos, r, col, 16, stroke);
      const ImVec2 h1 = Point24(center, size, 14.2f, 14.2f);
      const ImVec2 h2 = Point24(center, size, 20.0f, 20.0f);
      dl->AddLine(h1, h2, col, stroke);
   }

   // Tabler: star (5-pointed star, outline or filled)
   inline void DrawStar(ImDrawList* dl, ImVec2 center, float size, ImU32 col, bool filled = false, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      const float rOut = 9.2f * s;
      const float rIn = 4.4f * s;
      const ImVec2 starCenter(center.x, center.y + 0.6f * s);

      ImVec2 pts[10];
      const float pi = 3.14159265358979323846f;
      for (int i = 0; i < 10; ++i)
      {
         const float angle = -pi * 0.5f + (float)i * (pi / 5.0f);
         const float r = (i % 2 == 0) ? rOut : rIn;
         pts[i] = ImVec2(starCenter.x + r * cosf(angle), starCenter.y + r * sinf(angle));
      }

      if (filled)
      {
         for (int i = 0; i < 10; ++i)
            dl->AddTriangleFilled(starCenter, pts[i], pts[(i + 1) % 10], col);
      }
      dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, stroke);
   }

   inline void DrawLayoutSidebar(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const float rounding = 2.0f * s;
      dl->AddRect(P(4, 4), P(20, 20), col, rounding, 0, stroke);
      dl->AddLine(P(9, 4.6f), P(9, 19.4f), col, stroke);
   }

   inline void DrawGridDots(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      const float s = size / 24.0f;
      const float r = ImMax(1.0f, 1.3f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const float xs[3] = { 6.5f, 12.0f, 17.5f };
      const float ys[3] = { 6.5f, 12.0f, 17.5f };
      for (int yi = 0; yi < 3; ++yi)
         for (int xi = 0; xi < 3; ++xi)
            dl->AddCircleFilled(P(xs[xi], ys[yi]), r, col);
   }

   // Tabler-style magnet - a solid filled horseshoe body (a thick stroked
   // arch, which ImGui renders as filled quad geometry rather than a thin
   // outline) with two square filled feet at the leg ends, matching a
   // solid block glyph rather than a thin decorative polyline+stripes.
   inline void DrawMagnet(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float thick = customStroke > 0.0f ? customStroke : ImMax(2.6f, 4.3f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Arch: a thick stroked path (left leg up, arc over the top, right
      // leg down) - stroking with a large thickness fills the band solidly,
      // and the arc's tangent at each spring point matches the vertical
      // legs so the leg/arc joint reads as one continuous solid shape.
      const ImVec2 archCenter = P(12.0f, 10.6f);
      const float radiusPx = 6.6f * s;
      const ImVec2 legBottomL = P(5.4f, 18.2f);
      const ImVec2 legBottomR = P(18.6f, 18.2f);

      dl->PathClear();
      dl->PathLineTo(legBottomL);
      dl->PathArcTo(archCenter, radiusPx, IM_PI, 2.0f * IM_PI, 16);
      dl->PathLineTo(legBottomR);
      dl->PathStroke(col, ImDrawFlags_None, thick);

      // Square feet, flared a touch wider than the leg thickness, so each
      // pole end reads as a distinct block rather than a rounded tip.
      const float footHalf = thick * 0.62f;
      dl->AddRectFilled(ImVec2(legBottomL.x - footHalf, legBottomL.y - footHalf * 0.7f),
                         ImVec2(legBottomL.x + footHalf, legBottomL.y + footHalf * 1.3f), col);
      dl->AddRectFilled(ImVec2(legBottomR.x - footHalf, legBottomR.y - footHalf * 0.7f),
                         ImVec2(legBottomR.x + footHalf, legBottomR.y + footHalf * 1.3f), col);
   }

   // Drag handle: two columns of three dots (Tabler grip-vertical) - the
   // conventional "grab to reorder/drag" affordance used by DAWs, table
   // rows, and list editors, so it reads as draggable without a tooltip.
   inline void DrawGripVertical(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      const float s = size / 24.0f;
      const float r = ImMax(0.9f, 1.15f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const float xs[2] = { 9.0f, 15.0f };
      const float ys[3] = { 6.0f, 12.0f, 18.0f };
      for (int xi = 0; xi < 2; ++xi)
         for (int yi = 0; yi < 3; ++yi)
            dl->AddCircleFilled(P(xs[xi], ys[yi]), r, col);
   }

   inline void DrawGauge(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const ImVec2 arcCenter = P(12, 14);
      const float radius = 8.0f * s;
      dl->PathArcTo(arcCenter, radius, IM_PI * 1.0f, IM_PI * 2.0f, 24);
      dl->PathStroke(col, 0, stroke);

      const ImVec2 needleTip = P(16.2f, 9.5f);
      dl->AddLine(arcCenter, needleTip, col, stroke);
      dl->AddCircleFilled(arcCenter, ImMax(1.4f, 1.8f * s), col);
   }

   // Arcade joystick: flat base plate, a shaft, and a filled ball top -
   // reads clean as "gaming" at small toolbar sizes where a full
   // controller's D-pad/button detail turns to mush.
   inline void DrawJoystick(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      dl->AddRect(P(5.0f, 16.5f), P(19.0f, 19.5f), col, 1.4f * s, 0, stroke);
      dl->AddLine(P(12.0f, 16.5f), P(12.0f, 9.0f), col, stroke);
      dl->AddCircleFilled(P(12.0f, 7.2f), ImMax(1.8f, 2.6f * s), col);
      dl->AddCircleFilled(P(16.2f, 18.0f), ImMax(0.8f, 1.0f * s), col);
   }

   // Vinyl/CD disc: outer rim, centre spindle hole and ring, with a couple
   // of short groove arcs in two opposite quadrants for texture - reads as
   // a record/disc at a glance without needing fill/shading.
   inline void DrawDisc(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.1f, 1.4f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };
      const ImVec2 c = P(12.0f, 12.0f);

      dl->AddCircle(c, 9.0f * s, col, 32, stroke);
      dl->AddCircle(c, 3.0f * s, col, 20, stroke * 0.85f);
      dl->AddCircleFilled(c, ImMax(0.9f, 1.1f * s), col);

      const float grooveStroke = stroke * 0.75f;
      for (float r : { 5.2f, 7.1f })
      {
         dl->PathArcTo(c, r * s, IM_PI * 1.02f, IM_PI * 1.30f, 10);
         dl->PathStroke(col, 0, grooveStroke);
         dl->PathArcTo(c, r * s, IM_PI * 0.02f, IM_PI * 0.30f, 10);
         dl->PathStroke(col, 0, grooveStroke);
      }
   }

   // Tabler-style metronome: tapered body, a pendulum arm and its weight.
   inline void DrawMetronome(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.7f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const ImVec2 body[4] = { P(9.5f, 3.0f), P(14.5f, 3.0f), P(19.0f, 20.5f), P(5.0f, 20.5f) };
      dl->AddPolyline(body, 4, col, ImDrawFlags_Closed | ImDrawFlags_RoundCornersAll, stroke);
      dl->AddLine(P(12.0f, 16.5f), P(16.5f, 7.0f), col, stroke);
      dl->AddCircleFilled(P(15.0f, 10.3f), ImMax(1.2f, 1.9f * s), col);
   }

   inline void DrawTimeline(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.1f, 1.4f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      dl->AddLine(P(3.0f, 6.0f), P(21.0f, 6.0f), col, stroke);
      dl->AddLine(P(6.0f, 6.0f), P(6.0f, 9.0f), col, stroke);
      dl->AddLine(P(12.0f, 6.0f), P(12.0f, 9.0f), col, stroke);
      dl->AddLine(P(18.0f, 6.0f), P(18.0f, 9.0f), col, stroke);

      const float r = 1.2f * s;
      dl->AddRect(P(3.0f, 11.0f), P(13.0f, 15.0f), col, r, 0, stroke);
      dl->AddRect(P(15.0f, 11.0f), P(21.0f, 15.0f), col, r, 0, stroke);
      dl->AddRect(P(5.0f, 17.0f), P(18.0f, 21.0f), col, r, 0, stroke);
   }

   // Tabler-style "cube": an isometric hexagon split into three facets by
   // lines from center to alternating vertices - reads as a single building
   // block/module, distinct from the timeline-strip glyph it replaces for
   // the Arrangement Timeline panel toggle (that glyph looked too close to
   // the panel's own ruler+lane content to work as a top-bar icon).
   inline void DrawBox3D(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.1f, 1.4f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const ImVec2 top(P(12.0f, 4.0f));
      const ImVec2 upperRight(P(19.5f, 8.2f));
      const ImVec2 lowerRight(P(19.5f, 16.2f));
      const ImVec2 bottom(P(12.0f, 20.4f));
      const ImVec2 lowerLeft(P(4.5f, 16.2f));
      const ImVec2 upperLeft(P(4.5f, 8.2f));
      const ImVec2 c(P(12.0f, 12.2f));

      const ImVec2 hexPts[7] = { top, upperRight, lowerRight, bottom, lowerLeft, upperLeft, top };
      dl->AddPolyline(hexPts, 7, col, ImDrawFlags_None, stroke);

      dl->AddLine(c, top, col, stroke);
      dl->AddLine(c, lowerRight, col, stroke);
      dl->AddLine(c, lowerLeft, col, stroke);
   }

   // Tabler: scissors (two finger rings, blades crossing to the right)
   inline void DrawScissors(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      dl->AddCircle(P(6.0f, 7.0f), 3.0f * s, col, 16, stroke);
      dl->AddCircle(P(6.0f, 17.0f), 3.0f * s, col, 16, stroke);
      dl->AddLine(P(8.6f, 8.6f), P(19.0f, 19.0f), col, stroke);
      dl->AddLine(P(8.6f, 15.4f), P(19.0f, 5.0f), col, stroke);
   }

   // Tabler: flag (pole plus a waving pennant)
   inline void DrawFlag(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      dl->AddLine(P(5.0f, 21.0f), P(5.0f, 4.0f), col, stroke);
      dl->PathClear();
      dl->PathLineTo(P(5.0f, 5.0f));
      dl->PathBezierCubicCurveTo(P(8.0f, 2.5f), P(11.0f, 7.5f), P(19.0f, 5.0f), 10);
      dl->PathLineTo(P(19.0f, 14.0f));
      dl->PathBezierCubicCurveTo(P(11.0f, 16.5f), P(8.0f, 11.5f), P(5.0f, 14.0f), 10);
      dl->PathStroke(col, ImDrawFlags_Closed, stroke);
   }

   // Tabler: folder (a track-group row's icon - never drawn for a leaf track)
   inline void DrawFolder(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.3f, 1.8f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      dl->PathClear();
      dl->PathLineTo(P(3.0f, 6.0f));
      dl->PathLineTo(P(3.0f, 18.0f));
      dl->PathLineTo(P(21.0f, 18.0f));
      dl->PathLineTo(P(21.0f, 8.0f));
      dl->PathLineTo(P(11.0f, 8.0f));
      dl->PathLineTo(P(9.0f, 6.0f));
      dl->PathStroke(col, ImDrawFlags_Closed, stroke);
   }

   // Tabler: list (3 bullets on left, 3 horizontal bars on right)
   inline void DrawList(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      const float dotR = ImMax(1.1f, 1.4f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const float ys[3] = { 6.0f, 12.0f, 18.0f };
      for (int i = 0; i < 3; ++i)
      {
         dl->AddCircleFilled(P(5.5f, ys[i]), dotR, col);
         dl->AddLine(P(9.5f, ys[i]), P(19.0f, ys[i]), col, stroke);
      }
   }

   // Tabler: adjustments-horizontal / sliders
   inline void DrawSliders(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      const float y0 = center.y - 5.5f * s;
      const float y1 = center.y;
      const float y2 = center.y + 5.5f * s;
      const float xLeft = center.x - 8.0f * s;
      const float xRight = center.x + 8.0f * s;

      dl->AddLine(ImVec2(xLeft, y0), ImVec2(xRight, y0), col, stroke);
      dl->AddCircleFilled(ImVec2(center.x - 2.0f * s, y0), 2.5f * s, col);

      dl->AddLine(ImVec2(xLeft, y1), ImVec2(xRight, y1), col, stroke);
      dl->AddCircleFilled(ImVec2(center.x + 3.0f * s, y1), 2.5f * s, col);

      dl->AddLine(ImVec2(xLeft, y2), ImVec2(xRight, y2), col, stroke);
      dl->AddCircleFilled(ImVec2(center.x - 4.0f * s, y2), 2.5f * s, col);
   }

   // Tabler: screen / monitor (for mini viewport toggle)
   inline void DrawMonitor(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.2f, 1.6f * s);
      const ImVec2 screenMin(center.x - 8.0f * s, center.y - 7.0f * s);
      const ImVec2 screenMax(center.x + 8.0f * s, center.y + 3.5f * s);
      dl->AddRect(screenMin, screenMax, col, 2.0f * s, 0, stroke);
      // stand
      dl->AddLine(ImVec2(center.x, center.y + 3.5f * s), ImVec2(center.x, center.y + 7.0f * s), col, stroke);
      dl->AddLine(ImVec2(center.x - 4.5f * s, center.y + 7.0f * s), ImVec2(center.x + 4.5f * s, center.y + 7.0f * s), col, stroke);
   }

   // Tabler: edit / clip settings (box with pencil)
   inline void DrawEdit(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.5f, 1.9f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Box contour: (7, 7) -> (4.5, 7) -> (4.5, 19.5) -> (17, 19.5) -> (17, 17)
      const ImVec2 boxPts[] = {
         P(7.0f, 7.0f), P(4.5f, 7.0f), P(4.5f, 19.5f), P(17.0f, 19.5f), P(17.0f, 17.0f)
      };
      dl->AddPolyline(boxPts, 5, col, ImDrawFlags_None, stroke);

      // Pencil body & tip
      const ImVec2 pencilPts[] = {
         P(9.0f, 15.0f), P(12.0f, 15.0f), P(20.2f, 6.8f),
         P(17.2f, 3.8f), P(9.0f, 12.0f), P(9.0f, 15.0f)
      };
      dl->AddPolyline(pencilPts, 6, col, ImDrawFlags_Closed, stroke);
      dl->AddLine(P(15.8f, 5.2f), P(18.8f, 8.2f), col, stroke);
   }

   // Tabler: repeat / loop (dual loop arrows)
   inline void DrawRepeat(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.6f, 2.0f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Top loop: (4.5, 12) -> (4.5, 7.5) -> (19.5, 7.5) with arrowhead
      const ImVec2 topPts[] = { P(4.5f, 12.0f), P(4.5f, 7.5f), P(19.5f, 7.5f) };
      dl->AddPolyline(topPts, 3, col, ImDrawFlags_None, stroke);
      const ImVec2 topArrow[] = { P(16.5f, 4.5f), P(19.5f, 7.5f), P(16.5f, 10.5f) };
      dl->AddPolyline(topArrow, 3, col, ImDrawFlags_None, stroke);

      // Bottom loop: (19.5, 12) -> (19.5, 16.5) -> (4.5, 16.5) with arrowhead
      const ImVec2 botPts[] = { P(19.5f, 12.0f), P(19.5f, 16.5f), P(4.5f, 16.5f) };
      dl->AddPolyline(botPts, 3, col, ImDrawFlags_None, stroke);
      const ImVec2 botArrow[] = { P(7.5f, 19.5f), P(4.5f, 16.5f), P(7.5f, 13.5f) };
      dl->AddPolyline(botArrow, 3, col, ImDrawFlags_None, stroke);
   }

   // Tabler: zoom / search (magnifying glass)
   inline void DrawZoom(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.5f, 2.0f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const ImVec2 c = P(10.0f, 10.0f);
      const float r = 6.8f * s;
      dl->AddCircle(c, r, col, 24, stroke);
      dl->AddLine(P(14.8f, 14.8f), P(20.5f, 20.5f), col, stroke);
   }

   // Tabler: hand-stop / grab (clean palm with 4 upright fingers and thumb)
   inline void DrawHand(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.5f, 1.8f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Four fingers with rounded caps
      // Index finger
      const ImVec2 f1[] = { P(8.0f, 14.0f), P(8.0f, 6.5f), P(9.2f, 5.0f), P(10.5f, 5.0f), P(10.5f, 12.0f) };
      dl->AddPolyline(f1, 5, col, ImDrawFlags_None, stroke);

      // Middle finger
      const ImVec2 f2[] = { P(10.5f, 12.0f), P(10.5f, 4.0f), P(11.8f, 2.8f), P(13.2f, 2.8f), P(13.5f, 4.0f), P(13.5f, 12.0f) };
      dl->AddPolyline(f2, 6, col, ImDrawFlags_None, stroke);

      // Ring finger
      const ImVec2 f3[] = { P(13.5f, 12.0f), P(13.5f, 5.2f), P(14.8f, 4.2f), P(16.0f, 4.2f), P(16.2f, 5.2f), P(16.2f, 12.5f) };
      dl->AddPolyline(f3, 6, col, ImDrawFlags_None, stroke);

      // Pinky & Palm outer contour down to wrist and thumb
      const ImVec2 palm[] = {
         P(16.2f, 12.5f), P(16.2f, 7.8f), P(17.4f, 6.8f), P(18.6f, 6.8f), P(18.8f, 7.8f),
         P(18.8f, 15.0f), P(18.2f, 18.5f), P(15.5f, 21.0f), P(11.5f, 21.0f),
         P(8.5f, 19.5f), P(6.5f, 16.5f), P(4.2f, 14.2f), P(4.0f, 12.8f),
         P(5.2f, 11.5f), P(6.8f, 12.5f), P(8.0f, 14.5f)
      };
      dl->AddPolyline(palm, 16, col, ImDrawFlags_None, stroke);
   }

   // Tabler: line-height / track height
   inline void DrawLineHeight(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.5f, 1.9f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Top arrowhead & stem
      const ImVec2 topArrow[] = { P(3.2f, 7.5f), P(6.0f, 4.5f), P(8.8f, 7.5f) };
      dl->AddPolyline(topArrow, 3, col, ImDrawFlags_None, stroke);
      dl->AddLine(P(6.0f, 4.5f), P(6.0f, 19.5f), col, stroke);
      // Bottom arrowhead
      const ImVec2 botArrow[] = { P(3.2f, 16.5f), P(6.0f, 19.5f), P(8.8f, 16.5f) };
      dl->AddPolyline(botArrow, 3, col, ImDrawFlags_None, stroke);

      // Right horizontal lines
      dl->AddLine(P(12.0f, 6.0f), P(20.5f, 6.0f), col, stroke);
      dl->AddLine(P(12.0f, 12.0f), P(20.5f, 12.0f), col, stroke);
      dl->AddLine(P(12.0f, 18.0f), P(20.5f, 18.0f), col, stroke);
   }

   // Tabler: pointer (Select Tool)
   // SVG: M7.904 17.563a1.2 1.2 0 0 0 2.228 .308l2.09 -3.093l4.907 4.907a1.067 1.067 0 0 0 1.509 0l1.047 -1.047a1.067 1.067 0 0 0 0 -1.509l-4.907 -4.907l3.113 -2.09a1.2 1.2 0 0 0 -.309 -2.228l-13.582 -3.904l3.904 13.563
   inline void DrawPointer(ImDrawList* dl, ImVec2 center, float size, ImU32 col, bool filled = true, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.4f, 1.8f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const ImVec2 pts[] = {
         P(4.0f, 4.0f),
         P(7.9f, 17.6f),
         P(10.1f, 17.9f),
         P(12.2f, 14.8f),
         P(17.1f, 19.7f),
         P(18.6f, 19.7f),
         P(19.7f, 18.6f),
         P(19.7f, 17.1f),
         P(14.8f, 12.2f),
         P(17.9f, 10.1f),
         P(17.6f, 7.9f)
      };
      if (filled)
      {
         const ImVec2 head[] = { pts[0], pts[1], pts[3], pts[10] };
         const ImVec2 tail[] = { pts[3], pts[4], pts[7], pts[8] };
         dl->AddConvexPolyFilled(head, 4, col);
         dl->AddConvexPolyFilled(tail, 4, col);
      }
      dl->AddPolyline(pts, 11, col, ImDrawFlags_Closed, stroke);
   }

   // Tabler: trim tool (two opposing chevrons, no middle lines)
   inline void DrawTrim(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.6f, 2.0f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Left chevron (<)
      const ImVec2 leftArrow[] = { P(9.5f, 6.5f), P(4.5f, 12.0f), P(9.5f, 17.5f) };
      dl->AddPolyline(leftArrow, 3, col, ImDrawFlags_None, stroke);

      // Right chevron (>)
      const ImVec2 rightArrow[] = { P(14.5f, 6.5f), P(19.5f, 12.0f), P(14.5f, 17.5f) };
      dl->AddPolyline(rightArrow, 3, col, ImDrawFlags_None, stroke);
   }

   // Tabler: pencil (draw / spawn clip tool)
   // SVG: M4 20h4l10.5 -10.5a2.828 2.828 0 1 0 -4 -4l-10.5 10.5v4
   //      M13.5 6.5l4 4
   inline void DrawPencil(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.5f, 1.9f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Outer pencil contour (body + rounded eraser cap + tip)
      const ImVec2 body[] = {
         P(4.0f, 20.0f),
         P(8.0f, 20.0f),
         P(18.5f, 9.5f),
         P(19.8f, 8.2f),
         P(19.8f, 6.8f),
         P(18.5f, 5.5f),
         P(17.2f, 4.2f),
         P(15.8f, 4.2f),
         P(14.5f, 5.5f),
         P(4.0f, 16.0f)
      };
      dl->AddPolyline(body, 10, col, ImDrawFlags_Closed, stroke);

      // Collar line: M13.5 6.5l4 4
      dl->AddLine(P(13.5f, 6.5f), P(17.5f, 10.5f), col, stroke);
   }

   // Tabler: cursor-text / range selection (I-beam text selection cursor)
   // SVG: M10 12h4
   //      M9 4a3 3 0 0 1 3 3v10a3 3 0 0 1 -3 3
   //      M15 4a3 3 0 0 0 -3 3v10a3 3 0 0 0 3 3
   inline void DrawCursorText(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.6f, 2.0f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      // Center crossbar
      dl->AddLine(P(9.5f, 12.0f), P(14.5f, 12.0f), col, stroke);

      // Left bracket
      const ImVec2 leftPts[] = {
         P(9.0f, 4.0f), P(10.5f, 4.3f), P(11.7f, 5.5f), P(12.0f, 7.0f),
         P(12.0f, 17.0f),
         P(11.7f, 18.5f), P(10.5f, 19.7f), P(9.0f, 20.0f)
      };
      dl->AddPolyline(leftPts, 8, col, ImDrawFlags_None, stroke);

      // Right bracket
      const ImVec2 rightPts[] = {
         P(15.0f, 4.0f), P(13.5f, 4.3f), P(12.3f, 5.5f), P(12.0f, 7.0f),
         P(12.0f, 17.0f),
         P(12.3f, 18.5f), P(13.5f, 19.7f), P(15.0f, 20.0f)
      };
      dl->AddPolyline(rightPts, 8, col, ImDrawFlags_None, stroke);
   }

   // Alias for Range Selection Tool
   inline void DrawRange(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      DrawCursorText(dl, center, size, col, customStroke);
   }

   // Blank-icon-slot fallback: a crisp rounded rect with a subtle inner square
   inline void DrawPlaceholder(ImDrawList* dl, ImVec2 center, float size, ImU32 col, float customStroke = 0.0f)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = customStroke > 0.0f ? customStroke : ImMax(1.1f, 1.4f * s);
      auto P = [&](float x, float y) { return Point24(center, size, x, y); };

      const float r = 2.5f * s;
      dl->AddRect(P(4.5f, 4.5f), P(19.5f, 19.5f), col, r, 0, stroke);
      dl->AddRectFilled(P(9.0f, 9.0f), P(15.0f, 15.0f), col, 1.0f * s);
   }
}

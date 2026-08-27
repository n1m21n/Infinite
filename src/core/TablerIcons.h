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

   // Tabler: player-track-prev / rewind
   inline void DrawPlayerRewind(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
   {
      if (!dl) return;
      const float s = size / 24.0f;
      const float stroke = ImMax(1.2f, 1.8f * s);
      // Vertical bar
      dl->AddLine(Point24(center, size, 5.5f, 6.0f), Point24(center, size, 5.5f, 18.0f), col, stroke);
      // Triangle pointing left
      const ImVec2 p0 = Point24(center, size, 18.5f, 5.5f);
      const ImVec2 p1 = Point24(center, size, 18.5f, 18.5f);
      const ImVec2 p2 = Point24(center, size, 8.5f, 12.0f);
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
}

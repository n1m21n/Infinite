// Draws one Infinite Glyph (Glyphs.gen.h) centred on a point, at an exact pixel size.
#pragma once
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/Glyphs.gen.h"
#include "imgui.h"
#include <cmath>

namespace glyph
{
   // `size` is the 20-unit canvas edge in pixels; the font's 1000-unit em equals the canvas, so the text size is the same number.
   // Where the glyph's top edge lands for a given canvas centre. Merged glyphs hang off the BASE font's baseline (line top
   // + its ascent), not the icon font's own; the icon font puts grid y=10 (the canvas centre) 0.3 em above its baseline.
   // AddText snaps its origin to whole pixels, so the drawn centre can differ from `center` by up to half a pixel.
   inline float TopFor(ImFont* font, ImVec2 center, float size)
   {
      return center.y + 0.3f * size - (float)(int)(font->GetFontBaked(size)->Ascent + 0.5f);
   }

   // The centre a glyph is really drawn at: draw backing shapes (discs, tiles) here so they sit concentric with it.
   inline ImVec2 SnappedCentre(ImVec2 center, float size)
   {
      ImFont* font = ImGui::GetFont();
      const float raw = TopFor(font, center, size);
      return ImVec2(center.x, center.y + ((float)(int)(raw + 0.5f) - raw));
   }

   inline void Draw(ImDrawList* dl, ImVec2 center, float size, ImU32 col, const char* g)
   {
      ImFont* font = ImGui::GetFont();
      const ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0.0f, g);
      dl->AddText(font, size, ImVec2(center.x - ext.x * 0.5f, (float)(int)(TopFor(font, center, size) + 0.5f)), col, g);
   }

   // Call-site wrappers (same argument order the retired glyph:: icons used). A trailing stroke argument is accepted and
   // ignored: stroke weight is part of the glyph, not the call site.
#define INF_GLYPH_FN(Name, Glyph) \
   inline void Draw##Name(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float = 0.0f) { if (dl) Draw(dl, c, size, col, IconsInfinite::Glyph); }
   INF_GLYPH_FN(PlayerPause, Pause)
   INF_GLYPH_FN(PlayerRewind, Rewind)
   INF_GLYPH_FN(X, Close)
   INF_GLYPH_FN(Plus, Plus)
   INF_GLYPH_FN(Refresh, Refresh)
   INF_GLYPH_FN(ChevronDown, ChevronDown)
   INF_GLYPH_FN(ChevronUp, ChevronUp)
   INF_GLYPH_FN(ChevronRight, ChevronRight)
   INF_GLYPH_FN(Search, Search)
   INF_GLYPH_FN(LayoutSidebar, Browser)
   INF_GLYPH_FN(GridDots, GridDots)
   INF_GLYPH_FN(Magnet, Snap)
   INF_GLYPH_FN(Disc, Perform)
   INF_GLYPH_FN(Box3D, Cube)
   INF_GLYPH_FN(Scissors, Scissors)
   INF_GLYPH_FN(Flag, Flag)
   INF_GLYPH_FN(Folder, Folder)
   INF_GLYPH_FN(Edit, Edit)
   INF_GLYPH_FN(Repeat, Loop)
   INF_GLYPH_FN(Zoom, Zoom)
   INF_GLYPH_FN(Hand, Hand)
   INF_GLYPH_FN(LineHeight, TrackHeight)
   INF_GLYPH_FN(Trim, Trim)
   INF_GLYPH_FN(Pencil, Pencil)
   INF_GLYPH_FN(Range, Range)
   INF_GLYPH_FN(Placeholder, Placeholder)
#undef INF_GLYPH_FN

   inline void DrawPlayerPlay(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool filled = true)
   {
      if (dl) Draw(dl, c, size, col, filled ? IconsInfinite::PlayFill : IconsInfinite::Play);
   }

   inline void DrawStar(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool filled = false, float = 0.0f)
   {
      if (dl) Draw(dl, c, size, col, filled ? IconsInfinite::StarFill : IconsInfinite::Star);
   }

   inline void DrawPointer(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool filled = true, float = 0.0f)
   {
      if (dl) Draw(dl, c, size, col, filled ? IconsInfinite::PointerFill : IconsInfinite::Pointer);
   }

   // Metronome whose pendulum swings: static body glyph + a drawn rod. swing -1..1 (0 = upright), +-1 tilts ~23 degrees.
   inline void DrawMetronome(ImDrawList* dl, ImVec2 c, float size, ImU32 col, float swing = 0.0f, float = 0.0f)
   {
      if (!dl) return;
      Draw(dl, c, size, col, IconsInfinite::MetronomeBody);
      const float u = size / 20.0f;
      const float a = (swing < -1.0f ? -1.0f : swing > 1.0f ? 1.0f : swing) * 0.41f;
      const ImVec2 pivot(c.x, c.y + 3.2f * u);
      const float len = 9.0f * u;
      const ImVec2 tip(pivot.x + sinf(a) * len, pivot.y - cosf(a) * len);
      dl->AddLine(pivot, tip, col, 1.5f * u);
      dl->AddCircleFilled(ImVec2(pivot.x + sinf(a) * len * 0.8f, pivot.y - cosf(a) * len * 0.8f), 1.2f * u, col);
   }
}

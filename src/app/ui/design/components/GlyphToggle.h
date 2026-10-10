// GlyphToggle: the small header toggle (eye, viewport, scale snap, bypass) - C8. A bare glyph, no tile, so it fits the
// single-row node header. Off = dim text colour, hover = full text colour (eased), on = accent glyph. `lit` draws the
// bypass-style filled disc behind a white glyph instead. Returns true on click; the tooltip is the caller's job.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"

namespace GlyphToggle
{
   inline bool Draw(const char* id, const char* glyphOff, const char* glyphOn, bool on, float w = 22.0f, bool lit = false)
   {
      const float h = 18.0f;
      const ImVec2 o = ImGui::GetCursorScreenPos();
      const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
      const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const ImVec2 c(o.x + w * 0.5f, o.y + h * 0.5f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (on && lit)
      {
         const ImVec4 a = hv > 0.5f ? app::AccentEmphasisHover() : app::AccentEmphasisSelected();
         dl->AddCircleFilled(glyph::SnappedCentre(c, 18.0f), 6.6f, ImGui::GetColorU32(a), 32);
         glyph::Draw(dl, c, 18.0f, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), glyphOn);
         return pressed;
      }
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImVec4 col = ImVec4(t.x, t.y, t.z, 0.5f + 0.5f * hv);
      if (on)
      {
         const ImVec4 a = app::AccentEmphasisSelected();
         col = ImVec4(a.x + (t.x - a.x) * 0.25f * hv, a.y + (t.y - a.y) * 0.25f * hv, a.z + (t.z - a.z) * 0.25f * hv, 1.0f);
      }
      glyph::Draw(dl, c, 18.0f, ImGui::GetColorU32(col), on ? glyphOn : glyphOff);
      return pressed;
   }
}

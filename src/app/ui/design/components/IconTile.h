// IconTile: the one toolbar icon button (plan section 2 + 4b). Outline glyph when off, accent tile + fill glyph when on.
// Selected never changes hue on hover (brightens ~4%); eases come from UiAnim; reduce motion = instant.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/UiAnim.h"

namespace IconTile
{
   // `swingTarget` in -1..1 swaps the glyph for a metronome whose pendulum eases to that side; > 1 = ordinary glyph.
   // Returns true on click. `size` = tile edge (tok::control_h default); the glyph is always tok::icon_control. Tooltip is the caller's job.
   inline bool Draw(const char* id, const char* glyphOff, const char* glyphOn, bool on, float size = tok::control_h,
                    float rowH = 0.0f, bool enabled = true, float swingTarget = tok::icon_tile_swing_target)
   {
      ImGuiWindow* win = ImGui::GetCurrentWindow();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float h = rowH > 0.0f ? rowH : size;
      ImGui::PushID(id);
      ImGui::BeginDisabled(!enabled);
      const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(size, h));
      ImGui::EndDisabled();
      const bool hov = ImGui::IsItemHovered() && enabled;
      const bool down = ImGui::IsItemActive() && enabled;
      const ImGuiID aid = ImGui::GetItemID();
      ImGui::PopID();

      const float hv = UiAnim::Hover(aid, hov, tok::motion_hover_in, tok::motion_hover_out);
      const float onv = UiAnim::Value(aid + 1, on ? 1.0f : 0.0f, on ? tok::motion_on : tok::motion_off);
      const ImVec2 c(p.x + size * 0.5f, p.y + h * 0.5f);
      const float ts = size;  // tile is square, centred in the row
      const ImVec2 t0(c.x - ts * 0.5f, c.y - ts * 0.5f), t1(c.x + ts * 0.5f, c.y + ts * 0.5f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const float disabledA = enabled ? 1.0f : tok::alpha_disabled;

      // Neutral hover/press overlay (6% / 12% of the text colour), fades with the hover value.
      const float overlay = (down ? tok::alpha_fill_press : tok::alpha_fill_rest * hv) * (1.0f - onv);
      if (overlay > 0.001f)
         dl->AddRectFilled(t0, t1, ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, overlay)), tok::radius_control);
      // On: accent tile grows from the centre (radius-scaled), hue constant on hover, +4% light at most.
      if (onv > 0.001f)
      {
         ImVec4 a = app::AccentEmphasisSelected();
         const float lift = tok::alpha_lift * hv;
         a = ImVec4(a.x + (1.0f - a.x) * lift, a.y + (1.0f - a.y) * lift, a.z + (1.0f - a.z) * lift, 1.0f);
         if (down) a = app::AccentEmphasisPressed();
         const float k = onv;
         dl->AddRectFilled(ImVec2(c.x - ts * 0.5f * k, c.y - ts * 0.5f * k), ImVec2(c.x + ts * 0.5f * k, c.y + ts * 0.5f * k),
                           ImGui::GetColorU32(a), tok::radius_control * k);
      }
      if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
         dl->AddRect(ImVec2(t0.x - tok::space_0_5, t0.y - tok::space_0_5), ImVec2(t1.x + tok::space_0_5, t1.y + tok::space_0_5),
                     ImGui::GetColorU32(ImGuiCol_NavHighlight), tok::radius_control + tok::space_0_5, 0, tok::icon_tile_outline_stroke);
      const char* g = (on && glyphOn != nullptr) ? glyphOn : glyphOff;
      const float gs = (swingTarget <= 1.0f || g == nullptr ? tok::icon_control : glyph::FitSize(c, tok::icon_control, g)) * (down ? 0.94f : 1.0f);
      const ImU32 glyphCol = ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, text.w * disabledA));
      if (swingTarget <= 1.0f)
         glyph::DrawMetronome(dl, c, gs, glyphCol, UiAnim::Value(aid + 2, swingTarget, tok::motion_off));  // ui-audit-ok: hash offset of an anim id
      else
         glyph::Draw(dl, c, gs, glyphCol, g);
      (void)win;
      return clicked && enabled;
   }
}

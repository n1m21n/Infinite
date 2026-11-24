// ActionButton: the one text button for node bodies, panels and inspectors (C7 "plain" and "primary").
// A 6% well with an eased hover and a centred label, the same construction as ChipButton, plus the few
// semantic fills a body button needs (record = red, learn = amber, go = green). Call sites pass a Kind,
// never a colour. `size.x` is a width floor (0 = hug the label, < 0 = fill the row); `size.y` 0 = frame height.
// Label may carry a "##key" suffix. Icon-only buttons are IconTile, not this.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "imgui.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/Tokens.gen.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/selftest/UiAudit.h"

namespace app { ImVec4 AccentEmphasisSelected(); bool IsThemeLight(); }

namespace ActionButton
{
   enum class Kind { Plain, Primary, Selected, Record, Learn, Go, Solo };

   // Set by PushSelectedButtonColors / PushPrimaryButtonStyle (and cleared by their Pop) so a button drawn
   // between them takes that look; an explicit Kind at the call wins.
   inline Kind& Scoped() { static Kind k = Kind::Plain; return k; }

   // Where the slider/dropdown faces start and how wide they are inside a param column of `colW`
   // (the 12px pin box + 4px gap sit left of every face). Button rows span exactly that.
   inline float ColumnIndent() { return tok::space_4; }
   inline float ColumnWidth(float colW) { return std::max(1.0f, colW - ColumnIndent()); }
   // Width of each of `n` equal buttons laid across the slider column of `colW` with the 4px gutter.
   inline float RowWidth(int n, float colW)
   {
      return std::max(1.0f, (ColumnWidth(colW) - tok::space_1 * (float)(n - 1)) / (float)std::max(1, n));
   }
   // Start a row / continue it: call RowBegin() before the first button, RowNext() between buttons.
   inline void RowBegin() { ImGui::Indent(ColumnIndent()); }
   inline void RowNext() { ImGui::SameLine(0.0f, tok::space_1); }
   inline void RowEnd() { ImGui::Unindent(ColumnIndent()); }

   // `glyph` (an IconsInfinite:: glyph) puts a 14px icon before the label, the pair centred as one.
   inline bool Draw(const char* label, ImVec2 size = ImVec2(0, 0), Kind kind = Kind::Plain, const char* glyph = nullptr)
   {
      if (kind == Kind::Plain)
         kind = Scoped();
      const char* key = std::strstr(label, "##");
      const std::string shown = key != nullptr ? std::string(label, key) : std::string(label);
      const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
      const float h = size.y > 0.0f ? size.y : ImGui::GetFrameHeight();
      const float w = size.x < 0.0f ? std::max(1.0f, ImGui::GetContentRegionAvail().x + size.x + 1.0f)
                                    : size.x > 0.0f
                                    // A given width is a layout cell (a strip that must end flush): kept unless the
                                    // label would not fit with even the small padding.
                                    ? std::max(size.x, ts.x + (glyph != nullptr ? tok::icon_lg : 0.0f) + tok::action_button_min * tok::space_1)
                                    : ts.x + (glyph != nullptr ? tok::icon_lg : 0.0f) + tok::action_button_w_ratio * tok::space_2;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::ButtonLabelHook != nullptr)
         ImGui::ButtonLabelHook(label); // headless --describe lists the buttons a node draws, as ImGui::Button does
      const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
      const bool held = ImGui::IsItemActive();
      if (kind == Kind::Selected)
         UiAudit::NoteOn();
      const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const bool light = app::IsThemeLight();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImVec4 fill;
      bool solid = true;
      switch (kind)
      {
      case Kind::Primary:
      case Kind::Selected: fill = app::AccentEmphasisSelected(); break;
      case Kind::Record: fill = tok::V4(tok::action_record, light); break;
      case Kind::Learn: fill = tok::V4(tok::action_learn, light); break;
      case Kind::Go: fill = tok::V4(tok::action_go, light); break;
      case Kind::Solo: fill = tok::V4(tok::action_solo, light); break;
      default: fill = ImVec4(t.x, t.y, t.z, tok::alpha_fill_rest + tok::alpha_hover_add * hv + (held ? tok::alpha_lift : 0.0f)); solid = false; break;
      }
      if (solid)
      {
         const float lift = tok::action_button_lift_ratio * hv - (held ? 0.08f : 0.0f);
         fill = ImVec4(std::clamp(fill.x + lift, 0.0f, 1.0f), std::clamp(fill.y + lift, 0.0f, 1.0f), std::clamp(fill.z + lift, 0.0f, 1.0f), 1.0f);
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(fill), tok::radius_control);
      const float iconW = glyph != nullptr ? tok::icon_lg : 0.0f; // 14px glyph + 6px gap
      const ImU32 ink = ImGui::GetColorU32(kind == Kind::Solo ? tok::V4(tok::action_button_ink_shade_sm) : solid ? tok::V4(tok::action_button_ink_shade_lg) : ImVec4(t.x, t.y, t.z, tok::alpha_text_soft));
      const float x0 = p.x + (w - ts.x - iconW) * 0.5f;
      if (glyph != nullptr)
         glyph::Draw(dl, ImVec2(std::round(x0 + tok::action_button_strum_frames), std::round(p.y + h * 0.5f)), tok::action_button_top, ink, glyph);
      // Snap to device pixels, not points: 15 pt text in a 24 pt control sits at 4.5 pt, where ImGui's own frames put it.
      const float px = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y);
      const ImVec2 tp(std::round(x0 + iconW), std::round((p.y + (h - ts.y) * 0.5f) * px) / px);
      dl->AddText(tp, ink, shown.c_str());
      if (glyph != nullptr)
      {
         ImVec2 gmn, gmx;
         glyph::InkBox(ImVec2(std::round(x0 + tok::action_button_strum_frames), std::round(p.y + h * 0.5f)), tok::action_button_glyph_box, glyph, gmn, gmx);
         UiAudit::NoteInk(1, gmn, gmx, tok::icon_sm, shown.c_str(), glyph);
      }
      if (!shown.empty())
         UiAudit::NoteInk(glyph != nullptr ? 5 : 0, tp, ImVec2(tp.x + ts.x, tp.y + ts.y));
      return clicked;
   }

   // One button across the whole slider column of `colW`.
   inline bool Column(const char* label, float colW, Kind kind = Kind::Plain)
   {
      ImGui::Indent(ColumnIndent());
      const bool c = Draw(label, ImVec2(ColumnWidth(colW), 0), kind);
      ImGui::Unindent(ColumnIndent());
      return c;
   }
}

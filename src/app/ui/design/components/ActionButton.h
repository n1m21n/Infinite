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

namespace app { ImVec4 AccentEmphasisSelected(); bool IsThemeLight(); }

namespace ActionButton
{
   enum class Kind { Plain, Primary, Selected, Record, Learn, Go, Solo };

   // Set by PushSelectedButtonColors / PushPrimaryButtonStyle (and cleared by their Pop) so a button drawn
   // between them takes that look; an explicit Kind at the call wins.
   inline Kind& Scoped() { static Kind k = Kind::Plain; return k; }

   // Where the slider/dropdown faces start and how wide they are inside a param column of `colW`
   // (the 12px pin box + 4px gap sit left of every face). Button rows span exactly that.
   inline float ColumnIndent() { return 16.0f; }
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

   inline bool Draw(const char* label, ImVec2 size = ImVec2(0, 0), Kind kind = Kind::Plain)
   {
      if (kind == Kind::Plain)
         kind = Scoped();
      const char* key = std::strstr(label, "##");
      const std::string shown = key != nullptr ? std::string(label, key) : std::string(label);
      const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
      const float h = size.y > 0.0f ? size.y : ImGui::GetFrameHeight();
      const float w = size.x < 0.0f ? std::max(1.0f, ImGui::GetContentRegionAvail().x + size.x + 1.0f)
                                    : std::max(size.x, ts.x + 2.0f * tok::space_2);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::ButtonLabelHook != nullptr)
         ImGui::ButtonLabelHook(label); // headless --describe lists the buttons a node draws, as ImGui::Button does
      const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
      const bool held = ImGui::IsItemActive();
      const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const bool light = app::IsThemeLight();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImVec4 fill;
      bool solid = true;
      switch (kind)
      {
      case Kind::Primary:
      case Kind::Selected: fill = app::AccentEmphasisSelected(); break;
      case Kind::Record: { const tok::Rgba8& c = light ? tok::action_record.light : tok::action_record.dark; fill = ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f); break; }
      case Kind::Learn: { const tok::Rgba8& c = light ? tok::action_learn.light : tok::action_learn.dark; fill = ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f); break; }
      case Kind::Go: { const tok::Rgba8& c = light ? tok::action_go.light : tok::action_go.dark; fill = ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f); break; }
      case Kind::Solo: { const tok::Rgba8& c = light ? tok::action_solo.light : tok::action_solo.dark; fill = ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f); break; }
      default: fill = ImVec4(t.x, t.y, t.z, 0.06f + 0.05f * hv + (held ? 0.04f : 0.0f)); solid = false; break;
      }
      if (solid)
      {
         const float lift = 0.06f * hv - (held ? 0.08f : 0.0f);
         fill = ImVec4(std::clamp(fill.x + lift, 0.0f, 1.0f), std::clamp(fill.y + lift, 0.0f, 1.0f), std::clamp(fill.z + lift, 0.0f, 1.0f), 1.0f);
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(fill), tok::radius_tile);
      dl->AddText(ImVec2(std::round(p.x + (w - ts.x) * 0.5f), std::round(p.y + (h - ts.y) * 0.5f)),
                  ImGui::GetColorU32(kind == Kind::Solo ? ImVec4(0.10f, 0.10f, 0.12f, 1.0f) : solid ? ImVec4(1, 1, 1, 1) : ImVec4(t.x, t.y, t.z, 0.85f)), shown.c_str());
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

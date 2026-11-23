// ColourChip: the colour field of a colour param (C10). A rounded swatch of the colour with a hairline edge in the
// text tone; hover brightens the edge. Click returns true; the picker/binding logic stays with the caller.
#pragma once
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/components/FieldWell.h"
#include <cmath>

namespace ColourChip
{
   inline bool Draw(const char* id, const float* rgb, ImVec2 size = ImVec2(tok::colour_chip_w, 0.0f))
   {
      const float h = size.y > 0.0f ? size.y : ImGui::GetFrameHeight();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(size.x, h));
      const float hv = UiAnim::Hover(ImGui::GetItemID(), ImGui::IsItemHovered(), tok::motion_hover_in, tok::motion_hover_out);
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 q(p.x + size.x, p.y + h);
      dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)), tok::radius_control);
      dl->AddRect(p, q, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, tok::alpha_swatch_edge + tok::alpha_swatch_edge_hover * hv)), tok::radius_control, 0, 1.0f);
      return clicked;
   }

   // A colour param row: the field well with the caption inside on the left and a fixed-size rounded swatch on the
   // right (same grammar as the slider and dropdown faces). One hit item over the whole face. `labelTint` (optional)
   // recolours the caption, used for a palette-bound colour.
   inline bool DrawRow(const char* id, const char* caption, const float* rgb, float width, const ImVec4* labelTint = nullptr)
   {
      const float h = ImGui::GetFrameHeight();
      const float pad = tok::space_2;
      const ImVec2 mn = ImGui::GetCursorScreenPos();
      const ImVec2 mx(mn.x + width, mn.y + h);
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      FieldWell::Draw(dl, mn, mx, hovered, ImGui::IsItemActive());
      const float sw = tok::colour_swatch_w, inset = tok::switch_inset;
      const ImVec2 sMin(mx.x - inset - sw, mn.y + inset), sMax(mx.x - inset, mx.y - inset);
      dl->AddRectFilled(sMin, sMax, ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)), tok::radius_control);
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      dl->AddRect(sMin, sMax, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, tok::alpha_swatch_edge)), tok::radius_control, 0, 1.0f);
      ImVec4 dim = labelTint != nullptr ? *labelTint : ImVec4(t.x, t.y, t.z, t.w * tok::alpha_caption);
      dl->PushClipRect(mn, ImVec2(sMin.x - pad, mx.y), true);
      dl->AddText(ImVec2(mn.x + pad, mn.y + (h - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(dim), caption);
      dl->PopClipRect();
      UiAudit::NoteKind("colour", id, ImGui::CalcTextSize(caption).x, sMin.x - pad - (mn.x + pad));
      return clicked;
   }

   // The colour menu row every panel uses (the Sample Studio look): small round dots, perRow to a row, a ring on the
   // current one. Returns the clicked index or -1. Menus may hold this; they never hold sliders or fields.
   inline int Dots(const ImU32* cols, const char* const* names, int n, int selected, int perRow = 7)
   {
      const float d = ImGui::GetFontSize() * 1.1f;
      int hit = -1;
      for (int i = 0; i < n; i++)
      {
         if (i % perRow != 0)
            ImGui::SameLine(0.0f, tok::space_1);
         ImGui::PushID(i);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         if (ImGui::InvisibleButton("##dot", ImVec2(d, d)))
            hit = i;
         const bool hov = ImGui::IsItemHovered();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
         dl->AddCircleFilled(c, d * (hov ? 0.46f : 0.42f), cols[i]);
         if (i == selected)
            dl->AddCircle(c, d * 0.5f, ImGui::GetColorU32(ImGuiCol_Text), 0, 1.5f);
         if (hov && names != nullptr && names[i] != nullptr)
            app::HelpTip("%s", names[i]);
         ImGui::PopID();
      }
      return hit;
   }

   // The clip / track / element tint row: kArrangePalette as Dots, entry 0 = no tint (0,0,0). Returns the picked
   // index or -1; tint() gives that entry's rgb (0,0,0 for entry 0).
   inline int TintDots(float r, float g, float b)
   {
      ImU32 cols[10];
      const char* names[10];
      int selected = -1;
      for (int i = 0; i < 10; i++)
      {
         cols[i] = app::kArrangePalette[i].col;
         names[i] = app::kArrangePalette[i].name;
         const ImVec4 c = ImGui::ColorConvertU32ToFloat4(cols[i]);
         if ((i == 0 && r == 0.0f && g == 0.0f && b == 0.0f) ||
             (i > 0 && std::fabs(r - c.x) < 0.01f && std::fabs(g - c.y) < 0.01f && std::fabs(b - c.z) < 0.01f))
            selected = i;
      }
      return Dots(cols, names, 10, selected, 5);
   }
   inline ImVec4 Tint(int i) { return i <= 0 ? ImVec4(0, 0, 0, 1) : ImGui::ColorConvertU32ToFloat4(app::kArrangePalette[i].col); }
}

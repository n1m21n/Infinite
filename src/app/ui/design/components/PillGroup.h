// PillGroup: a segmented control. A rounded group background, one accent pill on the selected segment
// (eases between segments), labels in Body/Medium. Segments are equal flex cells laid out by UiLayout.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "app/AppShared.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/UiType.h"
#include <string>

namespace PillGroup
{
   struct Segment
   {
      const char* key;    // stable id suffix, e.g. "mode.live"
      const char* label;  // shown and spoken
   };

   // Returns the index clicked this frame, or -1. `selected` is the current index.
   inline int Draw(const char* groupKey, const UiLayout::Rect& r, const Segment* segs, int count, int selected,
                   UiType::Size labelSize = UiType::Size::Body)
   {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.Right(), r.Bottom()),
                        ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.06f)), tok::radius_group);
      const UiLayout::Rect inner = r.Inset(2.0f);
      std::vector<UiLayout::Cell> cells(static_cast<size_t>(count), UiLayout::Flex());
      const std::vector<UiLayout::Rect> rects = UiLayout::Row(inner, cells, 0.0f);

      ImGui::PushID(groupKey);
      int clicked = -1;
      // The accent pill slides: its x is eased by UiAnim, keyed to the group.
      const ImGuiID gid = ImGui::GetID("##pill");
      if (selected >= 0 && selected < count)
      {
         const UiLayout::Rect sel = UiLayout::Snap(rects[static_cast<size_t>(selected)]);
         const float px = UiAnim::Value(gid, sel.x, tok::motion_on);
         const ImVec4 a = app::AccentEmphasisSelected();
         dl->AddRectFilled(ImVec2(px, sel.y), ImVec2(px + sel.w, sel.Bottom()), ImGui::GetColorU32(a), tok::radius_pill);
      }
      for (int i = 0; i < count; ++i)
      {
         const UiLayout::Rect cr = UiLayout::Snap(rects[static_cast<size_t>(i)]);
         const UiInteract::State s = UiInteract::Item(segs[i].key, cr, UiInteract::Role::Tab, segs[i].label,
                                                      true, i == selected ? "selected" : nullptr);
         const float hv = UiAnim::Hover(s.id, s.hovered && i != selected, tok::motion_hover_in, tok::motion_hover_out);
         if (hv > 0.001f)
            dl->AddRectFilled(ImVec2(cr.x, cr.y), ImVec2(cr.Right(), cr.Bottom()),
                              ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.06f * hv)), tok::radius_pill);
         if (s.focused)
            dl->AddRect(ImVec2(cr.x - 1, cr.y - 1), ImVec2(cr.Right() + 1, cr.Bottom() + 1),
                        ImGui::GetColorU32(ImGuiCol_NavHighlight), tok::radius_pill + 1.0f, 0, 2.0f);
         UiType::Scope ts(labelSize, UiType::Weight::Medium);
         const ImVec2 sz = ImGui::CalcTextSize(segs[i].label);
         dl->AddText(ImVec2(std::round(cr.CenterX() - sz.x * 0.5f), std::round(cr.CenterY() - sz.y * 0.5f)),
                     ImGui::GetColorU32(text), segs[i].label);
         if (s.clicked)
            clicked = i;
      }
      ImGui::PopID();
      return clicked;
   }
}

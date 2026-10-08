// FilterBar: the Sort / direction / Filter row of a browser panel. Three controls on one 32 pt line, the same
// height, fill and corner radius as the search field, so the row is symmetrical: [ Sort: X v ][ arrow ][ Filter: Y v ].
// The two chips are equal flex cells; the direction button is a square. An active filter takes the accent tint
// and shows a clear (x) in place of the chevron. Choosing from a chip opens the app's shared dropdown popup.
#pragma once
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/UiType.h"

namespace FilterBar
{
   constexpr float kHeight = 32.0f;

   namespace detail
   {
      inline ImVec4 TextCol() { return ImGui::GetStyleColorVec4(ImGuiCol_Text); }

      // Same construction as PillGroup: a group well (6% text, radius_group) with a 2 pt inset pill inside.
      // Hover lights the pill at 6%; `tint` swaps it for the accent pill (an active filter), like a selected tab.
      inline void Well(ImDrawList* dl, const UiLayout::Rect& r, float hv, float tint)
      {
         const ImVec4 t = TextCol();
         dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.Right(), r.Bottom()),
                           ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.06f)), tok::radius_group);
         const UiLayout::Rect in = r.Inset(2.0f);
         if (hv > 0.001f)
            dl->AddRectFilled(ImVec2(in.x, in.y), ImVec2(in.Right(), in.Bottom()),
                              ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.06f * hv)), tok::radius_pill);
         if (tint > 0.001f)
         {
            const ImVec4 a = app::AccentEmphasisSelected();
            dl->AddRectFilled(ImVec2(in.x, in.y), ImVec2(in.Right(), in.Bottom()),
                              ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, tint)), tok::radius_pill);
         }
      }

      // Icons are drawn as lines on one shared centre line, so they cannot drift off-centre like font glyphs.
      inline void Chevron(ImDrawList* dl, ImVec2 c, ImU32 col)
      {
         dl->AddLine(ImVec2(c.x - 4.0f, c.y - 2.0f), ImVec2(c.x, c.y + 2.0f), col, 1.5f);
         dl->AddLine(ImVec2(c.x, c.y + 2.0f), ImVec2(c.x + 4.0f, c.y - 2.0f), col, 1.5f);
      }
      inline void Cross(ImDrawList* dl, ImVec2 c, ImU32 col)
      {
         dl->AddLine(ImVec2(c.x - 3.5f, c.y - 3.5f), ImVec2(c.x + 3.5f, c.y + 3.5f), col, 1.5f);
         dl->AddLine(ImVec2(c.x - 3.5f, c.y + 3.5f), ImVec2(c.x + 3.5f, c.y - 3.5f), col, 1.5f);
      }

      inline void OpenMenu(const std::vector<std::string>& options, int current, std::function<void(int)> onSelect)
      {
         app::gDropdown.options = options;
         app::gDropdown.categories.clear();
         app::gDropdown.onSelect = std::move(onSelect);
         app::gDropdown.current = current;
         app::gDropdown.justOpened = true;
         app::gDropdown.focusSearch = false;
      }

      // One chip. Returns true on a click on the chip body; `clearClicked` is set when the (x) was hit instead.
      inline bool Chip(const char* key, const UiLayout::Rect& r, const char* caption, const std::string& value,
                       bool active, bool* clearClicked)
      {
         const UiInteract::State s = UiInteract::Item(key, r, UiInteract::Role::Button, caption);
         const float hv = UiAnim::Hover(s.id, s.hovered, tok::motion_hover_in, tok::motion_hover_out);
         const float on = UiAnim::Value(s.id + 1, active ? 1.0f : 0.0f, tok::motion_on);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         Well(dl, r, hv, on);
         const ImVec4 t = TextCol();
         UiType::Scope ts(UiType::Size::Title);
         const std::string cap = std::string(caption) + "  ";
         const float capW = ImGui::CalcTextSize(cap.c_str()).x;
         const std::string label = value;
         const float tx = r.x + tok::space_3 + capW;
         const float trail = r.Right() - tok::space_2 - 8.0f;
         const float room = trail - tok::space_2 - tx;  // value gets what the caption leaves
         std::string shown = label;
         while (shown.size() > 3 && ImGui::CalcTextSize(shown.c_str()).x > room)
            shown.pop_back();
         if (shown != label)
            shown += "...";
         const float ty = std::round(r.CenterY() - ImGui::GetFontSize() * 0.5f);
         dl->AddText(ImVec2(r.x + tok::space_3, ty), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, active ? 0.8f : 0.5f)), cap.c_str());
         dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.95f)), shown.c_str());
         bool cleared = false;
         if (active)
         {
            const ImVec2 c(trail, r.CenterY());
            const bool hov = ImGui::IsMouseHoveringRect(ImVec2(c.x - 10, c.y - 10), ImVec2(c.x + 10, c.y + 10));
            detail::Cross(dl, c, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, hov ? 1.0f : 0.8f)));
            cleared = s.clicked && hov;
         }
         else
            detail::Chevron(dl, ImVec2(trail, r.CenterY()), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.6f)));
         if (clearClicked)
            *clearClicked = cleared;
         return s.clicked && !cleared;
      }

      // Square button with a drawn up or down arrow (shaft + head).
      inline bool DirButton(const char* key, const UiLayout::Rect& r, bool descending)
      {
         const UiInteract::State s = UiInteract::Item(key, r, UiInteract::Role::Button,
                                                      descending ? "Descending" : "Ascending");
         const float hv = UiAnim::Hover(s.id, s.hovered, tok::motion_hover_in, tok::motion_hover_out);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         Well(dl, r, hv, 0.0f);
         const ImVec4 t = TextCol();
         const ImU32 col = ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.6f + 0.3f * hv));
         // Sort glyph: three bars of different length. Longest on top = descending, shortest on top = ascending.
         const float cx = r.CenterX(), cy = r.CenterY();
         for (int i = 0; i < 3; ++i)
         {
            const float len = (descending ? 12.0f - 4.0f * i : 4.0f + 4.0f * i);
            const float y = std::round(cy + (i - 1) * 4.0f) + 0.5f;
            dl->AddLine(ImVec2(std::round(cx - 6.0f), y), ImVec2(std::round(cx - 6.0f + len), y), col, 1.5f);
         }
         if (s.hovered)
            ImGui::SetTooltip("%s", descending ? I18n::T("Descending") : I18n::T("Ascending"));
         return s.clicked;
      }
   }

   // Draws the row at the cursor and advances it. Returns true when sort, direction or filter changed this frame
   // (menu picks land one frame later through the shared dropdown, as with DropdownButton).
   inline bool Draw(const char* key, app::BrowserFilterState& state, const std::vector<std::string>& sortNames,
                    const std::vector<std::string>& typeNames, const char* sortCaption, const char* filterCaption,
                    const char* ascTip = nullptr, const char* descTip = nullptr)
   {
      (void)ascTip;
      (void)descTip;
      const app::BrowserFilterState before = state;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      const UiLayout::Rect row{ p.x, p.y, w, kHeight };
      std::vector<UiLayout::Cell> cells = { UiLayout::Flex(), UiLayout::Fixed(kHeight) };
      if (!typeNames.empty())
         cells.push_back(UiLayout::Flex());
      const std::vector<UiLayout::Rect> c = UiLayout::Row(row, cells, tok::space_2);

      ImGui::PushID(key);
      const std::string kSort = std::string(key) + ".sort", kDir = std::string(key) + ".dir",
                        kType = std::string(key) + ".type";
      const int sIdx = std::max(0, std::min(state.sortMode, (int)sortNames.size() - 1));
      if (!sortNames.empty() &&
          detail::Chip(kSort.c_str(), UiLayout::Snap(c[0]), sortCaption, sortNames[sIdx], false, nullptr))
         detail::OpenMenu(sortNames, sIdx, [&state](int i) { state.sortMode = i; });
      if (detail::DirButton(kDir.c_str(), UiLayout::Snap(c[1]), state.descending))
         state.descending = !state.descending;
      if (!typeNames.empty())
      {
         const int tIdx = std::max(0, std::min(state.typeFilter, (int)typeNames.size() - 1));
         bool cleared = false;
         if (detail::Chip(kType.c_str(), UiLayout::Snap(c[2]), filterCaption, typeNames[tIdx], tIdx > 0, &cleared))
            detail::OpenMenu(typeNames, tIdx, [&state](int i) { state.typeFilter = i; });
         if (cleared)
            state.typeFilter = 0;
      }
      ImGui::PopID();
      ImGui::SetCursorScreenPos(p);
      ImGui::Dummy(ImVec2(w, kHeight));
      return before.sortMode != state.sortMode || before.typeFilter != state.typeFilter ||
             before.descending != state.descending;
   }
}

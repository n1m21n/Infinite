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
   constexpr float kHeight = 36.0f;

   namespace detail
   {
      inline ImVec4 TextCol() { return ImGui::GetStyleColorVec4(ImGuiCol_Text); }

      inline void Well(ImDrawList* dl, const UiLayout::Rect& r, float hv, float tint)
      {
         const ImVec4 t = TextCol();
         dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.Right(), r.Bottom()),
                           ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.04f + 0.05f * hv)), tok::radius_tile);
         dl->AddRect(ImVec2(r.x + 0.5f, r.y + 0.5f), ImVec2(r.Right() - 0.5f, r.Bottom() - 0.5f),
                     ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.14f + 0.08f * hv)), tok::radius_tile);
         if (tint > 0.001f)
         {
            const ImVec4 a = app::AccentEmphasisSelected();
            dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.Right(), r.Bottom()),
                              ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, 0.22f * tint)), tok::radius_tile);
         }
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
         dl->AddText(ImVec2(r.x + tok::space_3, ty), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.5f)), cap.c_str());
         dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.95f)), shown.c_str());
         bool cleared = false;
         if (active)
         {
            const ImVec2 c(trail, r.CenterY());
            const bool hov = ImGui::IsMouseHoveringRect(ImVec2(c.x - 10, c.y - 10), ImVec2(c.x + 10, c.y + 10));
            glyph::Draw(dl, c, 14.0f, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, hov ? 1.0f : 0.7f)), IconsInfinite::Close);
            cleared = s.clicked && hov;
         }
         else
            glyph::Draw(dl, ImVec2(trail, r.CenterY()), 14.0f, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.55f)),
                        IconsInfinite::ChevronDown);
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
         const ImU32 col = ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.55f + 0.35f * hv));
         const float cx = std::round(r.CenterX()) + 0.5f, cy = r.CenterY();
         const float h = 6.0f, head = 3.5f, d = descending ? 1.0f : -1.0f;   // d: +1 points down
         dl->AddLine(ImVec2(cx, cy - h * d), ImVec2(cx, cy + h * d), col, 1.5f);
         dl->AddLine(ImVec2(cx - head, cy + (h - head) * d), ImVec2(cx, cy + h * d), col, 1.5f);
         dl->AddLine(ImVec2(cx + head, cy + (h - head) * d), ImVec2(cx, cy + h * d), col, 1.5f);
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
      const std::vector<UiLayout::Rect> c = UiLayout::Row(row, cells, tok::space_3);

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

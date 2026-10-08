// LibraryParts: the pieces of the Library panel (search field, section header, list row).
// Cursor-flow components: each takes the cursor, draws, and advances it like a normal ImGui item,
// so they sit in a scrolling child and work with the clipper. Hover eases through UiAnim.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include "app/AppShared.h"
#include "core/CategoryColors.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/UiAnim.h"
#include "app/ui/design/UiType.h"

namespace LibraryParts
{
   constexpr float kFieldH = 32.0f;
   constexpr float kRowH = 26.0f;
   constexpr float kHeaderH = 28.0f;

   // Search field: rounded well, magnifier at the left, clear (x) at the right while there is text.
   // Returns true when the text changed. `buf` is a char array of `cap` bytes.
   inline bool SearchField(const char* id, const char* hint, char* buf, size_t cap)
   {
      const float w = ImGui::GetContentRegionAvail().x;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float padL = tok::space_2 + 16.0f + tok::space_1;   // well edge + magnifier + gap
      const float padR = tok::space_2 + 16.0f;                  // room for the clear button

      ImGui::PushID(id);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padL, (kFieldH - ImGui::GetFontSize()) * 0.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_tile);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(text.x, text.y, text.z, 0.07f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(text.x, text.y, text.z, 0.10f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(text.x, text.y, text.z, 0.10f));
      // ImGui's own nav ring is replaced by ours below, drawn only in keyboard mode.
      ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
      ImGui::SetNextItemWidth(w);
      const bool changed = ImGui::InputTextWithHint("##q", hint, buf, cap);
      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar(3);
      const bool active = ImGui::IsItemActive();
      const bool hasText = buf[0] != '\0';
      // Focus ring only while navigating by keyboard; mouse use gets none.
      static bool kbd = false;  // Tab / arrow-key navigation switches the ring on; leaving the field resets it
      if (!active)
         kbd = false;
      else if (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
         kbd = true;
      const float focus = UiAnim::Value(ImGui::GetItemID() + 7, (active && kbd) ? 1.0f : 0.0f, tok::motion_focus);

      if (focus > 0.001f)
      {
         const ImVec4 a = app::AccentEmphasisSelected();
         dl->AddRect(p, ImVec2(p.x + w, p.y + kFieldH), ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, 0.85f * focus)),
                     tok::radius_tile, 0, 1.0f);
      }
      glyph::Draw(dl, ImVec2(p.x + tok::space_2 + 8.0f, p.y + kFieldH * 0.5f), 16.0f,
                  ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, active ? 0.8f : 0.5f)), IconsInfinite::Search);

      bool cleared = false;
      if (hasText)
      {
         const ImVec2 c(p.x + w - tok::space_2 - 8.0f, p.y + kFieldH * 0.5f);
         const ImVec2 saved = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(c.x - 10.0f, c.y - 10.0f));
         if (ImGui::InvisibleButton("##clear", ImVec2(20.0f, 20.0f)))
            cleared = true;
         const bool hov = ImGui::IsItemHovered();
         ImGui::SetCursorScreenPos(saved);
         const ImU32 col = ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, hov ? 0.9f : 0.5f));
         const float r = 3.5f;
         dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.5f);
         dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.5f);
      }
      ImGui::PopID();
      if (cleared)
      {
         buf[0] = '\0';
         return true;
      }
      return changed;
   }

   // "Audio  12": body semibold, dimmed, with the item count right-aligned. Space above (not on the first one).
   inline void SectionHeader(const char* label, int count, bool first = false)
   {
      const float w = ImGui::GetContentRegionAvail().x;
      const float top = first ? tok::space_1 : tok::space_3;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(w, top + kHeaderH - tok::space_2));
      UiType::Scope ts(UiType::Size::Title, UiType::Weight::Semibold);
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float y = std::round(p.y + top + (kHeaderH - tok::space_2 - ImGui::GetFontSize()) * 0.5f + 2.0f);
      const ImU32 col = ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.6f));
      dl->AddText(ImVec2(p.x + tok::space_3, y), col, label);
      if (count >= 0)
      {
         char n[16];
         snprintf(n, sizeof(n), "%d", count);
         const ImVec2 sz = ImGui::CalcTextSize(n);
         dl->AddText(ImVec2(p.x + w - tok::space_3 - sz.x, y),
                     ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.35f)), n);
      }
   }

   struct RowResult
   {
      bool clicked = false;
      bool hovered = false;
      ImVec2 min, max;
   };

   // One list row: rounded hover tile, optional category dot, label, then a star (favourite) or "+" on hover.
   // `label` is the already-truncated text. Opens no popup itself: the caller can BeginPopupContextItem right after.
   inline RowResult Row(const char* id, const std::string& label, const ImVec4* dot, bool fav)
   {
      RowResult r;
      const float w = ImGui::GetContentRegionAvail().x;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(id);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));   // rows butt up; the hover tile carries the gap
      r.clicked = ImGui::InvisibleButton("##row", ImVec2(w, kRowH));
      ImGui::PopStyleVar();
      r.hovered = ImGui::IsItemHovered();
      const bool down = ImGui::IsItemActive();
      const float hv = UiAnim::Hover(ImGui::GetItemID(), r.hovered, tok::motion_hover_in, tok::motion_hover_out);
      ImGui::PopID();
      r.min = p;
      r.max = ImVec2(p.x + w, p.y + kRowH);

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const float o = down ? 0.12f : 0.07f * hv;
      if (o > 0.001f)
         dl->AddRectFilled(ImVec2(p.x + tok::space_1, p.y + 1.0f), ImVec2(p.x + w - tok::space_1, p.y + kRowH - 1.0f),
                           ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, o)), tok::radius_tile);
      float x = p.x + tok::space_3;
      const float cy = p.y + kRowH * 0.5f;
      if (dot != nullptr)
      {
         dl->AddCircleFilled(ImVec2(x + 3.0f, cy), 3.0f, ImGui::GetColorU32(ImVec4(dot->x, dot->y, dot->z, 1.0f)), 12);
         x += 6.0f + tok::space_2;
      }
      UiType::Scope ts(UiType::Size::Title);
      dl->AddText(ImVec2(x, std::round(cy - ImGui::GetFontSize() * 0.5f)), ImGui::GetColorU32(text), label.c_str());

      const ImVec2 trail(p.x + w - tok::space_3 - 6.0f, cy);
      if (fav)
         glyph::DrawStar(dl, trail, 12.0f, tok::U32(tok::pal::c_FFCD2DFF), /*filled=*/true);
      else if (hv > 0.001f)
         glyph::Draw(dl, trail, 14.0f, ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.6f * hv)), IconsInfinite::Plus);
      return r;
   }

   // The recessed list well: a darker rounded surface under a scrolling child, so the controls above read as a
   // header and the list as its own surface. Pair with EndWell. Fills the rest of the panel.
   inline void BeginWell(const char* id)
   {
      const ImVec2 w0 = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 w1(w0.x + avail.x, w0.y + avail.y);
      const bool light = CategoryColors::IsThemeLight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(w0, w1, ImGui::GetColorU32(ImVec4(0, 0, 0, light ? 0.04f : 0.27f)), tok::radius_pill);
      dl->AddRect(w0, w1, ImGui::GetColorU32(light ? ImVec4(0, 0, 0, 0.055f) : ImVec4(1, 1, 1, 0.04f)), tok::radius_pill);
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_2, tok::space_2));
      ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
   }
   inline void EndWell() { ImGui::EndChild(); }
}

// PanelKit: the parts every rail panel shares (internal/plans/ui-system/density.md). One anatomy (pages, toolbar,
// tool settings, content, inspector) and the UI system sheet's sizes (control_h rows, row_gap between), pairs of short
// settings on one row. Panels call these instead of keeping private copies, so a size changes in one place.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include "app/AppCommon.h"
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/selftest/UiAudit.h"

namespace PanelKit
{
   using app::HelpTip;

   // ---- tokens (panels only; nodes keep their own sizes)
   constexpr float kChip = tok::control_h;        // every chip and button, app-wide (UI system sheet 2c)
   constexpr float kRow = tok::control_h;        // a settings row: wells, sliders, checks
   constexpr float kRowGap = tok::row_gap;       // between rows
   constexpr float kSection = tok::section_gap;  // before a section header

   // The frame padding that makes an ImGui frame kRow tall at the current font.
   inline float FramePadY() { return std::max(1.0f, (kRow - ImGui::GetFontSize()) * 0.5f); }

   // ---- chips

   // An icon chip, w x h (0 = square kChip). The glyph is scaled to the icon set's ink (0.9 of the em, what most glyphs draw at), so a bar
   // of icons reads as one size. `tip` is also the audit's name for it.
   inline bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip, float w = 0.0f, float h = 0.0f,
                        ActionButton::Kind kind = ActionButton::Kind::Plain)
   {
      if (h <= 0.0f)
         h = kChip;
      if (w <= 0.0f)
         w = h;
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(id, ImVec2(w, h), kind);
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (tip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         HelpTip("%s", tip);
      if (glyphStr == nullptr)
         return clicked;
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      const ImVec2 gc((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
      const float roleGs = tok::icon_control;
      const float gs = glyph::FitSize(gc, roleGs, glyphStr);
      glyph::Draw(ImGui::GetWindowDrawList(), gc, gs, on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text), glyphStr);
#ifdef IMGUI_ENABLE_TEST_ENGINE
      ImVec2 imn, imx;
      glyph::InkBox(gc, gs, glyphStr, imn, imx);
      UiAudit::NoteInk(1, imn, imx, roleGs, tip, glyphStr);
#endif
      return clicked;
   }

   // A text chip, sized to its label (w = 0) or to a cell.
   inline bool TextChip(const char* label, bool on, const char* tip, float w = 0.0f, float h = 0.0f,
                        ActionButton::Kind kind = ActionButton::Kind::Plain)
   {
      if (h <= 0.0f)
         h = kChip;
      if (w <= 0.0f)
         w = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + 2.0f * tok::space_2;
      ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
      const bool clicked = ActionButton::Draw(label, ImVec2(w, h), kind);
      ActionButton::Scoped() = ActionButton::Kind::Plain;
      if (tip != nullptr && ImGui::IsItemHovered())
         HelpTip("%s", tip);
      return clicked;
   }

   // Width of each of n chips that together fill `total`, `gap` apart.
   inline float CellW(float total, int n, float gap = tok::space_1) { return std::floor((total - (n - 1) * gap) / (float)n); }

   // Width of chip i of n that together fill `total` exactly: the rounding spreads one pixel at a time, so no chip
   // is more than 1 px off its neighbours and the strip still ends flush.
   inline float StripW(float total, int n, int i, float gap = tok::space_1)
   {
      const float span = total - (n - 1) * gap;
      return std::round(span * (i + 1) / (float)n) - std::round(span * i / (float)n);
   }

   // ---- sections

   // A folding section header, remembered per text. Returns whether the section is open.
   inline bool FoldHeader(const char* text, bool first = false, bool startOpen = true)
   {
      static std::map<std::string, bool> sOpen;
      if (!first)
         ImGui::Dummy(ImVec2(0.0f, kSection - kRowGap * 2.0f));
      auto it = sOpen.find(text);
      bool open = it != sOpen.end() ? it->second : startOpen;
      ImGui::PushID(text);
      const float h = ImGui::GetTextLineHeight() + 2.0f;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##sec", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), h)))
         open = !open;
      const bool hov = ImGui::IsItemHovered();
      ImGui::PopID();
      sOpen[text] = open;
      const ImU32 col = ImGui::GetColorU32(hov ? ImGuiCol_Text : ImGuiCol_TextDisabled);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p.x + 5.0f, p.y + h * 0.5f);
      if (open)
         dl->AddTriangleFilled(ImVec2(c.x - 4, c.y - 2), ImVec2(c.x + 4, c.y - 2), ImVec2(c.x, c.y + 3), col);
      else
         dl->AddTriangleFilled(ImVec2(c.x - 2, c.y - 4), ImVec2(c.x + 3, c.y), ImVec2(c.x - 2, c.y + 4), col);
      dl->AddText(ImVec2(p.x + 16.0f, p.y + 1.0f), col, text, ImGui::FindRenderedTextEnd(text));
      return open;
   }

   // ---- form grid: short settings side by side, label inside the well

   // While a Grid is alive, rows that support it (a panel's slider / drag / check rows) skip their label column,
   // take one cell of `total` width and carry their label inside the well ("Size  24 px"). Cells wrap every
   // `cols`. Rows that do not support it draw as usual.
   struct GridState
   {
      int cols = 0;
      int at = 0;
      float total = 0.0f;
      float rowY = 0.0f;   // where the current grid row starts, so every cell in it starts at the same height
   };
   inline GridState& Grid() { static GridState g; return g; }

   struct GridScope
   {
      GridState keep;
      GridScope(int cols, float total)
      {
         keep = Grid();
         Grid() = { cols, 0, total, 0.0f };
      }
      ~GridScope() { Grid() = keep; }
   };

   // Places the next cell (SameLine after the first in a row) and returns its width; 0 when no grid is active.
   inline float NextCell()
   {
      GridState& g = Grid();
      if (g.cols <= 0)
         return 0.0f;
      const int col = g.at % g.cols;
      if (col != 0)
      {
         // SameLine returns to the last item's line, which a centred check box has nudged down; go back to the row's top.
         ImGui::SameLine(0.0f, tok::space_1);
         ImGui::SetCursorPosY(g.rowY);
      }
      else
         g.rowY = ImGui::GetCursorPosY();
      g.at++;
      const float w = CellW(g.total, g.cols);
      // The last column takes what rounding left, so a grid row ends exactly where a plain row does.
      return col == g.cols - 1 ? g.total - (g.cols - 1) * (w + tok::space_1) : w;
   }

   // "Label " + fmt, with any % in the label doubled so the format still reads.
   inline std::string InWellFormat(const char* label, const char* fmt)
   {
      std::string s;
      const char* end = ImGui::FindRenderedTextEnd(label);
      for (const char* c = label; c < end; c++)
      {
         s += *c;
         if (*c == '%')
            s += '%';
      }
      // A cell is narrow: one space, and no " px" unit (the label already says what it is).
      s += ' ';
      std::string f = fmt;
      const size_t px = f.rfind(" px");
      if (px != std::string::npos && px + 3 == f.size())
         f.erase(px);
      s += f;
      return s;
   }
}

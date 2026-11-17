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
#include "app/ui/design/UiDensity.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/CheckBox.h"
#include "app/selftest/UiAudit.h"

namespace PanelKit
{
   using app::HelpTip;

   // ---- tokens (panels only; nodes keep their own sizes)
   // Sizes at the current density (UiDensity): one control tall, the row gap between.
   inline float Chip() { return UiDensity::M().row; }     // every chip and button
   inline float Row() { return UiDensity::M().row; }      // a settings row: wells, sliders, checks
   inline float RowGap() { return UiDensity::M().gap; }   // between rows
   constexpr float kSection = tok::section_gap;  // before a section header

   // The frame padding that makes an ImGui frame Row() tall at the current font.
   inline float FramePadY() { return std::max(1.0f, (Row() - ImGui::GetFontSize()) * 0.5f); }

   // ---- chips

   // An icon chip, w x h (0 = square Chip()). The glyph is scaled to the icon set's ink (0.9 of the em, what most glyphs draw at), so a bar
   // of icons reads as one size. `tip` is also the audit's name for it.
   inline bool IconChip(const char* id, const char* glyphStr, bool on, const char* tip, float w = 0.0f, float h = 0.0f,
                        ActionButton::Kind kind = ActionButton::Kind::Plain)
   {
      if (h <= 0.0f)
         h = Chip();
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
      // One icon size per chip size: 0.72 of the chip, capped at the control icon, so a compact strip's glyphs do
      // not fill their chips edge to edge (16 in an 18 chip read as oversized).
      const float roleGs = std::min(tok::icon_control, std::round(h * 0.72f));
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
         h = Chip();
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

   // Set by the UI audit's crawl: every folding section draws open (nothing is stored), so the audit sees the rows
   // a closed section hides. Every fold header in a panel reads it.
   inline bool& AuditOpenAll()
   {
      static bool b = false;
      return b;
   }

   // A folding section header, remembered per text. Returns whether the section is open.
   inline bool FoldHeader(const char* text, bool first = false, bool startOpen = true)
   {
      static std::map<std::string, bool> sOpen;
      if (!first)
         ImGui::Dummy(ImVec2(0.0f, kSection - RowGap() * 2.0f));
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
      open = open || AuditOpenAll();
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

   // "Label " + fmt, with any % in the label doubled so the format still reads. With a cell width, a label that would
   // push the value past the well ends in an ellipsis (room measured with the format at 0 and a sign).
   inline std::string InWellFormat(const char* label, const char* fmt, float cellW = 0.0f)
   {
      std::string s;
      std::string fitted;
      const char* end = ImGui::FindRenderedTextEnd(label);
      if (cellW > 0.0f)
      {
         // An int format takes an int, a float format a double: find the conversion first.
         bool isInt = false;
         for (const char* c = std::strchr(fmt, '%'); c != nullptr && *c; c++)
            if (std::strchr("diuxX", *c) != nullptr) { isInt = true; break; }
            else if (std::strchr("fFeEgG", *c) != nullptr) break;
         char sample[64];
         if (isInt)
            snprintf(sample, sizeof sample, fmt, 0);
         else
            snprintf(sample, sizeof sample, fmt, -0.0);
         std::string probe = std::string("-") + sample;
         for (char& c : probe)
            if (c >= '1' && c <= '9')
               c = '0';
         const float room = cellW - 2.0f * tok::space_2 - ImGui::CalcTextSize(probe.c_str()).x - ImGui::CalcTextSize(" ").x;
         fitted = app::TruncateWithEllipsis(std::string(label, end), std::max(0.0f, room));
         label = fitted.c_str();
         end = label + fitted.size();
      }
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

   // ---- check grid

   // Several on/off settings as one block: columns sized to the widest label (1..maxCols, as many as fit the width),
   // each cell a box and its label, the whole cell one hit. Returns the index clicked, -1 for none; the caller flips
   // it after its own undo step. Use it for any run of checks (overlays, stamp fields, X/Y/Z) instead of one check
   // per row or hand-placed boxes on one line.
   inline int CheckGrid(const char* id, const char* const* labels, const bool* values, int n, int maxCols = 3)
   {
      if (n <= 0)
         return -1;
      const float box = CheckBox::Size(), gapIn = tok::space_2, gapCol = tok::space_3, rowH = Row(), gapRow = RowGap();
      float widest = 0.0f;
      for (int i = 0; i < n; i++)
         widest = std::max(widest, ImGui::CalcTextSize(labels[i], ImGui::FindRenderedTextEnd(labels[i])).x);
      const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      const float cellMin = box + gapIn + widest;
      const int cols = std::clamp((int)std::floor((avail + gapCol) / (cellMin + gapCol)), 1, std::min(maxCols, n));
      const float cellW = std::floor((avail - gapCol * (cols - 1)) / (float)cols);
      const int rows = (n + cols - 1) / cols;
      const ImVec2 o = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      int hit = -1;
      ImGui::PushID(id);
      for (int i = 0; i < n; i++)
      {
         // Column-major: a column reads top to bottom, as a list does.
         const int col = i / rows, row = i % rows;
         const ImVec2 p(o.x + col * (cellW + gapCol), o.y + row * (rowH + gapRow));
         ImGui::SetCursorScreenPos(p);
         ImGui::PushID(i);
         const bool clicked = ImGui::InvisibleButton((std::string(labels[i]) + "##cell").c_str(), ImVec2(cellW, rowH));
         const bool hovered = ImGui::IsItemHovered();
         const ImGuiID iid = ImGui::GetItemID();
         UiAudit::NoteKind("gridcheck", labels[i]);   // a checkbox already in a grid
         ImGui::PopID();
         if (clicked)
            hit = i;
         const bool on = values[i] != clicked;   // drawn as it will be, so the tick starts this frame
         const float hv = UiAnim::Hover(iid, hovered, tok::motion_hover_in, tok::motion_hover_out);
         const float onv = UiAnim::Hover(iid ^ 0x5bd1e995u, on, tok::motion_on, tok::motion_off);
         CheckBox::Draw(dl, ImVec2(p.x, std::round(p.y + (rowH - box) * 0.5f)), onv, hv);
         const char* end = ImGui::FindRenderedTextEnd(labels[i]);
         const ImVec2 tp(p.x + box + gapIn, std::round(p.y + (rowH - ImGui::GetFontSize()) * 0.5f));
         const ImVec2 tmax(p.x + cellW, p.y + rowH);
         dl->PushClipRect(tp, tmax, true);
         dl->AddText(tp, ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, hovered ? 0.9f : 0.72f)), labels[i], end);
         dl->PopClipRect();
         const float tw = ImGui::CalcTextSize(labels[i], end).x;
         UiAudit::NoteInk(4, tp, ImVec2(tp.x + tw, tp.y + ImGui::GetFontSize()), 0.0f, labels[i]);
         if (hovered && tp.x + tw > tmax.x)
            HelpTip("%s", std::string(labels[i], end).c_str());
      }
      ImGui::PopID();
      ImGui::SetCursorScreenPos(o);
      ImGui::Dummy(ImVec2(avail, rows * rowH + (rows - 1) * gapRow));
      return hit;
   }
}

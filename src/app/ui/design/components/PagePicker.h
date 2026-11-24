// PagePicker: the page strip every paged panel shares (the Performance grammar). One chip per page (colour dot,
// optional glyph, name clipped at kW), then +. Click switches, double-click renames in place, drag reorders,
// middle-click closes (canClose panels). Right-click, always these rows in this order:
// Rename | Duplicate | Colour > | Move Left | Move Right | --- | Delete Page. A row that can't run is greyed, never
// hidden, and + is always drawn. The strip scrolls sideways (wheel) when the pages outgrow maxW. Sizes are the
// "page" group of tokens.json. The panel owns the pages: Draw only reports what was asked.
#pragma once
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "app/ui/design/components/ReorderDrag.h"
#include "app/AppCommon.h"
#include "app/AppShared.h"
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/MenuParts.h"

namespace PagePicker
{
   using app::HelpTip;
   using app::L;
   using app::T;

   constexpr float kW = tok::page_chip_max_w;    // widest chip: a long name clips
   constexpr float kMinW = tok::page_chip_min_w; // narrowest chip
#ifdef __APPLE__
   constexpr const char* kRenameKey = "Cmd+R";
#else
   constexpr const char* kRenameKey = "Ctrl+R";
#endif

   struct Entry
   {
      std::string name;
      unsigned int color = 0;      // 0xRRGGBB, 0 = no colour
      const char* glyph = nullptr; // optional icon before the name
   };

   enum class Act
   {
      None,
      Select,
      Add,
      Rename,
      Recolor,
      Delete,
      Duplicate,
      Move,
      Close // middle-click on a chip: hide the page, keep what it shows (panels with canClose)
   };

   struct Result
   {
      Act act = Act::None;
      int index = -1;         // the page acted on (Move: from)
      int to = -1;            // Move: the slot it lands in
      std::string name;       // Rename
      unsigned int color = 0; // Recolor
   };

   struct Options
   {
      // Every paged panel shows the same six rows in the same order:
      // Rename, Duplicate, Colour, Move Left, Move Right, | Delete Page.
      // A row that can't run right now is greyed, never hidden.
      float maxW = tok::page_strip_max_w; // the strip scrolls past this
      bool duplicate = true;  // Duplicate enabled
      bool reorder = true;    // Move Left/Right and drag enabled
      bool canDelete = true;  // Delete Page enabled (a panel may need one page left)
      bool canAdd = true;     // the + chip enabled
      bool canClose = false;  // middle-click closes a page (Curve, Field Patcher)
   };

   struct RenameState
   {
      int index = -1;
      bool focus = false;
      char buf[64] = {};
      int menu = -1; // the chip the menu is open on
   };
   inline RenameState& Rename(ImGuiID id)
   {
      static std::unordered_map<ImGuiID, RenameState> m;
      return m[id];
   }

#ifdef IMGUI_ENABLE_TEST_ENGINE
   // Self-test probe (PageMenuTests.cpp): what the strip drew last frame, keyed by its id. Debug builds only.
   struct ProbeRow
   {
      std::string label, key;
      ImVec2 mn, mx;
      bool enabled = true;
   };
   struct Probe
   {
      int frame = -1;
      std::vector<Entry> entries;
      std::vector<ImVec2> chipMn, chipMx;
      ImVec2 addMn, addMx;
      int current = -1, renaming = -1, menuFor = -1;
      bool menuOpen = false, colourOpen = false;
      std::vector<ProbeRow> rows;
      std::vector<ImVec2> swatchMn, swatchMx;
   };
   inline std::unordered_map<std::string, Probe>& Probes()
   {
      static std::unordered_map<std::string, Probe> m;
      return m;
   }
   inline Probe*& ProbeNow()
   {
      static Probe* p = nullptr;
      return p;
   }
   // The row as shown: L() labels read "shown###original".
   inline void ProbeRowAdd(const char* label, const char* key, bool enabled, const char* suffix = "")
   {
      if (Probe* p = ProbeNow())
      {
         std::string shown = label;
         shown = shown.substr(0, shown.find("###")) + suffix;
         p->rows.push_back({ shown, key != nullptr ? key : "", ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), enabled });
      }
   }
#define PAGEPICKER_PROBE(stmt) stmt
#else
#define PAGEPICKER_PROBE(stmt)
#endif

   inline ImU32 U32(unsigned int rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, tok::page_picker_probe_row_alpha); }  // ui-audit-ok: channel mask / bit packing, not a colour value

   inline float ChipW(const Entry& e)
   {
      float w = ImGui::CalcTextSize(e.name.c_str()).x + tok::page_picker_w_ratio * tok::space_3;
      if (e.color != 0)
         w += tok::page_dot + tok::space_2;
      if (e.glyph != nullptr)
         w += tok::icon_control + tok::space_1;
      return std::clamp(w, kMinW, kW);
   }

   // Starts a rename of `index` from outside (a shortcut such as Cmd+R).
   inline void BeginRename(const char* id, const std::vector<Entry>& pages, int index)
   {
      if (index < 0 || index >= (int)pages.size())
         return;
      RenameState& r = Rename(ImGui::GetID(id));
      r.index = index;
      r.focus = true;
      std::snprintf(r.buf, sizeof r.buf, "%s", pages[index].name.c_str());
   }

   // Cmd/Ctrl+R while the panel is hovered renames the current page: the key the page menu shows.
   inline void RenameKey(const char* id, const std::vector<Entry>& pages, int current)
   {
      const ImGuiIO& io = ImGui::GetIO();
      if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput && (io.KeySuper || io.KeyCtrl) && !io.KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_R, false))
         BeginRename(id, pages, current);
   }

   inline void ColourRow(const Entry& page, int index, Result& out)
   {
      // The clip colours of the Timeline, plus None.
      static const unsigned int kColours[] = { 0xE5534B, 0xF08C3A, 0xE8C547, 0x6CC46A, 0x3FB6A8, 0x4A8FE7, 0x8C6BE0, 0xD45DB0 };
      const float d = tok::page_swatch;
      for (int c = 0; c < 8; c++)
      {
         if (c > 0)
            ImGui::SameLine(0.0f, tok::space_1);
         ImGui::PushID(c);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const bool hit = ImGui::InvisibleButton("##sw", ImVec2(d, d));
         PAGEPICKER_PROBE(if (Probe* pr = ProbeNow()) { pr->colourOpen = true; pr->swatchMn.push_back(ImGui::GetItemRectMin()); pr->swatchMx.push_back(ImGui::GetItemRectMax()); })
         if (hit)
         {
            out.act = Act::Recolor;
            out.index = index;
            out.color = kColours[c];
            ImGui::CloseCurrentPopup();
         }
         const bool on = page.color == kColours[c];
         ImDrawList* mdl = ImGui::GetWindowDrawList();
         mdl->AddCircleFilled(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f - (ImGui::IsItemHovered() || on ? 0.0f : tok::page_picker_colour_row), U32(kColours[c]), tok::page_picker_colour_row_segments);
         if (on)
            mdl->AddCircle(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f + tok::page_picker_colour_row_gap, ImGui::GetColorU32(ImGuiCol_Text), tok::page_picker_colour_row_segments, 1.0f);
         ImGui::PopID();
      }
      if (MenuParts::Item(L("None"), nullptr, page.color == 0))
      {
         out.act = Act::Recolor;
         out.index = index;
         out.color = 0;
      }
   }

   inline Result Draw(const char* id, const std::vector<Entry>& pages, int current, const char* addTip, const Options& opt = {})
   {
      Result out;
      RenameState& rn = Rename(ImGui::GetID(id)); // keyed outside the PushID, as BeginRename is
      auto startRename = [&](int i) {
         rn.index = i;
         rn.focus = true;
         std::snprintf(rn.buf, sizeof rn.buf, "%s", pages[i].name.c_str());
      };
      ImGui::PushID(id);
      PAGEPICKER_PROBE(Probe& probe = Probes()[id]; probe = Probe(); probe.frame = ImGui::GetFrameCount(); probe.entries = pages;
                       probe.current = current; probe.renaming = rn.index; ProbeNow() = &probe;)

      float natural = 0.0f;
      for (size_t i = 0; i < pages.size(); i++)
         natural += ChipW(pages[i]) + (i > 0 ? tok::space_1 : 0.0f);
      const float stripW = std::max(1.0f, std::min(natural, opt.maxW));
      const bool scrolls = natural > opt.maxW;

      if (!pages.empty())
      {
         ImGui::BeginChild("##strip", ImVec2(stripW, tok::control_h), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         if (scrolls && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * tok::page_wheel_step);
         // Taken before the chips' PushID(i): OpenPopup inside the loop would hash the chip's index into the ID
         // and the menu below (drawn outside the loop) would never open.
         const ImGuiID menuId = ImGui::GetID("##pageMenu");
         // Drag a chip to reorder: the others slide aside live (ReorderDrag), the move lands on release.
         int drawn = 0;
         for (int i : ReorderDrag::Order("INFINITE_PAGE_CHIP", (int)pages.size(), false))
         {
            if (drawn++ > 0)
               ImGui::SameLine(0.0f, tok::space_1);
            ReorderDrag::Flow(i);
            ImGui::PushID(i);
            const Entry& e = pages[i];
            const float w = ChipW(e);
            if (rn.index == i)
            {
               if (rn.focus)
                  ImGui::SetKeyboardFocusHere();
               ImGui::SetNextItemWidth(std::max(w, tok::page_rename_min_w));
               const bool done = FieldWell::InputText("##rename", rn.buf, sizeof rn.buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               PAGEPICKER_PROBE(probe.chipMn.resize(pages.size()); probe.chipMx.resize(pages.size()); probe.chipMn[i] = ImGui::GetItemRectMin(); probe.chipMx[i] = ImGui::GetItemRectMax();)
               const bool lost = !rn.focus && !ImGui::IsItemActive();
               rn.focus = false;
               if (done || lost)
               {
                  if (rn.buf[0] != 0 && e.name != rn.buf)
                  {
                     out.act = Act::Rename;
                     out.index = i;
                     out.name = rn.buf;
                  }
                  rn.index = -1;
               }
               ImGui::PopID();
               continue;
            }
            const bool on = i == current;
            ActionButton::Scoped() = on ? ActionButton::Kind::Selected : ActionButton::Kind::Plain;
            if (ActionButton::Draw("##chip", ImVec2(w, tok::control_h)) && !on)
            {
               out.act = Act::Select;
               out.index = i;
            }
            ActionButton::Scoped() = ActionButton::Kind::Plain;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               startRename(i);
            if (opt.canClose && ImGui::IsItemClicked(ImGuiMouseButton_Middle))
            {
               out.act = Act::Close;
               out.index = i;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
               rn.menu = i;
               ImGui::OpenPopup(menuId);
            }
            if (opt.reorder && pages.size() > 1)
               ReorderDrag::Item(i, e.name.c_str());
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            PAGEPICKER_PROBE(probe.chipMn.resize(pages.size()); probe.chipMx.resize(pages.size()); probe.chipMn[i] = bmin; probe.chipMx[i] = bmax;)
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float cy = (bmin.y + bmax.y) * 0.5f;
            const ImU32 col = on ? tok::Role(tok::col::ink_white) : ImGui::GetColorU32(ImGuiCol_Text);
            float tx = bmin.x + tok::space_3;
            if (e.color != 0)
            {
               dl->AddCircleFilled(ImVec2(tx + tok::page_dot * 0.5f, cy), tok::page_dot * 0.5f, U32(e.color), tok::page_picker_dot_segments);
               tx += tok::page_dot + tok::space_2;
            }
            if (e.glyph != nullptr)
            {
               glyph::Draw(dl, ImVec2(tx + tok::icon_control * 0.5f, cy), tok::icon_control * tok::icon_page_ratio, col, e.glyph);
               tx += tok::icon_control + tok::space_1;
            }
            dl->PushClipRect(bmin, ImVec2(bmax.x - tok::space_2, bmax.y), true);
            dl->AddText(ImVec2(tx, cy - ImGui::GetFontSize() * 0.5f), col, e.name.c_str());
            dl->PopClipRect();
            ImGui::PopID();
         }
         if (const ReorderDrag::Move mv = ReorderDrag::End())
         {
            out.act = Act::Move;
            out.index = mv.from;
            out.to = mv.to;
         }
         if (MenuParts::BeginPopup("##pageMenu"))
         {
            const int m = rn.menu;
            PAGEPICKER_PROBE(probe.menuOpen = true; probe.menuFor = m;)
            if (m >= 0 && m < (int)pages.size())
            {
               const char* renameKey = m == current ? kRenameKey : nullptr;
               const bool renameHit = MenuParts::Item(L("Rename"), renameKey);
               PAGEPICKER_PROBE(ProbeRowAdd(L("Rename"), renameKey, true);)
               if (renameHit)
                  startRename(m);
               const bool dupHit = MenuParts::Item(L("Duplicate"), nullptr, false, opt.duplicate);
               PAGEPICKER_PROBE(ProbeRowAdd(L("Duplicate"), nullptr, opt.duplicate);)
               if (dupHit)
               {
                  out.act = Act::Duplicate;
                  out.index = m;
               }
               const bool colourOpen = MenuParts::SubMenu(L("Colour"));
               PAGEPICKER_PROBE(ProbeRowAdd(L("Colour"), nullptr, true, " >");)
               if (colourOpen)
               {
                  ColourRow(pages[m], m, out);
                  ImGui::EndMenu();
               }
               const bool canLeft = opt.reorder && m > 0, canRight = opt.reorder && m + 1 < (int)pages.size();
               const bool leftHit = MenuParts::Item(L("Move Left"), nullptr, false, canLeft);
               PAGEPICKER_PROBE(ProbeRowAdd(L("Move Left"), nullptr, canLeft);)
               if (leftHit)
               {
                  out.act = Act::Move;
                  out.index = m;
                  out.to = m - 1;
               }
               const bool rightHit = MenuParts::Item(L("Move Right"), nullptr, false, canRight);
               PAGEPICKER_PROBE(ProbeRowAdd(L("Move Right"), nullptr, canRight);)
               if (rightHit)
               {
                  out.act = Act::Move;
                  out.index = m;
                  out.to = m + 1;
               }
               MenuParts::Separator();
               PAGEPICKER_PROBE(if (ProbeNow()) ProbeNow()->rows.push_back({ "---", "", {}, {}, false });)
               const bool delHit = MenuParts::Item(L("Delete Page"), nullptr, false, opt.canDelete);
               PAGEPICKER_PROBE(ProbeRowAdd(L("Delete Page"), nullptr, opt.canDelete);)
               if (delHit)
               {
                  out.act = Act::Delete;
                  out.index = m;
               }
            }
            MenuParts::EndPopup();
         }
         EndChildSafe();
      }
      {
         if (!pages.empty())
            ImGui::SameLine(0.0f, tok::space_1);
         ImGui::BeginDisabled(!opt.canAdd);
         if (ActionButton::Draw("##add", ImVec2(tok::control_h, tok::control_h)))
            out.act = Act::Add;
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            HelpTip("%s", addTip != nullptr ? addTip : T("New Page"));
         const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
         PAGEPICKER_PROBE(probe.addMn = a; probe.addMx = b; ProbeNow() = nullptr;)
         glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tok::icon_control, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Plus);
      }
      ImGui::PopID();
      return out;
   }
}

// PagePicker: the page strip every paged panel shares (the Performance grammar). One chip per page (colour dot,
// optional glyph, name clipped at kW), then +. Click switches, double-click renames in place, drag reorders,
// right-click: Rename, Duplicate, Colour, Delete Page. The strip scrolls sideways (wheel) when the pages outgrow
// maxW. The panel owns the pages: Draw only reports what was asked.
#pragma once
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
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

   constexpr float kW = 140.0f;  // widest chip: a long name clips
   constexpr float kMinW = 56.0f; // narrowest chip

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
      Move
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
      float maxW = 420.0f;    // the strip scrolls past this
      bool duplicate = true;  // Duplicate in the menu
      bool colour = true;     // Colour in the menu
      bool reorder = true;    // drag to reorder
      bool canDelete = true;  // Delete Page in the menu (a panel may need one page left)
      bool canAdd = true;     // the + chip
      const char* deleteLabel = nullptr;     // default "Delete Page"
      std::function<void(int)> extraMenu;    // more rows at the end of a chip's menu
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

   inline ImU32 U32(unsigned int rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }

   inline float ChipW(const Entry& e)
   {
      float w = ImGui::CalcTextSize(e.name.c_str()).x + 2.0f * tok::space_3;
      if (e.color != 0)
         w += 8.0f + tok::space_2;
      if (e.glyph != nullptr)
         w += tok::icon_md + tok::space_1;
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

   inline void ColourRow(const Entry& page, int index, Result& out)
   {
      // The clip colours of the Timeline, plus None.
      static const unsigned int kColours[] = { 0xE5534B, 0xF08C3A, 0xE8C547, 0x6CC46A, 0x3FB6A8, 0x4A8FE7, 0x8C6BE0, 0xD45DB0 };
      const float d = 16.0f;
      for (int c = 0; c < 8; c++)
      {
         if (c > 0)
            ImGui::SameLine(0.0f, tok::space_1);
         ImGui::PushID(c);
         const ImVec2 p = ImGui::GetCursorScreenPos();
         if (ImGui::InvisibleButton("##sw", ImVec2(d, d)))
         {
            out.act = Act::Recolor;
            out.index = index;
            out.color = kColours[c];
            ImGui::CloseCurrentPopup();
         }
         const bool on = page.color == kColours[c];
         ImDrawList* mdl = ImGui::GetWindowDrawList();
         mdl->AddCircleFilled(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f - (ImGui::IsItemHovered() || on ? 0.0f : 1.5f), U32(kColours[c]), 24);
         if (on)
            mdl->AddCircle(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f + 1.5f, ImGui::GetColorU32(ImGuiCol_Text), 24, 1.0f);
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

      float natural = 0.0f;
      for (size_t i = 0; i < pages.size(); i++)
         natural += ChipW(pages[i]) + (i > 0 ? tok::space_1 : 0.0f);
      const float stripW = std::max(1.0f, std::min(natural, opt.maxW));
      const bool scrolls = natural > opt.maxW;

      if (!pages.empty())
      {
         ImGui::BeginChild("##strip", ImVec2(stripW, tok::tile), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         if (scrolls && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 40.0f);
         for (int i = 0; i < (int)pages.size(); i++)
         {
            if (i > 0)
               ImGui::SameLine(0.0f, tok::space_1);
            ImGui::PushID(i);
            const Entry& e = pages[i];
            const float w = ChipW(e);
            if (rn.index == i)
            {
               if (rn.focus)
                  ImGui::SetKeyboardFocusHere();
               ImGui::SetNextItemWidth(std::max(w, 96.0f));
               const bool done = FieldWell::InputText("##rename", rn.buf, sizeof rn.buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
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
            if (ActionButton::Draw("##chip", ImVec2(w, tok::tile)) && !on)
            {
               out.act = Act::Select;
               out.index = i;
            }
            ActionButton::Scoped() = ActionButton::Kind::Plain;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               startRename(i);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
               rn.menu = i;
               ImGui::OpenPopup("##pageMenu");
            }
            if (opt.reorder && pages.size() > 1)
            {
               if (ImGui::BeginDragDropSource())
               {
                  ImGui::SetDragDropPayload("INFINITE_PAGE_CHIP", &i, sizeof i);
                  ImGui::TextUnformatted(e.name.c_str());
                  ImGui::EndDragDropSource();
               }
               if (ImGui::BeginDragDropTarget())
               {
                  if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("INFINITE_PAGE_CHIP"))
                  {
                     const int from = *static_cast<const int*>(p->Data);
                     if (from != i)
                     {
                        out.act = Act::Move;
                        out.index = from;
                        out.to = i;
                     }
                  }
                  ImGui::EndDragDropTarget();
               }
            }
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float cy = (bmin.y + bmax.y) * 0.5f;
            const ImU32 col = on ? tok::U32(tok::pal::c_FFFFFFFF) : ImGui::GetColorU32(ImGuiCol_Text);
            float tx = bmin.x + tok::space_3;
            if (e.color != 0)
            {
               dl->AddCircleFilled(ImVec2(tx + 4.0f, cy), 4.0f, U32(e.color), 24);
               tx += 8.0f + tok::space_2;
            }
            if (e.glyph != nullptr)
            {
               glyph::Draw(dl, ImVec2(tx + tok::icon_md * 0.5f, cy), tok::icon_md * 0.8f, col, e.glyph);
               tx += tok::icon_md + tok::space_1;
            }
            dl->PushClipRect(bmin, ImVec2(bmax.x - tok::space_2, bmax.y), true);
            dl->AddText(ImVec2(tx, cy - ImGui::GetFontSize() * 0.5f), col, e.name.c_str());
            dl->PopClipRect();
            ImGui::PopID();
         }
         if (MenuParts::BeginPopup("##pageMenu"))
         {
            const int m = rn.menu;
            if (m >= 0 && m < (int)pages.size())
            {
               if (MenuParts::Item(L("Rename"), m == current ? "Cmd+R" : nullptr))
                  startRename(m);
               if (opt.duplicate && MenuParts::Item(L("Duplicate")))
               {
                  out.act = Act::Duplicate;
                  out.index = m;
               }
               if (opt.colour && MenuParts::SubMenu(L("Colour")))
               {
                  ColourRow(pages[m], m, out);
                  ImGui::EndMenu();
               }
               if (opt.extraMenu)
                  opt.extraMenu(m);
               if (opt.canDelete)
               {
                  MenuParts::Separator();
                  if (MenuParts::Item(opt.deleteLabel != nullptr ? opt.deleteLabel : L("Delete Page")))
                  {
                     out.act = Act::Delete;
                     out.index = m;
                  }
               }
            }
            MenuParts::EndPopup();
         }
         ImGui::EndChild();
      }
      if (opt.canAdd)
      {
         if (!pages.empty())
            ImGui::SameLine(0.0f, tok::space_1);
         if (ActionButton::Draw("##add", ImVec2(tok::tile, tok::tile)))
            out.act = Act::Add;
         if (ImGui::IsItemHovered())
            HelpTip("%s", addTip != nullptr ? addTip : T("New Page"));
         const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
         glyph::Draw(ImGui::GetWindowDrawList(), ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), tok::icon_md, ImGui::GetColorU32(ImGuiCol_Text), IconsInfinite::Plus);
      }
      ImGui::PopID();
      return out;
   }
}

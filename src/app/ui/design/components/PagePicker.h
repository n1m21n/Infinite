// PagePicker: the page dropdown every paged panel shares (Piano Roll grammar). A fixed-width face shows the
// page's colour dot and name, clipped; the menu lists the pages, then New Page, Rename, Colour and Delete Page.
// Rename swaps the face for a text field in place. The panel owns the pages: Draw only reports what was asked.
#pragma once
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "app/AppCommon.h"
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/components/MenuParts.h"

namespace PagePicker
{
   using app::HelpTip;
   using app::L;
   using app::T;

   constexpr float kW = 140.0f; // one face width in every panel, so the top bar never shifts with a long name

   struct Entry
   {
      std::string name;
      unsigned int color = 0; // 0xRRGGBB, 0 = no colour
   };

   enum class Act
   {
      None,
      Select,
      Add,
      Rename,
      Recolor,
      Delete
   };

   struct Result
   {
      Act act = Act::None;
      int index = -1;         // the page acted on
      std::string name;       // Rename
      unsigned int color = 0; // Recolor
   };

   struct RenameState
   {
      int index = -1;
      bool focus = false;
      char buf[64] = {};
   };
   inline RenameState& Rename(ImGuiID id)
   {
      static std::unordered_map<ImGuiID, RenameState> m;
      return m[id];
   }

   inline ImU32 U32(unsigned int rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }

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

   inline Result Draw(const char* id, const std::vector<Entry>& pages, int current, const char* tip, float w = kW)
   {
      Result out;
      const ImGuiID key = ImGui::GetID(id);
      RenameState& rn = Rename(key);
      if (rn.index >= 0 && rn.index < (int)pages.size())
      {
         if (rn.focus)
            ImGui::SetKeyboardFocusHere();
         ImGui::SetNextItemWidth(w);
         const std::string fid = std::string(id) + "Rename";
         const bool done = FieldWell::InputText(fid.c_str(), rn.buf, sizeof rn.buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
         const bool lost = !rn.focus && !ImGui::IsItemActive();
         rn.focus = false;
         if (done || lost)
         {
            if (rn.buf[0] != 0 && pages[rn.index].name != rn.buf)
            {
               out.act = Act::Rename;
               out.index = rn.index;
               out.name = rn.buf;
            }
            rn.index = -1;
         }
         return out;
      }
      rn.index = -1;

      const bool has = current >= 0 && current < (int)pages.size();
      const bool clicked = ActionButton::Draw(id, ImVec2(w, tok::tile));
      if (tip != nullptr && ImGui::IsItemHovered())
         HelpTip("%s", tip);
      const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float cy = (bmin.y + bmax.y) * 0.5f;
      float tx = bmin.x + tok::space_3;
      if (has && pages[current].color != 0)
      {
         dl->AddCircleFilled(ImVec2(tx + 4.0f, cy), 4.0f, U32(pages[current].color), 24);
         tx += 8.0f + tok::space_2;
      }
      dl->PushClipRect(bmin, ImVec2(bmax.x - tok::space_2, bmax.y), true);
      dl->AddText(ImVec2(tx, cy - ImGui::GetFontSize() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), has ? pages[current].name.c_str() : T("No page"));
      dl->PopClipRect();

      const std::string menu = std::string(id) + "Menu";
      if (clicked)
         ImGui::OpenPopup(menu.c_str());
      ImGui::SetNextWindowPos(ImVec2(bmin.x, bmax.y + tok::space_1));
      ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(160.0f, w), 0.0f), ImVec2(280.0f, MenuParts::ListMaxH()));
      if (!MenuParts::BeginPopup(menu.c_str()))
         return out;
      for (int i = 0; i < (int)pages.size(); i++)
      {
         const std::string label = pages[i].name + "##pg" + std::to_string(i);
         if (MenuParts::Choice(label.c_str(), i == current) && i != current)
         {
            out.act = Act::Select;
            out.index = i;
         }
      }
      MenuParts::Separator();
      if (MenuParts::Item(L("New Page")))
         out.act = Act::Add;
      if (has)
      {
         if (MenuParts::Item(L("Rename"), "Cmd+R"))
            BeginRename(id, pages, current);
         if (MenuParts::SubMenu(L("Colour")))
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
                  out.index = current;
                  out.color = kColours[c];
                  ImGui::CloseCurrentPopup();
               }
               const bool on = pages[current].color == kColours[c];
               ImDrawList* mdl = ImGui::GetWindowDrawList();
               mdl->AddCircleFilled(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f - (ImGui::IsItemHovered() || on ? 0.0f : 1.5f), U32(kColours[c]), 24);
               if (on)
                  mdl->AddCircle(ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), d * 0.5f + 1.5f, ImGui::GetColorU32(ImGuiCol_Text), 24, 1.0f);
               ImGui::PopID();
            }
            if (MenuParts::Item(L("None"), nullptr, pages[current].color == 0))
            {
               out.act = Act::Recolor;
               out.index = current;
               out.color = 0;
            }
            ImGui::EndMenu();
         }
         MenuParts::Separator();
         if (MenuParts::Item(L("Delete Page")))
         {
            out.act = Act::Delete;
            out.index = current;
         }
      }
      MenuParts::EndPopup();
      return out;
   }
}

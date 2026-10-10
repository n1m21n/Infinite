// Templates: small starter patches shipped in Resources/templates (written by tools/templates/gen_templates.py), with
// a thumbnail each. Opening one makes an untitled copy: the shipped file is never the open document, so Save asks
// where to put it and the original can't be overwritten. The picker is File > Template library: a window like Settings,
// one card per template under its group heading (thumbnail, title, one line).
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/SectionCard.h"
#include "core/gl3.h"
#include "stb_image.h"
#include <map>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace app
{
namespace
{
struct Entry
{
   std::string slug, group, title, blurb;
};

std::vector<Entry>& Entries()
{
   static std::vector<Entry> sEntries;
   static bool sRead = false;
   if (!sRead)
   {
      sRead = true;
      std::ifstream in(BundledResourcePath("templates/index.txt"));
      std::string line;
      while (std::getline(in, line))
      {
         if (line.empty() || line[0] == '#')
            continue;
         std::vector<std::string> f;
         std::stringstream ss(line);
         std::string part;
         while (std::getline(ss, part, '|'))
            f.push_back(part);
         if (f.size() >= 4)
            sEntries.push_back({ f[0], f[1], f[2], f[3] });
      }
   }
   return sEntries;
}

void OpenEntry(const Entry& e)
{
   const std::string path = BundledResourcePath(("templates/" + e.slug + ".inf").c_str());
   GuardUnsavedChanges([path]() { OpenTemplate(path); });
}

}

namespace
{
   // Thumbnails decode once, on first sight, into a GL texture kept for the session. 0 = missing (card shows a blank tile).
   unsigned int ThumbTexture(const std::string& slug)
   {
      static std::map<std::string, unsigned int> sCache;
      auto it = sCache.find(slug);
      if (it != sCache.end())
         return it->second;
      unsigned int tex = 0;
      std::ifstream f(BundledResourcePath(("templates/thumbs/" + slug + ".png").c_str()), std::ios::binary | std::ios::ate);
      if (f)
      {
         const std::streamoff n = f.tellg();
         std::vector<unsigned char> bytes((size_t)std::max<std::streamoff>(n, 0));
         f.seekg(0, std::ios::beg);
         if (n > 0 && f.read(reinterpret_cast<char*>(bytes.data()), n))
         {
            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
            if (px != nullptr)
            {
               glGenTextures(1, &tex);
               glBindTexture(GL_TEXTURE_2D, tex);
               glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
               glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
               glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
               glBindTexture(GL_TEXTURE_2D, 0);
               stbi_image_free(px);
            }
         }
      }
      sCache[slug] = tex;
      return tex;
   }

   // One template card: thumbnail over title over the one-liner. Returns true when clicked.
   float CardHeight(float w)
   {
      const float textH = ImGui::GetTextLineHeight();
      return tok::space_2 + (w - 2.0f * tok::space_2) + tok::space_2 + textH + 2.0f + textH * 2.0f + tok::space_2;
   }

   bool Card(const Entry& e, float w)
   {
      const float thumb = w - 2.0f * tok::space_2;
      const float textH = ImGui::GetTextLineHeight();
      const float h = CardHeight(w);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(e.slug.c_str());
      const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec4 t = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, hovered ? 0.10f : 0.05f)), tok::radius_group);
      const ImVec2 t0(p.x + tok::space_2, p.y + tok::space_2);
      if (const unsigned int tex = ThumbTexture(e.slug); tex != 0)
         dl->AddImageRounded((ImTextureID)(intptr_t)tex, t0, ImVec2(t0.x + thumb, t0.y + thumb), ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32_WHITE, tok::radius_tile);
      else
         dl->AddRectFilled(t0, ImVec2(t0.x + thumb, t0.y + thumb), ImGui::GetColorU32(ImVec4(t.x, t.y, t.z, 0.06f)), tok::radius_tile);
      const float ty = t0.y + thumb + tok::space_2;
      dl->PushClipRect(ImVec2(p.x + tok::space_2, ty), ImVec2(p.x + w - tok::space_2, p.y + h), true);
      dl->AddText(ImVec2(p.x + tok::space_2, ty), ImGui::GetColorU32(ImGuiCol_Text), e.title.c_str());
      dl->AddText(nullptr, 0.0f, ImVec2(p.x + tok::space_2, ty + textH + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  e.blurb.c_str(), nullptr, w - 2.0f * tok::space_2);
      dl->PopClipRect();
      if (hovered)
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      return clicked;
   }
}

// File > Template library: a window like Settings. Groups in index.txt order, cards flowing left to right.
void DrawTemplatesWindow(bool* open)
{
   ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
   ImGui::SetNextWindowSize(ImVec2(740, 560), ImGuiCond_FirstUseEver);
   PushElevatedPanelStyle(/*isChild=*/false);
   FormParts::PushWindowPad();
   const bool visible = ImGui::Begin(L("Template library"), open, ImGuiWindowFlags_NoCollapse);
   FormParts::PopWindowPad();
   if (!visible)
   {
      ImGui::End();
      PopElevatedPanelStyle();
      return;
   }
   SectionCard::BeginWindow();
   ImGui::TextDisabled("%s", T("Opens as an untitled copy. The original is never changed."));
   ImGui::Spacing();

   constexpr float kCardW = 150.0f;
   const std::vector<Entry>& entries = Entries();
   const float avail = ImGui::GetContentRegionAvail().x;
   const float gap = ImGui::GetStyle().ItemSpacing.x;
   const int perRow = std::max(1, (int)((avail + gap) / (kCardW + gap)));
   const Entry* picked = nullptr;
   size_t i = 0;
   while (i < entries.size())
   {
      const std::string group = entries[i].group;
      SectionCard::Begin(T(group.c_str()));
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float cardH = CardHeight(kCardW);
      int col = 0;
      for (; i < entries.size() && entries[i].group == group; i++, col++)
      {
         ImGui::SetCursorScreenPos(ImVec2(origin.x + (float)(col % perRow) * (kCardW + gap),
                                          origin.y + (float)(col / perRow) * (cardH + gap)));
         if (Card(entries[i], kCardW))
            picked = &entries[i];
      }
      const int rows = (col + perRow - 1) / perRow;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (float)rows * (cardH + gap) - gap));
      ImGui::Dummy(ImVec2(0.0f, 0.0f));
   }
   SectionCard::End();
   SectionCard::EndWindow();
   ImGui::End();
   PopElevatedPanelStyle();
   if (picked != nullptr)
   {
      *open = false;
      OpenEntry(*picked);
   }
}
}

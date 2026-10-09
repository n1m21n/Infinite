// Templates: small starter patches shipped in Resources/templates (written by tools/templates/gen_templates.py), with
// a thumbnail each. Opening one makes an untitled copy: the shipped file is never the open document, so Save asks
// where to put it and the original can't be overwritten. The picker is the File > New from template submenu.
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/MenuParts.h"
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

// File > New from template: a submenu, one item per template under its group heading. No dialog.
void DrawTemplatesMenu()
{
   std::vector<Entry>& entries = Entries();
   if (!MenuParts::SubMenu(L("New from template"), !entries.empty()))
      return;
   const char* lastGroup = nullptr;
   for (Entry& e : entries)
   {
      if (lastGroup == nullptr || e.group != lastGroup)
      {
         if (lastGroup != nullptr)
            MenuParts::Separator();
         lastGroup = e.group.c_str();
         ImGui::TextDisabled("%s", T(e.group.c_str()));
      }
      if (MenuParts::Item(e.title.c_str()))
         OpenEntry(e);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("%s", e.blurb.c_str());
   }
   ImGui::EndMenu();
}
}

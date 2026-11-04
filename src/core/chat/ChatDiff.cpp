#include "ChatDiff.h"

#include <cstdlib>

namespace chat
{
   namespace
   {
      std::string Ref(const CatalogNode& n) { return "@" + n.title + "#" + std::to_string(n.index); }

      // Enums by option name, bools as on/off, numbers as saved.
      std::string Shown(const CatalogParam& p)
      {
         if (p.kind == "enum")
         {
            const int i = std::atoi(p.value.c_str());
            if (i >= 0 && i < (int)p.options.size())
               return p.options[(size_t)i];
         }
         if (p.kind == "bool")
            return p.value == "1" ? "on" : "off";
         return p.value;
      }

      // Text a code node holds: more than one line, or too long to read on one.
      bool IsCode(const CatalogParam& p) { return p.kind.empty() && (p.value.find('\n') != std::string::npos || p.value.size() > 60); }

      std::string End(const Snapshot& s, int node, int output)
      {
         const CatalogNode* n = s.catalog.Node(node);
         if (n == nullptr)
            return "#" + std::to_string(node);
         return Ref(*n) + (output > 0 ? ":" + std::to_string(output) : std::string());
      }

      bool Has(const std::vector<CatalogLink>& links, const CatalogLink& l)
      {
         for (const CatalogLink& o : links)
            if (o == l)
               return true;
         return false;
      }
   }

   std::vector<ChangeLine> Diff(const Snapshot& before, const Snapshot& after)
   {
      std::vector<ChangeLine> out;
      for (const CatalogNode& n : after.catalog.nodes)
         if (before.catalog.Node(n.index) == nullptr)
            out.push_back({ "added " + Ref(n) + (n.type != n.title ? " (" + n.type + ")" : std::string()), "" });
      for (const CatalogNode& n : before.catalog.nodes)
         if (after.catalog.Node(n.index) == nullptr)
            out.push_back({ "removed " + n.title, "" });

      for (const CatalogNode& n : after.catalog.nodes)
      {
         const CatalogNode* was = before.catalog.Node(n.index);
         if (was == nullptr)
            continue;
         if (was->title != n.title)
            out.push_back({ "renamed " + was->title + " to " + Ref(n), "" });
         for (const CatalogParam& p : n.params)
         {
            const CatalogParam* old = nullptr;
            for (const CatalogParam& q : was->params)
               if (q.key == p.key)
                  old = &q;
            if (old == nullptr)
               continue;
            const std::string at = Ref(n) + "." + p.key;
            if (p.modulated != old->modulated)
               out.push_back({ at + (p.modulated ? " modulated" : " no longer modulated"), "" });
            if (p.expression != old->expression)
               out.push_back({ at + (p.expression ? " follows a formula" : " no longer follows a formula"), "" });
            if (p.value == old->value)
               continue;
            if (IsCode(p) || IsCode(*old))
               out.push_back({ at + " changed", p.value });
            else
               out.push_back({ at + " " + Shown(*old) + " → " + Shown(p), "" });
         }
      }

      for (const CatalogLink& l : after.links)
         if (!Has(before.links, l))
            out.push_back({ "connected " + End(after, l.src, l.srcOutput) + " → " + End(after, l.dst, l.dstSlot > 0 ? l.dstSlot : 0), "" });
      for (const CatalogLink& l : before.links)
         if (!Has(after.links, l) && after.catalog.Node(l.src) != nullptr && after.catalog.Node(l.dst) != nullptr)
            out.push_back({ "disconnected " + End(after, l.src, l.srcOutput) + " → " + End(after, l.dst, l.dstSlot > 0 ? l.dstSlot : 0), "" });
      return out;
   }
}

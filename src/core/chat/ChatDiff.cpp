#include "ChatDiff.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chat
{
   namespace
   {
      std::string Ref(const CatalogNode& n) { return "@" + n.title + "#" + std::to_string(n.index); }

      std::string Shown(const CatalogParam& p) { return ShownValue(p); }

      // Bookkeeping a node saves next to its params (a Field node's param id map), never a change to read.
      bool Internal(const CatalogParam& p) { return p.key.rfind("field_", 0) == 0; }

      // Saved text keeps newlines, tabs and quotes escaped; a code block shows the code itself.
      std::string Unescape(const std::string& t)
      {
         std::string out;
         out.reserve(t.size());
         for (size_t i = 0; i < t.size(); i++)
         {
            if (t[i] != '\\' || i + 1 >= t.size())
            {
               out += t[i];
               continue;
            }
            const char c = t[++i];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
         }
         return out;
      }

      // Text a code node holds: more than one line, or too long to read on one.
      bool IsCode(const CatalogParam& p)
      {
         return p.kind.empty() && (p.value.find('\n') != std::string::npos || p.value.find("\\n") != std::string::npos || p.value.size() > 60);
      }

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

   std::string Drift(const ChangeLine& line, const Catalog& now)
   {
      if (line.node < 0 || line.key.empty())
         return {};
      const CatalogNode* n = now.Node(line.node);
      const CatalogParam* p = n != nullptr ? n->Find(line.key) : nullptr;
      if (p == nullptr || p->modulated || p->expression || p->value == line.after)
         return {};
      char* endA = nullptr;
      char* endB = nullptr;
      const double a = std::strtod(line.after.c_str(), &endA);
      const double b = std::strtod(p->value.c_str(), &endB);
      const bool numbers = endA != line.after.c_str() && *endA == '\0' && endB != p->value.c_str() && *endB == '\0';
      if (numbers && std::abs(a - b) <= 1e-4 * std::max(1.0, std::abs(a)))
         return {};
      return ShownValue(*p);
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
            if (old == nullptr || Internal(p))
               continue;
            const std::string at = Ref(n) + "." + p.key;
            if (p.modulated != old->modulated)
               out.push_back({ at + (p.modulated ? " modulated" : " no longer modulated"), "" });
            if (p.expression != old->expression)
               out.push_back({ at + (p.expression ? " follows a formula" : " no longer follows a formula"), "" });
            if (p.value == old->value)
               continue;
            if (IsCode(p) || IsCode(*old))
               out.push_back({ at + " changed", p.value.find('\n') == std::string::npos ? Unescape(p.value) : p.value });
            else
               out.push_back({ at + " " + Shown(*old) + " → " + Shown(p), "", n.index, p.key, p.value });
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

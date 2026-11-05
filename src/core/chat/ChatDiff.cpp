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

      // The same node in the other snapshot: by its uid when both know it (an undo or a reload can number
      // nodes again, so index 7 may now be another node), else by index.
      const CatalogNode* Same(const Snapshot& s, const CatalogNode& n)
      {
         if (n.uid != 0)
            for (const CatalogNode& o : s.catalog.nodes)
               if (o.uid == n.uid)
                  return &o;
         const CatalogNode* o = s.catalog.Node(n.index);
         return o != nullptr && (o->uid == 0 || n.uid == 0) ? o : nullptr;
      }

      // A link in the other snapshot's numbering, through the uids of its ends; as is when they are unknown.
      CatalogLink Renumber(const CatalogLink& l, const Snapshot& from, const Snapshot& to)
      {
         CatalogLink r = l;
         const CatalogNode* a = from.catalog.Node(l.src);
         const CatalogNode* b = from.catalog.Node(l.dst);
         const CatalogNode* a2 = a != nullptr ? Same(to, *a) : nullptr;
         const CatalogNode* b2 = b != nullptr ? Same(to, *b) : nullptr;
         r.src = a2 != nullptr ? a2->index : (a != nullptr && a->uid != 0 ? -1 : l.src);
         r.dst = b2 != nullptr ? b2->index : (b != nullptr && b->uid != 0 ? -1 : l.dst);
         return r;
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
         if (Same(before, n) == nullptr)
            out.push_back({ "added " + Ref(n) + (n.type != n.title ? " (" + n.type + ")" : std::string()), "" });
      for (const CatalogNode& n : before.catalog.nodes)
         if (Same(after, n) == nullptr)
            out.push_back({ "removed " + n.title, "" });

      for (const CatalogNode& n : after.catalog.nodes)
      {
         const CatalogNode* was = Same(before, n);
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
            if (old == nullptr || IsInternal(p))
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
         if (!Has(before.links, Renumber(l, after, before)))
            out.push_back({ "connected " + End(after, l.src, l.srcOutput) + " → " + End(after, l.dst, l.dstSlot > 0 ? l.dstSlot : 0), "" });
      for (const CatalogLink& b : before.links)
      {
         const CatalogLink l = Renumber(b, before, after);  // in the after numbering
         if (!Has(after.links, l) && after.catalog.Node(l.src) != nullptr && after.catalog.Node(l.dst) != nullptr)
            out.push_back({ "disconnected " + End(after, l.src, l.srcOutput) + " → " + End(after, l.dst, l.dstSlot > 0 ? l.dstSlot : 0), "" });
      }
      return out;
   }
}

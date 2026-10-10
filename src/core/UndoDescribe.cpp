#include "core/UndoDescribe.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace UndoDescribe
{
namespace
{
   std::string NodeKey(const Patch::NodeRecord& n) { return n.uid != 0 ? "u" + std::to_string(n.uid) : "i" + std::to_string(n.index); }

   // 0.2000 -> "0.2", 0.55 -> "0.55"; anything that is not a plain number is shortened text.
   std::string Show(const std::string& v)
   {
      char* end = nullptr;
      const double d = std::strtod(v.c_str(), &end);
      if (!v.empty() && end != nullptr && *end == '\0')
      {
         char buf[32];
         std::snprintf(buf, sizeof buf, "%.2f", d);
         std::string s = buf;
         while (s.size() > 1 && s.back() == '0')
            s.pop_back();
         if (!s.empty() && s.back() == '.')
            s.pop_back();
         return s;
      }
      return v.size() > 18 ? v.substr(0, 17) + "..." : v;
   }

   std::string Plural(size_t n, const char* one, const char* many)
   {
      return n == 1 ? std::string(one) : std::to_string(n) + " " + many;
   }

   size_t Differences(const std::vector<Patch::CableRecord>& a, const std::vector<Patch::CableRecord>& b, size_t& added, size_t& removed)
   {
      auto key = [](const Patch::CableRecord& c) {
         return std::to_string(c.dstIndex) + ":" + std::to_string(c.dstSlot) + "<" + std::to_string(c.srcIndex) + "." + std::to_string(c.srcOutput);
      };
      std::set<std::string> sa, sb;
      for (const auto& c : a) sa.insert(key(c));
      for (const auto& c : b) sb.insert(key(c));
      for (const auto& k : sb) if (!sa.count(k)) added++;
      for (const auto& k : sa) if (!sb.count(k)) removed++;
      return added + removed;
   }
}

std::string Change(const Patch::Data& before, const Patch::Data& after)
{
   std::map<std::string, const Patch::NodeRecord*> was, now;
   for (const auto& n : before.nodes) was[NodeKey(n)] = &n;
   for (const auto& n : after.nodes) now[NodeKey(n)] = &n;

   std::vector<const Patch::NodeRecord*> added, removed;
   for (const auto& kv : now) if (!was.count(kv.first)) added.push_back(kv.second);
   for (const auto& kv : was) if (!now.count(kv.first)) removed.push_back(kv.second);
   if (!added.empty() && removed.empty())
      return added.size() == 1 ? "Add " + added[0]->typeName : "Add " + std::to_string(added.size()) + " nodes";
   if (!removed.empty() && added.empty())
      return removed.size() == 1 ? "Delete " + removed[0]->typeName : "Delete " + std::to_string(removed.size()) + " nodes";
   if (!added.empty() && !removed.empty())
      return "Replace nodes";

   size_t cAdd = 0, cRem = 0;
   Differences(before.cables, after.cables, cAdd, cRem);
   Differences(before.geometry, after.geometry, cAdd, cRem);
   Differences(before.audio, after.audio, cAdd, cRem);
   Differences(before.notes, after.notes, cAdd, cRem);
   if (cAdd > 0 && cRem == 0) return "Connect " + Plural(cAdd, "cable", "cables");
   if (cRem > 0 && cAdd == 0) return "Disconnect " + Plural(cRem, "cable", "cables");
   if (cAdd > 0 && cRem > 0) return "Rewire cable";

   if (before.modulation.size() != after.modulation.size())
      return after.modulation.size() > before.modulation.size() ? "Add modulation" : "Remove modulation";

   // Per-node changes.
   size_t paramChanges = 0, bypassChanges = 0, moved = 0;
   std::string oneKey, oneOld, oneNew, oneType, bypassType;
   bool bypassOn = false;
   std::set<std::string> paramNodes;
   for (const auto& kv : now)
   {
      const Patch::NodeRecord& a = *was[kv.first];
      const Patch::NodeRecord& b = *kv.second;
      if (a.bypassed != b.bypassed)
      {
         bypassChanges++;
         bypassType = b.typeName;
         bypassOn = b.bypassed;
      }
      std::map<std::string, std::string> pa;
      for (const auto& p : a.params) pa[p.first] = p.second;
      for (const auto& p : b.params)
      {
         auto it = pa.find(p.first);
         if (it == pa.end() || it->second != p.second)
         {
            paramChanges++;
            paramNodes.insert(kv.first);
            oneKey = p.first;
            oneOld = it == pa.end() ? "" : it->second;
            oneNew = p.second;
            oneType = b.typeName;
         }
      }
      if (a.x != b.x || a.y != b.y)
         moved++;
   }
   if (paramChanges == 1)
   {
      if (oneKey.size() > 2 && oneKey[1] == ' ')
         oneKey.erase(0, 2);
      return oneKey + " " + Show(oneOld) + " \xE2\x86\x92 " + Show(oneNew);
   }
   if (paramChanges > 1)
      return paramNodes.size() == 1 ? "Edit " + oneType : "Edit " + std::to_string(paramChanges) + " settings";
   if (bypassChanges > 0)
      return bypassChanges == 1 ? (bypassOn ? "Bypass " : "Enable ") + bypassType : "Bypass " + std::to_string(bypassChanges) + " nodes";
   if (before.modulation.size() == after.modulation.size() && !before.modulation.empty())
   {
      for (size_t i = 0; i < before.modulation.size(); i++)
         if (before.modulation[i].depth != after.modulation[i].depth || before.modulation[i].centre != after.modulation[i].centre ||
             before.modulation[i].polarity != after.modulation[i].polarity)
            return "Edit modulation";
   }
   if (before.expressions.size() != after.expressions.size())
      return "Edit expression";
   for (size_t i = 0; i < before.expressions.size(); i++)
      if (before.expressions[i].text != after.expressions[i].text)
         return "Edit expression";
   if (moved > 0)
      return moved == 1 ? "Move node" : "Move " + std::to_string(moved) + " nodes";
   if (before.streams.size() != after.streams.size() || before.markers.size() != after.markers.size())
      return "Edit timeline";
   return "Edit";
}
}

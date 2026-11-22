#pragma once
// Which nodes reach an Output or an Audio Out, from patch text (Patch::WriteText / a canonical .inf).
// Shared by `infinite patch check` (CLI) and the app's live `check` RPC, so both judge a patch the same way.
// Header-only: the CLI and the app are separate targets with explicit source lists.
#include "json.hpp"
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace PatchReach
{
   // [{node, type}] for every node with no path to an Output / Audio Out (Comments excluded).
   inline nlohmann::json UnreachableNodes(const std::string& text)
   {
      std::map<int, std::string> types;
      std::multimap<int, int> feeders;
      std::istringstream in(text);
      std::string line;
      while (std::getline(in, line))
      {
         std::istringstream words(line);
         std::string tag;
         words >> tag;
         if (tag == "node")
         {
            int index = -1;
            std::string category, type;
            words >> index >> category;
            std::getline(words, type);
            type.erase(0, type.find_first_not_of(' '));
            while (!type.empty() && (type.back() == '\r' || type.back() == ' '))
               type.pop_back();
            types[index] = type;
         }
         else if (tag == "cable" || tag == "geo" || tag == "aud" || tag == "note")
         {
            // All four are "dst slot src output".
            int dst = -1, slot = 0, src = -1;
            words >> dst >> slot >> src;
            feeders.insert({ dst, src });
         }
         else if (tag == "mod")
         {
            int dst = -1, param = 0, src = -1;
            words >> dst >> param >> src;
            feeders.insert({ dst, src });
         }
      }
      std::set<int> reached;
      std::vector<int> open;
      for (const auto& kv : types)
         if (kv.second == "Output" || kv.second == "Audio Out")
            open.push_back(kv.first);
      while (!open.empty())
      {
         const int at = open.back();
         open.pop_back();
         if (!reached.insert(at).second)
            continue;
         const auto range = feeders.equal_range(at);
         for (auto it = range.first; it != range.second; ++it)
            open.push_back(it->second);
      }
      nlohmann::json out = nlohmann::json::array();
      for (const auto& kv : types)
         if (reached.count(kv.first) == 0 && kv.second != "Comment")
            out.push_back({ { "node", kv.first }, { "type", kv.second } });
      return out;
   }
}

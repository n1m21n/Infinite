#include "PatchSchema.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace PatchSchema
{
   namespace
   {
      std::string Lower(std::string s)
      {
         for (char& c : s)
            c = (char)std::tolower((unsigned char)c);
         return s;
      }

      int EditDistance(const std::string& a, const std::string& b)
      {
         std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
         for (size_t j = 0; j <= b.size(); j++)
            prev[j] = (int)j;
         for (size_t i = 1; i <= a.size(); i++)
         {
            cur[0] = (int)i;
            for (size_t j = 1; j <= b.size(); j++)
               cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
            std::swap(prev, cur);
         }
         return prev[b.size()];
      }

      std::string Num(float v)
      {
         char buf[48];
         std::snprintf(buf, sizeof(buf), "%.9g", v);
         return buf;
      }

      std::string Q(const std::string& s) { return "\"" + Headless::JsonEscape(s) + "\""; }

      std::string Join(const std::vector<std::string>& v)
      {
         std::string s;
         for (size_t i = 0; i < v.size(); i++)
            s += (i ? ", " : "") + v[i];
         return s;
      }

      Headless::Issue Make(const char* code, const std::string& msg, int line, int node, const std::string& hint = "")
      {
         Headless::Issue is;
         is.code = code;
         is.message = msg;
         is.line = line;
         is.node = node;
         is.hint = hint;
         return is;
      }
   }

   std::string ParamKindName(char kind)
   {
      switch (kind)
      {
      case 'f': return "float";
      case 'i': return "int";
      case 'b': return "bool";
      case 'c': return "color";
      case 's': return "text";
      }
      return "unknown";
   }

   std::vector<std::string> Nearest(const std::string& word, const std::vector<std::string>& pool, size_t n)
   {
      const std::string w = Lower(word);
      std::vector<std::pair<int, std::string>> scored;
      for (const std::string& p : pool)
      {
         const std::string lp = Lower(p);
         int d = EditDistance(w, lp);
         // A name that merely contains the word is a better hint than its distance says.
         if (!w.empty() && lp.find(w) != std::string::npos)
            d = std::min(d, 1 + (int)(lp.size() - w.size()) / 4);
         scored.push_back({ d, p });
      }
      std::stable_sort(scored.begin(), scored.end(),
                       [](const auto& a, const auto& b) { return a.first < b.first; });
      std::vector<std::string> out;
      for (size_t i = 0; i < scored.size() && i < n; i++)
         out.push_back(scored[i].second);
      return out;
   }

   std::string ToJson(const TypeSchema& t)
   {
      std::string s = "{\"type\":" + Q(t.name) + ",\"category\":" + Q(t.category);
      s += ",\"hardware_driven\":" + std::string(t.hardwareDriven ? "true" : "false");
      s += ",\"params\":[";
      for (size_t i = 0; i < t.params.size(); i++)
      {
         const ParamInfo& p = t.params[i];
         s += (i ? "," : "") + std::string("{\"key\":") + Q(p.key) + ",\"tag\":\"" + std::string(1, p.kind) +
              "\",\"kind\":" + Q(ParamKindName(p.kind)) + ",\"default\":" + Q(p.def) + "}";
      }
      s += "],\"inputs\":[";
      for (size_t i = 0; i < t.inputs.size(); i++)
      {
         const SlotInfo& in = t.inputs[i];
         s += (i ? "," : "") + std::string("{\"slot\":") + std::to_string(in.slot) + ",\"kind\":" + Q(in.kind) +
              ",\"label\":" + Q(in.label) + "}";
      }
      s += "],\"outputs\":[";
      for (size_t i = 0; i < t.outputs.size(); i++)
         s += (i ? "," : "") + std::string("{\"output\":") + std::to_string(i) + ",\"kind\":" + Q(t.outputs[i].kind) +
              ",\"label\":" + Q(t.outputs[i].label) + "}";
      s += "],\"modulatable\":[";
      for (size_t i = 0; i < t.modulatable.size(); i++)
      {
         const ModulatableInfo& m = t.modulatable[i];
         s += (i ? "," : "") + std::string("{\"index\":") + std::to_string(m.index) + ",\"label\":" + Q(m.label) +
              ",\"min\":" + Num(m.minValue) + ",\"max\":" + Num(m.maxValue) + ",\"step\":" + Num(m.step);
         if (m.isBool)
            s += ",\"bool\":true";
         if (m.isEnum)
         {
            s += ",\"enum\":[";
            for (size_t k = 0; k < m.enumOptions.size(); k++)
               s += (k ? "," : "") + Q(m.enumOptions[k]);
            s += "]";
         }
         s += "}";
      }
      s += "]}";
      return s;
   }

   void Validate(const Patch::Data& data, const Env& env, std::vector<Headless::Issue>& errors,
                 std::vector<Headless::Issue>& warnings)
   {
      std::map<int, const Patch::NodeRecord*> byIndex;
      std::map<uint64_t, int> uidSeen;
      for (const Patch::NodeRecord& n : data.nodes)
      {
         if (!byIndex.insert({ n.index, &n }).second)
            errors.push_back(Make("E_DUPLICATE_INDEX", "node index " + std::to_string(n.index) + " is used twice",
                                  n.line, n.index, "give every node its own index"));
         if (n.uid != 0 && !uidSeen.insert({ n.uid, n.index }).second)
            warnings.push_back(Make("W_DUPLICATE_UID", "uid " + std::to_string(n.uid) + " is shared by two nodes", n.line, n.index));

         const TypeSchema* t = env.schema(n.typeName);
         if (t == nullptr)
         {
            errors.push_back(Make("E_UNKNOWN_TYPE", "unknown node type '" + n.typeName + "'", n.line, n.index,
                                  "did you mean: " + Join(Nearest(n.typeName, env.allTypes, 3))));
            continue;
         }
         std::set<std::string> seenKeys;
         std::vector<std::string> keys;
         for (const ParamInfo& p : t->params)
            keys.push_back(p.key);
         for (size_t i = 0; i < n.params.size(); i++)
         {
            const std::string& full = n.params[i].first; // "f radius"
            const int line = i < n.paramLines.size() ? n.paramLines[i] : n.line;
            if (full.size() < 3)
               continue;
            const char kind = full[0];
            const std::string key = full.substr(2);
            const ParamInfo* found = nullptr;
            for (const ParamInfo& p : t->params)
               if (p.key == key)
                  found = &p;
            if (found == nullptr)
            {
               // `uid`-style keys owned by dynamic nodes are not in a fresh node's list; still worth a warning.
               const std::vector<std::string> near = Nearest(key, keys, 1);
               warnings.push_back(Make("W_UNKNOWN_PARAM", "'" + key + "' is not a parameter of " + t->name, line, n.index,
                                       near.empty() ? "" : "did you mean '" + near.front() + "'?"));
            }
            else if (found->kind != kind)
               warnings.push_back(Make("W_TYPE_MISMATCH",
                                       "'" + key + "' is written as " + ParamKindName(kind) + " but " + t->name +
                                          " reads it as " + ParamKindName(found->kind),
                                       line, n.index, std::string("write it with the '") + found->kind + "' tag"));
         }
      }

      // Edges for reachability and cycle checks: from -> to (data flows from -> to).
      std::multimap<int, int> flowsTo;
      auto known = [&](int index) { return byIndex.count(index) != 0; };
      auto typeOf = [&](int index) { return byIndex.at(index)->typeName; };

      auto checkCables = [&](const std::vector<Patch::CableRecord>& list, const char* tag)
      {
         for (const Patch::CableRecord& c : list)
         {
            if (!known(c.dstIndex) || !known(c.srcIndex))
            {
               errors.push_back(Make("E_DANGLING",
                                     std::string(tag) + " line refers to node " +
                                        std::to_string(!known(c.dstIndex) ? c.dstIndex : c.srcIndex) + " which does not exist",
                                     c.line, !known(c.dstIndex) ? c.dstIndex : c.srcIndex));
               continue;
            }
            flowsTo.insert({ c.srcIndex, c.dstIndex });
            if (!env.schema(typeOf(c.dstIndex)) || !env.schema(typeOf(c.srcIndex)) || !env.link)
               continue;
            // The line's own tag must match the kind of slot it lands on
            // (an `aud` line into an image slot is a kind mismatch, not a bad slot).
            const TypeSchema* dstSchema = env.schema(typeOf(c.dstIndex));
            const SlotInfo* slotInfo = nullptr;
            for (const SlotInfo& in : dstSchema->inputs)
               if (in.slot == c.dstSlot)
                  slotInfo = &in;
            if (slotInfo != nullptr)
            {
               const std::string t = tag;
               const bool tagOk = (t == "cable" && slotInfo->kind == "image") ||
                                  (t == "aud" && slotInfo->kind == "audio") ||
                                  (t == "note" && slotInfo->kind == "note") ||
                                  (t == "geo" && (slotInfo->kind == "geometry" || slotInfo->kind == "modulator"));
               if (!tagOk)
               {
                  static const std::map<std::string, std::string> kTagFor = {
                     { "image", "cable" }, { "audio", "aud" }, { "note", "note" }, { "geometry", "geo" }, { "modulator", "geo" } };
                  errors.push_back(Make("E_KIND_MISMATCH",
                                        typeOf(c.dstIndex) + " slot " + std::to_string(c.dstSlot) + " takes " + slotInfo->kind +
                                           ", but the line is '" + t + "'",
                                        c.line, c.dstIndex, "use a '" + kTagFor.at(slotInfo->kind) + "' line for that slot"));
                  continue;
               }
            }
            const Link r = env.link(typeOf(c.srcIndex), c.srcOutput, typeOf(c.dstIndex), c.dstSlot);
            if (r == Link::BadSlot)
               errors.push_back(Make("E_BAD_SLOT",
                                     typeOf(c.dstIndex) + " has no input slot " + std::to_string(c.dstSlot), c.line, c.dstIndex,
                                     "run Infinite --describe \"" + typeOf(c.dstIndex) + "\" for its slots"));
            else if (r == Link::KindMismatch)
               errors.push_back(Make("E_KIND_MISMATCH",
                                     std::string(tag) + " from " + typeOf(c.srcIndex) + " cannot plug into slot " +
                                        std::to_string(c.dstSlot) + " of " + typeOf(c.dstIndex),
                                     c.line, c.dstIndex, "the source's output kind does not match that slot's kind"));
         }
      };
      checkCables(data.cables, "cable");
      checkCables(data.geometry, "geo");
      checkCables(data.audio, "aud");
      checkCables(data.notes, "note");

      for (const Patch::ModRecord& m : data.modulation)
      {
         if (!known(m.dstIndex) || !known(m.srcIndex))
            errors.push_back(Make("E_DANGLING", "mod line refers to a node that does not exist", m.line,
                                  !known(m.dstIndex) ? m.dstIndex : m.srcIndex));
         else
            flowsTo.insert({ m.srcIndex, m.dstIndex });
      }
      for (const Patch::ExprRecord& e : data.expressions)
         if (!known(e.dstIndex))
            errors.push_back(Make("E_DANGLING", "expr line refers to node " + std::to_string(e.dstIndex) + " which does not exist",
                                  e.line, e.dstIndex));
      for (const Patch::PaletteRecord& p : data.palette)
      {
         if (!known(p.dstIndex) || !known(p.srcIndex))
            errors.push_back(Make("E_DANGLING", "pal line refers to a node that does not exist", 0, !known(p.dstIndex) ? p.dstIndex : p.srcIndex));
         else
            flowsTo.insert({ p.srcIndex, p.dstIndex });
      }

      // Audio and note feedback: the engine refuses these loops, so an authored file must not contain one.
      auto findCycle = [&](const std::vector<Patch::CableRecord>& list, const char* what)
      {
         std::map<int, std::vector<int>> adj;
         for (const Patch::CableRecord& c : list)
            if (known(c.srcIndex) && known(c.dstIndex))
               adj[c.srcIndex].push_back(c.dstIndex);
         std::map<int, int> state; // 0 new, 1 on stack, 2 done
         bool reported = false;
         std::function<void(int)> dfs = [&](int u)
         {
            state[u] = 1;
            for (int v : adj[u])
            {
               if (state[v] == 1 && !reported)
               {
                  reported = true;
                  errors.push_back(Make("E_CYCLE", std::string("the ") + what + " cables form a feedback loop through node " +
                                                      std::to_string(v), 0, v, "break the loop with a delay node or remove a cable"));
               }
               else if (state[v] == 0)
                  dfs(v);
            }
            state[u] = 2;
         };
         for (const auto& kv : adj)
            if (state[kv.first] == 0)
               dfs(kv.first);
      };
      findCycle(data.audio, "audio");
      findCycle(data.notes, "note");

      // Terminals: an Output or the audio master. Everything that cannot reach one does nothing.
      std::vector<int> terminals;
      int outputs = 0;
      for (const Patch::NodeRecord& n : data.nodes)
      {
         if (n.typeName == "Output")
            outputs++;
         if (n.typeName == "Output" || n.typeName == "Audio Out")
            terminals.push_back(n.index);
      }
      if (outputs == 0)
      {
         if (env.forRender)
            errors.push_back(Make("E_NO_OUTPUT", "the patch has no Output node", 0, -1, "add a node 'Utility Output' and cable your image into it"));
         else
            warnings.push_back(Make("W_NO_OUTPUT", "the patch has no Output node, so it cannot be rendered", 0, -1));
      }
      std::multimap<int, int> flowsFrom;
      for (const auto& kv : flowsTo)
         flowsFrom.insert({ kv.second, kv.first });
      std::set<int> reaches(terminals.begin(), terminals.end());
      std::vector<int> stack = terminals;
      while (!stack.empty())
      {
         const int u = stack.back();
         stack.pop_back();
         auto range = flowsFrom.equal_range(u);
         for (auto it = range.first; it != range.second; ++it)
            if (reaches.insert(it->second).second)
               stack.push_back(it->second);
      }
      for (const Patch::NodeRecord& n : data.nodes)
         if (!reaches.count(n.index) && env.schema(n.typeName) != nullptr)
            warnings.push_back(Make("W_UNUSED_NODE", n.typeName + " does not reach an Output or Audio Out", n.line, n.index));
   }
}

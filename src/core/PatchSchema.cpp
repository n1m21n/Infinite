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
      s += ",\"can_bypass\":" + std::string(t.canBypass ? "true" : "false");
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
         if (n.bypassed && !t->canBypass)
         {
            Headless::Issue is = Make("W_BYPASS_IGNORED",
                                      t->name + " cannot be bypassed (it has more than one input, or it is an instrument with no pass-through), so bypassed=1 is ignored and the node stays on",
                                      n.line, n.index, "remove the node or its cables to switch it off; only single-input nodes can be bypassed");
            warnings.push_back(is);
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
         std::map<std::pair<int, int>, int> slotLine; // (dst, slot) -> line of the first cable
         for (const Patch::CableRecord& c : list)
         {
            // Two cables into one slot: the later one silently replaces the first.
            if (known(c.dstIndex))
            {
               auto ins = slotLine.insert({ { c.dstIndex, c.dstSlot }, c.line });
               if (!ins.second)
               {
                  errors.push_back(Make("E_SLOT_TAKEN",
                                        typeOf(c.dstIndex) + " slot " + std::to_string(c.dstSlot) + " already has a cable (line " +
                                           std::to_string(ins.first->second) + "); this one (line " + std::to_string(c.line) +
                                           ") would replace it",
                                        c.line, c.dstIndex, "an input takes one cable; one output can feed many inputs, so fan out from the source, or merge with a Blend/Mixer"));
                  continue;
               }
            }
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
               const bool tagOk = (t == "cable" && (slotInfo->kind == "image" || slotInfo->kind == "environment")) ||
                                  (t == "aud" && slotInfo->kind == "audio") ||
                                  (t == "note" && slotInfo->kind == "note") ||
                                  (t == "geo" && (slotInfo->kind == "geometry" || slotInfo->kind == "modulator" ||
                                                  slotInfo->kind == "camera" || slotInfo->kind == "light" ||
                                                  slotInfo->kind == "palette"));
               if (!tagOk)
               {
                  static const std::map<std::string, std::string> kTagFor = {
                     { "image", "cable" }, { "environment", "cable" }, { "audio", "aud" }, { "note", "note" }, { "geometry", "geo" }, { "modulator", "geo" },
                     { "camera", "geo" }, { "light", "geo" }, { "palette", "geo" } };
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
         {
            flowsTo.insert({ m.srcIndex, m.dstIndex });
            const TypeSchema* srcSchema = env.schema(typeOf(m.srcIndex));
            if (srcSchema != nullptr)
            {
               const bool ok = m.srcOutput >= 0 && m.srcOutput < (int)srcSchema->outputs.size() &&
                               srcSchema->outputs[m.srcOutput].modulator;
               if (!ok)
               {
                  std::string which;
                  for (size_t o = 0; o < srcSchema->outputs.size(); o++)
                     if (srcSchema->outputs[o].modulator)
                        which += (which.empty() ? "" : ", ") + std::to_string(o);
                  errors.push_back(Make("E_NOT_A_MODULATOR",
                                        typeOf(m.srcIndex) + " output " + std::to_string(m.srcOutput) + " is not a modulator, so it cannot drive a parameter",
                                        m.line, m.dstIndex,
                                        which.empty() ? "use an LFO, Envelope, Macro, Path or an analyzer output as the mod source"
                                                      : "that node's modulator outputs are: " + which));
               }
            }
         }
      }
      CheckParamIndices(data, env, errors);
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

      // Image and geometry loops. The cook hands a node its own last-frame
      // texture when it meets it again, so a loop of plain nodes is a hidden
      // one-frame delay whose result depends on cook order. A Feedback node
      // is the sanctioned delay: edges leaving one do not count. Modulator-
      // input pins (Math, Smooth...) ride on `geo` lines too; those loops are
      // a different case and not reported here.
      {
         std::map<int, std::vector<int>> adj;
         auto addEdges = [&](const std::vector<Patch::CableRecord>& list)
         {
            for (const Patch::CableRecord& c : list)
            {
               if (!known(c.srcIndex) || !known(c.dstIndex) || typeOf(c.srcIndex) == "Feedback")
                  continue;
               const TypeSchema* d = env.schema(typeOf(c.dstIndex));
               bool modSlot = false;
               if (d != nullptr)
                  for (const SlotInfo& in : d->inputs)
                     if (in.slot == c.dstSlot && in.kind == "modulator")
                        modSlot = true;
               if (!modSlot)
                  adj[c.srcIndex].push_back(c.dstIndex);
            }
         };
         addEdges(data.cables);
         addEdges(data.geometry);
         std::map<int, int> state;
         std::vector<int> path;
         std::set<int> inReported;
         std::function<void(int)> dfs = [&](int u)
         {
            state[u] = 1;
            path.push_back(u);
            for (int v : adj[u])
            {
               if (state[v] == 1)
               {
                  std::string names;
                  bool on = false;
                  bool fresh = false;
                  for (int n : path)
                  {
                     if (n == v)
                        on = true;
                     if (on)
                     {
                        names += typeOf(n) + " " + std::to_string(n) + " -> ";
                        fresh = inReported.insert(n).second || fresh;
                     }
                  }
                  if (fresh)
                     warnings.push_back(Make("W_IMAGE_CYCLE",
                                             "these nodes feed each other with no Feedback node in the loop: " + names + typeOf(v) + " " + std::to_string(v),
                                             0, v, "the loop becomes a hidden one-frame delay whose result depends on cook order; put a Feedback node in it"));
               }
               else if (state[v] == 0)
                  dfs(v);
            }
            path.pop_back();
            state[u] = 2;
         };
         for (const auto& kv : adj)
            if (state[kv.first] == 0)
               dfs(kv.first);
      }

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

      // Cables per destination, by slot, over every cable kind.
      std::map<int, std::set<int>> filled;
      for (const auto* list : { &data.cables, &data.geometry, &data.audio, &data.notes })
         for (const Patch::CableRecord& c : *list)
            if (known(c.srcIndex) && known(c.dstIndex))
               filled[c.dstIndex].insert(c.dstSlot);

      // A merge node with an empty input does not fail, it dims: the missing
      // side counts as transparent black (or silence). Only where it matters,
      // that is on a node that reaches an Output.
      struct MergeRule { const char* type; size_t need; const char* what; };
      static const MergeRule kMerge[] = { { "Blend", 2, "both A and B" },
                                          { "Blend Audio", 2, "both A and B" },
                                          { "Layer Stack", 1, "at least its base layer" },
                                          { "Mixer", 1, "at least one input" } };
      for (const Patch::NodeRecord& n : data.nodes)
      {
         if (!reaches.count(n.index))
            continue;
         for (const MergeRule& r : kMerge)
            if (n.typeName == r.type && filled[n.index].size() < r.need)
               warnings.push_back(Make("W_OPEN_INPUT",
                                       n.typeName + " needs " + r.what + " connected; " + std::to_string(filled[n.index].size()) +
                                          " of its inputs " + (filled[n.index].size() == 1 ? "is" : "are") + " wired, so the empty side counts as transparent black or silence",
                                       n.line, n.index, "cable something into every input, or use a single-input node instead"));
      }

      // An Output whose image pin is empty renders a blank frame without complaint.
      for (const Patch::NodeRecord& n : data.nodes)
      {
         if (n.typeName != "Output")
            continue;
         bool imageWired = false;
         for (const Patch::CableRecord& c : data.cables)
            if (c.dstIndex == n.index && c.dstSlot == 0 && known(c.srcIndex))
               imageWired = true;
         if (!imageWired)
            warnings.push_back(Make("W_OUTPUT_EMPTY", "Output " + std::to_string(n.index) + " has nothing cabled into its image input, so every frame is blank",
                                    n.line, n.index, "add a 'cable " + std::to_string(n.index) + " 0 <source node>' line"));
      }
   }

   void CheckParamIndices(const Patch::Data& data, const Env& env, std::vector<Headless::Issue>& errors)
   {
      if (!env.maxParamIndex)
         return;
      std::map<int, std::string> typeOfIndex;
      for (const Patch::NodeRecord& n : data.nodes)
         typeOfIndex[n.index] = n.typeName;
      auto check = [&](int dst, int param, int line, const char* what)
      {
         auto it = typeOfIndex.find(dst);
         if (it == typeOfIndex.end())
            return;
         const int max = env.maxParamIndex(it->second);
         if (max == -2)
            return;
         if (param < 0 || param > max)
            errors.push_back(Make("E_BAD_PARAM",
                                  std::string(what) + " line targets parameter " + std::to_string(param) + " but " + it->second +
                                     (max < 0 ? " has no modulatable parameters" : " only has parameters 0.." + std::to_string(max)),
                                  line, dst, "run Infinite --describe \"" + it->second + "\" for the modulatable indices"));
      };
      for (const Patch::ModRecord& m : data.modulation)
         check(m.dstIndex, m.dstParam, m.line, "mod");
      for (const Patch::ExprRecord& e : data.expressions)
         check(e.dstIndex, e.dstParam, e.line, "expr");
   }
}

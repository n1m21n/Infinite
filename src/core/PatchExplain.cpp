#include "PatchExplain.h"

#include <cstdio>
#include <cstdlib>
#include <set>

#include "HeadlessJob.h"

namespace
{
   using PatchExplain::Env;

   std::string Num(float v)
   {
      char b[32];
      std::snprintf(b, sizeof(b), "%.6g", (double)v);
      return b;
   }

   std::string Label(const Patch::Data& d, int index)
   {
      for (const Patch::NodeRecord& n : d.nodes)
         if (n.index == index)
         {
            std::string s = n.typeName;
            if (!n.id.empty())
               s += " \"" + n.id + "\"";
            return s + " (" + std::to_string(index) + ")";
         }
      return "? (" + std::to_string(index) + ")";
   }

   std::string TypeOf(const Patch::Data& d, int index)
   {
      for (const Patch::NodeRecord& n : d.nodes)
         if (n.index == index)
            return n.typeName;
      return std::string();
   }

   // The saved key when the type was probed, else the UI label, else the raw index.
   std::string ParamName(const Env& env, int node, int param)
   {
      std::string k = env.paramKey ? env.paramKey(node, param) : std::string();
      if (!k.empty())
         return k;
      std::string l = env.paramLabel ? env.paramLabel(node, param) : std::string();
      return l.empty() ? "param " + std::to_string(param) : l;
   }

   std::string BareKey(const std::string& key)
   {
      const size_t sp = key.find(' ');
      return sp == std::string::npos ? key : key.substr(sp + 1);
   }

   // Saved floats carry 9 digits ("0.600000024"); the text form rounds them for reading.
   std::string Pretty(const PatchExplain::Param& p)
   {
      if (!p.option.empty())
         return p.option + " (" + p.value + ")";
      if (p.key.empty() || (p.key[0] != 'f' && p.key[0] != 'c'))
         return p.value;
      std::string out;
      size_t i = 0;
      while (i < p.value.size())
      {
         const size_t e = p.value.find(' ', i);
         const std::string tok = p.value.substr(i, e == std::string::npos ? std::string::npos : e - i);
         char b[32];
         std::snprintf(b, sizeof(b), "%.6g", std::atof(tok.c_str()));
         out += (out.empty() ? "" : " ") + std::string(b);
         if (e == std::string::npos)
            break;
         i = e + 1;
      }
      return out;
   }
}

namespace PatchExplain
{
   Explanation Build(const Patch::Data& data, const Env& env)
   {
      Explanation e;
      std::set<int> wired;
      for (const Patch::NodeRecord& n : data.nodes)
      {
         Node out;
         out.index = n.index;
         out.type = n.typeName;
         out.id = env.idOf ? env.idOf(n.index) : n.id;
         out.bypassed = n.bypassed;
         std::map<std::string, std::string> def;
         if (env.defaultParams)
            def = env.defaultParams(n.typeName);
         for (const auto& kv : n.params)
         {
            Param p;
            p.key = kv.first;
            p.value = kv.second;
            auto it = def.find(kv.first);
            p.isDefault = it != def.end() && it->second == kv.second;
            if (env.optionName && kv.first.rfind("i ", 0) == 0)
               p.option = env.optionName(n.typeName, BareKey(kv.first), std::atoi(kv.second.c_str()));
            out.params.push_back(std::move(p));
         }
         e.nodes.push_back(std::move(out));
      }
      // Ids come from the file, not the live graph; fall back to the record's own.
      for (Node& n : e.nodes)
         if (n.id.empty())
            for (const Patch::NodeRecord& r : data.nodes)
               if (r.index == n.index)
                  n.id = r.id;

      auto label = [&](int index)
      {
         for (const Node& n : e.nodes)
            if (n.index == index)
            {
               std::string s = n.type;
               if (!n.id.empty())
                  s += " \"" + n.id + "\"";
               return s + " (" + std::to_string(index) + ")";
            }
         return Label(data, index);
      };

      auto cables = [&](const std::vector<Patch::CableRecord>& list, const char* kind)
      {
         for (const Patch::CableRecord& c : list)
         {
            const std::string dstType = TypeOf(data, c.dstIndex);
            std::string slot = env.slotName ? env.slotName(dstType, c.dstSlot, kind) : std::string();
            std::string t = std::string(kind) + " " + label(c.dstIndex) + " slot " + std::to_string(c.dstSlot);
            if (!slot.empty())
               t += " [" + slot + "]";
            t += " <- " + label(c.srcIndex);
            if (c.srcOutput != 0)
               t += " out " + std::to_string(c.srcOutput);
            e.relations.push_back({ kind, t });
            wired.insert(c.dstIndex);
            wired.insert(c.srcIndex);
         }
      };
      // A key a binding writes holds whatever the binding last wrote, not an authored value.
      auto drive = [&](int node, int param, const char* by)
      {
         const std::string key = env.paramKey ? env.paramKey(node, param) : std::string();
         if (key.empty())
            return;
         for (Node& n : e.nodes)
            if (n.index == node)
               for (Param& p : n.params)
                  if (BareKey(p.key) == key)
                  {
                     p.driven = by;
                     p.isDefault = false;
                  }
      };

      cables(data.cables, "cable");
      cables(data.geometry, "geo");
      cables(data.audio, "aud");
      cables(data.notes, "note");

      for (const Patch::ModRecord& m : data.modulation)
      {
         float lo = m.lo, hi = m.hi;
         if (env.resolveMod)
            env.resolveMod(m, lo, hi);
         std::string t = "mod " + label(m.dstIndex) + " " + ParamName(env, m.dstIndex, m.dstParam) + " <- " +
                         label(m.srcIndex) + " out " + std::to_string(m.srcOutput) +
                         (m.polarity == 1 ? ", bipolar" : ", absolute") + ", depth " + Num(m.depth) +
                         ", range " + Num(lo) + ".." + Num(hi) + (m.enabled ? "" : ", DISABLED");
         e.relations.push_back({ "mod", t });
         drive(m.dstIndex, m.dstParam, "mod");
         wired.insert(m.dstIndex);
         wired.insert(m.srcIndex);
      }
      for (const Patch::PaletteRecord& p : data.palette)
      {
         e.relations.push_back({ "pal", "pal " + label(p.dstIndex) + " colour " + std::to_string(p.dstColor) + " <- " +
                                            label(p.srcIndex) + " swatch " + std::to_string(p.srcSwatch) });
         wired.insert(p.dstIndex);
         wired.insert(p.srcIndex);
      }
      for (const Patch::ExprRecord& x : data.expressions)
      {
         e.relations.push_back({ "expr", "expr " + label(x.dstIndex) + " " + ParamName(env, x.dstIndex, x.dstParam) +
                                             " = " + x.text });
         drive(x.dstIndex, x.dstParam, "expr");
         wired.insert(x.dstIndex);
      }
      for (const Node& n : e.nodes)
         if (!wired.count(n.index))
            e.unconnected.push_back(label(n.index));
      return e;
   }

   std::string ToText(const Explanation& e, bool all)
   {
      std::string s = "Nodes (" + std::to_string(e.nodes.size()) + ")\n";
      for (const Node& n : e.nodes)
      {
         s += "  " + n.type;
         if (!n.id.empty())
            s += " \"" + n.id + "\"";
         s += " (" + std::to_string(n.index) + ")";
         if (n.bypassed)
            s += "   bypassed";
         s += "\n";
         for (const Param& p : n.params)
         {
            if (p.isDefault && !all)
               continue;
            s += "    " + BareKey(p.key) + " " + Pretty(p) +
                 (!p.driven.empty() ? "   (live value, driven by " + p.driven + ")" : p.isDefault ? "   (default)" : "") + "\n";
         }
      }
      s += "Relations (" + std::to_string(e.relations.size()) + ")\n";
      for (const Relation& r : e.relations)
         s += "  " + r.text + "\n";
      s += "Unconnected:";
      if (e.unconnected.empty())
         s += " none";
      for (size_t i = 0; i < e.unconnected.size(); i++)
         s += (i ? ", " : " ") + e.unconnected[i];
      return s + "\n";
   }

   std::string ToJson(const Explanation& e)
   {
      using Headless::JsonEscape;
      std::string s = "{\"nodes\":[";
      for (size_t i = 0; i < e.nodes.size(); i++)
      {
         const Node& n = e.nodes[i];
         s += (i ? "," : "") + std::string("{\"index\":") + std::to_string(n.index) + ",\"type\":\"" + JsonEscape(n.type) +
              "\",\"id\":\"" + JsonEscape(n.id) + "\",\"bypassed\":" + (n.bypassed ? "true" : "false") + ",\"params\":[";
         for (size_t k = 0; k < n.params.size(); k++)
         {
            const Param& p = n.params[k];
            s += (k ? "," : "") + std::string("{\"key\":\"") + JsonEscape(p.key) + "\",\"value\":\"" + JsonEscape(p.value) +
                 "\",\"default\":" + (p.isDefault ? "true" : "false");
            if (!p.option.empty())
               s += ",\"option\":\"" + JsonEscape(p.option) + "\"";
            if (!p.driven.empty())
               s += ",\"driven\":\"" + p.driven + "\"";
            s += "}";
         }
         s += "]}";
      }
      s += "],\"relations\":[";
      for (size_t i = 0; i < e.relations.size(); i++)
         s += (i ? "," : "") + std::string("{\"kind\":\"") + e.relations[i].kind + "\",\"text\":\"" +
              JsonEscape(e.relations[i].text) + "\"}";
      s += "],\"unconnected\":[";
      for (size_t i = 0; i < e.unconnected.size(); i++)
         s += (i ? "," : "") + std::string("\"") + JsonEscape(e.unconnected[i]) + "\"";
      return s + "]}";
   }
}

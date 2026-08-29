#include "PatchJson.h"

using json = nlohmann::json;

namespace PatchJson
{
   json ToJson(const Patch::Data& data)
   {
      json out;

      out["nodes"] = json::array();
      for (const Patch::NodeRecord& n : data.nodes)
      {
         json jn;
         jn["index"] = n.index;
         jn["category"] = n.category;
         jn["typeName"] = n.typeName;
         jn["x"] = n.x;
         jn["y"] = n.y;
         jn["showParams"] = n.showParams;
         jn["bypassed"] = n.bypassed;
         jn["showMiniViewport"] = n.showMiniViewport;
         jn["showAdvancedParams"] = n.showAdvancedParams;
         jn["params"] = json::object();
         for (const auto& p : n.params)
            jn["params"][p.first] = p.second;
         out["nodes"].push_back(std::move(jn));
      }

      auto cablesToJson = [](const std::vector<Patch::CableRecord>& cables)
      {
         json arr = json::array();
         for (const Patch::CableRecord& c : cables)
            arr.push_back({ {"dstIndex", c.dstIndex}, {"dstSlot", c.dstSlot},
                             {"srcIndex", c.srcIndex}, {"srcOutput", c.srcOutput} });
         return arr;
      };
      out["cables"] = cablesToJson(data.cables);
      out["geometry"] = cablesToJson(data.geometry);
      out["audio"] = cablesToJson(data.audio);
      out["notes"] = cablesToJson(data.notes);

      out["modulation"] = json::array();
      for (const Patch::ModRecord& m : data.modulation)
         out["modulation"].push_back({ {"dstIndex", m.dstIndex}, {"dstParam", m.dstParam},
                                        {"srcIndex", m.srcIndex}, {"srcOutput", m.srcOutput},
                                        {"polarity", m.polarity}, {"depth", m.depth},
                                        {"centre", m.centre}, {"lo", m.lo}, {"hi", m.hi},
                                        {"hasRange", m.hasRange}, {"enabled", m.enabled} });

      out["palette"] = json::array();
      for (const Patch::PaletteRecord& p : data.palette)
         out["palette"].push_back({ {"dstIndex", p.dstIndex}, {"dstColor", p.dstColor},
                                     {"srcIndex", p.srcIndex}, {"srcSwatch", p.srcSwatch} });

      out["expressions"] = json::array();
      for (const Patch::ExprRecord& e : data.expressions)
         out["expressions"].push_back({ {"dstIndex", e.dstIndex}, {"dstParam", e.dstParam},
                                         {"text", e.text} });

      out["globals"] = json::array();
      for (const Patch::GlobalRecord& g : data.globals)
         out["globals"].push_back({ {"name", g.name}, {"expr", g.expr} });

      out["perfLayout"] = {
         {"cellSize", data.perfLayout.cellSize},
         {"pageCount", data.perfLayout.pageCount},
         {"pageNames", data.perfLayout.pageNames}
      };

      out["performance"] = json::array();
      for (const Patch::PerfRecord& p : data.performance)
      {
         json tgts = json::array();
         for (const auto& t : p.targets)
            tgts.push_back({ {"dstIndex", t.dstIndex}, {"dstParam", t.dstParam}, {"boolName", t.boolName} });
         json tgtsY = json::array();
         for (const auto& t : p.targetsY)
            tgtsY.push_back({ {"dstIndex", t.dstIndex}, {"dstParam", t.dstParam}, {"boolName", t.boolName} });

         out["performance"].push_back({
            {"kind", p.kind},
            {"dstIndex", p.dstIndex},
            {"dstParam", p.dstParam},
            {"dstParam2", p.dstParam2},
            {"cellX", p.cellX},
            {"cellY", p.cellY},
            {"page", p.page},
            {"colorR", p.colorR},
            {"colorG", p.colorG},
            {"colorB", p.colorB},
            {"value", p.value},
            {"value2", p.value2},
            {"boolName", p.boolName},
            {"label", p.label},
            {"targets", tgts},
            {"targetsY", tgtsY}
         });
      }

      return out;
   }
}

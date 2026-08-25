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
         jn["showPreview"] = n.showPreview;
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
                                        {"centre", m.centre} });

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

      if (data.settings.present)
      {
         const Patch::SceneSettings& s = data.settings;
         out["settings"] = {
            {"audioOutputDeviceId", s.audioOutputDeviceId}, {"audioInputDeviceId", s.audioInputDeviceId},
            {"audioSampleRate", s.audioSampleRate}, {"audioBufferFrames", s.audioBufferFrames},
            {"audioOversample", s.audioOversample}, {"targetFps", s.targetFps}, {"vsync", s.vsync},
            {"snapToGrid", s.snapToGrid}, {"gridSnap", s.gridSnap}, {"zoomSensitivity", s.zoomSensitivity},
            {"minimapEnabled", s.minimapEnabled}, {"minimapCorner", s.minimapCorner},
            {"minimapSize", s.minimapSize}, {"minimapOpacity", s.minimapOpacity},
            {"nodePanelOpen", s.nodePanelOpen}, {"nodePanelWidth", s.nodePanelWidth},
            {"viewportPanelDock", s.viewportPanelDock},
            {"viewportPanelWidth", s.viewportPanelWidth}, {"viewportPanelHeight", s.viewportPanelHeight},
            {"themePreset", s.themePreset}, {"diagnosticLog", s.diagnosticLog},
            {"autosaveEnabled", s.autosaveEnabled}, {"autosaveSeconds", s.autosaveSeconds}
         };
      }

      return out;
   }
}

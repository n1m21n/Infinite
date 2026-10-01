#include "HeadlessNotes.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace Headless
{
   namespace
   {
      bool ReadJson(const std::string& path, nlohmann::json& out, std::string& error)
      {
         std::ifstream f(path, std::ios::binary);
         if (!f)
         {
            error = "could not open " + path;
            return false;
         }
         std::stringstream ss;
         ss << f.rdbuf();
         out = nlohmann::json::parse(ss.str(), nullptr, false);
         if (out.is_discarded())
         {
            error = path + " is not valid JSON";
            return false;
         }
         return true;
      }

      double NumberOr(const nlohmann::json& obj, const std::string& key, double fallback)
      {
         if (key.empty() || !obj.is_object())
            return fallback;
         auto it = obj.find(key);
         return (it != obj.end() && it->is_number()) ? it->get<double>() : fallback;
      }
   }

   bool LoadNoteSchedule(const std::string& eventsPath, const std::string& mapPath, NoteSchedule& out, std::string& error)
   {
      nlohmann::json events, map;
      if (!ReadJson(eventsPath, events, error) || !ReadJson(mapPath, map, error))
         return false;
      if (events.is_object() && events.contains("events"))
         events = events["events"];
      if (!events.is_array())
      {
         error = eventsPath + " must be an array of events (or an object with an \"events\" array)";
         return false;
      }
      if (!map.is_object())
      {
         error = mapPath + " must be an object mapping event types to note targets";
         return false;
      }

      for (auto it = map.begin(); it != map.end(); ++it)
         if (!it.value().is_object() || !it.value().contains("node"))
         {
            error = mapPath + ": \"" + it.key() + "\" needs an object with a \"node\"";
            return false;
         }

      for (const nlohmann::json& ev : events)
      {
         if (!ev.is_object() || !ev.contains("t") || !ev["t"].is_number())
         {
            error = eventsPath + ": every event needs a numeric \"t\" in seconds";
            return false;
         }
         const std::string type = ev.value("type", std::string());
         auto m = map.find(type);
         if (m == map.end())
         {
            if (std::find(out.unmappedTypes.begin(), out.unmappedTypes.end(), type) == out.unmappedTypes.end())
               out.unmappedTypes.push_back(type);
            continue;
         }
         const nlohmann::json& e = m.value();
         NoteHit h;
         h.t = ev["t"].get<double>();
         h.node = e["node"].is_string() ? e["node"].get<std::string>() : std::to_string(e["node"].get<long long>());
         h.pitch = (int)std::clamp(NumberOr(e, "pitch", 60.0), 0.0, 127.0);
         h.velocity = (float)std::clamp(NumberOr(ev, e.value("velocity_from", std::string()), NumberOr(e, "velocity", 1.0)), 0.0, 1.0);
         h.length = std::max(0.001, NumberOr(ev, e.value("length_from", std::string()), NumberOr(e, "length", 0.25)));
         if (e.contains("pan_from") && std::find(out.panIgnoredNodes.begin(), out.panIgnoredNodes.end(), h.node) == out.panIgnoredNodes.end())
            out.panIgnoredNodes.push_back(h.node);
         if (std::find(out.nodes.begin(), out.nodes.end(), h.node) == out.nodes.end())
            out.nodes.push_back(h.node);
         out.hits.push_back(h);
      }
      std::stable_sort(out.hits.begin(), out.hits.end(), [](const NoteHit& a, const NoteHit& b) { return a.t < b.t; });
      return true;
   }
}

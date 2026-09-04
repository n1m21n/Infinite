#include "FieldDevice.h"

#include "json.hpp"

#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace Field
{
   std::string ToJsonString(const DeviceFile& device)
   {
      json out;
      out["format"] = "infdev";
      out["version"] = device.version;

      json meta;
      meta["name"] = device.name;
      meta["author"] = device.author;
      meta["description"] = device.description;
      meta["domain"] = device.domain;
      out["meta"] = meta;

      json dev;
      dev["code"] = device.code;
      json params = json::object();
      for (const auto& kv : device.params)
         params[kv.first] = kv.second;
      dev["params"] = params;
      json settings = json::object();
      for (const auto& kv : device.nodeSettings)
         settings[kv.first] = kv.second;
      dev["nodeSettings"] = settings;
      out["device"] = dev;

      return out.dump(2);
   }

   bool FromJsonString(const std::string& jsonText, DeviceFile& outDevice, std::string& outError)
   {
      json parsed;
      try
      {
         parsed = json::parse(jsonText);
      }
      catch (const std::exception& e)
      {
         outError = std::string("malformed JSON: ") + e.what();
         return false;
      }

      if (!parsed.is_object() || !parsed.contains("format") || parsed["format"] != "infdev")
      {
         outError = "not an .infdev file (missing/wrong \"format\" field)";
         return false;
      }

      DeviceFile result;
      result.version = parsed.value("version", 1);

      if (parsed.contains("meta") && parsed["meta"].is_object())
      {
         const json& meta = parsed["meta"];
         result.name = meta.value("name", std::string());
         result.author = meta.value("author", std::string());
         result.description = meta.value("description", std::string());
         result.domain = meta.value("domain", std::string());
      }

      if (parsed.contains("device") && parsed["device"].is_object())
      {
         const json& dev = parsed["device"];
         result.code = dev.value("code", std::string());
         if (dev.contains("params") && dev["params"].is_object())
         {
            for (auto it = dev["params"].begin(); it != dev["params"].end(); ++it)
            {
               if (it.value().is_number())
                  result.params[it.key()] = it.value().get<float>();
            }
         }
         if (dev.contains("nodeSettings") && dev["nodeSettings"].is_object())
         {
            for (auto it = dev["nodeSettings"].begin(); it != dev["nodeSettings"].end(); ++it)
            {
               if (it.value().is_number())
                  result.nodeSettings[it.key()] = it.value().get<double>();
            }
         }
      }

      outDevice = std::move(result);
      return true;
   }

   bool SaveToInfdevFile(const std::string& path, const DeviceFile& device)
   {
      std::ofstream file(path, std::ios::binary);
      if (!file)
         return false;
      file << ToJsonString(device);
      return (bool)file;
   }

   bool LoadFromInfdevFile(const std::string& path, DeviceFile& outDevice, std::string& outError)
   {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
         outError = "could not open file: " + path;
         return false;
      }
      std::ostringstream ss;
      ss << file.rdbuf();
      return FromJsonString(ss.str(), outDevice, outError);
   }
}

#pragma once

// Field build step 17: the ".field" (and legacy ".infdev") portable device file -
// one Field program plus its param values plus a small set of node-specific settings,
// saved/loaded/exported/imported independently of any .inf patch.
//
// This is a serialization-only type plus free functions - it has no
// knowledge of any concrete node class. Each Field node type (FieldElementNode,
// FieldPixelNode, FieldSampleNode, FieldGraphNode, FormulaNode) owns its own
// ToDeviceFile()/LoadDeviceFile() pair that reads/writes its own live fields
// into/out of a DeviceFile - the same "format is dumb, the node owns its
// data" split Patch.h's writer/reader already uses.

#include <map>
#include <string>

namespace Field
{
   struct DeviceFile
   {
      int version = 1;
      std::string name;
      std::string author;
      std::string description;
      std::string domain; // "element" | "pixel" | "sample" | "graph" | "formula"
      std::string code;
      std::map<std::string, float> params;        // Field `param` name -> value
      std::map<std::string, double> nodeSettings;  // domain-specific, small (see plan §2); int/bool stored as double
   };

   // Pure (de)serialization - no INode knowledge, no file I/O.
   std::string ToJsonString(const DeviceFile& device);
   // Returns false and fills outError on malformed JSON or a missing/wrong
   // "format" field. Never throws.
   bool FromJsonString(const std::string& jsonText, DeviceFile& outDevice, std::string& outError);

   // File I/O wrappers - the only place this file touches disk.
   bool SaveToFieldFile(const std::string& path, const DeviceFile& device);
   bool LoadFromFieldFile(const std::string& path, DeviceFile& outDevice, std::string& outError);

   // Backward-compatible aliases
   inline bool SaveToInfdevFile(const std::string& path, const DeviceFile& device) { return SaveToFieldFile(path, device); }
   inline bool LoadFromInfdevFile(const std::string& path, DeviceFile& outDevice, std::string& outError) { return LoadFromFieldFile(path, outDevice, outError); }
}

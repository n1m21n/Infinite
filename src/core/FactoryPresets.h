#pragma once
// Factory presets for audio nodes (v0.6 2.4) and Material: one table keyed by node type.
// A preset is a list of saved-param keys and values laid over the type's
// defaults and applied through Patch::LoadParams, the same path a patch load
// takes, so a preset can set anything a saved patch can and nothing else.
#include <string>
#include <utility>
#include <vector>

class INode;

namespace FactoryPresets
{
   struct Preset
   {
      std::string name;
      std::vector<std::pair<std::string, std::string>> values;
   };

   // Node types that have presets, in table order.
   const std::vector<std::string>& Types();
   // nullptr when the type has none.
   const std::vector<Preset>* For(const std::string& typeName);
   // Resets the node to its type's defaults, then lays the preset over them.
   // False when the type or index is unknown.
   bool Apply(INode* node, const std::string& typeName, int index);
}

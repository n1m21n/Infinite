#pragma once

#include <string>
#include <vector>

// Declarative description of one filter-node "type". FilterNode (one C++ class)
// is instantiated once per FilterDef, so adding a new effect/color-adjustment
// module is a table entry, not a new class - this is what makes "give every
// Affinity effect its own module with its own params" tractable, mirroring how
// Bespoke's EffectChain hosts many effect kinds behind one module shell.
struct FilterParamDef
{
   enum class Type
   {
      Float,
      Int,
      Bool,
      Color // 3-component RGB
   };

   std::string label; // shown in the params panel
   std::string uniformName; // GLSL uniform this drives
   Type type = Type::Float;
   float minVal = 0.0f;
   float maxVal = 1.0f;
   float defaultVal[3] = { 0.0f, 0.0f, 0.0f };
};

struct FilterDef
{
   std::string name; // node-factory key / spawn menu label
   std::string category; // "Effects" or "Color"
   std::string fragmentBody; // extra uniform decls + main(), appended after the shared preamble
   std::vector<FilterParamDef> params;
};

const std::vector<FilterDef>& GetFilterDefs();

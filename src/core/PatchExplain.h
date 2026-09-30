#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Patch.h"

// PatchExplain: reads back what a patch actually built (docs/fix-briefs/headless-engine.md, 3.3b).
//
// The input is the LIVE graph as Patch::Data (main.cpp's BuildPatchData after the load), so it
// shows what ApplyPatchData produced, not what the file said. Everything that needs the running
// app (default values, slot names, resolved modulation ranges) comes in through Env callbacks,
// which keeps this file free of GL/ImGui and unit-testable.
//
// Text form: a node section, then one line per wire under "Relations". Each relation line starts
// with the record tag that wrote it (cable/geo/aud/note/mod/pal/expr), so a count of those lines
// equals a count of the same tags in the saved file.
namespace PatchExplain
{
   struct Env
   {
      // Params a fresh node of the type saves, key ("f radius") -> value. Empty = unknown.
      std::function<std::map<std::string, std::string>(const std::string& type)> defaultParams;
      // Name of an input slot of the type, or "" when unknown.
      std::function<std::string(const std::string& type, int slot, const std::string& kind)> slotName;
      // Label of a control on a live node, or "" when unknown.
      std::function<std::string(int nodeIndex, int paramIndex)> paramLabel;
      // Saved key of that control ("sizeX"), or "" when the type was never probed.
      std::function<std::string(int nodeIndex, int paramIndex)> paramKey;
      // Option name a dropdown key shows for `value`, or "" when it is not a dropdown.
      std::function<std::string(const std::string& type, const std::string& key, int value)> optionName;
      // The range a mod binding writes into after ResolvedSourceFor; false = use the record.
      std::function<bool(const Patch::ModRecord&, float& lo, float& hi)> resolveMod;
      // Authoring `id` of a node, or "".
      std::function<std::string(int nodeIndex)> idOf;
   };

   struct Param
   {
      std::string key; // "f radius"
      std::string value;
      std::string option; // dropdown option name for an `i` key, when known
      std::string driven; // "mod" / "expr" when a binding writes this key: value is then the live one
      bool isDefault = false;
   };
   struct Node
   {
      int index = 0;
      std::string type, id;
      bool bypassed = false;
      std::vector<Param> params;
   };
   struct Relation
   {
      std::string kind; // cable geo aud note mod pal expr
      std::string text; // the whole line, without indent
   };
   struct Explanation
   {
      std::vector<Node> nodes;
      std::vector<Relation> relations;
      std::vector<std::string> unconnected; // nodes with no wire in or out
   };

   Explanation Build(const Patch::Data& data, const Env& env);
   // `all`: also list parameters left at their default (marked "(default)").
   std::string ToText(const Explanation& e, bool all);
   // Parameters always listed, with "default": true|false (plus "option" / "driven" when set).
   std::string ToJson(const Explanation& e);
}

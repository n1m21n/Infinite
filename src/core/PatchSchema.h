#pragma once

#include <functional>
#include <string>
#include <vector>

#include "HeadlessJob.h"
#include "Patch.h"

// =========================================================================
// PatchSchema - the machine-readable description of every node type
// (`Infinite --describe`) and the strict pass over a Patch::Data that uses it
// (`Infinite --validate`, and every headless load). Pure data + logic with no
// GL/ImGui dependency: main.cpp fills the schema from live node instances and
// supplies the connection rule, this file only reads them
// (docs/fix-briefs/headless-engine.md, blocks 3.1 and 3.3).
// =========================================================================

namespace PatchSchema
{
   struct ParamInfo
   {
      std::string key;
      char kind = 'f';        // f i b c s - the patch-file tag letter
      std::string def;        // default, already text
   };

   // One parameter a `mod`/`expr` line can address, in the order the node's
   // draw registers it. Only present when the schema was built from a drawn node.
   struct ModulatableInfo
   {
      int index = 0;
      std::string label;
      float minValue = 0.0f, maxValue = 1.0f, step = 0.0f;
      bool isEnum = false, isBool = false;
      std::vector<std::string> enumOptions;
   };

   struct SlotInfo
   {
      int slot = 0;
      std::string kind; // image | geometry | modulator | audio | note
      std::string label;
   };

   struct OutputInfo
   {
      std::string label;
      std::string kind; // image | geometry | modulator | audio | note | palette | camera | light
      bool modulator = false; // a `mod` line may read this output (a modulator or a predictor)
   };

   struct TypeSchema
   {
      std::string name;
      std::string category;
      std::vector<ParamInfo> params;
      std::vector<SlotInfo> inputs;
      std::vector<OutputInfo> outputs;
      std::vector<ModulatableInfo> modulatable;
      bool hardwareDriven = false;
      bool canBypass = false; // `flags bypassed=1` takes effect (CanBypass in main.cpp)
   };

   // Answer of the connection rule for one record.
   enum class Link { Ok, BadSlot, KindMismatch };

   struct Env
   {
      // nullptr when the type is unknown.
      std::function<const TypeSchema*(const std::string&)> schema;
      std::vector<std::string> allTypes;
      // The real rule (IsInputSlotCompatible via probe nodes). tag is
      // cable|geo|aud|note.
      std::function<Link(const std::string& srcType, int srcOutput, const std::string& dstType, int dstSlot)> link;
      // Highest `mod`/`expr` parameter index the node type registers when drawn:
      // -2 = not known here, -1 = it registers none. Filled from a drawn node,
      // so it is only available once a window has drawn one.
      std::function<int(const std::string& type)> maxParamIndex;
      // Strict: E_NO_OUTPUT is an error rather than silence, and so is
      // W_BYPASS_IGNORED (the render would not match what the author wrote).
      bool forRender = false;
   };

   std::string ParamKindName(char kind);
   std::string ToJson(const TypeSchema& t);

   // The `n` closest names to `word` by edit distance (case-insensitive).
   std::vector<std::string> Nearest(const std::string& word, const std::vector<std::string>& pool, size_t n);

   // E_BAD_PARAM for `mod`/`expr` lines whose parameter index the destination
   // does not have. Split out because the index list only exists after a node
   // has been drawn, which a render checks after loading. Needs env.maxParamIndex.
   void CheckParamIndices(const Patch::Data& data, const Env& env, std::vector<Headless::Issue>& errors);

   // Appends to `errors` / `warnings`. Never mutates `data`.
   void Validate(const Patch::Data& data, const Env& env, std::vector<Headless::Issue>& errors,
                 std::vector<Headless::Issue>& warnings);
}

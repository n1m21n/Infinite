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
      std::string key; // the saved key this control edits; empty = not found (see unkeyed)
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

   // One saved key with what the UI knows about it (filled from a drawn probe node).
   struct ControlInfo
   {
      std::string key;
      std::string label;             // what the UI calls it; empty if no widget registers it
      int index = -1;                // the mod/expr parameter index; -1 = not modulatable
      bool hasRange = false;         // min/max come from a registered widget
      float minValue = 0.0f, maxValue = 0.0f, step = 0.0f;
      bool isEnum = false, isBool = false;
      std::vector<std::string> options;
   };

   // One button a node draws. `effect` says what pressing it means for a hand-written patch.
   struct ActionInfo
   {
      std::string label;
      std::string effect;
   };

   // Best-effort reading of what a button does, from its label alone (no node is clicked):
   // "sets a path" (write the key instead), "randomizes or resets params", "UI-only", or
   // "unclassified" when nothing in the label says. Pure, so it is testable without a window.
   std::string ClassifyAction(const std::string& label);

   struct TypeSchema
   {
      std::string name;
      std::string category;
      std::vector<ParamInfo> params;
      std::vector<SlotInfo> inputs;
      std::vector<OutputInfo> outputs;
      std::vector<ModulatableInfo> modulatable;
      // How well the labelled controls were joined to saved keys (main.cpp ParamKeyJoiner).
      std::vector<ControlInfo> controls; // one per saved f/i/b key, when controlsKnown
      bool controlsKnown = false;        // a probe node was drawn and joined
      std::vector<ActionInfo> actions;   // buttons the probe node drew, in draw order
      int joinRegistered = 0; // controls the draw pass registered
      int joinKeyed = 0;      // of those, with a saved key
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
      // Control keys (ParamRef.key). paramIndexOfKey: -2 = the type was never probed,
      // -1 = it has no such modulatable control, else the `mod`/`expr` parameter index.
      // Only available once a headless run has drawn and joined a node of the type.
      std::function<int(const std::string& type, const std::string& key)> paramIndexOfKey;
      std::function<std::vector<std::string>(const std::string& type)> modulatableKeys;
      // The option names of a dropdown key (empty when it is not one).
      std::function<std::vector<std::string>(const std::string& type, const std::string& key)> optionsOf;
      // E_NO_OUTPUT is an error rather than a warning. (Strict mode, which
      // promotes the warnings too, is Headless::PromoteWarnings.)
      bool forRender = false;
   };

   // The authoring name of each input of a type, parallel to t.inputs: the
   // lowercased label with every non-alphanumeric run turned into `_`; an empty
   // label is "input", a numeric one "in_<n>"; a repeated name gets `_2`, `_3`.
   // Never parses as an integer, so it cannot be mistaken for a slot number.
   std::vector<std::string> SlotNames(const TypeSchema& t);

   // Turns every `id`/slot word in `data` into the index it stands for, so
   // Validate and ApplyPatchData only ever see numbers. Reports E_BAD_ID,
   // E_DUPLICATE_ID and E_BAD_REF (unknown node, slot or output word, with the
   // nearest names; an output word is matched against TypeSchema::outputs labels). A no-op when data.hasNamedRefs is false. Clears the words it
   // resolved; `id` values stay on the nodes (--keep-ids) until the caller drops them.
   void Resolve(Patch::Data& data, const Env& env, std::vector<Headless::Issue>& errors);

   // Turns `mod 5 radius ...` / `expr 5 radius ...` keys into parameter indices and
   // `i shapeType Star` option names into numbers. Reports E_BAD_KEY and E_BAD_VALUE with
   // the nearest valid names. Records it could not resolve are left as they were.
   void ResolveKeys(Patch::Data& data, const Env& env, std::vector<Headless::Issue>& errors);

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

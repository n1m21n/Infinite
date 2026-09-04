#pragma once

#include <map>
#include <string>
#include <vector>

// Build step 12 (docs/plans/field/step-12-dynamic-pins-ir.md S4): a
// near-verbatim copy of ParamTable's shape (see ParamTable.h), adapted for
// output/input pin identity instead of param identity. Kept independent of
// ParamTable/INode.h - this step does not wire PinTable into any node or
// visitor yet (that is later work); it exists purely as the compiler-side
// identity-tracking structure the IR collection (FieldIR.h/BackendRegister.cpp)
// feeds into.
namespace Field
{
   // One `output`/`input` declaration as collected by a kernel compile -
   // the input to PinTable::Reconcile, mirroring ParamTable's DeclaredParam.
   struct DeclaredPin
   {
      std::string name;
      std::string typeName;   // "float", "int", "bool", "vec2/3/4", "geometry", "audio", "image"
      std::string domainName; // "frame", "element", "pixel", "sample" (never "graph" - see S5.7)
      bool isOutput = true;   // true = `output` pin, false = `input` pin
   };

   // A stable pin identity. `id` is minted once and never reused across a
   // shape change (S5.4): if a later compile re-declares the same `name`
   // with a different typeName/domainName/isOutput, the OLD entry is
   // retired (isDeclared=false) and a NEW entry with a fresh id takes over
   // the name - exactly like a rename, so any cable bound to the old id
   // becomes discoverably orphaned rather than silently reinterpreted.
   struct PinEntry
   {
      int id = 0;
      std::string name;
      std::string typeName;
      std::string domainName;
      bool isOutput = true;
      bool isDeclared = false;
   };

   class PinTable
   {
   public:
      // v1 ceiling (S5.5 / matches FieldIR.cpp's kFieldMaxDeclaredPinsPerProgram
      // and BackendRegister.cpp's per-kernel pin-count check) on the combined
      // number of output+input pins a single kernel may declare.
      static constexpr int kMaxDeclaredPins = 16;

      PinTable() = default;

      // Reconciles the table against a fresh compile's declared pin list.
      // Purely a discoverability pass: any previously-declared pin missing
      // from `declared`, and any pin whose shape (type/domain/direction)
      // changed, is marked isDeclared=false and its name is reported via
      // outNotice - this method never inspects or touches cable state
      // (Patch::CableRecord) and never refuses the reconcile itself; the
      // accept/refuse decision belongs to a later build step (Apply()'s
      // cable-orphan check, docs/plans/field/step-12-dynamic-pins-ir.md S1.3).
      void Reconcile(const std::vector<DeclaredPin>& declared, int nodeIndex, std::string& outNotice);

      const std::vector<PinEntry>& Pins() const { return mPins; }
      std::vector<PinEntry>& Pins() { return mPins; }

      // Returns the currently-declared entry matching `name` if one exists;
      // otherwise falls back to the most recently retired entry of that
      // name (so an orphaned cable can still resolve a display label).
      const PinEntry* Find(const std::string& name) const;
      PinEntry* Find(const std::string& name);
      const PinEntry* FindById(int id) const;

      int NextPinId() const { return mNextPinId; }
      void SetNextPinId(int nextId) { mNextPinId = nextId; }

      std::string SerializePinMap() const;
      void DeserializePinMap(const std::string& str);

   private:
      std::vector<PinEntry> mPins;
      std::map<std::string, int> mPinIdByName;
      int mNextPinId = 1;
   };

   // Build step 13 (docs/plans/field/step-13-dynamic-pins-node-wiring.md
   // S5.1): the cable-orphan refusal check needs to know whether a pin slot
   // currently has a live cable attached, but that information (gLinks) is
   // only known to main.cpp's UI/graph-editor code, which is compiled in a
   // different translation unit (and inside an anonymous namespace) from
   // the Field*Node::Apply() methods that need to ask the question. Rather
   // than exposing gLinks itself, main.cpp installs this single function
   // pointer once at startup; Apply() calls it (a null checker - e.g. in a
   // headless/test context that never installed one - is treated as "no
   // live cable", i.e. never refuses). isOutput selects which pin table
   // (output vs input) `slot` indexes into, using the same compacted,
   // gap-free slot numbering as INode::OutputLabel/ModulatorOutput and
   // GeometryInputSlot/AudioInputSlot/ModulatorInputSlot.
   using LiveCableChecker = bool (*)(int nodeIndex, int slot, bool isOutput);
   extern LiveCableChecker gLiveCableChecker;

   // Sibling bridge to gLiveCableChecker: when a pin that would retire still
   // has a live cable, ReconcileFieldPins calls this (if installed) to sever
   // that cable at its actual node-graph source rather than refusing the
   // whole reconcile - a preset switch that drops a declared pin (e.g. Field
   // Modifier's `glow`) should auto-disconnect and retire it, not block the
   // preset load until the user manually unplugs it first. A null
   // disconnector (headless/test context) falls back to the old refuse
   // behavior.
   using LiveCableDisconnector = void (*)(int nodeIndex, int slot, bool isOutput);
   extern LiveCableDisconnector gLiveCableDisconnector;

   // Shared S5.1 reconcile/refuse/commit step, used identically by
   // FieldElementNode, FieldSampleNode and FieldPixelNode's Apply(). Tries
   // to reconcile `live` against a fresh compile's `declared` list. If any
   // pin that would retire (removed, or redeclared with a changed shape)
   // still has a live cable attached to its current compacted slot (per
   // gLiveCableChecker), that cable is auto-disconnected via
   // gLiveCableDisconnector (if installed) and the retirement proceeds -
   // `outNotice` names the auto-disconnected pin(s) so the caller can surface
   // it as an FYI, not an error. Only when no disconnector is installed
   // (headless/test context) does a live cable fall back to refusing the
   // whole reconcile: `live` is left completely untouched, `outRefusal`
   // names the offending pin(s), and this returns false - the caller must
   // then abort Apply() entirely (keep the previous program live), the same
   // "keep last working program" discipline as a compile error.
   // On success, `live` has been updated in place (same object identity,
   // so any raw PinEntry* a caller cached across the call is invalidated)
   // and this returns true.
   // `nativeCount` is how many non-PinTable-tracked pins occupy the front
   // of the compacted slot numbering (e.g. FieldElementNode output 0 "geo",
   // plus output 1 "publish" when its toggle is on) - needed to translate a
   // PinTable entry's position among currently-declared entries into the
   // real compacted slot number gLiveCableChecker expects.
   bool ReconcileFieldPins(PinTable& live, const std::vector<DeclaredPin>& declared, int nodeIndex,
                            int nativeCount, std::string& outNotice, std::string& outRefusal);
}

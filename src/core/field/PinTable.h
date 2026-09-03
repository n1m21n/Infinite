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
}

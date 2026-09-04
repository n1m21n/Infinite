#pragma once

#include "core/INode.h"
#include "core/ImageCable.h"
#include "core/Modulation.h"
#include "field/FieldGraphHost.h"
#include "field/FieldGraphKernel.h"
#include "field/FieldGraphOwnership.h"
#include "field/FieldIR.h"
#include "field/ParamTable.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Shared by FieldGraphNode.cpp's Regenerate() call-site auto-placement and
// main.cpp's step-16 unpack topological-depth layout: only Wavetable/Drum
// Sequencer render at kAudioWideWidth (main.cpp DrawWavetableBody/
// DrawDrumSequencerBody), everything else fits the narrower auto-place
// column without visibly wasting canvas. One classification, two call
// sites - not a third copy of the list.
inline bool IsWideAutoPlaceType(const std::string& typeName)
{
   return typeName == "Wavetable" || typeName == "Drum Sequencer";
}

// Field 'graph' domain (build step 10): a kernel that runs once, at edit
// time, and emits/wires/configures real Infinite nodes (main.cpp §MainGraphHost
// drives the real graph; see FieldGraphReconciler.h for the diff and
// FieldGraphKernel.h for the interpreter). Main-thread-only, edit-time-only -
// this node itself never cooks any DSP/pixels of its own, unlike the other
// three Field node types. Build step 15 follow-up (§5.4): GetOutputTexture()/
// CookIfNeeded() below DO forward to whatever mBoundaryOutput currently
// points at, so an outer cable plugged into this node's own (single, derived)
// boundary output pin reads the current primary terminal's picture - see
// SetBoundaryOutputTarget's comment for who calls it and when.
class FieldGraphNode : public INode
{
public:
   static INode* Create() { return new FieldGraphNode(); }

   // Forwards through mBoundaryOutput exactly the way NullNode
   // (UtilityNodes.h) already forwards through its own ImageCable input -
   // not a new pattern, the same one. mBoundaryOutput's target is refreshed
   // by main.cpp's node-body draw dispatch every frame and right after every
   // Regenerate() (RunFieldGraphRegenerate) via SetBoundaryOutputTarget,
   // never cached here across a SpawnNode call (§5.4; this class has no
   // gNodes access to resolve mOwnership indices itself, hence the push
   // rather than a pull).
   unsigned int GetOutputTexture() override
   {
      INode* src = mBoundaryOutput.Resolved();
      return src ? src->GetOutputTexture() : 0;
   }
   int GetOutputWidth() const override { return mBoundaryOutput.Width(); }
   int GetOutputHeight() const override { return mBoundaryOutput.Height(); }
   void CookIfNeeded(int frameId) override
   {
      if (mLastCookFrame == frameId)
         return;
      mLastCookFrame = frameId;
      mBoundaryOutput.Pull(frameId);
   }
   void VisitParams(ParamVisitor& v) override;

   // Build step 15 follow-up (§5.4): sets this frame's boundary-output-pin
   // target - the terminal (§5.1) that an outer cable plugged into this
   // node's own output pin should read from. Called by main.cpp's node-body
   // draw dispatch (every frame, using the same "first terminal with a
   // texture" resolution the inline preview already computes) and by
   // RunFieldGraphRegenerate right after Regenerate() (so a cook that
   // happens before the next draw still reads the right target). Passing
   // nullptr is correct and expected whenever nothing has been regenerated
   // yet, or no terminal currently produces a texture.
   void SetBoundaryOutputTarget(INode* target) { mBoundaryOutput.Connect(target); }
   INode* BoundaryOutputTarget() const { return mBoundaryOutput.GetSource(); }

   // Appends one more clause to mNotice (semicolon-separated, matching the
   // style Regenerate() itself already builds mNotice in) - used by
   // RunFieldGraphRegenerate to report a boundary-pin cable detachment that
   // happens after Regenerate() has already built its own notice string
   // (§5.4), without a second/competing notice mechanism.
   void AppendNotice(const std::string& extra);

   // Dynamic pins, Phase 1 (build step 11, §5.5): one modulator INPUT pin, a
   // trigger. This is ordinary INode pin plumbing (ModulatorInputSlot),
   // entirely separate from emit()/connect()/set()/place() - those stay
   // inside FieldGraphKernel.cpp/FieldGraphReconciler.*, this never touches
   // them. A rising edge (crossing 0.5) polled once per frame on the main
   // thread re-runs Regenerate() the same way the "Regenerate" button does -
   // see PollTriggerEdge() and its call site (main.cpp, right after the
   // gFieldGraphPendingRegenerate drain, trap T14).
   int ModulatorInputCount() const override { return addTriggerInput ? 1 : 0; }
   IModulator** ModulatorInputSlot(int slot) override
   {
      return (addTriggerInput && slot == 0) ? &mTriggerInput : nullptr;
   }
   const char* InputLabel(int slot) const override { return slot == 0 ? "trigger" : nullptr; }

   bool addTriggerInput = false;
   bool TriggerInputWired() const { return mTriggerInput != nullptr; }
   // Transient (not saved) - see FieldElementNode::pinRefusal for the
   // rationale; identical cable-orphaning refusal policy (decision 4),
   // applied to an input pin here instead of an output.
   std::string pinRefusal;

   // Polls the trigger input for a rising edge (crossing 0.5) since the last
   // call and updates internal edge-detection state - call at most once per
   // frame, from the main thread, outside ed::Begin()/ed::End(). Returns
   // false when no trigger pin exists or nothing is wired to it.
   bool PollTriggerEdge()
   {
      if (!addTriggerInput || mTriggerInput == nullptr)
         return false;
      float v = mTriggerInput->Value01();
      bool rising = (v >= 0.5f) && (mLastTriggerValue < 0.5f);
      mLastTriggerValue = v;
      return rising;
   }

   // Lex -> parse -> LowerGraphProgramToIR only. Populates mLastError/
   // mLastProgram on success. Never mutates the graph - safe to call from
   // any path that just needs the node's compiled state refreshed (paste,
   // patch load, undo/redo - see main.cpp's ReloadDerivedState, T11).
   bool Apply();

   // Compiles if needed (Apply()), then interprets the program and
   // reconciles the result against the live graph via `host`. This is the
   // only path that actually mounts/unmounts/wires nodes - it must only be
   // called from the main thread, outside the ImGui draw pass proper (see
   // doc trap T14), and wraps its own undo checkpoint (doc §5.4).
   bool Regenerate(Field::IFieldGraphHost& host);

   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }

   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }

   const Field::GraphOwnershipMap& Ownership() const { return mOwnership; }
   Field::GraphOwnershipMap& Ownership() { return mOwnership; }

   // Build step 16 ("Unpack to Canvas"), §3.2/§4.2: the last successful
   // Regenerate()'s plan - `connects` feeds Field::ComputeEmitDepths for
   // topological-depth layout, `emits` gives each key's typeName for the
   // width classification IsWideAutoPlaceType() below already needs. Empty/
   // default if nothing has regenerated yet or the last regenerate failed
   // (mirrors TerminalIndices()' own "nothing yet" handling).
   const Field::GraphPlan& LastPlan() const { return mLastPlan; }

   // 16 lowercase hex chars, regenerated only when this node's identity
   // must diverge from a source node's (paste - doc §5.7.3), never on an
   // ordinary compile/regenerate.
   const std::string& Uid() const { return mUid; }
   void SetUid(const std::string& uid) { mUid = uid; }
   static std::string NewUid();

   bool DrivesParam(int targetNodeIndex, const std::string& paramName) const
   {
      for (const auto& s : mLastPlan.sets)
      {
         if (s.paramName == paramName && mOwnership.Get(s.targetKey) == targetNodeIndex)
            return true;
      }
      return false;
   }

   // Build step 15 ("Instrument Mode"): true (default, new nodes) = mounted
   // children are real gNodes entries flagged GraphNode::hiddenFromCanvas,
   // never drawn/picked in the node editor. False after step 16's "Unpack to
   // Canvas" (or on loading a patch saved before this field existed - see
   // VisitParams below, the load-time default is the opposite of this
   // member's own compile-time default).
   bool encapsulated = true;

   // The gNodes indices this FieldGraphNode currently owns, mirroring
   // mOwnership's values as a flat set for O(1) "is this index mine" checks.
   // Rebuilt from mOwnership at the end of every successful Regenerate() -
   // never a second source of truth, never persisted (VisitParams doesn't
   // need a line for it; ownershipText already round-trips the real data).
   const std::set<int>& MountedIndices() const { return mMountedIndices; }
   bool OwnsMountedIndex(int idx) const { return mMountedIndices.count(idx) > 0; }

   // Build step 15 §4.2: pushes any declared param whose value has changed
   // since the last call directly to its live-forwarded target(s) via
   // host.SetParam - no re-interpretation, no ReconcileGraphPlan, no
   // Mount/Unmount. Safe to call every frame; a no-op when mLiveForward is
   // empty (nothing in the program was a bare-param pass-through set()).
   // Main-thread only - see field-integration §4 / doc trap 7.
   void PushLiveParams(Field::IFieldGraphHost& host);

   // Build step 15 §5.1: boundary-output candidates - entries in
   // mLastPlan.emits whose key never appears as a connect's srcKey, i.e.
   // nothing inside the graph consumes their output, so each is a leaf of
   // the internal wiring. Returned as mounted gNodes indices (via
   // mOwnership), in plan.emits order; empty if nothing has been
   // regenerated yet or the last regenerate failed. This file has no
   // gNodes/FindNodeByIndex reference (same discipline as
   // FieldGraphKernel.h) - the caller (main.cpp's node-body draw dispatch)
   // resolves each index to a real INode* and picks the first one that
   // answers "yes" to whichever of §5.1's three questions it's asking.
   std::vector<int> TerminalIndices() const;

   // Build step 15 follow-up (§5.3.1): inline audio-terminal waveform
   // preview cache. Lives here, not on the terminal (the terminal doesn't
   // know it's being previewed) - adapts GranularNode's own decimated
   // min/max shape (GranularNode.h kWaveformCacheSize/waveformMin/Max)
   // rather than inventing a new one. Filled by main.cpp's
   // DrawFieldGraphWaveform, which decimates whatever the resolved audio
   // terminal's own pre-existing MeterRing-backed scope (ReadScope(), the
   // pattern several concrete audio node types already implement) reports,
   // on a throttled cadence - never written from the audio thread, never
   // serialized (same discipline as every other node's scopeCache/
   // waveformMin/Max member in this codebase).
   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;
   double waveformCacheTime = -1.0;

   std::string code =
      "# emit(\"Type Name\", k0, k1, ...) -> handle\n"
      "# connect(srcHandle, srcSlot, dstHandle, dstSlot)\n"
      "# set(handle, \"paramName\", value)\n"
      "# place(handle, x, y)\n";
   std::string ownershipText; // mirrors mOwnership.ToText(), synced by VisitParams

private:
   Field::ParamTable mParamTable;
   Field::GraphIRProgram mLastProgram;
   Field::GraphPlan mLastPlan;
   Field::GraphOwnershipMap mOwnership;
   std::string mUid;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   bool mHasCompiledProgram = false;

   IModulator* mTriggerInput = nullptr;
   float mLastTriggerValue = 0.0f;

   std::set<int> mMountedIndices;

   // (declared param name) -> list of (mounted node index, paramName) it
   // forwards to directly - built once per successful Regenerate() from
   // mLastPlan.sets' sourceParamName provenance (FieldGraphKernel.h §4.2)
   // and mOwnership. Never saved - fully re-derivable from code+ownership.
   std::map<std::string, std::vector<std::pair<int, std::string>>> mLiveForward;
   // Last value pushed per declared param name, so PushLiveParams only calls
   // host.SetParam for params that actually changed since the last call.
   std::map<std::string, float> mLastPushedValue;

   // Build step 15 follow-up (§5.4): the boundary output pin's current
   // target, held as an ImageCable (the same forwarding member NullNode
   // uses for its input) rather than a bare INode* precisely so
   // GetOutputTexture()/CookIfNeeded() above can reuse ImageCable::
   // Resolved()/Pull() instead of duplicating the bypass-walking/cook
   // dance. Set only by SetBoundaryOutputTarget - never resolved from
   // mOwnership in this file (no gNodes access here, same discipline as
   // TerminalIndices() above).
   ImageCable mBoundaryOutput;
   int mLastCookFrame = -1;

   void RebuildLiveForward();
};

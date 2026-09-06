#pragma once

#include "INode.h"
#include "Geometry3DNodes.h"
#include "Modulation.h"
#include "field/ElementStore.h"
#include "field/ElementBackend.h"
#include "field/FieldState.h"
#include "field/ParamTable.h"
#include "field/PinTable.h"
#include "field/FieldDevice.h"
#include <memory>
#include <string>
#include <vector>

class FieldElementNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new FieldElementNode(); }

   FieldElementNode();
   ~FieldElementNode() override = default;

   // INode overrides
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   INode* BypassSource() override { return dynamic_cast<INode*>(input); }

   IGeometrySource** GeometryInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int) const override { return "geo"; }

   // Dynamic pins, Phase 1 (build step 11, §5.1): a second output, "publish",
   // reads a Frame-domain-hoisted scalar (e.g. `publish = ...` at prologue
   // scope) out of the compiled program via ElementVM::ReadFrameVar - no IR
   // or domain-inference change, purely C++-hardcoded plumbing. Append-only:
   // output 0 (geometry) never moves.
   //
   // Dynamic pins, Phase 2b (build step 13, §5.1): kernel `output` decls on
   // top of the two native/toggle outputs above, compacted with no gap -
   // native "geo" (0), then "publish" when the toggle is on, then each
   // currently-declared entry of mOutputPins in PinTable insertion order.
   // See Apply() for how mOutputPins gets reconciled/refused; see
   // mDeclaredOutputMods/DeclaredOutputPlaceholder below for which declared
   // pins read a real live value vs. a fixed placeholder 0.
   int NativeOutputCount() const { return publishScalarOutput ? 2 : 1; }
   int DeclaredOutputCount() const
   {
      int n = 0;
      for (const auto& p : mOutputPins.Pins())
         if (p.isDeclared) n++;
      return n;
   }
   int OutputCount() const override { return NativeOutputCount() + DeclaredOutputCount(); }
   const char* OutputLabel(int index) const override
   {
      if (index == 0) return "geo";
      if (publishScalarOutput && index == 1) return "publish";
      int declIdx = index - NativeOutputCount();
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx) return p.name.c_str();
         seen++;
      }
      return "out";
   }
   IModulator* ModulatorOutput(int index) override
   {
      // Gated on publishScalarOutput, not just index==1: OutputCount() is 1
      // when the pin is off, so index 1 does not nominally exist - a caller
      // that reaches for it anyway (e.g. a headless ConnectNodes(idx, 1, ...))
      // must see nullptr, the same as any other out-of-range output index,
      // not a live IModulator for a pin the UI never drew.
      if (publishScalarOutput && index == 1)
         return static_cast<IModulator*>(&mPublishOutput);
      int declIdx = index - NativeOutputCount();
      if (declIdx < 0)
         return nullptr;
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx)
         {
            if (declIdx >= Field::PinTable::kMaxDeclaredPins)
               return nullptr;
            // Re-point this slot's placeholder at its current pin identity
            // every call, not just at reconcile time: PinTable::Pins() order
            // (and so declIdx<->name mapping) can shift across an Apply(),
            // and this stays cheap enough to just always redo it.
            DeclaredOutputPlaceholder& mod = mDeclaredOutputMods[declIdx];
            mod.owner = this;
            mod.pinName = (p.domainName == "frame" && p.typeName != "audio" && p.typeName != "image") ? p.name : std::string();
            return &mod;
         }
         seen++;
      }
      return nullptr;
   }

   bool publishScalarOutput = false;
   // Transient (not saved) - set by the UI toggle handler when a cable-
   // orphaning refusal happens, so the node body can surface a one-line
   // reason. Cleared as soon as the refused state is no longer relevant.
   // Also set by Apply() itself (step 13) when a kernel edit would orphan a
   // cable attached to a declared pin that the edit would retire.
   std::string pinRefusal;

   const Field::PinTable& OutputPinTable() const { return mOutputPins; }
   const Field::PinTable& InputPinTable() const { return mInputPins; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Int("maxElements", maxElements);
      v.Int("generateCount", generateCount);
      v.Bool("publishScalarOutput", publishScalarOutput);
      mParamTable.VisitParams(v);
      mState.VisitParams(v);
      // Dynamic pins, Phase 2b (build step 13, §5.1 step 8): persists each
      // table's (name -> id) map so a reload reconciles declared pins
      // against the same identities cables were saved against, rather than
      // reminting ids from scratch. DeserializePinMap runs here, before
      // ReloadDerivedState calls Apply() (see main.cpp) - that ordering is
      // what makes reload-time reconcile faithful.
      std::string outPins = mOutputPins.SerializePinMap();
      std::string inPins = mInputPins.SerializePinMap();
      v.Text("__outputPins", outPins);
      v.Text("__inputPins", inPins);
      // Unconditional: on a save, outPins/inPins already equal what
      // SerializePinMap just produced, so this is a harmless no-op
      // reconstruction of the same skeleton entries (PinEntry::Find below
      // finds the already-declared ones and adds nothing new). On a load,
      // `v.Text` overwrote outPins/inPins with the saved string, and this
      // is what actually restores mNextPinId/mPinIdByName before Apply()
      // (called by ReloadDerivedState right after VisitParams returns)
      // reconciles against the freshly-compiled program.
      mOutputPins.DeserializePinMap(outPins);
      mInputPins.DeserializePinMap(inPins);
   }

   // IGeometrySource overrides & passthrough forwarding
   const Mesh& GetMesh() override { return mOutMesh; }
   unsigned long long MeshRevision() override { return mMeshRevision; }
   Mat4 GetModelMatrix() const override { return input ? input->GetModelMatrix() : Mat4::Identity(); }
   Material GetMaterial() const override { return input ? input->GetMaterial() : Material(); }
   unsigned int GetSurfaceTexture() override { return input ? input->GetSurfaceTexture() : 0; }
   unsigned int GetMaterialTexture(int map) override { return input ? input->GetMaterialTexture(map) : 0; }
   unsigned long long SurfaceTextureRevision() const override { return input ? input->SurfaceTextureRevision() : 0; }
   MappingTransform GetMappingTransform() const override { return input ? input->GetMappingTransform() : MappingTransform(); }
   IGeometrySource* PassthroughSource() const override { return input; }
   Mat4 GetInstanceGroupMatrix() const override { return input ? input->GetInstanceGroupMatrix() : Mat4::Identity(); }
   const std::vector<unsigned char>* InstanceSelection() const override { return input ? input->InstanceSelection() : nullptr; }
   unsigned long long InstanceSelectionRevision() const override { return input ? input->InstanceSelectionRevision() : 0; }
   const std::vector<Mat4>* InstanceTransformOverride() const override { return input ? input->InstanceTransformOverride() : nullptr; }
   const std::vector<Particle>* GetPointCloud() override { return input ? input->GetPointCloud() : nullptr; }
   unsigned long long PointCloudRevision() override { return input ? input->PointCloudRevision() : 0; }
   float PointBaseSize() const override { return input ? input->PointBaseSize() : 1.0f; }
   const Polyline* GetCurve() override { return input ? input->GetCurve() : nullptr; }
   unsigned long long CurveStamp() override { return input ? input->CurveStamp() : 0; }
   // Field element processing does not touch splats (see docs/plans/
   // gaussian-splat-node.md S8 - a per-frame element-domain EWA kernel would
   // be a deoptimization vs. the dedicated GPU path, and the sort isn't
   // expressible there). This is pure passthrough, same as GetPointCloud/
   // GetCurve above: a splat cloud upstream of a Field node it isn't wired
   // into must still reach Render3D rather than vanishing.
   const SplatIO::SplatCloud* GetSplatCloud() override { return input ? input->GetSplatCloud() : nullptr; }
   unsigned long long SplatCloudRevision() override { return input ? input->SplatCloudRevision() : 0; }

   // Field compilation & UI
   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   bool WasTruncated() const { return mWasTruncated; }
   int ActualElementCount() const { return mActualElementCount; }
   const std::shared_ptr<Field::ElementProgram>& Program() const { return mProgram; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   const Field::FieldState& State() const { return mState; }
   Field::FieldState& State() { return mState; }
   const char* CostReadout() const { return mCostReadout; }

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);

   // Field build step 17 (.infdev device files): reads code, GetParamTable(),
   // generateCount, maxElements into/out of a portable Field::DeviceFile. See
   // FieldDevice.h and docs/plans/field/step-17-infdev-format.md §3.
   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   IGeometrySource* input = nullptr;
   std::string code;
   int maxElements = 65536;
   int presetIndex = 0;
   // Used only when nothing is wired into `input`: FieldElement then acts as
   // a generator (like DistributePointsInGridNode) instead of a modifier,
   // producing this many fresh points for the kernel to shape via `i`/`count`.
   int generateCount = 64;

   // Reads the last-executed program's Frame-domain `publish` value, if one
   // was assigned; 0.0 when unused (not an error - §5.1 step 4). Modeled on
   // MacroXYNode::YAxis (src/nodes/MacroNodes.h) - a small owned IModulator
   // with an owner back-pointer, added as the 2nd output rather than reused
   // via inheritance since output 0 (geometry) has no IModulator meaning here.
   struct PublishOutput : public IModulator
   {
      FieldElementNode* owner = nullptr;
      float Value01() override
      {
         if (owner == nullptr)
            return 0.0f;
         float v = 0.0f;
         owner->mVM.ReadFrameVar("publish", v);
         return v;
      }
   };

private:
   Field::ElementStore mStore;
   Field::ElementVM mVM;
   Field::ParamTable mParamTable;
   Field::FieldState mState;
   std::shared_ptr<Field::ElementProgram> mProgram;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   PublishOutput mPublishOutput;

   // Dynamic pins, Phase 2b (build step 13). mOutputPins/mInputPins track
   // this node's kernel `output`/`input` declarations (see PinTable.h);
   // Apply() reconciles them each successful compile, refusing the whole
   // Apply() if that reconcile would orphan a live cable (§5.1).
   Field::PinTable mOutputPins;
   Field::PinTable mInputPins;

   // One instance per slot position (not a single shared instance) so
   // distinct declared pins are distinct IModulator identities, matching
   // every other multi-output node. A frame-domain, non-structural declared
   // output (`chime`, `glow`, ...) reads its real, live value via
   // ElementVM::ReadFrameVar, the same name-keyed channel `publish` already
   // uses (FieldIR.cpp's DeclOutput lowering now emits the frame-var write
   // that makes this possible). Any other declared output (element/pixel/
   // sample domain, or audio/image-typed) has no such runtime channel yet -
   // pinName is left empty for those, and this reads a fixed 0 as before.
   struct DeclaredOutputPlaceholder : public IModulator
   {
      FieldElementNode* owner = nullptr;
      std::string pinName;
      float Value01() override
      {
         if (owner == nullptr || pinName.empty())
            return 0.0f;
         float v = 0.0f;
         owner->mVM.ReadFrameVar(pinName, v);
         return v;
      }
   };
   DeclaredOutputPlaceholder mDeclaredOutputMods[Field::PinTable::kMaxDeclaredPins];

   Mesh mOutMesh;
   unsigned long long mMeshRevision = 1;
   unsigned long long mLastUpstreamRevision = 0;
   unsigned long long mLastResetEpoch = 0;
   char mCostReadout[128] = { 0 };
   float mLastEvalT = -999999.0f;
   // Build step 23: cooks since this node's state bank was cleared, bound to
   // the reserved name `age`. Lets a kernel seed itself exactly once.
   float mStateAge = 0.0f;
   bool mWasTruncated = false;
   int mActualElementCount = 0;
   int mLastCookFrame = -1;

   // Hash of mParamTable's current values as of the last rebuild. A static
   // kernel (no `t`, no `state`) has no other signal that a param slider
   // moved - upstream revision doesn't change and isTimeDependent is false -
   // so without this, CookIfNeeded's needRebuild gate never fires and the
   // mesh silently goes stale relative to the slider.
   size_t mLastParamHash = 0;
};

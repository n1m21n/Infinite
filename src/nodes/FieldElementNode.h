#pragma once

#include "INode.h"
#include "Geometry3DNodes.h"
#include "Modulation.h"
#include "field/ElementStore.h"
#include "field/ElementBackend.h"
#include "field/FieldState.h"
#include "field/ParamTable.h"
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
   int OutputCount() const override { return publishScalarOutput ? 2 : 1; }
   const char* OutputLabel(int index) const override { return index == 1 ? "publish" : "geo"; }
   IModulator* ModulatorOutput(int index) override
   {
      // Gated on publishScalarOutput, not just index==1: OutputCount() is 1
      // when the pin is off, so index 1 does not nominally exist - a caller
      // that reaches for it anyway (e.g. a headless ConnectNodes(idx, 1, ...))
      // must see nullptr, the same as any other out-of-range output index,
      // not a live IModulator for a pin the UI never drew.
      return (publishScalarOutput && index == 1) ? static_cast<IModulator*>(&mPublishOutput) : nullptr;
   }

   bool publishScalarOutput = false;
   // Transient (not saved) - set by the UI toggle handler when a cable-
   // orphaning refusal happens, so the node body can surface a one-line
   // reason. Cleared as soon as the refused state is no longer relevant.
   std::string pinRefusal;

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Int("maxElements", maxElements);
      v.Int("generateCount", generateCount);
      v.Bool("publishScalarOutput", publishScalarOutput);
      mParamTable.VisitParams(v);
      mState.VisitParams(v);
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

   Mesh mOutMesh;
   unsigned long long mMeshRevision = 1;
   unsigned long long mLastUpstreamRevision = 0;
   unsigned long long mLastResetEpoch = 0;
   char mCostReadout[128] = { 0 };
   float mLastEvalT = -999999.0f;
   bool mWasTruncated = false;
   int mActualElementCount = 0;
   int mLastCookFrame = -1;
};

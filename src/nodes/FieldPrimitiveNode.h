#pragma once

#include "INode.h"
#include "Geometry3DNodes.h"
#include "Modulation.h"
#include "field/ElementStore.h"
#include "field/ElementBackend.h"
#include "field/FieldState.h"
#include "field/ParamTable.h"
#include "field/PinTable.h"
#include "core/field/FieldDevice.h"
#include <memory>
#include <string>
#include <vector>

class FieldPrimitiveNode : public INode, public IGeometrySource
{
public:
   static INode* Create() { return new FieldPrimitiveNode(); }

   FieldPrimitiveNode();
   ~FieldPrimitiveNode() override = default;

   // INode overrides
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   INode* BypassSource() override { return nullptr; }

   IGeometrySource** GeometryInputSlot(int) override { return nullptr; }

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
   std::string pinRefusal;

   const Field::PinTable& OutputPinTable() const { return mOutputPins; }
   const Field::PinTable& InputPinTable() const { return mInputPins; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Int("count", count);
      v.Int("maxElements", maxElements);
      v.Bool("publishScalarOutput", publishScalarOutput);
      mParamTable.VisitParams(v);
      mState.VisitParams(v);

      std::string outPins = mOutputPins.SerializePinMap();
      std::string inPins = mInputPins.SerializePinMap();
      v.Text("__outputPins", outPins);
      v.Text("__inputPins", inPins);
      mOutputPins.DeserializePinMap(outPins);
      mInputPins.DeserializePinMap(inPins);
   }

   // IGeometrySource overrides (pure generator - identity transforms, no passthrough)
   const Mesh& GetMesh() override { return mOutMesh; }
   unsigned long long MeshRevision() override { return mMeshRevision; }
   Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
   Material GetMaterial() const override { return Material(); }
   unsigned int GetSurfaceTexture() override { return 0; }
   unsigned int GetMaterialTexture(int) override { return 0; }
   unsigned long long SurfaceTextureRevision() const override { return 0; }
   MappingTransform GetMappingTransform() const override { return MappingTransform(); }
   IGeometrySource* PassthroughSource() const override { return nullptr; }
   Mat4 GetInstanceGroupMatrix() const override { return Mat4::Identity(); }
   const std::vector<unsigned char>* InstanceSelection() const override { return nullptr; }
   unsigned long long InstanceSelectionRevision() const override { return 0; }
   const std::vector<Mat4>* InstanceTransformOverride() const override { return nullptr; }
   const std::vector<Particle>* GetPointCloud() override { return nullptr; }
   unsigned long long PointCloudRevision() override { return 0; }
   float PointBaseSize() const override { return 1.0f; }
   const Polyline* GetCurve() override { return nullptr; }
   unsigned long long CurveStamp() override { return 0; }

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

   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   std::string code;
   int count = 256;
   int maxElements = 65536;
   int presetIndex = 0;

   struct PublishOutput : public IModulator
   {
      FieldPrimitiveNode* owner = nullptr;
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

   Field::PinTable mOutputPins;
   Field::PinTable mInputPins;

   struct DeclaredOutputPlaceholder : public IModulator
   {
      FieldPrimitiveNode* owner = nullptr;
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
   unsigned long long mLastBuiltCount = 0;
   unsigned long long mLastResetEpoch = 0;
   char mCostReadout[128] = { 0 };
   float mLastEvalT = -999999.0f;
   bool mWasTruncated = false;
   int mActualElementCount = 0;
   int mLastCookFrame = -1;
   size_t mLastParamHash = 0;
};

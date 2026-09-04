#pragma once

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "Modulation.h"
#include "field/FieldIR.h"
#include "field/GlslBackend.h"
#include "field/PixelState.h"
#include "field/ParamTable.h"
#include "field/PinTable.h"
#include "field/FieldDevice.h"
#include <string>
#include <vector>

class FieldPixelNode : public INode
{
public:
   static INode* Create() { return new FieldPixelNode(); }

   FieldPixelNode();
   ~FieldPixelNode() override;

   // INode overrides
   unsigned int GetOutputTexture() override;
   // Dynamic pins, Phase 1 (build step 11, §5.3): a second image output
   // exposing the state ping-pong bank (mState) that already exists for the
   // kernel's own `state` cells - the entry that actually exercises the
   // ImageCable/INode::GetOutputTexture(int)/Patch.cpp "cable"-tag srcOutput
   // plumbing added for this, since nothing before this pin supported more
   // than one image output per node. index==0 is the ordinary render output
   // (unchanged); index==1 is the aux texture, live only when
   // exposeAuxTexture is on AND the kernel actually declared state cells -
   // mState is never resized/populated otherwise, so returning its texture
   // then would hand out a stale or zero FBO.
   unsigned int GetOutputTexture(int index) override;
   int GetOutputWidth() const override;
   int GetOutputHeight() const override;
   void CookIfNeeded(int frameId) override;
   // No native input pin (see the device-catalog simplification: Field Pixel
   // is generation-only for now) - BypassSource has nothing upstream to skip
   // to, so it falls back to INode's default (returns nullptr).
   const char* InputLabel(int slot) const override
   {
      int seen = 0;
      for (const auto& p : mInputPins.Pins())
      {
         if (!p.isDeclared || p.typeName != "image") continue;
         if (seen == slot) return p.name.c_str();
         seen++;
      }
      return nullptr;
   }

   // Dynamic pins, Phase 2b (build step 13, §5.1): kernel `output` decls on
   // top of the native "out" and toggle "state" outputs above, compacted
   // with no gap - see FieldElementNode's identical layering scheme.
   int NativeOutputCount() const { return exposeAuxTexture ? 2 : 1; }
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
      if (index == 0) return "out";
      if (exposeAuxTexture && index == 1) return "state";
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
      // A declared `image` output has no IModulator meaning (routed via
      // GetOutputTexture(int)/ImageCable instead, like the native "out" and
      // toggle "state" outputs); only a scalar/vector declared output gets
      // a placeholder modulator.
      int declIdx = index - NativeOutputCount();
      if (declIdx < 0)
         return nullptr;
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx)
         {
            if (p.typeName == "image")
               return nullptr;
            return (declIdx < Field::PinTable::kMaxDeclaredPins) ? &mDeclaredOutputMods[declIdx] : nullptr;
         }
         seen++;
      }
      return nullptr;
   }

   bool exposeAuxTexture = false;
   // Transient (not saved) - see FieldElementNode::pinRefusal for the
   // rationale; identical cable-orphaning refusal policy (decision 4).
   std::string pinRefusal;

   const Field::PinTable& OutputPinTable() const { return mOutputPins; }
   const Field::PinTable& InputPinTable() const { return mInputPins; }

   // Dynamic pins, Phase 2b (build step 13, §5.7): kernel `input image`
   // decls, one ImageCable each, wired at main.cpp input slots 0..N-1 (no
   // native pin ahead of them - see the device-catalog simplification's
   // removal of the native "src" pin) - see main.cpp's InputCountFor/CableFor
   // FieldPixelNode branches. Only image-typed declared inputs get a real
   // cable; a declared scalar/vector input is tracked in mInputPins for
   // refusal/save-load purposes only (its value is not read by the kernel
   // yet - same deferred-runtime-value scoping as declared outputs).
   int DeclaredImageInputCount() const
   {
      int n = 0;
      for (const auto& p : mInputPins.Pins())
         if (p.isDeclared && p.typeName == "image") n++;
      return n;
   }
   // `i` is 0-based among currently-declared image inputs only (not the
   // same numbering as mInputPins.Pins() itself, which also holds
   // non-image declared inputs).
   ImageCable* DeclaredImageInput(int i)
   {
      if (i < 0 || i >= Field::PinTable::kMaxDeclaredPins) return nullptr;
      int seen = 0;
      for (const auto& p : mInputPins.Pins())
      {
         if (!p.isDeclared || p.typeName != "image") continue;
         if (seen == i) return &mDeclaredImageInputs[i];
         seen++;
      }
      return nullptr;
   }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Float("width", width);
      v.Float("height", height);
      v.Bool("animate", animate);
      v.Bool("exposeAuxTexture", exposeAuxTexture);
      mParamTable.VisitParams(v);
      // Dynamic pins, Phase 2b (build step 13, §5.1 step 8) - see
      // FieldElementNode::VisitParams's identical block for the rationale.
      std::string outPins = mOutputPins.SerializePinMap();
      std::string inPins = mInputPins.SerializePinMap();
      v.Text("__outputPins", outPins);
      v.Text("__inputPins", inPins);
      mOutputPins.DeserializePinMap(outPins);
      mInputPins.DeserializePinMap(inPins);
   }

   // Compilation & UI
   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   unsigned int Program() const { return mProgram; }
   const Field::GlslEmitResult& EmitResult() const { return mEmitResult; }
   const Field::PixelIRProgram& IR() const { return mIR; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::PixelStateBank& State() const { return mState; }
   Field::PixelStateBank& State() { return mState; }
   int BranchCount() const { return mEmitResult.branchCount; }
   int StateCellCount() const { return (int)mIR.declaredStates.size(); }
   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);

   // Field build step 17 (.infdev device files) - see FieldElementNode's
   // identical pair for the rationale.
   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   std::string code;
   float width = 1024.0f;
   float height = 1024.0f;
   bool animate = true;
   int presetIndex = 0;

private:
   unsigned int mProgram = 0;
   std::string mLastError;
   std::string mNotice;
   int mLastCookFrame = -1;

   GLUtil::Fbo mOut;
   Field::PixelStateBank mState;
   Field::ParamTable mParamTable;
   Field::PixelIRProgram mIR;
   Field::GlslEmitResult mEmitResult;
   std::vector<int> mUniformLocs;

   // Dynamic pins, Phase 2b (build step 13) - see FieldElementNode's
   // identical members for the full rationale.
   Field::PinTable mOutputPins;
   Field::PinTable mInputPins;
   struct DeclaredOutputPlaceholder : public IModulator
   {
      float Value01() override { return 0.0f; }
   };
   DeclaredOutputPlaceholder mDeclaredOutputMods[Field::PinTable::kMaxDeclaredPins];
   ImageCable mDeclaredImageInputs[Field::PinTable::kMaxDeclaredPins];

   int mLocRes = -1;
   int mLocT = -1;
   int mLocDt = -1;
   int mLocFrame = -1;
   int mLocAge = -1;
   // Cooks since mState was last cleared. Feeds the `age` reserved name, so a
   // kernel can seed itself exactly once.
   float mStateAge = 0.0f;
   int mLocSrcTex = -1;
   int mLocSrcAlpha = -1;
   int mLocOutMode = -1;
   int mLocStateBank0 = -1;

   unsigned long long mLastResetEpoch = 0;
   int mNodeIndex = -1;
   float mLastEvalT = -999999.0f;
};

#pragma once

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "field/FieldIR.h"
#include "field/GlslBackend.h"
#include "field/PixelState.h"
#include "field/ParamTable.h"
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
   INode* BypassSource() override { return input.GetSource(); }
   const char* InputLabel(int) const override { return "src"; }

   int OutputCount() const override { return exposeAuxTexture ? 2 : 1; }
   const char* OutputLabel(int index) const override { return index == 1 ? "state" : "out"; }

   bool exposeAuxTexture = false;
   // Transient (not saved) - see FieldElementNode::pinRefusal for the
   // rationale; identical cable-orphaning refusal policy (decision 4).
   std::string pinRefusal;

   // The one-and-only image input ("src" in the kernel), wired the same way
   // as every other image-consuming node - see CableFor/InputCountFor in
   // main.cpp. Was a bare INode* before this pin was actually registered
   // there, which meant nothing could ever be connected to it.
   ImageCable& TextureInput() { return input; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Float("width", width);
      v.Float("height", height);
      v.Bool("animate", animate);
      v.Bool("exposeAuxTexture", exposeAuxTexture);
      mParamTable.VisitParams(v);
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

   ImageCable input;
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

   int mLocRes = -1;
   int mLocT = -1;
   int mLocDt = -1;
   int mLocFrame = -1;
   int mLocSrcTex = -1;
   int mLocSrcAlpha = -1;
   int mLocOutMode = -1;
   int mLocStateBank0 = -1;

   unsigned long long mLastResetEpoch = 0;
   int mNodeIndex = -1;
   float mLastEvalT = -999999.0f;
};

#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Compositing node: takes two upstream image cables and blends them using the
// full Affinity-style blend-mode list (Normal..Erase, ~30 modes) as one int
// uniform switch in a single shader - cheap to grow since it's just shader math,
// no new node classes.
class BlendNode : public INode
{
public:
   static INode* Create() { return new BlendNode(); }
   static const std::vector<std::string>& ModeNames();

   ~BlendNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   ImageCable& InputA() { return mInputA; }
   ImageCable& InputB() { return mInputB; }
   INode* BypassSource() override
   {
      return mInputA.IsConnected() ? mInputA.GetSource() : mInputB.GetSource();
   }
   int& ModeIndex() { return mModeIndex; }
   float& Mix() { return mMix; }
   unsigned int GetOutputFormat() const override { return mOut.internalFormat; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mModeIndex); v.Float("mix", mMix);
      v.Int("alphaOp", alphaOp); v.Int("sizeFrom", sizeFrom);
      v.Int("customW", customW); v.Int("customH", customH);
      v.Int("preFit", preFit); v.Int("justifyH", justifyH); v.Int("justifyV", justifyV);
      v.Int("extendB", extendB); v.Int("format", format);
      v.Float("bTx", bTx); v.Float("bTy", bTy); v.Float("bScale", bScale); v.Float("bRotate", bRotate);
   }

   // Turbo (TOP parity, Composite TOP). Defaults reproduce the old Blend:
   // output = A's size, B stretched over it, no transform, 8-bit.
   enum PreFit { kStretch, kFit, kFill, kNative, kFitH, kFitV };
   static const std::vector<std::string>& AlphaOpNames();
   static const std::vector<std::string>& SizeFromNames();
   static const std::vector<std::string>& PreFitNames();
   static const std::vector<std::string>& JustifyHNames();
   static const std::vector<std::string>& JustifyVNames();
   static const std::vector<std::string>& ExtendNames();
   static const std::vector<std::string>& FormatNames();
   int alphaOp = 0;  // 0 = blend mode only, else Over/Under/Inside/Outside/Atop/Xor of B against A
   int sizeFrom = 0; // 0 = A (B when A is not patched), 1 = B, 2 = custom
   int customW = 1920;
   int customH = 1080;
   int preFit = kStretch;
   int justifyH = 1; // left, centre, right
   int justifyV = 1; // bottom, centre, top
   int extendB = 1;  // GLUtil::Extend for B outside its placed rectangle (Zero)
   int format = 0;   // 8-bit, 16f, 32f, A's format
   float bTx = 0.0f; // B's translate, fraction of the output
   float bTy = 0.0f;
   float bScale = 1.0f;
   float bRotate = 0.0f; // degrees

private:
   bool EnsureShader();

   ImageCable mInputA;
   ImageCable mInputB;
   int mModeIndex = 0;
   float mMix = 1.0f;

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   // Turbo: skip-if-unchanged (see CookSignature in INode.h)
   CookSignature mBuiltSig;
   bool mHasBuilt = false;
   unsigned long long mRevision = 0;
};

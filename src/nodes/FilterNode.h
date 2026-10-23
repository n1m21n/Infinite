#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "FilterDefs.h"

// Single-input, single-output shader-pass node. One class serves every entry
// in the FilterDefs table (Effects + Color/Compositing) - the whole point of
// the declarative FilterDef approach: dozens of Affinity-style modules, each
// spawnable as its own node with its own params, without a class per effect.
class FilterNode : public INode
{
public:
   // Compiles one not-yet-compiled effect shader stage; false when every
   // def is warm. Called once a frame after startup so the first add of an
   // effect links instead of compiling (a blur's three stages cost a frame).
   static bool WarmNextShader();

   // `def` must outlive the node - defs live in the static vector returned by GetFilterDefs().
   static INode* CreateFor(const FilterDef& def) { return new FilterNode(def); }

   explicit FilterNode(const FilterDef& def);
   ~FilterNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   ImageCable& Input2() { return mInput2; }
   const FilterDef& Def() const { return mDef; }

   // component in [0,2] for Color params, 0 for Float/Int/Bool.
   float GetParamValue(size_t paramIndex, int component = 0) const { return mParamValues[paramIndex][component]; }
   void SetParamValue(size_t paramIndex, int component, float value) { mParamValues[paramIndex][component] = value; }

   // Direct handle for widgets that edit in place (colour picker).
   float* ParamPtr(size_t paramIndex) { return mParamValues[paramIndex].data(); }

   // Params added after a patch was saved (linear light, the corrected
   // modes) are missing from it: they load at their legacyVal so the patch
   // keeps the look it was made with. New nodes keep the defaults.
   void BeforeLoadParams() override
   {
      for (size_t i = 0; i < mDef.params.size(); i++)
         if (mDef.params[i].hasLegacy)
            mParamValues[i][0] = mDef.params[i].legacyVal;
      mHeadroom = false;
   }

   // Light above 1.0 survives this node (GLUtil::WithClampedOutput). On for
   // new nodes; patches saved before it existed load it off and keep clipping.
   bool Headroom() const { return mHeadroom; }
   void SetHeadroom(bool on) { mHeadroom = on; }

   // Keyed by uniformName rather than by index: the FilterDef table is shared
   // across every FilterNode of this kind, so the name is the one thing stable
   // enough to survive across app versions if params are reordered.
   void VisitParams(ParamVisitor& v) override
   {
      for (size_t i = 0; i < mDef.params.size(); i++)
      {
         const FilterParamDef& def = mDef.params[i];
         if (def.type == FilterParamDef::Type::Color)
            v.Color(def.uniformName.c_str(), mParamValues[i].data());
         else
            v.Float(def.uniformName.c_str(), mParamValues[i][0]);
      }
      // A patch file, OSC or RPC set stores any finite float; the knobs bind
      // only the UI. Shaders trust the def's range (dither loops uMode + 1
      // times, so 1e9 hung the GPU), so hold every param to it, as the knobs do.
      for (size_t i = 0; i < mDef.params.size(); i++)
         if (mDef.params[i].type != FilterParamDef::Type::Color && mDef.params[i].minVal < mDef.params[i].maxVal)
            mParamValues[i][0] = std::clamp(mParamValues[i][0], mDef.params[i].minVal, mDef.params[i].maxVal);
      v.Bool("headroom", mHeadroom);
   }

private:
   // Uniform locations are fixed for the life of a linked program, so they're
   // looked up once in EnsureShader() rather than every cook - glGetUniformLocation
   // is a name-hash lookup into the driver, not free, and every FilterNode pays it
   // every uncached cook otherwise.
   struct PassLocs
   {
      GLint src = -1;
      GLint src2 = -1;
      GLint hasSrc2 = -1;
      GLint pass = -1;
      GLint texel = -1;
      GLint time = -1;
      GLint converted = -1;
      GLint headroom = -1;
      std::vector<GLint> params;
   };

   bool EnsureShader();
   void LookupLocs(unsigned int program, PassLocs& locs) const;
   void BindUniforms(const PassLocs& locs, unsigned int srcTex, unsigned int srcTex2,
                     unsigned int passTex, float time, int converted = 0) const;

   // Everything the shader pass's output depends on. Reusing mOut's contents
   // is only safe when all of this is identical to the last time it ran -
   // mirrors GeometryOpNode::Signature (GeometryOpNodes.cpp).
   struct Signature
   {
      unsigned long long upstreamRev = 0;
      unsigned long long upstreamRev2 = 0;
      int width = 0;
      int height = 0;
      std::vector<std::array<float, 3>> params;
      // The exact uTime value uploaded, 0 for filters that don't read it.
      // Transport time only advances while playing, so a time-driven filter
      // on a stopped transport caches like any other instead of re-rendering
      // an identical image and forcing everything downstream to recook.
      float time = 0.0f;
      bool headroom = true;

      bool operator==(const Signature& o) const
      {
         return upstreamRev == o.upstreamRev && upstreamRev2 == o.upstreamRev2 &&
                width == o.width && height == o.height && params == o.params &&
                time == o.time && headroom == o.headroom;
      }
   };

   const FilterDef& mDef;
   std::vector<std::array<float, 3>> mParamValues;

   ImageCable mInput;
   ImageCable mInput2; // only used when Def().inputs == 2
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   unsigned int mPreProgram = 0;
   unsigned int mConvProgram = 0;
   int mLinearParam = -1;  // index of uLinear in mDef.params, or -1
   int mQualityParam = -1; // index of uQuality, or -1
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   PassLocs mMainLocs;
   PassLocs mPreLocs;
   PassLocs mConvLocs;

   // Filters whose shader reads uTime are inherently animated, so the
   // uploaded time value is part of Signature.
   bool mUsesTime = false;
   bool mHeadroom = true;
   bool mHasBuilt = false;
   Signature mBuilt;
   unsigned long long mRevision = 0;
};

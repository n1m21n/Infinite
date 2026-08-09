#pragma once

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
   }

private:
   bool EnsureShader();

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

      bool operator==(const Signature& o) const
      {
         return upstreamRev == o.upstreamRev && upstreamRev2 == o.upstreamRev2 &&
                width == o.width && height == o.height && params == o.params;
      }
   };

   const FilterDef& mDef;
   std::vector<std::array<float, 3>> mParamValues;

   ImageCable mInput;
   ImageCable mInput2; // only used when Def().inputs == 2
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   // Filters whose shader reads uTime are inherently animated - caching them
   // on a param/upstream signature alone would freeze the animation, so they
   // always re-render (same as the pre-caching behavior).
   bool mUsesTime = false;
   bool mHasBuilt = false;
   Signature mBuilt;
   unsigned long long mRevision = 0;
};

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

   ImageCable& Input() { return mInput; }
   ImageCable& Input2() { return mInput2; }
   const FilterDef& Def() const { return mDef; }

   // component in [0,2] for Color params, 0 for Float/Int/Bool.
   float GetParamValue(size_t paramIndex, int component = 0) const { return mParamValues[paramIndex][component]; }
   void SetParamValue(size_t paramIndex, int component, float value) { mParamValues[paramIndex][component] = value; }

   // Direct handle for widgets that edit in place (colour picker).
   float* ParamPtr(size_t paramIndex) { return mParamValues[paramIndex].data(); }

private:
   bool EnsureShader();

   const FilterDef& mDef;
   std::vector<std::array<float, 3>> mParamValues;

   ImageCable mInput;
   ImageCable mInput2; // only used when Def().inputs == 2
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};

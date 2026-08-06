#pragma once

#include <string>

#include "INode.h"
#include "GLUtil.h"

// User-authored formula source. The user types a GLSL expression body; Apply()
// wraps it in a standard preamble and compiles at runtime. A failed compile
// keeps the last working program and surfaces the error text in the UI, so a
// typo never blanks the graph or crashes the app.
class FormulaNode : public INode
{
public:
   static INode* Create() { return new FormulaNode(); }

   FormulaNode();
   ~FormulaNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   // Recompiles from `formula`. Returns false and fills LastError() on failure,
   // leaving the previously-working program active.
   bool Apply();
   const std::string& LastError() const { return mLastError; }

   // Bound directly by the ImGui params panel.
   std::string formula;
   float width = 1024.0f;
   float height = 1024.0f;
   float knobA = 0.5f;
   float knobB = 0.5f;
   float knobC = 0.5f;
   float knobD = 0.5f;
   bool animate = true;

private:
   unsigned int mProgram = 0;
   std::string mLastError;
   float mClock = 0.0f;

   GLUtil::Fbo mOut;
   int mLastCookFrame = -1;
};

#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
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

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);
   const std::string& LastError() const { return mLastError; }

   void VisitParams(ParamVisitor& v) override;

   // Turbo 0.56: Shadertoy mode. Code that defines mainImage(out vec4, in vec2)
   // compiles as a whole file with Shadertoy's uniforms (iTime, iResolution,
   // iFrame, iTimeDelta, iFrameRate, iMouse, iDate, iChannel0-3,
   // iChannelResolution, iChannelTime, iSampleRate); iChannelN is inN.
   // Otherwise the code is the body of shape(uv, p, t) as before.
   bool ShadertoyMode() const { return mShadertoy; }

   // Turbo 0.56: `uniform float name; // lo..hi = default` lines in the code
   // become knobs (up to kMaxUserParams), saved as "uniform_<name>".
   struct UserParam
   {
      std::string name;
      float lo = 0.0f, hi = 1.0f, def = 0.5f, value = 0.5f;
   };
   static constexpr int kMaxUserParams = 16;
   std::vector<UserParam> userParams;
   // Re-reads the uniform lines of `formula`; values of names that stay are kept.
   void ParseUserParams();

   // Turbo 0.56: Shadertoy-style buffers. floatBuffer renders to RGBA32F and
   // keeps alpha (state, like a Buffer A); selfChannel (0-3, -1 off) feeds the
   // node's own previous frame to that iChannel / uIn instead of the input.
   bool floatBuffer = false;
   int selfChannel = -1;
   // iMouse: 0..1 over the output, CV-able; zw follow Shadertoy (negative when up).
   float mouseX = 0.5f;
   float mouseY = 0.5f;
   bool mouseDown = false;

   // Turbo (TOP parity, GLSL TOP): image inputs, read in the formula as
   // uIn0..uIn3 (uHasIn0..3 says which are patched, uInRes0..3 their size).
   static constexpr int kInputs = 4;
   ImageCable& Input(int slot) { return mInputs[slot]; }
   const char* InputLabel(int slot) const override;
   INode* BypassSource() override { return mInputs[0].GetSource(); }
   bool matchInput = false; // render at in0's size instead of width x height

   // Bound directly by the ImGui params panel.
   std::string formula;
   float width = 1024.0f;
   float height = 1024.0f;
   float knobA = 0.5f;
   float knobB = 0.5f;
   float knobC = 0.5f;
   float knobD = 0.5f;
   bool animate = true;
   int presetIndex = 0;

private:
   unsigned int mProgram = 0;
   std::string mLastError;
   float mClock = 0.0f;
   float mLastClock = 0.0f;
   int mFrame = 0;          // iFrame: frames since the last compile or resize
   bool mShadertoy = false;
   std::string mParsedFormula = "\x01"; // formula text userParams were read from

   GLUtil::Fbo mOut;
   GLUtil::Fbo mPrev;       // the last frame, for selfChannel (ping-pong)
   int mLastCookFrame = -1;
   ImageCable mInputs[kInputs];
};

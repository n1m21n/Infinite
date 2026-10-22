#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "GLUtil.h"
#include "ImageCable.h"
#include "field/FieldDevice.h"

// User-authored formula source. The user types a GLSL expression body; Apply()
// wraps it in a standard preamble and compiles at runtime. A failed compile
// keeps the last working program and surfaces the error text in the UI, so a
// typo never blanks the graph or crashes the app.
//
// Shadertoy mode runs code pasted from shadertoy.com as is: a preamble defines
// iTime, iTimeDelta, iFrame, iResolution, iMouse, iDate and iChannel0-3 and calls
// mainImage. iChannel0-3 are four image pins; an unwired channel reads buffer A-D
// (the same letter's number), the usual Shadertoy setup, so a buffer reading its own
// channel gets its previous frame. Buffers are float16. A `uniform float name;
// // lo..hi = default` line becomes a knob, up to 8, in source order.
// The idea (Shadertoy paste support) comes from Ricardo Palmieri's Infinite-Turbo
// fork; this is a fresh implementation.
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
   std::string CodeError() const override { return mLastError; }

   // Field build step 17 (.infdev device files). FormulaNode predates Field
   // and is not itself a Field node (field-integration skill §1) - its
   // "params" are typed `=` expressions via Expression::Evaluate, not
   // ParamTable entries, so device.params stays empty/unused here by design
   // (plan §7). `formula` alone carries everything meaningful.
   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("formula", formula);
      v.Float("width", width); v.Float("height", height);
      v.Float("knobA", knobA); v.Float("knobB", knobB);
      v.Float("knobC", knobC); v.Float("knobD", knobD);
      v.Bool("animate", animate);
      v.Int("mode", mode);
      v.Int("buffers", buffers);
      static const char* kPassKeys[kPasses] = { "stImage", "stBufA", "stBufB", "stBufC", "stBufD", "stCommon" };
      for (int i = 0; i < kPasses; i++)
         v.Text(kPassKeys[i], passCode[i]);
      static const char* kKnobKeys[kMaxKnobs] = { "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7" };
      for (int i = 0; i < kMaxKnobs; i++)
         v.Float(kKnobKeys[i], stKnob[i]);
      v.Text("stKnobNames", mKnobNames);
   }

   // ---- Shadertoy mode ----
   enum Mode { kExpression = 0, kShadertoy = 1 };
   enum Pass { kImage = 0, kBufA = 1, kBufB = 2, kBufC = 3, kBufD = 4, kCommon = 5, kPasses = 6 };
   static constexpr int kChannels = 4;
   static constexpr int kMaxKnobs = 8;
   static const std::vector<std::string>& ModeNames();     // Expression, Shadertoy
   static const std::vector<std::string>& BufferNames();   // none, A, A + B, A - D
   static const char* PassName(int pass);                  // "Image", "Buffer A", ..., "Common"
   int BufferCount() const { return buffers <= 0 ? 0 : buffers == 1 ? 1 : buffers == 2 ? 2 : 4; }
   bool IsShadertoy() const { return mode == kShadertoy; }

   struct Knob
   {
      std::string name;
      float lo = 0.0f, hi = 1.0f, def = 0.0f;
   };
   // Knobs declared by the current code; stKnob[i] is knob i's value.
   const std::vector<Knob>& Knobs() const { return mKnobs; }

   ImageCable& Channel(int i) { return mChannel[i]; }
   int ChannelCount() const { return IsShadertoy() ? kChannels : 0; }
   const char* InputLabel(int slot) const override;

   // Pixel coordinates over the preview, Shadertoy's convention: xy = where the
   // button is held, zw = where it went down (negative z once released).
   float mouse[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

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
   int mode = kExpression;
   int buffers = 0;
   std::string passCode[kPasses];
   float stKnob[kMaxKnobs] = {};

private:
   bool ApplyShadertoy();
   void CookShadertoy(int frameId);
   void ParseKnobs();
   std::string MapError(const std::string& error, int pass, int preambleLines) const;

   unsigned int mPassProgram[kBufD + 1] = {};
   GLUtil::Fbo mBuf[4][2];
   int mBufCur[4] = {};
   ImageCable mChannel[kChannels];
   std::vector<Knob> mKnobs;
   std::string mKnobNames;   // '|' joined, what stKnob[] was set up for
   int mFrame = 0;
   float mLastTime = 0.0f;

   unsigned int mProgram = 0;
   std::string mLastError;
   float mClock = 0.0f;

   GLUtil::Fbo mOut;
   int mLastCookFrame = -1;
};

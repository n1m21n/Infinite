#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"

// Modulators produce a control value, not an image, so they report a zero output
// texture; the editor draws a value meter for them instead of a preview. All three
// are tempo-synced: rates are in beats, so changing the global BPM retimes them.

class LFONode : public INode, public IModulator
{
public:
   static INode* Create() { return new LFONode(); }
   static const std::vector<std::string>& ShapeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   int shape = 0;          // index into ShapeNames()
   float rateBeats = 4.0f; // one full cycle every N beats
   float phase = 0.0f;     // 0..1
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shape", shape); v.Float("rateBeats", rateBeats);
      v.Float("phase", phase); v.Float("low", low); v.Float("high", high);
   }
};

// A fixed value, as a modulator.
//
// Exists because Math has no way to express "multiply this LFO by 0.5" - both
// its inputs are modulator pins, so a literal needs a node to come from. Macro
// Knob is close but is meant to be performed; this is meant to be set once and
// left, and it reads in the destination's own units rather than 0..1.
class ConstantNode : public INode, public IModulator
{
public:
   static INode* Create() { return new ConstantNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override
   {
      // Clamped, since every modulator is contractually 0..1 and a destination
      // maps that onto its own range.
      return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
   }

   float value = 0.5f;

   void VisitParams(ParamVisitor& v) override { v.Float("value", value); }
};

class RandomNode : public INode, public IModulator
{
public:
   // Each spawned Random starts on a different seed. Two of them dropped on the
   // canvas should not march in lockstep, which is what a shared default does.
   static float NextSeed() { static int sSpawnCounter = 0; return (float)(++sSpawnCounter) * 7.3f; }

   static INode* Create()
   {
      auto* node = new RandomNode();
      node->seed = NextSeed();
      return node;
   }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   float rateBeats = 1.0f; // new target value every N beats
   float smooth = 0.5f;    // 0 = stepped, 1 = fully interpolated
   float low = 0.0f;
   float high = 1.0f;

   // Without this every Random node in a patch runs the identical sequence:
   // the value is a hash of the transport step, and the step is global. Each
   // instance gets a different default so two freshly spawned Random nodes are
   // uncorrelated, while the same seed still reproduces the same run exactly.
   float seed = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("rate", rateBeats); v.Float("smooth", smooth);
      v.Float("low", low); v.Float("high", high); v.Float("seed", seed);
   }

private:
   float ValueForStep(long long step) const;
};

// An 8-step sequencer: the value walks through eight sliders and repeats.
class PatternNode : public INode, public IModulator
{
public:
   static const int kSteps = 8;

   static INode* Create() { return new PatternNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   int CurrentStep() const { return mCurrentStep; }

   float steps[kSteps] = { 0.0f, 0.15f, 0.35f, 0.6f, 1.0f, 0.6f, 0.35f, 0.15f };
   int length = kSteps;    // how many of the eight are used before looping
   float stepBeats = 1.0f; // one step every N beats
   bool smoothSteps = false;
   float low = 0.0f;
   float high = 1.0f;

   void VisitParams(ParamVisitor& v) override
   {
      static const char* kStepKeys[kSteps] = {
         "step0", "step1", "step2", "step3", "step4", "step5", "step6", "step7"
      };
      for (int i = 0; i < kSteps; i++)
         v.Float(kStepKeys[i], steps[i]);
      v.Int("length", length); v.Float("stepBeats", stepBeats);
      v.Bool("smoothSteps", smoothSteps);
      v.Float("low", low); v.Float("high", high);
   }

private:
   int mCurrentStep = 0;
};

// Combines two other modulators with an arithmetic operation.
class MathNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MathNode(); }
   static const std::vector<std::string>& OpNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   // The editor patches other modulators in here; either may be null, in which
   // case the corresponding constant is used instead.
   IModulator* inputA = nullptr;
   IModulator* inputB = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &inputA : (slot == 1 ? &inputB : nullptr); }
   int ModulatorInputCount() const override { return 2; }

   int op = 0;
   float constantA = 0.5f; // used when nothing is patched into A
   float constantB = 0.5f;
   float gain = 1.0f;
   float offset = 0.0f;
   bool clampOutput = true;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op); v.Float("constantA", constantA); v.Float("constantB", constantB);
      v.Float("gain", gain); v.Float("offset", offset); v.Bool("clampOutput", clampOutput);
   }
};

// Outputs 1 or 0 based on a comparison between two other modulators.
class CompareNode : public INode, public IModulator
{
public:
   static INode* Create() { return new CompareNode(); }
   static const std::vector<std::string>& OpNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   IModulator* inputA = nullptr;
   IModulator* inputB = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &inputA : (slot == 1 ? &inputB : nullptr); }
   int ModulatorInputCount() const override { return 2; }

   int op = 0;
   float constantA = 0.5f; // used when nothing is patched into A
   float constantB = 0.5f;
   float tolerance = 0.001f; // equality/inequality treat |A-B| <= tolerance as equal

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op); v.Float("constantA", constantA); v.Float("constantB", constantB);
      v.Float("tolerance", tolerance);
   }
};

// Remaps a modulator from one range to another.
class RangeToRangeNode : public INode, public IModulator
{
public:
   static INode* Create() { return new RangeToRangeNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f; // used when nothing is patched
   float inLow = 0.0f, inHigh = 1.0f;
   float outLow = 0.0f, outHigh = 1.0f;
   bool clampOutput = true;

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("inLow", inLow); v.Float("inHigh", inHigh);
      v.Float("outLow", outLow); v.Float("outHigh", outHigh); v.Bool("clampOutput", clampOutput);
   }
};

// Exponential moving average over another modulator, to damp jittery sources.
class SmoothNode : public INode, public IModulator
{
public:
   static INode* Create() { return new SmoothNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f;
   float amount = 0.85f; // 0 = pass-through, close to 1 = heavy smoothing

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("amount", amount);
   }

private:
   float mLast = -1.0f;      // sentinel: not yet initialized
   double mLastBeats = -1.0; // beat at which mLast was last updated
};

// Mirrors a modulator around a low/high pivot. Not a flat 1-v, so it does the
// right thing fed something already outside 0..1 (e.g. an unclamped Math output).
class InvertNode : public INode, public IModulator
{
public:
   static INode* Create() { return new InvertNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   const char* InputLabel(int) const override { return "in"; }

   float Value01() override;

   IModulator* input = nullptr;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }

   float constantIn = 0.5f;
   float low = 0.0f, high = 1.0f; // default 0..1 gives classic 1-v

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("constantIn", constantIn); v.Float("low", low); v.Float("high", high);
   }
};

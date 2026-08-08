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
   static INode* Create()
   {
      static int sSpawnCounter = 0;
      auto* node = new RandomNode();
      node->seed = (float)(++sSpawnCounter) * 7.3f;
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

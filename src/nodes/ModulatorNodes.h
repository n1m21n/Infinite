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
};

class RandomNode : public INode, public IModulator
{
public:
   static INode* Create() { return new RandomNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   float rateBeats = 1.0f; // new target value every N beats
   float smooth = 0.5f;    // 0 = stepped, 1 = fully interpolated
   float low = 0.0f;
   float high = 1.0f;

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
};

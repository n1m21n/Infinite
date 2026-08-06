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

class PatternNode : public INode, public IModulator
{
public:
   static INode* Create() { return new PatternNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   // Re-parses `text` into `steps`. Accepts numbers separated by commas or spaces.
   void Reparse();
   const std::vector<float>& Steps() const { return mSteps; }
   int CurrentStep() const { return mCurrentStep; }

   std::string text = "0, 0.25, 0.5, 1, 0.5, 0.25";
   float stepBeats = 1.0f; // one step every N beats
   bool smoothSteps = false;
   float low = 0.0f;
   float high = 1.0f;

private:
   std::vector<float> mSteps;
   int mCurrentStep = 0;
};

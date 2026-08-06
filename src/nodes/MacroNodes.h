#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "Modulation.h"

// A hand-driven knob exposed as a modulator. One macro can feed any number of
// parameters at once - each destination maps the macro's 0..1 into its own
// range, so a single knob can sweep a blur radius, an opacity and a hue shift
// together without them needing to share units.
class MacroKnobNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroKnobNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}

   float Value01() override;

   float value = 0.5f;
   float curve = 1.0f;   // <1 eases in, >1 eases out
   bool invert = false;
   std::string label = "Macro";
};

// A macro XY pad: two independent normalised outputs (X and Y) from one
// control surface, with the same record / play / loop path behaviour as the
// resynth pad so a gesture can be performed once and replayed in time.
class MacroXYNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroXYNode(); }

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   // Output 0 is X, output 1 is Y.
   int OutputCount() const override { return 2; }
   const char* OutputLabel(int index) const override { return index == 0 ? "x" : "y"; }

   float Value01() override; // X
   IModulator* YOutput() { return &mYOutput; }
   IModulator* ModulatorOutput(int index) override
   {
      return index == 1 ? static_cast<IModulator*>(&mYOutput) : static_cast<IModulator*>(this);
   }

   struct PadPoint
   {
      float x = 0.5f;
      float y = 0.5f;
      double beat = 0.0;
   };

   void StartRecording();
   void StopRecording();
   void PlayPath();
   void StopPath();
   void ClearPath();
   bool IsRecordingPath() const { return mRecording; }
   bool IsPlayingPath() const { return mPlaying; }
   const std::vector<PadPoint>& Path() const { return mPath; }

   float padX = 0.5f;
   float padY = 0.5f;
   bool loopPath = true;
   float speed = 1.0f;

private:
   // Adapter so the Y axis can be handed out as its own IModulator without a
   // second node.
   struct YAxis : public IModulator
   {
      MacroXYNode* owner = nullptr;
      float Value01() override { return owner ? owner->padY : 0.0f; }
   };

   void UpdatePath();

   YAxis mYOutput;
   std::vector<PadPoint> mPath;
   bool mRecording = false;
   bool mPlaying = false;
   double mStartBeat = 0.0;
   int mLastCookFrame = -1;

public:
   MacroXYNode() { mYOutput.owner = this; }
};

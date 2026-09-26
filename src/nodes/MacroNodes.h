#pragma once

#include <algorithm>
#include <cstdint>

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

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("value", value); v.Float("curve", curve);
      v.Bool("invert", invert); v.Text("label", label);
   }
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

   void VisitParams(ParamVisitor& v) override
   {
      v.Float("padX", padX); v.Float("padY", padY);
      v.Bool("loopPath", loopPath); v.Float("speed", speed);
      // Encoded the same way as ResynthNode's pad path - see there for why the
      // decode-on-save is a harmless no-op.
      std::string encoded = EncodePath(mPath);
      v.Text("path", encoded);
      mPath = DecodePath(encoded);
   }

private:
   static std::string EncodePath(const std::vector<PadPoint>& path);
   static std::vector<PadPoint> DecodePath(const std::string& s);
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

// ---- macro family (ported from upstream Infinite) ---------------------------
// Hand-driven controls exposed as modulators; each destination maps the
// 0..1 output into its own range. All are MIDI-learnable from their body.

// A vertical fader.
class MacroSliderNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroSliderNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override { return std::clamp(value, 0.0f, 1.0f); }

   float value = 0.5f;
   std::string label = "Slider";
   void VisitParams(ParamVisitor& v) override { v.Float("value", value); v.Text("label", label); }
};

// A centre-detent knob, -1..+1; 0.5 out is the centre.
class MacroBipolarKnobNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroBipolarKnobNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override { return std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f); }

   float value = 0.0f;
   std::string label = "Bipolar";
   void VisitParams(ParamVisitor& v) override { v.Float("value", value); v.Text("label", label); }
};

// A latching on/off switch (0 or 1).
class MacroToggleNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroToggleNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override { return state ? 1.0f : 0.0f; }

   bool state = false;
   std::string label = "Toggle";
   void VisitParams(ParamVisitor& v) override { v.Bool("state", state); v.Text("label", label); }
};

// A momentary bang pad: 1 while held, 0 otherwise.
class MacroTriggerNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroTriggerNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override { return pressed ? 1.0f : 0.0f; }

   bool pressed = false; // runtime only
   float flash = 0.0f;   // UI decay
   std::string label = "Trigger";
   void VisitParams(ParamVisitor& v) override { v.Text("label", label); }
};

// A number box: drag or type a value between min and max.
class MacroNumBoxNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroNumBoxNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override
   {
      return maxVal > minVal ? std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f) : 0.0f;
   }

   float value = 0.0f;
   float minVal = 0.0f;
   float maxVal = 100.0f;
   float step = 1.0f;
   std::string label = "NumBox";
   void VisitParams(ParamVisitor& v) override
   {
      v.Float("value", value); v.Float("minVal", minVal);
      v.Float("maxVal", maxVal); v.Float("step", step);
      v.Text("label", label);
   }
};

// A row of 2..8 radio buttons; output steps evenly from 0 to 1.
class MacroRadioSelectorNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroRadioSelectorNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override
   {
      const int n = std::clamp(count, 2, 8);
      return std::clamp((float)selected / (float)(n - 1), 0.0f, 1.0f);
   }

   int selected = 0;
   int count = 8;
   std::string label = "Selector";
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("selected", selected); v.Int("count", count);
      v.Text("label", label);
   }
};

// An 8-step gate sequencer locked to the transport.
class MacroStepGateNode : public INode, public IModulator
{
public:
   static INode* Create() { return new MacroStepGateNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override;
   int CurrentStep() const { return mCurrentStep; }

   uint8_t pattern = 0b10011001;
   float rateBeats = 0.25f; // 16th notes
   std::string label = "Step Gate";
   void VisitParams(ParamVisitor& v) override
   {
      int patInt = (int)pattern;
      v.Int("pattern", patInt);
      pattern = (uint8_t)patInt;
      v.Float("rateBeats", rateBeats);
      v.Text("label", label);
   }

private:
   int mCurrentStep = 0;
};

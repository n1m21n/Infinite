#pragma once

#include <algorithm>
#include "core/INode.h"
#include "core/Modulation.h"

// Performance controls that emit normal 0..1 CV values. Any MIDI/OSC/CV
// modulator can be connected to their input without a special protocol path.
class UIButtonNode : public INode, public IModulator
{
public:
   static INode* Create() { return new UIButtonNode(); }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   float Value01() override;
   float DisplayValue() const;
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }
   const char* InputLabel(int) const override { return "cv"; }
   void Trigger();
   void Release();
   void SetPointerHeld(bool held);
   bool PointerHeld() const { return mHeld; }
   void HandlePointerFrame(bool pressedThisFrame, bool mouseDown);
   void SetMode(int newMode);
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode);
      v.Int("pulseFrames", pulseFrames);
      v.Bool("state", state);
   }

   IModulator* input = nullptr;
   int mode = 0; // 0 momentary, 1 pulse, 2 toggle
   int pulseFrames = 6;
   bool state = false;
private:
   bool mHeld = false;
   bool mLastInput = false;
   int mPulseFramesRemaining = 0;
   int mLastCookFrame = -1;
};

class UISliderNode : public INode, public IModulator
{
public:
   static INode* CreateHorizontal() { return new UISliderNode(false); }
   static INode* CreateVertical() { return new UISliderNode(true); }
   explicit UISliderNode(bool vertical = false) : isVertical(vertical) {}
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   float Value01() override { return input ? std::clamp(input->Value01(), 0.0f, 1.0f) : std::clamp(value, 0.0f, 1.0f); }
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   int ModulatorInputCount() const override { return 1; }
   const char* InputLabel(int) const override { return "cv"; }
   void VisitParams(ParamVisitor& v) override { v.Float("value", value); v.Bool("vertical", isVertical); }

   IModulator* input = nullptr;
   float value = 0.5f;
   bool isVertical = false;
};

class UIXYNode : public INode, public IModulator
{
public:
   static INode* Create() { return new UIXYNode(); }
   UIXYNode() { mY.owner = this; }
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int) override {}
   int OutputCount() const override { return 2; }
   const char* OutputLabel(int i) const override { return i == 0 ? "x" : "y"; }
   float Value01() override { return inputX ? std::clamp(inputX->Value01(), 0.0f, 1.0f) : x; }
   IModulator* ModulatorOutput(int i) override { return i == 1 ? static_cast<IModulator*>(&mY) : static_cast<IModulator*>(this); }
   IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &inputX : (slot == 1 ? &inputY : nullptr); }
   int ModulatorInputCount() const override { return 2; }
   const char* InputLabel(int i) const override { return i == 0 ? "x cv" : "y cv"; }
   void VisitParams(ParamVisitor& v) override { v.Float("x", x); v.Float("y", y); }

   IModulator* inputX = nullptr;
   IModulator* inputY = nullptr;
   float x = 0.5f;
   float y = 0.5f;
private:
   struct YOutput : IModulator { UIXYNode* owner = nullptr; float Value01() override { return owner->inputY ? std::clamp(owner->inputY->Value01(), 0.0f, 1.0f) : owner->y; } } mY;
};

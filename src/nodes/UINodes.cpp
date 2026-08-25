#include "UINodes.h"

float UIButtonNode::Value01()
{
   const bool incoming = input && input->Value01() > 0.5f;
   if (incoming && !mLastInput)
   {
      if (mode == 1) mPulseFramesRemaining = std::max(1, pulseFrames);
      else if (mode == 2) state = !state;
   }
   mLastInput = incoming;
   if (mode == 0) return (mHeld || incoming) ? 1.0f : 0.0f;
   if (mode == 1)
      return mPulseFramesRemaining > 0 ? 1.0f : 0.0f;
   return state ? 1.0f : 0.0f;
}

float UIButtonNode::DisplayValue() const
{
   if (mode == 0) return (mHeld || (input && input->Value01() > 0.5f)) ? 1.0f : 0.0f;
   if (mode == 1) return mPulseFramesRemaining > 0 ? 1.0f : 0.0f;
   return state ? 1.0f : 0.0f;
}

void UIButtonNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   if (mPulseFramesRemaining > 0)
      --mPulseFramesRemaining;
}

void UIButtonNode::Trigger()
{
   if (mode == 0) mHeld = true;
   else if (mode == 1) mPulseFramesRemaining = std::max(1, pulseFrames);
   else if (mode == 2) state = !state;
}

void UIButtonNode::Release()
{
   mHeld = false;
}

void UIButtonNode::SetPointerHeld(bool held)
{
   // Momentary follows the physical pointer every frame. It cannot remain
   // latched when a release happens outside the button or the window loses
   // focus, two cases where a one-shot deactivation event may be missed.
   mHeld = held;
}

void UIButtonNode::HandlePointerFrame(bool pressedThisFrame, bool mouseDown)
{
   if (pressedThisFrame)
      Trigger();

   if (mode == 0)
   {
      // Capture begins only on this button, but it remains owned by the node
      // until the physical mouse button is released. This deliberately does
      // not depend on ImGui's active-item lifetime inside imgui-node-editor.
      mHeld = mouseDown && (pressedThisFrame || mHeld);
   }
}

void UIButtonNode::SetMode(int newMode)
{
   mode = std::clamp(newMode, 0, 2);
   mHeld = false;
   mPulseFramesRemaining = 0;
   state = false;
}

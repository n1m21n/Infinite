#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Cycles between its connected inputs on a fixed interval, with an optional
// crossfade. The interval is expressed in beats or seconds and read from the
// Transport, so switching stops when the transport is paused.
class SwitcherNode : public INode
{
public:
   static const int kSlots = 4;

   static INode* Create() { return new SwitcherNode(); }
   static const std::vector<std::string>& UnitNames();

   ~SwitcherNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& Input(int slot) { return mInputs[slot]; }
   INode* BypassSource() override
   {
      for (int i = 0; i < kSlots; i++)
         if (mInputs[i].IsConnected())
            return mInputs[i].GetSource();
      return nullptr;
   }

   int ActiveSlot() const { return mActiveSlot; }

   int unit = 0;             // 0 = beats, 1 = seconds
   float interval = 4.0f;    // switch every N beats/seconds
   float crossfade = 0.0f;   // fade duration as a fraction of the interval
   bool manual = false;      // ignore the clock and hold manualSlot
   int manualSlot = 0;

private:
   bool EnsureShader();

   ImageCable mInputs[kSlots];
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
   int mActiveSlot = 0;
};

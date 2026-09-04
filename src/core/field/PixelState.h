#pragma once

#include "GLUtil.h"
#include <array>
#include <vector>

namespace Field
{
   class PixelStateBank
   {
   public:
      PixelStateBank();
      ~PixelStateBank() = default;

      // Build step 22 (OPEN-C): highPrecision selects RGBA32F over RGBA16F.
      // A kernel that reads its neighbours integrates for minutes; at 16F
      // Gray-Scott visibly drifts within seconds. A plain trails kernel
      // re-normalises every frame and stays correct at 16F, so it keeps the
      // cheaper bank. Changing the flag reallocates and clears.
      void Resize(int w, int h, bool highPrecision = false);
      bool HighPrecision() const { return mHighPrecision; }
      size_t BytesInUse() const;
      void Reset();
      void Swap();
      void BindReadUnits(unsigned int program, int samplerLoc);
      void ClearBoth(const float initRgba[4] = nullptr);

      GLUtil::Fbo& ReadFbo() { return mPair[mFront]; }
      GLUtil::Fbo& WriteFbo() { return mPair[1 - mFront]; }
      const GLUtil::Fbo& ReadFbo() const { return mPair[mFront]; }
      const GLUtil::Fbo& WriteFbo() const { return mPair[1 - mFront]; }

      unsigned int ReadTexture() const { return GLUtil::FboTexture(mPair[mFront]); }
      unsigned int CurrentOutputTexture() const { return GLUtil::FboTexture(mPair[mFront]); }

      int Width() const { return mW; }
      int Height() const { return mH; }
      int Front() const { return mFront; }
      bool NeedsClear() const { return mNeedsClear; }
      void SetNeedsClear(bool nc) { mNeedsClear = nc; }

   private:
      std::array<GLUtil::Fbo, 2> mPair;
      int mFront = 0;
      int mW = 0;
      int mH = 0;
      bool mNeedsClear = true;
      bool mHighPrecision = false;
   };
}

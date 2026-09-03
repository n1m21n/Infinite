#include "PixelState.h"
#include <algorithm>
#include "gl3.h"

namespace Field
{
   PixelStateBank::PixelStateBank()
   {
   }

   void PixelStateBank::Resize(int w, int h)
   {
      w = std::max(4, w);
      h = std::max(4, h);
      if (mW != w || mH != h)
      {
         mW = w;
         mH = h;
         mNeedsClear = true;
      }
      GLUtil::EnsureFbo(mPair[0], mW, mH, GL_RGBA16F);
      GLUtil::EnsureFbo(mPair[1], mW, mH, GL_RGBA16F);
   }

   void PixelStateBank::Reset()
   {
      mNeedsClear = true;
   }

   void PixelStateBank::Swap()
   {
      mFront = 1 - mFront;
   }

   // samplerLoc is resolved once after link, not looked up per cook (T22).
   void PixelStateBank::BindReadUnits(unsigned int program, int samplerLoc)
   {
      (void)program;
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, mPair[mFront].tex);
      if (samplerLoc >= 0)
         glUniform1i(samplerLoc, 0);
   }

   void PixelStateBank::ClearBoth(const float initRgba[4])
   {
      GLint oldFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
      GLfloat oldClear[4];
      glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear);

      float r = initRgba ? initRgba[0] : 0.0f;
      float g = initRgba ? initRgba[1] : 0.0f;
      float b = initRgba ? initRgba[2] : 0.0f;
      float a = initRgba ? initRgba[3] : 0.0f;

      glClearColor(r, g, b, a);
      for (int i = 0; i < 2; i++)
      {
         glBindFramebuffer(GL_FRAMEBUFFER, mPair[i].fbo);
         glClear(GL_COLOR_BUFFER_BIT);
      }

      glClearColor(oldClear[0], oldClear[1], oldClear[2], oldClear[3]);
      glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
      mNeedsClear = false;
   }
}

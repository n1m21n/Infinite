#include "SwitcherNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kUnitNames = { "beats", "seconds" };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uA;\n"
      "uniform sampler2D uB;\n"
      "uniform float uMix;\n"
      "void main() {\n"
      "   fragColor = mix(texture(uA, vUv), texture(uB, vUv), uMix);\n"
      "}\n";
}

const std::vector<std::string>& SwitcherNode::UnitNames()
{
   return kUnitNames;
}

SwitcherNode::~SwitcherNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool SwitcherNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void SwitcherNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   // Only connected inputs take part, so a two-input switcher alternates
   // between exactly those two rather than fading through empty slots.
   int connected[kSlots];
   int count = 0;
   for (int i = 0; i < kSlots; i++)
   {
      if (mInputs[i].IsConnected())
         connected[count++] = i;
   }
   if (count == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }

   int slotA = connected[0];
   int slotB = connected[0];
   float mix = 0.0f;

   if (manual || count == 1)
   {
      const int pick = manual ? std::max(0, std::min(manualSlot, kSlots - 1)) : connected[0];
      slotA = mInputs[pick].IsConnected() ? pick : connected[0];
      slotB = slotA;
   }
   else
   {
      const double clock = (unit == 0) ? Transport::Instance().Beats()
                                       : Transport::Instance().Seconds();
      const double step = std::max(0.01f, interval);
      const double pos = clock / step;
      const long long index = (long long)std::floor(pos);
      const float phase = (float)(pos - (double)index);

      slotA = connected[(int)(((index % count) + count) % count)];
      slotB = connected[(int)((((index + 1) % count) + count) % count)];

      const float fade = std::max(0.0f, std::min(crossfade, 0.999f));
      if (fade <= 0.0f)
         mix = 0.0f;                                   // hard cut
      else if (phase > 1.0f - fade)
         mix = (phase - (1.0f - fade)) / fade;         // ease into the next input
   }

   mActiveSlot = (mix >= 0.5f) ? slotB : slotA;

   unsigned int texA = mInputs[slotA].Pull(frameId);
   unsigned int texB = (slotB == slotA) ? texA : mInputs[slotB].Pull(frameId);
   if (texA == 0)
   {
      GLUtil::DestroyFbo(mOut);
      return;
   }
   if (texB == 0)
      texB = texA;

   const int w = mInputs[slotA].Width();
   const int h = mInputs[slotA].Height();

   if (!EnsureShader())
      return;
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this, texA, texB, mix]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texA);
      glUniform1i(glGetUniformLocation(mProgram, "uA"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texB);
      glUniform1i(glGetUniformLocation(mProgram, "uB"), 1);
      glUniform1f(glGetUniformLocation(mProgram, "uMix"), mix);
   });
}

#include "TimeMachineNode.h"

#include "platform/OpenGLHeaders.h"
#include <algorithm>
#include <cmath>

namespace
{
   const char* kWriteFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "void main() { fragColor = texture(uSrc, vUv); }\n";

   // Layer age a (0 = newest) sits at (head - 1 - a) mod length, head being the layer
   // written next. The delay is blended between the floor and ceil frame.
   const char* kReadFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2DArray uRing;\n"
      "uniform sampler2D uMap;\n"
      "uniform int uHasMap;\n"
      "uniform int uLength;\n"
      "uniform int uHead;\n"
      "uniform int uInvert;\n"
      "vec4 frame(int age) {\n"
      "   int layer = (uHead - 1 - age) % uLength;\n"
      "   if (layer < 0) layer += uLength;\n"
      "   return texelFetch(uRing, ivec3(ivec2(gl_FragCoord.xy), layer), 0);\n"
      "}\n"
      "void main() {\n"
      "   float g = uHasMap != 0 ? dot(texture(uMap, vUv).rgb, vec3(0.299, 0.587, 0.114)) : vUv.x;\n"
      "   g = clamp(uInvert != 0 ? 1.0 - g : g, 0.0, 1.0);\n"
      "   float d = g * float(uLength - 1);\n"
      "   int a = int(floor(d));\n"
      "   int b = min(a + 1, uLength - 1);\n"
      "   fragColor = mix(frame(a), frame(b), d - float(a));\n"
      "}\n";
}

TimeMachineNode::~TimeMachineNode()
{
   DestroyRing();
   GLUtil::DestroyFbo(mOut);
   for (unsigned int p : { mProgWrite, mProgRead })
      if (p != 0)
         glDeleteProgram(p);
}

void TimeMachineNode::DestroyRing()
{
   for (unsigned int& f : mLayerFbo)
      if (f != 0)
      {
         glDeleteFramebuffers(1, &f);
         f = 0;
      }
   if (mRingTex != 0)
   {
      glDeleteTextures(1, &mRingTex);
      mRingTex = 0;
   }
   mRingW = mRingH = mRingLayers = 0;
}

bool TimeMachineNode::EnsureShaders()
{
   if (mShadersTried)
      return mProgRead != 0;
   mShadersTried = true;
   mProgWrite = GLUtil::CompileProgram(kWriteFrag);
   mProgRead = GLUtil::CompileProgram(kReadFrag);
   if (mProgWrite == 0 || mProgRead == 0)
   {
      mProgRead = 0;
      return false;
   }
   return true;
}

bool TimeMachineNode::EnsureRing(int w, int h, int layers)
{
   if (mRingTex != 0 && w == mRingW && h == mRingH && layers == mRingLayers)
      return true;
   DestroyRing();
   glGenTextures(1, &mRingTex);
   glBindTexture(GL_TEXTURE_2D_ARRAY, mRingTex);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
   GLint prevFbo = 0;
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   bool ok = glGetError() == GL_NO_ERROR;
   for (int l = 0; ok && l < layers; l++)
   {
      glGenFramebuffers(1, &mLayerFbo[l]);
      glBindFramebuffer(GL_FRAMEBUFFER, mLayerFbo[l]);
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mRingTex, 0, l);
      ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
   }
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   if (!ok)
   {
      DestroyRing();
      return false;
   }
   mRingW = w;
   mRingH = h;
   mRingLayers = layers;
   mHead = 0;
   mFilled = false;
   return true;
}

void TimeMachineNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const unsigned int srcTex = mInput.Pull(frameId);
   if (srcTex == 0 || !EnsureShaders())
      return;
   const unsigned int mapTex = mMap.IsConnected() ? mMap.Pull(frameId) : 0;

   const int div = resolution <= 0 ? 1 : (resolution == 1 ? 2 : 4);
   const int w = std::max(16, mInput.Width() / div);
   const int h = std::max(16, mInput.Height() / div);
   const int len = std::clamp(length, 2, kMaxLength);
   if (!EnsureRing(w, h, len) || !GLUtil::EnsureFbo(mOut, w, h, GL_RGBA8, "TimeMachine"))
      return;

   // The write goes straight into a layer's framebuffer; a stand-in Fbo carries it to
   // RunShaderPass, which only needs the framebuffer name and the size.
   auto writeLayer = [&](int layer)
   {
      GLUtil::Fbo target;
      target.fbo = mLayerFbo[layer];
      target.w = w;
      target.h = h;
      GLUtil::RunShaderPass(target, mProgWrite, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, srcTex);
         glUniform1i(glGetUniformLocation(mProgWrite, "uSrc"), 0);
      });
   };

   if (!mFilled)
   {
      for (int l = 0; l < len; l++)
         writeLayer(l);
      mHead = 0;
      mFilled = true;
   }
   else
   {
      writeLayer(mHead);
      mHead = (mHead + 1) % len;
   }

   GLUtil::RunShaderPass(mOut, mProgRead, [&]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D_ARRAY, mRingTex);
      glUniform1i(glGetUniformLocation(mProgRead, "uRing"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mapTex);
      glUniform1i(glGetUniformLocation(mProgRead, "uMap"), 1);
      glUniform1i(glGetUniformLocation(mProgRead, "uHasMap"), mapTex != 0 ? 1 : 0);
      glUniform1i(glGetUniformLocation(mProgRead, "uLength"), len);
      glUniform1i(glGetUniformLocation(mProgRead, "uHead"), mHead);
      glUniform1i(glGetUniformLocation(mProgRead, "uInvert"), invert ? 1 : 0);
   });
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

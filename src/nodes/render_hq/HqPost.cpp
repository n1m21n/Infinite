// Turbo 0.71 (S8): Render 3D HQ's finish - Filament's bloom chain and the
// tonemap pass. See HqPost.h; the passes follow PostProcessManager::bloom()
// (docs/filament-notes.md section 6) and colorGrading.mat's order.

#include "HqPost.h"

#include <algorithm>
#include <initializer_list>

#include "core/GLUtil.h"
#include "platform/OpenGLHeaders.h"

namespace
{
   // Filament's BloomOptions: resolution (rows of the bloom buffer) and
   // highlight (the clamp before the bloom).
   const int kBloomResolution = 384;
   const float kBloomHighlight = 1000.0f;

   unsigned int MakeTarget(int w, int h, GLenum internal, unsigned int& fbo, const char* label)
   {
      unsigned int tex = 0;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexStorage2D(GL_TEXTURE_2D, 1, internal, w, h);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      GLUtil::LabelObject(GL_TEXTURE, tex, label);
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      return tex;
   }

   void FreeTarget(unsigned int& tex, unsigned int& fbo)
   {
      if (tex != 0) glDeleteTextures(1, &tex);
      if (fbo != 0) glDeleteFramebuffers(1, &fbo);
      tex = fbo = 0;
   }
}

bool HqPost::EnsurePrograms()
{
   if (mTried)
      return mDown.id != 0 && mUp.id != 0 && mFin.id != 0;
   mTried = true;
   mDown.id = HqBuildProgram("hq_background.vert.glsl", "hq_bloom_down.frag.glsl");
   mUp.id = HqBuildProgram("hq_background.vert.glsl", "hq_bloom_up.frag.glsl");
   mFin.id = HqBuildProgram("hq_background.vert.glsl", "hq_finish.frag.glsl");
   if (mDown.id == 0 || mUp.id == 0 || mFin.id == 0)
      return false;
   auto loc = [](unsigned int p, const char* n) { return glGetUniformLocation(p, n); };
   glUseProgram(mDown.id);
   mDown.mode = loc(mDown.id, "uMode");
   mDown.threshold = loc(mDown.id, "uThreshold");
   mDown.invHighlight = loc(mDown.id, "uInvHighlight");
   mDown.fireflies = loc(mDown.id, "uFireflies");
   glUniform1i(loc(mDown.id, "uSource"), 0);
   glUseProgram(mUp.id);
   mUp.dstTexel = loc(mUp.id, "uDstTexel");
   glUniform1i(loc(mUp.id, "uSource"), 0);
   glUniform1i(loc(mUp.id, "uBase"), 1);
   glUseProgram(mFin.id);
   mFin.bloomStrength = loc(mFin.id, "uBloomStrength");
   mFin.keyMode = loc(mFin.id, "uKeyMode");
   mFin.bg = loc(mFin.id, "uBg");
   mFin.bgOpacity = loc(mFin.id, "uBgOpacity");
   mFin.tonemap = loc(mFin.id, "uTonemap");
   glUniform1i(loc(mFin.id, "uSrc"), 0);
   glUniform1i(loc(mFin.id, "uBloom"), 1);
   glUniform1i(loc(mFin.id, "uKey"), 2);
   glUseProgram(0);
   glGenVertexArrays(1, &mVao);
   return true;
}

void HqPost::EnsureTargets(int w, int h)
{
   if (mOut != 0 && w == mW && h == mH)
      return;
   FreeTarget(mOut, mOutFbo);
   for (int i = 0; i < kMaxPre; i++)
      FreeTarget(mPre[i], mPreFbo[i]);
   for (int i = 0; i < kMaxLevels; i++)
   {
      FreeTarget(mChain[i], mChainFbo[i]);
      FreeTarget(mSum[i], mSumFbo[i]);
   }
   mOut = MakeTarget(w, h, GL_RGBA8, mOutFbo, "Render 3D HQ: output");
   // Halve until within 2x of the bloom resolution; the last step is level 0
   // of the chain, the ones before it are the pre-downscale targets.
   int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
   mPreCount = 0;
   while (bh > 2 * kBloomResolution && mPreCount < kMaxPre)
   {
      mPreW[mPreCount] = bw;
      mPreH[mPreCount] = bh;
      mPre[mPreCount] = MakeTarget(bw, bh, GL_R11F_G11F_B10F, mPreFbo[mPreCount], "Render 3D HQ: bloom downscale");
      mPreCount++;
      bw = std::max(1, bw / 2);
      bh = std::max(1, bh / 2);
   }
   mChainCount = 0;
   while (mChainCount < kMaxLevels)
   {
      const int i = mChainCount++;
      mChainW[i] = bw;
      mChainH[i] = bh;
      mChain[i] = MakeTarget(bw, bh, GL_R11F_G11F_B10F, mChainFbo[i], "Render 3D HQ: bloom level");
      mSum[i] = MakeTarget(bw, bh, GL_R11F_G11F_B10F, mSumFbo[i], "Render 3D HQ: bloom sum");
      if (bw == 1 || bh == 1)
         break;
      bw = std::max(1, bw / 2);
      bh = std::max(1, bh / 2);
   }
   glBindTexture(GL_TEXTURE_2D, 0);
   mW = w;
   mH = h;
}

void HqPost::Draw(unsigned int fbo, int w, int h)
{
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glViewport(0, 0, w, h);
   glDrawArrays(GL_TRIANGLES, 0, 3);
}

unsigned int HqPost::Finish(unsigned int src, int w, int h, const HqPostSettings& s, int keyMode, unsigned int key,
                            const float bg[3], float bgOpacity)
{
   mLastLevels = 0;
   if (!EnsurePrograms())
      return 0;
   GLUtil::DebugScope group("Render 3D HQ: post");
   EnsureTargets(w, h);
   glBindVertexArray(mVao);
   glActiveTexture(GL_TEXTURE0);

   const int levels = s.bloom > 0.0f ? std::clamp(s.levels, 1, mChainCount) : 0;
   if (levels > 0)
   {
      GLUtil::DebugScope bloomGroup("Render 3D HQ: bloom");
      // (a) Threshold + 2x steps down to the bloom buffer (level 0).
      glUseProgram(mDown.id);
      glUniform1i(mDown.mode, 0);
      glUniform1f(mDown.invHighlight, 1.0f / kBloomHighlight);
      unsigned int from = src;
      for (int step = 0; step <= mPreCount; step++)
      {
         const bool first = step == 0;
         glUniform1f(mDown.threshold, first ? std::max(0.0f, s.threshold) : 0.0f);
         glUniform1i(mDown.fireflies, first && s.fireflies ? 1 : 0);
         glBindTexture(GL_TEXTURE_2D, from);
         if (step < mPreCount)
         {
            Draw(mPreFbo[step], mPreW[step], mPreH[step]);
            from = mPre[step];
         }
         else
            Draw(mChainFbo[0], mChainW[0], mChainH[0]);
      }
      // (b) The chain: 9 taps from an even-sized level, else 13.
      for (int i = 1; i < levels; i++)
      {
         const bool even = (mChainW[i - 1] % 2) == 0 && (mChainH[i - 1] % 2) == 0;
         glUniform1i(mDown.mode, even ? 1 : 2);
         glBindTexture(GL_TEXTURE_2D, mChain[i - 1]);
         Draw(mChainFbo[i], mChainW[i], mChainH[i]);
      }
      // (d) Smallest level up: each level plus the one below upsampled.
      glUseProgram(mUp.id);
      for (int i = levels - 1; i >= 1; i--)
      {
         glUniform2f(mUp.dstTexel, 1.0f / (float)mChainW[i - 1], 1.0f / (float)mChainH[i - 1]);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, mChain[i - 1]);
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, i == levels - 1 ? mChain[i] : mSum[i]);
         Draw(mSumFbo[i - 1], mChainW[i - 1], mChainH[i - 1]);
      }
      mLastLevels = levels;
   }

   glUseProgram(mFin.id);
   glUniform1f(mFin.bloomStrength, levels > 0 ? s.bloom / (float)levels : 0.0f);
   glUniform1i(mFin.keyMode, keyMode);
   glUniform3f(mFin.bg, bg[0], bg[1], bg[2]);
   glUniform1f(mFin.bgOpacity, bgOpacity);
   glUniform1i(mFin.tonemap, s.tonemap);
   glActiveTexture(GL_TEXTURE2);
   glBindTexture(GL_TEXTURE_2D, key);
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, levels > 1 ? mSum[0] : mChain[0]);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, src);
   Draw(mOutFbo, w, h);

   for (int unit = 2; unit >= 0; unit--)
   {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
   glBindVertexArray(0);
   glUseProgram(0);
   return mOut;
}

void HqPost::Release()
{
   FreeTarget(mOut, mOutFbo);
   for (int i = 0; i < kMaxPre; i++)
      FreeTarget(mPre[i], mPreFbo[i]);
   for (int i = 0; i < kMaxLevels; i++)
   {
      FreeTarget(mChain[i], mChainFbo[i]);
      FreeTarget(mSum[i], mSumFbo[i]);
   }
   for (unsigned int* p : { &mDown.id, &mUp.id, &mFin.id })
   {
      if (*p != 0)
         glDeleteProgram(*p);
      *p = 0;
   }
   if (mVao != 0)
      glDeleteVertexArrays(1, &mVao);
   mVao = 0;
   mW = mH = 0;
   mTried = false;
}

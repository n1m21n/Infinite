#include "GpuPoints.h"

#include "gl3.h"
#include "BenchReport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace GpuPointsGL
{
   static bool& EnabledFlag()
   {
      static bool enabled = []()
      {
         const char* v = getenv("INFINITE_GPU_POINTS");
         return v == nullptr || strcmp(v, "0") != 0;
      }();
      return enabled;
   }

   bool Enabled() { return EnabledFlag(); }
   void SetEnabledForTest(bool enabled) { EnabledFlag() = enabled; }

   static unsigned int MakeTexture(int w, int h, unsigned int internalFormat, unsigned int type, const char* label)
   {
      unsigned int tex = 0;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, GL_RGBA, type, nullptr);
      // Read with texelFetch only; NEAREST keeps the texture complete without
      // mipmaps and stops anything from ever blending two points together.
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      Bench::GpuMem::RecordTexture(tex, Bench::GpuMemCategory::RenderTargets, w, h, internalFormat, false, label);
      return tex;
   }

   bool EnsureTarget(Target& t, int w, int h, const char* label)
   {
      if (w <= 0 || h <= 0)
         return false;
      if (t.fbo != 0 && t.w == w && t.h == h)
         return true;
      DestroyTarget(t);

      t.posTex = MakeTexture(w, h, GL_RGBA32F, GL_FLOAT, label);
      t.colorTex = MakeTexture(w, h, GL_RGBA32F, GL_FLOAT, label);

      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glGenFramebuffers(1, &t.fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.posTex, 0);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, t.colorTex, 0);
      const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
      glDrawBuffers(2, bufs);
      const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glBindTexture(GL_TEXTURE_2D, 0);

      if (status != GL_FRAMEBUFFER_COMPLETE)
      {
         fprintf(stderr, "GpuPoints: target %dx%d incomplete, status=0x%x\n", w, h, status);
         DestroyTarget(t);
         return false;
      }
      t.w = w;
      t.h = h;
      return true;
   }

   void DestroyTarget(Target& t)
   {
      if (t.fbo != 0)
         glDeleteFramebuffers(1, &t.fbo);
      if (t.posTex != 0)
      {
         Bench::GpuMem::ReleaseTexture(t.posTex);
         glDeleteTextures(1, &t.posTex);
      }
      if (t.colorTex != 0)
      {
         Bench::GpuMem::ReleaseTexture(t.colorTex);
         glDeleteTextures(1, &t.colorTex);
      }
      t = Target();
   }

   void BindOutputs(const Target& t, unsigned int program, const char* posOut, const char* colorOut)
   {
      // glDrawBuffers maps fragment output *location* i to bufs[i]; the linker
      // picked the locations, so place each attachment at its output's slot.
      const GLint posLoc = glGetFragDataLocation(program, posOut);
      const GLint colorLoc = glGetFragDataLocation(program, colorOut);
      if (posLoc < 0 || colorLoc < 0 || posLoc > 1 || colorLoc > 1 || posLoc == colorLoc)
         return;
      GLenum bufs[2];
      bufs[posLoc] = GL_COLOR_ATTACHMENT0;
      bufs[colorLoc] = GL_COLOR_ATTACHMENT1;
      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
      glDrawBuffers(2, bufs);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   }
}

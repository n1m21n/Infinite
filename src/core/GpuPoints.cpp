#include "GpuPoints.h"

#include "gl3.h"
#include "GLUtil.h"
#include "BenchReport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

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

   bool Upload(Target& t, GpuPoints& out, const std::vector<Particle>& points, float baseSize, const char* label)
   {
      const int n = (int)points.size();
      if (n <= 0)
         return false;
      const int w = std::min(n, 1024);
      const int h = (n + w - 1) / w;
      if (!EnsureTarget(t, w, h, label))
         return false;

      std::vector<float> pos((size_t)w * (size_t)h * 4, 0.0f);
      std::vector<float> color((size_t)w * (size_t)h * 4, 0.0f);
      float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
      float maxHalf = 0.0f;
      int live = 0;
      for (int i = 0; i < n; i++)
      {
         const Particle& p = points[(size_t)i];
         float* q = &pos[(size_t)i * 4];
         float* c = &color[(size_t)i * 4];
         q[0] = p.px; q[1] = p.py; q[2] = p.pz;
         c[0] = p.r; c[1] = p.g; c[2] = p.b; c[3] = PointLifeFraction(p);
         if (!p.alive)
            continue; // w stays 0: hidden, like the CPU draw's skip
         q[3] = p.scale * baseSize;
         live++;
         maxHalf = std::max(maxHalf, std::fabs(q[3]));
         lo[0] = std::min(lo[0], p.px); hi[0] = std::max(hi[0], p.px);
         lo[1] = std::min(lo[1], p.py); hi[1] = std::max(hi[1], p.py);
         lo[2] = std::min(lo[2], p.pz); hi[2] = std::max(hi[2], p.pz);
      }

      glBindTexture(GL_TEXTURE_2D, t.posTex);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, pos.data());
      glBindTexture(GL_TEXTURE_2D, t.colorTex);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, color.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      Bench::GpuMem::NoteUpload((pos.size() + color.size()) * sizeof(float));

      out.posTex = t.posTex;
      out.colorTex = t.colorTex;
      out.texW = w;
      out.texH = h;
      out.count = n;
      out.liveCount = live;
      out.pointMatrix = Mat4::Identity();
      out.hasBounds = live > 0;
      for (int k = 0; k < 3; k++)
      {
         out.lo[k] = live > 0 ? lo[k] : 0.0f;
         out.hi[k] = live > 0 ? hi[k] : 0.0f;
      }
      out.maxHalfSize = maxHalf;
      return true;
   }

   const char* const kPointPassHeader =
      "#version 150\n"
      "uniform sampler2D uPointPos;\n"
      "uniform sampler2D uPointColor;\n"
      "uniform mat4 uPointMatrix;\n"
      "uniform int uPointTexW;\n"
      "out vec4 outPos;\n"
      "out vec4 outColor;\n"
      "vec3 P;\n"
      "float W;\n"
      "vec3 Cd;\n"
      "float Life;\n"
      "int index;\n"
      "void LoadPoint() {\n"
      "   ivec2 cell = ivec2(gl_FragCoord.xy);\n"
      "   vec4 p = texelFetch(uPointPos, cell, 0);\n"
      "   P = (uPointMatrix * vec4(p.xyz, 1.0)).xyz;\n"
      "   W = p.w;\n"
      "   vec4 c = texelFetch(uPointColor, cell, 0);\n"
      "   Cd = c.rgb;\n"
      "   Life = c.a;\n"
      "   index = cell.y * uPointTexW + cell.x;\n"
      "}\n";

   bool RunPointPass(Target& t, GpuPoints& out, const GpuPoints& src, unsigned int program,
                     const std::function<void()>& setup, const char* label)
   {
      if (program == 0 || src.posTex == 0 || src.texW <= 0 || src.texH <= 0)
         return false;
      if (!EnsureTarget(t, src.texW, src.texH, label))
         return false;
      BindOutputs(t, program, "outPos", "outColor");

      // RunShaderPass wants a GLUtil::Fbo; this one carries the MRT
      // framebuffer, whose draw buffers BindOutputs just set.
      GLUtil::Fbo target;
      target.fbo = t.fbo;
      target.tex = t.posTex;
      target.w = t.w;
      target.h = t.h;
      target.internalFormat = GL_RGBA32F;

      const GLboolean prevBlend = glIsEnabled(GL_BLEND);
      glDisable(GL_BLEND); // positions are data
      GLUtil::RunShaderPass(target, program, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, src.posTex);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, src.colorTex);
         glUniform1i(glGetUniformLocation(program, "uPointPos"), 0);
         glUniform1i(glGetUniformLocation(program, "uPointColor"), 1);
         glUniformMatrix4fv(glGetUniformLocation(program, "uPointMatrix"), 1, GL_FALSE, src.pointMatrix.m);
         glUniform1i(glGetUniformLocation(program, "uPointTexW"), src.texW);
         setup();
      });
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, 0);
      if (prevBlend)
         glEnable(GL_BLEND);

      out.posTex = t.posTex;
      out.colorTex = t.colorTex;
      out.texW = t.w;
      out.texH = t.h;
      out.count = src.count;
      out.pointMatrix = Mat4::Identity();
      return true;
   }
}

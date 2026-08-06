#include "GLUtil.h"

#include <OpenGL/gl3.h>
#include <cstdio>
#include <cstdlib>

namespace GLUtil
{
   static const char* kVertSrc =
      "#version 150\n"
      "in vec2 aPos;\n"
      "in vec2 aUv;\n"
      "out vec2 vUv;\n"
      "void main() {\n"
      "   vUv = aUv;\n"
      "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
      "}\n";

   static unsigned int sQuadVao = 0;
   static unsigned int sQuadVbo = 0;

   static void EnsureQuad()
   {
      if (sQuadVao != 0)
         return;

      // clip-space pos.xy, uv.xy - a single GL_TRIANGLE_STRIP covering the viewport
      float verts[] = {
         -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         -1.0f, 1.0f, 0.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f
      };

      glGenVertexArrays(1, &sQuadVao);
      glGenBuffers(1, &sQuadVbo);
      glBindVertexArray(sQuadVao);
      glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
      glBindVertexArray(0);
   }

   static void DrawQuad()
   {
      EnsureQuad();
      glBindVertexArray(sQuadVao);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glBindVertexArray(0);
   }

   bool EnsureFbo(Fbo& fbo, int w, int h)
   {
      if (w <= 0 || h <= 0)
         return false;

      if (fbo.fbo != 0 && fbo.w == w && fbo.h == h)
         return true;

      DestroyFbo(fbo);

      glGenFramebuffers(1, &fbo.fbo);
      glGenTextures(1, &fbo.tex);

      glBindTexture(GL_TEXTURE_2D, fbo.tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.tex, 0);

      GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0);

      if (status != GL_FRAMEBUFFER_COMPLETE)
      {
         fprintf(stderr, "GLUtil::EnsureFbo failed, status=0x%x\n", status);
         DestroyFbo(fbo);
         return false;
      }

      fbo.w = w;
      fbo.h = h;
      return true;
   }

   void DestroyFbo(Fbo& fbo)
   {
      if (fbo.tex != 0)
         glDeleteTextures(1, &fbo.tex);
      if (fbo.fbo != 0)
         glDeleteFramebuffers(1, &fbo.fbo);
      fbo = Fbo();
   }

   unsigned int CompileProgram(const char* fragSrc, std::string* outError)
   {
      auto report = [outError](const char* prefix, const char* log)
      {
         if (outError != nullptr)
            *outError = std::string(prefix) + log;
         else
            fprintf(stderr, "GLUtil::CompileProgram %s%s\n", prefix, log);
      };

      auto compile = [&report](GLenum type, const char* src) -> unsigned int
      {
         unsigned int shader = glCreateShader(type);
         glShaderSource(shader, 1, &src, nullptr);
         glCompileShader(shader);
         GLint ok = 0;
         glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
         if (!ok)
         {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            report("shader error: ", log);
            glDeleteShader(shader);
            return 0u;
         }
         return shader;
      };

      unsigned int vert = compile(GL_VERTEX_SHADER, kVertSrc);
      unsigned int frag = compile(GL_FRAGMENT_SHADER, fragSrc);
      if (vert == 0 || frag == 0)
      {
         if (vert)
            glDeleteShader(vert);
         if (frag)
            glDeleteShader(frag);
         return 0;
      }

      unsigned int program = glCreateProgram();
      glBindAttribLocation(program, 0, "aPos");
      glBindAttribLocation(program, 1, "aUv");
      glAttachShader(program, vert);
      glAttachShader(program, frag);
      glLinkProgram(program);

      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);

      glDeleteShader(vert);
      glDeleteShader(frag);

      if (!linked)
      {
         char log[1024];
         glGetProgramInfoLog(program, sizeof(log), nullptr, log);
         report("link error: ", log);
         glDeleteProgram(program);
         return 0;
      }

      return program;
   }

   void RunShaderPass(const Fbo& out, unsigned int program, const std::function<void()>& setup)
   {
      if (out.fbo == 0 || program == 0)
         return;

      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      GLint prevViewport[4];
      glGetIntegerv(GL_VIEWPORT, prevViewport);

      glBindFramebuffer(GL_FRAMEBUFFER, out.fbo);
      glViewport(0, 0, out.w, out.h);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(program);
      if (setup)
         setup();

      DrawQuad();

      glUseProgram(0);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
   }

   void DrawTextureToScreen(unsigned int tex, int windowW, int windowH)
   {
      static const char* kBlitFragSrc =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "void main() { fragColor = texture(uTex, vUv); }\n";

      static unsigned int sBlitProgram = 0;
      static int sLocTex = -1;
      if (sBlitProgram == 0)
      {
         sBlitProgram = CompileProgram(kBlitFragSrc);
         sLocTex = glGetUniformLocation(sBlitProgram, "uTex");
      }
      if (sBlitProgram == 0)
         return;

      glViewport(0, 0, windowW, windowH);
      glClearColor(0.1f, 0.1f, 0.1f, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(sBlitProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      if (sLocTex >= 0)
         glUniform1i(sLocTex, 0);

      DrawQuad();

      glUseProgram(0);
   }
}

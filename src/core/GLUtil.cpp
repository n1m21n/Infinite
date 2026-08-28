#include "GLUtil.h"

#include "gl3.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

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

   static std::unordered_map<GLFWwindow*, unsigned int> sQuadVaos;
   static unsigned int sQuadVbo = 0;

   static unsigned int EnsureQuad()
   {
      GLFWwindow* context = glfwGetCurrentContext();
      auto found = sQuadVaos.find(context);
      if (found != sQuadVaos.end())
         return found->second;

      // clip-space pos.xy, uv.xy - a single GL_TRIANGLE_STRIP covering the viewport
      float verts[] = {
         -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         -1.0f, 1.0f, 0.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f
      };

      unsigned int vao = 0;
      glGenVertexArrays(1, &vao);
      if (sQuadVbo == 0)
      {
         glGenBuffers(1, &sQuadVbo);
         glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
         glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
      }
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
      glBindVertexArray(0);
      sQuadVaos[context] = vao;
      return vao;
   }

   static void DrawQuad()
   {
      const unsigned int vao = EnsureQuad();
      glBindVertexArray(vao);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glBindVertexArray(0);
   }

   void ForgetCurrentContextObjects()
   {
      GLFWwindow* context = glfwGetCurrentContext();
      auto found = sQuadVaos.find(context);
      if (found == sQuadVaos.end())
         return;
      glDeleteVertexArrays(1, &found->second);
      sQuadVaos.erase(found);
   }

   bool EnsureFbo(Fbo& fbo, int w, int h, unsigned int internalFormat)
   {
      if (w <= 0 || h <= 0)
         return false;

      if (fbo.fbo != 0 && fbo.w == w && fbo.h == h && fbo.internalFormat == internalFormat)
         return true;

      DestroyFbo(fbo);

      GLenum format = GL_RGBA;
      GLenum type = (internalFormat == GL_RGBA16F) ? GL_FLOAT : GL_UNSIGNED_BYTE;

      glGenFramebuffers(1, &fbo.fbo);
      glGenTextures(1, &fbo.tex);

      glBindTexture(GL_TEXTURE_2D, fbo.tex);
      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
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
      fbo.internalFormat = internalFormat;
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

   void DrawTextureToScreen(unsigned int tex, int windowW, int windowH, int texW, int texH,
                             bool checkerBg)
   {
      static const char* kBlitFragSrc =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "void main() { fragColor = texture(uTex, vUv); }\n";

      // Composites the texture's own alpha over the same dark checkerboard
      // pattern the node editor draws behind a transparent preview (see
      // DrawCheckerboardBackdrop in main.cpp), so a node with a transparent
      // background reads the same way here as it does inline/in the viewport
      // panel instead of just showing solid black where alpha is 0.
      static const char* kBlitCheckerFragSrc =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "void main()\n"
         "{\n"
         "   vec4 c = texture(uTex, vUv);\n"
         "   vec2 cell = floor(gl_FragCoord.xy / 12.0);\n"
         "   float parity = mod(cell.x + cell.y, 2.0);\n"
         "   vec3 bg = mix(vec3(30.0, 30.0, 38.0) / 255.0, vec3(18.0, 18.0, 24.0) / 255.0, parity);\n"
         "   fragColor = vec4(mix(bg, c.rgb, c.a), 1.0);\n"
         "}\n";

      static unsigned int sBlitProgram = 0;
      static int sLocTex = -1;
      static unsigned int sCheckerProgram = 0;
      static int sLocTexChecker = -1;
      if (sBlitProgram == 0)
      {
         sBlitProgram = CompileProgram(kBlitFragSrc);
         sLocTex = glGetUniformLocation(sBlitProgram, "uTex");
      }
      if (sCheckerProgram == 0)
      {
         sCheckerProgram = CompileProgram(kBlitCheckerFragSrc);
         sLocTexChecker = glGetUniformLocation(sCheckerProgram, "uTex");
      }
      const unsigned int program = checkerBg ? sCheckerProgram : sBlitProgram;
      const int locTex = checkerBg ? sLocTexChecker : sLocTex;
      if (program == 0)
         return;

      // Clear the full window first (letterbox bars, if any, show this).
      glViewport(0, 0, windowW, windowH);
      glClearColor(0.1f, 0.1f, 0.1f, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      int vpX = 0, vpY = 0, vpW = windowW, vpH = windowH;
      if (texW > 0 && texH > 0 && windowW > 0 && windowH > 0)
      {
         const float srcAspect = (float)texW / (float)texH;
         const float dstAspect = (float)windowW / (float)windowH;
         if (srcAspect > dstAspect)
         {
            // Source is relatively wider than the window - full width, bars top/bottom.
            vpW = windowW;
            vpH = (int)(windowW / srcAspect + 0.5f);
            vpX = 0;
            vpY = (windowH - vpH) / 2;
         }
         else
         {
            // Source is relatively taller than the window - full height, bars left/right.
            vpH = windowH;
            vpW = (int)(windowH * srcAspect + 0.5f);
            vpY = 0;
            vpX = (windowW - vpW) / 2;
         }
      }
      glViewport(vpX, vpY, vpW, vpH);

      glUseProgram(program);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex);
      if (locTex >= 0)
         glUniform1i(locTex, 0);

      DrawQuad();

      glUseProgram(0);
   }

   bool ReadTexturePixels(unsigned int& scratchFbo, unsigned int srcTex, int w, int h,
                          std::vector<float>& outRGBA)
   {
      if (srcTex == 0 || w <= 0 || h <= 0)
         return false;

      if (scratchFbo == 0)
         glGenFramebuffers(1, &scratchFbo);

      GLint prevFbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, scratchFbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);

      const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
      if (ok)
      {
         outRGBA.assign((size_t)w * h * 4, 0.0f);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, outRGBA.data());
      }

      // Detach rather than leave srcTex bound to a framebuffer this function
      // does not own - the caller may delete or rebind it next frame.
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
      return ok;
   }
}

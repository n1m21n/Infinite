#include "GLUtil.h"

#include "gl3.h"
#include "BenchReport.h"

// GLFW_INCLUDE_NONE: glfw3.h pulls in the legacy <GL/gl.h> unless told not to,
// and gl3.h above has already provided the 3.x core header (glad on non-Apple).
// Only used here for glfwGetCurrentContext() - see sQuadVaos below.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <map>

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

   // The fullscreen quad's GPU state, split by what an OpenGL share group
   // actually shares.
   //
   // A projector/output window (main.cpp's OpenProjectorWindow) creates its
   // context with the editor window as the share context, and then blits
   // through DrawTextureToScreen with ITS context current. Share groups share
   // *object* state - textures, buffers, shaders, programs - but explicitly
   // NOT container objects: VAOs, FBOs, program pipelines and transform
   // feedback objects are per-context (GL 3.3 core, Appendix D "Shared
   // Objects and Multiple Contexts"). Apple's implementation shares VAOs
   // anyway, as a documented non-conformance, which is why a single global
   // VAO worked on macOS and drew nothing on Windows: glBindVertexArray with
   // a name from another context raises GL_INVALID_OPERATION, leaves no
   // vertex array bound, and in a core profile glDrawArrays with no bound
   // VAO draws nothing at all - a uniformly dark-grey projector window.
   //
   // So: one VAO per context, keyed on the GLFW context that created it, and
   // one shared VBO holding the actual vertices. The attribute pointers live
   // in the VAO, not the buffer, so every per-context VAO has to redo the
   // bind/glVertexAttribPointer/glEnableVertexAttribArray setup against that
   // same shared buffer.
   //
   // Main-thread-only, like every other GL call site in this codebase.
   static unsigned int sQuadVbo = 0;
   static std::map<GLFWwindow*, unsigned int> sQuadVaos;

   static unsigned int EnsureQuad()
   {
      GLFWwindow* context = glfwGetCurrentContext();
      auto it = sQuadVaos.find(context);
      if (it != sQuadVaos.end() && it->second != 0)
         return it->second;

      if (sQuadVbo == 0)
      {
         // clip-space pos.xy, uv.xy - a single GL_TRIANGLE_STRIP covering the viewport
         float verts[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, 0.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f
         };
         glGenBuffers(1, &sQuadVbo);
         glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
         glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
         Bench::GpuMem::RecordBuffer(sQuadVbo, Bench::GpuMemCategory::MeshBuffers, sizeof(verts), "QuadVbo");
      }

      unsigned int vao = 0;
      glGenVertexArrays(1, &vao);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);

      sQuadVaos[context] = vao;
      return vao;
   }

   static void DrawQuad()
   {
      const unsigned int vao = EnsureQuad();
      if (vao == 0)
         return;
      glBindVertexArray(vao);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glBindVertexArray(0);
   }

   void ReleaseCurrentContextResources()
   {
      GLFWwindow* context = glfwGetCurrentContext();
      auto it = sQuadVaos.find(context);
      if (it == sQuadVaos.end())
         return;
      if (it->second != 0)
         glDeleteVertexArrays(1, &it->second);
      sQuadVaos.erase(it);
   }

   bool EnsureFbo(Fbo& fbo, int w, int h, unsigned int internalFormat, const char* label)
   {
      if (w <= 0 || h <= 0)
         return false;

      if (fbo.fbo != 0 && fbo.w == w && fbo.h == h && fbo.internalFormat == internalFormat)
         return true;

      DestroyFbo(fbo);
      NoteFboAllocation();

      GLenum format = GL_RGBA;
      GLenum type = (internalFormat == GL_RGBA16F || internalFormat == GL_RGBA32F) ? GL_FLOAT : GL_UNSIGNED_BYTE;

      glGenFramebuffers(1, &fbo.fbo);
      glGenTextures(1, &fbo.tex);

      glBindTexture(GL_TEXTURE_2D, fbo.tex);
      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      Bench::GpuMem::RecordTexture(fbo.tex, Bench::GpuMemCategory::RenderTargets, w, h, internalFormat, false, label);

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

   static unsigned long long sFboAllocationCount = 0;
   void NoteFboAllocation() { sFboAllocationCount++; }
   unsigned long long FboAllocationCount() { return sFboAllocationCount; }

   void DestroyFbo(Fbo& fbo)
   {
      if (fbo.tex != 0)
      {
         Bench::GpuMem::ReleaseTexture(fbo.tex);
         glDeleteTextures(1, &fbo.tex);
      }
      if (fbo.fbo != 0)
         glDeleteFramebuffers(1, &fbo.fbo);
      fbo = Fbo();
   }

   namespace
   {
      struct ScratchEntry
      {
         Fbo fbo;
         unsigned long long lastUsedFrame = 0;
      };
      constexpr unsigned long long kScratchIdleFrames = 300;
      std::vector<ScratchEntry> sScratch; // a handful of entries at most: linear scan
      unsigned long long sScratchFrame = 0;
   }

   Fbo* AcquireScratchFbo(int w, int h, unsigned int internalFormat)
   {
      for (ScratchEntry& e : sScratch)
      {
         if (e.fbo.w == w && e.fbo.h == h && e.fbo.internalFormat == internalFormat)
         {
            e.lastUsedFrame = sScratchFrame;
            return &e.fbo;
         }
      }
      ScratchEntry e;
      if (!EnsureFbo(e.fbo, w, h, internalFormat, "ScratchFbo"))
         return nullptr;
      e.lastUsedFrame = sScratchFrame;
      sScratch.push_back(e);
      return &sScratch.back().fbo;
   }

   void EndFrameScratchFbos()
   {
      ++sScratchFrame;
      for (size_t i = 0; i < sScratch.size();)
      {
         if (sScratchFrame - sScratch[i].lastUsedFrame > kScratchIdleFrames)
         {
            DestroyFbo(sScratch[i].fbo);
            sScratch.erase(sScratch.begin() + (long)i);
         }
         else
            i++;
      }
   }


   // The driver's info log for a shader or program, at its full length. A
   // fixed-size buffer would cut a long GLSL error list off mid-line, and
   // FormulaNode shows this text to the user verbatim.
   static std::string InfoLog(unsigned int object, bool isProgram)
   {
      GLint length = 0;
      if (isProgram)
         glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
      else
         glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
      if (length <= 1)
         return std::string();

      std::string log((size_t)length, '\0');
      if (isProgram)
         glGetProgramInfoLog(object, length, nullptr, &log[0]);
      else
         glGetShaderInfoLog(object, length, nullptr, &log[0]);
      const size_t terminator = log.find('\0');
      if (terminator != std::string::npos)
         log.resize(terminator);
      return log;
   }

   // One compiled stage, or 0 with `log` holding the driver's reason.
   static unsigned int CompileStage(GLenum stage, const char* src, std::string& log)
   {
      const unsigned int shader = glCreateShader(stage);
      glShaderSource(shader, 1, &src, nullptr);
      glCompileShader(shader);

      GLint compiled = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
      if (compiled)
         return shader;

      log = InfoLog(shader, false);
      glDeleteShader(shader);
      return 0;
   }

   unsigned int CompileProgram(const char* fragSrc, std::string* outError)
   {
      auto fail = [outError](const char* stage, const std::string& log) -> unsigned int
      {
         if (outError != nullptr)
            *outError = std::string(stage) + log;
         else
            fprintf(stderr, "GLUtil::CompileProgram %s%s\n", stage, log.c_str());
         return 0;
      };

      std::string log;
      const unsigned int vert = CompileStage(GL_VERTEX_SHADER, kVertSrc, log);
      if (vert == 0)
         return fail("shader error: ", log);

      const unsigned int frag = CompileStage(GL_FRAGMENT_SHADER, fragSrc, log);
      if (frag == 0)
      {
         glDeleteShader(vert);
         return fail("shader error: ", log);
      }

      // Attribute slots are fixed before linking so the shared quad's VAO
      // layout (EnsureQuad) matches every program without a lookup.
      const unsigned int program = glCreateProgram();
      glBindAttribLocation(program, 0, "aPos");
      glBindAttribLocation(program, 1, "aUv");
      glAttachShader(program, vert);
      glAttachShader(program, frag);
      glLinkProgram(program);

      // The linked program keeps its own copy; the stage objects are done.
      glDetachShader(program, vert);
      glDetachShader(program, frag);
      glDeleteShader(vert);
      glDeleteShader(frag);

      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (!linked)
      {
         log = InfoLog(program, true);
         glDeleteProgram(program);
         return fail("link error: ", log);
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

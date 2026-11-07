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
#include <cstring>
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
         int slot = 0;
         unsigned long long lastUsedFrame = 0;
      };
      constexpr unsigned long long kScratchIdleFrames = 300;
      std::vector<ScratchEntry> sScratch; // a handful of entries at most: linear scan
      unsigned long long sScratchFrame = 0;
   }

   Fbo* AcquireScratchFbo(int w, int h, unsigned int internalFormat, int slot)
   {
      for (ScratchEntry& e : sScratch)
      {
         if (e.fbo.w == w && e.fbo.h == h && e.fbo.internalFormat == internalFormat && e.slot == slot)
         {
            e.lastUsedFrame = sScratchFrame;
            return &e.fbo;
         }
      }
      ScratchEntry e;
      e.slot = slot;
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

   // A compiled stage, kept by source text. Every instance of a node type
   // compiles the same source, and compiling is about two thirds of building
   // a program: adding a second Shape or Blur only links. The cache owns the
   // stage objects; callers must not delete them.
   static unsigned int CachedStage(GLenum stage, const char* src, std::string& log)
   {
      static std::map<std::string, unsigned int> sStages[2];
      static constexpr size_t kMaxStages = 512; // a live-typed Field Pixel adds one per edit
      std::map<std::string, unsigned int>& stages = sStages[stage == GL_VERTEX_SHADER ? 0 : 1];
      const auto it = stages.find(src);
      if (it != stages.end())
         return it->second;

      const unsigned int shader = CompileStage(stage, src, log);
      if (shader == 0)
         return 0;
      if (stages.size() >= kMaxStages)
      {
         // Linked programs keep their own copy, so dropping these is safe.
         for (const auto& entry : stages)
            glDeleteShader(entry.second);
         stages.clear();
      }
      stages.emplace(src, shader);
      return shader;
   }

   unsigned int ImageWorkingFormat()
   {
      static const unsigned int sFormat = getenv("INFINITE_IMAGE_RGBA8") != nullptr ? GL_RGBA8 : GL_RGBA16F;
      return sFormat;
   }

   std::string WithClampedOutput(const std::string& fragSrc, const char* beforeClamp)
   {
      std::string src = fragSrc;
      const size_t at = src.find("void main()");
      if (at == std::string::npos)
         return src;
      src.replace(at, 11, "void imageMain()");
      // uImgHeadroom (SetImageHeadroom): 0, the default, is the old 0..1
      // clip; 1 keeps light above 1.0 (half-float max), alpha stays 0..1.
      if (src.find("uniform int uImgHeadroom;") == std::string::npos)
      {
         const size_t eol = src.rfind("#version", 0) == 0 ? src.find('\n') + 1 : 0;
         src.insert(eol, "uniform int uImgHeadroom;\n");
      }
      // Half-float targets truncate toward zero on write, so a value a hair
      // under a half step loses the whole step: 2^-10 above 1.0, 0.2% of
      // the light per node. With headroom, adding half a step first makes
      // the write round to nearest (legacy writes are left byte-identical).
      src += std::string("\nvoid main() { imageMain(); ") + beforeClamp +
             " if (uImgHeadroom == 1) {"
             " vec3 c = clamp(fragColor.rgb, 0.0, 65504.0);"
             " c += exp2(floor(log2(max(c, vec3(6.103515625e-05)))) - 11.0);"
             " fragColor = vec4(min(c, vec3(65504.0)), clamp(fragColor.a, 0.0, 1.0)); }"
             " else fragColor = clamp(fragColor, 0.0, 1.0); }\n";
      return src;
   }

   void SetImageHeadroom(unsigned int program, bool on)
   {
      const GLint loc = glGetUniformLocation(program, "uImgHeadroom");
      if (loc >= 0)
         glUniform1i(loc, on ? 1 : 0);
   }

   const char* const kOutputDitherGlsl =
      "vec3 OutputDither(vec3 c) {\n"
      "   // interleaved gradient noise (Jimenez 2014): fixed, so renders stay deterministic\n"
      "   float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
      "   return c + (n - 0.5) * (0.999 / 255.0);\n"
      "}\n";

   // The curved decode segment is a degree-5 fit of ((c+0.055)/1.055)^2.4 on
   // [0.04045, 1] pinned to the exact values at both ends (continuous with
   // the linear segment, white stays 1.0 for the dividing blend modes), max
   // error 0.03 of an 8-bit code: every 8-bit code round trips through
   // SrgbEncode unchanged. Negatives take the linear segment,
   // which is already odd-symmetric.
   const char* const kSrgbGlsl =
      "vec3 SrgbDecode(vec3 c) {\n"
      "   vec3 curve = 0.0008922681118 + c * (0.03350293781 + c * (0.5124953018 + c * (0.6903623787 + c * (-0.3381287731 + c * 0.1008758867))));\n"
      "   vec3 d = mix(c / 12.92, curve, step(0.04045, c));\n"
      // the fit only holds on [0, 1]: headroom above white decodes exactly
      "   if (any(greaterThan(c, vec3(1.0)))) d = mix(d, pow(max(c + 0.055, 0.0) / 1.055, vec3(2.4)), step(1.0, c));\n"
      "   return d;\n"
      "}\n"
      "vec3 SrgbEncode(vec3 c) {\n"
      "   vec3 a = abs(c);\n"
      "   return sign(c) * mix(a * 12.92, 1.055 * pow(a, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, a));\n"
      "}\n";

   void WarmFragmentStage(const char* fragSrc)
   {
      std::string log;
      CachedStage(GL_VERTEX_SHADER, kVertSrc, log);
      CachedStage(GL_FRAGMENT_SHADER, fragSrc, log);
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
      const unsigned int vert = CachedStage(GL_VERTEX_SHADER, kVertSrc, log);
      if (vert == 0)
         return fail("shader error: ", log);

      const unsigned int frag = CachedStage(GL_FRAGMENT_SHADER, fragSrc, log);
      if (frag == 0)
         return fail("shader error: ", log);

      // Attribute slots are fixed before linking so the shared quad's VAO
      // layout (EnsureQuad) matches every program without a lookup.
      const unsigned int program = glCreateProgram();
      glBindAttribLocation(program, 0, "aPos");
      glBindAttribLocation(program, 1, "aUv");
      glAttachShader(program, vert);
      glAttachShader(program, frag);
      glLinkProgram(program);

      // The linked program keeps its own copy; the stages stay in the cache.
      glDetachShader(program, vert);
      glDetachShader(program, frag);

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
                             bool checkerBg, bool transparentWindow)
   {
      static const std::string kBlitFragSrc = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n") +
         kOutputDitherGlsl +
         "void main() { vec4 c = texture(uTex, vUv); fragColor = vec4(OutputDither(c.rgb), c.a); }\n";

      // Composites the texture's own alpha over the same dark checkerboard
      // pattern the node editor draws behind a transparent preview (see
      // DrawCheckerboardBackdrop in main.cpp), so a node with a transparent
      // background reads the same way here as it does inline/in the viewport
      // panel instead of just showing solid black where alpha is 0.
      static const std::string kBlitCheckerFragSrc = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n") +
         kOutputDitherGlsl +
         "void main()\n"
         "{\n"
         "   vec4 c = texture(uTex, vUv);\n"
         "   vec2 cell = floor(gl_FragCoord.xy / 12.0);\n"
         "   float parity = mod(cell.x + cell.y, 2.0);\n"
         "   vec3 bg = mix(vec3(30.0, 30.0, 38.0) / 255.0, vec3(18.0, 18.0, 24.0) / 255.0, parity);\n"
         "   fragColor = vec4(OutputDither(mix(bg, c.rgb, c.a)), 1.0);\n"
         "}\n";

      // For a window with a transparent framebuffer: the texture's own alpha
      // goes to the window (premultiplied, what every compositor expects) so
      // the desktop / OBS window capture sees through it, no backdrop.
      static const std::string kBlitPremulFragSrc = std::string(
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n") +
         kOutputDitherGlsl +
         // Bilinear in premultiplied space: filtering straight alpha mixes the black RGB under transparent
         // texels into the edge and draws a dark fringe; premultiplying each tap first keeps the edge colour.
         "void main()\n"
         "{\n"
         "   ivec2 sz = textureSize(uTex, 0);\n"
         "   vec2 p = vUv * vec2(sz) - 0.5;\n"
         "   ivec2 i0 = ivec2(floor(p));\n"
         "   vec2 f = p - vec2(i0);\n"
         "   ivec2 hi = sz - ivec2(1);\n"
         "   vec4 a = texelFetch(uTex, clamp(i0, ivec2(0), hi), 0);\n"
         "   vec4 b = texelFetch(uTex, clamp(i0 + ivec2(1, 0), ivec2(0), hi), 0);\n"
         "   vec4 c = texelFetch(uTex, clamp(i0 + ivec2(0, 1), ivec2(0), hi), 0);\n"
         "   vec4 d = texelFetch(uTex, clamp(i0 + ivec2(1, 1), ivec2(0), hi), 0);\n"
         "   a.rgb *= a.a; b.rgb *= b.a; c.rgb *= c.a; d.rgb *= d.a;\n"
         "   vec4 m = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
         "   fragColor = vec4(OutputDither(m.rgb), m.a);\n"
         "}\n";

      static unsigned int sPremulProgram = 0;
      static int sLocTexPremul = -1;
      static unsigned int sBlitProgram = 0;
      static int sLocTex = -1;
      static unsigned int sCheckerProgram = 0;
      static int sLocTexChecker = -1;
      if (sBlitProgram == 0)
      {
         sBlitProgram = CompileProgram(kBlitFragSrc.c_str());
         sLocTex = glGetUniformLocation(sBlitProgram, "uTex");
      }
      if (sCheckerProgram == 0)
      {
         sCheckerProgram = CompileProgram(kBlitCheckerFragSrc.c_str());
         sLocTexChecker = glGetUniformLocation(sCheckerProgram, "uTex");
      }
      if (transparentWindow && sPremulProgram == 0)
      {
         sPremulProgram = CompileProgram(kBlitPremulFragSrc.c_str());
         sLocTexPremul = glGetUniformLocation(sPremulProgram, "uTex");
      }
      const unsigned int program = transparentWindow ? sPremulProgram : (checkerBg ? sCheckerProgram : sBlitProgram);
      const int locTex = transparentWindow ? sLocTexPremul : (checkerBg ? sLocTexChecker : sLocTex);
      if (program == 0)
         return;

      // Clear the full window first (letterbox bars, if any, show this).
      // Transparent window: the bars are transparent too.
      glViewport(0, 0, windowW, windowH);
      if (transparentWindow)
         glClearColor(0, 0, 0, 0);
      else
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

   void AsyncRead::Issue(int w, int h, GLenum format, GLenum type, size_t bytesPerPixel)
   {
      if (fence != nullptr)
         glDeleteSync((GLsync)fence);
      bytes = (size_t)w * (size_t)h * bytesPerPixel;
      if (pbo == 0)
         glGenBuffers(1, &pbo);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
      glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_READ);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, w, h, format, type, (void*)0);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      fence = (void*)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   }

   bool AsyncRead::Take(void* out, size_t outBytes, bool wait)
   {
      if (fence == nullptr || outBytes < bytes)
         return false;
      const GLuint64 timeout = wait ? 2000000000ull : 0; // 2 s, then give up
      const GLenum state = glClientWaitSync((GLsync)fence, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
      if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED)
         return false;
      glDeleteSync((GLsync)fence);
      fence = nullptr;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
      const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT);
      if (mapped != nullptr)
         std::memcpy(out, mapped, bytes);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      return mapped != nullptr;
   }

   void AsyncRead::Destroy()
   {
      if (fence != nullptr)
         glDeleteSync((GLsync)fence);
      fence = nullptr;
      if (pbo != 0)
         glDeleteBuffers(1, &pbo);
      pbo = 0;
      bytes = 0;
   }
}

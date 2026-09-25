#define INFINITE_GL_NO_REDIRECT
#include "platform/OpenGLHeaders.h"
#include "GLUtil.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "platform/SettingsPaths.h"

namespace GLUtil
{
   namespace
   {
      struct UniformEntry
      {
         std::string name;
         GLint location = -1;
      };
      // Main (GL) thread only, like every other GL call in the app.
      std::unordered_map<GLuint, std::vector<UniformEntry>>& UniformCache()
      {
         static std::unordered_map<GLuint, std::vector<UniformEntry>> cache;
         return cache;
      }
   }

   GLint CachedUniformLocation(GLuint program, const GLchar* name)
   {
      if (program == 0 || name == nullptr)
         return -1;
      std::vector<UniformEntry>& entries = UniformCache()[program];
      for (const UniformEntry& e : entries)
         if (std::strcmp(e.name.c_str(), name) == 0)
            return e.location;
      const GLint location = glGetUniformLocation(program, name);
      // Only cache once the program is linked; before that the driver answers
      // -1 with GL_INVALID_OPERATION and a later link would make it valid.
      GLint linked = GL_FALSE;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (linked == GL_TRUE)
         entries.push_back({ name, location });
      return location;
   }

   void LinkProgramInvalidate(GLuint program)
   {
      UniformCache().erase(program);
      glLinkProgram(program);
   }

   void DeleteProgramInvalidate(GLuint program)
   {
      UniformCache().erase(program);
      glDeleteProgram(program);
   }
}


#include "platform/OpenGLHeaders.h"
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

   void DrawFullscreenQuad()
   {
      DrawQuad();
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

   // ---- Turbo: on-disk program binary cache -----------------------------
   // Every node compiles its GLSL the first time it cooks, so opening a big
   // patch used to stall on dozens of driver compiles. Linked programs are
   // saved with glGetProgramBinary under
   // %LOCALAPPDATA%\Infinite\shadercache\<driver hash>\<source hash>.bin and
   // loaded with glProgramBinary next time. A driver update changes the
   // driver hash (new folder); a rejected binary falls back to compiling.
   // Disable with INFINITE_SHADER_CACHE=0.
   namespace
   {
      uint64_t Fnv1a64(const std::string& text, uint64_t hash = 1469598103934665603ull)
      {
         for (unsigned char c : text)
         {
            hash ^= c;
            hash *= 1099511628211ull;
         }
         return hash;
      }

      std::string Hex64(uint64_t v)
      {
         char buf[17];
         std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
         return buf;
      }

      const std::filesystem::path& ShaderCacheDir()
      {
         static std::filesystem::path dir = []() -> std::filesystem::path
         {
            const char* off = std::getenv("INFINITE_SHADER_CACHE");
            if (off != nullptr && std::strcmp(off, "0") == 0)
               return {};
            GLint formats = 0;
            if (GLEW_VERSION_4_1 || GLEW_ARB_get_program_binary)
               glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
            if (formats <= 0)
               return {};
            const std::string settings = InfiniteSettingsDirectory();
            if (settings.empty())
               return {};
            auto str = [](GLenum e) -> std::string
            {
               const GLubyte* s = glGetString(e);
               return s ? std::string((const char*)s) : std::string();
            };
            const std::string driver = str(GL_VENDOR) + "|" + str(GL_RENDERER) + "|" + str(GL_VERSION);
            std::filesystem::path d = std::filesystem::u8path(settings) / "shadercache" / Hex64(Fnv1a64(driver));
            std::error_code ec;
            std::filesystem::create_directories(d, ec);
            return ec ? std::filesystem::path() : d;
         }();
         return dir;
      }

      constexpr uint32_t kShaderCacheMagic = 0x43535449; // "ITSC"

      unsigned int LoadCachedProgram(const std::filesystem::path& file)
      {
         std::ifstream in(file, std::ios::binary);
         if (!in)
            return 0;
         uint32_t magic = 0, format = 0;
         in.read((char*)&magic, sizeof(magic));
         in.read((char*)&format, sizeof(format));
         if (!in || magic != kShaderCacheMagic)
            return 0;
         std::vector<char> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
         if (blob.empty())
            return 0;
         unsigned int program = glCreateProgram();
         glProgramBinary(program, (GLenum)format, blob.data(), (GLsizei)blob.size());
         GLint linked = GL_FALSE;
         glGetProgramiv(program, GL_LINK_STATUS, &linked);
         if (linked != GL_TRUE)
         {
            glDeleteProgram(program);
            glGetError(); // swallow GL_INVALID_ENUM from a stale format
            return 0;
         }
         return program;
      }

      void SaveCachedProgram(const std::filesystem::path& file, unsigned int program)
      {
         GLint length = 0;
         glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
         if (length <= 0)
            return;
         std::vector<char> blob((size_t)length);
         GLenum format = 0;
         GLsizei written = 0;
         glGetProgramBinary(program, length, &written, &format, blob.data());
         if (written <= 0)
            return;
         const std::filesystem::path tmp = file.string() + ".tmp";
         {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
               return;
            const uint32_t magic = kShaderCacheMagic, fmt = (uint32_t)format;
            out.write((const char*)&magic, sizeof(magic));
            out.write((const char*)&fmt, sizeof(fmt));
            out.write(blob.data(), written);
            if (!out)
               return;
         }
         std::error_code ec;
         std::filesystem::rename(tmp, file, ec);
      }
   }

   unsigned int CompileProgram(const char* fragSrc, std::string* outError)
   {
      std::filesystem::path cacheFile;
      if (!ShaderCacheDir().empty())
      {
         cacheFile = ShaderCacheDir() / (Hex64(Fnv1a64(std::string(fragSrc), Fnv1a64(kVertSrc))) + ".bin");
         if (unsigned int cached = LoadCachedProgram(cacheFile))
            return cached;
      }

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
      if (!cacheFile.empty())
         glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
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

      if (!cacheFile.empty())
         SaveCachedProgram(cacheFile, program);
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
                             bool checkerBg, int fitMode, const float* bgRGB)
   {
      static const char* kBlitFragSrc =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "uniform vec3 uBg;\n"
         // Turbo: composite over the window background by the texture's
         // alpha, so transparent / edge-blended outputs fade to black on a
         // projector instead of showing their raw RGB.
         "void main() { vec4 c = texture(uTex, vUv); fragColor = vec4(mix(uBg, c.rgb, clamp(c.a, 0.0, 1.0)), 1.0); }\n";

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
      static int sLocBg = -1;
      static unsigned int sCheckerProgram = 0;
      static int sLocTexChecker = -1;
      if (sBlitProgram == 0)
      {
         sBlitProgram = CompileProgram(kBlitFragSrc);
         sLocTex = glGetUniformLocation(sBlitProgram, "uTex");
         sLocBg = glGetUniformLocation(sBlitProgram, "uBg");
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
      if (bgRGB != nullptr)
         glClearColor(bgRGB[0], bgRGB[1], bgRGB[2], 1);
      else
         glClearColor(0.0f, 0.0f, 0.0f, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      int vpX = 0, vpY = 0, vpW = windowW, vpH = windowH;
      const bool haveSize = texW > 0 && texH > 0 && windowW > 0 && windowH > 0;
      if (haveSize && fitMode == kFitPixel)
      {
         // Real pixel size, centred. Larger than the window = cropped by
         // the viewport, smaller = black around it.
         vpW = texW;
         vpH = texH;
         vpX = (windowW - texW) / 2;
         vpY = (windowH - texH) / 2;
      }
      else if (haveSize && fitMode == kFitFill)
      {
         // Cover the whole window keeping aspect; the overflow is cropped.
         const float scale = std::max((float)windowW / (float)texW, (float)windowH / (float)texH);
         vpW = (int)(texW * scale + 0.5f);
         vpH = (int)(texH * scale + 0.5f);
         vpX = (windowW - vpW) / 2;
         vpY = (windowH - vpH) / 2;
      }
      else if (haveSize && fitMode == kFitStretch)
      {
         // whole window, aspect ignored (vp already covers it)
      }
      else if (haveSize)
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
      if (!checkerBg && sLocBg >= 0)
      {
         if (bgRGB != nullptr)
            glUniform3f(sLocBg, bgRGB[0], bgRGB[1], bgRGB[2]);
         else
            glUniform3f(sLocBg, 0.0f, 0.0f, 0.0f);
      }

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

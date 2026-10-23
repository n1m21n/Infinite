#define INFINITE_GL_NO_REDIRECT
#include "platform/OpenGLHeaders.h"
#include "GLUtil.h"
#include <algorithm>
#include <atomic>
#include <map>
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
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

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

   bool EnsureFbo(Fbo& fbo, int w, int h, unsigned int internalFormat, const char* label)
   {
      if (w <= 0 || h <= 0)
         return false;

      if (fbo.fbo != 0 && fbo.w == w && fbo.h == h && fbo.internalFormat == internalFormat)
         return true;

      DestroyFbo(fbo);

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

      LabelObject(GL_TEXTURE, fbo.tex, label);
      LabelObject(GL_FRAMEBUFFER, fbo.fbo, label);
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

   namespace
   {
      struct ScratchEntry
      {
         Fbo fbo;
         unsigned long long lastUsedFrame = 0;
      };
      constexpr unsigned long long kScratchIdleFrames = 300;
      std::vector<ScratchEntry> sScratch; // a handful of entries at most
      unsigned long long sScratchFrame = 0;

      // Turbo 0.70: INFINITE_GPUPROF=1 - every debug group is also timed on the
      // GPU (timestamp queries at push and pop, read two frames later without
      // waiting) and the average per group is printed every 120 frames: which
      // pass the GPU spends a frame on, by the same names as captures.
      bool GpuProfOn()
      {
         static const bool on = getenv("INFINITE_GPUPROF") != nullptr;
         return on;
      }
      struct GpuZone
      {
         std::string name;
         GLuint q0 = 0, q1 = 0;
         unsigned long long frame = 0;
      };
      std::vector<GLuint> sFreeQueries;
      std::vector<GpuZone> sOpenZones, sDoneZones;
      std::map<std::string, std::pair<double, int>> sZoneTotals; // ms, calls
      unsigned long long sProfStart = 0;

      GLuint TakeQuery()
      {
         if (sFreeQueries.empty())
         {
            GLuint q[64];
            glGenQueries(64, q);
            sFreeQueries.assign(q, q + 64);
         }
         const GLuint q = sFreeQueries.back();
         sFreeQueries.pop_back();
         return q;
      }
   }

   Fbo* AcquireScratchFbo(int w, int h, unsigned int internalFormat)
   {
      for (ScratchEntry& e : sScratch)
         if (e.fbo.w == w && e.fbo.h == h && e.fbo.internalFormat == internalFormat)
         {
            e.lastUsedFrame = sScratchFrame;
            return &e.fbo;
         }
      ScratchEntry e;
      if (!EnsureFbo(e.fbo, w, h, internalFormat))
         return nullptr;
      e.lastUsedFrame = sScratchFrame;
      sScratch.push_back(e);
      return &sScratch.back().fbo;
   }

   unsigned int SamplerFor(int extend, int filter)
   {
      extend = extend < 0 || extend >= kExtendCount ? kExtendHold : extend;
      filter = filter < 0 || filter >= kFilterCount ? kFilterLinear : filter;
      static GLuint sSamplers[kExtendCount][kFilterCount] = {};
      static bool sUnsupported = false;
      if (sUnsupported)
         return 0;
      GLuint& s = sSamplers[extend][filter];
      if (s != 0)
         return s;
      if (!(GLEW_VERSION_3_3 || GLEW_ARB_sampler_objects))
      {
         sUnsupported = true;
         return 0;
      }
      glGenSamplers(1, &s);
      static const GLenum kWrap[kExtendCount] = { GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER, GL_REPEAT, GL_MIRRORED_REPEAT };
      glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)kWrap[extend]);
      glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)kWrap[extend]);
      const GLfloat transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      glSamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, transparent);
      const GLint mag = filter == kFilterNearest ? GL_NEAREST : GL_LINEAR;
      const GLint min = filter == kFilterNearest ? GL_NEAREST
                      : filter == kFilterMipmap  ? GL_LINEAR_MIPMAP_LINEAR
                                                 : GL_LINEAR;
      glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, mag);
      glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
      return s;
   }

   void BindSampler(int unit, int extend, int filter)
   {
      if (GLuint s = SamplerFor(extend, filter))
         glBindSampler((GLuint)unit, s);
   }

   void UnbindSampler(int unit)
   {
      if (GLEW_VERSION_3_3 || GLEW_ARB_sampler_objects)
         glBindSampler((GLuint)unit, 0);
   }

   void GenerateMipmaps(unsigned int tex)
   {
      if (tex == 0)
         return;
      GLint prev = 0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
      glBindTexture(GL_TEXTURE_2D, tex);
      glGenerateMipmap(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
   }

   const char* FormatName(unsigned int internalFormat)
   {
      switch (internalFormat)
      {
         case GL_RGBA16F: return "16f";
         case GL_RGBA32F: return "32f";
         default: return "8-bit";
      }
   }

   void EndFrameScratchFbos()
   {
      ++sScratchFrame;
      // INFINITE_GPUPROF: collect zones from two frames ago, report every 120.
      if (GpuProfOn())
      {
         size_t used = 0;
         for (; used < sDoneZones.size(); used++)
         {
            GpuZone& z = sDoneZones[used];
            if (z.frame + 2 > sScratchFrame)
               break;
            GLint ready = 0;
            glGetQueryObjectiv(z.q1, GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready)
               break;
            GLuint64 t0 = 0, t1 = 0;
            glGetQueryObjectui64v(z.q0, GL_QUERY_RESULT, &t0);
            glGetQueryObjectui64v(z.q1, GL_QUERY_RESULT, &t1);
            auto& tot = sZoneTotals[z.name];
            tot.first += (double)(t1 - t0) * 1e-6;
            tot.second++;
            sFreeQueries.push_back(z.q0);
            sFreeQueries.push_back(z.q1);
         }
         sDoneZones.erase(sDoneZones.begin(), sDoneZones.begin() + (std::ptrdiff_t)used);
         if (sProfStart == 0)
            sProfStart = sScratchFrame;
         if (sScratchFrame - sProfStart >= 120)
         {
            const double frames = (double)(sScratchFrame - sProfStart);
            std::vector<std::pair<double, std::string>> rows;
            for (const auto& kv : sZoneTotals)
               rows.push_back({ kv.second.first / frames, kv.first });
            std::sort(rows.rbegin(), rows.rend());
            printf("GPUPROF (ms per frame, nested groups include their children):");
            for (size_t i = 0; i < rows.size() && i < 12; i++)
               printf(" [%s %.2f]", rows[i].second.c_str(), rows[i].first);
            printf("\n");
            sZoneTotals.clear();
            sProfStart = sScratchFrame;
         }
      }
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

      // Turbo 0.59 (upstream's "adding a node no longer recompiles its
      // shader"): program binaries already built or read this session, by
      // source hash, so a second Blur / Shape / effect links from memory -
      // no disk read and no compile in the frame the node is added.
      struct MemBinary
      {
         uint32_t format = 0;
         std::vector<char> blob;
      };
      std::unordered_map<uint64_t, MemBinary>& MemCache()
      {
         static std::unordered_map<uint64_t, MemBinary> cache;
         return cache;
      }

      unsigned int ProgramFromBinary(uint32_t format, const std::vector<char>& blob)
      {
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

      void RememberProgramBinary(uint64_t key, unsigned int program)
      {
         GLint length = 0;
         glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
         if (length <= 0)
            return;
         MemBinary mb;
         mb.blob.resize((size_t)length);
         GLenum format = 0;
         GLsizei written = 0;
         glGetProgramBinary(program, length, &written, &format, mb.blob.data());
         if (written <= 0)
            return;
         mb.blob.resize((size_t)written);
         mb.format = (uint32_t)format;
         MemCache()[key] = std::move(mb);
      }

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
      const uint64_t key = Fnv1a64(std::string(fragSrc), Fnv1a64(kVertSrc));
      const bool binaries = !ShaderCacheDir().empty();
      if (binaries)
      {
         // Turbo 0.59: memory first, then the disk cache (kept in memory after).
         auto hit = MemCache().find(key);
         if (hit != MemCache().end())
         {
            if (unsigned int p = ProgramFromBinary(hit->second.format, hit->second.blob))
               return p;
            MemCache().erase(hit);
         }
         cacheFile = ShaderCacheDir() / (Hex64(key) + ".bin");
         if (unsigned int cached = LoadCachedProgram(cacheFile))
         {
            RememberProgramBinary(key, cached);
            return cached;
         }
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
      if (binaries)
         RememberProgramBinary(key, program);
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
                             bool checkerBg, int fitMode, const float* bgRGB, bool transparentWindow)
   {
      // Turbo 0.51: premultiplied alpha straight to a transparent-framebuffer window.
      static const char* kBlitPremulFragSrc =
         "#version 150\n"
         "in vec2 vUv;\n"
         "out vec4 fragColor;\n"
         "uniform sampler2D uTex;\n"
         "void main() { vec4 c = texture(uTex, vUv); fragColor = vec4(c.rgb * c.a, c.a); }\n";
      static unsigned int sPremulProgram = 0;
      static int sLocTexPremul = -1;

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
      if (transparentWindow && sPremulProgram == 0)
      {
         sPremulProgram = CompileProgram(kBlitPremulFragSrc);
         sLocTexPremul = glGetUniformLocation(sPremulProgram, "uTex");
      }
      const bool premul = transparentWindow && sPremulProgram != 0;
      const unsigned int program = premul ? sPremulProgram : (checkerBg ? sCheckerProgram : sBlitProgram);
      const int locTex = premul ? sLocTexPremul : (checkerBg ? sLocTexChecker : sLocTex);
      if (program == 0)
         return;

      // Clear the full window first (letterbox bars, if any, show this).
      glViewport(0, 0, windowW, windowH);
      if (premul)
         glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // bars are transparent too
      else if (bgRGB != nullptr)
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
      if (!premul && !checkerBg && sLocBg >= 0)
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

namespace GLUtil
{
   namespace
   {
      size_t PixelBytes(unsigned int format, unsigned int type)
      {
         const size_t comps = format == GL_RGBA ? 4 : format == GL_RGB ? 3 : format == GL_RG ? 2 : 1;
         const size_t size = type == GL_FLOAT ? 4 : (type == GL_HALF_FLOAT ? 2 : 1);
         return comps * size;
      }
   }

   AsyncReadback::~AsyncReadback()
   {
      for (Slot& s : mSlots)
      {
         if (s.fence)
            glDeleteSync((GLsync)s.fence);
         if (s.pbo)
            glDeleteBuffers(1, &s.pbo);
      }
      if (mFbo)
         glDeleteFramebuffers(1, &mFbo);
   }

   bool AsyncReadback::Read(unsigned int tex, int w, int h, unsigned int format, unsigned int type, void* out)
   {
      if (tex == 0)
         return false;
      return Run(tex, w, h, format, type, out);
   }

   bool AsyncReadback::ReadBound(int w, int h, unsigned int format, unsigned int type, void* out)
   {
      return Run(0, w, h, format, type, out);
   }

   bool AsyncReadback::Run(unsigned int tex, int w, int h, unsigned int format, unsigned int type, void* out)
   {
      if (w <= 0 || h <= 0 || out == nullptr)
         return false;
      const size_t bytes = (size_t)w * h * PixelBytes(format, type);

      GLint prevRead = 0, prevPack = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPack);
      if (tex != 0)
      {
         if (mFbo == 0)
            glGenFramebuffers(1, &mFbo);
         glBindFramebuffer(GL_READ_FRAMEBUFFER, mFbo);
         glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
         if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
         {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
            return false;
         }
      }
      glPixelStorei(GL_PACK_ALIGNMENT, 1);

      bool got = false;
      const bool sameShape = w == mLastW && h == mLastH && format == mLastFormat && type == mLastType;
      if (!sameShape)
      {
         // New shape (or the first call): drop what is in flight, read now.
         for (Slot& s : mSlots)
            if (s.fence)
            {
               glDeleteSync((GLsync)s.fence);
               s.fence = nullptr;
            }
         glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
         glReadPixels(0, 0, w, h, (GLenum)format, (GLenum)type, out);
         mLastW = w;
         mLastH = h;
         mLastFormat = format;
         mLastType = type;
         got = true;
      }
      else
      {
         // The newest copy the GPU has finished; older finished ones are dropped.
         Slot* best = nullptr;
         for (Slot& s : mSlots)
         {
            if (s.fence == nullptr)
               continue;
            const GLenum st = glClientWaitSync((GLsync)s.fence, 0, 0);
            if (st != GL_ALREADY_SIGNALED && st != GL_CONDITION_SATISFIED)
               continue;
            glDeleteSync((GLsync)s.fence);
            s.fence = nullptr;
            if (s.w == w && s.h == h && s.format == format && s.type == type && (best == nullptr || s.order > best->order))
               best = &s;
         }
         if (best != nullptr)
         {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, best->pbo);
            if (const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT))
            {
               std::memcpy(out, p, bytes);
               glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
               got = true;
            }
         }
         // Queue this frame's copy.
         for (Slot& s : mSlots)
         {
            if (s.fence != nullptr)
               continue;
            if (s.pbo == 0)
               glGenBuffers(1, &s.pbo);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
            if (s.capacity < bytes)
            {
               glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_READ);
               s.capacity = bytes;
            }
            glReadPixels(0, 0, w, h, (GLenum)format, (GLenum)type, nullptr);
            s.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            s.w = w;
            s.h = h;
            s.format = format;
            s.type = type;
            s.bytes = bytes;
            s.order = ++mSeq;
            break;
         }
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPack);
      if (tex != 0)
         glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
      return got;
   }

   // Turbo 0.70 (F3): GL_KHR_debug groups, labels and debug output.
   namespace
   {
      bool KhrDebug()
      {
         static const bool has = GLEW_VERSION_4_3 || GLEW_KHR_debug;
         return has;
      }

      // Open debug groups, innermost last (main thread). With synchronous
      // output (INFINITE_GLDEBUG=sync) the callback runs inside the failing
      // call, so it can say which group the message came from.
      std::vector<std::string> sGroups;
      bool sSyncOutput = false;

      // Synchronous errors: the calling code, from the PDB next to the exe
      // (Release builds link /DEBUG:FULL). Once per calling site, at most 32.
      void PrintCallers()
      {
#ifdef _WIN32
         static std::vector<DWORD64> sShown;
         static bool sSymbols = false;
         HANDLE process = GetCurrentProcess();
         if (!sSymbols)
         {
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            sSymbols = SymInitialize(process, nullptr, TRUE) != FALSE;
         }
         void* frames[24];
         const USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
         int printed = 0;
         for (USHORT i = 0; i < n && printed < 8; i++)
         {
            alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            DWORD64 disp = 0;
            if (!sSymbols || !SymFromAddr(process, (DWORD64)frames[i], &disp, sym))
               continue;
            // Skip this file's frames and the driver's.
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (!SymGetLineFromAddr64(process, (DWORD64)frames[i], &lineDisp, &line))
               continue;
            if (strstr(line.FileName, "GLUtil.cpp") != nullptr)
               continue;
            if (printed == 0)
            {
               // The first frame outside this file is the site: report it once.
               const DWORD64 site = (DWORD64)frames[i];
               if (sShown.size() >= 32 || std::find(sShown.begin(), sShown.end(), site) != sShown.end())
                  return;
               sShown.push_back(site);
            }
            fprintf(stderr, "    at %s (%s:%lu)\n", sym->Name, line.FileName, line.LineNumber);
            printed++;
         }
#endif
      }

      void GLAPIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                    const GLchar* message, const void* /*user*/)
      {
         if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || type == GL_DEBUG_TYPE_PUSH_GROUP ||
             type == GL_DEBUG_TYPE_POP_GROUP)
            return;
         // The driver may call this from its own thread.
         static std::atomic<int> sCount{ 0 };
         static const int sMax = getenv("INFINITE_GLDEBUG_MAX") ? atoi(getenv("INFINITE_GLDEBUG_MAX")) : 200;
         const int n = ++sCount;
         if (n > sMax)
         {
            if (n == sMax + 1)
               fprintf(stderr, "GL debug: further messages suppressed (INFINITE_GLDEBUG_MAX)\n");
            return;
         }
         const char* kind = type == GL_DEBUG_TYPE_ERROR                ? "error"
                            : type == GL_DEBUG_TYPE_PERFORMANCE         ? "performance"
                            : type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR  ? "undefined behaviour"
                            : type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR ? "deprecated"
                                                                        : "other";
         std::string where;
         if (sSyncOutput)
         {
            for (const std::string& g : sGroups)
               where += (where.empty() ? " in " : " > ") + g;
            if (where.empty())
               where = " outside any group";
         }
         // sScratchFrame counts main-loop frames (EndFrameScratchFbos).
         fprintf(stderr, "GL debug [%s, id %u, source 0x%x, frame %llu]%s: %s\n", kind, id, source, sScratchFrame,
                 where.c_str(), message);
         if (sSyncOutput && (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_PERFORMANCE))
            PrintCallers();
      }
   }

   void PushDebugGroup(const char* name)
   {
      if (GpuProfOn())
      {
         GpuZone z;
         z.name = name != nullptr ? name : "";
         z.q0 = TakeQuery();
         glQueryCounter(z.q0, GL_TIMESTAMP);
         sOpenZones.push_back(std::move(z));
      }
      if (!KhrDebug())
         return;
      glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name != nullptr ? name : "");
      if (sSyncOutput)
         sGroups.push_back(name != nullptr ? name : "");
   }

   void PopDebugGroup()
   {
      if (GpuProfOn() && !sOpenZones.empty())
      {
         GpuZone z = std::move(sOpenZones.back());
         sOpenZones.pop_back();
         z.q1 = TakeQuery();
         glQueryCounter(z.q1, GL_TIMESTAMP);
         z.frame = sScratchFrame;
         sDoneZones.push_back(std::move(z));
      }
      if (!KhrDebug())
         return;
      glPopDebugGroup();
      if (sSyncOutput && !sGroups.empty())
         sGroups.pop_back();
   }

   void LabelObject(unsigned int identifier, unsigned int object, const char* label)
   {
      if (KhrDebug() && object != 0 && label != nullptr)
         glObjectLabel(identifier, object, -1, label);
   }

   void InstallDebugOutput()
   {
      const char* env = getenv("INFINITE_GLDEBUG");
#ifdef NDEBUG
      if (env == nullptr)
         return;
#endif
      if (!KhrDebug())
      {
         fprintf(stderr, "GL debug: GL_KHR_debug not available\n");
         return;
      }
      glEnable(GL_DEBUG_OUTPUT);
      // "sync": slower, but each message names the debug group it came from.
      sSyncOutput = env != nullptr && strcmp(env, "sync") == 0;
      if (sSyncOutput)
         glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
      glDebugMessageCallback(DebugCallback, nullptr);
      fprintf(stderr, "GL debug output on%s\n", sSyncOutput ? " (synchronous)" : "");
   }
}

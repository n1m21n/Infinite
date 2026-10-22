#include "PopGpu.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include "platform/OpenGLHeaders.h"

namespace pop
{
   namespace
   {


      struct Program
      {
         GLuint prog = 0;
         std::unordered_map<std::string, GLint> locs;
         GLint Loc(const std::string& n)
         {
            auto it = locs.find(n);
            if (it != locs.end())
               return it->second;
            const GLint l = glGetUniformLocation(prog, n.c_str());
            locs[n] = l;
            return l;
         }
      };

      std::unordered_map<std::string, Program>& Cache()
      {
         static std::unordered_map<std::string, Program> cache;
         return cache;
      }

      GLuint gKernelVao = 0;



      bool Compile(const std::string& src, const std::vector<std::string>& attribNames,
                   const std::vector<std::string>& varyings, GLuint& outProg, std::string& error)
      {
         GLuint vs = glCreateShader(GL_VERTEX_SHADER);
         const char* s = src.c_str();
         glShaderSource(vs, 1, &s, nullptr);
         glCompileShader(vs);
         GLint ok = 0;
         glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
         if (!ok)
         {
            char log[4096] = { 0 };
            glGetShaderInfoLog(vs, sizeof(log) - 1, nullptr, log);
            error = std::string("kernel compile: ") + log;
            glDeleteShader(vs);
            return false;
         }
         GLuint prog = glCreateProgram();
         glAttachShader(prog, vs);
         for (size_t i = 0; i < attribNames.size(); i++)
            glBindAttribLocation(prog, (GLuint)(i + 1), attribNames[i].c_str());
         std::vector<const char*> names;
         for (const std::string& v : varyings)
            names.push_back(v.c_str());
         if (!names.empty())
            glTransformFeedbackVaryings(prog, (GLsizei)names.size(), names.data(), GL_SEPARATE_ATTRIBS);
         glLinkProgram(prog);
         glDeleteShader(vs);
         glGetProgramiv(prog, GL_LINK_STATUS, &ok);
         if (!ok)
         {
            char log[4096] = { 0 };
            glGetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
            error = std::string("kernel link: ") + log;
            glDeleteProgram(prog);
            return false;
         }
         outProg = prog;
         return true;
      }

      // Saves and restores the GL state a kernel run touches.
      struct StateGuard
      {
         GLint prog = 0, vao = 0, arrayBuf = 0, activeTex = 0, tex2d = 0, texBuf = 0;
         GLboolean discard = GL_FALSE;
         StateGuard()
         {
            glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
            discard = glIsEnabled(GL_RASTERIZER_DISCARD);
         }
         ~StateGuard()
         {
            if (!discard)
               glDisable(GL_RASTERIZER_DISCARD);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, (GLuint)tex2d);
            glActiveTexture((GLenum)activeTex);
            glUseProgram((GLuint)prog);
            glBindVertexArray((GLuint)vao);
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)arrayBuf);
         }
      };

      void SetUniform(Program& p, const KernelUniform& u)
      {
         const GLint l = p.Loc(u.name);
         if (l < 0)
            return;
         switch (u.type)
         {
            case KernelUniform::kFloat: glUniform1f(l, u.v[0]); break;
            case KernelUniform::kInt: glUniform1i(l, u.i); break;
            case KernelUniform::kVec2: glUniform2fv(l, 1, u.v); break;
            case KernelUniform::kVec3: glUniform3fv(l, 1, u.v); break;
            case KernelUniform::kVec4: glUniform4fv(l, 1, u.v); break;
            case KernelUniform::kMat4: glUniformMatrix4fv(l, 1, GL_FALSE, u.v); break;
         }
      }

   }

   bool RunKernel(const KernelSpec& spec, const PopData& in, const std::vector<KernelUniform>& uniforms,
                  const KernelEnv& env, std::vector<BufferPtr>& out, std::string& error)
   {
      out.clear();
      if (spec.writes.empty() || spec.writes.size() > 4)
      {
         error = "a kernel writes between one and four attributes";
         return false;
      }
      auto check = [&](const std::vector<KernelIO>& list) {
         for (const KernelIO& io : list)
            if (!ValidAttribName(io.name))
            {
               error = "'" + io.name + "' is not a usable attribute name";
               return false;
            }
         return true;
      };
      if (!check(spec.reads) || !check(spec.writes) || !check(spec.lookups))
         return false;

      const int count = in.pointCount;
      if (count <= 0)
      {
         for (const KernelIO& w : spec.writes)
            out.push_back(MakeBuffer(std::vector<float>(), w.comps));
         return true;
      }

      std::vector<std::string> attribNames, varyings;
      const std::string src = BuildKernelSource(spec, uniforms, attribNames, varyings);

      // ---- program -------------------------------------------------------
      auto& cache = Cache();
      auto it = cache.find(src);
      if (it == cache.end())
      {
         if (cache.size() > 512)
         {
            for (auto& kv : cache)
               glDeleteProgram(kv.second.prog);
            cache.clear();
         }
         Program p;
         if (!Compile(src, attribNames, varyings, p.prog, error))
            return false;
         it = cache.emplace(src, std::move(p)).first;
      }
      Program& prog = it->second;

      StateGuard guard;
      if (gKernelVao == 0)
         glGenVertexArrays(1, &gKernelVao);
      glUseProgram(prog.prog);
      glBindVertexArray(gKernelVao);

      glUniform1i(prog.Loc("uCount"), count);
      glUniform1f(prog.Loc("uTime"), (float)env.time);
      glUniform1f(prog.Loc("uBeat"), (float)env.beat);
      glUniform1f(prog.Loc("uDelta"), env.delta);
      glUniform1i(prog.Loc("uFrame"), env.frame);
      glUniform1f(prog.Loc("uSeed"), env.seed);
      for (const KernelUniform& u : uniforms)
         SetUniform(prog, u);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, env.imageTex);
      glUniform1i(prog.Loc("uImage"), 0);
      glUniform1i(prog.Loc("uHasImage"), env.imageTex != 0 ? 1 : 0);

      // Inputs: location i + 1 (0 stays free).
      std::vector<GLuint> enabled;
      for (size_t i = 0; i < spec.reads.size(); i++)
      {
         const GLuint loc = (GLuint)(i + 1);
         const PopAttrib* a = in.Find(spec.reads[i].name);
         if (a != nullptr && a->buffer->Count() >= count)
         {
            glBindBuffer(GL_ARRAY_BUFFER, a->buffer->Gpu());
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, a->buffer->Comps(), GL_FLOAT, GL_FALSE, 0, nullptr);
            enabled.push_back(loc);
         }
         else
         {
            glDisableVertexAttribArray(loc);
            float d[4];
            DefaultValue(spec.reads[i].name, d);
            glVertexAttrib4fv(loc, d);
         }
      }

      // Lookups: float texture buffers on units 1..
      std::vector<GLuint> tbos;
      for (size_t k = 0; k < spec.lookups.size(); k++)
      {
         const KernelIO& l = spec.lookups[k];
         const int from = l.input >= 0 ? l.input : spec.lookupInput;
         const PopData* src2 = env.lookupData[from == 1 ? 1 : 0];
         if (src2 == nullptr)
            src2 = &in;
         const PopAttrib* a = src2->Find(l.name);
         GLuint tex = 0;
         glGenTextures(1, &tex);
         tbos.push_back(tex);
         glActiveTexture(GL_TEXTURE1 + (GLenum)k);
         glBindTexture(GL_TEXTURE_BUFFER, tex);
         BufferPtr keep = a ? a->buffer : MakeFilled(1, l.comps, [&] { static float d[4]; DefaultValue(l.name, d); return d; }());
         glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, keep->Gpu());
         const std::string& ln = l.as.empty() ? l.name : l.as;
         glUniform1i(prog.Loc("t_" + ln), (GLint)(1 + k));
         glUniform1i(prog.Loc("c_" + ln), keep->Comps());
         glUniform1i(prog.Loc("n_" + ln), keep->Count());
      }

      // Outputs.
      std::vector<GLuint> outBufs;
      for (size_t w = 0; w < spec.writes.size(); w++)
      {
         const GLuint b = AcquireGpuBuffer((size_t)count * (size_t)spec.writes[w].comps * sizeof(float));
         glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, (GLuint)w, b);
         outBufs.push_back(b);
      }

      glEnable(GL_RASTERIZER_DISCARD);
      glBeginTransformFeedback(GL_POINTS);
      glDrawArrays(GL_POINTS, 0, count);
      glEndTransformFeedback();
      glDisable(GL_RASTERIZER_DISCARD);

      for (size_t w = 0; w < outBufs.size(); w++)
         glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, (GLuint)w, 0);
      for (GLuint loc : enabled)
         glDisableVertexAttribArray(loc);
      for (size_t k = 0; k < tbos.size(); k++)
      {
         glActiveTexture(GL_TEXTURE1 + (GLenum)k);
         glBindTexture(GL_TEXTURE_BUFFER, 0);
      }
      if (!tbos.empty())
         glDeleteTextures((GLsizei)tbos.size(), tbos.data());

      for (size_t w = 0; w < outBufs.size(); w++)
         out.push_back(std::make_shared<PopBuffer>(outBufs[w], count, spec.writes[w].comps));
      return true;
   }

   AsyncRows::~AsyncRows()
   {
      for (Slot& s : mSlots)
      {
         if (s.fence)
            glDeleteSync((GLsync)s.fence);
         if (s.buf)
         {
            glBindBuffer(GL_COPY_WRITE_BUFFER, s.buf);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            glDeleteBuffers(1, &s.buf);
         }
      }
   }

   void AsyncRows::Request(const PopData& d, const std::vector<std::string>& names)
   {
      Latest(); // retire finished copies first
      Slot& s = mSlots[mNext];
      if (s.fence != nullptr) // both slots still in flight: skip this frame
         return;
      std::vector<const PopAttrib*> attrs;
      std::vector<size_t> offs;
      size_t bytes = 0;
      for (const std::string& n : names)
         if (const PopAttrib* a = d.Find(n))
            if (a->buffer && a->buffer->Count() == d.pointCount)
            {
               attrs.push_back(a);
               offs.push_back(bytes);
               bytes += (size_t)d.pointCount * (size_t)a->buffer->Comps() * sizeof(float);
               bytes = (bytes + 255) / 256 * 256;
            }
      if (attrs.empty() || bytes == 0)
         return;
      if (bytes > s.bytes)
      {
         if (s.buf)
         {
            glBindBuffer(GL_COPY_WRITE_BUFFER, s.buf);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            glDeleteBuffers(1, &s.buf);
         }
         const size_t want = bytes + bytes / 4;
         glGenBuffers(1, &s.buf);
         glBindBuffer(GL_COPY_WRITE_BUFFER, s.buf);
         const GLbitfield flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
         glBufferStorage(GL_COPY_WRITE_BUFFER, (GLsizeiptr)want, nullptr, flags | GL_CLIENT_STORAGE_BIT);
         s.ptr = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, (GLsizeiptr)want, flags);
         s.bytes = s.ptr ? want : 0;
         if (s.ptr == nullptr)
         {
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            return;
         }
      }
      glBindBuffer(GL_COPY_WRITE_BUFFER, s.buf);
      s.snap = Snapshot();
      s.snap.count = d.pointCount;
      s.snap.revision = d.revision;
      for (size_t i = 0; i < attrs.size(); i++)
      {
         const PopAttrib* a = attrs[i];
         glBindBuffer(GL_COPY_READ_BUFFER, a->buffer->Gpu());
         glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, (GLintptr)offs[i],
                             (GLsizeiptr)((size_t)d.pointCount * a->buffer->Comps() * sizeof(float)));
         s.snap.names.push_back(a->name);
         s.snap.comps.push_back(a->buffer->Comps());
         s.snap.data.push_back((const float*)((const char*)s.ptr + offs[i]));
      }
      glBindBuffer(GL_COPY_READ_BUFFER, 0);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
      s.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      s.done = false;
      glFlush(); // so the fence gets signalled without anyone waiting on it
      mNext ^= 1;
   }

   const AsyncRows::Snapshot* AsyncRows::Latest()
   {
      for (int i = 0; i < 2; i++)
      {
         Slot& s = mSlots[i];
         if (s.fence == nullptr)
            continue;
         const GLenum r = glClientWaitSync((GLsync)s.fence, 0, 0);
         if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED)
         {
            glDeleteSync((GLsync)s.fence);
            s.fence = nullptr;
            s.done = true;
            // Newer of the two finished copies wins.
            if (mReady < 0 || !mSlots[mReady].done || s.snap.revision >= mSlots[mReady].snap.revision || mReady == i)
               mReady = i;
         }
      }
      if (mReady >= 0 && mSlots[mReady].done && mSlots[mReady].fence == nullptr)
         return &mSlots[mReady].snap;
      return nullptr;
   }

   namespace
   {
      struct BoundsGl
      {
         GLuint prog = 0, fbo = 0, tex = 0, vao = 0;
         GLint signLoc = -1;
         bool failed = false;
      };

      BoundsGl* BoundsResources()
      {
         static BoundsGl g;
         if (g.failed)
            return nullptr;
         if (g.prog != 0)
            return &g;
         const char* vsSrc =
            "#version 150\nin vec3 aP;\nuniform float uSign;\nout vec3 vP;\n"
            "void main() { vP = aP * uSign; gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
         const char* fsSrc =
            "#version 150\nin vec3 vP;\nout vec4 frag;\nvoid main() { frag = vec4(vP, 1.0); }\n";
         GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
         glShaderSource(vs, 1, &vsSrc, nullptr);
         glShaderSource(fs, 1, &fsSrc, nullptr);
         glCompileShader(vs);
         glCompileShader(fs);
         g.prog = glCreateProgram();
         glAttachShader(g.prog, vs);
         glAttachShader(g.prog, fs);
         glBindAttribLocation(g.prog, 0, "aP");
         glBindFragDataLocation(g.prog, 0, "frag");
         glLinkProgram(g.prog);
         glDeleteShader(vs);
         glDeleteShader(fs);
         GLint ok = 0;
         glGetProgramiv(g.prog, GL_LINK_STATUS, &ok);
         if (!ok)
         {
            g.failed = true;
            return nullptr;
         }
         g.signLoc = glGetUniformLocation(g.prog, "uSign");
         glGenTextures(1, &g.tex);
         GLint prevTex = 0;
         glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
         glBindTexture(GL_TEXTURE_2D, g.tex);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
         GLint prevFbo = 0;
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
         glGenFramebuffers(1, &g.fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.tex, 0);
         const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
         glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
         if (!complete)
         {
            g.failed = true;
            return nullptr;
         }
         glGenVertexArrays(1, &g.vao);
         return &g;
      }

      // Turbo 0.62: the bounds as a compute-shader reduction (GL 4.3, Windows).
      // The MAX-blend version below draws every point into one pixel, so the
      // blending of a 300k-point cloud is serialised on that pixel - several
      // ms of GPU per call, per node, per frame. Here 256-thread groups reduce
      // in shared memory and only one thread per group touches the result,
      // through atomics on order-preserving uint encodings of the floats.
      // Result layout as the blend path: 4 x max, then 4 x -min (encoded;
      // DecodeBounds turns them back into floats after the read).
      struct BoundsComputeGl
      {
         GLuint prog = 0, result = 0;
         GLint countLoc = -1, compsLoc = -1;
         bool failed = false;
      };

      BoundsComputeGl* BoundsComputeResources()
      {
         static BoundsComputeGl c;
         if (c.failed)
            return nullptr;
         if (c.prog != 0)
            return &c;
         const char* src =
            "#version 430\n"
            "layout(local_size_x = 256) in;\n"
            "layout(std430, binding = 0) readonly buffer Pts { float p[]; };\n"
            "layout(std430, binding = 1) buffer Res { uint r[8]; };\n"
            "uniform int uCount;\n"
            "uniform int uComps;\n"
            "shared vec3 sMax[256];\n"
            "shared vec3 sMin[256];\n"
            "uint enc(float f) { uint u = floatBitsToUint(f); return (u & 0x80000000u) != 0u ? ~u : (u | 0x80000000u); }\n"
            "void main() {\n"
            "   vec3 mx = vec3(-1e30), mn = vec3(1e30);\n"
            "   uint stride = gl_NumWorkGroups.x * 256u;\n"
            "   for (uint i = gl_GlobalInvocationID.x; i < uint(uCount); i += stride) {\n"
            "      uint b = i * uint(uComps);\n"
            "      vec3 v = vec3(p[b], uComps > 1 ? p[b + 1u] : 0.0, uComps > 2 ? p[b + 2u] : 0.0);\n"
            "      mx = max(mx, v); mn = min(mn, v);\n"
            "   }\n"
            "   uint l = gl_LocalInvocationID.x;\n"
            "   sMax[l] = mx; sMin[l] = mn;\n"
            "   barrier();\n"
            "   for (uint s = 128u; s > 0u; s >>= 1u) {\n"
            "      if (l < s) { sMax[l] = max(sMax[l], sMax[l + s]); sMin[l] = min(sMin[l], sMin[l + s]); }\n"
            "      barrier();\n"
            "   }\n"
            "   if (l == 0u) {\n"
            "      for (int k = 0; k < 3; k++) {\n"
            "         atomicMax(r[k], enc(sMax[0][k]));\n"
            "         atomicMax(r[4 + k], enc(-sMin[0][k]));\n"
            "      }\n"
            "   }\n"
            "}\n";
         GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
         glShaderSource(cs, 1, &src, nullptr);
         glCompileShader(cs);
         GLint ok = 0;
         glGetShaderiv(cs, GL_COMPILE_STATUS, &ok);
         if (!ok)
         {
            glDeleteShader(cs);
            c.failed = true;
            return nullptr;
         }
         c.prog = glCreateProgram();
         glAttachShader(c.prog, cs);
         glLinkProgram(c.prog);
         glDeleteShader(cs);
         glGetProgramiv(c.prog, GL_LINK_STATUS, &ok);
         if (!ok)
         {
            glDeleteProgram(c.prog);
            c.prog = 0;
            c.failed = true;
            return nullptr;
         }
         c.countLoc = glGetUniformLocation(c.prog, "uCount");
         c.compsLoc = glGetUniformLocation(c.prog, "uComps");
         glGenBuffers(1, &c.result);
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, c.result);
         glBufferData(GL_SHADER_STORAGE_BUFFER, 8 * sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
         return &c;
      }

      // Encoded uints (as read back) -> floats, in place.
      void DecodeBounds(float r[2][4])
      {
         for (int i = 0; i < 2; i++)
            for (int k = 0; k < 4; k++)
            {
               uint32_t u;
               std::memcpy(&u, &r[i][k], 4);
               u = (u & 0x80000000u) != 0u ? (u & 0x7fffffffu) : ~u;
               std::memcpy(&r[i][k], &u, 4);
            }
      }

      // Runs the reduction; the result goes to `pbo` (encoded, read later)
      // or, with pbo == 0, is read now and decoded into dst. False when
      // compute shaders are unavailable (the blend path then runs).
      bool RunBoundsCompute(unsigned int buffer, int count, int comps, float dst[2][4], GLuint pbo)
      {
         BoundsComputeGl* c = BoundsComputeResources();
         if (c == nullptr)
            return false;
         GLint prevProg = 0;
         glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
         const GLuint zero = 0;
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, c->result);
         glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, c->result);
         glUseProgram(c->prog);
         glUniform1i(c->countLoc, count);
         glUniform1i(c->compsLoc, std::max(1, comps));
         const GLuint groups = (GLuint)std::min(256, std::max(1, (count + 255) / 256));
         glDispatchCompute(groups, 1, 1);
         glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
         glUseProgram((GLuint)prevProg);
         if (pbo != 0)
         {
            glBindBuffer(GL_COPY_READ_BUFFER, c->result);
            glBindBuffer(GL_COPY_WRITE_BUFFER, pbo);
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 8 * sizeof(GLuint));
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
         }
         else
         {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, c->result);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 8 * sizeof(GLuint), dst);
            DecodeBounds(dst);
         }
         glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
         return true;
      }

      // Max pass then (negated) min pass, each read into `dst` (client memory)
      // or, when pbo != 0, into that pixel-pack buffer at offsets 0 and 16
      // without waiting for the GPU.
      void BoundsPasses(BoundsGl& g, unsigned int buffer, int count, int comps, float dst[2][4], GLuint pbo)
      {
         if (RunBoundsCompute(buffer, count, comps, dst, pbo))
            return;
         GLint prevFbo = 0, prevProg = 0, prevVao = 0, prevArray = 0, prevPack = 0, viewport[4] = { 0, 0, 0, 0 };
         GLint blendEqRgb = 0, blendEqA = 0;
         GLfloat clear[4] = { 0, 0, 0, 0 };
         glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
         glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
         glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
         glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
         glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPack);
         glGetIntegerv(GL_VIEWPORT, viewport);
         glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRgb);
         glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqA);
         glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
         const GLboolean blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST),
                         scissor = glIsEnabled(GL_SCISSOR_TEST), cull = glIsEnabled(GL_CULL_FACE);

         glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
         glViewport(0, 0, 1, 1);
         glDisable(GL_DEPTH_TEST);
         glDisable(GL_SCISSOR_TEST);
         glDisable(GL_CULL_FACE);
         glEnable(GL_BLEND);
         glBlendEquation(GL_MAX);
         glUseProgram(g.prog);
         glBindVertexArray(g.vao);
         glBindBuffer(GL_ARRAY_BUFFER, buffer);
         glEnableVertexAttribArray(0);
         glVertexAttribPointer(0, std::min(3, comps), GL_FLOAT, GL_FALSE, comps * (GLsizei)sizeof(float), nullptr);
         glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
         for (int pass = 0; pass < 2; pass++)
         {
            glClearColor(-1e30f, -1e30f, -1e30f, -1e30f);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1f(g.signLoc, pass == 0 ? 1.0f : -1.0f);
            glDrawArrays(GL_POINTS, 0, count);
            if (pbo != 0)
               glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, (void*)(uintptr_t)(pass * 4 * sizeof(float)));
            else
               glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, dst[pass]);
         }
         glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPack);
         glDisableVertexAttribArray(0);

         glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
         glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
         glBlendEquationSeparate((GLenum)blendEqRgb, (GLenum)blendEqA);
         if (!blend) glDisable(GL_BLEND);
         if (depth) glEnable(GL_DEPTH_TEST);
         if (scissor) glEnable(GL_SCISSOR_TEST);
         if (cull) glEnable(GL_CULL_FACE);
         glClearColor(clear[0], clear[1], clear[2], clear[3]);
         glUseProgram((GLuint)prevProg);
         glBindVertexArray((GLuint)prevVao);
         glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevArray);
      }

      bool BoundsFromPixels(const float r[2][4], int comps, float lo[3], float hi[3])
      {
         for (int k = 0; k < 3; k++)
         {
            hi[k] = r[0][k];
            lo[k] = -r[1][k];
         }
         for (int k = comps; k < 3; k++)
            lo[k] = hi[k] = 0.0f;
         return lo[0] <= hi[0] && hi[0] > -1e29f;
      }
   }

   bool GpuBounds(unsigned int buffer, int count, int comps, float lo[3], float hi[3])
   {
      if (buffer == 0 || count <= 0)
         return false;
      BoundsGl* g = BoundsResources();
      if (g == nullptr)
         return false;
      float result[2][4];
      BoundsPasses(*g, buffer, count, comps, result, 0);
      return BoundsFromPixels(result, comps, lo, hi);
   }

   AsyncBounds::~AsyncBounds()
   {
      if (fence != nullptr)
         glDeleteSync((GLsync)fence);
      if (pbo != 0)
         glDeleteBuffers(1, &pbo);
   }

   bool GpuBoundsAsync(AsyncBounds& s, unsigned int buffer, int count, int comps, float lo[3], float hi[3])
   {
      // Collect a finished reduction without waiting.
      if (s.fence != nullptr)
      {
         const GLenum st = glClientWaitSync((GLsync)s.fence, 0, 0);
         if (st == GL_ALREADY_SIGNALED || st == GL_CONDITION_SATISFIED)
         {
            float r[2][4];
            GLint prev = 0;
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prev);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
            glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, sizeof(r), r);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prev);
            if (BoundsComputeResources() != nullptr)
               DecodeBounds(r); // the compute path stores encoded uints
            s.have = BoundsFromPixels(r, s.comps, s.lo, s.hi);
            glDeleteSync((GLsync)s.fence);
            s.fence = nullptr;
         }
         else if (st == GL_WAIT_FAILED)
         {
            glDeleteSync((GLsync)s.fence);
            s.fence = nullptr;
         }
      }
      if (buffer == 0 || count <= 0)
         return false;
      BoundsGl* g = BoundsResources();
      if (g == nullptr)
         return false;
      if (!s.have && s.fence == nullptr)
      {
         // First use: nothing to show yet, so pay for one synchronous read.
         float result[2][4];
         BoundsPasses(*g, buffer, count, comps, result, 0);
         s.comps = comps;
         s.have = BoundsFromPixels(result, comps, s.lo, s.hi);
      }
      else if (s.fence == nullptr)
      {
         // Start the next reduction; its result is picked up a frame or so
         // later, so bounds trail an animated cloud by about a frame.
         if (s.pbo == 0)
         {
            GLint prev = 0;
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prev);
            glGenBuffers(1, &s.pbo);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER, 8 * sizeof(float), nullptr, GL_STREAM_READ);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prev);
         }
         float unused[2][4];
         BoundsPasses(*g, buffer, count, comps, unused, s.pbo);
         s.comps = comps;
         s.fence = (void*)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      }
      for (int k = 0; k < 3; k++)
      {
         lo[k] = s.lo[k];
         hi[k] = s.hi[k];
      }
      return s.have;
   }

   BufferPtr GpuShiftIn(const BufferPtr& newest, const BufferPtr& history, int blockFloats, int blocks, int comps)
   {
      blocks = std::max(1, blocks);
      const size_t block = (size_t)std::max(0, blockFloats) * sizeof(float);
      const size_t total = block * (size_t)blocks;
      const GLuint dst = AcquireGpuBuffer(total);
      if (block == 0 || !newest)
         return std::make_shared<PopBuffer>(dst, blockFloats * blocks / std::max(1, comps), comps);
      GLint prevRead = 0, prevWrite = 0;
      glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &prevRead);
      glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &prevWrite);
      glBindBuffer(GL_COPY_WRITE_BUFFER, dst);
      glBindBuffer(GL_COPY_READ_BUFFER, newest->Gpu());
      glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, (GLsizeiptr)block);
      const bool haveHistory = history && (size_t)history->Count() * (size_t)history->Comps() * sizeof(float) >= total;
      if (blocks > 1)
      {
         if (haveHistory)
         {
            glBindBuffer(GL_COPY_READ_BUFFER, history->Gpu());
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, (GLintptr)block, (GLsizeiptr)(total - block));
         }
         else
         {
            // Fill by doubling from the first block (log2(blocks) copies).
            glBindBuffer(GL_COPY_READ_BUFFER, dst);
            size_t filled = block;
            while (filled < total)
            {
               const size_t n = std::min(filled, total - filled);
               glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, (GLintptr)filled, (GLsizeiptr)n);
               filled += n;
            }
         }
      }
      glBindBuffer(GL_COPY_READ_BUFFER, (GLuint)prevRead);
      glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)prevWrite);
      return std::make_shared<PopBuffer>(dst, blockFloats * blocks / std::max(1, comps), comps);
   }

   bool InstanceMatrices(const PopData& points, const float model[16], float scale, int alignMode,
                         std::vector<BufferPtr>& cols, std::string& error)
   {
      KernelSpec spec;
      spec.reads = { { attr::P, 3 }, { attr::N, 3 }, { attr::PScale, 1 }, { attr::Rotate, 3 }, { attr::Active, 1 },
                     { attr::Scale, 3 } };
      spec.writes = { { "M0", 4 }, { "M1", 4 }, { "M2", 4 }, { "M3", 4 } };
      spec.body = R"GLSL(
   mat3 R = eulerDeg(Rotate + uRotOffset);
   if (uAlign != 0)
   {
      vec3 n = length(N) > 1e-6 ? normalize(N) : vec3(0.0, 0.0, 1.0);
      R = alignVec(uAlign == 1 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), n) * R;
   }
   vec3 s = vec3(PScale * uScale * (Active != 0.0 ? 1.0 : 0.0)) * Scale * uScale3;
   mat4 m = mat4(vec4(R[0] * s.x, 0.0), vec4(R[1] * s.y, 0.0), vec4(R[2] * s.z, 0.0), vec4(P + uOffset, 1.0));
   m = uModel * m;
   M0 = m[0]; M1 = m[1]; M2 = m[2]; M3 = m[3];
)GLSL";
      const PopData::InstanceLook& look = points.instanceLook;
      std::vector<KernelUniform> u(6);
      u[0].name = "uModel"; u[0].type = KernelUniform::kMat4;
      std::memcpy(u[0].v, model, sizeof(float) * 16);
      u[1].name = "uScale"; u[1].type = KernelUniform::kFloat; u[1].v[0] = scale;
      u[2].name = "uAlign"; u[2].type = KernelUniform::kInt; u[2].i = alignMode;
      u[3].name = "uScale3"; u[3].type = KernelUniform::kVec3; std::memcpy(u[3].v, look.scaleXYZ, sizeof(float) * 3);
      u[4].name = "uRotOffset"; u[4].type = KernelUniform::kVec3; std::memcpy(u[4].v, look.rotate, sizeof(float) * 3);
      u[5].name = "uOffset"; u[5].type = KernelUniform::kVec3; std::memcpy(u[5].v, look.offset, sizeof(float) * 3);
      KernelEnv env;
      return RunKernel(spec, points, u, env, cols, error);
   }
}

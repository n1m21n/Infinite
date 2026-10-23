#include "GpuAlgo.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/OpenGLHeaders.h"
#include "PopData.h"

namespace pop
{
   namespace gpu
   {
      namespace
      {
         // Work-efficient (Blelloch) scan of one 512-element block per group
         // in shared memory; the block total goes to blockSums when asked.
         const char* kScanBlock = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer In { uint inData[]; };
layout(std430, binding = 1) writeonly buffer Out { uint outData[]; };
layout(std430, binding = 2) writeonly buffer Sums { uint blockSums[]; };
uniform int uCount;
uniform int uWriteSums;
shared uint s[512];
void main()
{
   uint t = gl_LocalInvocationID.x;
   uint base = gl_WorkGroupID.x * 512u;
   uint a = base + t, b = base + t + 256u;
   uint n = uint(uCount);
   s[t] = a < n ? inData[a] : 0u;
   s[t + 256u] = b < n ? inData[b] : 0u;
   uint offset = 1u;
   for (uint d = 256u; d > 0u; d >>= 1u)
   {
      memoryBarrierShared();
      barrier();
      if (t < d)
      {
         uint ai = offset * (2u * t + 1u) - 1u;
         uint bi = offset * (2u * t + 2u) - 1u;
         s[bi] += s[ai];
      }
      offset <<= 1u;
   }
   memoryBarrierShared();
   barrier();
   if (t == 0u)
   {
      if (uWriteSums != 0)
         blockSums[gl_WorkGroupID.x] = s[511];
      s[511] = 0u;
   }
   for (uint d = 1u; d < 512u; d <<= 1u)
   {
      offset >>= 1u;
      memoryBarrierShared();
      barrier();
      if (t < d)
      {
         uint ai = offset * (2u * t + 1u) - 1u;
         uint bi = offset * (2u * t + 2u) - 1u;
         uint tmp = s[ai];
         s[ai] = s[bi];
         s[bi] += tmp;
      }
   }
   memoryBarrierShared();
   barrier();
   if (a < n) outData[a] = s[t];
   if (b < n) outData[b] = s[t + 256u];
}
)GLSL";

         const char* kAddOffsets = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) buffer Out { uint outData[]; };
layout(std430, binding = 1) readonly buffer Sums { uint scannedSums[]; };
uniform int uCount;
void main()
{
   uint i = gl_GlobalInvocationID.x;
   if (i >= uint(uCount)) return;
   outData[i] += scannedSums[i / 512u];
}
)GLSL";

         const char* kTotal = R"GLSL(#version 430
layout(local_size_x = 1) in;
layout(std430, binding = 0) readonly buffer In { uint inData[]; };
layout(std430, binding = 1) readonly buffer Out { uint outData[]; };
layout(std430, binding = 2) writeonly buffer Total { uint total[]; };
uniform int uCount;
void main()
{
   uint last = uint(uCount) - 1u;
   total[0] = outData[last] + inData[last];
}
)GLSL";

         const char* kScatter = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Flags { uint flags[]; };
layout(std430, binding = 1) readonly buffer Scan { uint scanned[]; };
layout(std430, binding = 2) writeonly buffer Idx { uint indices[]; };
uniform int uCount;
void main()
{
   uint i = gl_GlobalInvocationID.x;
   if (i >= uint(uCount)) return;
   if (flags[i] != 0u)
      indices[scanned[i]] = i;
}
)GLSL";

         const char* kDispatchArgs = R"GLSL(#version 430
layout(local_size_x = 1) in;
layout(std430, binding = 0) readonly buffer Count { uint countBuf[]; };
layout(std430, binding = 1) writeonly buffer Args { uint args[]; };
uniform int uMax;
void main()
{
   uint n = min(countBuf[0], uint(uMax));
   args[0] = (n + 255u) / 256u;
   args[1] = 1u;
   args[2] = 1u;
}
)GLSL";

         const char* kGather = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) readonly buffer Idx { uint indices[]; };
layout(std430, binding = 2) readonly buffer Count { uint countBuf[]; };
layout(std430, binding = 3) writeonly buffer Dst { float dst[]; };
uniform int uComps;
uniform int uMax;
void main()
{
   uint k = gl_GlobalInvocationID.x;
   if (k >= min(countBuf[0], uint(uMax))) return;
   uint c = uint(uComps);
   uint from = indices[k] * c;
   for (uint j = 0u; j < c; j++)
      dst[k * c + j] = src[from + j];
}
)GLSL";

         const char* kFloatToFlags = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Flags { uint flags[]; };
uniform int uCount;
void main()
{
   uint i = gl_GlobalInvocationID.x;
   if (i >= uint(uCount)) return;
   flags[i] = src[i] > 0.5 ? 1u : 0u;
}
)GLSL";

         // `precise` keeps the driver from fusing the multiply-add, so the sum is
         // the CPU's, operation for operation.
         const char* kWeighted = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) readonly buffer Off { uint offs[]; };
layout(std430, binding = 2) readonly buffer Idx { uint idx[]; };
layout(std430, binding = 3) readonly buffer Wt { float wt[]; };
layout(std430, binding = 4) writeonly buffer Dst { float dst[]; };
uniform int uRows;
uniform int uComps;
uniform int uNormalize;
void main()
{
   uint r = gl_GlobalInvocationID.x;
   if (r >= uint(uRows)) return;
   uint c = uint(uComps);
   uint b = offs[r], e = offs[r + 1u];
   precise float acc[4] = float[4](0.0, 0.0, 0.0, 0.0);
   for (uint k = b; k < e; k++)
   {
      uint s = idx[k] * c;
      float w = wt[k];
      for (uint j = 0u; j < c; j++)
      {
         precise float term = src[s + j] * w;
         acc[j] = acc[j] + term;
      }
   }
   if (uNormalize != 0 && c == 3u && e - b > 1u)
   {
      precise float l2 = acc[0] * acc[0];
      l2 = l2 + acc[1] * acc[1];
      l2 = l2 + acc[2] * acc[2];
      float l = sqrt(l2);
      if (l > 1e-12)
      {
         acc[0] = acc[0] / l;
         acc[1] = acc[1] / l;
         acc[2] = acc[2] / l;
      }
   }
   for (uint j = 0u; j < c; j++)
      dst[r * c + j] = acc[j];
}
)GLSL";

         // Turbo 0.70 (P6): each line's running length, one thread per line in
         // the CPU loop's order (Line Resample).
         const char* kLineLengths = R"GLSL(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Src { float p[]; };
layout(std430, binding = 1) readonly buffer Pts { uint pts[]; };
layout(std430, binding = 2) readonly buffer Start { uint start[]; };
layout(std430, binding = 3) readonly buffer Len { uint len[]; };
layout(std430, binding = 4) writeonly buffer Cum { float cum[]; };
uniform int uStrips;
uniform int uComps;
void main()
{
   uint s = gl_GlobalInvocationID.x;
   if (s >= uint(uStrips)) return;
   uint b = start[s], n = len[s], c = uint(uComps);
   precise float acc = 0.0;
   cum[b] = 0.0;
   for (uint k = 1u; k < n; k++)
   {
      uint i0 = pts[b + k - 1u] * c, i1 = pts[b + k] * c;
      precise float dx = p[i0] - p[i1];
      precise float dy = p[i0 + 1u] - p[i1 + 1u];
      precise float dz = p[i0 + 2u] - p[i1 + 2u];
      precise float d = sqrt(dx * dx + dy * dy + dz * dz);
      acc = acc + d;
      cum[b + k] = acc;
   }
}
)GLSL";

         // Turbo 0.70 (P6): each output row's segment (the first whose end
         // reaches the row's distance, as the CPU's forward scan) and lerp factor.
         const char* kResampleRows = R"GLSL(#version 430
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Cum { float cum[]; };
layout(std430, binding = 1) readonly buffer Pts { uint pts[]; };
layout(std430, binding = 2) readonly buffer Start { uint start[]; };
layout(std430, binding = 3) readonly buffer Len { uint len[]; };
layout(std430, binding = 4) readonly buffer Den { float den[]; };
layout(std430, binding = 5) readonly buffer RowStrip { uint rowStrip[]; };
layout(std430, binding = 6) readonly buffer RowJ { uint rowJ[]; };
layout(std430, binding = 7) writeonly buffer Idx { uint idx[]; };
layout(std430, binding = 8) writeonly buffer Wt { float wt[]; };
uniform int uRows;
void main()
{
   uint r = gl_GlobalInvocationID.x;
   if (r >= uint(uRows)) return;
   uint s = rowStrip[r], b = start[s], n = len[s];
   float L = cum[b + n - 1u];
   precise float target = L * float(rowJ[r]) / den[s];
   uint lo = 0u, hi = n - 2u;
   while (lo < hi)
   {
      uint mid = (lo + hi) / 2u;
      if (cum[b + mid + 1u] < target) lo = mid + 1u;
      else hi = mid;
   }
   precise float span = cum[b + lo + 1u] - cum[b + lo];
   precise float t = span > 1e-12 ? (target - cum[b + lo]) / span : 0.0;
   t = clamp(t, 0.0, 1.0);
   idx[r * 2u] = pts[b + lo];
   idx[r * 2u + 1u] = pts[b + lo + 1u];
   wt[r * 2u] = 1.0 - t;
   wt[r * 2u + 1u] = t;
}
)GLSL";

         struct Programs
         {
            bool tried = false, ok = false;
            GLuint scanBlock = 0, addOffsets = 0, total = 0, scatter = 0, dispatchArgs = 0, gather = 0;
            GLuint floatToFlags = 0, weighted = 0, lineLengths = 0, resampleRows = 0;
            GLuint argsBuffer = 0;
         };

         GLuint Build(const char* src, bool& ok)
         {
            GLuint cs = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(cs, 1, &src, nullptr);
            glCompileShader(cs);
            GLint status = 0;
            glGetShaderiv(cs, GL_COMPILE_STATUS, &status);
            if (!status)
            {
               char log[2048] = { 0 };
               glGetShaderInfoLog(cs, sizeof(log) - 1, nullptr, log);
               fprintf(stderr, "PointZ GpuAlgo: compile failed: %s\n", log);
               glDeleteShader(cs);
               ok = false;
               return 0;
            }
            GLuint prog = glCreateProgram();
            glAttachShader(prog, cs);
            glLinkProgram(prog);
            glDeleteShader(cs);
            glGetProgramiv(prog, GL_LINK_STATUS, &status);
            if (!status)
            {
               fprintf(stderr, "PointZ GpuAlgo: link failed\n");
               glDeleteProgram(prog);
               ok = false;
               return 0;
            }
            return prog;
         }

         Programs* Get()
         {
            static Programs p;
            if (!p.tried)
            {
               p.tried = true;
               bool ok = true;
               p.scanBlock = Build(kScanBlock, ok);
               p.addOffsets = Build(kAddOffsets, ok);
               p.total = Build(kTotal, ok);
               p.scatter = Build(kScatter, ok);
               p.dispatchArgs = Build(kDispatchArgs, ok);
               p.gather = Build(kGather, ok);
               p.floatToFlags = Build(kFloatToFlags, ok);
               p.weighted = Build(kWeighted, ok);
               p.lineLengths = Build(kLineLengths, ok);
               p.resampleRows = Build(kResampleRows, ok);
               if (ok)
               {
                  glGenBuffers(1, &p.argsBuffer);
                  glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, p.argsBuffer);
                  glBufferData(GL_DISPATCH_INDIRECT_BUFFER, 4 * sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
                  glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
               }
               p.ok = ok;
            }
            return p.ok ? &p : nullptr;
         }

         // Restores the caller's program and clears the SSBO bindings used.
         struct Scope
         {
            GLint prevProg = 0;
            Scope() { glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg); }
            ~Scope()
            {
               for (GLuint b = 0; b < 9; b++)
                  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, b, 0);
               glUseProgram((GLuint)prevProg);
            }
         };

         GLuint Groups(int count) { return (GLuint)((std::max(count, 1) + 255) / 256); }

         void ScanRec(Programs& p, GLuint in, GLuint out, int count)
         {
            const int blocks = (count + 511) / 512;
            GLuint sums = 0, sumsScanned = 0;
            if (blocks > 1)
            {
               sums = AcquireGpuBuffer((size_t)blocks * sizeof(GLuint));
               sumsScanned = AcquireGpuBuffer((size_t)blocks * sizeof(GLuint));
            }
            glUseProgram(p.scanBlock);
            glUniform1i(glGetUniformLocation(p.scanBlock, "uCount"), count);
            glUniform1i(glGetUniformLocation(p.scanBlock, "uWriteSums"), blocks > 1 ? 1 : 0);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, in);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, blocks > 1 ? sums : out);
            glDispatchCompute((GLuint)blocks, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            if (blocks > 1)
            {
               ScanRec(p, sums, sumsScanned, blocks);
               glUseProgram(p.addOffsets);
               glUniform1i(glGetUniformLocation(p.addOffsets, "uCount"), count);
               glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, out);
               glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sumsScanned);
               glDispatchCompute(Groups(count), 1, 1);
               glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
               // Back to the pool: reused two frames from now, after this work ran.
               ReleaseGpuBuffer(sums, (size_t)blocks * sizeof(GLuint));
               ReleaseGpuBuffer(sumsScanned, (size_t)blocks * sizeof(GLuint));
            }
         }
      }

      static bool sForceCpu = false;
      bool Available() { return !sForceCpu && Get() != nullptr; }
      void ForceCpuForTest(bool on) { sForceCpu = on; }

      bool FloatToFlags(unsigned int src, int count, unsigned int flags)
      {
         Programs* p = Get();
         if (p == nullptr || count <= 0)
            return false;
         Scope scope;
         glUseProgram(p->floatToFlags);
         glUniform1i(glGetUniformLocation(p->floatToFlags, "uCount"), count);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flags);
         glDispatchCompute(Groups(count), 1, 1);
         glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
         return true;
      }

      bool ExclusiveScanU32(unsigned int in, unsigned int out, int count, unsigned int total)
      {
         Programs* p = Get();
         if (p == nullptr || count <= 0 || in == out)
            return false;
         if ((count + 511) / 512 > 65535) // one dispatch per level (GL guarantees 65535 groups)
            return false;
         Scope scope;
         ScanRec(*p, in, out, count);
         if (total != 0)
         {
            glUseProgram(p->total);
            glUniform1i(glGetUniformLocation(p->total, "uCount"), count);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, in);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, total);
            glDispatchCompute(1, 1, 1);
         }
         glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
         return true;
      }

      bool CompactIndices(unsigned int flags, int count, unsigned int indices, unsigned int countBuf)
      {
         Programs* p = Get();
         if (p == nullptr || count <= 0)
            return false;
         const GLuint scanned = AcquireGpuBuffer((size_t)count * sizeof(GLuint));
         if (!ExclusiveScanU32(flags, scanned, count, countBuf))
         {
            ReleaseGpuBuffer(scanned, (size_t)count * sizeof(GLuint));
            return false;
         }
         Scope scope;
         glUseProgram(p->scatter);
         glUniform1i(glGetUniformLocation(p->scatter, "uCount"), count);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, flags);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, scanned);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, indices);
         glDispatchCompute(Groups(count), 1, 1);
         glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
         ReleaseGpuBuffer(scanned, (size_t)count * sizeof(GLuint));
         return true;
      }

      bool GatherRows(unsigned int src, int comps, unsigned int indices, unsigned int countBuf, int maxCount,
                      unsigned int dst)
      {
         Programs* p = Get();
         if (p == nullptr || comps <= 0 || maxCount <= 0)
            return false;
         Scope scope;
         // Dispatch size from the count on the GPU: no readback.
         glUseProgram(p->dispatchArgs);
         glUniform1i(glGetUniformLocation(p->dispatchArgs, "uMax"), maxCount);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, countBuf);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, p->argsBuffer);
         glDispatchCompute(1, 1, 1);
         glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

         glUseProgram(p->gather);
         glUniform1i(glGetUniformLocation(p->gather, "uComps"), comps);
         glUniform1i(glGetUniformLocation(p->gather, "uMax"), maxCount);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, indices);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, countBuf);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, dst);
         GLint prevIndirect = 0;
         glGetIntegerv(GL_DISPATCH_INDIRECT_BUFFER_BINDING, &prevIndirect);
         glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, p->argsBuffer);
         glDispatchComputeIndirect(0);
         glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, (GLuint)prevIndirect);
         glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                         GL_BUFFER_UPDATE_BARRIER_BIT);
         return true;
      }

      bool WeightedRows(unsigned int src, int comps, unsigned int offsets, unsigned int indices, unsigned int weights,
                        int rows, unsigned int dst, bool normalizeN)
      {
         Programs* p = Get();
         if (p == nullptr || rows <= 0 || comps <= 0 || comps > 4)
            return false;
         Scope scope;
         glUseProgram(p->weighted);
         glUniform1i(glGetUniformLocation(p->weighted, "uRows"), rows);
         glUniform1i(glGetUniformLocation(p->weighted, "uComps"), comps);
         glUniform1i(glGetUniformLocation(p->weighted, "uNormalize"), normalizeN ? 1 : 0);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, offsets);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, indices);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, weights);
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, dst);
         glDispatchCompute(Groups(rows), 1, 1);
         glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                         GL_BUFFER_UPDATE_BARRIER_BIT);
         return true;
      }

      uint32_t RowStencil::Copy(uint32_t i)
      {
         idx.push_back(i);
         w.push_back(1.0f);
         offs.push_back((uint32_t)idx.size());
         return (uint32_t)Rows() - 1;
      }

      uint32_t RowStencil::Lerp(uint32_t i, uint32_t j, float t)
      {
         const int rows[2] = { (int)i, (int)j };
         const float weights[2] = { 1.0f - t, t };
         return Weighted(rows, weights, 2);
      }

      uint32_t RowStencil::Weighted(const int* rows, const float* weights, int n)
      {
         for (int k = 0; k < n; k++)
         {
            idx.push_back((uint32_t)rows[k]);
            w.push_back(weights[k]);
         }
         offs.push_back((uint32_t)idx.size());
         return (uint32_t)Rows() - 1;
      }

      GpuStencil Upload(const RowStencil& s)
      {
         auto bits = [](const std::vector<uint32_t>& u) {
            std::vector<float> f(u.size());
            std::memcpy(f.data(), u.data(), u.size() * sizeof(uint32_t));
            return f;
         };
         GpuStencil g;
         g.rows = s.Rows();
         g.offs = MakeBuffer(bits(s.offs), 1);
         g.idx = MakeBuffer(s.idx.empty() ? std::vector<float>(1, 0.0f) : bits(s.idx), 1);
         g.w = MakeBuffer(s.w.empty() ? std::vector<float>(1, 0.0f) : std::vector<float>(s.w), 1);
         return g;
      }

      std::shared_ptr<PopData> ApplyStencil(const PopData& in, const GpuStencil& s)
      {
         auto out = std::make_shared<PopData>();
         out->pointCount = s.rows;
         if (s.rows <= 0)
            return out;
         for (const PopAttrib& at : in.attribs)
         {
            if (at.buffer->Count() < in.pointCount)
               continue;
            const int comps = at.buffer->Comps();
            const GLuint dst = AcquireGpuBuffer((size_t)s.rows * (size_t)comps * sizeof(float));
            const bool ok = WeightedRows(at.buffer->Gpu(), comps, s.offs->Gpu(), s.idx->Gpu(), s.w->Gpu(), s.rows, dst,
                                         at.name == attr::N);
            out->Set(at.name, std::make_shared<PopBuffer>(dst, s.rows, comps));
            if (!ok)
               return nullptr;
         }
         return out;
      }

      BufferPtr UintBuffer(const std::vector<uint32_t>& values)
      {
         // Uint buffers go up bit for bit through float buffers (as Upload).
         std::vector<float> f(std::max<size_t>(values.size(), 1), 0.0f);
         if (!values.empty())
            std::memcpy(f.data(), values.data(), values.size() * sizeof(uint32_t));
         return MakeBuffer(std::move(f), 1);
      }

      bool ResampleStencil(const PopData& in, const ResampleLayout& l, GpuStencil& out)
      {
         Programs* p = Get();
         const PopAttrib* pa = in.Find(attr::P);
         if (p == nullptr || pa == nullptr || pa->buffer->Comps() < 3 || l.strips <= 0 || l.rows <= 0)
            return false;
         const size_t cumBytes = (size_t)l.ptsCount * sizeof(float);
         const GLuint cum = AcquireGpuBuffer(cumBytes);
         const GLuint idx = AcquireGpuBuffer((size_t)l.rows * 2 * sizeof(GLuint));
         const GLuint wt = AcquireGpuBuffer((size_t)l.rows * 2 * sizeof(float));
         {
            Scope scope;
            glUseProgram(p->lineLengths);
            glUniform1i(glGetUniformLocation(p->lineLengths, "uStrips"), l.strips);
            glUniform1i(glGetUniformLocation(p->lineLengths, "uComps"), pa->buffer->Comps());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pa->buffer->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, l.pts->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, l.start->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, l.len->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, cum);
            glDispatchCompute((GLuint)((l.strips + 63) / 64), 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glUseProgram(p->resampleRows);
            glUniform1i(glGetUniformLocation(p->resampleRows, "uRows"), l.rows);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cum);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, l.den->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, l.rowStrip->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, l.rowJ->Gpu());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, idx);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, wt);
            glDispatchCompute(Groups(l.rows), 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
         }
         // Back to the pool: reused two frames from now, after this work ran.
         ReleaseGpuBuffer(cum, cumBytes);
         out.rows = l.rows;
         out.offs = l.offs;
         out.idx = std::make_shared<PopBuffer>(idx, l.rows * 2, 1);
         out.w = std::make_shared<PopBuffer>(wt, l.rows * 2, 1);
         return true;
      }

      std::shared_ptr<PopData> ConcatRows(const std::vector<const PopData*>& parts, bool mismatchAsMissing)
      {
         // Names and component counts in CPU Concat order (Load puts P first).
         std::vector<std::pair<std::string, int>> names;
         auto add = [&names](const std::string& name, int comps) {
            for (const auto& nc : names)
               if (nc.first == name)
                  return nc.second == comps;
            names.push_back({ name, comps });
            return true;
         };
         int total = 0;
         for (const PopData* d : parts)
         {
            if (const PopAttrib* p = d->Find(attr::P))
               if (!add(p->name, p->buffer->Comps()) && !mismatchAsMissing)
                  return nullptr;
            for (const PopAttrib& at : d->attribs)
               if (at.name != attr::P && !add(at.name, at.buffer->Comps()) && !mismatchAsMissing)
                  return nullptr;
            total += d->pointCount;
         }
         auto out = std::make_shared<PopData>();
         out->pointCount = total;
         for (const auto& nc : names)
         {
            const int comps = nc.second;
            const GLuint dst = AcquireGpuBuffer((size_t)total * (size_t)comps * sizeof(float));
            size_t row = 0;
            for (const PopData* d : parts)
            {
               const int rows = d->pointCount;
               if (rows > 0)
               {
                  const PopAttrib* at = d->Find(nc.first);
                  BufferPtr src;
                  if (at != nullptr && at->buffer->Comps() == comps && at->buffer->Count() >= rows)
                     src = at->buffer;
                  else
                  {
                     float def[4];
                     DefaultValue(nc.first, def);
                     src = MakeFilled(rows, comps, def);
                  }
                  glBindBuffer(GL_COPY_READ_BUFFER, src->Gpu());
                  glBindBuffer(GL_COPY_WRITE_BUFFER, dst);
                  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0,
                                      (GLintptr)(row * (size_t)comps * sizeof(float)),
                                      (GLsizeiptr)((size_t)rows * (size_t)comps * sizeof(float)));
               }
               row += (size_t)rows;
            }
            out->Set(nc.first, std::make_shared<PopBuffer>(dst, total, comps));
         }
         glBindBuffer(GL_COPY_READ_BUFFER, 0);
         glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
         return out;
      }
   }
}

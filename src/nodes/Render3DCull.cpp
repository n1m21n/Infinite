#include "Render3DCull.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "platform/OpenGLHeaders.h"
#include "GLUtil.h"
#include "Render3DTemporal.h"
#include "pop/GpuAlgo.h"

namespace
{
   // glDrawElementsIndirect's command: count, instanceCount, firstIndex,
   // baseVertex, baseInstance.
   constexpr int kCommandBytes = 5 * sizeof(GLuint);
   constexpr int kQuadIndices = 6;

   void PointStream(unsigned int loc, unsigned int buf, int size, int stride, int offset)
   {
      glBindBuffer(GL_ARRAY_BUFFER, buf);
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(loc, size, GL_FLOAT, GL_FALSE, stride * (GLsizei)sizeof(float),
                            (void*)(size_t)(offset * sizeof(float)));
      glVertexAttribDivisor(loc, 1);
   }
}

const unsigned int Render3DCull::kStreamLoc[kStreams] = { 3, 4, 5, 6, 7, 9 };

Render3DCull::~Render3DCull()
{
   Release();
}

bool Render3DCull::Available()
{
   return pop::gpu::Available();
}

void Render3DCull::FrustumPlanes(const Mat4& vp, float planes[6][4])
{
   // Rows of the column-major matrix; clip space is -w..w on every axis.
   auto row = [&](int r, int k) { return vp.m[k * 4 + r]; };
   for (int axis = 0; axis < 3; axis++)
      for (int side = 0; side < 2; side++)
      {
         float* p = planes[axis * 2 + side];
         const float sign = side == 0 ? 1.0f : -1.0f;
         for (int k = 0; k < 4; k++)
            p[k] = row(3, k) + sign * row(axis, k);
         const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
         if (len > 0.0f)
            for (int k = 0; k < 4; k++)
               p[k] /= len;
      }
}

float Render3DCull::InsideFraction(const float planes[6][4], const float lo[3], const float hi[3])
{
   // 4 x 4 x 4 points at the centres of the box's cells.
   int inside = 0;
   for (int ix = 0; ix < 4; ix++)
      for (int iy = 0; iy < 4; iy++)
         for (int iz = 0; iz < 4; iz++)
         {
            const float x = lo[0] + (hi[0] - lo[0]) * (ix + 0.5f) * 0.25f;
            const float y = lo[1] + (hi[1] - lo[1]) * (iy + 0.5f) * 0.25f;
            const float z = lo[2] + (hi[2] - lo[2]) * (iz + 0.5f) * 0.25f;
            bool in = true;
            for (int k = 0; k < 6 && in; k++)
               in = planes[k][0] * x + planes[k][1] * y + planes[k][2] * z + planes[k][3] >= 0.0f;
            inside += in ? 1 : 0;
         }
   return inside / 64.0f;
}

bool Render3DCull::EnsureProgram()
{
   if (!mProgramTried)
   {
      mProgramTried = true;
      mProgram = Render3DTemporal::BuildComputeShader("render3d_instance_cull.comp.glsl");
   }
   return mProgram != 0;
}

void Render3DCull::EnsureCapacity(int count)
{
   if (mCommands == 0)
   {
      glGenBuffers(1, &mFullFlags);
      glGenBuffers(1, &mSmallFlags);
      glGenBuffers(2, mIndices);
      glGenBuffers(2, mCounts);
      glGenBuffers(1, &mCommands);
      for (int l = 0; l < 2; l++)
      {
         glBindBuffer(GL_COPY_WRITE_BUFFER, mCounts[l]);
         glBufferData(GL_COPY_WRITE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);
      }
      glBindBuffer(GL_COPY_WRITE_BUFFER, mCommands);
      glBufferData(GL_COPY_WRITE_BUFFER, 2 * kCommandBytes, nullptr, GL_DYNAMIC_DRAW);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
   }
   if (count <= mCapacity)
      return;
   const int cap = std::max(count, mCapacity + mCapacity / 2);
   const GLsizeiptr bytes = (GLsizeiptr)cap * (GLsizeiptr)sizeof(GLuint);
   glBindBuffer(GL_COPY_WRITE_BUFFER, mFullFlags);
   glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
   glBindBuffer(GL_COPY_WRITE_BUFFER, mSmallFlags);
   glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
   for (int l = 0; l < 2; l++)
   {
      glBindBuffer(GL_COPY_WRITE_BUFFER, mIndices[l]);
      glBufferData(GL_COPY_WRITE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
   }
   glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
   // The gathered attribute copies are sized per source in Run().
   for (int l = 0; l < 2; l++)
      for (int s = 0; s < kStreams; s++)
         mDstFloats[l][s] = 0;
   mCapacity = cap;
}

bool Render3DCull::Run(const Request& r)
{
   if (r.count <= 0 || !Available())
      return false;
   for (int c = kCol0; c <= kCol3; c++)
      if (r.streams[c].buf == 0 || r.streams[c].size != 4)
         return false;
   if (!EnsureProgram())
      return false;
   EnsureCapacity(r.count);
   mReq = r;

   // Classify.
   glUseProgram(mProgram);
   GLint stride[4], offset[4];
   for (int c = 0; c < 4; c++)
   {
      stride[c] = r.streams[kCol0 + c].stride;
      offset[c] = r.streams[kCol0 + c].offset;
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, (GLuint)c, r.streams[kCol0 + c].buf);
   }
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mFullFlags);
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mSmallFlags);
   glUniform1i(glGetUniformLocation(mProgram, "uCount"), r.count);
   glUniform4iv(glGetUniformLocation(mProgram, "uStride"), 1, stride);
   glUniform4iv(glGetUniformLocation(mProgram, "uOffset"), 1, offset);
   glUniform4fv(glGetUniformLocation(mProgram, "uLocalSphere"), 1, r.localSphere);
   glUniform4fv(glGetUniformLocation(mProgram, "uPlanes"), 6, &r.planes[0][0]);
   glUniform1i(glGetUniformLocation(mProgram, "uFrustum"), r.frustum ? 1 : 0);
   glUniformMatrix4fv(glGetUniformLocation(mProgram, "uViewProj"), 1, GL_FALSE, r.viewProj.m);
   glUniform1f(glGetUniformLocation(mProgram, "uProjScaleY"), r.projScaleY);
   glUniform1f(glGetUniformLocation(mProgram, "uLodPixels"), r.lodPixels);
   glDispatchCompute((GLuint)((r.count + 255) / 256), 1, 1);
   glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
   for (GLuint b = 0; b < 6; b++)
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, b, 0);

   // The distinct (buffer, stride) sources: one gather each per list.
   mSources = 0;
   for (int s = 0; s < kStreams; s++)
   {
      mStreamSource[s] = -1;
      const Stream& st = r.streams[s];
      if (st.buf == 0)
         continue;
      int j = 0;
      while (j < mSources && !(mSource[j] == st.buf && mSourceStride[j] == st.stride))
         j++;
      if (j == mSources)
      {
         mSource[j] = st.buf;
         mSourceStride[j] = st.stride;
         mSources++;
      }
      mStreamSource[s] = j;
   }

   // Compact each list and gather its instance attributes.
   const int lists = r.lodPixels > 0.0f ? 2 : 1;
   const unsigned int flags[2] = { mFullFlags, mSmallFlags };
   bool ok = true;
   for (int l = 0; l < lists && ok; l++)
   {
      ok = pop::gpu::CompactIndices(flags[l], r.count, mIndices[l], mCounts[l]);
      for (int j = 0; j < mSources && ok; j++)
      {
         const int floats = mCapacity * mSourceStride[j];
         if (mDst[l][j] == 0)
            glGenBuffers(1, &mDst[l][j]);
         if (mDstFloats[l][j] != floats)
         {
            glBindBuffer(GL_COPY_WRITE_BUFFER, mDst[l][j]);
            glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)floats * (GLsizeiptr)sizeof(float), nullptr,
                         GL_DYNAMIC_COPY);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            mDstFloats[l][j] = floats;
         }
         ok = pop::gpu::GatherRows(mSource[j], mSourceStride[j], mIndices[l], mCounts[l], r.count, mDst[l][j]);
      }
   }

   if (ok)
   {
      // Draw commands, with the instance counts copied in on the GPU.
      const GLuint commands[10] = { (GLuint)r.indexCount, 0, 0, 0, 0, (GLuint)kQuadIndices, 0, 0, 0, 0 };
      glBindBuffer(GL_COPY_WRITE_BUFFER, mCommands);
      glBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(commands), commands);
      for (int l = 0; l < lists; l++)
      {
         glBindBuffer(GL_COPY_READ_BUFFER, mCounts[l]);
         glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, l * kCommandBytes + sizeof(GLuint),
                             sizeof(GLuint));
      }
      glBindBuffer(GL_COPY_READ_BUFFER, 0);
      glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
      glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
   }
   glUseProgram(0);
   return ok;
}

void Render3DCull::Bind(int list) const
{
   for (int s = 0; s < kStreams; s++)
   {
      const Stream& st = mReq.streams[s];
      if (st.buf == 0 || mStreamSource[s] < 0)
         continue;
      PointStream(kStreamLoc[s], mDst[list][mStreamSource[s]], st.size, st.stride, st.offset);
   }
   glBindBuffer(GL_ARRAY_BUFFER, mIndices[list]);
   glEnableVertexAttribArray(kIndexLoc);
   glVertexAttribIPointer(kIndexLoc, 1, GL_UNSIGNED_INT, sizeof(GLuint), (void*)0);
   glVertexAttribDivisor(kIndexLoc, 1);
}

void Render3DCull::Draw(int list, unsigned int prim) const
{
   glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mCommands);
   glDrawElementsIndirect(prim, GL_UNSIGNED_INT, (void*)(size_t)(list * kCommandBytes));
   glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void Render3DCull::Restore() const
{
   for (int s = 0; s < kStreams; s++)
   {
      const Stream& st = mReq.streams[s];
      if (st.buf != 0)
         PointStream(kStreamLoc[s], st.buf, st.size, st.stride, st.offset);
   }
   glDisableVertexAttribArray(kIndexLoc);
   glVertexAttribDivisor(kIndexLoc, 0);
}

unsigned int Render3DCull::QuadVao()
{
   if (mQuadVao != 0)
      return mQuadVao;
   struct QuadVertex { float px, py, pz, nx, ny, nz, u, v; };
   static const QuadVertex kQuad[4] = {
      { -1, -1, 0,  0, 0, 1,  0, 0 },
      {  1, -1, 0,  0, 0, 1,  1, 0 },
      {  1,  1, 0,  0, 0, 1,  1, 1 },
      { -1,  1, 0,  0, 0, 1,  0, 1 },
   };
   static const GLuint kQuadIdx[kQuadIndices] = { 0, 1, 2, 0, 2, 3 };
   glGenVertexArrays(1, &mQuadVao);
   glGenBuffers(1, &mQuadVbo);
   glGenBuffers(1, &mQuadIbo);
   glBindVertexArray(mQuadVao);
   glBindBuffer(GL_ARRAY_BUFFER, mQuadVbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mQuadIbo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIdx), kQuadIdx, GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)0);
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)(3 * sizeof(float)));
   glEnableVertexAttribArray(2);
   glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)(6 * sizeof(float)));
   GLUtil::LabelObject(GL_VERTEX_ARRAY, mQuadVao, "Render 3D: LOD quad");
   return mQuadVao;
}

void Render3DCull::Release()
{
   if (mProgram != 0) glDeleteProgram(mProgram);
   if (mFullFlags != 0) glDeleteBuffers(1, &mFullFlags);
   if (mSmallFlags != 0) glDeleteBuffers(1, &mSmallFlags);
   if (mIndices[0] != 0) glDeleteBuffers(2, mIndices);
   if (mCounts[0] != 0) glDeleteBuffers(2, mCounts);
   if (mCommands != 0) glDeleteBuffers(1, &mCommands);
   for (int l = 0; l < 2; l++)
      for (int s = 0; s < kStreams; s++)
      {
         if (mDst[l][s] != 0) glDeleteBuffers(1, &mDst[l][s]);
         mDst[l][s] = 0;
         mDstFloats[l][s] = 0;
      }
   if (mQuadVao != 0) glDeleteVertexArrays(1, &mQuadVao);
   if (mQuadVbo != 0) glDeleteBuffers(1, &mQuadVbo);
   if (mQuadIbo != 0) glDeleteBuffers(1, &mQuadIbo);
   mProgram = 0;
   mProgramTried = false;
   mFullFlags = mSmallFlags = mCommands = 0;
   mIndices[0] = mIndices[1] = mCounts[0] = mCounts[1] = 0;
   mQuadVao = mQuadVbo = mQuadIbo = 0;
   mCapacity = 0;
   mSources = 0;
}

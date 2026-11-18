// fork: asynchronous GPU->CPU RGBA8 readback through a 2-PBO ring (see docs/fps-changes.md).
// A synchronous glReadPixels stalls the CPU until the GPU drains its queue; here the read is
// queued into a pixel-pack buffer and the result of the PREVIOUS call is returned instead
// (1 frame latency). The very first call (and any size change) falls back to a sync read.
#pragma once
#include "gl3.h"
#include "PreviewQuality.h"
#include <chrono>
#include <cstring>
#include <vector>

class PboReadback
{
public:
   PboReadback() = default;
   PboReadback(const PboReadback&) = delete;
   PboReadback& operator=(const PboReadback&) = delete;
   ~PboReadback() { Release(); }

   // Reads size x size RGBA8 from the currently bound READ framebuffer into out.
   // Caller binds the FBO first (as it did for glReadPixels). Always fills `out`.
   void Read(int size, std::vector<unsigned char>& out)
   {
      const size_t bytes = (size_t)size * size * 4;
      out.resize(bytes);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      // Sparse sampling (< ~10 Hz): the previous result would be a whole sample period old,
      // so read synchronously instead - the stall is rare there anyway.
      const auto now = std::chrono::steady_clock::now();
      const bool stale = mHavePrev && std::chrono::duration<double>(now - mLastCall).count() > 0.1;
      mLastCall = now;
      if (stale)
         mHavePrev = false;
      if (ForkLegacyMode())
      {
         glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
         return;
      }
      if (size != mSize || mPbo[0] == 0)
      {
         Release();
         mSize = size;
         glGenBuffers(2, mPbo);
         for (int i = 0; i < 2; i++)
         {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_READ);
         }
         glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
         glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, out.data()); // sync once
         mHavePrev = false;
         return;
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[mCur]);
      glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); // async
      const int prev = 1 - mCur;
      if (mHavePrev)
      {
         glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[prev]);
         if (const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_READ_BIT))
         {
            std::memcpy(out.data(), p, bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
         }
      }
      else
      {
         // No earlier frame yet: one sync read of the same pixels.
         glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
         glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      mHavePrev = true;
      mCur = prev;
   }

   void Release()
   {
      if (mPbo[0] != 0)
         glDeleteBuffers(2, mPbo);
      mPbo[0] = mPbo[1] = 0;
      mSize = 0;
      mHavePrev = false;
   }

private:
   GLuint mPbo[2] = { 0, 0 };
   int mSize = 0;
   int mCur = 0;
   bool mHavePrev = false;
   std::chrono::steady_clock::time_point mLastCall{};
};

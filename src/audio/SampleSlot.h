#pragma once

#include <atomic>
#include <cstddef>

#include "platform/Platform.h"

// Main-thread-hands-a-buffer-to-the-audio-thread lifetime management,
// lifted out of SamplerNode.cpp (where it first shipped as a single
// pending/active/retire triple plus BufferRetireRing) so DrumSequencerNode
// can give each of its 8 lanes its own instance without reimplementing the
// same use-after-free trap eight times.
//
// Contract, unchanged from the original: the main thread calls Push() with
// a freshly allocated T* it owns; the audio thread calls SwapIn() at the top
// of its own ProcessBlock (never mid-block) to adopt it and retires whatever
// was active before into an SPSC ring rather than deleting it there
// (deleting on the audio thread is one of the standing audio-thread
// prohibitions); the main thread calls DrainRetired() once per CookIfNeeded
// to actually free those.
//
// Templatised on the payload type T (originally hard-typed to
// Platform::SampleBuffer*) so WaveTerrainNode's BankData and
// ImageSpectralSynthNode's SpectrogramMatrix double-buffers - which had the
// exact same "main thread writes, audio thread holds a reference for a
// whole block, a second write mid-block would race" lifetime problem as a
// sampler's decoded buffer - can reuse this instead of hand-rolling a third
// variant of it. The only requirement on T is that `delete` works on a
// T* (a plain value type, no virtual destructor needed since the exact
// type is always known here).
namespace SampleSlotDetail
{
   // Tiny SPSC ring of raw pointers: the audio thread retires a superseded
   // T* here instead of deleting it directly. The main thread drains and
   // deletes them. Same index discipline as MeterRing/NoteEventQueue. A
   // full ring silently drops the retire (the buffer leaks) rather than
   // overwriting a slot the consumer hasn't read - loads are rare enough
   // that this never triggers in practice, and leaking one buffer beats a
   // double free.
   template <typename T>
   class BufferRetireRing
   {
   public:
      static constexpr int kCapacity = 8;

      // Audio thread only.
      void Retire(T* buf)
      {
         const size_t tail = mTail.load(std::memory_order_relaxed);
         const size_t head = mHead.load(std::memory_order_acquire);
         const size_t next = (tail + 1) % kCapacity;
         if (next == head)
            return; // full - drop rather than overwrite an unread slot
         mEntries[tail] = buf;
         mTail.store(next, std::memory_order_release);
      }

      // Main thread only.
      T* Drain()
      {
         const size_t head = mHead.load(std::memory_order_relaxed);
         const size_t tail = mTail.load(std::memory_order_acquire);
         if (head == tail)
            return nullptr;
         T* out = mEntries[head];
         mHead.store((head + 1) % kCapacity, std::memory_order_release);
         return out;
      }

   private:
      T* mEntries[kCapacity] = {};
      std::atomic<size_t> mHead { 0 };
      std::atomic<size_t> mTail { 0 };
   };
}

template <typename T>
class SampleSlotT
{
public:
   // Main thread only. Hands over ownership of a freshly built T*; the
   // previously *pending* one (if any - two loads in a row before the audio
   // thread got to the first) is deleted here since the audio thread never
   // saw it and can't be racing a read against it.
   void Push(T* buf)
   {
      T* old = mPendingBuffer.exchange(buf, std::memory_order_acq_rel);
      if (old != nullptr)
         delete old;
   }

   // Audio thread only, top of ProcessBlock. Adopts a newly pushed buffer if
   // one is waiting, retiring whatever was active before rather than
   // deleting it in place. Returns true if a swap happened, so a caller can
   // reset any playback state that shouldn't carry across a buffer change.
   bool SwapIn()
   {
      T* fresh = mPendingBuffer.exchange(nullptr, std::memory_order_acq_rel);
      if (fresh == nullptr)
         return false;
      if (mActiveBuffer != nullptr)
         mRetireRing.Retire(mActiveBuffer);
      mActiveBuffer = fresh;
      return true;
   }

   // Audio thread only.
   T* Active() const { return mActiveBuffer; }

   // Main thread only, called once per frame from CookIfNeeded.
   void DrainRetired()
   {
      while (T* b = mRetireRing.Drain())
         delete b;
   }

private:
   T* mActiveBuffer = nullptr;
   std::atomic<T*> mPendingBuffer { nullptr };
   SampleSlotDetail::BufferRetireRing<T> mRetireRing;
};

// The original, still-used-as-is name: every existing SamplerNode/
// GranularNode/PaulStretchNode/DrumSequencerNode call site keeps compiling
// unchanged against this alias.
using SampleSlot = SampleSlotT<Platform::SampleBuffer>;

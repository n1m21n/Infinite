#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Turbo 0.48 (upstream WP8): one bucket of one arrangement clip's live
// waveform, measured by the audio thread as the clip actually played.
//
// `bucket` is an index from the clip's own start, not from the timeline's, so
// playing the same stretch twice overwrites rather than appends. The main
// thread owns invalidation (ArrangeClipShape / ArrangeLiveWave in
// ArrangeUi.inl); the audio thread only labels and publishes.
struct ClipPeak
{
   uint64_t clipId = 0;
   // The clip shape this bucket was measured under (ArrangeClipShape). A
   // bucket still in flight when an edit reshapes its clip is dropped by the
   // main thread instead of lighting a pixel of the new waveform.
   uint64_t shape = 0;
   int bucket = -1;
   float minValue = 0.0f;
   float maxValue = 0.0f;
};

// 1/16 beat (Arrange::kPPQ / 16 ticks). Spelled in beats because the audio
// thread works in beats and this header must not depend on the model.
constexpr double kClipPeakBucketsPerBeat = 16.0;

// Lock-free single-producer (audio thread) / single-consumer (main thread)
// ring of finished buckets. Neither side allocates. A full ring drops: the
// waveform is a display, and the next pass over that stretch fills it in.
class ClipPeakRing
{
public:
   static constexpr int kCapacity = 8192; // upstream size: a long busy arrangement must not drop buckets

   // Audio thread only.
   void Write(const ClipPeak& peak)
   {
      const size_t tail = mTail.load(std::memory_order_relaxed);
      const size_t next = (tail + 1) % kCapacity;
      if (next == mHead.load(std::memory_order_acquire))
      {
         mDropped.fetch_add(1, std::memory_order_relaxed);
         return;
      }
      mEntries[tail] = peak;
      mTail.store(next, std::memory_order_release);
   }

   // Main thread only. Returns how many entries were read.
   int Read(ClipPeak* out, int maxCount)
   {
      size_t head = mHead.load(std::memory_order_relaxed);
      const size_t tail = mTail.load(std::memory_order_acquire);
      int n = 0;
      while (head != tail && n < maxCount)
      {
         out[n++] = mEntries[head];
         head = (head + 1) % kCapacity;
      }
      mHead.store(head, std::memory_order_release);
      return n;
   }

   // Diagnostic only.
   uint64_t DroppedCount() const { return mDropped.load(std::memory_order_relaxed); }

private:
   ClipPeak mEntries[kCapacity] {};
   std::atomic<size_t> mHead { 0 };   // consumer reads from here
   std::atomic<size_t> mTail { 0 };   // producer writes here
   std::atomic<uint64_t> mDropped { 0 };
};

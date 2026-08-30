#include "AudioCaptureRing.h"

// Every producer writes numFrames*2 floats per call - this ring only ever
// carries interleaved stereo (see the header). Dropping on overflow (or
// splitting a read) must respect that pairing: the old per-sample loop below
// could break mid-pair, silently leaving the ring's tail at an odd offset
// against the L,R,L,R.. grid. Nothing ever re-anchors that phase, so every
// pair written after such a drop is channel-swapped from then on - which
// doesn't shorten the file (the existing sample-count/duration assertions
// don't catch it) and decodes as intermittent noise, not silence, until
// another odd-offset event happens to restore parity by chance. That matches
// a reported take that plays sped-up, then alternates silence and "weird
// noise" for the rest of its length far better than a pure sample-count
// shortfall does. Fixed by only ever writing/reading whole stereo pairs.
void AudioCaptureRing::Write(const float* samples, int count)
{
   size_t tail = mTail.load(std::memory_order_relaxed);
   const size_t head = mHead.load(std::memory_order_acquire);

   int i = 0;
   for (; i + 1 < count; i += 2)
   {
      const size_t next1 = (tail + 1) % kCapacity;
      const size_t next2 = (next1 + 1) % kCapacity;
      if (next1 == head || next2 == head)
      {
         // Full: main thread isn't draining fast enough. Drop the rest of
         // this write, but only in whole pairs - never leave a lone L
         // sample committed with its R sample dropped, or vice versa.
         overflowCount.fetch_add((uint64_t)(count - i), std::memory_order_relaxed);
         mTail.store(tail, std::memory_order_release);
         return;
      }

      mEntries[tail] = samples[i];
      mEntries[next1] = samples[i + 1];
      tail = next2;
   }
   if (i < count)
   {
      // Defensive only: every real caller passes an even count (a whole
      // number of stereo frames). An odd leftover sample here can't be
      // paired, so it is dropped rather than committed alone.
      overflowCount.fetch_add(1, std::memory_order_relaxed);
   }
   mTail.store(tail, std::memory_order_release);
}

int AudioCaptureRing::Read(float* out, int maxCount)
{
   size_t head = mHead.load(std::memory_order_relaxed);
   const size_t tail = mTail.load(std::memory_order_acquire);

   int n = 0;
   while (head != tail && n < maxCount)
   {
      out[n++] = mEntries[head];
      head = (head + 1) % kCapacity;
   }
   if ((n & 1) != 0)
   {
      // Never hand the caller a dangling half-pair: back off the odd
      // trailing sample and leave it (with its still-unread partner) in the
      // ring for the next Read() instead of consuming it alone, which would
      // shift every pair after it out of phase from the consumer side.
      n -= 1;
      head = (head + kCapacity - 1) % kCapacity;
   }
   mHead.store(head, std::memory_order_release);
   return n;
}

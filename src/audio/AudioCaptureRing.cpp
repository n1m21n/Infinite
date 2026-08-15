#include "AudioCaptureRing.h"

void AudioCaptureRing::Write(const float* samples, int count)
{
   size_t tail = mTail.load(std::memory_order_relaxed);
   const size_t head = mHead.load(std::memory_order_acquire);

   for (int i = 0; i < count; ++i)
   {
      const size_t next = (tail + 1) % kCapacity;
      if (next == head)
      {
         overflowCount.fetch_add((uint64_t)(count - i), std::memory_order_relaxed);
         break; // full: main thread isn't draining fast enough, drop the rest of this write
      }

      mEntries[tail] = samples[i];
      tail = next;
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
   mHead.store(head, std::memory_order_release);
   return n;
}

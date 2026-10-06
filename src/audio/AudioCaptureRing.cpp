#include "AudioCaptureRing.h"

// Turbo 0.51: stereo-frame granularity. A write is accepted whole (2 floats
// per frame) or the frames that do not fit are dropped and counted in
// overflowCount (in floats), so L/R never shift after an overflow.
void AudioCaptureRing::Write(const float* samples, int count)
{
   size_t tail = mTail.load(std::memory_order_relaxed);
   const size_t head = mHead.load(std::memory_order_acquire);

   const int frames = count / 2;
   for (int f = 0; f < frames; ++f)
   {
      const size_t n1 = (tail + 1) % kCapacity;
      const size_t n2 = (tail + 2) % kCapacity;
      if (n1 == head || n2 == head)
      {
         overflowCount.fetch_add((uint64_t)(frames - f) * 2, std::memory_order_relaxed);
         break;
      }
      mEntries[tail] = samples[f * 2];
      mEntries[n1] = samples[f * 2 + 1];
      tail = n2;
   }
   mTail.store(tail, std::memory_order_release);
}

int AudioCaptureRing::Read(float* out, int maxCount)
{
   size_t head = mHead.load(std::memory_order_relaxed);
   const size_t tail = mTail.load(std::memory_order_acquire);

   maxCount &= ~1;
   int n = 0;
   while (head != tail && n < maxCount)
   {
      out[n++] = mEntries[head];
      head = (head + 1) % kCapacity;
   }
   mHead.store(head, std::memory_order_release);
   return n;
}

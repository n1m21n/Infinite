#pragma once

#include <atomic>
#include <cstddef>

// Lock-free single-producer (audio thread) / single-consumer (main thread)
// ring of decimated sample data (e.g. one peak per N samples - callers
// decide the decimation, this class just moves floats). Generic rather than
// audio-node-specific; P3's Scope node will be the first real consumer.
class MeterRing
{
public:
   static constexpr int kCapacity = 4096;

   // Audio thread only.
   void Write(const float* samples, int count);

   // Main thread only. Returns the number of samples actually read.
   int Read(float* out, int maxCount);

   // Main thread only. Drains everything queued and returns only the most recent
   // value. For latest-value signals (playheads, level meters) where a backlog is
   // staleness, not history - Read() one-per-frame against a producer that writes
   // once per audio block accumulates unbounded lag.
   bool ReadLatest(float& out);

private:
   float mEntries[kCapacity] {};
   std::atomic<size_t> mHead { 0 }; // consumer reads from here
   std::atomic<size_t> mTail { 0 }; // producer writes here
};

#pragma once

#include <atomic>
#include <cstddef>

// Lock-free single-producer (audio thread) / single-consumer (main thread)
// ring carrying interleaved float samples for one Audio Out's WAV capture -
// same SPSC shape as MeterRing, but sized for real audio instead of a
// decimated meter trace. MeterRing's kCapacity (4096 floats, ~42ms of 48kHz
// stereo) would drop audio on a single missed UI frame; this ring holds
// roughly 2 seconds of stereo at 48kHz so a slow frame doesn't glitch the file.
class AudioCaptureRing
{
public:
   static constexpr size_t kCapacity = 192000;

   // Audio thread only. Drops (and counts) samples past the ring's capacity
   // rather than blocking - a stall shows up as a reported overflow instead
   // of a hitch on the real-time thread.
   void Write(const float* samples, int count);

   // Main thread only. Returns the number of samples actually read.
   int Read(float* out, int maxCount);

   // Audio thread checks this before writing; main thread sets it when
   // starting/stopping a recording. Plain bool, not gated behind topology -
   // starting a recording must not require a topology rebuild to take effect.
   std::atomic<bool> enabled { false };

   // Incremented (audio thread) whenever the ring was full and samples were
   // dropped; read and reset by the consumer that reports it, so a stall
   // shows up in the UI instead of silently producing a glitched file.
   std::atomic<uint64_t> overflowCount { 0 };

private:
   float mEntries[kCapacity] {};
   std::atomic<size_t> mHead { 0 }; // consumer reads from here
   std::atomic<size_t> mTail { 0 }; // producer writes here
};

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "NoteEvent.h"

// Lock-free single-producer/single-consumer ring of NoteEvent, following
// MeterRing's index discipline (producer writes only mTail, consumer writes
// only mHead) but never dropping in a way that can leave a voice stuck.
//
// Today both ends of every queue run on the real-time audio thread (a note
// producer's ProcessBlock, then its consumer's, in the same callback,
// ordered by the topology walk - see RebuildAudioTopology's note pass in
// main.cpp). It is still built as a true SPSC ring rather than a plain
// same-thread buffer: a future MIDI-In producer feeding from a CoreMIDI
// callback thread needs exactly this contract and nothing else, so there is
// no separate "queue" type to introduce later - see P3a-notes-prompt.md's
// live-MIDI-constraint section.
//
// Capacity: 256 events. At a 512-frame block (~10ms @ 48kHz) that is far more
// headroom than any Part-1 producer can fill in one block - the cost is
// sizeof(NoteEvent) * 256, a few KB, trivial to preallocate.
//
// Overflow policy: a note-ON that arrives when the ring is full is dropped
// (matches MeterRing's "producer isn't keeping up" behaviour - losing an
// attack is audible but recoverable). A note-OFF that arrives when the ring
// is full is never dropped: it forces an overwrite of the oldest unread
// slot instead, because a lost note-off is a stuck note - strictly worse
// than losing whatever stale event it displaces. Every forced overwrite and
// every dropped note-on increments mOverflowCount, mirroring how
// AudioEngine::XrunCount() surfaces dropouts rather than hiding them.
class NoteEventQueue
{
public:
   static constexpr int kCapacity = 256;

   // Audio thread only (the producer side).
   void Push(const NoteEvent& e)
   {
      size_t tail = mTail.load(std::memory_order_relaxed);
      const size_t head = mHead.load(std::memory_order_acquire);
      const size_t next = (tail + 1) % kCapacity;

      if (next == head)
      {
         // Full. A note-off must still get through - force it into the
         // slot the consumer hasn't read yet (dropping that oldest event)
         // rather than dropping the note-off itself.
         if (!e.isNoteOn)
         {
            mEntries[tail] = e;
            mTail.store(next, std::memory_order_relaxed);
            mHead.store((head + 1) % kCapacity, std::memory_order_release);
         }
         mOverflowCount.fetch_add(1, std::memory_order_relaxed);
         return;
      }

      mEntries[tail] = e;
      mTail.store(next, std::memory_order_release);
   }

   // Audio thread only (the consumer side). Returns the number of events
   // actually written into `out` (capacity `maxCount`), in the order they
   // were pushed.
   int Pop(NoteEvent* out, int maxCount)
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

   uint64_t OverflowCount() const { return mOverflowCount.load(std::memory_order_relaxed); }

private:
   NoteEvent mEntries[kCapacity];
   std::atomic<size_t> mHead { 0 };
   std::atomic<size_t> mTail { 0 };
   std::atomic<uint64_t> mOverflowCount { 0 };
};

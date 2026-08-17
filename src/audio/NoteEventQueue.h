#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "NoteEvent.h"

// Lock-free single-producer/multi-consumer ring of NoteEvent, following
// MeterRing's index discipline (producer writes only mTail) but never
// dropping in a way that can leave a voice stuck.
//
// Today both ends of every queue run on the real-time audio thread (a note
// producer's ProcessBlock, then its consumers', in the same callback,
// ordered by the topology walk - see RebuildAudioTopology's note pass in
// main.cpp). It is still built as a true lock-free ring rather than a plain
// same-thread buffer: a future MIDI-In producer feeding from a CoreMIDI
// callback thread needs exactly this contract and nothing else, so there is
// no separate "queue" type to introduce later - see P3a-notes-prompt.md's
// live-MIDI-constraint section.
//
// Multi-consumer: a single note source can fan out to several consumers
// (two synths, a note processor, etc). Each consumer registers its own
// cursor via RegisterConsumer() and pops with that cursor id, so one
// consumer draining events can never starve another. The ring only
// reclaims space up to the slowest cursor's head - a fast consumer can't
// let the producer overwrite events a slower consumer hasn't read yet.
// RegisterConsumer/ResetConsumers are called from the topology-rebuild
// pass only, never concurrently with Push/Pop.
//
// Capacity: 256 events. At a 512-frame block (~10ms @ 48kHz) that is far more
// headroom than any Part-1 producer can fill in one block - the cost is
// sizeof(NoteEvent) * 256, a few KB, trivial to preallocate.
//
// Overflow policy: a note-ON that arrives when the ring is full (relative to
// the slowest cursor) is dropped (matches MeterRing's "producer isn't
// keeping up" behaviour - losing an attack is audible but recoverable). A
// note-OFF that arrives when the ring is full is never dropped: it forces an
// overwrite of the oldest unread slot instead, because a lost note-off is a
// stuck note - strictly worse than losing whatever stale event it displaces;
// any cursor still sitting on the overwritten slot is advanced past it.
// Every forced overwrite and every dropped note-on increments
// mOverflowCount, mirroring how AudioEngine::XrunCount() surfaces dropouts
// rather than hiding them.
class NoteEventQueue
{
public:
   static constexpr int kCapacity = 256;
   static constexpr int kMaxConsumers = 8;

   // Topology-rebuild thread only, never concurrent with Push/Pop. Drops
   // every registered cursor so ids don't leak across rebuild generations;
   // call once per producer at the start of each rebuild, before any
   // RegisterConsumer() calls for that generation.
   void ResetConsumers() { mNumCursors = 0; }

   // Topology-rebuild thread only. Registers a new reader and returns its
   // cursor id, or -1 if kMaxConsumers is already registered. The cursor
   // starts at the current tail, so a newly wired consumer only sees events
   // pushed after it was wired.
   int RegisterConsumer()
   {
      const int n = mNumCursors;
      if (n >= kMaxConsumers)
         return -1;
      mCursorHeads[n].store(mTail.load(std::memory_order_relaxed), std::memory_order_relaxed);
      mNumCursors = n + 1;
      return n;
   }

   // Audio thread only (the producer side).
   void Push(const NoteEvent& e)
   {
      size_t tail = mTail.load(std::memory_order_relaxed);
      const size_t next = (tail + 1) % kCapacity;
      const size_t minHead = MinCursorHead();

      if (next == minHead)
      {
         // Full (relative to the slowest cursor). A note-off must still get
         // through - force it into the slot the slowest consumer hasn't
         // read yet (dropping that oldest event) rather than dropping the
         // note-off itself.
         if (!e.isNoteOn)
         {
            mEntries[tail] = e;
            mTail.store(next, std::memory_order_relaxed);
            AdvanceCursorsPast(minHead);
         }
         mOverflowCount.fetch_add(1, std::memory_order_relaxed);
         return;
      }

      mEntries[tail] = e;
      mTail.store(next, std::memory_order_release);
   }

   // Audio thread only (a consumer side, identified by the cursor id it was
   // given by RegisterConsumer). Returns the number of events actually
   // written into `out` (capacity `maxCount`), in the order they were
   // pushed.
   int Pop(int cursor, NoteEvent* out, int maxCount)
   {
      if (cursor < 0 || cursor >= mNumCursors)
         return 0;

      size_t head = mCursorHeads[cursor].load(std::memory_order_relaxed);
      const size_t tail = mTail.load(std::memory_order_acquire);

      int n = 0;
      while (head != tail && n < maxCount)
      {
         out[n++] = mEntries[head];
         head = (head + 1) % kCapacity;
      }
      mCursorHeads[cursor].store(head, std::memory_order_release);
      return n;
   }

   uint64_t OverflowCount() const { return mOverflowCount.load(std::memory_order_relaxed); }

private:
   // With no consumers registered, behave like the old single-stuck-head
   // ring: nothing is reading, so the floor never advances and the queue
   // eventually overflows exactly as before.
   size_t MinCursorHead() const
   {
      if (mNumCursors == 0)
         return 0;
      size_t minHead = mCursorHeads[0].load(std::memory_order_acquire);
      for (int i = 1; i < mNumCursors; i++)
         minHead = std::min(minHead, mCursorHeads[i].load(std::memory_order_acquire));
      return minHead;
   }

   void AdvanceCursorsPast(size_t overwrittenSlot)
   {
      const size_t next = (overwrittenSlot + 1) % kCapacity;
      for (int i = 0; i < mNumCursors; i++)
      {
         size_t h = mCursorHeads[i].load(std::memory_order_relaxed);
         if (h == overwrittenSlot)
            mCursorHeads[i].store(next, std::memory_order_relaxed);
      }
   }

   NoteEvent mEntries[kCapacity];
   std::atomic<size_t> mCursorHeads[kMaxConsumers];
   int mNumCursors = 0;
   std::atomic<size_t> mTail { 0 };
   std::atomic<uint64_t> mOverflowCount { 0 };
};

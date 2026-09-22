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

   // No-device-running use only (a single-threaded test fixture, or the
   // owning thread when no audio callback can ever race it), never
   // concurrent with Push/Pop. RebuildAudioTopology's real topology-builder
   // pass no longer calls this directly - it only *describes* the wiring
   // (AudioTopology::noteOutboxes/noteWires); AudioEngine::ApplyNoteWiringIfNew
   // applies it via AdoptConsumers() below, on whichever thread actually owns
   // the ProcessList, so cursors are never reset out from under the audio
   // thread mid-block. Drops every registered cursor so ids don't leak
   // across rebuild generations; call once per producer at the start of each
   // rebuild, before any RegisterConsumer() calls for that generation.
   void ResetConsumers() { mNumCursors = 0; }

   // No-device-running use only - see ResetConsumers' comment; the real
   // topology-rebuild path goes through AdoptConsumers() instead. Registers a
   // new reader and returns its cursor id, or -1 if kMaxConsumers is already
   // registered. The cursor starts at the current tail, so a newly wired
   // consumer only sees events pushed after it was wired.
   int RegisterConsumer()
   {
      const int n = mNumCursors;
      if (n >= kMaxConsumers)
         return -1;
      mCursorHeads[n].store(mTail.load(std::memory_order_relaxed), std::memory_order_relaxed);
      mNumCursors = n + 1;
      return n;
   }

   // Owning-thread-only (the thread currently running the ProcessList this
   // queue's generation belongs to - the audio thread during a device
   // callback, or the main thread while no device is open and it owns the
   // list outright), never concurrent with Push/Pop since that thread IS the
   // one calling Push/Pop. The real replacement for a ResetConsumers() +
   // `count` x RegisterConsumer() sequence: sets every head first, the count
   // second, so a Pop() that (on this same thread, strictly after this call
   // returns) reads mNumCursors never sees a partially-adopted head array.
   // Called by AudioEngine::ApplyNoteWiringIfNew once per outbox per
   // generation - see its comment for how `heads` is computed (carried over
   // from what each consumer last actually applied, or the current tail for
   // a freshly wired one).
   void AdoptConsumers(int count, const size_t heads[kMaxConsumers])
   {
      for (int i = 0; i < count; i++)
         mCursorHeads[i].store(heads[i], std::memory_order_relaxed);
      mNumCursors = count;
   }

   // Owning-thread-only (same rule as AdoptConsumers). The current tail, so
   // a caller computing a freshly-wired consumer's starting head doesn't need
   // its own separate accessor for "current tail".
   size_t Tail() const { return mTail.load(std::memory_order_relaxed); }

   // Owning-thread-only (same rule as AdoptConsumers). The raw ring index a
   // still-registered cursor currently sits at - used by
   // AudioEngine::ApplyNoteWiringIfNew to read a consumer's OLD read
   // position, under its OLD cursor id, before AdoptConsumers() overwrites
   // this queue's cursor table with the new generation's ids. Returns the
   // current tail for an out-of-range cursor (nothing to carry over).
   size_t CursorHead(int cursor) const
   {
      if (cursor < 0 || cursor >= mNumCursors)
         return mTail.load(std::memory_order_relaxed);
      return mCursorHeads[cursor].load(std::memory_order_relaxed);
   }

   // Audio thread only (the producer side).
   void Push(const NoteEvent& e)
   {
      size_t tail = mTail.load(std::memory_order_relaxed);
      const size_t next = (tail + 1) % kCapacity;
      const size_t worstHead = WorstBacklogHead(tail);

      if (next == worstHead)
      {
         // Full (relative to the slowest cursor). A note-off must still get
         // through - force it into the slot the slowest consumer hasn't
         // read yet (dropping that oldest event) rather than dropping the
         // note-off itself.
         if (!e.isNoteOn)
         {
            mEntries[tail] = e;
            // release, matching the normal-path store below: a future MIDI-In
            // producer thread's Push here must be as visible to consumers as
            // any other Push, and this is the one path that used to differ
            // (see the header comment on the multi-producer plan this queue
            // already carries a cursor contract for).
            mTail.store(next, std::memory_order_release);
            AdvanceCursorsPast(worstHead);
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

   // Main thread only, and only while no audio callback can run (the engine's
   // device-less note pump). A consumer that is never drained there - a synth
   // that only runs with a device - would otherwise sit on a growing backlog
   // and eventually stall the ring for every consumer that IS being drained.
   // Any cursor more than `maxBacklog` events behind is moved to the tail.
   void TrimLaggingConsumers(size_t maxBacklog)
   {
      const size_t tail = mTail.load(std::memory_order_acquire);
      for (int i = 0; i < mNumCursors; i++)
      {
         const size_t head = mCursorHeads[i].load(std::memory_order_relaxed);
         const size_t backlog = (tail + kCapacity - head) % kCapacity;
         if (backlog > maxBacklog)
            mCursorHeads[i].store(tail, std::memory_order_release);
      }
   }

   uint64_t OverflowCount() const { return mOverflowCount.load(std::memory_order_relaxed); }

private:
   // The cursor blocking the ring the most: the one with the largest backlog
   // `(tail + kCapacity - head) % kCapacity`, not the numerically smallest
   // raw ring index. Picking the smallest index used to pick the wrong
   // cursor across a wrap - a lagging cursor sitting at 250 lost to a fast
   // one at 5 even though 250 is barely behind (its backlog is small) and 5
   // could be nearly a full lap behind (huge backlog), which let Push's
   // "full" check misfire exactly on the overflow path that exists to
   // protect note-offs. With no consumers registered, behave like the old
   // single-stuck-head ring: nothing is reading, so the floor never advances
   // and the queue eventually overflows exactly as before (head 0 has
   // backlog `tail`, the whole ring - always the worst possible answer).
   size_t WorstBacklogHead(size_t tail) const
   {
      if (mNumCursors == 0)
         return 0;
      size_t worstHead = mCursorHeads[0].load(std::memory_order_acquire);
      size_t worstBacklog = (tail + kCapacity - worstHead) % kCapacity;
      for (int i = 1; i < mNumCursors; i++)
      {
         const size_t head = mCursorHeads[i].load(std::memory_order_acquire);
         const size_t backlog = (tail + kCapacity - head) % kCapacity;
         if (backlog > worstBacklog)
         {
            worstBacklog = backlog;
            worstHead = head;
         }
      }
      return worstHead;
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

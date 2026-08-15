# 03 — `NoteEventQueue`'s single-consumer violation

Small, independent, no dependencies. Worth doing early precisely because it is
cheap and the bug is the kind that only shows up later, under load, on someone
else's machine.

This is a **latent** bug, not a live one. Both ends of the queue currently run
on the same audio thread (stated at `src/audio/NoteEventQueue.h:13-16`), so
nothing races today. It matters because the same header (`:16-20`) says the
type exists in SPSC form specifically so a future CoreMIDI-thread producer can
use it unchanged — and on that day this becomes a real data race in the note
path, which is the path where a dropped or duplicated event means a stuck note.

Paste everything below into a fresh Claude Code session.

---

Fix the single-consumer-invariant violation in Infinite's `NoteEventQueue`
(/Users/namansoni/infinte).

Read `src/audio/NoteEventQueue.h` in full, then read
`src/audio/ParamMailbox.h:16-19` — that comment records an earlier ring-buffer
version of the *param* path being deliberately removed for this exact bug
("the producer writing the consumer-owned head index on overrun, which broke
the single-consumer invariant under real concurrent load"). The same mistake is
still present in the note path.

## The defect

`NoteEventQueue` is otherwise a correct lock-free SPSC ring: inline
`NoteEvent mEntries[256]` (`:37, :86`), `std::atomic<size_t> mHead/mTail`
(`:87-88`), producer `Push` (`:40-63`) loads tail relaxed / head acquire and
stores tail release, consumer `Pop` (`:68-81`) mirrors it. No allocation, no
locks. Overflow is split by event type (`:46-59`): a full-ring note-**on** is
dropped, a full-ring note-**off** overwrites the oldest unread slot so it can
never be lost, and both bump `mOverflowCount` (`:89`).

That note-off path is the problem. To overwrite the oldest slot it advances the
consumer's index from the producer side:

```
NoteEventQueue.h:55:   mHead.store((head + 1) % kCapacity, ...);
```

With a separate producer thread this races `Pop`'s own `mHead` store — the
consumer can be mid-read of the slot the producer is recycling, and the two
stores can interleave to lose or repeat events.

## What to decide

The *intent* — never lose a note-off, because a lost note-off is a stuck note —
is correct and must survive. Only the mechanism is wrong. Weigh at least these
and justify the choice in a comment; do not just pick the first:

- Make note-off delivery not depend on ring capacity at all (e.g. a separate
  small always-drained channel, or folding pending note-offs into a bitmask/
  per-voice flag the consumer polls — 128 pitches is a fixed, tiny set).
- Keep one ring but have the **consumer** do all reclamation, with the producer
  only signalling pressure.
- Grow/size the ring so overflow is unreachable in practice — weakest option,
  since it converts a correctness bug into a tuning assumption; say so if you
  pick it.

Whatever you choose, the "stuck note" failure mode must be impossible by
construction, not merely unlikely.

## Rules that override anything you infer

1. Audio-thread constraints are absolute: no allocation, locks, `dynamic_cast`,
   `std::function`/`map`/`string`, GL, ImGui, file I/O, or `printf`. See
   `src/audio/AudioNode.h:6-12`.
2. Preserve the asymmetry of intent: note-ons may be dropped under overload,
   note-offs may not.
3. Keep `mOverflowCount` or an equivalent — silent overload is not acceptable.
   If it is not surfaced anywhere yet, say so; wiring it into the UI is 04's
   territory, not this session's.
4. The queue must remain usable unchanged by a future CoreMIDI-thread producer.
   That is the entire reason for this work — do not "simplify" it into a
   same-thread-only structure.
5. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. A `getenv("INFINITE_NOTEQUEUETEST")` fixture that drives the queue from a
   genuinely separate producer thread — the single-thread case cannot expose
   this — and asserts under sustained overflow that: no note-off is lost, no
   event is duplicated, and every note-on that was accepted is eventually
   matched by its off. Follow the existing convention (`INFINITE_DSPTEST`,
   `main.cpp:15300`) and register it in
   `.claude/skills/run-infinite-hygiene/driver.sh`.
2. Run that fixture against the **current** code first and show it failing or
   racing (under TSan if the toolchain allows: `-fsanitize=thread` on the one
   TU is acceptable as a throwaway local check, not a committed build mode).
   If you cannot make it fail, say so plainly rather than claiming a fix for an
   unreproduced bug — the analysis above is inspection, not observation.
3. No stuck notes in normal play: verify by ear or by meter that a held chord
   through MIDI Notes → any synth still releases cleanly.
4. `/run-infinite-hygiene` passes.
5. Update the comment block at `NoteEventQueue.h:13-20` so it describes the new
   guarantee, and note the fix in `docs/plans/audio/STATUS.md`.

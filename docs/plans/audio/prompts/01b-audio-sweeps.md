# 01b — the two generic audio sweeps

Scheduled here, not in P4, for the same reason README.md §5 gives for doing
P2's generic `AudioInputSlot` before the nodes: a sweep built now covers every
node added after it, including the thirteen in this plan. Built at the end, it
covers them only if someone remembers to enrol each one.

Both sweeps are already referenced as exit criteria by
`.claude/skills/new-audio-node/SKILL.md` §5 and by every node prompt in this
directory, and neither exists yet — so until this session runs, that criterion
is unverifiable.

Paste everything below into a fresh Claude Code session.

---

Implement the two generic audio sweeps specified in
docs/plans/audio/README.md §4 (P4) and §7, in Infinite
(/Users/namansoni/infinte).

| Sweep | Invariant |
|---|---|
| `AUDIOPARAMSWEEPTEST` | For **every** registered audio/note node type: every param registered through `VisitParams` survives a save/load round trip, and moving a param on the main thread reaches the audio thread's smoothed value within one block. Catches the "forgot to push it through the mailbox" class. |
| `AUDIOTEARDOWNSWEEPTEST` | For **every** registered audio/note node type: spawn it, wire it into a running graph, delete it mid-playback, keep rendering. No crash, zero xruns. This is `DELETECRASHTEST` for the audio graph. |

Both must enumerate node types **from the registry**, discovering audio and
note nodes off their interfaces (`IAudioSource`, `INoteSource`,
`AudioNodeForNotePorts`) the way `InputCountFor` already probes
`AudioInputSlot`/`NoteInputSlot` generically. A hand-maintained list of node
names defeats the entire point — a node added next month must be covered
without anyone editing this file.

Follow the existing fixture convention exactly (ARCHITECTURE.md §6): a
`getenv("INFINITE_…")` fixture in main.cpp printing a verdict line ending
`OK` / containing `FAIL` / ending `BUG`. `INFINITE_DSPTEST` (main.cpp ~9194,
dispatched ~9988) is the model — it renders the real node→AudioEngine chain
headless with no device. Do not introduce a second test mechanism.

Then register both with the hygiene skill rather than leaving them
standalone: add them to `.claude/skills/run-infinite-hygiene/`'s curated
check list and its coverage table, and give the audio sweeps their own driver
shaped like `.claude/skills/geometry-transform-sweep/driver.sh`.

Rules that override anything you infer:
1. Clean room: do not open, read, grep or reference
   /Users/namansoni/BespokeSynth.
2. On the audio thread: no allocation, locks, dynamic_cast, std::function/
   map/string, GL, ImGui, file I/O, or printf. The sweeps drive the engine
   from the main thread; assertions read through MeterRing, not by reaching
   into audio-thread state.

Exit criteria — report each explicitly:
1. Both sweeps run headless and print `OK` against the currently shipped
   audio nodes (Wavetable, Gain, Audio Out, Mixer, Splitter, MIDI Notes,
   Envelope, and Audio Filter if it has landed).
2. Deliberately breaking one node's mailbox push makes `AUDIOPARAMSWEEPTEST`
   print `FAIL` — demonstrate this, then revert. A sweep that passes because
   it checks nothing is worse than no sweep.
3. `/run-infinite-hygiene` passes and lists both new checks.
4. STATUS.md's P4 section updated.

# 01 — Audio lifecycle correctness

Three defects found by re-grading `../research-implementation-map.md` against
the code on 2026-08-14. All three live in the same seam: what happens to
already-built audio nodes when the *engine* starts, stops, or changes rate.
None is in the research map — it was written before the engine existed.

Paste everything below into a fresh Claude Code session.

---

Fix three audio-engine lifecycle bugs in Infinite (/Users/namansoni/infinte).

Read `ARCHITECTURE.md`, `src/audio/AudioEngine.h/.cpp`, `src/audio/ParamMailbox.h/.cpp`,
and `RebuildAudioTopology` in `src/main.cpp` before changing anything.

## Bug 1 — nodes are never prepared when the engine starts (worst of the three)

`RebuildAudioTopology` (`src/main.cpp:11819-11824`) is the only caller of
`PrepareToPlay(sampleRate, …)`, and it skips the call entirely unless
`sampleRate > 0.0`. It is invoked on cable edits, node removal and patch load
(`main.cpp:11063, 11877, 12223, 15571, 16045-16056`) — **never after
`AudioEngine::Start()`**.

Reproduce: load a patch, then press Start Audio. Every node's `ParamMailbox`
still has the sample rate and smoother time constants it was built with, not
the device's, until the user happens to touch a cable.
`ParamMailbox::PrepareToPlay` (`src/audio/ParamMailbox.cpp:3-8`) is what sets
those. The same hole reopens after every Apply-audio-settings restart
(`main.cpp:17348-17363`).

Fix so that a topology rebuild (or an equivalent explicit prepare pass) happens
after the engine has a real negotiated rate, not only on graph edits. Decide
deliberately whether `Start()` triggers the rebuild or the settings-apply path
does both, and write the reason down — the two call sites must not drift apart
again.

## Bug 2 — `mSampleRate` goes stale under a running engine

`AudioEngine::mSampleRate` is written only in `Start()`/`Stop()`
(`AudioEngine.cpp:47, 55`). Consumers read it at `main.cpp:6931, 8154, 11819,
17001`. A rate change while running leaves all of them on the old value. Fix
alongside bug 1 — the same prepare/republish path should be the single place
the rate propagates.

## Bug 3 — false xrun on restart

`mLastCallbackMs` (`AudioEngine.h:146`) is not cleared in `Stop()`, so the
first callback after a restart compares against a stale timestamp and trips the
`kXrunGapMultiplier = 1.5` heuristic (`AudioEngine.cpp:21-27, 172-178`). The
session counter `mXrunCount` (`AudioEngine.h:145`) is also never resettable,
which makes the status-bar readout (`main.cpp:17521-17548`) useless for "did
*this* change introduce dropouts". Clear the timestamp on stop; add a reset
path for the counter and decide whether Start/Stop resets it (say why in a
comment — the current always-cumulative behaviour was probably not deliberate).

## Rules that override anything you infer

1. On the audio thread: no allocation, locks, `dynamic_cast`,
   `std::function`/`map`/`string`, GL, ImGui, file I/O, or `printf`. The
   constraint list in `src/audio/AudioNode.h:6-12` is authoritative.
2. Preparing/rebuilding is **main-thread** work. Do not move any of it into the
   render callback to make the lifecycle simpler.
3. `AudioEngine`'s topology publish is an atomic swap with one-generation
   retire (`AudioEngine.h:66-74`). Do not break that to solve bug 1.
4. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. A new `getenv("INFINITE_AUDIOLIFECYCLETEST")` fixture in `main.cpp`,
   following the existing convention exactly (verdict line ending `OK` /
   containing `FAIL`; `INFINITE_AUDIOPARAMSWEEPTEST` at `main.cpp:15283` is the
   model), that asserts: after load-patch-then-Start, a node's mailbox reports
   the device sample rate; and a Stop/Start cycle adds no xrun.
2. That fixture **fails if you revert the bug-1 fix** — demonstrate it, then
   restore. A test that passes against the broken code is worthless.
3. Registered in `.claude/skills/run-infinite-hygiene/driver.sh`'s `TESTS`
   array and its coverage table.
4. `/run-infinite-hygiene` passes.
5. `docs/plans/audio/STATUS.md`'s P4/P5 hardening line updated. Note while you
   are there: that line currently claims the xrun counter is missing, which is
   wrong — it exists (`AudioEngine.h:78, 145`, UI at `main.cpp:17521-17548`).
   Fix that claim too.

# 02 — Device-change and sleep/wake recovery

Run after 01, which fixes the prepare-on-start path this session depends on.

The audio engine currently has **no notification observers of any kind**. A
repo-wide grep for `AVAudioEngineConfigurationChangeNotification`,
`NSNotificationCenter`, `addObserver` and `AudioObjectAddPropertyListener`
returns zero hits. Unplugging headphones, switching the system default output,
an external sample-rate change, or a sleep/wake cycle are all simply not
noticed, and nothing detects or restarts a dead engine. The xrun heuristic
fires once and then goes quiet, because no further callbacks arrive to measure.

This is the largest single gap between "works on my desk" and "a program
someone else can rely on".

Paste everything below into a fresh Claude Code session.

---

Make Infinite's audio engine survive device changes and system sleep
(/Users/namansoni/infinte).

Read `src/platform/Platform.mm:2098-2169` (the `AVAudioEngine` +
`AVAudioSourceNode` setup), `Platform.mm:2104-2145` (CoreAudio HAL property
writes, then rate read-back from `outputFormatForBus:0`), the manual
audio-settings path at `src/main.cpp:17245-17364`, and
`src/audio/AudioEngine.h/.cpp` — plus whatever 01 changed, since the recovery
path must reuse its prepare/republish work rather than duplicating it.

## What to build

1. **Configuration-change handling.** Observe
   `AVAudioEngineConfigurationChangeNotification` (and/or a CoreAudio default-device
   property listener — pick one and justify it) and drive a rebuild through the
   *same* code path Apply-audio-settings already uses
   (`main.cpp:17348-17363`), so there is one restart implementation, not two.
   The negotiated rate/buffer must be re-read and republished, not assumed
   unchanged.
2. **Sleep/wake recovery.** `NSWorkspace`'s will-sleep / did-wake
   notifications. On wake, verify the engine is actually still rendering and
   restart it if not.
3. **A liveness check that does not rely on the xrun heuristic.** Something
   that can distinguish "engine stopped" from "engine running fine" — the
   current `mLastCallbackMs` gap test cannot, because a dead engine produces no
   callbacks to compare.
4. **Honest UI state.** If the engine dies and cannot be recovered, the
   toolbar/status bar must say so rather than showing a stale healthy load
   figure (`main.cpp:17521-17548`).

There is one existing precedent worth copying rather than inventing around:
the per-frame `AudioInputCapturePump` (`Platform.mm:2020-2070`, rationale at
`Platform.h:252-258`) already reinstalls the *input tap* on whatever engine
currently exists. That is the shape of self-healing this codebase already
accepts — but it recovers the input tap only, not the output engine.

## Rules that override anything you infer

1. Notifications arrive on **arbitrary threads**. Nothing in the handler may
   touch the audio graph directly, and nothing may run render-thread-hostile
   work on the render thread. Marshal to the main thread and let the existing
   main-thread restart path do the work; `src/audio/AudioNode.h:6-12` is the
   authoritative constraint list.
2. Recovery must be **idempotent and rate-limited**. A device flapping, or a
   notification storm on wake, must not spawn overlapping restarts or an
   infinite restart loop. Say in a comment how you bounded it.
3. `Platform.mm` is compiled with ARC (`CMakeLists.txt`
   `set_source_files_properties … -fobjc-arc`). Observer tokens must be
   removed on teardown — a dangling observer on a freed engine is exactly the
   crash class this session is supposed to prevent.
4. Do not silently change the user's chosen device. If the user explicitly
   picked a device and it vanishes, decide and document the policy (fall back
   to default, or go silent and say so) rather than leaving it implicit.
5. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. Manual verification, actually performed and reported honestly — these cannot
   be faked headless: (a) start audio, unplug/switch the output device, confirm
   audio continues or recovers; (b) sleep the machine, wake it, confirm audio
   recovers; (c) confirm no crash and no restart loop in either case.
2. A `getenv("INFINITE_AUDIORECOVERYTEST")` fixture covering what *is*
   mechanisable: that the restart path is idempotent, rate-limited, and leaves
   nodes prepared at the new rate. Follow the existing convention
   (`INFINITE_AUDIOPARAMSWEEPTEST`, `main.cpp:15283`) and register it in
   `.claude/skills/run-infinite-hygiene/driver.sh`.
3. `/run-infinite-hygiene` passes, including the audio teardown sweep — the
   restart path touches the same topology-swap machinery it exercises.
4. `docs/plans/audio/STATUS.md` hardening line updated: device/sample-rate
   change and sleep-wake recovery move from left to shipped.
5. State plainly which of the four "what to build" items you did **not**
   complete, if any.

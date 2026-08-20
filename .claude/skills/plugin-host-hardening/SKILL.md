---
name: plugin-host-hardening
description: The strategy and diagnostic method for stopping third-party VST3/AU plugins from crashing Infinite. Use whenever a user reports that a specific plugin crashes/hangs/silences the app (Serum, Pigments, ZENOLOGY, Massive X, FabFilter, EZkeys and friends), whenever they paste a macOS crash report (.ips / "Translated Report") naming Infinite, when asked "why does this plugin crash", "why does AU work but VST3 doesn't", "why is this plugin silent", "can we make plugin hosting bulletproof", or before touching anything in PluginVST3.mm / Platform.mm's plugin paths. Explains what is already fixed, what is deliberately not guarded, and why "zero crashes" is an architecture decision rather than a patch.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`),
not this skill directory.

This work lives on the **`build/vst3-latest`** branch and is **private**
— see the `repo-privacy-split` skill before any `git push`. VST3 hosting
has never shipped to the public `origin`.

## The problem in one paragraph

Infinite loads third-party plugin binaries **into its own process**, sharing
one address space and one main thread. So a null deref inside Serum2, or an
`NSWindow` created off-thread by ZENOLOGY, is not "a plugin bug" from the
user's perspective — it's Infinite dying. There is no way to catch a
`SIGSEGV` after the fact and continue safely in general, because memory is
already suspect by then. Every in-process host has this problem: BespokeSynth
(JUCE-based, same architecture) has it open and unsolved in
[#1443](https://github.com/BespokeSynth/BespokeSynth/issues/1443) and
[#112](https://github.com/BespokeSynth/BespokeSynth/issues/112). Bitwig,
Ableton, and REAPER's sandbox mode avoid it by running **each plugin instance
in its own child process** with audio over shared memory and the GUI proxied
over IPC. That is the only complete fix, and it is a multi-day architectural
project, not a patch.

**Do not promise "no errors" without that change.** Say what is actually
true: crashes we cause are fixed, crashes plugins cause are contained at
specific call sites, and the rest needs out-of-process hosting.

## The three tiers (know which one a given fix belongs to)

**Tier 1 — crashes *we* cause. Always fix these properly.** These are our
bugs, not the plugin's, and they are the majority of what users hit. Signature:
the crash is inside AppKit/Qt/CoreAudio, on a non-main thread, reached from
our code. Root cause is almost always *calling plugin code on the wrong
thread* or *skipping a spec-required call*.

**Tier 2 — crashes the *plugin* causes, at a call site where "the call
failed" is a safe outcome.** Contain with `RunPluginCallGuarded`. Only valid
for narrow, synchronous, main-thread, non-realtime calls with no in-flight
audio and no half-applied graph mutation riding on them.

**Tier 3 — everything else.** Realtime `process()`, instantiation itself,
anything that corrupts rather than faults cleanly. **Not guardable.** Needs
out-of-process hosting. Do not extend the signal guard into these — catching
a signal mid-`process()` with half-written audio buffers is not a safe thing
to shrug off, and a guard there creates the illusion of safety without it.

## Triage: read the crash report before theorising

The user will usually paste a full `.ips` / "Translated Report", or you can
find one in `~/Library/Logs/DiagnosticReports/Infinite-*.ips`. Read it
literally — it names the culprit, you do not need to guess.

1. **Find the faulting thread** (`Triggered by Thread: N`) and read its
   frames bottom-up. The boundary matters more than the top frame: find the
   last `Infinite` frame — that is the call *we* made — and everything above
   it in the plugin's binary is the plugin's own code.
2. **Check the dispatch queue on the faulting thread.** If it is *not*
   `com.apple.main-thread` and the stack touches AppKit, Qt, or window
   creation, it is **Tier 1 — our threading bug**, regardless of which plugin
   surfaced it.
3. **Read the exception type:**
   - `EXC_BAD_ACCESS (SIGSEGV)` at a small address (`0x60`, `0xb0`) →
     null-pointer-plus-field-offset deref. If it is inside the plugin image,
     it is the plugin's own bug → Tier 2 candidate.
   - `EXC_CRASH (SIGABRT)` with a `Last Exception Backtrace` containing
     `objc_exception_throw` → uncaught ObjC exception. Nearly always AppKit
     used off the main thread → Tier 1.
   - `EXC_BREAKPOINT (SIGTRAP)` in `dispatch_assert_queue_fail` → a bundled
     Qt runtime asserting it is not on the main thread → Tier 1.
4. **Check `Binary Images` for the culprit's real identity** — the plugin's
   bundle path and version. Several vendors ship a shared framework
   (Kilohearts `HeartCore`, NI's namespaced Qt) that is the actual crashing
   image while a different plugin name appears in the stack.
5. **Only then** open the named function in our source and decide the tier.

A crash report tells you the tier in about two minutes. Skipping this and
guessing at a fix has been wrong every time it was tried.

## What is already fixed (do not redo)

**Tier 1, all shipped:**

- **Out-of-process *scanning*** — `--vst3-scan-bundle` re-execs the app
  binary per bundle, wire protocol over a pipe, `select()` timeout, `waitpid`,
  immediate same-session blocklisting on crash/hang. A crashing plugin can no
  longer break a scan. Entry point: `src/main.cpp:23208`.
- **Scan children no longer appear as extra Infinite windows** —
  `Platform::SuppressAppUIForScanChild()` (`src/platform/Platform.mm:3248`)
  claims the `NSApplication` singleton with
  `NSApplicationActivationPolicyProhibited` before any plugin code runs, so a
  plugin's lazy `[NSApplication sharedApplication]` can't make the child
  visible. Called first thing in the child at `src/main.cpp:23214`.
- **VST3 bus activation** (`src/platform/PluginVST3.mm:1598`) — the spec
  requires `IComponent::activateBus` before processing; without it, strict
  SDK-based plugins (FabFilter et al.) correctly emit silence while `process()`
  still returns `kResultOk`. This was the entire "VST3 is silent but AU works"
  class of report. Lenient JUCE plugins ignore the omission, which is why it
  went unnoticed.
- **Main-thread instantiation, both formats.** VST3 create dispatches to
  `dispatch_get_main_queue()` (`src/platform/PluginVST3.mm:1368`), not a
  global queue. AU create uses synchronous
  `-initWithComponentDescription:options:error:`
  (`src/platform/Platform.mm:3311`), *not* the async
  `instantiateWithComponentDescription:completionHandler:` variant, whose
  handler runs on an arbitrary queue. This fixed ZENOLOGY (`NSPanel` off-main
  → `SIGABRT`) and the Qt-based NI plugins (`dispatch_assert_queue` trap).
  **Accepted tradeoff:** instantiation now briefly blocks the UI thread. That
  is what every compliant host does; the old always-background design was the
  non-standard part and the direct cause of this whole crash class. Do not
  "optimise" it back onto a background queue.

**Tier 2, shipped:** `RunPluginCallGuarded()`
(`src/platform/PluginVST3.mm:2135`) — `sigsetjmp`/`siglongjmp` with a
`SIGSEGV`/`SIGBUS`/`SIGILL` handler, wrapping exactly two call groups:

| Call site | Flag set on crash |
|---|---|
| `createView` / `getSize` / `attached` (editor open) | `editorUnstable` |
| `getState` / `setState` (save, restore, **every undo checkpoint**) | `stateCallsUnstable` |

The flags are per-instance and independent — Serum2 has a real bug in *both*
paths, and a plugin can have a broken editor with a fine `getState()`. Once
set, that call class is skipped outright rather than retried; a plugin that
faulted once will fault again on the next checkpoint.

Note the state path is reached from `Patch::SaveParams` →
`(anonymous namespace)::BuildPatchData()`, which runs on undo checkpoints —
i.e. on nearly every graph edit, not just explicit saves. That is why a
latent `getState()` bug reads to the user as a random background crash.

## Still open

- **EZkeys 2 `OSStatus -10875`** — never reproduced or diagnosed.
- **FPS drop with plugins loaded** — hypothesis only: plugin editors are real
  `NSWindow`s sharing the host's main-thread run loop with the GLFW/ImGui
  render loop, so continuously-animating GUIs (FabFilter analyzers, XO meters)
  cost host frames. **Unconfirmed** — needs an editor-open vs. editor-closed
  comparison before it is stated as fact.
- **Tier 3 generally** — a realtime-thread crash in any plugin still takes the
  app down. Only out-of-process hosting fixes it.

## Working rules

1. **Get the crash report first.** Do not propose a fix for a plugin crash
   you have not seen a stack for.
2. **Name the tier explicitly** when reporting back, and never describe a
   Tier 2 containment as if it fixed the plugin. It did not — the plugin is
   still broken, we just stopped it taking us with it.
3. **Never extend the signal guard to realtime or instantiation paths.**
4. **Do not blame the format.** "AU works, VST3 doesn't" (or the reverse) has
   so far *always* been our bug in one path — a missing spec call or a wrong
   thread — not a property of the format. Check both paths for asymmetry
   before concluding anything about the plugin.
5. **Build, then copy to Desktop** — the user tests from `~/Desktop`, and a
   stale bundle there has wasted a debugging round before:
   ```bash
   cd build && ninja && pkill -f "build/Infinite.app/Contents/MacOS/Infinite"; rm -rf ~/Desktop/Infinite.app && cp -R build/Infinite.app ~/Desktop/Infinite.app
   ```
   Incremental `ninja` is fine and correct for these files; only do
   `ninja -t clean` first if stale artifacts are actually suspected.

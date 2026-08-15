# 06 — RealtimeSanitizer build mode

> **Read this section before starting. This session is blocked on a
> prerequisite you must satisfy first, and a previous session already
> investigated and deferred it — do not rediscover that from scratch.**

`docs/plans/audio/cook-rate-decision.md:61-68` records the blocker: **Apple
Clang 21 rejects `-fsanitize=realtime`**, no Homebrew LLVM is installed, and it
says in terms: "Do not attempt to wire up `INFINITE_RTSAN` until then." RTSan
needs LLVM 19+ from Homebrew (`brew install llvm`), i.e. a **second compiler
toolchain**, not a flag.

This is also the one opt-in mode that **cannot** follow the project's
established pattern. Every other `INFINITE_*` mode is a `getenv` gate compiled
into the normal binary plus an entry in
`.claude/skills/run-infinite-hygiene/driver.sh`'s `TESTS` array — about 60 of
them (`main.cpp:15283, 15300, 16041`, driver at `:145`). RTSan is a separate
build configuration with a different compiler. Expect friction: the project
builds a universal `arm64;x86_64` macOS bundle with a FetchContent GLFW, ARC
Objective-C++, and ad-hoc codesigning (`CMakeLists.txt`), none of which is
guaranteed to work unchanged under Homebrew LLVM.

**Why do it anyway:** `src/audio/AudioNode.h:6-12` *asserts* the real-time
constraints and 16 DSP kernels plus ~40 audio-capable nodes *claim* to honour
them. Nothing verifies that. The research map filed this when there were zero
audio nodes, calling it "cheaper to catch a violation with zero audio nodes
than with twenty" — that argument has inverted into urgency, not gone away.

**Judgement call before you begin:** if wiring the second toolchain turns into
a yak-shave (universal-binary or framework-linking failures under Homebrew
LLVM), **stop and report** rather than restructuring the build. An
arm64-only, non-bundle, audio-TU-only sanitizer target is a perfectly good
outcome. Do not destabilise the shipping build to gain a diagnostic.

Paste everything below into a fresh Claude Code session.

---

Add an opt-in RealtimeSanitizer build mode to Infinite
(/Users/namansoni/infinte), to verify the audio thread actually honours the
constraints `src/audio/AudioNode.h:6-12` asserts.

First read `docs/plans/audio/cook-rate-decision.md:61-68` (why this was
deferred), `../research-implementation-map.md` §1.3 (the original ask), and
`CMakeLists.txt` in full.

## Steps

1. Confirm the prerequisite: is an LLVM 19+ `clang++` available? If not, install
   Homebrew LLVM. **Report the exact version you ended up using** — RTSan's
   flag and attribute spelling have been in flux, so verify the current syntax
   against the installed toolchain rather than assuming
   `-fsanitize=realtime` / `[[clang::realtime]]` / `[[clang::nonblocking]]`.
2. Add an `INFINITE_RTSAN` CMake option that builds with that toolchain and
   compiles the audio translation units with the sanitizer. Default **off**; it
   must not affect the normal build at all.
3. Mark the render-block entry point(s) with whatever attribute the installed
   version wants — start at `AudioEngine`'s callback and `RunTopology`
   (`AudioEngine.cpp:141, 188-199`), and the `AVAudioSourceNode` `renderBlock`
   in `Platform.mm:2149-2169`.
4. Run it against a real patch with audio playing, exercising the heavy kernels
   (Reverb's FDN, Sampler, Wavetable, Stutter) and the note path.
5. Fix what it finds, or — if a finding is a false positive or a deliberate,
   understood exception — document it in place with the reason. Do not silence
   findings wholesale to get a clean run.
6. Wire it as an opt-in check alongside `run-infinite-hygiene`, **not** a
   default. It is slow and exists to catch violations before they are audible.

## Rules that override anything you infer

1. The normal build must be untouched: same universal `arm64;x86_64` bundle,
   same GLFW FetchContent, same ARC on `Platform.mm`, same ad-hoc codesign. If
   the sanitizer build needs to relax any of these, relax them **only** in the
   `INFINITE_RTSAN` configuration and say which.
2. If RTSan reports a violation in code you did not write this session, that is
   a **finding, not a nuisance**. Report it prominently even if you cannot fix
   it — that is this session's entire product.
3. Do not "fix" a violation by moving work off the audio thread in a way that
   changes audible behaviour or adds latency, without flagging the tradeoff.
4. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. The LLVM version used, and the exact flag/attribute spelling that worked.
2. The normal build still produces a working signed universal bundle — verify by
   building and launching it, not by assuming.
3. `INFINITE_RTSAN` builds and runs with audio playing, and you report
   **verbatim** every violation found, with a fixed / documented-exception /
   unresolved disposition for each. A clean report is only credible alongside
   evidence the sanitizer was actually active — demonstrate it firing on a
   deliberately introduced violation (e.g. a temporary `malloc` in a kernel's
   `ProcessBlock`), then revert.
4. `/run-infinite-hygiene` still passes on the normal build.
5. `docs/plans/audio/STATUS.md` and `cook-rate-decision.md:61-68` updated — the
   latter currently instructs future sessions not to attempt this, and must not
   keep saying that once it is done.
6. If you stopped early per the judgement call above: say exactly where you
   stopped, what failed, and what a future session would need.

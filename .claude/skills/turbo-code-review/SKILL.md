---
name: turbo-code-review
description: Review Infinite-Turbo (Windows) changes against the Turbo rules on top of upstream's four standards - patch-file compatibility, upstream-verbatim code, GPU-first PointZ with no CPU wait per frame, CPU->GPU equivalence, slider ordinals, editions and licences, perf gate. Use for every diff on turbo/next before declaring a task done, when asked to "review", "check this", "is this ready", or after another session or AI wrote code for Infinite-Turbo.
---

# turbo-code-review

Run the upstream standards first: `.claude/skills/infinite-code-review/SKILL.md` (accuracy,
experimentality, design, quality). Its paths say `/Users/namansoni/...`: read them as this repo's
root. Then check every rule below on the diff (`git diff` against `turbo/next`'s base, plus
untracked files). Report only verified findings, each with file:line and a concrete failure
scenario; say plainly when a rule was not checkable.

## T1 Patch files (blocker)

- No saved type name or param key renamed or removed. A rename goes through
  `NodeFactory::CanonicalName` only.
- New params at the END of `VisitParams` and at the end of the slider list: the slider ordinal
  is a modulation / MIDI-learn / gesture / Performance key. Conditional sliders (shown per mode)
  must not shift the ordinals of the ones after them; prefer a stable key (`saveKey`).
- Defaults of new params reproduce the old behaviour when an old patch loads (the key is
  missing).
- Unknown keys still round-trip (patchcompat reports them).

## T2 Upstream code (blocker)

Files inherited from Naman stay verbatim: no reformatting, renames, reorders or moves. A Turbo
change inside an inherited file is minimal, marked `// Turbo 0.70:` and listed in the C1 map.
Performance changes to inherited modules must not change their output.

## T3 GPU-first, no CPU wait per frame (blocker on per-frame paths)

Search the diff for `glReadPixels`, `glGetTexImage`, `glGetBufferSubData`, `glMapBuffer*` with
read access, `glFinish`, `glClientWaitSync` with a timeout, `glGet*` (state queries),
`glGetQueryObject*` without `GL_QUERY_RESULT_AVAILABLE`, `->Cpu()` and `.Read(` on PopData.
Each one on a per-frame path must be replaced by `pop::AsyncRows` (`src/pop/PopGpu.h`),
`GLUtil::AsyncReadback` (`src/core/GLUtil.h`), fences polled without waiting, or a cached state.
Allowed: load time, tests, user actions (export, save), or a documented one-frame-late result.

## T4 CPU -> GPU equivalence (blocker for PointZ migrations)

- An env var runs both paths and compares attribute by attribute (Copy PointZ pattern); max diff
  0 or a documented tolerance; the CPU path stays as fallback.
- RNG changes (e.g. GPU particles) are documented and a legacy mode exists.
- `docs/pointz-gpu-status.md` row added or updated.

## T5 GPU resources

- Every created GL/CUDA/OptiX object has an owner and is released; no growth per frame (pools and
  arenas bounded, a VRAM warning when a limit is hit).
- Buffers reused only after the GPU is done with them (fence or 2-frame delay, F5).
- GL state changed is restored through the project's guard (GlState once F4 lands), never left
  dirty for the next node.
- New GPU work carries `glPushDebugGroup` / `glObjectLabel` names (F3).
- Shaders added in this diff live in `shaders/*.glsl` (A5), unless they extend an existing
  embedded kernel.

## T6 Editions and licences

- Full-only code (GPL, ASIO, RTX/OptiX, NDI, DLSS) is behind its CMake option and the MIT build
  still compiles (stub path, e.g. `src/nodes/RtxStub.cpp`).
- New third-party code: licence compatible with its edition, entry in `THIRD_PARTY.md` (version,
  licence, edition), proprietary SDK headers fetched at configure time, not committed.

## T7 Perf and tests

- The gate in `turbo-branch-workflow` ran: suite, patchcompat, perf. Quote the numbers.
- A new node or operator has a self-test or is covered by a sweep (audio-node-sweep,
  geometry-transform-sweep, PORTEDTEST, POPTEST).

## T8 Codebase idiom

3-space indent, braces on their own line, `// Turbo 0.xx:` comments explaining why, comment
density like the surrounding code. main.cpp fragments: keep nesting shallow (MSVC limit; see the
`DrawPortedNodeParams` dispatch). Strings with `\n` in C++ edited with the Edit tool, not shell
heredocs.

## Report

Group by severity: blocker (T1-T4, crashes, leaks per frame), should-fix, nit. For each:
file:line, rule, what breaks and when, the fix. End with what was not verified.

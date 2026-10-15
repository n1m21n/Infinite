---
name: patch-variation
description: Mutate real user patches (parameter nudges and extremes, odd sizes / sample rates / tempos, a node bypassed or deleted, a cable removed) through the headless engine and find what crashes, hangs, goes NaN or stops being deterministic. Use when asked to "fuzz the corpus", "run the patch variation test", "stress the community patches", before a release (protocol category A), or after an engine change that could affect old patches.
---

# Patch variation (smoke slice)

Tool: `tools/release/patch_variants.py`. Python only, never builds, serial, one engine process at a time. It uses the binary the owner already has (`build-check/Infinite.app`, else `build/Infinite.app`; override with `--bin`) and reports which one.

## Before you run

1. `pgrep -fl "cmake --build|make |clang"` and look for a running Infinite app. The tool refuses (exit 2) if either exists; do not work around it with `pkill`. Ask the owner to close the app or pick a quiet moment.
2. Do not touch the originals: variants are written to a temp folder. The corpus is `website/assets/community/*.inf` (path-blanked, manifest `tools/website/community.json`) plus `assets/examples/**/*.inf`.
3. Not a benchmark: elapsed time is only compared with the unmutated patch and only as a FLAG.

## Commands

```
python3 tools/release/patch_variants.py --list                     # dry run: patch, mode, the operators each variant would use
python3 tools/release/patch_variants.py --only delay-wash --variants 3
python3 tools/release/patch_variants.py                            # smoke slice: whole corpus x 5 variants
python3 tools/release/patch_variants.py --ops env,struct --seed 7  # other operator mix / another seed
python3 tools/release/patch_variants.py --self-test
```

Options: `--corpus DIR` (repeatable), `--variants K`, `--seed N`, `--ops` (nudge min max random enum size rate bpm duration fps bypass delete uncable, or groups params/env/struct), `--timeout S`, `--only SUBSTR`, `--bin PATH`, `--date`, `--version v0.6`, `--no-findings`, `--no-determinism`, `--no-canon`.

## What it does

- Variant `n` of a patch has seed `sha256(seed|patch|n)`; the same seed always gives the same operators. Re-running skips seeds already in `docs/release/runs/<date>/variants.tsv`, so an interrupted run resumes. The TSV holds the operators as JSON, so any row can be replayed.
- Each variant combines 1-3 operators: parameters via `--set node.param=value` (range and enum values come from `--describe`, never outside the documented range), environment (`--size`, `--sample-rate`, `--bpm`, `--duration`, `--fps`), and structural text edits (bypass or delete a node, remove a cable).
- Image patches run `--frame` (or `--frames-dir` when fps/duration is varied); audio patches run `--audio-summary --wav`; patches with both alternate by variant. Patches with neither are logged and skipped. Strict `--validate` runs first; only patches that fail it get `--lenient`, and they are named in the output.
- Hard checks: exit 0 (3 invalid, 4 hardware, 2 rejected argument are skips, logged), no signal, no exit 6 or wall-clock hang, final JSON line, no NaN/inf in frame_stats or audio summary, output size / rate / frame count as requested, audio peak bounded, `--canonicalize` twice identical and stable, same variant twice byte-identical (`tools/determinism-by-design.txt` honoured). FLAGs: slow against the unmutated run, large peak.
- On a failure the minimiser drops operators one at a time while the same check keeps failing, saves the smallest repro `.inf` in `docs/release/runs/<date>/repro/`, prints the one engine command that reproduces it, and files one finding per distinct failure (category A, `found by patch_variants.py`, title tagged `[pv:xxxxxxxx]` so reruns do not duplicate it).

## Severity guess

Crash, hang or NaN: S1, and S0 when the unmutated patch fails the same way. Wrong output size, nondeterminism, canonicalize drift, undocumented exit: S2. Triage still decides; this is only the first guess.

## Reading the result

A pass is "nothing broke on these seeds", not "robust". An argument rejected with exit 2 or a variant refused with exit 3 is the engine saying no cleanly; look at the `note` column before calling it a gap. Failures found are findings, not fixes: do not edit engine code from this skill.

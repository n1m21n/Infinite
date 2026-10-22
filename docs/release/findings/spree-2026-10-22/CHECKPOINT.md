# Spree checkpoint, 2026-10-22 (updated late in the day)

Resume from here. Everything below is merged into local **main**. Nothing is pushed.

The raw run logs are in `docs/release/runs/spree-2026-10-22` (not committed). Session sweep output is in the session scratchpad (`sigrun*/`, `bf3/`, `fz/`). Use the kit in `tools/bughunt/` to re-create any of it.

## Where things stand

- **Findings:** 42 filed (B-A-001 … B-K-024). Every one is fixed except three:
  - B-G-001 and B-H-001: P3, need a quiet-Mac re-run.
  - B-J-001: the Windows headless hang, P5.
- **Since the morning checkpoint:**
  - **B-K-020:** a damaged WAV/AIFF/W64 chunk size hung the decode.
  - **B-K-021:** a HAP sample table allocated gigabytes.
  - **B-K-022:** a WAV header's frame count was trusted.
  - **B-K-023:** one NaN/inf block latched 17 effects silent until reload.
  - **B-K-024:** a decoded file's NaN/inf samples were played as-is.
  - **Plus AudioSummary:** non-finite frames are now counted, as `non_finite_frames` in `--audio-summary`. Before this, NaN was invisible to every audio check.
- **The bug-hunt kit (`tools/bughunt/`, see its README)** is merged. It contains:
  - decoder fuzzers, with a macOS driver and `-timeout`;
  - hardened libc++ in sanitizer builds (`INFINITE_HARDEN`);
  - a coverage build;
  - `code_lint.py` and `triage.py`;
  - `param_fuzz.py`, with the kinds int, float, text, mod and signal.
- **The signal sweep** (every audio-in, audio-out type; nan/inf/denorm/±1e6/DC/square bursts) is clean on main@f19fa7f2:
  - Every effect is NANPASS: NaN only while the burst plays.
  - Every file player gets the signal.
  - Grain Molder is skipped, since it only has a record-armed input.
- **DSPTEST:** OK, including two new checks. "effect recovers from a NaN/inf block" covers all 28 effects; "decode zeroes NaN/inf samples" is the other.

## In progress

- **TSan build** in `worktrees/main/build-tsan` (`-DINFINITE_SANITIZE=thread`). Next, run headless audio cases and DSPTEST under it, then triage the races.

## Not yet run

1. `param_fuzz.py --kind float`, `--kind text` and `--kind mod` over every type, on an ASan+UBSan build of current main. build-asan needs a rebuild first, since it predates today's merges.
2. A coverage report from the fixed coverage build. Wipe the stale `$SP/prof` first.
3. Re-time Join Geometry, Revolve 100000 and Remove Background alone on a release build, to confirm they are slow and not hangs.

## Leads, not yet filed

- Headless renders of Molder are not reproducible. Its analysis runs on a worker thread, which `--audio-summary` doesn't wait for, and it seeds from `random_device`. The same patch went silent for 0, 1 or 3 s across three runs. Probably belongs with the census's "nondeterministic" tag rather than being a bug. Decide before filing.

## Decisions waiting on the owner

1. **Push:** whether to push main.
2. **Go or no-go on P3 and P5:** B-G-001 and B-H-001 (benches), and B-J-001 (Windows).
3. **The old plan file:** delete claude-context/v06-release/RELEASE_BUGHUNT_PLAN.md, or mark it superseded.
4. **Linux-only sanitizers:**
   - LeakSanitizer, with a suppressions file instead of `detect_leaks=0`.
   - RealtimeSanitizer on the audio callback.
   - Real libFuzzer.

   Apple clang has none of these, so each would be a Linux CI job.

## Rules in force

- One `bugfix/` branch per fix, in its own worktree, cut from main. Run a blast-radius scan before each fix.
- One build at a time. App-launching tests run serially. Kill specific PIDs only.
- Merging to local main is authorized. No push without the owner's word. No new nodes, modes or panels without approval.

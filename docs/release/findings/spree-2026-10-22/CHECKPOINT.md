# Spree checkpoint, 2026-10-23 (morning)

Resume from here. Everything below is merged into local **main**. Nothing is pushed.

The raw run logs are in `docs/release/runs/spree-2026-10-22` (not committed). Session sweep output is in the session scratchpad (`sigrun*/`, `bf3/`, `fz/`). Use the kit in `tools/bughunt/` to re-create any of it.

## Where things stand

- **Findings:** 46 filed (B-A-001 … B-K-028). Every one is fixed except three:
  - B-G-001 and B-H-001: P3, need a quiet-Mac re-run.
  - B-J-001: the Windows headless hang, P5.
- **New on 2026-10-23** (all merged to main):
  - **B-K-025:** reloading a Molder patch re-rolled its seed and generation, so a saved patch never came back the same. This was the "Molder not reproducible" lead.
  - **B-K-026:** patch values past int range (±1e30, inf, 3e9) hit float-to-int UB in 30+ nodes. Now the patch reader drops floats past ±2³¹ and saturates ints. Unsigned seed casts and sample-count products are saturated where they're cast (Simulation, FieldRandom, NoteNodes, GeometryTable).
  - **B-K-027:** Palette seeds of 2 and up all mapped to the same RNG seed, so every seed gave the same palette.
  - **B-K-028:** a NaN phase in Wavetable/Wavetable Shaper was UB, `(int)NaN`. This was found by DSPTEST under ASan.
- **Since the 2026-10-22 morning checkpoint:**
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

- **TSan:** `build-tsan`, run with `tools/bughunt/tsan_selftests.sh` over 14 live-engine selftests. 0 warnings. A canary confirmed reports are logged. CONVOLUTIONTEST fails only its CPU-cost budget, as expected under TSan.

- **Float sweep** (main@6f79f6f6, ASan+UBSan, 305 types): 181 BAD rows, mostly float-to-int UB, which is B-K-026. A re-run with the fixes plus new values 1e9 and -1 is in progress on the verify build (`verify/spree-batch3`).

## Not yet run

1. Finish the float re-sweep and triage its 1e9/-1 rows. These are values that fit an int but sit outside a param's range, which can size a buffer or index past the end.
2. `param_fuzz.py --kind text` and `--kind mod` over every type, on the ASan+UBSan build.
3. The signal sweep again, on the ASan+UBSan build. The earlier sweep missed B-K-028.
4. A coverage report from the fixed coverage build. Wipe the stale `$SP/prof` first.
5. Re-time Join Geometry, Difference, Intersect, Revolve 100000 and Remove Background alone on a release build, to confirm they are slow and not hangs.

## Leads, not yet filed

- **Huge param values make audio NaN or hot** (float sweep, before the fixes):
  - NANAUDIO: Gain 1e30, Mixer 1e30, Analog ±1e30, Velocity Curve -1e30.
  - HOTAUDIO: Oscillator ±1e30, Stereo 1e30.

  Check these again after the re-sweep. Those values are now dropped on load, but they can still be typed into the inspector or reached through an expression.
- **CSG under ASan:** Difference and Intersect time out on the base row, and Union takes about 92 s. Re-time them on a release build (item 5 above).
- **Molder render timing:** the analysis still runs on a worker thread, and `--audio-summary` doesn't wait for it. Its seed is now restored on reload (B-K-025).

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

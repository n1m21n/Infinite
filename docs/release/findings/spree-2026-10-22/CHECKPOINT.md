# Spree checkpoint, 2026-10-23 (afternoon)

Resume from here. Everything below is merged into local **main**. Nothing is pushed.

The raw run logs are in `docs/release/runs/spree-2026-10-22` (not committed). Session sweep output is in the session scratchpad (`sigrun*/`, `bf3/`, `fz/`). Use the kit in `tools/bughunt/` to re-create any of it.

## Where things stand

- **Findings:** 53 filed (B-A-001 … B-K-035). Every one is fixed except three:
  - B-G-001 and B-H-001: P3, need a quiet-Mac re-run.
  - B-J-001: the Windows headless hang, P5.
- **New on 2026-10-23, afternoon** (from the float sweep's 1e9 rows; all merged to main):
  - **B-K-031:** effect params from a patch, OSC or RPC skipped the effect's range; Stereo width 1e9 made the output enormous. AudioEffectNode now clamps to the EffectDef ranges.
  - **B-K-032:** Gain, Mixer and Audio In gain past ~770 dB gave NaN; a Velocity Curve curve below 0.25 sent 1/velocity downstream.
  - **B-K-033:** Wavetable and Oscillator volume (and other engine params) skipped the knob range; volume 1e9 played ~1e9 loud.
  - **B-K-034:** Analog params at ±1e30 or 1e9 made the voice NaN.
  - **B-K-035:** filter params skipped the FilterDef range; dither mode 1e9 looped a billion times per pixel and the GPU hang aborted the app. FilterNode now clamps to the def ranges.
  - **A central clamp on load was tried and dropped.** A knob range is not a param's domain: ranges change with mode, and some writers go past the knob on purpose. Each fix clamps in the node, after `clamp_check.py` shows no saved patch is outside the new range.
  - **New tools:** `clamp_check.py` (does any tracked patch hold a value outside a proposed range; 0.3 s for 855 patches) and the `shader-loop` rule in `code_lint.py` (GLSL loops bounded by a non-literal).
- **New on 2026-10-23, morning** (all merged to main):
  - **B-K-025:** reloading a Molder patch re-rolled its seed and generation, so a saved patch never came back the same. This was the "Molder not reproducible" lead.
  - **B-K-026:** patch values past int range (±1e30, inf, 3e9) hit float-to-int UB in 30+ nodes. Now the patch reader drops floats past ±2³¹ and saturates ints. Unsigned seed casts and sample-count products are saturated where they're cast (Simulation, FieldRandom, NoteNodes, GeometryTable).
  - **B-K-027:** Palette seeds of 2 and up all mapped to the same RNG seed, so every seed gave the same palette.
  - **B-K-029:** a Molder patch with generation 2^31 froze the UI for about 7 minutes on load and on every re-render. The generation is now capped at 10000.
  - **B-K-030:** Note Echo with more than 15 repeats, from a file or remote set, left notes stuck because voices shared repeat ids. Repeats and transpose are now clamped to the knob ranges.
  - Both were found by auditing int params whose UI range the node never enforces. The int sweep passed both nodes because it gives them no file and no notes, so the loops never ran. The next step for the kit is a stimulus per kind.
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

1. Finish the float re-sweep (paused, resumable, about 1,525 rows done) and triage its 1e9/-1 rows. Re-run the B-K-031..035 rows on the fixed build first.
   - A faster route is an in-process headless param fuzz (seconds instead of hours) for the NaN and hot-audio classes.
2. `param_fuzz.py --kind text` and `--kind mod` over every type, on the ASan+UBSan build.
3. The signal sweep again, on the ASan+UBSan build. The earlier sweep missed B-K-028.
4. A coverage report from the fixed coverage build. Wipe the stale `$SP/prof` first.
5. Re-time Join Geometry, Difference, Intersect, Distribute Points on Faces (density 1e9 gives the full 200k-point cap; it timed out under ASan), Revolve 100000 and Remove Background alone on a release build, to confirm they are slow and not hangs.

## Leads, not yet filed

- **Huge param values make audio NaN or hot** (float sweep, before the fixes):
  - NANAUDIO: Gain 1e30, Mixer 1e30, Analog ±1e30, Velocity Curve -1e30.
  - HOTAUDIO: Oscillator ±1e30, Stereo 1e30.

  Check these again after the re-sweep. Those values are now dropped on load, but they can still be typed into the inspector or reached through an expression.
- **Render 3D and Fit at 1e9:** E_RENDER, an uncapped FBO size. S4.
- **paletteOffset overflow** at GeometryOpNodes.cpp:1276. S4.
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

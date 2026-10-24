# Spree closed, 2026-10-24

**The spree is closed.** Everything is merged into local **main**. Nothing is pushed: pushing needs the owner's word.

## Final state

- **60 findings** (B-A-001 … B-K-042). 56 are fixed. Four are not fixed (B-K-041 is wontfix):

  | Finding | Decision |
  |---|---|
  | B-G-001, B-H-001 (P3 timing) | Moved to the perf initiative (docs/plans/perf/README.md). A quiet-Mac re-run is not possible while other sessions build. |
  | B-K-041 (Analog/Oscillator resonance 1 on SVF LP36 peaks +28 dBFS) | Wontfix: knob at max, finite; a fix changes saved patches' sound. |
  | B-J-001 (Windows headless hang) | Deferred: the self-test hang is gone, and the smoke and render steps are re-checked on the next push. See the finding. |

- **Closing work on 2026-10-24:**
  - **B-K-039:** Sampler start, end and position, and Bouncing Balls ballSize, at 1e9 hit float-to-int UB (UBSan abort). Clamped in each node, and ReadSample also guards its position.
  - **CI fix merged:** `bugfix/ci-selftest-failures` (4ad61191) fixes Windows RESTARTTEST, a gcc13 miscompile, a Linux ASan clamp abort in Sampler and the PNG chunk order. This covers all 4 red main jobs; it is unconfirmed until a push.
  - **Tools:**
    - `code_lint.py` has a new rule, `clamp-bounds`: `std::clamp` with two runtime bounds aborts under the Linux hardened library but not on Mac.
    - `param_fuzz.py` has three new flags:
      - `--values` picks only the values to run.
      - `--budget MIN`, default 20 min, caps wall time and is resumable.
      - After 3 nodes time out on the same value, the rest get a 5 s probe.
- **Final sweep (float at 1e9 and -1, part of signal), 1116 rows.** It found the B-K-039 crashes. The other non-ok rows were triaged and are not bugs:
  - **Saturator, Transient Shaper (hot audio):** this is the knob at max (+12 dB output, +24 dB attack). The defs already clamp, and the output stays finite.
  - **28 shape nodes plus Text and Texture (timeout under ASan):** on release they take 3.7 s at the B-K-038 8192² cap. The time is ASan slowdown, not a hang.
  - **Difference, Intersect, Distribute Points on Faces (slow under ASan):** these are perf, not crashes.
  - **Projection (E_RENDER):** setting every warp point to 1e9 collapses the mesh. The app reports a handled "no image" error.
- **Dropped:**
  - The coverage report: it only measures.
  - ~~The mod and text sweeps~~ **Reopened and run on 2026-10-24** (see "Mod and text sweeps" below). The earlier line was wrong: B-K-026 only guards numbers coming from a patch, OSC or RPC.
  - LSan, RTSan and libFuzzer: Linux-only, follow-ups for a later spree.
  - The old RELEASE_BUGHUNT_PLAN.md: superseded by this folder.
- **Lessons from the spree:** see the end of `tools/bughunt/README.md`. Before any long job, budget it: estimate rows × seconds and what it can find that the last run did not, then stop once new rows only repeat known classes.

## Mod and text sweeps, 2026-10-24

**Code read.** Modulation cannot write past a knob range. Every writer in `ApplyModulationAndPalette` (LFO and other modulators, predictors, macro number box, expressions) ends in `ShapeToParam`, which snaps to `step`, clamps to `ref.minValue..maxValue` and replaces NaN with the held value. So the per-node clamps of B-K-031..039 (which cover values that skip the knob: patch, OSC, RPC) do not need a second copy for modulation, and no shared fix was needed.

**Mod sweep** (release app, 873 rows in 20 min, 19 types left over budget): 12 BAD, none a crash or NaN.

| Rows | Class | Verdict |
|---|---|---|
| Array, Depth Projection, Audio Texture (E_BAD_PARAM) | the sweep binds conditional controls (e.g. Array `radial`) that only register in another mode; the schema check rejects the line | Harness limit, not an app bug. Those types' mod rows are untested. |
| Dynamics, EQ, Saturator, Transient Shaper, Wavetable Shaper (HOTAUDIO, bi) | output/makeup gain knob at +12/+24 dB | Knob at max, not a bug |
| Analog, Oscillator (HOTAUDIO, uni) | resonance 1 on SVF LP36 | B-K-041, wontfix |

**Text sweep** (release app, 520 rows, 64 BAD): one crash.

| Rows | Class | Verdict |
|---|---|---|
| Equation Synth `parens` (exit -11) | parser recursion | B-K-040, fixed |
| Formula, Field Modifier/Primitive/Effect, Equation Synth, Sketch (E_FORMULA_COMPILE, E_FIELD_COMPILE, E_RENDER; 61 rows) | garbage text is rejected with a handled error | Not bugs |
| Text 3D `big` (1 MB) and `parens` (50000 chars): timeout at 25 s | extruding that many glyphs | Lead, not re-timed or fixed: finite work on a pathological input |

B-K-040 was verified with a standalone build of `EquationDsp.h` (old header segfaults, new header refuses). The in-app DSPTEST case is committed but was not run: the fresh worktree had no build dir and a full first build did not fit the budget. Run `INFINITE_DSPTEST=1` on the next build.

**Loose ends, closed the same day:**
- DSPTEST is OK on a dev build of main with the B-K-040 case and the new B-K-042 case.
- Text 3D timeouts were slow work, not a hang: B-K-042, fixed (50000 characters 56 s -> 2.8 s).
- `param_fuzz.py` now drops mod lines the schema rejects (E_BAD_PARAM) and re-runs the row. Array, Depth Projection and Audio Texture then ran clean (9 rows, 0 BAD).
- The last 24 mod types (alphabetical by category, type) ran: 72 rows, 3 BAD. Oscillator is B-K-041; Spectral Synth is the same class (addendum). The full 310-type mod sweep is now covered, in two runs.

---

## Earlier checkpoint, 2026-10-23 (evening)

Resume from here. Everything below is merged into local **main**. Nothing is pushed.

The raw run logs are in `docs/release/runs/spree-2026-10-22` (not committed). Session sweep output is in the session scratchpad (`sigrun*/`, `bf3/`, `fz/`). Use the kit in `tools/bughunt/` to re-create any of it.

## Where things stand

- **Findings:** 56 filed (B-A-001 … B-K-038). Every one is fixed except three:
  - B-G-001 and B-H-001: P3, need a quiet-Mac re-run.
  - B-J-001: the Windows headless hang, P5.
- **New on 2026-10-23, evening** (merged to main):
  - **INFINITE_PARAMFUZZ** (`tools/bughunt/param_fuzz_inproc.py`): in-process fuzz of every audio node, one key at a time at ±1e9, -1 and 0, flagging NaN, HOT (peak > 100) and SLOW. The full run takes minutes; the app sweep took hours. The driver restarts past UBSan aborts.
  - **B-K-036:** sample-player params (Audio File, Slicer, PaulStretch, Molder, Granular, Sampler, Drum Sequencer) skipped the knob range. Volume 1e9 played 5e8 loud, and pitch or speed 1e9 overflowed the read position. Each node now clamps to its knob range.
  - **B-K-037:** Set Color's palette offset near INT_MAX overflowed int (UB only; release picked a valid swatch). The swatch math is now 64-bit.
  - **B-K-038:** image size params had no upper cap. Text width 1e9 asked for 4 TB and aborted the app; Render 3D, Fit and 9 other generators failed or asked the GPU for gigabytes. All 13 sites now use `GLUtil::TargetSide` (lo..8192).
  - Tools: `clamp_check.py` lists unseen keys (0 hits proves nothing for them) and refuses repeated keys; `param_fuzz_inproc.py` streams rows as found.
  - Lead: Audio In `deviceId` 1e9 took 15 s in one run (hardware lookup). Not chased.
  - `build-tsan` in main was deleted to free disk; rebuild it before the next TSan run.
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
   - INFINITE_PARAMFUZZ now covers the NaN and hot-audio classes for audio nodes; the app sweep is still needed for visual nodes.
2. `param_fuzz.py --kind text` and `--kind mod` over every type, on the ASan+UBSan build.
3. The signal sweep again, on the ASan+UBSan build. The earlier sweep missed B-K-028.
4. A coverage report from the fixed coverage build. Wipe the stale `$SP/prof` first.
5. Re-time Join Geometry, Difference, Intersect, Distribute Points on Faces (density 1e9 gives the full 200k-point cap; it timed out under ASan), Revolve 100000 and Remove Background alone on a release build, to confirm they are slow and not hangs.

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

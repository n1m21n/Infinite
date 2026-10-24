# tools/bughunt

The bug-hunt kit. Each tool targets a bug class that earlier sprees found by hand, so the next spree can find it by machine. Every script has a `selftest` and a usage block at the top.

## Order of a spree

1. **Static:** `code_lint.py` (seconds). Only new hits fail; review each one. It has five rules:
   - stack-control: a widget given `&local`;
   - loaded-index: a loaded int used as an array index;
   - unbounded-int: a ranged int widget whose member the node never bounds;
   - loaded-loop: a loaded int used as a loop bound with no upper clamp.
   - shader-loop: a GLSL loop whose bound isn't a literal. A uniform bound is a GPU hang from one large value (B-K-035).

   The last two found B-K-029 and B-K-030 after the int sweep passed both nodes.
2. **Decoders:** `fuzz/build.sh` then 5–10 min per target (no app build needed).
3. **Nodes:** `INFINITE_PARAMFUZZ=1` first (minutes, one process), then `param_fuzz.py` against a sanitizer build of the app (one launch per row, capped by `--budget`, default 20 min). Before a run, write down how many rows it will cost and what it could find that the last run did not. A kind the last run already covered is not worth another hour.
4. **Before a clamp fix ships:** `clamp_check.py "Type.key=LO..HI" ...` (under a second). It lists every tracked patch the clamp would change. Any hit means look before clamping.
5. **Group:** `triage.py OUTDIR --stubs DIR` turns BAD rows into one draft finding per distinct bug.
6. **Selftests under the sanitizer build:** `INFINITE_DSPTEST=1` and the other headless selftests. They reach states the sweeps don't. B-K-028 was found this way after the signal sweep missed it.
7. **Gaps:** `coverage.py` over the runs above. Its zero-hit list is where to aim the next harness.

## Builds

| Build | Configure | Catches |
|---|---|---|
| ASan+UBSan | `-DINFINITE_SANITIZE=address,undefined` | memory errors, UB; hardened stdlib is on by default |
| TSan | `-DINFINITE_SANITIZE=thread` | data races (audio thread vs main thread) |
| Hardened only | `-DINFINITE_HARDEN=ON` | `v[i]` past `size()`, empty `front()` — traps even inside capacity, where ASan sees valid memory |
| Coverage | `-DINFINITE_COVERAGE=ON` | nothing; measures what ran. `src/core/CoverageFlush.h` writes the profile before every `std::_Exit` |

The following don't exist on Apple clang (macOS), so they would need Linux CI or Homebrew LLVM:
- LeakSanitizer (`-fsanitize=leak`)
- RealtimeSanitizer (`-fsanitize=realtime`)
- libFuzzer

## param_fuzz.py — every node, headless

`param_fuzz.py BIN OUTDIR --kind int|float|text|mod|signal|all`

| Kind | What it does | BAD when |
|---|---|---|
| int / float / text | Sets every param of that kind to an extreme | crash, sanitizer report, hang, new error code, NaN (`non_finite_frames`), > +12 dBFS |
| mod | Puts every modulatable control on a full-depth LFO | same |
| signal | Feeds every audio input 0.25 s of nan / inf / denormals / ±1e6 / DC / full-scale square, then a clean sine | NANSTUCK (NaN long after the burst), HOTSTUCK, SILENT tail, SLOW (4× base) |

In signal mode:
- **NANPASS** means NaN only while the burst plays. It's recorded but not BAD.
- **NOSIGNAL** on a base row means the test signal never reached the sink, so that type's rows prove nothing. Fix the harness wiring for that type.
- File players (Granular, Sampler, Molder, PaulStretch...) only capture "record in" once a UI record button is armed. They get the signal as their `path`/`filePath` instead, which tests the decoder's NaN zeroing rather than node state.
- The Audio File stimulus runs with `INFINITE_DECODE_KEEP_NONFINITE=1`. Otherwise the decoder zeroes the NaN/inf burst before it plays.
- HOTSTUCK needs the last second to sit 20 dB over base *and* not be falling (within 3 dB of the second before), so a reverb tail dying away doesn't count.

If the Mac sleeps during a row, the row is re-run. A slept row otherwise shows as a TIMEOUT hours long, because the app's own `--timeout` counts the sleep. The check is `kern.waketime`.

In the int, float and mod kinds, file players (any node with a `path`/`filePath` param) get a clean sine WAV as their file. Without one, their per-file work never runs: Molder's generation loop only runs once a file is analysed (B-K-029). The text kind leaves the path to the fuzz value.

The sweep measures frames and audio, not note pairing. A note node that leaves notes stuck still renders fine, so stuck notes are NOTEAUDIT's job (`INFINITE_NOTEAUDIT=1`, PAIR and LOAD). Add a NOTEAUDIT case, as was done for Note Echo x20, rather than expect the sweep to see it.

Float values are ±1e30, nan, inf, 1e9 and -1. 1e9 casts to int fine but can size a buffer. -1 is UB once it's cast to an unsigned seed.

Resumable: re-running skips rows already written. `--values 1e9,-1` runs only those values (the float rows that still reach a node since B-K-026). `--types-from results.tsv` re-runs only the BAD types (the verify step after a fix).

## INFINITE_PARAMFUZZ — every audio node, in process

`INFINITE_PARAMFUZZ=1 Infinite` (or `=Type,Type` for a subset). Run it under `perl -e 'alarm N; exec @ARGV'`.

It loads `1e9`, `-1e9`, `-1` and `0` into every float and int param of every audio node, one at a time, through `Patch::LoadParams`. That is the route a patch file, OSC, RPC and presets share. Each node then renders 0.26 s on the AUDIOPARAMSWEEPTEST rig: a drive tone on every audio input and a held note on note inputs.

- **NAN:** a non-finite sample.
- **HOT:** a peak past 100 (the drive tone peaks at 0.4).
- **SLOW:** a row past 5 s.

Every row prints `PARAMFUZZ try Type.key=value` before it runs, so after a crash or alarm the last line names the row.

It covers the audio classes of `param_fuzz.py --kind float|int` in one process. It can't see frames, file loading or app-level wiring, so the app-launch sweep still owns those.

## tsan_selftests.sh — races

`tsan_selftests.sh BIN OUT [TEST ...]` runs 14 live-engine selftests (GUI plus the real audio device) one at a time under a TSan build.

- Headless renders process audio on the main thread, so they can't show audio-thread races. Use these instead.
- Baseline on 2026-10-22 (main@c2ab99be): 0 warnings in all 14. A racy canary program confirmed reports reach `log_path` on this setup.

## clamp_check.py — a clamp's blast radius on saved patches

It lists every Type.key no patch stores (or a type name no patch has) as `unseen`: 0 hits proves nothing for those. A repeated Type.key is refused, since only the last range would be checked.

`clamp_check.py "Type.key=LO..HI" ...` or `clamp_check.py --knobs DESCRIBE.json [Type ...]`

Parses every tracked `.inf` (855 on 2026-10-23) and prints each saved float that the given range would change. Exit 1 on any hit. `--in DIR` scans another folder, such as a user's patch folder.

Baseline with every knob range from `--describe`: 11 hits in 4 keys, all legitimate values (see the lesson below). The B-K-031..034 clamps: 287 keys, 0 hits.

## fuzz/ — decoders, coverage-guided

`fuzz/build.sh [OUT] [hap patch gltf audio_decode]`

Targets build in seconds with ASan + UBSan + hardened libc++. On macOS they link `driver.cpp`, a small trace-pc-guard loop with the same command line as libFuzzer:

```
OUT/fuzz_hap OUT/corpus/hap -max_total_time=300 -timeout=10 -artifact_prefix=OUT/
OUT/fuzz_hap OUT/crash-<hash>          # replay
```

- `-timeout` matters: three of the four decoder bugs found so far were hangs or giant allocations, not crashes. They show up as `timeout-<hash>`. Sample the process (`sample PID 2`) to see where it is stuck.
- Seeds in `fuzz/seeds/<target>/` carry the harness selector byte where one is used:
  - gltf: 0 = gltf, 1 = glb.
  - audio_decode: 0 wav, 1 aif, 2 aiff, 3 flac, 4 mp3.

  A target whose edge count stops climbing early needs deeper seeds. For example, HAP went from 107 to 420 edges once given a valid 2-frame movie.

## Lessons the tools encode

- **A header count is a claim.** Never size an allocation from it before checking it against the bytes actually there (B-K-020/021/022). Decode frame-wise, as the MP3/FLAC/WAV paths now do.
- **NaN hides.** `max()` skips it and filters keep it. Any audio check needs an explicit non-finite count (`non_finite_frames` in `--audio-summary`).
- **`--start` seeks the transport,** and file players follow it. To test what happens *after* an event, render from 0 and read the later seconds (`loudness_per_second`).
- **Fix a NaN where it is stored, not where it shows.** One central check per host (AudioEffectNode after the kernel *and* its dry path; the decoder for files) beat 19 per-kernel fixes. Check every state the host owns: the first guard missed Drive's dry oversampler.
- **A silent base run proves nothing.** Check that the stimulus reached the sink before trusting a clean result.
- **A knob range is not the param's domain.** A central load clamp built from every knob's range was tried and dropped, because `clamp_check.py` found real patches it would have changed. Ranges change with mode: Select draws `selectC` as 1..32, −1..1 or −3..3, and Frequency Shifter's range toggle switches `shift` between ±100 and ±10000. Other gestures also write past the knob: the viewport camera writes `camDistance`, and reseed writes `seed`. Clamp in the node, to the range its maths needs, and run `clamp_check.py` first.
- **A GPU hang shows as a plain abort.** dither 1e9 exited -6 with no sanitizer log; stderr had Metal's "Caused GPU Hang Error". When a row aborts without a report, re-run it with stderr kept (the sweep sends stderr to /dev/null).
- **Change one param per row, on a node that plays.** The app float sweep logged `Audio File 1e9 ok` while volume 1e9 played 5e8 loud (B-K-036). It sets every param of a type at once, so a param that silences the node (pitch → NaN position) hides one that makes it hot. Its file players also never started. INFINITE_PARAMFUZZ changes one key per row on the `SweepPrepare` fixtures, so it finds both kinds.
- **A hardened-library abort is a Mac blind spot.** `std::clamp(pos, start, end)` with a loaded start > end aborted under Linux's `_GLIBCXX_ASSERTIONS`, while macOS libc++ fast mode let it pass, so only Linux ASan CI saw it. `code_lint.py` rule `clamp-bounds` lists every clamp whose bounds are both runtime values; the 58 existing ones are in the baseline (release returns a bound, nothing user-visible). Order the bounds in new code.

## Lesson: a sweep without a budget eats the hour (2026-10-24)

A float and signal run took about 56 min: 1074 of 1116 rows finished in under 5 s, and a third of the wall time went to timeouts. 28 shape nodes each burned a 22 s cap on one shared 1e9 cause. The sweep now has `--budget` (it stops starting types and is resumable). After 3 types time out on the same value, the rest of that value gets a 5 s probe. Estimate the cost first, check the yield after the first block, and stop once the new rows only repeat known classes.
- **A knob range stops a modulator, not a patch.** `ShapeToParam` clamps every modulated write to the knob range, so a mod sweep cannot find an out-of-range bug. It finds combination bugs (all controls moving at once) and loudness at the top of the range. Run it for those, not for clamps.
- **Free text needs a depth and length limit.** A recursive-descent parser with no limit crashed on 50000 "(" (B-K-040), and a flat `1+1+1+...` built an AST as deep as it was long. Check the text sweep's `parens` row for any new parser; the `big` string alone does not reach recursion.
- **The mod sweep binds every key, including conditional ones.** Keys that register only in some mode (Array `radial`) used to fail the whole row with E_BAD_PARAM. `run_case` now drops the rejected lines and re-runs, so the type's other controls are tested.

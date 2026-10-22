# tools/bughunt

The bug-hunt kit. Each tool targets a bug class that earlier sprees found by hand, so the next spree can find it by machine. Every script has a `selftest` and a usage block at the top.

## Order of a spree

1. **Static:** `code_lint.py` (seconds). Only new hits fail; review each one.
2. **Decoders:** `fuzz/build.sh` then 5–10 min per target (no app build needed).
3. **Nodes:** `param_fuzz.py` with every kind against a sanitizer build of the app.
4. **Group:** `triage.py OUTDIR --stubs DIR` turns BAD rows into one draft finding per distinct bug.
5. **Gaps:** `coverage.py` over the runs above. Its zero-hit list is where to aim the next harness.

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

Resumable: re-running skips rows already written. `--types-from results.tsv` re-runs only the BAD types (the verify step after a fix).

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
- **A silent base run proves nothing.** Check that the stimulus reached the sink before trusting a clean result.

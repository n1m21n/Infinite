# Spectral Field (per-bin FFT kernel)

Status: **design, sections 1 to 7, waiting for sign-off.** Nothing is built.
Sections 8 to 12 are filled in after sign-off and after the build.

Origin: Beestonian on Discord. "A shader, but for frequency bins."

---

## 1. Goal

A new audio effect node, **Spectral Field**: audio in, STFT, a user-written
Field kernel runs once per frequency bin per frame, inverse STFT, audio out.
One blank spectral canvas instead of one fixed spectral plugin per effect.

```
            Size (512..8192), Hann, 4x overlap            latency = Size
 in ──▶ [ FIFO ] ──▶ [ window + FFT ] ──▶ [ polar + true freq ] ──┐
                                                                  │  per bin:
 B  ──▶ [ FIFO ] ──▶ [ window + FFT ] ──▶ [ polar + true freq ] ──┤  bin, mag,
 (optional, declared in the kernel)                               │  phase, freq
                                                                  ▼
                                    ┌──────────────────────────────────┐
                                    │  Field kernel, once per bin      │
                                    │  mag = mag.at(bin / 2)           │
                                    │  freq = freq.at(bin / 2) * 2     │
                                    └──────────────────────────────────┘
                                                                  │  mag, freq
                                                                  │  or phase
                                                                  ▼
 out ◀── [ Mix, dry delayed by Size ] ◀── [ OLA ] ◀── [ IFFT ] ◀── [ phase synth ]
```

## 2. Problem

| Today | Gap |
|---|---|
| Field Effect / Field Synth run per **sample** (time domain) | no way to write a spectral effect in Field |
| Spectral effects exist only as fixed C++ kernels: Spec Blur, Spectrum Slide, Pitch Shifter spectral mode, Key Snap, Paul Stretch, Partials | each new spectral idea needs a C++ node and a release |
| A per-sample kernel cannot see frequency content | `reduce.rms(in, lo, hi)` gives one band level per block, nothing per bin |
| Other tools that do per-bin programming (pfft~, pvs, PV_) are patch-cord or opcode based | none is live text that shares a language with visuals |

## 3. Scope

| In (v1) | Out (v1), with reason |
|---|---|
| One node, `Spectral Field`, category Audio Effects | a spectral **generator** (no input). Later, if asked |
| One kernel per bin per frame, CPU, audio thread | GPU. Memory rule: GPU is for video/3D, CPU for audio; a readback would add frames of latency |
| Inputs: `mag`, `phase`, `freq`, `bin`, `bins`, `binhz`, `sr`, `hop` | `reduce` across bins (frame max, centroid inside the kernel). Later |
| Outputs: `mag`, plus `freq` (vocoder) or `phase` (raw) | `re`/`im` outputs. Polar covers every use case; two output spaces is one too many |
| Bin pointer `X.at(k)`, fractional k | tables (`state float t[N]`) and `delay()` in this host. Per-bin `state` covers freeze/smooth |
| Per-bin `state` (previous frame), `.at(k)` on state | per-bin delay lines (spectral delay). v2 candidate |
| One optional second input `input bin audio B` | more than one declared input |
| Stereo, L and R processed separately | mid/side. A Mid/Side node pair before and after does it |
| Size param, Mix param, kernel params | overlap and window as params |
| Live spectrum, in vs out | per-bin output for visuals (section 3a) |
| 8 shipped presets = the 8 use cases | a preset browser beyond the existing Field one |

### 3a. Visual tie-in (later, not v1)

Publish the output spectrum as 64 log bands through the same MeterRing the
spectrum display already uses, and expose it as a frame-domain data output that
a Field Pixel / Field Modifier `param` can be cabled from. No new cross-thread
channel. Decided now so v1's ring layout already fits it.

## 4. Existing constraints

### 4a. Code facts (read, not guessed)

| Fact | Where | Consequence for this node |
|---|---|---|
| Sample backend is AST to register bytecode, not the typed IR | `src/core/field/BackendRegister.cpp`, header comment | add a **host mode**, not a new IR pass |
| A host flag already adds names: `notesHost` (beat, tick, deg, rand, note) | `BackendRegister.h:17`, `.cpp:39` | `spectralHost` follows the same pattern |
| Interpreter is header-only and allocation-free | `src/core/field/SampleRuntime.h:102` `RunSampleProgram` | run it once per bin; state pointer offset per bin, no interface change |
| Caps: 512 instr, 128 regs, 64 state cells, 128 params | `SampleProgram.h` | spectral host caps state at **16 cells** (16 x 4097 bins x 2 banks x 2 ch = 1 MB) |
| `X.at(k)` already exists for element neighbour reads, reads the cook's input snapshot, clamps, rounds | `FieldIR.cpp:541-600`, `ElementBackend.cpp:1375` | same spelling for bins; spectral differs on fraction and range (see 5b) |
| `Access` AST node exists | `FieldAst.h:26` | `B.mag` parses today; only lowering is new |
| `input sample audio name` creates a real pin | `docs/field/MANUAL.md` 13.4 | `input bin audio B` mirrors it |
| Shared FFT: `PortableFft::RealFft`, prepared once for max size, smaller sizes by stride, `Inverse` allocation-free | `src/audio/dsp/PortableFft.h` | reuse; Prepare(13) once for 8192 |
| Spec Blur / Spectrum Slide use 2048/512 Hann and report `LatencySamples() = kFftSize` | `SpecBlurKernel.h:66`, `SpectrumSlideKernel.h:60` | same latency rule; `MaxLatencySamples() = 8192` |
| Pitch Shifter spectral mode: zero-phase framing + identity phase locking (Laroche and Dolson 1999) | `SpectralPitchShifter.h` | phase-synth stage copies its framing and locking idea, not its peak-move engine |
| Spectrum display: audio thread writes mono samples to a `MeterRing`, main thread draws | `AudioEffectNode.cpp:182,238` | reuse the pattern; publish 64 bands in + 64 out instead of raw samples |
| Field Effect presets are an in-code `{name, code}` table | `FieldSampleNode.cpp:337` | same table shape for the 8 presets |

### 4b. House rules

| Rule | Applies as |
|---|---|
| New nodes need owner approval | this doc; no code until sign-off |
| Audio node minimalism (KHS bar) | card = editor + spectrum + **Size** + **Mix**; everything else comes from the kernel's own `param` lines |
| Professional DSP under minimal controls | phase locking, latency-aligned Mix, denormal flush, NaN sweep are internal, no knob |
| Audio-thread prohibitions | no alloc, locks, `std::string`, `map`, `function`, printf in ProcessBlock |
| Two-object rule, `CookIfNeeded` < 5 us | INode on main, AudioNode on audio; ParamMailbox + MeterRing only |
| Compile on main thread; failed compile changes nothing running | same as Field Effect (`FormulaNode::Apply` reference) |
| Node help coverage: 4 hand-kept tables + per-OS branches | done-when item |
| Builds only via `scripts/ibuild.py`, one at a time | build phase |
| No debug tools in release; short labels; no dropdown chevrons; no Mermaid | UI and docs |
| Field language settled rules: bare names, state as delay sugar, rate inferred, `.at()` for index reads | 5a, 5b |

### 4c. Prior art (docs and papers only; no GPL source opened)

| Tool | How per-bin programming is exposed | What we take | What Infinite does better |
|---|---|---|---|
| Max/MSP `pfft~` | a subpatcher runs once per frame; `fftin~` gives real, imag, bin index as signals, `fftout~` takes them back | the "bin index is just a signal" idea | text, not cords; polar and true frequency given, not re/im |
| Pure Data `block~` + `rfft~` | reblocked subpatch, bins stream as a signal vector | same as pfft~ | overlap, window and normalisation are done for you |
| SuperCollider `PV_` UGens, `pvcollect` | fixed per-bin operators chained on an FFT buffer; `pvcollect` maps a function over (mag, phase, index) | the closest model: a function per bin | live recompile without dropouts, `.at()` cross-bin reads, state per bin |
| Csound `pvs` opcodes (pvsanal, pvsynth, pvshift, pvsfreeze, pvscross) | frames carry **amplitude + true frequency**, resynthesis integrates phase | the representation: mag + freq as the default output, phase integrated by the node | one kernel instead of picking among 40 opcodes |
| Kyma | spectral analysis into oscillator banks, live tweak | true-frequency tracks make pitch work clean | free, patchable with visuals |
| iZotope Iris | draw a selection on a spectrogram, filter by it | visual feedback on what passes | the in/out spectrum on the card |
| Spectral plugins (SoundMagic Spectral, Spectral by various) | many fixed per-bin effects, each with its own panel | the 8 classic effects as the preset list | each is 2 to 6 lines of editable text |

## 5. Hypothesis: the design

A spectral kernel is the existing sample register machine run in a new
**host mode**, once per bin instead of once per sample. Domain name in docs and
error messages: **`bin`**. Lattice: `graph ⊑ frame ⊑ bin`, `bin` incomparable
to `sample`, `element`, `pixel`. Per-frame values (params, `bins`, `hop`)
behave like any coarser value.

### 5a. Language surface

| Name | Read / write | Meaning |
|---|---|---|
| `bin` | read | this bin's index, 0 to `bins - 1` (int as float) |
| `bins` | read | bin count, `Size / 2 + 1` |
| `binhz` | read | Hz per bin, `sr / Size` |
| `sr` | read | sample rate |
| `hop` | read | hop in samples, `Size / 4` |
| `mag` | read and write | amplitude; a 0 dBFS sine reads **1.0** on its peak bin (window gain compensated) |
| `phase` | read and write | analysis phase, radians, zero-phase framed (-pi..pi) |
| `freq` | read and write | **true** frequency in Hz, measured from phase advance (Csound pvs model) |
| `rand()` | read | 0..1, node's audio-thread generator (allowed in this host) |
| `X.at(k)` | read | `X` of bin `k`: `mag`, `phase`, `freq`, a state cell, or `B.mag/B.phase/B.freq` |
| `input bin audio B` | declare | second audio pin, analysed with the same STFT; `B.mag`, `B.phase`, `B.freq` |

Not available, each a compile error naming the reason: `in`, `out`, `n`,
`gate`, `delay()`, tables, `reduce`, `note()`.

Why `bin * binhz` and not `freq` for a filter: on noise the true-frequency
estimate wanders a few bins, so `freq < cutoff` would leak; the bin centre is
exact.

### 5b. Bin pointer `X.at(k)`

| Choice | Decision | Why |
|---|---|---|
| Spelling | `mag.at(k)`, not `mag(k)` | `.at()` is the settled index-read spelling (element, manual 13.2); call syntax is the pixel coordinate read |
| Snapshot | reads this frame's **input** (analysis) values; state reads see the previous frame | same as element: order independent, no read sees an earlier bin's write |
| Fractional k | `mag`, `freq`, state: linear interpolation. `phase`: nearest bin | interpolating a wrapped angle is wrong |
| Out of range (k < 0 or k > bins - 1) | reads **0** | element clamps, but clamping here piles energy at DC and Nyquist on every shift; 0 means "nothing there", which is what a shifted spectrum is |

### 5c. Output and phase handling

The node infers the mode from what the kernel writes. No mode switch.

| Kernel writes | Mode | Resynthesis phase per bin |
|---|---|---|
| `phase` (any bin) | **raw** | exactly the written phase |
| `freq`, or nothing | **vocoder** (default) | if out freq == analysis freq: analysis phase (exact). else: previous synthesis phase + 2 pi x freq x hop / sr, then identity phase locking: bins in a peak's region take the peak's phase (zero-phase framing makes lobe bins share one phase) |

| Why vocoder is the default | |
|---|---|
| Pitch and frequency shift are only clean when phase follows the new frequency | copying raw phase to another bin gives the classic phasey, beating sound |
| Freeze needs phase to keep advancing while magnitude holds | the integrator does that for free; the kernel just holds `freq` |
| Untouched bins resync to the analysis phase | identity, filters and gates reconstruct exactly; only moved bins pay vocoder artefacts |
| Phase locking is internal, no knob | minimalism rule 5; same technique already proven in Pitch Shifter |

Synthesis phase accumulators are `double`, wrapped every frame.

### 5d. State across frames

| Choice | Decision |
|---|---|
| Syntax | existing `state float x = 0`, now **one cell per bin** (as element state is one per element). No new `prev.` syntax |
| Read | bare `x` = this bin's value from the previous frame; `x.at(k)` = bin k's previous-frame value |
| Previous input | `state float pm = 0` ... `pm = mag` gives last frame's magnitude; delay sugar, the settled rule |
| Cap | 16 cells per kernel (memory: 16 x 4097 x 4 B x 2 banks x 2 ch = 1 MB, allocated on the main thread at Prepare) |
| Reset | transport reset, recompile (non-transplanted cells), Size change (bin count changed), same rule as element `age` |
| Hot reload | transplant by (name, type), existing rule |

### 5e. FFT settings

| Setting | Decision | Why |
|---|---|---|
| Size | param, 512 / 1024 / 2048 / 4096 / 8192, default **2048** | resolution vs latency is the one trade users hear |
| Overlap | fixed **4x** (hop = Size / 4) | Hann x Hann at 4x sums to a constant 1.5, so OLA is exact; 2x is not |
| Window | fixed **Hann**, analysis and synthesis | standard, matches Spec Blur and Pitch Shifter |
| Framing | zero-phase (rotate by Size / 2) | needed by phase locking; `phase` reads are centred |
| Latency to PDC | `Size` samples, `MaxLatencySamples() = 8192` | same as Spec Blur; 43 ms at 2048 / 48 kHz |
| Size change | audio thread resets FIFOs, OLA and state, fades in over one hop; no allocation (buffers sized for 8192) | |
| Mix | node param, dry delayed by `Size` | latency-aligned, rule 5. A spectral-domain mix is not a crossfade |

### 5f. Inputs

| Choice | Decision |
|---|---|
| Main input | one stereo audio pin, `in` |
| Second input | appears only when the kernel declares `input bin audio B`; analysed with the same Size; unconnected reads mag 0 |
| Channels | L and R analysed, run and resynthesised separately, each with its own state bank. Mono source = same signal twice |

### 5g. Performance (M2 8 GB, measured micro-bench, section 6)

Kernel runs = bins x frames/s = 1025 x 93.75 = **96 k/s per channel** at
2048/4x, about 2x a Field Effect's 48 k/s.

| Part, per channel, 2048/4x | Cost | % of one core |
|---|---|---|
| FFT + IFFT | 30.3 us per frame | 0.28 |
| polar, true freq, phase synth, back to rect (estimate: atan2 + sqrt + sincos per bin, ~30 ns) | ~2.9 ms/s | ~0.29 |
| kernel, 20 ops (low-pass to freq-shift size) | 28.4 ns/bin | 0.27 |
| **total, per channel** | | **~0.85** |
| **stereo, no B input** | | **~1.7** |
| stereo + B input (2 more FFTs + polar) | | ~2.8 |

| Worst single callback | |
|---|---|
| 8192 / 4x, stereo, 40-op kernel | 2 x (4097 x ~74 ns + 143 us) = ~0.9 ms in one callback every 8 blocks of 256 |
| budget at 256 / 48 kHz | 5.3 ms, so ~17 % of that one callback |

CPU, audio thread. No allocation after Prepare: FIFO, OLA, spectra, state
banks and accumulators are all sized for 8192 on the main thread.

### 5h. UI

```
┌ Spectral Field ──────────────────────────────┐
│ ┌──────────────────────────────────────────┐ │
│ │ param float cutoff = 2000 [20, 20000]    │ │  code editor,
│ │ mag = if(bin * binhz < cutoff, mag, 0)   │ │  same face as
│ └──────────────────────────────────────────┘ │  Field Effect
│ ┌──────────────────────────────────────────┐ │
│ │ ░░▒▒▓▓██▓▓▒▒░░·····                      │ │  in  = dim line
│ │ ██████▓▓▒▒                               │ │  out = filled
│ └──────────────────────────────────────────┘ │  64 log bands
│  cutoff ◯         Size [2048]    Mix ◯       │  kernel params,
│                                              │  then node params
└──────────────────────────────────────────────┘
```

- Short labels only, no hint lines. Size is a field well + value, no chevron.
- Spectrum: audio thread publishes 64 log bands in + 64 out once per frame to
  one `MeterRing` (128 floats x 94/s, well under its 4096 capacity per UI
  frame); main thread keeps the latest full set. No main-thread FFT.
- Compile errors show in the editor like Field Effect.

### 5i. The 8 presets (each is also an acceptance test)

```
# 1. Low-pass (brick wall)
param float cutoff = 2000 [20, 20000]
mag = if(bin * binhz < cutoff, mag, 0)
```
```
# 2. Spectral gate
param float thresh = -50 [-100, 0]
mag = if(mag > 10 ^ (thresh / 20), mag, 0)
```
```
# 3. Freeze
param float hold = 0 [0, 1]
state float m = 0
state float f = 0
m = if(hold > 0.5, m, mag)
f = if(hold > 0.5, f, freq)
mag = m
freq = f
```
```
# 4. Octave up
mag = mag.at(bin / 2)
freq = freq.at(bin / 2) * 2
```
```
# 5. Frequency shift
param float shift = 200 [-2000, 2000]
k = bin - shift / binhz
mag = mag.at(k)
freq = freq.at(k) + shift
```
```
# 6. Spectrum flip
k = bins - 1 - bin
mag = mag.at(k)
freq = sr / 2 - freq.at(k)
```
```
# 7. Robot / whisper
param float whisper = 0 [0, 1]
phase = if(whisper > 0.5, rand() * 6.2831853, 0)
```
```
# 8. Cross-synthesis (mag from in, phase from B)
input bin audio B
phase = B.phase
```

### 5j. Decision summary

| Question | Decision |
|---|---|
| New domain or new IR? | host mode `spectralHost` of the existing register backend; domain `bin` in docs/errors; new ops only for bin loads and `.at()` |
| Bin pointer | `X.at(k)`, interpolated (phase nearest), out of range = 0 |
| State | per-bin `state`, `.at(k)` reads previous frame; no `prev.` syntax |
| Phase | inferred: write `phase` = raw; else vocoder with resync on untouched bins + identity phase locking |
| FFT | Size param 512..8192 (2048), Hann, 4x fixed, zero-phase, latency = Size |
| Inputs | stereo per channel; optional `input bin audio B` |
| CPU / GPU | CPU, audio thread, ~1.7 % core stereo at 2048 |
| Controls | Size + Mix + kernel params |

## 6. Baseline (recorded once, 2026-11-10, M2 8 GB, `-O2`, not re-measured later)

Micro-bench: the real `RunSampleProgram` and `PortableFft::RealFft`
headers from `f733f2ac`, hand-built programs, one channel, 20 s of frames.
Source: scratchpad `bench/bench.cpp` (not committed).

| Measure | Result |
|---|---|
| interpreter, 6-op kernel | 22.4 ns per bin |
| interpreter, 20-op kernel | 28.4 ns per bin (0.27 % core per channel at 2048/4x) |
| interpreter, 40-op kernel | 43.6 ns per bin (0.42 %) |
| FFT + IFFT 512 | 6.4 us (0.24 % at 4x) |
| FFT + IFFT 2048 | 30.3 us (0.28 % at 4x) |
| FFT + IFFT 8192 | 143.1 us (0.34 % at 4x) |
| Spectral Field today | does not exist; no spectral effect is writable in Field |

## 7. Success metric (done when)

Headless self-test `INFINITE_SPECTRALFIELDTEST`, each preset on a fixed test
signal at 48 kHz, Size 2048 unless stated. All checks after the reported
latency.

| # | Kernel | Test signal | Pass when |
|---|---|---|---|
| 0 | identity `mag = mag` + `phase = phase` | white noise | output vs input delayed by `Size`: error under **-90 dB** |
| 0b | identity, vocoder (`mag = mag` only) | white noise | same, under -90 dB (resync path) |
| 1 | low-pass, cutoff 2 kHz | white noise | energy at or above 2.5 kHz under **-60 dB** relative to the same band of the input |
| 2 | gate, -40 dB | 1 kHz sine at -6 dB + noise at -60 dB | residual (output minus the sine) under -80 dB |
| 3 | freeze | 440 Hz saw, hold set at 0.5 s, input stops at 1 s | output level after 1 s stays within 0.5 dB frame to frame for 2 s; pitch stays 440 Hz |
| 4 | octave up | 440 Hz sine | spectral peak moves from 440 to **880 Hz** (within one bin); 440 Hz is down at least 40 dB |
| 5 | freq shift, +200 Hz | 1 kHz sine | peak at 1200 Hz (within one bin) |
| 6 | flip | pink noise low-passed at 4 kHz | output centroid = sr/2 - input centroid, within 5 % |
| 7 | robot / whisper | 220 Hz saw | robot: autocorrelation peak at lag `hop` above 0.8; whisper: spectral flatness rises at least 3x |
| 8 | cross-synthesis | in = noise with a 4 Hz amplitude envelope; B = 220 Hz saw | output envelope correlates with in's at r > 0.9; output pitch 220 Hz within 3 % |

| Also required | Check |
|---|---|
| no allocation or locks on the audio thread | allocation counter around ProcessBlock in the test reads 0; diff passes the field-realtime checklist |
| CPU per block reported | test prints us per block for each preset at 2048 and 8192; stereo 2048 low-pass under 3 % of one core |
| failed compile leaves sound running | test compiles a bad kernel mid-render; output unchanged |
| help tables | all 4 tables + per-OS branches list the node; checked both directions |
| regressions | `INFINITE_DSPTEST` and `INFINITE_FIELDTEST` still OK; audio sweeps zero xruns |
| UI | Size and Mix only on the card besides kernel params; no hint text; no chevron |

---

## 8. Methodology

```
compiler (spectral host)  ->  engine (STFT host)  ->  node + card  ->  self-test  ->  build + regressions
  830f93ba, a7f11eee          58ebf49d                48818f7c         40d1acbc       dev build, 4 suites
```

- Engine first, outside the app: a scratch harness compiled the engine and
  compiler alone, so the DSP was proven (identity -138 dB) before any UI.
- Then the node, mirroring Field Effect: same program swap, mailbox,
  device file and pin reconcile, so nothing new on the threading side.
- One headless test covers every section 7 row. It drives the real node
  through `ProcessBlock`, not the engine.
- CPU: the dev build is Debug (-O0), so the 3 % bar is checked under
  `NDEBUG` only. The optimised figure comes from the -O2 harness on the same
  engine path.

## 9. Solution trajectories

| Decision | Taken | Why |
|---|---|---|
| Synthesis | raw when the kernel writes `phase`, else vocoder with identity phase locking | mag-only kernels reconstruct exactly; pitch and shift kernels keep partials coherent |
| Unchanged bins | keep analysis phase when `freq` is left equal | identity in vocoder mode at -138 dB, not a resynth approximation |
| Program swap | engine `ProgramSwapped()` flag | a reused allocation address could otherwise skip the state re-map |
| Bin input | one, `input bin audio B`, `B.mag/phase/freq` and `.at(k)` | cross-synthesis needs one; more pins would be card clutter |
| Mix | dry ring delayed by the latency | wet and dry stay aligned at every Size |
| Gate metric | noise leak (output minus the same gate on the sine alone) | output minus the raw sine also counts the gate trimming the sine's own Hann skirt (-62 dBFS), which a spectral gate does by design |

## 10. Benchmark against baseline

`INFINITE_SPECTRALFIELDTEST`: OK, 0 failures.

| # | Check | Result | Bar |
|---|---|---|---|
| 0 | identity raw, 512 / 2048 / 8192 | -139.0 / -138.6 / -138.2 dB | -90 dB |
| 0b | identity vocoder, same sizes | -139.0 / -138.6 / -138.2 dB | -90 dB |
| 1 | low-pass 2 kHz, band >= 2.5 kHz | -128.8 dB | -60 dB |
| 2 | gate -40 dB, noise leak | -83.3 dBFS | -80 dB |
| 3 | freeze, frame-to-frame | 0.10 dB max over 184 frames, 440.0 Hz | 0.5 dB, 440 Hz |
| 4 | octave up | 880.0 Hz, 440 Hz at -105 dB | 880 Hz, -40 dB |
| 5 | shift +200 | 1200.0 Hz | 1200 Hz |
| 6 | flip centroid | 23612 vs 23669 Hz (0.2 %) | 5 % |
| 7 | robot autocorr at hop / whisper flatness | 1.000 / x5647 | 0.8 / x3 |
| 8 | cross-synth envelope r / pitch | 0.992 / 220.2 Hz | 0.9 / 220 +-3 % |
| | Mix 0, Size latency, bad compile, allocation | exact dry, 2048 -> 8192, unchanged, 0 bytes | |

CPU, stereo, per 512-sample block (10.67 ms), optimised:

| Preset | 2048 | 8192 |
|---|---|---|
| Low-pass | 2.31 % | 2.06 % |
| Gate | 2.01 % | 2.11 % |
| Freeze | 1.94 % | 2.10 % |
| Octave up | 2.10 % | 2.23 % |
| Freq shift | 2.02 % | 2.19 % |
| Flip | 2.17 % | 2.31 % |
| Robot / whisper | 1.73 % | 1.88 % |
| Cross-synth | 2.53 % | 2.71 % |

Regressions: `INFINITE_FIELDTEST`, `INFINITE_DSPTEST`, `INFINITE_FIELDSAMPLETEST` OK.

## 11. Critique

- Cost is per sample, not per frame: 8192 costs about the same as 2048.
  The interpreter runs bins x 4 per hop, so the kernel dominates, not the FFT.
- The 3 % bar holds with little headroom (2.3 %) on a loaded 8 GB machine.
- The gate row as first written measured the wrong thing; the leak metric
  is the one that says whether noise gets through.
- No in-app check of the card yet; verified by code and the headless test.
- Help text is English only; the five language tables fall back to it.

## 12. Next steps

1. Rebase onto main (`git rebase --onto main fix/timeline-loop-bar1-rewind feat/spectral-field`) once that branch merges or is dropped.
2. Release build, `scripts/deploy_desktop.sh`, owner checks the card and the editor.
3. Audio sweeps for xruns with the node in a patch.
4. Library panel entries for the eight presets, as Field Effect has.

# Beat Arranger v2 — redesign prompt

> Self-contained brief for a fresh Claude Code session. **Supersedes
> `beat-arranger-prompt.md`** (the 8-strip / step-grid design). The node was
> built from v1 in commit `bdde1e1` on `main`; this prompt reshapes it into
> the design the user actually wants and folds in only the review fixes that
> still apply to that design.
>
> `bdde1e1` is in no release tag (`git tag --contains bdde1e1` is empty), so
> no saved patch in the wild depends on its param names. Breaking its save
> format is fine.

## 0. The design, in one picture

Modelled on XLN Life's "Source File" strip: one waveform, cut at transients,
each slice coloured by what it sounds like.

```
┌──────────────────────────────────────────────────────────────────────┐
│ [kick ][hat][snare ][hat][piano     ][kick][bass      ][ ··· ][synth]│  ← one waveform,
│  ▮▮▮▮   ▮▮   ▮▮▮▮▮   ▮▮   ▮▮▮▮▮▮▮▮     ▮▮▮   ▮▮▮▮▮▮▮▮    grey   ▮▮▮▮  │    coloured slices
├──────────────────────────────────────────────────────────────────────┤
│                         [ Generate ]                                 │  ← the main button
│ time sig ▾   swing ───  rand pitch ───  speed ───                    │  ← sliders, not knobs
│ transient ───  decay ───                          output ───         │
└──────────────────────────────────────────────────────────────────────┘
```

Backend flow:

```
drop one file → decode → SlicerDsp::Detect (transients)
             → per slice: DrumClassifier::Classify → class + confidence
Generate     → new seed → BeatArranger::Arrange(slices, meter, seed)
             → hit list "kick hat hat kick hat piano kick kick …"
audio thread → Transport::Beats() → step → play slice[hit] with pitch/speed/env
```

That is the whole node. **No per-sample strips, no step grid, no knobs, no
Arrange/Re-Arrange/Clear Groove/Roll buttons, no seed knob, no rate or bars
control, no 8 per-sample outputs.**

## 1. Before writing code

1. Branch: `feature/beat-arranger-v2` off `main` (`.claude/skills/git-branch-workflow/SKILL.md`).
2. Load and follow: `.claude/skills/new-audio-node/SKILL.md`, `.claude/skills/audio-node-ui/SKILL.md`,
   `.claude/skills/node-ui-pillars/SKILL.md`, `.claude/skills/rhythmic-quantization-standard/SKILL.md`.
3. Clean room (`AGENTS.md`): MIT. Cite papers only. Do not open aubio (GPL), Essentia (AGPL), or any GPL source.
4. Rework the existing class **in place** (`src/nodes/BeatArrangerNode.h/.cpp`, the
   `DrawBeatArranger*` functions in `src/main.cpp`), keeping the `REGISTER_NODE`
   name `Beat Arranger`. Don't add a second node.

## 2. Node model: one source file

Replace the 8-strip model with **one** source buffer, the way `SlicerNode` holds
one (`src/nodes/SlicerNode.h`: `mFilePath`, `mSourceMono`, worker-thread
`LaunchJob` / `JoinWorkerIfDone` / `mAbort`). Remove from `BeatArrangerNode`:

- all `strip*` arrays, `kNumStrips`, `LoadFileToStrip`, `ClearStrip`, `DrawBeatArrangerStripCard`,
  `DrawBeatArrangerStripWaveform`, the S1–S8 step grid in `DrawBeatArrangerTimeline`;
- the 8 extra output pins (`OutputCount` becomes 1, `out`);
- `rate`, `bars`, the visible `seed` knob, `Roll`, `Clear Groove`, `Re-Arrange`;
- the 89 entries in `.claude/skills/run-infinite-hygiene/audio-param-sweep-expected.txt` (Section 8).

New public API: `LoadFile(path)`, `ReloadFromPath()`, `Generate()`, `HasBeat()`,
`Slices()` (start frame, end frame, class, confidence, per slice), `Status()`.

Dropping a file **replaces** the source. Several files dropped at once: load the
first one, and the status line says `1 of N loaded`.

## 3. Params: exactly these, all sliders

Draw them with `AudioSlider` / `AudioSliderInt` (`src/main.cpp:11528`, the ones
`DrawSlicerBody` uses), not `AudioKnobRow`. Order = draw order = modulation-pin order.

| Param | Type / range | Behaviour |
|---|---|---|
| `time sig` | dropdown: `transport`, 4/4, 3/4, 6/8, 5/4, 7/8 | `transport` reads `Transport::Instance().TimeSigNumerator()/Denominator()` (`src/core/Transport.h:141`). Sets steps per bar (16th grid: 4/4 → 16, 3/4 → 12, 6/8 → 12, 5/4 → 20, 7/8 → 14) and where the strong beats are. Changing it **regenerates with the same seed** so the groove fits the new bar; it never plays a hit list built for another length. Use the shared dropdown path (`row.Dropdown` / `PushDropdownStyle`), not a raw `ImGui::Combo` |
| `swing` | 0..1 | Delays odd 16ths, applied at play time. Currently `ArrangeParams::swing` is passed in and never read; move swing to the audio thread |
| `rand pitch` | 0..1 | Scales per-hit pitch offsets. **Applied live at play time**: the hit stores a fixed random value in −1..1, and the audio thread multiplies by `rand pitch`. Moving the slider is audible immediately, and a modulator can drive it |
| `speed` | 0.25..4 | Slice read-head rate; slice confinement is in read-head position so it stretches correctly (the `SlicerNode` rule) |
| `transient` | −1..1 | Attack emphasis on every hit |
| `decay` | 0..1 (top = no decay) | Envelope decay, **scaled to each slice's own length**, not the whole file |
| `output` | 0..1 | Bottom-right slot per node-ui-pillars |

Hidden, saved, not drawn: `seed` (uint32), the file path, the slice list, and the hit list (Section 6).

**Generate**: one full-width button between the waveform and the sliders.
Each press sets `seed` to a fresh value (hash the old seed with a counter, not `rand()` or time) and re-arranges.
It is undoable like any param change. It is disabled, labelled `analysing…`, while the worker runs.
With no file loaded it is disabled and labelled `drop a sample`.

## 4. Detection: what each slice is

`src/audio/dsp/DrumClassifier.h/.cpp` exists. Keep its structure; fix and extend it.

### 4a. New classes (append-only; the index is saved)

Append to `enum class DrumClass` (`DrumClassifier.h:28`), after `Perc`: `Synth`, `Piano`, `Tonal`.
Bump `kNumClasses`. Add the comment `// append-only: the index is saved in patches` on the enum.

| Class | What separates it |
|---|---|
| **Kick** | Low energy dominant, **f0 falling** over the first 50 ms, short decay, low centroid. **Penalise centroid > 1.5 kHz** (today nothing does, which is why a snare reads as Kick) |
| **Bass** | Stable low f0 (30–250 Hz) over ≥ 2 windows, long sustain, low flatness |
| **Snare** | Body 150–300 Hz plus broadband noise, centroid 1–5 kHz, **one** onset |
| **Clap** | Noise 1–3 kHz, **3–4 micro-onsets 5–15 ms apart** in the first 40 ms |
| **HatClosed / HatOpen** | Centroid > 5 kHz, high flatness, little energy < 500 Hz; closed < ~120 ms decay, open longer |
| **Piano** | Pitched (clear f0 above 80 Hz, harmonic peaks), fast attack, **smooth exponential decay**, slightly stretched partials (inharmonicity; Fletcher & Rossing, *The Physics of Musical Instruments*) |
| **Synth** | Pitched, attack not percussive or with a flat sustain plateau, stable partials |
| **Tonal** | Pitched but neither of the above clearly: the fallback for melodic material |
| **Perc** | Everything unpitched that matches no drum class above |

Pitchedness: normalised autocorrelation peak over 80–2000 Hz lags, plus harmonic-peak ratio from the spectrum.
Cite Peeters (CUIDADO 2004) for feature definitions, Herrera/Yeterian/Gouyon (2002) and Gillet & Richard (ICASSP 2004) for drum features, and de Cheveigné & Kawahara, "YIN" (JASA 2002), if you use a YIN-style f0 (a clean-room reimplementation from the paper is fine).

**"Closest to"**: keep `scores[]` normalised and store `confidence` = the margin between the top two scores.
The view labels low-confidence slices `~kick` (Section 5).

### 4b. Classifier fixes from the review (all still apply)

| # | Where | Fix |
|---|---|---|
| D1 | `DrumClassifier.cpp:498-506` | Check `ohh` / `open` **before** `hh` / `hat` |
| D2 | `:473-513` | Split the file name into tokens on non-alphanumerics and case boundaries, then match whole tokens (`bd`, `sd`, `hh`…). No substring matching (`sub` must not match in "subtle", `hat` must not match in "what"). Add `piano|keys|rhodes|ep` → Piano and `synth|pad|lead|pluck|stab` → Synth. The prior applies only when the file yields one slice; a whole loop dropped as the source ignores its file name for per-slice classes |
| D3 | `:148-171` | Micro-onset count: run the flux ODF (`SlicerDsp` style) at a 128-sample hop over the first 40 ms, count the frame-0 onset, and count only peaks 5–15 ms apart above an adaptive threshold. Measured today: single noise burst → 4, clap spaced 18 ms → 1 |
| D4 | `:214-227` | Pitch stability needs ≥ 2 windows. When the slice is too short for two, use shorter windows (min lag-limited) or report stability = unknown (0 contribution), never one window's correlation |
| D5 | `:194-210` | Low-pass (~300 Hz one-pole ×2) before the low-band autocorrelation, and prefer the first peak above 0.8× max (octave-error guard) |
| D6 | `:102-116` | Decay envelope: RMS window ≥ one period of 30 Hz (~33 ms), not 3 ms |
| D7 | `:363-382` | Kick needs a centroid penalty, and the centroid-drop rule needs a real threshold (> 20% drop, not −5 Hz) |
| D8 | `:287-296` | Size the FFT so the sub band has ≥ 3 bins at any sample rate (e.g. 4096 at ≥ 88.2 kHz), or compute sub energy from a low-passed time-domain RMS |
| D9 | `:56-57, 74-75` | Silent/empty input returns Perc with `scores[Perc] = 1`, same as the normal fallback |

## 5. The waveform view (the only visual)

Rewrite `DrawBeatArrangerTimeline` into `DrawBeatArrangerSource`, based on
`DrawSlicerWaveform` (`src/main.cpp:12593`). It fills the full node width at the top of the body.

- Draw the waveform once (min/max cache like `SlicerNode::waveformMin/Max`).
- Draw each slice as a **rounded coloured block behind its part of the waveform**, spanning exactly `[start, end)` of the **whole buffer**.
  Review bug: markers today are scaled by `lastOnset * 1.15` (`main.cpp:14548`), which puts them in the wrong place.
- One fixed colour per class, readable in light and dark theme (node-ui-pillars contrast budget).
  Slices the current beat **doesn't use** stay grey, like Life's unused slices.
- Class label in the top-left of each block (`kick`, `hat`, `piano`…). Prefix `~` when confidence is low. Skip the label when the block is narrower than the text.
- The slice that is sounding right now brightens (from the audio-thread visual snapshot).
- Empty state: centred text `drop a sample here`. While analysing: `analysing…`.
- Right-click a slice → a small menu: `auto` + every class, to override a wrong guess.
  The override is saved with the slice and wins on the next Generate. This is the only control on the view.

## 6. Arranger: `src/audio/dsp/BeatArranger.h/.cpp`

Change the API to single-source:

- `SliceInfo { int slice; DrumClass cls; float confidence; float lenSec; float centroid; float decaySec; float f0; }`
- `ArrangeParams { int stepsPerBar; int bars; int strongBeatMask; }`
- `ArrangedHit { int step; int slice; float velocity; float pitchRand; }`: `pitchRand` in −1..1, scaled at play time.

Rules:

- **Bars**: fixed at 2 (not a control). Steps = `stepsPerBar × 2`.
- **Drum roles**: as now (kick anchors the strong beats plus seeded syncopation; snare/clap on the backbeat; closed hat as a Euclidean `E(k, steps)` with accents, after Toussaint (BRIDGES 2005); open hat on off-beats, choking closed hats; perc fills gaps sparsely).
- **Bass follows the kick**: seeded offsets from the actual kick steps, never on the snare step.
  Fixes A4 (bass is hard-coded to steps 2/7/10/14 today, `BeatArranger.cpp:286-293`).
- **Tonal roles (new)**: Piano/Synth/Tonal slices play sparse stabs, 1–4 per bar, on off-beats or bar starts, seeded.
  Their `rand pitch` snaps to musical intervals {0, ±3, ±5, ±7, ±12}, so random pitch stays musical.
  Drums use continuous ±12, kick and bass ±3.
  Fixes A3: the range comes from the **slice's own class**, not the role it fills (today a kick filling a hat role gets ±12, `:165`).
- **Missing roles** (A1): promote by features, not by copying a whole class or the whole pool.
  With no kick: take the slice with the lowest centroid and shortest decay. With no hat: the highest centroid.
  With no snare: the mid-centroid noisiest slice. Leave a role empty rather than make one slice play everything.
- **No duplicates** (A2): at most one hit per `(step, slice)`. Delete the dead `hasDistinctClap/Snare` branches (`:223-232`).
- **Repetition**: within a role, pick slices weighted by confidence, so some slices repeat far more than others.
- **Deterministic across platforms** (A5): `std::stable_sort` with a total order (step, then slice).
- **Serialisation** (A6): write floats with `%.9g` so a save → load round trip is exact.

## 7. Node and audio fixes from the review that still apply

`src/nodes/BeatArrangerNode.cpp`, mirroring `DrumSequencerNode.cpp` where noted:

1. **Global transient/decay dead** (`:871-874` vs `:909-910`): the shadow copies are updated before they are compared, so envelopes never recompute. Compare first, then update. Remove the matching expected-failure lines from the sweep file.
2. **Envelope times computed with no sample loaded** (`:916-921`): the envelope is computed per slice from the slice's length when the hit fires, or recomputed when a new analysis lands. Never from a length of 0 / a 0.05 s fallback.
3. **Wrong sample rate** (`:921, :926`): recompute coefficients when the rate changes. Copy `srChanged` from `DrumSequencerNode.cpp:514-519`.
4. **`PrepareToPlay` resets smoothed params to defaults** (`:78-89`): restore from the last-pushed atomics, like `DrumSequencerNode.cpp:54-60`.
5. **No `Reset()`; step position starts at 0.0** (`:508, :226-259`): add `Reset()` like `DrumSequencerNode.cpp:66-80`. Step 0 must fire at beat 0, and the first block after wiring or undo must not replay every past hit.
6. **Replacing the source doesn't silence the old one** (`:776-777`): `SampleSlot::Push(nullptr)` does nothing to the active buffer. Push an empty buffer, like `DrumSequencerNode.cpp:869-874`.
7. **Old slices after a new load** (`:663-707`): on load, clear the slice list, the hit list, and the audio thread's slice count before analysis starts. The view shows `analysing…`, and nothing plays the new buffer with the old boundaries.
8. **Saved beat changes on load** (`:947-997`, `:713-716`): save the slice boundaries (frames), each slice's class and override, and the hit list, like `SlicerNode::SerializeSlices` / `mSliceBlob`. On load, **don't re-detect and don't re-arrange**. Only a new file or Generate changes them.
9. **Leak** (`:67-71`): free the active and pending sample buffers and hit lists in the destructor. Give `SampleSlotT` a destructor in `src/audio/SampleSlot.h` if that is cleaner; it fixes `DrumSequencerNode` too. Also drop the second full main-thread copy of the sample if only the mono analysis copy is needed.
10. **Clicks**: fade voices out over 2–3 ms at the slice end, on steal, and on choke. Steal the **oldest** voice, not the next one in rotation (`:343-346, :443-447`).
11. **Choke**: implement open hat ↔ closed hat choke (the dead `isClosedHat` at `:397` / comment at `:421`), or delete it.

## 8. main.cpp wiring

| Site | Change |
|---|---|
| Body dispatch (~24533) | Calls the new `DrawBeatArrangerBody` (waveform + Generate + sliders) |
| `AudioNodeWidth` (~11689) | Keep wide |
| OS file drop onto the node (~69776 / ~69932) | Drop anywhere on the node → `LoadFile(first path)`. Remove the strip +1 wrap logic and `BeatArrangerStripForCanvasPos` (~1934-1950) |
| **Sample-browser drag release** (~88580-88637) | **Missing today**: dropping a browser sample on the node must load it, same as SlicerNode |
| **Drop picker `kOptions`** (~89233) | **Missing today**: add `Beat Arranger` |
| Reload-on-load (~7946) | `ReloadFromPath()` restores the buffer only; it keeps the saved slices and hits (Section 7.8) |
| Help tables (~39587, ~40411) | Rewrite the description for the new design |
| `audio-param-sweep-expected.txt` | Remove all 80 per-strip entries and the deleted globals. Keep only entries that genuinely can't be checked with no sample loaded, and make sure `transient`/`decay` aren't masked |
| `ARCHITECTURE.md` | Add the node (new-audio-node exit criterion 7) |

## 9. Tests (replace the fixture at `src/main.cpp` ~53015-53159)

Synthesise every signal in code, no asset files:

1. **Classifier, one test per class**:
   - Kick: pitch-dropping 60→40 Hz sine, 150 ms.
   - Bass: steady 55 Hz saw, 800 ms.
   - Snare: 200 Hz tone + band-passed noise, 150 ms.
   - Clap: 4 noise bursts 10 ms apart.
   - HatClosed: 40 ms high-passed noise.
   - HatOpen: 400 ms high-passed noise.
   - Piano: 220 Hz, 8 harmonics with slight stretch, exponential decay.
   - Synth: 220 Hz saw, 20 ms attack, flat sustain.
   
   Each must classify exactly, no "Kick or Bass" accepts. The spec snare must **not** read as Kick. Sample rates 44.1k, 48k and 96k.
2. **Filename prior**: `OHH_01` → HatOpen, `subtle_pad` → not Bass, `Kick 01` on an ambiguous signal → Kick.
3. **Short slices**: lengths 0, 1, 100 and 1023 don't crash and return a normalised result.
4. **Arranger**: same input + seed ⇒ **byte-identical** hit list (compare contents, not sizes); a new seed ⇒ a different list; no duplicate `(step, slice)`; a hat-only pool never puts a hat on the kick role; kick `pitchRand × 1.0` stays within ±3 st; the hit count fits 3/4 and 7/8.
5. **Node round trip**: load → Generate → save → load into a fresh node ⇒ identical slice boundaries and hit list, with no worker job launched.
6. **Audio**: render through `AudioBeatArrangerNode` (pattern of `RunDrumSequencerFixture`): step 0 fires at beat 0; moving `decay` and `transient` changes the output; moving `rand pitch` changes the output without regenerating.

## 10. Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then run `.claude/skills/run-infinite-hygiene/SKILL.md` (audio param/teardown sweeps, all-node-type round trip) and `.claude/skills/audio-node-sweep/SKILL.md`. Copy `build/Infinite.app` to `~/Desktop/Infinite.app`. Merge `feature/beat-arranger-v2` into `main` with `--no-ff` per git-branch-workflow.

## Out of scope

- A trained ML classifier. Rule-based plus right-click override is v2.
- Several source files at once, sample layering, or Life's beat shortlist / circle view.
- MIDI export, dragging the beat to the timeline, per-slice knobs.
- Changes to `SlicerNode` or `DrumSequencerNode` beyond the shared `SampleSlotT` destructor.

## Open choices (recommendation given)

- **`time sig` options**: `transport` + 4/4, 3/4, 6/8, 5/4, 7/8 (recommended), or follow the transport only with no control.
- **Piano vs Synth**: a best-effort guess from rule features. Expect confusion on soft pads vs electric piano; `Tonal` is the honest fallback and the right-click override fixes the rest.

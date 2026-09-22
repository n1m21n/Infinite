# Beat Arranger node — implementation prompt

> Self-contained brief for a fresh Claude Code session. Inspired by XLN Life/XO:
> drop up to 8 samples, the node chops them at transients, classifies every
> slice (kick / bass / snare / clap / closed hat / open hat / perc), and lays
> the slices out as a groove. `arrange` builds it, `re-arrange` rolls a new one.

## 0. Before writing any code

1. Branch per `.claude/skills/git-branch-workflow/SKILL.md`: `feature/beat-arranger-node`
   off `main`. **Note:** the working tree currently has uncommitted
   `src/main.cpp` changes on `feature/audio-drop-target-picker` (the
   `gAudioDropPicker` popup, ~171 lines). Do not stash or discard them —
   ask the user whether that branch lands first. Section 7 hooks into it.
2. Load and follow, in this order:
   - `.claude/skills/new-audio-node/SKILL.md` (two-object rule, wiring sites, exit criterion)
   - `.claude/skills/audio-node-ui/SKILL.md` and `.claude/skills/node-ui-pillars/SKILL.md` (before any `Draw*Body`)
   - `.claude/skills/rhythmic-quantization-standard/SKILL.md` (the step rate must come from `src/audio/MusicTime.h`)
   - Clean-room rule in `AGENTS.md`: MIT codebase. Cite papers only; do not open GPL code (Essentia is AGPL, aubio is GPL, librosa is ISC but just use the papers below).

## 1. What already exists — reuse, don't rebuild

| Need | Already in the repo | Use it how |
|---|---|---|
| Transient detection | `SlicerDsp::Detect` — `src/audio/dsp/SlicerDsp.h/.cpp` (log spectral flux, SuperFlux max filter, Dixon adaptive peak picking) | Call it unchanged on each dropped sample, on a worker thread, the way `SlicerNode::LaunchJob` does (`src/nodes/SlicerNode.cpp`) |
| Main→audio buffer handoff | `SampleSlot<T>` — `src/audio/SampleSlot.h` | One slot per source sample (8), same as DrumSequencer's lanes |
| 8 per-sample strips with pitch/fine/decay/transient/volume/pan/start/end | `DrumSequencerNode` — `src/nodes/DrumSequencerNode.h:89-104`, lane card UI `DrawDrumLaneCard` (`src/main.cpp` ~16371), waveform `DrawDrumLaneWaveform` (~16261) | Mirror the field layout, dirty-tracked `PushDirtyParams` shadow copies, and the `effTransient = clamp(lane + global)` offset pattern (`DrumSequencerNode.cpp:553`) |
| Transport-locked stepping | `AudioDrumSequencerNode::ProcessBlock` derives the step from `Transport::Beats()` every block | Same approach — no internal step counter |
| Slice-confined voice with attack/decay, speed-correct boundary | `AudioSlicerNode` voice (`SlicerNode.h` class comment explains crossthrough vs decay) | Voices play `[sliceStart, sliceEnd)` of a source buffer, confinement in read-head position so `speed` stretches it correctly |
| Main waveform + playheads | `DrawSlicerWaveform` (`src/main.cpp` ~12552), `SlicerVoiceSnapshot` triple buffer | Base for the main arrangement view |
| Lane drop resolution | `DrumSequencerLaneForCanvasPos` (`src/main.cpp:1922`) + the `FindNodeUnderCanvasPoint<DrumSequencerNode>` drop path (~15387, ~87948) | Same for dropping onto a strip |
| Deterministic RNG | xorshift32 in `src/audio/DspMath.h:227-244` | Seeded pattern generation. **Do not** copy `DrumSequencerNode::Randomize`'s `rand()` — it is not reproducible from a seed |

## 2. New pure-DSP modules (reusable later — no INode, no ImGui, no threads)

### 2a. `src/audio/dsp/DrumClassifier.h/.cpp`

`Classify(const float* mono, int len, double sr, const char* fileNameHint) -> Result { DrumClass cls; float confidence; float scores[kNumClasses]; Features f; }`

`enum class DrumClass { Kick, Bass, Snare, Clap, HatClosed, HatOpen, Perc }` — append-only; the index is saved.

**Features** (definitions from Peeters, "A large set of audio descriptors…", CUIDADO report, IRCAM 2004), computed over the first ~250 ms after the slice's onset, 1024-pt Hann STFT via `PortableFft`:

| Feature | Why |
|---|---|
| Band-energy ratios: sub 20–90 Hz, low 90–250, lowmid 250–1k, mid 1–4k, high 4–10k, air >10k | Main separator between all classes |
| Spectral centroid (mean + trajectory over first 50 ms) | Hats high; kick low; kick's centroid **falls fast** (pitch sweep) |
| Spectral flatness (Wiener entropy) | Noise-like (hat, clap, snare wires) vs tonal (kick body, bass) |
| Zero-crossing rate | Cheap noisiness cross-check |
| Attack time (10%→90% envelope) | Bass/pads slow, drums fast |
| Decay time (peak → −20 dB) | Closed vs open hat; kick vs bass; one-shot vs sustained |
| Low-band pitch stability (autocorrelation f0 in 30–250 Hz over time) | **Bass** = stable f0, long sustain; **kick** = f0 dropping, short |
| Micro-onset count in first 40 ms (re-run the flux ODF at a 128-sample hop over that window, count peaks 5–15 ms apart) | **Clap** signature: 3–4 bursts; a snare has one |

**Decision rule**: weighted rule scores per class → normalise → argmax + confidence (margin between top two). Rule-based rather than a trained model: no training data, deterministic, explainable, MIT-clean. This follows the feature sets shown effective in Herrera, Yeterian & Gouyon, "Automatic Classification of Drum Sounds: A Comparison of Feature Selection Methods and Classification Techniques" (ICMC/ Music & AI 2002) and Gillet & Richard, "Automatic Transcription of Drum Loops" (ICASSP 2004). Put these citations in the header comment, the way `SlicerDsp.h` cites Dixon / Böck.

**Filename prior**: tokens in the file name (`kick|kik|bd|bassdrum` → Kick, `808|sub|bass` → Bass (but `808` + short decay → Kick), `snare|snr|sd|rim` → Snare, `clap|clp|cp` → Clap, `hh|hat|chh|closed` → HatClosed, `ohh|open` → HatOpen, `perc|tom|shaker|conga|cow` → Perc). Adds a fixed bonus to that class's score — a prior, not an override. Only applies to a sample that yields **one** slice; a chopped loop's slices ignore the file name.

**User override always wins**: each strip gets a class dropdown with `auto` + the 7 classes (Section 4).

### 2b. `src/audio/dsp/BeatArranger.h/.cpp`

`Arrange(const std::vector<SliceInfo>& pool, const ArrangeParams& p, uint32_t seed) -> std::vector<ArrangedHit>`

- `SliceInfo { int sample; int slice; DrumClass cls; float confidence; float lenSec; }`
- `ArrangedHit { int step; int sample; int slice; float velocity; float pitchOffsetSemis; }`
- 16 steps × `bars` (1, 2 or 4; default 2), rate from `MusicTime.h`.

Algorithm (role-template + seeded variation):

1. **Role templates** per class: Kick anchored on step 0 of each bar plus seeded syncopations weighted to the "and" positions; Snare/Clap on the backbeat (steps 4 and 12), clap may layer the snare or replace it by seed; HatClosed as a Euclidean `E(k,16)` with k from seed in 6..12 plus accent velocities (Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms", BRIDGES 2005); HatOpen on off-beats, choking closed hats; Bass follows kick with offsets, never on the snare step; Perc fills gaps with a sparse Euclidean.
2. **Missing roles**: if the pool has no kick, promote the lowest-centroid slice with the shortest decay into the kick role, and so on down — the node should always make a groove from whatever it is given.
3. **Slice choice per hit**: within a role, pick slices weighted by confidence so some slices repeat far more than others (the "certain slices repeated more" behaviour). Seeded.
4. **Pitch randomisation**: `pitchOffsetSemis = randPitch * U(-12, 12)` from the same seed, **fixed per hit** (it does not change every loop). Kicks and bass get a narrower range (±3 × randPitch) so the low end stays in tune.
5. Same `(pool, params, seed)` ⇒ byte-identical output. This is what makes `re-arrange` = `seed + 1` and lets a patch reproduce.

## 3. The node: `src/nodes/BeatArrangerNode.h/.cpp`

Two-object rule per `new-audio-node`: `BeatArrangerNode : INode, IAudioSource` (main thread) + `AudioBeatArrangerNode` (audio thread). No note input — free-running from Transport like DrumSequencer. Outputs: `out` + 8 per-sample outs (mirror `DrumSequencerNode::OutputCount`/`OutputLabel`).

Flow:

```
drop file(s) → decode → SampleSlot[i] ─┐
                        worker: SlicerDsp::Detect → per-slice DrumClassifier::Classify
arrange btn  → BeatArranger::Arrange(pool, params, seed) → hits → push to audio (mailbox / slot)
re-arrange   → seed = seed + 1, Arrange again
audio thread → Transport::Beats() → step → trigger hits → slice voices
```

- Analysis happens on a worker thread (mirror `SlicerNode`'s `mWorkerThread` / `mAbort` / `mResultReady` / `JoinWorkerIfDone`). Nothing on the audio thread allocates; the arranged hit list is handed over through a `SampleSlot<HitList>`.
- Dropping more than 8 files: fill empty strips in order, then stop, status line says how many were ignored.
- **The main view starts empty** (per the user) and fills only after `arrange`. Dropping a file does not arrange automatically.
- Persistence: save each strip's path (reload like `DrumSequencerNode::ReloadFromPaths`), the seed, all params, and the **arranged hit list as a blob** (same pattern as `SlicerNode::SerializeSlices` / `mSliceBlob`). Do not re-arrange on load — a changed classifier later must not silently rewrite someone's saved groove.

## 4. Params

Global (main view header, one `AudioKnobRow`, in this order = modulation pin order):

| Param | Range | Notes |
|---|---|---|
| `arrange` / `re-arrange` | button | Label reads `arrange` until a groove exists, then `re-arrange` (seed + 1) |
| `seed` | int 0..9999 | The pattern seed. Editable, so a user can go back to a groove |
| `rand pitch` | 0..1 | Scales the per-hit fixed pitch offsets |
| `transient` | −1..1 | Offset added to every strip's transient (clamped), DrumSequencer semantics |
| `decay` | −1..1 | Offset added to every strip's decay |
| `speed` | 0.25..4 | Multiplies every slice's playback rate (read-head speed, not tempo) |
| `bars` | 1 / 2 / 4 | Pattern length |
| `swing` | 0..1 | Optional — keep only if it fits the row; DrumSequencer has one |
| `volume` | 0..1 | Standard bottom-right output level per node-ui-pillars |

Per strip (8 strips below the main view, mirror `DrawDrumLaneCard`):
waveform thumb with slice markers + class colour, class dropdown (`auto` + 7), `pitch` (±24 st), `fine` (±50 ct), `speed` (0.25..4), `transient`, `decay`, `volume`, `pan`, mute/solo, clear `x`. Seven knobs max per strip (memory rule: keep audio nodes plugin-simple). `start`/`end` trims are optional; leave them out unless the card has room.

Every param goes through `VisitParams` so it saves, undoes and modulates. Strip arrays use indexed names (`pitch1`..`pitch8`), mirroring `DrumSequencerNode.cpp:667`.

## 5. Main arrangement view (Life/XO look)

- Full-width strip drawn with `ImDrawList` (base it on `DrawSlicerWaveform`): the pattern timeline, one coloured block per arranged hit, width = slice length at current speed, colour = class. Fixed palette per class, readable in light and dark theme (node-ui-pillars contrast budget).
- Playhead from the audio-thread snapshot (same triple-buffer shape as `SlicerVoiceSnapshot`).
- Empty state: centered text `drop samples, then arrange`.
- Click a block → audition that slice. Nice-to-have, not required.

## 6. main.cpp wiring (follow `new-audio-node` for the full list — these are the confirmed DrumSequencer sites to mirror)

| Site | DrumSequencer line (approx.) |
|---|---|
| `#include` | 228 |
| `REGISTER_NODE(..., "Synths")` | 5879 |
| Reload-on-load block | 7924 |
| `AudioNodeWidth` → `kAudioWideWidth` | 11648 |
| Body draw dispatch | 24094 |
| File drop onto a node / strip | ~15387, ~87948 (and `DrumSequencerLaneForCanvasPos` at 1922) |
| Sample-browser drag release | ~88623 |
| Self-test fixture registration | ~54147 |
| `CMakeLists.txt` sources | 535 (add the node + both DSP files) |

Also add the node to the right-click help tables (memory note: help is a 5-level fallback across 4 hand-kept tables — check both directions).

## 7. Drop-target picker

If `feature/audio-drop-target-picker` has landed, add `Beat Arranger` to the `gAudioDropPicker` list of nodes that accept a sample, and when several files are dropped at once, load all of them (up to 8) into the new node's strips.

## 8. Tests (self-test fixture in main.cpp, pattern of `RunDrumSequencerFixture` ~51872)

1. **Classifier**: synthesise in code (no asset files): a 50→40 Hz pitch-dropping sine burst with 150 ms decay (Kick), a steady 55 Hz saw with 800 ms sustain (Bass), 200 Hz tone + band-passed noise (Snare), four 10 ms noise bursts 8 ms apart (Clap), 40 ms high-passed noise (HatClosed), 400 ms high-passed noise (HatOpen). Each must classify correctly. Filename-prior test: an ambiguous signal named `kick_01.wav` flips to Kick.
2. **Arranger determinism**: same pool + seed ⇒ identical hit list; seed + 1 ⇒ different list.
3. **Missing-role promotion**: a pool with no kick still produces hits on step 0.
4. **Round trip**: save → load keeps the hit list byte-identical and does not re-arrange.
5. **Teardown**: delete the node mid-playback — covered by `audio-node-sweep` (`AUDIOTEARDOWNSWEEPTEST`); make sure the new node is in its type list.

## 9. Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then run `.claude/skills/run-infinite-hygiene/SKILL.md` (includes the audio param/teardown sweeps and the all-node-types round trip) and `.claude/skills/audio-node-sweep/SKILL.md`. Copy `build/Infinite.app` to `~/Desktop/Infinite.app`. Windows/Linux: DSP files are portable C++ only; no `Platform::` changes needed — confirm with the `windows-parity` checklist if anything platform-shaped is touched.

## Out of scope

- A trained ML classifier. The rule-based one plus user override is v1. `DrumClassifier`'s API (`Features` exposed in `Result`) leaves room for a model later.
- Changing `SlicerNode` or `DrumSequencerNode` behaviour. Reuse, don't edit. The only exception is lifting a helper into a shared header when it would otherwise be copied verbatim.
- Groove templates per genre (trap, house, …). One neutral template set; `seed` gives the variety.
- Exporting the arrangement to MIDI or the timeline.

## Open choices (recommendation given, confirm with the user)

- **Name**: `Beat Arranger` (recommended) vs `Groove Sampler`.
- **New node vs a DrumSequencer mode**: new node recommended. DrumSequencer's lanes are one sample = one lane; here one sample can become many slices spread over many roles, which would bend its model.
- **Strips = samples, not slices**: per the request, the 8 strips are the 8 dropped files. A chopped loop's slices share its strip's settings.

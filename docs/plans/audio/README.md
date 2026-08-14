# Real-time audio in Infinite — feasibility, consolidation, phases

Revision 2. Supersedes the first draft's 40-node list.

Written against: Infinite's graph substrate (`src/core/`, the wiring sites in
`src/main.cpp`), and the user's own BespokeSynth fork at
`/Users/namansoni/BespokeSynth` (PR #2089 — Wavetable, Tracker, MolderSampler,
PaulStretch, MetallicSynth, MutableResonator, NoiseSynth, Macro*, LimiterEffect,
DiceSynth, Composite, EffectMatrix, SearchPanel).

---

## 1. Is it heavy? — measured, not estimated

**No. The DSP is not the cost. Drawing is.**

Measured on this machine (Apple M2, 8 cores / 4 performance):

| Observation | Value |
|---|---|
| `BeSpokeMod` running, sampled over 6 s | **41.8 – 43.1 % of one core** (≈5 % of the machine) |

That number is almost entirely **Bespoke's immediate-mode CPU UI**, which
redraws every module every frame on the CPU. Infinite does not work that way —
it draws through ImGui + GL on the GPU. So Infinite does **not** inherit that
cost, and the audio addition is only the DSP, which runs on its own thread and
never touches the render thread.

What a realistic "full patch" actually costs at 48 kHz on an M2:

| Load | Cost (one core) |
|---|---|
| 16-voice poly: PolyBLEP osc + SVF + ADSR per voice (~60 ops/sample/voice) | ~1–3 % |
| 8×8 FDN reverb | < 1 % |
| Tempo-synced delay, EQ/filter, compressor, stereo | ~1 % each |
| Drum sequencer + sample playback, 8 lanes | ~1 % |
| **Typical full patch (2 synths, drums, 6 effects)** | **~10–20 % of one core ≈ 1.5–2.5 % of the machine** |

The three things that *are* expensive, and each has a mitigation already
written into the phases below:

1. **Scopes and waveform views.** 20 nodes each drawing 1024 ImGui line
   segments per frame will cost more than every synth and effect combined.
   → decimate to ~128 points, cap redraw at 30 Hz, collapse scopes by default.
2. **Spectral / granular modes** in the Sampler (FFT per grain). ~5–15 % of a
   core when active. → they are modes the user opts into, not always-on.
3. **Sample-library scanning.** → background thread, persisted index, never on
   the audio or render thread (this is exactly what your `SearchPanel.cpp`
   already does).

**Verdict: comfortably feasible.** The visual side is and remains the
expensive half of this app.

## 2. Clean-room: every line written from scratch

**Decision, settled — no code is ported from BespokeSynth.** Infinite is MIT
(`LICENSE`, "Copyright (c) 2026 n1m21n"); BespokeSynth is GPLv3. Even though
you authored the PR #2089 modules and retain copyright on your own lines,
writing fresh removes every provenance question and leaves you holding
unambiguous rights to the whole of Infinite. That is worth more than the
time it saves.

### What this does and does not cost you

Copyright protects **the expression of code, not ideas.** So the following are
yours to use freely and are *not* affected by this decision:

- the **node taxonomy and consolidation** in §3 — which nodes exist, what
  merges into what
- **parameter sets and ranges** — that a sampler has 5 engines, that a
  compressor exposes ratio/knee/attack/release
- **DSP algorithms themselves** — PolyBLEP, TPT/Zavalishin SVF, RBJ biquad
  cookbook, FDN reverb topologies, Voss-McCartney pink noise, phase-vocoder
  stretch. These are published literature. Implement each from its **primary
  reference**, not from anyone's existing implementation — it is faster to
  verify against the paper's own test cases anyway.
- **everything you learned building it** — which is the actual asset

What it costs: the ~40 % head start the existing code would have given is
gone. Budget **P3b (synths) as the heaviest phase**, not the cheapest, and
expect Sampler and Wavetable to each be a session of their own.

**A hard rule for every implementing session**, to keep the clean-room claim
clean and stated in each phase prompt:

> Do not open, read, grep, or reference `/Users/namansoni/BespokeSynth`.
> Work only from this document's specification and the cited DSP literature.

### Feature reference — what to build, not what to copy

Your Bespoke modules are the best available **specification** of the behaviour
you want. Use this table to write the feature list into each phase prompt; do
not use it as a reading list.

| Bespoke module | Lands in | Behaviour worth specifying |
|---|---|---|
| MolderSampler | **Sampler**, displayed as "sample player" (shipped) | Shipped minimal first (load/tune/start/loop/volume + waveform+playhead), then grew on direct user request: pitch+finetune (coarse/fine tuning), a -2..2 varispeed `speed` control, a record input pin (captures whatever's cabled in, becomes the loaded buffer), an interactive waveform (click anywhere to audition from that point; drag its two edge handles to set the loop range), and independent reverse/ping-pong toggles for what happens at the range edges. Registered type name stays "Sampler" for patch compatibility - only DisplayName() changed, same pattern "Dynamics" -> "compressor" already uses. Still not the 5-engine/onset-detection spectral spec below - that's left here as a still-open idea, not a to-do this covered. |
| Wavetable | **Wavetable** | dual A/B oscillators, per-oscillator filter + envelope, unison, waveform preview drawing |
| Tracker | **Drum Sequencer** | step sequencer with per-step sample, volume, decay, pitch and repeat; transport sync; randomise |
| PaulStretch | **Pitch Time** | extreme spectral time-stretch |
| MetallicSynth, MutableResonator | **Resonator** | modal / resonator-bank synthesis |
| Maze | **Note Sequencer** | generative chord engine: scaled degrees, chord count, groove, strum, upper harmonics, humanise timing + velocity |
| MacroKnobs, MacroBars, MacroXY | **Macro** | three layouts of the same multi-target control |
| LimiterEffect | **Dynamics** | limiter mode |
| SearchPanel | **P3e sample browser** | background indexing, filter-as-you-type, disk-persisted index, per-location rescan |

Note what this table also tells you: **9 of your modules collapse into 8
Infinite nodes**, which is the consolidation in §3 arrived at independently.

**Honest estimate: the existing code removes ~40 % of total effort**, and it
removes it from the highest-risk phases. It does not remove the plumbing
phases, which are where this project's actual risk lives.

---

## 3. Consolidation — 55 nodes down to 30

Your instinct is right, and it goes further than you proposed. The rule I
applied: **merge when the nodes share a signal path and differ only in
parameters.** Keep separate when the *interaction model* differs, because a
dropdown that swaps the entire UI is a worse node than two nodes.

### Notes: 19 → 7

| One node | Replaces | How |
|---|---|---|
| **Note Sequencer** | note sequencer, euclidean sequencer, polyrhythmic, note recorder | `pattern` = Grid / Euclidean / Polyrhythm — all three are "choose steps on a grid", only the step-selection rule differs. Note recorder becomes a **record-arm button** on the grid, which is how every DAW does it and is better UX than a separate node. |
| **Arpeggiator** | arp | up/down/updown/random/as-played, octaves, rate |
| **Note Filter** | scaling, quantiser, note range filter, note chance | All four are *gates on a note's pitch*: snap-to-scale, pass-if-in-range, pass-if-lucky. Scale and quantiser are the same operation. |
| **Note Modify** | transposing, note duration, note panning, velocity expressions, note expressions | All are *"change an attribute in flight"*: pitch offset, octave, velocity curve, gate length, pan, humanise. You almost never want just one. **Biggest single win.** |
| **Note Echo** | note echo | generates new notes over time — genuinely distinct from Modify |
| **Note Router** | note distributor, note chain | `mode` = round-robin / random / probability / chain. 1-in, 4-out. |
| **Note Display** | note displayer, keyboard displayer | `view` = piano roll / keyboard. Same data, two skins. |

**Dropped as nodes, folded into params:** *pitch bend* and *portamento* are
per-voice synth behaviour (`glide` is an Oscillator param); pitch-bend from
hardware is a modulator output on **MIDI In** (upgrade of the existing
`MidiTriggerNode`, which already has a keyboard mode).

### Modulators: 10 → 4 new, 2 extended

The shared family. These drive **audio params and visual params identically** —
that already works today via `Modulation::Bind`, no new code needed.

| One node | Replaces | Note |
|---|---|---|
| **Envelope** | ADSR | note-triggered; also the "flash the visuals on a note" node |
| **Shaper** | curve | curve editor → transfer function. `DrawCurveEditor` already exists in `main.cpp:1388` — reuse it. |
| **Mod Recorder** | modulation recorder | record + loop a modulator |
| **Macro** | macro knobs, macro sliders/bars, macro XY, *and the existing* `Macro Knob` + `Macro XY` | `layout` = knobs / bars / XY, `count` = 1–8. **4 nodes → 1, and it deletes two existing ones.** |
| *(extend)* **Pattern** | control sequencer | existing node: 8 → 16 steps, add curve interpolation. **No new node.** |
| *(extend)* **Audio Analyze** | audio→modulator, pitch→modulator | existing node: add pitch-track and envelope-follower outputs to its 13 taps. **No new node.** |
| *(moves)* waveform viewer | → **Scope**, in Utility | it is a display, not a modulator |

### Effects: 15 → 7 (superseded 2026-08-14 — see below)

**2026-08-14 update: expanded back out to 15 effects per explicit user
request**, pointing at a screenshot of the KHS Audio plugin suite as the
params/knobs reference. Bitcrush, Transient Shaper, Stutter, Ring Mod and
Formant Filter, none of which appear in the table below, were added as new
standalone `AudioEffectNode` entries rather than folded into an existing
node — see `STATUS.md`'s P3c section for the shipped list and each kernel's
own class-comment for its DSP reference. The consolidation reasoning below
(Bitcrush → part of Drive's `curve`, Stutter → a Delay `mode`) is what this
session overrode, not what shipped.

| One node | Replaces | Why it's one node |
|---|---|---|
| **Audio Filter** | filter, EQ | `bands` = 1–4, each with type/freq/Q/gain. An EQ *is* filters in series. |
| **Dynamics** | compressor, limiter, gain staging | Identical signal path — detector → gain computer → makeup. A limiter is ∞:1 with a hard knee. `mode` = compress / limit / gate / expand. Sidechain input. |
| **Delay** | delay, freq delay, stutter | `mode` = simple / ping-pong / multiband / stutter. All one fractional-delay buffer. *(Stutter is the loosest fit here — split it out if the UI fights.)* |
| **Reverb** | reverb, convolve | `engine` = algorithmic (FDN) / convolution (load IR). Same node, same wet/dry/predelay. |
| **Drive** | distortion | transfer curve: tanh / hard clip / foldback / bitcrush + downsample, 4× oversampled |
| **Pitch Time** | pitch shifter, paulstretch | `mode` = shift / stretch / paulstretch. All spectral/granular on one engine. **Your PaulStretch code lands here.** |
| **Stereo** | stereo, panning, mono | pan, width, mono-fold, mid/side |

**Dropped entirely: "audio shaper (ShaperBox-style)".** It does not need to
exist — it *is* **Shaper** (modulator) patched into **Gain**/**Audio Filter**'s
param pin. That is the whole point of having a node graph, and it is more
powerful than ShaperBox because the same curve can also drive a visual. Ship it
as a documented idiom and a starter patch instead of a node.

*audio splitter* → moves to Utility.

### Synths: 12 → 5

| One node | Replaces | Why |
|---|---|---|
| **Oscillator** | oscillator, FM synth, noise synth | Waveform (sine/saw/square/tri/pulse/white/pink/brown) + a 2-operator FM section (ratio + index) + unison/detune. A 2-op FM *is* an oscillator modulated by an oscillator. **Caveat: this does not give you 4–6-op DX-style FM.** If you want that, it is a separate node later — say so now rather than discovering it in Phase 3. |
| **Wavetable** | wavetable | Kept separate from Oscillator: A/B morphing, per-osc filter+env and table previews are a different UI, and you have 933 lines of it already. |
| **Sampler** | molder, granulator, paulstretch(playback), looper | **Your MolderSampler already is this consolidation** — 5 engines + live recording. Looper = live-record + loop mode. **4 → 1, and the code exists.** |
| **Drum Sequencer** | drum sequencer, tracker | Your Tracker *is* a drum sequencer with richer per-step data (sample/vol/decay/pitch/repeat). One node. |
| **Resonator** | metallic synth, mutable resonator | Both are banks of resonant filters. `model` = metallic / modal / string / plate. |

### Utility: 7

**Mixer** (4–8 ch, gain/pan/mute/solo) · **Audio Out** (device + channel
selection, master limiter, clip indicator) · **Audio In** · **Gain** ·
**Splitter** (1→4) · **Scope** (`view` = waveform / spectrum / meter — replaces
waveform viewer + meter + spectrum, and its spectrum mode emits an **image
texture**, the audio→visual bridge) · **Recorder** (sample recorder → disk or
into a Sampler slot).

### Totals

| | Yours | This plan |
|---|---|---|
| Notes | 19 | 7 |
| Modulators | 10 | 4 new + 2 extended |
| Effects | 15 | 7 |
| Synths | 12 | 5 |
| Utility | 3 | 7 |
| **Spawnable node types** | **~55** | **30** |
| **Hand-written classes** | — | **~24** |

### Name collisions — verified against the live registry

I extracted all 119 currently-registered node names and checked. **Six collide:**

| Wanted | Collides with | Use instead |
|---|---|---|
| `Noise` | `Noise` (Source, `NoiseNode`) | *no node needed* — a waveform on **Oscillator** |
| `Curve` | `Curve` (3D) | **Shaper** |
| `Curves` | `Curves` (Color) | **Shaper** |
| `Shape` | `Shape` (Source, `ShapeNode`) | **Shaper** |
| `Pattern` | `Pattern` (Modulators) | *reuse it* — extend the existing node |
| `Transform` | `Transform` (3D) | **Note Modify** |

`Filter` does not collide (the `FilterDefs` entries are `Blur`, `Sharpen`, …)
but is ambiguous in the search panel — use **Audio Filter**.

---

## 4. Phases

Your requested shape: feasibility → base architecture → node families →
implementation → test design → testing.

One adjustment, and only one: **the test *harness* is infrastructure and must
be built in P1**, because DSP written without a way to hear/measure it is
debugged by ear, which is slow and unreliable. What belongs in P4 is *designing
and authoring the per-node tests*. Everything else follows your order.

### P0 — Feasibility (½ day, throwaway code)

Env-gated `INFINITE_AUDIOSPIKE`. One process-wide `AVAudioEngine` +
`AVAudioSourceNode` rendering a 440 Hz sine. No nodes, no UI.

**Produces numbers:** actual callback block size and sample rate; callback
jitter; **FPS delta with a heavy visual patch running** (this is the question
that matters); whether `AVAudioSourceNode` suffices or a raw AUHAL output unit
is needed.

**Exit:** 60 s of glitch-free sine alongside a heavy visual patch, FPS within
noise. Throw the code away; keep the numbers.

### P1 — Base architecture

New directory `src/audio/`, deliberately not `src/nodes/`.

- `AudioEngine` — the singleton: device, callback, flattened process order,
  **atomic topology swap** (build the new list on the main thread, publish a
  pointer, retire the old one later), FTZ/DAZ denormal guard, xrun counter
- `AudioBuffer` — non-owning block view · `AudioNode` — `ProcessBlock` /
  `PrepareToPlay` / `Reset`
- `ParamMailbox` — lock-free main→audio, per-block one-pole smoothing (~5 ms)
- `MeterRing` — SPSC ring, audio→main, pre-decimated for scopes
- `DspMath` — PolyBLEP, TPT SVF, RBJ biquads, one-pole, dB↔lin, equal-power
  pan, fast tanh
- `AudioVoice` — voice allocator (round-robin + oldest-steal) + ADSR, shared by
  all five synths
- **`INFINITE_DSPTEST`** — headless, no device: render N blocks, assert on
  samples. This is the harness, built now, populated in P4.

**The rule this phase establishes, which every later phase depends on:**

> An audio node is **two objects**: an `INode` on the main thread (UI, params,
> save/load, pins) owning an `AudioNode` on the audio thread (`ProcessBlock`
> only). They communicate through the mailbox and the ring — never by sharing
> mutable fields. `CookIfNeeded` on an audio node does **no DSP**: it drains
> meters and pushes dirty params, budget < 5 µs.
>
> On the audio thread: no allocation, no locks, no `dynamic_cast`, no
> `std::function`/`map`/`string`, no GL, no ImGui, no file I/O, no `printf`.

**Exit:** hardcoded osc→filter→out renders to the device *and* to a test
buffer, both verified. Zero node-editor changes.

### P2 — Node family introduction (cables, pins, categories)

The mechanical, error-prone phase. Do it with only three nodes existing
(Oscillator, Gain, Audio Out) so mistakes are obvious.

Two new cable types — **Audio** (blue) and **Note** (green) — through every
wiring site. This list *is* the deliverable:

1. `InputCountFor` 2. an `AudioCableFor`/`NoteCableFor` beside `CableFor`
3. the connect path beside `ConnectGeometrySlot` 4. `IsInputSlotCompatible`
5. `QueryNewLink` accept/reject + pin colours 6. link-table rebuild + tinting
7. **`DisconnectLinkById` / `DisconnectAllTo` / `RemoveNodeByIndex`**
8. `Patch.h/.cpp` — two record lines, `audio` and `note`, shaped like `geo`
9. `BuildPatchData`/`ApplyPatchData` 10. `CategoryColors` — 4 new categories

**Do #7 generically.** Add `AudioInputSlot(int)` / `NoteInputSlot(int)` to
`INode` mirroring `GeometryInputSlot`, so disconnect is a loop, not a
hand-maintained ladder. `docs/plans/phase2-one-geometry-interface.md` documents
this exact hazard on the geometry side — its own comment warns a missing entry
"would leave a pointer to a freed node and crash on the next cook." Do not
re-earn that lesson.

**Connection rules to enforce** (reject with the existing red flash):
audio→param pin (force an Envelope Follower), audio↔image, note↔audio,
geometry→audio, modulator→audio input, and **any cycle in the audio graph**
(the visual graph allows cycles via `FeedbackNode`; audio must not, or the
topological sort deadlocks — state this in the code or someone will "fix" it).

**Exit:** Oscillator → Gain → Audio Out plays; survives save/load/undo/copy/
paste/delete; every invalid connection in the matrix is rejected.

### P2.5 — Transport (small, do it here)

Flip the clock: the audio callback advances a sample counter, `Transport::
Beats()` reads an atomic, the frame-driven `Tick` becomes the engine-stopped
fallback. Sample-accurate note scheduling depends on this, and **the visual
modulators get a clock that no longer stutters when the GPU hitches.** Must
land before P3a, because note scheduling is written against whatever clock
exists at the time.

### P3 — Implementation

- **P3a — Notes** (7 nodes) + Envelope, on the note cable with block-offset
  scheduling. Note Sequencer's generative mode implements the chord/groove/
  strum/humanise behaviour specified in §2.
- **P3b — Synths** (5 nodes) on the shared voice allocator. **The heaviest
  phase** — budget Sampler and Wavetable a session each. Order within the
  phase: Oscillator (proves the voice allocator) → Drum Sequencer → Resonator
  → Wavetable → Sampler.
- **P3c — Effects** (7 nodes). One `AudioEffectNode` class with a table-driven
  param UI (the `FilterDefs` pattern) dispatching to 7 DSP kernels in separate
  files. **Be realistic: the UI is table-driven, the DSP is not** — a reverb
  and a compressor share no code.
- **P3d — Utility** (7 nodes) + the visual bridge (Scope's spectrum → texture,
  Envelope Follower → modulator). **Enforce the §1 drawing rules here** —
  decimated rings, 30 Hz redraw, scopes collapsed by default.
- **P3e — Sample browser (shipped).** Extended the existing docked
  node-browser panel with a **Samples mode**: per-folder add/remove,
  filter-as-you-type, a `SampleScanner` background `std::thread` scan (the
  first of its kind in this codebase - see its header comment), disk-
  persisted index (`SampleFolders.json`/`SampleIndex.json` via crude_json,
  loaded on startup with no rescan - only an explicit Refresh triggers one),
  and drag-and-drop onto either empty canvas (spawns a loaded Sampler) or an
  existing Sampler's body (swaps its file). The drag itself is hand-tracked
  (`gSampleDragActive` in `main.cpp`), not ImGui's native drag-drop API,
  because the drop target is imgui_node_editor's internally-managed canvas.
  Shipped alongside a minimal **Sampler** node (see the table above) as its
  prerequisite drop target.

### P4 — Test design

Author `INFINITE_*TEST` fixtures against the P1 harness, following the
existing convention exactly (see §7): spawn a fixture graph, step N frames,
`printf` a verdict line ending `OK` / containing `FAIL` / ending `BUG`.

DSP is far more testable than pixels. Per-node fixtures:

SVF frequency response at known cutoffs · PolyBLEP aliasing below a threshold ·
ADSR segment times · delay accuracy in samples · reverb RT60 · dynamics
gain-reduction curve vs. analytic · denormal-free decay tails · note
scheduling landing on exact sample offsets · voice-steal correctness.

**Two generic sweeps, not one fixture per node.** This is the lesson
`geometry-transform-sweep` already encodes — a fixture someone remembered to
write covers one node; a sweep covers every node including the ones added
next month:

| Sweep | Invariant |
|---|---|
| `AUDIOPARAMSWEEPTEST` | For **every** audio node type: every registered param survives a save/load round trip, and moving a param on the main thread reaches the audio thread's smoothed value within one block. Catches the "forgot to push it through the mailbox" class — the audio analogue of the dropped-`GetMappingTransform()` bug. |
| `AUDIOTEARDOWNSWEEPTEST` | For **every** audio node type: spawn it, wire it into a running graph, delete it mid-playback, keep rendering. Asserts no crash and zero xruns. This is `DELETECRASHTEST` for the audio graph — the use-after-free surface P2 exists to remove. |

### P5 — Testing and hardening

Run the sweeps and fixtures. Then: xrun counter surfaced in the UI (silent
dropouts are the worst failure mode) · device change / sample-rate change /
sleep-wake recovery · per-node CPU meter and a patch-wide budget readout ·
update `ARCHITECTURE.md` (stale: claims 9,146 lines, actual 14,798) with an
Audio Engine section as category 7.

---

## 5. Ordering rationale

**P0 before everything.** If a glitch-free callback can't coexist with the GL
loop on this machine, every later design changes. Half a day.

**P2 before P3.** The only phase that *reduces* future cost. Every node added
before the generic `AudioInputSlot` accessor exists is another entry someone
must remember in `DisconnectAllTo` — and forgetting one is a use-after-free,
not a glitch.

**P3c's table before writing effects one at a time.** Same argument, one level
down.

**Consolidation before implementation, not after.** Merging Note Transpose +
Duration + Pan + Velocity into Note Modify costs nothing today and is a
migration with patch-format fallout once patches exist.

## 6. How to execute this — one phase, one session

**Do not hand this whole document to an implementing session.** It is an
evaluation, not a prompt. Each phase becomes its own self-contained prompt,
written by the **`write-fix-brief`** skill, executed by a fresh session that
has never seen this conversation — the same pattern the geometry roadmap in
`docs/plans/` already uses.

For each phase, in order:

```
/write-fix-brief  P1 of docs/plans/audio/README.md
```

That skill's job here is the **verification step**: before writing the prompt
it re-greps the codebase to confirm the line numbers, function names and
claims in this document are still true. They will drift — `ARCHITECTURE.md`
is already stale by 5,600 lines, and `main.cpp` moves every commit. A prompt
built on stale line numbers costs the implementing session several rounds of
rediscovery, which is exactly what the skill exists to prevent.

Every phase prompt it produces must carry these four, because a fresh session
cannot infer them:

1. **The clean-room rule from §2, verbatim** — do not read
   `/Users/namansoni/BespokeSynth`. This is the one instruction that, if
   dropped, silently undoes the licensing decision.
2. **The two-object rule from P1** — `INode` on the main thread owns an
   `AudioNode` on the audio thread; `CookIfNeeded` does no DSP. Every
   audio-node phase re-states it.
3. **The audio-thread prohibitions** — no allocation, locks, `dynamic_cast`,
   `std::function`/`map`/`string`, GL, ImGui, file I/O, `printf`.
4. **The exit criterion**, as something machine-checkable.

Phases P2 and P3a–P3e are each one session. P3b is five (one per synth). P1
is likely two — engine first, then `DspMath` + `AudioVoice`.

## 7. How to test it

Infinite has no separate test binary. Tests are ~50 `getenv("INFINITE_…")`
fixtures compiled into the app, driven by env vars, printing verdict lines —
see `ARCHITECTURE.md` §6 and the `run-infinite-hygiene` skill. **Audio tests
follow that convention exactly**; do not introduce a second test mechanism.

**Every phase ends by running the existing suite**, not just the new checks —
the audio work touches `INode`, `Patch`, and the connect/disconnect paths that
167 node types share, so the regression surface is the whole app:

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

Use `--skip-build` while iterating. It prints `[pass]`/`[FAIL]`/`[CRASH]` per
check, writes full logs to `/tmp/infinite_test_<NAME>.log`, and exits non-zero
on any failure. Also read `/tmp/infinite_hygiene_shot.png` with the Read tool —
pass/fail lines do not catch "renders blank".

Three checks in that suite are the ones audio work is most likely to break,
and P2 in particular must leave all three green:

- **`ROUNDTRIPTEST`** — every registered node type through copy/paste +
  save/load. Each new audio node must be added to it, and the audio and note
  cable records must survive.
- **`PATCHTEST`** — patch format round trip, extended in P2 with an audio
  patch.
- **`DELETECRASHTEST`** — the use-after-free fixture. Its audio counterpart is
  `AUDIOTEARDOWNSWEEPTEST` (P4).

**Register the new sweeps with the skill, don't leave them standalone.** Once
`AUDIOPARAMSWEEPTEST` and `AUDIOTEARDOWNSWEEPTEST` exist, add them to
`run-infinite-hygiene`'s curated list and its coverage table, and give the
audio sweeps their own driver in the shape of
`geometry-transform-sweep/driver.sh`. That skill's SKILL.md documents how to
add a node type to a sweep — audio nodes should be enrolled the same way, so a
node added in a later phase is covered automatically rather than by whoever
remembers.

Finally, P0 and the FPS claims in §1 are themselves a test: measure FPS with a
heavy visual patch, audio engine off vs. on, and record the delta in the P0
exit notes. If that number is ever not "within noise", the two-object rule has
been violated somewhere.

## 8. Explicitly out of scope

No VST/AU hosting · no arrangement timeline · no MPE or microtuning · no audio
feedback loops in the graph (Delay owns its own) · no per-node audio device ·
no 4–6-op FM (2-op lives inside Oscillator; revisit only if it proves limiting).

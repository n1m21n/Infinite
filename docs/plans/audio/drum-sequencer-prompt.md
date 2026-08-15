# Implementation prompt — Drum Sequencer node

Hand this whole file to a fresh Claude Code session in `/Users/namansoni/infinte`.
Everything below was verified against the tree at commit `f50cef7`.

---

Implement the **Drum Sequencer** node in Infinite (`/Users/namansoni/infinte`).

Category `Synths`. Shape: **audio source with a note input** — `INode` +
`IAudioSource`, `NoteInputSlot(0)`. Same shape as the shipped `SamplerNode`
(`src/nodes/SamplerNode.h:28`), which is the closest existing node in the tree
and the one to read first.

Spec: `docs/plans/audio/README.md:99` and `:203` (the Tracker row — "step
sequencer with per-step sample, volume, decay, pitch and repeat; transport
sync; randomise"), and `STATUS.md`'s P3b row. **That spec is per-*step*
sample/vol/decay/pitch/repeat. This prompt deliberately moves all of it to
per-*lane* and keeps only on/off + velocity per step** — see §1. Where this
file and the README disagree, this file wins; update the README row at the end.

Read `.claude/skills/new-audio-node/SKILL.md` for the procedure and
`.claude/skills/audio-node-ui/SKILL.md` (+ `docs/plans/audio/audio-node-ui-system.md`)
for the body layout. Both are prescriptive — do not re-derive either.

Five rules that override anything you infer:

1. **Clean room.** Do not open, read, grep or reference
   `/Users/namansoni/BespokeSynth` or any GPLv3 source. There is no exotic DSP
   here; everything needed already exists in `src/audio/DspMath.h` and
   `src/nodes/SamplerNode.cpp`.
2. **Two objects.** `INode` (main thread) owns `AudioDrumSequencerNode` (audio
   thread), forward-declared in the header and held in a `std::unique_ptr` with
   an out-of-line ctor/dtor. They talk only through `ParamMailbox` /
   `std::atomic` param mirrors and `MeterRing`.
3. **`CookIfNeeded` does no DSP.** Drain meters, push dirty params, retire dead
   sample buffers. Budget < 5 µs — and note this node has 8 lanes, so a naive
   "push every lane's every param every cook" is 100+ atomic stores per frame;
   push on dirty only.
4. **Audio thread:** no allocation, locks, `dynamic_cast`, `std::function` /
   `map` / `string`, GL, ImGui, file I/O, or `printf`. Sample buffers are
   decoded on the main thread and handed over; see §4.
5. **Minimalism.** The step grid is the visualizer. Simultaneously visible
   controls are **5 lane sliders + 4 global controls + 3 buttons** (run,
   randomise, clear). Do not add per-step pitch, per-step repeat/ratchet,
   pattern banks, a second edit mode, a note-output pin, a per-lane filter, or
   a per-node tempo. Every one of those is listed in §7 as explicitly out of
   scope, with the reason.

---

## 1. The design (all decisions resolved — implement as written)

### 1a. Why per-lane parameters and not per-step

The README's Tracker row specifies per-step sample, volume, decay, pitch and
repeat. Implementing that means the node has five parallel 8×16 value grids and
therefore needs a **mode selector to say which grid you are editing** — and
`audio-node-ui`'s "node layout must not resize when a mode changes" plus the
minimalism rule both push hard against that. It also makes the patch file
carry 640 floats for a pattern that is, in practice, 90 % zeros.

The resolution used by every hardware drum machine: **the step grid carries
trigger + velocity; everything else belongs to the pad.** You select a lane,
and one strip of sliders edits that lane. That is one grid, one strip, no mode
selector, and the same expressive range in practice.

### 1b. Structure

**8 lanes × 1–16 steps.** `steps` defaults to **16**; set it to 8 for the 8×8
grid. It is an int param, range 1–16, and the grid redraws to fit the content
width whatever it is (the grid is the only element whose cell size varies —
the node width never changes).

> 16 is the cap because at `kAudioNodeWidth` (440) with an 84 px lane gutter,
> 16 steps gives ~21 px cells, which is the smallest reliably clickable cell.
> 32 steps would need `kAudioWideWidth` (960) and a second lane-strip column;
> that is a later call, not this session's.

**Step length** is a `MusicTime::RateDivision` dropdown (`src/audio/MusicTime.h:21`),
default `kSixteenth`. **Do not add a separate time-signature control** —
`Transport::SetTimeSignature` already exists and is already surfaced next to
BPM (`STATUS.md` §0.2), and `MusicTime::BeatsFor` already reads the live
signature through `BarsToBeats`. The node reads the global signature; it does
not own one. A 6/8 pattern is `steps = 12` at `1/8`, and the bar markers in the
grid come from `Transport::Instance().BeatsPerBar()`.

### 1c. Per-step data — trigger + velocity, nothing else

`float steps[8][16]`, `0.0f` = off, `> 0` = on at that velocity.

- **Click** a cell toggles it (on → `0.8f`).
- **Click-drag horizontally** paints the toggle state of the cell where the
  drag started, so you can draw a 16th-note hat line in one gesture.
- **Vertical drag on a lit cell** sets its velocity 0.05–1.0.
- The cell draws as a **fill whose height is its velocity**, so velocity is
  readable across the whole pattern at a glance with no second edit mode. An
  off cell draws its empty frame (never a blank area — `audio-node-ui` §3f).

### 1d. Per-lane parameters — the strip under the grid

Clicking a lane's name in the left gutter selects it (selected lane's gutter
row is tinted; `selectedLane` is a param and survives save/load). The strip
edits the selected lane only:

| Param | Range | Default | Notes |
|---|---|---|---|
| `volume` | 0 – 1 | 0.8 | multiplied by step velocity and by the note-in velocity |
| `pan` | −1 – +1 | 0 | equal-power, `DspMath` already has it |
| `pitch` | −24 – +24 st | 0 | plain playback-rate repitch, same as `SamplerNode::pitch` |
| `decay` | 0 – 1 | 1 | 1 = play the sample out; below 1 applies an exponential amp decay whose time constant scales to the sample length. This is the 808 decay knob, and it is what makes one kick sample usable as both a boom and a tick |
| `transient` | −1 – +1 | 0 | bipolar attack shaping, per voice: `> 0` shortens the amp attack toward 0 and adds up to +4 dB over the first ~15 ms; `< 0` lengthens the attack to up to ~40 ms. **Do not reuse `src/audio/dsp/TransientShaperKernel.h`** — that is a bus-level envelope-difference detector operating on a summed signal, which is the wrong tool for a per-voice, trigger-synchronous envelope where the exact attack moment is already known. |

**These five are horizontal sliders (`AudioSlider`), not knobs.** This is a
deliberate, documented exception to `audio-node-ui`'s "knob by default" rule.
Write the reason in a comment above the strip: these are values compared
*across eight lanes* — the same category the rule already grants faders to for
the Mixer's channel gains — and a bank of dials directly under a step grid
gives the card two competing focal points, where a slider strip continues the
grid's horizontal rhythm and keeps the grid unambiguously the subject of the
node. Slider labels sit left, values right, inside the track, per the spec.

Also per lane, as small controls in the **gutter row**, not the strip:

- **M / S** (mute / solo). Any lane soloed silences every non-soloed lane.
- **choke group**, 0–3, `0` = none. Two lanes in the same non-zero group cut
  each other off — the closed-hat/open-hat case, and the one thing a drum
  sequencer is unusable without.

### 1e. Global controls

One `AudioKnobRow` of 4, under the strip:

| Control | Type | Notes |
|---|---|---|
| `rate` | dropdown | `MusicTime::RateDivisionList()`, default `1/16` |
| `steps` | int knob | 1–16, default 16 |
| `swing` | knob | 0 – 1, default 0. Delays every *odd-indexed* step by `swing * 0.5` of one step's length. At 1.0 an odd step lands on the following triplet position — the standard MPC/Linn definition. Apply it in the audio thread's step scheduling **and** in the grid's playhead so the two agree. |
| `volume` | knob | 0 – 1, default 0.8, node output level |

Plus three buttons beside the readout strip: **run** (§1f-3, a toggle, default
on), **randomise** (README's Tracker row asks for it; seed the density per lane
so it produces something musical — kick on downbeats, hats dense, snare on
5/13 — not white noise) and **clear**. Randomise and clear `PushUndoCheckpoint()`
first.

### 1f. Note input — notes trigger lanes

`NoteInputSlot(0)`, labelled `"notes"`.

- A note-on with pitch `p` triggers lane `p - baseNote`. `baseNote` is a param,
  default **36** (C1 — the GM kick, and what every pad controller sends).
  Notes outside `baseNote .. baseNote + 7` are ignored, not clamped.
- Note velocity multiplies the lane volume, exactly as a step's velocity does.
  A note-triggered hit is otherwise identical to a sequenced one — same voice
  pool, same envelope, same choke behaviour.
- Note-ins never advance or gate the sequence. The grid keeps running off the
  transport regardless; the note pin is a way to play the kit by hand *over*
  the pattern, which is what makes the node useful live.

### 1f-2. Free-running is the default — the node pulses with nothing patched

**A Drum Sequencer spawned from the palette, with nothing connected to its note
pin, starts playing its pattern immediately.** This is the same convention
`WavetableNode` and `SamplerNode` already follow (both sound the moment they
are patched, with no note cable). Nothing has to be added to make it work —
`Transport`'s `mPlaying` defaults to `true` (`src/core/Transport.h:114`) — but
it does have to not be *broken*, so:

- **Never gate step firing on whether a note cable is connected**, and never
  gate it on having received a note. `NoteInputSlot(0)` being null or silent
  changes nothing about the sequence. The two paths into a lane's voice pool
  are independent by construction.
- **Derive the step index from absolute transport position, not from an
  internal counter**: `stepPos = Transport::Beats() / MusicTime::BeatsFor(rate)`,
  step index `= (int)floor(stepPos) % steps`. A stateless derivation means the
  pattern is automatically phase-locked to the bar, agrees with every other
  tempo-synced node in the patch, and survives `Transport::Rewind()` with no
  resync code. An internal "steps fired so far" counter drifts against the
  transport and double-fires after a rewind — do not write one.
- **The only state the audio node keeps between blocks is the previous block's
  `stepPos`**, used to find which boundaries were crossed. Guard it: if this
  block's `stepPos` is *less* than the previous one (a rewind, or the user
  scrubbed), reset the previous value to the current one and fire nothing for
  that block, rather than iterating backwards over a negative range.
- **A paused transport needs no special case.** `AdvanceAudioClock` only
  advances while playing, so when paused a block's start and end `stepPos` are
  equal, no boundary is crossed, and nothing fires. Do not add an
  `if (!IsPlaying())` early-out — voices already sounding must be allowed to
  finish their tails rather than being cut off at the pause.

### 1f-3. Per-node `run` toggle

Global pause freezes the entire patch, which is right for the transport but
useless when a patch has two drum machines and you want to mute one part. The
node gets its own **`run`** toggle (bool param, default **on**), drawn as a
play/stop button beside the readout strip next to `randomise` / `clear`.

- `run` off stops step firing only. It does not cut sounding tails, does not
  touch the transport, and does not silence the note pin — you can still play
  the kit by hand over a stopped pattern.
- With `run` off the grid still draws in full, with the playhead **parked on
  the step it would next fire** rather than hidden (a grid with no playhead
  reads as broken — `audio-node-ui` §3f).
- The readout strip's idle text says which state it is in:
  `"16 steps - 1/16 - 4 loaded"` when running, `"stopped - 16 steps - 1/16"`
  when not.
- This is one bool and one button. It is **not** a second clock source — do not
  add an internal free-run tempo independent of `Transport`. Everything
  time-based in this app reads one clock so that play/pause genuinely freezes
  the whole patch and a tempo change retimes everything at once
  (`src/core/Transport.h:7`); a node with a private BPM breaks that guarantee
  and cannot stay in sync with a `Delay` or an `LFO`.

### 1g. Sample loading — three routes, all required

1. **Drag from the Samples panel onto a lane.** The existing mechanism is
   `gSampleDragActive`, resolved at `src/main.cpp:24087`. That block currently
   hit-tests `FindNodeUnderCanvasPoint<SamplerNode>`; add a
   `DrumSequencerNode` branch **before** the Sampler one. Resolving *which
   lane* needs the drop point's y relative to the node's own grid rect — cache
   the grid's canvas-space top and row height on the node during
   `DrawDrumSequencerBody` and read them back here. Read the long comment at
   `src/main.cpp:24095` before touching this: it explains why
   `ed::GetHoveredNode()` cannot be used and a plain rect test must be.
2. **OS file drop.** `OnFilesDropped` (`src/main.cpp:626`, registered at
   `:15526`) — same lane resolution.
3. **Click the lane's name** with nothing loaded → the native audio file
   dialog, the same call `SamplerNode::LoadFile` reaches
   (`Platform::DecodeAudioFileToBuffer`, `src/platform/Platform.h:272`).

A lane with no sample loaded shows its index and a dimmed `--` and is silent;
its steps still edit and still light up. Do not make an empty lane an error
state.

---

## 2. Layout

`kAudioNodeWidth` (440). One body, no columns.

```
┌ readout strip ─────────────────────────── idle: "16 steps - 1/16 - 4 loaded" ┐
│                                                                              │
│  ┌ step grid (the visualizer, full content width) ───────────────────────┐   │
│  │ 1 kick.wav   M S ▸ │█│ │ │ │█│ │ │ │█│ │ │ │█│ │ │ │  ← velocity as    │   │
│  │ 2 snare.wav  M S ▸ │ │ │ │ │█│ │ │ │ │ │ │ │█│ │ │ │     fill height   │   │
│  │ ... 8 lanes ...                                                        │   │
│  │ bar/beat rules every BeatsPerBar(); playhead column outlined           │   │
│  └────────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌ lane 1 - kick.wav ─────────────────────────── [choke 0] ──────────────┐   │
│  │ volume ──────────── 0.80    pan ──────────────── 0.00                 │   │
│  │ pitch  ──────────── 0.0 st  decay ────────────── 1.00                 │   │
│  │ transient ───────── 0.00                                              │   │
│  └────────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  [ rate 1/16 ▾ ]  [ steps ]  [ swing ]  [ volume ]  [▶] [random] [clear]     │
└──────────────────────────────────────────────────────────────────────────────┘
```

- The lane section header carries the selected lane's number and file name, so
  it is never ambiguous which lane the sliders are editing.
- Sliders are `AudioHalfWidth()` in pairs (see the skeleton in
  `audio-node-ui/SKILL.md`).
- **The idle readout is never empty** — `"16 steps - 1/16 - 4 loaded"`.
- **The grid is never blank at rest**: frame, bar rules, lane gutter, and the
  playhead column outlined even when the transport is stopped.
- The playhead comes from `Transport::Instance().Beats()` on the main thread —
  the same clock the audio node advances from, so no cross-thread read (spec
  §3f, and the `audio-node-ui` RT-safety section).

---

## 3. Audio-thread implementation

`AudioDrumSequencerNode : public AudioNode`, in `src/nodes/DrumSequencerNode.cpp`'s
anonymous namespace (the `SamplerNode.cpp` pattern — the audio class does not
get its own header unless main.cpp needs it, and it does not).

**Step scheduling, sample-accurate.** At the top of `ProcessBlock`, read
`Transport::Instance().Beats()` for the block start and compute the block's end
in beats from `numFrames / sampleRate * bpm / 60`. Convert both to fractional
step positions via `MusicTime::BeatsFor(rate)`. Fire every step boundary
crossed inside the block, at `frameOffset = (stepBeat - blockStartBeat) /
blockBeats * numFrames`, applying the swing offset to odd steps. **Do not fire
once per block at offset 0** — that quantises the pattern to ~10 ms and the
groove audibly collapses, which is the same trap `prompts/14-note-sequencer.md`
calls out for note-offs.

**Voices.** A fixed pool, no allocation: **4 voices per lane, 32 total**, each
a `{ laneIndex, bufferPtr, double readPos, double rate, float amp, ampCoeff,
attackRemaining, panL, panR, active }` POD in a plain array. A new hit on a
lane steals that lane's oldest voice. A choke steals every active voice in the
same non-zero group. Retriggering a lane does **not** cut its own previous
voice unless the lane's choke group matches itself — overlapping tails is what
a real sampler does.

**Reading samples.** Lift `SamplerNode.cpp`'s `ReadSample` (`:351`) — do not
rewrite the interpolation. Playback rate is `pow(2, pitch/12) * (bufferRate /
engineRate)`.

**Envelope.** Per voice: linear attack over `attackRemaining` frames (from
`transient`), then unity, then — if `decay < 1` — a one-pole exponential whose
coefficient is precomputed **on the main thread** in the param push, never
`exp()` on the audio thread.

**Denormals.** The decay tails need the flush check (`new-audio-node` §4).

---

## 4. Sample buffer lifetime — 8 lanes, and the trap

`SamplerNode.cpp` already solves this exactly once, for one buffer:
`BufferRetireRing` (`:42-82`), `PushBuffer` (`:122`), `DrainRetired` (`:130`).
The main thread hands a `Platform::SampleBuffer*` over through an atomic
exchange; the audio thread swaps it in and pushes the superseded one back
through an SPSC ring; the main thread deletes it in `CookIfNeeded`. **Nothing
is ever deleted on the audio thread.**

Do not reimplement this. **Lift `BufferRetireRing` and the pending/active/retire
triple out of `SamplerNode.cpp`'s anonymous namespace into a small shared
header** (`src/audio/SampleSlot.h` — one struct holding
`mPendingBuffer` / `mActiveBuffer` / `mRetireRing` with `Push`, `SwapIn`,
`DrainRetired`), have `SamplerNode` use it, and give the drum sequencer **eight
instances, one per lane**. Reimplementing it per lane is where this session
would most plausibly ship a use-after-free.

Note the consequence for save/load: like `SamplerNode`, each lane persists its
**file path**, and `ReloadDerivedState` re-decodes from disk. A lane whose file
has moved shows its status and stays silent; it must not block the patch load.

---

## 5. Params — the `VisitParams` list

Names are stable patch keys; renaming one silently drops it from existing
patches. Per lane, indexed (`lane0_volume`, `lane0_pan`, …):

`filePath`, `volume`, `pan`, `pitch`, `decay`, `transient`, `mute`, `solo`,
`choke` × 8 lanes, plus the pattern (`lane0_steps` as a flat 16-float run per
lane), plus globals `rate`, `steps`, `swing`, `volume`, `run`, `baseNote`,
`selectedLane`.

Confirm the node is picked up by `AUDIOPARAMSWEEPTEST` and
`AUDIOTEARDOWNSWEEPTEST` rather than writing per-node versions. Expect the
per-lane params to be **structural blind spots** for the param sweep — a lane
with no sample loaded produces silence, so altering its volume changes nothing
audible. Either give the sweep a lane preloaded with a synthesised click
buffer, or document the blind spot in the same style `EffectDefs.cpp` documents
Reverb's and Stereo's, and hand-verify in the fixture. Do not leave it
undocumented.

---

## 6. DSP fixture — `RunDrumSequencerFixture`

Follow `INFINITE_DSPTEST` (`src/main.cpp` ~9522) — render the real
node → `AudioEngine` chain headless with no device. Assert:

1. **Step timing.** A pattern with lane 0 step 0 only, at 120 BPM 1/16, fires
   hits exactly `sampleRate * 0.125` frames apart (±1 frame), and the *first*
   hit is not quantised to a block boundary.
2. **Swing.** At `swing = 0.5`, odd-step hits land 25 % of a step late; even
   steps are unmoved. At `swing = 0`, output is bit-identical to a run with
   swing never touched.
3. **Velocity scales linearly.** Step at 0.5 velocity peaks at half the
   amplitude of the same step at 1.0.
4. **Choke.** Two lanes in group 1, lane B fired one step after lane A → lane
   A's contribution is zero within ~2 ms of B's trigger.
5. **Solo/mute.** Soloing lane 3 silences lanes 0–2 and 4–7 exactly.
6. **Note-in parity.** A note-on at `baseNote + 2` produces output within 1 dB
   of the same lane's sequenced hit at equal velocity.
7. **Decay.** `decay = 0.2` produces a tail measurably shorter than
   `decay = 1.0` on the same sample, and no NaN/denormal spike at the end.
8. **Voice-steal safety.** 200 triggers on one lane inside 100 ms produce
   bounded, finite output and never exceed the 4-voice pool.
9. **Free-run.** A node built with **no note inbox attached at all** produces
   the pattern, at the same times and amplitudes as one with an idle note
   cable connected. This is the assertion that catches a future edit
   accidentally gating the sequence on note input — §1f-2.
10. **Transport agreement.** Pausing the transport mid-pattern stops further
    hits but lets a sounding voice finish its tail; resuming fires the next
    step at the correct absolute beat, not one step early or late. Calling
    `Transport::Rewind()` mid-pattern fires nothing extra in the block that
    straddles the rewind, and resumes from step 0.
11. **`run` toggle.** `run = false` fires no steps, leaves already-sounding
    voices to decay naturally, and still plays a note-in hit at full level.

---

## 7. Explicitly out of scope — do not build these

Each is a real feature that a drum sequencer could have. Each is excluded on
purpose; if you think one is essential, say so in your report rather than
building it.

- **Per-step pitch / decay / repeat (ratchet).** Needs an edit-mode selector,
  which the layout rule forbids and the minimalism rule kills. §1a.
- **Pattern banks A/B/C/D with chaining.** The single highest-value follow-up,
  and a session of its own — it changes the save format and needs bar-quantised
  switching. Note it in STATUS as the obvious next step.
- **A note *output* pin** (so the grid drives other synths). Genuinely
  valuable, but it makes the node an `IAudioSource` **and** an `INoteSource`
  simultaneously, which is not one of the four shapes in
  `new-audio-node` §2 and would need that rule extended first. Not in this
  session.
- **Per-lane filter / drive / send.** Patch an `Audio Filter` after it. That is
  what the effect nodes are for, and per-lane inserts would put 8 more param
  sets on one card.
- **Per-step probability.** A second per-step value grid for a feature a
  per-lane chance slider covers at 1/16th the complexity — and even that is cut
  here to hold the control budget.
- **Sample start/end/reverse per lane.** That is `SamplerNode`'s job and it
  already ships. A drum lane plays a one-shot from 0.
- **A sample browser inside the node.** The docked Samples panel already
  exists and §1g wires it to this node.
- **A per-node tempo or internal clock.** The `run` toggle in §1f-3 covers
  "stop this one machine". A private BPM would break the one-clock guarantee
  `Transport` exists to provide and would drift against every tempo-synced
  effect in the patch.

---

## 8. Wiring checklist (per `new-audio-node` §3)

1. `src/nodes/DrumSequencerNode.h` / `.cpp`.
2. `CMakeLists.txt` — node file near line 98; `src/audio/SampleSlot.h` is
   header-only, nothing to add for it.
3. `src/main.cpp` — `#include "nodes/DrumSequencerNode.h"` (~line 92).
4. `REGISTER_NODE(DrumSequencerNode, Drum Sequencer, "Synths")`. The category
   token has no space — a space silently corrupts patch load (§3.4).
5. `DrawDrumSequencerBody` + a branch in `DrawAudioNodeBody` (~line 5309).
   `IsAudioBodyNode` accepts it generically; `InputCountFor` needs nothing.
6. `InputLabel(0)` → `"notes"`.
7. The node help table (~line 7780) — one sentence in the existing voice: what
   it does, plus the one non-obvious thing (notes from `baseNote` up play the
   eight lanes over the running pattern).
8. The `gSampleDragActive` branch at `src/main.cpp:24112` and `OnFilesDropped`
   at `:626` — §1g.

Do **not** add entries to connect/disconnect/`DisconnectAllTo`/
`RemoveNodeByIndex`/cycle detection/patch save/load/undo/copy-paste. All of
those are generic; needing one is a sign the node is built wrong.

---

## 9. Done when

All seven of `new-audio-node` SKILL.md §6's criteria hold. **Report each one
explicitly, including any that did not pass:**

1. Builds clean.
2. Spawns from the palette with 1 labelled note pin + audio out; body renders
   at 440 with a non-empty readout strip.
3. Loads samples by all three routes in §1g, and **plays its pattern in sync
   with the transport with nothing connected to its note pin** (§1f-2) as well
   as when driven by notes — wired into a real chain ending at `Audio Out`.
4. Params (including the whole pattern and all 8 lanes' file paths) survive
   save → load → undo → copy/paste → delete unchanged.
5. Deleting it mid-playback does not crash, logs zero xruns
   (`AUDIOTEARDOWNSWEEPTEST`).
6. `RunDrumSequencerFixture` prints `OK`; `/run-infinite-hygiene` passes.
7. `ARCHITECTURE.md`'s audio section, `docs/plans/audio/README.md` §3's Drum
   Sequencer row (which currently specifies per-step params — correct it to
   per-lane and say why), and `docs/plans/audio/STATUS.md`'s P3b row are all
   updated.

Then build and copy `build/Infinite.app` to `~/Desktop/Infinite.app`.

---
name: new-audio-node
description: The standard procedure for adding any audio, note, or synth node to Infinite — the two-object rule, the exact wiring sites in main.cpp/CMakeLists, the bug traps each rule exists to prevent, and the machine-checkable exit criterion. Use whenever implementing a node from docs/plans/audio/README.md §3 (Oscillator, Audio Filter, Delay, Reverb, Dynamics, Drive, Stereo, Pitch Time, Sampler, Drum Sequencer, Resonator, Scope, Audio In, Recorder, Note Filter/Modify/Echo/Router/Display, Arpeggiator, Note Sequencer, Shaper, Macro, Mod Recorder), when writing the prompt for a fresh session that will implement one, or when a newly added audio node has no pins / no body / doesn't save / crashes on delete.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

This skill is the **implementation** half. The **appearance** half is the
`audio-node-ui` skill (`docs/plans/audio/audio-node-ui-system.md`). For any
node involving tempo sync, rate divisions, or quantization, also follow the
`rhythmic-quantization-standard` skill (`src/audio/MusicTime.h`). Every node
needs these to be correct and usable. Read these first before writing any code.

---

## 0. Four invariants — restate these verbatim in any prompt you write

A fresh session cannot infer them, and each has already cost real time here.

1. **Clean room.** *Do not open, read, grep, or reference
   `/Users/namansoni/BespokeSynth`.* Infinite is MIT, BespokeSynth is GPLv3.
   Work only from the spec in `docs/plans/audio/README.md` and the cited DSP
   literature (PolyBLEP, TPT/Zavalishin SVF, RBJ biquad cookbook, FDN reverb,
   Voss-McCartney pink noise). Implement each algorithm from its **primary
   reference**, not from anyone's implementation.

2. **The two-object rule.** A node is an `INode` on the main thread (UI,
   params, save/load, pins) that **owns** an `AudioNode` on the audio thread
   (`ProcessBlock` only). They talk through `ParamMailbox` (main→audio) and
   `MeterRing` (audio→main) — never by sharing mutable fields.

3. **`CookIfNeeded` does no DSP.** It drains meters and pushes dirty params.
   Budget < 5 µs. If you find yourself generating samples there, the node is
   built wrong.

4. **Audio-thread prohibitions.** Inside `ProcessBlock` and anything it calls:
   no allocation, no locks, no `dynamic_cast`, no `std::function` / `map` /
   `string`, no GL, no ImGui, no file I/O, no `printf`.

5. **Minimalism is a requirement, not a nice-to-have.** This has already gone
   wrong twice (Dynamics, Delay — both shipped with 4 selectable modes and
   30+ params, then had to be cut back down after the fact). The bar is a
   real hardware-plugin-grade unit, not an exhaustive one: **KHS Audio's
   Delay** (Time, Tone, Feedback, Pan, Duck, a Bounce on/off switch, Mix — 7
   controls, one mode) and **KHS Audio's Compressor** (Threshold, Ratio,
   Attack, Release, Makeup, a Peak/RMS switch, a Sidechain source dropdown, a
   meter — 7 controls + a meter, one mode) are the literal reference bar.
   Treat every control in the design doc's "Tier 2" table as a *candidate for
   deletion*, not a default inclusion — implement Tier 1 only, and add a
   Tier 2 control only if the node is genuinely broken without it (not
   "more capable", not "closer to the spec doc", not "a real plugin would
   have this too" — a real plugin that ships this many knobs is the exception,
   not the norm, which is exactly why KHS's are the reference and not, say,
   a Waves channel strip). A dropdown that switches between more than two
   processing modes is itself a smell — collapse it to a toggle or cut the
   modes that aren't the obviously-primary one (a single binary `analog`
   character toggle per effect node, switching between pure digital and vintage
   hardware modeling, is an approved two-state control). Before writing `DrawXxxBody`,
   count the controls on the card; more than ~8 (excluding the visualizer and
   the mix knob) means cut until it's at or under that, not build a bigger
   card to fit them. When a design doc (`docs/plans/audio/README.md` §3 or
   `P3c-P3a2-design.md`) specifies more than this, the doc is wrong for this
   codebase's actual bar — implement the smaller node and update the doc to
   match, don't implement the doc as written.

---

## 1. Read these before writing code

| File | Why |
|---|---|
| `docs/plans/audio/README.md` §3 | the node's spec — what it replaces, its param set |
| `docs/plans/audio/audio-graph-semantics.md` | §1/§2 fan-in/fan-out rules, §3 topology, §6 modulator-output nodes |
| `src/audio/AudioNode.h` | the base class you derive the audio half from |
| `src/audio/DspMath.h` | PolyBLEP, TPT SVF, RBJ biquads, one-pole, dB↔lin, equal-power pan, fast tanh — **check here before writing any DSP primitive** |
| `src/audio/AudioVoice.h` | voice allocator + ADSR, shared by every polyphonic node |
| `src/audio/ParamMailbox.h`, `MeterRing.h` | the only two legal cross-thread channels |
| `src/nodes/AudioNodes.h` | `GainNode` is the smallest complete reference; `MixerNode` is the multi-slot one |
| `src/nodes/WavetableNode.cpp` | the reference *large* node (synth + UI + tests) |
| `src/nodes/NoteNodes.h` | the reference note node, and `EnvelopeNode` for the audio-in/modulator-out shape |

---

## 2. Pick the node's shape first

Which interfaces it implements determines everything downstream. There are
exactly four shapes:

| Shape | Implements | Example | Pins |
|---|---|---|---|
| **Audio source / effect** | `INode`, `IAudioSource` | Oscillator, Audio Filter, Delay | audio out; audio in via `AudioInputSlot` |
| **Note source / processor** | `INode`, `INoteSource` | MIDI Notes, Arpeggiator, Note Filter | note out; note in via `NoteInputSlot` |
| **Note → modulator** | `INode`, `IModulator`, + `AudioNodeForNotePorts()` | Envelope | note in, modulator out |
| **Terminal** | `INode` only | Audio Out | audio in, no output |

Two traps here:

- **`IAudioSource::GetAudioNode()` vs `INode::AudioNodeForNotePorts()`.** The
  first means *"my output is an audio buffer, cable me as an audio source"*.
  The second means *"I own an AudioNode the topology builder must
  `PrepareToPlay`/`ProcessBlock`/`SetNoteInbox`, but my output is not audio"* —
  that is Envelope. Getting this backwards makes a modulator node offerable as
  an audio source, or leaves a note-driven node's `ProcessBlock` never called.
  `INode.h:156` documents the distinction; read it.
- **A synth is both.** Wavetable is an `IAudioSource` with a `NoteInputSlot`.

---

## 3. The wiring checklist

Everything not in this list is already generic and needs no work — connect,
disconnect, `DisconnectAllTo`, `RemoveNodeByIndex`, cycle detection, patch
save/load, undo, copy/paste and the topology rebuild all go through
`AudioInputSlot()` / `NoteInputSlot()` / `VisitParams()`. **Adding an entry to
any of those is a sign you built the node wrong.**

What genuinely is per-node:

1. **`src/nodes/XxxNode.h` / `.cpp`** — the `INode` half and its `AudioNode`
   half. Forward-declare the audio class in the header and hold it in a
   `std::unique_ptr`, with an out-of-line constructor/destructor (see
   `AudioNodes.h:26`) so main.cpp never sees the audio-thread class.
2. **`CMakeLists.txt`** — add the `.cpp` to the source list (audio-thread DSP
   files under `src/audio/`, around line 54; node files under `src/nodes/`,
   around line 98).
3. **`src/main.cpp` include** — `#include "nodes/XxxNode.h"` with the others
   (~line 92).
4. **`RegisterNodes()`** — `REGISTER_NODE(XxxNode, Display Name, "Category")`.
   Categories: `Notes`, `Synths`, `AudioEffects`, `AudioUtility`. All four
   already have colours in `src/core/CategoryColors.cpp` for all five themes —
   nothing to add there.
   **The category string must be one whitespace-free token.** `Patch.cpp` reads
   the `node <index> <category> <typeName>` line with `>>`; a space in a
   category silently eats the type name on load. This is why it is
   `"AudioUtility"` and not `"Audio Utility"`.
5. **Name collision check** — `NodeFactory::DuplicateNames()` catches it at
   runtime, but check `README.md` §3's collision table first. `Noise`, `Curve`,
   `Curves`, `Shape`, `Pattern` and `Transform` are all taken by visual nodes.
6. **`VisitParams`** — every field that must survive save/load. Names are
   stable keys in the patch file; renaming one silently drops it from existing
   patches.
7. **`InputLabel(slot)`** — without it the pin is a bare unlabelled dot.
8. **`DrawXxxBody` + a branch in `DrawAudioNodeBody`** (~line 5309). This is
   the *only* dispatch ladder an audio node still needs an entry in.
   `IsAudioBodyNode` already accepts it generically off the four interfaces.
9. **The node help table** (~line 7780) — one sentence, in the existing voice:
   what it does and the one non-obvious thing about it.

`InputCountFor` is **no longer** a site: it now counts audio and note pins
generically by probing `AudioInputSlot`/`NoteInputSlot` over a shared slot
index space. Keep it that way.

---

## 4. Bug traps, each of which has already happened here

- **Pin count silently zero.** Was the `InputCountFor` ladder; now generic.
  If a new node shows no pins, the cause is non-contiguous slot indices — the
  count stops at the first index neither virtual answers.
- **Audio and note pins share one slot index space.** A node with note-in at
  slot 0 and audio-in at slot 1 must answer `NoteInputSlot(0)` and
  `AudioInputSlot(1)`, not both from 0.
- **Space in a category name corrupts the save file** (see 3.4).
- **`ImGui::SetTooltip` between `ed::Begin()` and `ed::End()`** lands offset
  from the cursor and grows with zoom. Use the readout strip; if a tooltip is
  truly unavoidable, wrap it in `ed::Suspend()`/`ed::Resume()`.
- **Cycles.** The visual graph allows them via `FeedbackNode`; the audio graph
  must not, or the topological sort deadlocks. `WouldCreateAudioCycle` /
  `WouldCreateNoteCycle` (main.cpp ~2511) enforce this. Do not "fix" them.
- **One cable per audio input, one per audio output.** Summing goes through
  `Mixer`, fan-out through `Splitter` — that is the entire reason those two
  nodes exist (`audio-graph-semantics.md` §1/§2). A new node must not quietly
  sum at a pin.
- **Denormals in decay tails.** Reverb/delay/filter feedback paths need the
  FTZ/DAZ guard the engine sets, plus the usual tiny-DC or flush check.
- **Drawing is the real cost, not the DSP** (README §1). Decimate scopes to
  ~128 points, cap redraw at 30 Hz, collapse by default. 20 nodes each drawing
  1024 ImGui segments per frame costs more than every synth and effect
  combined.

---

## 5. Tests — write them with the node, not after

Infinite has no test binary; tests are `getenv("INFINITE_…")` fixtures in
`main.cpp` that print a verdict line ending `OK` / containing `FAIL` / ending
`BUG`. Follow `INFINITE_DSPTEST` (main.cpp ~9522) — it renders the real
node→`AudioEngine` chain headless, with no device.

For each node add:

- **A DSP fixture** asserting on samples against an analytic expectation —
  SVF response at known cutoffs, ADSR segment times, delay accuracy in
  samples, reverb RT60, dynamics gain-reduction curve, PolyBLEP aliasing
  below a threshold.
- Confirm the node is picked up by the two generic sweeps rather than writing
  per-node versions of them: `AUDIOPARAMSWEEPTEST` (every param survives
  save/load and reaches the audio thread's smoothed value within a block) and
  `AUDIOTEARDOWNSWEEPTEST` (spawn, wire into a running graph, delete
  mid-playback, keep rendering — no crash, zero xruns).

Then run `/run-infinite-hygiene` before committing.

---

## 6. Exit criterion — state it machine-checkably in every prompt

A node is done when all of these hold:

1. It builds clean.
2. Spawned from the palette it shows the right pin count and labels, and its
   body renders at one of the three legal widths with a non-empty readout strip.
3. It makes the sound / produces the events it is specified to, wired into a
   real chain ending at `Audio Out`.
4. Its params survive save → load → undo → copy/paste → delete unchanged.
5. Deleting it mid-playback does not crash and logs zero xruns.
6. Its DSP fixture prints `OK`; `/run-infinite-hygiene` passes.
7. `ARCHITECTURE.md`'s audio section and `docs/plans/audio/README.md` §3 are
   updated to mark it shipped.

---

## 7. Prompt template for a fresh session

```
Implement the <NAME> node in Infinite (/Users/namansoni/infinte).

Spec: docs/plans/audio/README.md §3, the <NAME> row. It replaces <...>.
Params: <...>. Category: <Notes|Synths|AudioEffects|AudioUtility>.
Shape: <audio source | note processor | note→modulator | terminal>.

Follow .claude/skills/new-audio-node/SKILL.md for the procedure and
.claude/skills/audio-node-ui/SKILL.md for the body layout. Both are
prescriptive — do not re-derive either.

Five rules that override anything you infer:
1. Clean room: do not read /Users/namansoni/BespokeSynth. Implement the DSP
   from its primary reference.
2. Two objects: INode (main thread) owns AudioNode (audio thread); they talk
   only through ParamMailbox and MeterRing.
3. CookIfNeeded does no DSP — drain meters, push dirty params, < 5 µs.
4. On the audio thread: no allocation, locks, dynamic_cast, std::function/
   map/string, GL, ImGui, file I/O, or printf.
5. Minimalism: ship Tier 1 only, ~8 controls max, one processing mode unless
   a second is unavoidable. KHS Audio's Delay/Compressor are the reference
   bar, not the design doc's full Tier 1+2 table — cut before you build.

Reference nodes: GainNode (smallest complete), WavetableNode (largest),
EnvelopeNode (note-in / modulator-out).

Done when SKILL.md §6's seven criteria all hold. Report each one.
```

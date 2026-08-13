# 01 — §0 shared infrastructure + Audio Filter

The one session that must land before any other node in P3c or P3a-part-2.
It is infrastructure plus the first effect, deliberately together: Audio
Filter is what forces `AudioEffectNode` into a shape that actually works.

Paste everything below into a fresh Claude Code session.

---

Implement §0.1–0.5 of docs/plans/audio/P3c-P3a2-design.md, then the Audio
Filter node (§1.1), in Infinite (/Users/namansoni/infinte).

Read §0 and §1.1 in full before writing anything. §0 is five pieces:

- §0.1 `RateDivision` + `BeatsFor` in a new `src/audio/MusicTime.h`, shared by
  DSP and UI for the reason `src/audio/SynthModes.h` states — a dropdown built
  from one list and a switch written against another drift apart silently.
- §0.2 time signature on `Transport` (`SetTimeSignature`, `Bars()`,
  `BeatsPerBar()`), atomics, matching the existing members' pattern in
  src/core/Transport.h, surfaced in the transport UI next to BPM.
- §0.3 global key + scale on `Transport`, and the scale table / `SnapToScale` /
  `DegreeToNote` helpers in `MusicTime.h`.
- §0.4 `AudioEffectNode` — the table-driven param surface. The UI is
  table-driven; the DSP is not. Seven kernels will live under `src/audio/dsp/`
  behind `IEffectKernel { PrepareToPlay, ProcessBlock, Reset }`; this session
  writes the first one. Visualizers are explicitly NOT in the table.
- §0.5 the effect conventions all seven obey — mix, `LatencySamples()`,
  denormal guards, audio in at slot 0, smoothing.

Then Audio Filter per §1.1: `bands` 1–4, the six Tier 1 controls, the two
Tier 2 sections, RBJ cookbook biquads plus cascaded `DspMath::TptSvf` stages
for the LP/HP slopes, coefficients computed main-thread and pushed as
coefficients so the audio thread never runs `tan()`, and the draggable
log-frequency response curve — which is the reason this node is first, since
Dynamics and Drive reuse that visualizer pattern.

Category: `AudioEffects`. Node shape: audio effect.

Procedure: .claude/skills/new-audio-node/SKILL.md — prescriptive, follow it.
Body layout: .claude/skills/audio-node-ui/SKILL.md and
docs/plans/audio/audio-node-ui-system.md. Do not re-derive either; both have
been through multiple revisions and the reasons are written down.

Four rules that override anything you infer:
1. Clean room: do not open, read, grep or reference
   /Users/namansoni/BespokeSynth. Implement the DSP from the primary
   reference named in the spec section (RBJ Audio-EQ Cookbook,
   TPT/Zavalishin SVF), not from any implementation.
2. Two objects: the INode (main thread) owns an AudioNode (audio thread);
   they communicate only through ParamMailbox and MeterRing.
3. CookIfNeeded does no DSP — drain meters, push dirty params, budget < 5 us.
4. On the audio thread: no allocation, locks, dynamic_cast, std::function/
   map/string, GL, ImGui, file I/O, or printf.

Reference nodes already in the tree: GainNode (smallest complete),
WavetableNode (largest, with its own DSP tests), EnvelopeNode (note-in,
modulator-out).

Exit criteria — report each explicitly, including any that did not pass:
1. `MusicTime.h` compiles with a unit fixture asserting `BeatsFor` on all 18
   divisions at both 4/4 and 7/8.
2. `AudioEffectNode` builds one working effect end to end (Audio Filter), so
   the table shape is proven by use rather than by assertion.
3. All seven of new-audio-node SKILL.md §6's criteria hold for Audio Filter,
   including its DSP fixture: magnitude response at known cutoffs against the
   analytic biquad response (±0.5 dB), the −3 dB point per slope, and a
   stability sweep across the full freq × Q grid at every type with no NaN.
4. `/run-infinite-hygiene` passes.

When finished, update docs/plans/audio/STATUS.md — mark the §0 infrastructure
row and Audio Filter shipped.

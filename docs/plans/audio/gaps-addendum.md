# Gaps found after the consolidation — addendum to README.md §3

Raised 2026-08-13, reviewing the plan against what the user actually wants and
against the Bespoke modules as a **specification** (README §2: param sets and
feature lists are not the protected thing; code is — nothing here was read).

The consolidation in README §3 stands. This is what it missed, plus two of its
merges that the evidence now argues against.

Nothing here is scheduled. Sessions 02–14 in `prompts/` are unaffected except
where noted per item.

---

## A — Effects: one new node, five modes

Net effect on the count: **30 → 31 spawnable types.**

| # | Gap | Resolution | Cost |
|---|---|---|---|
| A1 | **Chorus / flanger / phaser** — absent entirely. Delay's "movement" section choruses its *own tail*, which is a different thing. | **New node: `Modulation`.** `mode` = chorus / flanger / phaser / tremolo / auto-pan. Chorus and flanger are one short modulated delay line differing in feedback and time range; phaser is an all-pass chain. One node by the same argument that makes Dynamics one node. Rate via `RateDivision` (§0.1). Benchmark: Ableton Chorus-Ensemble + Phaser-Flanger, Eventide Instant Flanger. | a session |
| A2 | **Transient shaper** — Dynamics is compress/limit/gate/expand, all threshold-driven. Attack/sustain shaping uses a *differential* envelope (fast vs. slow follower) and ignores threshold entirely. | Fifth Dynamics mode, `transient`, with `attack` and `sustain` ±100 %. Different enough from the gain computer that it is a branch, not a param. Benchmark: SPL Transient Designer. | a section |
| A3 | **Frequency shifter** — not a pitch shifter. Linear Hz offset via Hilbert transform; inharmonic by nature. | Fourth Pitch Time mode. Cheap next to the phase vocoder already in that kernel. | a mode |
| A4 | **Multi-tap delay** — Delay's modes are simple / ping-pong / multiband / stutter. | Fifth Delay mode, `multitap`, with tap count and per-tap time/level/pan. | a mode |
| A5 | **Repeater / beat-repeat** — partly covered by Delay's stutter mode. §1.3 already hedges: *"Stutter is the loosest fit here — split it out if the UI fights."* Listing it separately is that evidence. | Decide when Delay is built (session 03). Either a real stutter section, or its own node. | TBD |
| A6 | **Shimmer** — reverb with pitch-shifted feedback. | Reverb Tier 2 *shimmer* section: shift amount (±12/24 st), shimmer mix, position in the feedback path. **The most expensive of the six** — it needs a pitch shifter inside the reverb kernel, so build it after Pitch Time exists and reuse that kernel rather than writing a second one. | a section, ordered last |

**Ordering consequence:** A6 must come after Pitch Time (session 07). A1 is a
new session, sensibly after Delay (03), since it shares the modulated-delay-line
machinery.

## B — Notes: one new node, one un-merge, one missing control

| # | Gap | Resolution |
|---|---|---|
| B1 | **Chorder** — absent. One note in, a chord out. Chord generation currently exists only buried inside Note Sequencer's generative mode, where it cannot be used on a live keyboard or an arp. | **New node: `Chorder`.** Chord type / scale degrees / voicing / spread / upper harmonics / strum / octave doubling, scale-aware via §0.3. Benchmark: Ableton Chord. Needs the note-on→note-off map (§2.3) — one incoming note becomes N outgoing, and all N note-offs must be emitted. |
| B2 | **Strum** — appears only inside Note Sequencer's generative description. It is what turns a chord from an organ stab into a guitar, so it belongs wherever chords are made. | A control on **Chorder** and on **Maze**, not a sub-mode of one node. Shared helper alongside the swing/humanise helpers in `MusicTime.h` (§2.0's argument: three implementations of one feel control is a phasing mess). |
| B3 | **Maze — un-merge it.** README §3 folds it into Note Sequencer's generative mode. Its actual interaction is an 8×8 grid of chord events in a 2D field, not a linear step sequencer with a different step-selection rule (which is what grid / euclidean / polyrhythm are). Its params — scaled degree, count, randomise, groove, humanise vel, upper harmonics, strum, humanise timing — are nearly disjoint from a step grid's per-step data (on/off, degree, velocity, probability, ratchet, tie). | **Keep `Maze` as its own node.** This applies §3's own merge rule rather than overriding it: *"Keep separate when the interaction model differs, because a dropdown that swaps the entire UI is a worse node than two nodes."* Session 14 (Note Sequencer) then drops its generative mode and gets simpler. |

Randomised velocity and humanise timing are **already covered** — Humanizer
(one of the eight single-purpose nodes that replaced Note Modify), and §2.0
lists them as two of the four musicality levers, deliberately split into two
controls rather than one "humanize" knob.

## C — PaulStretch is a source, and the plan has it as an effect

The sharpest finding. README §3 puts paulstretch inside **Pitch Time**, which
processes an incoming audio cable — it has no buffer, no play region, no file
drop. The actual module is a **sample player**: drop a sample, start / end /
current position, loop, plus stretch, window size, phase randomisation, pitch
shift, fine tune, freq shift, unison, detune.

So paulstretch-on-a-sample is **not reachable** by the plan as written.

**Resolution:** a **sixth Sampler engine** (`paulstretch`), which is where the
buffer, play region, loop and file drop already live. Pitch Time keeps
paulstretch as its live-input mode. The two share one STFT kernel under
`src/audio/dsp/`; neither owns it.

Note the corroboration: that module carries `freq shift`, `unison` and `detune`
— A3 was identified independently before this screenshot, and lands in the same
kernel.

## Decisions confirmed while raising these

- **Sampler stays one node, five (now six) engines** — not split into
  Granulator / Slicer / Spectral. They share buffer, play region and loop model.
- **Pitch bend / glide / portamento stay per-synth params** — glide on
  Oscillator/Wavetable, hardware bend as a modulator output on MIDI Notes.
  `NoteEvent` gains no continuous-pitch field, so no note node can apply bend
  across a downstream synth. Revisit only if that limitation bites in practice;
  the retrofit cost is every producer and consumer, which is exactly the
  argument `NoteEvent.h`'s own `source` comment makes.

## Running total

| | README §3 | With this addendum |
|---|---|---|
| Notes | 7 | 9 (+ Chorder, + Maze un-merged) |
| Effects | 7 | 8 (+ Modulation) |
| Synths | 5 | 5 |
| Utility | 7 | 7 |
| Modulators | 4 new + 2 extended | unchanged |
| **Spawnable types** | **30** | **33** |

Plus five modes/sections on nodes already planned (A2–A6) and one Sampler
engine (C).

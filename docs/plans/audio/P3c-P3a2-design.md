# P3c Effects + P3a Part 2 Notes — design and prompt source

The specification two implementing sessions work from. `README.md` §3 says
*which* nodes exist; this says what each one **is** — params with ranges,
defaults and curves, the DSP kernel and its primary reference, the exact body
layout under `audio-node-ui-system.md`'s grammar, the visualizer, and the
tests. §0 is shared infrastructure that must land before any node in either
part. §5 is the prompt generator.

Companions: `new-audio-node` skill (procedure), `audio-node-ui` skill
(appearance), `STATUS.md` (what is shipped).

Benchmarks are named per node so "modern standards" is checkable rather than
asserted. The benchmark is the **behaviour and param set** to match — not code
to read. The clean-room rule covers BespokeSynth; ordinary professional
practice is that you match a plugin's control surface because that surface is
what users already know, and you write the DSP from its published reference.

---

# §0 — Shared infrastructure, built first

Five pieces. Every node in both parts depends on at least one. Building them
per-node instead produces four incompatible "rate" controls and three
disagreeing scale tables, which is the single most likely way this phase goes
wrong.

## 0.1 `RateDivision` — the musical pulse, and the answer to "can I change it"

**Yes — every rhythmic node carries its own rate, expressed as a note
division against the global time signature, and every one is independently
settable.** There is no hidden fixed pulse anywhere.

New header `src/audio/MusicTime.h`, shared by DSP and UI for the reason
`SynthModes.h` states: a dropdown built from one list and a switch written
against another drift apart silently.

```
enum RateDivision {  // index order IS the dropdown order, slowest to fastest
   k4Bars, k2Bars, k1Bar,
   kHalf, kHalfDot, kHalfTrip,
   kQuarter, kQuarterDot, kQuarterTrip,
   kEighth, kEighthDot, kEighthTrip,
   kSixteenth, kSixteenthDot, kSixteenthTrip,
   kThirtySecond, kThirtySecondTrip,
   kSixtyFourth,
   kNumRateDivisions
};
```

- `double BeatsFor(RateDivision d)` — the division's length **in beats**, the
  unit `Transport::Beats()` already speaks. Dotted = ×1.5, triplet = ×2/3.
- `double BarsToBeats(double bars)` — reads the time signature (0.2).
- Default for every node: `kSixteenth` for step grids, `kEighth` for
  arpeggiator and echo.

Each rhythmic node exposes **three** params, not one:

| Param | Type | Meaning |
|---|---|---|
| `sync` | bool, default **true** | rate follows the transport, vs. free-running |
| `rate` | `RateDivision` | the division, when `sync` is true |
| `freeHz` | float 0.01–50 Hz, log | the rate when `sync` is false |

UI: one `row.Dropdown("rate", ...)` cell showing `1/16`, `1/8.`, `1/4T`, `1 bar`
— the labels every DAW uses — which swaps to a `rate Hz` knob when `sync` is
off. The `sync` toggle is Tier 2. This is exactly Ableton's and Bitwig's
sync/free control and needs no explanation to anyone who has used either.

**Scheduling rule, and the reason this is infrastructure rather than a
convenience.** A node computes its next fire time in *beats* and converts to a
sample offset inside the block:

```
frameOffset = int((fireBeats - blockStartBeats) * secondsPerBeat * sampleRate)
```

`NoteEvent::frameOffset` (see `NoteEvent.h`) exists for precisely this. A node
that instead fires "once per `ProcessBlock`" quantises everything to the block
boundary (~10 ms at 512 frames) and the whole part sounds loose. Every note
node in Part B must schedule to the sample.

## 0.2 Time signature on `Transport`

`Transport` today has BPM and beats but **no bar concept**, so nothing can
express "1 bar" or "reset every bar". Add:

- `SetTimeSignature(int numerator, int denominator)`, numerator 1–16,
  denominator ∈ {1, 2, 4, 8, 16}; default 4/4.
- `double Bars() const` — musical bar position, derived from `Beats()`.
- `double BeatsPerBar() const` — `numerator * 4.0 / denominator`.

Both atomics, safe from either thread, matching the existing members' pattern.
Surfaced in the transport UI next to BPM. Everything bar-relative in Part B
(sequencer length, arp retrigger-on-bar, euclidean rotation) reads these.

## 0.3 Global key and scale — the musicality lever

New in `src/audio/MusicTime.h`. This is the piece that decides whether a patch
sounds musical by default or needs per-node setup to stop sounding wrong.

- `Transport::SetKey(int root /*0=C..11=B*/)`, `SetScale(ScaleType)`.
- Scale table, name and interval set declared side by side:
  major · natural minor · harmonic minor · melodic minor · dorian · phrygian ·
  lydian · mixolydian · locrian · major pentatonic · minor pentatonic · blues ·
  whole tone · chromatic.
- `int SnapToScale(int midiNote, int root, ScaleType, SnapDir)` — `SnapDir` ∈
  up / down / nearest.
- `int DegreeToNote(int degree, int octave, int root, ScaleType)` — lets a
  sequencer store **scale degrees** rather than semitones, so changing the key
  transposes the whole patch in key. This is the single highest-value
  musicality decision in the document.

**Every note node gets a `useGlobalScale` bool, default true**, with local
`root`/`scale` overrides greyed out while it is set. Global-by-default is how
Bespoke and Ableton's Scale device behave, and it is what makes "drop in an
arpeggiator and it is already in key" true.

## 0.4 `AudioEffectNode` — the table-driven param surface

P3c's stated deliverable, and the reason Audio Filter is built first. **The UI
is table-driven; the DSP is not** — a reverb and a compressor share no kernel.

One `EffectDef` table entry per effect declares: display name, category
(`AudioEffects`), body width, the Tier 1 control list, the Tier 2 section
list, the visualizer id, the readout-stat formatter, and a kernel id. One
`AudioEffectNode` class walks that table to build the body, `VisitParams`, and
the mailbox push. Seven kernels live in separate files under `src/audio/dsp/`
behind a small `IEffectKernel { PrepareToPlay, ProcessBlock, Reset }`.

What this buys: params declared once (so `VisitParams` and the mailbox push
cannot disagree — the "forgot to push it through the mailbox" class the
`AUDIOPARAMSWEEPTEST` sweep exists to catch), and adding effect eight is a
table row plus a kernel.

What it does **not** cover, and must not be forced into the table: the
visualizers. A frequency-response curve, a goniometer and a transfer curve
share nothing. Each is its own draw function selected by the visualizer id.

## 0.5 Effect conventions every one of the seven obeys

- **`mix` (dry/wet) 0–1 on every effect**, Tier 1 unless the effect is
  inherently fully-wet. Equal-power crossfade, not linear.
- **Latency reporting.** Lookahead (Dynamics), oversampling (Drive) and the
  FFT window (Pitch Time) each add delay. Each kernel reports
  `LatencySamples()`; the engine compensates. Uncompensated latency in a
  parallel path is a comb filter, and it is diagnosed as "the reverb sounds
  phasey" rather than as a latency bug.
- **Denormal guard on every feedback path.** Delay, Reverb and filter
  feedback tails decay toward zero and hit denormal territory, where CPU cost
  spikes 10–100×. The engine's FTZ/DAZ is the first line; add the tiny-DC
  offset or explicit flush in each kernel too.
- **Audio input at slot 0**, sidechain (Dynamics only) at slot 1. One cable per
  audio pin — summing goes through Mixer (`audio-graph-semantics.md` §1).
- **Smoothing.** Every continuous param is one-pole smoothed (~5 ms) by
  `ParamMailbox`. Delay *time* additionally needs interpolated read to avoid
  zipper artefacts when swept.

---

# §1 — Effects: the seven

Each entry: benchmark · Tier 1 (hard cap 6, `audio-node-ui-system.md` §5) ·
Tier 2 sections · DSP kernel and primary reference · visualizer · readout stat
· tests. All bodies are `kAudioNodeWidth` (440) unless noted.

## 1.1 Audio Filter — build this one first

Replaces *filter* and *EQ*. **Benchmark: FabFilter Pro-Q 3, Ableton EQ Eight.**

`bands` = 1–4, each with type / freq / Q / gain. An EQ *is* filters in series.

**Tier 1** — band count, then the *selected* band's four controls:

| Control | Range | Default | Curve |
|---|---|---|---|
| `bands` dropdown | 1–4 | 1 | — |
| `type` dropdown | LP 12/24/36, HP 12/24/36, BP, Notch, Low Shelf, High Shelf, Peak, All-pass | LP 24 | — |
| `freq` knob | 20 Hz – 20 kHz | 1 kHz | log |
| `Q` knob | 0.1 – 18 | 0.707 | log |
| `gain` knob | −24 – +24 dB | 0 | linear, greyed unless shelf/peak |

Slope spelled into the type (`LP 24` is one choice, not two) — the reasoning is
already written in `SynthModes.h` and this reuses that table's shape.

**Tier 2:** *band strip* — per-band enable + solo + a band selector.
*output* — output gain −24…+12 dB, mix 0–1.

**Kernel:** RBJ Audio-EQ Cookbook biquads for shelf/peak/notch/BP/AP; cascaded
12 dB `DspMath::TptSvf` stages for the LP/HP slopes, matching the Wavetable's
existing filter so the two sound consistent. Coefficients recomputed
main-thread on param change, pushed as coefficients (not as freq/Q) so the
audio thread never runs a `tan()`.

**Visualizer — the reason this node is first.** Full-width log-frequency
response curve, 20 Hz–20 kHz, −24…+24 dB, with a **draggable handle per band**:
X = freq, Y = gain, scroll = Q, double-click = enable/disable. Per §3g the
picture *is* the parameter. Computed main-thread from the closed-form magnitude
response — never by calling into the live `AudioNode`. At rest it draws the
grid, the octave ticks, the 0 dB line and the current (flat, if unset) curve.
This is the Pro-Q interaction, and it is the single control that makes the
node feel like a real filter rather than four knobs.

Latch the handle on press for the whole gesture; `PushUndoCheckpoint()` once
per gesture on `IsItemActivated()`.

**Stat:** `"LP 24 - 1.0 kHz - Q 0.71"`, or `"3 bands"` when bands > 1.

**Tests:** magnitude response at known cutoffs against the analytic biquad
response (±0.5 dB); −3 dB point per slope; stability sweep — no NaN across the
full freq × Q grid at every type.

## 1.2 Dynamics

Replaces *compressor*, *limiter*, *gate*, *expander*, *gain staging*.
**Benchmark: FabFilter Pro-C 2, Ableton Compressor.**

Identical signal path throughout — detector → gain computer → makeup. A limiter
is ∞:1 with a hard knee.

**Tier 1:**

| Control | Range | Default | Curve |
|---|---|---|---|
| `mode` dropdown | compress / limit / gate / expand | compress | — |
| `threshold` knob | −60 – 0 dB | −18 | linear |
| `ratio` knob | 1:1 – 20:1 (∞ in limit) | 4:1 | log |
| `attack` knob | 0.05 – 200 ms | 10 | log |
| `release` knob | 5 – 2000 ms | 100 | log |
| `makeup` knob | −12 – +24 dB | 0 | linear |

**Tier 2:** *detector* — knee 0–24 dB (6), peak/RMS, RMS window 1–100 ms,
lookahead 0–10 ms, stereo link 0–100 % (100), auto-release toggle.
*sidechain* — external toggle (slot 1 pin), sidechain HP 20–500 Hz, audition.
*gate* — hold 0–500 ms, range −60–0 dB (greyed outside gate/expand).
*output* — mix 0–1 (parallel compression, and it is Tier 2 precisely because
reaching for it means you already know what it does).

**Kernel:** log-domain gain computer with soft-knee interpolation; branching
peak detector with separate attack/release one-poles; RMS via a leaky
integrator. Lookahead = a delay line on the signal path, not the detector,
with reported latency. References: Giannoulis, Massberg & Reiss, *Digital
Dynamic Range Compressor Design — A Tutorial and Analysis* (JAES 2012) — the
soft-knee gain computer and the branching/decoupled detector topologies both
come straight from it with test cases included.

**Visualizer:** transfer curve (input dB → output dB) showing the knee, with
the **live operating point riding on it** as a dot and a gain-reduction bar
down the right edge. Curve is main-thread from params; the dot and bar are one
`MeterRing` value each. At rest: the curve, the 1:1 diagonal, and dB ticks.

**Stat:** `"compress 4:1 - GR 3.2 dB"`.

**Tests:** static gain-reduction curve vs. analytic at 5 thresholds × 4 ratios;
attack/release time constants measured off a step input (±10 %); limit mode
never exceeds threshold on a +12 dB burst; gate hold prevents chatter on a
signal dithering across the threshold.

## 1.3 Delay

Replaces *delay*, *freq delay*, *stutter*. **Benchmark: Soundtoys EchoBoy,
Valhalla Delay, Ableton Echo.**

**Tier 1:**

| Control | Range | Default |
|---|---|---|
| `mode` dropdown | simple / ping-pong / multiband / stutter | simple |
| `rate` dropdown | `RateDivision` (§0.1), or `time` 1–2000 ms free | 1/8 |
| `feedback` knob | 0 – 110 % | 35 |
| `mix` knob | 0 – 1 | 0.3 |
| `filter` knob | bipolar tilt −1…+1 in the feedback path | 0 |

Feedback above 100 % is deliberate and standard — it is how every one of the
benchmarks lets a delay build into self-oscillation. Clamp the *output*, not
the feedback coefficient.

**Tier 2:** *time* — sync toggle, free Hz, L/R offset ±50 % (ping-pong spread),
`Transport` sync toggle. *tone* — feedback HP 20–2000 Hz, LP 200 Hz–20 kHz,
saturation 0–1 (drive in the loop, the thing that makes repeats degrade like
tape). *movement* — mod rate 0.01–10 Hz, mod depth 0–20 ms (chorusing on the
tail), ducking 0–1 (dry signal sidechains the wet — the modern "delay that
stays out of the way" control). *stutter* — gate rate, gate depth, freeze
toggle (greyed outside stutter).

**Kernel:** one fractional-delay line, Hermite (4-point, 3rd-order)
interpolated read so time sweeps do not zipper; per-mode routing on top of it.
Multiband = 3-way Linkwitz-Riley crossover into three lines. Reference:
Välimäki et al., *Fractional Delay Filters — Design and Applications*.

**Visualizer:** decaying tap bars on a time axis (tap position = delay time,
height = feedback-decayed amplitude), with a wet/dry level pair beneath. Fully
main-thread from params except the two meter values. Deliberately **not** a
full IR view — that is the draw cost `README.md` §1 warns about.

**Stat:** `"1/8 - 35% fb - 30% wet"`.

**Tests:** impulse lands at the exact expected sample for 6 divisions at 3
tempos; feedback at 50 % decays 6 dB per repeat (±0.2 dB); ping-pong L/R
alternation; no denormal CPU spike after a 60 s tail (measure block time).

## 1.4 Reverb

Replaces *reverb*, *convolve*. **Benchmark: Valhalla VintageVerb, Ableton
Reverb, Altiverb (convolution half).**

**Tier 1:**

| Control | Range | Default |
|---|---|---|
| `engine` dropdown | algorithmic / convolution | algorithmic |
| `size` knob | 0 – 1 | 0.5 |
| `decay` knob | 0.1 – 20 s (RT60) | 2.0, log |
| `damping` knob | 0 – 1 | 0.4 |
| `predelay` knob | 0 – 500 ms, sync-able to `RateDivision` | 20 |
| `mix` knob | 0 – 1 | 0.25 |

**Tier 2:** *character* — diffusion 0–1, mod rate 0.01–5 Hz, mod depth 0–1
(the thing that stops an FDN ringing metallically), early/late balance.
*tone* — low cut 20–1000 Hz, high cut 1 k–20 kHz, low decay ×0.25–4,
high decay ×0.25–4 (frequency-dependent RT60, the VintageVerb control that
does most of the "sounds like a real room" work). *space* — width 0–1,
freeze toggle. *convolution* — IR file picker, IR gain, reverse, stretch
0.25–4× (greyed on the algorithmic engine).

**Kernel:** 8×8 FDN with a Hadamard mixing matrix, mutually-prime delay
lengths, per-line one-pole damping, Schroeder all-pass diffusion block in
front. Reference: Jot & Chaigne, *Digital Delay Networks for Designing
Artificial Reverberators* (AES 1991), and Rocchesso & Smith on circulant
feedback matrices. Convolution engine: uniform-partition FFT overlap-save;
IR loading happens on a worker thread and swaps atomically — never file I/O on
the audio thread.

**Visualizer:** decay envelope (RT60 curve on a time axis, with the predelay
gap drawn) plus a wet/dry level pair. Main-thread from params.

**Stat:** `"algorithmic - 2.0 s - 25% wet"`.

**Tests:** measured RT60 within ±10 % of the `decay` param at 3 settings;
predelay lands at the exact sample; energy decays monotonically (no build-up →
the matrix is not unitary); damping at 1.0 shows measurably faster HF decay
than LF; 60 s tail has no denormal spike.

## 1.5 Drive

Replaces *distortion*. **Benchmark: Soundtoys Decapitator, FabFilter Saturn 2.**

**Tier 1:**

| Control | Range | Default |
|---|---|---|
| `curve` dropdown | tanh / hard clip / foldback / bitcrush / diode / tube | tanh |
| `drive` knob | 0 – 40 dB | 6 |
| `bias` knob | −1 – +1 | 0 |
| `tone` knob | bipolar tilt −1 – +1 | 0 |
| `output` knob | −24 – +12 dB | 0 |
| `mix` knob | 0 – 1 | 1.0 |

`bias` is not decoration: an asymmetric transfer curve produces **even**
harmonics, which is the whole difference between "fuzzy" and "warm", and it is
why every serious saturator has it.

**Tier 2:** *quality* — oversample 1/2/4/8× (default 4×, with reported
latency), DC blocker toggle (default on — asymmetric clipping generates DC).
*digital* — bit depth 1–16, downsample 1–64× (greyed outside bitcrush).
*filters* — pre HP 20–2000 Hz, post LP 200 Hz–20 kHz.

**Kernel:** polynomial/`tanh` waveshapers evaluated inside an oversampled
block, with a polyphase half-band up/down filter pair. Anti-derivative
anti-aliasing (ADAA, Parker/Zavalishin/Bilbao 2016) on the hard-clip and
foldback curves, where plain oversampling still aliases audibly.

**Visualizer:** the transfer curve, input −1…+1 → output, with the live input
range shaded on it. The curve is the node's identity and it is main-thread from
`curve`/`drive`/`bias` alone.

**Stat:** `"tanh - 6.0 dB - 4x OS"`.

**Tests:** THD at a known drive matches the analytic series for `tanh` (±5 %);
aliasing 20 dB below the fundamental for a 5 kHz sine at 4× on hard clip;
bit-depth quantisation step count is exactly 2^bits; DC blocker removes the
offset a bias of 0.5 introduces.

## 1.6 Stereo

Replaces *stereo*, *panning*, *mono*. **Benchmark: Ableton Utility, bx_control.**

**Tier 1:**

| Control | Range | Default |
|---|---|---|
| `mode` dropdown | stereo / mono / mid-side / swap | stereo |
| `pan` knob | −1 – +1 | 0 |
| `width` knob | 0 – 2 | 1 |
| `bass mono` knob | 0 – 500 Hz (0 = off) | 0 |

Bass-mono is Tier 1 rather than buried, because it is the one control that
turns a wide patch into something that survives a club system, and burying it
means nobody uses it.

**Tier 2:** *trim* — L gain, R gain, mid gain, side gain, pan law
(−3 / −4.5 / −6 dB, default −3 equal-power). *phase* — invert L, invert R.

**Kernel:** equal-power pan from `DspMath`, M/S matrix, Linkwitz-Riley low
crossover for the bass-mono fold.

**Visualizer:** goniometer (Lissajous X = M, Y = S) with a correlation meter
strip beneath, −1…+1. This is the standard stereo display and it is the only
one that makes a phase problem visible. `MeterRing` of decimated L/R pairs,
~256 points at the 30 Hz cap. At rest: the axes, the ±45° mono guides, and the
correlation scale.

**Stat:** `"stereo - width 1.00 - corr +0.9"`.

**Tests:** pan law holds constant power across the sweep (±0.1 dB); width 0
produces bit-identical L and R; M/S round-trips to the input; bass-mono at
120 Hz measurably correlates below and leaves above untouched.

## 1.7 Pitch Time — build last

Replaces *pitch shifter*, *paulstretch*. **Benchmark: Soundtoys Little
AlterBoy, Ableton Complex Pro, PaulStretch.** The heaviest kernel here; budget
it a session of its own.

**Tier 1:**

| Control | Range | Default |
|---|---|---|
| `mode` dropdown | shift / stretch / paulstretch | shift |
| `pitch` knob | −24 – +24 semitones | 0 |
| `fine` knob | −100 – +100 cents | 0 |
| `stretch` knob | 0.125 – 50× | 1 (greyed in shift) |
| `formant` knob | −12 – +12 semitones | 0 |
| `mix` knob | 0 – 1 | 1.0 |

Independent formant control is what separates a modern pitch shifter from a
1990s one — without it, shifting up gives you chipmunk.

**Tier 2:** *engine* — window size 512–8192 (2048), overlap 2–8× (4), transient
preserve 0–1. *paulstretch* — smear 0–1, freeze toggle (greyed outside
paulstretch). *scale lock* — snap the shift to the global scale (§0.3), so a
shift of "+3" lands on the in-key third rather than always a minor third.

**Kernel:** phase vocoder with per-bin phase-locking (Laroche & Dolson,
*Improved Phase Vocoder Time-Scale Modification of Audio*, IEEE 1999 — the
identity phase-locking that removes the classic phasiness). Formants via
spectral-envelope shift over a cepstral estimate. Paulstretch = the same STFT
with randomised phase and heavy overlap. Report the window latency.

**Visualizer:** spectrum bars (reuse the hand-rolled bar draw
`DrawAudioAnalyzeParams` already uses), input behind output.

**Stat:** `"shift +7 st - formant 0"`.

**Tests:** a 440 Hz sine shifted +12 measures 880 ±1 Hz; a 2× stretch produces
exactly 2× the samples and preserves pitch ±5 cents; formant shift moves the
spectral centroid while leaving f0 fixed; no NaN at the range extremes.

## Build order for Part A

**Audio Filter → Dynamics → Delay → Stereo → Drive → Reverb → Pitch Time.**

Filter first because it forces `AudioEffectNode` (§0.4) and the draggable-curve
visualizer pattern that Dynamics and Drive then reuse. Stereo is placed early
and out of "difficulty" order deliberately — it is small, and once it exists
every other effect can be auditioned in stereo. Pitch Time last because it is
the only one whose kernel can slip a session.

---

# §2 — Notes: the seven, and where musicality comes from

`MIDI Notes` (shipped) is the source; `Envelope` (shipped) is the note→modulator
sink. These seven sit between them.

## 2.0 The four levers that make it musical

Asked directly: musicality is not a param on one node, it is these four things
being shared and on by default.

1. **Scale-aware everything (§0.3).** Sequencer steps are stored as **scale
   degrees**, not semitones. Arp octaves walk the scale. Note Echo's pitched
   taps snap to the scale. Change the global key and the whole patch
   transposes in key. Default on.
2. **Swing, defined the way an MPC defines it.** `swing` 50–75 %, applied to
   odd-numbered subdivisions of the node's own `rate`: at 50 % the grid is
   straight, at 66.7 % it is triplet-feel. Shared helper in `MusicTime.h` so
   the sequencer, arp and echo swing *identically* — three different swing
   implementations in one patch is a phasing mess.
3. **Humanise, split into two controls.** `humanizeTime` 0–50 ms (uniform
   random, applied to `frameOffset`) and `humanizeVel` 0–50 % — separately,
   because timing looseness and dynamic variation are different musical
   decisions and one knob for both is why "humanize" controls usually get left
   at zero.
4. **Probability and ratchets per step**, not per node. Probability makes a
   loop stop repeating identically; ratchets (1–8 retriggers inside one step)
   are what modern hardware sequencers use for fills. Both are per-step data.

Every node in this part carries `sync` / `rate` / `freeHz` (§0.1), so **yes —
each node pulses on its own division, independently settable, against a time
signature you can change.**

## 2.1 Note Sequencer

Replaces *note sequencer*, *euclidean*, *polyrhythmic*, *note recorder*.
**Benchmark: Elektron/Ableton step sequencers; Bitwig's note grid.**

Body width `kAudioNodeWidth`; the **step grid is the visualizer** (grammar §1
region 3 — the editable readout takes that slot).

**Tier 1:** `pattern` dropdown (grid / euclidean / polyrhythm) · `steps` 1–64
(16) · `rate` (§0.1) · `swing` 50–75 % · `gate` 1–200 % · `octave` −4–+4.

**Tier 2:**
- *euclidean* (greyed otherwise) — pulses 1–64, rotation 0–63. Bjorklund's
  algorithm; reference Toussaint, *The Euclidean Algorithm Generates Traditional
  Musical Rhythms*.
- *polyrhythm* (greyed otherwise) — per-lane length 1–32, 4 lanes.
- *scale* — `useGlobalScale` (on), root, scale type.
- *feel* — humanise time, humanise velocity, probability (global scale on the
  per-step values).
- *record* — record-arm button, overdub toggle, clear. Arming captures incoming
  note events from the input pin onto the grid, quantised to `rate`. This is the
  "note recorder" node, folded in as a button because that is where every DAW
  puts it.

**Per-step data** (edited on the grid): on/off · degree (scale degree, or
semitone when scale is off) · velocity 0–1 · probability 0–100 % · ratchet 1–8 ·
tie. Grid interaction: click toggles, vertical drag sets degree, shift-drag sets
velocity, right-click opens the step's probability/ratchet.

**Pins:** note in (slot 0, for record-arm and for chaining), note out.

**Stat:** `"Grid - 16 steps - 1/16"`.

## 2.2 Arpeggiator

**Benchmark: Ableton Arpeggiator, Bitwig Arpeggiator, Roland hardware arps.**

**Tier 1:** `mode` dropdown (up / down / up-down / down-up / converge /
diverge / as-played / random / chord) · `rate` (§0.1) · `octaves` 1–4 ·
`gate` 1–200 % · `swing` 50–75 % · `velocity` mode dropdown (as-played /
fixed / pattern / accent).

Nine modes rather than the usual four, because converge/diverge and down-up
(which repeats the endpoints, unlike up-down) are exactly the ones that stop an
arp sounding like a preset. Cheap to implement — they are orderings of one
sorted held-note array.

**Tier 2:**
- *octave* — octave mode (up / down / alternate), octave distance 1–2.
- *hold* — latch toggle (arp continues after keys release), retrigger mode
  (on new note / on bar / off).
- *feel* — ratchet 1–4, humanise time, humanise velocity, note-length free
  ms override.
- *velocity* — fixed value, accent every N steps 2–16, accent amount 0–100 %.
- *scale* — `useGlobalScale`, root, scale, scale-lock (forces the octave walk
  into the scale rather than in raw octaves).

**State:** a sorted array of currently-held notes, rebuilt on every note-on/off
from the input. `latch` keeps the array after the last note-off. Note-offs for
generated notes are scheduled by `gate` — the arp owns them, and every generated
note must have its off scheduled at generation time, or a stuck note is one
missed edge case away.

**Visualizer:** the held-note array as a small keyboard strip with the current
step highlighted — main-thread from the published held-note words, the same
pattern `MIDI Notes` already uses.

**Stat:** `"up - 1/8 - 2 oct - 3 held"`.

## 2.3 Note Filter

Replaces *scaling*, *quantiser*, *note range filter*, *note chance*. All four
are gates on a note's pitch. **Benchmark: Ableton Scale + Pitch + Chance.**

**Tier 1:** `scale snap` toggle · `root` dropdown · `scale` dropdown ·
`snap dir` dropdown (up / down / nearest) · `chance` 0–100 % · `range` mode
dropdown (off / block / fold / clamp).

**Tier 2:**
- *range* — low note 0–127, high note 0–127, drawn as a two-handle range on the
  keyboard visualizer. `fold` transposes an out-of-range note by octaves until
  it fits (musical); `clamp` pins it to the boundary; `block` drops it.
- *chance* — per-note vs. per-event, random seed (so a "random" pattern is
  reproducible across a save/load — without a seed, chance makes a patch
  unrepeatable, which is a common and avoidable frustration).
- *velocity gate* — min/max velocity pass band.
- *scale* — `useGlobalScale`.

**Note-off correctness, the one real trap in this node.** If a note-on is
transposed by snapping, its note-off must be transposed **identically**, or the
voice never releases. Keep a 128-entry map from incoming note → emitted note,
written on note-on and read on note-off. Same rule applies to a note-on that
`chance` dropped: its note-off must be dropped too. Every node in §2.3–2.5 that
alters or drops pitch needs this map; state it in each prompt.

**Visualizer:** a two-octave keyboard with in-scale keys lit, the range handles
drawn on it, and passing notes flashing.

**Stat:** `"C minor - nearest - 100%"`.

## 2.4 Note Modify

Replaces *transposing*, *note duration*, *note panning*, *velocity
expressions*, *note expressions*. The plan's biggest single consolidation.

**Tier 1:** `transpose` −48–+48 semitones · `octave` −4–+4 · `degrees` −14–+14
(scale-aware transpose — +2 degrees moves up a third in the key, which is the
musical operation semitone transpose is not) · `velocity` ×0–2 ·
`gate` ×0.05–4 · `humanise` 0–50 ms.

**Tier 2:** *velocity* — offset −1–+1, curve −1–+1 (exponential shaping),
fixed-velocity toggle + value, humanise velocity 0–50 %.
*timing* — delay/advance ±200 ms, gate free ms override.
*pan* — pan −1–+1, pan spread (per-note random) 0–1.
*drum map* — fixed-note toggle + note (turns any input into one drum hit,
which is how you drive a single drum voice from a melodic pattern).
*scale* — `useGlobalScale`, snap after transpose.

Needs the same note-on→note-off map as 2.3.

**Visualizer:** none natural — a stat line instead, per the catalogue's
"everything else" row. Body drops to Tier 1 rows + sections.

**Stat:** `"+7 st - vel x1.00 - gate x1.00"`.

## 2.5 Note Echo

Genuinely distinct from Modify: it **generates new notes over time**.
**Benchmark: Ableton Note Echo, Bitwig Note Echo.**

**Tier 1:** `taps` 1–16 (4) · `rate` (§0.1) · `feedback` 0–100 % (velocity
decay per tap) · `pitch` −12–+12 semitones per tap (cumulative) · `gate` ×
0.1–2 per tap · `mix` (dry note passes through, toggle).

**Tier 2:** *spread* — pan spread per tap −1–+1, ping-pong toggle.
*timing* — per-tap rate multiplier (accelerating/decelerating echoes), humanise.
*scale* — `useGlobalScale` + scale-lock on the pitched taps. **This is the
control that makes pitched echo musical**: a raw +3 semitone cumulative shift
walks out of key by tap three; snapped to the scale it walks up the chord.
*limit* — max simultaneous generated notes 1–32 (a runaway echo with feedback
near 100 % and 16 taps can flood the voice allocator; cap it and say so).

Every generated note owns its scheduled note-off. Taps are scheduled in beats
and converted to `frameOffset` per §0.1.

**Visualizer:** tap bars on a time axis, height = decayed velocity, colour =
pitch offset. Main-thread from params.

**Stat:** `"4 taps - 1/8 - 60% fb"`.

## 2.6 Note Router

Replaces *note distributor*, *note chain*. 1 in, 4 out.

**Tier 1:** `mode` dropdown (round-robin / random / probability / chain /
pitch split / velocity split) · `outputs` 2–4 · plus mode-dependent controls.

**Tier 2:** *weights* — per-output probability 0–100 % (probability mode).
*splits* — three split points, as MIDI notes or velocities, drawn on the
visualizer (split modes). *chain* — per-output note limit before advancing.

Round-robin is what turns one melodic line into four alternating voices — the
classic use — so it is the default.

**Pins:** note in slot 0; four note outputs. Note that `OutputCount()` /
`OutputLabel()` must be overridden — this is the only node in either part with
more than one output.

Needs the note-on→output map so a note-off is routed to the **same** output its
note-on went to. Without it, round-robin leaves stuck notes immediately.

**Visualizer:** four lanes showing recent routed notes, with split points drawn
when in a split mode.

**Stat:** `"round-robin - 4 out"`.

## 2.7 Note Display

Replaces *note displayer*, *keyboard displayer*. `view` = piano roll / keyboard.
Pass-through: note in, note out, unchanged.

**Tier 1:** `view` dropdown · `history` 1–16 s (piano roll) · `low note` /
`high note` display range.

**Tier 2:** *display* — show velocity as colour, show note names, follow
transport (piano roll scrolls with the grid rather than free).

**Visualizer:** this node *is* its visualizer, and it is the one place in this
document where the body should be taller than usual (up to ~140 px). Ring
buffer of recent events drained in `CookIfNeeded`, redrawn at the 30 Hz cap.

**Stat:** `"piano roll - 8 s - 3 active"`.

## Build order for Part B

**Note Filter → Note Modify → Arpeggiator → Note Echo → Note Router → Note
Display → Note Sequencer.**

Filter and Modify first: they are the smallest, they establish the
note-on→note-off map every later node needs, and they are pure transforms with
no scheduling. Arpeggiator third, as the first *generator* — it proves scheduled
note-off ownership. Echo fourth reuses that directly. Sequencer **last** despite
being the headline node: it is the largest UI in the part (step grid with six
per-step fields, three pattern algorithms, record-arm) and it should be built
once the other six have settled the scale, swing and humanise helpers it
consumes.

---

# §3 — What Part B needs from Part A, and vice versa

Nothing. The two parts are independent and can run in either order or in
parallel sessions, **except** that §0 must land before either. If they run in
parallel, §0 is its own session and both wait on it.

---

# §4 — Registry names, checked against collisions

`README.md` §3's collision table stands. Names to register:

| Part | Name | Category |
|---|---|---|
| A | `Audio Filter` | `AudioEffects` |
| A | `Dynamics` | `AudioEffects` |
| A | `Delay` | `AudioEffects` |
| A | `Reverb` | `AudioEffects` |
| A | `Drive` | `AudioEffects` |
| A | `Stereo` | `AudioEffects` |
| A | `Pitch Time` | `AudioEffects` |
| B | `Note Sequencer` | `Notes` |
| B | `Arpeggiator` | `Notes` |
| B | `Note Filter` | `Notes` |
| B | `Note Modify` | `Notes` |
| B | `Note Echo` | `Notes` |
| B | `Note Router` | `Notes` |
| B | `Note Display` | `Notes` |

`Delay` and `Reverb` do not collide with any of the 119 registered visual node
names — but re-run the check at implementation time, since the registry has
grown. `NodeFactory::DuplicateNames()` reports collisions at runtime; the
category string must stay one whitespace-free token (`AudioEffects`, not
`Audio Effects`) or `Patch.cpp`'s `>>` read eats the type name on load.

---

# §5 — Prompt generator

Fill the five bracketed fields from this document and hand the result to a
fresh session. Nothing else needs to be pasted — the session reads the two
skills and this doc itself.

```
Implement the [NODE NAME] node in Infinite (/Users/namansoni/infinte).

Spec: docs/plans/audio/P3c-P3a2-design.md §[SECTION]. Read that section in
full, plus §0 (shared infrastructure — [WHICH §0 PIECES THIS NODE USES]).
Category: [CATEGORY]. Node shape: [audio effect | note processor |
note generator].

Procedure: .claude/skills/new-audio-node/SKILL.md — prescriptive, follow it.
Body layout: .claude/skills/audio-node-ui/SKILL.md and
docs/plans/audio/audio-node-ui-system.md. Do not re-derive either; both have
been through multiple revisions and the reasons are written down.

Four rules that override anything you infer:
1. Clean room: do not open, read, grep or reference
   /Users/namansoni/BespokeSynth. Implement the DSP from the primary
   reference named in the spec section, not from any implementation.
2. Two objects: the INode (main thread) owns an AudioNode (audio thread);
   they communicate only through ParamMailbox and MeterRing.
3. CookIfNeeded does no DSP — drain meters, push dirty params, budget < 5 us.
4. On the audio thread: no allocation, locks, dynamic_cast, std::function/
   map/string, GL, ImGui, file I/O, or printf.

Reference nodes already in the tree: GainNode (smallest complete),
WavetableNode (largest, with its own DSP tests), EnvelopeNode (note-in,
modulator-out).

Write the DSP fixture alongside the node, not after — the spec section lists
the assertions. Then run /run-infinite-hygiene.

Done when all seven of new-audio-node SKILL.md §6's criteria hold. Report
each one explicitly, including the ones that did not pass.

[FOR NOTE NODES THAT ALTER OR DROP PITCH, ADD:]
This node changes or suppresses note pitches, so it must keep a 128-entry
map from incoming note number to emitted note number, written on note-on and
read on note-off. A note-off that is not transformed identically to its
note-on leaves a voice stuck on. A note-on that was dropped must have its
note-off dropped too. See §2.3.

[FOR NOTE GENERATORS, ADD:]
This node generates notes, so it owns their note-offs: schedule the off at
the moment the on is generated, from the gate parameter. Schedule in beats
and convert to NoteEvent::frameOffset per §0.1 — a node that fires once per
ProcessBlock quantises to the block boundary (~10 ms) and the part sounds
loose.
```

## Pre-filled field values

| Node | §  | §0 pieces | Category | Shape | Extra blocks |
|---|---|---|---|---|---|
| Audio Filter | 1.1 | 0.4, 0.5 | AudioEffects | audio effect | — |
| Dynamics | 1.2 | 0.4, 0.5 | AudioEffects | audio effect | — |
| Delay | 1.3 | 0.1, 0.4, 0.5 | AudioEffects | audio effect | — |
| Reverb | 1.4 | 0.1, 0.4, 0.5 | AudioEffects | audio effect | — |
| Drive | 1.5 | 0.4, 0.5 | AudioEffects | audio effect | — |
| Stereo | 1.6 | 0.4, 0.5 | AudioEffects | audio effect | — |
| Pitch Time | 1.7 | 0.3, 0.4, 0.5 | AudioEffects | audio effect | — |
| Note Filter | 2.3 | 0.3 | Notes | note processor | pitch map |
| Note Modify | 2.4 | 0.1, 0.3 | Notes | note processor | pitch map |
| Arpeggiator | 2.2 | 0.1, 0.2, 0.3 | Notes | note generator | pitch map, generator |
| Note Echo | 2.5 | 0.1, 0.3 | Notes | note generator | pitch map, generator |
| Note Router | 2.6 | — | Notes | note processor | pitch map (route map) |
| Note Display | 2.7 | — | Notes | note processor | — |
| Note Sequencer | 2.1 | 0.1, 0.2, 0.3 | Notes | note generator | generator |

The §0 session has no template — it is infrastructure, not a node. Its prompt
is "Implement §0.1–0.5 of docs/plans/audio/P3c-P3a2-design.md", plus the same
four override rules, and its exit criterion is that `MusicTime.h` compiles with
a unit fixture asserting `BeatsFor` on all 18 divisions at 4/4 and 7/8, and
that `AudioEffectNode` builds one working effect end to end (use Audio Filter
as that effect, so §0.4 and §1.1 are one session).

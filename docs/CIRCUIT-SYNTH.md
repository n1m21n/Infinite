# Circuit Synth

A synth node (Synths) that makes its sound by simulating a small electrical circuit in real time.
MIT, clean room: written from textbook circuit simulation only, so it ships in both editions.

## Using it

MIDI Notes (or any note source) -> **Circuit Synth** -> Audio Out. Unpatched, **TAP** plays a C3.

- **Header**: the circuit (preset), the generator's wave (saw, pulse, sine, noise, step), mono / poly,
  tap / hold. Picking a circuit sets its play settings and puts every part knob back to 0.
- **schematic**: the loaded circuit, drawn from its netlist (generator, R, C, L, diodes, lamp,
  Chua / negative resistance boxes, op-amp, ground). Each part carries the name of the knob that
  scales it; hovering a knob lights every part it moves. **A** and **B** mark where the left and right
  sides listen (probe A / probe B).
- **parts**: one knob per part (or group of parts, e.g. every coil of a line). The caption is the part,
  the readout its value; each knob scales its part from 1/8 to 8 times the drawn value (R, C, L,
  generator volts; a diode's knob scales its emission coefficient, i.e. its clipping level).
- **play**: pitch (semitones), glide, contact, attack, release; **C key / L key** make the capacitors /
  coils follow the key, so the circuit's own resonances move with the note (both on: exact tracking).
  **tap** touches the source for an instant (at most a quarter period of the note) and lets the
  circuit ring on its own; **hold** keeps it connected while the note is held. **contact** is how soft
  the connection is: the generator's resistance goes from open to its value over that time, and back.
- **lfo**: rate, depth, shape; target tune (all tracking C and L, +/- 1 octave), power (generator
  volts), damp (the circuit's loss part), level, cutoff (+/- 2 octaves).
- **filter**: an output filter with the same types as the other synths (off, lp / hp 12-36, bp / notch
  12-24, comb + / -): cutoff, reso, key (the cutoff follows the last note, 1 = fully, from 220 Hz).
- **stereo**: probe A / probe B (where each side listens), width (mid/side: 0 mono, 1 as built, 1.5),
  spread (part tolerance between the twins, 0..5 %), level. The scope shows L (cyan) over R (amber)
  and their correlation.

## Circuits (presets)

Diode Clipper (the 2 V saw -> 2.2k -> antiparallel diodes + 56 nF circuit), Fuzz (op-amp with diodes in
its feedback, then a germanium / silicon pair), RC Lowpass, RLC Lowpass, Series LC, Tank Filter,
Twin-T Notch, LC Ladder, LC Tank, Thump, Coupled Tanks, Bell (five tanks at bell partial ratios), LC
Line (a lumped line: a pluck), LC Mesh (a 3 x 4 grid: a membrane), Van der Pol, Pushed VdP, Neon Relax
(Pearson-Anson), Chua (Kennedy's values), BT Kick / BT Tom / Rim (multiple-feedback "bridged-T"
op-amp resonators, 808 style), Buzzer, Ringer. Presets are saved by index: the table only grows at
its end.

## From the MCP

The MCP's `authoring_guide` (section "Circuit Synth", `src/platform/AISkillContent.h`) lists the
preset indices and every setting key. Setting `preset` from a tool changes only the circuit, so a
client sets the play settings (`trigger`, `wave`) with it and puts `k0`..`k7` back to 0; the knobs'
drawn names follow the circuit (`explain` shows them).

## How it works

`src/audio/dsp/CircuitSynthDsp.*` (engine), `CircuitSynthPresets.cpp` (netlists),
`src/nodes/CircuitSynthNode.*` (node), `src/app/bodies/CircuitSynthBody.inl` (panel and schematic).
Each preset places its nodes on a small grid (`Preset::nodeX / nodeY`, a left-to-right chain unless
placed) and a part may ask for a route (`PartDef::route`: L-shaped, symbol on the vertical leg,
detour over / under); the panel draws the rest. `INFINITE_CIRCUITSYNTHUITEST=<preset name>` opens one
node on that circuit (with `IMAGERESYNTH_SCREENSHOT` for a picture; `..._HOVER=<knob>` lights a knob).

- **Modified Nodal Analysis** with trapezoidal companion models for C and L and Norton generators; gmin
  on every node, as in SPICE. The linear part is one matrix, LU-factored only when a value changes
  (key, glide, knob, LFO, contact edge: at most once per 16-frame chunk), never for the sources.
- **Nonlinear parts on ports** (diode, neon lamp with strike / maintain hysteresis, Chua's
  piecewise-linear diode, cubic negative resistance, saturating op-amp): the response of the linear
  network to a unit current at each port is precomputed with the same LU, so Newton runs in the port
  space only (1-3 unknowns), safeguarded by a bracket in the scalar case. Antiparallel diode pairs
  share one exp.
- **Oversampling**: nonlinear circuits near 192 kHz, linear ones near 96 kHz, back to the device rate
  through a 63-tap Blackman-windowed sinc; a DC blocker at 8 Hz (the circuits' own bias is real).
- **Safety**: fixed-size arrays sized for 20 nodes / 64 parts / 4 ports, nothing allocated or locked
  on the audio thread; a state past 100 kV is reset; companion states below 1e-30 are flushed.
- **Stereo, designed in**: every voice runs its circuit twice (L and R). Each part of each twin has a
  fixed tolerance (spread); the twin R's generator wanders around twin L's phase, slowly (0.23 / 0.37
  Hz) and bounded to 3 % of a cycle per percent of spread, like two real oscillators that never sit in
  phase, yet the mono sum never loses the fundamental (a free detune would, once per beat). Each side
  listens at its own probe (a node, a difference of nodes, a weighted sum of up to four nodes - a
  pickup that draws no current - or a part's current). Width is mid/side on the result; poly voices
  are panned across the field. Spread 0 with probe A = probe B is exactly mono.
- **Voices**: mono (last-note priority, legato keeps the circuit running) or poly, capped per circuit
  by an estimate of its cost so one node stays under ~30 % of a core.

## Test status

`INFINITE_CIRCUITSYNTHTEST` (`src/app/selftest/CircuitSynthTests.inl`; driver name CIRCUITSYNTHTEST)
checks the physics (LC tank frequency, RC corner, diode clipper bound, van der Pol self-oscillation,
key tracking), stability with every part swept live in every preset, the default preset's stereo
(correlation < 0.9, mono sum > -3 dB, riff and a 4 s note; spread 0 = mono), CPU per voice and the
poly cap, the node path (a note lands on its frame), and renders a WAV per preset to
`build\circuit-synth-wavs`. Measured numbers: see the run's log.

## Sources

Ho, Ruehli & Brennan, "The modified nodal approach to network analysis" (1975); Vlach & Singhal,
Computer Methods for Circuit Analysis and Design; Shockley diode equation; van der Pol, "On
relaxation-oscillations" (1926/27); Chua (1983) and Kennedy, "Robust op amp realization of Chua's
circuit" (1992); Pearson & Anson (1922) neon relaxation oscillator; textbook Twin-T and
multiple-feedback bandpass design equations. No code, preset or value was taken from any other
circuit-simulating instrument.

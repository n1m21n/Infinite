# Prompt: Spectral Field node (per-bin FFT kernel)

Paste everything below the line into a new session.

---

Design (and only after my sign-off, build) a **Spectral Field** node for Infinite: audio in, FFT, a user-written Field kernel runs once per frequency bin, inverse FFT, audio out. It is a new node, so the design needs my approval before any code.

## The idea (from a user, Beestonian, on Discord)

Every FFT bin gives four primitives: **bin index, frequency, magnitude, phase**. The user patches math on them and outputs a new magnitude and phase per bin, which is then resynthesised to audio. A **bin pointer** lets a bin read magnitude/phase from any other bin, which gives pitch shift, spectrum flip, smear and cross-bin effects. Think "a shader, but for frequency bins": a blank spectral canvas instead of one fixed spectral plugin.

Target use cases the design must cover (each one is also an acceptance test, written as a kernel):
1. Brick-wall low-pass: `mag = freq < cutoff ? mag : 0`
2. Spectral gate / denoise: drop bins below a threshold
3. Spectral freeze: hold magnitudes, let phase advance
4. Pitch shift by an octave: bin k reads bin k/2
5. Frequency shift: bin k reads bin k-N (inharmonic, metallic)
6. Spectrum flip: bin k reads bin (N-1-k)
7. Robot / whisper: phase = 0 / random
8. Cross-synthesis: magnitude from input A, phase from input B (second audio input)

## Ground yourself first (read, don't guess)

- Skills: `infinite-map`, `infinite-field-language`, `new-audio-node`, `new-effect-node`, `field-language`, `field-realtime`, `field-domains`, `field-compiler`, `prior-art-scout`.
- Field language source: `src/core/field/` (lexer, parser, IR, bytecode, backends). Docs: `docs/field/MANUAL.md`.
- The existing Field audio nodes: `src/nodes/FieldSynthNode.cpp/.h` (Field Synth and Field Effect: per-sample, time domain), `FieldNotesNode`, `FieldSampleNode`.
- Existing in-node FFT code to reuse rather than add a new FFT: `src/nodes/PaulStretchNode.*`, `PartialsNode.*`, `ImageSpectralSynthNode.*`, `AnalyzeNodes.cpp`, `AudioEffectNode.cpp` (spectral mode). Find which FFT routine they share.
- Memory rules: new nodes need my approval; audio node minimalism (KHS-simple); node help coverage (4 hand-kept tables plus per-OS branches); no debug tools in release; build only through `scripts/ibuild.py`, one at a time (8 GB Mac); DSP first, not measuring; no Mermaid (ASCII diagrams and tables only).

## Design doc

Write `docs/plans/spectral-field/README.md` using the 12-section research workflow template (goal through next steps). Settle every decision yourself and give a recommendation; don't end with open questions. It must answer:

- **Language surface:** the names the kernel sees (`bin`, `freq`, `mag`, `phase`, `bins`, `sr`, `hop`), the outputs (`mag`, `phase`, or `re`/`im`), and the bin pointer syntax (e.g. `mag(k)`, `phase(k)`, with fractional k interpolated?). Does it reuse the existing Field compiler with a new domain, or need new IR ops? Keep the existing language's settled rules (bare names, state as delay-sugar, rate inferred).
- **State across frames:** the previous frame's mag/phase per bin (needed for freeze, smoothing, phase vocoder). Expose it as `prev.mag(k)`, or through the existing delay-sugar?
- **Phase handling:** raw phase vs a phase-vocoder (true frequency / phase advance) so pitch-shift kernels sound clean. Pick the default and say why.
- **FFT settings:** size (512 to 8192), hop/overlap, window; which ones are parameters; latency reported to PDC.
- **Inputs:** one audio in, plus an optional second input for cross-synthesis; mono or stereo (per channel, or mid/side).
- **Performance:** cost per block at 2048/4x on an M2 8 GB; the kernel runs bins × frames/sec times. CPU or GPU? (Memory: the GPU is for video/3D render, CPU for audio.) No allocation on the audio thread.
- **UI:** the same code-editor face as Field Effect, plus a small live spectrum showing input vs output magnitude. Short labels, no hint text, no dropdown chevrons.
- **Presets:** the 8 use cases above as shipped presets.
- **Prior art:** how others expose per-bin programming (Max/MSP pfft~, Pure Data, SuperCollider PV_ UGens, Csound pvs opcodes, Kyma, iZotope Iris, Spectral plugins), and what Infinite does better (live, patchable, shared language with visuals).
- **Visual tie-in (optional, later):** a per-bin output the visual side can read.

Stop after sections 1 to 7 of the doc and show me a short summary (ASCII diagram, a table of decisions, the 8 example kernels). Build nothing until I sign off.

## After sign-off: build and done-when

- One branch, a commit per step, no attribution lines.
- **Done when:**
  - A headless self-test (`INFINITE_SPECTRALFIELDTEST`) renders each of the 8 preset kernels on a test signal and checks a measurable property (low-pass: energy above the cutoff is under -60 dB; octave shift: the peak moves from f to 2f; flip: the spectral centroid mirrors; freeze: output stays stationary after input stops; cross-synthesis: the output follows A's envelope and B's pitch).
  - An identity kernel (`mag = mag; phase = phase`) gives back the input within -90 dB after the latency.
  - No allocation or locks on the audio thread.
  - CPU per block is reported.
  - Help tables are updated in both directions.
  - DSPTEST is still OK.

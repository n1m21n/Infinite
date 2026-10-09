# Cluster: audio DSP and synthesis (index-opensource scan, 2026-10-09)

Candidates were chosen from my own knowledge and docs/plans/index-opensource-scan.md; they were NOT cross-checked
against the indexopensource.com list (site blocked).

## Projects read
- DaisySP (electro-smith/DaisySP, branch master). LICENSE fetched from raw: "Published under the MIT license". Files read:
  Effects/wavefolder.h, Effects/autowah.cpp, Drums/analogbassdrum.cpp, Drums/hihat.h, Synthesis/vosim.cpp (also fetched, skimmed only: overdrive, decimator, zoscillator, soap, phaser, fm2).
- Not read in depth (licence verified only): STK (thestk/stk, MIT-like text in LICENSE), Airwindows (MIT, Chris Johnson).
  Reason: DaisySP already covers the gaps; Airwindows is one-file-per-effect and mostly duplicates what Infinite has.
- Skipped: Mutable Instruments eurorack (mixed MIT/GPL per file, not verified; DaisySP's drum ports already carry the same algorithms), Faust libs (licence mixed per library, unverified), Cmajor/SuperCollider/Bespoke (GPL, AGENTS.md rule 1).
- Gotcha: DaisySP 404s for several expected paths (fold.h, particle, modalvoice, stringvoice, comb, jitter), so those were not read.

## What Infinite does today (grep-verified)
- 26 audio effects registered in src/audio/EffectDefs.cpp (names at lines 45-836): Audio Filter, EQ, Dynamics, Limiter, Delay, Reverb,
  Drive, Stereo, Pitch Shifter, Chorus, Flanger, Phaser, Bitcrush, Transient Shaper, Stutter, Ring Mod, Frequency Shifter, Tremolo,
  Formant Filter, Wavetable Shaper, Resonator Bank, Cycle Shaper, Spec Blur, Key-Snap, Spectrum Slide, Shape Resonator.
- Kernels live in src/audio/dsp/*Kernel.cpp/.h; shared primitives in src/audio/DspMath.h (TptSvf, comb at ~495, PinkNoise at 255, PolyBLEP).
- Wavefolding already exists: WavetableShaperKernel.h:17-21 (phase wrap = fold); WavetableSynthCore.h:162-166 has a mirror warp. Not a gap.
- Filters: ZdfLadderFilter.h (Huovilainen), formant filter, TptSvf; Karplus-Strong waveguide in MetallicResonator.h:16 and DspMath.h:495-503 comb.
- Synth sources: AnalogNode (AnalogSynthCore.h), WavetableNode (WavetableSynthCore.h, SynthModes.h warp: FM/AM/RM/PD/sync etc.), OscillatorNode
  (OscillatorNode.h:54-60: sine/tri/saw/square only), MetallicNode, GranularNode, SamplerNode, FieldSynthNode, DrumSequencerNode/MpcNode.
- Drive is deliberately one tanh/arctan saturator: DriveKernel.h:6-26 states the multi-mode curve dropdown (foldback/diode/tube) and
  oversampling/ADAA were "cut entirely, not hidden". (grep "ADAA|oversampl" also hits DspMath.h, LimiterKernel.h, WavetableShaperKernel.h, so
  some oversampling exists elsewhere; Drive's own does not.)
- Auto-wah: Audio Filter's envAmount (EffectDefs.cpp:56-62) is an internal sine LFO; the comment at EffectDefs.cpp:79-84 says the
  envelope-follower/sidechain drive was removed. Dynamics has a sidechain input (EffectDefs.cpp:208) but nothing couples a follower to a filter.
- Dynamics (EffectDefs.cpp:202-232): threshold/ratio/attack/release/makeup/RMS-peak/sidechain. grep "gate|expander" in src/audio/dsp/DynamicsKernel.*
  returns nothing, so there is no downward expander / noise gate.
- Drum voices: DrumSequencerNode plays sample files (DrumPatterns.h:48-55 "05-clap.wav" etc.). Synthesised drum presets exist only as Field source
  text in FieldSynthNode.cpp:547 ("Punchy 909 Kick") and :570 ("Acoustic Trap Snare"). grep for hihat/analogbassdrum/snare in src/audio and
  src/nodes finds only those Field presets and unrelated comments.
- Not found by grep (zero relevant hits in src/audio, src/nodes): VOSIM oscillator, Hilbert/quadrature SSB (Frequency Shifter exists at
  FrequencyShifterKernel.h but is the only allpass-pair user), crossfeed (headphone), expander, sample-and-hold audio effect, particle/dust noise
  generator as an audio source (only a Field preset mentions dust).

## What the peer does differently
- DaisySP Wavefolder (wavefolder.h:19-48): input gain (negative = thru-zero) and pre-gain offset for asymmetric folding, DC-blocked. Infinite folds only through table wrap.
- DaisySP Autowah (autowah.cpp:19-65): peak-hold + two-stage smoothing envelope follower maps level to a resonant 2-pole bandpass frequency
  (2^(2.3*env)), with wet/dry and level compensation. Infinite's Audio Filter has no level-to-cutoff path.
- DaisySP AnalogBassDrum (analogbassdrum.cpp:28-95): 808 bridged-T model: trigger pulse -> diode (line 40-50) -> self-FM resonator with attack FM,
  accent, tone, decay, sustain mode. Fully parameterised and trigger-driven; compact (192 lines).
- DaisySP HiHat (hihat.h:190-250): six-square metallic noise source -> SVF colouring -> VCA -> HPF, with tone/noisiness/decay/accent; templated on the noise source.
- DaisySP VosimOscillator (vosim.cpp:19-65): carrier-reset two-formant sine burst oscillator; 3 params (formant1, formant2, shape); a cheap vocal/"choir" timbre source Infinite lacks.

## Concrete improvements
### Existing nodes
1. Audio Filter: envelope-follower cutoff mode (true auto-wah). Port the idea of autowah.cpp: follower from input or the Dynamics-style sidechain pin
   (hasSidechain exists on EffectDef, EffectDefs.h:110-116) into the existing envAmount path (AudioFilterKernel). User benefit: touch-sensitive wah/filter. Skill: new-audio-node + param-truth-audit (envAmount semantics change; preserve saved patches, EffectDefs.cpp:79-84 warns about silent pin loss). Effort S-M. Idea only, no code copy needed.
2. Drive: add a `fold` option (or `color` continuation) using DaisySP's gain+offset wavefold, plus 2x ADAA tanh. Conflicts with DriveKernel.h:6-26's minimalism rule, so this needs an explicit decision (semi-brain). Effort S. Reimplement from the description.
3. Dynamics: downward expander / gate (ratio < 1 region or a `mode`). Not from DaisySP; reported because the gap is real (grep above). Effort S-M, but changes the DynamicsTransfer visualizer.
### New nodes
4. Analog Drum voice node (bass drum + hi-hat + snare, note-triggered): turn the DaisySP models into a source node so drums need no samples. Benefit: sample-free 808-style kit that takes NoteEvents; pairs with DrumSequencerNode patterns. Skill: new-audio-node (synth/note source). Effort M-L. Licence: MIT, near-port of DaisySP; DaisySP's drum files derive from Mutable Instruments, so keep the MIT notice and Electrosmith + Emilie Gillet credit if any logic is ported. Prefer reimplementing the physical model from the 808 schematic idea.
5. VOSIM voice (as a Wavetable/Analog warp mode or small oscillator). Effort S as one more engine mode. Skill: new-audio-node. Licence MIT, tiny algorithm, rewrite is trivial.

## Nothing to learn
Reverb, delay, chorus/flanger/phaser, pitch shift, bitcrush, ladder filter: Infinite's kernels are already richer (tempo sync, analog modes, visualizers) than DaisySP's. STK instrument models (Karplus/bowed/etc.) are partly covered by MetallicNode; not pursued.

## Candidates
Analog drum voices (kick/snare/hat) | sample-free note-triggered drums | M-L | MIT (DaisySP), credit if ported
Auto-wah envelope follower in Audio Filter | touch-sensitive filter, restores removed feature | S-M | idea only
Dynamics gate/expander | missing basic dynamics tool | S-M | none (textbook)
VOSIM oscillator mode | cheap vocal timbre | S | MIT (DaisySP)
Drive wavefold + ADAA | cleaner high-drive, new character | S | MIT idea only

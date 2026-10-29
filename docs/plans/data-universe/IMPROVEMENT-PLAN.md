# Improvement plan: every model behind the Sample generator (written 2026-10-29)

The aim: the generator should sound right because it has learned how sound works, not because of hand rules. Each section
says where the model stands, what limits it, and the next steps, best first. Bars come from GOAL.md and the claims ledger (C1-C11) and do
not move without Naman. A miss is recorded as a miss. Results go to HANDOFF.md, and the running list is in NEXT_SESSION.md.

**What moved this session.** Infinite can generate its own labelled training audio (`tools/universe/wasm_render_parts.js`), and
`frame_features.py` measures it frame by frame. On the role critic, that took held-out top-1 from 54% to 94% and met both bars.
The same pattern (render with Infinite, measure frame by frame, hold out whole styles or banks) is the plan for most models below.

---

## 1. Role critic (GOAL item 4): MET on the generator's own parts
Today: FRR 2.6%, catch 97.9%, top-1 93.9% on held-out styles (`feel/role_critic_render_oof.json`). The dot-shaped test is running at the time of writing; see HANDOFF.
Limits: dots were at rest in training. It checks parts against each other, not against real instruments. Not used by the app yet.
1. **Dot robustness** (started 2026-10-29): render with random dots and variation and test on held-out styles; if it drops, train on both.
2. **Note features** (started 2026-10-29): `frame_features._notes` (notes at once, note length, pitch steadiness) targets plucks/pads/leads/synths.
3. **Effects robustness**: render the parts through Infinite's Reverb, Delay, EQ and Distortion nodes (headless, `infinite-patch-authoring`
   renders a patch), measure, and test on effect-processed held-out styles.
4. **Real instruments**: run `frame_features.measure` on real samples (WD Black; or any local library). Train on generated and real, and test on real packs.
   This is also the "does our kick sound like a real kick" check: score generated parts with a real-trained critic.
5. **Spectrogram model**: a small conv net over the 32-band frames (numpy, or install torch) once 1-4 are in; only if confusions remain.
6. **Ship it**: export weights and cut-offs to C++ (pattern: `tools/universe/export_critic_inc.py`) so `SampleStyles` re-rolls a rejected
   take; measure the re-roll rate and blind-test the takes.

## 2. Knobs: what each control does (GOAL item 3, C2)
Today: held-out direction on reliable cells: Analog 89.5, Oscillator 91.6, FM 91.6, Metallic 95.6, String 92.8, Sampler 90.9. Wavetable is
short of data (1,118 presets for 61 knobs); a render up to about 4,000 presets ran on 2026-10-29, and its rerun is in HANDOFF.
Bar: >= 90% held out for the 7 main synths (Analog, Wavetable, Oscillator, Metallic, String, FM, Sampler).
1. Finish Wavetable: if still < 90%, render one-knob moves only (`own_sweeps.py` MIX "one" share up) for its 61 knobs.
2. Analog (89.5) is 0.5 short: more one-knob pairs on its weakest knobs (`knob_direction.reliable` lists cells with data and no consensus).
3. Held-out notes: train on the C3 windows and test on C1/C5/C6 and pp/mf (the stored STIM windows), so direction is known across the keyboard.
4. Replace per-axis slopes with frame features (attack, decay, brightness over time) so "what this knob does" includes time.

## 3. Sound model: knob settings -> sound (C1, C2 size)
Today: MLP R^2 0.211 on the bank split; it gains +0.10 per doubling of data, so it is data-limited. Renders are deterministic.
1. Render more: 5-10k presets per main engine, overnight, one engine at a time (`own_sweeps.py run --budget`).
2. One model across engines: input = engine one-hot + knob vector; output = full 32-band x time spectrogram (not summary axes);
   use `renders/spectrum.jsonl` (4,382) and the `own_*.jsonl` mel windows. Test on held-out banks and on a held-out engine.
3. Inverse direction (sound -> knobs) for "make it like this": train on the same pairs, score with the forward model.

## 4. Note model: what notes the generator plays (GOAL item 2)
Today: key 51% exact / 70% incl. relative; next-chord +5.6 over baseline (bar +10); per-role register and density MISS (bass 89%, melodic 62% inside corpus range).
1. Bass lift: `SampleStyles.cpp` change (lift a bass line whose middle is under MIDI 33 by an octave) is uncommitted; rebuild wasm,
   rerun `role_spread.py` against the baseline in the old scratchpad (`p-base.jsonl`, `role_base.json`), and commit if it wins.
2. Melodic register: 104 of 273 melodic layers sit above the corpus 95th percentile (79): cap the motif range per role from midi_full.
3. Compare strategies on held-out MIDI (by file): n-gram (now), a small transformer or LSTM on note tokens, and a chord-transition
   model conditioned on style. Score: next-chord gain, key accuracy, register and density per role. Pick the best.
4. Retry the 372 failed MIDI files (365 timeouts: raise to 20 s; 7 dtype bugs).

## 5. Words and tags (C4)
Today: only 5 of 30 words testable; pack-held-out AUC dark .749, punchy .726, hard .524, tight .507, clean .500.
1. Label words from about 40k preset names and tags (presets.jsonl, air_presets, air_plain, air_logic_strips, air_ni_guitarrig, air_ffp):
   a word is a label when it appears in a name or tag; features = measured audio where the preset has it (Diva 25.8k renders).
2. Own renders: tag own-engine presets by which knobs moved (e.g. "dark" = cutoff low), then check that listeners agree (blind test).
3. Test per word on held-out packs; drop a word only with Naman's sign-off.

## 6. Mood: energy and valence (C5)
Today: energy r 0.82-0.84 (met); valence r 0.51-0.55 (missed).
1. Download a public valence/arousal dataset (Naman OK'd); check the licence first; read untrusted files with `python -I` in their own folder.
2. Frame and note features (mode, chord colour, tempo, brightness over time) as inputs; test on held-out songs.

## 7. Genre (C8)
Today: at chance from whole-file axes; drum grooves carry some.
1. Generator renders carry exact style labels: train a style classifier on frame and groove features with held-out seeds, then check that
   real loops from the matching genre land near their style.
2. Use it as a critic: "does this deep house take sound like deep house".

## 8. Effects (audio effects and note effects)
Today: not measured.
1. Audio effects: render each effect node over a fixed set of parts at a grid of knob values, and measure frame features before and after;
   learn knob -> change. This tells the generator what each effect will do.
2. Note effects: feed corpus phrases through each note node (arp, chord, scale, humanise ...) and measure input -> output (density, register,
   rhythm). Same held-out-phrase test.

## 9. Instruments (GOAL item 1 and the instrument list)
Today: String is farthest from acoustic families (ratios 4.3-7.1); pitch-from-audio 66.9% (piano 33%).
1. Guitar body fit: load a measured body table into String (`kBodyModes` is fixed at 10 modes), then re-test string families.
2. Bowed (STK Bowed; patent notice accepted), then a wind node (reed/jet/lip), then a piano node. Designs first; prior art in
   `docs/prior-art/physical-models-2026-10-29.md` (licence traps listed there).
3. Pitch from audio: harmonic-sum pitch estimate so piano stops locking on the octave.
4. The instrument list in the generator grows only when an instrument passes its family test.

## 10. Wire the dots to learned models
Today: hand rules in `SampleStyles.cpp` (brightness :162, energy and bpm :1030, mood switches scale :1038, hats :1420).
Once the knob model (2), sound model (3) and words (5) pass: each dot asks the learned models for the knob changes that move its
axis, and the critic (1) guards the take. Blind test before shipping.

## 11. Product surfaces
1. Sample studio panel in the app (plan settled): blob and dots left, waveform right, export WAV, convert to patch; Shift+W; dock right/top/bottom.
2. Glass restyle of the parts dropdown when the instrument list grows.
3. Website: the dots already reshape takes; after (10), they use the same learned path as the app.

## 12. Tests that need Naman
40-pair blind test (C3), 40 mood clips (C5), and listening checks before anything learned replaces a hand rule.

# Plug-in plan: every synth and effect on the Air (2026-10-28)

Goal: for every plug-in that holds sounds worth learning, one row per preset = recipe (knobs) + measured sound (spectrum, envelope,
velocity, per-note), so the model sees the whole picture. Counts are files found on the Air by `scan_presets.py` (home-folder scan, run
2026-10-28) plus rows already in `~/Infinite/training-data/presets/*.jsonl` (parsed recipes) and `audio/render_features*.jsonl` (renders).

## Three levels of coverage

| Level | What | Tool | State |
|---|---|---|---|
| 1 | Default sound + first 16 knobs at 0 / 0.5 / 1 | `render_pass.py --sweep-params` | 234 of 404 plug-ins done, loop running on the Air |
| 2 | Each preset's own knob values applied through the host parameter list | `preset_render.py` (to build) | not started |
| 3 | Each preset loaded through the plug-in's own state | `omni_render.py` pattern: saved template state + splice the preset in | Omnisphere running (9,315 patches) |

Level 2 needs the preset's value scale to match the host's 0..1 parameter (calibrate once per plug-in against its sweep rows). Level 3
needs a saved template state per plug-in. Where a plug-in's state is plain text or XML (Omnisphere, u-he, Vital) level 3 is the cheaper,
exact route; where it is binary (Kontakt, NI) only levels 1 and 2 are possible.

## Synths by size of the preset library on the Air

| Plug-in | Preset files on the Air | Format | Parsed recipe rows | Render rows (level 1) | Next |
|---|---:|---|---:|---:|---|
| Diva | 20,904 u-he .h2p (shared with Hive, Repro, others) + 1,356 NKS | text .h2p | 15,303 + 1,356 | 339 | level 3 (h2p text is the state) |
| Hive | 2,420 NKS + 89 | .nksf / .h2p | 3,876 + 2,420 | 111 | level 3 |
| Repro-1 / Repro-5 | 1,453 NKS + 12 | .nksf / .h2p | 1,783 + 1,453 | 108 | level 3 |
| Omnisphere | 9,315 patches | XML in .db | 9,526 (knobs: `omni_params.py`) | 393 | running |
| Phase Plant | 1,111 | .phaseplant | 1,111 | 216 | level 2 or 3 |
| Pigments (Arturia) | 1,669 third-party NKS | .nksf | 0 | 99 | reader needed |
| Massive X | 660 + 5 | .nksf | 665 | 99 | level 2 |
| Vital | 800 | .vital (JSON) | 800 | 405 | level 3 (state is the JSON) |
| Serum 2 | 626 | .SerumPreset | 626 | 0 (render loop pending) | level 3 |
| Arturia V-collection (Modular V3, Jup-8 V4, Buchla, Prophet-VS, CS-80, Jun-6, Acid V, Prophet-5, Wurli, MS-20 V) | about 2,000 NKS | .nksf | 0 | 99 to 210 each | NKS reader (one reader serves all) |
| KORG (MS-20, Polysix, M1, MS-20FX) | 293 + 268 + 33 + 3 | .fxp / .fxb | 0 | 105 to 114 | fxp reader (chunk) |
| Zenology, Triton Extreme | none found in the home scan | inside the app | 0 | 117 / 0 | find the store; Triton not rendered yet |
| TAL (J-8, NoiseMaker, BassLine, U-NO, Sampler) | none found | | 0 | 105 / 108 / 0 | find the store |
| Spire, SubLab (69 NKS), Dexed (33 .syx), SaxLab 2 (79 .fxp), DS Tantra 2 (709 .prg), Infiltrator (1,660 .preset) | listed | mixed | 0 | various | after the big ones |
| Sample players (Kontakt 8, LABS, Addictive Drums 2, Addictive Keys, EZkeys 2, Battery 4, Bloom Ensemble Strings, XO, DecentSampler) | .nki / .ncw / .sfz / internal | opaque or sampler maps | EXS and Kontakt maps parsed separately | 81 to 210 | level 1 only, then instrument maps from the drive |

## Effects

Hundreds of audio effects: level 1 gives each an effect response (drum loop, pink burst, sine sweep) at default and 16 knobs.
Preset libraries exist for FabFilter (2,910 + categories parsed), Valhalla (about 3,500 parsed), soothe2 (148), Plugin Alliance (1,698
.aupreset), kHs (40 plug-ins, level 1 only). Level 2 for these gives "how does the knob move the response", which feeds the node
response harness directly.

## Universal reader

`preset_render.py` takes a plug-in name, finds its preset rows (or files), and does one of:

```
preset row (modules -> {param: value})
   -> map names to the host parameter list (cache per plug-in from --plugin-params)
   -> scale values to 0..1 (calibrated against the plug-in's own sweep rows)
   -> write map_slots into the Keyboard -> Plugin -> Audio Out patch
   -> render C2 / C3 / C4 (+ velocity) and measure
```

or, where a template state exists: splice the preset into the saved state (omni_render.py). One output schema for every plug-in, the same
rows as Omnisphere's, so the join with the parsed recipe is by `src`.

## Headless state templates (no GUI step)

`Infinite --audio-summary p.inf out.json --plugin-state` returns each loaded plug-in's saved state; `plugin_states.py` runs it over every
installed plug-in (rows in `plugin_states.jsonl`, key synths in `plugin_states_key.jsonl`). Splice formats found (2026-10-28):

| Plug-in | State container | Splicer | Status |
|---|---|---|---|
| Diva | u64 len + `#pgm=name` + .h2p text, twice | `uhe` | tested on the Pro, presets sound different |
| Hive, Repro-1, Repro-5 | u64 + u32 + text, twice | `uhe` | written, test on the Air |
| Vital | JUCE chunk with the preset JSON | `vital` | written, test on the Air |
| TAL (BassLine, J-8, NoiseMaker, Mod, Sampler, U-NO) | JUCE chunk with `<tal>` XML; 1,800 presets in ~/Library/Application Support/ToguAudioLine | `tal` | written, test on the Air |
| Omnisphere | 32-byte header + multi XML | `omni_render.py` (+ `--multis`) | running |
| Massive X, Pigments, Arturia, SubLab | zlib block or wrapper; preset's own chunk is PCHK inside the .nksf | `nks_state.py --probe` finds the variant per plug-in | probe on the Air |
| Serum 2 | XferJson header + zstd | file-as-state variants all rendered the default | open: level 2 or level 1 |
| Zenology, Triton Extreme | presets live in Roland/KORG packs (.exz/.sdz, PCM bin), no per-preset files | none | level 1 plus pack name lists |
| KORG MS-20, Polysix, M1 | 594 .fxp | fxp reader (chunk) | to build |

Gap tracker: see the numbered list in the session notes; every row above is closed only when its rows are in a jsonl and the audit passes.

## Order

1. Level 1 loop and `plugin_states.py` on the Air (running), merge into `render_features.jsonl`.
2. Omnisphere level 3 (running, about 7 hours at the current shared rate) and join with `omni_params.jsonl.gz`.
3. `preset_state.py` for Diva (Pro, running), Hive, Repro, Vital, TAL (Air).
4. NKS probe + render, fxp reader, level 2 for effects with preset libraries and for Serum 2.
5. Coverage audit again; fill the thin instrument families (`coverage_audit.py`).

## Efficiency rule (2026-10-28, measured)

- Order: farthest-first coreset (`coreset_order.py`); any prefix covers the library. Diva: first 3,000 of 15,173.
- Notes: render C3, C4, C3f only. C2, C5, C3p are predicted from them (`note_predict.py`, held-out R2: spectrum 0.88, scalars 0.81,
  envelope 0.69). Predicted rows must carry `predicted: true` and their held-out score. Cost per preset about 6.3 s instead of 9 s.
- Open: train with and without predicted rows and compare on real rows; nonlinear predictor; velocity check.

# Brief for the Air (Opus): "3D synth tutorial", a 3D node-tutorial film under 20 s

You are the Air (`Namans-MacBook-Air`, Secondary + Marketing). Branch `air/3d-synth-tutorial`, worktree `~/infinte-wt/3d-synth-tutorial`, claim row `air-3d-synth-tutorial` in `docs/NOW.md`. Commit on your branch and stop; do not merge or push `origin`.
Owner has asked for this render now (the "no renders until asked" rule is lifted for this film only).

## Read first (in this order)

| # | Read |
|---|---|
| 1 | `docs/NOW.md`, `docs/plans/two-laptops/README.md` |
| 2 | `docs/brand/BRAND.md` (esp. section 9 "The node in 3D", sections 2, 4 type, motion springs), `docs/brand/brand.json` |
| 3 | Skills: `node-tutorial-film` (+ `references/patch-to-film.md`, `film-recipes.md`), `motion-film`, `film-direction`, `camera-language`, `motion-animation`, `motion-typography`, `motion-sfx`, `colour-and-light`, `model-3d`, `anim-3d`, `infinite-patch-authoring` |
| 4 | Prior films for structure and mix: `~/films/kick-reactive-tutorial` (reference film, copy its `lib/`, `render.py`, `mix.py`, `mux.sh`), `~/films/modulate-anything/BRIEF.md` (brief format). If they are missing on the Air, `rsync` from the Pro first. |
| 5 | `tools/brand/build_3d.py` and `art/brand/3d/` (the 3D node kit and `studio.blend`) |

## The film

| Item | Value |
|---|---|
| Title / hook | "make a synth patch in 5 nodes" (final wording: yours, 3 to 6 words, Patch Type) |
| Length | 18 s target, hard cap 19.5 s, loops (last frame = frame 0) |
| Format | 9:16 native master 1080x1920 60 fps, safe areas per `motion-film/references/composition.md`; 16:9 2K cut only if time remains |
| Look | Midnight mode (`!#151930`), 3D nodes per BRAND section 9 (real slab, wells, knurled knobs, matte tubes, key/rim/fill, Standard view transform). No VO. No overlay FX. Camera sweeps sideways, does not orbit. |
| Series end card | the what-if end card, unchanged (`node-tutorial-film` defaults) |

## The patch (author it real, headless, first)

```
Note Sequencer --note--> Oscillator --audio--> Reverb --> Delay --> Audio Out
        LFO A (slow)  --> Oscillator.cutoff   (filter opens, then closes)
        LFO A (slow)  --> Oscillator.ampDecay (decay lengthens as it opens, shortens as it closes)
        LFO B         --> Reverb mix
```

| Decision (settled, do not ask) | Value |
|---|---|
| Sequencer | 8 steps, 8th notes, five different pitches (A minor pentatonic, e.g. A2 C3 E3 G3 A3), one rest, 120 BPM so bars = 2.0 s |
| Oscillator | saw, unison 1, filter on, cutoff and ampDecay are the two modulated params; check real keys with `Infinite --describe "Oscillator"` (keys seen: `cutoff`, `resonance`, `ampDecay`, `waveform`) |
| Modulation | two LFO nodes (one per rate), cabled with real `mod` lines; LFO A one full open-close cycle over the film's body (about 8 to 10 s), LFO B a different rate so the reverb swell is not locked to the filter. Depth/centre chosen by ear so the filter audibly goes from dull to bright to dull and the reverb goes dry to wet. |
| Reverb / Delay | defaults as base, mix modulated on Reverb only; Delay mix 0.3, 3/16 or 1/8 dotted synced, feedback low |
| Chain on screen | 7 nodes max: sequencer, oscillator, reverb, delay, audio out, LFO A, LFO B |

Steps (run all, per `infinite-patch-authoring`): `--describe` each type, write the patch, `python3 tools/patch-layout.py`, `--validate` (exit 0), `--explain` (every wire is there), `--audio-summary`, then `--render` for the real audio. The headless engine is the sound source: **the film's music is the engine's rendered audio**, trimmed to the film length. Binary: `build/Infinite.app/Contents/MacOS/Infinite` in the worktree if present, else `/Applications/Infinite.app/Contents/MacOS/Infinite`. If neither exists, stop and write the blocker in your commit; do not build (Air may not build).

Then `python3 ~/.claude/skills/node-tutorial-film/scripts/patch_to_film.py patch.inf --out assets/patch --bpm 120` for `chain.json` and `beats.md`. Every number on screen (knob positions, readouts, LFO range and rate, step pitches) comes from `chain.json`, never retyped.

## 3D nodes (the Blender tool exists; extend it)

| Step | Do |
|---|---|
| 1 | Check `get_addon_status` on the Blender MCP, or use `tools/brand/build_3d.py` (system Python re-launches Blender at `/Applications/Blender.app/Contents/MacOS/Blender`, override with `$BLENDER`). |
| 2 | The kit has Audio Filter, LFO and Field Pixel only. Add **Note Sequencer, Oscillator, Reverb, Delay, Audio Out** as new node builders in `build_3d.py` (or a new `tools/brand/build_3d_synth.py` importing its helpers), following BRAND 9.1 steps 1 to 8. LFO reuses `node_lfo.glb`. Layout comes from the node body code (`src/app/bodies/`, Draw<Node>Body/Params) and `docs/plans/ui-system/element-sizes.md`; row order, labels and values must match the app. |
| 3 | Same card set, same slab size, same light and camera as the kit; category colour only on title strip, jacks, cables; the key cable (the Oscillator cutoff cable from LFO A) is Ember, modulation cables use the Modulation colour. |
| 4 | Make it live (BRAND 9.2 animation table): sequencer playhead steps on the beat, knobs turn on the soft spring following the real LFO values, cables draw ease-out and plugs seat on the slab spring, Audio Out meter follows the render's level. |
| 5 | Check side by side with the app's layout (top view `node_top.png` per node) before any beauty render. Owner screenshots are only needed if the code leaves a layout ambiguous: list those nodes in your commit message instead of inventing them. |
| 6 | Render 1080p proof stills first (hook + two cards), then the full film. Cycles with low samples for proofs; at most 3 renders in parallel on this Mac. |

## Acts (18 s at 120 BPM, bar = 2 s)

| Act | Time | Shows | Sound |
|---|---|---|---|
| Hook | 0 to 1.5 | title + coral hand word, sweep to the canvas | the sequencer's first notes (dry, filter closed) |
| Build | 1.5 to 7 | Sequencer, Oscillator, Reverb, Delay, Audio Out land in order, each cable seats as the next card lands | notes already play through each stage as it joins (patch audio, cut to match), SFX tick/seat clicks underneath |
| Open | 7 to 13 | LFO A drops in, ember cable to cutoff, filter knob and decay knob turn; sound opens | filter opens, decays lengthen |
| Wet | 13 to 16.5 | LFO B to Reverb mix, mix knob swells, camera pulls back over the chain | reverb blooms, filter closes again |
| End | 16.5 to 18 | end card, folds into frame 0 | tail gated before the loop |

## Sound

- Music = the engine render of the patch (real-time sound); it is the film. Do not replace it with a synthesised groove.
- SFX of your choice **underneath**: card contact thuds, cable zips, plug-seat clicks, a soft riser into the "Open" moment, tuned to A minor (`motion-sfx`: sound 0 to 45 ms early, peak per class, never louder than the music). Synth kit in `~/.claude/skills/node-tutorial-film/lib/sfx_synapse.py` and `mix.py`.
- Loudness -14 LUFS, -1 dBTP. Mux with `mux.sh`; check last frame = frame 0 with a frame diff.

## Done check (all must pass before you commit)

| # | Check |
|---|---|
| 1 | `--validate` exit 0 and `--explain` matches the diagram above; the `.inf` is saved in the film folder and committed under `docs/handoff/` or the film's `assets/patch/` |
| 2 | Every on-screen value equals `chain.json`; the modulated knobs sweep the same range on the same clock as the audio |
| 3 | `python3 ~/.claude/skills/film-direction/scripts/film_audit.py out/<film>.mp4 --bpm 120` clean; length under 20 s |
| 4 | `python3 tools/brand/brand_lint.py` on any new brand file does not get worse; 9:16 stills pass `safesheet.py` |
| 5 | Contact sheet of 8 frames + the `.mp4` in `~/films/3d-synth-tutorial/out/`; BRIEF.md in that folder (use `modulate-anything/BRIEF.md` format) |
| 6 | Commit on `air/3d-synth-tutorial` (no Claude attribution lines, per project memory), update `docs/NOW.md` (remove the claim, one decision line), then stop. Report: length, file path, anything flagged "ask owner". |

## Do not

Never UI-script the live Infinite app. Never read GPL source. No debug tools in any shipped surface. No second renders "to be safe": proof stills, then one master.

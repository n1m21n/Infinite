# Spatial audio

End result: a Spatial Mixer node with a head visualiser; each input cable is a
draggable dot (azimuth, elevation, distance, width), every dot a ParamRef.

## Framing (sections 1-7 await owner sign-off)

| # | Section | Proposal |
|---|---|---|
| 1 | Goal | Spatial Mixer node with head view; drag dots to place up to 12 inputs in 3D |
| 2 | Problem | Mixer pan is L/R only (AudioNodes.h `pan`); no height/depth/front-back, no spatial export |
| 3 | Scope | In: objects, binaural, distance/room, binaural/AmbiX/ADM/IAMF export. Out: Dolby codecs, WFS |
| 4 | Constraints | kAudioMaxChannels = 8, kAudioMaxNodeInputs = 12, MIT clean-room, 3 platforms, realtime thread |
| 5 | Hypothesis | Objects + HRTF + small room (+ head tracking) externalise on plain headphones; one scene drives all outputs |
| 6 | Baseline (once) | Same blind test (8 azimuths x 3 elevations) on today's Mixer pan AND Logic's Atmos binaural render (industry reference); Mixer CPU |
| 7 | Success metric | See v2 benchmarks below: match or beat Logic |

## v2 benchmarks (owner: approve if industry-grade)

| Pillar | Benchmark |
|---|---|
| Accuracy | mean azimuth error <=20 deg (generic HRTF); front/back <=25% untracked, <=10% tracked; elevation error <=25 deg |
| Accuracy | loudness flat +-1.5 dB around the circle (diffuse-field EQ HRTF) |
| Accuracy | 360 deg/1 s dot sweep: no clicks, residual <= -60 dB |
| Accuracy | objective: rendered ITD/ILD match HRTF set within tolerance (headless fixture; models from papers, AMT is GPL - never read) |
| Accuracy | ADM opens in Logic + validator; IAMF decodes in iamf-tools; AmbiX ACN/SN3D order verified |
| Performance | 12 objects <=5% one core, 32 objects <=10% (48k/128); <=1 block latency; head-to-sound <=60 ms; 0 inputs = ~0 CPU |
| UX | first-time user places 8 sources <=60 s; every dot modulatable/undoable/saved; per-object binaural mode + width; node-ui-pillars |
| Innovation | geometry emitters in Block B; LFO/Field/Prediction move sounds; orbit/path modes; low-end mono lock; masking hints |
| Cross-platform | bit-exact DSP on 3 OSes; head-tracking fallback per OS |

Self-score v1 -> v2: accuracy 5->8, innovation 6->8, UX 4->7, realtime 6->8, industry parity 5->7, cross-platform 7->8.

## Baseline (locked 2026-10-08, one listener, 48 trials each, generic HRTF, no head tracking)

Kit: tools/spatial/make_baseline.py -> ~/infinite-spatial-baseline; scored by tools/spatial/score_baseline.py.

| Condition | Azimuth error | Front/back confusion | Elevation error |
|---|---|---|---|
| Today: Mixer equal-power pan | 57.2 deg | 50% | 18.2 deg |
| Logic, Apple renderer, binaural | 67.1 deg | 50% | 41.6 deg |
| v2 target | <=20 deg | <=25% | <=25 deg |

Observed: L/R correct in both; every back source heard in front; Logic binaural heard high (~60 deg) and pulled to centre (90 deg heard as ~25 deg) - in-head localisation.
Logic file objective: ITD ~0.65 ms at 90 deg, ILD up to ~12 dB with HF head shadow (genuinely binaural).
Test-rig lessons: hold a voice L/R check before any session (owner's headphones were swapped); the top view must state orientation; no metronome in bounces; no cached audio.



| Block | Delivers |
|---|---|
| A. Headphones | Node, SOFA HRTF, early reflections + diffuse room (externalisation), head tracking (AirPods on macOS; webcam fallback all 3 OSes), HRTF picker (short front/back listening test over a few maximally different sets), head-view UI, modulatable dots, binaural export |
| B. Scene + outputs | Geometry emitters, ambisonic bed, VBAP speakers, channel cap -> 16 (7.1.4), AmbiX / ADM BWF / IAMF export |
| C. Perception | Low-end mono lock, masking hints, anthropometric HRTF matching, spectral cue enhancement research |

## Node spec: Spatial Mixer (proposed 2026-10-08, rev 3)

The Spatial Mixer of section 1, built as an output: category Utility, beside Audio Out. Mixer stays the plain L/R summing node; Spatial Mixer sums in 3D and plays/exports the result (audio-graph-semantics rule 2: only mixers sum).

### Inputs: drag in many cables

| Rule | Spec | Why / today |
|---|---|---|
| Auto-growing pins | Always one empty pin at the bottom ("+"). Drop a cable on it and a new pin and dot appear. Max 12 (`kAudioMaxNodeInputs`) | Today's Mixer makes you set a `channels` slider first (`NoteBodies.cpp:157`); dropping a cable should be enough |
| Drop onto the stage | Dropping a cable onto the circle connects it to the next free pin and places the dot where it was dropped | Places and connects in one move |
| New dot default | Spread round the front arc (0, +-30, +-60 deg...) so new sources don't stack | Stacked dots in the centre can't be grabbed |
| Unplug | Dot greys out and keeps its position; reconnecting restores it. "Tidy" in the node menu removes empty slots | Positions are part of the mix and shouldn't be lost to a slipped cable |
| Labels | Dot label = upstream node's name (`kick`, `Wavetable 2`), pin label = the same | Matches what's patched, not "1..12" |
| Mono/stereo in | Mono = one dot. Stereo = one dot with a width arc (L/R spread around it) | Same as Logic and Atmos object panners |
| Topology | Pin count change -> `RebuildAudioTopology()`; slot index stays stable for save/load and modulation | Same path Mixer uses |

### Stage: mixing in the circle

| Gesture | Does |
|---|---|
| Drag dot | Direction + distance (distance = gain + air + room-send, not just level) |
| Scroll / shift-drag on dot | Height |
| Alt-drag on dot | Level (dB), shown as the dot's ring thickness |
| Drag dot's arc ends | Width (stereo sources) |
| Click dot | Selects it; its row in the source strip highlights |
| Shift-click / box-drag | Multi-select; dragging rotates/scales the group around the head |
| Double-click dot | Reset to its default spot |
| Right-click dot | mute, solo, binaural on/off for that source, remove |

Every dot drawing is live: dot core = colour, ring = level meter (post-gain peak, same cache as Mixer's `ChannelLevel`), faint line from head to dot while dragging. Rings: 1 m, 2 m and edge distance. front/back/left/right ticks. Grid snaps (15 deg) while holding Cmd/Ctrl. Height in the top view: dots grow above ear level and shrink with a dashed outline below it; the orbit view shows height exactly.

### Source strip (under the stage)

One compact row per input, same order as the pins: colour chip, name, level fader, mute, solo. Clicking a row selects the dot. This keeps exact numbers and mute/solo reachable when dots overlap; it scrolls inside the body at more than 6 sources rather than growing the node.

### Selected-source knobs (one knob row, P1 grid)

| Cell | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Selected source | azimuth (large) | elevation (large) | distance | width |
| Master | head tracking (dropdown, leftmost per P3: off / AirPods / webcam) | room | bass mono (Hz) | out (dB, last) |

P4 (`mix` last) is for effects; a mixer ends on `out`, matching Gain.

### Head

| View | Drawing |
|---|---|
| Top view | Vector top-down head from `ImDrawList` (anti-aliased, scales with zoom): skull oval slightly longer front-to-back, nose bump, two ears, shoulder arc behind, faint 60 deg "facing" wedge in front. Fill = surface tone, outline = text tone, so it holds contrast in light and dark (node-ui-pillars contrast budget). No bitmap, no emoji-style circle |
| 3D orbit | Small low-poly head mesh (a few hundred triangles, embedded, flat shaded with the theme's light direction) inside the room box, like Logic's; drag empty space to orbit; Top / Front / Side buttons snap the camera |
| Head tracking on | The head turns live with the listener; the room and dots stay put (that's what makes back sounds read as back) |
| Room > 0 | Room box outline drawn at the room size |

### Output: a terminal node, no out cable (owner decision 2026-10-08)

The node is a mixer and an output in one, like Audio Out (`AudioOutputNode`, `AudioNodes.h:246`): its rendered buffer joins `AudioEngine::RunTopology`'s terminal buffers and is summed into the device output. Nothing can be chained after the render, which is how Atmos/Logic work (renderer last). Effects go on each source before its pin.

| Area | Spec |
|---|---|
| Name | Spatial Mixer (category Utility, next to Audio Out). One node: mixer + output + export |
| Pins | Inputs only (auto-growing, see Inputs). No output pin |
| Playback | Rendered to the device; PDC-aligned with other Audio Outs; `live` toggle as Audio Out |
| Render mode | Dropdown: binaural (headphones) / stereo (speaker-safe pan). Block B adds speaker layouts (VBAP) |
| Master section | Linked true-peak limiter (on by default, same gain on both ears so direction is kept), loudness meter (LUFS short-term + true peak) |
| Export panel | Record button, time, file size and dropped-sample count like Audio Out; destination folder remembered |
| Export formats, Block A | Binaural WAV/FLAC 24-bit; head fixed facing front, never the recorded head movements |
| Export formats, Block B | ADM BWF (objects + positions over time), AmbiX (ACN/SN3D), IAMF, 7.1.4 channel WAV, dry stems + position CSV |
| Tracking while recording | Two renders: monitor = head-rotated, file = facing front. CPU for the second render counts in the perf budget |
| Movie export | Output node's movie audio uses the facing-front render (check with av-sync-sweep) |

Why it removes the earlier edge cases: no binaural signal can reach a reverb, ping-pong delay, Splitter or a second Spatial Mixer, so double ear filtering and smeared cues after the render can't happen, and object exports always match what you hear.

| Case | Rule |
|---|---|
| Reverb / delay on the whole mix | Use the node's `room`; or put the effect on a source before its pin. A shared send bus is Block B (`send` per source -> internal reverb rendered as a diffuse bed) |
| Dry stereo source that shouldn't be placed (click track, stereo stem) | Either its own Audio Out (sums at the device, turns with your head), or a Spatial Mixer pin set to `head-locked` (no ear filters, gain only) so it's in the export too |
| Two Spatial Mixers | Both sum at the device; one global head pose; each exports its own file |
| Spatial Mixer + Audio Out together | Both play; Spatial Mixer's export holds only its own inputs, said in the export panel |
| Output device is speakers | Status line says "binaural needs headphones"; render mode switches to stereo with one click |
| Mono output device | Warning: binaural folds badly to mono; stereo render mode advised |
| Fast dot modulation | Positions smoothed per block, crossfaded between HRTF points; no Doppler in Block A |
| Sample-rate change / device loss | HRTF set resampled off the audio thread; node stays silent until ready, never clicks |
| Wide stereo source (pad, reverb tail) | Width arc; width 0 folds it to a point |
| Feeding its own output | Impossible (no output pin) |

### Standards checklist (must pass before merge)

| Standard | Check |
|---|---|
| audio-node-ui | Fixed `kAudioNodeWidth` 440 (square stage needs it; no Mixer-style growth with count); one visualiser at full body width; sections are panels, not hairlines; captions = param names; knob sizes carry hierarchy |
| Readout strip | Never empty: idle `"5 in -> binaural - tracked - -14 LUFS"`; hover/drag shows `vox  az -30  el 10  2.1 m  -6 dB` |
| No canvas tooltips | All values go to the readout strip (no `SetTooltip` inside `ed::Begin/End`) |
| node-ui-pillars | Row grid, mod pin centred on its control, light/dark contrast on dots, rings, head |
| Modulation | Per source azimuth, elevation, distance, width, level are ParamRefs (`VisitParams`); modulated dots show their moving position plus a ghost of the base position |
| Undo | One checkpoint per drag, not per frame |
| Save/load | Slot positions, names, mute/solo, master params round-trip (ROUNDTRIPTEST, audio-node-sweep) |
| Bypass | 2+ input node: never bypasses (bypass rule) |
| Cables | cable-logic-sweep: auto-grow pin, drop-on-stage, unplug, no output pin, head-locked pin |
| Export | Binaural file matches the monitor with the head facing front; null test vs a tracking-off render <= -60 dB |
| Help | Node help tables + per-OS head-tracking text (node help coverage) |
| Realtime | Positions reach the audio thread through the param mailbox, smoothed per block; no allocation on pin growth in the audio callback |

Why not a Logic-style 3D box as the main editor: dragging in perspective can't separate height from depth, so placement is slow and imprecise. It stays as a view. A single X/Y pad can't show many sources or height.

## Block A status (2026-10-08, branch feature/spatial-mixer-a)

| Area | State |
|---|---|
| Terminal node, 12 auto-growing pins, stage, source strip, export panel (WAV/FLAC, 16/24 bit, live, binaural/stereo) | Built |
| Measured HRTF (MIT KEMAR, baked offline) with parametric model as a second choice | Built; benchmarks in `INFINITE_SPATIALTEST` |
| Room, bass mono, true-peak limiter (-1 dB), BS.1770 LUFS meter, head-locked pins | Built |
| Head tracking: AirPods via CoreMotion, macOS 14+; two renders while recording (monitor turned, file facing front) | Built (macOS); Windows/Linux report unsupported and stay head-fixed |
| Webcam head-tracking fallback | Not built |
| SOFA/libmysofa HRTF picker and the listening test that would justify it | Not built (see decision log) |
| Drop-on-stage connect, Tidy, upstream-name labels, 3D orbit view, right-click menu, mono-device / "needs headphones" warning | Not built |

## Licence notes

Readable: Steam Audio (Apache 2.0), Resonance Audio (Apache 2.0), libmysofa (BSD), EBU ADM Renderer (verify), IAMF tools (verify).
Never read source: SPARTA, IEM Plugin Suite, libspatialaudio (GPL/LGPL).

## Decision log

| Date | Decision |
|---|---|
| 2026-10-08 | Block A targets headphones binaural first |
| 2026-10-08 | Owner: approve 1-7 if industry-grade; benchmarks raised to v2 (Logic as reference baseline) |
| 2026-10-08 | Owner: one node, named Spatial Mixer (mixer + output + export). Submix groups / >12 sources = Block B `Spatial Group` node feeding a Spatial Mixer pin over a scene cable |
| 2026-10-08 | Owner: no output cable; the node is a terminal output with its own spatial export (Spatial Out). Downstream rules below superseded |
| 2026-10-08 | Superseded: downstream rules - binaural stereo out, cueSafe warning, Spatial-into-Spatial = binaural bus (A) / merged group (B), object exports are pre-chain (awaiting owner) |
| 2026-10-08 | Proposed: node spec rev 2 - auto-growing inputs, mix in the circle (dot = position, ring = level), source strip, vector head + 3D orbit head, standards checklist (awaiting owner) |
| 2026-10-08 | Baseline locked (Mixer 57/50/18, Logic 67/50/42). Targets unchanged. Head tracking + HRTF picker + room moved into Block A (evidence: Begault/Wenzel/Anderson 2001 JAES; listener HRTF selection studies) |
| 2026-10-08 | Owner approved a downloaded HRTF set. Chose MIT KEMAR (compact, no usage restrictions, citation requested) baked offline into a C table (`tools/spatial/bake_hrtf.py`) instead of libmysofa + HDF5 + zlib on three platforms. Consequence: the picker is a 2-way choice (KEMAR / model); a SOFA picker needs the listening test first |
| 2026-10-08 | Webcam head tracking deferred: AirPods cover the evidence-backed case (yaw resolves front/back); no Windows/Linux headphone-motion API exists |
| 2026-10-08 | Export depth: 24-bit added to `AudioFileWriter` (WAV and FLAC), used by the Spatial Mixer; other recorders keep 16-bit |

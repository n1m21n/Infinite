# Piano Roll: refinement list (2026-10-30)

One place for everything still to polish in the piano roll. Spec: `README.md` (same folder). Handoff: `NEXT_SESSION.md`.
State: Blocks A (model, node, engine, NoteGen) and B (the panel, Generate) are merged into `main` at 515ad374, not pushed.
Block C has not started.

How to use this file: work top to bottom. Tick a row when it is fixed **and** checked (a test, a screenshot or a sweep).
Fill in the owner-feedback section (§1) after a hands-on session, and turn each note into a row in §2-§4.

---

## 0. Refinement round, owner feedback of 2026-10-30 (built on `feature/piano-roll-refine`)

| Decision | What changed |
|---|---|
| One toolbar row, like Timeline / Performance / Sample Studio | page dropdown, tool dropdown, grid, snap, scale, fold, audition, follow, Quantise, Fx, Generate + type dropdown, then Record and the Edit-icon settings toggle pinned right |
| Pages | a dropdown (switch, New Page, Rename, Delete); the "+" chip and the link chip are gone |
| Assign a node | page settings, "Assign Piano Roll": click it, then click a node on the canvas; Unlink beside it |
| Tools | one dropdown with Select, Draw, Erase, Cut, Mute, Hand and Zoom In / Out / Fit; new Mute and Piano glyphs |
| Settings panel | hidden until the Edit icon (or a note double-click); chips Settings / Generate; Quantise and Functions cards |
| Quantise | the word, not "Q"; chip click quantises, right-click or Shift+Q opens its settings (per note, clip or page) |
| Notes | double-click the roll = new note; double-click a note = its settings; right-click selects it and opens one menu; Delete deletes |
| Copy / paste / duplicate | notes and whole clips land right after the original (clip grows to hold them), not at the playhead; Shift+D |
| Removed | the expression lane and its curves, takes, the Style combo in Generate |
| Record | MIDI record chip, enabled only with a MIDI controller; writes notes while the transport plays |
| Ruler / strip | Sample Studio's ruler (bar and bar.beat labels by zoom), compact clip strip |

Status: built; panels-sweep 8/8, PIANOROLLTEST and NOTEGENTEST pass, determinism check identical, i18n lint clean. Ratchet: no piano-roll file over its allowance (SampleStudio and Settings were already over). node-ui-sweep: 4 drag fixtures fail with the roll closed (unconfirmed against main). Not driven by the owner. Note settings (Expression at start) were kept.

---

## 1. Owner feedback (fill in while testing)

Open the panel with `Shift+R` or View > Piano Roll. Add a Piano Roll node on the canvas, wire it into a synth, then try each area.

| Area | What to try | Feedback |
|---|---|---|
| Dock | Bottom/top dock, resize the height, close and reopen, the top-bar toggle | |
| Pages | `+` page, rename (double-click or `Cmd+R`), `[` / `]`, link to a node, the page colour | |
| Clip strip | Draw a clip, move, trim, blade, `Cmd+D`, copy/paste, colour, loop a clip | |
| Roll | Draw, erase, move, resize, Alt-drag copy, marquee, double-click delete | |
| Zoom and scroll | Pinch, `Cmd+wheel`, `Alt+Cmd+wheel`, `=` `-` `F` `Z`, two-finger swipe | |
| Grid and snap | Grid dropdown, triplets, adaptive, `Cmd` to drag freely | |
| Scale | Highlight follows the global key, scale snap, fold, Fit to Scale | |
| Audition | Notes sound on draw/click/move, the key column plays | |
| Inspector | Nothing / clip / note selected, "mixed" values with several notes | |
| Expression lane | removed in the 2026-10-30 round | n/a |
| Quantize | `Q`, `Shift+Q` (strength, swing, ends) | |
| Functions menu | Legato, Humanize, Reverse, Invert, Double/Half Speed, Strum, Chop, Velocity Ramp, Fit to Scale | |
| Keys | Every key in spec §4.7 while the panel has focus; none fire on the canvas | |
| Generate | Each type, each setting, `G`, ruler-range generate, regenerate a selection, the locks | |
| Takes | removed in the 2026-10-30 round | n/a |
| Drums | A drum page: row labels, drawing, Generate drums | |
| Playback | The node plays the lane in time, loops, chance, mute, follow playhead | |
| Save/load | Save, reopen: pages, clips, notes, takes and links all return | |
| Feel | Anything that feels slow, small, cramped, unclear or unlike Sample Studio / Timeline | |

---

## 2. Known UI issues (found in the review screenshots)

| # | Issue | Where | State |
|---|---|---|---|
| U1 | The inspector's fields ran past the card edge (the Settings column widths in a 268 pt column) | `FormParts::Narrow` | fixed |
| U2 | The Seed row's label sat in a different column from the other rows | `DrawInspector`, all rows use `FormParts::Label` | fixed |
| U3 | The roll showed about three rows at the 340 pt default height | `gPianoRollHeight` is now 480 | fixed; **a height saved in your layout (`panelPianoRollH`) still wins**, so drag it taller once if it opens small |
| U4 | A fresh take landed off-view, so chords looked cut off | `FitRows`, live refit while `autoFit` | fixed |
| U5 | Drum labels overlapped at 12 pt rows | drum rows have an 18 pt floor | fixed |
| U6 | The "Takes" label's baseline | `DrawTakes` | moot: takes removed |
| U7 | The C4 label sits oddly relative to its row; check the octave naming (C3 vs C4 for MIDI 60) matches the rest of the app | key column | fixed: MIDI 60 is C4, as in the note bodies |
| U8 | The rows fit only until the user scrolls or zooms; after that a new take can land off-view again | `FitRows`, `autoFit` | decided: keep refit on every Generate (current) |
| U9 | Screenshots at three zoom levels (spec §10's check) | — | partly done: default zoom, dark and light; the ruler labels switch by zoom in code |
| U10 | Light theme (Infinite Paper) | — | checked on the roll, toolbar and scale lanes (lanes softened); menus not shot |

Screenshot route (any page, either theme):

```bash
INFINITE_OPENPANELS=pianoroll INFINITE_SCREENSHOT_FRAME=30 IMAGERESYNTH_SCREENSHOT=/abs/out.png \
INFINITE_PIANOROLL_GENSHOT="type=chords style=deep_house seed=42" \
build-dev/Infinite.app/Contents/MacOS/Infinite tests/headless/format/piano_roll.inf
```

Add `INFINITE_THEME="Infinite Paper"` for the light theme.

---

## 3. Spec items missing or different in Block B

| # | Spec | What shipped | Action |
|---|---|---|---|
| S1 | §12.3: the Generate card has key/scale override, velocity (dynamics, accent), humanize, voicing, inversion, voice leading, rate, typed numerals, strum ms, arp rate and step gates, bass octave and follow kick, contour, motif repeat, rest amount, drum rows, fills, ghost amount | The card covers only what `GenSpec` holds: Style, Bars, Form, Seed, Lock seed, Creativity, Energy, Mood, Density, Swing, Gate, Low/High Note, Chord Size, Arp Mode, Octaves, Role, Personal, Lock rhythm, Lock pitch | Add each missing field to `GenSpec` and the `Recipe` first (they must reach the engine), then to the card. Only ship controls the engine honours (`param-truth-audit`) |
| S2 | §9: the takes row sits under the clip strip | It sits above the ruler | Decide: keep it (it is near the Generate chip) or move it under the clip strip as drawn |
| S3 | §12.4: takes are recipes (seed + settings), one line each | `gentake` stores the full spec | Fine as is (still one line); update the spec text |
| S4 | §12.3: harmony from another page's chords | Not in B | Block C, with types 9-12 |
| S5 | §4.1: Consolidate | Not in the panel | moved to Block C |
| S6 | §4.1: drag a clip onto another page's tab to move it to that lane | Check: not found in a code search | Add or move it to Block C |
| S7 | §9: the empty-page text "Draw a clip, drop a .mid, or record" | Check in the app | Add if missing (without "record" until Block C) |
| S8 | §4.5: fold shows only keys in use **or** only scale keys | The fold chip says "Fold to the keys in use" | decided: not added; scale snap now colours the lanes, which covers the need |
| S9 | §11 decision 4: Sample Studio's scissors should move from `C` to `B` | Not done | left alone: Sample Studio is outside this round; do it with its own help-table row |

---

## 4. Musical quality (Generate)

| # | Issue | Action |
|---|---|---|
| M1 | The `deep_house` chords take repeats one voicing per bar, so it sounds static | Check `NoteGen` chords for that style: is the progression bank giving one chord, or is the post-pass collapsing it? Compare with the Sample Studio take for the same recipe and seed (they should match, decision 7) |
| M2 | Variety (B5) is 0.72 x real, so takes can feel alike (§12.7) | The Creativity temperature work in `SampleStyles`; judge on unseen seeds with the data-universe tools |
| M3 | Density misses its bar (bass 53.5 %, melodic 78.5 %) | The Density setting overrides; judge per style family |
| M4 | Each type x each style family has not been listened to by a person | Owner listening pass, a few seeds per type; note the bad combinations here |

---

## 5. Checks not yet run

| Check | Why | How |
|---|---|---|
| `panels-sweep` | Block B's own gate (§10) | the skill |
| `shortcuts-sweep` | Every §4.7 key against the global map and the help table | the skill |
| `node-ui-sweep` | The Piano Roll node body, undo, copy/paste | the skill |
| Screenshots at 3 zoom levels, both themes | §10 | the route in §2 |
| Driving the panel by hand | §10 | §1 above |
| `wasm/parity_dump.js` | The new Recipe fields must change no sample | needs emcc (not installed) |
| Ratchet on `main` | `main` already fails on SampleStudioPanel (3) and SettingsWindow (25 vs 22), not from the piano roll | fix on its own branch so the gate is green again |
| Release build + Desktop | So you can test without the dev menus | `scripts/ibuild.py` release mode, then `scripts/deploy_desktop.sh`; check swap is under 4 GB first |

Re-run after any change: `INFINITE_PIANOROLLTEST`, `INFINITE_NOTEGENTEST`,
`python3 tools/determinism-check.py --audio-only tests/headless/format/piano_roll.inf`, and the panel's ratchet row
(`tools/design/ratchet.py`).

---

## 6. Block C (next branch, `feature/piano-roll-c-integration`)

Suggested order, a commit per step:

1. **MIDI tracks in the Timeline** (note thumbnails) and **MIDI columns in Session**; clicking a Session slot opens it on its page with a
   "Session · Scene N" chip.
2. **Per-note gain and pan**: new `NoteEvent::gainDb` / `pan`, read by Oscillator, Wavetable and Sampler, then the inspector fields.
   `param-truth-audit` must pass before the fields show.
3. **Recording** (arm a page, overdub or replace) and **retro capture** (the last 8 bars).
4. **.mid export** (a small writer; drag a clip out to Finder).
5. **Ghost notes** and **select-by** (a pitch, every Nth, a velocity range, invert).
6. **Generation types 9-12** (follow another page's harmony, counter-melody, continue, vary), **Generate all parts**, and Sample Studio's
   **"Notes to Piano Roll"**.

Checks: `param-truth-audit` on gain and pan, `av-sync-sweep` with a MIDI-driven synth in a render, `verify-gate` on the branch.

---

## 7. Where the code is

| Piece | File |
|---|---|
| The panel (about 3100 lines) | `src/app/panels/PianoRollPanel.cpp` |
| Narrow inspector columns | `src/app/ui/design/components/FormParts.h` (`Narrow`, `ControlW`) |
| Default panel height | `src/app/AppState.cpp` (`gPianoRollHeight`) |
| Model and note ops | `src/arrange/ArrangeModel.h` |
| Note generation | `src/app/graph/NoteGen.*`, `src/app/graph/SampleStyles.*`, `OfflineGraph::RenderNotes` |
| Review hook | `INFINITE_PIANOROLL_GENSHOT="<GenSpec line>"` in `DrawPanel()` |
| Tests | `INFINITE_PIANOROLLTEST=1`, `INFINITE_NOTEGENTEST=1` (`INFINITE_NOTEGENSPEC="type=pads style=dub_techno seed=2"` prints one take) |

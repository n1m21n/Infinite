# Prompt: record the Infinite v1 showcase demo

Paste everything below the line into a fresh Claude Code session on the Pro (`Namans-MacBook-Pro-366`),
with the working folder `/Users/namansoni/Infinite/media/films/showcase-demo`.

---

You are recording the shots for the Infinite v1 product demo film, then cutting a 4-bar proof film for
the owner to review. Do not do a full render. Stop at the proof.

## 0. Read first, in this order
1. `~/Infinite/repo/AGENTS.md`, `~/Infinite/repo/docs/DECISIONS.md`.
2. Skills: `film-studio` (the hub), `motion-film` (the Infinite house style), `node-tutorial-film`,
   `motion-animation`, `motion-typography`, `motion-sfx`, `build-discipline`.
3. `BRIEF.md` in this folder. It holds the shot list, the format and the build state. It wins over this prompt
   if the two disagree; note any disagreement in BRIEF.md §8.
4. `tools/rec.py`, the recorder. It has `launch`, `quit`, `Take`, `enlarge` and `find_split`.
   Every pointer action is logged to `src/<take>.json`, so the pointer and the click ripples can be drawn in post.
5. `~/Infinite/repo/docs/showcase/shoot.sh`. It already holds the working CLI steps for the Studio take,
   the Terminal prompt and the hero-panels layout. Reuse them; don't reinvent them.

## 1. Hard rules
- Never push, never publish, never upload. Commit nothing in `~/Infinite/repo` unless the owner asks.
- Builds: `scripts/ibuild.py` only, dev, `-j2`. Don't build at all if `ibuild status` says stressed.
  Use the existing `~/Infinite/infinte-base/build-dev/Infinite.app`.
- Leave the owner's own Infinite (`/Users/namansoni/Desktop/Infinite.app`) alone. `rec.py` backs up
  `~/Library/Application Support/Infinite/control_token`. Make sure `rec.py quit` restores it at the end,
  even after a failure.
- Privacy: no personal paths or names on screen, ever. That means:
  - no Library Samples or Media tab;
  - no `user@host` in the Terminal;
  - no `/Users/namansoni/...` in any field, title or file dialog;
  - no notifications.
  If one shows in a take, retake it. Don't plan to blur it in post.
- No whoosh and no wind on cuts (owner, walkthrough r3).

## 2. Pre-flight (all must pass before the first take)
| Check | How |
|---|---|
| Right laptop | `hostname -s` = `Namans-MacBook-Pro-366` |
| No other Infinite running | `pgrep -fl "Infinite.app/Contents/MacOS/Infinite"` is empty. Other sessions shoot off-screen apps too: if one is running, wait or ask; never kill a process you didn't start |
| Machine calm | `python3 ~/Infinite/repo/scripts/ibuild.py status` not stressed, and no build running |
| Screen | unlocked, owner away from the keyboard, Do Not Disturb on, no other windows over the app. Run `caffeinate -dimsu` in the background for the whole session |
| Display | built-in display at 1440x900 pt (2x). The app goes macOS fullscreen, so `rec.py` uses `TOP = 0` |
| Owner told | send one message, "Recording now, the Mac is taken over for about N minutes, please don't touch it", then go |

## 3. The three open fixes (do these first, check each with `rec.py snap`)
1. **Sample Studio file path.** The Studio does not resolve a relative path. Load the take with
   `--cli studio set "text=..."`, using the absolute `~/Infinite/repo/docs/showcase/media/studio-take.wav` and the
   eight slices exactly as `shoot.sh` builds them. Done when the waveform and eight coloured slices show,
   with no "file not found" message.
2. **Terminal prompt.**
   - Before the shot, send `PROMPT='%1~ %% '; cd <showcase dir>; clear` with `--cli term send`.
   - Click once inside the terminal text area, so the stray selection block left by the `enlarge` drag goes away.
   - Done when the prompt reads `showcase %` and no highlight block is on screen.
3. **Empty Viewport.** Put a card in it. Select the final Output node (node 35) with RPC `select nodes=[35]`
   (or the Render 3D / 3D View node 32), then press Shift+V through RPC `ui key`, or use the node's right-click
   item "Open in viewport panel". Done when the Viewport panel shows the live image with its close button.

Write each fix into `tools/rec.py` as a function (`studio_load()`, `terminal_ready()`, `viewport_card()`),
so every retake starts from the same state.

## 4. Layout rules (the reason for this round: every panel clearly visible)
These apply to every shot. Check them on the shot sheet after each take; a take that fails one is retaken.

| Rule | Value |
|---|---|
| Panel size | Every docked panel is opened with `enlarge(name)`, which puts the divider at y = 280 pt, so the panel fills the lower ~70% of the screen. Two panels at once only in the payoff shot |
| One subject per shot | The thing the caption names (dropdown, cable, knob, slice, clip) is the only thing moving in that shot, and the pointer ends on it |
| Size at 1080p | The subject's smallest text is ≥ 14 px tall in the 1080p output after the camera zoom. The subject fills ≥ 35% of the frame at the zoom peak |
| Camera | log2 zoom inside the card only, max 2.2x. Start at the whole window, push into the subject's box over 0.8–1.2 s, hold, and pull out before the dissolve |
| Holds | After every click, keystroke or menu open, hold ≥ 0.8 s with nothing moving. An open dropdown or menu is held ≥ 1.2 s before it closes. Each take has ≥ 1 s of still handle at both ends for the 0.6 s dissolves |
| Pointer speed | Moves 0.5–0.8 s with cosine ease (as `Take.move` does). Never jump. The pointer rests away from the subject's text, not on top of it |
| Caption band | The caption sits in the band below the card, never over the recording. Nothing important in the bottom 6% of the recording |
| Clean state | `reset()` before each take: escape, close every panel, `view fit`, Library closed. Nodes not selected unless the shot needs it. No tooltips left showing at the cut |
| Playing | The patch plays in every shot (transport on, scene Groove unless the table says otherwise), so meters and the 3D scene move. `tempo 120` is set at launch |

Write each shot's zoom box (in window points) in a `ZOOM = {shot: (x0, y0, x1, y1)}` table at the top of
the recorder script. Read node positions from a fresh `rec.py snap` after `view fit`; never guess coordinates.

## 5. Shots and what each must make visible
One take per shot, named `s01_hook` … `s16_payoff`. Shot lengths are from BRIEF.md §3 (1 bar = 2 s at
120 BPM); record each take 2 s longer than its shot.

| # | Take | Setup | Action (what the pointer does) | Must be clearly visible at the zoom peak |
|---|---|---|---|---|
| 1 | s01_hook | scene Groove, `view fit` | none, slow push-in only | the whole graph, cables lit, the 3D card moving |
| 2 | s02_search | canvas | right-click empty canvas, type `wave` slowly (`Take.type`, gap 0.14) | the search field with the typed text and the filtered list, Wavetable highlighted |
| 3 | s03_categories | canvas | right-click canvas, hover down 3 categories (Synths, 3D, Effects), let each submenu open | the category column and its open submenu, one at a time |
| 4 | s04_dropdowns | Wavetable (node 3) or LFO (node 16) on screen | click its waveform/mode dropdown, hover 2 items, pick one; then a second dropdown | the open list and the chosen value changing on the node |
| 5 | s05_cables | two free pins on screen (an LFO out and an unused input) | drag from the output pin to the input pin with `Take.drag`, dur 0.9 | the cable following the pointer and snapping onto the pin |
| 6 | s06_modulation | after s05 | drag LFO 17 onto a knob, then `enlarge("modmatrix")` | the knob's ring moving, then the Mod Matrix row for that binding |
| 7 | s07_pianoroll | `enlarge("pianoroll")` | click two notes into the grid, then drag one note | notes, playhead, the keyboard on the left |
| 8 | s08_studio | `studio_load()`, `enlarge("samplestudio")` | click two slices, so they flash and play | waveform, 8 coloured slices, the slice being played |
| 9 | s09_timeline | `enlarge("timeline")` | click the ruler to seek, then play across a clip edge | tracks, clips, playhead crossing a clip |
| 10 | s10_session | `enlarge("session")` | click the scene Lift launcher, wait a bar, then Drop | the scene row lighting up and the clip state changing |
| 11 | s11_perf | `enlarge("perf")`, Perform mode | drag two knobs and one fader | the controls moving and the value readouts changing |
| 12 | s12_view3d | panel view3d docked bottom (already large) | slow orbit drag on the axis gizmo | the 3D scene with lights, instances and particles |
| 13 | s13_field | Field Pixel node 19 | open its editor, type one short edit (e.g. change a number), so the preview changes | the code with the edited line, and the preview reacting |
| 14 | s14_terminal | `terminal_ready()`, `enlarge("terminal")` | the `shoot.sh` terminal commands, typed with `--cli term send` at reading speed | the prompt `showcase %`, the commands and their output, readable |
| 15 | s15_settings | settings window (Cmd+0) | click through Appearance, Audio, Field and Terminal | the page list on the left and each page's controls |
| 16 | s16_payoff | the `shoot.sh` hero-panels layout, scene Drop | none, slow pull-out | piano roll, mod matrix, perf and 3D all live at once |

Shots 0 (title) and 17 (end card) have no recording: the engine draws them.

## 6. How to record each take
1. `python3 tools/rec.py launch` once (22 s boot, fullscreen, tempo 120).
2. For each shot, a function in `tools/record_shots.py` runs `reset()`, then the setup, then
   `with Take(name, secs) as t:` and the actions above. Log every action through `Take` (move, click, drag,
   key, type, cli, sh). Don't call RPC outside `Take` while it records, or the post pointer goes out of sync.
3. Run shots one at a time: `python3 tools/record_shots.py s05_cables`. `screencapture` refuses to overwrite,
   and `Take` deletes the old file first.
4. After each take, make its sheet: `python3 tools/shotsheet.py <take> <take>:1:<secs-1>`. Look at
   `probe/shots_<take>.jpg` and tick every rule in §4, plus the "must be visible" cell in §5. Retake up to
   3 times. If it still fails, write why in BRIEF.md §9 and move on.
5. Crop is set in post: window content only (fullscreen 2880x1800, no title bar). Check that frame 0 has no Dock
   and no menu bar.
6. When all takes are done: `python3 tools/rec.py quit`. Then check `control_token` is restored and no
   Infinite you started is still running.

## 7. The 4-bar proof film (then stop)
- Bars 1–4 = title (1 bar), hook (2 bars) and the first half of search (1 bar), cut with the copied walkthrough
  engine (`walk.py` / `render.py`, scenes in `scenes/`).
- The proof includes: pointer and ripples drawn from the take's JSON log, captions with the Ember word,
  log2 zoom, one 0.6 s dissolve, pads from `assets/music/` at -14 LUFS, and click foley at -7/-13 dB on the
  logged clicks (sound 0–45 ms before the frame, never over 40 ms late).
- Output `out/proof_4bar.mp4`, 1080p. Check it with `film-direction`'s `scripts/film_audit.py`, and check
  loudness with ffmpeg ebur128.
- Also make one contact sheet of every recorded take at its zoom peak (`out/takes_sheet.jpg`), so the owner can
  judge the layout of all 16 shots at once.
- Send the owner both files, and stop. No full render until the owner approves.

## 8. Done check
- [ ] 16 takes in `src/`, each with a `.json` action log and a passing shot sheet (or a written reason).
- [ ] Every frame free of personal paths, names, notifications, Dock and menu bar.
- [ ] `out/proof_4bar.mp4` and `out/takes_sheet.jpg` sent to the owner.
- [ ] BRIEF.md §9 updated (state, what was retaken, what is open). The owner's notes go in §8.
- [ ] Owner's `control_token` restored, no stray Infinite running, `caffeinate` stopped.

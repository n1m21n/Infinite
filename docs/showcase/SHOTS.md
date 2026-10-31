# v1.0 (beta) README shots

All nine images in `docs/screenshots/v1/` come from `infinite-v1-showcase.inf` in this folder. `shoot.sh` reproduces them: run `docs/showcase/shoot.sh` for all of them, or `docs/showcase/shoot.sh timeline terminal` for some.

## The patch

There are 35 nodes in two bands.

**Sound**
- Drum Sequencer: kick, clap and hats from `media/`.
- Three Piano Rolls drive a Wavetable pad (into Reverb), an Analog bass, and a Sampler. The Sampler plays `media/studio-take.wav`, two bars cut from the patch's own mix and sliced 8 ways in Sample Studio, into a Delay.
- Mixer, Limiter, Audio Out.
- Three Audio to CV analysers, two LFOs and a Random modulate 10 params across sound and picture, so the Mod Matrix shows 10 live rows.

**Picture**
- FieldPixel (Aurora) and Field Modifier on an Icosphere.
- 3D scene:
  - World, Camera and two Lights
  - two Tori
  - Plane and Cube into Instance on Points, then Set Vertex Color
  - Particle System
- 3D View, then bloom, vignette and Output.

**Arrangement**
- 6 lanes: Stage, Sky, Drums, Keys, Bass, Chops.
- Markers: Intro, Groove, Lift, Drop. Loop over bars 1-16.
- Three scenes (Groove, Lift, Drop) with every column filled.
- A Performance page named "Showcase" holds 3 Knobs, 2 Faders, 2 Sliders and 2 NumBoxes.

Checks:
- `Infinite --validate infinite-v1-showcase.inf`: ok, 35 nodes, no warnings.
- `--audio-summary --duration 16`: -15.8 LUFS, -2.3 dBTP.

## Launch (what shoot.sh does)

1. Wait until `python3 scripts/ibuild.py status` prints `build lock: free`.
2. Launch the app on the patch:
   ```
   INFINITE_NO_SINGLE_INSTANCE=1 INFINITE_CONTROL_PORT=7790 INFINITE_NOSPLASH=1 INFINITE_NO_UPDATE_CHECK=1 \
     "$APP" docs/showcase/infinite-v1-showcase.inf &
   ```
3. Copy `~/Library/Application Support/Infinite/control_token` right after launch, and drive the app with `INFINITE_CONTROL_PORT=7790 INFINITE_CONTROL_TOKEN=<copy> "$APP" --cli ...`. Other running sessions can overwrite the shared token file. Restore the old file when you finish.
4. Run `--cli studio set "text=..."` with the take's absolute path. The Studio does not resolve the relative `src path=media/studio-take.wav` on load (see the gaps below).
5. Run `--cli session scene 1`. This starts the audio engine and plays scene "Groove".
6. Capture only the app window: `screencapture -o -x -l <window id>`. The window id is the largest layer-0 CoreGraphics window of the app's pid (a small Swift helper, built by the script).
7. Shrink with `sips -Z` in 160 px steps from 2880 until the file is under 1.5 MB.

Before every shot, all panels are closed and `view params off` is set.

| Shot | Commands | Width |
|---|---|---|
| `node-graph.png` | `view params on`, `view fit` | 2880 |
| `3d-view.png` | `panel view3d open top`, wait 7 s | 2400 |
| `mod-matrix.png` | `panel modmatrix open bottom`, `view fit` | 2880 |
| `session-clips.png` | `session scene 1`, `panel session open` | 2880 |
| `timeline.png` | `session stop`, `play`, `seek 2`, `panel timeline open top` | 2880 |
| `piano-roll.png` | `panel pianoroll open top` | 2880 |
| `hero-panels.png` | `session scene 2`, then open pianoroll top, modmatrix left, perf right, view3d bottom; `view fit` | 2400 |
| `sample-studio.png` | `panel samplestudio open top`, `view fit` | 2880 |
| `terminal.png` | `panel terminal open right`, `view fit`, then `term send` the four lines below | 2880 |

The terminal shot sends these four lines. The first hides user@host and the home path.

```
PROMPT='%1~ %% '; cd docs/showcase; clear
infinite explain | grep ' mod ' | sed -E 's/^ +mod //; s/ out 0, absolute, depth 1, range/ /'
infinite session scene 2
infinite perf | jq -r '.controls[] | "\(.label) -> node \(.node)"'
```

Use `term send`, not `term run`: `term run` adds a visible `printf '\033]133;D;...'` to the command line.

## Gaps in the app

None of these were changed in C++.

- **Studio take path.** `studio src path=` is not resolved relative to the patch on load, unlike node media paths (`MediaCollect::ResolveRelative`), so it needs an absolute path live. Save also writes it absolute.
- **MIDI clip source.** A MIDI clip with `srcUid 0` (session slots made from the UI) is drawn hatched (offline) in the Session grid until `timeline clip set <id> node=<piano roll>` is run.
- **Timeline view.** There is no RPC for timeline zoom (`gArrangePixelsPerBeat`) or panel height. MIDI lanes are not shown in the Timeline panel, so `timeline.png` shows only the Stage, Sky and Drums lanes and about 5 bars.
- **3D camera.** The 3D View's camera view toggle (key `0`) has no RPC or env, so `3d-view.png` shows User Perspective, not the patch camera.
- **Starting audio.** Only `session scene N` starts the audio engine. There is no env to launch a scene at startup.
- **Terminal prompt.** There is no env for a clean prompt. The prompt is set by hand with `PROMPT=`.
- **Title bar.** It shows a dirty dot (`• infinite-v1-showcase.inf`), because `studio set` and scene launches mark the patch edited.

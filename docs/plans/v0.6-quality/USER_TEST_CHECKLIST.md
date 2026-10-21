# v0.6 user test checklist (build `feature/v06-3.5`, commit 4d538db3)

**Run the right copy.** Launch exactly this one:
`/Users/namansoni/Infinite/worktrees/v06-time-machine/build/Infinite.app`
Infinite itself warns when a patch double-click would open a different copy
(`~/Desktop/Infinite.app`). The Desktop copy is an older build and will NOT show anything below.
The title bar of the right copy says "Infinite 0.5.0", so use the warning, not the version, to tell them apart.

Module names in the Modules panel are lowercase. Type them in "search modules...".
Not in this build: 2.38 launch grid (bar-quantised pause/restart), and anything committed after `5bcbd8dd` on `feature/v06-sound`.

## Block 3 (visual)

### Time Machine (compositing)
- **What goes in "delay map":** a grey image. Black = now, white = the oldest frame kept.
  Use a **ramp** (Source) for a clean test: add `ramp`, set its colours black to white (angle 0 = left to right).
- Steps: add `video` (or any moving source, e.g. `noise` with animation, or a `shape` that moves) -> Time Machine "image".
  Add `ramp` -> Time Machine "delay map". Set the node's frames/delay length to 30+.
- Expect: the black end of the ramp shows the live picture, the white end shows the past, and in between it is a smooth smear in time.
  Swap the ramp's angle to 90 and the effect runs top to bottom. An all-black map must look identical to the input.

### Optical Flow, Proximity Lines (you confirmed both fine)

### Sweep and Revolve (3D category)
- Search `sweep` and `revolve` in the Modules panel (both are in the "3D" category, next to `proximity lines`).
- Steps: add `curve` (3D) -> `revolve`, then `revolve` -> `render 3d`. Expect a closed solid of revolution (vase-like) you can orbit.
- Sweep takes a profile curve and a path curve (two inputs): add two `curve` nodes into `sweep` -> `render 3d`. Expect a tube-like surface along the path, no holes.
- If the search shows nothing, you are on the wrong app copy.

### Render 3D "quality" (3.5)
- Select `render 3d`. The quality block is at the **bottom of the right column**, below "raster". Scroll that column.
- ambient occlusion: raise to ~1 with a few objects on a plane. Expect darker creases and contact shadows where objects meet the floor.
  Turn "occlusion radius" up to widen the effect.
- depth of field: raise the aperture, then move "focus distance". Expect the object at that distance to stay sharp, nearer and farther ones to blur.
- 16-bit float output: tick it. The picture looks the same on screen; it matters for `--frames-dir ... --depth 16`.
- offline supersample (1..64): only acts in offline render/export. Live view is unchanged by design.
- Defaults: all off, so old patches look identical.

## Block 2 (audio, show)
- **First sound:** File > New Synth (Cmd+Shift+N). Expect Keyboard, Analog and Audio Out wired and audio started; press letter keys to play.
- **Sample drop:** drag one audio file onto the canvas. Expect Audio File wired into Audio Out and playing.
- **Timeline keys** (timeline focused): `F` fit, `=`/`-` zoom, Cmd+L loop, `I`/`O` loop in/out.
- **Presets:** right-click `analog` (or oscillator, wavetable, delay, reverb, drive, dynamics, eq) > Presets.
- **New synths** (Synths): `fm`, `partials` (drop a sample in), `string` (pluck/bow/strike). Play each from `keyboard`.
- **New effects:** `convolution reverb` (design the room with size/material/decay), `saturator` (three stages, routing), delay `tape` switch (wow, flutter).
- **Changed nodes:** `dynamics` has a style (clean/bus/opto/fet); `audio filter` has ladder/ms-20/diode types; `splitter` has bands (2-4); `envelope` has hold and per-stage curves; `field` has phase shaping; `wavetable` has formant/pinch/skew warps.
- **Modulators:** `mseg` (draw breakpoints), `voice spread`, and a per-voice switch on `lfo`/`random`.
- **MPE:** `midi notes` in MPE mode drives pressure and slide into note expression.
- **Plugins:** a plugin with a sidechain gets a sidechain pin; a crashed plugin is left out next launch.
- **Stage > Show Set:** list patches, step through live; Show lock = Cmd+Shift+L (blocks delete/drag/new cables); latching panic.
- **OSC control:** server on 127.0.0.1:9100, e.g. `/infinite/node/<index>/<param>` with a float sets a param.

## Block 1 (mostly invisible)
- Audio Out and the Output node's Record/Render show a "clipping +x dB" notice when the master went over full scale.
- Other Block 1 work is measurement and fixes (load-bench, plugin crashes, edit hitches), with nothing new to click.

Items not verified by me in the GUI: placement of individual controls above comes from the code and README, not from clicking through them. Tell me which one is missing or wrong.

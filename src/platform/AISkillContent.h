#pragma once

// Infinite-Turbo 0.46 (MCP phase 4): the patch-building guide for AI
// assistants. Served by the MCP bridge (tool `authoring_guide`, prompt
// `build_patch`) and saved by Settings > AI > "Install AI Skill" as a Claude
// skill folder (infinite-turbo-patching/SKILL.md).
//
// Kept in several raw-string pieces: MSVC caps one string literal piece at 16 KB.

namespace AISkillContent
{
   inline const char* kPatchSkillMarkdown =
R"SKILL(---
name: infinite-turbo-patching
description: Build, change, check and look at patches in Infinite-Turbo (node-based realtime video, audio and 3D for Windows) through its MCP tools. Use whenever the user asks to make or edit an Infinite patch, wire nodes, modulate parameters, set up a live visual or sound, or see what a patch looks like.
---

# Building Infinite-Turbo patches with the MCP tools

Infinite-Turbo runs on the user's Windows machine; the `infinite-turbo` MCP server edits the patch
open in it, live. Every change is visible on the user's screen at once and is one undo step
(Ctrl+Z, or the `undo` tool), so work in small, checkable steps and say what you changed.

## The loop

1. **Know the parts.** `describe` (no arguments) lists every node type with a one-line summary.
   `describe {type}` gives that type's inputs (slot, label, kind), outputs (index, label, kind),
   settings (key, type, default) and help. Never guess a type name, slot or key: describe first.
2. **Look at what is there.** `explain` reads the live graph: nodes, what feeds each input, the
   drawn params (name, range, value, modulation, expression), settings and warnings. Start here when
   the user already has a patch open; do not replace their work unless asked.
3. **Build.** Prefer one `batch` per idea (all or nothing, one undo step):
   `{"commands":[{"method":"create_node","params":{"type":"Shape"}},
                 {"method":"create_node","params":{"type":"gaussianblur"}},
                 {"method":"create_node","params":{"type":"Output"}},
                 {"method":"connect","params":{"srcIndex":"$0","dstIndex":"$1"}},
                 {"method":"connect","params":{"srcIndex":"$1","dstIndex":"$2"}}]}`
   `"$N"` is command N's result index; `"$N.field"` another field of it. `create_node` without
   x/y is placed by the auto layout; `settings` sets keys at creation.
4. **Check.** `explain` again: every wire there, warnings empty (an amber hint means a half-wired
   mixer, an empty Output or an image loop without Feedback).
5. **Look.** `render_frame` shows the Output as the projector sees it; `screenshot_node {index}`
   shows any image node. Judge the picture against the request, then adjust.
6. **Tune.** `set_param` for settings; `modulate` / `set_expression` to make things move.

For a whole patch at once you can write text instead: `patch_format` gives the grammar,
`validate_patch_text` checks it without touching anything, `load_patch_text` replaces the canvas
(one undo step; nodes without `pos` are laid out automatically). `get_patch_text` reads the
current patch as text.

## How the graph works

- **Kinds of cable**: image (2D pictures), geometry (3D meshes, curves, point clouds; also camera,
  light, environment pins of Render 3D), audio, note (MIDI-like note events), modulator (a 0..1
  control signal). `describe` and `explain` say the kind of every input and output; `connect`
  refuses a wrong pairing.
- **To be seen, an image chain must end in an Output** (or Projection / Spout Out). To be heard,
  audio must reach an **Audio Out** (Clip Matrix, MPC, Looper and synths need one too).
- **Mixing: use the Super Mixer.** As soon as a patch has two or more sound sources (synths, drums,
  samples, a Looper, a Plugin, video soundtracks), wire each one into its own **Super Mixer**
  channel (input slots `1`..`16`, numbers 0..15) and only the Super Mixer into the Audio Out. Name
  nothing else "the mix": it gives every source a fader, pan, mute, solo and EQ the user can play
  live, a master meter and the master limiter. Set the levels with `gain<n>` (dB, 0 = unity; start
  sources around -6) and leave `fxLimOn` true for anything loud. Effects that belong to one source
  (a Delay on the lead) go before its channel; a shared Reverb goes after the Super Mixer or on its
  own channel. The plain **Mixer** (8 inputs, levels only) is only for a quick sum inside a chain
  (two oscillators into one filter); **Spatial Mixer** is for binaural / 3D placement.
- **3D** is built from geometry nodes (Geometry, Model 3D, Text 3D, Particle System ...) wired into
  a **Render 3D** (geometry slots, then camera, lights, environment), whose image goes on to effects
  and an Output. Render 3D has four geometry slots: for more objects put them into a **Group 3D**
  (8 inputs, nestable, each object keeps its own material and transform; up to 64 objects per
  Render 3D). **Join Geometry** instead merges meshes into one. Render 3D `renderPass` gives 0 colour,
  1 linear depth, 2 raw Z, 3 normals, 4 position, for depth-of-field, fog or relight effects in 2D.
  A red node border means a geometry input of the wrong kind (a mesh where a point cloud is expected);
  `explain` shows the message under warnings.
- **Feedback** loops need a Feedback (or Trails) node; anything else in a loop is a warning.
- **Slots and outputs** can be given by number or by label (`"dstSlot": "B"`, `"srcOutput": "audio"`).

## Parameters: two names for two jobs

- **Settings** (`set_param`, `create_node.settings`, patch `f/i/b/c/s` lines) use the saved keys
  that `describe` / `explain` list under `settings`: `{"index":3,"name":"radius","value":4}`.
  Colors are `[r,g,b]` 0..1, booleans `true/false`, dropdowns their index. File paths are text
  settings (Video `path`, Image Source `path`, Audio File `path` ...).
- **Modulation and expressions** address the drawn controls that `explain` lists under `params`
  (name and range): `modulate {"index":3,"param":"radius","srcIndex":5}`. A node created a moment
  ago has not drawn yet: the reply says `pending` and it lands within a few frames.
- `polarity: "absolute"` (default) replaces the value with the modulator's 0..1 mapped onto the
  param's range; `"bipolar"` swings around the current value by `depth`. `range` =
  `[inMin, inMax, outMin, outMax]` maps part of the signal onto part of the range.
- Expressions: `set_expression {"index":3,"param":"angle","expression":"t * 0.5"}`. `t` is
  transport seconds, `pi`, `lo` / `hi` the param's range (`lerp(lo, hi, sin(t) * 0.5 + 0.5)`
  sweeps it), and the node's other params by name (`width * 0.5`). Empty text removes it.

## Useful nodes (always confirm with describe)

| Job | Nodes |
|---|---|
| Image sources | Shape, Noise, Ramp, Texture, Formula (GLSL, Shadertoy code as is), Image Source, Video, Video In (camera), Text, Draw, Slideshow, Spout In |
| Effects | the filter family (gaussianblur and many more: describe lists them), Blend, Layer Stack, Layout, Fit, Color Ramp, Curves, Feedback, Trails, Reaction Diffusion |
| Output | Output (a window / projector), Projection (warp + edge blend), Spout Out |
| PointZ (GPU points) | Grid / Sphere / Box / Point Generator / Line / Text PointZ, Noise / Transform / Twist / Math / Field / Random / GLSL PointZ, Instance PointZ, Particle PointZ + Null PointZ, Copy / Merge / Delete / Extrude / Sprinkle PointZ, Image to PointZ (`describe` lists all 85) |
| 3D | Geometry, Model 3D, Text 3D, Material, Mapping, Displacement, the Geometry Op family (Transform, Array, Subdivide, Extrude, Twist, Decimate ...), Join Geometry, Group 3D, Instance on Points, Distribute Points on Faces / in Grid, Delaunay Mesh, Voronoi Cells, Curve, Curve Ops, Particle System, Cloth, Metaballs, Camera, Light, HDRI, Render 3D |
| Modulators | LFO, Envelope, Random, Drift, Pattern, Math, Smoothing, Range to Range, Audio Analyze (audio to CV), Image Analyze, MIDI CC, Macro Knob / Slider / XY |
| Sound | Oscillator, Analog, Wavetable, Circuit Synth (a simulated circuit), Sampler, MPC, Granular, Slicer, Audio File, Audio In, Plugin (VST3), Looper, Audio Out |
| Mixing | **Super Mixer** (16 channels, the default whenever 2+ sources play), Mixer (8-input sum inside a chain), Spatial Mixer (binaural) |
| Notes | Keyboard (virtual keyboard: mouse or computer keys, typingAlways plays without hover), Note Sequencer, Drum Sequencer, Arpeggiator, Pattern Player, Note Looper, Chord Progression, MIDI File, Quantizer, MIDI Notes, MIDI Out |
| Performance | Clip Matrix (+ Clip Matrix Out), Transport Control, Timeline |

)SKILL"
R"SKILL(## Recipes

**A moving shape, blurred, on screen**: Shape -> gaussianblur -> Output; LFO, then
`modulate` the blur radius (or the shape's rotation) from the LFO.

**Audio-reactive visuals**: Audio In (or any sound source) -> Audio Analyze; `modulate` a visual
param (scale, brightness, blur) from one of its outputs (`describe Audio Analyze` lists bands and
level). Keep the sound itself routed to an Audio Out if it should be heard.

**Video with effects**: Video (`set_param path` to a file the user gives you) -> effects -> Output.
The Video's audio output goes to an Audio Out to hear its soundtrack. Any FFmpeg format plays;
for many layers prefer HAP / HAP Q (decoded on the GPU) or Resolume DXV clips.

**Punch FX** (full build only; `describe` lists it when present): audio in -> Punch FX -> audio out.
`effect` 0-26 (0 Loop 1/4 ... 25 Fade out, 26 Freeze) and `engage` 1 fire one effect; `pad<n>fx` and
`pad<n>note` (n 0-15) map 16 pads to effects and MIDI notes (cable notes into the second input); `latch` 1 toggles.

**Chord Progression**: `followKey` 1 makes it follow the global key (`homeKey` = the key it was written in);
`setsKey` 1 does the opposite (never both).

**Pattern Player**: plays a pattern from the bank (`category`, `patternName` text; `playing` 1 runs it).
`source` 0 auto (held notes, else global chord), 1 input, 2 global: the chord on `degree` 1-7 of the
global key/scale, `chordSize` 3-5, so it needs no note input. `speed` 0 half 1 x1 2 double; `gate`,
`swing`, `velocity`, `octave`, `transpose`, `latch`, `syncBar`. Pattern text tokens (for user files):
`.` rest, `-` tie, `1..9` chord tone, `c` chord, `b` bass, `r` random, `+` together; `'` up, `,` down,
`!` accent, `?` 50%, `~` legato, `*N` ratchet. User .mid patterns go in %LOCALAPPDATA%\Infinite\NotePatterns.

`trimStart` / `trimEnd` (steps, 0 = whole) loop part of it; `fitScale` 1 snaps .mid patterns to the
global scale. Bundled packs: Classical themes, Classical figures, Folk and traditional, Basslines,
Latin and Brazil, Jazz and blues, Electronic, Turbo examples.

**Note Looper**: MPC-style note recorder (notes in, notes out): `lengthBars`, `quantize`, `strength`,
`swing`, `countIn`, `thru`, `activeSeq` 0-7, `switchMode` (0 next bar, 1 end of loop, 2 now: how a
sequence change takes over while playing); record / play / overdub from the node's buttons.

**Camera / capture card**: Video In, `active` 1, `deviceId` from the node's device list,
`resolution` (0 Auto, 1 1080p, 2 720p, 3 480p) and `frameRate` (0 Auto, 1 60, 2 30, 3 25).

**A 3D scene**: Geometry (or Text 3D) -> Material -> Render 3D (slot 0), Camera into its camera
slot, a Light into a light slot; Render 3D -> Output. More than four objects: Group 3D into one slot.

**A terrain from points**: Distribute Points in Grid (or a Particle System) -> Delaunay Mesh ->
Displacement / Material -> Render 3D. Voronoi Cells on the same points gives cracked-glass cells.

**A synth line**: Note Sequencer (or Keyboard) -> Oscillator / Analog (note input) -> Super Mixer
channel 1 -> Audio Out. `transport {"play": true, "audio": true}` starts the clock and the audio engine.

**A band / a song**: every source on its own Super Mixer channel (drums 1, bass 2, chords 3, lead 4,
loops and samples next), per-source effects before the channel, the Super Mixer into the Audio Out,
`fxLimOn` true. Put the channel faders (`gain<n>`) and mutes on the Performance panel so the user can
mix live.

## Rules that bite

- Do not invent node types, keys or slots; `describe` them. Filter type names are lowercase ids
  (e.g. `gaussianblur`); `describe` without arguments shows the exact spelling.
- Leave settings you do not need at their defaults.
- One idea per `batch`, then `explain` / `render_frame`. If a batch fails, nothing was changed:
  read the error (it names the command) and fix that command.
- Node indices stay stable while you work; after `load_patch_text` call `explain` for the live
  ones.
- Do not open, save over or delete the user's files unless asked; `save_patch` only to a path they
  give you.
- Respect what is on the canvas: add next to it, and ask before `new_patch` or `load_patch_text`
  over existing work.
- Audio does not run until the engine is on (`transport {"audio": true}`); video sources play
  while the transport plays.
- When something looks wrong, `screenshot_node` each stage of the chain from the source onwards
  to find where the picture goes wrong.

)SKILL"
R"SKILL(## Turbo-only features (not in upstream Infinite)

Infinite-Turbo adds performance tools that upstream Infinite does not have. Prefer them when the
user wants something to play live, trigger clips, loop or mix. Keys below are settings
(`set_param` / `create_node.settings`); check them with `describe {type}` before relying on them.

**Clip Matrix** (session view, Source): rows are tracks, columns are scenes.
- Settings: `rows` (default 4), `cols` (8), up to 16 x 16; `quantize` 0 None, 1 1/16, 2 1/8,
  3 1/4, 4 1/2, 5 1 bar (default), 6 2 bars, 7 4 bars.
- Load a clip: `c<row>_<col>_path` = an audio, video or image file (rows and cols count from 0).
  Per cell: `c<r>_<c>_mode` (0 loop, 1 once, 2 gate), `_quant` (-1 = global), `_gain` dB,
  `_pitch` semitones, `_sync` (stretch to tempo), follow actions `_fon` / `_fa` / `_fb` / `_fch` /
  `_fbars`.
- Scenes `s<col>_name` / `s<col>_tempo` / `s<col>_num` / `s<col>_den`; rows `r<row>_name` /
  `_gain` / `_mute` / `_blend` / `_opacity`.
- Outputs: 0 video (rows composited), 1 audio (wire to an Audio Out). Note input: note
  `baseNote + row * cols + col` launches that cell.
- Play it with `clip_matrix {"index":N,"action":"launch","row":0,"col":2}`; `scene`, `stop_row`,
  `stop_all`, `state`. Launches land on the next quantize line while the transport plays
  (`transport {"play":true,"audio":true}`).
- **Clip Matrix Out** (wire the matrix's audio output into it): one video + one audio output per
  row, for separate effects per track.

**MPC** (16 sample pads) and **VMPC** (16 video-clip pads): `pad<n>_path` loads pad n (0-15),
`pad<n>_mode` 0 one shot, 1 gate, 2 loop toggle; MPC also `_volume` / `_pitch` / `_pan` /
`_start` / `_end` / `_speed` (-2..2, negative = backwards) / `_fine` (cents) / `_fadein` /
`_fadeout` (ms, every pass) / `_sync` (0 synced, 1 free) / `_div` (division index, 6 = 1/4),
VMPC `_start` / `_end` / `_speed`. A synced pad fires on the next line of its division; a synced
loop pad restarts on every line. Play with `pads {"index":N,"pad":0}` (`down` / `up` for gate
pads; `"action":"state"` with `speed` / `fine` / `fade_in` / `fade_out` / `sync` / `division`
only sets them), or notes (base note 36 = pad 0). **MPC Out** (wired from the MPC's output) picks
one pad's own audio for its own effect chain.
VMPC clip switches (Turbo 0.50): `transitionStyle` 0 Cut (default), 1 Fade, 2 Slide Left,
3 Slide Right, 4 Wipe Left, 5 Wipe Right, 6 Zoom Fade; `transitionTime` in seconds. Applies when a
hit replaces another pad's visible clip; the blend starts on the new clip's first frame.

**Slideshow** stepping (Turbo 0.50): mappable triggers `restart sequence` (cut to the first
image, hold timer restarts), `prev image`, `next image` (play the node's `transition`, then the
auto-advance carries on from there); e.g. `perf_add {"index":N,"param":"next image"}`.

**Looper** (audio in): one "take length" menu: free (REC again closes it), 1/16 to 1/2 bar,
1 to 32 bars. From the tools: `looper {"index":N,"length":"2 bars","action":"record"}`, then
`stop_record` (free takes), `play`, `stop`, `overdub`, `stop_overdub`, `clear`, `undo`, `redo`,
`state`. With `in_time` on (default, key `syncStart`) and the transport playing, REC, PLAY and DUB
wait for the next bar line (the take length if shorter) and start exactly on it; pressed just
after a line they start at once, in phase. `state` reports `waiting_s` while one waits. So: start
the transport first, then record or play. Saved keys: `lengthMode` (0 bars, 1 sub-bar, 2 free, 3
division), `bars`, `subDivision` (0 = 1/2 .. 3 = 1/16 bar), `takeDivision`, `syncStart`, `loop`,
`direction`, `thru`, `level`, `speed` / `pitch` / `finetune` (varispeed: away from 1x the loop
drifts and overdub pauses), `fadeIn` / `fadeOut` (ms per pass), `volume`. A Macro Trigger wired
to REC / PLAY / DUB or to an MPC pad presses it on every trigger.

**Super Mixer** (16 channels): per channel `trim<n>` / `gain<n>` / `pan<n>` / `mute<n>` /
`solo<n>` / `eqLow<n>` / `eqMid<n>` / `eqHigh<n>`, plus `master`. Every control is modulatable.
Master bus (0.50): `masterPan` (balance -1..1, unity at centre), `masterMute` (click-free), a
stereo peak / RMS meter with peak hold and latched clip LEDs, and a mastering chain run in this
order, each stage with its own switch (all off by default): EQ `fxEqOn` (`fxEqLowHz`/`fxEqLowDb`
low shelf, `fxEqMidHz`/`fxEqMidDb`/`fxEqMidQ` peak, `fxEqHighHz`/`fxEqHighDb` high shelf, +/-15
dB), glue compressor `fxCompOn` (`fxCompThreshDb`, `fxCompRatio`, `fxCompAttackMs`,
`fxCompReleaseMs`, `fxCompMakeupDb`; stereo-linked, soft knee), width `fxWidthOn` / `fxWidth` (0
mono, 1 unchanged, 2 wide), saturation `fxSatOn` (`fxSatDriveDb`, `fxSatMix`, `fxSatOutDb`), then
the master fader and balance, then a true-peak lookahead limiter `fxLimOn` (`fxLimCeilingDb`
dBTP, default -0.3; `fxLimReleaseMs`; `fxLimLookaheadMs` 0.5..5, default 1.5, never below ~0.9
ms at 48 kHz; `fxLimLink` stereo link 0..100 %; `fxLimTruePeak` 4x inter-sample peak detection,
default on; `fxLimAutoRelease` program-dependent release, default on, the knob then sets the
fast stage; `fxLimStyle` 0 transparent, 1 punchy, 2 loud). While switched in it delays the
master by its lookahead and reports it for delay compensation; switched out there is no delay
and the signal is untouched. Toggling ducks for 2 ms then crossfades (no comb). A red dot on
the limit GR bar means the last-resort clamp caught an over (click the LED to reset). For a
live set: limiter on, ceiling -1, comp at 2:1 with 2-4 dB of gain reduction. `fxOpen` only
unfolds the knobs.

**Layout** (pixel-exact canvas, 8 image inputs): `canvasW` / `canvasH`, per layer `x<n>` / `y<n>`
(canvas pixels), `scale<n>`, `opacity<n>`, `visible<n>`. Use it to place several images exactly
(screens of an installation, a stage layout). **Projection** warps an image to a surface with
edge blend; **Spout Out** / **Spout In** share images with other Windows apps.

**Transport Control**: tempo (`driveTempo` + `bpm`, `glide`), key and scale, time signature, a
click, external MIDI clock, `audioOnOpen` (starts audio when the patch opens). Its outputs give
`beat`, `bar`, `bpm` and `play` as modulators.

**Performance Mode**: a panel of big controls for playing live, bound to any params.
`perf_add {"index":N,"param":"cutoff","kind":"knob","label":"filter"}` (kind: knob, fader, slider,
toggle, xy with `param_y`, trigger, numbox, selector, bipolar, stepgate; the default follows the
param), `perf_list`, `perf_remove`, `perf_show {"open":true,"perform":true}`. When you build
something meant to be played, finish by putting its 4-8 most expressive params on the panel.

**Drum Sequencer patterns** (Turbo): 141 grooves, each in three parts: A verse, B bridge
(lighter / breakdown), C chorus (fuller, often 2 bars ending in a fill). `drum_pattern {}` lists
them (`{"category":"brazil"}` filters) with the lane roles; `drum_pattern {"index":N,
"pattern":"ijexa","part":"C"}` (name, part of a name, or number) fills steps, rate, swing and
accent pitch and loads the bundled kit into empty lanes. For a song, switch parts per section
(A for verses, C for choruses, B for a break). Lanes: 1 kick, 2 snare, 3 closed hat, 4 open hat,
5 clap / rim / clave, 6 low tom / conga, 7 high tom / conga, 8 bell / ride (two-tone: accented
steps play `lane<L>_accentPitch` semitones away), so samples you load should follow that order.
The groove lists a suggested tempo; set it with `transport` if the user wants that feel. Edit
single steps with `set_param` on `lane<L>_step<S>` (velocity 0..1, 0 = off).
For live part changes the node has mappable controls: `part` (selector 0 A, 1 B, 2 C, applies
when it changes), triggers `A verse`, `B bridge`, `C chorus`, `prev groove`, `next groove`, and
`groove` (index in the shown category); e.g. `perf_add {"index":N,"param":"B bridge"}` adds a
trigger, `{"param":"part","kind":"selector"}` a 3-way switch.
0.50: each part keeps its edits (switching A / B / C or groove and back restores them; rate and
swing are shared and never change on a part switch; `reset:true` reloads the library groove).
`drum_pattern {"index":N,"part":"B"}` switches part, `{"index":N,"import":"C:/x/beat.mid"}` reads
GM drum notes from a .mid into the live part (16ths, swing estimated, velocity >= 100 = accent),
`{"list_presets":true}`, `{"index":N,"save_preset":"name"}`, `{"index":N,"load_preset":"name"}`.
The user's own MIDI patterns: .mid files in `%LOCALAPPDATA%\Infinite\DrumPatterns` (subfolders
become styles; an `index.json` array with arquivo/file, titulo/title, estilo/style, bpm,
compassos/bars names them) are folded by style into the library groups (electro-funk in
Electronic, r-b in Funk & Breaks; unmatched styles get a `MIDI: <style>` group) and listed in
the `drum_pattern {}` list with `"source":"midi"`, `path`, `orig_bpm`, `bars`. Load one with
`drum_pattern {"index":N,"pattern":"<path or name>","part":"A"}`: A is the file's first chunk of
whole bars that fits 128 steps (8 bars of 4/4), B and C the next chunks that differ from it
(copies of A for a short file); rate and swing come from the file, the tempo does not change
(say the orig_bpm if the user wants that feel). Edits are kept per file like a library groove.
`{"rescan":true}` rescans the folder after the user adds files.

**Scenes** (Macros, Turbo 0.50): a radio-button scene launcher for live sets. Rows are scenes
(`scenes`, 1-8, default 4) plus an off row on top (the base state); columns are outputs (`outputs`,
1-8), each cabled to a param: `modulate {"index":DST,"param":"mute","srcIndex":SCENES,"srcOutput":2}`
(outputs numbered from 0 or named by their label). Pressing a scene enters its row: every output
follows that row, so whatever is ON in the old scene and OFF in the new one switches off by itself.
Pressing the playing scene again goes to the off row (`press_again_off`, default true; false =
restart). Output modes: `on/off` (default, 1 or 0: mutes, toggles, enable switches), `choice` (a
stepped value; steps `auto` = the cabled param's options, e.g. a Note Switcher slot or a drum
`part`, or 2..128), `level` (0..1), `pulse` (a short trigger when a scene whose cell is ON starts:
wire to a restart button or `B bridge`). Set up with `scenes {"index":N,"names":["intro","verse"],
"outs":[{"label":"lead","mode":"on/off"},{"label":"part","mode":"choice"},{"label":"rst",
"mode":"pulse"}],"grid":[["on","A",1],["off","B",1]],"off":["off","A",0]}` (cells: 0/1 or
"on"/"off", a 0-based choice or its dropdown name, a level; `cells [[scene,output,value]]`, scene -1 =
off row; `state` lists each output's target, choices and live values). Actions: `go` (scene, -1 =
off), `press` (scene, like the button), `off`, `next`, `prev`, `cancel`. `quantize` (0 immediate,
1 next beat, 2 next bar, 3 2 bars, 4 4 bars) makes changes, off included, wait for the grid while the
transport plays. Mappable: triggers `scene 1`..`scene 8`, `all off`, `prev scene`, `next scene`,
selector `scene` (0 = off). `perf_add {"index":N,"param":"scene 2","label":"verse"}` gives one
Performance trigger per scene; it lights while its scene plays (a toggle element works too). A cabled
output owns its param (the knob follows the scene), so cable only what scenes should change. A
Performance control can also drive several params directly: `perf_add {"index":DST,"param":"<name>",
"element":E}` adds a destination to control E (same value).

**Chord Progression qualities** (the builder dropdown): triads, 6ths / add9 / 6/9, 7ths (incl.
7#5, 7b5, maj7#5, dim(maj7)), 9ths / 11ths / 13ths (11, m11, maj7#11, maj9#11, 13, m13, maj13,
13sus4, 9#11, m(maj9)), altered dominants (7b9, 7#9, 7#11, 7b13, 7alt, m7b9) and voicings
(quartal, so what). From the tools, write each chord as `mask<N>`: bit k = key k counted up from
C of `baseOctave`, two octaves (bits 0-23), e.g. C13 = keys 0,4,7,10,14,21. Chord names follow.
Chord names spell the quality out (the UI is all caps): "C MAJ", "A MIN", "A MIN7", "C MAJ7",
"B DIM", "C SUS4"; a bare number is dominant or plain ("C7", "C6", "C9", "C5"). Minor qualities
read "min..." in the builder (min7, min9, min(maj7)...). Live restart (0.50): trigger `restart`
starts the progression again from chord 1 at the next grid line set by `restartQuant` (0
immediate, 1 next beat, 2 next bar = default, 3 next 2 bars, 4 next 4 bars); `perf_add
{"index":N,"param":"restart"}` adds it to a Performance page, a modulator's rising edge on the
`restart` pin fires it too. Note Sequencer, Arpeggiator (pattern and gate grid from step 1) and MIDI
File (file from the top, even after it ended) have the same `restart` trigger and
`restartQuant` setting.

**Note Switcher** (0.50): 8 note inputs (slots 0-7). `inputs` (2-8, default 4) sets how many
pins show; set it before connecting to a slot past it. `switchQuant` (0 immediate = default, 1
next beat, 2 next bar, 3 next 2 bars, 4 next 4 bars) makes a manual slot change wait for that
grid line. Triggers `slot 1` .. `slot 8` pick a slot (and turn `manual` on), good Performance
Mode buttons. Notes held on the old slot still release normally.

**MIDI File** (Notes): plays a Standard MIDI File (.mid, format 0/1) as notes, locked to the
transport in beats, so it follows the app tempo (not the file's). Settings: `path` (the file;
setting it loads it), `track` (0 all, N = track N), `channel` (0 all, 1-16), `transpose`
(semitones), `velocity` (scale 0-2), `loop`, `loopBars` (0 = file length rounded up to bars),
`quantize` (start grid as a rate division, 2 = 1 bar default; playback starts, restarts and
loops on it), `play`. Wire its note output into a synth. The body shows the file's tempo with a
button that copies it to the transport; dropping a .mid on the canvas spawns one.

**MIDI Notes** (Notes): live MIDI input as notes. `channel` is stored 0-based: -1 omni, 0-15 =
MIDI channel 1-16 (the body shows "ch 1".."ch 16" and the last channel heard). Use one node per
channel to split a controller's keys and pads. MIDI CC / MIDI Trigger `channel` is also 0-15.

**0.49 notes**: Random Note `style` (0 walk = classic, 1 melodic with accents); Sampler has 16
voices; Image to Points / Depth Projection `relativePointSize` (true = point size 1 fills a cell);
an Arrangement clip of a Sampler or Analog can carry its own pitch (clip settings).

**Other Turbo nodes**: OSC to CV (8 OSC addresses to 8 CV outputs), Plugin (VST3 instruments and
effects; settings `plugin_id` etc., the plugin state is saved with the patch), Chord Progression
(sets the global key), Predictive Notes / Quantize / Velocity / Rhythm, Macro controls (Knob,
Slider, Toggle, Trigger, Radio Selector, Step Gate, XY) which are also good MIDI-learn targets.

**Not reachable from these tools** (tell the user where to do it by hand): the Arrangement
Timeline (Shift+T), gesture recording (Shift + drag a knob), MIDI learn (Ctrl+M), the
Performance panel's MIDI learn (right-click a control).

**The bundled example** `superSynthMCP` (FILE / Settings > Startup) is a good reference: two
Analog synths fed by Note Sequencers through Note Switchers, Delay / Mixer / Reverb to an Audio
Out (it predates the Super Mixer; new patches mix in a Super Mixer), an Audio Analyze driving the visuals (Noise, kaleidoscope, Blend, glitch, bloom), and a
Performance page with chords, delay, filters, autoplay and reverb. `explain` it to learn a working
layout.
)SKILL"
R"SKILL(
**Chord Progression editing (0.51)**: `chord_progression {"index":N,"action":"set_slots",
"slots":["Cmaj7(9,11)/E","Am7",{"chord":"F","bars":"3b"},{"notes":["C3","E3","G3","B3"],"bars":2}]}`
replaces the progression (1-16 slots); `get`, `insert` (at, slot), `duplicate` / `delete` (slot) and
`move` (from, to) edit it. Symbols take a root, a quality ("m7", "maj7", "7sus4", "6/9", "m7b5", "dim7"...),
extensions in parentheses ("(9,11)", "(b9,#11)", "(no5)") and a slash bass ("/E"). `bars` takes 2, 1.5,
"3b" (beats), "2:2" (bars:beats); the minimum is 1/16 bar. Keys now span four octaves (extra save keys
`maskHi0`..`maskHi15` = keys 24-47, `slashBass0`..`slashBass15` = pitch class or -1); `mask<N>` keeps keys 0-23.
Names of chords outside the quality table come from interval analysis ("C MAJ7(9,11)", "G7(b9,#11)", "/E").

## Glitch and Datamosh (Turbo 0.51)

`glitch` (Effects) has ten kinds: Slice Shift, RGB Shift, Scanlines, Blocks, Wave, Datamosh, Scan Jitter, VHS,
Compression, Pixel Sort. The params after Seed (Size Var, Splits, Contrast, Density, Stagger, Burst, Decay, Sync,
RGB Var, Color FX, Axis, Clock) default to the old look, so only set them to change it. Sync (1 bar to 1/32)
locks steps to the transport beat; Clock = Free-run keeps it moving while the transport is stopped.
The `Datamosh` node (Effects) is the real codec-style effect: inputs `image` and optional `motion`; params
mosh, gain, bloom, threshold, block, blockVar, leak, refresh, refreshChance, seed.
)SKILL"
R"SKILL(
## GLSL into the Formula node: Shadertoy and friends (Turbo 0.56)

The `Formula` node (Source) runs GLSL. It picks its mode by itself:
- **Shadertoy mode**: the code defines `void mainImage(out vec4 fragColor, in vec2 fragCoord)`. It
  compiles as a whole file (`#version 330`, 150 on old contexts) with Shadertoy's uniforms: `iTime`
  (transport seconds), `iResolution` (vec3), `iFrame` (frames since compile / resize), `iTimeDelta`,
  `iFrameRate`, `iMouse` (from the params mouseX / mouseY 0..1 and mouseDown), `iDate`, `iSampleRate`,
  `iChannel0..3` (= image inputs in0..in3), `iChannelResolution[4]`, `iChannelTime[4]`. `texture2D` is
  defined as `texture`. Output alpha is forced to 1 unless `floatBuffer` is on.
- **native mode**: anything else is the body of `vec4 shape(vec2 uv, vec2 p, float t)` (uv 0..1,
  p centred and aspect-corrected, t seconds) and must `return` a vec4. Helpers in scope: PI, TAU,
  rand(vec2), rot(a), sdCircle, sdBox; images `texture(uIn0, uv)`..uIn3 with uHasIn0..3, uInRes0..3.

Both modes: knobs `uA`..`uD` (0..1, settings knobA..knobD), and **extra knobs from the code**: each
line `uniform float NAME; // lo..hi = default` becomes a knob with a CV pin (max 16, saved as
`uniform_NAME`; range and default optional, 0..1 and the middle). Do not declare uA-uD, uTime or
any i* uniform yourself (those lines are dropped).

**Workflow**: `create_node {"type":"Formula","settings":{"width":960,"height":540,"formula":"<code>"}}`
(or `set_param` key `formula` on an existing one; it recompiles at once). Then `explain` the node:
its `shader` block gives `mode`, `error` (the GLSL log; line numbers match the user's code in
Shadertoy mode) and `uniform_knobs`. Fix and set again until `error` is empty, then
`screenshot_node` to look. Keep 960x540 or less for heavy raymarchers and say so; scale up with Fit.

**Converting a Shadertoy, tab by tab**
- **Image only**: paste it unchanged.
- **Common tab**: put its code above the Image code in the same formula.
- **Buffer A..D**: one Formula per buffer, chained like the Shadertoy channel setup: the buffer's
  output cable goes into the next pass's inN matching its iChannelN. Buffers set `floatBuffer` true
  (RGBA32F, alpha kept: Shadertoy buffers are float and often store state in alpha). A buffer that reads
  itself (iChannelN = Buffer A inside Buffer A) sets `selfChannel` to N: that channel gets the node's own
  previous frame (cleared to 0 on compile / resize, iFrame restarts at 0). Two buffers that read each
  other: route one of them through a `Feedback` node.
- **Textures** (noise, organic, font, etc.): Shadertoy's own images are not bundled; feed an Image
  Source with a similar picture, or a `Noise` / `Texture` node, into the matching in0..in3. Tell the user.
- **Video / webcam channels**: Video, VMPC or Video In into inN.
- **Sound / microphone / music channels** (texture with FFT in row 0, waveform in row 1): build it from
  audio with `Audio Texture` (AudioVisual) into inN; check its rows with describe and adapt the
  texture() reads if needed.
- **Keyboard channel**: not available; replace the key reads with uniform knobs (map them to MIDI or
  Macro buttons) and explain the change.
- **Cubemap channels / cubemap passes, 3D volume textures, VR mainVR, sound shaders (mainSound)**:
  not supported. Say so; offer a 2D rewrite (e.g. an equirect texture read with a direction-to-uv
  function instead of a samplerCube).
- **iMouse**: mouseX / mouseY / mouseDown params (CV-able: map them to a Macro XY or MIDI).

**GLSL ES to desktop GLSL gotchas** (fix while converting): `precision` lines are fine; avoid
`const` arrays built from non-constant expressions; global variables initialised from uniforms
(`vec2 R = iResolution.xy;` at file scope) must move into functions or become `#define R iResolution.xy`;
integer-float mixing needs explicit casts on strict drivers (`float(i)`, `2.0` not `2`); `texelFetch`
needs `ivec2`; loops with non-constant bounds are fine. Rename user identifiers that clash with
the node's own (`uA`..`uD`, `uTime`, `uAspect`, `uIn0`...), and in native mode avoid redefining
`rand`, `rot`, `sdCircle`, `sdBox`.

**Other formats**: GLSL Sandbox / glslsandbox / The Book of Shaders (`uniform float time;
uniform vec2 resolution; uniform vec2 mouse; void main(){ gl_FragColor = ... }`): wrap as
`mainImage`: rename `time` -> iTime, `resolution` -> iResolution.xy, `mouse` -> iMouse.xy/iResolution.xy,
`gl_FragCoord` -> fragCoord, `gl_FragColor` -> fragColor, drop the uniform lines and `main`.
**ISF** (VDMX / Resolume / Magic: JSON header + main): drop the JSON, map `TIME` -> iTime,
`RENDERSIZE` -> iResolution.xy, `isf_FragNormCoord` -> fragCoord/iResolution.xy, `IMG_NORM_PIXEL(img, uv)`
-> texture(iChannelN, uv), `IMG_PIXEL` -> texelFetch, each INPUT float -> `uniform float name; // min..max = default`,
image INPUTs -> iChannel0..3, PASSES -> chained Formulas as Buffers. **TouchDesigner GLSL TOP**:
`sTD2DInputs[n]` -> iChannelN, `vUV.st` -> fragCoord/iResolution.xy, `uTD2DInfos[n].res.zw` ->
iChannelResolution[n].xy, `fragColor = TDOutputSwizzle(c)` -> `fragColor = c`, custom uniforms -> uniform
knob lines. **Vulkan / HLSL / Metal**: rewrite by hand into GLSL (float2 -> vec2, lerp -> mix,
frac -> fract, mul(a,b) -> a*b with care for order, saturate(x) -> clamp(x,0.,1.)).

**Licences**: Shadertoy code is CC BY-NC-SA 3.0 unless the author says otherwise, and some
authors forbid any reuse. Check the header. Never put third-party shaders into the Infinite-Turbo
repository or a release; keep them in the user's own patches, and keep the author's credit line.
)SKILL"
R"SKILL(
## Circuit Synth: a circuit simulated as an instrument (Turbo 0.73)

`Circuit Synth` (Synths) makes its sound by simulating a small electrical circuit. Notes in (slot 0),
stereo audio out: MIDI Notes / Keyboard / Note Sequencer -> Circuit Synth -> a Super Mixer channel ->
Audio Out. Its panel draws the circuit's schematic.

- `preset` picks the circuit by index: 0 Diode Clipper, 1 Fuzz, 2 RC Lowpass, 3 RLC Lowpass, 4 Series LC,
  5 Tank Filter, 6 Twin-T Notch, 7 LC Ladder, 8 LC Tank, 9 Thump, 10 Coupled Tanks, 11 Bell, 12 LC Line,
  13 LC Mesh, 14 Van der Pol, 15 Pushed VdP, 16 Neon Relax, 17 Chua, 18 BT Kick, 19 BT Tom, 20 Rim,
  21 Buzzer, 22 Ringer.
- Setting `preset` from a tool changes only the circuit, not its play settings (the panel's menu sets
  those). Set them with it: the struck circuits 8-13 and 18-20 want `trigger` 0 (tap) and `wave` 4
  (step); 14, 16 and 17 `wave` 4 with `trigger` 1 (hold); 21 and 22 `wave` 1 (pulse); the rest `wave` 0
  (saw) and `trigger` 1. Put `k0`..`k7` back to 0 when you switch circuit.
- `k0`..`k7`: one knob per part (or group of parts) of the current circuit, -1..1 = 1/8 .. 8 times the
  drawn value (R, C, L, volts; a diode's knob moves its clipping level 0.5..2x). Their drawn names (for
  modulate, set_expression, perf_add) are the part names (`R`, `C`, `D`, `L line`, `R decay`...) and
  change with the circuit: `explain` the node after setting `preset`.
- Play: `mode` 0 mono / 1 poly, `trigger` 0 tap (touch the source and let the circuit ring) / 1 hold,
  `wave` 0 saw 1 pulse 2 sine 3 noise 4 step, `cFollow` / `lFollow` (C and L follow the key), `pitch`
  (semitones), `glide` (s), `contact` (ms, how soft the source connects), `attack` / `release` (s).
- LFO: `lfoRate` (Hz), `lfoDepth` (0..1, 0 = off), `lfoShape` 0 sine 1 triangle 2 square 3 random,
  `lfoTarget` 0 tune 1 power 2 damp 3 level 4 cutoff.
- Output filter, the synths' usual list: `filter` 0 off, 1-3 lp 12/24/36, 4-6 hp 12/24/36, 7-8 bp
  12/24, 9-10 notch 12/24, 11-12 comb +/-; `cutoff` (Hz), `reso` (0..1), `filterKey` (0..1, the cutoff
  follows the last note).
- Stereo: `probeA` / `probeB` (where the left and right sides listen; each circuit has 2-4 probe points,
  `explain` names them), `width` (0 mono .. 1.5), `spread` (0..5 %, part tolerance between the two
  circuits each voice runs), `level`.
)SKILL"
R"SKILL(
## PointZ nodes: GPU point operators (Turbo 0.58)

The `PointZ` category is Infinite-Turbo's take on TouchDesigner's POP family: geometry as points with
named attributes, processed on the GPU. Names end in ` PointZ` (`Noise PointZ`, `Transform PointZ`,
`Particle PointZ`...); `describe` lists them all and every setting key. `docs/POINTZ-REFERENCE.md` in the
repository has the full table.

- **Attributes**: P (position), N (normal), Color (rgba), Tex (uv), PScale (point / instance size),
  Rotate (degrees), Active (0 hides an instance), Part* (particles: PartVel, PartAge, PartLifeSpan,
  PartForce, PartMass, PartId), plus any name an operator writes (Group, Weight, Phase, ArcU...).
  Most operators take an `attrib` setting naming the attribute they work on, and a `mask` naming a
  0..1 attribute that limits them per point (from Field PointZ or Group PointZ).
- **Mixing with the rest of 3D**: any 3D source plugs into a PointZ input (Geometry, Model 3D, Particle
  System... are converted, with their placement). A PointZ plugs into Render 3D directly (or through
  Material / Mapping / Null 3D / Group 3D), drawn straight from the GPU: faces as surfaces, lines as
  lines, bare points as sprites. Every other 3D node can read a PointZ too.
- **Instancing (the Geometry COMP idea)**: `Instance PointZ`, points on input 0, the shape on input 1,
  into Render 3D. One draw call for every point; each instance reads P (translate), N (aim, setting
  `align`), PScale (uniform size), Scale (vec3, size per axis), Rotate (degrees), Color and Active; the
  `*Attr` settings rename any of them, and `scaleXYZ` / `rotOffset` / `offset` add to all instances.
  Per-instance textures: up to four images on the `texture 0..3` pins with `texMode` 1 (pick image by
  index), or `texMode` 2 (atlas grid, `atlasCols` x `atlasRows` cells of texture 0, or of the Material's
  texture when no pin is connected); the index comes from `texFrom` (0 the TexIndex attribute, 1 the
  instance number, 2 random). This is the fast way to draw many copies: prefer it to Copy PointZ, which
  builds real geometry on the CPU. Example: `Grid PointZ` (kind 2 points) -> `Noise PointZ` (attrib
  PScale, mode 1 set) -> `Instance PointZ` with `Box PointZ` as the shape -> Material -> Render 3D.
- **Noise PointZ**: types 0 gradient, 1 fractal, 2 curl, 3 ridged, 4 turbulence, 5 cells, 6 value,
  7 random; `use4d` (on by default) moves time and `t4d` through a fourth dimension so the noise
  evolves in place; `oct` / `spread` / `rough` shape the harmonics, `expo` sharpens, `bias` offsets.
- **Loops**: `Particle PointZ` and `Feedback PointZ` read last frame's result from a `Null PointZ` whose `tag`
  matches their `target`. Particle chain: source points -> Particle PointZ (target "loop") -> Force
  Radial PointZ / Noise PointZ on PartForce -> Null PointZ (tag "loop") -> Render 3D. Cache Select / Cache
  Blend read a Cache PointZ by its `tag`.
- **Images in and out**: `Image to PointZ` (an image as coloured points / a relief), `Lookup Texture PointZ`
  (colour or displace points from an image), `Signal to PointZ` (a row of an image as a line: feed it an
  Audio Texture for a live waveform). Render 3D's `renderPass` gives depth / normals / position for
  2D effects.
- **Your own GLSL**: `GLSL PointZ` (set `outputs` to the attributes you change, write the body in `code`:
  every attribute is a variable of its name, plus id, uCount, uTime, uBeat, uA..uD, gnoise / fbm /
  curl / hash3i / rotX / eulerDeg helpers), `GLSL Create PointZ` (points from nothing), `GLSL Copy PointZ`
  (copyId per copy), `GLSL Advanced PointZ` (fetch_<name>(i) reads input B). After setting code,
  `explain` the node: a compile error shows as its warning.
- **Files**: Point File In (PLY, xyz, csv), File In (OBJ, PLY with faces), File Out (PLY, OBJ, CSV;
  setting `write` true writes once), Table to PointZ (a typed table).
- Cost: kernels (Noise, Transform, Math, Field...), Instance PointZ and Trail PointZ (matched by index)
  stay on the GPU; topology operators (Merge, Copy, Delete, Extrude, Subdivide, the Line PointZ nodes,
  Particle) work on a CPU copy, fine for tens or hundreds of thousands of points, and a CPU operator after
  a GPU one reads the data back. The node shows points, faces, lines and its cook time.
- Render 3D viewport: left drag orbits, middle drag (or Shift + left) pans, right drag dollies, wheel zooms.
)SKILL";
}

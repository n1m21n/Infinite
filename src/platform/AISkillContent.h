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
- **3D** is built from geometry nodes (Geometry, Model 3D, Text 3D, Particle System ...) wired into
  a **Render 3D** (geometry slots, then camera, lights, environment), whose image goes on to effects
  and an Output.
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
| Image sources | Shape, Noise, Ramp, Texture, Image Source, Video, Video In (camera), Text, Draw, Slideshow, Spout In |
| Effects | the filter family (gaussianblur and many more: describe lists them), Blend, Layer Stack, Layout, Fit, Color Ramp, Curves, Feedback, Trails, Reaction Diffusion |
| Output | Output (a window / projector), Projection (warp + edge blend), Spout Out |
| 3D | Geometry, Model 3D, Text 3D, Material, Instance on Points, Particle System, Camera, Light, HDRI, Render 3D |
| Modulators | LFO, Envelope, Random, Drift, Pattern, Math, Smoothing, Range to Range, Audio Analyze (audio to CV), Image Analyze, MIDI CC, Macro Knob / Slider / XY |
| Sound | Oscillator, Analog, Wavetable, Sampler, MPC, Granular, Slicer, Audio File, Audio In, Plugin (VST3), Mixer, Super Mixer, Looper, Audio Out |
| Notes | Keyboard, Note Sequencer, Drum Sequencer, Arpeggiator, Chord Progression, MIDI File, Quantizer, MIDI Notes |
| Performance | Clip Matrix (+ Clip Matrix Out), Transport Control, Timeline |

)SKILL"
R"SKILL(## Recipes

**A moving shape, blurred, on screen**: Shape -> gaussianblur -> Output; LFO, then
`modulate` the blur radius (or the shape's rotation) from the LFO.

**Audio-reactive visuals**: Audio In (or any sound source) -> Audio Analyze; `modulate` a visual
param (scale, brightness, blur) from one of its outputs (`describe Audio Analyze` lists bands and
level). Keep the sound itself routed to an Audio Out if it should be heard.

**Video with effects**: Video (`set_param path` to a file the user gives you) -> effects -> Output.
The Video's audio output goes to an Audio Out to hear its soundtrack.

**A 3D scene**: Geometry (or Text 3D) -> Material -> Render 3D (slot 0), Camera into its camera
slot, a Light into a light slot; Render 3D -> Output.

**A synth line**: Note Sequencer (or Keyboard) -> Oscillator / Analog (note input) -> Audio Out.
`transport {"play": true, "audio": true}` starts the clock and the audio engine.

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
Out, an Audio Analyze driving the visuals (Noise, kaleidoscope, Blend, glitch, bloom), and a
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
)SKILL";
}

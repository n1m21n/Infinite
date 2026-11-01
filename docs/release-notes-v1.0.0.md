## What's new in v1.0.0

v1.0.0 adds the panels around the node canvas: a 3D View, a Terminal with an `infinite` command, a Sample Studio and a Piano Roll. It adds five node types (342 to 347), a rebuilt template library, a vertical Settings list with new Plugins and Terminal pages, fuller translations and a Windows VST3 scan fix for issue #44. Session, Timeline, the Mod Matrix and the Performance panel are carried over from 0.6.6 with small changes (the Timeline gains MIDI lanes and shared tool keys).

Since v0.6.6 there are 667 commits on main (638 without merge commits). 183 of them touch the app itself (source, resources, assets, fonts); the rest are the website, blog, manuals, tools and research.

### 3D View (Shift + 3)

- A final-stage composer for 3D patches. Right-click a 3D node and choose Add to 3D View, then arrange node outputs in one scene
- Cameras and lights are the existing Camera and Light nodes. Each object has its own list of lights it ignores
- The world is none (transparent), a colour with a strength, or an HDRI. The Surface list offers "None, transparent", "Background colour" and "Environment texture"
- Look through a camera, with a real f-stop for depth of field, ambient occlusion, bloom, composition guides and safe areas; keys and setting names follow Blender's
- Render Image gives a still. Render Animation renders the camera for a number of seconds at a chosen frame rate
- Scenes stay off the canvas until you choose Convert to node. A scene can be duplicated, copied, pasted and deleted, and items can be grouped
- The Shift + A menu adds a node, camera, light, HDRI or new scene to the view
- Arranging never writes back to the node; geometry, materials and simulation stay in the node chain. The panel composes, it does not model
- Fixes found while building it: shadow contact, glass highlights, area light lines and world reflections in Render 3D, and AO striping on sloped floors

### Terminal and the `infinite` command (Ctrl + `)

- A real terminal in the right rail, running your own shell. It can also dock on the other three sides. On Windows it runs on ConPTY
- Quitting while a program runs in it asks first; an update restart warns the same way
- Inside it the `infinite` command talks to the running app. Typing `infinite` lists every verb. Groups: look (`graph`, `params`, `selection`), edit (`add`, `connect`, `set`, `mod`, `expr`, `undo`), play (`play`, `tempo`, `timeline`, `session`), see and hear (`shot`, `frame`, `render`, `audio`), `panel` and `settings` for any panel or setting, `term` for terminal tabs
- `infinite code new | check | watch | docs | examples` writes and checks Field, Sketch, Sketch 3D, GLSL and formula code against the real compilers. `infinite patch new | check | layout | apply` builds whole patches
- Verbs that take a `.inf` file run without the app. `infinite context` prints a short brief of the open patch; `infinite guide` prints the patch authoring reference
- No coding agent ships with Infinite. The Terminal runs the tools you already have
- The Terminal follows the light theme. Over the 3D View, Ctrl + ` toggles the 3D View overlays instead of the Terminal

### Sample Studio (Shift + W)

- A self-contained sample editor. Open a file, drop one in, record from an audio input (R in the panel, only when an input device is connected), or generate a take
- Zoom the waveform and cut it into slices by transients, by grid or into equal parts
- Each slice has gain, pan, pitch, fine tune, stretch, length, fades, transient and decay shaping, compressor, filter, reverse, colour, mute and solo
- Editing stays inside the Studio and never changes the canvas. Convert puts the result on the canvas as a Generate Samples, Sampler, Slicer, Molder, Grain Molder, PaulStretch or Granular node. For a generated take, Convert also offers the patch that made it
- Export writes the whole edit as WAV (16 or 24 bit), FLAC or MP3, or each slice to a folder
- Studio work is saved in the patch
- Fixed: the wave past the end marker no longer draws as a solid block

### Piano Roll (Shift + R)

- A note editor for MIDI lanes on the Timeline, laid out in pages and clips. Draw, erase, cut, mute, select, quantise (with strength and swing), transpose and nudge
- Scale snap colours the in-scale rows. Note functions: Humanize, Strum, Legato, Chop to Grid, Velocity Ramp, Reverse, Invert, Half Speed and Double Speed
- Record from a MIDI input (R), import a `.mid` file into a lane, or press G to Generate notes
- Lanes, clips and notes are saved in the patch
- The new Piano Roll node plays its linked MIDI lane

### New nodes (342 to 347)

- **Piano**: felt hammer, stiff-string waveguides, unisons, dampers and a soundboard, with Grand, Rhodes and Wurlitzer models. 8 factory presets
- **Wind**: a waveguide wind instrument with clarinet, sax, flute and brass. 9 factory presets
- **Generate Samples**: a sample player whose sample is made, not loaded. Choose a part, press generate, and a loop of 1, 2, 4 or 8 bars is rendered from Infinite's own instruments. The node offers 18 parts: synths, keys, piano, leads, strings, winds, pads, plucks, textures, bass, drums, kicks, snares, hats, toms, percussion, fx and foley. It saves as a recipe, not audio, so a newer version of Infinite may render a slightly different loop when a patch is reopened
- **Piano Roll**: plays a MIDI lane from the Piano Roll panel
- **3D View**: the scene node behind the 3D View panel (image output)
- **String** (existing node): fitted body types (guitar, harp, violin, cello), pick hardness, bow pressure and bow noise; 8 factory presets. Defaults are unchanged

### Template library

- File > Template library now has two groups, Showcase and Starters
- Starters are 15 small patches of 3 to 6 nodes each (plus comment notes), one idea each, with thumbnails taken from the node manual
- Showcase is one new patch, "Infinite v1 showcase" (drums, pads, 3D and a Performance page), listed first

### Settings

- The section list is now vertical: Appearance, Canvas & Workspace, Audio, General & Performance, Field Language, Expression Globals, Extensions, Plugins, Terminal
- Plugins: scan controls and the list of scanned plugins in one place
- Terminal: how to open the panel, the `infinite` command, and AI skill files. Install All AI Skills saves the patch authoring, Field Language and Expression Globals reference files to a folder you pick
- Learning: "Learn from my playing" is on by default. Kept takes, notes you play and saved patches are logged on your computer for the generator. Switch it off to stop all logging, open the folder, or press Forget me (two clicks) to delete everything learned
- The Field Language and Expression Globals pages match the compiler's function lists

### Shortcuts

- New panel keys: Shift + 3 (3D View), Shift + W (Sample Studio), Shift + R (Piano Roll), Ctrl + ` (Terminal). Shift + S, T, M, P and U keep their 0.6.6 meaning
- The shortcuts window lists every key in one card per section
- Where keys clash, the panel wins while it has the pointer or focus: canvas keys give way to the 3D View, and Sample Studio single-letter keys fire only when no modifier is held. Canvas Shift + A gives way to the Performance Matrix

### Languages

- Every missing UI string is filled in Spanish, German, Chinese, Japanese and Russian (584 strings each)
- The bundled Noto Sans SC and JP subsets were rebuilt from the full release; the commit records no missing Chinese or Japanese glyphs

### Windows

- VST3 scanning (issue #44): plugin paths stay UTF-8 through the folder walk, the scanner's arguments and `LoadLibrary`. A plugin path with a character the ANSI code page lacks used to throw on the scan thread and close the app in both scan modes
- An exception on the scan thread no longer ends the app; the scan keeps what it found. A dead scanner's marker file is cleared so the single-plugin retry can run. Scan progress, the scanner's exit code and blocklisting are written to log.txt
- Scanning within Infinite no longer unloads plugins
- These fixes have not been run on Windows hardware by the author (see Known limitations). If a plugin is still blocklisted, please open an issue at https://github.com/n1m21n/Infinite/issues with the plugin name, vendor, version, your Windows version and the scanner's exit code (`infinite-vst3-scanner.exe --batch "<path to the .vst3>"`, then `$LASTEXITCODE` in PowerShell)

### Manuals, docs and packaging

- The Node Reference Manual, Field Language Manual and Headless User Guide are rebuilt. The Node manual's Using the App chapter, and its Performance and Timeline pages, are reshot at the v1 layout. The Headless guide gains a section on the `infinite` command
- Homebrew: `brew install --cask n1m21n/tap/infinite` (added after 0.6.6, with a workflow that updates the package manifests on release)
- CI can run a single platform on a manual run, and a workflow copies the Windows zips and the AppImage onto a release
- The Terminal vendors libvterm and Geist Mono
- Headless runs skip the canvas swap, the frame limiter and the media indexes, and can report plugin parameters and saved plugin state

### Known limitations

- Platforms: only macOS has been built and verified by the author for 1.0.0. Windows and Linux builds are not confirmed until CI is green on both for the release commit. That includes the Windows ConPTY terminal and the #44 scan fix
- The Showcase template opens with an empty Sample Studio. The Studio's own `src path` is not resolved relative to the patch, so `media/studio-take.wav` is not found. The showcase's drum lanes load correctly
- The template group names Starters and Showcase are not translated; the other UI text is
- The Node Reference Manual has no pages yet for Piano, Wind, Generate Samples or the 3D View node
- Piano, Wind and String did not reach their family-test bars in the author's measurements (commits 99c49353, 40c09604, d0b6cda2); String's new bodies are options and its defaults are unchanged. Treat them as new instruments, not sampled-instrument replacements
- The 3D View composes node outputs. It has no modelling tools, and an image node cannot be used as the world (colour or HDRI only)
- The Sample Studio is its own editor: its edits do not change the canvas until you Convert, and modulation applies only after that
- Reopening a patch need not give identical results across versions (Generate Samples loops are re-rendered from their recipe); patches are expected to open, play and show a picture
- The Terminal ships no coding agent

---

Audit notes (not part of the published notes; delete this section before publishing). Checked against main at 493e682f, range v0.6.6..main. Read-only; nothing was built or run.

| Statement | Evidence |
|---|---|
| 667 commits (638 non-merge, 371 first-parent) | `git rev-list --count v0.6.6..main`, with `--no-merges`, with `--first-parent`. The README says "about 600" |
| 183 commits touch the app | `git log --no-merges v0.6.6..main -- src resources assets external` |
| 342 to 347 node types | the generated "N node types" line in `docs/ai-skills/infinite-patch-authoring.md` at v0.6.6 (342) and main (347). New types: 3D View (d17cb6bb), Generate Samples (3df4730e), Piano (49a9deba), Piano Roll (3c55e136), Wind (c527b778) |
| Factory preset counts 8, 8, 9 | message of the "String, Piano, Wind: factory presets (8, 8, 9)" commit, in that order |
| Starters have 3 to 6 nodes | non-Comment `node` lines in `assets/templates/*.inf` |
| #44 fix | cc104ae8 message and diff stat; not run on Windows |
| Settings sections, Learning default on | `SettingsWindow.cpp` kTabs and Learning card; `LearningLog.cpp` `gEnabled = true` |
| Glyph claim | 8780832f message only; the glyph count was not re-measured |
| Keys | the shortcut table in `HelpWindows.cpp` (lines 640 to 780) |
| Not read in this audit | Terminal pty code on macOS and Linux (the notes do not claim it), CI status |

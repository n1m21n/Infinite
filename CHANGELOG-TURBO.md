# Infinite-Turbo (for Windows) - changelog

## Unreleased (feature/timeline-align)

Timeline aligned with upstream before new timeline features (plan: `docs/plans/TIMELINE-ANALYSIS.md`).

- **Model and patch format = upstream's.** `ArrangeModel` taken verbatim from upstream main
  (per-clip speed / reverse, Session view slots and scenes). Patches read and write `clipspeed`,
  `slotclip`, `sessionslot`, `scene`, `scenemeta`, `sessionmeta` and legacy seconds `clip` lines
  like upstream, so a patch saved by either build keeps all of it.
- **Fix: timeline audio played one audio block early.** The mix read the block's end beat;
  it now reads the block's start (`Transport::BlockStartBeats`, upstream's).
- **Fix: undo could hand out a clip id again.** Undo no longer rewinds the id counter.
- **Every audio clip is position-locked** (upstream rule), not only dropped samples; a clip's
  speed reaches the file player. Reverse is saved and loaded but not yet played backwards.
- **New: record to timeline.** R on an audio track arms it and picks what it records (Audio In, a
  synth played by hand, a Mixer, any audio node). REC in the timeline toolbar starts the transport
  and records every armed track; stop or pause ends the take, which lands on its track as an audio
  clip (its own Audio File node, WAV in the recordings folder) at the beat it was played, replacing
  what was under it. An armed track is heard while recording or stopped (auto monitoring). Takes
  are linear: the loop is off while recording. Before this, the timeline only gated live nodes, so
  parts played by hand were gone after a rewind.
- **Self-tests:** `INFINITE_ARRANGETEST`, `INFINITE_ARRANGEAUDIOTEST` (ported from upstream),
  `INFINITE_ARRANGEMULTITRACKTEST` (three tracks side by side, live and in a WAV render) and
  `INFINITE_ARRANGERECORDTEST` (record an Oscillator, delete it, the take still plays at the same
  level), in the gate.

## Unreleased (feature/circuit-synth)

- **New: Circuit Synth** (Synths, both editions, MIT clean room). Makes its sound by simulating a
  small electrical circuit in real time: Modified Nodal Analysis with trapezoidal companions, Newton
  on the nonlinear parts (diode, neon lamp with hysteresis, Chua's diode, cubic negative resistance,
  saturating op-amp), oversampled 2x / 4x. 23 circuits: Diode Clipper, Fuzz, RC / RLC Lowpass,
  Series LC, Tank Filter, Twin-T Notch, LC Ladder, LC Tank, Thump, Coupled Tanks, Bell, LC Line, LC
  Mesh, Van der Pol, Pushed VdP, Neon Relax, Chua, BT Kick / BT Tom / Rim (808-style bridged-T),
  Buzzer, Ringer. One knob per part, notes from the notes pin (mono / poly, tap / hold, C and L key
  tracking, pitch, glide, contact, attack, release), an LFO on tune / power / damp / level. Stereo is
  designed in: twin circuits with part tolerance (spread), two probe points, mid/side width, poly
  voices panned; the default circuit measures L/R correlation 0.8 with the mono sum within 0.5 dB.
  The panel draws the circuit's schematic: every part labelled with its knob, the parts a knob
  moves light up while it is hovered, A / B show where each side listens. An output filter with the
  synths' usual types (lp / hp / bp / notch / comb) with cutoff, reso and key tracking.
  Self-test `INFINITE_CIRCUITSYNTHTEST` (renders a WAV per circuit to `build\circuit-synth-wavs`).
  Docs: `docs/CIRCUIT-SYNTH.md`.

## 0.72.0-turbo (2026-10-24)

Real instrument firmware inside the patch. Full build only (GPL). Example:
`synthref/examples/SynthREF_SMK37_MIDI_Notes.inf` (MIDI Notes -> Synth REF -> Audio Out).

- **New: Synth REF** (Synths). Runs an instrument's own open firmware natively, with its panel in the
  node. First instrument: M-VAVE SMK-37 Pro with M-VAVE's SMK37 OPEN V01 firmware, the whole
  application compiled unmodified (DX7 FM with 128 voices, 303, 808, PSG, pluck and sample engines,
  8 tracks, sequencer, factory projects). Its audio is bit-exact with M-VAVE's own browser simulator.
  FULL PANEL draws the firmware's screen and every control (buttons with LEDs, encoders, faders,
  wheels, 16 pads, 37 keys); COMPACT is a normal card with a small live screen. With audio off the
  firmware keeps running silently, so the panel still works.
- **Synth REF: more instruments, and a panel per instrument.** A dropdown at the top of the node picks
  the firmware. New: **M-VAVE FM-1 with FM1 OPEN V14** (M-VAVE's open firmware, Supported; same
  family as SMK37 OPEN, with the same .syx / .fm1 / .fm1t imports) and **SMK-37 Pro with SLOOP SMK
  2.6 (SLOOP 2.8)**, the community GPL-3.0 firmware with its own engines, screen and panel
  (Experimental: built without the all-rights-reserved Felucca artwork, so its knob-card icons are
  blank; no file import, its state is saved in the patch). The panel is now built from the controls
  each firmware declares: SMK-37 (18 buttons, 8 encoders, faders, 16 pads, 37 keys), FM-1 (14
  buttons, 7 encoders, a volume pot, 27 keys with LEDs) and SLOOP SMK (18 buttons, 8 encoders, faders,
  16 pads with pad lights, 37 keys). MiniLab 3 profiles for each.
- **Synth REF MIDI**: notes from the notes pin when it is cabled, otherwise from the MIDI device chosen
  on the node (lock it, or let it follow the selected Synth REF); AUTO MAP from controller profiles
  (Arturia MiniLab 3 included) or MIDI LEARN through Infinite's own MIDI learn; MIDI DEVICE RESCAN,
  automatic reconnection when the controller comes back, MIDI DIAGNOSTICS.
- **Synth REF timbres without recompiling**: LOAD FILE takes DX7 .syx (single voice or 32-voice bank)
  and the firmware's .fm1 projects / .fm1t timbres; your files and profiles live in
  Documents\Infinite-Turbo\SynthRef. The firmware's whole state is saved in the patch; several
  Synth REF nodes run independent firmwares. Docs: `docs/SYNTH-REF.md`, `docs/SYNTH-REF-BANCOS.md`,
  `docs/SYNTH-REF-MVP-TEST-GUIDE.md`.
- Build: the full build fetches a pinned llvm-mingw Clang once (like the ASIO SDK) to compile firmware
  cores; the MIT build is unchanged.

## 0.71.1-turbo (2026-10-24)

Sand, dust and particle sculptures with PointZ, rendered by Render 3D HQ. Example:
`examples/hq/J_sand_1m.inf` - a million grains settling on drifting dunes, parted by a
wandering repulsor (~8.9 ms a frame at 1080p, RTX 4060 Laptop).

- **Render 3D HQ: points as dots.** New "points as" menu at the end of the panel: Spheres (as
  before) or Dots - every PointZ point a grain "dot size" pixels wide (times PScale) at any
  distance, lit as a tiny sphere, with shadows, GTAO and TAA like the spheres.
- **Lookup Attribute PointZ: components and renames**, as in TouchDesigner's POPs: `P.y` takes only
  the source's y, `P.y=Lum` writes the source's Lum into y, `Height=P.y` makes a new attribute;
  new "combine" (replace / add / subtract / multiply / min / max). Old patches look the same.
- **New: Force Target PointZ** - pulls each particle to its own point of a second PointZ (a text, a
  grid with noise, a mesh) with a damped spring: particles form and hold a shape.
- **New: Collide PointZ** - a floor (any plane) for particles, with bounce and friction: sand piles
  instead of falling through.
- **Render 3D HQ: screen-space GI** ("screen-space GI" at the end of the panel, off by default):
  light bounces off what is on screen, so emissive objects finally light the floor, walls and their
  neighbours (Filament itself has no GI). One bounce per frame from last frame's image, more over
  frames; its own temporal accumulation keeps it smooth. ~+0.5 ms a frame at 1080p. Example:
  `examples/hq/K_emissive_gi.inf`.
- **Fix, Render 3D HQ: ambient occlusion on spheres and plain instances** no longer bleeds across their
  silhouettes (the full-screen pass they are shaded in read the far plane as their depth).
- **Fix, Render 3D HQ: ghost trails behind Instance PointZ copies under TAA** when the points move
  (noise on a grid of boxes). Each copy now has its own motion from last frame's transform; before,
  only the camera's motion was known, so the TAA history lagged behind every moving copy.

## 0.71.0-turbo (2026-10-23)

Plan: `docs/plans/HQ-071-STRATEGY.md`; technical notes: `docs/RENDER-3D-HQ-FILAMENT.md`.

- **New node: Render 3D HQ** - a second 3D renderer next to Render 3D, wired the same way (geometry
  A-D, camera, three lights, HDRI), with techniques ported from Google Filament @ 13be3649
  (Apache-2.0, rewritten in GLSL; no Filament code is linked). Render 3D is unchanged. Example
  scenes in `examples/hq/filament/`, and the same scenes for Render 3D in `examples/hq/raster/`.
  - Depth prepass, so each pixel is shaded once; physically based materials (Filament's BRDF with
    multiple-scattering energy compensation, clear coat, sheen, specular anti-aliasing, the
    Material node's 8 maps and Mapping); image-based lighting from the HDRI with no seam.
  - PointZ points drawn as round, lit spheres straight from their GPU buffers; Instance PointZ
    copies; PointZ triangles. Spheres and plain instances are shaded in one deferred pass.
  - Shadows from the sun (Filament's PCF, soft option, contact shadows), GTAO ambient occlusion,
    Filament's TAA with per-object and per-particle motion (the HQ's anti-aliasing: no jagged
    edges or flickering small objects), Filament's bloom and its ACES tonemaps next to Render 3D's,
    render scale with FSR 1, RCAS sharpening.
  - Quality presets Performance / Balanced / High set the real params (shadow map size, GTAO on
    and its resolution, render scale). A still scene reuses its last frame once the TAA settles.
  - With TAA off (e.g. a texture sliding over its surface, which TAA smears), FXAA smooths the
    edges. Soft and contact shadows are filtered in screen space: smooth penumbrae with or without
    TAA. A bump from a grayscale height map no longer sparkles in the distance.
  - The Material's opacity, alpha cutoff and texture alpha work as in Render 3D (transparent
    surfaces keep their highlights, like glass), and the 360 output (equirect, cubemap, dome) too.
  - Measured on the reference scenes (whole frame, 1080p, RTX 4060 Laptop): as fast as Render 3D or
    faster on 8 of 9 scenes while drawing more - 1M spheres 11.86 -> 5.61 ms, the generative cloud
    5.16 -> 2.55 ms; +0.14 ms on the particle studio. Details: `docs/RENDER-3D-HQ-FILAMENT.md`
    section 13.
  - Not in the HQ yet: PointZ lines, shadows from more than one light, Render 3D FX after it (the
    picture passes through), RTX (stays in Render 3D). Known issues: section 15 of the same doc.
  - Built with `INFINITE_ENABLE_RENDER_HQ` (on, both editions); without it the node draws as
    Render 3D and says so.
- **Faster (P1, measured A/B against the S7 build, whole frame, 1080p):** the owner's reference
  patch 14.64 -> 10.11 ms (-31%), the HQ's 1M-point scene 9.65 -> 5.68 ms (-41%), Render 3D's
  1M-point scene 15.76 -> 11.92 ms (-24%).
  - Noise PointZ, curl over 4D gradient noise (the default of the HQ scenes and the reference
    patch): each lattice cell's 16 gradients are hashed once and shared by the curl's samples
    (1M points: 4.88 -> 1.31 ms of GPU). Same arithmetic; positions move by at most ~2e-5 (the
    driver rounds the two code shapes a few ulps apart).
  - Particle PointZ on the CPU engine (patches saved before 0.70) copied its whole GPU-made source
    back to the CPU every frame (305k points with every attribute under `inherit`); it now draws
    the next births ahead, in the same order, and copies back only those rows: the same particles
    bit for bit, 3.57 -> 0.20 ms of GPU.
- **Fix, visible in saved patches: Sphere, Cylinder, Cone, Torus Knot, Helix, Pyramid and the tubes
  of Curve / Curve Ops / L-System were wound inside out.** Back-face culling then drew the inside of
  their far half: the right outline, but lighting and reflections inverted in depth (a light from
  behind looked like a light from the front; a polished metal sphere reflected the floor on top).
  They now face outwards like the other shapes (`tests/unit/MeshTests.cpp` checks 21 of them), so
  these objects light and reflect correctly in Render 3D, RTX and the HQ.

## 0.70.0-turbo (2026-10-23)

Plan: `docs/plans/NEXT-STRATEGY.md` (milestones, gate) and `docs/plans/NEXT-BACKLOG.md` (item IDs).

Measured on the 1080p PointZ reference patch against 0.62 (same machine, interleaved runs): frame
19.9 -> 14.4 ms (-28%) as saved; with GPU particles 10.0 ms; with GPU particles and `lod pixels` 4,
7.3 ms. 27 corpus patches open with the same picture as 0.62.

### Fixes from the owner's test
- **Deleting a PointZ -> PointZ cable works**: the editor removed it but the input kept its source
  (`DisconnectLinkById` had no generic geometry-pin case), so it came back the next frame. Every
  node's geometry pin is now cleared (`LINKDELTEST`).
- **Textures on instances in RTX**: RTX now draws Instance PointZ's per-copy textures (image pins,
  pick by index, atlas grid, by the raster's rules), and a Material on the instance's **shape** input
  reaches the copies in raster and RTX (it was dropped in both). `RTXTEXTEST` checks Material over the
  instances, on the shape, as an atlas and the image pin, raster and RTX.
- **RTX on a moving scene: temporal accumulation with motion vectors** (X1, SVGF's temporal half;
  replaces the blind blend of the first test, which left trails behind every moving copy). Each
  pixel's first hit says where it was last frame - through the previous camera and each instance's
  previous transform, so moving copies and particles reproject too - and `rtxReproject` keeps the
  history only where the surface matches (distance and normal), clamped to the current neighbourhood,
  up to 24 frames. The motion is taken from the pixel centre, not the jittered sample (that blurred
  the picture over frames). The fast filter and the AI denoiser read the accumulated image.
- **HDRI: upright, and seen by RTX at all.** The HDRI node uploaded the image top row first, so the
  previews and the raster background showed it upside down; it is now uploaded bottom row first
  (GL's convention, which Render 3D's sampling already assumed). **Visible in saved patches**: an
  HDRI background and the reflections and lighting it gives now come out the right way up
  (a vertical flip no rotation could undo) - the compat run lists exactly these patches
  (3d_noise_teste_56fps, paartCubes) as differing, nothing else. It was also a 3-channel texture,
  which CUDA cannot map: RTX never saw the HDRI and lit everything with the sky gradient. Now RGBA,
  and RTX samples it the raster's way. The global stb_image "flip" flag that the glTF texture decoder
  toggled could also flip an HDRI loading at the same time; that decoder now uses the per-thread flag.
- **RTX matches the raster's look where they differed by mistake**: vertex and instance colours as
  the raster uses them (they were linearised a second time - redder, darker), the raster's rim term
  and ambient-tinted image-based diffuse, its roughness floor (0.045).
- **Less RTX grain**: the lobe of a near-mirror metal is chosen by its actual weight (one ray in ten
  went to a diffuse lobe worth ~2% and came back 10x), light after a diffuse bounce is capped against
  fireflies (a mirror's reflection is not), the fast filter leaves the background alone and costs
  4 ms instead of 7 at 1080p. "AI" uses the fast filter while the scene moves (the AI denoiser on a
  few samples a frame painted blotches for ~20 ms) and takes over once it holds still.
- Measured on the owner's patch (1080p, animated, as saved: AI, FSR 1.5x): trace 4.7 ms + denoise
  2.0 ms; no trails, no blur, edges as crisp as the raster. Still darker and redder than the raster:
  the copies are dense mirrors, and in RTX most of their reflections see the neighbouring copies
  (orange on orange), where the raster's image-based light assumes an open sky - a difference of
  model, not a bug.
- **New node: Render 3D FX** (3D category; patched right after a Render 3D, the way upstream's
  Volume reads the render's camera and depth, so Render 3D itself stays close to upstream). From the
  render's depth and motion: **ambient occlusion** (contact shadows; on by default), **screen-space
  reflections**, **depth of field** (focus distance and range) and **motion blur** with a shutter of
  up to 8 frames (the third test showed why one frame was not enough: slow particles move 1-3 px a
  frame). Each effect is off at 0; with any other image, or an RTX render, the picture passes
  through. Render 3D gains `RequestVelocity()` / `VelocityTexture()` next to its `RequestDepth()`.
- Render 3D FX review (before the commit): **motion blur did not show on a moving object**, for two
  reasons. The velocity pass's bookkeeping dropped each slot's last placement on the first moving
  frame (the slot's source was not recorded with it), so that frame had no motion; and the blur
  averaged each pixel along its own motion only, so an object smeared inside its own silhouette and
  never over the background. The blur is now McGuire et al.'s reconstruction filter: each tile's
  longest motion, the longest that can reach each tile (as far as a streak reaches, not just the
  next tile), a per-pixel jittered gather weighted by depth - a surface in front streaks over what
  is behind it, never the reverse, and a fast object shows what is behind it through its blur. New
  check `R3DFXTEST`: each effect changes the picture, all off passes it through, no NaN or GL error,
  with MSAA and render scale, the moving object's velocity reaches the pass, an RTX render passes
  through (`INFINITE_R3DFXTEST_PNG` saves the motion-blurred frame).
- **TAA keeps moving objects sharp**: its motion vectors were the camera's only, so every copy or
  particle that moved on its own was reprojected to the wrong place and the history clipping
  smeared it (the soft, "not crisp" picture of the third test). The TAA now runs Render 3D's
  velocity pass while the scene moves - each copy's matrix last frame, found by `PartId` (or index
  while the count holds), through the same culled lists and LOD quads as the opaque pass.
- The interim `motionBlur` param on Render 3D (never released) is gone; RTX's motion blur kernel
  stays for its 0.72 rework.
- **Particles keep their motion through births and deaths**: copies of an Instance PointZ over
  particles (and RTX's particle spheres) are followed by `PartId` - a ring of last frame's matrices
  on the GPU, in both modes - instead of by index, which the compaction of dead particles
  reshuffles. The RTX temporal accumulation and the motion blur both use it. Instance PointZ now
  passes `PartId` on with its points (shared buffer, no copy).
- **AI denoiser while things move = OptiX's temporal model** (flow from the motion vectors), and at
  render scale 2x with AI chosen, **OptiX's temporal 2x upscaler** replaces FSR 1 for RTX (FSR 1
  enlarged the path tracer's grain). Both are slow at 1080p (~25-30 ms on an RTX 4060): "Auto" and
  "Fast" stay the real-time choices; "AI (best)" is for renders that can wait.
- HDRI review: `.hdr` files are now read bottom row first by stb_image itself (thread-local flag),
  like the EXR loader already did - the first fix flipped every image at upload and would have
  turned EXR environments upside down.
- **Font lists**: Text 3D's font dropdown showed only "Arial" - JUCE's family list came back empty
  when first asked and the fallback stayed cached; the families now come from Windows itself (GDI)
  plus JUCE's. Text PointZ's `font` is now the same dropdown (it was a text field).
- **Output fullscreen on a monitor above or beside the editor** no longer covers the editor's top
  rows: the output window was placed while GLFW still counted a frame around it, so it came out a
  frame larger than its monitor (and it is topmost).
- **The Material node's preview shows the material**: the thumbnail shader used only colour and
  texture; metallic, roughness, specular and emission now shape it (GGX highlight, sky reflection).

### Render quality (after the owner's 1M-cube test patch)
- **RTX traces several samples per frame** (Frame::traceBudgetMs, ~5 ms; `INFINITE_RTX_BUDGET_MS`
  overrides, 0 = one as before): a moving scene restarts the accumulation every frame and the trace
  cost ~0.6 ms for one sample while the AI denoiser spent 10-20 ms inventing the picture from it.
  Measured on the owner's patch (1080p): 1 -> ~8 samples per frame, cubes keep their faces, the
  grain is mostly gone.
- **LOD copies are exact boxes**: the quad that stands in for a far copy now intersects the view ray
  with the copy's box per pixel - exact silhouette, face normal, UV and object position - instead of
  a flat card lit by one face (the "ghost" look at `lod pixels` 11.5). Still 2 triangles per copy;
  depth stays the quad's (no `gl_FragDepth`, so early-z holds for every draw). CULLTEST: the LOD
  covers 99.8% of the real boxes' pixels.
- Still to come (0.71): a hybrid mode (raster first hit + traced reflections and shadows), the
  usual answer for animated scenes; the OptiX temporal denoiser and 2x upscaler (above) are too slow
  at 1080p for real time.

### Audio: every param reaches the audio thread (AUDIOPARAMSWEEPTEST green)
- **Fixed: Super Mixer's compressor attack below 10 ms did nothing** - `MasterChain::TimeCoef`
  floored every time constant at 10 ms (the max was taken after converting ms to s), which also
  stretched a short automatic limiter release.
- The sweep rig: the note consumer is registered before the held note (synths ran their first
  blocks silent, hiding most of their params); windows of blocks, a note-off, a running transport,
  an input step and worker-thread settling make time-based params observable; extra input pins get
  their own tone; a cube feeds geometry pins. Gated params declare their prerequisites (rateDiv needs
  sync, Mixer channels 9-12 need 12 channels, Punch FX pads their note, ...). What the rig cannot
  drive headless is SKIP with its reason, never pass. 1758 FAILs -> 0.
- ROUNDTRIPTEST: copy/paste accepts only the values a paste re-derives by design (Drum Sequencer's
  out-of-range trims clamped, a pasted Looper's own loop id and file); every other key must match.

Moves to 0.71: C4, U1-U3, F4 (GlState: also removes the `glGet` state queries the GPU helpers
still make per call), A1/A2, X1's spatial half (SVGF's variance-guided filter; the temporal half
is in), the RTX hybrid mode, one multi-draw for all slots and Hi-Z (R1), R3, P2 sort, Subdivide / Extrude of faces on the GPU, Line Resample by length, the
4-byte count readback of GPU particles with `PartDeath`, M6 (V1-V5, C1).

### M0: measure and protect
- **Perf regression run** (F1): `test-windows.bat -Perf` / `-PerfOnly` plays the perf corpus
  (`perf-corpus.txt`, starting with the 1080p PointZ reference patch) three times with VSync and the
  frame limiter off (`INFINITE_PERF_UNCAPPED`), keeps the median frame, CPU, GPU, Render 3D and
  per-node GPU times, and fails when one is more than 10% and 0.5 ms worse than
  `perf-baseline.json`. Per-node CPU times are reported only: they swing up to ~70% between
  identical runs.
- **Patch corpus gate** (C3): `test-windows.bat -Compat` opens 27 patches (examples, test patches,
  the user's patches and lessons) and fails when one goes from ok to crash / hang against the
  v0.62.0 reference; `compare-patchcompat.py --gate`.
- **MIT edition in the driver** (F7): `test-windows.bat -Mit` builds and tests
  `build\windows-vs2022-mit`; verified: same two known failures as the full edition, and Render 3D's
  RTX mode reports "RTX is not in this build".
- **GPU capture markers** (F3): debug groups for each PointZ cook (named after the operator),
  Render 3D (shadow, post, panorama) and 3D previews; render targets labelled. `INFINITE_GLDEBUG=1`
  (or a Debug build) prints GL errors and performance warnings with the frame number;
  `INFINITE_GLDEBUG=sync` also names the debug group and the calling code (file:line, from the
  PDB) of each error and performance warning; `INFINITE_GLDEBUG_MAX` raises the 200-message cap.
- **Built-in GPU profiler**: `INFINITE_GPUPROF=1` times every debug group on the GPU (timestamp
  queries read two frames later, no waiting) and prints the average ms per group every 120 frames
  (Render 3D, its shadow and post passes, 3D previews, each PointZ operator). On the reference patch
  with GPU particles it showed the frame is GPU-bound in Render 3D's main and shadow passes (the
  per-node CPU times in FRAMEPROF were the CPU waiting on that). Measured there: 3D previews 1.1 ms,
  shadow pass 1.3 ms, instance matrices 0.16 ms, MSAA 8x vs 2x 1.2 ms - the rest is drawing the
  instanced geometry twice (main + shadow), the case for GPU culling and LOD (R1). New groups:
  "Render 3D: opaque slot", "PointZ instance matrices".

### M2 start: PointZ kernels as compute shaders (P1)
- **Compute backend, now the default** for every GPU PointZ operator
  (`docs/adr/0001-pointz-compute-backend.md`): the same kernel specs generated as GL 4.3 compute
  shaders over packed float SSBOs; no four-output limit; lookups read buffers directly instead of a
  texture made per call. Transform feedback stays as the fallback (a kernel that does not build as
  compute, or `INFINITE_POP_BACKEND=tf`) and is tested by the new `POPTFTEST` check. The patch
  corpus renders identically to v0.62.0 (27/27).
- **GPU primitives** (P2, `src/pop/GpuAlgo`): exclusive scan, stream compaction of indices and an
  indirect-dispatch gather - the count never comes back to the CPU. `GPUALGOTEST` checks them
  against CPU loops from 1 to 1,000,003 elements (two scan levels): identical.

### Particle PointZ on the GPU (P3)
- **New `engine` param (CPU / GPU)**, at the end of the list: a Particle PointZ made from the picker
  runs on the GPU; a patch saved before keeps the CPU engine (the new `PopDef::savedBefore`, through
  `INode::BeforeLoadParams`), so old patches behave exactly as they did.
- The GPU engine keeps the particles in GPU buffers from birth to death: integration, ageing and size
  / colour over life are kernels, the dead are removed by GPU stream compaction, births gather their
  source rows on the GPU. The CPU only draws the random numbers - same generator and order as the
  CPU engine, so a seed gives the same particles - and counts the living per birth cohort with the
  same float operations the kernel uses: no readback, no per-frame upload.
- **Equivalence** (`PARTICLEGPUTEST`, 90 frames of births, deaths, drag, damping, random velocity,
  jitter, size and colour over life): same count and ids, ages and life spans identical, positions,
  velocities, sizes and colours within 1.2e-7.
- **Speed** (A/B against the M0 reference build): the reference patch with its Particle on GPU,
  frame 19.1 -> 9.7 ms (~52 -> ~103 fps; Particle GPU 3.5 -> 0.06 ms, Render 3D 14.6 -> 1.6 ms, no
  more upload of every particle each frame). A 1,000,000-particle variant: 150 -> 30 ms; the
  simulation itself is 1.8 ms, the rest is drawing a box instance per particle (R1 / R8).
- A `PartDeath` attribute from the force chain (e.g. Group PointZ named PartDeath in the loop) kills
  on the GPU too; from then on that node's count comes from the GPU (a 4-byte readback a frame, the
  one place the GPU engine waits). `PARTICLEGPUTEST` runs such a loop on both engines: same count,
  same ids.
- The transform-feedback fallback runs kernels with more than four outputs as passes of four (ADR
  0001, step 4); the particle kernels (5 and 7 outputs) match compute bit for bit (`POPEQUIVTEST`).

### PointZ topology on the GPU (P5, first part)
- **Delete PointZ on a GPU-made input** gathers the kept points on the GPU (the mask, and so the
  indices and the count, are known on the CPU) instead of bringing every attribute down and up
  again; faces and lines are remapped as before. `GPUALGOTEST` checks the kept rows are exactly the
  input's.
- **Merge PointZ with a GPU-made input** joins by GPU buffer copies (`gpu::ConcatRows`, the CPU
  Concat's rules: an attribute any input has is given to all, defaults where missing); the CPU path
  stays for CPU-only inputs and for an attribute shaped differently in two inputs. Particle PointZ's
  GPU engine shares the same helper. `GPUALGOTEST` checks rows, attributes and faces.
- **Normal PointZ (smooth) of a GPU-made input** runs on the GPU: the faces around each point are
  built once per topology (in face order, as the CPU sums them) and a kernel per point sums the face
  normals from P in place. Identical to the CPU result (max difference 0, `GPUALGOTEST`).
- **Facet PointZ (unique points) of a GPU-made input** gathers the face corners on the GPU,
  keeps the sequential faces per topology and computes the flat normals in a kernel. P exactly the
  corners, normals identical to the CPU (`GPUALGOTEST`).
- **Subdivide PointZ on lines of a GPU-made input** runs on the GPU: each level's Chaikin stencil
  and topology are built once per input topology, the new primitive `gpu::WeightedRows` applies them
  (summed in the CPU's order with `precise`, so no fused multiply-add): bit for bit the CPU result
  (`GPUALGOTEST`). Faces still subdivide on the CPU (MeshOps).
- **Line Smooth PointZ on a GPU-made line** relaxes on the GPU (P6): neighbours built once per
  topology, one kernel per pass with the CPU's formula, bit for bit (`GPUALGOTEST`). Kernel bodies
  may now use `precise` (no fused multiply-add on compute; the transform-feedback fallback defines
  it away for GLSL 1.50).
- **Line Divide PointZ on a GPU-made line** (linear or Catmull-Rom) divides on the GPU, bit for bit
  (`GPUALGOTEST`). Subdivide and Divide now share `gpu::RowStencil` / `ApplyStencil`: a recipe of
  output rows written like PointBuilder (Copy / Lerp / Weighted), built once per topology.
- **Revolve PointZ and Extrude PointZ (lines) on a GPU-made profile** stay on the GPU (P5): the
  copies are a stencil, each ring's matrix / each step's offset (the CPU's floats) moves P in a
  kernel with `precise`, Revolve's Tex comes from a table and the normals from the shared
  `GpuFaceNormals` (Normal PointZ uses it too, now with the CPU's (0, 0, 1) for a point with no
  area around it). P, Tex and topology bit for bit; N within 2e-7. Extrude on faces still goes
  through MeshOps on the CPU.
- **Line Resample PointZ by count on a GPU-made line** stays on the GPU (P6): each line's length
  is summed on the GPU in the CPU's order (one thread per line), each new point finds its segment
  by binary search (the CPU's forward scan gives the same segment) and every attribute is lerped by
  `ApplyStencil`. Within 1e-5 of the CPU (the GPU's sqrt and divide). "By segment length" stays on
  the CPU: there the number of points depends on the length, which would need a readback.
- `GPUALGOTEST` now checks each of these against its own CPU path on the same GPU-made line
  (`gpu::ForceCpuForTest`), and that the result stays on the GPU.

### Render 3D: motion vectors, TAA and FSR upscale (M4: MV, R4, R5, X4 first part)
- **TAA** (new `TAA` checkbox in Render 3D's new "quality" section): every frame is drawn with a
  different sub-pixel camera jitter (16 Halton steps) and blended into a history in linear HDR
  before the tonemap - variance clipping in YCoCg against ghosting, a Catmull-Rom history fetch,
  luminance-weighted blending against fireflies. A still scene keeps refining for 16 frames and is
  then cached again as before. The edge against the plain background stays soft and the background
  colour exact: the TAA also averages the geometry's coverage of each pixel and the HDR finish takes
  the cleared background out before the tonemap and puts it back after (new key mode in
  Render3DPost).
- **Motion vectors** (MV, the base shared with the RTX temporal denoiser, X1): camera motion from
  the depth buffer and last frame's camera (`Render3DTemporal`). Surfaces that move by themselves
  are not in them yet (no per-vertex history): the history clipping limits the smear on them.
- **Render scale + AMD FSR 1** (new `upscale` dropdown, `sharpness` slider): the scene is drawn at the
  output size over 1.5x / 1.7x / 2x / 3x and AMD FidelityFX Super Resolution 1 (MIT, both editions)
  brings it back - EASU upscale then RCAS sharpening, on the tonemapped image. Works for raster and
  RTX (traced at the smaller size). The output keeps the node's size. FSR expects an anti-aliased
  input: use it with TAA (or MSAA).
- New keys at the end of Render 3D's params (`taa`, `upscale`, `sharpness`); missing = off, so old
  patches render exactly as before. New check `TAATEST` (TAA + FSR 2x on a still scene: output
  size, convergence, cached after it, coverage); `INFINITE_TAATEST=taa|fsr|plain` writes each variant
  for comparison (`INFINITE_TAATEST_PNG`).
- Shaders now live in `shaders/*.glsl` (A5), compiled into the exe by `cmake/EmbedFiles.cmake` and
  read with `EmbeddedFile()`; the FSR headers are fetched at configure time.
- **Measured** on the reference patch (1080p, RTX 4060, one run each, `INFINITE_GPUPROF`). The raster
  render there is bound by geometry, not pixels (a quarter of the pixels saves only 13% of Render 3D's
  GPU time), so the raster gain comes from replacing MSAA:

  | Variant | Frame | Render 3D GPU |
  |---|---|---|
  | as saved (MSAA 8x) | 9.93 ms | 8.38 ms |
  | TAA, no MSAA | 8.32 ms (-16%) | 6.85 ms |
  | TAA + FSR 1.5x, no MSAA | 8.04 ms (-19%) | 6.54 ms |
  | RTX native | 17.06 ms | trace 3.0 + denoise 5.9 ms |
  | RTX + FSR 1.5x | 11.68 ms (-32%) | trace 1.4 + denoise 2.5 ms |
  | RTX + FSR 2x | 10.25 ms (-40%) | trace 0.9 + denoise 1.45 ms |

  TAA costs 0.25 ms at 1080p (motion vectors 0.03 ms), FSR 0.19 ms.

### Render 3D: GPU culling and LOD of instances (R1, first part)
- An instanced slot with 32768+ copies (Instance PointZ, or a CPU instancer) is sorted on the GPU
  when a good share of it (over 25% of its box, tested on the CPU) is off screen, or with `lod
  pixels` on: a compute pass tests each copy's bounding sphere against the
  frustum, stable compaction + gathers (`pop::gpu`) build the kept copies' attributes and the draw
  reads its count from the GPU (`glDrawElementsIndirect`) - nothing is read back, no `glGet`
  (`src/nodes/Render3DCull.*`, `shaders/render3d_instance_cull.comp.glsl`). The copies keep their
  order and their original number (`aInstanceIndex`, used by the per-instance texture choice), so the
  picture is the same.
- Fixed (gate): a 4096 threshold and "any part off screen" sorted the reference patch's 305k boxes
  every frame for a sliver outside the view, 0.7 ms more than drawing them all (seen as Particle
  PointZ +27% in PERF). Now: 32768 copies and a quarter of the box off screen.
- With `lod pixels` on, the **shadow pass** sorts the same slots against the light's view (same GPU
  pass, sizes in shadow-map texels): a copy smaller than that casts no shadow, a texel or two that
  the soft shadow filter hides anyway. Off (0) draws every copy, as before.
- New `lod pixels` slider (quality section, key `lodPixels` at the end of the params; 0 = off, old
  patches unchanged): a copy smaller than that on screen is drawn as one camera-facing quad with
  its box's projected area, lit with the normal of the face the camera sees most of.
- New check `CULLTEST`: forced and automatic culling give the plain draw's pixels bit for bit; LOD
  quads cover the same area (98%). `INFINITE_R3D_CULL=force|off` for A/B.
- **Measured** on the reference patch (1080p, 305k boxes of 2-3 px in slot 1): culling alone
  changes nothing there (everything is on screen); `lod pixels` 4: slot 1 3.91 -> 1.98 ms,
  Render 3D 8.44 -> 6.52 ms, frame 9.98 -> 8.07 ms (-19%), picture visually the same. With the
  shadow pass culled too (GPU particles, `ref_lod4_gpu.inf` vs `ref_particles_gpu.inf`): frame
  10.03 -> 7.26 ms (-28%).
- `DRAGTEST`: the pan phase starts on the graph point farthest from every node (it started on a
  fixed point that the taller Render 3D node now covered, so the pan never armed - test only).

### Patch files
- **Lines a node does not read now survive a save** (C2): a patch from upstream or a newer build,
  saved in Turbo, keeps those param lines (`GraphNode::foreignParams`); a key the node writes itself
  wins. `FOREIGNKEYTEST` checks it end to end. (Copy / paste of a node still drops them.)
- **Equivalence (R5):** `INFINITE_POP_BACKEND=both` runs both backends and prints one `POPEQUIV` line
  per distinct kernel. Every kernel of the reference patch, POPTEST and PORTEDTEST (about 40): max
  difference 0, bit-identical. New check `POPEQUIVTEST` in the suite.
- **Speed with compute** (reference patch, A/B against the M0 reference build): frame 18.4 ->
  14.05 ms (-24%; 15.3 ms with transform feedback), Transform PointZ GPU 0.52 -> 0.04 ms,
  Copy PointZ 0.54 -> 0.07 ms, Noise PointZ 1.89 -> 1.53 ms. Particle PointZ 2.8 -> 3.65 ms (to
  look at in P3).

### Fixes and speed (found with the M0 tools)
- **Render 3D lost every uniform on the frame its image-based lighting was (re)built** (patch load,
  HDRI change): the prefilter passes left no program bound, so ~190 GL_INVALID_OPERATION a frame and
  one frame drawn with stale uniforms. The program is bound again after the rebuild.
- **PointZ bounds no longer stall the pipeline** (F6): every slot's async bounds reduction cleared
  one shared result buffer while the previous slot's copy was still queued, a pipeline sync per
  PointZ slot per frame. Each reduction now has its own buffer. Reference patch, A/B in the same
  session: frame 18.4 -> 15.3 ms (-17%), Render 3D cook 14.4 -> 12.7 ms.
- **GPU buffer pool waits two frames before reusing a store** (F5): a kernel no longer overwrites a
  buffer that this frame's draws or async copies still read. Particle PointZ GPU time 3.4 -> 2.8 ms,
  Render 3D cook 12.7 -> 11.1 ms (frame time unchanged: the frame is now bound elsewhere).
- **Tracy profiler** (F2), optional: `cmake -DINFINITE_ENABLE_TRACY=ON` fetches Tracy v0.11.1
  (BSD-3) and adds CPU zones per frame stage, PointZ cook (named after the operator) and kernel,
  Render 3D, its post and the 3D previews, plus GL GPU zones. On demand: no measurable cost until
  the Tracy GUI (v0.11.1) connects (reference patch 15.28 ms with, 15.25 ms without). Off by default
  and in release builds; `src/core/Profile.h`.
- **Unit tests** (A7): `InfiniteUnitTests` (doctest 2.4.11, MIT, one header fetched at configure time;
  not part of the normal build) runs in every `test-windows.bat` as UNITTEST. First suite: the .inf
  reader and writer (every param type, escapes, nodes, cables, Turbo and upstream `mod` lines,
  unknown lines skipped), 7 cases; the C2 gap (unread keys dropped on save) is an expected failure
  until it is fixed.
- Docs: `docs/patch-format.md` (C2; found: unread param keys and unknown lines are dropped on the
  next save), `docs/pointz-gpu-status.md` (P11, 86 operators, first heuristic pass).
- Skills: `turbo-branch-workflow`, `turbo-code-review`; `run-turbo-tests` documents the new switches.

## 0.62.0-turbo (2026-10-22)

GPU first: an animated PointZ patch at 1080p went from ~17 fps to ~52 fps (raster) on an RTX 4060,
and the path tracer became a hardware RTX renderer.

### Render 3D
- **RTX mode** replaces the GL path tracer (which stalled the whole machine): NVIDIA OptiX on the
  RT cores. One sample per pixel per frame, accumulating while nothing moves; meshes, PointZ
  geometry and instances, particles as real spheres; HDRI, lights, glass; 360 equirect / cubemap /
  dome. Animated geometry is refitted, not rebuilt. **Denoise**: Auto (fast a-trous filter while the
  scene moves, the OptiX AI denoiser once it is still), AI, Fast, Off. The test patch: ~25 ms a frame
  at 1080p with everything animated.
- RTX is an optional module of the build (full edition only, needs the CUDA Toolkit; the OptiX 7.7
  headers are fetched from NVIDIA's optix-dev repository). Without it, or without an RTX GPU, the
  mode draws raster and says why.

### PointZ on the GPU
- **Copy PointZ** runs as GPU kernels (identical output, verified against the CPU path): no template
  download, no per-point CPU work.
- **Particle PointZ** samples its source from last frame's GPU copy (no stall), and removes dead
  particles in place (~3 ms -> ~0.3 ms a frame).
- **Image to PointZ** keeps its grid and reruns only the kernel.
- Point bounds (framing, shadows) are a compute-shader reduction instead of blending every point into
  one pixel.
- Kernel lookups can read both inputs (`KernelIO::input` / `as`).

### Previews and readbacks
- 3D node previews draw PointZ straight from its GL buffers (they converted the whole cloud to a CPU
  mesh every frame - ~38 ms on the test patch) and refresh every other frame unless hovered.
- Image Analyze, Palette, Predictive Coloring, Image to Points, Depth Projection and Displacement
  read the GPU back asynchronously (GLUtil::AsyncReadback, about a frame of latency) instead of
  stalling the pipeline every cook.

### Menus
- The SHAPES category is gone: the named shapes were the Shape node with a preset, which offers
  every shape itself. Patches that use them by name still open.

### Tools
- `INFINITE_FRAMEPROF=1`: CPU and GPU time per frame stage, the slowest node bodies, Render 3D's
  share, RTX stage times. `INFINITE_PATCHCOMPAT_PLAY=1`: the patch probe with the transport
  running, with per-node CPU and GPU cook times.

## 0.61.0-turbo (2026-10-21)

3D quality and 360 output, and patches that travel with their media.

### Render 3D
- **Linear HDR**: the scene is lit and blended in 16-bit float and finished in one pass - **bloom**
  (threshold, radius) from what is brighter than white, tonemaps **ACES**, Reinhard, **AgX** and
  **Khronos PBR Neutral**, sRGB and dither. The plain background colour stays exactly as chosen;
  depth / normals / position passes are untouched.
- **HDRI lighting done properly**: a GGX-prefiltered copy of the HDRI (one roughness per mip), a
  diffuse irradiance map and the BRDF table (split-sum, as the Khronos glTF-Sample-Viewer) replace
  the mip-blur approximation - metals and rough surfaces read correctly.
- **Path traced** mode: a progressive GPU path tracer over the same meshes, materials, lights and
  HDRI - soft shadows, light bouncing between objects, real glass. Noisy at first, clean after
  `samples` frames; any change restarts it. Traces meshes, PointZ geometry and instances, and
  particles / point sprites (as small solids, with their colour); surface texture only. Animated
  scenes restart it every frame, so it is for stills and slow moves.
- **360 output**: equirectangular (2:1), cubemap 3x2 or **dome** (domemaster fisheye, square, with
  `dome angle` and `dome tilt` - 0 = zenith at the centre, 90 = camera forward at the centre), from
  the camera's position. Raster renders six faces through the normal pipeline (cached when nothing
  moves); path traced traces the panorama directly.

### PointZ / video
- **Particle PointZ**: size over life (birth, death, curve), colour over life (birth, death) and
  turbulence (amount, scale, speed).
- **VMPC**: during a transition the outgoing clip keeps playing instead of freezing.

### Menus
- Node categories grouped by family with coloured headings (VISUAL 2D, 3D, SOUND, CONTROL), in the
  side panel and the floating menu; Prediction moved to CONTROL.

### Files
- **FILE > Save with media...** (Collect Media): the patch plus every file it uses (samples, video,
  images, 3D models with their glTF buffers, MIDI, LUTs, fonts, presets) copied into
  `<name>_media/<type>/` next to it, same content once, with a report of what was copied and what
  is missing. Paths inside the patch's folder are saved relative and resolved on open, so the folder
  opens on any machine. Output paths (render / record / snapshot targets) are not collected, and
  a file already in the folder with the same content is reused instead of copied as `_2`.

### Tests
- PATHTRACETEST, PANORAMATEST, COLLECTMEDIATEST.

## 0.60.0-turbo (2026-10-21)

Catch-up with upstream (Naman's Infinite) after v0.6: 30 nodes and operators, taken as upstream wrote
them. Old patches open unchanged.

### Image and video
- **Time Machine**: each pixel from a different past frame, by a grey map (slit-scan by default).
- **Optical Flow**: motion vectors per pixel on the GPU (Lucas-Kanade), or as colour.
- **Fluid**: a 2D GPU fluid the picture rides in, with force and obstacle inputs and a stirrer.
- **Volume**: ray-marched fog, smoke or cloud; after a Render 3D it sits in that scene (its camera
  and depth). Render 3D now keeps its depth as a texture for this.
- **dither**: posterize with an ordered (Bayer) or noise threshold, so gradients keep their tone.
- **NDI In / NDI Out**: video over the network (OBS, vMix, Resolume). Needs the free NDI runtime;
  without it the nodes say so and nothing else changes.

### 3D
- **Sweep** (profile along a path) and **Revolve** (lathe) surfaces.
- **L-System**: plants and fractals from rewrite rules, drawn by a 3D turtle.
- **Proximity Lines**: plexus / constellation lines between nearby points.
- **Rigid Bodies**: pieces of a mesh fall, collide and stack (box, sphere or hull colliders).
- **Bevel** and **Inset** in the geometry operators.

### Audio and control
- Synths **FM** (4 operators, 11 algorithms), **String** (waveguide, pluck / bow / hammer) and
  **Partials** (playable additive resynthesis of a sample).
- Effects **Saturator** (three stages, six curves, five routings), **Convolution Reverb** (a designed
  room or a loaded impulse file) and **Shape Resonator** (rings the modes of the shape on its pin).
- Modulators **MSEG** (a drawn shape, looped or triggered), **Voice Spread** and **Note Expression**
  (MPE pressure / slide; MIDI inputs now store channel pressure and CC74). LFO and Random get the
  upstream rate modes (beats, synced division, Hz) and LFO "phase from"; Smooth gets time and spring.
- Drum Sequencer: **accent vol** next to accent pitch (0-12 dB on accented steps).
- **Synth engine from upstream**: Analog and Wavetable run upstream's current cores (band-limited
  oscillators, voice lanes, the newer filter models), with the parameter mailbox and note queue
  they need. Turbo's own changes stay on top: comb filters, the old filter order of Equation /
  Wave Terrain / Image Spectral Synth, the one-block timeline clip pitch, Sampler's one-shot release.
- **Per voice**: LFO, Random and MSEG get the "per voice" switch. Cabled into a voice param of
  Analog or Wavetable, each note gets its own cycle; Voice Spread and Note Expression are per note
  too (MPE pressure and slide carried on each note).

### Opening upstream patches
- **Patches with names** (upstream's hand-written format, 93 of its 493 patches): nodes named by an
  `id` line, cables and bindings that name nodes, inputs and outputs, `mod` / `expr` lines that name
  the control by its key. These used to open with no cables and no modulation; they open wired now
  (upstream's PatchSchema, verbatim). Bindings by name attach once their nodes have drawn.
- Upstream's `mod` range (lo / hi, the destination's own units) becomes Turbo's range mapper; Turbo
  files carry a marker line so the two `mod` layouts are never confused. A file with `rev 2` keeps
  the defaults for keys it does not write, as upstream reads it.
- A geometry loop (Join -> Transform -> Join) is refused on the canvas and over RPC, and a patch that
  has one opens with the closing cable dropped - it used to crash the app (0.59 too).
- **Modulation on a collapsed node** no longer freezes (upstream fix): a collapsed node that a cable,
  expression, MIDI mapping or gesture drives still registers its controls, without drawing them.
- **Mixer**: 0 to 12 channels (the channels stepper) and solo per channel, as upstream. A Mixer saved
  by an older Turbo keeps its cables and modulation (its bindings move up one slot on load).
- Sampler, Molder, Grain Molder, PaulStretch and Drum Sequencer play a file at its own sample rate
  (a 44.1 kHz file is no longer sharp on a 48 kHz device); older patches keep the old pitch.
- Effects on upstream's current kernels: Delay wow / flutter / saturation / diffusion (tape, with
  analog on), Dynamics style (clean / bus / opto / FET) and sidechain high-pass, Pitch Shifter
  spectral mode, Bitcrush dither, and the kernels' other fixes. Audio Filter (Turbo's LFO), Phaser
  (Turbo's feedback) and EQ keep Turbo's own. New controls sit after the old ones, so saved
  modulation keeps its target.

### Gesture learning (upstream)
- **Moves**, **Predictive Modulator** and upstream's **Drift** (replacing Turbo's older Drift; its
  `home` knob is gone, every other key reads the same): they learn from how you move the knobs.
  Upstream's movement log records every param change and who made it (hand, modulator, expression,
  gesture, prediction); predictors write each destination in its own 0..1. Self-tests MOVELOGTEST
  and MOVESTATSTEST.
- **Shift-grab** a knob a predictor drives: it turns green, your hand takes over while you hold it,
  and on release the predictor learns where you left it and how fast you were moving.
- Drift draws one trace per destination in its header; SETUP > Movement log turns the log on/off,
  opens its folder and caps its size (256 MB / 1 GB / 4 GB). Knobs with a dB or frequency taper are
  logged and predicted along their own curve.
- Saved modulation and expressions name their control by key (`mod 5 cutoff ...`, upstream's own
  format), so a node whose body reorders its controls no longer moves a saved binding.

### Prediction and geometry (from the to-do)
- **Predictive Coloring**: learns what graded footage looks like and grades incoming frames toward it.
- Null 3D, Material, Mapping and Cloth forward the point cloud and curve riding with the mesh
  (upstream's component passthrough), so a Null no longer drops a cloud.
- Render 3D caps a point cloud's sprite fill under a budget (upstream's guard against a Windows GPU
  watchdog reset) by shrinking sprites instead of dropping points.

### Housekeeping
- Old local branches deleted; unmerged remote ones kept as tags (archive/*). Unused macOS icons removed.
- New `PORTEDTEST` self-test (23 checks) and upstream's `VOICEMODTEST` (88 checks).
- Patch compatibility probe (`INFINITE_PATCHCOMPAT`, `patchcompat.ps1` + `compare-patchcompat.py`):
  493 upstream and the Turbo patches opened in 0.59.1 and 0.60, compared node by node - no
  regressions.

## 0.59.1-turbo (2026-10-17)

### Audio: no blink when adding a module (upstream fix)
- Every topology rebuild (adding a node, moving a cable) re-prepared every audio node, zeroing delay
  lines, filters, reverb tails and smoothers of everything playing: the Audio Out blinked. A node is
  now prepared only when it is new, its sample rate or block size changed, or the engine restarted.
- Delay-compensation (PDC) lines live on the node and keep their contents across rebuilds; their
  Prepare is a no-op when the amount is unchanged.

### Show safety (after upstream v0.6)
- **Panic** (Ctrl+. or VIEW > Show): output fades out in 5 ms, every MIDI Out sends all-notes-off,
  plugins release their notes, the transport stops; again to resume. A red PANIC button shows it.
- **Show lock** (Ctrl+Shift+L): no deleting nodes or cables, dragging nodes, new cables or undo/redo;
  knobs, transport, Performance mode and the Set list keep working. A LOCKED badge shows it.
- **Pre-show check** (VIEW > Show): closed audio device, plugins not loaded, missing media files,
  absent MIDI devices and Outputs with nothing connected, each with a button to find the node.

### Performance and safety fixes ported from upstream v0.6
- Saving is atomic: Ctrl+S, the app settings and the recent list are written beside the file and
  renamed over it, so a crash mid-save cannot damage the old copy (accented paths work too).
- A plugin that crashed the app is named in `crash.log` and left unloaded when a patch brings it back
  (the node passes sound through); picking it again in Plugins retries.
- Adding an effect no longer compiles shaders in that frame: program binaries are cached in memory,
  and effect shaders are built ahead, one per frame, from frame 30.
- Particle System: the free-slot search is one pass per emission, not one per particle (same results).
- One note output feeds up to 64 consumers (was 8; past that they stayed silent).
- Node editor: a node with huge or non-finite bounds no longer crashes on reposition.
- Random: the seed hash uses unsigned arithmetic (no undefined overflow; identical values).

## 0.59.0-turbo (2026-10-17)

### Set blocks (long performances)
- Every Group is now a block that switches off: its nodes stop cooking, leave the audio graph
  (silent, no CPU) and read as nothing to cables from outside. Power button on the group header.
- **Set list** window (VIEW > Set list, Shift+L, top-bar icon): GO starts a block (alone, by default),
  rewinds and plays; Page Up / Page Down step through the set; All on / All off.
- FILE > **Import patch as a block**: a whole .inf joins the open patch inside its own group, off
  when the patch already has content, so several songs live in one patch.
- Blocks in **Performance mode**: right-click a group's power button (or the faders icon in the Set
  list) to add its on / off as a Toggle, or Assign an existing control and click the power button.

### AI chat and AI connections
- The chat talks to **Claude** (Claude Code) or **ChatGPT** (OpenAI's Codex CLI with a ChatGPT login).
- Redesign: message bubbles, tool calls folded into one line per answer, input that grows and empties
  on send, attachments (file button, Ctrl+V / paste image, files dropped on the window) with thumbnails.
- Guided setup when the CLI is missing or not logged in, with buttons that run the commands.
- **Select** shows the conversation as selectable text (drag, Ctrl+A, Ctrl+C); **Copy all** copies it;
  right-click Copy also on errors and notices.
- ChatGPT (Codex) can use the Infinite tools: the in-app MCP server is pre-approved for Codex
  (`default_tools_approval_mode`), which refused every call with `approval_policy=never`.
- SETUP > **AI connections**: ChatGPT on the web through a connector URL (`--mcp-http` plus a free
  Cloudflare quick tunnel, secret path), Claude Desktop, Claude Code, Codex CLI and other MCP clients.

### Menus and top bar
- MENU is now **SETUP** (startup, AI, performance, interface, theme, audio, updates, diagnostics);
  canvas, minimap and windows moved to VIEW, params and globals to EDIT, Quit to FILE, a new HELP.
- Top bar with icons: play / pause, rewind and audio power; BPM drags, scrolls or takes a typed value;
  the time signature is one button with a popup; the position reads bar, beat and one cell per beat.
- CPU, audio, GPU, VRAM and FPS in one monitor group (icon + value, details on hover).

### Plugins
- A VST3 that sends MIDI out (arpeggiator, sequencer, MIDI effect) gets a **midi** output pin: its
  note on / off leave as note events, to another synth, a note tool or a MIDI Out (external gear).
  Notes it leaves held are released when it is bypassed, swapped or unloaded. Bypassed, it is a MIDI
  thru: the notes coming in leave by the midi pin unchanged.

### Transform
- **edges** on the Transform node, under Translate: cut (empty), hold edge, repeat (tiles) or mirror,
  for what fills the frame where a translate or a smaller scale uncovers it. It was already there as
  common > more > extend, where nobody found it; both are the same setting.

### Smooth motion
- A node whose param holds an expression is no longer culled off-screen. Expressions only run for
  params drawn that frame, so a `=t...` param out of view (or near the edge) updated twice a second
  and whatever it drove (a looping translate) jumped at random, with the FPS steady.
- With the audio engine running, the clock moved once per audio block (10-20 ms) and blocks arrive
  unevenly, so `t` and the beats advanced in uneven steps from frame to frame. The frame now reads a
  smooth clock that advances by each frame's real duration and leans gently toward the audio
  position (it snaps on a seek, loop or rewind); the audio thread keeps the block-exact position.
- The NVIDIA GPU / VRAM reading (NVML) runs on its own thread: its driver calls took several ms on the
  frame thread twice a second (and nvmlInit far longer on the first frame).
- With the diagnostic log on, frames much slower than the average are logged with their time
  (`slow frame: ... at t=...` in Infinite.log), to find what a periodic hitch is.

### Expressions
- Typing a formula in a param field works however the field was opened: any `=` makes the text a
  formula (a double-click seed left in front, `0.500=t % 1`, used to commit the number 0.5 and drop
  the formula without a word).
- A formula that fails to evaluate shows a red `fx!` next to the slider; hovering says why.
- Param fields and the Expression globals window show what you type in its real case (the rest of
  the UI stays all caps): expressions are case-sensitive, and `t` and `T` used to look the same.

### Build
- `build-windows.bat` only refuses when the app window is open: an MCP bridge (`--mcp`) kept running
  by Claude has its exe renamed to `.old` and keeps working, so Claude needs no restart to build.
- Link fix: the chat thumbnails decode from memory (the app's stb_image has no file I/O).

### Startup and examples
- Opens with an empty patch and the audio off by default. SETUP > Startup picks an empty patch, the
  last patch, the example or a chosen .inf, and whether audio starts.
- `examples/` folder, copied next to the app by the build and the package: FILE > Examples opens
  them untitled.

## 0.58.0-turbo (2026-10-16)

### PointZ nodes (new category)
- 85 point operators after TouchDesigner's POP family, all one node class driven by an operator
  table (`src/pop`): tier A (generators and GPU kernels: Noise, Transform, Twist, Math, Random,
  ReRange, Quantize, Trig, Limit, Normalize, Projection, Texture Map, Field, Force Radial, Lookup
  Attribute / Channel / Texture, Group, Blend, Dimension, Phaser, Point, Attribute, Attribute
  Combine / Convert, Math Combine / Mix, GLSL, GLSL Select, Switch, Null, In, Out, SOP / TOP / CHOP
  to PointZ) and tier B (Merge, Delete, Convert, Normal, Facet, Primitive, Select, Sort, Topology, Copy,
  Extrude, Subdivide, Sprinkle, Revolve, Curve, Line Resample / Divide / Break / Metrics / Smooth /
  Thick, Accumulate, Analyze, Histogram, Particle, Feedback, Trail, Cache, Cache Select, Cache Blend,
  GLSL Create / Copy / Advanced, Point File In, File In, File Out, Table to PointZ, Text, Instance).
- GPU kernels run as transform feedback (OpenGL 3.2), so the module also builds on macOS.
- Render 3D draws PointZ nodes from the GPU (faces, lines, sprites) and instances with **Instance PointZ**.
- Loops: Particle / Feedback PointZ read the Null PointZ whose `tag` matches their `target`.
- Any existing 3D source plugs into a PointZ; every 3D node can read a PointZ back.

### 3D from upstream (Naman)
- Group 3D, Delaunay Mesh, Voronoi Cells, Curve Ops, Decimate op, Render 3D render passes, Material
  bump (height map) and texture alpha, cook-time warnings on 3D pins (red border, also in `explain`).

### MCP
- The authoring guide now says to mix every multi-source patch in a Super Mixer (one channel per
  source), lists the new 3D nodes and has a PointZ section; the server instructions say so too.

- Instance PointZ works like the Geometry COMP: per-axis Scale attribute, scale X/Y/Z, rotate and translate
  offsets for every instance, any attribute renamed, and per-instance textures (four image pins picked by
  TexIndex, instance number or at random, or an atlas grid of one image or the Material's texture).
- Noise PointZ: gradient, fractal, curl, ridged, turbulence, cells, value and random, translate 4D (time and
  a W offset move through a fourth dimension), harmonics / spread / roughness, exponent, offset, frequency
  per axis. Type and dimension are compiled into the kernel, so only the branch in use runs.
- Speed: Render 3D reads PointZ bounds without stalling the GPU (a 1x1 read per cloud every frame waited
  for the whole pipeline), missing attributes share one constant buffer instead of a new upload per frame,
  kernel outputs reuse pooled GPU buffers, and Trail PointZ (points matched by index) keeps its history on
  the GPU with a fixed topology instead of downloading every frame.
- The PointZ menu sits between 3D Geometry and 3D Scene & Render.
- Render 3D viewport: middle drag (or Shift + left) pans, right drag dollies, as in TouchDesigner; a drag
  that starts on the viewport no longer pans the canvas.
- Copy PointZ and GLSL Copy PointZ write every piece in place in one multithreaded pass and keep
  their topology between frames (index buffers upload once): a box copied onto 7k animated points
  went from ~29 ms to a few ms per frame.

### Fixes
- Audio device recovery: the minimum interval between restarts now counts from the end of a restart
  (a slow WASAPI/ASIO restart fired its own config-change notifications straight into a second
  restart) and the give-up window closes after 10 s of calm, so five slow failures still give up.
- Clip Matrix no longer touches OpenGL when cooked without a window (crashed the headless audio sweep).
- The startup example is never loaded during a self-test run.

### Tests
- `GEOWAVEUTEST` (upstream 3D) and `POPTEST` (every PointZ cooks on the GPU, GPU vs CPU, Render 3D,
  Particle loop) in `test-windows.bat`.
- DRAGTEST drags inside the graph's own rect (the docked module panel covered the old spot);
  SAMPLERDRAG / MEDIADRAG compare files, not spellings (`C:\tmp\...` vs `/tmp/...`); PLUGINDRAG
  uses one fake library row instead of a full VST3 scan (`INFINITE_PLUGINDRAG_REALSCAN=1` keeps the
  real one).

## 0.57.0-turbo (2026-10-14)

### Pattern Player (new, Notes)
- Plays note patterns from a bank: 66 built in (Arp, Piano, Guitar, Strum, Bass, Sequencer, Ambient),
  118 bundled .mid loops in 8 packs (Classical themes, Classical figures, Folk and traditional,
  Basslines, Latin and Brazil, Jazz and blues, Electronic, Turbo examples), your own .mid files in
  `%LOCALAPPDATA%\Infinite\NotePatterns` (one subfolder per category, **rescan** to reload) and
  sequences saved from the Note Looper. The bundled loops are all MIT: themes from public domain works
  (Bach, Beethoven, Mozart, Grieg, Joplin, Satie, Vivaldi, Pachelbel...) and traditional tunes,
  transcribed by hand, plus original patterns in common styles (see `assets/note-patterns/CREDITS.txt`).
- **open .mid**: copies any .mid into the bank (category Imported) and plays it.
- **Trim**: drag the start / end handles on the step view to loop part of the pattern.
- .mid patterns follow the file's key signature (its tonic lands on the chord root; files without
  one use their lowest note). **fit scale** snaps them to the global scale, so the degree knob moves
  a melody inside the key.
- **play** button (CV-able), **latch**, **bar sync** (starts on the next bar), **thru**.
- **source**: auto / input / global. With nothing held (or on global) it plays the chord built on the
  chosen **degree** of the global key and scale (stacked thirds, 3-5 notes), so it runs with no input
  and follows key / scale changes. With notes held it arpeggiates them.
- octave, transpose, speed (half / x1 / double), gate, swing, velocity; < / > / random (CV-able).
- Patterns are written as text tokens: `.` rest, `-` tie, `1..9` chord tone, `c` whole chord, `b` bass,
  `r` random tone, `+` play together; `'` octave up, `,` octave down, `!` accent, `?` 50% chance,
  `~` legato, `*N` ratchet.

### Note Looper (new, Notes)
- MPC-style note recorder: 8 sequences of up to 8 layers, length in bars, count-in, record / play /
  overdub that start on the bar, undo / redo by layer, erase, mute per layer.
- Quantize (with strength and swing) applies at playback, so the original timing is kept.
- **switch**: what happens when you pick another sequence while playing. **next bar** and **end of
  loop** keep the current one playing until then (no gap); **now** starts the new one from its top at
  once.
- **Trim**: start / end handles on the piano roll (1/16 grid) loop part of a take; saved with the
  sequence, used by save to bank.
- MPC-style face: a display (sequence, mode, bar.beat, tempo, length), big REC / OVERDUB / PLAY pads,
  UNDO / CLEAR / ERASE, sequence pads A-H in a 4 x 2 grid.
- Export / import .mid; **save to bank** writes the sequence to the Pattern Player bank
  (`NotePatterns/Note Looper/`). Sequences are saved in the patch.

### From upstream Infinite
- **MIDI Out**: sends notes and CC to a MIDI port (WinMM), clock optional.
- **Spatial Mixer**: binaural / stereo object panner (MIT KEMAR HRTF set) with a top-view stage. Head
  tracking is left out.
- Effects: **Key-Snap** (snaps the spectrum to the global key and scale) and **Spectrum Slide**.
- Shape Resonator is not ported (it needs a 3D geometry input).

### Fixes
- Dropdowns inside node bodies opened at the bottom-right corner of the screen and could not be
  used (MPC "sync to" division, Layout layer picker). They now open at the button, through the same
  deferred dropdown as every other node menu (`NodeDropdown`).

### Code and repository
- `src/main.cpp` (52k lines) is now a 280-line outline that includes the app's code from `src/app/`
  in order: `ui/`, `bodies/`, `graph/`, `frame/` (main() step by step) and `selftest/`. Still one
  translation unit, so the code, inlining and link are the same as before (checked line by line).
- MIDI file reader: key signatures (FF 59).
- `.gitattributes`: text stored as LF, `.bat` always CRLF, binaries marked.

### Infinite Dark (2026) theme
- Upstream's new look as a theme (Settings > Theme): design tokens, Inter font, rounded fields, accent
  ladder, field-well dropdowns, ring param pins that fill and pulse when modulated, quiet category
  caption on node headers. The classic themes are unchanged.

## 0.56.0-turbo (2026-10-13)

### Formula: Shadertoy mode
- Code that defines `mainImage(out vec4 fragColor, in vec2 fragCoord)` compiles as a whole file
  (GLSL 330, 150 on old contexts) with Shadertoy's uniforms: iTime, iResolution, iFrame, iTimeDelta,
  iFrameRate, iMouse, iDate, iSampleRate, iChannel0-3 (= in0-in3), iChannelResolution, iChannelTime.
  Paste the Image tab as is (Common tab code above it). Errors point at your own line numbers. Code
  without mainImage stays the body of shape(uv, p, t), as before; the 0.55 adapter block still works.
- Extra knobs from the code: each `uniform float name; // lo..hi = default` line becomes a knob with
  a CV pin (up to 16, saved by name as `uniform_<name>`), in both modes.
- Buffers: **float buffer** renders RGBA32F and keeps alpha; **last frame to** feeds the node's own
  previous frame to iChannel0-3 (ping-pong, cleared on compile / resize, iFrame restarts). Chain
  Formula nodes for Buffer A -> B -> Image.
- **mouse x / mouse y / mouse down** params drive iMouse (CV-able: map them to an XY pad or MIDI).
- Two new presets written for Infinite-Turbo (MIT): **Shadertoy: fractal tunnel** and **Shadertoy:
  terrain and clouds**, each with its own uniform knobs.
- GLSL editor: growable text buffer (the 8 KB one refused longer code), **Paste + Apply**, Tab inserts a
  tab, Apply is one undo step.
- MCP: the authoring guide has a full conversion section (Shadertoy tabs, buffers, textures, sound,
  keyboard, cubemaps, GLSL ES gotchas, GLSL Sandbox, ISF, TouchDesigner, HLSL, licences); `explain` on a
  Formula reports its mode, the compile error and its uniform knobs.

### Claude chat panel
- Paste works for long text (the input was a fixed 4 KB buffer, and ImGui drops a paste that does not
  fit whole). Shift+Enter makes a new line, Enter sends.

## 0.55.0-turbo (2026-10-13)

### Chord Progression
- **follows key**: the progression follows the global key. It remembers the key it was written in
  (home) and plays transposed by the distance to the transport's key, the nearest way (-6..+5), from the
  next chord on (no stuck notes). Raise the global key a semitone and every chord rises with it.
  Excludes **sets key** (turning one on turns the other off: they would chase each other).
- **Suggestions**: style + progression dropdowns, **load** and **random**. 75 progressions: 15 famous ones
  (pop axis, doo-wop, ii-V-I, Andalusian, Pachelbel, royal road, blues, lo-fi, neo soul...) and the 60
  style templates of NT ChordGen 5.1 (Ricardo Palmieri / Noisetupi): rock / alternative, Brazil (samba,
  bossa, samba-jazz, choro, baiao), soul / jazz / funk, flamenco, Africa / diaspora, modal, and composer
  styles (Radiohead, Bjork, Arrigo Barnabe, Itamar Assumpcao, Sakamoto, Clube da Esquina, Hermeto,
  Stevie Wonder, Debussy). **random** builds a new progression from the style's templates. Chords land
  in the global key, one bar each, voice-led; home is set to that key; one undo step.
- Following the mode (major to minor) is not automatic: chords are note sets, so only the key moves them.
- Fix: the right-click chord menu opened at the bottom-right corner of the screen. It now opens at the
  mouse (drawn outside the node editor, like the Clip Matrix menu) and no longer opens the node menu too.
- The suggestions row fits the node width (it used to stretch the node).

### Virtual Keyboard
- The **Keyboard** node is now shown as **VIRTUAL KEYBOARD** (type key unchanged, old patches load).
- **always**: computer-key typing plays without hovering the node, for live use. Still off while a text
  field has focus or Ctrl/Alt/Shift is held, so every app shortcut keeps working.
- **-** and **=** shift the octave while typing. Changing the octave (keys, knob or patch) releases the
  held keys first, so no note gets stuck.

### Menu organisation
- Categories follow the signal path: make, change, combine, send out. Order: Source, Shapes 2D, Video,
  Color, Effects, Mask, Feedback, Compositing, Output / 3D Sources, 3D Geometry, 3D Scene & Render /
  Audio In-Out, Note Sources & Sequencers, Note Tools, Prediction, Synths & Samplers, Audio Effects &
  Plugins, Audio Mix & Routing, Audio to Visual / Modulators, CV Tools, Analysis, MIDI-OSC-Transport,
  Macros, UI / Utility.
- New groups: **Shapes 2D** (the 21 named shapes, out of Source), **3D** split into Sources / Geometry /
  Scene & Render, **Notes** split into Note Sources (Keyboard, MIDI Notes, MIDI File, Chord Progression,
  Note Sequencer, Random Note Generator, Bouncing Balls) and Note Tools. Clip Matrix and Timeline moved
  to Video. Saved patches re-file their nodes on load.

## 0.54.0-turbo (2026-10-13)

Two editions from one source, and the first GPL "superpower".

### Editions
- **MIT build** (`build-windows.bat mit`, preset `windows-vs2022-mit`, folder `build\windows-vs2022-mit`):
  Infinite + Turbo code, all MIT. `release-windows.bat` now builds and publishes only this one.
- **Full build** (`build-windows.bat`, default): the MIT build plus `src/gpl/`, modules ported from GPL-3.0
  projects. CMake option `INFINITE_ENABLE_GPL`; the startup log says which edition is running.
- Opening a patch with full-build nodes in the MIT build keeps the rest of the patch and the status line
  names the missing nodes.
- New `THIRD_PARTY_NOTICES.md` (shipped in the ZIP) and `src/gpl/README.md` + `src/gpl/LICENSE`.

### Punch FX (full build)
- Pocket-operator style performance effects, a float port of SLOOP / Felucca's punch-in FX (GPL-3.0),
  27 effects: loops 1/4, 1/8, 1/16, 1/32 and 1/8T, stutter, reverse, tape stop, half speed, low / high
  pass sweeps, phone, crush, alias, gate, echo, wobble, kill low / mid / high, rewind, octave up, flanger,
  auto-pan, mono, fade out and **freeze** (new: a granular hold of the last quarter second).
- **16 pads** laid out like the MPC, each with its own effect (click a pad, pick its effect below) and
  its own pin: MIDI learn a controller pad straight onto it, or patch a Macro / MIDI Trigger. Notes cabled
  into the second input play the pads too (36 = pad 1).
- **Any number of effects at once**: every effect runs in its own engine, chained in table order.
- **Latch**: pads (mouse and MIDI) toggle on / off instead of momentary.
- Loops and stutter start on the beat grid while the transport runs. `effect` + `engage` stay as one
  MIDI-learnable trigger.

### Drum Sequencer
- A new Drum Sequencer (sidebar, canvas menu or MCP) comes with the **CC0 acoustic kit** on its lanes
  (kick, snare, closed / open hat, clap, low / high tom, bell); patches that load keep their own samples.
  Lanes whose saved sample moved with an older install are found again in any bundled kit folder.

- **Kit picker** on the Drum Sequencer: every bundled kit, one click loads all 8 lanes (undoable).
- **SLOOP's 37 drum kits** (full build only): 808, 909, 606, 80s, vintage, trap, drill, boom bap, lo-fi,
  phonk, house, deep house, techno, minimal, electro, disco, garage, jungle, dubstep, dembow, amapiano,
  afro, latin, tribal, synthwave, chip, arcade, glitch, industrial, hyper, ambient, jazz and the five
  sampled ones, rendered with SLOOP's own engine: 8 lane sounds + 8 extras per kit (FLAC, 19 MB), also in
  the Samples panel under "SLOOP kits".
- **Pattern packs**: `assets/drum-patterns/<pack>` is copied once into the MIDI pattern folder, where the
  MIDI browser lists it by style. `scripts/import_groove_midi.py` builds the Magenta Groove MIDI pack
  (human drummers, CC BY 4.0) from the dataset's zip.
- The Samples panel picks up new bundled content once (a rescan of the Turbo kits root for the CC0 pack).

### CC0 sample pack (both builds)
- `assets/drumkits/cc0-acoustic-kit` (11 sounds, named like the Drum Sequencer lanes plus rim, crash, ride), `cc0-percussion` (tambourine, shaker, conga, claves,
  woodblock, three vinyl scratches) and `cc0-instruments` (piano, vibes, bass, horns, strings, flute
  multisamples). VCSL / VSCO-2 CE / Sonic Pi recordings as prepared by SLOOP, all CC0. They show in the
  Samples panel under Turbo kits.

## 0.53.0-turbo (2026-10-13)

Video engine rebuilt for live use. Old patches load unchanged.

### MCP / docs
- Authoring guide (MCP skill) and README: video formats, Video In `resolution` / `frameRate`.
- ARCHITECTURE: the 0.53 video path.

### Video files
- New decoder on FFmpeg directly (OpenCV no longer sits in the video path).
- **HAP, HAP Alpha, HAP Q, HAP Q Alpha, HAP R** play GPU-native: the compressed DXT/BC blocks go
  straight to the graphics card, HAP Q's YCoCg is decoded in a shader. Many 1080p/4K layers with
  almost no CPU. Chunked HAP frames unpack on several cores.
- **Resolume DXV** (.mov) plays, multithreaded.
- H.264 / HEVC / VP9 / AV1 decode on the GPU (D3D11VA) when the card supports it; NV12 / YUV
  frames are uploaded as-is and converted to RGB on the GPU (no CPU colour conversion, no CPU
  flip, a third of the upload bandwidth of RGBA).
- Uploads go through pixel buffers (asynchronous DMA) instead of stalling the render thread.
- Video, VMPC and Clip Matrix share the same path.

### Video In (cameras and HDMI capture)
- New Media Foundation backend (replaces OpenCV/DirectShow, which never switched capture sticks
  off their default YUY2 mode). Every mode the device offers is listed in the log; the best one
  for the chosen size and frame rate is picked explicitly (MJPG preferred at 720p and up) and
  decoded to NV12, hardware MJPG decoder when available. Closing a device that stopped sending
  no longer hangs.
- New **fps** control (Auto, 60, 30, 25). Auto = 1080p at the highest rate the device offers.
- The node shows the negotiated format and the measured frame rate
  (e.g. "1920x1080 MJPG 30 fps, 29.9 delivered"); the same line goes to the log.
- One CPU copy per frame; colour swizzle, flip and mirror moved to the GPU.

### Spout In
- One GPU copy per frame instead of two.

### Quit
- Quitting with a camera open froze the app: the capture device was shut down from the UI thread
  while the capture thread was waiting for a frame. The capture thread now tears the device down
  itself; a device that stopped sending is left to finish on its own instead of blocking.
- Every teardown step is logged and a watchdog terminates the process if one blocks for 4 s, so
  quitting always finishes; the log names the step (and node) that hung.

### Repo
- Removed the upstream PDFs, three demo .mov files, an old prompt, stale build archives, an unused
  vcpkg_installed copy and old dist packages.

## 0.52.0-turbo (2026-10-13)

TouchDesigner TOP parity, stage 1 (plan: docs/TOP-PARITY.md). Old patches render as before: every
new control defaults to the previous behaviour.

### Common page on every filter
- **Resolution**: use input, half, quarter, eighth, 2x, 4x, custom, fit in custom, limit to custom.
- **Pixel format**: 8-bit, 16-bit float, 32-bit float, or the input's.
- **Extend**: what is read past the image edge - hold, zero, repeat, mirror (node default otherwise).
  Transform, Crop, Displace, Liquify and Kaleidoscope use it.
- **Smoothness** (interpolate, nearest, mipmap), **passes** (repeat the filter on its own output) and
  a **channel mask** (R G B A).
- Folded under "common" at the bottom of every filter; the line above it shows size and format.

### Image nodes
- **Crop**: new default "Cut (smaller image)" discards the cropped part, like the Crop TOP.
- **Transform**: pivot, transform order, background colour with comp over, tile limit, and rotation
  that no longer shears non-square images.
- **Blend**: Composite TOP placement of B (pre-fit stretch / fit / fill / native / fit h / fit v,
  justify, translate, scale, rotate, extend), alpha ops (over, under, inside, outside, atop, xor),
  size from A, B or custom, pixel format.
- **Fit**: limit (shrink only) and smoothness.
- **Switcher**: manual index is now fractional and modulatable; 1.5 blends B and C.
- **Feedback**: pixel format (16-bit float as before, 32-bit float, 8-bit).
- **Formula**: four image inputs, read as `texture(uIn0, uv)` .. `uIn3`, and "size from in0".
- **Displace**: amount X / Y, midpoint, and which channel moves each direction.

### Send / Receive
- Ten new Utility nodes: Send / Receive for images, CV, notes, audio and geometry, connected by a
  channel name, like Pure Data's `[send]` / `[receive]`. Several Sends can share a name; Receives
  combine them (image: first / index / add / max / average; CV: first / sum / average / max / min;
  notes: merged; audio: summed with gain; geometry: by index). A bypassed Send is muted.
  A loop through a Receive Image works like a Feedback node; audio and geometry loops are refused.

### Fixes
- Modulator cables into Range to Range, Smoothing, Mod Depth and the other modulator-input nodes
  (all but Math) were not restored when a patch was loaded; a multi-output modulator (XY, Path)
  feeding one now reloads on the right output.

### Tests
- New `TOPPARITYTEST` self-test (20 checks: pixels, sizes, formats, every filter compiling).

## 0.51.0-turbo (2026-10-06)

### Glitch, rebuilt
- Slices and blocks now come in **varied sizes** (Size Var, Splits): big and tiny pieces side by side.
- **Contrast** (most slices move a little, a few jump far), **Density**, **Stagger** (each slice on its
  own clock), **Burst / Decay** (glitch bursts with clean frames between), **RGB Var**, **Color FX**,
  **Axis** (horizontal, vertical, both).
- **Sync** to the beat (bar to 1/32) and a **Free-run** clock that keeps moving with the transport stopped.
- New kinds: **Scan Jitter**, **VHS**, **Compression** (macroblocks), **Pixel Sort**.
- Old patches look the same: every new control defaults to the previous behaviour.
- New **Datamosh** node: real codec-style moshing with block motion vectors on the previous frame,
  bloom (smear), threshold, block size, leak, refresh on the beat or by a mappable button, and an
  optional second input whose motion moves the first image.

### Chord Progression
- **Slots**: right-click to duplicate, insert, delete, move, copy and paste; drag to reorder; "+" cell;
  Ctrl+D, Ctrl+C, Ctrl+V, Delete, Alt+arrows.
- **Length**: double-click to type (2, 1.5, 3b for beats, 1.2 for bars.beats), drag snaps to beats;
  minimum 1/16 bar.
- **Extension chips** (7, maj7, b9, 9, #9, 11, #11, b13, 13, add9, sus2, sus4, no3, no5) add to the chord.
- **Keyboard plays** when clicked, shows the root, spans **4 octaves**, Shift+click sets a slash bass.
- Voicings: close, drop2, spread, octave up / down, lead (smallest movement); inversions wrap around.
- Any note set gets a name ("C MAJ7(9,#11)/E"). MCP: new `chord_progression` tool (chord symbols).

### Mixer faders
- Faders follow the **console law** on screen (0 dB at 3/4, as MIDI and Performance Mode already did),
  with dB marks, "-inf" at the bottom, Ctrl / Alt + click resets to 0 dB. Same fix on the Mixer, Gain,
  MPC Out, Clip Matrix gains and every frequency knob. Saved values are unchanged.

### Audio effects (from upstream Infinite)
- **Analog** character mode on Dynamics, Delay, Chorus, Flanger, Phaser, Pitch Shifter, Bitcrush,
  Ring Mod and Frequency Shifter; Flanger spread; Wavetable Shaper stereo.
- Audio Filter **LFO** (free or synced) next to the envelope and the cutoff-mod input.
- EQ: new band types hp / lp 24 and 36, band pass, notch, all pass (comb kept).
- Metallic resonator: correct long decays, clean release, calibrated strike.

### Fixes and polish (from upstream Infinite)
- **Crash fix**: the audio engine frees old node lists by generation; two rebuilds inside one audio
  block (paste plus a sample finishing to load) could crash. The audio thread starts on an empty list.
- Accented and non-Latin names (ł, ő, Ж, quotes, dashes) draw instead of "?".
- Interface font choice: IBM Plex or **Atkinson Hyperlegible**.
- Old patches whose bundled drum kit folder moved (each release unzips to a new folder) reload the kit.
- Dropdown search from 12 items, accent-insensitive.
- Output windows can be transparent when the Layout background is transparent.

### Recording and plugins

- Fixed a crash with audio plugins: a re-prepare (sample rate, block size or engine restart) republished the same plugin handle and a later swap destroyed the live plugin, so autosave read freed memory.
- Output recording no longer drops audio when the UI stalls: the capture ring is about 10 s, takes whole stereo frames only (no left/right swap after an overflow), and any lost samples are replaced by silence so audio stays in sync with video. Overflows are logged once per recording.
- The encoder thread now writes all pending audio blocks each time it wakes.
- Mirror output (Settings > Audio): plays a copy of the master on a Windows Audio device so OBS or screen recorders can capture it while ASIO is in use. Drift between the two devices is compensated; off by default.

## 0.50.0-turbo (2026-10-06)

### Quantized restart on sequencers
- **Chord Progression, Note Sequencer, Arpeggiator and MIDI File** get a mappable **restart**
  button and a **restart q** setting (immediate, next beat, next bar, 2 bars, 4 bars; default next
  bar). The sequence starts again from step 1 / chord 1 on the grid line. A blinking dot shows a
  restart waiting. Map it to MIDI, CV or a Performance button.
- Chord Progression now lands chord changes on the real block start (they were one audio block late).

### Chord names spelled out
- Qualities read **MAJ / MIN** in the all-caps UI: C MAJ, A MIN, A MIN7, C MAJ7, B DIM, C AUG,
  C SUS4. Plain numbers stay (C7, C9). Long names wrap root over quality.

### Note Switcher
- **8 inputs** (the **inputs** setting shows 2-8 pins; old patches keep 4).
- **switch q**: in manual mode a slot change waits for the next beat / bar / 2 / 4 bars and lands on
  the exact sample. Pending slot shows in amber.
- **slot 1..8** buttons (mappable) pick a slot and turn manual on.

### Scenes node (Macros): one button, several changes
- A radio-button scene launcher: up to 8 scenes x 8 outputs, cabled to any params. Entering a
  scene sets every output to that row, so what is ON in another scene and OFF here switches off.
- Output modes: **on/off** (default: mutes, switches), **choice** (switcher slot, drum part:
  follows the cabled dropdown), **level** (0..1), **pulse** (a trigger on entry: restarts).
- **Off row** on top of the grid: pressing the playing scene again goes there (option), plus a
  mappable **all off**. **q** quantizes every change; the queued scene blinks.
- **scene 1..8**, prev / next and the selector are mappable; a Performance button bound to a
  scene lights while that scene plays.
- Performance Mode: right-click a control, **+ Add Another Parameter...** to drive several params
  with one control. MCP: new `scenes` tool, `perf_add` with `element`.

### MIDI channels
- **MIDI Notes**: channel is a dropdown (omni, ch 1-16), a **learn** button takes the channel of the
  next note, and "last note in: ch N" shows what arrives (marked when filtered out). Example:
  MiniLab keys on ch 1, pads on ch 7, one MIDI Notes per channel.
- MIDI CC and MIDI Trigger get a channel dropdown; MIDI Trigger no longer fires when edited.

### Drum Sequencer
- **Import .mid**: button, drag and drop, or MCP. GM drum notes go to the 8 lanes, quantized to 16ths
  (swing or triplets detected), bars from the file, velocity 100+ as accent, into the live part.
- **Rate and swing stay** when switching A / B / C.
- **Edits are kept** per part and per groove; **revert** reloads the library part.
- **Your MIDI folder**: .mid files in `%LOCALAPPDATA%\Infinite\DrumPatterns` (subfolders, or an
  index.json with title, style, bpm) join the matching library group by style (electro-funk in
  Electronic, r-b in Funk & Breaks), tagged MIDI; unmatched styles get a "MIDI: <style>" group. A / B / C are
  the file's first distinct 8-bar chunks; the tempo is not changed. Rescan / open folder in the menu.
- **User presets**: save / load by name (all parts, rate, swing) in `%LOCALAPPDATA%\Infinite\DrumPresets`.
- Lane trims survive patch load, undo and paste.

### Super Mixer master
- Stereo **VU** (peak, RMS, peak hold, clip LEDs), **master pan** and **MUTE** (click-free).
- **Mastering chain** with a bypass each (all off by default): 3-band EQ, glue compressor,
  stereo width, saturation and a mastering limiter, with gain-reduction meters.
- **Limiter**: true-peak (4x oversampled) ceiling in dBTP, lookahead 0.5-5 ms with a smooth
  attack, auto release (fast for transients, slow for dense material), stereo link, styles
  (transparent, punchy, loud), GR history. Its lookahead is reported to delay compensation;
  with the limiter off there is no delay.

### Visuals
- **Slideshow**: mappable restart / prev / next.
- **VMPC**: transition styles between clips (the Slideshow set) and transition time; default Cut.

### Other
- New nodes always appear in front of existing ones.
- **Equation Synth**: 30 new presets (pads, plucks, basses, bells, leads, organs, percussion,
  drones), grouped by category, with envelope, filter, unison and drive.
- Grain Molder saves its sample file in the patch (it reopened empty before).

## 0.49.0-turbo (2026-10-03)

### Waveforms you can trim by eye
- Sampler, Drum Sequencer lanes, MPC pads, Slicer, Molder, Grain Molder, Granular and Paul
  Stretch draw a real waveform: min / max with the RMS body inside, one column per screen pixel,
  finer as you zoom the canvas in (a multi-resolution peak cache built once per sample).
- **Play cursors**: a line with a small head for every sounding voice (Sampler note voices and
  audition, each Drum lane voice, MPC pads, Slicer, Molder, Granular...), placed on the whole
  sample so it lines up with the trim handles.
- **Zoom to trim**: the magnifier chip in a waveform's corner shows only start..end (plus a
  margin), with a bar locating it in the whole sample.
- **Trim handles**: the time shows while dragging (1.234 s / m:ss.mmm), Shift drags 10x finer,
  grabbing no longer jumps, and the range can go down to 0.1% of the sample.

### Side panel: Samples and Media by folder
- **Library folders** you manage: Add folder, Rescan, rename or remove a root (files stay on
  disk); the bundled Turbo kits are added once as "Turbo kits". Saved in
  `%LOCALAPPDATA%\Infinite\SampleFolders.json` / `MediaFolders.json`.
- **Browse by folder** with a breadcrumb (Library / root / kit / sub): subfolders first with
  their file counts, then the files. Typing in the search box searches every folder and shows
  where each hit lives.
- **No duplicates**: a file reached through two roots, a root inside another root, different
  spellings of the same path or a link loop is listed once.
- **Drag a folder** onto a Drum Sequencer (lanes 1-8) or an MPC (pads 1-16) to load its audio
  files in name order; onto empty canvas it creates a Drum Sequencer with them. Single samples
  can now be dropped onto an MPC pad too.
- Scans run in the background and only the new folder is scanned when one is added.

### Crash fix: plugins
- **Crash writing MIDI into a plugin that was being reloaded or re-prepared** (seen in a crash
  dump: the pitch-bend range message going into a freed MIDI buffer). The plugin node now holds
  the plugin for its whole audio block, MIDI included, and reload / prepare / remove wait for it;
  a plugin still busy after 2 s is left in memory instead of freed under the audio thread.
- Release builds now write a PDB next to the exe, so future crash dumps can be read with names.

### Mappable controls
More node buttons go through the param machinery, so they can be MIDI-learned (Ctrl+M or
right-click), driven by a CV cable and put on the Performance panel:
- **Drum Sequencer** pattern picker: a `part` selector (A / B / C; a CV, MIDI knob or
  Performance selector switches the part once when it changes), the **A verse / B bridge /
  C chorus** buttons as triggers (a MIDI pad fires "B"), **< / >** groove as triggers
  ("prev groove" / "next groove") and `groove` (index in the shown category). A driven change
  from MIDI or the Performance panel is one undo step; a CV cable adds none.
- **Slicer**: Record, Audition (state buttons like the Sampler's), re-slice (trigger).
- **Molder**: Record (state), Roll, Iterate, Reset (triggers).
- **Grain Molder**: Record, Audition (state).
- **CV Recorder**: record / stop (state: high records, low stops).
- **Predictive Rhythm / Predictive Notes**: Learn / Stop (state).
- **Clip Matrix**: REC > ARR (state).

### From upstream Infinite
- **Sampler**: 16 voices (was 8); a file shared by several Samplers or reloaded is decoded once
  (decode cache).
- **Timeline clip pitch** for clips of a Sampler or an Analog (a "pitch (st)" slider in the
  clip settings), added on top of the node's own pitch; it lasts only while the clip plays.
- **Random Note**: style "melodic" (upstream's melody picker with bar / beat accent velocities)
  next to the original "walk" (default, so old patches play the same).
- **Geometry point size**: Image to Points and Depth Projection get "size relative to cell"
  (upstream's point base size: point size 1 matches the cell, carried through Geometry Op, Set
  Color and Switcher 3D into Render 3D). Off by default, so old patches look the same.

## 0.48.0-turbo (2026-10-03)

### New: MIDI File node (Notes)
- Plays a Standard MIDI File (.mid / .midi, formats 0 and 1) in time with the transport: positions
  are in beats, so the file follows the app tempo ("use file tempo" sets the transport to the
  file's). Playback starts on the quantize grid (default 1 bar) when the transport starts, a file
  loads, play is turned on, after a seek and at each loop.
- Track (all or one) and channel filter, transpose, velocity scale, loop with length in bars, a
  piano-roll preview with the playhead. Note-offs are sent on stop, loop, seek and changes.
- Drop a .mid on the canvas to create one, or on a MIDI File node to load it there. Own parser,
  robust against damaged files.

### From upstream Infinite
- **Looper**: one simple **take length** menu (free, 1/16 to 1/2 bar, 1 to 32 bars) replaces the
  mode / bars / sub-bar controls; speed, pitch and fine tune (varispeed, like upstream; away from
  1.0 overdub pauses), fade in / fade out per pass, output volume.
- **Looper PLAY in time**: with "in time" on (the old "sync to bar") and the transport playing,
  PLAY and DUB now wait for the next bar line like REC already did (PLAY blinks, "play in 1.2 s");
  pressed just after a line they start at once, already in phase. Pressing PLAY again cancels.
  REC and PLAY also lost a one-audio-block late start (the grid was read at the block's end).
- **MPC**: per pad speed (negative plays the trim range backwards), fine tune, fade in / out and
  sync to a musical division (a hit waits for the next grid line).
- **Macro Trigger and Performance bangs** fire Looper buttons and MPC / VMPC pads on every
  trigger (before, toggles reacted to every second one).
- **Audio In**: pick a stereo pair, a single input or a mix of all inputs, listed with the
  device's channel names; warns when the patch was set up on another input device.
- **Timeline**: live waveforms of clips from node audio are measured at 1/16 beat with min / max,
  like upstream.
- **Random Note**: groove (swing of the odd steps, also longer than an audio block).
- **Sampler**: position (start point inside start..end) and decay (0 = held, as before).
- **Metronome**: the top bar CLICK is now a drawn metronome whose rod swings with the beat.
- Patches without node positions wait for the measured node sizes before the automatic layout.

### Notes
- New saved keys are appended, so older patches load and sound as before. A CV / MIDI mapping on
  the old Looper mode, sub-bar or "sync to bar" controls needs to be redone on the new menu.
- MCP: `looper` takes `length` ("2 bars", "1/4 bar", "free") and `in_time`, and reports
  `length`, `in_time` and `waiting_s`; `looper` and `pads` accept the new params (`pads` also has a `state` action), and invalid
  values are rejected without changing anything.

## 0.47.0-turbo (2026-10-02)

### Claude chat inside the app
- **CLAUDE** in the top bar (or Shift+C, or VIEW > Claude chat) opens a chat window. It runs the
  Claude Code CLI you already have, logged in with your Claude account (Pro / Max), so it costs
  nothing beyond your plan and needs no API key. Its only tools are Infinite-Turbo's own MCP
  tools, so Claude reads and edits the open patch, looks at the picture and loads grooves, the
  same as from Claude Desktop. Model menu (default, sonnet, opus, haiku), New chat, Stop; the
  conversation continues between messages. Every change is one undo step.
- Not installed yet? The window shows the one-line cmd install
  (`curl -fsSL https://claude.ai/install.cmd -o install.cmd && install.cmd && del install.cmd`)
  and asks you to run `claude` once to log in. `INFINITE_CLAUDE_PATH` points it at another exe.

### Drum Sequencer: pattern library
- **141 grooves in 10 groups, each with three parts**: A verse (main groove), B bridge (lighter
  or breakdown) and C chorus (fuller, often two bars ending in a fill). Rock & Pop, Funk & Breaks
  (Funky Drummer, Amen, Impeach, Levee feels), Hip Hop (boom bap, Dilla, trap, drill, Baltimore /
  Jersey club, footwork), Electronic (house, techno, garage, DnB, jungle, dubstep, trip-hop
  Massive Attack / Portishead feels, Björk feels, big beat Prodigy / Chemical Brothers feels,
  IDM), Latin & Caribbean (son and rumba clave, cascara, tumbao, salsa, mambo, cha-cha-chá,
  songo, mozambique, bembé, guaguancó, reggaeton, cumbia, merengue, bolero, reggae, ska, soca),
  Brazil (ijexá, samba batucada, partido alto, bossa, baião, xote, arrasta-pé, xaxado, maracatu,
  samba-reggae, frevo, coco, ciranda, funk carioca tamborzão and Volt Mix, axé), Middle East &
  North Africa (maqsum, baladi, saidi, malfuf, ayoub, chiftetelli, samai 10/8, karsilama 9/8,
  aksak, fallahi, gnawa), India (teentaal, keherwa, dadra, rupak, jhaptaal, ektaal with bayan /
  dayan and doubled-bol fills, bhangra), Africa & World and Jazz.
- The Brazilian, Afro-Cuban, Caribbean and Middle Eastern grooves were rewritten part by part
  from the traditional ensembles (agogô, atabaques and xequerê in ijexá, surdos 1/2/3, caixa,
  tamborim and agogô in the batucada, zabumba and triângulo in baião, dum / tek / ka in the
  doumbek rhythms...). Song-named entries are feel sketches, not transcriptions.
- **Picker**: group, groove (only that group's, so the list is short), < > to step through
  grooves, and A / B / C buttons that switch the part in one click while it plays. The groove
  and part are saved with the patch. A pattern sets steps, rate (triplet grids for 6/8, 12/8
  and swing) and swing; the groove shows a suggested tempo.
- **Accent pitch** per lane (lane card): accented steps play that many semitones away, so one
  lane plays a two-tone bell (agogô, campana, cencerro: X = low bell, x = high). Grooves set it.
- Lanes show their role when empty (1 kick, 2 snare, 3 closed hat, 4 open hat, 5 clap / rim,
  6 low tom / conga, 7 high tom / conga, 8 bell / ride). Empty lanes get the bundled **Turbo kit**
  (eight synthesized one-shots in `assets/drumkits/turbo-basic`, the bell an agogô), hats in one
  choke group.
- MCP: **`drum_pattern`** lists the library (optionally one category) or loads a groove and part.

### Chord Progression: more chords
- 26 new builder qualities: 11 (9sus4), m11, maj7#11, maj9#11, 13, m13, maj13, 13sus4, 9#11,
  m(maj9), 7b9, 7#9, 7#11, 7b13, 7alt, m7b9, 7#5, 7b5, maj7#5, dim(maj7), 6/9, m6/9, madd9, add11,
  quartal and the "so what" voicing. The quality menu is grouped (triads, 6ths & adds, 7ths,
  9ths / 11ths / 13ths, altered, voicings). Saved patches keep their chords.

### Fixes
- **File > New crashed while audio was playing** (e.g. right after startup, with the example
  running): New dropped the nodes but never told the audio thread, which kept processing freed
  nodes. New now republishes the (empty) audio graph, and removed nodes are destroyed only after
  the audio thread has finished two blocks without them. New also clears the Performance panel.

- **Dropping a WAV onto a Drum Sequencer lane that was sounding crashed**: the lane's voices kept
  reading the replaced sample after it was freed. Its voices now stop the moment the new sample
  takes over (also on the lane's x and when the kit loads).

### Small
- Long dropdowns get a filter box; dropdown items with the same name no longer collide.

## 0.46.0-turbo (2026-10-02)

The rest of upstream's Windows-relevant features: Performance Mode, MCP phases 3 and 4, and the
small items. Left out on purpose: the Field language and everything macOS / Linux / headless only.

### Opens with an example
- Infinite-Turbo starts on **superSynthMCP**, a bundled example (two Analog synths through delay,
  mixer and reverb, note sequencers switched from a Performance page, audio-reactive kaleidoscope /
  glitch / bloom visuals), already playing. It opens untitled, so Save never overwrites it.
- Settings > Startup: "Open the example at startup" (on by default; off = start empty) and
  "Open the example now". A `.inf` given on the command line or an autosave to recover wins.
- `release-windows.bat`: clean build + package, versioned ZIP and release notes in `dist\release`,
  a local `v<version>` tag, and the GitHub "new release" page opened for the upload.

### Performance Mode (from upstream)
- **PERF** in the top bar, **Shift+P** or VIEW > Performance mode: a dockable panel (bottom, top, left
  or right) with pages of controls for playing live: knob, fader, slider, toggle, XY pad, trigger,
  number box, radio selector, bipolar knob, step gate.
- Each control is bound to any node parameter: right-click it > Assign Parameter..., then click the
  parameter on the canvas (the nearest one in the node under the mouse lights up). Or right-click a
  parameter's pin > "add to Performance".
- Edit mode (move, resize by type, rename, colour, copy / paste / duplicate, pages: add, rename,
  duplicate, reorder, delete) and Perform mode. MIDI learn per control (two axes on the XY pad).
- Saved with the patch in upstream's format (`perfui` / `perfname` / `perf` / `perftarget` /
  `perfmidi`), so the panels open in both.
- Discrete parameters (dropdowns, checkboxes, integers) follow the panel too.

### Claude / MCP
- **`screenshot_node`** and **`render_frame`**: Claude can look at any node's image or the live Output
  (JPEG / PNG, downscaled); `render_frame path` also saves the Output at full size.
- **`authoring_guide`** tool and **`build_patch`** prompt: the patch-building guide for AI assistants.
- **Turbo-only MCP tools**: `clip_matrix` (launch / release clips, scenes, stop rows, read state), `pads`
  (hit MPC / VMPC pads), `looper` (record, play, overdub, undo, state) and `perf_list` / `perf_add` /
  `perf_remove` / `perf_show` (build and open the Performance Mode panel).
- The authoring guide (and the AI skill) gained a **Turbo-only features** section: Clip Matrix, MPC /
  VMPC, Looper, Super Mixer, Layout, Projection, Spout, Transport Control and Performance Mode keys, so
  Claude can build patches with the nodes that only exist in Turbo.
- Settings menu, "AI assistants": Connect to Claude Desktop (same as `setup-mcp.bat`), Install AI skill
  for Claude Code (`%USERPROFILE%\.claude\skills\infinite-turbo-patching`), Save AI skill to a folder.

### Small ports (from upstream)
- **Single instance**: double-clicking a `.inf` with Infinite-Turbo already open opens it there (with
  unsaved edits it asks first: save then open / discard / cancel). Paths with accents work.
- **Interface scale**: the UI follows Windows' display scale per monitor (and changes when the window
  moves to another monitor). Settings > Interface scale: Auto or a fixed 80-200%.
- **Fit**: offset X / Y in output pixels (+ right, + up), modulatable.
- **CLICK** in the top bar: a metronome on the audio output while the transport plays, no node needed;
  the button flashes on the beat.
- **Update check**: one request at startup to this fork's GitHub releases; a newer one shows an
  UPDATE badge (opens the release page). Settings > Updates turns it off.

## 0.45.0-turbo (2026-10-02)

Upstream bug fixes, an MCP server for Claude, and three upstream patching aids.

### Build patches with Claude (MCP)
- **`Infinite-Turbo.exe --mcp`** is an MCP server (stdio) that edits the patch open in the running app
  over the local control port. If the app is closed, the first request starts it.
- **`setup-mcp.bat`** registers it in Claude Desktop's config (other servers kept, `.bak` of the old
  file). Claude Code: `claude mcp add infinite-turbo -- "<path>\Infinite-Turbo.exe" --mcp`.
- New RPC tools: `describe` (node types; per type its inputs, outputs, settings and help), `explain`
  (live graph with connections, param names and ranges, modulation, warnings), `batch` (one undo
  step, all or nothing, `$N` references), `get_patch_text` / `validate_patch_text` /
  `load_patch_text`, `modulate` / `set_expression` / `unmodulate` by param name (waits for a new node
  to draw), `auto_layout`, `transport`, `patch_format`.
- `create_node` takes `settings` and places the node by itself when no x/y is given; `connect` takes
  slots and outputs by label ("B", "audio"); `set_param` ignores case, spaces and underscores.
- The control server serves each client on its own thread and sends large replies whole.

### Patching aids (from upstream)
- **Live reload**: the open patch file is watched. A change made outside (an editor, git, an AI)
  reloads it as one undo step; with unsaved edits a banner asks Reload / Keep mine.
- **Auto layout**: nodes without a `pos` line are laid out by signal flow once they have drawn.
- **Live hints**: an amber border and a tooltip on a half-wired Blend or two-input filter, an Output
  with nothing connected, and an image loop without a Feedback node (300 ms after the last edit).

### Fixes (ported from upstream)
- Transform moves curves and point clouds too; Switcher 3D and Set Color pass curves and clouds on.
- Depth Projection and Image to Points no longer apply their tint twice (it was squared), and
  Depth Projection no longer applies its colour image twice.
- Distribute on Faces re-bakes the point colours when the inherited material or its colour changes.
- Render 3D draws translucent meshes back to front without depth write, so one does not hide the
  other behind it.
- A source wired to both an Audio Out and an Output is mixed into the device once (it was doubled).
- Gesture loops follow the render's video time during a timeline render (they played at the speed of
  the UI frame rate).

### Fixes (Turbo)
- Undo no longer forgets which file is open (the title lost its name and Ctrl+S asked for a new one).
- Undo, redo and reloads keep the node numbers of the saved patch when they can.

### Docs
- README rewritten: quick start, what Turbo adds, MCP, shortcuts, build, troubleshooting.

## 0.44.1-turbo (2026-10-02)

### Fixes
- **Crash during the timeline WAV / MP4 render**: while the main thread renders offline the device callback
  outputs silence, but it skipped its liveness timestamp, so after 0.75 s the audio recovery thought the
  device had died and reopened the ASIO driver in the middle of the render (crash inside the driver).
  The engine now stays "alive" during a render and device recovery waits until it ends.
- Time-stretch buffers (Audio File Player clips, Clip Matrix voices) are no longer reallocated on every
  audio topology rebuild when nothing changed (avoids freeing memory the audio thread may still be using).

### Timeline
- **Rename tracks**: double-click the track name or any free spot of the track header, press F2 on the
  selected track, or right-click > Rename. Enter or clicking away confirms, Esc cancels, an empty name
  returns to the default (A1, V1...).

## 0.44.0-turbo (2026-10-02)

### Clip Matrix (session view / clip launcher)
- **New node Clip Matrix** (Source): rows are tracks, columns are scenes (4 x 8 by default, up to 16 x 16).
  Audio, video and image clips, dropped on the cells or loaded from the cell menu.
- **Quantized launching on the audio thread**: clips, scenes and stops start on the next grid line
  (None / free, 1/16 to 4 bars; global or per clip), sample-accurate inside the block.
- **Modes**: loop (default), once, gate (plays while held). Launching on an empty cell stops the row.
- **Audio clips** are tempo-aware: detected BPM, sync to tempo (stretched, pitch kept), pitch per clip.
  Video clips follow the same clock, with their soundtrack.
- **Follow actions** per clip (after N bars or the clip length: stop, again, next, previous, first, last,
  any, other, with a chance for action B) and **scenes with tempo / time signature**.
- Every cell, scene and stop has a CV pin; MIDI notes launch cells (base note + row x scenes + column).
- **REC > ARR** records the performance into the Arrangement Timeline (one audio / video track per row,
  each launch and loop pass a clip, one undo step).
- **Clip Matrix Out** (AudioUtility): splits the matrix into one video + one audio output per row, like
  MPC Out. Cables now remember which output of a multi-output node they come from (saved as an optional
  4th token on `cable` / `aud` lines; older files load unchanged).

## 0.43.2-turbo (2026-10-02)

- **Spout In / Spout Out**: the Syphon nodes are named after what they are on Windows (Spout2), in the
  node browser, help and docs. Patches with the old `Syphon In` / `Syphon Out` names still open (renamed on
  load, like Set Color).

## 0.43.1-turbo (2026-10-01)

### Arrangement Timeline, rest of upstream's feature set
- **Video follows the playhead**: every Video clip is locked to the timeline (jumps, loop, scrub), with
  a movie offset; "lock video to timeline" off lets it run free.
- **Render / export queue** (RENDER...): MP4 / MOV with audio, or 32-bit float WAV, of the whole
  arrangement, the loop, the selected clips or a bar range; offline, one frame at a time, so picture and
  sound stay frame-accurate. Several jobs run one after another; cancel / show file.
- **Track groups (folders)**: + GROUP, track menu > Group, collapsible group rows with on/off, colour,
  rename, nesting, duplicate, ungroup / delete; clicking a group row selects its clips.
- **Waveforms and thumbnails**: audio clips show the file's waveform (analysed in the background) or,
  for live sources, what they played; video clips keep a film strip of frames captured while playing.
- **3D nodes** can be video clips (rendered like their mini viewport).
- **Audio Samples**: tempo detected on import, "sync to tempo" stretches them to the project tempo
  without changing pitch (Signalsmith Stretch, MIT, vendored), per-clip pitch +/-24 st, /2 x2 reset.
- **Per-clip modulation bypass**: a clip can hold any of its source's modulated knobs still while it
  plays.

## 0.43.0-turbo (2026-10-01)

### Arrangement Timeline (port of upstream's)
- **Docked timeline panel** (VIEW > Arrangement timeline, Shift+T): video and audio tracks, clips in
  bars/beats (upstream's model and patch lines verbatim, so arrangements open in both), ruler with
  scrub, markers, loop range, snap grid (bar to 1/16, triplets; Alt = free), zoom, follow.
- **Clips** point at canvas nodes: right-click a node > Add to Timeline, double-click an empty spot
  on a track, or drop audio / video / image files on the panel (each gets its own source node as a
  "Sample" clip, locked to the timeline). Move (across tracks), trim, fades, split, blade, duplicate,
  copy/paste, groups, enable/disable, colours, rename, clip settings window.
- **Timeline mode** (TIMELINE button): the timeline owns the audio output. Audio clips are gated,
  faded and panned per sample with track gain/pan/mute/solo and meters; canvas Audio Outs go quiet
  (live ones stay).
- **Video**: the video tracks are composited at the playhead (32 blend modes, opacity, fades as
  crossfades, brightness/contrast/saturation), shown in the panel monitor and available as the new
  **Timeline** source node, so the arrangement can go to an Output / projector.
- Transport gains seek and a loop range; nodes get a stable `uid` saved in the patch.

## 0.42.2-turbo (2026-10-01)

### Looper
- **No more late first take**: every buffer (two 120 s banks and a 1 s pre-roll) is allocated and
  committed on the first frame after the node is inserted, and the audio thread keeps the pages
  resident, so the first take never stalls on page faults.
- **Late REC presses land on the grid**: with sync on, a REC pressed up to 200 ms (or 1/4 of the
  grid) after a bar / sub-bar line starts the take on that line, back-filled from the pre-roll,
  instead of waiting a whole bar. While armed, the strip shows how long until the take starts.
- **The loop is saved with the project**: the current loop is written as a 32-bit float WAV in the
  Recordings folder (`looper_<id>.wav`) and reloaded when the patch opens. A whole-patch undo no
  longer wipes the loop either (the respawned node re-adopts it).
- **UNDO / REDO of takes and overdub layers** (with CV pins). UNDO during an overdub closes the
  layer and removes it; a new layer after an undo drops the redo branch. Up to 32 steps.
- **EXPORT WAV**: writes the loop, all layers summed, to a 32-bit float stereo WAV.
- Paste / duplicate of a Looper copies its loop into an independent looper.

## 0.42.1-turbo (2026-10-01)

### Metronome
- **Transport Control has a metronome**: `click` on/off (a pin, so a MIDI pad or Macro Toggle can
  switch it), `volume`, `accent` (higher click on the first beat of the bar). Plays while the
  transport runs, locked to the same beat clock as the sequencers.
- **Two routes**: `direct` mixes it straight into the audio device output, with no cable and
  without touching any audio connection (and it stays out of Audio Out recordings); the new
  `click` output pin sends it into the graph, for a mixer, an effect or an Audio Out.

## 0.42.0-turbo (2026-09-30)

Tempo, key and scale stay with the project and can be driven without the mouse; wave B of the
upstream catch-up.

### Transport
- **Patches save BPM, time signature, key and scale** (upstream `transport` line, so patches stay
  compatible both ways). A new document starts at 120 / 4/4 / C major. Undo does not move them.
- **Tempo changes no longer jump the playhead**: while audio runs, a new tempo lands at the next
  block after re-basing the clock (upstream fix). Before, every tempo edit during playback
  re-measured the whole elapsed time.
- **Transport Control** (Control, Turbo original): PLAY / STOP / REWIND / TAP buttons, each also a
  trigger pin; bpm with glide (accelerando/ritardando), follow external MIDI clock, key and scale
  (optionally waiting for the next bar), time signature. Every control is a modulation pin, so a
  MIDI controller (MIDI learn), LFO, Macro or sequence can drive them. A section only takes over
  while its `drive` switch is on; off, it mirrors the live transport. `T` taps tempo while the
  pointer is over the node. AUDIO starts/stops the audio engine; `audio on open` starts it
  whenever the patch is opened. Outputs (modulators): `beat` and `bar` (0..1 ramps locked to the
  transport), `bpm` (20..300 as 0..1) and `play` (1 while playing).
- **Settings > Audio > "Start audio when Infinite opens"** (off by default, machine setting).
- **Chord Progression `sets key`**: each chord sets the global key and a matching scale, so
  Quantizer, Random Note and other key-aware nodes follow the progression.

### From upstream
- **Gesture recording**: hold Shift while dragging any knob or slider to record the movement;
  releasing Shift loops it (while the transport plays). A green dot on the pin marks a looping
  param; grabbing it without Shift takes it back. Saved with the patch.
- **glTF / GLB import**: dropping a .gltf/.glb builds Model 3D + Material + one Image Source per
  texture map (albedo, roughness, metallic, normal, AO, emission), already wired, in one undo
  step. Dropped onto an existing Model 3D it just reloads. FBX, DAE and 3DS drops also open.
- **Sampler fade in / fade out** per pass (replaces the loop crossfade; the old key is ignored).
- **Patch reader** tolerates CRLF line ends and a UTF-8 BOM (files edited in Notepad or by tools).
- **Crash reports**: an unhandled crash leaves a minidump and a log line in
  `%LOCALAPPDATA%\Infinite\crash`. `diagnose-windows.bat` now includes a system report
  (GL, audio driver and devices, MIDI, VST3 blocklist) and the crash log.

### Looper
- A Macro Trigger or MIDI pad on REC / PLAY / DUB presses the button once per hit (footswitch
  style) instead of holding it only while high.

### Not ported
- Instance selection (needs upstream's instance-realize geometry refactor).
- Upstream's browser sort/filter strip and 9-category taxonomy: our browsers already sort and
  filter, and our menu organisation differs.
- Upstream's right-click MIDI learn: we keep our own MIDI learn mode.

## 0.41.0-turbo (2026-09-30)

Wave 1 of the upstream catch-up, plus a chord progression node.

### New nodes
- **Chord Progression** (Notes, Turbo original): a looped chord progression locked to the transport.
  Up to 16 chords, each set by clicking keys on a two-octave keyboard or filled from a root +
  quality builder (maj, min, 7, maj7, m7, dim, aug, sus2/4, 6, m6, 9, maj9, m9, m7b5, dim7, add9,
  7sus4, m(maj7), 5), with `inv` for the next inversion. Each chord lasts its own number of bars
  (half-bar steps). Chord names are recognised from the keys (including slash chords). Knobs:
  chords, octave, transpose, velocity, gate (1 = legato); `bass` doubles the lowest note an octave
  down. Play modes: block, strum up/down (`strum` ms between notes), arp up/down/up-down/random
  (one note per `rate` step over 1-3 `octaves`), pulse (chord re-struck every step), alberti
  (low-high-middle-high) and bass + chord (oom-pah). Chord changes and steps are sample-accurate.
  Plays while the transport runs, releases on stop.
- **Analog** (Synths, from upstream): polyphonic virtual-analog synth, two oscillators with sync,
  unison, sub, noise, ZDF ladder / SVF filters, amp envelope.
- **Slicer**, **Molder**, **Grain Molder** (Synths, from upstream): onset/grid sample slicer played
  from notes; partial-spectrum resynthesis; granular molding. Samples load by dragging from the
  Samples panel onto the node.
- **Geometry Table** (Modulators, from upstream): samples a mesh / point cloud / slice contour into
  rows of x/y/z modulator outputs plus centroid and spread. Instanced sources read as their base
  mesh for now.
- **Depth Projection** (3D, from upstream): turns a depth map (plus optional colour) into a point
  cloud or torn triangle mesh (perspective, planar, radial, cylindrical).

### Upstream features
- Compressor transfer curve shows live gain reduction.
- PaulStretch: modulatable `position` and waveform scrub.
- Wave Terrain: shows the baked wavetable stack.

### Fixed
- Nodes with 10+ outputs: the output pin block overflowed into the next node (Image Analyze `cy`,
  Audio Analyze `b6`-`b8`). Pin stride widened from 1000 to 1050, as upstream.

### Comb filter (from upstream)
- `comb +` / `comb -` filter types (feedback delay line; cutoff sets the tooth spacing, resonance
  the depth) in Wavetable, Oscillator, Equation, Wave Terrain, Image Spectral Synth, Metallic,
  Audio Filter and EQ. The response curves draw the comb teeth.
- Equation, Wave Terrain and Image Spectral Synth now offer the whole shared filter list (lp/hp
  12/24/36, bp 12/24, notch 12/24, comb). Unlike upstream, their saved filter index keeps its old
  meaning: the original five come first and the rest are appended, so old patches do not change
  filter. Metallic keeps its storage order as upstream does.
- Audio Filter's comb follows the env and mod sweep like the other types (upstream uses an LFO).
- Metallic modes are normalised for their strike peak (upstream fix in the same change): louder,
  and long decays no longer fade into silence early.

### Renamed
- **Set Color** is now **Set Vertex Color**, as upstream. Old patches load it under the new name.

## 0.40.0-turbo (2026-09-26)

Performance pass, then more upstream features.

### Performance
- **Off-screen nodes skip their body** (ported from upstream): a node well outside the view keeps its
  last box and pins (cables still land) and is not drawn. Always drawn: selected, dragged, nodes with
  modulation / MIDI / expression / palette bindings, anything while a popup or MIDI learn is open,
  the Keyboard node, and every node once per 30 frames. `INFINITE_NO_CULL=1` turns it off.
- **Image chains settle**: Noise, Shape, Blend, Fit, Layer Stack, Ramp, Color Ramp and Curves only
  re-render when their params, inputs or size change, so a still chain stops recooking everything
  downstream every frame. Filters cache while the transport is stopped even if they read time.
- **Separable blurs**: Gaussian Blur, Box Blur, Bloom, Diffuse Glow, Unsharp Mask and Outer Glow run
  as two passes (2N texture reads per pixel instead of N squared), through one shared scratch
  render target per size. Filter uniform locations are looked up once, not every cook.
- **Video / camera upload**: decoder and capture threads hand over BGRA (the GPU's native upload
  format); the render thread no longer pays a 3-byte BGR swizzle per frame.
- **Undo / delete / autosave**: the patch snapshot is O(N) (was O(N squared) with dynamic_cast in the
  inner loop), the undo stack drops old entries in O(1), deleting a selection rebuilds the audio graph
  once instead of once per node, and the autosave writes on a worker thread (no periodic hitch).

### Added
- **Audio In**: choose the input pair (1+2, 3+4...) or a single channel (mono) of a multichannel
  interface; each Audio In node reads independently (two Audio In nodes used to steal each
  other's samples).
- **Slideshow** (Source): the images of a folder with fade / slide / wipe / zoom transitions.
- **CV Recorder** (CV Tools): record any modulator on the beat clock, loops on stop, speed, low / high.
- **Resonator Bank**, **Cycle Shaper**, **Spec Blur** (AudioEffects) with their visualizers.
- **Reverb**: upstream's 16-line FDN redesign (Schroeder diffusion, delay modulation, SSE path) and
  an analog toggle. Existing params and patches unchanged.
- **Audio Filter / EQ**: live spectrum of the signal behind the response curve; EQ: Shift-drag a
  band dot to change its Q.
- **Modulation matrix** (VIEW, Shift+M): every modulation cable in one table, with amount,
  bipolar, range mapper, live value, jump to node and delete; expressions listed below.
- **Explode**: "by" Faces or Loose Parts (each connected part moves as a rigid piece).

### Live audio latency
- **ASIO**: the build downloads Steinberg's ASIO SDK 2.3.4 (GPLv3 since Oct 2025) and enables
  JUCE's ASIO driver; `INFINITE_ENABLE_ASIO=OFF` to leave it out. If the download fails the build
  goes on without ASIO.
- **Settings > Audio > Driver**: WASAPI shared (the old default), ASIO, WASAPI exclusive, WASAPI
  low latency, DirectSound. Buffer sizes down to 32 frames. The panel shows the driver-reported
  round trip in ms.
- **Audio Out "live"**: this output skips the delay compensation that lines several Audio Outs up
  with the slowest branch, so a mic -> plugin -> headphones path is never held back.
- The device list is scanned only for the chosen driver and cached (it used to rescan every driver
  type every frame while Settings was open, which can glitch live audio).
- build-windows.bat clears a stray CMAKE_GENERATOR_PLATFORM from the environment (the
  "will be ignored" warning on every build).

### Fixed
- **Plugins with a mono output** (Auto-Tune and other vocal tools, some synths): only the left
  channel carried the plugin, so panning right in a mixer gave silence. The host now asks for a
  stereo layout when loading and, for a plugin that stays mono, copies its output to both sides.
- **Plugin pitch smear at small buffers** (32/48 frames): the plugin processed a full 64-frame
  buffer on every smaller block and ran ahead of real time. It now processes exactly the block size.
  A mono source feeding a stereo plugin input goes to both input channels.
- **Plugin editors that draw with OpenGL** (Antares Auto-Tune and others) froze the Infinite window
  while audio kept running: the plugin left its own GL context current. The host now saves and
  restores the GL context around every piece of plugin UI code and reclaims it after each event pump.

### Not ported
- Mod Mixer (reverted upstream), Drum Sequencer lane outs (Turbo's MPC + MPC Out covers it),
  Geometry Table, asset decode cache, Performance Mode, update checker, glTF: next rounds.

## 0.39.0-turbo (2026-09-26)

Upstream features ported (items 1-13 of the easy list), adapted to Turbo.

### Added
- **Alpha filters** (Compositing): show alpha, opacity, set alpha (second input's luminance becomes
  the alpha), alpha invert, alpha from luma, alpha levels, premultiply / unpremultiply.
- **Theme**: Forest Green (light) preset; the Theme picker shows panel / text / accent swatches per
  preset. Nord's dim text is readable now.
- **Instance numbering**: nodes that share a title show "#1", "#2"... in the header, MIDI map, output
  window titles and background-preview labels. Display only, patches are unchanged.
- **Comment node**: hover it and start typing (or Enter / double-click) to edit, drag the bottom-right
  corner to resize, font size Small / Normal / Large / Extra Large.
- **Macros**: Macro Slider, Macro Bipolar Knob, Macro Toggle, Macro Trigger, Macro NumBox (min / max /
  step), Macro Radio Selector (2-8), Macro Step Gate (8 steps on the transport). All controls live in the
  node body and can be MIDI-learned there. New "Macros" category (Macro Knob and Macro XY moved in).
- **Keyboard** (Notes): on-screen piano plus laptop typing while hovered (Z row / Q row), octave,
  transpose, velocity, snap to the global scale.
- **Velocity to CV** (Analysis) and **Note Switcher** (Notes, 4 note inputs on a beat / seconds clock
  or manual).
- **Drift** (Modulators): Ornstein-Uhlenbeck random walk with speed, stray, momentum, home, range,
  depth, tempo quantize and smoothing. Upstream's version learns a landscape from your hand moves
  (its MovementStats engine, not in Turbo); this one keeps the same physics around a fixed home.
- **Audio Meter** (AudioUtility): stereo RMS + peak bars, peak hold, max-peak readout, clip latch
  (click to reset); measures even with its output unconnected.
- **Blend / Layer Stack**: Anti-Erase mode (keep A only where B is opaque), index 31, old patches
  unchanged.
- **Transform (geometry)**: pivot x / y / z for rotate and scale.
- **Material**: UV wrap mode (clamp / repeat / mirror) applied to every map.
- **Phaser**: feedback knob (-0.9..0.9, default 0.5) around the allpass cascade.
- **Flanger**: ~7 kHz damping on the feedback path (repeats darken like a BBD).
- **Sampler**: loop xfade (0-250 ms, default 8 ms, equal power) removes the wrap click.
- **Browser**: star favourites in all four modes (Modules gets a FAVOURITES section on top), filter
  (All / Favourites / type) and sort (name / type / folder / favourites first, asc / desc) in Samples,
  Media and Plugins; right-click a row: favourite, Show in Explorer, copy path. Saved in
  `%LOCALAPPDATA%\Infinite\Infinite.browserfavorites`.
- **3D viewports**: numpad-style keys while hovering (1 front, 3 right, 7 top, Ctrl for the opposite
  side, 0 three-quarter view).

### Not ported
- **Moves**: it projects a gesture onto the principal components of your recorded hand movements,
  which needs upstream's MovementLog / MovementStats engine. Left for the prediction-engine port.

## 0.38.1-turbo (2026-09-26)

### Fixed
- **MIDI faders jumping** (e.g. nanoKONTROL on the Super Mixer): the mapping was linear in dB while
  the on-screen fader uses the console taper (unity at 75% of throw). MIDI now follows the widget's
  taper (dB faders and knobs, log frequency knobs), so hardware and screen move together.
- 7-bit controller jitter: a one-step move that reverses direction is ignored, and each mapping glides
  to its target ("smooth" in the mapping editor, default 25 ms, 0 = off). Saved with the patch
  (`midismooth` line, older patches load with the default).

## 0.38.0-turbo (2026-09-26)

### Added
- **MIDI learn for every parameter** that has a CV pin (sliders, knobs, buttons, toggles,
  dropdowns, pads):
  - Ctrl+M or VIEW > MIDI learn mode: the node widgets lock, click a parameter (the one nearest
    the click in that node), move a control - mapped. Stays in learn mode for the next one; Esc
    cancels / exits. Pins show the state: orange ring = learnable, pulsing = waiting, green = mapped
    (bright when MIDI arrives).
  - Right-click a parameter's pin: MIDI learn / learn again / clear, and the mapping editor.
  - A mapping is device + channel + CC or note, so sources never mix. Identical controllers get
    distinct keys ("name", "name #2", ...). "any device" / "any channel" make a patch portable to
    another machine or controller.
  - Per mapping: continuous (knob/fader), momentary (hold) or toggle (press on/off), invert, soft
    takeover (no jumps), and min/max in the parameter's own units - editable any time.
  - VIEW > MIDI map...: connected devices, rescan (controllers plugged in later), every mapping with
    live activity, edit, learn again, show node, delete.
  - Values are written only when new MIDI arrives, so the mouse still works on mapped sliders; a
    wired modulation cable wins over MIDI. Saved with the patch (`midimap` lines).
- **VMPC plays the clip's soundtrack** on a new "audio" output (wire it to Audio Out or a mixer),
  in sync with the picture (the audio is the clock while it plays), with trim, speed/reverse and
  loop modes; "play audio" and "audio volume" controls. Clips without an audio track stay silent.

### Changed
- MIDI inputs with the same name get separate ids ("#2", "#3"); the first keeps the old id, so
  MIDI CC / Trigger nodes saved before keep working.

## 0.37.0-turbo (2026-09-25)

### Menus
- **Categories reorganised** (node browser, search popup, spawn menu), from what each node does:
  Source (Text joined it), **Video** (Video, VMPC, Video In, Syphon/Spout In), Effects (Resynthesize
  joined), **Utility** (Comment, Group, Null, Viewport), **Audio In / Out** (Audio In, Audio Out,
  Audio File), **Audio to Visual** (Audio Texture, Audio Color Ramp, Audio Displacement, Audio
  Ribbon - the old 2-node "Audio"), Audio Mix & Routing, Audio Effects & Plugins, Synths & Samplers,
  Notes & Sequencers, **Prediction**, Modulators (generators), **CV Tools** (Math, Compare, Invert,
  Range to Range, Smoothing, Mod Depth, Null Modulator, CV to Pitch), **Analysis** (Audio/Image
  Analyze, Audio to CV, Note to CV, Palette), **MIDI & OSC** (MIDI CC/Trigger, OSC Receive/Send,
  OSC to CV). Node type names are unchanged; old patches load and re-file their nodes.

### Added
- **Range mapper** per modulation binding (right-click a modulated parameter): in min/max with
  "capture" from the live signal, out min/max in the parameter's units, invert, reset. Saved in the
  patch (trailing tokens of the `mod` line; older builds ignore them).
- **Color markers** for nodes (right-click > Color marker): coloured band + frame, 8 colours,
  applies to the whole selection, saved in the patch (6th token of `flags`).
- **OSC to CV** (MIDI & OSC): 8 channels, each an address + argument index, input range, invert,
  smoothing, and learn (arm, move the control, the next address is assigned). 8 CV outputs.
- **Predictive Notes / Quantize / Velocity / Rhythm**, ported from upstream Infinite
  (NoteModel, NoteTheory): learn from a note chain and play or correct in that style. Their shared
  cross-session learning pools are saved in `%LOCALAPPDATA%\Infinite\prediction`.

### Changed
- **Noise**: five 4D types in the style of TouchDesigner's Noise TOP - Simplex 4D (new default),
  Perlin 4D, Ridged 4D, Turbulence 4D, Billow 4D. The image is a slice of 4D noise and time moves
  along the 4th axis (speed), so it morphs in place instead of scrolling sideways. New controls:
  translate x/y, drift x/y (optional scroll), z, rotate, exponent; octaves, lacunarity, gain, warp,
  contrast, brightness, seed and rgb noise apply too. The original 2D types are unchanged.

### Fixes
- **VST3 plugins now see the host transport** (play/stop, tempo, time signature, bar and beat
  position). Tempo-synced and sequenced effects (Glitch 2, Effectrix, synced delays/LFOs) used to
  read "stopped, no tempo" and pass the audio through dry.
- **OSC**: the listener only bound 127.0.0.1, so controllers on the LAN (phone, tablet, another
  PC) never reached it; it now binds all interfaces. Two OSC nodes on the same port fought over the
  socket (the second got nothing); one shared listener per port now. Bundles and every numeric
  argument (f, i, d, h, T/F) are decoded. OSC Receive's low/high accept any range (0..127, -1..1),
  it can pick an argument, and it shows the raw value and the last address received.

## 0.36.0-turbo (2026-09-25)

### Fixes
- **Fullscreen editor flicker** with a fullscreen output window: the editor window is now one
  pixel taller than the monitor (so it is never promoted to exclusive-style presentation), and
  output windows only re-assert "topmost" when they actually lost it (it was every 0.5 s).
- **Looper recorded late**: takes are shifted by the interface round-trip latency (input +
  output latency reported by the driver + one buffer), overdub too, and a manual offset
  (-100..+300 ms) sits on top. The node shows the compensation in use.
- **Audio Analyze** only accepted an Audio File node. Its single input is now an "audio" pin
  that takes any audio cable (synth, VST, Audio In, mixer, Audio File) and wins over the live
  input; old patches with an Audio File load unchanged.

### New node
- **VMPC** (Source): the MPC for video clips, a VJ clip launcher. 16 pads with one video each, hit
  with the mouse, a CV pin per pad or the note input (base note 36). Per pad: one shot, gate
  (loops while held) or loop toggle, trim in/out, speed (negative = reverse). Output: the clip of
  the last pad hit (transparent when nothing plays, or hold the last frame). Decoders stay open
  per pad, so a hit only seeks. Silent (use a Video node for a soundtrack). Load video / load
  folder go through the non-blocking dialogs.

### Changed
- **Layout**: scale is anchored at the layer centre (x/y place the layer at scale 1); fit / fill
  also work with a custom size.
- **Super Mixer**: input gain knob per channel (+/-24 dB, pre-EQ, CV pin). It is the first knob
  row, so CV mappings made on the Super Mixer's knobs in 0.33-0.35 move by one row.
- **Projection**: transparent outside the warped image (option, on by default) and per-edge edge
  blend with width (0..1 of the image, default 0.30), S-curve and gamma, for overlapping
  projectors. Blend mode **alpha** (default: the edge fades out, for real blending in video
  mapping) or **black** (darkens RGB). **Antialiased outline** (on by default, 0.5-8 px): the warped
  border fades over a few pixels instead of a jagged triangle edge.
- Output windows composite the image's alpha over black, so transparent and alpha-blended edges
  show as a fade on the projector.

## 0.35.0-turbo (2026-09-25)

### Added
- **Canvas preview** (TouchDesigner-style): one node's output is drawn behind the node canvas,
  under nodes and cables. Ctrl+Shift+B toggles it for the selected node; also in the node's
  right-click menu (*Show behind the canvas*) and in the OUTPUT WINDOW section of Output /
  Projection / Layout params. **VIEW > Output behind the canvas**: scaling (fit / real pixels 1:1 /
  fill / stretch), darken veil, node card opacity, grid on/off. Geometry nodes go through Render 3D.
- **Fullscreen UI**: F11 in the editor switches the main window to borderless fullscreen on its
  current monitor (no display mode change) and back, restoring size and maximized state. Output
  windows keep their own F11.
- **VIEW menu**: side panel, fullscreen UI, canvas preview settings.

### Fixes
- **File dialogs no longer pause Infinite.** Open/save dialogs (samples, images, video, models,
  folders, patches, recording destination) ran on the render thread and froze the transport,
  sequencers, video and output windows until closed. They now run on their own thread; the result
  is applied at the start of a later frame (node-bound results are dropped if the node was deleted).
- **Dialogs open in front of the editor**, also in fullscreen: every dialog is owned by the main
  window (it used to open behind a fullscreen editor, reachable only with Alt+Tab). The editor
  ignores input while a dialog is up; playback and outputs keep running.
- Save in the "Unsaved Changes" prompt waits for the Save As dialog before continuing.

### Changed
- The top-bar SEARCH button is now **PANEL**, highlighted while the side panel is open; Ctrl+B
  toggles the panel.

## 0.34.0-turbo (2026-09-25)

### Fixes
- **Buffer underruns.** The render thread was registered with MMCSS "Games" at HIGH priority and
  competed with the JUCE audio thread; the call was removed (the audio thread keeps its own
  priority, FTZ/DAZ stay on).
- **Kontakt / Native Instruments not found.** The VST3 folder scan only accepted `.vst3` bundle
  folders; single-file `.vst3` plugins (how NI installs Kontakt and others) are now found too.
  Run `Rescan plugins` once.
- **MPC**: pad volume, pitch, pan and trim are drag fields, so double-click (or Ctrl+click) lets
  you type the value.

### Added
- **Build version in the app**: window title and FILE menu show the version and build date
  (`INFINITE_TURBO_VERSION` comes from `project(... VERSION)` in CMakeLists.txt).
- **MPC trim in/out** per pad: drag the handles on the waveform or type the start/end; saved in the
  patch; loading a new sample resets it.
- **OUTPUT WINDOW panel** in the parameters of Output, Projection and Layout: open, size in
  pixels (W/H + presets + source size), scaling mode, fullscreen, close. The right-click menu
  still has the same options.
- **Layout** node (Compositing): canvas of exact pixel size with 8 image inputs, each placed at its
  source's real pixel size (or a custom width/height), x/y in canvas pixels, scale, opacity,
  visibility; miniature editor (click to select, drag to move); 1:1 / fit / fill / centre;
  background colour + alpha; CV pins on x, y, scale and opacity of every layer.
- `GLUtil::DrawFullscreenQuad()` for nodes that composite into sub-rectangles.

### Build
- `build-windows.bat` now only compiles the test build (`build\windows-vs2022\Release`);
  `build-windows.bat pack` creates `dist\` and the ZIP.

## 0.33.0-turbo (2026-09-25)

### Fixes
- **VST instruments never received notes.** The topology builder calls the slot-aware
  `AudioNode::SetNoteInbox(slot, ...)` with the node's real note-pin slot, and the default only
  forwarded slot 0. The Plugin node keeps audio at slot 0 and notes at slot 1, so DecentSampler,
  Kontakt & co. got nothing (same for Spectral Synth and Wave Terrain). The default now forwards
  any slot (single-note-pin nodes only get one call); Note Merge still overrides it.
- **VST3 scan and Native Instruments plugins**: a plugin that describes itself and then crashes
  while unloading at exit is now accepted (the scanner also exits without running teardown);
  the scan timeout went from 30 s to 180 s and a timeout no longer blocklists the plugin. The
  blocklist file moved to `PluginVST3Blocklist-v4.txt`, so plugins wrongly blocked by R31a are
  retried. VST2-only plugins (`.dll`) are still not supported.
- Save dialog: default extension `.inf` (same format/extension as upstream Infinite),
  case-insensitive check.

### Output windows
- Background and letterbox bars are black (were grey).
- Node menu > Output window > **Size (pixels)**: exact drawable size (W/H + Apply, presets, source
  size), correct under Windows display scaling.
- Output window > **Scaling**: Fit (letterbox), **Real pixels 1:1** (centred), Fill (crop), Stretch.

### New nodes
- **MPC** (Synths): 16 sample pads, modes one shot / gate (hold) / loop (toggle), note input
  (base note 36), a CV pin per pad, load sample / load folder, per-pad volume/pitch/pan, master.
- **MPC Out** (AudioUtility): one pad's own stereo output from an MPC.
- **Looper** (AudioUtility): REC / PLAY / DUB / CLEAR (all with CV pins), length in bars,
  sub-bar or free, sync to the bar grid, forward / reverse / ping-pong, loop on/off, thru and level.
- **Super Mixer** (AudioUtility): 16 channels, fader, pan, mute, solo, 3-band EQ per channel,
  master fader; every control has a CV pin.
- Engine: `kAudioMaxNodeInputs` 8 -> 16; the PDC scratch buffer is heap-allocated once per audio
  thread instead of a 1-2 MB `thread_local` array reserved for every thread in the process.
- `INode::ResolveAudioTaps()`: called before each topology publish (used by MPC Out).

## 0.32.1-turbo (2026-09-24)

### Performance
- **Link table rebuild** (every frame): owner lookups go through maps built once per frame
  instead of scanning all nodes with a `dynamic_cast` for every connected input (was O(N^2)
  casts per frame; now O(N)).
- **Shader binary cache**: linked programs are stored in
  `%LOCALAPPDATA%\Infinite\shadercache\<driver hash>\` and reloaded with `glProgramBinary`,
  so opening a patch no longer recompiles every node shader. A driver update uses a new folder;
  a rejected binary falls back to compiling. Disable with `INFINITE_SHADER_CACHE=0`.
- **MMCSS**: the render/UI thread registers as a "Games" task (`avrt`), for steadier frame
  pacing under background load.

### Not changed
- Spout receiver: the first `ReceiveTexture()` call only connects/checks the sender and does not
  copy; removing it could break new-frame detection. Left as is.

### Tests on Windows
- New skill `.claude/skills/run-turbo-tests` (PowerShell driver) replaces the macOS hygiene
  driver: build + screenshot + the self-test suite, results in `build\test-results\`
  (`summary.txt`, one log per check, `screenshot.png`). Detects `[CRASH]` (with hex exit code),
  `[HANG]` (timeout kill) and `[EMPTY]`.
- `test-windows.bat` at the repo root: `-Quick`, `-Only A,B`, `-SkipBuild`, `-Config Debug`,
  `-ShotOnly`.
- `known-baseline.md` lists the checks that fail by design.
- The other skills now point to `test-windows.bat` / `/run-turbo-tests`; the macOS `.sh` drivers
  were removed.

## 0.32.0-turbo (2026-09-24)

First Infinite-Turbo cut, on top of the Windows R31a port. Branch `turbo/windows-only`.
Nothing here has been compiled with MSVC yet: the changed sources were syntax-checked with a
MinGW cross compiler; the first `build-windows.bat` run is the real test.

### Windows-only
- Removed every macOS path: `Platform.mm`, `PluginVST3.mm/.h`, `PluginHandleInternal.h`,
  `scanner_main.mm`, `AudioFileWriter.mm`, CoreText `TextNode.cpp`, `external/syphon`,
  the Steinberg `external/vst3sdk` submodule (JUCE hosts VST3 on Windows), plist /
  entitlements / .icns / iconsets, `package.sh` (DMG), `website/`, the GitHub Pages workflow,
  macOS crash reports and the macOS-only skills `ship-infinite` and `plugin-host-hardening`.
- Resolved all `#if defined(__APPLE__)` / `#if defined(_WIN32)` blocks to their Windows branch
  (14 files).
- `CMakeLists.txt` rewritten for MSVC only: `/MP`, Release `/Oi /Ot /Gy /GS-`, `/OPT:REF /OPT:ICF`,
  `/Zc:__cplusplus`, option `INFINITE_ENABLE_AVX2` (ON), option `INFINITE_ENABLE_LTCG` (OFF).

### Name
- Executable `Infinite-Turbo.exe`, window title `... - Infinite-Turbo`, version resource 0.32.0.
- Package `dist\Infinite-Turbo-Windows-x64(.zip)`.
- User data stays in `%LOCALAPPDATA%\Infinite` so settings, plugin index, blocklist and models
  from R31a keep working.

### Performance and correctness fixes
- **Audio: denormal protection was OFF on Windows.** `AudioEngine::Process` guarded FTZ/DAZ with
  `__x86_64__`, which MSVC never defines. Reverb/filter/delay tails decaying into denormals could
  cost 10-100x CPU. Now always set.
- **Video decoder** (`PlatformWindows.cpp`): read-ahead cache in the playback direction, `grab()`
  for small forward jumps instead of keyframe seeks, reverse playback decoded in batches (one seek
  per batch instead of one per frame), D3D11VA/DXVA hardware decode requested (disable with
  env `INFINITE_VIDEO_HWACCEL=0`), frame buffers swapped/recycled instead of copied, BGR->RGBA
  CPU conversion removed.
- **Video / camera upload**: BGR uploaded directly (GPU swizzle), texture storage allocated only on
  size change (`glTexSubImage2D` otherwise). Video node now reports a real `TextureRevision`, so a
  paused clip no longer forces every downstream filter to re-render.
- **Camera**: requests MJPG (30 fps at 720p/1080p on most USB webcams), 1-frame capture buffer,
  mirror + flip in a single pass.
- **Remove Background**: no more synchronous full-resolution `glReadPixels`. Source is copied on the
  GPU (paired with its mask), a downscaled copy (max 512 px) is read back through PBO + fence,
  and the worker always processes the newest frame (the old queue could process stale frames).
  GPU copies are recycled, bounded even when inference fails.
- **Uniform-location cache**: all ~340 `glGetUniformLocation` call sites are transparently served
  from a per-program cache (`OpenGLHeaders.h` / `GLUtil.cpp`); re-link and delete invalidate it.
- **Frame limiter**: high-resolution waitable timer (`Platform::PreciseSleep`) instead of
  `sleep_for` + spin; frees most of a CPU core at capped FPS.
- **VSync with output windows**: exactly one VSync wait per frame. With projector windows open the
  projector owns VSync (tear-free projection paced by its display); otherwise the editor does.
- **Node/link lookup**: `FindNodeByIndex` / `FindLink` use validated position hints (O(1) hits).
- **OpenGL context**: requests 4.6 core (falls back 4.5 > 4.3 > 3.3 > 3.2); logs renderer and
  version to `Infinite.log`. Shaders unchanged (`#version 150`).
- File names in titles/menus: `find_last_of('/')` -> `find_last_of("/\\")` (12 sites) so Windows
  paths show just the file name.

### Setup scripts
- `build-windows.bat`: args `debug`, `fresh`, `nopack`; configures only when needed (no forced
  `--fresh` every build), all cores (`/m` + `/MP`), refuses to build while the app is running,
  no submodule step.
- `install-dependencies.bat`: vcpkg binary cache in `%LOCALAPPDATA%\InfiniteBuild\vcpkg-binary-cache`
  (reinstall/second checkout reuses compiled OpenCV etc.), no submodule step.
- `run-windows.bat`: new exe name, Debug fallback, above-normal priority, pauses only on error.
- `install-runtime.bat`: reuses models bundled in the ZIP, one checksum routine.
- `diagnose-windows.bat`: new names, reads any blocklist version.
- `scripts/package-*.ps1` and CI workflow renamed and cleaned of macOS files.

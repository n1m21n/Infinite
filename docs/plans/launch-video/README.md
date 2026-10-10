# Infinite launch video: production brief (v2)

Status: done (2026-10-09, owner). v2 brief written 2026-09-26; the film is finished.
Owner-locked: 145 BPM · dynamic palette (Claude's call) · tool-switching pointer · real equations,
code and probabilities · ending "A DAW for **you**" · owner's artworks + screenshots, shown premium.

## 1. The idea in one line

Someone moves across a blank canvas and finds a scientific revolution: one dot becomes a universe,
the universe becomes a network, and the network turns out to link everything (atoms, paint, cells,
maths, sound). Infinite is the canvas where you patch all of it together.

**Through-line:** the **pointer** appears in every scene. It is the viewer's hand, the continuity
anchor and the focus guide. It **switches between Infinite's real tools** (and a spaceship) according to what
it's doing. The **cables** are the argument: everything is connected.

## 2. Format

| Item | Spec |
|---|---|
| Length | 35 bars at 145 BPM = **57.9 s** |
| Master | 3840x2160, 60 fps, ProRes 4444 layers → ProRes 422 HQ master |
| Deliverables | 16:9 master (H.264 + HEVC), 9:16 reframe, 1:1 reframe, 6 s loop teaser |
| Colour | Rec.709, 10-bit, tagged `bt709` |
| Loudness | -14 LUFS integrated, -1 dBTP |
| Shot length | ≤ 1 bar (1.655 s); most are 1-2 beats |
| Narration | none |

### Tempo contract: 145 BPM

| Unit | Seconds | Frames @60 |
|---|---|---|
| Bar | 1.6552 | 99.3 |
| Beat | 0.4138 | 24.83 |
| 1/8 | 0.2069 | 12.41 |
| 1/16 | 0.1034 | 6.21 |

The beat is not a whole number of frames, so **every event is placed at `round(absolute_time × 60)`**,
always computed from bar 1 and never accumulated. The worst error is 8 ms, with no drift.
`timeline.json` holds bar/beat → frame; if the BPM changes, everything re-times itself.

## 3. Branding system

Source: `website/style.css`, `src/core/CategoryColors.cpp`. Reference feel: Anthropic (warm paper,
editorial type), Nothing (mono, dot precision), Apple (one idea per frame), 1X NEO (soft, tactile).

### 3.1 Dynamic palette (it changes with the story, never at random)

```
PAPER ─► NIGHT (deep field) ─► NIGHT (nature) ─► PAPER-INNER ─► NIGHT ──► SPECTRUM ─► PAPER ────► PAPER
Act I     Act I bar 3-8          Act II            Act III        Act IV    Act V       Act VI      Act VII
ball +    hard cut to black 1/8  Zoom dives into   carousel of    play /    11 category screens +   the ball
splatter  after the bang; pop-in atoms→flower      artworks       the drop  colours     laptop      again
```

| Palette | Background | Primary | Secondary | Focus accent | Triggered by |
|---|---|---|---|---|---|
| Paper | `#FBFAF6` | ink `#1F1D1A` | `#5A534C` | terracotta `#C2593F` | open |
| Night | app window `#0A0B0F` | `#EEF0F6` | `#9198AD` | accent `#6FB4FF`; cables in their dark-theme colours; star colours §3.4 | **Hard cut to black** an 1/8 note after the bang (bar 2 beat 2.5), then the stars pop in (owner note 2026-09-26) |
| Paper-inner | `#F3F0E7` | ink | `#857C74` | terracotta | zoom-out whip |
| Night + track | `#0A0B0F` | `#EEF0F6` | | terracotta play button | the play click |
| Spectrum | Paper | category colour per beat | | the category colour itself | counter lands |
| Paper (final) | `#FBFAF6` | ink | | terracotta "you" | |

Transitions are always motivated: a tool click, a dive or a drop. Never a crossfade.

### 3.2 Type system

| Role | Font (all OFL, free) | Use |
|---|---|---|
| Display | **Geist** (variable 300-800) | Headlines, the seven words, "A DAW for" |
| Editorial accent | **Instrument Serif** *italic* | The "scientific revolution" voice: single words that need gravitas (e.g. *connected*) |
| Data / code | **Geist Mono** | Equations-as-code, counters, weights, node names, captions |
| Human hand | **Caveat** | The cycling end word, notebook annotations (as on the site hero) |

**Emphasis kit** (used only where the eye must go, at most one per shot):

| Mark | How it animates | Where |
|---|---|---|
| **Highlighter** | Terracotta at 28% opacity, marker swipe left→right in 6 frames, slightly ragged edge, sits behind the text | Key word of a line, a probability that "wins", the category name |
| **Hand underline** | A Caveat-style stroke drawn in 5 frames, slight overshoot past the last letter | The seven words as each lands; "you" |
| **Bold morph** | Variable-font weight 400 → 700 over 4 frames (Geist is variable, so the width reflows smoothly) | The winning term in an equation; "200+" |
| **Hand circle** | A loose ink ellipse drawn around a detail, 8 frames | Screenshot details, one knob |

Tracking: display type is -2%, mono is +2%. Numbers use tabular figures, so counters never jitter.

### 3.3 Other rules

- **Glow is palette-bound** (owner note 2026-09-26). Paper scenes keep the site's zero-glow rule: light is ink developing or contrast, never bloom. **Night scenes may use light**: additive star dots, soft galaxy cores, diffraction spikes, prismatic edges and iridescence (§3.4). Still no lens flares and no generic bloom over the whole frame.
- **Ease:** `cubic-bezier(0.16, 1, 0.3, 1)` (the site's) for all moves; snaps get a 2-frame overshoot.
- **Cables:** Bezier with sag like the app's; real cable-type colours. Light theme: Stream `#373E4E`, Mod `#D7780A`, Audio `#1E64E6`, Note `#14963C`, Palette `#AA28B4`. Dark: Stream `#E6EBF5`, Mod `#FFBE5A`, Audio `#5A96FF`, Note `#5ADC82`, Palette `#DF6BE8`.
- **Category colours:** Source `#4ADE80`, 3D `#38BDF8`, Compositing `#818CF8`, Effects `#FACC15`, Modulators `#A3E635`, Prediction `#22C55E`, Macros `#FC963C`, Utility `#A3A9BA`, Notes `#4CD964`, Synths `#6992F6`, AudioEffects `#53C7E3`.

### 3.4 Colour and light at night (owner references, 2026-09-26)

The owner's 8 mood refs are in `~/infinite-launch-video/assets/refs/` (outside the repo). What they share: black ground, thin
bright light, colour appears only as **chromatic dispersion** (rainbow fringes on an edge), chrome, and iridescence. It is never a flat fill.

| Ref | Look | Used in |
|---|---|---|
| `ref01_warp-streaks` | long thin light streaks converging, with RGB-split tips | Bar 8 rewiring accel; bar 15 strobe recap; Act IV drop |
| `ref02_lily-prismatic` | white petals with rainbow-fringed edges, fine dust | Bar 12 flower bloom |
| `ref03_atom-prismatic` | orbital rings as thin prismatic lines around a white core | Bar 9 atoms/molecules |
| `ref04_light-filaments` | braided glowing threads, flowing | Cables at night; bar 10 ink-in-water currents |
| `ref05_holo-marble` | holographic marbling, soft pastel iridescence | Bar 10 ink/paint |
| `ref06_chrome-liquid-stars` | chrome droplets + star field | Act I bang → deep-field bridge; bar 10 |
| `ref07_iridescent-cells` | soap-film cells, thin-film rainbow rims | Bar 11 cells |
| `ref08_chrome-membrane` | chrome membrane / network skin | Bar 11 Physarum network |

**Deep-field palette (Act I night, built):** white `#F4F6FF`, blue-white `#B9CCFF` (spiral arms), warm `#FFD9A8` (bulges),
orange-red `#FF9A6A` (distant galaxies), pink `#FF86B5` (star-forming knots), teal `#8FE3E0` (rare field stars). The nebula haze is
magenta/blue/teal at 13-35% alpha. Stars are **crisp animated dots** (r 0.9-2.6 px at 4K, additive, twinkling). Motion streaks are
capped at about 2× the radius, so they never read as splatter. About 16 bright stars get 6-point JWST diffraction spikes plus a short horizontal spike.

**Prismatic rule:** dispersion lives on edges only (petal rims, orbit rings, cell walls, streak tips): R/G/B offset by 1-3 px at 4K along the edge normal. Interiors stay white or chrome.

## 4. The pointer: a tool-switching character

The real Arrange tools (`src/main.cpp:1436`), drawn with **Tabler icons** (MIT; the same set the app
draws in `Tabler::Draw*`). **Each tool does in the video what it does in the app.**

| Tool (key) | Icon | Job in the video |
|---|---|---|
| Select (A) | arrow | First appearance; clicks the stars; presses play |
| Spaceship | line-drawn ship; the arrow's triangle glides into it | Fast travel between touch points; its trail becomes the first cable |
| Pencil (P) | pencil | **Draws cables** between stars |
| Blade (B) | scissors | **Cuts cables** in the rewiring scene, and then Pencil reconnects |
| Range (R) | marquee | Box-selects a star cluster, which becomes the network |
| Zoom (Z) | magnifier | **Clicks into a node → iris dive into the microscope** (Act II; already night); Alt-click zooms back out |
| Hand (H) | hand | Flicks the carousel; pans the galaxy |
| Trim (T) | trim bracket | Scrubs the waveform before play |

Tool-swap micro-animation: the old icon scales to 0.6 and fades over 3 frames while the new one pops
from 0.6 → 1.05 → 1.0. A tiny mono label (`B`, `Blade`) flashes beside the pointer for 6 frames,
like a shortcut hint. SFX: a dry key tap.

## 5. Focus grammar

1. **One subject per shot**; everything else drops to 35% or out of focus.
2. **One accent-coloured thing at a time** means "look here" (terracotta on paper, blue/terracotta at night).
3. **Eye-trace match cuts:** the subject at the end of shot N sits where the subject of shot N+1 starts.
4. **The pointer leads:** action happens where the pointer is or where it's heading.
5. **One emphasis mark per shot** at most (highlighter, underline, bold or circle).
6. **Camera never fully still:** a slow 1-2% push on every shot.

### 5.1 Micro-maths on nature scenes (owner note 2026-09-26)

Whenever nature is on screen (the deep field, atoms, ink, cells, flower), tiny equations and numbers float **at the coordinates of the motion**:

- Geist Mono at about 30 px (4K), `#9198AD`, 45-90% alpha. A 1 px leader line and a 9 px ring mark the anchor point.
- Anchored to a moving point and **live**: coordinates tick (`x 1284.6  y 603.1`) and angles advance (`theta 352.2 deg`).
- Types on over about 12 frames, staggered 0.11 s apart. At most about 8 per frame. They never overlap the subject's centre.
- The content is real for the thing shown: the log spiral `r = a*exp(b*theta)`, `w(r) = v/r`, `v_rot ~ 212 km/s` and de Vaucouleurs' law for galaxies. Bond lengths and angles for molecules (C-C 1.54 Å, 109.5°). Navier-Stokes terms and vorticity for ink. Division times and Physarum's `dD/dt = |Q| - D` for cells. The golden angle 137.5° and Fibonacci phyllotaxis `r = c*sqrt(n)` for the flower.
- Geist Mono has no Greek glyphs. Spell the letters out in ASCII, or set that single glyph in Instrument Serif.

## 6. Shot list

Legend: **P** = pointer tool/action, **FX** = sound, **μ** = microdetails. Times are bar numbers and seconds.

### Act I: Genesis (bars 1-8 · 0:00-0:13.2) · palette: Paper → Night (hard cut an 1/8 note after the bang)

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 1 | Empty paper. A **ball**, a shaded matte ink sphere (r 84 at 4K) with a soft contact shadow, breathes on beats 2 and 4 | none | paper hush, 2 sub heartbeats, **quiet tension click on every beat** (owner likes it; keep it in the mix) | 2% paper grain; the ball trembles through bar 2 beat 1 |
| 2 | Ball collapses (4 frames), 1 silent frame, then **big bang**: an ink splatter | none | 1/16 click roll into the sub boom + air burst on beat 2 | Curl-noise flow; droplet sizes vary; 180° motion blur. **Built and approved as a splatter** |
| 2½ | **Hard cut to black** an 1/8 note after the bang; black holds for about 1 beat | none | soft sub bloom + wide air on the cut | Pure black, no nebula yet |
| 2¾-3 | Stars and galaxies **pop in** as a wave from the centre (beat 3 of bar 2 → bar 3). Each dot pops 0 → 1.7× → 1× with a white flash, each galaxy blooms from its core outward with a slight spin-in, and the spiked bright stars pop together on bar 3's downbeat. The result is a **JWST-style deep field**: a hero spiral, a face-on spiral, an edge-on, 2 ellipticals, a small spiral, about 16 tiny red distant galaxies, about 2,600 field stars, 16 spiked bright stars and faint nebula haze | Hand pans slightly | granular shimmer | 3 parallax layers; galaxies slowly rotate (differential ω); the micro-maths labels type on (§5.1) |
| 4 | Select appears, hesitates, then **glides into the spaceship** and flies | Select → Ship | tool taps, then whoosh panned with x | Pointer in `#EEF0F6` with a night outline and no shadow; the ship trail is a light line |
| 5-6 | Seven touch points, one per beat: **light · sound · geometry · motion · physics · art · music** (8th beat holds). Each is a star in the deep field | Ship arrives → flips to Select → clicks star → word | 7 plucks climbing the track's scale | Word in Geist 600 `#EEF0F6`, tracking tightens +40 → -2; the clicked star flares its spikes; terracotta hand underline; previous words drop to 35% |
| 7 | Cables drawn between the seven stars | **Pencil** drags each cable | cable zip + pin snap per link | Dark-theme cable colours drawn as light filaments (ref04); pin dots at both ends; 2-frame snap overshoot |
| 8 | Network **rewires** across the galaxy, faster and faster, then Range-selects one cluster and **Zoom clicks into a node** | Blade cuts → Pencil reconnects (alternating, accelerating) → Range → Zoom | snip/zip roll into riser; the Zoom click lands on bar 9's downbeat | Rewiring is preferential attachment (organic); the accel reads as warp streaks (ref01); the key-hint label flashes each tool swap |

### Act II: Everything is connected (bars 9-15 · 0:13.2-0:24.8) · palette: Night (nature)

**One graph, many materials:** the node positions and cable topology stay fixed on screen while the matter changes underneath.
Every cut is a match cut, and that persistence is the interconnection feeling. The Zoom click on bar 9 is an **iris dive into the clicked node**
(we are already at night, so there is no inversion). Rhythm: 1 bar each, then two half bars, then the strobe recap. The drop stays at bar 22 and the total stays at 35 bars.
Every nature bar carries micro-maths (§5.1).

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 9 | **Atoms and molecules** (dopamine): white cores, orbitals as thin prismatic rings (ref03), bonds = cables | Select pokes an atom and the bond vibrates | glassy tick + wobble | Element labels in mono (`C`, `N`, `O`); micro-maths: bond lengths and angles |
| 10 | **Ink / paint in water**: holographic marbling and chrome droplets (ref05, ref06), currents as light filaments (ref04) | Hand drags through it and the pigment follows | wet swirl, low bubble | Stable-fluids sim; microscope vignette + focus breathing; micro-maths: vorticity, velocity |
| 11 | **Cells**: soap-film iridescent cells divide (ref07), then a chrome Physarum membrane network (ref08) | Select taps a cell and it divides | organic pop | Physarum builds real transport networks, the cable metaphor made biological; micro-maths: dD/dt = abs(Q) − D |
| 12 | **Flower bloom**: a white lily/phyllotaxis bloom with rainbow-fringed petal edges and fine dust (ref02). **Real DSP equations (§7) float** among the petals, and cables link their shared terms | Pencil links two equations | chalk tick + chime per link; the bloom unfolds on the bar | Petals open on a golden-angle spiral (137.5°); the linked term bold-morphs. Replaces the standalone DSP-maths bar |
| 13a (½ bar) | **Prediction**: probabilities and weights live-updating | Range selects a distribution | fast data ticks | Markov transition probabilities rolling in mono; the winning probability gets the highlighter; nEff confidence bar filling |
| 13b (½ bar) | **Field code** typing itself, and each line spawns a cable to the shape it moves | none | key taps | Real presets (§7); the cursor blinks at 145 BPM |
| 14 | **Synth knobs** floating, amber modulation cables linking them | Select turns one knob and the linked knobs follow | detent clicks + filter sweep | Knobs styled like Infinite's own node knobs (`audio-node-ui` skill); modulation ring arcs |
| 15 | Strobe recap: atom → ink → cell → flower → probability → code → knob at 1/8 notes, then all layered in one frame, then **Alt+Zoom pulls out** | Zoom (Alt) | stutter of all cues into one hit, then a reverse whoosh | Strobe transitions are warp streaks (ref01); the layered frame is the thesis frame; the palette flips to paper on the zoom-out |

### Act III: What it makes (bars 16-20 · 0:24.8-0:33.1) · palette: Paper-inner

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 16-19 | **Carousel** of owner artworks as live video cards (§8), accelerating: 2 beats each → 1 beat → 1/8 notes | Hand flicks at bars 16, 18 and 19 | whoosh per flick; a card tick on each card change | Cards are 3:4, 20 px radius, premium shadow; a tiny mono caption under each (the node that made it); 1-2% perspective tilt with motion |
| 20 | Carousel decelerates hard and settles on one hero artwork, which scales up to fill the frame | Hand releases | brake tick then air | The hero's dominant colour carries into Act IV |

### Act IV: Play (bars 21-24 · 0:33.1-0:39.7) · palette: Night + track

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 21 | Flat ink waveform (light on night) + terracotta play button | Trim scrubs the waveform, then switches to Select and hovers play | scrub zips, then **silence on beat 4** | Hover: button 1.00 → 1.06; waveform preview flickers under the scrub |
| 22-24 | **Click → the drop**. The waveform draws itself from the real audio | click | real click, then owner's track | Amplitude from the actual track; terracotta playhead; spectrum bars (the FFT) flicker under it |

### Act V: Scale (bars 25-28 · 0:39.7-0:46.3) · palette: Spectrum

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 25 | Counter rolls 0 → **200+** (split-flap), then "**nodes** across" | none | mechanical tick roll, thunk | "200+" bold-morphs 400 → 800; tabular figures |
| 26-27 | **11 categories**, one per 1/8 note pair, each name in its colour over a zoomed screenshot crop of that category's nodes | Select taps each | pitched pluck per category | Category name highlighted in its own colour at 28%; crops chosen from owner screenshots (to tag next session) |
| 28 | All 11 colours collapse into cables converging on one point | none | converge swell | Leads into the screenshot window at the same point (match cut) |

Category counts (repo 2026-09-26, ≈229; re-verify before render): Compositing 36 · 3D 32 ·
Modulators 28 · Effects 28 · Audio Effects 25 · Notes 22 · Synths 15 · Source 13 · Utility 13 ·
Macros 9 · Prediction 8.

### Act VI: The real thing (bars 29-32 · 0:46.3-0:53.0) · palette: Paper

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 29-30 | **Screenshot gallery**: 5-6 owner screenshots as floating app windows (§8) in a slow 3D stack; camera pushes into one, then a focus zoom onto a detail with a hand circle | Zoom tool clicks into a detail | soft window whooshes, UI ticks | Parallax between window layers; each window 1-2° tilt; premium shadow |
| 31-32 | Generic unbranded laptop (Blender), slow orbit; screen = owner's recording | the pointer enters the screen and becomes the real macOS cursor | air + UI ticks | Screen as an alpha matte + corner-pin data; no Apple marks |

### Act VII: Name it (bars 33-35 · 0:53.0-0:57.9) · palette: Paper

| Bar | Shot | P | FX | μ |
|---|---|---|---|---|
| 33-34 | "A DAW for *designers*": the last word (Caveat, terracotta) cycles one per beat: designers → artists → musicians → VJs → scientists → creatives → **you** | none | type tick per word; "you" lands on bar 35's downbeat | Word width animates so "A DAW for" slides smoothly; "you" gets the hand underline |
| 35 | Logo + "Infinite" + macOS · Windows · Linux, then everything shrinks into **the single ink dot** | final Select click | the heartbeat from bar 1 | Last frame == first frame, so it loops seamlessly |

## 7. Equations, code and probabilities (all real, from the repo)

| Source | Shown as |
|---|---|
| FFT (spectral nodes) | X[k] = Σₙ x[n] · e^(−2πikn/N) |
| TPT state-variable filter (`src/audio/DspMath.h:292`) | g = tan(π·f_c / f_s) |
| Biquad (`DspMath.h:341`) | y[n] = b₀x[n] + b₁x[n−1] + b₂x[n−2] − a₁y[n−1] − a₂y[n−2] |
| AR(1) movement model (`src/core/MovementStats.h:80`) | x_{t+1} = φ·x_t + θ + ε |
| Ornstein-Uhlenbeck fit (`MovementStats.h:160`) | dx = θ(μ − x)dt + σ dW |
| DMD "Play Like Me" (`MovementStats.h:266`) | x_{t+1} = A·x_t,  ρ(A) ≤ 1 |
| Variable-order Markov notes (`PredictiveNotesNode.h:17`) | P(nₜ \| nₜ₋ₖ … nₜ₋₁) with live probabilities 0.62 / 0.21 / 0.09 … |
| Exponential forgetting | w = 2^(−Δt / t½) |
| Field (`FieldElementNode.cpp:15,42,45`) | `P.y += sin(P.x * freq + t * speed) * amp` · `Cd = mix(Cd, normCol, blend)` · `heat = (P.y + 1.0) * 0.5` · `P.y += bass * 2` |

Equations are typeset properly (KaTeX/LaTeX to vector); code is set in Geist Mono with minimal syntax tint.

## 8. Owner assets and how they're presented

Inventory (2026-09-26):

| Folder | Count | Formats / sizes |
|---|---|---|
| `~/Desktop/Infinite Selected` | 6 (5 video, 1 png) | mostly 1440x1920 (3:4) @60 fps, one 1920x1920, one 1404x1920 |
| `~/Desktop/Infinite Artworks/Artworks` | 26 (video), **not for the carousel** | 22 at 1440x1920, 2 at 1920², 1 at 4096², 1 at 1920x1080 |
| `~/Desktop/Infinite Artworks/Screesnhots` | 41 | mixed; mostly 2880x1800 (16:10), plus 2048x1610, 1236x1536, 1449x1536 … |
| `~/Desktop/Infinite Artworks/Audio`, `/Patches`, `infinite_output.mov` | not yet reviewed | |

### Artworks → carousel cards
- **One aspect for every card: 3:4** (the native size of 80% of them). Squares and 16:9 clips are centre-cropped to 3:4 with a chosen focal point; the 1404-wide clip is scaled to fill.
- **Carousel source = the 6 in `Infinite Selected` only** (owner, 2026-09-26). `Artworks/` is not for the carousel.
- Fill to 14 cards with ~8 **Claude-generated procedural pieces** (flow fields, reaction-diffusion, fluid, galaxy, differential growth, metaballs, Physarum, particle ink), rendered at 1440x1920, 60 fps, and colour-matched to the 6 so the set reads as one collection.
- Owner clips get the hero slots: the first card, the held cards in bars 16-17, and the bar-20 hero.
- Cards play the **live video**, not stills. Order is by colour and energy so the carousel reads as a gradient.

### Screenshots → premium app windows
- **One frame for everything: a 16:10 window** (the native 2880x1800 size). Other sizes sit inside the same frame with a motion crop (Ken Burns), so the frame never changes shape.
- **Premium shadow stack** (warm, ink-tinted, never grey), plus a 1 px hairline at 6% ink and a 20 px radius:

| Layer | Offset / blur | Opacity |
|---|---|---|
| Contact | 0 / 1 px, 2 px blur | 10% |
| Mid | 0 / 8 px, 24 px blur | 10% |
| Ambient | 0 / 32 px, 80 px blur | 12% |
| Floor (on tilt) | 0 / 60 px, 140 px blur | 6% |

- **Motion:** a slow push (1.00 → 1.06 per shot), a 1-2° 3D tilt, parallax between stacked windows, and a focus zoom to 2-2.5x onto one detail with a hand circle. The shadow grows with the lift.
- 2880-wide sources can take a 1.3x zoom at 4K; beyond that the zoom is capped, or the crop gets a quality check.

## 9. Sound design mechanism

**Event-driven cue sheet.**

```
scene renderer ──emits──► events.json {t, type, tool, x, y, intensity}
                                │
      timeline.json (145 BPM) ──┤ quantise to the 1/16 grid, round to frame
                                ▼
                     sfx_mix.py: sample by type, pan by x,
                     gain by intensity, pitch plucks to track key,
                     sidechain-duck under the track
                                ▼
                     sfx_stem.wav 48k/24  +  owner track  →  loudnorm -14 LUFS
```

| Event | Sound | Synthesised from |
|---|---|---|
| tool swap | dry key tap | short filtered noise + tiny tonal body |
| appear / click | tick, click | noise bursts |
| connect / snap (Pencil) | zip + pluck | noise sweep + Karplus-Strong in key |
| cut (Blade) | snip | two high noise transients 30 ms apart |
| move / flick / cut | whoosh | band-passed noise sweep, panned |
| bang | sub boom + air | sine drop 60 → 30 Hz + noise |
| reveal | granular shimmer | grains of the owner's track (so it's in key) |
| organic | pop, swirl | FM + filtered noise |
| knob | detents + sweep | micro-clicks + resonant filter |
| data / counter | mechanical ticks | clicks with a slight pitch random walk |

All synthesised from scratch: licence-clean, in key, dry and precise.

## 10. Build pipeline

| Layer | Tool | Acts |
|---|---|---|
| 2D procedural (particles, galaxy, cables, fluid, cells) | Python: numpy + skia (vector AA) + moderngl (GPU sims) | I, II |
| Type, equations, UI motion, emphasis kit | skia with variable fonts; KaTeX → SVG for equations | I, II, V, VII |
| Cards and windows with shadows, 3D tilt | skia/moderngl compositor | III, VI |
| 3D: molecule, knobs, laptop | Blender via MCP (Blender must be open with the add-on) | II, VI |
| Edit, grade, encode, loudness | ffmpeg | all |
| Timing | `timeline.json` shared by all | all |

Rules: every scene is a pure `frame(t)`; layers are rendered separately (bg / subject / cables / pointer /
type); motion blur comes from sub-frame accumulation; 10-bit + dither so the paper never bands.
**The work folder lives outside the repo.** Only this brief stays in `docs/plans/`.

## 11. QA gate

| Check | How |
|---|---|
| Timing | Every event is within 1 frame of its grid time (script over events.json) |
| Eye trace | Contact sheet of the last/first frames at each cut, focus point marked |
| Focus | One accent-colour subject per frame (pixel count), and one emphasis mark per shot |
| Aspect | All cards 3:4, all windows 16:10 (script asserts) |
| Type | Safe areas for 16:9 and 9:16; readable on a phone |
| Banding | 400% crop of the paper and night gradients |
| Audio | -14 LUFS / -1 dBTP; SFX never mask the drop |
| Loop | Last frame == first frame |
| Owner review | Low-res preview per act → notes → next act |

## 12. What the owner still provides

| Item | When |
|---|---|
| Track key (tempo locked at 145) and a draft with the drop at bar 22 | before Act IV |
| Final track WAV 48k/24 | before the final mix |
| Screen recording for the laptop shot (native res, 60 fps, ProRes) | before Act VI |
| Logo wordmark SVG (only `press_kit/logos/icon_1024.png` exists) | before Act VII |

## 13. Machine constraints and risks (checked 2026-09-26)

| Constraint | Value | Mitigation |
|---|---|---|
| RAM | 8 GB (M2) | Render frame by frame and stream to ffmpeg; never hold whole clips in memory |
| Free disk | 14 GB | Composite in-process (no per-layer ProRes 4444 files); keep previews in H.264; masters to an external drive if one is available |
| Python | system 3.9.6, only numpy + PIL | Project venv; pip install skia-python, moderngl, scipy, soundfile |
| Fonts | none installed | Download Geist, Geist Mono, Caveat and Instrument Serif (all OFL) into the work folder |
| Render time | 3,480 frames at 4K | Previews at 1080p30; heavy sims (fluid, cells) computed at half-res then upscaled; finals rendered overnight per act |
| Claude can't watch playback or listen | reviews are via frames and analysis | Owner reviews the motion feel and SFX taste on every act preview |

## 14. Next-session start checklist

0. **Proof-of-quality test:** bars 1-4 at 1080p. v1, v2 done; **v3 done 2026-09-26** (ball, splatter, hard cut to black, star pop-in, deep field, micro-maths) in `~/infinite-launch-video/out/act1_bars1-4_v3*.mp4`. Awaiting owner notes.
1. Read this brief.
2. Contact-sheet all 41 screenshots and 32 artworks; tag screenshots by category (for Act V) and pick the 5-6 for Act VI.
3. Check the Python deps (skia-python, moderngl, numpy) and fonts (Geist, Geist Mono, Instrument Serif, Caveat).
4. Build `timeline.json` + the shared renderer skeleton.
5. Render Act I at low res as the first review.

## 15. Build state (2026-09-26): full cut v1 done

All 7 acts rendered at 1080p60 and assembled (`assemble.py`). Deliverables in `~/infinite-launch-video/out/`:

| File | Audio |
|---|---|
| `infinite_launch_58s.mp4` | SFX over placeholder groove |
| `infinite_launch_58s_sfx_only.mp4` | SFX stem only (for the owner's track) |
| `infinite_launch_58s_guideclick.mp4` | SFX + beat click (timing review) |

Decisions and placeholders made while building Acts IV-VII:

| Area | Decision |
|---|---|
| Track | `groove.py` placeholder (A minor, Am F C G); Act IV reads `assets/track.wav` first when the owner drops it in |
| Motion | `lib/widgets.py`: underdamped `spring`, anticipation, `keyed` springs, arcs, overlap/stagger; applied to Acts IV-VII plus Act IV button/pointer |
| UI details | All rounded: knobs, sliders, param lists, LFO/osc/wavetable/EQ/clip cards; Act II prediction bars + nEff slider and the Range marquees rounded too |
| Act V | Node counts and names come from the repo (`src/main.cpp`, `src/core/FilterDefs.cpp`); one widget per category |
| Act VI | Screenshots are owner shots #11 (front) and #1, 2, 12, 33; laptop is drawn in Skia 3D, not Blender; seed and window share a 22 px corner |
| Act VII | Kinetic-type treatments from the refs: select box, stretch, echo stack, extrude, bulge, gradient; "you" in Caveat; brand lockup, then a shrink to the Act I ball (loop) |
| Loop | Last frame is within grain noise of frame 0 (mean diff 0.7/255) |

Re-render one act: `.venv/bin/python render.py actN --height 1080`, then `.venv/bin/python assemble.py`.

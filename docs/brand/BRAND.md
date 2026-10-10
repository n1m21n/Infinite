# Infinite brand book v2

The rules for every brand surface: website, films, thumbnails, social, docs, installers, press, 3D.

| File | What it is |
|---|---|
| `brand.json` | Every value. Edit here only. |
| `brand-book.html` | The visual book with computed figures. Generated; open it next to this file. |
| `brand.css` | CSS custom properties per mode (`data-mode="midnight|mist|paper"`). Generated. |
| `brand.tokens.json` | The same tokens in DTCG 2025.10 format, for Figma or Style Dictionary. Generated. |
| `img/` | The 3D renders used by the book. Generated. |
| `../../art/brand/3d/` | 3D kit: `.glb` files (metres), `kit.blend`, full-size renders. |
| `../../art/brand/fonts/` | Static Geist, Geist Mono and Caveat instances (OFL) for 3D labels. |

Rebuild after any edit:

```bash
python3 tools/brand/build_brand_book.py
```

```bash
python3 tools/brand/build_3d.py            # all 3D; --only node_hero_cutout,logo_3d_cutout for a few renders
```

Every brand file is made by a tool in `tools/brand/`. Run the tool; do not hand-edit its output.

| Tool | Makes | Check mode |
|---|---|---|
| `build_brand_book.py` | `brand-book.html`, `brand.css` (also `website/brand.css`), `brand.tokens.json` | Contrast gate fails the build |
| `build_3d.py` | `.glb` kit, `kit.blend`, renders, Cycles cutouts with transparent ground | `--only` |
| `build_logo.py` | `art/brand/logo/`: SVG, PNG, favicons, `.icns`, `.ico`, Linux icons from the lemniscate formula | `--check` against the shipped icon |
| `channels.py` | One message at all 14 `render.targets` sizes (OG, YouTube, X, LinkedIn, GitHub, README light/dark, Instagram, story, slide, Discord) | Fails on a wrong size |
| `templates.py` | `templates/`: slide deck (+PDF), release email (+PNG), press fact sheet (+PDF), facts read live | Fails on a failed render |
| `frame_shot.py` | Screenshots on the standard frame (section 10.1) into `img/screenshots/` | Warns below 2x |
| `press_kit.py` | `dist/press/Infinite-press-kit-<version>.zip` | Fails on missing logo or fact sheet |
| `brand_lint.py` | Report of off-brand and retired colours (ratchet in `lint_baseline.json`) | Exit 1 if a file got worse |
| `migrate_colours.py` | Applies `colour.retired` / `colour.migrate` swaps; dry run unless `--apply` | |

The book build fails if any colour pair in the contrast gate misses its WCAG ratio. The app's own UI is not covered here: it follows `src/app/ui/design/tokens.json` and the `infinite-design-system` skill. Where the two disagree on a brand surface, this book wins.

---

## 0. One page

| | Rule |
|---|---|
| Idea | **Two ends, one loop.** The mark's warm end (Ember) is the human act; its cool end (Signal) is the structure; Midnight is the space they connect across. |
| Belief | Connection. Anything patches into anything. |
| Line | **A DAW for you.** |
| Grounds | Midnight (show), Mist (explain), Paper (read). One mode per scene or section. |
| Accent | Ember, one look-here per frame, under 10% of its area. |
| Structure | Signal: lines, diagrams, data, focus. |
| Type | Geist, Geist Mono, one Caveat word. Pixel 5x7 only when the machine speaks. |
| Shape | Card 24, media 16, controls are pills. No outlines anywhere. |
| Motion | Four measured springs, three eases. Everything that shows a value stays live. |
| 3D | The node is an instrument: real slab, recessed wells, raised controls, printed type, one key light. |

## 1. What changed from v1, and why

| v1 | v2 | Why |
|---|---|---|
| Coral `#F5866B` accent on dark, terracotta `#C2593F` on paper, violet `#8B6CFF` glow | One warm family **Ember** (H 38) and one cool family **Signal** (H 276) | The owner approved periwinkle + orange across the v0.5 films (r5 to r7). v1 had two warm hues and two cool hues competing. |
| Warm inks `#1F1D1A` / `#5A534C` on Paper | Cool inks `#12152A` / `#404660` / `#5A6180` on every light mode | Every neutral now sits on the Midnight hue (274), so modes swap without a colour shift. |
| Two grounds (Midnight, Paper) | Three modes (Midnight, Mist, Paper) with named roles | Mist is the film's diagram mode; roles let a design swap modes without new hexes. |
| Contrast listed by hand | Contrast gate in the build, 36 pairs, fails loudly | White on `#FF6B35` measured 2.84:1 (fails). Text on orange is now Midnight (6.1) or white on Ember 600 (5.98). |
| Type sizes per surface | Fluid scale (major third, display × 1.5), tracking curve, film scale | One scale for web, a separate one for the 4K stage. |
| Easing names only | Springs with ζ and ω, overshoot and settle computed, CSS `linear()` emitted | Motion can be checked, not described. |
| Two knob sizes (56/40) | One knob size, 56 | Matches the app since v0.5 (`kKnobStd`; `kKnobLarge == kKnobSmall`). |
| 3D nodes improvised per film | `node3d` spec plus a generator (`build_3d.py`) | The r7 film's 3D nodes looked warped and floating (section 9). |
| No channel list | 23 channel specs with sizes and modes | Every distribution surface named. |

## 2. Colour

### 2.1 Families

All values are set and checked in OKLCH (Ottosson 2020) with `tools/brand/colour.py`. Ramps are generated at one hue with a sine-bell chroma, then gamut-mapped by chroma reduction. Anchors are the owner-approved values.

| Family | Hue | Anchors | Role |
|---|---|---|---|
| Midnight | 274 | 900 `#151930`, 950 `#0D1020` | Grounds, surfaces, inks |
| Signal | 276 | 300 `#A7B1FF` (dark modes), 600 `#4A54D6` (light modes) | Structure: lines, diagrams, data, focus, selection |
| Ember | 38 | 400 `#FF6B35`, 500 `#D9430E`, 600 `#B53700` | The accent |

### 2.2 Modes

Use roles, never raw hexes. `brand.css` sets them per `data-mode`.

| Role | Midnight | Mist | Paper |
|---|---|---|---|
| ground | `#151930` | `#E4E2EF` | `#FBFAF6` |
| deep (wells) | `#0D1020` | `#D8D5E5` | `#EFF0F4` |
| surface-1 | `#20263F` | `#F7F5FB` | `#FFFFFF` |
| surface-2 | `#2A3052` | `#FFFFFF` | `#FFFFFF` |
| surface-3 | `#343B5E` | `#EEEDF6` | `#F5F5F8` |
| line | `#343B5E` | `#C4C1D9` | `#DAD8E6` |
| ink | `#EEF1FA` (Moon) | `#12152A` | `#12152A` |
| ink-2 | `#A3AACB` | `#404660` | `#404660` |
| ink-3 | `#8890B1` | `#5A6180` | `#5A6180` |
| signal | `#A7B1FF` | `#4A54D6` | `#4A54D6` |
| accent (marks, large) | `#FF6B35` | `#D9430E` | `#D9430E` |
| accent-ink (text, fills) | `#FF6B35` | `#B53700` | `#B53700` |
| on-accent | `#151930` | `#FFFFFF` | `#FFFFFF` |

| Mode | Use for |
|---|---|
| Midnight | Films, social, thumbnails, showcase sections, the logo tile, 3D |
| Mist | Diagrams, science plates, explainer stills, docs headers |
| Paper | Reading: website body, docs, PDFs, manuals, release notes, email |

### 2.3 Rules

| Rule | Detail |
|---|---|
| 60 / 30 / 10 | Ground 60, surfaces and ink 30, Ember under 10 of any frame |
| One Ember per frame | The key cable, the call to action, or the hand word. Not two. |
| Text on Ember | Midnight on `#FF6B35`; white only on the `#B53700` fill. Never white on `#FF6B35`. |
| Signal is not an accent | It draws structure. It never fills a button. |
| Category colours | Owned by the app (`src/core/CategoryColors.cpp`). On brand surfaces they appear only where a node appears: title strip, pins, cables. |
| Gradient | Logo, brand ball and end-card lockup only. Exact stops `#F57F66 #D99A9C #C7AFC0 #A9CDF1` at 0 / .35 / .65 / 1, horizontal. |
| Iridescence | One hero object per frame (ball, sphere, glass ring). Never on type or UI. |
| Glass | Only behind short labels, 72 to 78% opacity, never behind body text. Respect Reduce Transparency. |

### 2.4 Findings from the audit

| Finding | Number | Action |
|---|---|---|
| White on `#FF6B35` | 2.84:1, fails | Midnight on Ember 400; white only on Ember 600 |
| Ember vs Signal under colour-vision deficiency | ΔE OK 25 to 34 for protan, deutan, tritan | The two-ends split works without hue |
| Compositing `#818CF8` vs Synths `#6992F6` | ΔE 3.1 with normal vision | App report for `infinite-design-system`, not a brand change |
| Source `#4ADE80` vs Notes `#4CD964` | ΔE 3.0 | App report |
| 3D vs Audio Effects (tritan) | ΔE 1.2 | App report |
| Effects vs Modulators (deutan) | ΔE 2.5 | App report |

The book prints the full collision table and the contrast gate with WCAG and APCA Lc for each pair.

### 2.5 Retired

| Was | Now |
|---|---|
| Coral `#F5866B` as accent | Ember 400 `#FF6B35`; coral survives only as the gradient's first stop `#F57F66` |
| Terracotta `#C2593F`, terracotta ink `#A8472F` | Ember 500 `#D9430E` (marks), Ember 600 `#B53700` (text, fills) |
| Violet `#8B6CFF` glow | Signal or iridescence |
| Warm inks `#1F1D1A` `#5A534C` `#6E6760`, inner `#F3F0E7` | Cool inks and Paper deep `#EFF0F4` |
| Bone `#F1ECE2` (film r5) | Moon `#EEF1FA` |
| Ember ground, Cobalt ground `#2A33A8` | Not grounds (owner r6b) |
| Instrument Serif | Not used |

## 3. Typography

| Family | Weights | Role |
|---|---|---|
| Geist | 400 to 800 | Everything people read |
| Geist Mono | 400, 500 | Values with units, code, node types, folios, coordinates |
| Caveat | 700 | One human word per scene or section, in Ember, about 1.5× its neighbours, with a hand underline |
| Pixel 5x7 | drawn | Only when the machine itself speaks (prediction, learning). Not a font file. |

Web scale (fluid from 375 to 1440 px; text steps × 1.25, display × 1.5):

| Step | Size px | Weight | Leading | Tracking |
|---|---|---|---|---|
| Display | 40 → 96 | 800 | 0.98 | -0.04 em |
| H1 | 32 → 61 | 750 | 1.04 | -0.035 em |
| H2 | 25 → 39 | 700 | 1.1 | -0.025 em |
| H3 | 20 → 25 | 650 | 1.2 | -0.015 em |
| Lead | 18 → 20 | 450 | 1.45 | -0.005 em |
| Body | 16 | 400 | 1.55 | 0 |
| Caption | 13.5 | 500 | 1.45 | +0.005 em |
| Micro mono | 11.5 | 500 | 1.35 | +0.04 em |
| Folio mono caps | 11 | 500 | 1.2 | +0.12 em |

| Rule | Detail |
|---|---|
| Measure | Body 60 to 72 characters, captions 30 to 45 |
| Film | 4K stage: poster 60 to 100% of height, display 300, headline 196, title 128, line 96, caption 64, machine 44, folio 32 |
| Kinetic type | At most 15 characters per second on screen |
| Folio | Mono caps, left "INSIDE INFINITE / 03 THE PATCH", right "BAR 05 / 160 BPM" |
| Slabs | Patch Type slabs are lowercase, tracking -0.03 em |
| Figures | Tabular figures for any value that changes |

## 4. Mark

| Measure | Value |
|---|---|
| Curve | Lemniscate of Bernoulli: x = A sin p / (1 + cos² p), y = A sin p cos p / (1 + cos² p) |
| Stroke s | 87/338 A = 0.2574 A, round joins |
| Bounds with stroke | 2.2574 A × 0.9645 A (2.341 : 1) |
| Arc length | 5.2441 A |
| Crossing | Branches cross at 90°, each at 45° to the axis |
| Clear space | 2 s on every side |
| Minimum | Mark 20 px wide; tile icon 16 px |
| Tile | Midnight `#151930` rounded square, 18.5% corner, mark at 0.74 of its width (measured from the shipped icon; `build_logo.py --check` guards it) |
| 3D | One tube, diameter s; the two passes through the crossing separate by 1.24 s so it reads over and under (`logo_3d.glb`) |

Draw it from the formula and compare with `assets/Infinite.iconset` before shipping. Never recolour, rotate, outline, add glow or shadow to the flat mark, place it on a busy photo without the tile, or approximate the curve.

Recurring characters:

| Character | Rule |
|---|---|
| Brand ball | The one iridescent hero object; a film can fold back into it |
| Ember key cable | The one key connection in any patch shown |
| Digit bird | The 0/1 sparrow; between hero moments, never near the logo |
| Pointer | The film cursor: swaps tools with a tick, eased arcs, pauses one beat before a click |

## 5. Grid

| Format | Grid |
|---|---|
| Web | 12 columns, max 1200, gutters 16 / 24 / 32, margins 16 / 24 / 48 at 375 / 768 / 1440, 8 px base |
| Film 16:9 | 3840 × 2160 stage, graphics safe 5%, action safe 3.5% (EBU R95), 12 columns of 288 |
| 9:16 | Keep the top 14% and the bottom 22% clear, and the right 14% clear from 40 to 85% of the height (platform UI) |
| 4:5 and 1:1 | 6% margins, 6 columns; check the 3:4 crop on Instagram grids |
| Docs | A4 and US Letter, 18 mm margins, 12 columns, 4 pt baseline |

## 6. Shape, space, depth

| Token | Value | Use |
|---|---|---|
| Card radius | 24 (20 on phones) | Every card: node card, panel, frame, modal |
| Media radius | 16 (12 on phones) | Inside a card: outer radius minus 8 padding |
| Chip radius | 10 | Code chips, tags, keycaps |
| Control | pill | Buttons, chips, toggles, inputs |
| Space | 4 8 12 16 24 32 48 64 96 128 | |
| Elevation | 0 to 3 | Surface step + three stacked soft shadows; never an outline |

Node cards on 2D brand surfaces keep the v1 card: Midnight 1 frosted surface, radius 24, title in Geist 600 with the family in mono, media at radius 16 with an 8 px inset, one calm sentence, pill controls of one height, no dividers. A Caveat caption sits above the card, never inside it.

## 7. Iconography

| Rule | Value |
|---|---|
| Grid | 20, 2 px padding, 16 live |
| Stroke | 1.5 at 20, 1.25 at 16, 1.75 at 24, round caps and joins |
| Corners | 2 outside, 1 inside |
| Keylines | Circle 16, square 14, portrait 12 × 16, landscape 16 × 12 |
| Dot | 3 px; a signal icon ends in the same dot as a pin |
| Fill | The `-fill` variant shows the on state only |
| Labels | Icons stand alone only for universal actions (play, stop, record, close). Elsewhere a label sits beside the icon (Wiedenbeck 1999). |

The set is in `art/icons/src` (73 glyphs). The book renders all of them.

## 8. Motion

| Spring | ζ | ω | Overshoot | Settle (2%) | SwiftUI response / damping | Use |
|---|---|---|---|---|---|---|
| slab | 0.55 | 20 | 12.6% | 292 ms | 0.314 / 0.55 | Patch Type slabs, key cable snap. The signature. |
| letter | 0.60 | 22 | 9.5% | 271 ms | 0.286 / 0.60 | Patch Type letters |
| soft | 0.90 | 18 | 0.2% | 262 ms | 0.349 / 0.90 | Everything else that lands |
| press | 1.00 | 28 | 0 | 209 ms | 0.224 / 1.00 | Buttons, keycaps, knob detents |

Overshoot = exp(-πζ / √(1 - ζ²)). Settle is measured by the build; CSS `linear()` curves are in `brand.css` (`--spring-slab` and so on).

| Ease | Value | Use |
|---|---|---|
| ease-out | `cubic-bezier(0.16, 1, 0.3, 1)` | Entrances, most movement |
| ease-in-out | `cubic-bezier(0.65, 0, 0.35, 1)` | Camera, large travel |
| ease-in | `cubic-bezier(0.7, 0, 0.84, 0)` | Exits only |

Durations are note values at 120 BPM (the transport default): micro 125 (1/16), small 188 (1/16 dotted), medium 333 (1/4 triplet), large 500 (1/4), page 667 (1/2 triplet) ms. Stagger: letter 10 (1/128 triplet), list 42 (1/32 triplet), card 62 (1/32) ms. The laws behind every move (clock, mass, spring, path, phase, perception) live in `tools/brand/motion.py`; `--check` keeps tokens on the grid.

| Verb | Meaning |
|---|---|
| Land | An object arrives and settles: soft spring, 4 to 8 u of travel |
| Patch | A cable draws pin to pin with ease-out over 320 to 480 ms; the plug seats with the slab spring; the destination answers within 100 ms |
| Carry | Between scenes an object travels and becomes the next frame: iris, zoom into a rect, zoom out of a rect, carried object |
| Live | Anything that shows a value keeps moving: knobs breathe ±2°, waveforms scroll, meters fall |
| Breathe | One slow loop under everything (4 to 8 s); never on type |

App motion stays at or under 200 ms and values never animate. All brand motion stops under `prefers-reduced-motion`.

## 9. The node in 3D

A node in 3D is a physical instrument panel built from the app's own measurements, not a screenshot tilted in space. Reference build: **Audio Filter** (`src/app/bodies/FxBodies1.cpp`, `docs/plans/ui-system/element-sizes.md`). Units: 1 u = 1 app px at 100% UI scale; glTF exports in metres at 1 px = 1 mm.

### 9.1 Process

| Step | Do |
|---|---|
| 1 Capture | Read the node body in code and take a 100% screenshot. Note the live values a user would set. |
| 2 Lay out flat | Rebuild the panel in app pixels: cells, wells, captions, pins. The top view must match the app before anything gets depth (`node_top.png`). |
| 3 Assign z | Each component takes its height from the z table. Wells go down, prints stay flush, controls come up, caps sit highest. |
| 4 Material | Matte body, satin knobs, glossy coated display well, printed type. Colour comes from the mode roles; category colour only on the title strip, jacks and cables. |
| 5 Light once | Key, rim and fill as below. Shadows always on. |
| 6 Frame | 3/4 view from one angle family; the camera sweeps sideways, it does not orbit. |
| 7 Make it live | Knobs, keys, cables and the viewer animate with the brand springs. |
| 8 Check side by side | Render next to the app screenshot: same order, same labels, same values. |

### 9.2 Construction

| Part | Spec |
|---|---|
| Body | 464 × body height (440 content + 2 × 12 padding), 18 thick, radius 24, 3 fillet, no outline |
| Title strip | Flush print in the category colour at 18% over surface-1; title Geist 600 15, family in mono caps on the right |
| Readout strip | 21 tall, recessed 1.5, mono values in ink-2 |
| Display well | 190 tall, recessed 3, radius 12, glossy coated floor; graticule in line, response curve in Signal with a dim Signal fill |
| Knob | One size, 56. Skirt +5, knurled cap (36 ridges) to +14 at 0.78 r, engraved Moon pointer from 0.25 to 0.86 of the cap. Value arc printed on the panel at 1.12 r, 270°, in Signal (bipolar knobs start at the top). Modulation arc outside it in the Modulation colour `#FFBE5A`. |
| Caption | Geist 500 13 in ink-2, 4 under the knob; value in Geist Mono 10.5, ink-3 |
| Dropdown | Flush well 92 × 21, radius 4, value only, no chevron |
| Checkbox | 16 keycap raised 4 with an LED dot (Signal, emissive when on) |
| Switch | 30 × 16 recessed track (Signal when on), thumb raised 4 |
| Keycap | Pill on a shallow well; rest +6, pressed +2.5 with a Signal tint, LED at the left |
| Fader | 6 rail recessed, 18 × 12 handle raised 7 with a Moon centre line, Signal fill along the rail |
| Jacks | In the side walls at the pin row, like a pedal: input left, output right. Ring 14 in the category colour, hole 8. |
| Cables | Matte tubes, 5 diameter; plug 12 × 18 with a metal sleeve, seated in the jack; the cable sags to the floor and runs off frame. The key cable is Ember; others take the source category colour. No glow. |

| Light / camera | Rule |
|---|---|
| Key | Azimuth 135°, elevation 50°, soft (5° sun) |
| Rim | Azimuth 45°, elevation 35°, 0.6 of key |
| Fill | From the camera, 0.35 of key |
| Shadows | Always on, contact 0.25, floor in the mode colour |
| Camera | Yaw 20 to 30°, pitch 25 to 35°, 50 mm; spec views orthographic (top, true isometric 30° / 35.264°) |
| Colour | Standard view transform so brand hexes stay true (AgX desaturates them) |

| Part | Animation |
|---|---|
| Knob | Turns with the soft spring; idle breath ±2° |
| Keycap | Down 80 ms with press, up 140 ms; LED fades 160 ms |
| Switch | Thumb slides with soft; track tints over 160 ms |
| Cable | Draws with ease-out; plug seats with the slab spring |
| Viewer | Always live |

### 9.3 What went wrong in the r7 film

| r7 film | v2 rule |
|---|---|
| Flat textures skewed in 2.5D, so cards looked warped | Real geometry: 18 u slab, fillet, recessed wells, raised controls |
| Outlines on cards and controls | No outlines; edges come from the fillet catching the key light |
| Win95 bevel blocks for buttons | Pill keycaps on a shallow well, LED for state |
| Dark cylinders for knobs, value arcs floating | Skirt + knurled cap + engraved pointer; the arc is printed on the panel |
| Labels baked tiny into a texture | Printed Geist geometry at the app's caption sizes |
| No shadows, objects floated, no key light | One key light, soft shadows, contact shadow |
| Glossy neon cables with big ball ends and glow | Matte tubes, real plugs in side-wall jacks, sag to the floor |

### 9.4 The 3D kit

| File | Contents |
|---|---|
| `node_audio_filter.glb` | The hero node with both cables; loop: freq knob turns on the slab spring |
| `node_lfo.glb` | LFO (Modulators): wave viewer, shape, rate / phase / low / high sliders; loop: playhead rides the wave |
| `node_field_pixel.glb` | Field Pixel (Source): preview, preset, Save / Export / Import, Edit Field..., preset sliders; loop: Edit key press on the press spring |
| `patch_lfo_field_pixel.glb` | Both nodes patched by a Modulation-colour cable; loop: the plug snaps home on the slab spring |
| `controls_kit.glb` | Knob, modulated knob, keycap off and on, switch, checkbox, dropdown, fader |
| `logo_3d.glb` | The mark as a loop |
| `logo_tile.glb` | The app tile |
| `brand_ball.glb` | The iridescent hero object |
| `kit.blend` | Everything, with lights, cameras and the baked loops (60 fps, 2 s) |
| `studio.blend` | The stage alone: world, key / rim / fill, shadow-catcher floor, cameras. Import any `.glb` into it |
| `renders/*.png` | node_hero, node_top, node_exploded, knob_macro, kit_components, logo_3d, logo_tile, brand_ball, node_lfo_top, node_field_pixel_top, patch_hero |
| `renders/*_cutout.png` | Transparent with contact shadow: node_hero, node_lfo, node_field_pixel, patch, logo_3d, brand_ball |

To build another node, add a `build_<node>()` next to `build_lfo()` in `tools/brand/build_3d.py`: `slab()` makes the body, wells, title strip and jacks; fill it with `knob`, `hslider`, `keycap`, `checkbox`, `switch`, `dropdown`, `fader` in the order of that node's body code (`src/app/bodies/`). Connect nodes with `patch_cable()`; add loops in `animate()` with `key_spring()` and a brand spring name.

## 10. Channels

| Channel | Asset | Size | Mode |
|---|---|---|---|
| Website | Open Graph | 1200 × 630 | Midnight |
| Website | Favicon | SVG + 32 + 180 touch | Tile |
| YouTube | Video master | 2560 × 1440 at 60 fps, plus 1920 × 1080 | Midnight |
| YouTube | Thumbnail | 1280 × 720, under 2 MB | Midnight |
| YouTube | Channel banner | 2560 × 1440, safe 1546 × 423 | Midnight |
| Shorts / Reels / TikTok | Vertical video | 1080 × 1920 | Midnight |
| Instagram | Feed post | 1080 × 1350 | Midnight or Paper |
| X | Post | 1600 × 900 or 1920 × 1080 video | Midnight |
| X | Header | 1500 × 500 | Midnight |
| LinkedIn | Post | 1200 × 627 or 1080 × 1350 | Paper or Midnight |
| LinkedIn | Banner | 1584 × 396, left 25% clear | Midnight |
| GitHub | Social preview | 1280 × 640 | Midnight |
| GitHub | README hero | 1280 × 640 @2x, light and dark via `<picture>` | Both |
| Release | Release notes | Markdown, sentence case | n/a |
| macOS | App icon | 1024 master, iconset 16 to 512 @2x | Tile |
| macOS | DMG background | 600 × 400 pt @2x (`tools/dmg/make_background.py`) | Paper |
| Windows | ICO | 16, 24, 32, 48, 64, 256 | Tile |
| Linux | Desktop icon | SVG + 256 + 512 | Tile |
| Docs / PDF | Manual page | A4 and US Letter | Paper |
| Press kit | Logo set | SVG, PNG 1024, mark + tile, light and dark | All |
| Email | Newsletter | 600 wide, live text, images @2x | Paper |
| Slides | Deck | 1920 × 1080 | Midnight or Paper |
| Discord | Icon / banner | 512 × 512 / 960 × 540 | Tile / Midnight |

Thumbnails: one Geist headline of 2 to 4 words at the left, the real product or a 3D node at the right, Midnight ground, one Ember element, the mark top left at 6% margin.

Make them with `python3 tools/brand/channels.py --headline "A DAW for you" --hand you --kicker "Infinite 0.5" --line "..."`. Sizes and safe boxes live in `brand.json` `render.targets`.

### 10.1 Screenshot standard

| Rule | Value |
|---|---|
| Capture | 2x (Retina), default dark theme, a real patch with signal flowing, no debug panels, cursor hidden |
| Window | Whole window with macOS Cmd+Shift+4 then Space (no desktop), or one node with `scripts/node_screenshot.py "<Node>"` |
| Content | Real names and values; no lorem, no "test", no personal paths in title bars |
| Frame | `python3 tools/brand/frame_shot.py shot.png [--mode paper] [--ratio 4:5]`: brand ground, 6% margin, 20 pt corners, two-layer shadow |
| Ratio | 16:9 for site, slides, YouTube; 4:5 for Instagram and LinkedIn; 1:1 for avatars and grids |
| Never | Upscale, recolour the UI, add device mockups, blur or crop through a control |
| Where | `docs/brand/img/screenshots/`; the press kit picks them up |

### 10.2 Templates

`python3 tools/brand/templates.py` writes `docs/brand/templates/`:

| Template | Use |
|---|---|
| `slides.html` | 1920 × 1080 deck on `brand.css`: title, statement, image, two-up, end. Arrow keys step; `?print` lays out every slide for PDF |
| `email.html` | 600 px release email, tables and inline styles only, Paper mode, one Ember button; fill the `{{ }}` fields |
| `fact-sheet.html` | A4 press sheet; version, licence and links read from `gh`, `LICENSE` and `README.md` |

`python3 tools/brand/press_kit.py` zips logo, renders, cutouts, channel cards, screenshots and the fact sheet.

## 11. Formatting and voice

| Topic | Rule |
|---|---|
| Units | Number, space, unit: 120 BPM, 4.00 ms, 1.20 kHz, -6.0 dB, 48 kHz / 24-bit |
| Dates | ISO in data and filenames (2026-10-10); 10 October 2026 in prose |
| Versions | v0.5.0 |
| Node names | Exactly as the app spells them, Title Case: Audio Filter, Color Ramp, Liquid Glass |
| Keys | Cmd/Ctrl + K as mono keycap chips; macOS first |
| Files and code | Geist Mono |
| Case | Sentence case for headings and buttons; lowercase slabs; mono caps only for folios |
| Punctuation | No em dashes, no exclamation marks |
| Spelling | British in prose (colour, centre); node and API names keep the app's spelling |

| Voice | Rule |
|---|---|
| Plain and specific | Say what it does and what the person can do, with real node names and values |
| Calm | No hype, no "world's first", no "revolutionary" |
| The line | **A DAW for you.** The end card cycles designers, artists, musicians, VJs, scientists and lands on an Ember "you". |
| Library copy | One short sentence: what it does, then what it needs |
| Free | The word is "Free", nothing qualifying it |
| Sound | No narration unless the owner supplies it; owner music only; every SFX attaches to a visible event, quantised to the grid |

## 12. Distinctive assets

Audit after Romaniuk, *Building Distinctive Brand Assets* (2018): spend where an asset is both recognised and unique to Infinite.

| Action | Assets |
|---|---|
| Invest | Lemniscate mark, Ember key cable, digit bird, mono folio system |
| Keep, one per frame | Brand gradient (logo only), Caveat hand word, Patch Type slabs, iridescent ball |
| Restrain | Dot grid (texture only, at most 6% opacity), frosted glass |
| Retire | Violet glow, Instrument Serif, coral as accent, terracotta |

## 13. Research basis

| Topic | Source | Rule here |
|---|---|---|
| Perceptual colour | Ottosson, OKLab (2020); CSS Color 4 | Ramps and gamut mapping in OKLCH |
| Contrast | WCAG 2.2 (W3C 2023) | The build gate |
| Perceptual contrast | APCA was removed from the WCAG 3 draft in 2023; the March 2026 WCAG 3 working draft still has no contrast method | APCA printed as a guide only |
| Colour vision | Machado, Oliveira, Fernandes (2009) | CVD checks in the book |
| Polarity | Piepenbrock et al. (2013): positive polarity reads faster | Paper for reading |
| Translucency | NN/g, Budiu, "Liquid Glass Is Cracked" (Oct 2025) | Glass only behind short labels |
| Expressive UI | Google, Material 3 Expressive: 46 studies, 18,000+ participants (2025) | One accent action per view, springs for state |
| Tokens | Design Tokens Community Group format 2025.10, first stable (28 Oct 2025) | `brand.tokens.json` |
| Aesthetics | Kurosu and Kashimura (1995), aesthetic-usability | Polish carries trust |
| Animation | Chang and Ungar (1993); Thomas and Johnston (1981) | Measured springs, no linear moves |
| Response | Nielsen 0.1 / 1 / 10 s; Doherty threshold 400 ms | App motion at most 200 ms |
| Icons | Wiedenbeck (1999); McDougall et al. (2000) | Icons with labels |
| Measure | Bringhurst; Dyson (2004) | 60 to 72 characters |
| Reading speed | Netflix and BBC subtitle guidance (about 17 cps) | Kinetic type at most 15 cps |
| Safe areas | EBU R95 | Film grid |

## 14. Migration

| Surface | State (2026-10-10) |
|---|---|
| `website/` | Done: links `brand.css`, palette aliases brand roles, springs by name, no retired colours (`brand_lint.py` guards it; illustration colours are baselined) |
| `website/glass.css` | Open: glass only behind short labels, 72 to 78% opacity |
| motion-film skills | Done: colours migrated, house style points at `brand.json` and the 3D kit |
| `~/films/inside-infinite` palette | Open: Moon `#EEF1FA` ink |
| 3D nodes in films | Done: import the `.glb` files or build with `build_3d.py` |
| Press kit | Done: `press_kit.py` |
| App icon (`assets/Infinite.iconset`) | Open: ground is `#1D223B`, brand tile is `#151930`; `build_logo.py --check` reports it |

## 15. Before shipping anything on a brand surface

1. Colours come from `brand.json` roles. No new hex values.
2. One mode, one Ember, one Caveat word per frame or section.
3. Cards 24, media 16, no outlines.
4. The contrast gate passes (the build checks it).
5. Reduced motion and reduced transparency are handled.
6. Type follows the scale; values are in mono with units.
7. A 3D node matches its app screenshot side by side.
8. Copy: plain, specific, no em dashes, no hype.
9. `python3 tools/brand/brand_lint.py <path>` passes.

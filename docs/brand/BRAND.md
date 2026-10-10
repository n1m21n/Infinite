# Infinite brand book

Status: v1, 2026-10-10. This is the one place that says how Infinite looks, moves and sounds. When a film brief, a skill, a stylesheet or a memory note disagrees with this file, this file wins and the other one gets fixed.

Values live in [`brand.json`](brand.json) (brand surfaces) and in `src/app/ui/design/tokens.json` + `src/core/CategoryColors.cpp` (the app). The visual version is [`brand-book.html`](brand-book.html), generated from `brand.json` by `tools/brand/build_brand_book.py` so the swatches can never drift from the numbers.

## 0. The one-page version

| | |
|---|---|
| Feeling | calm, premium, a little playful. Confident, never hype. References: Anthropic, Nothing, Apple, 1X NEO |
| Two grounds | **Midnight** `#151930` (dark) and **Paper** `#FBFAF6` (light). Never pure black, never pure white |
| One accent | **Coral** `#F5866B` on Midnight, **Terracotta** `#C2593F` on Paper. One accent thing per frame |
| One glow | **Violet** `#8B6CFF`, on Midnight only, for hero objects |
| Type | Geist for words, Geist Mono for machine text, Caveat for one human word, Instrument Serif rarely |
| Shape | everything rounded. Cards 24, media inside 16, controls pill. No outlines on cards |
| Depth | on dark, raised = lighter. Soft stacked shadows. A lit top edge, never a border |
| Motion | springs and ease-out, a small overshoot only on the signature slabs, nothing linear |
| Voice | plain, specific, lowercase on slabs, no em dashes, no "world's first" |
| Mark | the lemniscate, drawn from its formula, coral to sky, never approximated |

## 1. Three surfaces, one identity

Infinite shows up in three places. They share a spine but are not the same thing, and mixing them is how things went out of line.

| Surface | What | Ground | Governed by |
|---|---|---|---|
| **Product** | the app: canvas, nodes, panels, menus | app dark `#0A0B0F` / panel `#181B23`, accent `#6FB4FF`, 11 category colours | `tokens.json`, `CategoryColors.cpp`, skill `infinite-design-system` |
| **Brand** | website, films, thumbnails, social, docs, installers | Midnight or Paper, coral accent | this book + `brand.json` |
| **Bridge** | anything that shows the product inside a brand surface (node cards, screenshots) | node cards keep the app's category colours and layout, placed on Midnight surfaces | section 6 |

Rules of the bridge:

1. The product is never restyled to look like a brand surface, and a brand surface never borrows the app's neutral greys (`#181B23`, `#262A35`) for its cards. On the brand surfaces, cards are Midnight surfaces.
2. A node card on a brand surface keeps what makes it a node: category colour on the title bar and pins, the family glyph, ports. Nothing else about it is category-tinted.
3. The 11 category colours appear **only** inside node UI and in the node matrix. They are never used as page, button or heading colours.

## 2. Colour

### 2a. Palette

| Role | Name | Hex | Use |
|---|---|---|---|
| Ground (dark) | Midnight | `#151930` | page, frame, canvas |
| Surface 1 | Midnight 1 | `#20263F` | cards, slabs, panels |
| Surface 2 | Midnight 2 | `#2A3052` | hover, nested card, popover |
| Surface 3 | Midnight 3 | `#343B5E` | highest surface, hairlines on dark |
| Text on dark | Moon | `#EEF1FA` | primary text |
| Text 2 on dark | Moon 2 | `#A3AACB` | secondary text |
| Ground (light) | Paper | `#FBFAF6` | page, frame |
| Surface (light) | Card | `#FFFFFF` | raised card |
| Well (light) | Inner | `#F3F0E7` | media backing, code chips |
| Text on light | Ink | `#1F1D1A` | primary text |
| Text 2 on light | Ink 2 | `#5A534C` | secondary text |
| Text 3 on light | Ink 3 | `#6E6760` | tertiary text (was `#857C74`, which failed contrast) |
| Accent on dark | Coral | `#F5866B` | the accent |
| Accent on light | Terracotta | `#C2593F` | the accent: fills, large type, marks |
| Accent text on light | Terracotta ink | `#A8472F` | accent as small text or a link |
| Glow | Violet | `#8B6CFF` | hero objects on Midnight |
| Gradient | Coral to Sky | `#F57F66 #D99A9C #C7AFC0 #A9CDF1` | logo, ball, end card only |

### 2b. Rules

| Rule | Detail |
|---|---|
| 60 / 30 / 10 | 60 percent ground, 30 percent surfaces and ink, 10 percent accent |
| One accent | coral or terracotta, never both in one frame, never a second brand accent. Category colours are inside node UI only |
| One theme per piece | a light master (Paper) and a dark variant (Midnight) are each whole. No dark scenes in a light film and no dark panels in a light page, except the node cards, which are always dark glass |
| Elevation | on Midnight, raised means lighter (`Midnight 1` to `3`). Shadows are stacked and soft, tinted `#04050E` |
| Glow | glow and iridescence belong to dark scenes. Paper has none |
| Not empty | a dark ground can carry 3 to 4 glossy spheres at the edges. Big glowing shapes that overpower content are out |
| Gradient | exact four stops, horizontal. It is the logo's colour; do not use it as a decorative fill |
| Real colours | when a film shows a real ring or a real UI, sample the colour from the screenshot, never guess |

### 2c. Contrast (measured, WCAG)

| Pair | Ratio | Verdict |
|---|---|---|
| Coral on Midnight | 7.0 | AA, AAA large |
| Moon on Midnight | 15.3 | AAA |
| Moon 2 on Midnight | 7.6 | AAA |
| Coral on Midnight 1 | 6.0 | AA |
| Violet on Midnight | 4.7 | AA, use for objects and large type |
| Ink on Paper | 16.1 | AAA |
| Ink 2 on Paper | 7.2 | AAA |
| Ink 3 on Paper | 5.3 | AA |
| Terracotta on Paper | 4.2 | large text and marks only |
| Terracotta ink on Paper | 5.6 | AA for small text and links |
| White on Terracotta | 4.4 | large or bold labels only (buttons are 16 px bold and up) |
| old muted `#857C74` on Paper | 3.9 | retired |

Colour is never the only cue. Record, solo, learn, modulation, favourite and the others already have close colours; each is paired with a glyph or label.

### 2d. Product role colours (from the app, for reference)

Record, solo, learn, modulation, expression, prediction, go and favourite keep one fixed meaning each, defined in `tokens.json` (`action.*`, `pin.*`, `badge.favorite`) and listed in `brand.json`. Brand surfaces that show them (a tutorial, a diagram) use the same values.

## 3. Typography

| Family | Role | Rule |
|---|---|---|
| Geist | headlines, UI, body | display 700 to 800, tracking -0.02 to -0.035 em |
| Geist Mono | values, labels, code, coordinates | tabular figures, ASCII only |
| Caveat | the human word | 700, accent colour, hand underline, at most **1.5x** its neighbours, **one per scene or section** |
| Instrument Serif | editorial accent | italic, rare, never in the same line as Caveat |

No fourth face, ever. The website loads Geist, Geist Mono and Caveat from Google Fonts; each film ships its own `fonts/` folder.

| Step (web) | Size | Weight | Tracking |
|---|---|---|---|
| Display | clamp(2.4rem, 7vw, 5rem) | 800 | -0.04em |
| Heading | clamp(1.6rem, 3.4vw, 2.4rem) | 700 | -0.03em |
| Subheading | 1.1rem | 700 | -0.02em |
| Body | 1rem | 400 | 0 |
| Caption | 0.85rem | 500 | 0 |
| Micro (mono) | 0.72rem | 500 | +0.04em |

App sizes are 11 / 13 / 15 / 22 px (`tokens.json` `type`); do not copy the web scale into the app.

Type rules that hold everywhere:

| Rule | Detail |
|---|---|
| One treatment per idea | if words are handwritten and underlined, every such word is |
| A job for every treatment | a type effect is chosen for what it communicates; if it has no job it is decoration and is cut |
| Slab headlines | lowercase, Geist 800, on a node-card slab with port dots, tracking -0.03 |
| Machine text | Geist Mono; the 8-bit pixel face only when the machine is speaking |
| Loud but calm | loudness comes from scale and weight (type up to 60 to 100 percent of a film frame), never from colour noise |
| 9:16 | nothing above 14 percent or below 78 percent of the height; right 14 percent clear between 40 and 85 percent |

## 4. Shape, space, depth

### 4a. Radius

| Token | Brand surfaces | Use |
|---|---|---|
| Card | **24** (20 on phones) | every card without exception: node card, white panel, film frame, steps, modals |
| Media | **16** (12 on phones) | anything inside a card; always card radius minus the card's 8 px padding (concentric) |
| Chip | 10 | code chips, tags |
| Control | pill (999) | buttons, filter chips, toggles, inputs |

App radii are small by design (2, 4, 6, 8, 10: `tokens.json` `radius`) because the canvas is dense. Do not use app radii on brand surfaces or the reverse.

### 4b. Space

4 / 8 / 12 / 16 / 24 / 32 / 48 / 64. One page uses one gutter (16 on phones, 24 or 32 on desktop) and one card gap. A control row uses one control height (32 px in cards, 40 px for page-level, 44 minimum hit area on touch).

### 4c. Depth and edges

| Rule | Detail |
|---|---|
| No card outlines | cards are separated by surface value, a lit 1 px top highlight (`inset 0 1px 0 rgba(255,255,255,0.16)`) and a soft shadow. No 1 px ring, no divider line under a title, no rule above a footer |
| Hairlines | allowed only as table row separators and a scrolled header edge |
| Glass | brand cards are frosted: translucent Midnight 1, `backdrop-filter: blur(20px) saturate(1.6)`; Paper panels are translucent white. Always with a solid fallback when blur is unavailable or reduced transparency is on |
| Shadow | three stacked soft layers; never a single hard drop shadow |
| Real details | small live UI (knobs, curves, counters, meters) is what makes the product feel real; show them, do not draw generic boxes |

## 5. Node cards on brand surfaces (the bridge)

```
┌──────────────────────────┐   surface  Midnight 1 (frosted), radius 24, no outline
│ Name              Family │   header   title Geist 600, family in mono, no divider
│ ┌──────────────────────┐ │   media    radius 16, 8 px inset, the thing itself
│ └──────────────────────┘ │
│ one calm sentence        │   body     Moon 2 on Midnight 1
│ [ Free ]       [ ⧉ ][ ⤓ ]│   foot     controls pill, one height, no rule above
└──────────────────────────┘
```

- Port dots sit on the card edge in the category colour; the title bar tint is the only other category colour.
- A hand-written caption (Caveat, category-dark colour) sits above the card, never inside it.
- The same card is used for Library items, the homepage "what if" chain, and film nodes. It is one component.

## 6. Iconography

| Token | Value |
|---|---|
| Grid | 20 x 20, 2 px padding, 16 x 16 live area |
| Stroke | 1.5 px at 20; 1.25 at 16 (own master); 1.75 at 24 |
| Caps and joins | round |
| Corners | 2 outer, 1 inner |
| Fill state | solid shape with inner details knocked out |
| Gap | at least 2 px between strokes |
| Colour | one colour (`currentColor`); state colour comes from the theme |
| Motif | the dot: signal, routing and modulation icons end a stroke in a 3 px dot, the same dot as a pin, a cable end and a slider handle |
| Families | 11 category glyphs (`family-*-20.svg`), shown before the category name |

Masters are in `art/icons/src`; the built font is generated by `tools/design/build_glyphs.py`. Never use stock icon sets on brand surfaces; draw on the grid.

## 7. Motion

| Token | Value | Use |
|---|---|---|
| ease-out | `cubic-bezier(0.16, 1, 0.3, 1)` | entrances, most movement |
| ease-in-out | `cubic-bezier(0.65, 0, 0.35, 1)` | camera, large travel |
| ease-in | exits |  |
| signature spring | zeta 0.55 to 0.6, omega 20 to 22 | Patch Type letters and slabs only; 9 to 13 percent overshoot |
| soft spring | zeta 0.9 and up | everything else that lands |
| letter stagger | 1/90 s (about 11 ms) | slab text |
| reveal stagger | 40 ms | web lists |

App timings (ms) are in `tokens.json`: hover in 120, hover out 180, on 160, off 140, focus 100, tooltip 500 delay / 100 fade, accent fade 200. Press is instant. Nothing in the app animates a value, only state. Reduced motion turns eases off.

Rules: no linear position or scale. Cuts land on the grid and something always carries over a cut (an object, a shape, a match cut). Speed stays continuous across a cut. A hold follows the key message; an aside gets its own blank beat. Rejected: ink floods, a camera that whips to follow the pointer, scattered tilted cards, a sudden style change mid-piece.

## 8. Mark and recurring characters

| Element | Rule |
|---|---|
| Logo | the lemniscate (Bernoulli, `x = A sin p / (1 + cos² p)`, `y = A sin p cos p / (1 + cos² p)`), stroke 87/338 of A, round joins, the brand gradient across its full width. Draw it from the formula; compare side by side with `assets/Infinite.iconset` before shipping |
| Clear space | at least the stroke width x 2 on every side |
| App icon | the lemniscate on the Midnight tile (`assets/Infinite.icns`) |
| Brand ball | the glossy coral-to-sky sphere; it becomes the logo, and a film folds back into it |
| Digit bird | the 0/1 sparrow. Appears between hero moments, never on or near the logo |
| Pointer | the film cursor: swaps tools with a tick, eased arcs, pauses one beat before a click |
| Coral cable | the one key connection in a patch |

Never: recolour the logo, add effects to it, put it on a busy photo, stretch it, or approximate the curve with a hand-drawn path.

## 9. Voice

| Rule | Detail |
|---|---|
| Plain and specific | say what it does and what the person can do; real node names and real values |
| Calm | no hype, no exclamation marks, no "world's first", no "revolutionary" |
| Lowercase slabs | slab headlines and captions are lowercase; sentences elsewhere are normal case |
| No em dashes | use a comma, a colon or a new sentence |
| The line | **A DAW for you.** The end card cycles the words (designers, artists, musicians, VJs, scientists) and lands on a coral "you" |
| Library copy | one short sentence; what it does, then what it needs |
| Free means Free | the word is "Free", nothing qualifying it |

Sound (films): no narration unless the owner supplies it; music is synthesised or owner-supplied and varies by film; every SFX attaches to a visible event, quantised to the grid.

## 10. Applying it

| Surface | Use | Where the values are enforced |
|---|---|---|
| Website | Paper + Midnight node cards, terracotta accent, glass | `website/style.css`, `website/glass.css` (variables mirror `brand.json`) |
| Film | Midnight ground + coral (light master on Paper), one theme per film, house eases and springs | each film's `lib/core.py` palette; skill `motion-film` `infinite-house-style.md` |
| Thumbnail / social | big Geist headline left, real product screenshot right on Midnight with a soft brand aura, logo top left | see `motion-film/references/design.md` |
| Docs / PDFs | Paper, Geist, terracotta headings, mono code | |
| App | untouched by this book; follows `tokens.json` | `infinite-design-system` |

Before shipping anything on a brand surface:

1. Colours come from `brand.json`. No new hex values.
2. One accent, one Caveat word, one theme.
3. Every card is radius 24 with media at 16 and no outline.
4. Contrast: small text passes 4.5; large text and marks pass 3.
5. Reduced motion and reduced transparency are handled.
6. Copy: no em dashes, no hype.

## 11. Known gaps (decide when they come up)

| Gap | Note |
|---|---|
| inside-infinite `r5` colour system | the film's draft uses Midnight `#0D1020`, a periwinkle `#A7B1FF` and an orange `#FF6B35` with three modes (Midnight, Mist, Ember). It is a film draft, not adopted: its orange and periwinkle compete with Coral and Violet. Until the owner signs it off, films use this book's palette |
| Light film variant | Paper is defined; a Midnight-on-Paper hybrid for stills is not |
| Icon set | 73 glyphs exist; Prediction family glyph is the weakest at 14 px |
| Product vs brand greys | app panels (`#181B23`) and Midnight 1 (`#20263F`) are different on purpose; a unifying pass would need the owner |

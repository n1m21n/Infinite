# Website v2: self-prompt (2026-09-28)

This is Claude's own prompt for the website pass. It applies the brand, type and motion system built for the
launch film, the what-if film and the kick-reactive tutorial (`~/.claude/skills/motion-film/references/`) to
`website/`. Branch `feature/website-v2`. Nothing is pushed to `main` without the owner's OK (a push to `main` deploys Pages).

## 1. Goal
The site should feel like the films: paper, rounded cards, springs, the exact logo, the digit bird. Someone
who has watched the film should recognise the site in one second. It must work equally well at 375 px and at 1440 px.

## 2. What we learned, and where it lands on the site
| Learning (films) | Site application |
|---|---|
| Ending "A DAW for ... you" (Caveat word, hand underline, 1.5x headline max) | Hero headline: "A DAW for" + cycling hand-written word (designers, artists, musicians, VJs, scientists, you), with a hand underline. The line keeps a fixed anchor |
| Exact logo (Bernoulli lemniscate, stroke 87/338, coral to sky gradient) | Hero mark drawn from the formula as SVG. The brand ball rides the loop. Never approximated |
| Patch Type slabs (words on node-card slabs, port dots, cables) | Section titles sit on slabs, with a port dot. "What if your DAW could" capability chain |
| Node grammar (cards, ports, cables, category colours from `CategoryColors.cpp`) | Recipes become real node chains with cables, not arrow pills. Category colours are the app's own |
| Digit bird (0/1 sparrow, reacts, never touches the logo) | Canvas mascot that perches on section slabs, flaps between them on scroll, and hops on tap. It is never on the hero logo |
| One screenshot zoomed, never a bombardment | Hero product window: one screenshot with a scroll-driven zoom tour (3 stops with mono labels) |
| HUD language (dot grid, corner brackets, mono coordinates) | Quiet HUD brackets on the hero and the film frame. The existing dot grid stays |
| Springs, anticipation, stagger, arcs, rest | CSS `linear()` spring easing, 40 ms stagger on reveals, press anticipation on buttons |
| Aside gets its own beat ("oh, and also") | Prediction gets its own quiet band with a mono typewriter line |
| Audio gets audio UI | Listen = white waveform card (film act IV) with meter + time in tabular mono |
| Loop: last frame = first | Footer closes on "A DAW for you." with the bird alone, echoing the hero |
| Copy: no em dashes, real links | All new copy. Version from `gh release list` (v0.4.5) |

## 3. Page structure (top to bottom)
```
nav (logo | links | Download)          mobile: logo | Download | menu sheet
hero       "A DAW for <word>" + lemniscate + CTA (OS-detected) + platforms chip
product    one screenshot window, zoom tour on scroll
film       the launch film, muted autoplay, tap for sound
what if    slab chain: design visuals -> make music -> write code -> all at once   (4 live node cards, cabled)
aside      "oh, and also" -> predicts your next move (self-turning knobs, ghost arc)
nature     connected network (existing, restyled; reshuffles each time it enters view)
listen     waveform card
recipes    node chains with cables + screenshot
nodes      207 real node names as a mono matrix, coloured by category, search filter
setup      one card with OS tabs (macOS / Windows / Linux), detected OS first
download   specs + 3 buttons, detected OS primary
footer     "A DAW for you." + bird (the loop frame)
```

## 4. Brand tokens (light master, paper)
| Token | Value |
|---|---|
| Paper / card / inner | `#FBFAF6` / `#FFFFFF` / `#F3F0E7` |
| Ink / ink2 / muted | `#1F1D1A` / `#5A534C` / `#857C74` |
| Accent (one "look here" at a time) | terracotta `#C2593F` |
| Brand gradient | `#F57F66 -> #D99A9C -> #C7AFC0 -> #A9CDF1` |
| Category colours | the "Infinite" preset in `src/core/CategoryColors.cpp` |
| Fonts | Geist 700-800 display (tracking -0.03), Caveat 700 hand, Geist Mono HUD/code (tabular) |
| Radius | everything rounded. 10 / 16 / 24 / pill |
| Shadow | stacked 3-layer premium shadow + contact shadow |

## 5. Motion rules
| Rule | Implementation |
|---|---|
| Spring | `--spring: linear(...)` (zeta ~0.5), with a cubic-bezier fallback |
| Stagger | children reveal with `--i * 40ms` |
| Anticipation | `:active` scales to 0.96, release springs back |
| Rest | no looping motion on text. Only canvases loop, and they pause off screen (existing `createViewportLoop`) |
| Reduced motion | `prefers-reduced-motion`: no zoom tour, no bird flight, no word cycling (shows "you") |

## 6. Mobile contract
- 16 px gutter, zero horizontal scroll (checked with `scrollWidth <= innerWidth` at 375 px).
- Tap targets >= 44 px. Menu sheet instead of hidden links. OS tabs scroll horizontally if needed.
- Video uses `playsinline`, a poster, and `preload="none"` below the fold.
- Canvases are DPR-capped at 2.

## 7. Guardrails
- Keep the owner's uncommitted gtag snippet and the download event tracking.
- Keep every existing link, the PDFs, the setup text (facts unchanged, only layout), and `artworks.html`.
- The launch film is compressed for the web (H.264 1080p, ~8 MB, faststart) into `website/assets/film/`. The 2K masters stay outside the repo.
- No new JS libraries (three.js r128 stays for the cube).
- No Claude attribution in commits.
- Commit per step, explicit `git add` of website + this doc only (the semi-brain files stay out).

## 8. Verification
| Check | How |
|---|---|
| Console clean | `read_console_messages` at desktop and mobile |
| No overflow | JS `document.documentElement.scrollWidth` at 375 |
| Visual | screenshots of hero, what-if, recipes, setup tabs, footer at 1280 and 375 |
| Links | every href in the old page still present |

## 9. Steps (one commit each)
1. Tokens, nav + mobile sheet, hero (logo, word cycler, CTA), product zoom tour, film section.
2. What-if chain + aside + bird + nature restyle + listen card.
3. Recipes as node chains, node matrix, setup tabs, download, footer. Then verify on both sizes.

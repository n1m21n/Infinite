# PDF and print layout

How every manual, guide and print sheet is laid out. It extends `BRAND.md` (section 5 Docs, section 3 Typography, section 10 channels); where it is silent, the brand book wins. The values live in `tools/docs-shots/common.py` (`PDF` and `brand_css()`), and every PDF builder uses that one stylesheet: the Node Reference Manual, the Field Language Manual and the Headless User Guide.

## Page

| Rule | Value |
|---|---|
| Mode | Paper: ground `#FBFAF6`, surface `#FFFFFF`, deep `#EFF0F4`, ink `#12152A`, ink-2 `#404660`, ink-3 `#5A6180`, hairline `#DAD8E6` |
| Size | US Letter (8.5 × 11 in) by default; A4 with `PDF_PAGE=a4`. Same margins and scale on both |
| Margins | 18 mm left and right, 18 mm top, 22 mm bottom (the folio sits in the lower 10 mm) |
| Grid | 12 columns, 4 pt baseline. Every size, leading and space below is a multiple of 4 pt |
| Accent | Ember, one look-here per page, under 10% of its area: the section kicker, the folio mark or the one Caveat word. Signal for links, diagrams and focus |
| Never | Dark pages, outlines or borders around cards and figures, gradients, drop shadows on text, em dashes, exclamation marks |

## Type (print scale, pt)

| Role | Family | Size / leading | Weight | Notes |
|---|---|---|---|---|
| Cover title | Geist | 48 / 48 | 800 | -0.04 em; one Caveat word in Ember at 1.5x |
| H1 (chapter) | Geist | 28 / 32 | 750 | -0.035 em; 24 pt above (on a fresh page: 0), 12 below |
| H2 | Geist | 18 / 24 | 700 | -0.025 em; 24 above, 8 below |
| H3 | Geist | 13 / 20 | 650 | 16 above, 4 below |
| Lead | Geist | 12 / 20 | 450 | One per chapter, under the H1 |
| Body | Geist | 10 / 16 | 400 | Measure 60 to 72 characters (about 34 em), ragged right, no hyphenation |
| Caption | Geist | 8 / 12 | 500 | ink-2, 30 to 45 characters per line |
| Code, keys | Geist Mono | 8.5 / 12 | 500 | Files, commands, node types, values with units |
| Folio, kicker, table head | Geist Mono caps | 7 / 12 | 500 | +0.12 em; ink-3 |

Sentence case in headings. Node names exactly as the app spells them. Numbers, space, unit (120 BPM, 4.00 ms). British spelling in prose. No em dashes: use a colon, a comma or a full stop (the builders replace ` — ` automatically).

## Spacing

| Between | Space |
|---|---|
| Paragraphs | 8 pt |
| Paragraph and list, list items | 8 pt, 4 pt |
| Block (table, figure, code, card) and neighbours | 16 pt |
| Cards in a column | 12 pt; grid gap 12 pt |
| Section (H2) to previous content | 24 pt |
| A heading never ends a page (`break-after: avoid`); a figure, card, table row and code block never split (`break-inside: avoid`) | |

## Components

| Component | Rule |
|---|---|
| Folio | Fixed at the bottom, mono caps: left "INFINITE / <document> / <chapter or section>", right "<version>"; hairline rule above, 7 pt |
| Kicker | Mono caps 7 pt in Ember over the H1: "CHAPTER 21", "SECTION 03" |
| Card | Surface fill on the ground, radius 12 pt, padding 12 pt 16 pt, no border; name in H3, family tag in folio style on the right |
| Figure | The shot is framed by `frame_shot.py` (Paper ground, 20 pt corners, two-layer shadow). Max width the text column, max height 4.6 in. Caption below, 4 pt gap: what it shows, then what to notice. Never upscale, crop through a control or add a border |
| Node grid | Two columns, 12 pt gap; each cell a card with the node name (H3) and its category in folio style; image centred |
| Table | Header row: mono caps 7 pt ink-3 on deep; rows 8 pt padding, hairline between rows only, no vertical rules, first column ink, numbers in tabular figures; rows never split |
| Code block | Geist Mono 8.5 / 12 on deep, radius 8 pt, padding 12 pt, wraps, never split; inline code is the same face on deep with 2 pt radius |
| Callout | Surface card with a Caveat word in Ember as its only decoration |
| Contents | Number in Caveat Ember, title Geist 650, hairline dotted leader to the right, one line per entry |

## Checks

`python3 tools/docs-shots/pdf_check.py <file.pdf>` fails on: a font that is not Geist, Geist Mono or Caveat; a page that is not Letter or A4; an em dash in the text. `run.py` runs it on every PDF it builds.

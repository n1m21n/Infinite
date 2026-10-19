# tools/hygiene

Website hygiene checks. Python 3.9, Playwright (Chromium). Each script renders every public page in a real browser at phone (390), tablet (768) and desktop (1440) widths, measures the computed layout, and compares it with `docs/brand/brand.json`. No brand number is typed into a check.

| Script | Checks |
|---|---|
| `padding.py` | padding on the 4 px grid; left and right equal on buttons and cards |
| `spacing.py` | gaps on the grid; equal gaps and equal heights in a row; overlapping siblings |
| `typography.py` | Geist, Geist Mono, Caveat only; size on the web scale (11 px minimum); one hand word per section; measure; no em dashes; British spelling; sentence-case headings |
| `buttons.py` | pill radius, equal height and text size in a row, 44 px touch target on phones, no outlines |
| `layout.py` | page overflow, container centring and gutters, equal card widths |
| `tokens.py` | radius set (24/20, 16/12, 10, pill), motion durations and easing from the brand scale |
| `density.py` | long paragraphs, long intros, text-heavy pages (less information, more UI) |
| `platform.py` | pictures, screenshots, canvases, code and tables that overflow or do not fit on phone or desktop; stretched images |
| `loading.py` | page weight, heavy files, images without size, upscaled or oversized images, lazy loading, layout shift, LCP |
| `run_all.py` | all of the above in one pass |

Flags: `--page index.html` (repeatable), `--viewport phone|tablet|desktop`, `--all` (include generated library pages), `--limit N`, `--json file`. Exit 1 when any error-level rule fires; warnings are advice.

Related: `tools/seo/check.py audit` (SEO of the blog), `tools/brand/brand_lint.py` (colours), `tools/website/sync_chrome.py --check` (one header and footer).

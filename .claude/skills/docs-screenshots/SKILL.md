---
name: docs-screenshots
description: Refresh the Node Reference Manual, Field Language Manual and Headless User Guide with fresh screenshots of every node, panel, dialog and dropdown, and rebuild their PDFs in the brand style. Use at each release, after node UI changes, or when asked to update the manuals or docs screenshots.
---

# Docs screenshots

One command refreshes all three manuals from a built app:

```
python3 tools/docs-shots/run.py --app <path to release Infinite.app>
```

## Before you run

1. Never build for this. Use an existing release build (`--app`, or `$INFINITE_APP`). The runner refuses while cmake, make or clang is running; do not kill other sessions' processes, wait.
2. Smoke first: `python3 tools/docs-shots/run.py --app <app> --limit 5 --category Source --manuals node`. Subset runs write to `docs/shots/out/` and never touch the shipped PDFs. Open the PDF and check it.
3. Blank personal paths in the app (the Output node shows the Desktop export path) before a release run: the brand screenshot standard forbids them.

## Node screenshots must be the zoomed capture

The Node Reference Manual and the website use `docs/node_screenshots/`. Retake them only with `python3 scripts/screenshot_all_nodes.py` (zoom is the default, about 2x: 268 px wide for Volume, not 134). Never pass `--no-zoom` for the manual, website or blog. The `nodeshots` preflight check fails when the median shot is under 250 px wide. After recapturing, run `python3 tools/manual/build.py` and check one card in the PDF at full size before pushing.

## What it does

capture (env hooks, no UI scripting) -> frame (Paper ground, `tools/brand/frame_shot.py`) -> `docs/shots/manifest.json` -> build the three PDFs -> copy to `website/assets/` and `press_kit/manuals/`. Details and shot ids: `tools/docs-shots/README.md`.

## After a full run

- `git status`: shipped PDFs and `docs/shots/manifest.json` changed; shots stay local (gitignored).
- Add `{{shot:<id>|caption}}` lines to `docs/headless/GUIDE.md` where a figure helps. New Field nodes go in `FIELD_NODES` in `tools/manual/field/build_addendum.py`.
- Chapters 1-20 of the Node Reference Manual are a frozen PDF with no source; the Node Atlas covers every node instead.
- Do not hand-edit generated PDFs or styling; change `tools/docs-shots/common.py` (`brand_css`) and rerun.

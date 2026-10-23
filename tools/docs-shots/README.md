# Docs screenshots

`python3 tools/docs-shots/run.py --app <Infinite.app>` refreshes the three manuals from the live app.

| Step | What | Where |
|---|---|---|
| Capture | Every node (closed `c`, params open `o`, first dropdown open `d`), panels, windows, dialogs, UI sheet, driven by env hooks (`INFINITE_NODEGALLERY`, `INFINITE_OPENPANELS`, `INFINITE_OPENDIALOG`, `INFINITE_UIGALLERY`) | `docs/shots/raw/` |
| Frame | `tools/brand/frame_shot.py` Paper ground, 20 pt corners, two-layer shadow; plus a JPEG sized for PDFs | `docs/shots/framed/` |
| Manifest | id, kind, category, name, state, theme, paths, app version | `docs/shots/manifest.json` (committed; images are not) |
| Node Reference Manual | each card in chapters 21+ gets its node shot; a generated Node Atlas lists every node by category. Chapters 1-20 are a frozen PDF with no source | `tools/manual/build.py` |
| Field Language Manual | the whole manual from `docs/field/MANUAL.md` | `tools/manual/field/build_field.py` |
| Headless User Guide | `{{shot:<id>|caption}}` lines in `docs/headless/GUIDE.md` become figures; missing shots are dropped | `tools/headless-docs/build.py` |

Styling comes from `common.brand_css()` (Brand v2 Paper tokens, Geist, Geist Mono, Caveat from `art/brand/fonts`).
Shot ids: `node/<Category>/<Name>/<c|o|d>`, `panel/<x>`, `window/<x>`, `dialog/<x>`, `ui/gallery-<dark|light>`.

Rules: use a release build (`--app` or `$INFINITE_APP`), never build from here, and the runner refuses while a build is running.
Known gap: the Output node shows the user's Desktop path in its export field; blank it in the app before a release run.

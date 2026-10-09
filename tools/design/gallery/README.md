# Node gallery: shoot every node, headless

Used by `docs/plans/ui-system/node-polish-brief.md`. Never UI-script the canvas; these drive the app's own env hooks.

| Step | Command | Output |
|---|---|---|
| 1. List every node type | `INFINITE_NODELIST=1 INFINITE_EXITAFTER=5 ./build/Infinite.app/Contents/MacOS/Infinite > $OUT/list.txt` | `NODELIST Cat|Name` lines |
| 2. Shoot each node alone, params closed (`c`) and open (`o`) | `python3 tools/design/gallery/shoot.py [Cat ...]` | `$OUT/one/<Cat>__<Name>__<c|o>.png`, cropped to the node |
| 3. Contact sheets per family | `python3 tools/design/gallery/sheet.py <Cat> <c|o>` | `$OUT/sheets/<Cat>-<c|o>-N.png` |
| 4. Width growth when params open | `tools/design/gallery/sizes.sh > $OUT/sizes.txt` then `python3 tools/design/gallery/widen.py $OUT/sizes.txt` | nodes that get wider, worst first |

`OUT` defaults to `/tmp/node-gallery`. Theme: `THEME=light` (default `dark`).
Baseline from 2026-10-09 (before the polish pass): `~/infinite-node-gallery/` (shots, sheets, sizes).

# Showcase and template patches

How `docs/showcase/infinite-v1-showcase.inf` was built, so the next showcase is made the same way. Repo-only reference for the `infinite-patch-authoring` skill; it does not ship in the AI skill copy. Worked example: `docs/showcase/infinite-v1-showcase.inf` and `docs/showcase/SHOTS.md`.

The Template library has two tiers (`tools/templates/gen_templates.py` writes both, plus `assets/templates/index.txt`, lines `slug|group|title|blurb`):

| Tier | What it is | Size | Source |
|---|---|---|---|
| Starters | One idea each, explained with Comment nodes | 3-6 nodes besides comments (the generator asserts it) | `T(...)` entries in `gen_templates.py` |
| Showcase | A full piece that plays on its own: sound, picture, arrangement, Session, Perf page, media | As big as the piece needs | A patch in `docs/showcase/` listed in `SHOWCASE` in `gen_templates.py` |

## Building a showcase patch

1. **Plan two bands.** A Sound band (sequencers, piano rolls, synths, effects, mixer, limiter, Audio Out) and a Picture band (Field, 3D scene, effects, Output), left to right by wiring depth. Modulators and Audio to CV sit between them. Put real music in it: three or four Piano Rolls, a Drum Sequencer, a Sampler. Never a stub that only validates.
2. **Write it** with the loop in `SKILL.md`: `--describe` every type, smallest patch first, `f seed <n>` on every Random and S&H.
3. **Annotate.** One header Comment per band (`# band Sound`, `# band Picture`) and one note per node group (`# near <id>`), then `python3 tools/patch-layout.py` for a hand-written patch. A patch that was laid out by hand and canonicalized keeps its `pos` lines; `gen_templates.py` then adds its notes in the top margin (`SHOWCASE[...]["notes"]`: x, y, width, text) so the layout is never reflowed.
4. **Arrangement.** Several lanes (one per instrument or picture layer), markers on the section starts (Intro, Groove, Lift, Drop), a loop over the whole piece, and Session scenes with every column filled so a scene launch sounds like the piece. A Performance page with a few Knobs, Faders, Sliders and NumBoxes aimed at the controls a player would want.
5. **Media in a `media/` folder next to the patch**, with paths written relative (`media/01-kick.wav`). The loader resolves node params that name media against the patch's own folder (`MediaCollect::ResolveRelative`, called from `LoadPatchDataImpl`), so the folder travels with the template. Known gap: the Sample Studio's own `studio src path=...` line is not a node param and is not resolved. In the GUI, set it with an absolute path: `--cli studio set "text=<abs path>"` (see `docs/showcase/shoot.sh` and `tools/manual/ui_shots_offscreen.py`). The Sampler node itself plays the file either way.
6. **No personal paths** (`/Users/`, `C:\Users`, `/home/`: `gen_templates.py` refuses them) and a blank Output path (the Output node's `path` and recording keys stay empty).

## Done checks

Run each on the finished template, from a different working directory (so a relative path that only works from the repo root shows up):

| Check | Command | Passes when |
|---|---|---|
| Strict validate | `Infinite --validate patch.inf` | `ok`, no warnings (strict by default) |
| Explain | `Infinite --explain patch.inf` | every node reachable, every wire there, nothing at a default you meant to change |
| Pictures | `Infinite --frame patch.inf 4,12,20 dir/ --contact-sheet sheet.png` | not black (`frame_stats.mean_luma`), Output wired |
| Sound | `Infinite --audio-summary patch.inf out.json --duration 16` | about -16 LUFS integrated, true peak below -1 dBTP, no clipping |
| Template test | `INFINITE_ROUNDTRIPTEST=1 Infinite` (also in `driver.sh --fast`) | reads, every type exists, at least two Comment nodes, a thumbnail exists, write then read keeps the counts |

## Thumbnails

`assets/templates/thumbs/<slug>.png`, 320 x 320. The Showcase group is listed first in `index.txt` (the Templates window groups rows in file order). Showcase tile: a screenshot of the template's own node graph (owner's call, 2026-10-31). `python3 tools/templates/shoot_thumbs.py [slug ...]` writes it: one off-screen dev app per template in its own HOME, `view params on`, `view fit`, the app saves its own frame (`INFINITE_SHOT_REQUEST`), the canvas is cropped to the graph and centred in the tile. Run `gen_templates.py` first, wait for `ibuild.py status` to say the lock is free, and re-shoot whenever the template's layout changes.

Starter tiles: the manuals' own retina screenshot of one relevant node (`docs/shots/raw/<Category>__<Node>__o.png`, made by `tools/docs-shots`; the folder is gitignored, so pass `--shots <path>` from a checkout that has it), trimmed to the node: `python3 tools/templates/node_thumbs.py`. The slug-to-node table is at the top of that script; tall nodes keep their top section. A new starter needs a row in that table.

## Packaging

macOS lists `assets/templates/*.inf`, `index.txt`, `thumbs/*.png` and `media/*` as bundle resources (`CMakeLists.txt`, `MACOSX_PACKAGE_LOCATION`). Windows and Linux `copy_directory` the whole `assets/templates` folder, so new files there ship without a CMake edit. Re-run `cmake` after adding a media file on macOS (the lists are globbed at configure time).

---
name: tool-first
description: Write a script or tool instead of doing repetitive, by-hand or by-eye work - and reuse the ones Infinite already has (headless CLI, RPC, census, audits, render/determinism checks). Use when a task repeats, spans many nodes/files, needs a measurement, or "would take ages by hand".
---

# Tool first

## When to use (full scope)

- A check you will do more than twice (per node, per file, per patch, per commit).
- Anything over ~300 node types, every Draw*Body, every shader, every patch in a folder.
- "Does it look right / sound right / stay the same" - turn the eyeball into a number.
- Verifying UI without clicking the ImGui canvas (owner rule: never UI-script it with Control your Mac).
- Generating content that would otherwise be hand-typed (patches, presets, tables, docs blocks).
- A bug that only shows up after N steps - script the N steps once.
- Before writing a long manual procedure into a skill: could it be one command instead?

## The rule

1. **Look before you write.** Check the inventory below and `tools/`, `scripts/`, `scripts/bench/`. Extending an existing tool beats a new one.
2. **Write it the moment the second repetition appears**, not after the tenth. A 40-line Python script is cheaper than ten rounds of manual reads/screenshots.
3. **Turn judgment into a number or an exit code.** Exit 0 = pass, 1 = found a problem, 2 = tool/usage error (the convention `determinism-check.py` and `render-check.py` use).
4. **Drive the real app headless**, not the GUI: `Infinite --frame / --render / --audio-summary --wav / --describe --json`, or the RPC port (`scripts/live_rpc_smoke.py` shows how).
5. **Generate, don't hand-maintain.** If a table/list must match the code, write the generator (`gen-patch-skill.py`, `audit_node_params.py` are the model) and say "generated" in the target.
6. **Read the code through the tool.** Static checkers read app sources via `scripts/appsrc.py` (main.cpp is split across `src/app/`).

## Where it lives

| Kind | Place | Commit? |
|---|---|---|
| One-off for this session (probe, data crunch, quick diff) | session scratchpad | no |
| Reusable check / generator / audit | `tools/<area>/` or `scripts/` with a docstring: what, why, usage, exit codes | yes, own commit |
| Sweep driver | `scripts/sweep_runner.sh` shape, linked from its `*-sweep` skill | yes |
| A/B or bench route | `scripts/bench/`; route goes in `run-infinite-hygiene` "Efficient routes" (that skill owns the table) | yes |

When a one-off proves useful twice, promote it to `tools/` and add a row below.

Tools must stay **MIT-clean** (no GPL code pasted in), **three-platform aware** when they touch the app (`windows-parity`, `linux-parity`), and never ship debug tooling in release builds.

## Inventory (extend this when you add one)

| Need | Tool |
|---|---|
| Render a patch frame/video/audio headless | `Infinite --frame / --render / --audio-summary`; smoke: `scripts/headless_smoke.sh` |
| Node facts for patch writing | `Infinite --describe --json`; `tools/gen-patch-skill.py` |
| Same output twice? | `tools/determinism-check.py` |
| Golden image/audio regression | `tools/render-check.py` (`tests/render/`) |
| 2D effect quality vs industry formulas (format, headroom, banding, alpha bleed, blur, grade, LUT, blend modes) | `INFINITE_IMGAUDIT=1 INFINITE_EXITAFTER=1 Infinite`; baseline in `docs/plans/image-fx/README.md` |
| Lay out / preview an .inf without GUI | `tools/patch-layout.py`, `tools/patch-layout-preview.py` |
| Every node spawned, wired, checked | `scripts/node_census.py` |
| Node screenshots | `scripts/node_screenshot.py`, `scripts/screenshot_all_nodes.py` |
| Drive a running app | `scripts/live_rpc_smoke.py` (RPC) |
| Modulatable params inventory | `scripts/audit_node_params.py` (`node-param-audit`) |
| Knob range vs DSP truth | `scripts/audit_param_truth.py` (`param-truth-audit`) |
| Every audio effect measured (gain, stereo, tail, stress/NaN, param clicks, 44.1/48/96 kHz consistency, RT60/delay/rate/cutoff truths) | `INFINITE_FXAUDIT=1 [INFINITE_FXAUDIT_ONLY=Name] Infinite` (`src/app/selftest/FxAudit.cpp`; in the hygiene `audio` group) |
| Field preset compiles? | `tools/field_preset_check.cpp` |
| Quality registry, release gate, scoreboard (QUALITY rows from audits/probes/benches) | `tools/quality/quality.py` (`check`, `ingest`, `board`, `--self-test`; skill `quality-system`) |
| Benchmarks / A/B | `tools/bench.py`, `scripts/bench/ab.sh`, `base.sh`, `compare.py`, `rgbadiff.py` |
| Design tokens, glyphs, contrast, goldens | `tools/design/` (`contrast.py`, `golden.py`, `inventory.py`, ...) |
| i18n extract/lint | `tools/i18n/` |
| Linux build + Xvfb rig | `tools/linux/` |
| Architecture recall | `tools/semi-brain/` (`semi-brain`) |
| Templates / drum kit / HRTF generation | `tools/templates/`, `tools/make-drumkit.py`, `tools/spatial/` |
| Brand book, tokens, contrast gate | `tools/brand/build_brand_book.py` (values only in `docs/brand/brand.json`) |
| Logo files (SVG/PNG/favicon/icns/ico) from the formula; icon drift | `tools/brand/build_logo.py [--check] [--install]` |
| 3D nodes, kit, cutouts, baked loops, studio.blend | `tools/brand/build_3d.py [--quick] [--only render,...]` |
| One message at every channel size (OG, YouTube, X, LinkedIn, README, story...) | `tools/brand/channels.py --headline ... --hand ...` |
| Slide deck, release email, press fact sheet | `tools/brand/templates.py` |
| Screenshots on the brand frame | `tools/brand/frame_shot.py shot.png [--mode paper] [--ratio 4:5]` |
| Press kit zip | `tools/brand/press_kit.py` |
| Website Extensions page from the release catalog (one card per pack, Install = live install steps) | `tools/website/gen_extensions.py [--catalog FILE]`, then `tools/website/gen_seo.py` |
| Off-brand / retired colours (ratchet); apply swaps | `tools/brand/brand_lint.py [PATH]`, `tools/brand/migrate_colours.py [--apply]` |
| Confusable category colours per theme + brand.json drift | `tools/brand/category_audit.py` |
| Motion laws (tempo clock, one mass, beat-locked springs, mark arcs, golden phase, log zoom); token grid check | `tools/brand/motion.py [--check]`; import it in films/3D/site |
| Competitor survey: app inventory, plugin probe (latency/THD/alias/CPU vs Infinite nodes), synth probe (pitch/alias/envelope/velocity/CPU vs Infinite synths), UI map (menus/shortcuts via accessibility), capability map (official scripting APIs), app bench (launch/idle CPU/memory/threads), bounce kit (DAW export truth), host audit (every installed AU/VST3 loaded: crash/hang/silent, load time, auval, AU vs VST3), AU out-of-process cost (instantiate/render/CPU in vs out of Apple's hosting service), load bench (N synth+effect tracks, % of one core), stretch kit (time-stretch length/timing/pre-echo/flam/pitch/artefacts from each app's own stretched renders), built-ins kit (DAW-only instruments/effects measured from their own bounces vs Infinite nodes), render bench (movie render fps, H264 PSNR/SSIM vs own lossless frames, 10-min flash+click AV drift; Infinite vs Blender, movies read via AVFoundation `video_read.swift`), Blender bench (geometry nodes / cloth / depsgraph fps vs matched Infinite patches), AV bench (audio→picture latency, beat-to-frame lock, TD live audio-reactive frame times), project bench (N-node patch size and load/save time, old releases' patches opened today, other apps' project containers), workflow cost (ten common tasks: steps and KLM seconds per app, Infinite steps cited to code), round-trip latency through BlackHole, cables/.als/.amxd/.fcpxml/.toe/Blender readers, dossier lint + capability diff, ~/re-output storage | `tools/re-survey/` (`selftest.py`; plan in `docs/plans/re-survey/`) |

## Exit check

- Ran it, it gives a clear pass/fail, its docstring says how to run it.
- If kept: committed separately, row added above, and the owning skill links to it.

# UI audit: mechanical UI/UX bugs, found by one run

Owner approved 2026-11-03: build layers 2 and 3 first. Layers 4 and 5 later.

```
1 code lint ──> 2 one-frame snapshot ──> 3 scripted pokes ──> 4 truth ──> 5 screenshot review
               └──────── this plan ────────┘
```

## How it runs

One launch of the debug build (`python3 scripts/ibuild.py build --mode dev`, app in `build-dev/`):

```
INFINITE_UIAUDIT=1 build-dev/Infinite.app/Contents/MacOS/Infinite
   for each state (every panel, every panel tab, every node body, light + dark theme):
       draw ONE frame offscreen
       listener records every item ImGui adds: id, label, drawn rect, hit rect, flags, window clip
   layer 2: check the records (no input)
   layer 3: for each slider / drag / dropdown / checkbox / colour dot: send fake mouse input, check the value
   write ui-audit/report.tsv + report.md + one cropped screenshot per finding
```

A few hundred frames: minutes, not a click-through.

## The listener

Dear ImGui's test-engine hooks, our own code (do not pull in the Dear ImGui Test Engine; its licence is not MIT):
`#define IMGUI_ENABLE_TEST_ENGINE` in the debug build only, implement `ImGuiTestEngineHook_ItemAdd`,
`ImGuiTestEngineHook_ItemInfo` and `ImGuiTestEngineHook_Log` in `src/app/selftest/UiAudit.cpp`.
Release builds never define it (no debug tools in release).

States: drive the existing panel/tab switches directly (set the open panel and active tab the way the
panel code reads them), not by clicking. Node bodies: one patch holding one node of every type
(the docs-shots list), each body drawn in turn.

## Layer 2 checks (snapshot)

| Check | Rule | Example caught |
|---|---|---|
| duplicate id | two items with one id in one window | Surface and Emission "Colour" opening both pickers |
| hit smaller than drawn | hit rect covers < 95% of the drawn widget | material sliders that don't drag across their whole bar |
| overlap | two interactive hit rects intersect (skips AllowOverlap) | menu bar items sharing 6 px |
| clipped | item rect outside its window/child clip | controls cut off at a panel edge |
| text overflow | the text a control draws (label, slider value, dropdown value) is wider than its box, or an item on a node ends past the node's own edge | dropdown values cut mid-letter; knob captions cut |
| tiny target | interactive item under 12 px either side | |
| off grid | a control in an AudioKnobRow crosses its cell (P1: equal cells of the content width) | |
| contrast text | text under 4.5:1 against the node body or window behind it, per theme | |
| contrast fill | dark theme only: a control's resting fill more than 0.06 luminance from the node body (P10) | |

Text overflow, off grid and the two contrast checks need what the widget drew, which ImGui's item list does not
carry, so the design components report it (`UiAudit::NoteSlider/NoteKind/NoteFill/NoteNode/NoteRow`, no-ops in release).

## Layer 3 checks (pokes)

| Control | Poke | Finding when |
|---|---|---|
| slider | press at 25%, drag to the right end, then the left end, release, undo | press lands off, drag misses max/min, no undo step, undo does not restore |
| dropdown, colour dot | click, then Esc | popup did not open; more than one popup; Esc does not close; patch changed |
| checkbox | click twice | first click changes nothing; opens a popup; patch differs after two clicks |
| any (up to 6 per tab, 12 per panel, 40 per node) | hover, then mouse wheel up and down | patch changed |

"Patch changed" compares the whole patch text (canvas view excluded) before and after; the finding quotes where it first differs.

## Coverage

Both themes (dark first, then light). Per theme: the 3D View's eight side tabs, each paged down, with
`tests/render3d/shadows.inf` open; ten other panels (Settings, Help, Shortcuts, Templates, History, Piano Roll,
Sample Studio, Show Set, Node Panel, Viewport); then one node of every registered type, alone on an emptied
canvas (395 types). Not covered: the Terminal panel, floating editors, node bodies in states other than
freshly spawned (no cables, no media loaded).

Options: `--nodes "Moves,Delay"` or `--nodes none`, `--dark-only`, `--budget SECONDS`. A full run takes ~3 min.

## Output

`ui-audit/report.md`: one row per finding (state, control label, check, numbers) sorted by check, plus a
pass count per check. Exit code non-zero when any finding is new against `tools/ui-audit/baseline.tsv`,
so it can join the hygiene routes. Every fix after that is held by the baseline.

## Built (2026-11-03)

`python3 tools/ui-audit/run.py` (debug build, clean HOME, notices dismissed first because they float over the
panel's lower rows).

- Listener: `src/app/selftest/UiAudit.cpp`; `IMGUI_ENABLE_TEST_ENGINE` is set for Debug only (CMakeLists.txt).
- Its mouse (and wheel, Esc) is the only input while it runs: the backend's events are dropped each frame.
- Canary window: one known-bad control per layer-2 check; the run fails if any check stays silent.
- Findings are one row per control (first state it showed in); contrast rows are per theme.

## Results of the first full run (2026-11-03)

9043 item checks and 4398 pokes over 790 states. 5328 findings on the first pass; 5300 of them were the audit
counting the app's menu bar as part of every node (fixed in the audit). What was real:

| Check | Count | What | Outcome |
|---|---|---|---|
| Esc does not close | 37 | node dropdown lists with a search field: the focused field took the first Esc | fixed: Esc closes the list |
| text overflow | 21 -> 2 | knob captions and dropdown values cut mid-letter | fixed: end in an ellipsis; 2 left (Macro Slider "Slider", Macro Bipolar Knob "Pan/Detent"), not investigated |
| tiny target | 5 | the panel resize grips, 8 px | by design: edge grips, wide along the edge |
| overlap | 2 | main menu bar items share 6 px | ImGui menu spacing, harmless |
| clipped | 1 | 3D View list row 6 px past its child | left, cosmetic |
| hover/scroll changes value | 4 | Moves (live gesture input moves with the pointer), Field Graph (same), Render 3D viewport (wheel zooms the orbit camera) | by design |
| popup did not open / changes value | 2 | Drum Sequencer step cell reported as a dropdown | audit misclassifies; not a bug |
| toggle does not return | 3 | 3D View Render "on", Film/Transparent, Light/Casts shadow | not resolved: patch text differs after two clicks (scene arrangement, bake values); needs a narrower fingerprint |
| press lands off / drag misses | 4 | Safe Areas/Action (17% at 25%: integer range); Surface/Metallic in the light theme only: press lands at 0%, no undo step | Metallic open: a control that does not take the press; Safe Areas by design |
| contrast text, off grid, contrast fill | 0 | none found on node bodies in either theme | clean |

Nothing needed a change in `DrawMaterialParams`; the Metallic slider finding is the closest and is in the 3D View
Material tab, light theme.

The 23 that remain are in `tools/ui-audit/baseline.tsv`; a new finding fails the run.

## Done when

- One command runs both layers and writes the report.
- It finds the two known bugs on a build from before their fixes (duplicate colour id; the material sliders).
- The first full report is reviewed with the owner, and fixes go out in batches.

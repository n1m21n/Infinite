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
| overlap | two interactive hit rects intersect | |
| clipped | item rect outside its window/child clip | controls cut off at a panel edge |
| text overflow | label/value text wider than its box | |
| tiny target | interactive item under 16 px either side | |
| off grid | row cells not on the node-ui-pillars P1 grid | |
| contrast | checkbox/dropdown fill outside the P10 budget in dark | |

## Layer 3 checks (pokes)

| Control | Poke | Pass |
|---|---|---|
| slider / drag | press at the left end, drag to the right end, release | value reaches max; and back to min |
| dropdown | click | popup opens; Esc closes it; value unchanged |
| checkbox | click twice | toggles, then back |
| colour dot | click | exactly one popup opens; Esc closes |
| any | hover + scroll wheel without click | value unchanged |
| any edit | undo | value back to before |

Pokes run on a copy of the state and are reset after each control, so one bad control cannot cascade.

## Output

`ui-audit/report.md`: one row per finding (state, control label, check, numbers) sorted by check, plus a
pass count per check. Exit code non-zero when any finding is new against `tools/ui-audit/baseline.tsv`,
so it can join the hygiene routes. Every fix after that is held by the baseline.

## Built (2026-11-03)

`python3 tools/ui-audit/run.py` (debug build) runs it in ~15 s: the 3D View's eight side tabs, each paged
down, ~2400 item checks and 55 slider pokes. It opens `tests/render3d/shadows.inf` with a clean HOME and
dismisses notices first (they float over the panel's lower rows).

- Listener: `src/app/selftest/UiAudit.cpp`; `IMGUI_ENABLE_TEST_ENGINE` is set for Debug only (CMakeLists.txt).
- Its mouse is the only mouse while it runs: the backend's events (the real cursor) are dropped each frame.
- Canary window: one known-bad control per layer-2 check; the run fails if any check stays silent.
- Built checks: duplicate id, clipped (horizontal), tiny target (<12 px), overlap (skips AllowOverlap items),
  hit smaller than drawn, and the slider pokes (press lands, max, min, undo step, undo restores).
- Not yet: text overflow, off grid, contrast, dropdown/checkbox/colour pokes, hover + scroll, node bodies,
  other panels, dark theme.

First run found and fixed: Shadows and Ambient Occlusion "Strength" sharing one id (the second could not
be dragged); every 3D View slider's undo returned to the clicked value, not the one before the press.
The five that remain are in `tools/ui-audit/baseline.tsv`.

## Done when

- One command runs both layers and writes the report.
- It finds the two known bugs on a build from before their fixes (duplicate colour id; the material sliders).
- The first full report is reviewed with the owner, and fixes go out in batches.

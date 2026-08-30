---
name: panels-sweep
description: Sweep Infinite's docked panels for bugs on both macOS and Windows - the modulation matrix (rows, collapsed-node params, disabled rows, scroll/fill geometry in all four dock orientations, patch round trip), the performance matrix (macro elements, assignment, persistence), the node browser's sort and filter strip, and node viewport cards. Use after changing any panel, the dock layout, the browser sort/filter controls, the macro element types, or modulation row rendering; when a panel's rows are blank, misaligned, or wrong while scrolling, when a sort or filter control does nothing, when a panel breaks in one dock position only, or before a release as a panel regression gate.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/panels-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means every fixture passed.

## The four panels

| panel | state | fixture |
| --- | --- | --- |
| modulation matrix | `gModMatrixOpen`, `gModMatrixDock` | `MODMATRIXTEST`, `MODMATRIXGEOM` (x4 docks) |
| performance matrix | macro elements, assignments | `PERFMATRIXTEST` (headless) |
| node browser | `BrowserFilterState`, `LibraryFilterCache` | `BROWSERSORTTEST` (headless) |
| viewport cards | `gViewportPanelNodes`, `gViewportPanelDock` | **none** - see the gap below |

Two of the four have a **pure-function half** that runs before `glfwInit()`,
with no GL and no ImGui, which is why they are cheap and identical on both
OSes. The modulation matrix does not, so it is checked live.

## Why the browser check exists at all

`BROWSERSORTTEST` runs synthetic scanner indexes with known names, extensions
and folders through the exact `FilterAndSortSampleEntries` /
`FilterAndSortPluginEntries` / `ILess` + `CategoryColors::SemanticRank`
functions the panel itself calls, and asserts the output order and length.

The bug class it exists for is specific and worth understanding before editing
the panel: a **forgotten `LibraryFilterCache` key**. The results are cached
against a key built from the active filter/sort state; if a new control is not
folded into that key, the cache returns the previous result and the control
silently does nothing. It does not crash, does not throw, and no build-time
check can see it. Add a control to the strip, add it to the cache key, and add
a case here.

## Why the modulation matrix is run four times

`MODMATRIXGEOM` checks that row fill stays stable while the table scrolls. The
scroll/fill maths differs between a side dock and a top/bottom dock, and the
original bug showed up in only one orientation - the right dock, which is why
that is the fixture's default. `INFINITE_MODMATRIXDOCK=0..3` overrides it, and
the driver runs all four.

`MODMATRIXTEST` covers the panel's *semantics* rather than its geometry, and
each of its four numbered tests is a separate historical bug:

1. a real link produces a row at all,
2. a **collapsed** node still registers its params, and `KnownParam` stays
   sticky - otherwise collapsing a node empties its rows,
3. disabling a row freezes the driven value instead of leaving it wherever it
   was last written,
4. `enabled=false` survives the round trip through **both** the binary patch
   (4a) and the text patch (4b) - two separate serializers, so two assertions.

## The gap: viewport cards

There is no fixture for the docked viewport panel (`gViewportPanelNodes`). The
node-*local* mini viewport is covered by `MINIVIEWPORTTEST`, which renders a
selected and a plain geometry source into a `NodeViewport` and asserts the
selected one is tinted and the plain one is not, plus that the render tracks a
transform change - but the panel that stacks cards left-to-right when
bottom-docked and top-to-bottom when side-docked is unasserted.

The specific untested risks there are card lifetime (a card whose node is
deleted - `gViewportPanelNodes` is scrubbed at `src/main.cpp:19863` and
`:19918`, and it is not serialized or tracked by undo) and the stacking
direction flipping with the dock. If you touch `DrawViewportPanelContainer`,
that is unguarded code.

## Windows parity

- `PERFMATRIXTEST` and `BROWSERSORTTEST` are pure C++ and must give identical
  verdicts on both OSes; a divergence is a real cross-platform bug in sorting
  or in the macro maths, not a UI difference.
- The live modulation-matrix fixtures drive real ImGui, so running them on
  Windows genuinely re-tests the panel under a different DPI and input path.
  Dock geometry is where a DPI difference would show, which is what the
  four-orientation run is for.
- The old spelling `INFINITE_PERFPANELTEST` is kept as an alias for
  `INFINITE_PERFMATRIXTEST`; either works in a shell history on either OS.

## Reading a failure

- `SUSPECT` from `MOD MATRIX GEOM` in **one** dock orientation only - the fill
  maths, not the row model. The log prints `rows=` and `firstRows=` before the
  verdict; compare them across the four runs.
- `SUSPECT` from `MOD MATRIX TEST` - read which numbered test printed a `- BUG`
  line above it; they are four independent bugs.
- `BROWSERSORTTEST <what>: FAIL` - the failing assertion names the sort or
  filter it was checking. A newly added control that fails here almost always
  means the cache key, not the comparator.
- `[PERF MATRIX TEST FAIL] ...` - names the macro element type or the node it
  could not instantiate. The node-instantiation half walks the whole registry,
  so a failure there can be a broken node rather than a broken panel.

## What this sweep does not cover

- **Viewport cards**, as above.
- **No layout/pixel assertions.** Nothing checks that a panel looks right, only
  that its data and geometry maths are consistent.
- **Panel resizing and drag-to-redock are unasserted.**
- **The modulation matrix's *contents* across node types** are covered by
  `modulation-sweep`, not here. This sweep is about the panel; that one is
  about what the panel is showing.

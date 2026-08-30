---
name: output-projection-sweep
description: Sweep everything that leaves Infinite's own window for bugs on both macOS and Windows - Syphon (macOS) and Spout (Windows) publishing and receiving, the Projection node, Output/Viewport terminal nodes, and "open in new window" projector windows including their per-context GL resource lifetime. Use after changing SyphonOutNode, SyphonInNode, ProjectionNode, OutputNode, the projector window code, or anything under src/platform/ that touches windows or texture sharing; when a receiving app sees a black, flipped, or stale frame, when the projector window is the wrong size or not borderless, when closing a projector window corrupts nodes on the canvas, or before a release that ships output.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/output-projection-sweep/driver.sh
```

Static checks alone, no build, ~1s:

```bash
python3 .claude/skills/output-projection-sweep/check.py
```

## The honest state of coverage

This is the least fixture-covered area in the app, and pretending otherwise
would be the worst thing this skill could do. Inter-app texture sharing is by
definition a *two-process* feature; a single-process fixture can only ever
check part of it, and the projector window has no fixture at all. So this sweep
is deliberately weighted towards **static parity checks plus a documented
by-hand procedure**, not towards a green run.

| surface | automated | by hand |
| --- | --- | --- |
| Spout publish/receive (Windows) | `SPOUTLOOPTEST` in-process loopback | second real app |
| Syphon publish/receive (macOS) | none - `SPOUTLOOPTEST` SKIPs | second real app |
| Syphon/Projection/Output nodes cook | `IMAGERESYNTH_SELFTEST` | - |
| Platform seam parity | `check.py` | - |
| projector windows | **none** | the procedure below |

## Spout loopback, and why macOS SKIPs

`INFINITE_SPOUTLOOPTEST` creates a Spout server and client in the same process,
publishes a 64x64 synthetic texture, receives it back, and compares pixels. The
test image is a **diagonal split** - red in the top-left quadrant, blue
elsewhere - specifically so a *vertical or horizontal flip* bug is caught, not
just a wrong-colour one. A plain top/bottom split would miss a horizontal flip
entirely.

It needs a live GL context, so it runs after `glfwCreateWindow` + `gladLoadGL`,
not in the pre-GLFW dispatch block that most headless fixtures use.

It **skips rather than fails** in two cases, both intentional:

- On macOS - Syphon needs no loopback fixture; it is real inter-app tech
  exercised by using it.
- On a Windows machine without `WGL_NV_DX_interop2` - the fixture cannot tell
  "no interop hardware" from "something is broken" without real hardware to
  compare against, so per the spec's fail-soft requirement it treats "never got
  a frame" as SKIP.

A SKIP is graded as a pass. The consequence to keep in mind: **a green run of
this sweep on macOS says nothing about Spout**, and a green run on a machine
without interop says nothing either. Read the log line, not just the verdict.

## The platform seam check

`check.py` enforces the rule that makes any of this maintainable: the OS-specific
part lives behind `Platform::` and nowhere else.

1. **Two implementations.** Every function declared in `src/platform/Platform.h`
   *outside* a `_WIN32` guard must exist in both `src/platform/Platform.mm` and
   `src/platform/win/*.cpp` (131 functions as of writing). One-sided means it
   fails to link, or silently no-ops, on the other OS. Declarations inside an
   `#if defined(_WIN32)` block - `ConfigureOutputWindow`,
   `ReassertOutputWindowTopmost`, `SetWindowIconFromResource` - are Windows-only
   by design and are skipped.
2. **No `_WIN32` in `src/nodes/`** (0 uses as of writing). A node that branches
   on the OS has to be re-tested per OS forever.

`ConfigureOutputWindow` is worth reading before touching projector windows: it
applies the borderless/topmost/cursor policy *and* the move/resize in one
`SetWindowPos`, because `glfwSetWindowSize` sets the **client** area - sizing a
still-decorated window to the monitor's video mode and only then stripping the
frame leaves a borderless window larger than the monitor, overhanging
bottom-right. macOS has no equivalent problem, so main.cpp's non-Windows branch
deliberately still sizes first. That asymmetry is intentional; do not
"simplify" it.

## Projector windows: the trap that has no fixture

`DestroyProjectorWindow` (`src/main.cpp:24981`) makes the projector window's
**own** context current before releasing anything. This is not tidiness:

> VAO and FBO names are per-context - a share group does **not** share container
> objects - and they are allocated from 1 upward in every context
> independently.

A `NodeViewport` rendered inside a projector window generated its
VAO/FBO names in *that* context. Deleting them with the main context current
does not delete nothing; it deletes a live, identically-numbered FBO or VAO
belonging to an unrelated node in the editor. That is why closing a projector
window used to visibly corrupt other nodes on the canvas.

Nothing automated covers this. The by-hand check, on both OSes:

1. Open a patch with several nodes previewing on the canvas.
2. Right-click a geometry node -> open in new window.
3. Confirm the projector window is borderless, topmost, cursor hidden, and
   exactly fills its monitor (this is the Windows-specific sizing bug above).
4. Close the projector window.
5. **Look at the other nodes on the canvas.** Any node that goes black or
   garbled is the per-context deletion bug returning.
6. Repeat with two projector windows open at once, closing them in both orders,
   then quit with one still open (`CloseAllProjectorWindows` path).

## Windows parity

This whole sweep is the platform-divergence area, so treat a one-OS run as half
a result:

- `SPOUTLOOPTEST` is only meaningful **on Windows, on hardware with
  `WGL_NV_DX_interop2`**. Run it there or you have not tested Spout.
- Syphon has no automated check at all - test it by publishing from Infinite
  and receiving in a second real app.
- The projector window sizing bug is Windows-only in origin but the
  corrupted-canvas bug is not; do step 5 above on both.
- `check.py` is source-only and gives the same verdict either side, which makes
  it the one part of this sweep you can trust from a single machine.

## What this sweep does not cover

- **No second process.** Real inter-app sharing - the entire point of Syphon
  and Spout - is untested by anything automated here.
- **No multi-monitor.** Monitor selection, mixed-DPI and mixed-refresh setups
  are unexercised.
- **No output quality/colour check.** Nothing verifies colour space, bit depth,
  or that the published texture matches what the editor shows beyond
  `SPOUTLOOPTEST`'s 64x64 loopback.
- **No NDI or other protocols** - only Syphon/Spout and the projector window.

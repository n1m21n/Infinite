# Fix brief: interface must scale on every OS and every monitor

Reported by a user: "interface does not scale with monitor resolution. Unusable at higher resolutions."
OS and scale factor were not stated. Treat all three platforms as in scope.

Branch: `bugfix/ui-scale-hidpi` (already exists, off main at bc56c65, one commit f53116a). Do not touch main.
Skills to load first: `windows-parity`, `linux-parity`, `node-ui-pillars`, `node-ui-sweep`, `bug-blast-radius`, `run-infinite-hygiene`.

## What is already done (commit f53116a - keep it)

`src/core/UiScale.h` (`UiScale::Resolve`) picks the DPI math from the framebuffer, not the OS: if
`glfwGetFramebufferSize` is wider than `glfwGetWindowSize` the platform already enlarges the picture
(Retina, Wayland) and text keeps its point size; otherwise (Windows, X11) text and ImGui style grow by
`xscale * manualScale`. Used in the font block of `src/main.cpp` (~line 72260). Before this, Linux took the
macOS branch, baked the font at `xscale` and divided it back out, so text stayed 15 px on hi-DPI X11
(Windows had the same bug until #21, commit 440e69d). Logic was checked for 8 cases with a standalone
program (macOS 1x/2x, Windows 100/150/200%, X11, Wayland, manual slider). **Not seen on a real hi-DPI display.**

## What is still wrong (verified by reading, not by running)

**Layer 2 - custom-drawn sizes ignore the scale.** `ImGuiStyle::ScaleAllSizes` only scales ImGui's own
style. Layout in `src/main.cpp` uses literal pixels:
- `kAudioNodeWidth = 440`, `kAudioNarrowWidth = 200`, `kWideNodeWidth = 476`, `kAudioWideWidth = 960`,
  `kAudioHalfWidth = 214` (lines ~330-345; 66 uses of the first two), and about 26 more `k...Width/Height/Size/Pad`
  constants; the mixer comment mentions a 56 px knob; the minimap uses `kNodeW = 260, kNodeH = 90` (~49894).
- At 200% the font doubles but a node stays 440 px, so rows built from font-sized widgets can overflow or clip.
  This is the likely cause of "unusable at higher resolutions" on Windows/Linux. I did not confirm it visually.

**Layer 3 - no live rescale.** There is no `glfwSetWindowContentScaleCallback` and the font atlas is baked once at
startup, so dragging the window to another monitor or changing the OS scale does nothing until restart.

## Work, in order

1. **One scale accessor.** Add `UiScale::Factor()` (current effective layout scale: `xscale * manualScale` on
   pixel-for-pixel platforms, `manualScale` on point-based ones). Store the resolved value once at startup.
2. **Route the shared layout constants through it.** Start with the node-width constants and the audio body scope
   (`BeginAudioBody`/`EndAudioBody`), then knob/slider widths passed to `SetNextItemWidth`/`ImVec2` in node bodies,
   the minimap, node-editor padding and pin sizes. Convert constants to a function or a per-frame value; do not
   sprinkle `* scale` at call sites. Keep every `node-ui-pillars` symmetry/grid rule true at 1x, 1.5x and 2x.
3. **Live rescale.** Add a content-scale callback that re-resolves `UiScale`, rebuilds the font atlas
   (`io.Fonts->Clear`, re-add Inter + Lucide, recreate the GL fonts texture), and re-applies the style from a
   saved copy of the unscaled style (calling `ScaleAllSizes` twice compounds). Per-window: the projector and
   panel windows (`glfwGetFramebufferSize` is read at ~103110 and ~103216) must be checked too.
4. **Open design choice (my recommendation, not verified):** scale constants (step 2) rather than setting the
   node editor's canvas zoom to the DPI factor. Zoom would change interaction semantics and interact with the
   user's own zoom; constants keep zoom meaning "the user's zoom". If you find zoom is trivially correct, say so
   and justify before switching.
5. **Test without a hi-DPI screen.** Add a fixture that lays out representative nodes (an audio node, a wide
   3D node, the mixer, Wavetable) at scale 1.0, 1.5 and 2.0 through the headless path and asserts no body is
   wider than its node and no row overflows. Extend `core/UiScale.h`'s standalone checks into that fixture
   rather than a throwaway file.
6. **Platform parity.** `src/main.cpp` may use `_WIN32`; nodes may not. If anything needs the OS DPI
   (per-monitor awareness on Windows, `Xft.dpi` on X11), it goes behind a `Platform::` function with macOS,
   Windows and Linux sides. Confirm Windows is declared per-monitor-v2 DPI aware in its manifest, otherwise
   `xscale` is always 1 there and nothing above helps.

## Out of scope

Redesigning node layouts, changing the default look at 1x, or touching audio/video code. The 1x appearance on
every platform must be pixel-identical to today; verify by headless render of the reference patches
(`tools/render-check.py`).

## Done means

- `cmake --build build -j"$(sysctl -n hw.ncpu)"` compiles clean, `run-infinite-hygiene` is green.
- The new layout fixture passes at 1.0, 1.5, 2.0.
- CI green on all three platforms for the branch (macOS, Windows x64/ARM64, Linux gcc/clang).
- State plainly in the commit message what was only reasoned about (anything not run on a real hi-DPI
  Windows or Linux display).

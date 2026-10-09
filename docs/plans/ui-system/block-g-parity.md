# Block G: Windows and Linux parity for the UI

Method: code read of every path the Block A/B/D/F/E UI changes touch, plus macOS runs. **Nothing here was run on Windows or Linux.**
The Linux container image (`infinite-linux-dev`) is not built on this machine (a from-scratch build is a long job) and Windows only runs through CI.
Both are listed under "Not verified" below, not as passes.

Rule held throughout: fixes go in shared code, never behind `#ifdef` in a component.

## Differences table

| Area | macOS | Windows | Linux | Status |
|---|---|---|---|---|
| UI font | Bundled Inter first, system fonts only if Inter is missing | Bundled Inter; if missing, ImGui default font | Same as Windows | Same face on all three; the only fallback gap is a missing bundle file (then Latin-only, no crash) |
| Glyph coverage | Latin, Greek, Cyrillic, punctuation, arrows, currency; CJK merged from bundled Noto subset | Same code path | Same code path | Shared (`Startup.cpp` ranges and merge stack), no per-OS branch |
| Icons | Lucide plus Infinite Glyphs merged into every weight | Same | Same | Shared |
| HiDPI / UI scale | `glfwSetWindowContentScaleCallback` calls `UiScale::RequestRescale` (`Startup.cpp`) | Same callback | Same callback | Shared. `UISCALETEST` has one known failure (Render 3D width at 1.25), already in the leftovers list |
| Cmd vs Ctrl | `MODKEY` is `Cmd` | `Ctrl` | `Ctrl` | One macro in `AppCommon.h`; menus and tooltips build labels from it |
| File dialogs | `Platform::OpenPatchDialog` in `Platform.mm` | `PlatformWin.cpp` | `PlatformLinux.cpp` | Three implementations behind one `Platform::` signature; not exercised off macOS |
| Panel docks and open panels across restart | Dock side/size and open state were not saved; now `panel*` keys in `Infinite.workspace-settings` (`PatchIO.cpp`, debounced) | Same file, same code | Same file, same code | **Fixed** in shared code. Not exercised across a restart on Win/Linux |
| Projector on a display with another scale | Framebuffer size is read every frame before drawing | Same | Same | Shared; redraw size follows the framebuffer, not the window size |
| **Display unplugged under a fullscreen projector** | Window kept its stale fullscreen state, nothing re-placed it | Same | Same | **Fixed** (below) |
| **Display returns** | Output stayed where the OS had left it | Same | Same | **Fixed** (below) |
| Popup placement near screen edges | ImGui clamps popups to the viewport | Same | Same | Shared ImGui behaviour; not walked in every menu |

## Fixed in shared code

`UpdateProjectorsForMonitors()` (`src/app/graph/Projector.cpp`), called once per frame before projectors render:

| Event | Before | Now |
|---|---|---|
| Fullscreen display disappears | Window kept `fullscreen = true` against a monitor index that no longer existed | Drops to its windowed box on a remaining display, keeps rendering, remembers the display name |
| That display comes back (same name) | Nothing | Goes fullscreen there again |
| Monitor list reorders | `monitorIndex` went stale | Re-resolved by name |

Polled instead of a GLFW callback so there is one code path on all three platforms, and the cost is a string compare on a cached monitor list. `ProjectorWindow` gained `monitorName` and `returnToMonitor`.

## Not verified (honest list)

| Item | Why |
|---|---|
| Gallery sheets on Linux | Container image not built here |
| Gallery sheets on Windows | CI only |
| Hot-plug fix with a real second display | No second display on this machine; the logic was read through, not run |
| Fonts, popup edges, dialogs, restart persistence on Windows and Linux | Code read only |
| Mixed-scale displays | No hardware |

## Follow-up if the owner wants it checked for real

1. `tools/linux/local.sh build` then `local.sh test` (builds the image first).
2. Push the branch and read the Windows CI job's gallery output.
3. Unplug a second display with a projector fullscreen on it and plug it back.

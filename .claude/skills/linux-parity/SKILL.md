---
name: linux-parity
description: How to write code for Infinite that works on Linux — the three-sided obligation every `Platform::` function now carries, the container+Xvfb rig that lets you actually execute Linux code instead of only reading it, what llvmpipe rendering does and does not prove, and the per-subsystem trap catalogue (fontconfig/FreeType cost, no HarfBuzz, dlopen-don't-link, XDG paths, no bundle chdir, ALSA/PipeWire choice, AppImage glibc floor). Use before adding or changing anything in `src/platform/`, before adding a `Platform::` function, when touching audio device / MIDI / video / camera / text / dialog / crash-handler code, when working through a phase in `docs/plans/linux/`, when reviewing or fixing a Linux-only defect, or when a user reports something that works on macOS and not on Linux. Not for macOS-only work, and not a substitute for `run-infinite-hygiene`.
---

Paths are relative to the repo root.

**Read `windows-parity` first if you have not.** Linux is the *third* implementation
behind the same `Platform.h`, and roughly two-thirds of that skill — the
one-abstraction rule, why the node layer stays conditional-free, byte-identical
signatures, GLSL 330 strictness, denormal guards — applies verbatim here. This
skill covers only what is *different* about Linux, plus the traps Linux has that
Windows does not.

**The one structural difference, and it changes your whole posture: you can run
Linux.** `tools/linux/local.sh` builds and executes in a container on this
machine. Windows defects in this repo were all found by reading or by a user;
Linux defects should be found by *executing*. If you are reasoning about whether
Linux behaves a certain way, stop and run it — the round trip is minutes.

`docs/plans/linux/README.md` is the live phase plan and status table. Check it
before starting: it records what is genuinely verified versus what is still
stubbed, and its "unverified" column is deliberately honest about what the rig
cannot reach.

---

## 0. The shape

```
src/nodes/**              0 occurrences of __linux__   <- load-bearing, same as _WIN32
src/main.cpp              0 occurrences
src/platform/Platform.h   0 occurrences
src/core/SysInfo.cpp      2 occurrences                 <- diagnostics only
src/platform/linux/*.cpp  the Linux implementation (~2,180 lines, 9 files)
src/platform/common/*.cpp shared by Windows AND Linux
```

Two things to notice.

**Linux is stricter than Windows in the node layer.** `_WIN32` appears 11 times
in `main.cpp`; `__linux__` appears zero times anywhere outside `SysInfo.cpp`'s
diagnostic print. Keep it that way. Every Linux behaviour so far has fit behind
either an existing `Platform::` function or the portable `#else` branch that
Windows already needed — which is the strongest evidence the abstraction is
right. A `#if defined(__linux__)` in a node is a design failure, not a
platform necessity.

**`src/platform/common/` is new and is the preferred landing site.** When
Windows and Linux need the same thing, it goes there rather than being copied:

| File | What it shares |
|---|---|
| `common/MediaDecodePortable.cpp` | dr_libs / stb / tinyexr decode paths, and `OpenIfstreamUtf8` |
| `common/SubjectMaskOnnx.cpp` | ONNX matting, with a `ProviderHook` seam — Windows registers DirectML, Linux runs CPU EP |
| `common/PathOpen.h` | `OpenIfstreamUtf8`, the one correct way to open a file by UTF-8 path |

Before writing a Linux implementation, check whether the Windows one is
platform-specific or merely *lives* in `win/`. If it is portable, move it to
`common/` and add it to both `WIN32_SOURCES` and `LINUX_SOURCES` — that is how
`SubjectMaskOnnx` happened, and it deleted code rather than adding it.

---

## 1. Read these before writing code

| File | Why |
|---|---|
| `src/platform/Platform.h` | the whole contract, all three implementations |
| `src/platform/linux/PlatformLinux.cpp` | the core surface — paths, dialogs, HTTP, clipboard |
| `src/platform/common/PathOpen.h` | UTF-8 file opening; use it, don't hand-roll |
| `docs/plans/linux/README.md` | phase plan, status table, and the verified/unverified split |
| `docs/plans/linux/phase-0N-*.md` | exit criteria for the phase you are in |
| `CMakeLists.txt:508` | `LINUX_SOURCES` |
| `tools/linux/deps-apt.sh` | the declared dependency set — if you need a new lib, it goes here first |
| `.claude/skills/run-infinite-hygiene/known-test-failures-linux.txt` | the shrinking baseline, with a phase tag per entry |

---

## 2. The obligation is now three-sided

Declaring in `Platform.h` and implementing in `Platform.mm` builds clean on your
machine and fails to link on **both** other platforms. Every new declaration
needs:

1. `src/platform/Platform.mm` — macOS.
2. `src/platform/win/<Subsystem>Win.cpp` — Windows.
3. `src/platform/linux/<Subsystem>Linux.cpp` — Linux.
4. `CMakeLists.txt` — `WIN32_SOURCES` and/or `LINUX_SOURCES` (`:508`), only if
   you added a new `.cpp`.

**Signatures must be byte-identical across all three.** A stub that fills
`outError` and returns `false` is a legitimate landing; a missing definition is
not.

Stubs carry their phase tag so the baseline stays auditable:

```cpp
outError = "not yet implemented on Linux (P2)";   // AudioDeviceLinux.cpp:34
```

Use exactly that form — `(P2)` audio/MIDI, `(P3)` media/video/camera, `(P4)`
VST3. `known-test-failures-linux.txt` uses the same tags, and the rule written
at the top of that file is that the baseline must shrink each phase and contain
no P0–P4 entries before P5 ships. A stub without a phase tag is invisible to
that audit.

---

## 3. The trap catalogue

### 3.1 fontconfig and FreeType are expensive — cache everything

The first `TextLinux.cpp` called `FcInitLoadConfigAndFonts()` on *every* font
resolution, up to three times per `RasterizeText`, and paired
`FT_New_Face`/`FT_Done_Face` around every call. Loading the entire system font
configuration to answer "where is Helvetica" is a per-frame cost on a text node.

The pattern now in place, and the one to copy for any new FreeType work:

```cpp
FcConfig* SharedFcConfig()            // one config, process-wide
{
   static FcConfig* sConfig = FcInitLoadConfigAndFonts();
   return sConfig;
}
std::string ResolveFontPath(...)      // memoised on family|bold|italic
FT_Face AcquireFace(const std::string& path)   // faces cached, never FT_Done_Face
```

Faces are deliberately never freed — they live for the process. Do not
"fix" that by adding `FT_Done_Face`; you would reintroduce the cost. Anything
touching these caches takes `gFtMutex`.

### 3.2 The text buffer is PREMULTIPLIED alpha

`RasterizeText` fills `outPixelsRGBA` premultiplied, because `Platform.mm` fills
the same buffer through a `CGBitmapContext` with
`kCGImageAlphaPremultipliedLast` and every consumer downstream assumes it. So
`TextLinux.cpp`'s blend is `dst*(1-a) + src*a` with **no divide by alpha**.

This reads exactly like the straight-vs-premultiplied blending mistake this
codebase has made before (see `compositing-pipeline-sweep` check 4), and a reviewer
has already flagged it once. It is correct. There is a comment saying so at the
lambda — leave it there.

### 3.3 No HarfBuzz — complex scripts render unjoined

Linux text goes FreeType-direct with no shaping engine, so Arabic, Devanagari
and Thai render as unjoined isolated forms. Latin, Greek, Cyrillic and CJK are
fine.

This is a **shared Linux+Windows gap** (GDI+ does no complex shaping either),
not a Linux regression against macOS-only behaviour — which is why it is scoped
out of P1 rather than treated as a defect. If you are asked to fix it, the fix
is HarfBuzz on both platforms at once, not a Linux special case. The header
comment in `TextLinux.cpp` documents this.

### 3.4 Load it, don't link it

`HttpGet` reaches libcurl through `dlopen("libcurl.so.4")` with fallbacks to
`.so.3` and bare `.so` (`PlatformLinux.cpp:421`), failing gracefully when
absent. This is deliberate and is the house pattern for anything not in
`deps-apt.sh`'s hard set: a Linux binary that hard-links an optional library
fails to *start* on a distro that packages it differently, which is a far worse
failure than the feature being unavailable.

Same reasoning drives dialogs: `HasGuiDialogHelper()` probes for
zenity/kdialog/yad/qarma/matedialog on PATH and reports honestly rather than
assuming. `INFINITE_SYSINFO=1` prints the result. If you add an optional
subsystem, give it the same treatment — probe, report, degrade.

### 3.5 There is no bundle, and nothing chdirs for you

macOS's Cocoa chdirs a bundled app to `Contents/Resources` before `main()`.
Linux does not. Neither does Windows. Consequences:

- A relative path means something different on each platform. Resolve through
  `BundledResourcePath()` / `AppPaths.h`, never by assuming a working directory.
- Settings go to `$XDG_CONFIG_HOME` (falling back to `~/.config`), not
  `%APPDATA%`, not `~/Library`. `AppPaths.h` handles this; don't reimplement it.
- Patches use `.inf` on Linux and macOS, `.infinite` on Windows. Loaders accept
  both everywhere.

The bundle difference also bites *tooling*: a screenshot fixture writing a
relative path lands in the CWD on Linux and inside the `.app` on macOS.
`tools/linux/shots.sh` makes `OUT_DIR` absolute for exactly this reason.

### 3.6 Never let a Windows header into `main.cpp`

`src/main.cpp` includes **no** Windows headers, and that is load-bearing rather
than incidental. `wingdi.h` declares a *function* called `Polyline`, which hides
`core/Mesh.h`'s global `struct Polyline` — so every `MeshOps` declaration taking
a `const Polyline&` stops naming a type, and MSVC emits a wall of
`missing type specifier` errors pointing at Mesh.h, nowhere near the include
that caused them.

The way this happens is indirect and easy to miss in review: adding
`#include "platform/common/PathOpen.h"` to `main.cpp` is enough, because on
`_WIN32` that header includes `WinCommon.h`, which includes `<windows.h>`.
`windows-parity` §1 notes that `WinCommon.h` is deliberately on no include
path; a `common/` header that includes it inherits that restriction and must
not be pulled into `main.cpp`.

If you need a portable helper in `main.cpp`, check what it drags in on the other
two platforms first. Often the branch you are writing is single-platform anyway
— the window-icon path is Linux-only, where a UTF-8 path needs no conversion and
a plain `std::ifstream` is both correct and free of the problem.

### 3.7 x86_64 and arm64 do not agree on floating point

The dev container is arm64 on Apple silicon; CI and every shipped AppImage are
x86_64. The two contract floating-point arithmetic differently, so a value can
land on opposite sides of a comparison.

`PHASECTEST`'s metaball watertightness check found this the hard way: identical
6096-triangle meshes, `0` open edges on arm64 and `8` on x86_64. `MeshOps::
BuildWeldMap` already quantised positions to an integer grid before hashing —
that part was fine. The actual cause was upstream, in `EmitTetra`'s
`lerpPoint` (`src/core/Mesh.cpp`): two tetrahedra sharing an edge each
classify their own corners into `inside[]`/`outside[]`, so the same grid edge
could be lerped as `(a, b)` from one tetrahedron and `(b, a)` from its
neighbour. Those two expressions are mathematically equal but not
bit-identical, and x86_64 and arm64 contract the FMA differently, so the
"same" vertex landed a ULP apart — on opposite sides of one of BuildWeldMap's
quantisation boundaries, on x86_64 only. The fix was to canonicalise
`lerpPoint` on corner identity rather than the caller's inside/outside
labelling, so both call sites always evaluate the exact same expression.
Fixed on `feature/weld-map-quantization-fix`; the baseline entry has been
removed.

Two rules follow. Any geometry or DSP code that combines the same value via
more than one code path (here: the same edge crossed from two directions)
must canonicalise first so both paths evaluate an identical expression —
"mathematically equal" is not "bit equal," and downstream bucketing/hashing
will amplify a ULP-level difference into a different bucket. And **local
green on arm64 is not green** — for anything numerical, either push and read
x86_64 CI or run the container under `--platform linux/amd64`, which works on
this machine.

### 3.8 `STBI_NO_STDIO` — `stbi_load` does not exist in this binary

The single `STB_IMAGE_IMPLEMENTATION` (in `src/nodes/EnvironmentNode.cpp`) is
compiled with `STBI_NO_STDIO`, so only the `*_from_memory` loaders link. Calling
`stbi_load(path, ...)` compiles fine and produces an `undefined reference` at
link time.

macOS and Windows never hit it because neither takes those branches — so this is
a Linux-only *link* failure introduced by code that looks correct in review.
Read the bytes with `OpenIfstreamUtf8` and call `stbi_load_from_memory`. The
window-icon path in `main.cpp` is the worked example.

### 3.9 Audio: ALSA is the declared dependency, PipeWire is the reality

`deps-apt.sh` installs `libasound2-dev`, and `AudioDeviceLinux.cpp` is a P2 stub.
Before implementing it, note that on any current desktop ALSA is a *compatibility
shim* over PipeWire or PulseAudio, and the shim's buffer behaviour is not the
low-latency path. This decision is not yet made — make it explicitly, record it
in `docs/plans/linux/phase-02-audio-midi.md`, and do not let it get made by
accident by whoever writes the first `snd_pcm_open`.

The Windows teardown rule applies unchanged and is the highest-severity trap in
this area: **never gate `join()` on your own running flag** — `joinable()` is
the only correct predicate, and a joinable thread nobody joins calls
`std::terminate()` at destruction. See `windows-parity` §3.1.

### 3.10 Packaging: the glibc floor is set by the oldest supported distro

The target is an x86_64 AppImage. An AppImage bundles your libraries but **not
glibc**, so the build host's glibc becomes the minimum version every user needs.
Building on current Ubuntu silently excludes older LTS and Debian stable. Pin
the build image deliberately; do not let it float to `:latest`.

`external/tinyfiledialogs/tinyfiledialogs.c` is in `LINUX_SOURCES` and is C, not
C++ — if you touch build flags, make sure they still apply per-language.

---

## 4. The rig: how to actually run it

```bash
tools/linux/local.sh build          # configure + build in the container
tools/linux/local.sh test           # headless suite
tools/linux/local.sh shell          # interactive
tools/linux/local.sh <cmd>          # arbitrary command in the container
```

Rendering runs under Xvfb with Mesa llvmpipe:

```bash
tools/linux/xvfb-harness.sh --fast                        # smoke tier, 30 checks
tools/linux/xvfb-harness.sh --group ui,3d,compositing     # the render tiers
tools/linux/shots.sh                                      # six per-category screenshots
```

Three things about the rig that are easy to get wrong:

- **`XDG_RUNTIME_DIR` must be set** or GLFW fails at `glfwInit`. The harness
  sets it; a bare `build-linux/Infinite` invocation will not, and the error
  (`XDG_RUNTIME_DIR is invalid or not set`) looks like a code bug rather than a
  missing env var.
- **Run fixtures under a throwaway `HOME`.** imgui-node-editor persists canvas
  pan and zoom, so screenshots taken on a used machine restore whatever the last
  operator had scrolled to — which produced six *blank* reference images that
  all looked like a rendering failure. `shots.sh` mktemps `HOME` and
  `XDG_CONFIG_HOME` for this reason.
- **The container is arm64 on Apple silicon; CI is x86_64.** Local green does
  not prove x86_64. Push and read the Linux job for anything SIMD, alignment, or
  `char` signedness related.

### Screenshots need a content gate, not a size gate

The original gate failed any shot under 20 kB. It was worthless: a blank editor
grab measured 230 kB against a real render's 172 kB, because an empty grid of
hairlines compresses *worse* than actual imagery. Every blank frame passed.

`shots.sh` now counts distinct colours (ImageMagick, else `png-uniq-colors.py`,
else an explicit SKIP). A true empty editor is 183 colours at native resolution;
real renders land 3,391–18,602; the threshold is 600. **Measure at native
resolution if you retune it** — downscaling roughly doubles the count, and
calibrating on shrunk images produced two false positives.

The general lesson, which has now bitten this port twice: a verification step
that can only report success is not a verification step. The app also printed
`wrote <path>` for screenshots it had failed to write.

---

## 5. What CI and the rig actually prove

| Checked | Not checked |
|---|---|
| Builds under Clang **and** GCC on x86_64 | any compiler older than the CI image's |
| `ldd` shows no direct libGL/libGLX/libEGL and no direct libX11 — GLFW picks X11 or Wayland at runtime | that Wayland *works*; only X11 under Xvfb is ever exercised |
| `--fast` 30/30; `ui,3d,compositing` 45 pass / 1 known xfail | every subsystem in `known-test-failures-linux.txt` — audio, MIDI, video, camera, VST3 |
| Headless suite: a real SIGSEGV through `CrashHandlerLinux`, a real TLS request | anything needing a device, a codec, or a plugin |
| Six per-category renders through llvmpipe, colour-gated | **any real GPU driver** — no NVIDIA, AMD or Intel code path has ever run |
| — | real desktops: window management, HiDPI scaling, taskbar icon, real system fonts via fontconfig |

**llvmpipe is a reference rasteriser, and that cuts both ways.** It is stricter
than Apple's GL about some things and more forgiving about others; it will not
catch a driver-specific GLSL rejection, and it has no timing behaviour worth
trusting. A green render group means the pipeline is *logically* correct, not
that it runs on hardware.

Treat a green Linux job the way `windows-parity` §4 tells you to treat a green
Windows job — with the difference that here you can, and should, have executed
the thing locally first.

---

## 6. Exit criterion

```bash
# 1. No Linux-only branch entered the node layer. Must print nothing.
grep -rn "__linux__" src/nodes/

# 2. Every Platform:: declaration you added has all three definitions.
grep -rn "YourNewFunction" src/platform/

# 3. New .cpp files are in the build.
grep -n "platform/linux\|platform/common" CMakeLists.txt

# 4. No stub without a phase tag. Known baseline is 1 legitimate hit:
#    AudioSpikeStart, a P0 throwaway Windows declines too - never phased.
grep -rn "not yet implemented\|not supported" src/platform/linux/ | grep -v "(P[2-4])"

# 5. Linux green.
tools/linux/local.sh build && tools/linux/xvfb-harness.sh --fast

# 6. macOS not regressed — non-negotiable for any shared code.
.claude/skills/run-infinite-hygiene/driver.sh --fast
```

Then push and read the Linux job: **both Clang and GCC must compile on x86_64.**
If your change is in a subsystem §5 says nothing can reach, say so in the commit
message rather than implying green CI covered it.

---

## 7. Siblings

`windows-parity` is the prerequisite, not an alternative — most of the
abstraction discipline lives there and is not repeated here. `run-infinite-hygiene`
is the macOS gate. `new-audio-node`, `new-geometry-node`, `new-effect-node` cover
how a node is built; this skill covers what happens to it on the third platform.
`infinite-code-review` should apply §3 as a checklist whenever a diff touches
`src/platform/linux/` or `src/platform/common/`, and should treat any new
`src/platform/common/` file as a signal to check that *both* `WIN32_SOURCES` and
`LINUX_SOURCES` were updated.

---
name: windows-parity
description: How to write code for Infinite that works on Windows when you can only build and run it on macOS — the one-abstraction rule that keeps `_WIN32` out of the node layer, the two-sided obligation every `Platform::` function carries, and the per-subsystem trap catalogue (WASAPI teardown, WinMM status bytes, GDI glyph outlines, Media Foundation stride, wide paths, static CRT, GLSL 330 strictness) drawn from bugs that actually shipped in this repo. Use before adding or changing anything in `src/platform/`, before adding a `Platform::` function, when touching audio device / MIDI / video / camera / text-outline / Spout code, when reviewing or fixing a Windows-only defect, or when a user reports something that works on macOS and not on Windows. Not for macOS-only work, and not a substitute for `run-infinite-hygiene`.
---

Paths are relative to the repo root.

You cannot run Windows. Every Windows defect in this repo's history was found
by reading code or by a user, never by you executing it — so the whole point
of this skill is to front-load the reading. `docs/WINDOWS_VERIFICATION.md` is
the live defect ledger and Parallels protocol; this skill is how not to add
to it.

**`docs/WINDOWS_COMPATIBILITY_STANDARDS.md` is a design aspiration, not ground
truth.** It states several rules the codebase deliberately does not follow and
cites several open bugs as if they were satisfied invariants. Where it and this
skill disagree, this skill was checked against the tree; verify before quoting
either.

---

## 0. The shape: `__APPLE__` fast path, portable default, no `_WIN32`

```
src/nodes/**            0 occurrences of _WIN32   <- load-bearing
                        4 files (of 129) split on __APPLE__
src/main.cpp           11 occurrences of _WIN32   <- paths, extensions, entry
src/platform/Platform.h 2 occurrences             <- declaring win-only symbols
src/platform/Platform.mm      macOS implementation
src/platform/win/*.cpp        Windows implementation
```

`Platform.h` (829 lines) is a pure C++ declaration surface — no Objective-C,
no `windows.h`, no COM. Two implementations sit behind it and `CMakeLists.txt`
picks one. That is how 167 node types stay buildable on both platforms.

The node layer is not conditional-free, but the conditionals it has all take
the same shape, and the shape is the rule:

```cpp
#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>   // the platform fast path
#else
#include "dsp/PortableFft.h"         // the DEFAULT - everyone who isn't Apple
#endif
```

The four are `AnalyzeNodes.cpp`, `PaulStretchNode.cpp`,
`ImageSpectralSynthNode.cpp` (all vDSP vs `PortableFft`) and `TextNode.cpp`
(CoreText vs GDI+ `GraphicsPath`, because raw GDI `FillPath` is unantialiased
and reads as jagged at text sizes).

Three consequences, and they are the whole reason this section exists:

1. **Never write `#if defined(_WIN32)` in `src/nodes/`.** Zero is the current
   count and it should stay zero. A Windows-only branch in the node layer is a
   code path no macOS build compiles, no macOS test runs, and nobody with
   commit access can execute. Platform behaviour goes behind a new
   `Platform::` function (§2).
2. **The `#else` branch is the default, not the special case.** It is what
   Windows, and any future platform, actually runs. Write it first, and treat
   the `__APPLE__` branch as the optimisation.
3. **The two branches must be numerically equivalent, and only one of them is
   tested.** `PortableFft::Inverse` returned time-reversed audio for months
   while macOS sounded perfect and `INFINITE_DSPTEST` printed `PAULSTRETCHTEST
   OK` on the real Windows runner. If you add a branch here, add an assertion
   that compares the two paths on the same input — not one that only exercises
   whichever branch your machine compiles.

---

## 1. Read these before writing code

| File | Why |
|---|---|
| `src/platform/Platform.h` | the whole contract. Read the doc comments — they say who owns the memory and what the failure return means |
| `src/platform/win/WinCommon.h` | `Utf8ToWide` / `WideToUtf8` / `HrToString`. Deliberately on no include path — only Windows TUs may include it |
| `src/platform/AppPaths.h` | settings/desktop path resolution, shared by both platforms |
| `src/platform/NetCompat.h` | the sockets shim, and a written-up MSVC ICE caused by one careless `#define` |
| `docs/WINDOWS_VERIFICATION.md` | the open-defect ledger. Check it before "fixing" something already written up |
| `CMakeLists.txt` ~400-475 | `WIN32_SOURCES`, static CRT, and the `main.cpp` `/Od` workaround |

---

## 2. Adding a `Platform::` function is a three-sided obligation

Declaring in `Platform.h` and implementing in `Platform.mm` builds clean on
your machine and produces an **unresolved external symbol** on Windows CI.
Every new declaration needs all three:

1. `src/platform/Platform.mm` — the macOS implementation.
2. `src/platform/win/<Subsystem>Win.cpp` — the Windows implementation. A stub
   that fills `outError` and returns `false` is a legitimate first landing;
   a missing definition is not.
3. `CMakeLists.txt` `WIN32_SOURCES` (~401) — only if you added a new `.cpp`.
   Existing files need nothing.

If the symbol genuinely exists on one platform only (rare), it goes behind the
`#if defined(_WIN32)` at `Platform.h:823` and every *caller* is in `main.cpp`,
not in a node.

**Signatures must be byte-identical.** MSVC and clang disagree about nothing
here, which is exactly why a mismatched `const` or a defaulted argument on one
side only turns into a link error you discover twenty minutes into CI.

---

## 3. The trap catalogue

Each of these is a bug that happened here, or is open right now. They are
ordered by how much of the app they take down.

### 3.1 Thread teardown — never gate `join()` on your own running flag

```cpp
// WRONG - a failed device open leaves a joinable thread nobody joins,
// and ~thread() calls std::terminate(). No dialog, no log, hard exit.
if (running.load()) { running.store(false); thread.join(); }

// RIGHT - AudioDeviceWin.cpp:269
running.store(false, std::memory_order_release);
if (stopEvent) SetEvent(stopEvent);
if (thread.joinable()) thread.join();
```

`joinable()` stays true until `join()` or `detach()`, so it is the only
correct predicate. Applies to every worker thread in the Windows layer —
audio render, capture, video decode. This one shipped (ledger 1.2, fixed).

### 3.2 WinMM status bytes — masking channel bits eats System Realtime

```cpp
const BYTE type = status & 0xF0;
switch (type) { ... case 0xF8: /* UNREACHABLE - 0xF8 & 0xF0 == 0xF0 */ }
```

Channel messages (`0x80`-`0xE0`) carry the channel in the low nibble; System
Common/Realtime (`0xF0`-`0xFF`) do not. Branch on `status >= 0xF0` **first**,
then mask. No compiler warns about this — the case labels are valid `BYTE`
values, just never produced. **Still open** at `src/platform/win/MidiWin.cpp:185`:
`gClock.Pulse()` and `gClock.Reset()` are both unreachable, so
`MidiClockIsPresent()` always returns false and `MidiClockBpm()` never moves.

Severity caveat, so you don't over-prioritise it: `MidiClockIsPresent` /
`MidiClockBpm` are declared in `Platform.h:666` and implemented on **both**
platforms but called from nowhere in `src/` — external clock sync was never
wired to the transport on either OS. So today this is a latent bug with no
user-visible effect. It bites the moment someone wires it up, and they will
reasonably assume the platform layer works because macOS does.

### 3.3 One callback thread per device — WinMM rings are MPMC

`PublishNote` (`MidiWin.cpp:170`) uses `ringWrite.load(relaxed)` then a store:
a single-producer pattern. WinMM delivers `midiInOpen` callbacks on a thread
**per device**, so with two controllers connected, two producers claim the same
slot index and notes are lost or torn. `MidiOpenAll` opens **every** device in
a loop at `:290`, each with `CALLBACK_FUNCTION`, so N devices means N producer
threads — confirmed, not theoretical.

Two details worth knowing before you fix it. The locking is inconsistent:
Note Off calls `PublishNote` while holding `gState.mutex`, Note On calls it
after the guard's scope closes — so a Note On on one device still races a
Note Off on another. And `slot.seq` is written but the reader at `:408` never
validates it, so it is currently vestigial. A correct fix claims the index with
`fetch_add` and makes the reader actually check `seq`. **Still open** (ledger
1.8); needs two or more controllers connected to manifest.

### 3.4 GDI glyph outlines — `GGO_GLYPH_INDEX` changes what the first argument means

```cpp
// WRONG - PlatformWin.cpp:398. With GGO_GLYPH_INDEX set, L'H' is read as
// glyph index 0x48, which is a different character in every font.
GetGlyphOutlineW(hdc, L'H', GGO_NATIVE | GGO_GLYPH_INDEX, &metrics, ...);
```

Cap-height measurement takes a *character code*, so the flag must be absent.
The failure is silent by construction: glyph index 0x48 exists in any normal
font, so the call **succeeds**, `size > 0` holds, and the `capHeight <= 0.0f`
fallback at `PlatformWin.cpp:426` never fires. Which glyph you actually measure
is font-dependent, but it is not `H`, so the normalisation constant is wrong.

The per-character loop at `:439` correctly omits the flag, so outlines
themselves are right — the failure mode is *all Windows 3D text uniformly
mis-scaled against macOS*, not garbled text. **Still open** (ledger 1.4), and
of the open items this is the only one a user sees today.

### 3.5 Media Foundation — never assume `stride == width * 4`

Query `MF_MT_DEFAULT_STRIDE` (or `IMF2DBuffer2::Lock2DSize`) and honour it
row by row. MF pads rows for alignment on some widths and some drivers, so
the naive copy is correct on your test clip and skewed on the user's. Fixed
and correct at `MediaWin.cpp:269` — copy that pattern for any new MF surface.
A negative stride means bottom-up; do not `abs()` it away.

### 3.6 Denormals — per-kernel guards, not global MXCSR

The house convention is a tiny bias or an explicit flush inside each kernel:

```cpp
out.channels[ch][i] = in.channels[ch][i] * gr + 1.0e-20f;  // DynamicsKernel.h:135
inline float FlushDenormal(float x) { return std::fabs(x) < 1.0e-20f ? 0.0f : x; }
```

Do **not** add `_MM_SET_FLUSH_ZERO_MODE` — it appears nowhere in `src/`, it is
thread-local state that plugin code can clobber under you, and it does not
exist on the ARM64 Windows target. New feedback paths (delay lines, filter
state, reverb tanks) carry their own guard, same as the twenty existing kernels.

### 3.7 Paths and encodings

- Engine strings are UTF-8 `std::string` everywhere, always.
- In a Windows TU, `fopen` / `std::ifstream` with that string mangles any
  non-ASCII path. Convert with `WinCommon::Utf8ToWide` and use `_wfopen`.
  MSVC's `std::ifstream` accepts a `std::wstring` directly, which is the
  cheapest fix. There is **no UTF-8 `ActiveCodePage` manifest** in this repo,
  so the process ANSI code page is the system default and this conversion
  really does mangle — do not assume modern Windows saves you.
- `MediaDecodeWin.cpp:79` already documents the rule and defines a `WPATH()`
  helper for it; the dr_libs, stb_image and tinyexr paths all honour it. **The
  four hand-rolled parsers in the same file do not**: `LoadModelObj` (:192),
  `LoadModelStl` (:314), `LoadModelPly` (:445) and `DecodeAiff` (:660) pass the
  UTF-8 `std::string` straight to `std::ifstream`. These take user-chosen
  paths from a file dialog, so exposure is high. `ReadFileBytes` (:791) is the
  pattern to copy.
- `PluginVST3Win.cpp` has the same class at :356 (`json.save`), :367 and :418
  (`std::fopen`), plus the `fs::` calls around :1972-2209. These are app-owned
  paths under `%APPDATA%`, so they only break when the *Windows account name*
  is non-ASCII — lower exposure, and they fail closed (the crash sentinel and
  blocklist quietly stop protecting that user rather than crashing).
- Settings live under `%APPDATA%\Infinite` via `AppPaths.h`, which reads
  `APPDATA` then falls back to `USERPROFILE`. **Known gap:** this misses
  OneDrive folder redirection, where the Desktop is not under `USERPROFILE`.
  If you touch that file, `SHGetKnownFolderPath(FOLDERID_Desktop, ...)` is the
  fix — but it is not what the code does today, so don't assume it.
- Patches are `.infinite` on Windows, `.inf` on macOS (`main.cpp:20738`) —
  Windows reserves `.inf` for Setup Information files. Loaders accept both.

### 3.8 Sockets

Include `src/platform/NetCompat.h` and use `NetClose()`. Never
`#define close(fd) closesocket(fd)` — an unscoped macro named `close` also
hits `basic_filebuf::close()` in any TU that includes `<fstream>`, which
produced an internal compiler error in MSVC's "Generating Code" phase. The
header's comment block is the full post-mortem.

### 3.9 Build-level constraints you can break from macOS

- **Static CRT.** `CMakeLists.txt:57` links `/MT`. Any dependency you add that
  forces `/MD` makes the shipped `.exe` die at launch with "MSVCP140.dll was
  not found" on a clean Windows install. CI asserts this by scanning the PE
  import table — the single best guard in the whole pipeline.
- **`main.cpp` compiles at `/Od` on MSVC** (`CMakeLists.txt:458`) because the
  x64 backend ICEs on it at `/O2`. It is 37k lines and growing. Put new code
  in its own TU; every line you add to `main.cpp` is a line Windows runs
  unoptimised, and pushes the file further toward the next ICE.
- **GLSL 330 core is stricter than your driver.** Apple's GL and NVIDIA both
  accept `vec2(1, 0)`; Intel and AMD reject it. Write `vec2(1.0, 0.0)`, match
  uniform types exactly, and keep texture units sequential from `GL_TEXTURE0`.
  No macOS test catches this class — the shader compiles for you.

---

## 4. What CI actually proves (and what it does not)

Windows CI passing is a much weaker signal than macOS CI passing. Exactly:

| Checked | Not checked |
|---|---|
| x64 and ARM64 compile under MSVC 2022 | ARM64 **never executes** — compile + artifact only |
| 4 headless tests on x64: `DSPTEST`, `AUDIOPDCTEST`, `REMOVEBGTEST`, `AUDIOPARAMSWEEPTEST` (crash-only) | every other `INFINITE_*` fixture — ~85 of them |
| No `MSVCP140`/`VCRUNTIME140` imports in the PE | anything GUI, GL, audio-device, MIDI, video, or Spout |

`INFINITE_DSPTEST` printed `PAULSTRETCHTEST OK` on the real Windows runner for
the entire time the FFT inverse was returning time-reversed audio. Treat a
green Windows job as "it links", nothing more.

The macOS gate is unchanged and non-negotiable:
`.claude/skills/run-infinite-hygiene/driver.sh` must stay green for any shared
code you touched.

---

## 5. Exit criterion

Run from the repo root before pushing anything that touched a platform surface:

```bash
# 1. No Windows-only branch entered the node layer. Must print nothing.
#    (__APPLE__ splits are legal - see §0. windows.h in TextNode.cpp is the
#    sanctioned GDI+ include, so do not grep for that here.)
grep -rn "_WIN32" src/nodes/

# 2. Every Platform:: declaration you added has a Windows definition.
#    Substitute your function name; must hit src/platform/win/.
grep -rn "YourNewFunction" src/platform/

# 3. New Windows .cpp files are in the build.
grep -n "platform/win" CMakeLists.txt

# 4. No byte-path file I/O in the Windows layer. Known baseline is 6 bad
#    hits (§3.7) - your change must not add a 7th, and ideally removes some.
grep -rn "fopen(\|ifstream\|ofstream" src/platform/win/ | grep -v Utf8ToWide

# 5. macOS suite green.
.claude/skills/run-infinite-hygiene/driver.sh
```

Then push and read the Windows job — both arches must compile, and the CRT
import scan must pass. If your change is in a subsystem §4 says CI cannot
reach (which is most of them), say so explicitly in the commit message rather
than implying green CI covered it.

---

## 6. Open defects — do not re-file, do not assume fixed

All four re-verified against the execution path, not just the source line.

| # | Where | Real? | Effect today |
|---|---|---|---|
| 1.4 | `PlatformWin.cpp:398` | **yes, user-visible** | all Windows 3D text uniformly mis-scaled vs macOS; fails silently, fallback never fires |
| — | `MediaDecodeWin.cpp` 192/314/445/660 | **yes, user-visible** | OBJ/PLY/STL/AIFF under any non-ASCII path fail to load, in a file that already has the fix helper |
| 1.8 | `MidiWin.cpp:170` | **yes, conditional** | lost/torn notes, but only with 2+ MIDI devices connected |
| — | `PluginVST3Win.cpp` 356/367/418, `fs::` calls | **yes, narrow** | VST3 crash sentinel + blocklist silently inert for non-ASCII Windows account names |
| 1.3 | `MidiWin.cpp:185` | **yes, but latent** | clock unreachable — however `MidiClockIsPresent`/`MidiClockBpm` have no callers on either platform, so no effect until someone wires clock sync up |

Fixed and worth copying as reference: FFT inverse (`PortableFft.h:147`),
audio teardown (`AudioDeviceWin.cpp:269`), MF stride (`MediaWin.cpp:269`),
static CRT (`CMakeLists.txt:57`). Full write-ups, including the smaller
items, live in `docs/WINDOWS_VERIFICATION.md` Part 1.

---

## 7. Siblings

`new-audio-node`, `new-geometry-node`, `new-effect-node` cover how a node is
built; this skill covers what happens to it on the other platform. If you are
adding a node that needs a file, a device, a codec, or a font, read the
relevant one **and** §2 here. `infinite-code-review` should apply §3 as a
checklist whenever a diff touches `src/platform/`.

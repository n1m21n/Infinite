# Verifying the Windows port

The Windows platform layer (`src/platform/win/`) is ~4,900 lines of WASAPI,
WinMM, Media Foundation and GDI+ that nobody with commit access can run.
This document is the division of labour that makes it verifiable anyway:

| Who / what | Covers |
|---|---|
| `.github/workflows/build.yml` | Both platforms compile; headless self-tests pass. Produces a downloadable `Infinite.exe`. |
| A Windows contributor on real hardware | Audio devices, MIDI, camera, GPU, file dialogs — everything with a driver behind it. |
| A Parallels VM on an Apple Silicon Mac | Does it launch, does the node graph work, does file I/O work. **Not** audio latency or render performance. |

Nothing here replaces the macOS suite: `.claude/skills/run-infinite-hygiene/driver.sh`
remains the gate for shared code, and it must stay green on macOS regardless of
what the Windows side does.

---

## Part 1 — Known defects, for whoever fixes them next

These were found by reviewing PR #8 on a Mac. The first is a genuine Windows
bug proven numerically; the second and third are macOS regressions the port
introduced in shared code. Each block below is written to be pasted into a
fresh Claude Code session as-is — it names the files, the symptom, and the
check that proves the fix.

### 1.1 `PortableFft::Inverse` is broken (Windows only, blocker)

```
In src/audio/dsp/PortableFft.h, PortableFft::Inverse is not the inverse of
PortableFft::Forward. Two independent defects:

(a) RunCore() builds its twiddle tables with a negative angle (the forward
    DFT kernel) and uses them unconditionally. Its `complexIn` flag only
    selects whether the imaginary input array is read - it does not
    conjugate the twiddles. So Inverse() computes a FORWARD DFT over the
    Hermitian-mirrored spectrum, whose result is the input signal
    TIME-REVERSED rather than reconstructed.

(b) Inverse() calls RunCore(mRe.data(), mIm.data(), log2N, true), passing
    the class's own scratch arrays as RunCore's `inReal`/`inImag`. RunCore's
    first loop does `mRe[i] = inReal[r]` - reading and writing the same
    array through a non-identity permutation, which corrupts the data.

Verified empirically against Accelerate on macOS:
  - Forward() matches vDSP_fft_zrip to float precision (worst relative
    error 2.7e-07 for N = 32..16384). Do not change Forward.
  - Forward() -> Inverse() does not round-trip under ANY constant scale
    (relative residual ~1.0).
  - After fixing (b) alone, Inverse()'s output matches reverse(input) at
    best-fit scale 1.0000 with residual 2e-07, which is what isolates (a).

Fix both: give RunCore an `inverse` flag that negates the sine term
(wi = -mSin[tw]) and have Inverse() stage its packed spectrum into separate
scratch buffers before calling it. Keep the class allocation-free after
Prepare() - Inverse() runs on the audio thread via
AudioPaulStretchNode::ProcessBlock, so any new buffer must be a member sized
in Prepare(), not a local std::vector.

Then add a permanent regression check to RunDspTest() in src/main.cpp
(INFINITE_DSPTEST, which is exit-code gated and runs headless in CI):
assert that Forward() followed by halving and Inverse() reproduces a random
input to within 1e-5 relative, at a few sizes. This is the assertion whose
absence let the bug through.

Impact: the PaulStretch node is the only audio-thread consumer of Inverse()
(src/nodes/PaulStretchNode.cpp). On Windows it currently emits reversed,
corrupted audio. AnalyzeNodes.cpp uses Forward() only, so the spectrum
display is unaffected.
```

### 1.2 Settings paths moved, losing existing macOS users' state

```
PR #8 routed every settings path through AppPaths::AppSupportDir(), which
silently relocated two files that already exist on every current macOS
install:

  src/core/CategoryColors.cpp, ThemePath()
    was: ~/Library/Application Support/Infinite.theme
    now: ~/Library/Application Support/Infinite/Infinite.theme

  src/core/Patch.cpp, RecentsPath()
    was: ~/Library/Application Support/Infinite.recents
    now: ~/Library/Application Support/Infinite/Infinite.recents

Effect on macOS: the saved category-colour theme and the recent-patch list
both silently reset on first launch after upgrading.

The new layout is better, so migrate rather than revert: on both platforms,
if the new path does not exist and the old one does, move it (std::rename,
falling back to copy) once, then carry on. On Windows there is no old path,
so the migration is a no-op there. Note that main.cpp's settings-dir comment
explains why nothing may be written next to the executable on macOS - keep
that property.
```

### 1.3 README

```
PR #8 prepends a "# This Fork / This fork is completed vibe-coded..."
section to README.md. That belongs on the fork, not on the upstream repo.
Remove that block; keep the rest of the PR's README changes (the macOS ->
Windows facade table, the Windows build instructions, the updated
requirements line) - those are accurate and worth having.
```

### 1.4 Non-blocking, worth doing

```
1. src/platform/win/AudioDeviceWin.cpp - RenderThreadMain() never raises its
   thread priority. A WASAPI shared-mode render thread at normal priority
   glitches under GPU load, and this app is GPU-heavy by design. Call
   AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex) on entry and
   AvRevertMmThreadCharacteristics() on exit (avrt.lib). This is the most
   likely cause of "works on my machine, crackles on everyone else's".

2. src/platform/win/AudioDeviceWin.cpp:339 -
       if (gRender.renderer == nullptr || !SUCCEEDED(0))
   `!SUCCEEDED(0)` is always false, and the body re-tests the same null
   pointer. Reduce to a plain null check.
```

---

## Part 2 — Manual checklist for a real Windows machine

CI proves it compiles and that the headless DSP is sane. Everything below has
a driver behind it and can only be checked by a human at a real desktop.
Please report results per line rather than "seems fine" — a partial pass is
much more useful than a summary.

**Startup and graph**
- [ ] Launches to a window; `gladLoadGL` does not fail.
- [ ] Node palette opens; spawning one node of each category works.
- [ ] Save a patch, quit, relaunch, load it — graph and params come back.
- [ ] Settings land in `%APPDATA%\Infinite`, not next to the `.exe`.

**Audio (WASAPI)**
- [ ] Device list is populated; picking a device and hitting Start Audio produces sound.
- [ ] Change the Windows default output device *while running* — the app recovers instead of going silent.
- [ ] Run for ~10 minutes with a busy render graph and listen for dropouts. This is the check item 1.4.1 is about.
- [ ] Audio input / the input tap produces a live signal.
- [ ] Record to WAV, MP3 and FLAC; play each back in another program.

**MIDI (WinMM)**
- [ ] A connected MIDI keyboard appears and plays notes.
- [ ] MIDI out reaches another application.

**Media (Media Foundation)**
- [ ] Load an MP4 into the video source node; playback and seeking both work.
- [ ] Camera capture shows a live image.
- [ ] Record the canvas to MP4 with audio; the file opens elsewhere with A/V in sync.
- [ ] Load PNG, JPEG and EXR images.
- [ ] Load OBJ, PLY and STL models.

**Text (GDI+)**
- [ ] Text node renders; the font list is populated.
- [ ] Fill, stroke-and-fill, and stroke-only all look right — GDI+ has to
      reproduce what CoreText encodes as a signed stroke width, so this is
      the most likely place for a silent visual difference.
- [ ] Word wrap and fit-to-box behave like the screenshots in the README.

**Graceful degradation** (these have no Windows implementation and must fail cleanly, not crash)
- [ ] Audio Unit plugin hosting reports unavailable.
- [ ] Syphon In / Syphon Out nodes report unavailable.
- [ ] Remove Background node reports unavailable.

**PaulStretch** — leave this until 1.1 is fixed; it is known broken.

---

## Part 3 — Running it in Parallels on an Apple Silicon Mac

Useful for "does it work at all". Not useful for judging audio glitching or
render speed — the VM's audio timing and its OpenGL implementation are both
unrepresentative.

1. **Get the build.** Open the Actions tab, pick the newest `Build` run on
   the branch you care about, and download the `Infinite-windows-*` artifact
   from the run summary. Unzip it; `Infinite.exe` is self-contained (the build
   links everything but system libraries statically).

2. **Which artifact.** Parallels on Apple Silicon runs Windows 11 on ARM.
   Try `Infinite-windows-ARM64` first — it runs natively. If that job failed
   or the exe won't start, use `Infinite-windows-x64`, which runs under
   Windows' built-in x64 emulation: correct, but slower and a worse basis for
   judging performance.

3. **Install Parallels Tools** in the guest. Without them there is no 3D
   acceleration at all and the app will not get an OpenGL 3.3 core context.

4. **If the window never appears**, run it from a terminal in the guest and
   read stderr. `gladLoadGL failed (no OpenGL 3.3+ driver?)` means the VM's
   GL support, not the port. Two ways forward:
   - Confirm with any GL viewer that the guest reports GL >= 3.3 core.
   - Or drop a software `opengl32.dll` (Mesa llvmpipe, from the mesa-dist-win
     releases) next to `Infinite.exe`. That guarantees a context and lets you
     verify correctness; it will be very slow, so ignore the frame rate.

5. **What to trust from a VM run:** launch, node graph, save/load, text
   rendering, image and model import, file dialogs, settings location.
   **What not to trust:** audio dropouts, latency, frame rate, camera, MIDI
   device enumeration.

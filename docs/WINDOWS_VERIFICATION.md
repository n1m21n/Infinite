# Verifying the Windows port

The Windows platform layer (`src/platform/win/`) is ~4,900 lines of WASAPI,
WinMM, Media Foundation and GDI that nobody with commit access can run.
This document is the division of labour that makes it verifiable anyway:

| Who / what | Covers |
|---|---|
| `.github/workflows/build.yml` | Both platforms compile; headless self-tests pass. Produces a downloadable `Infinite.exe`. |
| Part 1 below | Defects found by review, written to be handed to whoever fixes them. |
| Part 2 below | Running the CI artifact in Parallels on an Apple Silicon Mac. |

Nothing here replaces the macOS suite: `.claude/skills/run-infinite-hygiene/driver.sh`
remains the gate for shared code, and it must stay green on macOS regardless of
what the Windows side does.

**Everything in Part 1 was found by reading, not by running Windows.** None of
it is caught by a build, and — this is the important part — none of it is caught
by the existing self-test fixtures either. `INFINITE_DSPTEST` prints
`PAULSTRETCHTEST OK` on the real Windows x64 CI runner while 1.1 is live. Two
new assertions are proposed below (1.1 and 1.4) precisely because compiling
cleanly is not evidence of anything here.

---

## Part 1 — Known defects

Each block is written to be pasted into a fresh Claude Code session as-is: it
names the file, the mechanism, the fix direction, and the check that proves it.

Severity, briefly:

| # | Defect | Effect |
|---|---|---|
| 1.1 | `PortableFft::Inverse` is not the inverse of `Forward` | PaulStretch emits reversed, corrupted audio |
| 1.2 | Audio device open failure leaves a joinable `std::thread` | `std::terminate()` — hard crash, no dialog |
| 1.3 | MIDI clock cases are unreachable | external clock/BPM sync silently never works |
| 1.4 | Cap height measured from the wrong glyph | all text renders at the wrong size |
| 1.5 | Settings paths moved | existing macOS users lose theme + recents |
| 1.6 | Analyser `low`/`mid`/`high` from wrong bands | Audio Analyze drives visuals wrongly |
| 1.7 | Media Foundation stride assumed to be `width*4` | skewed video/camera at some frame widths |
| 1.8 | MIDI note ring has N producers, not 1 | dropped/torn notes with 2+ controllers |
| 1.9 | Capture-thread races | data race, and a use-after-free on the audio thread |
| 1.10 | Assorted smaller items | see the block |
| 1.11 | Exe imported the VC++ redist CRT | wouldn't launch on a clean Windows install (fixed) |

### 1.1 `PortableFft::Inverse` is broken (blocker)

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
absence let the bug through - the existing PAULSTRETCHTEST fixture prints OK
on the Windows CI runner today, with the bug live.

Impact: the PaulStretch node is the only audio-thread consumer of Inverse()
(src/nodes/PaulStretchNode.cpp). On Windows it currently emits reversed,
corrupted audio. AnalyzeNodes.cpp uses Forward() only, so the spectrum
display is unaffected.
```

### 1.2 A failed audio-device open makes the app call `std::terminate()` (blocker)

```
src/platform/win/AudioDeviceWin.cpp - RenderState::Stop() decides whether to
join the render thread based on the `running` flag, but the render thread
clears that same flag itself when WASAPI setup fails. The result is a
joinable std::thread that nobody ever joins, and two ways to die from it.

Trace:

 1. AudioDeviceOpen() sets running = true and spawns RenderThreadMain.
 2. Setup fails inside the thread - a non-float mix format, >8 channels, an
    endpoint another app holds in exclusive mode, any Activate/Initialize
    failure. The thread hits
        if (gRender.renderer == nullptr ...) { running.store(false); return; }
    and exits. gRender.thread is still joinable.
 3. AudioDeviceOpen's poll loop guards on
        if (!gRender.thread.joinable()) break;   // "died"
    which can never be true - joinable() stays true until join() or
    detach(). So the open also stalls the calling thread for the full 4000 ms
    before reporting failure. That freeze is user-visible on its own.
 4. AudioDeviceClose() -> Stop(): running.exchange(false) returns FALSE
    (the thread already cleared it), so Stop() takes the early branch,
    closes the two event handles and returns WITHOUT joining.
 5. Now either of these terminates the process:
      - the user picks another device: AudioDeviceOpen does
        `gRender.thread = std::thread(...)`, and move-assigning over a
        joinable thread calls std::terminate().
      - the app quits: ~RenderState() -> Stop() -> same early return ->
        ~thread on a joinable thread calls std::terminate().

So one failed device open is enough to make the app abort on quit, and
trying a second device aborts immediately. No dialog, no log.

Fix:
  - In Stop(), join unconditionally on thread.joinable(), independent of the
    `running` flag. CaptureEngineBase::Stop() in the same file already does
    exactly this - it is the correct pattern, copy its shape.
  - Delete the `!gRender.thread.joinable()` check in the poll loop and give
    the thread an explicit atomic<bool> setupFinished (or setupFailed) to
    latch, so a failed open returns immediately instead of after 4 seconds.
  - While in there: gRender.renderer / gRender.sampleRate are written by the
    render thread and polled by the opener as plain non-atomic members. Make
    the handshake a single atomic flag rather than reading those directly.
  - The dead condition on the same path,
        if (gRender.renderer == nullptr || !SUCCEEDED(0))
    reduces to a plain null check (!SUCCEEDED(0) is always false).

Cheap way to reproduce without exotic hardware: open any endpoint whose mix
format is not IEEE float, or start something that grabs the default output in
exclusive mode (many ASIO drivers, some players), then switch devices.
```

### 1.3 MIDI clock handling is unreachable code (blocker for clock sync)

```
src/platform/win/MidiWin.cpp, HandleShortMessage(). The function computes

    const BYTE type = status & 0xF0;

and then switches on `type` with cases 0x90, 0x80, 0xB0, and then 0xF8
(timing clock), 0xFA (start) and 0xFC (stop).

Masking with 0xF0 can only ever produce 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0,
0xE0 or 0xF0. It can never produce 0xF8, 0xFA or 0xFC - those are System
Real-Time status bytes where the low nibble is part of the message identity,
not a channel. All three cases are dead code.

Effect: gClock.Pulse() and gClock.Reset() are never called on Windows, so
MidiClockIsPresent() always returns false and MidiClockBpm() always returns
0. Every external-clock / BPM-sync feature silently reports "no clock". The
whole ClockTracker struct - median-of-intervals BPM estimation and all - is
unreachable.

Fix: dispatch on the full status byte before masking, e.g.

    if (status >= 0xF0) { switch (status) { case 0xF8: ... } return; }
    const BYTE type = status & 0xF0;   // channel messages only below here

Then verify against a clock source. Note this is invisible to CI and to any
test that does not have a clock master attached, so it needs the note in the
handoff rather than a fixture.
```

### 1.4 Text cap height is measured from the wrong glyph (blocker for visual parity)

```
src/platform/win/PlatformWin.cpp, GetTextOutlines(). The first pass measures
cap height so the outlines can be normalised to "cap height ~ 1", the same
contract the CoreText/CoreGraphics path establishes. It calls:

    GetGlyphOutlineW(hdc, L'H', GGO_NATIVE | GGO_GLYPH_INDEX, &metrics, ...)

GGO_GLYPH_INDEX tells GDI to interpret the uChar argument as a GLYPH INDEX,
not a character. L'H' is 0x48, so this measures whichever glyph happens to
sit at index 72 in that particular font - which is font-dependent and in
practice almost never the letter H. In fonts with conventional glyph ordering
it lands somewhere in the lowercase range, so `capHeight` ends up being
roughly the x-height.

Every emitted point is then multiplied by `scale = 1.0f / capHeight`, so a
capHeight that is ~30% too small makes ALL text on Windows render
substantially too large, by a factor that varies per font. Fit-to-box and
letter spacing inherit the same error. For fonts where glyph 72 is unmapped
or empty, it silently falls back to 0.7 * em.

The tell is that the SECOND pass, over the actual characters, correctly uses
plain GGO_NATIVE without GGO_GLYPH_INDEX. The two calls disagree.

Fix: drop GGO_GLYPH_INDEX from both calls in the cap-height block (the two
GetGlyphOutlineW calls measuring L'H'), so it measures the character 'H'.

Two related items in the same function, worth fixing together:

  - Letter spacing is applied in em units:
        penX += (float)metrics.gmCellIncX + letterSpacing * (float)kEm;
    and everything is later divided by capHeight, so the effective spacing is
    letterSpacing * (kEm / capHeight) rather than letterSpacing * 1.0. Once
    capHeight is correct that is roughly a 1.4x overshoot versus the macOS
    path. Scale letterSpacing by capHeight, not kEm.

  - The TT_PRIM_QSPLINE tessellation never emits t = 1.0: the loop runs
    s = 1..steps with t = s / (steps + 1), so it stops short of each
    segment's on-curve endpoint and jumps to the next segment's first
    sample. That leaves a small notch at every on-curve point of every
    quadratic outline. Use t = s / steps.

Suggested new headless fixture, and the reason this one is worth adding:
GetTextOutlines() needs a device context but no GL context and no window, so
a check can run before glfwInit() and therefore in CI on both platforms. Add
an INFINITE_TEXTOUTLINETEST that calls GetTextOutlines("H", <a font present
on both platforms>) and asserts the maximum y over the returned contours is
within a few percent of 1.0 - that is the normalisation contract, and it
fails on Windows today. The existing INFINITE_TEXTFIT fixture cannot serve
here: it runs at frame 4, well after glfwInit().
```

### 1.5 Settings paths moved, losing existing macOS users' state

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

### 1.6 The input analyser's low/mid/high bands do not match macOS

```
src/platform/win/AudioDeviceWin.cpp, AnalyserEngine::RunAnalysis() derives
the three summary bands from fixed indices into the 16 log-spaced bands:

    next.low  = 0.5*(bands[0]+bands[1]) + 0.25*bands[2] + 0.25*bands[3];
    next.mid  = 0.25*bands[4] + 0.5*bands[5] + 0.25*bands[6];
    next.high = 0.5*bands[11] + 0.5*bands[12];

src/platform/Platform.mm computes the same three fields from explicit Hz
ranges instead:

    low  = rangeEnergy(20, 250)
    mid  = rangeEnergy(250, 2000)
    high = rangeEnergy(2000, 16000)

With kAudioBands = 16 spanning 20 Hz..16 kHz, each band is a factor of
800^(1/16) ~ 1.52 wide, so the Windows index picks resolve to roughly:

    low   20-106 Hz     (macOS: 20-250 Hz),   and the weights sum to 1.5,
                        not 1.0, so it is also scaled up by half again
    mid   106-373 Hz    (macOS: 250-2000 Hz) - this is bass, not mid; it
                        misses essentially the entire vocal range
    high  1982-4571 Hz  (macOS: 2000-16000 Hz) - no cymbals, no air

On top of that, macOS runs low/mid/high through the same shape() and
Smooth() the bands get, while the Windows path leaves those three raw -
AudioRead()'s smoothing loop only touches bands[].

Effect: every patch driven by the Audio Analyze node's low/mid/high outputs
responds differently and wrongly on Windows. Nothing crashes, nothing logs,
and CI cannot see it.

Fix: compute low/mid/high from the same Hz ranges Platform.mm uses, by
summing the spectrum bins in those ranges directly rather than re-weighting
band indices, and apply the same shaping and smoothing. Cross-check against
Platform.mm's rangeEnergy/shape/Smooth so the two paths agree by
construction rather than by tuning.
```

### 1.7 Media Foundation stride is assumed to equal `width * 4`

```
src/platform/win/MediaWin.cpp hardcodes the row stride at two places:

  line ~284 (video decode):  Rgb32ToRgbaFlipped(data, (LONG)(video->width * 4), ...)
  line ~694 (camera):        Rgb32ToRgbaFlipped(data, (LONG)(w * 4), ...)

with the comment "Contiguous buffers are tightly packed: stride is exactly
one row". That is not guaranteed. MF's video processor and hardware decoders
routinely pad each row out to an alignment boundary, and the negotiated
stride is reported in MF_MT_DEFAULT_STRIDE on the media type. Neither
MF_MT_DEFAULT_STRIDE nor IMF2DBuffer::Lock2D appears anywhere in the port.

Effect when the real stride is larger than width*4: each row is read from
slightly the wrong offset, and the error accumulates down the image - the
classic progressively-skewed / diagonal-shear frame. Widths that are already
comfortably aligned (1920, 1280) will look fine, which is exactly why this
survives casual testing; try something like 854x480 or a 1080x1350 vertical
video to provoke it.

Fix: read MF_MT_DEFAULT_STRIDE from the negotiated media type and pass it
through (it is already a LONG parameter on Rgb32ToRgbaFlipped, so the plumbing
exists). A negative value means bottom-up, which is RGB32's default and is
what the "Flipped" in that helper is already handling - keep the two
consistent. Prefer IMF2DBuffer2/IMF2DBuffer::Lock2D where available, since it
reports the stride directly.
```

### 1.8 The MIDI note ring is single-producer but has N producers

```
src/platform/win/MidiWin.cpp. MidiStart() opens EVERY installed midiIn
device with CALLBACK_FUNCTION, and WinMM delivers each open device's
callbacks on its own thread. PublishNote() is written as a single-producer
ring:

    const uint64_t idx = gState.ringWrite.load(relaxed);
    NoteRingSlot& slot = gState.ring[idx % kNoteRingCapacity];
    ...write slot...
    slot.seq = idx + 1;
    gState.ringWrite.store(idx + 1, release);

With two controllers connected, two callback threads can load the same idx,
write the same slot, and store the same idx + 1: notes are lost and slots
are torn. The file's own header comment says "written on the WinMM callback
thread", singular - the design assumption does not survive MidiStart's own
loop.

Note also that HandleShortMessage calls PublishNote INSIDE the gState.mutex
lock for Note Off (case 0x80) but OUTSIDE it for Note On (case 0x90), so
note-ons are the unserialised ones.

The scaffolding for the correct fix is already present and unused: each slot
has a `seq` field that PublishNote writes and MidiReadNotesSince never reads.
Reserve with a separate atomic counter (fetch_add), write the slot, then
publish by storing seq, and have the reader validate seq before accepting a
slot. The ring is read from the audio thread, so do not "fix" this with a
mutex around PublishNote.
```

### 1.9 Two races in the capture engines

```
src/platform/win/AudioDeviceWin.cpp. Both derive from CaptureEngineBase, and
BOTH can be running at once - the analyser (AudioStart, for Audio Analyze)
and the input tap (AudioInputCapture*, for Audio In) are independent engines
with independent threads.

(a) CaptureEngineBase::ThreadMain(), silence path:

        static std::vector<float> silence;
        silence.resize((size_t)packetFrames * fmt->nChannels, 0.0f);
        OnFrames(silence.data(), (int)packetFrames);

    That static lives in the shared base, so it is ONE object shared by both
    capture threads. Concurrent resize() from two threads is a data race on
    the vector's internals - torn reads at best, heap corruption at worst.
    It is also a heap allocation on a real-time capture thread. Make it a
    per-engine member sized once, not a function-local static.

(b) CaptureTapEngine::OnFormat() publishes readiness before the buffer
    exists:

        sampleRate = rate;                    // <- readers now pass the gate
        channels = std::clamp(chs, 1, kMaxChannels);
        ring.Init(channels);                  // <- assign() may reallocate

    AudioInputCaptureIsRunning() gates on `sampleRate > 0.0`, so between the
    first line and ring.Init(), AudioInputCaptureRead() - which runs on the
    AUDIO RENDER THREAD - can pass the gate and index ring.buffers while
    assign() is reallocating it. That is a use-after-free on the audio
    thread. The window opens on every input-engine restart, which
    PollAudioRecovery triggers automatically on any device change.

    Fix: allocate first, publish last, through a single atomic<bool> ready
    flag released after ring.Init() - and have the read path gate on that
    flag rather than on a plain double.

While here: ThreadMain() creates `bufferEvent` with CreateEventW and its
cleanup lambda never closes it, so every capture start/stop cycle leaks one
event handle.
```

### 1.10 Smaller items

```
1. src/platform/win/AudioDeviceWin.cpp - RenderThreadMain() never raises its
   thread priority. A WASAPI shared-mode render thread at normal priority
   glitches under GPU load, and this app is GPU-heavy by design. Call
   AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex) on entry and
   AvRevertMmThreadCharacteristics() on exit (avrt.lib). This is the most
   likely cause of "works on my machine, crackles on everyone else's".

2. Audio device ids are positional and therefore unstable. AudioListDevices()
   hands out `deviceId = index + 1` into a flat list rebuilt by
   RefreshDeviceList() on every call, mixing render and capture endpoints;
   ResolveEndpoint() maps the id straight back through that index without
   checking isInput. Plug or unplug anything and a previously chosen id now
   points at a different endpoint - possibly a capture one, in which case
   render Initialize fails and you land in 1.2's terminate path. On macOS the
   deviceId is a CoreAudio AudioDeviceID, stable for the device's lifetime,
   so nothing upstream expects this. Key the id off the immutable endpoint id
   string (hash it, or keep an append-only id table) and reject an id whose
   isInput does not match the requested direction.

3. src/platform/win/AudioDeviceWin.cpp - the render loop calls
   GetBuffer(framesAvailable) but ReleaseBuffer(frames), where frames is
   clamped to kPlanarCapacity (4096). Today the shared-mode buffer never
   exceeds that so the two always agree, but if it ever does, every callback
   silently under-fills its packet. Request min(framesAvailable,
   kPlanarCapacity) from GetBuffer so the two cannot diverge.

4. src/platform/AppPaths.h - HomeDir() checks HOME before USERPROFILE. On
   Windows, HOME is commonly set by Git Bash / MSYS2 / Cygwin to a POSIX-style
   path like /c/Users/name, which no Win32 API can resolve. Both call sites
   (src/main.cpp RecordingDirFor(), and the export/record default path) append
   "/Desktop" and use the result as a default output directory, so launching
   from such a shell silently writes nowhere. Prefer USERPROFILE first under
   _WIN32. Separately, "%USERPROFILE%\Desktop" is wrong on any machine with
   OneDrive Desktop redirection (the default on new consumer installs) or a
   localised profile - use SHGetKnownFolderPath(FOLDERID_Desktop).

5. src/platform/AppPaths.h - EnsureDir() ignores the mkdir result and always
   returns true, and only creates the leaf component. Harmless for %APPDATA%,
   but the return value is a lie; either report it or drop it.

6. src/platform/NetCompat.h - `#define close(fd) closesocket(fd)` is an
   unscoped macro named `close` in a header. It compiles today because
   neither RemoteControl.cpp nor OscNodes.cpp happens to call any other
   close(), including any member .close(), but it will break the first time
   one of those TUs touches an fstream. Use an inline NetClose() alongside
   the other shims instead. Also, the shims take `int fd` and cast to SOCKET,
   which is UINT_PTR (64-bit) on Win64; Microsoft explicitly documents that
   SOCKET must not be assumed to fit in an int. It works in practice because
   handles are allocated low, and error checks survive the truncation, but
   the socket type should be a typedef here rather than int.

7. COM apartment modes disagree between translation units on what is often
   the same thread: PlatformWin.cpp initialises COINIT_APARTMENTTHREADED (for
   the shell dialogs, which require STA) while AudioDeviceWin.cpp and
   MediaWin.cpp initialise COINIT_MULTITHREADED. A nested scope of the other
   mode gets RPC_E_CHANGED_MODE, which the ComScope helpers treat as failure -
   so the inner feature silently does nothing rather than reporting an error.
   The refcounting itself is correct (S_FALSE balanced, RPC_E_CHANGED_MODE not
   uninitialised), so this only bites when the scopes nest, e.g. inspecting a
   movie from inside a file-dialog callback. Either commit the process to one
   apartment and give the dialogs their own STA thread, or treat
   RPC_E_CHANGED_MODE as usable-but-do-not-uninitialise and log it.
```

### 1.11 The build was not actually self-contained (fixed)

```
package.ps1's header claims the staged exe needs no runtime DLLs ("the build
links everything statically except system libraries"), and the README's
Windows section repeats it. That was not true: MSVC defaults to the dynamic
CRT (/MD), so Infinite.exe imported MSVCP140.dll and VCRUNTIME140.dll from
the Visual C++ redistributable. On a clean Windows image - which is exactly
what a first-time user or a fresh VM has - it died before main() with

    The code execution cannot proceed because MSVCP140.dll was not found.

Nothing in the build or the headless tests could see this: the CI runner has
Visual Studio installed, so the DLLs are present there and every test passed.

Fixed in CMakeLists.txt by setting

    CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"

before the FetchContent_MakeAvailable calls, so glfw, libFLAC and Infinite all
agree on the static CRT (they must agree, or the link fails on mismatched
RuntimeLibrary). build.yml now also asserts the shipped exe's import table
contains no MSVCP140/VCRUNTIME140, so this cannot regress silently.

Worth correcting the claim in package.ps1's header comment and in the README
too - both still assert self-containment as a property of the old /MD build
rather than of the static-CRT setting that now actually provides it.
```


---

## Part 2 — Running the CI artifact in Parallels on an Apple Silicon Mac

Useful for "does it work at all". Not useful for judging audio glitching or
render performance — the VM's audio timing and its OpenGL implementation are
both unrepresentative.

1. **Get the build.** Open the Actions tab, pick the newest `Build` run on
   the branch you care about, and download the `Infinite-windows-*` artifact
   from the run summary. Unzip it and run `Infinite.exe` — it needs no
   installer and no other files. It is unsigned, so SmartScreen will offer
   "More info → Run anyway".

   (Builds before the static-CRT fix died at launch with "MSVCP140.dll was not
   found" on a clean Windows image — see 1.11. If you hit that on an older
   artifact, either grab a newer one or install the Visual C++ 2015-2022
   redistributable in the guest.)

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

5. **What to trust from a VM run:** launch, node graph, save/load, image and
   model import, file dialogs, settings location. Text rendering is worth a
   look too, but read 1.4 first — the size will be wrong, and that is the port,
   not the VM. **What not to trust:** audio dropouts, latency, frame rate,
   camera, MIDI device enumeration.

6. **Two things not to bother testing yet:** PaulStretch (broken, 1.1) and
   external MIDI clock sync (dead code, 1.3).

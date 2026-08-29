# Windows rendering fixes — session brief

Three independent defects make the 3D render path crash or hang on Windows
while behaving acceptably on macOS, plus the diagnostic gap that makes every
Windows bug report unactionable. All four were confirmed by reading the tree;
none is reproducible on macOS, and none is reachable by CI (`docs/WINDOWS_VERIFICATION.md` §4).

Work on a `bugfix/windows-render` branch off `main` per the repo's branch
workflow. `.claude/skills/run-infinite-hygiene/driver.sh` must be green before
landing. State explicitly in the commit message that CI cannot execute any of
these paths.

---

## 1. `glVertexAttribDivisor` is a null pointer on a GL 3.2 context (crash)

**Symptom:** connecting any point-cloud source (Particle System, Image to
Points, Mesh to Points) or an Instance on Points into a Render node crashes
instantly. Plain geometry into Render is fine.

**Mechanism.** `src/main.cpp:33316-33319` and `src/main.cpp:25077-25080` both
request `CONTEXT_VERSION 3.2, CORE_PROFILE, FORWARD_COMPAT`. All 56 shaders in
the tree are `#version 150`, so 3.2 is a deliberate, consistent target.

On Windows every GL entry point is a glad function pointer. glad was generated
as `gl:core=3.3` and gates each version block on the *runtime-reported* version:

```c
static void glad_gl_load_GL_VERSION_3_3(...) {
    if(!GLAD_GL_VERSION_3_3) return;                    // external/glad/src/gl.c:3552
    ...
    glad_glVertexAttribDivisor = load(userptr, "glVertexAttribDivisor");  // gl.c:3572
}
```

`GLAD_GL_VERSION_3_3` is parsed from `glGetString(GL_VERSION)` at
`external/glad/src/gl.c:7695`. A driver that honours the 3.2 request literally
leaves `glad_glVertexAttribDivisor == NULL`, and calling it is an access
violation.

`glVertexAttribDivisor` is the **only** GL 3.3 entry point in the entire
codebase — verified: `glGenSamplers`, `glBindSampler`, `glSamplerParameteri`,
`glBindFragDataLocationIndexed`, `glQueryCounter`, `glGetFragDataIndex`,
`glVertexAttribP*` all have zero uses. There are 12 call sites, all instancing:
`src/nodes/Geometry3DNodes.cpp:1860,1868,2159,2174` and the matching resets,
plus 5 in `src/core/NodeViewport.cpp`.

The existing guard does not catch it. `src/main.cpp:33337` prints
`"gladLoadGL failed (no OpenGL 3.3+ driver?)"` — the message already assumes
3.3 — but `gladLoadGL` returns the version it found, so 3.2 is a non-zero
success and it passes.

Why it's intermittent across users: NVIDIA and AMD desktop drivers return 4.6
regardless of what was requested, so glad loads 3.3 and everything works.
Drivers that return exactly the requested version get the NULL — Mesa-on-D3D12
(the OpenGL Compatibility Pack, which is what ARM64 Windows and most VMs run),
older Intel iGPU drivers, Remote Desktop, Parallels. The shipped ARM64 build
hits this by default.

**Fix.**
1. Change both hint blocks to `CONTEXT_VERSION_MINOR, 3`. Free — the shaders
   are `#version 150` and stay valid under 3.3.
2. After `gladLoadGL` in `src/main.cpp:33337`, check `GLAD_GL_VERSION_3_3`
   explicitly rather than trusting the return value, and fail with the message
   route from item 4 below (not `stderr`).
3. Do not add per-call null checks — one hard check at init is the right shape.

**Check.** `grep -rn "CONTEXT_VERSION_MINOR" src/main.cpp` shows `3` at both
sites; the post-load guard tests `GLAD_GL_VERSION_3_3`.

---

## 2. Render3D applies `Particle::scale` as a world size, ignoring the base-size contract (GPU hang / TDR)

**Symptom:** `Image -> Image to Points -> Render` hangs then crashes on
Windows; on macOS the same patch stalls badly but usually survives.

**Mechanism — a units mismatch, not a bad default.** `Particle::scale`
(`src/core/Mesh.h:269`, default `1.0f`) is documented as a *relative
multiplier*, not a world-space size. `src/nodes/GenerativeNodes.cpp:344-349`
states this outright, and three of its four consumers honour it:

| Consumer | Base size applied |
|---|---|
| `ImageToPointsNode::RebuildMeshIfNeeded` (`GenerativeNodes.cpp:352,375`) | `baseHalf = cell * 0.45`, `cell = min(width,height)/n` |
| `InstanceOnPointsNode::Rebuild` (`GeometryOpNodes.cpp:719`) | `instanceScale * p.scale` |
| `ParticleSystemNode` (`SimulationNodes.cpp:133,195`) | writes an absolute size directly (`startSize`, default `0.06`) |
| **`Render3DNode::drawCloudSlot`** (`Geometry3DNodes.cpp:1845`) | **none** — `const float s = p.scale * scaleX;` with `scaleX == 1.0` for an identity model matrix, then `Mat4::Scale(s,s,s)` against the +/-1 quad at `:1798-1803` |

So for Image to Points at its default `pointSize = 1.0`, Render3D draws each
sprite with a half-extent of **1.0 world unit**, where the node's own
mini-viewport uses **0.0094** (`density = 96` -> `cell = 2.0/96`,
`baseHalf = cell * 0.45`). That is a **106x linear, ~11,000x area** discrepancy
between two views of the same node.

The UI shows the same split. `src/main.cpp:17713` gives Image to Points
`ModSlider("point size", ..., 0.01f, 4.0f)` — a multiplier range — while its
sibling point-cloud nodes at `:17724` (Mesh to Points), `:17738` (Distribute on
Faces) and `:17756` (Distribute in Grid) all use `0.002f, 0.3f`, a world-space
range. Image to Points is the sole outlier.

**Why this kills Windows specifically.** Defaults: `camDistance = 3.0`,
`fov = 45` -> visible height at target = `2*3*tan(22.5) ~= 2.49` units, so one
sprite at 2.0 units covers ~80% of the frame. `density` defaults to 96 (slider
range 4-400, `main.cpp:17702`) -> 9,216 sprites. Output is 1024x1024 with
`samples = 2` (= 4x MSAA). That is roughly
`9216 * (0.8*1024)^2 * 4 ~= 2.5e10` shaded samples in a single frame through a
full PBR + shadow + env-map fragment shader, with no instance-count clamp
anywhere in Render3D. Windows' GPU watchdog (TDR) resets the driver after ~2s
and the context is lost, taking the process with it. macOS has no equivalent
hard kill, which is why it only stalls there.

**Fix.** Make Render3D honour the same contract as the other three consumers —
do **not** just lower the default, which would leave the 106x mismatch in place
and still blow up at higher slider values.

1. Give `IGeometrySource` a way to report the base sprite size its `p.scale`
   values are relative to (e.g. `virtual float PointBaseSize() const { return 1.0f; }`),
   and have `drawCloudSlot` use `p.scale * base * scaleX` at
   `Geometry3DNodes.cpp:1845`.
2. `ImageToPointsNode` returns the same `baseHalf` its own mesh path computes
   (`GenerativeNodes.cpp:352`), so the Render node and the mini-viewport agree.
3. `ParticleSystemNode` and the Distribute/Mesh-to-Points nodes return `1.0f` —
   their `p.scale` is already absolute, so they are unaffected.
4. Independently, add a defensive clamp on total sprite fill in `drawCloudSlot`:
   if `instanceCount * (sprite screen area)` exceeds a budget, clamp the sprite
   radius and log once. This is the backstop that stops any future units bug
   from being a crash rather than an ugly frame.

Do not clamp by dropping instances — silently rendering fewer points than the
cloud contains is worse than a visibly wrong size.

**Check.** With `Image -> Image to Points -> Render` at defaults, the sprite
size in the Render output visually matches the Image to Points mini-viewport.
`spriteSizeMode = 1` (screen) multiplies by view depth in the vertex shader
(`Geometry3DNodes.cpp:319-321`) — verify that path too, it compounds the same error.

---

## 3. `GL_POINTS` draws in Render3D never establish a point size (verify, lower confidence)

`src/nodes/Geometry3DNodes.cpp:2244` draws `GL_POINTS` for vertices-only meshes.
Unlike `src/core/NodeViewport.cpp:753-757`, it neither enables
`GL_PROGRAM_POINT_SIZE` nor calls `glPointSize`, and the Render3D vertex shader
(`Geometry3DNodes.cpp:285-345`) never writes `gl_PointSize`.
`src/core/NodeViewport.cpp:827` has the same omission.

With `GL_PROGRAM_POINT_SIZE` disabled this is defined behaviour (1px points) on
both platforms, so it is probably cosmetic rather than a parity bug — but
`GL_PROGRAM_POINT_SIZE` is global context state that `NodeViewport.cpp:753`
enables, and if any path ever leaves it enabled, `gl_PointSize` is *undefined*
in a shader that does not write it. Decide deliberately: either write
`gl_PointSize` in the Render3D vertex shader and enable the state around the
draw, or assert the state is off. Do not leave it implicit.

---

## 4. Windows failures are invisible — fix this first, it makes everything else diagnosable

- `CMakeLists.txt:559` sets `WIN32_EXECUTABLE TRUE` (GUI subsystem, no console),
  so **every `fprintf(stderr, ...)` in the app goes nowhere on Windows** —
  including `gladLoadGL failed` (`main.cpp:33338`), `glfwCreateWindow failed`,
  every shader compile log and every audio-device error. A user whose GL init
  fails sees the app flash and vanish with no message.
- There is **no `SetUnhandledExceptionFilter` and no minidump writer** anywhere
  in `src/` (verified by grep). macOS produces `.ips` reports automatically;
  Windows produces nothing.

**Fix.**
1. Install a `SetUnhandledExceptionFilter` on Windows that writes a minidump
   plus a text log to `%APPDATA%\Infinite\crash\`. Use `AppPaths.h` for the
   directory, and honour `WinCommon::Utf8ToWide` for the path (see §3.7 of the
   `windows-parity` skill — non-ASCII account names are a live bug class here).
2. Route fatal startup failures to a `MessageBoxW` as well as stderr.
3. Mirror the existing stderr diagnostics into a rolling
   `%APPDATA%\Infinite\log.txt` so a user can attach it to a report.

This is the highest-leverage item in the brief: it converts "it crashed" into a
stack trace for every future Windows report.

---

## 5. Secondary hardening while in here

Eight GLSL literals of the form `vec2(1,0)` — spec-legal, but the exact pattern
strict drivers reject, and free to normalise to `vec2(1.0, 0.0)`:

- `src/core/FilterDefs.cpp:463,464`
- `src/nodes/NoiseNode.cpp:44,45`
- `src/nodes/ResynthNode.cpp:53,54`
- `src/nodes/TextureNode.cpp:86,87`

---

## Out of scope for this session

Already written up in `docs/WINDOWS_VERIFICATION.md`, re-verified as still live,
but unrelated to rendering — do not fold them in:

- `PlatformWin.cpp:398` — `GGO_GLYPH_INDEX` with a character code; all Windows
  3D text uniformly mis-scaled.
- `MediaDecodeWin.cpp:192,314,445,660` — OBJ/STL/PLY/AIFF fail under non-ASCII
  paths, in a file that already defines the `WPATH()` fix helper.
- `MidiWin.cpp:170` (N-producer note ring), `:185` (unreachable System Realtime).
- `PluginVST3Win.cpp:367,418` — VST3 crash sentinel inert for non-ASCII account names.

## Exit criteria

```bash
grep -rn "_WIN32" src/nodes/                                   # must print nothing
grep -rn "CONTEXT_VERSION_MINOR" src/main.cpp                  # 3 at both sites
grep -rn "fopen(\|ifstream\|ofstream" src/platform/win/ | grep -v Utf8ToWide | wc -l   # must stay 6
.claude/skills/run-infinite-hygiene/driver.sh                  # green
```

---

# Addendum — Windows audio architecture

Read after the rendering items. `src/platform/win/AudioDeviceWin.cpp` is ~4,900
lines of WASAPI that nobody with commit access can run.

## What is architecturally sound (do not "fix" these)

- **Shared mode + event-driven** (`:347-359`): `Initialize(AUDCLNT_SHAREMODE_SHARED,
  AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, 0, mixFormat, nullptr)` with
  `SetEventHandle`, pumped by `WaitForMultipleObjects` on
  `{stopEvent, bufferEvent}` (`:387`). This is the correct idiom.
- **MMCSS** via `ProAudioScope`, reverted on every exit path including the
  early return (`:317`).
- **Teardown** uses `joinable()`, not a running flag (`:269`) — ledger 1.2, fixed.
- **`ReleaseBuffer(frames, 0)`** at `:430` releases exactly what was filled, not
  what `GetBuffer` returned — no uninitialised audio is ever played.
- **Device-change / sleep recovery** is real and shared: `OnDeviceRemoved` and
  `OnDefaultDeviceChanged` latch `gConfigChangedFlag` (`:95,:103`), consumed by
  `PollAudioRecovery` at `src/main.cpp:23348`, which funnels through the same
  `StartAudioEngine` choke point as the settings menu, with rate-limiting and a
  backoff window. `IsAlive()` also catches a render thread that exited on
  `AUDCLNT_E_DEVICE_INVALIDATED`.
- `OnPropertyValueChanged` correctly takes `PROPERTYKEY` **by value** — the
  comment at `:107` documents why a `const&` silently fails to override.

## A1. Non-float mix format is a hard failure — probable cause of "no audio on Windows"

`IsFloatFormat` (`:221-232`) accepts only `WAVE_FORMAT_IEEE_FLOAT` or
`WAVE_FORMAT_EXTENSIBLE` with `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`. It gates the
entire open on **both** the render path (`:342`) and the capture path (`:841`):

```cpp
if (IsFloatFormat(mixFormat) && mixFormat->nChannels <= kMaxChannels) { ... }
else { hr = AUDCLNT_E_UNSUPPORTED_FORMAT; }
```

There is **no PCM conversion path anywhere in the file**. When `GetMixFormat`
returns 16- or 24-bit PCM, Infinite has no audio at all and no usable message
(see item 4 of the main brief — stderr is invisible in a GUI-subsystem process).

This is a real and common configuration: Windows' Sound control panel lets the
user choose the shared-mode default format, and several driver classes report
PCM from `GetMixFormat` — older Realtek HDA, some USB class-compliant
interfaces, and Bluetooth/hands-free endpoints. CoreAudio always presents
float32, so macOS never exercises this branch. Textbook "works on Mac, dead on
Windows".

**Fix.** Add an interleaved format-conversion layer at the WASAPI boundary
covering int16, int24-in-32, and int32, in both directions (render and capture),
keeping the engine's internal planar float contract unchanged. Convert in the
same loop that already interleaves at `:425-429`. Keep the `nChannels >
kMaxChannels` rejection — that one is a genuine limit.

**Check.** Set the output device to "16 bit, 44100 Hz" in Sound control panel;
audio must still work. Add a headless fixture that runs the conversion helpers
over each supported `WAVEFORMATEXTENSIBLE` subtype and asserts round-trip
accuracy — this one *is* CI-reachable and should be added to the Windows job.

## A2. Sample rate and buffer size are silently discarded on Windows

`src/platform/win/AudioDeviceWin.cpp:556-557`:

```cpp
(void)requestedSampleRate;
(void)requestedBufferFrames;
```

macOS honours both (`src/platform/Platform.mm:2756-2772`, setting the device's
nominal rate and buffer frame size). The UI that feeds them is real:
`src/main.cpp:36206-36207` calls `SetRequestedSampleRate(gAudioSampleRate)` and
`SetRequestedBufferFrames(gAudioBufferFrames)` from the audio settings apply
path.

So on Windows a user opens audio settings, picks 44.1 kHz / 128 frames, applies,
and **nothing happens** — silently. The device keeps running at the Windows mix
format and the device period. This will read to users as "buffer size doesn't
work" and is worth expecting in the report pile.

**Fix — decide deliberately, do not paper over it.** Shared mode structurally
cannot change the sample rate; the honest options are:
1. Try `AUDCLNT_SHAREMODE_EXCLUSIVE` with the requested format first and fall
   back to the current shared path when it fails. Gives real control over both
   rate and period; costs device exclusivity.
2. Keep shared mode and **disable the controls on Windows** with a tooltip
   saying the format is owned by the Windows mix format, with a shortcut to the
   Sound control panel.

Option 2 is the smaller change and is not a regression — it just stops the UI
from lying. Do not leave the controls live and inert.

**Check.** Whichever is chosen, the applied rate/buffer reported back through
`outSampleRate` must match what the settings UI displays afterwards.

## A3. Still open from the ledger (audio-adjacent, unchanged)

- 1.6 analyser `low`/`mid`/`high` bands differ from macOS.
- 1.8 `MidiWin.cpp:170` N-producer note ring.
- 1.9 capture-thread races.

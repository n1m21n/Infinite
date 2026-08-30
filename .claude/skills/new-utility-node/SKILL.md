---
name: new-utility-node
description: The standard procedure for adding a Utility/IO node to Infinite - a node whose job is to move data in or out of the patch (Output/record/export, Syphon In/Out, Projection, OSC Send/Receive, Video In) rather than to make an image. Covers the terminal-node identity-pass pattern, the Platform:: one-abstraction rule that keeps it building on Windows, resource ownership across GL contexts, external side effects and how to gate them, and the machine-checkable exit criterion. Use when implementing an export/broadcast/receive/protocol node, when writing the prompt for a fresh session that will implement one, or when an IO node leaks a handle, crashes on delete, works on macOS but not Windows, or fires its side effect when it shouldn't.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read `new-source-node` / `new-compositing-node` first for the shared image-node
mechanics. This skill is about what makes an IO node different: **it has an
effect outside the process** - a file on disk, a texture published to another
app, a UDP packet, a projector window on a second display - and that effect
has a lifetime the node must own.

Current members: Output, Syphon In, Syphon Out, Projection, OSC Receive, OSC
Send, and the audio IO nodes (Gain, Audio In, Audio Out, Mixer, Splitter,
Blend Audio - those follow `new-audio-node`, not this skill).

---

## 0. A terminal node is still a normal node

`OutputNode`'s class comment states the rule:

> Terminal node. Passes its input through into its own FBO (identity pass) so
> it has a real cook/output-texture lifecycle like any other node.

So even a node whose point is "the image leaves here" still owns an FBO, still
memoizes on `frameId`, still reports width/height, and still overrides
`BypassSource()` to hand its input through. Do not write a node that returns
texture 0 and does its work as a side effect of being drawn - a viewport card,
a projector window, and the recorder all read the node's *output texture*.

Also declare `InputLabel()`. IO nodes usually have heterogeneous slots
(`OutputNode`: `"in"` and `"audio"`), and A/B labels are meaningless there.

---

## 1. The one-abstraction rule (read `windows-parity` before writing code)

**No `#ifdef _WIN32` in the node layer, ever.** Every platform difference
lives behind a `Platform::` function with a real implementation on both
sides. `SyphonOutNode` is the model: it holds a
`Platform::SyphonServerHandle*` and never mentions Syphon or Spout in its own
code - the macOS side is IOSurface, the Windows side is Spout2
(`src/platform/win/PlatformWinSyphon.cpp`), and the node is identical.

Every `Platform::` function you add carries a two-sided obligation: **write
both implementations in the same commit**, even when the Windows one is a
stub that fails soft. A missing Windows implementation is not a compile error
in a macOS-only build - it is a Windows build break someone else discovers.

`.claude/skills/windows-parity/SKILL.md` has the per-subsystem trap catalogue
(WASAPI teardown, WinMM status bytes, Media Foundation stride, wide paths,
GLSL 330 strictness). Read it before touching anything device- or file-backed.

---

## 2. Resource lifetime - the class of bug that actually bites here

An IO node owns handles that outlive a single frame: an encoder, a server, a
socket, a GL object created in *another context*. Three rules:

- **Release in the destructor, and make double-release safe.** Deleting the
  node mid-operation (recording, publishing, receiving) is a normal user
  action, not an edge case. `INFINITE_DELETECRASHTEST` and
  `INFINITE_RECTEARDOWNTEST` exist because this went wrong.
- **GL objects belong to the context that created them.** A projector window
  has its own GL context sharing with the main one; `NodeViewport::Render`
  called inside a projector window's context generates names owned by that
  context, and closing the window without releasing them there first leaked
  them into the editor (see the projector teardown block around `main.cpp`
  ~24957). If your node can render into more than one context, release in
  the one that allocated.
- **Drain before you free.** `OutputNode::StopRecording` drains the queue and
  joins the encoder before dropping `mRecorder`, which is why
  `LastRecordedFrames()/LastDroppedFrames()` exist as separate accessors -
  the live ones are gone by then. An async writer that is freed with frames
  in flight either loses them or crashes.

---

## 3. Side effects must be explicit and off by default

A node that writes files, opens ports, or publishes to other apps must not
start doing so merely by existing on the canvas:

- **Arm it explicitly** (a record button, a "publish" toggle, an explicit
  port/name field), and make the armed state visible on the node body.
- **Persist the intent, not the activity.** `VisitParams` saves the path, the
  server name, the port - never "currently recording". A patch that starts
  recording to the last take's path on load will overwrite someone's work.
- **Fail soft and say so.** Keep a status string on the node
  (`OutputNode::RecordStatus()`) and surface it in the body. An IO node that
  silently does nothing when a device/port/app is missing is indistinguishable
  from a broken one.

---

## 4. Split the pure arithmetic out so it is testable without hardware

The single most reusable pattern in this family, from `OutputNode`:

```cpp
// Pure arithmetic half of the A/V pacing, exposed so it can be asserted
// directly (INFINITE_RECSYNCTEST) without a GL context, a device or a movie
// file - which is what makes it checkable on Windows CI too.
static int PacedRepeat(long long audioFrames, double rate, int fps,
                       long long emitted, bool finalDrain);
```

Anything an IO node computes - pacing, timecode, warp matrices, packet
encoding, resolution negotiation - should be a static pure function with a
fixture, because the surrounding IO cannot be exercised on CI. Do this while
writing the node; retrofitting it means rewriting the node.

---

## 5. The wiring checklist

1. **Class** in `src/nodes/<Name>Node.h/.cpp`; both in `CMakeLists.txt`.
2. **Platform seam**: any OS-specific work goes behind a new or existing
   `Platform::` function, implemented on **both** platforms (§1).
3. **Image input**: add the node to all four chains in `main.cpp` -
   `InputCountFor`, `CableFor`, `IsInputSlotCompatible`, `WireInputSlot`
   (see `new-compositing-node` §0). Audio/note inputs are generic
   (`AudioInputSlot`/`NoteInputSlot`) and need no chain entry.
4. **`BypassSource()`** returning the input's source, and `InputLabel()`.
5. **`VisitParams`** - configuration only, never live state (§3).
6. **Register**: `REGISTER_NODE(FooNode, Foo, "Utility");` - one word.
7. **`Draw*Params`** with the `Mod*` widget family, plus the status line.
8. **Destructor** releasing every handle, safe to run mid-operation (§2).
9. **A pure-function fixture** for whatever arithmetic the node does (§4).

---

## 6. Bug traps, each of which has already happened in this codebase

- **`_WIN32` in the node layer** - see `windows-parity`; the fix is always a
  `Platform::` seam, never a branch in the node.
- **A `Platform::` function with only one implementation** - Windows build
  break discovered by someone else, days later.
- **GL objects released in the wrong context** (§2) - leaked projector
  textures.
- **Freeing an async writer with frames in flight** (§2).
- **Video/audio drift in the exported file** - the video track's PTS is a
  frame counter over `recordFps` while audio is stamped from the real
  captured sample count, so emitting one video frame per *rendered* frame
  makes the two walk apart linearly. `PacedRepeat` is the fix;
  `INFINITE_RECSYNCTEST` is the guard. See `av-sync-sweep`.
- **Saving live state in `VisitParams`** (§3).
- **A node that only works while its body is visible** - anything the node
  must do every frame belongs in `CookIfNeeded`, not in the draw function.
  Collapsed params, a scrolled-off node, and a headless fixture all skip
  drawing.

---

## 7. Tests

```bash
.claude/skills/output-projection-sweep/driver.sh   # Syphon/Spout, projector windows, Projection node
.claude/skills/av-sync-sweep/driver.sh             # export/record timing
.claude/skills/run-infinite-hygiene/driver.sh      # full gate, includes DELETECRASHTEST
```

On macOS, `INFINITE_SPOUTLOOPTEST` reports SKIP by design (Spout is
Windows-only) - that is not a pass for the Windows path. Anything IO-shaped
needs the `pillar-parity-audit` question asked explicitly: **what evidence do
we have on Windows?**

---

## 8. Exit criterion - state it machine-checkably in every prompt

> The node is done when: `run-infinite-hygiene/driver.sh` passes with the node
> registered (including `DELETECRASHTEST` with the node mid-operation);
> `output-projection-sweep/driver.sh` passes; every `Platform::` function the
> node introduced has a macOS *and* a Windows implementation, and
> `grep -rn "_WIN32" src/nodes/<Name>Node.*` is empty; the node's pure
> arithmetic has a fixture that runs headless; and a save/load round trip
> restores its configuration without resuming its side effect.

---

## 9. Prompt template for a fresh session

> Add a `<name>` utility/IO node to Infinite (category `Utility`).
> Follow `.claude/skills/new-utility-node/SKILL.md` and
> `.claude/skills/windows-parity/SKILL.md` exactly.
> It moves `<what>` `<in/out of>` the patch via `<protocol/API>`.
> All OS-specific work goes behind `Platform::` functions implemented on both
> macOS and Windows in the same commit; no `_WIN32` in the node. It is a
> terminal node with a real identity-pass FBO, overrides `BypassSource()` and
> `InputLabel()`, saves configuration only (never live state) in `VisitParams`,
> releases every handle in its destructor safely mid-operation, and exposes its
> arithmetic as a static pure function with a headless fixture.
> Start on a branch per `.claude/skills/git-branch-workflow/SKILL.md`.
> Done when the exit criterion in §8 of that skill is met.

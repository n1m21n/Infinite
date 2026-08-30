---
name: new-source-node
description: The standard procedure for adding a Source node to Infinite - a node that generates an image from nothing (procedural, file-backed, or live-device) rather than transforming one. Covers the INode/GLUtil::Fbo pattern, the per-frame cook memo, resolution ownership, the Transport clock rule for anything animated, TextureRevision and what it costs to skip it, and the machine-checkable exit criterion. Use when implementing a generator/loader/capture node (noise, gradient, text, shape, video, camera, file image), when writing the prompt for a fresh session that will implement one, or when a source node renders black, never animates, animates while paused, or forces everything downstream to re-render every frame.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Sibling of `new-effect-node` (which transforms an image) and
`new-compositing-node` (which combines several). A **Source has no image
input** - that one difference drives everything below. Read `new-effect-node`
first if you have not written an image node here before.

Current members of the category: Image Source, Shape (plus one registration
per shape name), Formula, Text, Video, Video In, Noise, Texture, Ramp, Draw,
Audio Texture. `Syphon In` is a source in behaviour but is deliberately
registered under `Utility` with the other app-to-app IO - see
`new-utility-node`.

---

## 0. The shape

```cpp
class NoiseNode : public INode
{
   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth()  const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
private:
   GLUtil::Fbo  mOut;
   unsigned int mProgram = 0;
   bool         mShaderTried = false;
   int          mLastCookFrame = -1;
};
```

`src/nodes/NoiseNode.h/.cpp` is the reference implementation for the
procedural case; read it once before writing anything.

A source **must not** override `BypassSource()`. The base returns `nullptr`,
and that is correct: bypassing a source removes it from the chain entirely
(`ImageCable::Resolved()` walks past bypassed nodes and hands back nullptr
when the walk ends on one). A source that returns anything here creates a
node that cannot be bypassed.

---

## 1. `CookIfNeeded` - the three rules

```cpp
void NoiseNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;   // 1. memoize per frame
   mLastCookFrame = frameId;
   if (!EnsureShader()) return;             // 2. fail soft, never half-render
   const int w = std::max(4, (int)width), h = std::max(4, (int)height);
   if (!GLUtil::EnsureFbo(mOut, w, h)) return;
   GLUtil::RunShaderPass(mOut, mProgram, [this]{ /* uniforms */ });  // 3.
}
```

1. **Memoize on `frameId`.** Cooking is pull-based: every downstream consumer
   calls `CookIfNeeded`, so a source feeding five nodes is asked five times
   per frame and must render once. Missing this is a silent 5x GPU cost, and
   for anything with per-cook state (a feedback buffer, a frame counter) it is
   a correctness bug, not just a speed one.
2. **Fail soft.** A failed shader compile or FBO allocation returns early and
   leaves the previous texture intact. Never leave a half-bound FBO or a
   partially-written texture - that is where "renders black on Windows only"
   comes from.
3. **Clamp your own resolution.** A source *owns* its output size (unlike a
   filter, which usually matches its input). Clamp the low end
   (`std::max(4, ...)`) - a 0-sized FBO is undefined behaviour in the GL
   layer, and width/height are modulatable floats a cable can drive to zero.

---

## 2. Animation reads the Transport clock, never wall-clock

```cpp
glUniform1f(..., "uTime", (float)Transport::Instance().Seconds() * speed);
```

`Transport::Seconds()`/`Beats()` freeze with Pause and follow BPM.
`glfwGetTime()`/`std::chrono` do not - a source using them keeps animating
through Pause and cannot be scrubbed, which breaks recording and export
determinism as well as the obvious UI expectation. Use `Beats()` for anything
rhythmic, `Seconds()` for anything with a physical rate.

---

## 3. `TextureRevision()` - opt in when the output is often static

The default `INode::TextureRevision()` returns a fresh value on every call -
"always changed" - which is conservative and correct, and is what `NoiseNode`
uses. Downstream `FilterNode` caches then re-render every frame.

Override it (`return mRevision;`, bumped only inside the branch that actually
re-renders - see `FilterNode.h`, `AudioTextureNode.h`, `EnvironmentNode.h`)
when the source is **usually idle**: a loaded file image, a text render, a
static gradient. That is the difference between an idle patch settling to no
GPU work and one pinned at 100%.

Two ways to get this wrong, both of which have shipped here in the geometry
equivalent (see `geometry-transform-sweep`'s REVISIONSWEEPTEST):

- **Bumping unconditionally** (in `CookIfNeeded` before the early-out, or on
  every cook regardless of whether pixels changed) - downstream caches never
  hit, and any consumer that treats a stamp change as a *structural* change
  resets state every frame.
- **Not bumping when pixels did change** - the frame freezes on screen and no
  param appears to do anything. If in doubt, do not override it at all;
  "always changed" is never wrong, only slow.

---

## 4. The wiring checklist

1. **Class** in `src/nodes/<Name>Node.h/.cpp`; add both to `CMakeLists.txt`.
2. **`VisitParams`** for every saveable field, including `width`/`height`.
3. **Register** in `RegisterNodes()` (`src/main.cpp` ~3235):
   `REGISTER_NODE(FooNode, Foo, "Source");`. One-word category (Patch.cpp
   reads it with `>>`). If the node is really one class with N presets, use
   the loop form that Shape/Geometry use - one searchable name per preset,
   `CreateFor(i)` - rather than a dropdown users have to know to open.
4. **`Draw*Params`/`Draw*Body`** in `main.cpp` using the `Mod*` widget family
   so every control is modulatable (`node-param-audit`).
5. **`#include`** the header near the other node includes in `main.cpp`.
6. **Nothing for pins** - a source has no image input slot, so there is no
   `CableFor` entry to add.

If the source is **file-backed** (image/video/model), also: keep the path in
`VisitParams` as `Text`, resolve it relative to the patch on load, and handle
the missing-file case by keeping the last good texture and surfacing the
error on the node body - never by rendering black silently.

If the source is **device-backed** (camera, screen, Syphon/Spout in), read
`.claude/skills/windows-parity/SKILL.md` before writing a line: every capture
path has a separate Windows implementation behind `Platform::`, and the
node layer must contain no `_WIN32`.

---

## 5. Bug traps, each of which has already happened in this codebase

- **No frame memo** (§1.1) - N-times-per-frame rendering.
- **Wall-clock animation** (§2) - runs while paused.
- **Zero/negative FBO size** from a modulated width/height - clamp.
- **Unconditional revision bump** (§3) - kills every downstream cache and, on
  the geometry side, continuously reset a running simulation.
- **Raw ImGui widgets in the params function** - the control silently is not
  modulatable and does not appear in the performance matrix picker. Same root
  cause for both symptoms; see `node-param-audit`.
- **A float param drawn conditionally** renumbers every float param below it
  (`gParamCounter++` ordinals) and existing cables jump to the wrong control.

---

## 6. Tests

```bash
.claude/skills/compositing-pipeline-sweep/driver.sh   # covers 2D chain invariants
.claude/skills/data-accuracy-sweep/driver.sh          # cook memo + revision honesty
.claude/skills/run-infinite-hygiene/driver.sh         # full gate before committing
```

The 167-node round trip in the hygiene suite covers save/load and spawn for
the new type automatically once it is registered. Add a node-specific fixture
only for behaviour those cannot know (an analytic gradient value, a known
pixel at a known coordinate).

---

## 7. Exit criterion - state it machine-checkably in every prompt

> The node is done when: `run-infinite-hygiene/driver.sh` passes with the node
> registered; `data-accuracy-sweep/driver.sh` reports the node cooks exactly
> once per frame under multiple consumers and its `TextureRevision()` is
> stable across two cooks with no param change; the node's row in
> `docs/node_param_audit.md` shows no non-text gaps; and a screenshot of the
> node with a downstream Output shows the expected image.

---

## 8. Prompt template for a fresh session

> Add a `<name>` source node to Infinite (category `Source`). Follow
> `.claude/skills/new-source-node/SKILL.md` exactly.
> Output: `<what the image is>`. Params: `<list, ranges, defaults>`.
> It owns its own resolution (width/height params, clamped), memoizes its cook
> on `frameId`, animates off `Transport::Instance().Seconds()/Beats()` (never
> wall-clock), fails soft on shader/FBO failure, declares every param in
> `VisitParams`, and draws its controls with the `Mod*` widget family.
> `<Override TextureRevision() with a real stamp / leave it at the default>`.
> Start on a branch per `.claude/skills/git-branch-workflow/SKILL.md`.
> Done when the exit criterion in §7 of that skill is met.

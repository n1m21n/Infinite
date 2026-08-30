---
name: new-compositing-node
description: The standard procedure for adding a Compositing node to Infinite - an image node with more than one input, or with state that persists across frames (Blend, Layer Stack, Switcher, Feedback, Trails, Reaction Diffusion, Curves, Color Ramp, Fit, Null, Viewport). Covers the four hand-maintained wiring chains an image-input node must be added to, the BypassSource rule, ping-pong/persistent-state cooking, and the machine-checkable exit criterion. Use when implementing a multi-input image operator or a frame-persistent effect, when writing the prompt for a fresh session that will implement one, or when a new image node draws no input pins, ignores a connected cable, or breaks when bypassed.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read `new-effect-node` first. **Most image ideas belong there, not here**: a
single fragment pass over one or two textures is a `FilterDef` table row, no
new class. This skill is for the cases that genuinely cannot be:

- more than two image inputs, or a variable input count (Layer Stack,
  Switcher),
- state that persists *across frames* - ping-pong buffers (Feedback, Trails,
  Reaction Diffusion),
- CPU-side or non-shader work (Remove Background's Vision segmentation),
- a node that manages image flow rather than pixels (Null, Viewport, Fit).

---

## 0. The trap that defines this family: four parallel chains

Audio and geometry inputs are discovered **generically** through virtuals on
`INode` (`AudioInputSlot`, `NoteInputSlot`, `GeometryInputSlot`). **Image
inputs are not.** They are resolved through hand-maintained `dynamic_cast`
chains in `src/main.cpp`, and a new image-input node must be added to each
one. Missing an entry fails silently and differently per chain:

| Site (`src/main.cpp`) | What it answers | Symptom if you forget |
|---|---|---|
| `InputCountFor` (~3587) | how many input pins to draw | the node draws no input pin at all |
| `CableFor` (~3723) | which `ImageCable*` a slot maps to | pin draws, cable "connects", nothing arrives |
| `IsInputSlotCompatible` (~3909) | what may connect there | drag is rejected (or wrongly accepted) |
| `WireInputSlot` (~3985) | performs the connection | link accepted, never stored |

`DisconnectAllTo` and the patch save/load path go through the same
`CableFor`, so a missing entry there also means the connection does not
survive a save, and deleting the upstream node leaves a stale pointer.

**Write all four entries in the same commit as the class.** This is the
single most common way a new compositing node ships half-wired.

---

## 1. The shape

`src/nodes/BlendNode.h` is the reference two-input node:

```cpp
class BlendNode : public INode
{
   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int  GetOutputWidth()  const override { return mOut.w; }
   int  GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   ImageCable& InputA() { return mInputA; }
   ImageCable& InputB() { return mInputB; }

   INode* BypassSource() override
   { return mInputA.IsConnected() ? mInputA.GetSource() : mInputB.GetSource(); }

   void VisitParams(ParamVisitor& v) override { v.Int("mode", mModeIndex); v.Float("mix", mMix); }
};
```

For a variable input count, follow `LayerStackNode`/`SwitcherNode`: a
`static constexpr int kSlots`, an `ImageCable& Input(int)` accessor, and the
`kSlots` entry in `InputCountFor`.

---

## 2. `BypassSource()` - every node here must override it

Bypass is not a per-node feature; it is the cable walk in
`ImageCable::Resolved()`:

```cpp
INode* node = mSource;
for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
   node = node->BypassSource();
return (node != nullptr && node->bypassed) ? nullptr : node;
```

So `BypassSource()` answers "if I am switched off, whose image should the
consumer get instead?". The convention:

- **One image input** → return that input's source.
- **Several** → return the *primary* one, falling back to the next connected
  one (Blend's A-else-B above). Never return `this` - the hop limit will
  catch the loop but the node renders nothing.
- **A source with no input** → do not override; `nullptr` is correct.

A missing override on a multi-input node means bypassing it blanks the chain
instead of passing the base layer through, which is what users expect and
what `INFINITE_BYPASSTEST` checks.

---

## 3. Cooking with cross-frame state

The per-frame memo (`if (mLastCookFrame == frameId) return;`) is mandatory
here for correctness, not just speed: a node with a ping-pong buffer that
cooks twice in one frame advances its simulation twice, at a rate that
depends on how many consumers are downstream.

Rules for persistent state:

- **Pull every input before rendering.** `cable.Pull(frameId)` cooks the
  upstream node and hands back its texture; skipping it for an input you
  "don't need this frame" leaves that branch of the graph uncooked.
- **Size changes must reallocate both buffers** and clear them - a
  ping-pong pair where only one side was resized reads garbage on the first
  frame after a resolution change.
- **Respect the Transport.** A frame-advancing simulation should step off
  `Transport::Instance()` (paused means paused), and must handle a rewind:
  if the transport jumped backwards, reset rather than run backwards.
- **Track `TextureRevision()` honestly.** A node with cross-frame state
  genuinely does change every frame while running, so the default
  "always changed" is right - but it should *stop* changing when the
  transport is paused and the buffers are static, or every downstream
  cache is dead for the whole session.

---

## 4. The wiring checklist

1. **Class** in `src/nodes/<Name>Node.h/.cpp`; both added to
   `CMakeLists.txt`.
2. **All four chains** in `main.cpp` (§0).
3. **`BypassSource()`** (§2).
4. **`VisitParams`** for every saveable field.
5. **Register**: `REGISTER_NODE(FooNode, Foo, "Compositing");` - one-word
   category, `Patch.cpp` reads it with `>>`.
6. **`Draw*Params`** with the `Mod*` widget family (`node-param-audit`).
7. **`#include`** the header in `main.cpp`.
8. **Input labels**: override `InputLabel(int slot)` - the editor defaults to
   A, B, C..., which is unreadable for a node whose slots mean different
   things ("base"/"blend", "src"/"mask").

---

## 5. Bug traps, each of which has already happened in this codebase

- **Three of four chains updated** (§0) - the failure is silent and the
  symptom depends on which one is missing.
- **No `BypassSource()`** (§2) - bypass blanks the chain.
- **Double-cook advancing a simulation** (§3).
- **Reading `GetOutputTexture()` without `Pull()`** - you get last frame's
  texture, or 0 on the first frame, and the bug only shows in patches where
  the upstream node has no other consumer.
- **Ignoring bypass when reading an input directly.** Use
  `cable.Pull()/Resolved()`, never `cable.GetSource()->GetOutputTexture()` -
  the latter reads *through* a bypassed node as if it were live.
- **Resolution taken from input A only.** With mismatched inputs, decide and
  document which input defines the output size (`FitNode` exists for the
  explicit case) rather than letting whichever cooked first win.

---

## 6. Tests

```bash
.claude/skills/compositing-pipeline-sweep/driver.sh
.claude/skills/cable-logic-sweep/driver.sh        # the four-chain consistency check
.claude/skills/run-infinite-hygiene/driver.sh     # full gate before committing
```

`cable-logic-sweep`'s static check is the one that catches a missing chain
entry without a build.

---

## 7. Exit criterion - state it machine-checkably in every prompt

> The node is done when: `run-infinite-hygiene/driver.sh` passes with the node
> registered; `cable-logic-sweep/driver.sh` reports the node present in all
> four wiring chains with consistent slot counts; `INFINITE_BYPASSTEST` passes
> with the node in a chain (bypassing it passes its primary input through);
> the node's row in `docs/node_param_audit.md` shows no non-text gaps; and a
> save/load round trip preserves every input connection.

---

## 8. Prompt template for a fresh session

> Add a `<name>` compositing node to Infinite (category `Compositing`).
> Follow `.claude/skills/new-compositing-node/SKILL.md` exactly.
> Inputs: `<list, with what each slot means>`. Params: `<list, ranges, defaults>`.
> It must be added to all four wiring chains in `main.cpp`
> (`InputCountFor`, `CableFor`, `IsInputSlotCompatible`, `WireInputSlot`),
> override `BypassSource()` to pass its primary input through, override
> `InputLabel()`, memoize its cook on `frameId`, pull every input cable
> before rendering, and declare every param in `VisitParams`.
> Start on a branch per `.claude/skills/git-branch-workflow/SKILL.md`.
> Done when the exit criterion in §7 of that skill is met.

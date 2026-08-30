---
name: cable-logic-sweep
description: Checks Infinite's connection rules - what can be patched into what, what must be refused, and whether a new node's pins are reachable at all. Cross-checks the four hand-maintained wiring chains in main.cpp against the cable members nodes actually declare (static, no build), and runs the fixtures that exercise connect/disconnect/delete. Use when asked "can this connect to that", "why won't this cable attach", "why does my node's pin do nothing", "check the connection logic", after adding any node with an input, or when a link is accepted and then silently lost on save.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
python3 .claude/skills/cable-logic-sweep/check.py     # static, ~1s, no build
.claude/skills/cable-logic-sweep/driver.sh            # static + runtime fixtures
```

## The rule the whole system rests on

**Audio, note, geometry and modulator inputs are discovered generically;
image inputs are not.** `INode` declares `AudioInputSlot`, `NoteInputSlot`,
`GeometryInputSlot` and `ModulatorInputSlot`, so a node that overrides one is
automatically found by every dispatch site. Image inputs are resolved by
hand-written `dynamic_cast` chains, and a node missing from one of them fails
silently, differently per chain:

| Site (`src/main.cpp`) | Answers | Symptom when a node is missing |
|---|---|---|
| `InputCountFor` (~3587) | how many pins to draw | no input pin appears |
| `CableFor` (~3723) | which `ImageCable*` a slot is | pin draws, link "connects", nothing arrives, nothing saves |
| `IsInputSlotCompatible` (~3909) | what a slot accepts | drag rejected, or wrongly accepted |
| `WireInputSlot` (~3985) | performs the connection | link accepted, never stored |

`check.py` proves 1-3 mechanically: every class declaring an `ImageCable` is
named in `CableFor`, everything in `CableFor` has a pin count, and every class
declaring an `AudioCable`/`NoteCable` overrides the matching generic accessor.
`IsInputSlotCompatible` and `WireInputSlot` both end in generic fallbacks, so
absence there is legitimate - the script lists their special cases for review
rather than failing on them.

## The connection matrix, as the code actually implements it

From `IsInputSlotCompatible`, in evaluation order - the order matters, the
first matching branch decides:

1. `AudioInputSlot(slot)` exists → accepts **audio sources only**
   (`IAudioSource` whose `IsAudioOutputIndex(outputIndex)` is true).
2. `NoteInputSlot(slot)` exists → accepts **note sources only** (`INoteSource`).
3. Source is audio or note but the slot is neither → **refused**. This is what
   stops an audio cable landing on an image pin.
4. Destination is Render 3D → geometry slots take geometry (not cameras, not
   lights), the camera slot takes a `CameraNode`, the env slot takes an
   `EnvironmentNode`, light slots take `LightNode`.
5. `GeometryInputSlot(slot)` exists → geometry only, explicitly excluding
   cameras and lights.
6. Material / Displacement / Set Color (non-palette slots) → image.
7. Set Color slot 2 → `IPaletteSource` only.
8. Source is geometry/camera/light and none of the above matched → **refused**:
   3D cables only go into 3D nodes.
9. `ModulatorInputSlot(slot)` exists (and the destination is not
   `ImageAnalyzeNode`) → modulator only.
10. Otherwise → **image**.

Two further rules live outside that function and are just as load-bearing:

- **No cycles in audio or notes.** `WouldCreateAudioCycle` /
  `WouldCreateNoteCycle` walk the existing graph and refuse a link that would
  close a loop. The image graph is *not* cycle-checked here - feedback is a
  legitimate 2D idiom, and `ImageCable::Resolved()` caps its bypass walk at 64
  hops instead.
- **A node cannot connect to itself** (`ConnectNodes`).

`ConnectNodes` (`main.cpp` ~4130) is the headless equivalent of the
`ed::QueryNewLink` UI path, sharing the same checks so the RemoteControl
`connect` RPC cannot create a link the UI would refuse. If you add a rule, add
it where both paths see it - inside `IsInputSlotCompatible`, not in the UI
handler.

## What the runtime fixtures cover

| Fixture | What it proves |
|---|---|
| `ROUNDTRIPTEST` | every registered type survives spawn → save → load |
| `PINDUPTEST` | no node gives two controls the same pin id (this used to hang the editor outright - a circular pin list in imgui-node-editor) |
| `DELETECRASHTEST` | deleting a wired node leaves no dangling image cable |
| `AUDIOTEARDOWNSWEEPTEST` | the same, generically, for every audio/note node type |
| `NOTEFANOUTTEST` | one note source feeding several consumers |

## The gap - what nothing checks yet

**There is no generic runtime sweep over the connection matrix itself.** The
static check proves a node is *reachable*; the fixtures prove specific
connections work. Nothing enumerates every (source type, destination type,
slot) pair and asserts accept/refuse against the table above.

That fixture is worth adding, and it is cheap because the pieces exist:
`DiscoverAudioSweepCandidates`-style enumeration over `NodeFactory`, a probe of
each node's slots via `InputCountFor`, and `IsInputSlotCompatible` called
directly - no UI, no GL, headless like `AUDIOPARAMSWEEPTEST`. The assertion is
that the accept/refuse verdict matches the matrix, and in particular that
**every refusal is a refusal for a stated reason** rather than a fall-through
to rule 10. Until it exists, a new node type that lands in the wrong branch of
that chain is caught only by someone dragging a cable by hand.

## Interpreting results

- `check.py` failure = a real wiring gap; the report names the chain and the
  symptom. Fix by adding the entry, not by working around it downstream.
- `[FAIL] DELETECRASHTEST` / `AUDIOTEARDOWNSWEEPTEST` = a use-after-free, not a
  cosmetic issue. Treat as blocking.
- `[NO VERDICT]` = the fixture never reached its printf. Raise its frame budget
  in `driver.sh` and re-run before assuming a failure.

# Connection rules — what can be patched into what, and what the red cable says

Every cable Infinite accepts or refuses is decided by one function,
`IsInputSlotCompatible` in `src/main.cpp`. Every sentence the user reads when a
drag is refused lives in the `ed::QueryNewLink` handler in the same file. This
document is the map between the two.

It is kept honest mechanically: `.claude/skills/cable-logic-sweep/check.py`
fails if a refusal message exists in the code but not in the table below, or in
the table below but not in the code. If you add a rule, add its message, and add
a row here — the sweep will tell you if you forget.

## The four paths a connection can be made through

All four now go through `IsInputSlotCompatible`, so a rule added there is seen
by every one of them. A rule added anywhere else is seen by exactly one.

| Path | Entry point | Used by |
|---|---|---|
| live drag | `ed::QueryNewLink` handler | the user dragging a cable |
| headless connect | `ConnectNodes` | RemoteControl `connect` RPC, `auto_wire_inputs`, cluster copy/paste |
| drag to empty canvas | spawn-on-drop auto-wire | dropping a cable on blank canvas and picking a node |
| node suggestions | `RecommendedNodeTypesForOutput` | the search popup's "recommended" list after that drop |

## The five cable kinds

| Cable | Carries | Discovered by |
|---|---|---|
| Image | pictures and video | hand-written `dynamic_cast` chains (`CableFor`, `InputCountFor`) |
| Geometry | 3D meshes | `INode::GeometryInputSlot` |
| Audio | sound | `INode::AudioInputSlot` |
| Note | musical events | `INode::NoteInputSlot` |
| Modulator | a wiggling knob | `INode::ModulatorInputSlot`, or a param pin |

Only image inputs are resolved by hand. A node missing from one of those chains
fails silently and differently per chain — see the cable-logic-sweep skill.

## The accept chain

`IsInputSlotCompatible` evaluates these in order. **The first matching branch
decides**; nothing below it runs.

| # | Condition on the destination slot | Accepts |
|---|---|---|
| 0 | source is an `IPredictor` (Predictive LFO / Macro) | **refuse everything** — it writes into params, it publishes no signal |
| 1 | `AudioInputSlot(slot)` exists | audio sources only, and only from an output index for which `IsAudioOutputIndex` is true |
| 2 | `NoteInputSlot(slot)` exists | note sources only |
| 3 | source is audio or note, slot is neither | **refuse** — this is what stops an audio cable landing on an image pin |
| 4 | Render 3D, slots 0-4 | geometry, explicitly not cameras or lights |
| 5 | Render 3D, slot 5 | `CameraNode` |
| 6 | Render 3D, slot 9 | `EnvironmentNode` |
| 7 | Render 3D, slots 6-8 | `LightNode` |
| 8 | `GeometryInputSlot(slot)` exists | geometry, explicitly not cameras or lights |
| 9 | Material, any non-geometry slot (1..`kMapCount`) | image |
| 10 | Displacement, slot 1 | image |
| 11 | Set Color, slot 2 | `IPaletteSource` only |
| 12 | Set Color, slot 1 | image |
| 13 | source is geometry/camera/light and none of the above matched | **refuse** — 3D cables only go into 3D nodes |
| 14 | `ModulatorInputSlot(slot)` exists and the destination is not Image Analyze | modulator only |
| 15 | otherwise | image |

Slot 0 of Material, Displacement and Set Color is a geometry slot and is caught
by rule 8 before rules 9-12 are reached.

## The three rules that live outside that function

| Rule | Where | Why |
|---|---|---|
| no audio cycles | `WouldCreateAudioCycle` | the audio graph has no delay stage, so a cycle makes the topological sort loop forever |
| no note cycles | `WouldCreateNoteCycle` | same walk, same reason |
| a node cannot connect to itself | `ConnectNodes`, and the `differentNodes` check in the drag handler | — |

**Image cycles are legal.** Feedback is a deliberate 2D idiom; the loop is
bounded by `ImageCable::Resolved()` capping its bypass walk at 64 hops rather
than by a refusal at connect time.

Param pins have two more refusals of their own, applied before the source is
even considered: a param driven by a Field Graph kernel cannot also be
modulated, and an `IPredictor` cannot drive an enum or bool param
(`PredictorBindRefusal`).

## Every message a refused drag can show

The cable turns red (`ImColor(255, 80, 80)`) and a tooltip carries one of these.
The tooltip is submitted between `ed::Suspend()` and `ed::Resume()` — inside the
canvas transform it would land offset from the cursor by an amount that grows
with zoom and pan.

### Drag shape

| Situation | Message |
|---|---|
| dragging a node's pin back to itself | `A node can't be connected to itself` |
| output-to-output, or input-to-input | `Drag from an output pin on the right of a node to an input pin on the left of another` |

### Parameter pins

| Situation | Message |
|---|---|
| the param is driven by a Field Graph kernel | `Cannot modulate a parameter driven by a Field Graph kernel` |
| a predictor onto an enum or bool param | `A predictor drives continuous parameters only, not switches or menus` |
| audio or note source onto a param | `Audio/note signals can't drive a parameter pin - only a modulator can` |
| geometry, camera or light onto a param | `3D objects cannot drive a parameter pin - only a modulator can` |
| anything else non-modulator onto a param | `Only modulator nodes (LFO, Envelope, Formula, etc.) can drive a parameter pin` |

### Colour pins

| Situation | Message |
|---|---|
| anything but a palette | `This color slot only accepts a Palette node` |

### Predictors

| Situation | Message |
|---|---|
| Predictive LFO / Macro into any node input | `Predictive LFO / Macro can only drive a parameter, knob or slider - not another node` |

### Audio and note

| Situation | Message |
|---|---|
| modulator into an audio pin | `A modulator can't drive an audio signal pin - only another audio source can` |
| anything else into an audio pin | `This pin only accepts an audio source` |
| non-note source into a note pin | `This pin only accepts a note source` |
| audio or note into a pin that is neither | `Audio/note signals only connect to a matching audio/note pin` |
| a cable that would close an audio loop | `Cannot connect: this would create an audio feedback loop` |
| a cable that would close a note loop | `Cannot connect: this would create a note feedback loop` |
| a non-audio source into Audio Analyze | `Audio Analyze accepts any audio source - Audio In, Audio File, an effect, a Mixer` |

### Render 3D

| Situation | Message |
|---|---|
| camera into a geometry slot | `Camera connects to the Camera slot (slot 5), not geometry slots` |
| light into a geometry slot | `Light connects to the Light slots (slots 6-8), not geometry slots` |
| HDRI into a geometry slot | `HDRI connects to the Environment slot (slot 9), not geometry slots` |
| anything else into a geometry slot | `Render 3D geometry slots only accept 3D geometry sources` |
| wrong source into slot 5 | `This slot only accepts a Camera 3D node` |
| wrong source into slot 9 | `Environment slot only accepts an HDRI Environment node` |
| wrong source into slots 6-8 | `This slot only accepts a Light 3D node` |

### Material, Displacement, Set Color, Mapping

| Situation | Message |
|---|---|
| geometry into a material map slot | `Material map slots accept 2D images or textures, not 3D geometry` |
| modulator into a material map slot | `Material map slots accept 2D images or textures, not modulators` |
| anything else into a material map slot | `Material map slots accept 2D images or textures` |
| geometry into the displacement height slot | `Displacement height slot accepts a 2D image or texture map, not 3D geometry` |
| anything else into the displacement height slot | `Displacement height slot accepts a 2D image or texture map` |
| wrong source into Set Color slot 2 | `Set Vertex Color palette slot only accepts a Palette node` |
| geometry into Set Color slot 1 | `Set Vertex Color texture slot accepts a 2D image or texture map, not 3D geometry` |
| anything else into Set Color slot 1 | `Set Vertex Color texture slot accepts a 2D image or texture map` |
| anything incompatible into Mapping | `Mapping transforms 3D surface coordinates. Wire 3D geometry into Mapping, then into Material or Render 3D.` |

### Geometry and image crossovers

| Situation | Message |
|---|---|
| camera or light into a geometry operator | `Camera and Light nodes connect to Render 3D, not geometry operators` |
| image into a geometry pin | `This pin requires a 3D geometry source, not a 2D image` |
| geometry, camera or light into a 2D image node | `3D geometry cannot be connected directly to a 2D image node. Connect geometry into a Render 3D node first.` |
| wrong source into a modulator input slot | `This pin only accepts a modulator source` |
| modulator into an image input | `Image inputs accept 2D image sources, not modulators` |
| nothing above matched | `Incompatible connection` |

`Incompatible connection` should be unreachable in practice: an image source
into a plain image pin is rule 15, which accepts. If a user ever sees it, a node
has landed in a branch of the accept chain that the reason chain does not cover,
and that is the bug to go and find.

## What is still not checked

There is no runtime fixture over the connection matrix itself. `check.py` proves
a node is *reachable* (its `ImageCable` is in `CableFor`, its `CableFor` entry
has a pin count, its audio/note cables override the generic accessors), and the
fixtures prove specific connections work. Nothing enumerates every
(source type, destination type, slot) triple and asserts the verdict against the
table above — in particular that **every refusal is a refusal for a stated
reason** rather than a fall-through to rule 15.

Two specific chains worth covering once that harness exists:

- a 2D `Feedback` loop walked through itself at 64+ hops, to check the
  `ImageCable::Resolved()` cap at its boundary rather than only its existence;
- a `Group` holding a node cabled to a node outside the group, then duplicated —
  cross-group cable rehoming on copy is a known sharp edge.

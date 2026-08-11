# Infinite — Phase 2: one geometry interface, no per-class ladders

Collapse `IGeometrySource` / `IPointCloudSource` / `ICurveSource` into a single
interface, then make connection, disconnection and link-rebuild generic instead
of a per-node-class `dynamic_cast` chain.

The payoff is not tidiness. `DisconnectAllTo` (`src/main.cpp:5255`) is a
hand-maintained list of every consumer field a dying node might be referenced
from, and its own comment says missing an entry "would leave a pointer to a
freed node and crash on the next cook." Every new geometry node type is a
chance to introduce a use-after-free. This change removes that class of bug
structurally.

Everything below was verified against the code before this prompt was written.
Do it in two independently buildable, independently revertible steps.

## A design decision already made — do not "improve" it

An earlier draft of this plan had `IGeometrySource` return a
`struct Geometry { shared_ptr<const Mesh>; shared_ptr<const PointCloud>; ... }`
by value. **That was rejected deliberately.** It requires all 19 implementors
to change how they *own* their mesh — `Mesh mCache` returned as `const Mesh&`
becomes `shared_ptr<const Mesh>` — plus every internal `GetMesh()` caller. That
is a rewrite, in a 12k-line `main.cpp` with no UI-layer test coverage, for a
purity win.

The plan below achieves the same external property — one socket type, one
validator, no ladders — without touching ownership or lifetimes at all. If you
find yourself editing how a node *stores* its mesh, you have left this phase.

## The current dispatch surface — confirmed

There are exactly **21** `dynamic_cast` sites across the three interfaces,
**all in `src/main.cpp`**: 12 for `IGeometrySource`, 6 for `IPointCloudSource`,
3 for `ICurveSource`.

Four ladders switch on node class to reach a differently-named pointer field:

| Ladder | Location | What it does |
|---|---|---|
| `ConnectGeometrySlot` | `src/main.cpp:1370` | assigns the source into the right field |
| `QueryNewLink` | `src/main.cpp:11620` (validation) and `:11740` (a **second**, separate assignment block) | validates, then assigns again |
| Link-table rebuild | `src/main.cpp:5470` and `src/main.cpp:11496` | matches pointers back to nodes via `asGeo`/`asCloud` `void*` compares |
| `DisconnectAllTo` | `src/main.cpp:5255` | nulls the dying node out of every consumer field |

Implementors: **19 real** `IGeometrySource` classes, plus 2 test-only probes
(`TransformProbeSource` `src/main.cpp:9263`, `MappingProbeSource`
`src/main.cpp:9480`). `IPointCloudSource`: `ParticleSystemNode`
(`src/nodes/SimulationNodes.h:26`), `ImageToPointsNode`
(`src/nodes/GenerativeNodes.h:128`). `ICurveSource`: `CurveNode`
(`src/nodes/CurveNode.h:14`).

**`InputCountFor` (`src/main.cpp:1219-1279`) needs no change.** It returns
per-class slot *counts*, which are unrelated to interface type. Confirmed by
reading it. Leave it alone.

**No patch-format change.** `Patch::CableRecord` (`src/core/Patch.h:47`) is
`{dstIndex, dstSlot, srcIndex}` with no type information, so pin/slot layout is
the only compatibility contract and this change preserves it. Existing patches
must load byte-identically.

## Step 2a — fold three interfaces into one

`src/core/Mesh.h` (where `IPointCloudSource` and `ICurveSource` live, at `:261`
and `:281`), `src/nodes/Geometry3DNodes.h:94` (`IGeometrySource`).

Add optional component accessors to `IGeometrySource`, defaulting to "I don't
have one":

    virtual const std::vector<Particle>* GetPointCloud() { return nullptr; }
    virtual unsigned long long PointCloudRevision() { return 0; }
    virtual const Polyline*             GetCurve()      { return nullptr; }
    virtual unsigned long long          CurveStamp()    { return 0; }

**Keep `const Mesh& GetMesh()` exactly as it is.** Do not change it to return a
value, a `shared_ptr`, or a wrapper struct. Ownership and lifetimes stay
untouched — that is what makes this step safe. A points-only node returns a
reference to a static empty `Mesh`.

Then:

- `ParticleSystemNode` and `CurveNode` implement `IGeometrySource` instead of
  the standalone interfaces. `ImageToPointsNode` already implements both
  (`src/nodes/GenerativeNodes.h:128`) — it just drops the second base class.
- Retire `IPointCloudSource` and `ICurveSource` entirely.
- Consumer fields become `IGeometrySource*`: `InstanceOnPointsNode::cloudSource`
  (`src/nodes/GeometryOpNodes.h:442`), `MetaBallNode::cloudSource`,
  `PathNode::curveSource`. Each then reads `GetPointCloud()` / `GetCurve()` and
  treats a `nullptr` return as "not connected to the right kind of thing" — the
  same guard it does today for a null pointer.
- **Delete the `clouds[kSlots]` parallel array added in Phase 1.**
  `Render3DNode::geometry[]` covers it now. This is the array Phase 1
  explicitly flagged as scaffolding. If Phase 1 has not landed yet, there is
  nothing to delete — skip this bullet.
- All 21 cast sites become one cast to `IGeometrySource*`. In `QueryNewLink`,
  every geometry pin's rule collapses to `valid = srcGeometry != nullptr`. In
  `DisconnectAllTo`, only `dyingGeometry` remains.
- `Render3DNode::Signature` (`src/nodes/Geometry3DNodes.cpp:1246`) must fold
  `PointCloudRevision()` and `CurveStamp()` into the draw signature alongside
  `geomRev`. If it does not, an animated cloud renders one frame and then
  freezes with no error.

**Judgment call, flagged:** `PathNode` and `MetaBallNode` currently *cannot* be
connected to a plain mesh, and after 2a they can be — the link validates, and
the node no-ops because `GetCurve()`/`GetPointCloud()` returns null. That is
Blender's actual behaviour (a node ignores components it can't use) and it is
the point of the change. But it trades a refused drag for a silent no-op. If
you'd rather keep the refusal, keep a narrow check in `QueryNewLink` against
those two pins only — do **not** reintroduce a general ladder. Recommendation:
allow it, and rely on the node preview showing nothing.

## Step 2b — a uniform geometry slot accessor

2a removes the *type* branching but leaves the *field-name* branching: each node
still stores its input in a differently-named member (`input`, `pointSource`,
`instanceShape`, `inputs[]`, `sourceInput`/`targetInput`, `geometry[]`). That is
why all four ladders still exist.

Add one virtual to `INode` (`src/core/INode.h`):

    // Address of the pointer field backing geometry input `slot`, or nullptr if
    // this node has no geometry input there. Lets connect/disconnect/rebuild be
    // generic instead of a per-class dynamic_cast chain.
    virtual IGeometrySource** GeometryInputSlot(int slot) { return nullptr; }

Each geometry-consuming node overrides it with a small `switch`/index returning
`&input`, `&pointSource`, `&inputs[slot]`, etc. Roughly 15 one-to-five-line
overrides.

Then rewrite the four ladders as generic loops:

- **`ConnectGeometrySlot`** → `if (auto** p = dst.node->GeometryInputSlot(slot)) *p = geo;`
  Keep the existing `CameraNode`/`LightNode`/`EnvironmentNode` branches — those
  are scene objects, not geometry, and stay special-cased.
- **`QueryNewLink`** → validate with
  `GeometryInputSlot(slot) != nullptr && srcGeometry != nullptr`, and **delete
  the duplicate assignment block at `:11740` entirely**, calling
  `ConnectGeometrySlot` instead. Two assignment paths that must agree is the
  bug source Phase 1 already had to warn about.
- **`DisconnectAllTo`** → one loop over `gNodes` × slots, nulling any slot whose
  value equals the dying source. This is where the use-after-free risk goes
  away: a new node type cannot forget to register.
- **Link-table rebuild** (`:5470`, `:11496`) → read slots through the accessor
  instead of `asGeo`/`asCloud` `void*` compares.

Also update the two test probes (`src/main.cpp:9263`, `:9480`) — they implement
`IGeometrySource` and will need the new virtual surface to compile.

## Out of scope

- **Named attributes (Phase 3).** No `Geometry` value type, no attribute arrays,
  no `shared_ptr` component ownership.
- **Selection as attribute (Phase 4).** `faceMask`/`selectionGroup` stay on
  `Mesh` exactly as they are.
- **Conversion nodes (Phase 5).** Add no new node types here.
- **`Camera`/`Light`/`HDRI` pins.** They are scene objects, not geometry
  components. Leave their branches alone.
- **`InputCountFor`.** Verified to need no change.

## Done means

1. Both steps build clean, separately:

       cmake --build build -j"$(sysctl -n hw.ncpu)"

2. The full hygiene suite passes — this change touches node lifecycle, patch
   load and the 163-node round trip, which is exactly what it covers:
   `.claude/skills/run-infinite-hygiene/`. Do not consider 2a or 2b done on a
   clean compile alone.
3. The geometry sweep passes, since every geometry node's side-channel
   forwarding is in scope here:

       .claude/skills/geometry-transform-sweep/driver.sh

4. **A patch saved before this change loads with every geometry link intact.**
   Save one on the current build *first*, with at least: a Render 3D with two
   geometry slots filled, an Instance on Points using both `points` and `shape`,
   a Metaballs fed by a Particle System, and a Path fed by a Curve. This is the
   single most important check — `CableRecord` carries no type info, so a
   slot-numbering slip loads silently wrong rather than failing loudly.
5. Deleting a node that feeds several consumers does not crash on the next cook.
   Test against Render 3D, Instance on Points (all three pins), Metaballs and
   Path in one graph.
6. `grep -rn "IPointCloudSource\|ICurveSource" src/` returns nothing.

## Stopping early is fine

2a alone is a real improvement and safely shippable. If 2b turns out messier
than the estimate, stopping after 2a leaves the codebase better rather than
half-migrated. Do not leave 2b partially applied across the four ladders — all
four or none.

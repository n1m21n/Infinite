# Design brief: dynamic (kernel-declared) pins on Field nodes

## Status

Not started. This is a research brief written for a future session to pick
up — no node or compiler code has been written against it. It was produced
by an investigation-only session; treat every file:line citation below as
current as of the commit this file was added in (`git log -1` on this file
to check drift before acting on it).

## Motivation, in the user's own words

> "the point of it was to customise it all as if someone was making a node
> from scratch and future use-cases is algorithm so would open up the
> possibilities in a different way"

Today Field has exactly four fixed node *shapes* — FieldElementNode (one
geometry input, one geometry output), FieldSampleNode (note + audio input,
one audio output), FieldPixelNode (one GLSL output), FieldGraphNode (no
typed I/O, spawns/wires other nodes via `emit`/`connect`/`set`/`place`). A
kernel today can only change *what happens inside* one of these fixed
envelopes — never the envelope itself. The user wants a kernel's own source
to be able to declare additional, differently-typed/domained pins on the
node hosting it — e.g. one kernel that both writes geometry *and* emits
audio, growing a second, audio-typed output pin because the source says so.
Their framing is explicit: this is a step toward Field kernels behaving like
fully custom node authoring (arbitrary in/out shape), not a small UI
convenience. Do not undersell this in any future proposal.

## Current state, with citations

### Pins are declared through fixed virtual accessors, not a data structure

`src/core/INode.h` has no vector-of-pins concept anywhere. Every pin kind is
a separate, fixed-shape virtual:

- `GeometryInputSlot(int)` → `IGeometrySource**` (INode.h:166)
- `ModulatorInputSlot(int)` / `ModulatorInputCount()` (INode.h:169-170)
- `AudioInputSlot(int)` / `NoteInputSlot(int)` (INode.h:176-177)
- `OutputCount()` (default `1`, INode.h:137) and `OutputLabel(int)` (default
  `"out"`, INode.h:138)
- `IAudioSource::IsAudioOutputIndex(int)` / `AudioOutputSlotForPin(int)`
  (INode.h:38-48) — lets one `INode` disambiguate *which* output index is
  its audio buffer once it has more than one output at all.

A node overriding these is picked up automatically by the generic dispatch
sites (audio/note/geometry/modulator inputs; see the `cable-logic-sweep`
skill's connection-matrix table). **Image inputs are the one exception** —
they go through four hand-maintained `dynamic_cast` chains in
`src/main.cpp` (`InputCountFor` ~3587, `CableFor` ~3723,
`IsInputSlotCompatible` ~3909, `WireInputSlot` ~3985). Any dynamic-pins
design that can add an image-typed pin at kernel-compile time must add an
entry to all four, by hand, or the new pin will silently misbehave (pin
draws but link doesn't connect / doesn't save / drags accept something it
shouldn't).

### A real precedent for *variable pin count* already exists — `GeometryTableNode`

`src/nodes/GeometryTableNode.h:45`:

```cpp
int OutputCount() const override { return 4 + 3 * RowCount(); }
```

`RowCount()` (line 51) is `std::clamp(rows, 1, 16)`, and `rows` is an
ordinary saved int param the user edits in the params panel. So this node's
pin *count* already changes at runtime based on the node's own state, and
main.cpp's generic output-pin-drawing code (main.cpp ~53966,
`std::max(1, gn.node->OutputCount())`) already copes with it generically —
except GeometryTableNode is special-cased there too (`geoTable != nullptr ?
4 : ...`) because it draws its own row pins inline in its params panel
rather than through the generic pin row, since pins beyond the first few
also carry an inline UI widget elsewhere. Read the comment at
GeometryTableNode.h:42-45 carefully — it states a **save-format stability
rule that any dynamic-pins design must inherit**: "growing `rows` must only
ever append new pins, never renumber existing ones," because
`Patch::CableRecord` (see below) addresses outputs by bare integer index,
not by name.

Other `OutputCount()` overrides worth knowing about: `DrumSequencerNode`
(`1 + kNumLanes`, a compile-time constant, not truly dynamic),
`MacroNodes.h` (fixed `2`), `NoteNodes.h`/`PathNode.h` (fixed `4`),
`AnalyzeNodes.h` (fixed `kOutputCount` enum), and `VideoSourceNode.h:32`
(fixed `2` — image output at index 0, audio output at index 1, using
`IsAudioOutputIndex`/`AudioOutputSlotForPin` to tell dispatch code which
index is which). **VideoSourceNode is the existing precedent for "one node,
two differently-typed outputs"** — but its two outputs are still hardcoded
at C++ compile time, not declared by any data the node interprets at
runtime. Nothing in the codebase today has a runtime-*typed*, not just
runtime-*counted*, output set.

**Conclusion: "variable pin count" is solved. "Variable pin domain/type,
driven by compiled kernel source" is not.** The gap is real but narrower
than "build pins from zero" — the pin-drawing and dispatch code already
tolerates `OutputCount()` changing at runtime; what's missing is (a) a way
for a *kernel* (not a hand-written C++ override) to say what those pins
*are* (their domain/type), and (b) the compiler/IR support to make each
output pin correspond to a distinct typed value the kernel computes.

### Save/load addresses pins by bare index, not name

`src/core/Patch.h`'s line grammar (comment block, Patch.h:16-53):

```
cable <dstIndex> <dstSlot> <srcIndex>
geo <dstIndex> <dstSlot> <srcIndex>
aud <dstIndex> <dstSlot> <srcIndex>
note <dstIndex> <dstSlot> <srcIndex>
```

`Patch::CableRecord::srcOutput` (Patch.h:72-83) is a bare `int`, described
in its own comment as "unused (always 0) for plain image cable records —
only Note Router has more than one note output, and only a node like
VideoSourceNode ... has more than one audio output." There is no pin *name*
or *type tag* anywhere in the saved format — a cable record is purely
`(dstIndex, dstSlot, srcIndex, srcOutput)` integers.

**This is the sharpest risk in the whole idea.** If a Field kernel's output
set is derived from its source text, then editing the kernel's `code`
param — an ordinary string param that already round-trips through
`VisitParams`/`ParamVisitor` (see `FieldElementNode::VisitParams`,
FieldElementNode.h:31-37) — can change `OutputCount()` and what each index
*means* between one save and the next load, or between undo states. A
saved patch's `srcOutput 1` might have meant "geometry" under one version
of the kernel text and "audio" under a later edit of the same node's code,
silently reconnecting a cable to the wrong domain with no error. Contrast
with GeometryTableNode, where growing `rows` is guaranteed append-only by
construction (aggregates are always 0-3, rows always 4+); a
kernel-declared-pins design has no such guarantee unless it invents one
(e.g. requiring outputs be declared in a fixed textual order and diffing
old-vs-new declaration lists on Apply() to decide what happened to
existing cables — unspecified today, needs a decision, see Open Questions).

### What the IR/grammar would need to add

Current `Domain` enum (`src/core/field/FieldIR.h:16-23`): `Graph, Frame,
Element, Pixel, Sample`. Current per-node-type IR programs
(`ElementIRProgram`, `PixelIRProgram`, `GraphIRProgram`, FieldIR.h:181-199)
each have exactly one implicit output channel per domain (element/pixel
kernels write to reserved attribute name `out`, checked and reserved at
parse time — `FieldParse.cpp:555` explicitly reserves `in`, `out`, `sr` as
sample-domain attribute names, and rejects `state` cells that shadow them).
There is no AST/IR concept today of "this kernel declares N outputs, each
with its own name/domain/type." The closest existing shape to imitate is
`AstDeclAttrib` (`FieldParse.cpp:592-621`, `attrib <type> <name> = <expr>`)
— a plausible *syntax* precedent would be an analogous `output <domain>
<type> <name> = <expr>` declaration that both (a) lowers to a new
`IRStmtKind` (sibling to `DeclAttrib`/`DeclState`) carrying a name/type/
domain triple, and (b) is collected into a new `declaredOutputs` vector on
whichever *IRProgram struct applies, the same way `declaredParams` and
`declaredAttribs`/`declaredStates` are already collected today
(`ElementIRProgram`, FieldIR.h:186-188). The domain-inference fixpoint
(mentioned in the `field-domains`/`field-compiler` skills) would need
teaching about join/compatibility rules across differently-domained
*declared outputs* on the same kernel, which is new territory — today
domain inference reasons about one expression tree per node, not about a
node that fans out into several independently-typed result channels.

This is compiler work, not just node-wiring work — flag that explicitly to
whoever picks this up. Load the `field-compiler` and `field-domains` skills
before touching FieldIR.h/FieldParse.cpp for real.

### Relationship to `emit()` (FieldGraphNode) — genuinely distinct, should not share a mechanism

`IRStmtKind::Emit` (`FieldIR.h:78-82,143-150`) lets a **graph**-domain
kernel spawn *other nodes* on the canvas (`emit("<nodeType>", ...)`, see
`FieldGraphNode.cpp`) and wire them with `Connect`/`SetParam`/`Place`
intrinsics. That is node-graph topology at edit time, interpreted once by
`FieldGraphKernel.cpp`, and it produces new `INode` instances entirely
separate from the graph-domain node itself. What the user is asking for
here is the *opposite direction*: a single kernel growing new pins **on
itself**, discovered by drawing more of that same node's own boundary, not
by creating new nodes. These do not obviously collide — `emit` operates in
Graph domain at interpret time and produces nodes; dynamic pins would
operate in Element/Pixel/Sample domain at compile time and produce typed
output values — but a future implementer should make the distinction
explicit in writing (e.g. in a design doc section) rather than
accidentally reusing `IRStmtKind::Emit`/`emitTypeName` machinery for
something that isn't node-spawning. There is a shallow conceptual link
worth naming: both are "the kernel describes structure beyond a single
value," so if both existed together, a future graph kernel might want to
emit a node and also wire multiple typed pins on it — that composition is
out of scope for a first cut and should be called out as such, not solved
here.

## Other risks a future implementer needs up front

- **The four main.cpp image-input chains** (`cable-logic-sweep` skill)
  are entirely hand-maintained per-C++-type dynamic_cast chains. If a
  dynamically-pinned Field node can grow an *image*-typed pin (as opposed
  to geometry/audio, which are already generically dispatched via
  `GeometryInputSlot`/`AudioInputSlot`), all four chains need a
  Field-node-aware branch, and `check.py` in that skill should be re-run
  after any change here.
- **Undo/redo**: changing a node's `code` param already goes through the
  normal undo stack (it's an ordinary string param). But if `Apply()`
  changing `OutputCount()` also needs to drop or remap existing cables
  (because a pin disappeared or changed domain), that cable mutation has
  to be captured in the same undo step as the code edit, or undo will
  restore the old code text while leaving stale/dangling cable state from
  the newer pin layout. No existing Field node does this today — the
  existing `Apply()` methods (`FieldElementNode::Apply`,
  `FieldSampleNode::Apply`) recompile in place without touching pin count.
- **Copy/paste**: duplicating a Field node duplicates its `code` param
  (ordinary param copy) but pin-following logic (how many pins to draw,
  what cables are legal) is presumably re-derived from a freshly-`Apply()`d
  copy, so this is *lower* risk than save/load as long as Apply() is
  always the single source of truth for "what pins exist right now" — but
  that invariant needs to be stated and enforced, not assumed.
- **The interim path is genuinely useful and should be considered
  seriously**: a small, closed set of allowed multi-output *shapes* per
  node type (e.g. FieldSampleNode may declare 0 or 1 extra reduce/rms-style
  scalar output, matching how `AnalyzeNodes.h` has a fixed enum of possible
  outputs) is far cheaper than fully kernel-driven arbitrary pins, and
  would validate the main.cpp/save-load plumbing before taking on the
  domain-inference compiler work. Do not treat this as "the real feature,
  scaled down" — treat it as a separate, shippable milestone.

## Proposed phased approach (for discussion, not a commitment)

**Phase 0 (this brief).** No code.

**Phase 1 — fixed small set of allowed multi-output shapes per node type.**
Each of the four Field node types gets a *hardcoded* (C++-declared, not
kernel-declared) small menu of alternate output shapes it may have — e.g.
FieldElementNode may optionally also declare one Frame-domain scalar
output (a "publish a single number out of the per-element loop" escape
hatch, conceptually close to `reduce`). This proves out: `OutputCount()`
returning something other than a hardcoded literal for a Field node type;
`Patch::CableRecord::srcOutput` addressing more than one Field output;
undo/redo and copy/paste correctness for a node whose pin count can
change. No IR/domain-inference changes required — the menu is finite and
known at C++ compile time, so backend/IR support is written once per menu
entry, by hand, the same way FieldSampleNode's single audio output is
written today.

**Phase 2 — kernel-declared dynamic pins.** The kernel's own source text
declares its output set (syntax TBD, see Open Questions), the compiler
collects declared outputs into the IR program struct, domain inference
extends to multiple declared outputs, `OutputCount()`/`OutputLabel()`
become genuinely driven by the last successfully compiled program (not a
menu), and the save-format stability problem is solved for real (name-
keyed cable addressing, or an explicit append-only discipline enforced by
the compiler, or a migration/remap step on load — a real design decision,
not a footnote). This is the phase that matches the user's actual ask and
should not be scoped down without saying so out loud.

## Open questions requiring a human decision before implementation

1. **Declaration syntax.** Is it a new keyword (`output <domain> <type>
   <name> = <expr>`, mirroring `attrib`) or is domain/type inferred purely
   from what the expression touches (no new keyword, more implicit, harder
   to make robust against ambiguity)?
2. **Inputs too, or outputs only?** The user's example was about outputs
   (geometry + audio). Should input pins ever become kernel-declared too,
   or is that a separate, later question? (Recommend: outputs only for a
   first cut — inputs already have more consumers depending on their
   identity, e.g. modulation bindings target `dstParam` indices that are a
   different addressing space, and the field-integration skill's
   `ParamRef` story assumes stable input shape.)
3. **Which of the four Field node types get this first?** FieldSampleNode
   (single audio output today) is probably the cheapest to extend
   (IAudioSource already has multi-output support built in via
   `IsAudioOutputIndex`/`AudioOutputSlotForPin`). FieldGraphNode is the
   node type where this idea is most likely to be confused with `emit()`
   (see above) and may be best done last, or explicitly excluded from
   Phase 1/2 and revisited only once the emit/dynamic-pins distinction has
   shipped and proven itself non-confusing to users.
4. **Save-format migration.** Does an existing saved patch referencing
   `srcOutput N` on a Field node need any special handling the day this
   ships (given N=0 is today's only legal value for every Field node), or
   is "old patches only ever used output 0, so they're trivially valid
   under any new scheme that keeps 0 stable" sufficient? (Likely yes, but
   should be verified against `ROUNDTRIPTEST`, see `cable-logic-sweep`.)
5. **What happens to an existing cable when a kernel edit removes or
   retypes the pin it's attached to?** Silently drop the cable (with a
   patch-load-style warning), refuse the `Apply()` (keep last working
   program, matching FormulaNode's existing keep-last-working-program
   convention per the `field-compiler` skill), or something else? This is
   the single most consequential open question in the whole brief.

## Sizing

This is a multi-week, two-layer effort (compiler/IR work in
`src/core/field/`, plus node/UI/save-load wiring in `src/nodes/` and
`src/main.cpp`), not a small feature. Phase 1 alone (fixed-menu multi-
output) is a reasonably scoped single build step, comparable in size to
one of the existing numbered Field build steps (see `git log` for
`feature/field-step-NN` branches). Phase 2 (true kernel-declared dynamic
pins with sound save/load semantics) is the larger, more open-ended half
and should not be estimated confidently until Phase 1 has shipped and the
open questions above have real answers.

---
name: data-accuracy-sweep
description: Sweep Infinite for data corruption along a patch chain on both macOS and Windows - values silently dropped by copy/paste or save/load, pass-through nodes that alter what passes through, stale caches served as fresh, TextureRevision lying about a change, cook memoization breaking under fan-out, bypass substituting its own output, and modulation writing values outside a destination's units. Use when a value is right at one end of a chain and wrong at the other, when a param resets after save/load or paste, when an image is stale or a mesh re-uploads every frame, when adding a param to any node, after changing VisitParams, Patch serialization, TextureRevision, cook caching or the Null/pass-through nodes, or before a release as a data-integrity gate.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/data-accuracy-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means every asserted
fixture passed and the cook-memoization gate is clean.

## The question this sweep answers

"Is the data pipeline correct, and not being messed up somewhere in the middle
of the chain?" splits into five checkable questions, each with its own owner:

| # | question | owner |
| --- | --- | --- |
| 1 | does a node's own state survive being copied and saved? | `ROUNDTRIPTEST`, `PATCHTEST`, `AUTOSAVETEST` |
| 2 | does a pass-through node pass data through unchanged? | `UTILTEST`, `BYPASSTEST` |
| 3 | is a cache ever served when the thing changed? | `REVISIONSWEEPTEST`, `CACHETEST` |
| 4 | is a node cooked once per frame, not once per reader? | `check.py` |
| 5 | does modulation write values in the destination's own units? | `MODBOUNDSTEST` |

## 1. Copy/paste and save/load

`ROUNDTRIPTEST` is the widest data-integrity fixture in the app. It walks
*every* node type that declares params, mutates each value through a
`ParamVisitor` (floats +1.x, ints +n, bools flipped, text suffixed, colours
nudged), then checks the values survive **both** restore paths: `CopyParams`
(copy/paste) and `Patch::LoadParams` (patch load). It reports per-type and
names the offenders.

This is the fixture that caught `FormulaNode`, `TextNode`, `NoiseNode` and
about eighteen others silently inheriting the base `INode::VisitParams` no-op,
so save/load and copy/paste dropped **every** value on them. That failure mode
is completely silent at runtime, which is why it survived so long - and it is
exactly what a new node gets wrong.

`PATCHTEST` and `AUTOSAVETEST` are the graph-level versions, and they are **two
separate serializers**: the binary patch and the text (autosave) patch. One
passing says nothing about the other, which is why both are in the list.
`PATCHTEST` checks params, image wiring, geometry links and modulation links
after save -> `NewPatch` -> load; `AUTOSAVETEST` additionally covers palette
bindings, expressions and globals.

## 2. Pass-through nodes

A Null node must forward its input *identically*. `UTILTEST` asserts the
forwarded mesh has the **same stamp**, the forwarded texture the **same id and
size**, and - the part that catches the interesting bug - that a steady frame
does **zero uploads** through the Null (`PASS-THROUGH CACHE OK`, else
`SUSPECT - re-uploading through Null`). A Null that copies instead of
forwarding still looks correct on screen; it just quietly doubles the cost and
breaks every downstream revision check.

`BYPASSTEST` is the same property for the bypass path: a bypassed node hands
its input through untouched rather than substituting its own output.

## 3. Caches and revisions

`TextureRevision()` / `MeshRevision()` are a node's promise about whether its
output changed. Downstream nodes skip work based on that promise, so a node
that lies produces stale images that look like a caching bug and are actually a
truthfulness bug.

`REVISIONSWEEPTEST` cooks probe nodes twice with no input change and asserts the
revision is *identical* across the two cooks. The case that originally caught
the bug is `Displacement` with a real map - a node whose revision moved every
frame because it was derived from something that itself churned.

Note the direction: `REVISIONSWEEPTEST` fails when a revision changes and should
not have. `RENDER3DCACHESWEEPTEST` (in `render-pipeline-sweep`) fails when a
cache is *kept* and should not have been. They pull in opposite directions and
both must pass; fixing one by weakening the other is the trap.

`CACHETEST` is the observational companion - `work=` and `idleStreak=` per frame
over a populated canvas. A patch that never goes idle is re-cooking data that
did not change.

## 4. Cook memoization (the static gate)

`CookIfNeeded(frameId)` is called once per **edge**, not once per node. Without
a frame-id memo, an accumulating node (Feedback, Trails, Reaction Diffusion,
Resynth) advances its state once per downstream cable - so the data depends on
how many things read it. The driver runs
`.claude/skills/compositing-pipeline-sweep/check.py`, which gates that plus the
companion rule that a `Pull()` inside a cook must be handed that cook's own
`frameId`.

## 5. Modulation units

`MODBOUNDSTEST` checks that a modulation source's lo/hi are applied **in the
destination's own units**, not as a raw 0..1 written into a parameter whose
range is something else. See `modulation-sweep` for the binding model itself.

## Windows parity

- The serializers are portable C++ but the *files* are not necessarily: line
  endings and locale-dependent float formatting in the text patch are the
  realistic divergence. Run `AUTOSAVETEST` on both, and if you have a patch
  file written on one OS, load it on the other by hand - nothing automated
  crosses that boundary.
- `ROUNDTRIPTEST` walks the node registry, so if a node is registered on one OS
  only, the two runs cover different sets. Compare the "N types tested" line
  across the two runs, not just the verdicts.
- Revision/cache logic is portable; a divergence there is a real bug.

## Reading a failure

- `copy/paste dropped values: <Node>` / `save/load dropped values: <Node>` -
  that node's `VisitParams` does not visit everything it stores (or does not
  exist). Add the missing `v.Float`/`v.Int`/`v.Bool`/`v.Text`/`v.Color` lines.
  A node failing *both* paths almost always has no `VisitParams` override at
  all.
- `SUSPECT - re-uploading through Null` - a pass-through node is copying, not
  forwarding.
- `REVISIONSWEEPTEST` failure names the probe class; the revision moved between
  two identical cooks.
- `check.py` problems name the file, line and class - a new node missing its
  frame memo.

## What this sweep does not cover

- **No numerical accuracy assertions.** Nothing checks that a value is
  *correct*, only that it is *unchanged* by copy, save, pass-through and cache.
  A node that computes the wrong number consistently passes everything here.
- **No cross-version patch compatibility.** Loading a patch written by an older
  build is untested.
- **No cross-OS patch file check**, as above.
- **Geometry attribute integrity along a chain** (UVs, normals, custom
  attributes surviving several ops) is only covered incidentally by the
  geometry fixtures in `render-pipeline-sweep`.

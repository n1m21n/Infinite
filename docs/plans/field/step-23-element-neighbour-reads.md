# Field build step 23 — element neighbour reads (OPEN-B)

**Status:** built and verified on `feature/field-step-23-element-neighbour-reads`.

Answers `OPEN-B` from `algorithms.md` §4 with the spelling decided in
`language-decisions-and-presets.md` §1. Step 22 made evolving **textures**
writable; this makes evolving **meshes** writable, which was the other half of
the same ask.

## What the language gained

| | |
|---|---|
| neighbour read | `X.at(k)` where `X` is element-domain: `P`, `N`, `uv`, `Cd`, a declared `attrib`, or an element `state` cell |
| what it reads | the cook's **input** buffer — the incoming mesh for an attribute, the previous cook's value for a state cell. Never a value written earlier in the same loop |
| out of range | clamped to `[0, count-1]`, so element 0 asking for `i - 1` sees itself and an open chain behaves like an open chain |
| refused | `.at()` on a param, a frame value or a graph constant, with a message saying it holds one value for the whole mesh, not one per element; and `.at()` outside an element kernel, which points a pixel kernel at `A(uv + d)` instead |
| `age` | now reserved in the element domain too — cooks since this node's state bank was cleared |

The parser needed no change: `P.at(i - 1)` already parsed as a dotted call, the
same shape as `reduce.max(x)`.

### Why the input buffer

Because element `j`'s result must never depend on whether element `j-1` has
already run. That is the property that keeps the loop vectorizable and leaves a
GPU compute backend possible with no thread barriers. The cost is one cook
(~16 ms) of staleness, so a stiff constraint reads slightly compliant —
sub-step it by chaining nodes, not by reaching for the live lane.

Implementation: before the element loop, the VM copies exactly the bases the
kernel names into a snapshot it reuses across cooks. A kernel with no `.at()`
copies nothing.

### Why the simulations keep their positions in `state`

The element store is refilled from the incoming mesh on every cook, so `P +=`
on its own accumulates nothing. Both shipped presets seed a `state` cell from
`P` on the first cook — which is what `age` is for — evolve the cell, and write
`P` back at the end.

## A pre-existing bug this exposed

**Five cross-lane builtins were missing from the element interpreter.**
`length`, `normalize`, `distance`, `dot` and `cross` fell straight through the
lane-wise `if`/`else` chain in `EvalElementBuiltin` and left `0.0` behind. So
`length(P)` read **zero for every vertex**, which quietly made the shipping
"Radial Ripple" and "Spherical Bulge" presets uniform instead of radial. The
comment above that function already stated the invariant — every name
`ValidateFunction` accepts must be handled here — and it was not being kept.
`fract`, `fmod` and `atan2` were missing from the lane loop for the same
reason. All eight are now implemented; a zero-length `normalize` returns zero
rather than NaN, matching the domain's existing "clamp, don't poison the mesh"
rule for `sqrt` and `log`.

## Files

| File | Change |
|---|---|
| `src/core/field/FieldIR.h` | `IRNode::isNeighbourRead` |
| `src/core/field/FieldIR.cpp` | a call whose callee ends in `.at` lowers to a neighbour read of the base; four refusals; `age` seeded as a frame-domain reserved name in the element scope |
| `src/core/field/FieldParse.cpp` | `age` reserved for both domains, with a message that says so |
| `src/core/field/FieldVM.h` | `ExecutionEnv::age` |
| `src/core/field/ElementBackend.h` | `OpLoadNeighbour`; `ElementProgram::neighbourBases`; the VM's snapshot |
| `src/core/field/ElementBackend.cpp` | emission, the per-cook snapshot, the clamped fetch, `age` in `LoadEnvScalar`, and the five cross-lane builtins |
| `src/nodes/FieldElementNode.{h,cpp}` | `mStateAge`; two new presets |
| `src/main.cpp` | `INFINITE_FIELDELEMENTTEST` sections 11-16 |

## Evidence

`INFINITE_FIELDELEMENTTEST` — all 16 sections pass. The new ones:

| # | Asserts |
|---|---|
| 11 | on a ramp mesh, `P.at(i - 1).x` is exactly one step below `P.x` — and equal at element 0, where the clamp bites. Fails if the index is ever dropped |
| 12 | a kernel that overwrites `P.x` and then reads its neighbour still sees the **input** ramp, not the 99 it just wrote. This is the order-independence property |
| 13 | a per-element cook counter minus its neighbour's is exactly 1 — the state snapshot is one cook behind, so it is taken before the loop, not after |
| 14 | `.at()` on a param and on an undeclared name are both refused, with messages that explain rather than just fail |
| 15 | `length(3,4,0) == 5`, `dot((1,2,3),(4,5,6)) == 32`, `normalize((0,3,0)).y == 1` — the regression guard for the bug above |
| 16 | `age` fires exactly once (0.25 after twelve cooks, not 3.0), and both new presets move and stay finite over 90 cooks |

`/run-infinite-hygiene`: 68 passed, 2 known xfail, 1 failure (`GROUPTEST`) that
reproduces identically on clean `main`. `INFINITE_FIELDPIXELTEST`: all 27
assertions still pass.

## What is still open

`OPEN-A` (ring buffers / delay lines) and `OPEN-D` (second sample input, note
events). Both have decided spellings in `language-decisions-and-presets.md` §1
and neither is built. Beyond the OPENs, the element domain still cannot change
`count`, so true differential growth — inserting points where a curve stretches
— remains out of reach at any spelling.

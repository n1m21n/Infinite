---
name: invariant-interaction-audit
description: Before writing (or right after writing) any code that establishes a guarantee — "note stays in scale," "value stays normalized," "budget stays under N," "state stays consistent" — check every OTHER control, branch, or downstream stage in the same node/function/pipeline that runs after your guarantee is established, because one of them can silently undo it. Then check whether the exact same undo-shape was copy-pasted into sibling nodes/functions elsewhere in the codebase. Use before shipping any fix or feature that adds an invariant, right after writing it as a self-check, and whenever a "we already fixed this" bug comes back — it usually means a sibling control or a sibling call site undid the fix, not that the fix was wrong.
---

Repo root is `/Users/namansoni/infinte`. Paths below are relative to it.

## The failure this exists to catch

We shipped scale quantization on the Random Note Generator (a real feature,
with its own UI toggle). It worked. Then a user reported an out-of-key note
(C#4 while locked to C major pentatonic) and it turned out the quantization
was correct — `MusicTime::SnapToScale` snapped the note into key every
time. The bug was one line later: the snapped note was then clamped into the
node's pre-existing `lo`/`hi` range knobs with plain `std::clamp`, and the
range boundary itself was never guaranteed to be in-scale. A knob that had
nothing to do with the feature we'd just built quietly destroyed it.

The mistake was not writing `SnapToScale` wrong. The mistake was writing it
without asking "what else in this node touches the value after this point?"
Two other nodes (`AudioBouncingBallsNode`, `CVToPitchNode`) had the identical
shape, because the clamp-after-snap idiom had been copy-pasted, not
reasoned about, at each site.

This applies past node UI too. Anywhere in the backend — a validated request
object that a caching layer re-derives from raw fields, a normalized vector
that a serializer re-scales, a deduplicated list that a downstream merge
step re-duplicates — the same shape recurs: **an invariant is established
correctly, and something written earlier (or copy-pasted later) that runs
after it is unaware the invariant exists.**

## When to run this

- Before finalizing any fix or feature that adds a guarantee to a value,
  state, or output — run Pass A before you write the fix, to find where the
  guarantee will actually need to hold, and run it again after, as a
  self-check that nothing downstream still violates it.
- Whenever a bug "we already fixed" resurfaces. Check whether a sibling
  control/branch/call site undid the fix before assuming the fix itself was
  wrong or incomplete.
- As a companion to `bug-blast-radius` (which triages a reported symptom)
  and `node-param-audit` (which inventories controls for modulation
  reachability) — this skill is about **interaction between controls/stages
  on the same value**, not about who calls a function or what's
  modulatable.

## Pass A — same-unit interaction sweep

Identify the exact point in the code where the invariant becomes true (the
"invariant point" — e.g. the `SnapToScale` call, a `Validate()` return, a
`Normalize()` step). Then, for the *same node / same function / same
request-handling unit*, list every other piece of logic that:

1. **Runs after the invariant point on the same value** — every subsequent
   line, branch, or param read that touches the same variable before it
   reaches its destination (output, return, serialization, storage).
2. **Reads a sibling control/field that was never routed through the
   invariant** — in a node, this means every other knob/dropdown/checkbox
   in the same `Draw*Body`/`Process*` pair; in a backend function, every
   other parameter, config value, or cached field the function also
   consults.

For each one, ask explicitly: *"Can this transform, override, or bypass the
value in a way that isn't aware the invariant was just established?"*
Write the answer down per item — don't wave a hand at "should be fine."
Concretely useful sub-questions:

- Does anything **clamp/round/quantize/normalize again** after the
  invariant point, using a *different* rule than the one that just ran? (The
  scale-then-range-clamp bug is this exact shape.)
- Does anything **read a cached/pre-invariant copy** of the value instead of
  the post-invariant one (stale field, snapshot taken before the fix,
  a value captured by reference earlier in the function)?
- Does a **conditional/early-return path** skip the invariant point
  entirely for some inputs (a bypass flag, a fast path, an edge case at a
  boundary)?
- If the invariant is a *range* or *set membership* guarantee, is there a
  **boundary value** (a min/max knob, a config default, a hardcoded
  fallback) that is only valid numerically and was never checked against
  the new rule?

## Pass B — cross-codebase pattern sweep

Once you know the *shape* of the interaction (not just this one instance),
grep for the same shape elsewhere — sibling nodes/functions doing the same
job are the most likely place the identical bug was copy-pasted:

```bash
grep -rn "<InvariantFunctionName>" src/
```

For every call site found, re-run the same question from Pass A: what runs
immediately after this call, on the same value, in this function? A hit
that shares the *idiom* (e.g. "snap, then clamp with a different rule") is
a genuine duplicate of the bug, even if the code isn't textually identical
(one node clamped the note, another clamped an index derived from the
note — same effect, different disguise). A hit that discards or rejects
instead of clamping/overriding is clean; say so and cite why.

## Output

State, per item checked in both passes: what it is (file:line), whether it
can violate the invariant, and if so, whether it's a genuine finding or a
false alarm (with the one-line reason). End with a verdict: is the
invariant actually held everywhere it's established, or does it need a
follow-up fix (and where). This is an audit, not automatically a patch —
if the user wants findings turned into a fix, apply them directly when the
scope is small (as here), or hand them to `write-fix-brief` when the fix
needs its own session.

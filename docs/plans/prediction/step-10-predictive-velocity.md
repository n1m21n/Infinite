# Prediction step 10: Predictive Velocity

Status: **design only, nothing built.** Written 2026-09-22.

Same relationship to `VelocityCurveNode` as Predictive Quantize (step 9) has to `QuantizerNode`:
today's velocity shaping is one fixed exponent curve (`VelocityCurveNode::curve`,
[`NoteNodes.h:364`](../../../src/nodes/NoteNodes.h)) the user dials in and it applies uniformly
forever. There's no version that learns *your* actual dynamics and reshapes toward them.

One line: **it learns the distribution of velocities you actually play, and reshapes new note
velocities toward that learned distribution instead of a hand-picked curve exponent.** One Learn
button, one Mix param. This is the simplest of the four — reuses more existing machinery than any
of the others and needs no new theory.

Line numbers are from commit `1ccdbe9`; re-grep the symbol if one has drifted.

## 1. What already exists

| Piece | Exists? | Where |
|---|---|---|
| Fixed velocity remap, audio thread | yes | `VelocityCurveNode` / `AudioVelocityCurveNode` — `outVel = 127 * (inVel/127)^curve` |
| Velocity already binned and learned online, as a byproduct of another node | yes | `NoteModel::kVel` viewpoint — `VelBin(vel127)` buckets into `kVelBins` (8) bins, `Tables::velMean[kVelBins]` already holds the learned mean velocity per bin, built by `NoteModel::Build` from real captured events |
| Learn button + worker build pattern | yes | `PredictiveNotesNode` |

Same finding as step 9: **the representation already exists** as part of `NoteModel`'s multi-
viewpoint model. This node needs the `kVel` marginal (or, more simply, its own lighter 8-bin
online histogram — velocity is a single scalar per event, cheap enough that reusing
`NoteModel::Tables` wholesale, which also carries pitch/spacing/duration this node doesn't need,
may not be worth the coupling; **decide during implementation** whether to depend on
`NoteModel::VelBin`/`kVelBins` directly for consistency with Predictive Notes' binning, or keep a
fully standalone histogram — leaning toward depending on `VelBin`/`kVelBins` for consistency, since
divergent binning between two nodes that both claim to model "velocity" would be a real
`invariant-interaction-audit`-style trap: two hand-kept definitions of the same thing drifting apart).

## 2. What it actually learns and does

Not "the average velocity" — a single number would make every note the same loudness, the opposite
of dynamics. What it learns is a **mapping**: for a given input velocity, what does this player's
*own* output distribution around that input level actually look like. Concretely:

1. While Learn is on, capture `(inVel, outVel)` — but there's no independent "corrected" velocity to
   learn from the way Predictive Coloring has a target histogram; the only signal is the played
   velocities themselves. So what's actually being learned is **the shape of the player's dynamic
   range**: the 8-bin histogram of played velocities, plus, within each bin, how much variance
   there is (a player who plays "loud" anywhere from 100–127 is different from one who always plays
   exactly 118).
2. Fit a monotonic remap curve (the same shape `VelocityCurveNode` already applies, but instead of
   one global exponent, a piecewise-monotonic curve through the 8 learned bin means) that maps the
   *nominal* MIDI velocity range (0–127, or whatever the note source actually produces) onto the
   player's own learned range: if the player never plays below 40 or above 110 in practice, the
   curve compresses the nominal range into that learned band — the "loudest" nominal note plays at
   the player's own real loudest, not at a theoretical 127.
3. This is the same category of idea as Predictive Coloring's self-normalize mode (§3.1 in step 8):
   fit the live signal's own range and re-express new events against it, rather than against a
   fixed theoretical range nobody's playing actually hits.

## 3. The node

**Params (`VisitParams`):**
```
v.Bool("learning", mLearning);
v.Float("mix", mix);                // 0 = untouched velocity, 1 = fully remapped
v.Text("velCurve", mEncodedCurve);  // learned per-bin remap, saved so a patch plays without re-learning
```

Two objects per `new-audio-node`: `PredictiveVelocityNode : INode, INoteSource` (Learn state,
histogram fit) / `AudioPredictiveVelocityNode : AudioNode` (applies the learned curve per note-on,
blended by `mix` — `outVel = lerp(inVel, curve(inVel), mix)`, same blend shape as step 9's onset
correction and step 8's color mix).

Per-patch persistence (`PredictiveNotesNode`'s shape), same reasoning as step 9 §3.4: a player's
dynamic range on one part is not obviously the same profile that should apply to an unrelated part
in a different patch. Revisit only if this turns out to be wrong in practice.

## 4. Traps

| Trap | Why |
|---|---|
| Learning "loudness" as one scalar | Collapses dynamics to a constant, which is the opposite of what a velocity model should do — must learn the *shape* (per-bin), not the mean. |
| Divergent velocity binning from `NoteModel` | See §1 — two independently-tuned 8-bin schemes for "the same thing" is a sibling-drift bug waiting to happen; prefer sharing `VelBin`/`kVelBins`. |
| Fitting from too little data | 8 bins need real coverage across the dynamic range to fit meaningfully; with only a few captured notes, most bins are empty. Gate the curve fit behind a minimum captured-note count (same spirit as Predictive Notes' `HeldOutCrossEntropy` gate, though this doesn't need anything as elaborate as a held-out cross-entropy test — a simple "at least N notes per bin, or fall back to identity" is enough). |
| Applying the curve to note-off / non-velocity events | Only note-on velocity is meaningful here; make sure the audio object doesn't touch anything else about the event on its way through. |

## 5. Test

Propose `INFINITE_PREDVELOCITYTEST`.

1. Feed a synthetic stream whose velocities cluster in a known sub-range (e.g. 60–90); after Learn,
   the fitted curve maps nominal 0–127 onto approximately that learned band.
2. `mix = 0`: output velocities bit-identical to input. `mix = 1`: output follows the fitted curve
   exactly.
3. Too-few-notes case: curve fit falls back to identity rather than overfitting to 2–3 samples.
4. `new-audio-node` §7 exit criterion: spawn, wire, save/load, delete mid-playback, no dangling
   binding.

## 6. Exit criterion

`PREDVELOCITYTEST` passes; `audio-node-sweep` clean; `node-ui-pillars` checklist clean (one toggle,
one knob — the simplest node in the whole category); owner has played into it live and confirms the
remap feels like "my own dynamic range," not compression or a flat boost.

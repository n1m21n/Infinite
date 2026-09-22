# Prediction step 9: Predictive Quantize

Status: **design only, nothing built.** Written 2026-09-22.

Today's `QuantizerNode` ([`NoteNodes.h:453`](../../../src/nodes/NoteNodes.h)) only knows two modes:
off (free timing) or locked to one fixed rate the user dials in (`div`, an index into
`MusicTime::QuantizeGridList()`). There is no middle ground — nothing that looks at what you're
actually playing and figures out the grid *for* you, or corrects toward it without hard-locking.
That gap is this node.

One line: **it learns the actual spacing between your incoming notes, and nudges new onsets toward
that learned spacing instead of a grid you had to pick by hand.** One Learn button, one Mix
(quantize strength) knob.

Line numbers are from commit `1ccdbe9`; re-grep the symbol if one has drifted.

## 1. What already exists

| Piece | Exists? | Where |
|---|---|---|
| Fixed-grid quantize, sample-accurate, audio thread | yes | `QuantizerNode` / `AudioQuantizerNode` (`NoteNodes.cpp:1429`) — snaps `onsetAbs` up to the next multiple of `MusicTime::QuantizeGridBeats(div) * samplesPerBeat` |
| A discrete, fine-grained (1/24 beat) onset-spacing representation, already learned online | yes | `NoteModel::kIoi` viewpoint (`audio/NoteModel.h:29`) — `SnapTicks(beats)` snaps to the nearest 1/24-beat tick, and `Tables::vp[kIoi]` already holds a Markov/marginal distribution over those ticks, built from real captured events |
| A "quantize strength" (partial correction, not hard snap) concept anywhere in Infinite | no | `QuantizerNode::div` is binary per onset: either snapped exactly or not touched |
| A Learn button + worker-thread table build pattern | yes | `PredictiveNotesNode` (step 6) |

The key finding: **the learned-spacing representation this node needs already exists**, as a
byproduct of Predictive Notes' `kIoi` viewpoint. This node does not need its own Markov model of
rhythm — it needs the *marginal* distribution of that viewpoint (ignoring the Markov context, just
"which spacings occur and how often") and a way to snap new onsets toward the modes of that
distribution instead of a fixed rate. If Predictive Bassline (step 11) and Predictive Quantize both
end up wanting this, it's worth factoring the marginal-extraction step out of `NoteModel::Tables`
into a small shared helper rather than each node reaching into `vp[kIoi].ctx` directly —
**decide during implementation** whether that shared helper lives in `NoteModel.h` itself (a
`MarginalHistogram(const ViewModel&)` free function) once a second caller exists.

## 2. What "learned grid" means here

Two different things could be meant by "dynamic quantizing," and they lead to different designs.
Picking the second, for the reason below:

- **(a) Tempo/grid detection**: figure out which *standard* rate (1/8, 1/16, triplet, ...) the
  input is already close to, and quantize to that — essentially auto-selecting `div` for the user
  instead of leaving it on a dial. Cheap, but it's still "one fixed rate," just chosen
  automatically — doesn't capture swing or a groove that sits *between* grid lines.
- **(b) Groove/template quantizing**: don't assume a standard rate at all. Learn the actual
  histogram of onset spacings (and, if there's a discernible meter from Transport, each spacing's
  *phase within the bar*), find its modes (peaks), and snap new onsets toward the nearest **learned
  mode**, not the nearest grid line. This is how DAW "groove quantize" / "humanize toward a
  template" features work, generalized to a template the node builds itself instead of one loaded
  from a preset library.

**(b) is the right one.** It's the only version that actually earns the name "predictive" instead
of "automatic" — (a) still quantizes to a fixed, generic rate, just chosen for you; (b) quantizes
to *this specific input's own* rhythm, which is what Drift and Predictive Notes both do for their
respective dimensions (Drift doesn't pick a generic LFO rate, it wanders where *you* tend to leave
the knob).

## 3. The model

### 3.1 Learning a mode set from the spacing histogram

While Learn is on, capture incoming note onsets the same way `PredictiveNotesNode` captures notes
(pass-through + capture, not a separate tap). For each onset, compute `ioiTicks` the same way
`NoteModel::Event` does (spacing from the previous onset, snapped to the nearest 1/24 beat via
`NoteModel::SnapTicks`) and accumulate a histogram over `0..kMaxIoiTicks` (192 bins at 1/24-beat
resolution — fine enough to represent swing, coarse enough that noise doesn't fragment it).

Once enough onsets are captured (tens, not hundreds — this is a much lower-dimensional thing to
learn than Predictive Notes' full multi-viewpoint model), find the histogram's **modes**: local
maxima above some minimum share of the mass, merged if within a few ticks of each other (avoids
splitting one real peak into two adjacent bins). This mode set — typically 1–4 spacings for most
real playing — *is* the learned grid. It is not required to line up with any `MusicTime::RateDivision`
at all; if the owner plays a steady triplet feel with occasional straight eighths, the mode set has
two peaks, neither forced to be "the" grid.

### 3.2 Applying it: strength, not a hard snap

`QuantizerNode`'s `ceil` to a fixed grid is a hard correction: every onset moves to the boundary. A
predictive version should support a *partial* correction the way real groove-quantize sliders do
(0% = untouched, 100% = full snap, in between = pulled toward the nearest mode by that fraction):

```
nearestMode = argmin_m |onsetTicks - m|      // over the learned mode set
correctedTicks = onsetTicks + mix * (nearestMode - onsetTicks)
```

This is why the node's one exposed param should be named the same as the user's stated intent —
**`mix`** — not `div`: it's a blend fraction between the live onset and its nearest learned mode,
not a mode selector. At `mix = 1` this degenerates to a hard snap-to-nearest-mode, which is exactly
`QuantizerNode`'s behavior but against a learned mode set instead of a fixed rate table.

### 3.3 Where this runs: main thread fit, audio thread apply

Same split as `AudioQuantizerNode`: the mode set is small (a handful of floats) and changes rarely
(only while Learn is on, or slowly as the profile updates), so it's fit on the main thread and
pushed to the audio object as a small fixed-size array behind an atomic version/pointer swap — the
same "worker builds, audio thread swaps a pointer, old table freed after the swap is observed"
pattern `PredictiveNotesNode`/`NoteModel::Build` already uses, just for a ~4-entry array instead of
a multi-KB table. The per-onset correction math (§3.2) is cheap enough to run directly in
`ProcessBlock`, same as `AudioQuantizerNode::ProcessBlock` already does its grid-snap inline.

### 3.4 Cross-session or per-patch?

Unlike Predictive Coloring (step 8), there's a natural per-node key here: this node sits on one
specific note chain, and "the rhythm of *this* part" is not obviously something that should bleed
into a different part in a different patch the way "how I like things graded" is one owner-wide
taste. Default to **per-patch** (Predictive Notes' shape: learned mode set saved as a param on this
node instance), not global `MovementStats`-style persistence. Revisit only if, in practice, owners
say they want a rhythm feel to follow them across patches the way Drift's knob habits do.

## 4. The node

**Params (`VisitParams`):**
```
v.Bool("learning", mLearning);
v.Float("mix", mix);              // 0 = free, 1 = full snap to nearest learned mode
v.Text("modeSet", mEncodedModes); // saved learned modes: "tick1:weight1;tick2:weight2;..."
```

Two objects, per `new-audio-node`: `PredictiveQuantizeNode : INode, INoteSource` (main thread —
Learn state, mode-set fitting, meter) and `AudioPredictiveQuantizeNode : AudioNode` (audio thread —
applies §3.2 per onset using the current mode-set array).

## 5. Traps

| Trap | Why |
|---|---|
| Forcing the mode set onto `MusicTime::RateDivision` values | Throws away exactly the thing that makes this "predictive" instead of "auto-select `div`" (§2). Keep modes as raw tick positions. |
| Snapping chords apart | `ioiTicks = 0` means "same onset" (§ NoteModel.h comment) — a chord's notes must never be corrected relative to each other, only the chord's onset as a whole relative to the previous onset. Group same-onset events before applying §3.2, the same way `NoteModel::Event` already treats `ioiTicks == 0` as a first-class case, not noise. |
| Re-fitting the mode set every onset | Same throttling lesson as step 8 §5: refit on a slower cadence (e.g. every N captured onsets, or on Learn-off) rather than after every single note, or the mode set (and therefore the correction target) jitters while playing. |
| Confusing this with `QuantizerNode` in the UI/help tables | Two nodes with adjacent purposes in the same category is exactly the kind of thing `node-help-coverage`'s hand-kept tables need checked in both directions (memory: `project_node_help_coverage`) — make sure right-click help on each clearly says which one is fixed-grid and which is learned. |

## 6. Test

Propose `INFINITE_PREDQUANTIZETEST`.

1. Feed a synthetic steady-eighth-note stream with light jitter (±10 ticks); after Learn, the mode
   set has one dominant peak near the eighth-note tick value.
2. Feed a swung stream (alternating long/short spacing, the classic swing ratio); after Learn, the
   mode set has two peaks in roughly that ratio — proves it isn't collapsing swing to one average
   spacing.
3. `mix = 0`: output onset times are bit-identical to input. `mix = 1`: output onset times land
   exactly on the nearest learned mode, matching `QuantizerNode`'s own snap-exactness expectation.
4. Chord input (multiple notes at `ioiTicks == 0`): correction moves the whole chord together, not
   individual notes apart.
5. `new-audio-node` §7 exit criterion: spawn, wire, save/load, delete mid-playback, no dangling
   note-inbox binding.

## 7. Exit criterion

`PREDQUANTIZETEST` passes; `audio-node-sweep` and `audio-pipeline-sweep` clean; `node-ui-pillars`
checklist clean (one toggle, one knob); owner has played into it live and confirms partial-`mix`
correction feels like "nudged toward my own groove," not like a generic grid snap.

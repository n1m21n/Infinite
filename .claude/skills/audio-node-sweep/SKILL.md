---
name: audio-node-sweep
description: Generic sweeps across every Infinite audio/note node type, checking two invariants at once - that every param declared through VisitParams survives a save/load round trip and reaches the audio thread within one block of its own CookIfNeeded call (AUDIOPARAMSWEEPTEST), and that spawning, wiring, and deleting any audio/note node mid-playback never crashes and leaves no dangling cable (AUDIOTEARDOWNSWEEPTEST). Use when asked to "check the audio params", "sweep the audio nodes", "does deleting an audio node crash", "did I forget to wire this param into the mailbox", or after adding/touching any audio or note node.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`), not
this skill directory.

## Run this first

```bash
.claude/skills/audio-node-sweep/driver.sh
```

Add `--skip-build` to reuse the existing `build/` tree. Runs both sweeps
below in one pass.

## What this catches

Two real bug classes, each generalized into a sweep that covers every
audio/note node type at once instead of a fixture per node someone
remembered to write (docs/plans/audio/README.md §4/§7):

1. **Dropped mailbox push** (`AUDIOPARAMSWEEPTEST`) - a node declares a param
   through `VisitParams` (so it shows up in the UI and survives save/load)
   but its `CookIfNeeded` never actually pushes the new value into the
   audio-thread side (`ParamMailbox::Push`, or a plain atomic store for a
   discrete param) - the audio analogue of the geometry sweep's dropped
   `GetMappingTransform()` bug. The param looks wired in the editor; nothing
   ever changes when you move it.
2. **Dangling cable on delete** (`AUDIOTEARDOWNSWEEPTEST`) - deleting an
   audio/note node mid-playback leaves a downstream node's `AudioCable`/
   `NoteCable` pointing at freed memory, crashing (or silently reading
   garbage into) the next `ProcessBlock`. This is `DELETECRASHTEST` for the
   audio graph - the use-after-free surface P2's generic `DisconnectAllTo`
   exists to remove.

## How it works

`src/main.cpp` defines a shared discovery helper
(`AudioNodeShape`/`ProbeAudioNodeShape`/`DiscoverAudioSweepCandidates`, just
above the `INFINITE_DSPTEST` block) that walks every name in
`NodeFactory::Instance()` - the same registry `RegisterNodes()` populates -
constructs one throwaway instance of each, and probes which of a node's
interfaces it answers: `IAudioSource`, `INoteSource`, `IModulator`,
`AudioNodeForNotePorts()`, and how many contiguous `AudioInputSlot`/
`NoteInputSlot` indices it has (the same generic probe `InputCountFor` uses
for pin counts). **Neither sweep hand-lists node names** - a node added next
month is covered by both without anyone editing this file.

**Param sweep** (`INFINITE_AUDIOPARAMSWEEPTEST`, headless, gated before
`glfwInit()` like `INFINITE_DSPTEST` - but calls `RegisterNodes()` itself
first, since it needs the registry populated and the DSPTEST early-exit
predates the app's normal `RegisterNodes()` call):

- **Check A (every node with `HasAudioNode()`)**: randomizes every param via
  a collector `ParamVisitor`, round-trips it through the *real*
  `Patch::SaveParams`/`Patch::LoadParams` text format, and compares.
- **Check B**: for each param independently, builds a fresh node instance
  (`Rig`), wires a shared 300 Hz excitation tone into every probed
  `AudioInputSlot`, holds a sustained note-on in a `NoteEventQueue` for any
  probed `NoteInputSlot`, warms up ~12 blocks, then alters *one* param
  through the same `VisitParams` mechanism, calls the node's own
  `CookIfNeeded` (the real call site - not a backdoor into `ParamMailbox`),
  and checks the next block for a measurable difference. What "measurable"
  means depends on the node's shape, all read through the public API, never
  by reaching into `ParamMailbox`/smoother internals:
  - `IAudioSource` → peak+RMS of the real rendered output buffer.
  - `IModulator` (Envelope) → `Value01()`.
  - `INoteSource` with a note input pin → event count/pitch/velocity popped
    from `AudioNode::NoteOutbox()`.
  - Nothing synthetic can drive it at all (MIDI Notes: sourced from real/
    injected hardware, no note-input pin) → `[SKIP]`, not a silent pass.
  Several candidate alternate values are tried in turn (see the
  `AlternateFloats`/`AlternateInts` comment in `main.cpp`) before reporting
  `[FAIL]`, since a single fixed guess can collide with a param's own clamp
  or a bool-as-float `!= 0.0f` decode.

**Teardown sweep** (`INFINITE_AUDIOTEARDOWNSWEEPTEST`, runs inside the normal
windowed app loop at `frameId == 4`, needs `INFINITE_EXITAFTER` to end the
run): for every node with `ParticipatesInAudioGraph()` (broader than the
param sweep's filter - this also covers pure terminals like Audio Out, which
own no `AudioNode` of their own), spawns it into the real `gNodes` graph,
wires whatever companion nodes its shape needs (an upstream MIDI Notes if it
has a note-input pin, a downstream Gain→Audio Out if it's an audio source, an
upstream Gain if it's a pure audio terminal, a downstream Envelope if it's a
pure note producer with no note-input pin of its own), calls the real
`RebuildAudioTopology()`, renders a few blocks "mid-playback", deletes the
candidate through the real `RemoveNodeByIndex` (which is what exercises
`DisconnectAllTo`'s generic `AudioInputSlot`/`NoteInputSlot` loop), renders a
few more, and checks any downstream companion's cable got cleared. Reaching
the final printf at all is most of the proof - a dangling `AudioNode*` left
in the topology crashes the very next `AudioEngine::ProcessOffline`, not
later.

**Every `GraphNode*`/`SpawnNode()` result inside the teardown sweep is used
immediately and then discarded, never held across another `SpawnNode` or
`RemoveNodeByIndex` call.** `GraphNode` is stored by value in the `gNodes`
vector, so any push or erase can reallocate/shift it and invalidate every
earlier pointer - this crashed the first version of this sweep on its very
first candidate. All spawning for one candidate happens first (via the
`SpawnIndex` lambda, which returns only the stable `int index`), then every
pointer is re-resolved through `FindNodeByIndex` once spawning is done.

## Adding a new node type to a sweep

Nothing to do. Register the node with `REGISTER_NODE`/`RegisterNode()` (see
`.claude/skills/new-audio-node/SKILL.md` §3) as normal; both sweeps pick it up
on the next run because they enumerate `NodeFactory` themselves.

## Interpreting results

- `[pass]` — the checked invariant held for that node/param.
- `[FAIL]` (param sweep) — either a real dropped mailbox push, **or** a param
  that is gated by another param's current value or state (see Blind spots
  below) — check by hand in the running app before treating it as a
  regression: does moving the param in isolation, from the node's spawn
  defaults, actually do anything audible?
- `[FAIL]` (teardown sweep) — a real use-after-free: a downstream cable
  wasn't cleared, or the process didn't reach the verdict line at all
  (crashed - see the `[CRASH]` handling in `driver.sh`).
- `[SKIP]` — no synthetic input exists to drive this node's params at all
  (currently only MIDI Notes: it reads live/injected hardware MIDI, not a
  note-cable input). Not a clean bill of health, the same way the geometry
  sweep's `[SKIP]` isn't - it means this node's params are untested here, not
  that they're correct.

## Blind spots — found running this against Audio Filter and Dynamics

Audio Filter (`AudioEffectNode`/`AudioFilterKernel`) is a single filter -
`type`/`freq`/`q`/`gain`/`outputGainDb`, no bands, no `selectedBand` UI state.
The one sharp edge testing-one-param-in-isolation-from-defaults surfaces here:
`gain` only affects shelf/peak types, so probing it against the default
type (lowpass) reports a misleading FAIL.

**This class is now fixed rather than merely documented.** `EffectParamDef`
(`src/audio/EffectDefs.h`) carries two fields for exactly this:
`prerequisites` (other params, and the values they must hold, for this one to
have any audible effect at all - `gain`'s prerequisite is `type=peak`) and
`uiOnly` (a param with no DSP meaning by design - reported `pass` with a note
instead of exhausting every alternate value). `AudioParamSweep::
ApplyPrerequisites` (`src/main.cpp`) sets a param's prerequisites on every rig
it builds before probing that param, and `TestOneParam` short-circuits
`uiOnly` params before even trying. Audio Filter is fully green as a result.

**A different, deeper blind spot remains, surfaced by Dynamics, that no
prerequisite can fix.** `knee`, `rmsWindow`, `lookahead`, `stereoLink` and
`autoRelease` are all wired all the way to `DynamicsKernel::ProcessBlock` (not
dropped), but every one of them only changes *how the signal gets to its
result* - transient shape, timing, or per-channel-vs-linked detection - never
the *settled* value a continuous, unchanging, L/R-identical tone converges to
after warmup. The sweep's generic rig is exactly that: one fixed-frequency
tone, duplicated onto every input channel, compared only once both the
"before" and "after" rigs have fully settled. No `EffectParamDef` field can
change what the rig itself drives through a node, so these five read FAIL
structurally, the same way `stereoLink` mathematically cannot matter when L
and R are bit-identical, or `lookahead` cannot matter when a sine's RMS is
phase-invariant. See `EffectDefs.cpp`'s Dynamics block for the per-param
detail.

None of the params in either category are testable generically without either
being handed the node's internal parameter dependencies (which the
prerequisite mechanism now provides in a table-driven way for the gated
case) or a non-stationary/asymmetric excitation signal per param (which would
defeat the point of one generic rig for every node type). **A `[FAIL]` here is
real information - it tells you this param cannot be verified by this sweep -
but it is not automatically a "forgot to wire the mailbox" bug.** Confirm by
hand (pin the gated param's prerequisite, drive an asymmetric L/R signal,
etc.) before treating a new one as a regression. This is the same category of
caveat `geometry-transform-sweep`'s own `[SKIP]` carries: a gap in what this
harness can prove, not a claim that the node is broken.

**Note-outbox nodes (Arpeggiator, Note Echo, Note Modify, ...) had the same
class of structural blind spot, now mostly fixed.** The `kNoteOutbox` read
mode's signature used to come from a single block, one note-on, three scalars
(event count, first event's note, first event's velocity) - blind by
construction to `frameOffset` (so every timing/humanize param was invisible),
to anything after the first block (so echo repeats, arpeggiator steps, and
glide ramps all fired outside the measurement window), and to a curve's fixed
point (velocity 1.0 maps to 1.0 under any shape). `RunNoteWindow`
(`src/main.cpp`) widens this: it runs a 12-block window (matching the warmup
count) and accumulates every popped event's note, velocity, `isNoteOn` *and*
`frameOffset` into a running signature, and `BuildRig`/`TestOneParamWithValue`
hold a 3-note chord at velocity 0.5 for note-outbox candidates instead of a
single note at 1.0, so mode/octaves/order-sensitive params have something to
differentiate. Two structural gaps remain, neither fixable by widening the
measurement window further:

- **Which outbox port an event went to.** A multi-port node (Note Router's
  `probability`, which redistributes events across its output ports) is only
  ever read from port 0 via the base `NoteOutbox()` call - a param whose only
  effect is *which* port an event lands on reads FAIL regardless of window
  size.
- **State gated behind an explicit transport command.** Note Capturer's
  `loop`/`quantizeDiv` only take effect via a `kStartRecord`/`kStopRecord`/
  `kStartPlay` command (see `AudioNoteCapturerNode::ProcessBlock`'s
  `mCommand` switch) that the generic rig never issues - the node stays in
  its idle state through the whole probe, so nothing downstream of that
  switch can ever be reached generically.

## Manually trying combinations these sweeps do not cover

- **Voice-start-gated params on an already-sounding voice.** A wavetable's
  static start phase or an envelope's attack shape only take effect at
  note-on, not to an already-triggered voice. `TestOneParam` retriggers a
  fresh note-on (compared against an equally-retriggered, unaltered control,
  to isolate the retrigger's own effect from the param's) for exactly this
  reason, but only for nodes with a note-input pin at all - a param on a
  purely audio-rate node (Gain, Mixer) that only matters at some other
  lifecycle event this sweep doesn't model would still be missed.
- **Deeper chains.** Both sweeps prove one node in isolation (param sweep) or
  one node plus its immediate companions (teardown sweep) - not multi-hop
  chains, and not whether real device audio (as opposed to `ProcessOffline`)
  behaves the same way.
- **Real xruns.** `AudioEngine::XrunCount()` is timing-gap detection against
  the real device callback cadence; the teardown sweep runs entirely through
  `ProcessOffline` (no device), so its `xruns=` line will read 0 regardless -
  it is printed for the record, not as a load-bearing assertion here.

If you build a patch by hand and a param looks wired but does nothing, or
deleting a node crashes, report which node and what you changed/deleted so a
case can be added to the blind-spots list above rather than assumed covered.

## Gotchas

- Like `run-infinite-hygiene`'s suite, the verdict is a printf line, not an
  exit code from the app itself - `main()` returns 0 even when
  `AUDIOPARAMSWEEPTEST` fails (it returns 1 only for that specific early-exit
  path; the teardown sweep's `main()` always returns 0 like every other
  windowed fixture). `driver.sh` greps for each sweep's own `... OK`/`...
  FAIL` marker rather than trusting the exit code alone.
- The teardown sweep needs `INFINITE_EXITAFTER` set (see any other windowed
  fixture) or the app will sit in its normal event loop after printing the
  verdict.
- This is a different, narrower harness than `run-infinite-hygiene` - it does
  not build/screenshot/eyeball rendering. `run-infinite-hygiene`'s suite runs
  both of these sweeps too, so a plain pre-commit hygiene run already covers
  this; use this skill directly when you want the sweeps in isolation or are
  adding a new audio/note node and want fast iteration (`--skip-build`).

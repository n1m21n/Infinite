# Fix brief: FM (Wavetable / Oscillator) + cutoff mod (Audio Filter)

Three features were added in the current uncommitted working tree: audio-rate FM into
`WavetableNode`, the same into `OscillatorNode` (both share `AudioWavetableNode` in
`src/nodes/WavetableSynthCore.h`), and an audio-rate cutoff-mod input on the Audio Filter
effect (`src/audio/dsp/AudioFilterKernel.h`). The new pins themselves are correct — the
topology walker, cycle rejection, pin discovery and save/load all pick them up generically,
the DSP modulates as it should, and the tree compiles.

But the first FM patch anyone would actually build is silent, for a reason that has nothing
to do with FM. Item 0 is that bug and it is the reason this brief exists. Items 1-2 are
regressions shipped in the same edit. Everything after is the feature being unfinished.

Do not restructure the audio graph, the mailbox, or the two-object split. Item 0 changes
`NoteEventQueue`'s consumer side and the topology builder's note pass; every other fix is
local to the files named.

---

## 0. [P0] One note source feeding two synths: the first consumer eats every event

**Reproduced, with the reported patch.** Note Sequencer fanned out to two Oscillators;
Oscillator 1 feeds Oscillator 2's `fm in`; Oscillator 2 feeds Audio Out. On screen,
Oscillator 1 reports `2 voices` and Oscillator 2 reports `0 voices`. Offline fixture, one
note pushed into the shared outbox:

```
osc1(modulator) voices=1   osc2(carrier->out) voices=0   output peak=0.00000
```

Total silence, deterministically.

The mechanism:

- `AudioNode::NoteOutbox()` (`src/audio/AudioNode.h:46`) returns a pointer to the
  producer's *one* `NoteEventQueue` member.
- `RebuildAudioTopology`'s note pass (`src/main.cpp`, the "Wire each note-consuming node's
  inbox to its producer's outbox" loop) hands that same pointer to **every** consumer of
  that producer.
- `AudioWavetableNode::ProcessBlock` consumes with `mNoteInbox->Pop(evts, 64)`
  (`src/nodes/WavetableSynthCore.h:304`) — destructive. `NoteEventQueue` is documented
  single-producer/**single-consumer**; its head index is the consumer's alone.

So with two consumers on one queue, whichever runs first in topology order drains it and
the second sees an empty inbox forever. This is pre-existing — any patch fanning one note
source into two synths or an Envelope hits it — but FM makes it deterministic and maximally
confusing: the FM cable *forces* the modulator to be ordered before the carrier, so the
silent one is always the node wired to Audio Out.

**Fix.** Make the queue multi-consumer rather than papering over it at the call site:

- Add a cursor API to `NoteEventQueue`: `int RegisterConsumer()` returning a cursor id (cap
  at a small fixed count, no allocation on the audio thread), `Pop(int cursor, NoteEvent*,
  int max)` reading from that cursor's own head, and reclaim space only up to
  `min(all cursors)` so a slow consumer can't have events overwritten under it. Reset all
  cursors when the topology generation changes.
- The topology builder already visits every consumer of every producer in that same loop —
  assign each consumer its cursor id there, and clear the producer's cursor registry at the
  start of each rebuild so ids don't leak across generations.
- Consumers store the cursor next to the inbox pointer: `SetNoteInbox(queue, cursor)`, then
  `mNoteInbox->Pop(mNoteCursor, evts, 64)`. Touches every note consumer (both synths,
  Envelope, the note-processing nodes) but each is a one-line change.
- Keep the overflow policy as documented (drop note-ons, never drop note-offs) — with
  multiple cursors, "full" is now measured against the slowest cursor.

**Verify:** the exact patch above — sequencer → both oscillators, osc1 → osc2 `fm in`,
osc2 → Audio Out — must show non-zero voices on *both* nodes and audible output. Add it to
`AUDIOTEARDOWNSWEEPTEST` or as its own fixture so it can't regress. Also check the
three-consumer case, and a consumer deleted mid-playback while others keep reading.

## 1. [P0 regression] The Wavetable and Oscillator master waveform display is dead

Confirmed by the user: the master-section waveform display is gone on both nodes since the
FM edit. `AudioWavetableNode::ProcessBlock` in `src/nodes/WavetableSynthCore.h` lost its
scope write. The block that used to sit at the end of the per-sample loop —

```cpp
if ((i & 3) == 0)
{
   const float s = (outL + outR) * 0.5f;
   mScopeRing.Write(&s, 1);
}
```

— was deleted, and nothing else in the file writes `mScopeRing` (grep it: only
`ScopeRing()` and the member declaration remain). `WavetableNode::ReadScope` /
`OscillatorNode::ReadScope` drain that ring, and `DrawWavetableScope`
(`src/main.cpp:5284`) and `DrawOscillatorScope` (`src/main.cpp:5338`) draw from it, so both
nodes' master displays render a flat line while the node is audibly playing.

Restore the write at the same place with the same decimation. Note this also made item 0
much harder to diagnose — with a live scope, "osc1 is playing, osc2 is not" would have been
visible at a glance.

## 2. [P0] Unexplained multichannel behaviour change

Same function, the output write changed from

```cpp
for (int ch = 2; ch < buffer.numChannels; ch++)
   buffer.channels[ch][i] = outL;      // was
   buffer.channels[ch][i] = 0.0f;      // now
```

Nothing about FM required this. Decide deliberately which is right (zeroing extra channels
is defensible; silently changing it inside an FM commit is not), and leave a one-line
comment saying why. If you keep the zero, confirm no >2-channel device path relied on the
duplicate.

## 3. [P1] `fmMode` is unreachable — the exponential-FM path is dead code

`fmMode` is declared on both nodes, saved via `VisitParams`, pushed through `PushParams`
into `mFmMode`, and branched on per-sample in `ProcessBlock` (`fmPhaseOffset` vs
`fmExpSemitones`) — but there is no UI control anywhere. `grep -n fmMode src/main.cpp`
returns nothing. Only the `fm depth` knob exists (`src/main.cpp:6575` for Wavetable,
`src/main.cpp:6793` for Oscillator), so mode is permanently 0 and half the DSP is
unreachable except by hand-editing a patch file.

Add the mode selector using the existing `DropdownKnob` mechanism — `AudioKnobRow`'s
`headerRowH` parameter exists precisely for a cell that carries a dropdown above its knob
(see the constructor comment at `src/main.cpp:5060`). Put the dropdown over the `fm depth`
cell in both node bodies, labelled `pm` / `fm` (or `phase` / `expo`). Row cell counts are
already correct (6 for Wavetable, 4 for Oscillator) — do not change them, only the row's
header height.

## 4. [P1] Audio Filter cutoff mod has no depth control

In `AudioFilterKernel::ProcessBlock` the external mod is hardcoded:

```cpp
const float extModOctaves = modActive ? (modSignal[i] * 4.0f) : 0.0f;
```

A full-scale modulator therefore sweeps ±4 octaves, always, with no way to attenuate. The
env-follower knob next to it is a proper `envAmount` param; this should be symmetrical.

Add a `modAmount` param to the Audio Filter `EffectDef` in `src/audio/EffectDefs.cpp`
(bipolar `-1..1`, default `0`, matching `envAmount`'s shape so a negative setting inverts),
give it its own mailbox slot in `AudioFilterKernel::PushParams`, read it with
`mMailbox.SmoothedValue(...)` and scale `extModOctaves` by it. Add the knob to the Audio
Filter body next to `env`.

## 5. [P1] The fast coefficient path is lost whenever a cable is merely connected

```cpp
const bool modActive = (modSignal != nullptr);
...
if (!envActive && !modActive) { /* fast path */ }
```

`modActive` is true for a connected-but-silent modulator, and true for a modulator you have
turned all the way down, so every patch with a cable in that pin pays a per-sample `tanf` /
`powf` / `ConfigureBiquad` forever. Once item 4 lands, gate it the same way `envActive` is
gated: `modActive = (modSignal != nullptr) && std::fabs(modAmount) > 1.0e-4f`. Note that
`modAmount` is smoothed, so read it once per sample as the other slots are read.

## 6. [P1 DSP] Audio-rate cutoff mod is unsafe on the biquad filter types

`AudioFilterDsp::IsSvf` is true only for `lp 12/24/36` and `hp 12/24/36` — those go through
`DspMath::TptSvf`, which is a topology-preserving transform and is well-behaved under
per-sample coefficient changes. Every other type (`bp`, `notch`, `low shelf`,
`high shelf`, `peak`, `all-pass`) is a direct-form `DspMath::Biquad` whose retained state is
coefficient-dependent; recomputing its coefficients every sample from an audio-rate signal
is the classic way to get level jumps, and at high Q and large excursion, blow-up.

Pick one and implement it (in preference order):

- Slew-limit the modulated cutoff feeding the biquad branch — a one-pole on `freqMod` with
  a few-millisecond time constant, applied only when `!IsSvf(type)`. Keeps the feature
  available on every type, kills the instability, costs one multiply-add.
- Or clamp the biquad branch's `totalOctaves` to a much smaller excursion than the SVF
  branch's.

Whichever you choose, say so in a comment at the branch, and sanity-check it: `bp` at
`q = 18`, cutoff 1 kHz, a full-scale sine into `cutoff mod` at `modAmount = 1` must not
produce output above roughly the unmodulated peak level and must not run away.

## 7. [P2] Unrelated clamp change inside the FM edit

The cutoff clamp changed from `std::clamp(..., 20.0f, 20000.0f)` to
`std::clamp(..., 20.0f, (float)mSampleRate * 0.45f)`. That also changes the pre-existing
env-follower behaviour — at a 96 kHz device an env sweep now reaches ~43 kHz where it
previously stopped at 20 kHz. Keep the new bound (it is the more correct one for a
sample-rate-dependent filter) but confirm `ConfigureBiquad` is sane that close to Nyquist
for the shelf/peak types, and comment the change.

## 8. [P2] Mod and FM inputs read the left channel only

Three places take `channels[0]` and ignore the rest:

- `AudioFilterKernel::ProcessBlock` — `sidechain->channels[0]`
- `AudioWavetableNode::ProcessBlock` — `inputs[1]->channels[0]`

A hard-right-panned or stereo-wide modulator therefore modulates weakly or not at all,
which reads as a bug to anyone patching it. Sum to mono instead: average across
`min(numChannels, 2)`.

## 9. [P2] Ranges and headroom

- `fm depth` is `0..5` (`src/main.cpp:6575`, `6793`). In PM mode that is five whole cycles
  of phase deviation — the top two thirds of the knob is undifferentiated noise, and the
  mip level is chosen from the carrier frequency alone (`Wavetable::MipForPhaseInc(inc)` in
  `RenderEngine`) so the PM sidebands alias hard up there. Consider `0..2` and a taper.
- In exponential mode, `fmExpSemitones = extFm * fmDepth * 12.0f` reaches ±60 semitones at
  full depth with no clamp on the resulting `freq` before `powf` — a full-scale modulator
  pushes the carrier far past Nyquist. Clamp the post-FM frequency (Nyquist × 0.45, as the
  filter now does) and reduce the multiplier.

## 10. [P2] `InputLabel` string-compares the effect name every frame

`src/nodes/AudioEffectNode.h:66`:

```cpp
return (mDef.name == "Audio Filter") ? "cutoff mod" : "sidechain";
```

This is a per-frame `std::string` compare on a UI path, and it hardcodes a node name in a
file whose whole point is that effects are table-driven. Add a `const char* sidechainLabel
= "sidechain";` field to `EffectDef` (`src/audio/EffectDefs.h`, next to `hasSidechain`),
set it to `"cutoff mod"` in the Audio Filter def, and return it.

## 11. [P2] No test coverage for either feature

Nothing exercises an FM cable. Add to the existing audio sweeps:

- `AUDIOPARAMSWEEPTEST` — confirm `fmDepth` and `fmMode` on both nodes actually reach the
  audio thread within a block of `CookIfNeeded` (`fmMode` goes to a plain atomic, not the
  mailbox, so it may need a debug accessor next to `DebugMailboxSampleRate`).
- `AUDIOTEARDOWNSWEEPTEST` — spawn Oscillator → Wavetable `fm in` while playing, delete the
  modulator, confirm no crash and no dangling cable. Same for a modulator wired into Audio
  Filter's `cutoff mod`.

---

## Verification before you call this done

1. `cmake --build build -j8` clean.
2. Run the hygiene pass (`.claude/skills/run-infinite-hygiene`), including the two audio
   sweeps.
3. By hand in the app: Oscillator → Wavetable `fm in`, both scopes alive, `pm`/`fm`
   dropdown audibly changes character, `fm depth` at 0 is bit-identical to no cable.
4. By hand: Oscillator → Audio Filter `cutoff mod` with type `lp 24` and with type `bp` at
   high Q — neither blows up, `mod amount` at 0 restores the fast path.
5. Copy `build/Infinite.app` to `~/Desktop/Infinite.app`.

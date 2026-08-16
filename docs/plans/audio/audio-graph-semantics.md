# Audio graph connection semantics

The rules governing which cables may connect, how many, and what the engine
must do to honour them. Companion to `audio-node-ui-system.md` (which covers
*node appearance* and the cable **type** compatibility matrix in its §6); this
document covers cable **arity** and the engine's buffer/evaluation model.

Decided by the user during P3a scoping. P3a-e sessions follow this document;
they do not re-litigate it.

## 1. The two rules

**Rule 1 — Fan-out always requires an explicit node.**
An output pin feeds exactly one destination. To send one signal to several
places you insert a splitter node: **Splitter** (1→4) for audio, **Note
Router** (1→4) for notes. A second cable dragged from an output that already
has one is refused.

*Why:* signal flow stays literally visible on the canvas, and every synth or
effect added in future gets the behaviour for free without its author having
to think about aliasing. The cost is one extra node for a common operation;
that was accepted deliberately.

**Rule 2 — Fan-in depends on the cable type.**

| Cable | Fan-in | Mechanism |
|---|---|---|
| **Audio** | Explicit only | **Mixer** (4–8 in, summed). Every other audio pin takes exactly one cable. |
| **Note** | **Implicit merge at the pin** | Any note pin accepts multiple cables; the engine merges the event streams. |

*Why the asymmetry:* summing audio is a mixing decision — it needs per-channel
gain, pan, mute, solo, which is a UI, which is a node. Merging note streams
has **no parameters** — it is interleaving timestamped events. Forcing a node
in front of it would be ceremony with nothing to configure.

Concretely: Note Sequencer + a keyboard + MIDI In can all drive one Oscillator
by dragging three cables to its note pin. Two Oscillators into one Audio Out
is refused — insert a Mixer.

## 2. Per-node arity table

Covers the plan's 30 nodes (`README.md` §3). "Pins" and "cables per pin" are
different things: Dynamics has two *pins* (input, sidechain) each taking one
cable — that is not fan-in.

| Node | Audio in | Note in | Out | Note |
|---|---|---|---|---|
| **Mixer** | **4–8, summed** | — | 1 | The only summing node in the system |
| **Splitter** | 1 | — | **4** | The only audio fan-out point |
| **Note Router** | — | 1 (merge) | **4** | The only note fan-out point |
| Audio Out | 1 | — | — | Refuse 2nd cable → "use a Mixer" |
| Gain | 1 | — | 1 | |
| Audio Filter, Delay, Reverb, Drive, Pitch Time, Stereo | 1 | — | 1 | Serial effects |
| Dynamics | 2 pins × 1 | — | 1 | in + sidechain; **not** summed |
| Scope, Recorder | 1 | — | — | Terminals (Scope also emits an image — see below) |
| Audio In | — | — | 1 | Device source |
| Oscillator, Wavetable, Sampler, Resonator | — | 1 (merge) | 1 | Note-driven synths |
| Drum Sequencer | — | — | 1 | Self-driving |
| Note Sequencer | — | — | 1 | Generator |
| Arpeggiator, Note Filter, Note Echo | — | 1 (merge) | 1 | Serial note processors |
| Note Display | — | 1 (merge) | — | Terminal |
| Envelope, Shaper, Mod Recorder, Macro | — | 1 (Envelope only) | modulator | Modulator outputs are unrestricted — see §6 |

**Scope** is the documented exception in `audio-node-ui-system.md`: its
spectrum mode emits an image texture, so its *image* output follows the visual
graph's rules, not these.

## 3. Engine consequences — this is the real work

**The current model cannot express any of this.** `AudioEngine::SetTopology`
takes a flat `std::vector<AudioNode*>`, `Process` runs them in order over one
shared `AudioBuffer`, and `AudioNode::ProcessBlock` is documented "in place:
read + overwrite". That works only for a single linear chain. A Splitter
handing the same signal to four consumers, or a Mixer reading four inputs
simultaneously, is impossible against one shared buffer — the second chain
sees whatever the first left behind.

Note that this is not a hypothetical future gap: **audio fan-out is already
reachable in the editor today and silently produces wrong audio.** The
topology DFS explicitly dedupes a node feeding several consumers
(`main.cpp:6300`), but the buffer model then aliases them. Rule 1 makes this
refusable rather than silently broken, which is a correctness fix, not just a
UX one.

Required changes:

- **Per-node output buffers, from a pool preallocated in `PrepareToPlay`.**
  Sizing is trivial (`maxBlockSize × channels × nodeCount` floats — kilobytes),
  and a uniform "every node owns its output buffer" model is far easier to
  reason about than optimising in-place chains and special-casing branch
  points. No allocation on the audio thread, per `AudioNode.h`'s constraints.
- **`AudioNode::ProcessBlock`'s contract changes** from "read and overwrite the
  shared buffer" to "read your input buffer(s), write your output buffer".
  This is a **breaking change to every existing audio node** — `Oscillator`
  (`AudioNodes.cpp:176` currently writes `buffer.channels[ch][i] = outL`) and
  `Gain` both need updating, and `AudioNode.h`'s header comment must be
  rewritten. Do this deliberately and all at once; a half-migrated engine
  where some nodes overwrite a shared buffer and others don't will produce
  bugs that are extremely hard to localise.
- **Topology becomes a DAG, not a list.** `SetTopology` must carry
  connectivity (which node's output feeds which node's input pin), not just an
  ordering. Evaluation order is a topological sort — the existing DFS
  post-order in `RebuildAudioTopology`/`CollectAudioChain` already produces a
  valid order, but the *edges* now have to survive into the audio thread too.
- **Retire/publish discipline is unchanged** — `SetTopology`'s existing
  atomic-exchange + one-generation-retire pattern still applies to the richer
  structure. Buffers owned by a retired topology must not be freed while the
  audio thread might still be inside a block using them.

## 4. Note merge semantics — the subtle part

Merging is not concatenation. The failure mode is a **stuck note**: source A
and source B both send note-on for C4; B sends note-off; if the consumer
matches that off to A's note-on, A's note is released and B's rings forever.

Required: **note events carry their originating source, and note-off matches
on `(source, pitch)`, not pitch alone.** The event struct P3a defines must
therefore include a source identifier from the outset — retrofitting it later
means touching every producer and consumer.

Secondary cases to decide and document when implementing:
- Same source, same pitch, two note-ons without an intervening off (a
  re-trigger). Retrigger the existing voice or steal a new one?
- Voice budget across merged sources: three sources into one synth can exhaust
  the `VoiceAllocator` faster than a single source would. The existing
  oldest-steal policy (`src/audio/AudioVoice.h`) handles it, but say so
  explicitly rather than discovering it as a bug.
- A source being deleted mid-playback while holding notes down. Those voices
  must be released, or they stick forever. This is the note-graph analogue of
  the use-after-free that `DisconnectAllTo` and `AUDIOGRAPHTEST` exist to
  prevent, and it needs equivalent test coverage.

## 5. Cycles

Audio feedback loops remain **rejected**, as today
(`WouldCreateAudioCycle`, `main.cpp:1764`). `README.md` §8 puts graph-level
audio feedback explicitly out of scope; Delay and Reverb own their internal
feedback. Note cycles must be rejected on the same basis — a Note Echo feeding
itself is an unbounded event storm on the audio thread.

## 6. Modulators

Unchanged from `cook-rate-decision.md`: a **visual modulator cannot cable into
an audio param**. The intended path is the published-atomic read described
there, and that mechanism does not exist yet. Modulator *outputs* are
unrestricted in fan-out (they are values read by consumers, not buffers), so
Rule 1 does not apply to them.

`Envelope` is a note-consuming modulator: note pin in, modulator value out,
and per `README.md` §3 it drives visual params too ("the flash the visuals on
a note" node).

## 7. Refusal UX — non-negotiable

Every rule above is a way for a user's drag to be refused. A silent refusal is
what cost real time when audio pins shipped unlabelled, so each must state its
reason:

| Refusal | Message |
|---|---|
| 2nd cable from an output | "One connection per output — use a Splitter" (or Note Router) |
| 2nd cable into Audio Out | "Audio Out takes one input — use a Mixer" |
| 2nd cable into any 1-in audio pin | "This input takes one cable — use a Mixer" |
| Modulator → audio param | "Modulators can't drive audio params yet" |
| Cycle | "This would create a feedback loop" |

Surface these per `audio-node-ui-system.md` §6a's refusal-reason mechanism.

## 8. Sequencing implication for the plan

**Mixer and Splitter move earlier.** As specified in `README.md` they sit in
P3d (Utility), roughly three phases out — but under these rules they are the
*only* way to sum or branch anything. Until they exist, a patch is limited to
one strictly linear chain: no chords across two oscillators, no parallel
effects, no metering a signal without interrupting it.

Recommendation: build **Mixer and Splitter alongside the routing work**, in
the same phase that changes the buffer model, since they are the features that
change proves. The rest of Utility (Audio In, Scope, Recorder, the sample
browser) stays in P3d.

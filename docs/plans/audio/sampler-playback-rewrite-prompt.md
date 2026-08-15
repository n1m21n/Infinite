# Prompt: rewrite the Sampler node's playback/trigger logic

Fix the Sampler node (`src/nodes/SamplerNode.cpp` / `.h`, UI in
`src/main.cpp`) so its three ways of making sound — the global transport
(spacebar), an incoming note cable, and the node's own Play/Stop button —
stop fighting each other. Today they share one voice and one note number, so
they cut each other off and the button label lies. All findings below were
verified against the current code; line numbers are from
`src/nodes/SamplerNode.cpp` unless stated otherwise.

## The model to implement

The node has exactly **two independent sound lanes** that never share a voice:

| Lane | Triggered by | Silenced by |
|---|---|---|
| **Notes** (polyphonic) | note-ons arriving on the note pin | matching note-off, or transport stop |
| **Self** (monophonic, one dedicated voice) | transport start when no note cable is connected (auto/free-run), *or* the user clicking the waveform / the node's ▶ button (audition) | transport stop if the transport owns it; the node's ■ button; end of a one-shot |

Two rules the user can memorise, and which the UI must state:

1. **Spacebar stops every sound this node is making** — free-run and note
   voices both. Nothing keeps droning after the transport stops.
2. **The node's own ▶/■ is an *audition* control, not a transport.** It only
   ever touches the Self lane's dedicated voice, works while the transport is
   stopped (that's the point — auditioning a sample without running the
   patch), and its label reflects only that one voice.

The Self lane's dedicated voice has one owner at a time: `Transport` (auto)
or `User` (audition), whichever triggered last. Transport stop silences it
only when the transport owns it — so a user audition made while stopped isn't
killed by a stray transport edge. Transport start always (re)takes ownership
and retriggers from `start` when no note cable is connected.

## The confirmed bugs this replaces

1. **The audition/free-run voice uses MIDI note 60** (`kFreeRunningNote`,
   line 31) and is triggered through `VoiceAllocator` like any other note
   (lines 201, 210, 331–368). So an incoming note-off for C4 silences the
   audition voice, and pressing the node's Stop —
   `RequestStopFromMainThread` → `mVoices.NoteOff(kFreeRunningNote)` at lines
   118 and 174–175 — silences whatever MIDI voice happens to be holding C4.
   This is the main "it glitches when notes are coming in and I hit play/stop".
2. **`mPreviewVoice` slot reuse can hijack a MIDI voice** (lines 339–354).
   The guard only checks `IsVoiceActive(mPreviewVoice)`, not that the slot
   still belongs to the preview — after the allocator round-robins that slot
   to a real note, the next audition retunes and repositions that note's
   voice mid-flight.
3. **The Play/Stop button label is driven by MIDI activity.**
   `mIsPlaying` is `mLastTriggeredVoice >= 0 && IsVoiceActive(...)` (lines
   305–306), and `mLastTriggeredVoice` is set by *every* trigger including
   note-ons (line 367). So the button flips to a red "Stop" whenever notes
   play, and pressing it fires bug 1. UI at `src/main.cpp:5703-5714`.
4. **Stop while free-running silences the node until the transport toggles.**
   `RequestStopFromMainThread` releases the voice but never clears
   `mFreeRunningStarted` (set true at line 202), and the re-trigger at lines
   197–203 is gated on that flag.
5. **Transport stop only stops the free-run auto-trigger** (lines 184–196).
   Note-driven voices and the audition voice keep sounding — if a note source
   freezes mid-note on transport stop, that note is stuck on forever.
6. **The playhead follows `mLastTriggeredVoice`** (lines 289–290), so with
   polyphony the yellow line jumps between unrelated voices.

## Changes

1. **`src/nodes/SamplerNode.cpp` — give the Self lane its own voice, outside
   `VoiceAllocator`.** Add a small self-contained struct in
   `AudioSamplerNode`: `Envelope mSelfEnv` (`Envelope` in
   `src/audio/AudioVoice.h:13` is standalone — `SetSampleRate` / `SetADSR` /
   `NoteOn` / `NoteOff` / `Process` / `IsActive`), plus `double mSelfPos`,
   `int mSelfDir`, and `enum class SelfOwner { None, Transport, User }
   mSelfOwner`. Wire its sample rate and the same 2/0/1/15 ADSR in
   `PrepareToPlay` (line 71). Render it in the per-sample loop alongside the
   allocator voices, reusing the same rate/range/loop/ping-pong maths — factor
   the per-voice advance+edge-handling out of the loop body (lines 239–291)
   into one helper both lanes call, so loop/reverse/ping-pong behaviour can't
   drift between them. It always plays at rate `NoteToRate(60, pitchSemis)`.
   Delete `mPreviewVoice`, `kFreeRunningNote`'s use as a trigger note, and the
   `isPreview` parameter of `TriggerVoice`; `TriggerVoice` becomes note-only.
2. **Ownership + transport edges.** Keep a `bool mTransportWasPlaying` and act
   on the edge, not the level:
   - rising edge: if no note cable (`mNoteInbox == nullptr`), trigger the Self
     voice from `start` with `mSelfOwner = Transport`.
   - falling edge: release **all** allocator voices (loop `v` over
     `mVoices.NumVoices()` calling `mVoices.EnvelopeAt(v).NoteOff()` — don't
     add a method to `AudioVoice.h`), and release the Self voice only if
     `mSelfOwner == Transport`.
   A pending `mPreviewFrac` (line 208) or a stop request sets
   `mSelfOwner = User` / `None`. Keep the existing "re-audition on new buffer"
   behaviour (lines 145–153) by retriggering the Self voice when
   `mSampleSlot.SwapIn()` returns true *and* the transport is playing with no
   note cable.
3. **`IsPlaying()` must report the Self voice only** — `mSelfEnv.IsActive()`.
   Add a second published flag for "notes are sounding" (any allocator voice
   active) and a published voice count; the UI uses these for the status line
   in change 5. Publish both through the existing `std::atomic` +
   `CookIfNeeded` drain pattern (lines 305–306, 417–420).
4. **Playhead:** report the Self voice's position when it's active, otherwise
   the most recently triggered *note* voice, otherwise park on `start` (line
   300 already handles the park case). Replaces lines 289–290.
5. **`src/main.cpp:5657` `DrawSamplerBody` — make the UI state the model.**
   - Relabel the Play/Stop button pair as audition: `▶ audition` / `■ stop`
     (keep the existing red-while-active styling at 5704–5714) and add an
     `ImGui::SetItemTooltip` saying it previews this node only and doesn't
     start the patch — spacebar does that.
   - Add one right-aligned status word on that row, driven by change 3:
     `notes · N` when a note cable is connected, `auto · running` /
     `auto · press space` when it isn't, and `auditioning` while the Self
     voice is user-owned. No new toggles or params — this is a read-only label.
   - The audition button must not push an undo checkpoint (it currently
     doesn't; keep it that way). Everything else on the row is unchanged.
6. **Update the two node-description strings** that describe the old
   behaviour: `src/main.cpp:12596` (the Sampler help text — its last sentence,
   "Free-running (no note cable) plays continuously the moment it's patched",
   is now transport-gated) and the class comment at
   `src/nodes/SamplerNode.h:16-28`. Also update `StopPreview`'s comment at
   `SamplerNode.h:82-86` — it's no longer "stop whatever the Play button
   triggered", it's "stop the audition voice".

## Design decisions already made (don't re-open)

- **Transport stop releases note voices too**, including notes from an
  external MIDI keyboard held at that moment. The alternative — letting live
  MIDI ring through a stopped transport, like a hardware synth — was
  considered and rejected: every note source in this patch graph is
  patch-internal (Drum Sequencer, Note Sequencer, Arpeggiator, MIDI Notes), so
  "space silences this node, always" is the simpler promise, and a held key
  simply retriggers on the next note-on.
- **No new user-facing control.** No run/mode/trigger toggle is added — free-run
  stays implicit on "no note cable connected", and the new status label is what
  makes it legible. This node already carries ~9 controls; keep it at that.
- **Audition stays monophonic** and retriggers in place, matching today's
  behaviour (lines 339–348) — rapid waveform clicks must not stack copies.

## Out of scope

Don't touch recording (`StartRecording`/`StopRecording`, `AudioTopologyRequest`
plumbing), the waveform widget's drag handles (`DrawSamplerWaveform`,
`src/main.cpp:4885`), sample loading, or the Drum Sequencer — which has its own
separate per-node `run` toggle and is deliberately not being unified with this.

## Done means

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

compiles clean, plus the audio sweeps, which cover this node's param
round-trip and mid-playback teardown:

```bash
.claude/skills/audio-node-sweep/driver.sh
```

Then verify by hand in the app, since none of the above is covered by an
automated test: patch a Sampler with a note source and confirm (a) incoming
notes never cut off an audition and the audition button never cuts off a note,
(b) spacebar-stop silences the node completely in both note-driven and
free-run patches, (c) the audition button still sounds while the transport is
stopped, and (d) the button label no longer flickers when notes play.

In /Users/namansoni/infinte, fix a real note-off matching bug: every consumer
that tracks active notes matches note-off events by MIDI pitch (int 0-127)
alone, not by voice identity. This means two overlapping notes at the same
pitch from the same producer (legato replay, a doubled chord note, an
arpeggiator repeat) can have the wrong one released, or the real one left
stuck on.

NoteEvent already has a `source` field (src/audio/NoteEvent.h:26) intended
for exactly this kind of matching, but it's currently read by zero
consumers — it only distinguishes producer identity, not overlapping notes
from the *same* producer, so it doesn't fully solve this on its own.

Add an explicit `int voiceId = 0;` field to NoteEvent (src/audio/NoteEvent.h,
after `source`). Add a small monotonic counter (a function like
`int NextVoiceId()` backed by a `std::atomic<int>`, colocated in
NoteEvent.h or NoteEventQueue.h) that producers call once per note-on.

Producers to update (all in src/nodes/NoteNodes.cpp unless noted) — each
assigns a fresh voiceId on note-on, stores it alongside whatever
already-tracked note number it keeps internally, and reuses that same
voiceId on the matching note-off:
- MIDI Notes: AudioMidiNotesNode::ProcessBlock, line 74 (currently sets
  `e.source = this`)
- Note Sequencer: AudioNoteSequencerNode::ProcessBlock, lines 2454/2477/2487
- Arpeggiator: AudioArpeggiatorNode::ProcessBlock, lines 2015/2075/2087 —
  currently matches note-off via linear search on `mHeld[h].note == e.note`
  (lines 1985-1994); add voiceId to the `HeldNote` struct and match on that
  instead
- Random Note Generator: AudioRandomNoteGeneratorNode::ProcessBlock, lines
  2630/2657/2667
- Chorder: AudioChorderNode::ProcessBlock, line 2850
- Bouncing Balls: AudioBouncingBallsNode::ProcessBlock, line 3496
- Note Echo: AudioNoteEchoNode::ProcessBlock, lines 1663/1693 — echoes are
  independent delayed copies, so just forward voiceId through unchanged,
  no new matching needed
- Note Router: AudioNoteRouterNode::ProcessBlock, lines 1847/1861 —
  currently keys `uint8_t mRoutedMask[128]` by `in.note`; add a parallel
  voiceId-keyed map (voiceId is not bounded like MIDI note, so this can't
  stay a flat 128-entry array — use a small fixed-capacity open-addressed
  table or std::array<std::pair<int,uint8_t>, N> with linear scan, matching
  the real-time-safety rules in AudioNode.h: no allocation, no std::map)
- Note Capturer: AudioNoteCapturerNode::ProcessBlock, lines 3103/3142,
  ForceOffAllSounding at 3211 — currently `bool mSounding[128]` keyed by
  note; needs the same voiceId-keyed approach as Note Router
- Note Modify: AudioNoteModifyNode, `int mOutNote[128]` keyed by input note
  (lines 686/800) — same treatment

Consumers to update:
- Wavetable: src/nodes/WavetableNode.cpp — `Voice mVoices[kMaxVoices]`
  array; `NoteOff(int note)` at line 805 does
  `if (!v.active || v.note != note || !v.held) continue;` — add a
  `voiceId` field to `Voice`, change NoteOff to take and match on voiceId
  (keep `note` on the struct for pitch, just stop using it for matching)
- Sampler: src/nodes/SamplerNode.cpp line 281 calls
  `mVoices.NoteOff(evts[evtIdx].note)` against the shared
  `VoiceAllocator` class (src/audio/AudioVoice.h/.cpp). `VoiceAllocator`'s
  `Voice` struct (AudioVoice.h:136-142) has only note/velocity/age/envelope
  — add `voiceId`, change `VoiceAllocator::NoteOff` (AudioVoice.cpp:60-70)
  to match on it instead of `voice.note == midiNote`

Explicitly out of scope, do not touch:
- Envelope (AudioEnvelopeNode::ProcessBlock, NoteNodes.cpp lines 226-244):
  has no per-note matching at all today (single global Envelope, any
  note-on/off affects it regardless of pitch or source) — this is a
  separate, larger design question about polyphonic envelopes, not a
  voiceId retrofit. Leave its behavior unchanged.
- Drum Sequencer (DrumSequencerNode.cpp): does not consume NoteEvents at
  all (self-contained step sequencer with its own Voice array) — nothing
  to change.
- AudioPluginNode (AudioPluginNode.cpp lines 46-58): translates NoteEvents
  directly into raw 3-byte MIDI messages for an external AU plugin; voice
  matching is delegated entirely to the plugin's own engine. Standard MIDI
  note messages have no per-note voice-id channel, so voiceId cannot be
  transmitted here — leave this file unchanged.

After implementing, build with:
  cmake --build build -j"$(sysctl -n hw.ncpu)"
and confirm it compiles clean. Then run the audio-node-sweep skill's
AUDIOTEARDOWNSWEEPTEST path (mid-playback spawn/delete of Wavetable,
Sampler, Arpeggiator, Note Router nodes) to confirm nothing crashes or
leaves a stuck voice — this bug class is exactly what that sweep is
designed to catch. Manually test: two overlapping same-pitch notes from
an Arpeggiator or Note Sequencer into a Wavetable/Sampler, confirm both
release independently rather than the second note-off double-releasing.

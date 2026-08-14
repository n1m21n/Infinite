#pragma once

#include <memory>

#include "core/INode.h"
#include "core/Modulation.h"
#include "core/NoteCable.h"

// The note-transport nodes. Both follow the same two-object rule every audio
// node already does (see docs/plans/audio/README.md): the INode owns an
// AudioNode-derived class that runs on the real-time thread, talked to only
// through param pushes - CookIfNeeded does no DSP.
class AudioMidiNotesNode;
class AudioEnvelopeNode;
class AudioNoteFilterNode;
class AudioNoteModifyNode;
class AudioNoteEchoNode;
class AudioNoteRouterNode;
class AudioArpeggiatorNode;
class AudioNoteSequencerNode;
class AudioRandomNoteGeneratorNode;
class AudioChorderNode;
class AudioNoteCapturerNode;
class AudioBouncingBallsNode;
class AudioScaleNotesNode;

// The system's note source: live MIDI in, turned into NoteEvents on the note
// cable. Replaces the P3a Note Sequencer, whose fixed one-pitch step grid was
// only ever a way to prove the note transport before there was real MIDI to
// drive it.
//
// It reads Platform's lock-free note ring (see Platform.h's "live note
// stream") from ProcessBlock rather than polling MidiChannelLastNote from the
// main thread, because a synth needs every note-off in order - a sampled
// "current note" loses the ones that arrive between two frames and leaves
// voices stuck on.
//
// Timing: Platform's ring carries no sample timestamps, so every event in a
// block is stamped frameOffset 0. That quantises incoming MIDI to the audio
// block boundary (~10 ms at 512 frames), which is the same resolution CoreMIDI
// delivery into a non-timestamped host gives anyway; sample-accurate MIDI
// would need the packet's own MIDITimeStamp carried through the ring and
// converted against the device's clock, which is a separate piece of work.
class MidiNotesNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new MidiNotesNode(); }
   MidiNotesNode();
   ~MidiNotesNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   // Starts the shared CoreMIDI engine (see Platform::MidiStart), same call
   // MidiCCNode/MidiTriggerNode's "MIDI Learn" makes. There is nothing to
   // learn here - channel is a param, not a binding - so this just flips the
   // engine on; kept as a bool return to surface a device-open failure.
   bool StartListening();

   int channel = -1;             // -1 = omni, else 0-15
   int transpose = 0;            // semitones applied to every incoming note
   float velocityScale = 1.0f;   // 0..2, applied before the note leaves this node

   // Main-thread view of what is currently held, for the inline keyboard.
   // Published by the audio thread as two atomics rather than 128 flags.
   void HeldKeys(bool out[128]) const;
   int HeldCount() const;
   int LastNote() const;   // -1 if nothing has played yet

private:
   std::unique_ptr<AudioMidiNotesNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A note consumer whose output is a modulator value, not an audio buffer -
// see docs/plans/audio/audio-graph-semantics.md §6. Built on the existing
// Envelope class (src/audio/AudioVoice.h), unchanged - this node just wires
// note events into NoteOn()/NoteOff() and publishes Process()'s per-block
// result for Value01() to read.
class EnvelopeNode : public INode, public IModulator
{
public:
   static INode* Create() { return new EnvelopeNode(); }
   EnvelopeNode();
   ~EnvelopeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* AudioNodeForNotePorts() override;

   // IModulator - reads the audio-thread-published current envelope level.
   float Value01() override;

   float attackMs = 10.0f;
   float decayMs = 200.0f;
   float sustainLevel = 0.6f;
   float releaseMs = 400.0f;
   NoteCable noteInput;

private:
   std::unique_ptr<AudioEnvelopeNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A gate on a note's pitch: snap-to-scale, pass-if-in-range, pass-if-lucky -
// see README.md §3's Notes table ("All four are gates on a note's pitch").
// Forwards note-on/off pairs it lets through unchanged in timing; a note-on
// that fails the gate has its matching note-off dropped too (tracked per
// input note number on the audio thread), so a rejected note can never leave
// a downstream voice stuck on.
class NoteFilterNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new NoteFilterNode(); }
   NoteFilterNode();
   ~NoteFilterNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   // MusicTime::ScaleType; kChromatic (every pitch class in the table) makes
   // the scale gate a no-op, which is the "off" state - no separate enable
   // flag needed.
   int scale = 13; // MusicTime::kChromatic
   int root = 0;   // 0 = C .. 11 = B
   int rangeLow = 0;
   int rangeHigh = 127;
   float chance = 100.0f; // percent
   NoteCable noteInput;

   // Main-thread readout for the visualizer - last note this node was asked
   // to gate, and whether it passed. -1 if nothing has arrived yet.
   int LastNoteIn() const;
   bool LastPassed() const;

private:
   std::unique_ptr<AudioNoteFilterNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Changes an attribute of a note in flight: transpose, velocity curve,
// timing/velocity humanise, and an optional fixed gate-length override -
// README.md §3's "biggest single win" consolidation (transposing, note
// duration, velocity expressions, note expressions all fold into one node).
// Pan is not included: NoteEvent (audio/NoteEvent.h) carries no per-note pan
// field, and adding one to route through every note consumer is a much
// bigger change than this node needs.
//
// gateHoldMs == 0 passes note-off through unchanged (default). > 0 ignores
// the real note-off and instead schedules an internal one that many ms after
// the note-on - this is also "note duration": a plucked/staccato patch is
// gateHoldMs with a short value, a pad that ignores host note length is a
// long one.
class NoteModifyNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new NoteModifyNode(); }
   NoteModifyNode();
   ~NoteModifyNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   int transposeSemi = 0;          // -48..48, folds in "octave" (dial by 12s)
   int pitchSemi = 0;               // -24..24, a second additive transpose (fine pitch offset)
   float velocityCurve = 1.0f;     // 0.25..4, exponent; 1 = linear/unchanged
   float humanizeTimingMs = 0.0f;  // 0..100, random jitter on note-on timing
   float humanizeVelocity = 0.0f;  // 0..100 percent, random jitter on velocity
   float gateHoldMs = 0.0f;        // 0..3000, 0 = passthrough note-off
   int quantizeDiv = 0;             // 0 = off, else index into kQuantizeDivisions (see .cpp)
   float glideMs = 0.0f;            // 0..2000, portamento time applied to LastNoteOut() readout consumers
   NoteCable noteInput;

   // Main-thread readout for the visualizer.
   int LastNoteOut() const;
   float LastVelocityOut() const;

private:
   std::unique_ptr<AudioNoteModifyNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Generates new notes over time from an incoming one - genuinely distinct
// from Note Modify (README.md §3), which only ever edits an event that's
// already there. Every incoming event (on and off alike) is echoed `repeats`
// times, each `delayMs` further apart, with velocity decaying and pitch
// optionally shifting per repeat. The original event always passes through
// unchanged first (repeat 0); this is a dry+wet echo, not a bypassable one -
// mute it downstream if only the repeats are wanted.
class NoteEchoNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new NoteEchoNode(); }
   NoteEchoNode();
   ~NoteEchoNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   float delayMs = 150.0f;         // 10..1000
   int repeats = 3;                // 1..8
   float decay = 60.0f;            // 0..100 percent, velocity multiplier per repeat
   int transposePerRepeat = 0;     // -12..12 semitones, applied cumulatively per repeat
   NoteCable noteInput;

   int PendingCount() const; // main-thread readout for the visualizer

private:
   std::unique_ptr<AudioNoteEchoNode> mAudioNode;
   int mLastCookFrame = -1;
};

// The system's only note fan-out point (audio-graph-semantics.md §1: "Note
// Router (1->4) for notes"). One note input, four note outputs, each a
// distinct NoteCable output slot (see NoteCable::GetOutputSlot()) - unlike
// every other note node, whose single implicit output is always slot 0.
//
// A note-on's routed destination(s) are remembered per input note number so
// its matching note-off always replays to the same output(s), regardless of
// mode changes or RNG state in between - the same stuck-note guard Note
// Filter and Note Modify already use.
class NoteRouterNode : public INode, public INoteSource
{
public:
   enum Mode { kRoundRobin, kRandom, kProbability, kChain };

   static INode* Create() { return new NoteRouterNode(); }
   NoteRouterNode();
   ~NoteRouterNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   // Four distinct note outputs - see GraphNode::kOutputBase/OutputPinId.
   int OutputCount() const override { return 4; }
   const char* OutputLabel(int index) const override;

   int mode = kRoundRobin;
   float probability = 50.0f; // 0..100, kProbability mode only
   NoteCable noteInput;

   int LastRoutedMask() const; // main-thread readout for the visualizer, bit i = output i

private:
   std::unique_ptr<AudioNoteRouterNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Holds whatever notes are currently down and re-plays them one at a time,
// tempo-synced (README.md §3: "up/down/updown/random/as-played, octaves,
// rate"). The incoming note-on/off pairs only ever change *which notes are
// held* - the arpeggiator's own output notes are a separate, self-timed
// sequence built from that held set, the same "notes in, different notes
// out, on its own clock" shape Note Sequencer's generator has.
class ArpeggiatorNode : public INode, public INoteSource
{
public:
   enum Mode { kUp, kDown, kUpDown, kDownUp, kConverge, kDiverge, kRandom, kAsPlayed };

   static INode* Create() { return new ArpeggiatorNode(); }
   ArpeggiatorNode();
   ~ArpeggiatorNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   int mode = kUp;
   int octaves = 1;         // 1..4
   int rateMode = 0;        // 0 = synced (rateBeats, a note-division), 1 = free (rateSeconds)
   float rateBeats = 0.25f; // beats per step, synced mode - a beat is a quarter note
   float rateSeconds = 0.2f; // seconds per step, free mode
   float gatePercent = 80.0f; // 0..100, percent of the step the note stays on

   NoteCable noteInput;

   int HeldCount() const;    // main-thread readouts for the visualizer
   int CurrentNote() const;  // -1 if nothing is currently sounding

private:
   std::unique_ptr<AudioArpeggiatorNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A self-driving step sequencer (README.md §3, arity table: no note input,
// generates its own stream). Up to 16 steps, each an independent (note,
// velocity, enabled) triple edited as a vertical bar - drag sets pitch,
// the thin strip below sets velocity, the dot toggles the step on/off.
// `steps` controls how many of the 16 loop before wrapping, the same
// "length vs. fixed slot count" split Pattern (the modulator step sequencer)
// already uses.
class NoteSequencerNode : public INode, public INoteSource
{
public:
   static constexpr int kMaxSteps = 16;

   static INode* Create() { return new NoteSequencerNode(); }
   NoteSequencerNode();
   ~NoteSequencerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   int steps = 8; // 1..kMaxSteps
   int rateMode = 0;          // 0 = synced (rateBeats), 1 = free (rateSeconds)
   float rateBeats = 0.25f;   // beats per step, synced mode
   float rateSeconds = 0.2f;  // seconds per step, free mode
   float gatePercent = 70.0f; // 0..100, percent of the step the note stays on

   int stepNote[kMaxSteps];
   float stepVelocity[kMaxSteps];
   bool stepEnabled[kMaxSteps];

   int CurrentStep() const; // main-thread readout for the visualizer, -1 if stopped

private:
   std::unique_ptr<AudioNoteSequencerNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A self-driving generative note source: a bounded random walk, not
// independent-per-step randomness - each new note is the previous note plus
// a small random offset (clamped to range, snapped to scale), so the line
// wanders instead of jumping around. That carried-forward previous-note
// state is "memory of previous note" (as opposed to Note Sequencer, whose
// steps are fixed data with no algorithm behind them). Step timing is beats,
// so it follows the transport the same way every other beat-synced node
// does; a bar's time signature is just "how many of these steps make a bar"
// at the rate chosen.
class RandomNoteGeneratorNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new RandomNoteGeneratorNode(); }
   RandomNoteGeneratorNode();
   ~RandomNoteGeneratorNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   int rangeLow = 48;
   int rangeHigh = 72;
   int scale = 13; // MusicTime::kChromatic
   int root = 0;
   int rateMode = 0;         // 0 = synced (rateBeats), 1 = free (rateSeconds)
   float rateBeats = 0.25f;
   float rateSeconds = 0.2f;
   int maxStep = 4; // semitones, max wander per step

   int LastNote() const; // main-thread readout for the visualizer

private:
   std::unique_ptr<AudioRandomNoteGeneratorNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A self-driving generative chord engine (README.md §3's Maze row: "scaled
// degrees, chord count, groove, strum, upper harmonics, humanise timing +
// velocity"). Picks its own chord roots by algorithm - a random scale degree
// every step, stacked in thirds (tertian chord-building over the chosen
// scale via MusicTime::DegreeToNote). Formerly two nodes (Maze and a
// grid-based Chorder); consolidated into one under the Chorder name.
class ChorderNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new ChorderNode(); }
   ChorderNode();
   ~ChorderNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   int scale = 0;   // MusicTime::kMajor
   int root = 0;
   int chordSize = 3;          // 2..6 stacked scale-degree thirds
   float rateBeats = 1.0f;     // groove - beats per chord
   float strumMs = 15.0f;      // 0..200, spacing between each chord tone's onset
   float humanizeTimingMs = 10.0f;
   float humanizeVelocity = 15.0f;
   float upperHarmonics = 20.0f; // 0..100 percent chance of an extra note an octave above

   int LastChordSize() const; // main-thread readout for the visualizer

private:
   std::unique_ptr<AudioChorderNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Quantizes every incoming note's pitch to the nearest degree of a chosen
// scale/root - a plain gate, no timing or velocity change at all. Distinct
// from Note Filter (which also range-gates and chance-gates): this node
// does one thing only, so it can sit inline in a chain without side effects.
class ScaleNotesNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new ScaleNotesNode(); }
   ScaleNotesNode();
   ~ScaleNotesNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   int scale = 0; // MusicTime::kMajor
   int root = 0;
   NoteCable noteInput;

   int LastNoteOut() const; // main-thread readout for the visualizer

private:
   std::unique_ptr<AudioScaleNotesNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A MIDI capturer/recorder/player with looping - records whatever note
// stream feeds it (tempo-relative timing, so it loops in sync regardless of
// BPM changes), then plays it back layered on top of the live input, which
// always passes through unchanged. Follows DrawNode's own record/play
// transport exactly (StartRecording/StopRecording/PlayRecording/
// StopPlayback, none of it persisted across save/load - a recording is
// session state, not patch state, the same call DrawNode's stroke recorder
// already made) - the only real difference is this one's record/playback
// loop runs on the audio thread, since its data is notes, not pixels.
class NoteCapturerNode : public INode, public INoteSource
{
public:
   static constexpr int kMaxEvents = 512;

   static INode* Create() { return new NoteCapturerNode(); }
   NoteCapturerNode();
   ~NoteCapturerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   bool loop = true;
   int quantizeDiv = 0; // 0 = off, else index into kQuantizeBeats (NoteNodes.cpp) - snaps recorded onsets on StopRecording
   NoteCable noteInput;

   void StartRecording();
   void StopRecording();
   void StartPlayback();
   void StopPlayback();
   void ClearRecording();

   bool IsRecording() const;
   bool IsPlaying() const;
   double RecordedLengthBeats() const;
   int RecordedCount() const;
   double Playhead01() const; // 0..1 position within the loop, for the visualizer
   // Most-recent-first-irrelevant snapshot for drawing: up to kMaxEvents
   // (note, velocity, beat) triples, note-on events only.
   int RecordedNoteOns(int outNote[kMaxEvents], float outBeat[kMaxEvents]) const;

   // Main-thread readout for the keyboard visualizer: notes currently
   // sounding, whether from the live passthrough or the playback layer.
   void HeldKeys(bool out[128]) const;

private:
   std::unique_ptr<AudioNoteCapturerNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Balls bouncing inside a shape, emitting a note whenever one hits the wall.
// The physics runs on the audio thread (fixed-size ball array, no
// allocation), the same reason every other generator in this file ticks its
// clock there rather than on CookIfNeeded - a note's timing has to come from
// the same clock the rest of the note graph runs on. The main thread only
// ever reads a published snapshot of the ball positions to draw them; it
// never touches the simulation state.
class BouncingBallsNode : public INode, public INoteSource
{
public:
   enum Shape { kCircle, kSquare, kTriangle };
   static constexpr int kMaxBalls = 12;
   static constexpr float kBound = 0.9f; // shape's half-extent / radius, normalized -1..1 space

   static INode* Create() { return new BouncingBallsNode(); }
   BouncingBallsNode();
   ~BouncingBallsNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   int shape = kCircle;
   int numBalls = 4;    // 1..kMaxBalls
   float ballSize = 0.06f;  // 0.02..0.15, normalized to the shape's -1..1 space
   float ballSpeed = 0.6f;  // 0.05..2.0, units/sec in that same normalized space
   int rangeLow = 48;
   int rangeHigh = 84;

   // Main-thread readouts for the visualizer: positions of the first
   // `numBalls` balls, normalized to the shape's -1..1 space, plus a
   // 0..1 flash level per ball that snaps to 1 on a wall hit and decays.
   int BallPositions(float outX[kMaxBalls], float outY[kMaxBalls], float outFlash[kMaxBalls]) const;

private:
   std::unique_ptr<AudioBouncingBallsNode> mAudioNode;
   int mLastCookFrame = -1;
};

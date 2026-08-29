#pragma once

#include <algorithm>
#include <memory>

#include "core/INode.h"
#include "core/Modulation.h"
#include "core/NoteCable.h"

// The note-transport nodes. Both follow the same two-object rule every audio
// node already does (see docs/plans/audio/README.md): the INode owns an
// AudioNode-derived class that runs on the real-time thread, talked to only
// through param pushes - CookIfNeeded does no DSP.
class AudioMidiNotesNode;
class AudioNoteToCVNode;
class AudioNoteFilterNode;
class AudioSemitoneShiftNode;
class AudioPitchBendNode;
class AudioVelocityCurveNode;
class AudioGateNode;
class AudioHumanizerNode;
class AudioQuantizerNode;
class AudioGlideNode;
class AudioNoteEchoNode;
class AudioNoteRouterNode;
class AudioNoteMergeNode;
class AudioArpeggiatorNode;
class AudioNoteSequencerNode;
class AudioRandomNoteGeneratorNode;
class AudioChorderNode;
class AudioNoteCapturerNode;
class AudioBouncingBallsNode;
class AudioStrumNode;
class AudioNoteStackNode;

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
   bool useGlobalScale = false;  // snap incoming notes to Transport's key/scale

   // Main-thread view of what is currently held, for the inline keyboard.
   // Published by the audio thread as two atomics rather than 128 flags.
   void HeldKeys(bool out[128]) const;
   int HeldCount() const;
   int LastNote() const;   // -1 if nothing has played yet

private:
   std::unique_ptr<AudioMidiNotesNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Note pitch -> normalized modulation value, so a note stream can drive a
// synth param (cutoff, warp, pan, ...) the same way an LFO does. Unlike
// Envelope (ModulatorNodes.h), this doesn't gate on note-on/off - it's a
// pitch tracker, not an amplitude stage - so the CV holds the last played
// note's pitch rather than falling back toward 0 on release; that's the
// useful behaviour for "modulate a filter by which note is playing".
class NoteToCVNode : public INode, public IModulator
{
public:
   static INode* Create() { return new NoteToCVNode(); }
   NoteToCVNode();
   ~NoteToCVNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* AudioNodeForNotePorts() override;

   // IModulator - normalized 0..1, (lastNote - rangeLow) / (rangeHigh - rangeLow).
   float Value01() override;

   int rangeLow = 0;    // MIDI note mapped to output 0.0
   int rangeHigh = 127; // MIDI note mapped to output 1.0
   float glideMs = 20.0f; // one-pole smoothing time toward a newly played note
   NoteCable noteInput;

   int LastNote() const; // -1 if nothing has played yet

private:
   std::unique_ptr<AudioNoteToCVNode> mAudioNode;
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
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   // MusicTime::ScaleType; kChromatic (every pitch class in the table) makes
   // the scale gate a no-op, which is the "off" state - no separate enable
   // flag needed.
   int scale = 13; // MusicTime::kChromatic
   int root = 0;   // 0 = C .. 11 = B
   int rangeLow = 0;
   int rangeHigh = 127;
   float chance = 100.0f; // percent
   bool useGlobalScale = false;
   NoteCable noteInput;

   // Main-thread readout for the visualizer - last note this node was asked
   // to gate, and whether it passed. -1 if nothing has arrived yet.
   int LastNoteIn() const;
   bool LastPassed() const;

private:
   std::unique_ptr<AudioNoteFilterNode> mAudioNode;
   int mLastCookFrame = -1;
};

// ---------------------------------------------------------------------------
// The eight nodes below are the note-modification surface: transpose,
// velocity curve, timing/velocity humanise, gate, quantize, glide, and pitch
// bend, one concern per node - each small enough to be one or two knobs,
// standing in a chain wherever only one of these is needed.

// A single semitone offset applied to every note that passes through,
// applied at note-on and remembered per input voice so the matching note-off
// lands on the same output pitch even if the knob moves while the note is
// held. This is a deliberate, quantised transpose - octave/key changes, not
// a performance gesture - which is why it's still the wrong shape for a
// live bend even though NoteEvent now carries a continuous bendSemitones
// channel (see PitchBendNode below, which uses that field instead): this
// class only ever re-pitches notes as they attack, and adding a bendUpdate
// re-emission path to it would just reinvent Pitch Bend under another name.
class NoteTransposeNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new NoteTransposeNode(); }
   NoteTransposeNode();
   ~NoteTransposeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   int semitones = 0; // -48..48
   bool useGlobalScale = false; // snap transposed output to global scale
   NoteCable noteInput;

   int LastNoteOut() const;

private:
   std::unique_ptr<AudioSemitoneShiftNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A hand-driven bend, patched inline in the note chain like Note Transpose -
// NoteEvent (audio/NoteEvent.h) now carries a continuous `bendSemitones`
// field plus a `bendUpdate` re-emission flag, exactly so this node can slide
// a note that's already sounding rather than only snap a new one to a
// different pitch at attack (which is all AudioSemitoneShiftNode-style
// nodes, or the old IModulator-only version of this node, could ever do).
//
// Every note-on gets the current knob value added into its bendSemitones
// (additive with whatever an upstream Pitch Bend already added, so multiple
// instances stack); every block where the knob has actually moved, this node
// re-emits a bendUpdate for each note it currently has held, carrying that
// note's original voiceId forward so downstream nodes and the synth can
// re-target the same voice without a new note-on. `bendSemitones` is stored
// and drawn in real semitones (not the usual 0..1), matching every other
// audio node whose knob has real physical units. Default range +/-2 st
// matches the standard MIDI wheel.
class PitchBendNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new PitchBendNode(); }
   PitchBendNode();
   ~PitchBendNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   static constexpr float kRange = 2.0f; // +/-2 st, the standard wheel range

   float bendSemitones = 0.0f; // -2..2
   NoteCable noteInput;

private:
   std::unique_ptr<AudioPitchBendNode> mAudioNode;
   int mLastCookFrame = -1;
};

// pow(velocity, curve) on every note-on; note-off passes through unchanged.
// curve < 1 boosts soft hits, curve > 1 favours hard ones, 1 = linear/off.
class VelocityCurveNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new VelocityCurveNode(); }
   VelocityCurveNode();
   ~VelocityCurveNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   float curve = 1.0f; // 0.25..4, exponent; 1 = linear/unchanged
   NoteCable noteInput;

   float LastVelocityIn() const;
   float LastVelocityOut() const;

private:
   std::unique_ptr<AudioVelocityCurveNode> mAudioNode;
   int mLastCookFrame = -1;
};

// gateHoldMs == 0 passes note-off through unchanged (default, i.e. off). > 0
// ignores the real note-off and instead schedules an internal one that many
// ms after the note-on - fixed note duration regardless of how long the key
// was actually held.
class GateNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new GateNode(); }
   GateNode();
   ~GateNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   float holdMs = 0.0f; // 0..3000, 0 = passthrough note-off
   NoteCable noteInput;

private:
   std::unique_ptr<AudioGateNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Random jitter on note-on timing and velocity - two knobs, both 0 = off.
class HumanizerNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new HumanizerNode(); }
   HumanizerNode();
   ~HumanizerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   float timingMs = 0.0f; // 0..100, random jitter on note-on timing
   float velocityPct = 0.0f; // 0..100, random jitter on velocity
   NoteCable noteInput;

private:
   std::unique_ptr<AudioHumanizerNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Snaps note-on timing forward to the nearest grid line at `div` - never
// earlier than the note actually arrived. div 0 = off.
class QuantizerNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new QuantizerNode(); }
   QuantizerNode();
   ~QuantizerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   int div = 0; // 0 = off, else index into kQuantizeDivisions (see .cpp)
   NoteCable noteInput;

private:
   std::unique_ptr<AudioQuantizerNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A fast chromatic run from the previously played note into each new one -
// NoteEvent carries no continuous pitch, so this approximates portamento as
// a glissando (a burst of very short intermediate notes) rather than a true
// pitch ramp. 0 = off (immediate note-on, no run).
class GlideNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new GlideNode(); }
   GlideNode();
   ~GlideNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   float glideMs = 0.0f; // 0..2000
   NoteCable noteInput;

private:
   std::unique_ptr<AudioGlideNode> mAudioNode;
   int mLastCookFrame = -1;
};

// A free-running sine LFO exposed as a modulator, not a note processor -
// NoteEvent has no continuous pitch channel for a real per-note vibrato to
// ride on (see NoteToCVNode's class comment), so this is the practical
// substitute: wire its output into a synth's own pitch/fine/detune knob
// (anything ModKnob's modulation pin accepts) for the same audible wobble.
// One knob: rate in Hz. Output is bipolar sine rescaled to 0..1, matching
// every other modulator's Value01() contract.
class VibratoNode : public INode, public IModulator
{
public:
   static INode* Create() { return new VibratoNode(); }
   VibratoNode();
   ~VibratoNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   float Value01() override;

   float rateHz = 5.0f; // 0.5..12
};

// Generates new notes over time from an incoming one - genuinely distinct
// from the single-purpose editors above (Note Transpose, Velocity Curve, and
// the rest), which only ever edit an event that's already there. Every
// incoming event (on and off alike) is echoed `repeats` times, each tap
// further apart than the last, with velocity decaying and pitch optionally
// shifting per repeat - the same shape as Ableton's Max for Live "Note Echo"
// device (Sync/Delay Time, Pitch, Delay-as-velocity, Input Thru/Mute).
// Spacing follows the same rateMode split as every other tempo-aware
// generator in this file (Arpeggiator, Note Sequencer, ...): synced to a
// beat division by default, or a free millisecond time. The original event
// passes through unchanged first (repeat 0) unless muteDry swallows it, in
// which case only the delayed copies are heard.
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
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   // rateMode defaults to free (1), matching every patch saved before this
   // field existed - its default rateSeconds (0.15s = the old fixed 150ms)
   // reproduces the old behaviour until the user opts into Synced.
   int rateMode = 1;               // 0 = synced (rateBeats), 1 = free (rateSeconds)
   float rateBeats = 0.25f;        // beats per tap, synced mode (default 1/16)
   float rateSeconds = 0.15f;      // 0.02..2, free mode
   int repeats = 3;                // 1..8
   float decay = 60.0f;            // 0..100 percent, velocity multiplier per repeat
   int transposePerRepeat = 0;     // -12..12 semitones, applied cumulatively per repeat
   bool muteDry = false;           // true = swallow the original event, only repeats sound
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
// Filter and Note Transpose already use.
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
   INode* BypassSource() override { return noteInput.GetSource(); }
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

// The system's only note fan-in point: up to four note inputs merged into
// one output stream, in timestamp order. Each input's notes stay independent
// voices (matched by NoteEvent::voiceId, never by MIDI pitch - see
// VoiceAllocator::NoteOff), so two inputs firing the same pitch at once
// become two overlapping voices rather than a collision. No priority logic
// (highest/lowest/last) lives here - that's NoteStackNode's job.
class NoteMergeNode : public INode, public INoteSource
{
public:
   static constexpr int kSlots = 4;

   static INode* Create() { return new NoteMergeNode(); }
   NoteMergeNode();
   ~NoteMergeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   INode* BypassSource() override
   {
      for (int i = 0; i < kSlots; i++)
         if (noteInputs[i].IsConnected())
            return noteInputs[i].GetSource();
      return nullptr;
   }
   NoteCable* NoteInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &noteInputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override;
   AudioNode* GetAudioNode() override;

   NoteCable noteInputs[kSlots];

private:
   std::unique_ptr<AudioNoteMergeNode> mAudioNode;
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
   enum Mode { kUp, kDown, kUpDown, kDownUp, kConverge, kDiverge, kRandom, kAsPlayed,
               kRepeat2, kRepeat4, kJoin, kSpread, kJoinSpread, kStairsUp, kStairsDown };

   static constexpr int kGateSteps = 8;

   static INode* Create() { return new ArpeggiatorNode(); }
   static const std::vector<std::string>& PresetNames();
   ArpeggiatorNode();
   ~ArpeggiatorNode() override;
   void ApplyPreset(int index);

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
   int stepGates = 0xFF;      // bit i = step i enabled; all on by default
   bool useGlobalScale = false;

   // UI-only: which preset the dropdown last showed as selected. Not visited
   // by VisitParams - the preset is an action that writes the real params
   // above, and those are already serialized; a persisted `preset` int would
   // be inert for AUDIOPARAMSWEEPTEST's Check B (see new-audio-node prompt).
   int mLastPresetUiIndex = 0;

   NoteCable noteInput;

   int HeldCount() const;      // main-thread readouts for the visualizer
   int CurrentNote() const;    // -1 if nothing is currently sounding
   int CurrentGridStep() const; // -1 if nothing is currently held (playhead parked)

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
   bool useGlobalScale = false;

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
   bool useGlobalScale = true; // follow Transport key/scale by default

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
   bool useGlobalScale = true; // follow Transport key/scale by default

   int LastChordSize() const; // main-thread readout for the visualizer

private:
   std::unique_ptr<AudioChorderNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Layers up to eight transposed copies on top of every incoming note - input-
// driven and key-agnostic, unlike Chorder above (a self-clocked generator).
// The dry note always passes through unchanged; each of the eight voices is
// an independent semitone offset with its own enable switch, added on top of
// it, not instead of it. The set of voices actually sounding for a given
// input note is captured at note-on and replayed verbatim at note-off, so
// toggling a voice mid-note never stray-releases or strands a pitch.
class NoteStackNode : public INode, public INoteSource
{
public:
   static constexpr int kVoices = 8;

   static INode* Create() { return new NoteStackNode(); }
   NoteStackNode();
   ~NoteStackNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   int  semitones[kVoices] = { 12, 7, 5, 3, -3, -5, -7, -12 };
   bool enabled[kVoices]   = { false, false, false, false, false, false, false, false };
   bool useGlobalScale = false;
   NoteCable noteInput;

   int LastStackSize() const; // main-thread readout: notes emitted for the last note-on

private:
   std::unique_ptr<AudioNoteStackNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Single-knob note strumming processor: spreads simultaneous notes in a chord
// across time by strumMs per voice (ascending pitch order), while preserving
// original note gate lengths.
class NoteStrumNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new NoteStrumNode(); }
   NoteStrumNode();
   ~NoteStrumNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   float strumMs = 25.0f; // 0..200 ms
   NoteCable noteInput;

   int LastStrumCount() const;

private:
   std::unique_ptr<AudioStrumNode> mAudioNode;
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
   INode* BypassSource() override { return noteInput.GetSource(); }
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
   int scale = 0;
   int root = 0;
   bool useGlobalScale = true;

   // Main-thread readouts for the visualizer: positions of the first
   // `numBalls` balls, normalized to the shape's -1..1 space, plus a
   // 0..1 flash level per ball that snaps to 1 on a wall hit and decays.
   int BallPositions(float outX[kMaxBalls], float outY[kMaxBalls], float outFlash[kMaxBalls]) const;

private:
   std::unique_ptr<AudioBouncingBallsNode> mAudioNode;
   int mLastCookFrame = -1;
};

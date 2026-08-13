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

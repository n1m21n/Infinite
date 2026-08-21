#pragma once

#include "AudioBuffer.h"
#include "NoteEventQueue.h"

// Audio-thread interface. ProcessBlock runs on the real-time render thread
// and must obey the standard real-time-safety constraints (Bencina,
// "Real-time audio programming 101" -
// docs/plans/optimization/research-implementation-map.md 1.1):
// no allocation, no locks with unbounded wait, no syscalls, no unbounded
// loops, no dynamic_cast, no std::function/map/string, no GL/ImGui/file
// I/O, no printf.
//
// Reads its input buffer(s) - one per declared input pin, in pin order,
// already fully computed by upstream nodes this block - and writes its own
// output buffer. `inputs[i]` is null when pin i's cable is unconnected; treat
// that as silence, don't skip the write. Never mutates an input buffer and
// never touches any buffer but the ones passed to this call - this replaced
// the old "read + overwrite one shared buffer" contract because that model
// aliases whenever a node has more than one consumer or a node has more than
// one input; see docs/plans/audio/audio-graph-semantics.md §3.
class AudioNode
{
public:
   virtual ~AudioNode() {}
   virtual void PrepareToPlay(double sampleRate, int maxBlockSize) {}
   virtual void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) = 0;
   virtual void Reset() {}

   // Samples of latency this node's own processing adds (lookahead,
   // oversampling, a hosted plugin's reported latency, ...) at whatever rate
   // it was last PrepareToPlay'd at. 0 (the default) for the overwhelming
   // majority of nodes, which add none. Main thread only - read once per
   // RebuildAudioTopology (main.cpp) to compute each branch's cumulative
   // latency for plugin/effect delay compensation (PDC); never called from
   // ProcessBlock or any other audio-thread path. See AudioEffectRuntime's
   // and AudioPluginAudioNode's overrides.
   virtual int LatencySamples() const { return 0; }

   // --- note ports (P3a) ---------------------------------------------------
   // Optional; the overwhelming majority of AudioNode subclasses carry no
   // note data and use neither. See docs/plans/audio/P3a-notes-prompt.md
   // "What to build" §1 for the design this implements: rather than a
   // second run-loop or a global note bus, a note-producing node owns its
   // own outbox queue and a note-consuming node is handed a pointer to
   // whichever outbox its note cable resolves to - the topology builder
   // (main.cpp's RebuildAudioTopology) wires that pointer once per rebuild,
   // the same cadence AudioEngine::SetTopology already publishes at. Both
   // sides read/write the queue from inside ProcessBlock, on the audio
   // thread, so this needs no extra synchronisation beyond the queue's own.
   //
   // A node that PRODUCES or forwards note events (Note Sequencer, and any
   // future serial note processor) overrides NoteOutbox() to return a
   // pointer to its own NoteEventQueue member, allocated at construction -
   // never on the audio thread mid-block.
   virtual NoteEventQueue* NoteOutbox() { return nullptr; }

   // Same, for a node with more than one note output (Note Router - see
   // NoteCable::GetOutputSlot()). Defaults to the single-outbox case so every
   // other producer is unaffected.
   virtual NoteEventQueue* NoteOutbox(int /*outputSlot*/) { return NoteOutbox(); }

   // A node that CONSUMES note events (Oscillator's note input, Wavetable's,
   // ...) overrides SetNoteInbox(), called once by the topology builder with
   // a pointer to its upstream producer's outbox plus the cursor id that
   // outbox's NoteEventQueue::RegisterConsumer() assigned this consumer, or
   // nullptr/-1 if this node's note pin isn't connected this generation. The
   // outbox can be shared by several consumers (one note source fanned out
   // to multiple synths) - each gets its own cursor so one consumer draining
   // events can never starve another; consumers must pop with
   // `inbox->Pop(cursor, ...)`, never the cursor-less form. The pointer is
   // valid for the lifetime of the topology generation it was set under -
   // AudioEngine's existing one-generation-retire discipline (see
   // AudioEngine.h) already guarantees the producer outlives any in-flight
   // callback using it.
   virtual void SetNoteInbox(NoteEventQueue* inbox, int cursor) { (void)inbox; (void)cursor; }
};

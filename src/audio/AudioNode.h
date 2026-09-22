#pragma once

#include "AudioBuffer.h"
#include "CompensationDelay.h"
#include "NoteEventQueue.h"

#include <atomic>
#include <cstdint>

// Count of Arrangement Timeline Sample re-seeks caused by drift (the node's
// own cursor disagreeing with the timeline by more than its tolerance) rather
// than by a transport jump or a fresh window. Continuous playback must leave
// it at zero; a non-zero delta means a rate mismatch the re-seek is papering
// over. Read by the INFINITE_ARRANGESAMPLETEST fixture.
inline std::atomic<uint64_t> gClipSampleDriftReseeks { 0 };

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

   // Main thread only. Sample rate this node was last PrepareToPlay'd at, or
   // -1.0 if never. Owned and updated entirely by the RebuildAudioTopology
   // call site (main.cpp) - not touched by PrepareToPlay itself or by any
   // subclass - so it survives exactly as long as this AudioNode instance
   // does and needs no separate lifetime bookkeeping (no global map keyed by
   // pointer that could alias a freed-and-reused address).
   //
   // Exists so a topology rebuild that simply re-includes an already-running
   // node (same sample rate) can skip calling PrepareToPlay on it again.
   // RebuildAudioTopology fires on every cable connect/disconnect and every
   // Arrangement Timeline active-clip change - far more often than "this
   // node just started running" - and nearly every DSP kernel's
   // PrepareToPlay unconditionally calls Reset(), zeroing filter/delay/
   // reverb/compressor state. Doing that to a node that is already live and
   // reachable to an Audio Out produced an audible click/pop on every one of
   // those triggers (cable connect while already routed, spacebar play/
   // pause, timeline scrub) - silent only when nothing was actually wired to
   // an Audio Out, since then the reset state had nowhere audible to reach.
   // A genuine sample-rate change (device switch) still compares unequal and
   // forces a real PrepareToPlay/Reset, which is correct - buffers sized for
   // the old rate are invalid at the new one.
   double preparedForSampleRate = -1.0;

   // Must match kAudioMaxNodeInputs (AudioEngine.h) - a static_assert there
   // checks it. Can't reference that constant directly: AudioEngine.h is the
   // one that includes AudioNode.h, not the other way around.
   static constexpr int kMaxInputPins = 12;

   // Main thread only (RebuildAudioTopology, main.cpp). Plugin/effect delay
   // compensation (PDC) state for each of this node's input pins, one
   // CompensationDelay per pin - see CompensationDelay's own comment for why
   // this has to live here, on the persistent AudioNode, rather than inside
   // the AudioTopologyEntry that main.cpp rebuilds from scratch every
   // generation: a fresh, empty CompensationDelay has no "previous state" for
   // Prepare()'s now-idempotent check to compare against, so persistence only
   // works if the SAME object is reused rebuild to rebuild. Same ownership
   // rationale as preparedForSampleRate just above.
   CompensationDelay inputCompensation[kMaxInputPins];
   virtual int AudioOutputCount() const { return 1; }
   virtual void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) = 0;
   virtual void ProcessBlockMulti(const AudioBuffer* const* inputs, int numInputs,
                                  AudioBuffer* const* outputs, int numOutputs)
   {
      if (numOutputs > 0 && outputs[0] != nullptr)
         ProcessBlock(inputs, numInputs, *outputs[0]);
      for (int i = 1; i < numOutputs; i++)
      {
         if (outputs[i] != nullptr)
         {
            for (int ch = 0; ch < outputs[i]->numChannels; ch++)
               std::fill(outputs[i]->channels[ch], outputs[i]->channels[ch] + outputs[i]->numFrames, 0.0f);
         }
      }
   }
   virtual void Reset() {}

   // Arrangement Timeline retrigger (main.cpp's RunTopology lookahead pass):
   // requests that this node seek its own playback position back to the
   // start the next time it cooks, without otherwise disturbing its
   // configuration. Called from the audio thread, immediately before the
   // node's own ProcessBlockMulti for that same block - so it must be a
   // lock-free, same-thread-safe request (an atomic flag consumed at the top
   // of ProcessBlock, the way AudioFilePlayerAudioNode::RequestRestart()
   // already works), never a blocking or allocating operation. No-op by
   // default: only node types with their own internal playback position
   // (a sample player, a phase-based generator) need to override it, and a
   // node with none is unaffected by a clip asking to retrigger it.
   virtual void RequestRetrigger() {}

   // Arrangement Timeline per-clip pitch (main.cpp's RunTopology lookahead
   // pass, same call site as RequestRetrigger): overrides the semitone shift
   // this node reads for its *next* cook only, so a node shared by several
   // Audio Clip/Audio Sample windows plays each one at its own pitch instead
   // of one clip's edit changing what every other clip using the same node
   // sounds like. Same audio-thread-safety contract as RequestRetrigger() - a
   // same-thread request consumed at the top of the node's own cook, never
   // blocking or allocating. No-op by default; only a node with its own
   // pitch/varispeed control (a sample player) needs to override it.
   virtual void SetClipPitchOverride(float semitones) { (void)semitones; }

   // Arrangement Timeline Audio Sample position lock (RunTopology, called
   // every block a Sample window overlaps, immediately before this node's own
   // ProcessBlockMulti on the audio thread). `sourceSeconds` is the position
   // in the source file that belongs at this block's FIRST frame - negative
   // when the clip starts later inside the block, beyond the file's end when
   // the box outlasts the audio - and `sourcePerSecond` is how many source
   // seconds elapse per real second (1 unsynced, projectTempo / sampleBpm
   // synced). `force` marks a transport discontinuity (seek, scrub, loop
   // wrap, Play). Pitch is passed alongside rather than through
   // SetClipPitchOverride so a time-preserving implementation can fold it
   // into its stretch ratio. A block with no call runs the node's normal
   // (canvas) playback. No-op by default; only a sample player overrides it.
   virtual void SetClipSamplePosition(double sourceSeconds, double sourcePerSecond, float pitchSemitones, bool force)
   {
      (void)sourceSeconds; (void)sourcePerSecond; (void)pitchSemitones; (void)force;
   }

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

   // Slot-aware inbox setter for a note consumer with more than one note
   // input (currently only Note Merge, which overrides this). Every other
   // consumer exposes exactly ONE note pin, so whichever slot the topology
   // builder calls with is that pin - forward unconditionally, never
   // `if (inputSlot == 0)`.
   //
   // The slot number is NOT always 0: a node whose slot 0 is an audio input
   // puts its note pin at slot 1 (AudioPluginNode, WaveTerrainNode,
   // ImageSpectralSynthNode). Gating this forward on slot 0 silently dropped
   // their inbox - the override was never called, mNoteInbox stayed nullptr,
   // and the node was mute no matter what was wired into it. That is exactly
   // how VST3/AU instruments lost their note input; do not reintroduce it.
   //
   // Forwarding every slot is safe because the builder only calls this for
   // slots where NoteInputSlot(slot) is non-null (see RebuildAudioTopology's
   // note pass in main.cpp) - a node never sees a slot it doesn't expose.
   virtual void SetNoteInbox(int inputSlot, NoteEventQueue* inbox, int cursor)
   {
      (void)inputSlot;
      SetNoteInbox(inbox, cursor);
   }

   // Upper bound on how many note-input slots any AudioNode exposes - mirrors
   // main.cpp's own kMaxNoteSlots (NoteMergeNode's 4-way fan-in is the
   // widest today). Defined here, not in main.cpp, so appliedInbox/
   // appliedCursor below can be fixed-size arrays and AudioEngine's
   // note-wiring apply pass can size against the same constant without a
   // main.cpp include; main.cpp's own kMaxNoteSlots is just this value.
   static constexpr int kMaxNoteSlots = 4;

   // Owning-thread-only (see AudioEngine::ApplyNoteWiringIfNew): the wiring
   // this node is ACTUALLY running right now, as of the last ApplyNoteInbox()
   // call - as opposed to what RebuildAudioTopology last *described* into
   // AudioTopology::noteWires, which can be a generation or more ahead of
   // what the audio thread has caught up to applying. ApplyNoteWiringIfNew
   // reads this to decide whether a rewired consumer should carry its read
   // cursor over (same producer/slot, just a new cursor id) or start fresh
   // at the producer's current tail (nothing here yet, or a different
   // producer). nullptr/-1 = "nothing applied to this slot yet", matching an
   // unconnected pin.
   NoteEventQueue* appliedInbox[kMaxNoteSlots] = {};
   int appliedCursor[kMaxNoteSlots] = { -1, -1, -1, -1 };

   // Owning-thread-only (same rule as appliedInbox above - called only from
   // AudioEngine::ApplyNoteWiringIfNew, never from RebuildAudioTopology
   // directly any more). Records the wiring this node is now running, then
   // forwards to the ordinary slot-aware virtual so subclasses get the exact
   // same SetNoteInbox(slot, inbox, cursor) callback they always have -
   // ApplyNoteInbox is purely the bookkeeping RebuildAudioTopology's direct
   // mutation used to skip.
   void ApplyNoteInbox(int slot, NoteEventQueue* inbox, int cursor)
   {
      if (slot < 0 || slot >= kMaxNoteSlots)
         return;
      appliedInbox[slot] = inbox;
      appliedCursor[slot] = cursor;
      SetNoteInbox(slot, inbox, cursor);
   }
};

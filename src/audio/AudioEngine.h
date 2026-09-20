#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "AudioBuffer.h"
#include "AudioCaptureRing.h"
#include "ClipPeakRing.h"
#include "AudioNode.h"
#include "CompensationDelay.h"
#include "SamplePreviewPlayer.h"

// Ceilings shared by the topology builder (main.cpp's RebuildAudioTopology)
// and the engine's buffer pool. kAudioMaxNodeInputs is Mixer's 8-in ceiling
// (docs/plans/audio/audio-graph-semantics.md §2); kAudioMaxBlockFrames/
// kAudioMaxChannels bound the fixed-capacity pooled buffers so allocation
// happens only when a topology is built (main thread), never inside a real
// device callback - generous enough that no real block size/channel count
// should ever exceed them. A block that somehow does gets truncated to the
// cap rather than overrunning a pooled buffer - see AudioEngine::RunTopology.
constexpr int kAudioMaxNodeInputs = 12;
constexpr int kAudioMaxNodeOutputs = 12;
constexpr int kAudioMaxBlockFrames = 4096;
constexpr int kAudioMaxChannels = 8;

// One node's place in a topological ordering: which pooled buffer(s) it
// reads (by index into the owning AudioTopology's buffer pool; -1 = that pin
// is unconnected, read as silence) and which pooled buffer it writes its own
// output into. See docs/plans/audio/audio-graph-semantics.md §3.
struct AudioTopologyEntry
{
   AudioNode* node = nullptr;
   int inputBufferIndices[kAudioMaxNodeInputs] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
   int numInputs = 0;
   int outputBufferIndices[kAudioMaxNodeOutputs] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
   int numOutputs = 1;
   int outputBufferIndex = -1; // primary output buffer (mirrors outputBufferIndices[0] for backwards compatibility)
   // A note producer/processor/consumer that needs no device to do its job
   // (Random Note, Sequencer, Note to CV, ...). PumpNoteNodesWithoutDevice runs
   // just these while no audio device is open.
   bool noteOnly = false;

   // Plugin/effect delay compensation (PDC): per input pin, the pin's source
   // branch needs a CompensationDelay so every pin merging into this node
   // arrives sample-aligned - RebuildAudioTopology (main.cpp) computes each
   // branch's cumulative latency and, for a multi-input node whose connected
   // pins carry different cumulative latencies, prepares the shallower pins'
   // delay to make up the difference; a pin whose branch is already the
   // slowest (or the node has only one connected pin) gets an inactive
   // (0-sample, unallocated) one. RunTopology applies these before handing
   // the node its inputs - see its comment.
   //
   // Lives on `node` itself (AudioNode::inputCompensation), NOT as a value
   // here: `order` is rebuilt from scratch every RebuildAudioTopology call,
   // so a CompensationDelay stored directly in this struct would lose its
   // in-flight ring contents (and the audio passing through it would click)
   // on every single rebuild, even when the delay amount never changed. See
   // AudioNode::inputCompensation's comment.
};

static_assert(AudioNode::kMaxInputPins == kAudioMaxNodeInputs,
              "AudioNode::inputCompensation must have one slot per AudioTopologyEntry input pin");

// One arrangement clip's slot in musical time, as the audio thread sees it.
// Beats, not seconds: a tempo change must not move a clip's onset, and
// Transport::Beats() is the one position both the UI and the audio callback
// agree on (see Transport's tempo-staging comment). Allocated on the main
// thread into AudioTopology::clipWindows and published with the topology, so
// the audio thread never allocates and never frees - the array dies with the
// ProcessList through the mRetiring/DrainRetired path.
struct ClipWindow
{
   // Which clip this window belongs to, so the audio thread can label the
   // waveform buckets it measures (WP8). Never dereferenced - it is an id,
   // and the model it indexes lives on the main thread.
   uint64_t clipId     = 0;
   // Hash of the clip fields the waveform cache is keyed on (src, output,
   // start, length), carried through to every ClipPeak this window produces
   // so the main thread can reject buckets measured under a stale shape.
   uint64_t shape      = 0;
   double startBeat    = 0.0;
   double endBeat      = 0.0;   // exclusive
   double fadeInBeats  = 0.0;
   double fadeOutBeats = 0.0;
   float  gain         = 1.0f;  // clip gain, linear (lane gain is on the terminal)
   // Clip pan, as per-channel gains (Mixer's equal-power law, unity - both
   // 1.0 - at centre), multiplied with the terminal's own lanePanL/R at
   // apply time. Channels past the second are left alone. A sample-dropped
   // audio clip's own `pan` field (Arrange::Clip::pan); every other clip
   // stays at the default (no additional pan beyond the lane's).
   float  panL         = 1.0f;
   float  panR         = 1.0f;
   // True when the previous / next window on the same terminal abuts this one
   // exactly. An abutting edge is not a discontinuity - the same node's output
   // runs straight through it - so the declick ramp is skipped there, which is
   // what keeps two adjacent clips of one node sounding like one take instead
   // of dipping every boundary.
   bool   abutsPrev    = false;
   bool   abutsNext    = false;
   bool   retrigger    = true;
   // Semitones, for any Audio Clip or Audio Sample window (Video windows leave
   // this at 0). Pushed to the terminal's sourceNode every block this window
   // is active (see RunTopology) rather than written onto the node itself:
   // the node can be shared by several clips, and a node member would make
   // one clip's pitch edit audible on all of them.
   float  pitch        = 0.0f;
   // True only for an Audio Sample window (Arrange::Clip::sampleDropped) -
   // mirrors the model field so RunTopology's exact-seek lookahead can gate
   // on it without touching the model from the audio thread. A live Audio
   // Clip window leaves this false: it stays onset-only retrigger, ramping
   // up like a real instrument the way the user expects "only clips are
   // supposed to be live" to mean.
   bool   sampleDropped = false;
   // Audio-Sample-only (0 / false for every other window). The source is a
   // pure function of the timeline, recomputed every block from the CURRENT
   // project tempo (tempo edits do not rebuild the topology):
   //   source seconds at beat b = sourceOffsetSeconds + (b - startBeat) * 60 / effBpm
   //   effBpm = syncToTempo ? sampleBpm : projectTempo
   // i.e. a synced Sample time-stretches by projectTempo / sampleBpm, an
   // unsynced one plays at native speed. See RunTopology and
   // AudioNode::SetClipSamplePosition. The arrange panel's static waveform
   // uses the same formula (ArrangeSampleSourceBpm in main.cpp).
   float  sampleBpm    = 0.0f;
   float  origBpm      = 0.0f;  // detected tempo, display only - never read by the audio thread
   bool   syncToTempo  = false;
   // Where in the source this window's box begins (a split/trimmed Sample).
   float  sourceOffsetSeconds = 0.0f;
};

// One connected Audio Out: the pooled buffer its source writes into, and
// (unconditionally) that Audio Out's own capture ring - RunTopology writes
// into it whenever the ring's own `enabled` flag is set, so starting/
// stopping a recording needs no topology rebuild.
struct AudioTerminal
{
   int bufferIndex = -1;
   AudioCaptureRing* capture = nullptr;

   // Linear gain applied when this terminal is summed into the device
   // buffer - 1.0 for every ordinary canvas Audio Out. Arrangement
   // Timeline's Timeline Strict mode is the one producer of terminals with
   // gain != 1.0, one per active clip, carrying that clip's own gainDb.
   float gain = 1.0f;

   // Arrangement Timeline clip scheduling. `numWindows > 0` marks this as a
   // timeline terminal: [windowOffset, windowOffset + numWindows) indexes
   // AudioTopology::clipWindows, sorted by startBeat and non-overlapping (one
   // terminal is one lane, and WP1's model invariant forbids overlap on a
   // lane). RunTopology walks them with `windowCursor` rather than searching,
   // so a block costs O(1) amortized no matter how long the arrangement is.
   //
   // This replaced a single fadeClipStart/Length pair rebuilt at every clip
   // boundary: carrying the WHOLE lane's windows is what lets one rebuild
   // cover the entire arrangement, so a second clip of the same node is no
   // longer silent and an onset no longer waits for a UI frame.
   int   windowOffset = -1;
   int   numWindows   = 0;
   // The clip's own source node (main.cpp's RebuildAudioTopology resolves it
   // from srcUid the same way it resolves `bufferIndex` above), so
   // RunTopology's retrigger lookahead can call RequestRetrigger() on the
   // right instance before it cooks. Null for a canvas Audio Out terminal -
   // only Arrangement Timeline terminals (numWindows > 0) ever set it.
   AudioNode* sourceNode = nullptr;
   float laneGain     = 1.0f;   // the lane's own gain, multiplied with each window's
                                // (0 for a muted / solo-silenced lane: still scheduled,
                                // so its live waveform keeps drawing)
   // The lane's pan, as per-channel gains (Mixer's equal-power law, unity at
   // centre). Channels past the second are left alone.
   float lanePanL     = 1.0f;
   float lanePanR     = 1.0f;
   // Audio-thread scratch, not configuration: the index of the window the
   // last block left off at. Monotonic forward, reset to 0 whenever the
   // position moves backwards (a seek or a loop wrap).
   mutable int windowCursor = 0;

   // Same PDC role as AudioNode::inputCompensation, one level up: when more
   // than one Audio Out (or one Audio Out among several) feeds the device
   // buffer with different cumulative latency, each terminal but the
   // slowest gets a compensating delay so RunTopology's unconditional sum
   // lands every terminal sample-aligned. Inactive for the common case of
   // one terminal, or several with equal (usually zero) latency.
   //
   // Only actually used when both `capture` and `externalCompensation` are
   // null - a value here is rebuilt fresh every generation and would click on
   // every rebuild if it were the one driving a long-lived terminal.
   CompensationDelay compensation;

   // A main-thread-owned CompensationDelay that outlives this generation, for
   // terminals that have no AudioCaptureRing to hang one off. Timeline clip
   // terminals use it: their PDC state has to survive a rebuild (an edit on an
   // unrelated lane rebuilds everything), and the owning map in main.cpp only
   // drops an entry once CompletedGeneration() has passed the generation that
   // stopped referencing it. Null for every canvas terminal.
   CompensationDelay* externalCompensation = nullptr;

   // Audio-thread scratch for the live waveform (WP8), not configuration:
   // the bucket currently being accumulated and its running min/max. Flushed
   // into AudioEngine::ClipPeaks() when the playhead crosses into the next
   // bucket, so a bucket is only ever published once it is complete.
   mutable uint64_t peakClipId = 0;
   mutable uint64_t peakShape  = 0;
   mutable int      peakBucket = -1;
   mutable float    peakMin    = 0.0f;
   mutable float    peakMax    = 0.0f;
};

// A full audio-thread topology: nodes in a valid topological order (sources
// before consumers - AudioEngine::Process relies on this, it does not sort),
// plus which pooled buffers get summed into the device's own output each
// block (one per connected Audio Out node). Built on the main thread by
// walking the editor graph's audio cables; consumed only by
// AudioEngine::Process/ProcessOffline via RunTopology.
struct AudioTopology
{
   std::vector<AudioTopologyEntry> order;
   std::vector<AudioTerminal> terminalBufferIndices;
   // Backing store for every timeline terminal's window range. One flat array
   // rather than a vector per terminal so the whole schedule is a single
   // allocation, and so terminals index into it (an index survives this
   // vector being moved into the ProcessList; a pointer taken while it was
   // still being filled would not).
   std::vector<ClipWindow> clipWindows;
   int numBuffers = 0; // buffer indices used across `order` span [0, numBuffers)
};

// Owns the audio device connection and runs a DAG of AudioNodes, each over
// its own pooled output buffer, per block. See
// docs/plans/audio/audio-graph-semantics.md §3.
class AudioEngine
{
public:
   static AudioEngine& Instance();

   bool Start(std::string& outError);
   void Stop();

   // Main thread only, before Start(): device/rate/buffer to request from
   // Platform::AudioDeviceOpen. 0 / 0.0 mean "system default" / "device's
   // current setting" - see Platform::AudioDeviceOpen's doc comment.
   void SetRequestedDevice(uint32_t deviceId) { mRequestedDeviceId = deviceId; }
   void SetRequestedSampleRate(double sampleRate) { mRequestedSampleRate = sampleRate; }
   void SetRequestedBufferFrames(int frames) { mRequestedBufferFrames = frames; }

   // Main thread only. Allocates this generation's pooled output buffers
   // (fixed-capacity, kAudioMaxChannels x kAudioMaxBlockFrames each - no
   // audio-thread allocation) and atomically publishes the topology; the
   // previous generation (nodes' pointers AND its buffer pool) is retired
   // (not freed) until the NEXT call to SetTopology, guaranteeing the audio
   // thread has finished any in-flight callback against it before it's
   // deleted - a topology swap happens at user-interaction rate (patching
   // cables), not per block, so this one-generation grace window is cheap
   // and sufficient.
   void SetTopology(AudioTopology topology);

   double SampleRate() const;
   uint64_t XrunCount() const;

   // Called from Platform's kAudioDeviceProcessorOverload listener - a real
   // CoreAudio overload notification, not the wall-clock heuristic Process()
   // uses below. May land on an arbitrary CoreAudio-managed thread, so this
   // must stay atomic-only: no locks, no allocation, nothing that touches
   // main.cpp UI state.
   void NotifyProcessorOverload();

   // True unless the engine believes it should be producing audio
   // (SampleRate() > 0) but no render callback has actually landed recently
   // enough to justify that belief. Deliberately not the same signal as the
   // xrun heuristic in Process(): that one compares the wall-clock gap
   // BETWEEN two successive callbacks, so a dead engine - which produces
   // ZERO further callbacks - gives it nothing to compare and it stays
   // silent forever. This instead compares "now" against the last callback
   // (or Start(), before the first one has arrived) from the outside, on
   // whatever thread polls it - see
   // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md.
   bool IsAlive() const;

   // Fraction of the block's real-time deadline spent inside RunTopology on
   // the last callback (1.0 == used the entire budget before the next block
   // is due). Smoothed with a one-pole filter on the audio thread so the
   // main-thread HUD reads a stable number rather than a spiky per-block one.
   double LastBlockLoad() const;

   // Main thread only: drains MeterRing, pushes any pending ParamMailbox
   // writes queued by node UI this frame. Does no DSP - see the two-object
   // rule in the plan doc. Real INode integration (calling this from
   // CookIfNeeded) is P3's job; the DSPTEST harness calls it directly.
   void PumpMainThread();

   // Main thread, once a frame. While no audio device is open (Start Audio is
   // off) nothing calls ProcessBlock, so note generators and note-driven
   // modulators would sit frozen. This runs only the topology's note-only
   // nodes, in order, in small blocks against the wall-clock-driven transport,
   // so notes and the CV they drive behave the same with audio off. A no-op
   // whenever a device (or an offline render) owns the graph.
   void PumpNoteNodesWithoutDevice();

   // Headless entry point for INFINITE_DSPTEST: runs the current topology
   // over a caller-owned scratch buffer without touching the real device.
   void ProcessOffline(AudioBuffer& buffer);

   // The Samples search panel's audition player (see
   // local-prompts/05-sample-preview-in-search-panel.md). Lives here, not in
   // the node topology, so it is unaffected by the graph, bypass, or the
   // transport, and survives a topology rebuild mid-preview - see Process()
   // mixing it in after RunTopology.
   SamplePreviewPlayer& Preview() { return mPreviewPlayer; }

   // Live arrangement-clip waveform buckets, written by the audio thread in
   // RunTopology's timeline branch and drained on the main thread by the
   // arrangement panel. Present whether or not a device is open: an offline
   // take fills it through ProcessOffline exactly the same way.
   ClipPeakRing& ClipPeaks() { return mClipPeaks; }

   // Main thread only. The generation number that will be attached to the
   // NEXT SetTopology() call - i.e. the generation of whatever topology is
   // mCurrent right now. A node being retired from the graph (main.cpp's
   // gRetiredNodes) can still be referenced by mCurrent up until the
   // following RebuildAudioTopology()/SetTopology() publishes a topology that
   // excludes it, so callers must read this BEFORE that publish and treat the
   // result as "the last generation that might still reach this node".
   uint64_t CurrentGeneration() const { return mPublishedGeneration.load(std::memory_order_relaxed); }

   // Main thread only. The generation number of the last topology the audio
   // thread has fully finished a RunTopology() pass over. A retired node
   // recorded against generation G is only safe to actually destroy once this
   // returns > G - that's the audio thread's own confirmation that it has
   // moved on to a topology built after the node was removed from gNodes, not
   // a guess based on how much wall-clock time has passed. See gRetiredNodes'
   // drain in main.cpp for why a one-video-frame heuristic isn't enough here
   // (unlike the GL-texture retirement it shares a mechanism with).
   uint64_t CompletedGeneration() const { return mCompletedGeneration.load(std::memory_order_relaxed); }

private:
   AudioEngine() = default;

   static void RenderThunk(float** buffers, int numChannels, int numFrames, void* userData);
   void Process(float** buffers, int numChannels, int numFrames);

   // One pooled output buffer: fixed-capacity storage, sliced to a block's
   // actual frame/channel count (always <= the caps above) via View().
   // Channel pointers are computed once at Allocate() and stay stable for
   // the buffer's whole lifetime - safe to hand out AudioBuffer views built
   // from them at any point after.
   struct PooledBuffer
   {
      std::vector<float> storage;
      float* channelPtr[kAudioMaxChannels] = {};

      void Allocate()
      {
         storage.assign((size_t)kAudioMaxChannels * (size_t)kAudioMaxBlockFrames, 0.0f);
         for (int ch = 0; ch < kAudioMaxChannels; ch++)
            channelPtr[ch] = storage.data() + (size_t)ch * (size_t)kAudioMaxBlockFrames;
      }

      AudioBuffer View(int numFrames, int numChannels)
      {
         AudioBuffer b;
         b.channels = channelPtr;
         b.numChannels = numChannels;
         b.numFrames = numFrames;
         return b;
      }
   };

   struct ProcessList
   {
      AudioTopology topology;
      std::vector<PooledBuffer> buffers;
      uint64_t generation = 0;
   };
   std::atomic<ProcessList*> mCurrent { nullptr };
   // Superseded lists awaiting the audio thread's confirmation - see
   // DrainRetired(). Main thread only.
   std::vector<ProcessList*> mRetiring;
   std::atomic<bool> mDeviceOpen { false };
   double mPumpFrames = 0.0;   // main thread: fractional frames owed to the device-less note pump
   double mPumpLastMs = -1.0;
   void DrainRetired();

   // Bumped once per SetTopology() call (main thread only) - see
   // CurrentGeneration(). Started at 0 so the first published topology is
   // generation 1 and 0 can mean "before anything was ever published".
   std::atomic<uint64_t> mPublishedGeneration { 0 };
   // Written by Process() (real audio thread only) right after it finishes
   // RunTopology() over `list` - see CompletedGeneration().
   std::atomic<uint64_t> mCompletedGeneration { 0 };

   // Shared by Process() (real device callback) and ProcessOffline() (tests):
   // walks `list`'s topology in order, handing each node its declared input
   // buffers and its own output buffer, then sums the terminal buffers into
   // `deviceBuffer`. A null `list` (nothing published yet) or a topology with
   // no terminals (no audio reaches an Audio Out) just silences deviceBuffer.
   void RunTopology(ProcessList* list, AudioBuffer& deviceBuffer);

   // Audio-thread-only (RunTopology never runs concurrently with itself):
   // the beat the previous block ended on, so this block can tell a genuine
   // discontinuity (a timeline scrub/seek, a loop wrap, a fresh Play landing
   // mid-clip) apart from ordinary continuous playback - see RunTopology's
   // exact-seek lookahead. Sentinel -1e18 means "no previous block yet",
   // which is itself treated as a discontinuity so the very first block
   // after Play still snaps a Sample to its exact position.
   double mLastBlockEndBeat = -1e18;

   std::atomic<double> mSampleRate { 0.0 };
   std::atomic<uint64_t> mXrunCount { 0 };
   std::atomic<double> mLastCallbackMs { -1.0 };
   std::atomic<double> mLastBlockLoad { 0.0 };
   // Set in Start(), read by IsAlive() as the "no callback yet" baseline -
   // without this, an engine that fails to ever produce a first callback
   // (mLastCallbackMs staying at its -1.0 sentinel forever) would read as
   // permanently alive instead of dead.
   std::atomic<double> mStartedAtMs { -1.0 };

   uint32_t mRequestedDeviceId = 0;
   double mRequestedSampleRate = 0.0;
   int mRequestedBufferFrames = 0;
   ClipPeakRing mClipPeaks;

   SamplePreviewPlayer mPreviewPlayer;
};

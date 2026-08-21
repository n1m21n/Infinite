#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "AudioBuffer.h"
#include "AudioCaptureRing.h"
#include "AudioNode.h"
#include "SamplePreviewPlayer.h"

// Ceilings shared by the topology builder (main.cpp's RebuildAudioTopology)
// and the engine's buffer pool. kAudioMaxNodeInputs is Mixer's 8-in ceiling
// (docs/plans/audio/audio-graph-semantics.md §2); kAudioMaxBlockFrames/
// kAudioMaxChannels bound the fixed-capacity pooled buffers so allocation
// happens only when a topology is built (main thread), never inside a real
// device callback - generous enough that no real block size/channel count
// should ever exceed them. A block that somehow does gets truncated to the
// cap rather than overrunning a pooled buffer - see AudioEngine::RunTopology.
constexpr int kAudioMaxNodeInputs = 8;
constexpr int kAudioMaxBlockFrames = 4096;
constexpr int kAudioMaxChannels = 8;

// One node's place in a topological ordering: which pooled buffer(s) it
// reads (by index into the owning AudioTopology's buffer pool; -1 = that pin
// is unconnected, read as silence) and which pooled buffer it writes its own
// output into. See docs/plans/audio/audio-graph-semantics.md §3.
struct AudioTopologyEntry
{
   AudioNode* node = nullptr;
   int inputBufferIndices[kAudioMaxNodeInputs] = { -1, -1, -1, -1, -1, -1, -1, -1 };
   int numInputs = 0;
   int outputBufferIndex = -1;
};

// One connected Audio Out: the pooled buffer its source writes into, and
// (unconditionally) that Audio Out's own capture ring - RunTopology writes
// into it whenever the ring's own `enabled` flag is set, so starting/
// stopping a recording needs no topology rebuild.
struct AudioTerminal
{
   int bufferIndex = -1;
   AudioCaptureRing* capture = nullptr;
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

   // Headless entry point for INFINITE_DSPTEST: runs the current topology
   // over a caller-owned scratch buffer without touching the real device.
   void ProcessOffline(AudioBuffer& buffer);

   // The Samples search panel's audition player (see
   // local-prompts/05-sample-preview-in-search-panel.md). Lives here, not in
   // the node topology, so it is unaffected by the graph, bypass, or the
   // transport, and survives a topology rebuild mid-preview - see Process()
   // mixing it in after RunTopology.
   SamplePreviewPlayer& Preview() { return mPreviewPlayer; }

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
   };
   std::atomic<ProcessList*> mCurrent { nullptr };
   ProcessList* mRetiring = nullptr; // freed on the NEXT SetTopology call

   // Shared by Process() (real device callback) and ProcessOffline() (tests):
   // walks `list`'s topology in order, handing each node its declared input
   // buffers and its own output buffer, then sums the terminal buffers into
   // `deviceBuffer`. A null `list` (nothing published yet) or a topology with
   // no terminals (no audio reaches an Audio Out) just silences deviceBuffer.
   void RunTopology(ProcessList* list, AudioBuffer& deviceBuffer);

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

   SamplePreviewPlayer mPreviewPlayer;
};

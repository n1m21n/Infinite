#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "AudioBuffer.h"
#include "AudioCaptureRing.h"
#include "AudioNode.h"
#include "CompensationDelay.h"
#include "SamplePreviewPlayer.h"
#include "Metronome.h"

// Ceilings shared by the topology builder (main.cpp's RebuildAudioTopology)
// and the engine's buffer pool. kAudioMaxNodeInputs is Mixer's 8-in ceiling
// (docs/plans/audio/audio-graph-semantics.md §2); kAudioMaxBlockFrames/
// kAudioMaxChannels bound the fixed-capacity pooled buffers so allocation
// happens only when a topology is built (main thread), never inside a real
// device callback - generous enough that no real block size/channel count
// should ever exceed them. A block that somehow does gets truncated to the
// cap rather than overrunning a pooled buffer - see AudioEngine::RunTopology.
constexpr int kAudioMaxNodeInputs = 16; // Turbo: 16 for SuperMixer
constexpr int kAudioMaxBlockFrames = 4096;
constexpr int kAudioMaxChannels = 8;

// One node's place in a topological ordering: which pooled buffer(s) it
// reads (by index into the owning AudioTopology's buffer pool; -1 = that pin
// is unconnected, read as silence) and which pooled buffer it writes its own
// output into. See docs/plans/audio/audio-graph-semantics.md §3.
struct AudioTopologyEntry
{
   AudioNode* node = nullptr;
   int inputBufferIndices[kAudioMaxNodeInputs] = { -1, -1, -1, -1, -1, -1, -1, -1,
                                                   -1, -1, -1, -1, -1, -1, -1, -1 };
   int numInputs = 0;
   int outputBufferIndex = -1;

   // Plugin/effect delay compensation (PDC): per input pin, the
   // CompensationDelay that pin's source branch needs so every pin merging
   // into this node arrives sample-aligned - RebuildAudioTopology (main.cpp)
   // computes each branch's cumulative latency and, for a multi-input node
   // whose connected pins carry different cumulative latencies, prepares the
   // shallower pins' delay to make up the difference; a pin whose branch is
   // already the slowest (or the node has only one connected pin) gets an
   // inactive (0-sample, unallocated) one. RunTopology applies these before
   // handing the node its inputs - see its comment.
   CompensationDelay inputCompensation[kAudioMaxNodeInputs];
};

// One connected Audio Out: the pooled buffer its source writes into, and
// (unconditionally) that Audio Out's own capture ring - RunTopology writes
// into it whenever the ring's own `enabled` flag is set, so starting/
// stopping a recording needs no topology rebuild.
struct AudioTerminal
{
   int bufferIndex = -1;
   AudioCaptureRing* capture = nullptr;

   // Same PDC role as AudioTopologyEntry::inputCompensation, one level up:
   // when more than one Audio Out (or one Audio Out among several) feeds the
   // device buffer with different cumulative latency, each terminal but the
   // slowest gets a compensating delay here so RunTopology's unconditional
   // sum lands every terminal sample-aligned. Inactive for the common case
   // of one terminal, or several with equal (usually zero) latency.
   CompensationDelay compensation;

   // Turbo: a "live" Audio Out (input monitoring) stays audible while the
   // Arrangement Timeline owns the output; every other canvas terminal is
   // muted then (its capture ring still records).
   bool live = false;

   // Turbo 0.45 (upstream R477): false when another canvas terminal already
   // sums this same pooled buffer into the device (one source wired to an
   // Audio Out and an Output, or to two Audio Outs). The device mix counts a
   // source once; this terminal still feeds its own capture ring.
   bool mixToDevice = true;
};

// Marks duplicate terminals (same pooled buffer) so the device mix counts a
// source once. A "live" terminal wins over a non-live twin, so input
// monitoring keeps working in Timeline mode. Call once the list is complete.
inline void MarkDuplicateDeviceTerminals(std::vector<AudioTerminal>& terminals)
{
   for (size_t i = 0; i < terminals.size(); i++)
      terminals[i].mixToDevice = true;
   for (size_t i = 0; i < terminals.size(); i++)
   {
      if (!terminals[i].mixToDevice)
         continue;
      size_t keep = i;
      for (size_t k = i + 1; k < terminals.size(); k++)
         if (terminals[k].bufferIndex == terminals[i].bufferIndex && terminals[k].live && !terminals[keep].live)
            keep = k;
      for (size_t k = i; k < terminals.size(); k++)
         if (k != keep && terminals[k].bufferIndex == terminals[i].bufferIndex)
            terminals[k].mixToDevice = false;
   }
}

// Turbo 0.43 (Arrangement Timeline, after upstream's ClipWindow): one clip's
// slot in musical time, as the audio thread sees it. Beats, not seconds, so
// a tempo change keeps every clip on its bar.
struct ArrangeClipWindow
{
   double startBeat = 0.0;
   double endBeat = 0.0;     // exclusive
   double fadeInBeats = 0.0;
   double fadeOutBeats = 0.0;
   float gain = 1.0f;        // clip gain, linear
   float panL = 1.0f;        // clip pan as per-channel gains
   float panR = 1.0f;
   bool abutsPrev = false;   // no declick ramp across an exact join
   bool abutsNext = false;
   // Audio Sample (file dropped on the timeline): the source is locked to
   // the timeline - on entering the window (or after a seek) the node is
   // told where in its file to play from (AudioNode::ClipSeek).
   bool seekSource = false;
   double sourceOffsetSeconds = 0.0;
   // Turbo 0.43.1: Sample tempo sync and pitch (upstream's ClipWindow).
   //   source seconds at beat b = offset + (b - startBeat) * 60 / effBpm
   //   effBpm = syncToTempo ? sampleBpm : project tempo
   bool syncToTempo = false;
   float sampleBpm = 120.0f;
   float pitch = 0.0f;
   // Live waveform: which clip this is, and the shape it was measured under.
   uint64_t clipId = 0;
   uint64_t shape = 0;
};

// Turbo 0.43.1: one live-waveform measurement, audio -> main (SPSC ring).
struct ArrangePeak
{
   uint64_t clipId = 0;
   uint64_t shape = 0;
   int bucket = 0;   // 1/16 beat from the clip start
   float peak = 0.0f;
};

// One (lane, source node) pair: the source's pooled buffer, gated and
// shaped by its windows (sorted, non-overlapping) and summed into the device
// buffer with the lane's live gain/pan (AudioEngine::SetArrangeLaneMix).
struct ArrangeTerminal
{
   int bufferIndex = -1;
   AudioNode* source = nullptr;
   int windowOffset = 0;
   int numWindows = 0;
   int laneSlot = 0;          // index into the engine's lane mix table
   mutable int cursor = 0;    // audio thread scratch
   mutable int activeWindow = -1;
   mutable uint32_t seenSeekSerial = 0;
   mutable int peakWindow = -1;   // live waveform accumulation
   mutable int peakBucket = -1;
   mutable float peakValue = 0.0f;
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
   // Turbo: Arrangement Timeline terminals (only built in Timeline mode).
   std::vector<ArrangeClipWindow> arrangeWindows;
   std::vector<ArrangeTerminal> arrangeTerminals;
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
   void SetRequestedInputDevice(uint32_t deviceId) { mRequestedInputDeviceId = deviceId; }
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
   // Increments after every successful device start. Plugin hosts use this
   // to re-run prepareToPlay even when a stop/start negotiates the same rate
   // and block size: many VST3 instruments keep device-owned state that is
   // invalid after the stream has been restarted.
   uint64_t StartGeneration() const { return mStartGeneration.load(std::memory_order_relaxed); }

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

   // Turbo: the "direct" metronome - mixed into the device output after the
   // graph (like the preview), so it needs no cable and never shows up in an
   // Audio Out recording. Main thread sets, audio thread reads.
   void SetDirectClick(bool enabled, float volume, bool accent)
   {
      mDirectClickVolume.store(volume, std::memory_order_relaxed);
      mDirectClickAccent.store(accent, std::memory_order_relaxed);
      mDirectClick.store(enabled, std::memory_order_relaxed);
   }

   // Turbo 0.43: Arrangement Timeline audio. In Timeline mode the canvas
   // Audio Outs are muted (live ones excepted) and the arrangement
   // terminals play; lane gain/pan live here so a fader drag never needs a
   // topology rebuild. Main thread sets, audio thread reads.
   static constexpr int kMaxArrangeLanes = 256;
   void SetTimelineMode(bool on) { mTimelineMode.store(on, std::memory_order_relaxed); }
   bool TimelineMode() const { return mTimelineMode.load(std::memory_order_relaxed); }
   void SetArrangeLaneMix(int slot, float gain, float panL, float panR)
   {
      if (slot < 0 || slot >= kMaxArrangeLanes)
         return;
      mLaneGain[slot].store(gain, std::memory_order_relaxed);
      mLanePanL[slot].store(panL, std::memory_order_relaxed);
      mLanePanR[slot].store(panR, std::memory_order_relaxed);
   }
   // Peak of each lane's last block, for the panel's meters.
   float ArrangeLanePeak(int slot) const
   {
      return (slot >= 0 && slot < kMaxArrangeLanes) ? mLanePeak[slot].load(std::memory_order_relaxed) : 0.0f;
   }
   // Main thread: drains the live clip-waveform measurements.
   template <class Fn>
   void DrainArrangePeaks(Fn&& fn)
   {
      uint32_t head = mPeakHead.load(std::memory_order_relaxed);
      const uint32_t tail = mPeakTail.load(std::memory_order_acquire);
      while (head != tail)
      {
         fn(mPeakRing[head % kPeakRingSize]);
         head++;
      }
      mPeakHead.store(head, std::memory_order_release);
   }

   // Turbo 0.43.1: offline render. While set, the device callback outputs
   // silence and leaves the clock alone; the main thread drives the clock
   // and the graph with RenderOfflineBlock.
   void SetOfflineRender(bool on);
   bool OfflineRender() const { return mOffline.load(std::memory_order_acquire); }
   void RenderOfflineBlock(AudioBuffer& buffer); // advances the transport by buffer.numFrames

private:
   AudioEngine() = default;

   struct ProcessList;
   void RunArrangeLookahead(ProcessList* list, int numFrames);
   void MixArrangeTerminals(ProcessList* list, AudioBuffer& deviceBuffer, int numFrames, int numChannels);

   std::atomic<bool> mTimelineMode { false };
   std::atomic<bool> mOffline { false };
   std::atomic<int> mInProcess { 0 };
   static constexpr uint32_t kPeakRingSize = 8192;
   ArrangePeak mPeakRing[kPeakRingSize];
   std::atomic<uint32_t> mPeakHead { 0 };
   std::atomic<uint32_t> mPeakTail { 0 };
   void PushPeak(const ArrangePeak& p)
   {
      const uint32_t tail = mPeakTail.load(std::memory_order_relaxed);
      if (tail - mPeakHead.load(std::memory_order_acquire) >= kPeakRingSize)
         return; // full: drop (the waveform just fills in on the next pass)
      mPeakRing[tail % kPeakRingSize] = p;
      mPeakTail.store(tail + 1, std::memory_order_release);
   }
   std::atomic<float> mLaneGain[kMaxArrangeLanes] = {};
   std::atomic<float> mLanePanL[kMaxArrangeLanes] = {};
   std::atomic<float> mLanePanR[kMaxArrangeLanes] = {};
   std::atomic<float> mLanePeak[kMaxArrangeLanes] = {};

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
   std::atomic<uint64_t> mStartGeneration { 0 };
   std::atomic<uint64_t> mXrunCount { 0 };
   std::atomic<double> mLastCallbackMs { -1.0 };
   std::atomic<double> mLastBlockLoad { 0.0 };
   // Set in Start(), read by IsAlive() as the "no callback yet" baseline -
   // without this, an engine that fails to ever produce a first callback
   // (mLastCallbackMs staying at its -1.0 sentinel forever) would read as
   // permanently alive instead of dead.
   std::atomic<double> mStartedAtMs { -1.0 };

   uint32_t mRequestedDeviceId = 0;
   uint32_t mRequestedInputDeviceId = 0;
   double mRequestedSampleRate = 0.0;
   int mRequestedBufferFrames = 0;

   SamplePreviewPlayer mPreviewPlayer;

   std::atomic<bool> mDirectClick { false };
   std::atomic<float> mDirectClickVolume { 0.5f };
   std::atomic<bool> mDirectClickAccent { true };
   MetronomeClick mDirectClickGen; // audio thread only
};

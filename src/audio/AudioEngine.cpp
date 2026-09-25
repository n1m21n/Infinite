#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "core/Transport.h"
#include "platform/Platform.h"

#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace
{
   double NowMs()
   {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
   }

   // Callback-gap heuristic: a wall-clock gap between successive Process()
   // entries past this multiple of the block period. Reported as
   // CallbackGapCount() for information only - it is NOT an xrun (the OS
   // can deliver a callback late and still meet the device deadline from
   // its own buffering), so it never feeds XrunCount(). Real xruns are the
   // deadline and OS counters; see AudioEngine.h's XrunCounts.
   constexpr double kXrunGapMultiplier = 1.5;

   // One-pole smoothing for the load readout - RunTopology's per-block cost
   // is noisy (cache effects, scheduler jitter), and the HUD wants a number
   // that doesn't flicker every 10ms.
   constexpr double kLoadSmoothing = 0.2;

   // How long IsAlive() will tolerate silence from a "should be running"
   // engine before calling it dead. Must clear comfortably past any real
   // block period (even the largest, 4096 frames @ 44.1kHz, is ~93ms) with
   // margin for scheduler jitter and the polling interval itself (main.cpp
   // polls once a frame, not continuously), while staying short enough that
   // a genuinely stuck engine is caught in a fraction of a second rather
   // than sitting silent for a while first.
   constexpr double kDeadEngineMs = 750.0;
}

AudioEngine& AudioEngine::Instance()
{
   static AudioEngine sInstance;
   return sInstance;
}

AudioEngine::AudioEngine()
{
   // One-time wiring of the scratch channel-pointer tables into the scratch
   // storage arrays - see AudioEngine.h's comment on why these are plain
   // members now instead of `static thread_local` locals lazily inited on
   // first RunTopology call.
   for (int i = 0; i < kAudioMaxNodeInputs; i++)
      for (int ch = 0; ch < kAudioMaxChannels; ch++)
         mCompScratchChannels[i][ch] = mCompScratch[i][ch];
   for (int ch = 0; ch < kAudioMaxChannels; ch++)
      mTerminalScratchChannels[ch] = mTerminalScratch[ch];
}

bool AudioEngine::Start(std::string& outError)
{
   double sampleRate = 0.0;
   // Raised BEFORE the device opens: the first callback can fire before
   // AudioDeviceOpen returns, and SetTopology's retire drain must never treat
   // that window as "no audio thread exists".
   mDeviceOpen.store(true, std::memory_order_release);
   if (!Platform::AudioDeviceOpen(&AudioEngine::RenderThunk, this, sampleRate, outError,
                                  mRequestedDeviceId, mRequestedSampleRate, mRequestedBufferFrames))
   {
      mDeviceOpen.store(false, std::memory_order_release);
      return false;
   }
   mSampleRate.store(sampleRate, std::memory_order_relaxed);
   mStartedAtMs.store(NowMs(), std::memory_order_relaxed);
   mPreviewPlayer.PrepareToPlay(sampleRate);
   Transport::Instance().NotifyAudioEngineStarted(sampleRate);
   return true;
}

void AudioEngine::Stop()
{
   Platform::AudioDeviceClose();
   mSampleRate.store(0.0, std::memory_order_relaxed);
   // AudioDeviceClose has returned, so no callback can still hold a list.
   mDeviceOpen.store(false, std::memory_order_release);
   DrainRetired();

   // Reset xrun-detection state here, on Stop(), not on the next Start():
   // Platform::AudioDeviceClose() has already returned, so Process() cannot
   // be mid-callback racing this write - "not running" starts exactly here.
   // mLastCallbackMs back to its -1.0 "no previous callback" sentinel means
   // the first callback after the next Start() has nothing stale to compare
   // its gap against, so a restart can no longer trip kXrunGapMultiplier on
   // its own (bug 3's false-positive xrun on restart). The counters reset to
   // 0 alongside it, a deliberate choice, not an oversight: the status-bar
   // readout (main.cpp:17521-17548-ish, "xruns=N") exists to answer "did
   // *this run* introduce dropouts", which only a per-run counter can answer
   // - a cumulative-since-process-start count can't distinguish "this
   // restart is clean" from "an earlier run had one and nobody's looked
   // since". If that ever needs to become "since the app launched" instead,
   // this reset is the one place to remove, not something to leave debatable
   // at every call site. All three counters reset together so XrunCount()
   // and its parts always describe the same run.
   mLastCallbackMs.store(-1.0, std::memory_order_relaxed);
   mXrunDeadline.store(0, std::memory_order_relaxed);
   mXrunOs.store(0, std::memory_order_relaxed);
   mCallbackGaps.store(0, std::memory_order_relaxed);

   Transport::Instance().NotifyAudioEngineStopped();
}

void AudioEngine::SetTopology(AudioTopology topology)
{
   ProcessList* fresh = new ProcessList();
   fresh->topology = std::move(topology);
   fresh->buffers.resize(fresh->topology.numBuffers);
   for (PooledBuffer& b : fresh->buffers) // main thread only - never inside Process()
      b.Allocate();
   fresh->generation = mPublishedGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

   ProcessList* old = mCurrent.exchange(fresh, std::memory_order_acq_rel);
   if (old != nullptr)
      mRetiring.push_back(old);
   DrainRetired();
}

// A superseded ProcessList is only freed once the audio thread has COMPLETED a
// RunTopology pass over a strictly newer generation (callbacks are serial, so
// that pass started after every pass that could still hold `list`), or when no
// device is open at all (offline renders run on this thread). The old rule -
// "delete the list retired one SetTopology ago" - assumed at least one audio
// callback between two publishes; two rebuilds inside one block period (paste
// followed by the clone's decode finishing, or per-frame rebuilds during a
// drag with a large buffer size) freed the list mid-ProcessBlock and crashed
// writing into a nulled channel pointer.
void AudioEngine::DrainRetired()
{
   const bool deviceOpen = mDeviceOpen.load(std::memory_order_acquire);
   const uint64_t completed = mCompletedGeneration.load(std::memory_order_acquire);
   size_t keep = 0;
   for (size_t i = 0; i < mRetiring.size(); i++)
   {
      ProcessList* list = mRetiring[i];
      if (!deviceOpen || completed > list->generation)
         delete list;
      else
         mRetiring[keep++] = list;
   }
   mRetiring.resize(keep);
}

double AudioEngine::SampleRate() const
{
   return mSampleRate.load(std::memory_order_relaxed);
}

uint64_t AudioEngine::XrunCount() const
{
   return Xruns().Total();
}

uint64_t AudioEngine::XrunDeadlineCount() const
{
   return mXrunDeadline.load(std::memory_order_relaxed);
}

uint64_t AudioEngine::XrunOsCount() const
{
   return mXrunOs.load(std::memory_order_relaxed);
}

uint64_t AudioEngine::CallbackGapCount() const
{
   return mCallbackGaps.load(std::memory_order_relaxed);
}

AudioEngine::XrunCounts AudioEngine::Xruns() const
{
   XrunCounts c;
   c.deadline = mXrunDeadline.load(std::memory_order_relaxed);
   c.os = mXrunOs.load(std::memory_order_relaxed);
   c.gaps = mCallbackGaps.load(std::memory_order_relaxed);
   return c;
}

void AudioEngine::NotifyProcessorOverload()
{
   // The device's own report. Deadline misses are counted separately in
   // Process(); the two overlap when our render ran long, but each also
   // catches what the other can't (an OS-side overload with a fast render,
   // or a slow render the device happened to absorb), so both count.
   mXrunOs.fetch_add(1, std::memory_order_relaxed);
}

// Trampoline for Platform.mm's kAudioDeviceProcessorOverload listener - see
// its own comment on why it can't just #include AudioEngine.h and call the
// method directly (AudioBuffer.h name collision with CoreAudioTypes.h).
extern "C" void AudioEngine_NotifyProcessorOverload(void* engineInstance)
{
   static_cast<AudioEngine*>(engineInstance)->NotifyProcessorOverload();
}

double AudioEngine::LastBlockLoad() const
{
   return mLastBlockLoad.load(std::memory_order_relaxed);
}

bool AudioEngine::IsAlive() const
{
   const double sampleRate = mSampleRate.load(std::memory_order_relaxed);
   if (sampleRate <= 0.0)
      return true; // not supposed to be running at all - "off", not "dead"

   const double lastMs = mLastCallbackMs.load(std::memory_order_relaxed);
   const double baselineMs = (lastMs >= 0.0) ? lastMs : mStartedAtMs.load(std::memory_order_relaxed);
   if (baselineMs < 0.0)
      return true; // Start() hasn't actually run yet by this reading - nothing to judge
   return (NowMs() - baselineMs) < kDeadEngineMs;
}

void AudioEngine::PumpMainThread()
{
   // No DSP here by design - see the two-object rule. Real drain/push of
   // per-node MeterRing/ParamMailbox instances happens once P3 wires actual
   // audio-backed INodes through this call.

   // Bookkeeping, not DSP: frees whatever preview buffer the audio thread
   // retired (superseded by a newer Play(), or Stop()'s buffer once a new
   // one lands) rather than leaving it to leak.
   mPreviewPlayer.DrainRetired();
   DrainRetired();
}

void AudioEngine::PumpNoteNodesWithoutDevice()
{
   const double nowMs = NowMs();
   const double lastMs = mPumpLastMs;
   mPumpLastMs = nowMs;
   if (mDeviceOpen.load(std::memory_order_acquire) || Transport::Instance().IsOfflineMode())
   {
      mPumpFrames = 0.0;
      return;
   }
   ProcessList* list = mCurrent.load(std::memory_order_acquire);
   if (list == nullptr || lastMs < 0.0)
      return;

   // No device means no callback thread, so this thread owns the list and its
   // pooled buffers outright for the duration of the call - including this
   // generation's note wiring, which RebuildAudioTopology only described.
   // Idempotent per generation (see ApplyNoteWiringIfNew's guard), so this
   // costs nothing on the ticks that aren't the first one after a rebuild.
   ApplyNoteWiringIfNew(list);

   constexpr double kPumpRate = 48000.0;
   constexpr int kPumpBlock = 256;
   const double dtSeconds = std::min(0.1, std::max(0.0, (nowMs - lastMs) * 0.001));
   mPumpFrames += dtSeconds * kPumpRate;
   const int blocks = std::min(32, (int)(mPumpFrames / kPumpBlock));
   mPumpFrames -= (double)blocks * kPumpBlock;
   if (blocks <= 0)
      return;

   bool prepared = false;
   for (AudioTopologyEntry& entry : list->topology.order)
   {
      if (!entry.noteOnly || entry.node->preparedForSampleRate == kPumpRate)
         continue;
      entry.node->PrepareToPlay(kPumpRate, kPumpBlock);
      entry.node->preparedForSampleRate = kPumpRate;
      prepared = true;
   }
   (void)prepared;

   for (int b = 0; b < blocks; b++)
   {
      for (AudioTopologyEntry& entry : list->topology.order)
      {
         if (!entry.noteOnly)
            continue;
         const AudioBuffer* inputPtrs[kAudioMaxNodeInputs] = {};
         const int numOuts = std::clamp(entry.numOutputs, 1, kAudioMaxNodeOutputs);
         AudioBuffer outputViews[kAudioMaxNodeOutputs];
         AudioBuffer* outputPtrs[kAudioMaxNodeOutputs];
         for (int o = 0; o < numOuts; o++)
         {
            int outIdx = entry.outputBufferIndices[o];
            if (outIdx < 0 && o == 0 && entry.outputBufferIndex >= 0)
               outIdx = entry.outputBufferIndex;
            if (outIdx >= 0 && outIdx < list->topology.numBuffers)
            {
               outputViews[o] = list->buffers[outIdx].View(kPumpBlock, 2);
               outputPtrs[o] = &outputViews[o];
            }
            else
               outputPtrs[o] = nullptr;
         }
         entry.node->ProcessBlockMulti(inputPtrs, std::min(entry.numInputs, kAudioMaxNodeInputs), outputPtrs, numOuts);
      }
   }

   // Consumers that need a device (synths) never drain their cursors here;
   // don't let them back the ring up for the ones that do.
   for (AudioTopologyEntry& entry : list->topology.order)
   {
      if (!entry.noteOnly)
         continue;
      for (int slot = 0; slot < 8; slot++)
         if (NoteEventQueue* q = entry.node->NoteOutbox(slot))
            q->TrimLaggingConsumers(NoteEventQueue::kCapacity / 2);
   }
}

void AudioEngine::ApplyNoteWiringIfNew(ProcessList* list)
{
   if (list == nullptr)
      return;
   if (mAppliedNoteGeneration.load(std::memory_order_relaxed) == list->generation)
      return;

   AudioTopology& topo = list->topology;

   // One pass per outbox: decide every one of this generation's cursors'
   // starting heads, THEN adopt them all in a single AdoptConsumers() call -
   // never interleaved, since AdoptConsumers overwrites the very cursor
   // table the carry-over reads below depend on.
   for (const NoteOutboxPlan& plan : topo.noteOutboxes)
   {
      NoteEventQueue* queue = plan.queue;
      if (queue == nullptr)
         continue;
      const int count = std::min(plan.consumerCount, NoteEventQueue::kMaxConsumers);
      const size_t tail = queue->Tail();

      size_t heads[NoteEventQueue::kMaxConsumers];
      for (int i = 0; i < count; i++)
         heads[i] = tail; // default: a freshly wired consumer sees only new events

      for (const NoteWire& wire : topo.noteWires)
      {
         if (wire.inbox != queue || wire.cursor < 0 || wire.cursor >= count)
            continue;
         AudioNode* consumer = wire.consumer;
         if (consumer == nullptr || wire.slot < 0 || wire.slot >= AudioNode::kMaxNoteSlots)
            continue;
         // Carry over only if this consumer's slot is ALREADY running this
         // exact producer - keyed on what it last actually applied
         // (AudioNode::appliedInbox/appliedCursor), not on the previous
         // topology's described wiring, so a skipped generation (two
         // SetTopology publishes inside one block) still carries over
         // correctly.
         if (consumer->appliedInbox[wire.slot] == wire.inbox && consumer->appliedCursor[wire.slot] >= 0)
            heads[wire.cursor] = queue->CursorHead(consumer->appliedCursor[wire.slot]);
      }

      queue->AdoptConsumers(count, heads);
   }

   // Every wire, including unconnected ones (inbox == nullptr, cursor == -1)
   // - ApplyNoteInbox forwards those through exactly like a connected one, so
   // a slot disconnected this generation gets cleared rather than left
   // holding a stale pointer from a producer that may no longer even be in
   // `order`.
   for (const NoteWire& wire : topo.noteWires)
   {
      if (wire.consumer != nullptr)
         wire.consumer->ApplyNoteInbox(wire.slot, wire.inbox, wire.cursor);
   }

   mAppliedNoteGeneration.store(list->generation, std::memory_order_relaxed);
}

void AudioEngine::ApplyNoteWiringIfNoDevice()
{
   // With a device open, the audio thread applies its own generation's
   // wiring from inside RunTopology - calling this here too would race it
   // for nothing (ApplyNoteWiringIfNew's generation guard makes a second
   // call harmless, but there is no reason to pay for it, and the whole
   // point of this function is to be safe ONLY in the no-device case).
   if (mDeviceOpen.load(std::memory_order_acquire))
      return;
   ApplyNoteWiringIfNew(mCurrent.load(std::memory_order_acquire));
}

void AudioEngine::ProcessOffline(AudioBuffer& buffer)
{
   Transport::Instance().BeginOfflineAudioBlock(buffer.numFrames);
   ProcessList* list = mCurrent.load(std::memory_order_acquire);
   RunTopology(list, buffer);
   Transport::Instance().EndOfflineAudioBlock();
}

void AudioEngine::RunTopology(ProcessList* list, AudioBuffer& deviceBuffer, double* outStageMs)
{
   if (outStageMs != nullptr)
   {
      for (int s = 0; s < kAudioStageCount; s++)
         outStageMs[s] = 0.0;
   }
   // Apply this generation's note wiring, on THIS thread, before anything
   // below cooks a single node - covers both the real device callback and
   // ProcessOffline. See ApplyNoteWiringIfNew's comment for why this,
   // rather than RebuildAudioTopology, is what actually mutates cursors/
   // inboxes now.
   ApplyNoteWiringIfNew(list);

   // Truncate to the pool's fixed capacity rather than overrun a pooled
   // buffer - see the kAudioMax* comment in AudioEngine.h. Silences the
   // buffer in full first so a truncated tail never carries stale/garbage
   // samples out to the device.
   for (int ch = 0; ch < deviceBuffer.numChannels; ch++)
      for (int i = 0; i < deviceBuffer.numFrames; i++)
         deviceBuffer.channels[ch][i] = 0.0f;

   // Note: gated on `order` being empty, not `terminalBufferIndices` - a
   // note-only chain (a generator feeding a Note Capturer/Router/etc with no
   // Audio Out anywhere downstream) has entries in `order` but never adds a
   // terminal, and still needs ProcessBlock called every callback so its
   // beat-synced generators actually free-run instead of sitting frozen
   // until some unrelated Audio-Out-reaching chain also exists.
   if (list == nullptr || list->topology.order.empty())
      return;

   const int numFrames = std::min(deviceBuffer.numFrames, kAudioMaxBlockFrames);
   const int numChannels = std::min(deviceBuffer.numChannels, kAudioMaxChannels);

   // Arrangement Timeline retrigger: seek each clip's own node BEFORE it
   // cooks this block. The per-terminal envelope loop further down only
   // gates/fades whatever the node already wrote to its output buffer this
   // block - it never touches the node's playback position - so a retrigger
   // has to land here, ahead of the `order` cook loop below, or it would
   // always be one block late.
   //
   // A window's onset only ever needs to fire once: the very first block
   // whose [blockStartBeat, blockEndBeat) span contains its startBeat. Once
   // blockStartBeat has advanced past it, the same check is naturally false
   // on every later block for that window - no separate "already fired" flag
   // needed. A loop wrap or a fresh Play from inside the window's own start
   // revisits that same span and correctly retriggers again.
   double runSampleRate = mSampleRate.load(std::memory_order_relaxed);
   if (runSampleRate <= 0.0 && Transport::Instance().IsOfflineMode())
      runSampleRate = Transport::Instance().AudioSampleRate();
   if (runSampleRate > 0.0 && Transport::Instance().IsPlaying())
   {
      const double bpm = (double)Transport::Instance().Tempo();
      const double beatsPerSample = bpm / (60.0 * runSampleRate);
      const double blockStartBeat = Transport::Instance().BlockStartBeats();
      const double blockEndBeat = blockStartBeat + (double)numFrames * beatsPerSample;

      // A genuine discontinuity - a scrub, a seek, a loop wrap, or a fresh
      // Play landing mid-clip - as opposed to this block simply continuing
      // where the last one left off. Half a sample's worth of beats of
      // slack absorbs float error in the beat math; the sentinel initial
      // value (see mLastBlockEndBeat's own comment) makes the very first
      // block after Play count as a discontinuity too, so a Sample under
      // the playhead at Play-time still snaps to its exact position instead
      // of free-running from wherever its node last left off.
      const bool discontinuity = std::abs(blockStartBeat - mLastBlockEndBeat) > beatsPerSample * 0.5;
      mLastBlockEndBeat = blockEndBeat;

      for (AudioTerminal& terminal : list->topology.terminalBufferIndices)
      {
         if (terminal.numWindows <= 0 || terminal.windowOffset < 0 || terminal.sourceNode == nullptr)
            continue;
         const ClipWindow* windows = list->topology.clipWindows.data() + terminal.windowOffset;
         int cursor = std::clamp(terminal.windowCursor, 0, terminal.numWindows - 1);
         while (cursor > 0 && blockStartBeat < windows[cursor].startBeat)
            cursor--;
         while (cursor < terminal.numWindows && windows[cursor].startBeat < blockEndBeat)
         {
            // No RequestRetrigger() here any more, and deliberately so. This
            // used to fire for a window with retrigger set that was NOT a
            // dropped Sample - a condition that became unsatisfiable once
            // RebuildAudioTopology started gating `w.retrigger` on
            // `c.sampleDropped` (the two conditions are each other's
            // negation), so it had been dead for every clip in every patch.
            // The position lock below now covers both cases with one rule, and
            // covers them better: an onset-only seek can only land a clip on a
            // block boundary, while the lock states the exact source second
            // that belongs at this block's first frame, every block. The one
            // node type that implements RequestRetrigger
            // (AudioFilePlayerAudioNode, behind both Audio File and Sampler)
            // implements SetClipSamplePosition too, so nothing lost a trigger
            // it was actually receiving.
            cursor++;
         }
         // Clamped, not stored raw: the walk above exits at `numWindows` once
         // the playhead passes the last clip, and the render pass below treats
         // an out-of-range cursor as "start over at 0" - which would then cost
         // a full forward walk of the lane every block for the rest of the
         // timeline, exactly the O(1) property this cursor exists to provide.
         terminal.windowCursor = std::clamp(cursor, 0, terminal.numWindows - 1);

         // Per-clip pitch (Audio Clip and Audio Sample alike): pushed to the
         // terminal's own sourceNode every block the playhead is inside a
         // window, so a node shared by several clips plays each one at its
         // own pitch instead of one clip's edit bleeding into every other
         // clip that happens to share its source. Block-granular - if this
         // block spans a window boundary the whole block still renders at the
         // window active at its start, one block's worth of a stale pitch at
         // worst. A block with no window covering its start (a gap, or the
         // playhead outside every clip) pushes nothing, leaving whatever the
         // node's own canvas pitch cook last set - inaudible either way since
         // nothing plays there.
         //
         // Pushed for every window, Sample or not. It used to skip a dropped
         // Sample on the grounds that the position lock below carries pitch
         // itself - true for the file player, but Sampler and the wavetable
         // synth override SetClipPitchOverride and do NOT implement
         // SetClipSamplePosition, so a Sample clip whose source was a Sampler
         // got its pitch from neither path and the control did nothing at all.
         // The engine cannot tell which of the two a given node consumes, so it
         // offers both; the file player's clip path reads the lock's pitch and
         // ignores this mailbox push for those blocks, which is harmless.
         int pitchCursor = std::clamp(terminal.windowCursor, 0, terminal.numWindows - 1);
         while (pitchCursor > 0 && blockStartBeat < windows[pitchCursor].startBeat)
            pitchCursor--;
         while (pitchCursor + 1 < terminal.numWindows && blockStartBeat >= windows[pitchCursor].endBeat)
            pitchCursor++;
         if (blockStartBeat >= windows[pitchCursor].startBeat && blockStartBeat < windows[pitchCursor].endBeat)
            terminal.sourceNode->SetClipPitchOverride(windows[pitchCursor].pitch);

         // Clip position lock, for EVERY audio clip - not just a dropped
         // Sample. Every block a window overlaps (not just blocks that start
         // inside it - a clip starting mid-block gets a negative source
         // position and so lands sample-accurately), the node is told exactly
         // which source second belongs at this block's first frame. Nothing is
         // inferred from the node's own free-running position, so the audio
         // cannot drift from the box, the waveform, or the transport.
         //
         // This used to be gated on sampleDropped, which left a real hole: a
         // clip whose source node was assigned by hand in the inspector rather
         // than created by dropping a file is not "dropped", so it got neither
         // the retrigger above (gated on sampleDropped from the other side) nor
         // this lock. Its node just free-ran from wherever its own canvas cook
         // had left it, which meant a hard seek into such a clip played the
         // wrong part of the file, and playing the same clip twice gave two
         // different results. One rule for every clip closes that.
         //
         // What stays Sample-only is the source-time MAPPING, not the lock:
         // sourceOffsetSeconds (a trim or split moves it, for any clip) is
         // passed through for all of them, while sampleBpm/syncToTempo are
         // zero for a non-Sample, so effBpm falls back to the live tempo and
         // sourcePerSecond to 1.0 - native speed, clip-relative. A source that
         // cannot be positioned (any synth, Audio In) ignores the call: the
         // base SetClipSamplePosition is a no-op and only the file player
         // overrides it.
         int sampleCursor = std::clamp(terminal.windowCursor, 0, terminal.numWindows - 1);
         while (sampleCursor > 0 && blockStartBeat < windows[sampleCursor].startBeat)
            sampleCursor--;
         while (sampleCursor + 1 < terminal.numWindows && blockStartBeat >= windows[sampleCursor].endBeat)
            sampleCursor++;
         const ClipWindow& sw = windows[sampleCursor];
         if (sw.startBeat < blockEndBeat && sw.endBeat > blockStartBeat)
         {
            const double effBpm = (sw.syncToTempo && sw.sampleBpm > 0.0f) ? (double)sw.sampleBpm : bpm;
            const double sourceSeconds = (double)sw.sourceOffsetSeconds +
               (blockStartBeat - sw.startBeat) * 60.0 / std::max(1.0, effBpm);
            const double sourcePerSecond = bpm / std::max(1.0, effBpm);
            terminal.sourceNode->SetClipSamplePosition(sourceSeconds, sourcePerSecond, sw.pitch, discontinuity);
         }
      }
   }

   // PDC scratch: a delayed-copy landing spot per input pin, reused node to
   // node (only one node's inputs are ever "in flight" at a time - the
   // buffer a pin's CompensationDelay writes into is fully consumed by
   // entry.node->ProcessBlock before the next entry runs). mCompScratch/
   // mCompScratchChannels are plain AudioEngine members (see AudioEngine.h),
   // wired up once in the constructor rather than lazily per-thread here.
   // Only touched for a pin whose CompensationDelay::IsActive() is true; the
   // common all-zero-latency topology never writes to this at all.
   for (AudioTopologyEntry& entry : list->topology.order)
   {
      AudioBuffer inputViews[kAudioMaxNodeInputs];
      const AudioBuffer* inputPtrs[kAudioMaxNodeInputs];
      for (int i = 0; i < entry.numInputs; i++)
      {
         const int idx = entry.inputBufferIndices[i];
         if (idx < 0)
         {
            inputPtrs[i] = nullptr;
         }
         else if (entry.node->inputCompensation[i].IsActive())
         {
            AudioBuffer src = list->buffers[idx].View(numFrames, numChannels);
            AudioBuffer delayed;
            delayed.channels = mCompScratchChannels[i];
            delayed.numChannels = numChannels;
            delayed.numFrames = numFrames;
            entry.node->inputCompensation[i].ProcessBlock(src, delayed);
            inputViews[i] = delayed;
            inputPtrs[i] = &inputViews[i];
         }
         else
         {
            inputViews[i] = list->buffers[idx].View(numFrames, numChannels);
            inputPtrs[i] = &inputViews[i];
         }
      }
      const int numOuts = std::clamp(entry.numOutputs, 1, kAudioMaxNodeOutputs);
      AudioBuffer outputViews[kAudioMaxNodeOutputs];
      AudioBuffer* outputPtrs[kAudioMaxNodeOutputs];
      for (int o = 0; o < numOuts; o++)
      {
         int outIdx = entry.outputBufferIndices[o];
         if (outIdx < 0 && o == 0 && entry.outputBufferIndex >= 0)
            outIdx = entry.outputBufferIndex;
         if (outIdx >= 0 && outIdx < list->topology.numBuffers)
         {
            outputViews[o] = list->buffers[outIdx].View(numFrames, numChannels);
            outputPtrs[o] = &outputViews[o];
         }
         else
         {
            outputPtrs[o] = nullptr;
         }
      }
      if (outStageMs != nullptr)
      {
         const double t0 = NowMs();
         entry.node->ProcessBlockMulti(inputPtrs, entry.numInputs, outputPtrs, numOuts);
         const double t1 = NowMs();
         const int sId = (entry.stageId >= 0 && entry.stageId < kAudioStageCount) ? entry.stageId : (int)kAudioStageOther;
         outStageMs[sId] += (t1 - t0);
      }
      else
      {
         entry.node->ProcessBlockMulti(inputPtrs, entry.numInputs, outputPtrs, numOuts);
      }
   }

   // Scratch interleave buffer for capture rings, and the terminal-summation
   // PDC scratch (one shared landing spot, reused terminal to terminal since
   // each is summed into deviceBuffer - and captured - immediately, before
   // the next terminal's turn - never two terminals' delayed copies needed
   // live at once): mInterleaveScratch/mTerminalScratch/
   // mTerminalScratchChannels are plain AudioEngine members now, wired up
   // once in the constructor - see the PDC scratch comment above.
   for (AudioTerminal& terminal : list->topology.terminalBufferIndices)
   {
      AudioBuffer src = list->buffers[terminal.bufferIndex].View(numFrames, numChannels);
      // Prefer the capture ring's own persistent compensation (survives
      // across topology rebuilds) over the terminal's own value, which is
      // rebuilt from scratch every generation - see AudioCaptureRing's and
      // AudioTerminal's comments. A Timeline Strict clip terminal has no
      // ring at all, so it falls back to its own (rebuild-transient) one.
      CompensationDelay& terminalComp = terminal.capture != nullptr
                                           ? terminal.capture->compensation
                                           : (terminal.externalCompensation != nullptr ? *terminal.externalCompensation
                                                                                       : terminal.compensation);
      if (terminalComp.IsActive())
      {
         AudioBuffer delayed;
         delayed.channels = mTerminalScratchChannels;
         delayed.numChannels = numChannels;
         delayed.numFrames = numFrames;
         terminalComp.ProcessBlock(src, delayed);
         src = delayed;
      }
      const float gain = terminal.gain;

      // Arrangement Timeline clip scheduling: a per-sample envelope in BEATS
      // on top of the flat gain above. The terminal carries every window of
      // its lane, so nothing here depends on which clip happens to be under
      // the playhead when the topology was built - that dependency is exactly
      // what made a second clip of the same node silent.
      //
      // The beat axis is derived from the block's own start rather than read
      // per sample: AdvanceAudioClock(numFrames) runs before RunTopology in
      // the same callback, so Beats() is already the position at the END of
      // this block and the block started numFrames earlier.
      // mEnvScratch is a plain AudioEngine member (see the PDC scratch
      // comment above) - was `static thread_local` here.
      //
      // Per-frame clip pan (see ClipWindow::panL/R): varies with which
      // window is under the playhead, unlike the terminal's own lane pan,
      // which is flat across the whole block - so it needs its own
      // per-frame scratch (mPanLScratch/mPanRScratch, also plain members)
      // alongside mEnvScratch rather than folding into the constant chGain
      // the no-window path below still uses.
      double runSampleRate = mSampleRate.load(std::memory_order_relaxed);
      if (runSampleRate <= 0.0 && Transport::Instance().IsOfflineMode())
         runSampleRate = Transport::Instance().AudioSampleRate();

      const bool isTimelineClip = terminal.numWindows > 0 && terminal.windowOffset >= 0;
      if (isTimelineClip && runSampleRate > 0.0)
      {
         const ClipWindow* windows = list->topology.clipWindows.data() + terminal.windowOffset;
         const int numWindows = terminal.numWindows;
         const float laneGain = terminal.laneGain;

         // Paused in Timeline mode is silence, DAW-style: the nodes keep
         // processing (they are seeds in `order` either way), but nothing
         // reaches the device. Without this a paused playhead sitting inside
         // a clip would drone. An offline render is never paused - the take
         // forces the transport to play for its whole duration - so this
         // needs no offline carve-out.
         const bool playing = Transport::Instance().IsPlaying();

         const double bpm = (double)Transport::Instance().Tempo();
         const double beatsPerSample = bpm / (60.0 * runSampleRate);
         // Not Beats() - numFrames*rate: on the block that crosses a loop end
         // the wrap has already moved Beats() back to the loop start, and that
         // subtraction would put the whole block on the new lap's axis - the
         // last few ms before the loop point would be zeroed out instead of
         // played. Transport captures the real start before advancing.
         const double blockStartBeat = Transport::Instance().BlockStartBeats();

         // 2 ms, expressed in beats at the current tempo. Applied at every
         // window edge that is NOT abutted by the neighbouring window, on top
         // of whatever user fade the clip carries. A hard edge on a running
         // oscillator is a step discontinuity and clicks; 2 ms is short
         // enough to still read as an instant onset.
         const double declickBeats = 0.002 * bpm / 60.0;

         int cursor = terminal.windowCursor;
         if (cursor < 0 || cursor >= numWindows)
            cursor = 0;
         // A seek or a loop wrap moves the position backwards; walk the
         // cursor back rather than searching, since the common case is
         // "still in the same window as last block".
         while (cursor > 0 && blockStartBeat < windows[cursor].startBeat)
            cursor--;

         for (int i = 0; i < numFrames; i++)
         {
            const double beat = blockStartBeat + (double)i * beatsPerSample;
            while (cursor + 1 < numWindows && beat >= windows[cursor].endBeat)
               cursor++;
            const ClipWindow& w = windows[cursor];
            if (!playing || beat < w.startBeat || beat >= w.endBeat)
            {
               mEnvScratch[i] = 0.0f;
               mPanLScratch[i] = 1.0f;
               mPanRScratch[i] = 1.0f;
               // A playing head that has left every window has also left the
               // bucket in progress - flush it here, or a clip followed by a
               // gap never publishes its last bucket and keeps a flat notch
               // at its right edge. Not when stopped: a stopped playhead's
               // bucket is half-measured (see below).
               if (playing && terminal.peakClipId != 0)
               {
                  if (terminal.peakBucket >= 0)
                     mClipPeaks.Write({ terminal.peakClipId, terminal.peakShape, terminal.peakBucket,
                                        terminal.peakMin, terminal.peakMax });
                  terminal.peakClipId = 0;
                  terminal.peakBucket = -1;
               }
               continue;
            }
            double env = (double)w.gain * (double)laneGain;
            const double sinceStart = beat - w.startBeat;
            const double untilEnd = w.endBeat - beat;
            if (w.fadeInBeats > 0.0 && sinceStart < w.fadeInBeats)
               env *= sinceStart / w.fadeInBeats;
            if (w.fadeOutBeats > 0.0 && untilEnd < w.fadeOutBeats)
               env *= untilEnd / w.fadeOutBeats;
            if (declickBeats > 0.0)
            {
               if (!w.abutsPrev && sinceStart < declickBeats)
                  env *= sinceStart / declickBeats;
               if (!w.abutsNext && untilEnd < declickBeats)
                  env *= untilEnd / declickBeats;
            }
            mEnvScratch[i] = (float)(env < 0.0 ? 0.0 : env);
            mPanLScratch[i] = w.panL;
            mPanRScratch[i] = w.panR;

            // Live waveform bucket (WP8). Measured on the clip's own
            // material BEFORE the envelope and both gains, so editing a
            // fade, a clip gain or a lane gain does not invalidate what has
            // already been drawn - only the four fields that change what
            // material the clip holds do (the panel clears on those).
            const int bucket = (int)((beat - w.startBeat) * kClipPeakBucketsPerBeat);
            if (bucket != terminal.peakBucket || w.clipId != terminal.peakClipId)
            {
               // Only complete buckets are published: the one in progress is
               // flushed when the playhead crosses out of it, which is also
               // why a stopped playhead leaves its last bucket unfilled.
               if (terminal.peakBucket >= 0 && terminal.peakClipId != 0)
                  mClipPeaks.Write({ terminal.peakClipId, terminal.peakShape, terminal.peakBucket,
                                     terminal.peakMin, terminal.peakMax });
               terminal.peakClipId = w.clipId;
               terminal.peakShape = w.shape;
               terminal.peakBucket = bucket;
               terminal.peakMin = 0.0f;
               terminal.peakMax = 0.0f;
            }
            for (int ch = 0; ch < numChannels; ch++)
            {
               const float v = src.channels[ch][i];
               if (v < terminal.peakMin) terminal.peakMin = v;
               if (v > terminal.peakMax) terminal.peakMax = v;
            }
         }
         terminal.windowCursor = cursor;

         if (numChannels >= 2)
         {
            for (int i = 0; i < numFrames; i++)
            {
               const float env = mEnvScratch[i];
               if (env <= 0.0f)
                  continue;
               const float pL = mPanLScratch[i];
               const float pR = mPanRScratch[i];
               const float inL = src.channels[0][i];
               const float inR = src.channels[1][i];

               float outL = inL * pL;
               float outR = inR * pR;
               if (pL > 1.0f && pR < 1.0f)
               {
                  const float fold = (1.0f - pR);
                  outL = (inL + inR * fold * 0.5f) * (pL / (1.0f + fold * 0.5f));
               }
               else if (pR > 1.0f && pL < 1.0f)
               {
                  const float fold = (1.0f - pL);
                  outR = (inR + inL * fold * 0.5f) * (pR / (1.0f + fold * 0.5f));
               }

               deviceBuffer.channels[0][i] += outL * gain * env;
               deviceBuffer.channels[1][i] += outR * gain * env;
            }
            for (int ch = 2; ch < numChannels; ch++)
            {
               for (int i = 0; i < numFrames; i++)
                  deviceBuffer.channels[ch][i] += src.channels[ch][i] * gain * mEnvScratch[i];
            }
         }
         else
         {
            for (int ch = 0; ch < numChannels; ch++)
            {
               for (int i = 0; i < numFrames; i++)
                  deviceBuffer.channels[ch][i] += src.channels[ch][i] * gain * mEnvScratch[i];
            }
         }
      }
      else if (terminal.numWindows > 0)
      {
         // A timeline terminal with no usable sample rate cannot place its
         // windows, and a terminal whose entire purpose is being gated must
         // fail to silence, not to unity gain - the flat-gain path below
         // would ignore the schedule, the disabled flag and the pause.
         // Unreachable today (a device callback always has a rate, and
         // ProcessOffline has Transport::AudioSampleRate()).
      }
      else
      {
         for (int ch = 0; ch < numChannels; ch++)
            for (int i = 0; i < numFrames; i++)
               deviceBuffer.channels[ch][i] += src.channels[ch][i] * gain;
      }

      if (terminal.capture != nullptr && terminal.capture->enabled.load(std::memory_order_relaxed))
      {
         // Always interleaved stereo for the WAV writer, regardless of the
         // topology's actual channel count - mono sources duplicate to both
         // channels, anything wider than stereo is summed down to it.
         // mInterleaveScratch is a fixed kAudioMaxBlockFrames*2 member (see
         // the PDC scratch comment above) - numFrames is already clamped to
         // kAudioMaxBlockFrames at the top of this function, so it always
         // fits; no resize() (the allocation this replaced) needed.
         for (int i = 0; i < numFrames; i++)
         {
            const float env = isTimelineClip ? mEnvScratch[i] : 1.0f;
            const float l = src.channels[0][i] * gain * env;
            const float r = numChannels > 1 ? src.channels[1][i] * gain * env : l;
            mInterleaveScratch[(size_t)i * 2 + 0] = l;
            mInterleaveScratch[(size_t)i * 2 + 1] = r;
         }
         terminal.capture->Write(mInterleaveScratch, numFrames * 2);
      }
   }
}

void AudioEngine::RenderThunk(float** buffers, int numChannels, int numFrames, void* userData)
{
   static_cast<AudioEngine*>(userData)->Process(buffers, numChannels, numFrames);
}

void AudioEngine::Process(float** buffers, int numChannels, int numFrames)
{
#if defined(__x86_64__)
   _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__)
   uint64_t fpcr;
   __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
   fpcr |= (1ULL << 24); // FZ bit
   __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif

   const double sampleRate = mSampleRate.load(std::memory_order_relaxed);
   const double nowMs = NowMs();
   const double lastMs = mLastCallbackMs.load(std::memory_order_relaxed);
   if (lastMs >= 0.0 && sampleRate > 0.0)
   {
      const double expectedGapMs = 1000.0 * (double)numFrames / sampleRate;
      const double actualGapMs = nowMs - lastMs;
      if (actualGapMs > expectedGapMs * kXrunGapMultiplier)
         mCallbackGaps.fetch_add(1, std::memory_order_relaxed);
   }
   mLastCallbackMs.store(nowMs, std::memory_order_relaxed);

   Transport::Instance().AdvanceAudioClock(numFrames);

   AudioBuffer buffer;
   buffer.channels = buffers;
   buffer.numChannels = numChannels;
   buffer.numFrames = numFrames;

   double stageMs[kAudioStageCount] = { 0.0 };
   const double topologyStartMs = NowMs();
   ProcessList* list = mCurrent.load(std::memory_order_acquire);
   RunTopology(list, buffer, stageMs);
   // Published after RunTopology fully returns, so a main-thread reader never
   // observes this generation as "completed" while entry.node->ProcessBlock
   // calls against `list`'s (possibly about-to-be-retired) nodes are still
   // in flight - see CompletedGeneration()'s doc comment.
   if (list != nullptr)
      mCompletedGeneration.store(list->generation, std::memory_order_release);
   // Deliberately after RunTopology, not part of it: the preview is not in
   // the node topology at all, so it stays audible (and unaffected by
   // bypass/the transport) across a topology swap, and even with nothing
   // patched to an Audio Out.
   mPreviewPlayer.ProcessBlock(buffer);
   const double topologyMs = NowMs() - topologyStartMs;

   if (sampleRate > 0.0)
   {
      const double expectedGapMs = 1000.0 * (double)numFrames / sampleRate;
      const double instantLoad = expectedGapMs > 0.0 ? topologyMs / expectedGapMs : 0.0;
      // Deadline miss: this block's render used the whole period, so the
      // device was handed it late. Same measurement as the cb_load meter.
      if (expectedGapMs > 0.0 && topologyMs >= expectedGapMs)
         mXrunDeadline.fetch_add(1, std::memory_order_relaxed);
      const double prevLoad = mLastBlockLoad.load(std::memory_order_relaxed);
      mLastBlockLoad.store(prevLoad + kLoadSmoothing * (instantLoad - prevLoad), std::memory_order_relaxed);
      mRawLoadHistory.Push((float)instantLoad);
      if (expectedGapMs > 0.0)
      {
         for (int s = 0; s < kAudioStageCount; s++)
            mStageLoadHistory[s].Push((float)(stageMs[s] / expectedGapMs));
      }
   }
}

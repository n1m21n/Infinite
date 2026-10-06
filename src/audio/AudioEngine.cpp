#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "core/Transport.h"
#include "platform/Platform.h"

#include <xmmintrin.h>

namespace
{
   double NowMs()
   {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
   }

   // Approximate xrun detection: AVAudioSourceNode gives us no direct xrun
   // notification (unlike raw AUHAL), so we compare the wall-clock gap
   // between successive Process() entries to the expected block period and
   // flag anything past this multiple as a probable dropout. This is a
   // judgment call, not a precise xrun count - retune once real patches
   // are running.
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
   for (int i = 0; i < kAudioMaxNodeInputs; i++)
      for (int ch = 0; ch < kAudioMaxChannels; ch++)
         mCompScratchChannels[i][ch] = mCompScratch[i][ch];
   for (int ch = 0; ch < kAudioMaxChannels; ch++)
      mTerminalScratchChannels[ch] = mTerminalScratch[ch];
}

bool AudioEngine::Start(std::string& outError)
{
   double sampleRate = 0.0;
   // Turbo 0.51 (upstream): raised BEFORE the device opens, the first callback
   // can fire before AudioDeviceOpen returns and the retire drain must not
   // treat that window as "no audio thread".
   mDeviceOpen.store(true, std::memory_order_release);
   if (!Platform::AudioDeviceOpen(&AudioEngine::RenderThunk, this, sampleRate, outError,
                                  mRequestedDeviceId, mRequestedSampleRate, mRequestedBufferFrames,
                                  mRequestedInputDeviceId))
   {
      mDeviceOpen.store(false, std::memory_order_release);
      return false;
   }
   mSampleRate.store(sampleRate, std::memory_order_relaxed);
   mStartGeneration.fetch_add(1, std::memory_order_relaxed);
   mStartedAtMs.store(NowMs(), std::memory_order_relaxed);
   mPreviewPlayer.PrepareToPlay(sampleRate);
   Transport::Instance().NotifyAudioEngineStarted(sampleRate);
   return true;
}

void AudioEngine::Stop()
{
   Platform::AudioDeviceClose();
   mSampleRate.store(0.0, std::memory_order_relaxed);
   // AudioDeviceClose has returned: no callback can still hold a list.
   mDeviceOpen.store(false, std::memory_order_release);
   DrainRetired();

   // Reset xrun-detection state here, on Stop(), not on the next Start():
   // Platform::AudioDeviceClose() has already returned, so Process() cannot
   // be mid-callback racing this write - "not running" starts exactly here.
   // mLastCallbackMs back to its -1.0 "no previous callback" sentinel means
   // the first callback after the next Start() has nothing stale to compare
   // its gap against, so a restart can no longer trip kXrunGapMultiplier on
   // its own (bug 3's false-positive xrun on restart). mXrunCount resets to
   // 0 alongside it, a deliberate choice, not an oversight: the status-bar
   // readout (main.cpp:17521-17548-ish, "xruns=N") exists to answer "did
   // *this run* introduce dropouts", which only a per-run counter can answer
   // - a cumulative-since-process-start count can't distinguish "this
   // restart is clean" from "an earlier run had one and nobody's looked
   // since". If that ever needs to become "since the app launched" instead,
   // this reset is the one place to remove, not something to leave debatable
   // at every call site.
   mLastCallbackMs.store(-1.0, std::memory_order_relaxed);
   mXrunCount.store(0, std::memory_order_relaxed);

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

// Turbo 0.51 (upstream): a superseded list is freed only once the audio thread
// has COMPLETED a pass over a strictly newer generation (callbacks are serial),
// or when no callback can be using it (no device, or offline render, where the
// device callback returns before touching the graph). The old "freed one
// SetTopology later" rule crashed when two rebuilds landed inside one block.
void AudioEngine::DrainRetired()
{
   const bool noAudioThread = !mDeviceOpen.load(std::memory_order_acquire) || mOffline.load(std::memory_order_acquire);
   const uint64_t completed = mCompletedGeneration.load(std::memory_order_acquire);
   size_t keep = 0;
   for (size_t i = 0; i < mRetiring.size(); i++)
   {
      ProcessList* list = mRetiring[i];
      if (noAudioThread || completed > list->generation)
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

uint64_t AudioEngine::BlocksDone() const
{
   return mBlocksDone.load(std::memory_order_acquire);
}

uint64_t AudioEngine::XrunCount() const
{
   return mXrunCount.load(std::memory_order_relaxed);
}

void AudioEngine::NotifyProcessorOverload()
{
   // Kept alongside, not instead of, the wall-clock heuristic in Process():
   // kAudioDeviceProcessorOverload catches genuine render-thread overruns,
   // but the heuristic also catches late/skipped-callback gaps (e.g.
   // silence-substitution) that this notification may not cover. Either
   // source bumping the same counter is a deliberate choice, not a race to
   // fix - see the comment on kXrunGapMultiplier.
   mXrunCount.fetch_add(1, std::memory_order_relaxed);
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

   if (mOffline.load(std::memory_order_acquire))
      return true; // Turbo 0.44.1: the main thread drives the graph, not "dead"
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

void AudioEngine::ProcessOffline(AudioBuffer& buffer)
{
   ProcessList* list = mCurrent.load(std::memory_order_acquire);
   RunTopology(list, buffer);
}

void AudioEngine::RunTopology(ProcessList* list, AudioBuffer& deviceBuffer)
{
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

   // PDC scratch: a delayed-copy landing spot per input pin, reused node to
   // node (only one node's inputs are ever "in flight" at a time - the
   // buffer a pin's CompensationDelay writes into is fully consumed by
   // entry.node->ProcessBlock before the next entry runs). thread_local so
   // it's allocated once per real-time thread, never per block or per node -
   // same discipline as sInterleaveScratch below. Only touched for a pin
   // whose CompensationDelay::IsActive() is true; the common all-zero-
   // latency topology never writes to this at all.
   // Turbo 0.51: mCompScratch/mCompScratchChannels are AudioEngine members.

   const bool timeline = mTimelineMode.load(std::memory_order_relaxed);
   if (timeline && !list->topology.arrangeTerminals.empty())
      RunArrangeLookahead(list, numFrames);

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
         else if (entry.inputCompensation[i].IsActive())
         {
            AudioBuffer src = list->buffers[idx].View(numFrames, numChannels);
            AudioBuffer delayed;
            delayed.channels = mCompScratchChannels[i];
            delayed.numChannels = numChannels;
            delayed.numFrames = numFrames;
            entry.inputCompensation[i].ProcessBlock(src, delayed);
            inputViews[i] = delayed;
            inputPtrs[i] = &inputViews[i];
         }
         else
         {
            inputViews[i] = list->buffers[idx].View(numFrames, numChannels);
            inputPtrs[i] = &inputViews[i];
         }
      }
      AudioBuffer output = list->buffers[entry.outputBufferIndex].View(numFrames, numChannels);
      entry.node->ProcessBlock(inputPtrs, entry.numInputs, output);
   }

   // Turbo 0.51: mInterleaveScratch / mTerminalScratch are AudioEngine members.

   for (AudioTerminal& terminal : list->topology.terminalBufferIndices)
   {
      AudioBuffer src = list->buffers[terminal.bufferIndex].View(numFrames, numChannels);
      if (terminal.compensation.IsActive())
      {
         AudioBuffer delayed;
         delayed.channels = mTerminalScratchChannels;
         delayed.numChannels = numChannels;
         delayed.numFrames = numFrames;
         terminal.compensation.ProcessBlock(src, delayed);
         src = delayed;
      }
      // Turbo: the Arrangement Timeline owns the output in Timeline mode.
      if (terminal.mixToDevice && (!timeline || terminal.live))
         for (int ch = 0; ch < numChannels; ch++)
            for (int i = 0; i < numFrames; i++)
               deviceBuffer.channels[ch][i] += src.channels[ch][i];

      if (terminal.capture != nullptr && terminal.capture->enabled.load(std::memory_order_relaxed))
      {
         // Always interleaved stereo for the WAV writer, regardless of the
         // topology's actual channel count - mono sources duplicate to both
         // channels, anything wider than stereo is summed down to it.
         for (int i = 0; i < numFrames; i++)
         {
            const float l = src.channels[0][i];
            const float r = numChannels > 1 ? src.channels[1][i] : l;
            mInterleaveScratch[(size_t)i * 2 + 0] = l;
            mInterleaveScratch[(size_t)i * 2 + 1] = r;
         }
         terminal.capture->Write(mInterleaveScratch, numFrames * 2);
      }
   }

   if (timeline && !list->topology.arrangeTerminals.empty())
      MixArrangeTerminals(list, deviceBuffer, numFrames, numChannels);

   // Last line of defence for the hardware stream. A faulty plugin or DSP
   // node must never poison every downstream terminal with NaN/Inf, nor send
   // an unbounded value to the driver. Keep generous headroom so normal
   // internal gain staging is untouched; the actual output device can still
   // apply its own final limiting/conversion policy.
   constexpr float kSafetyCeiling = 16.0f;
   for (int ch = 0; ch < numChannels; ch++)
   {
      float* samples = deviceBuffer.channels[ch];
      if (samples == nullptr)
         continue;
      for (int i = 0; i < numFrames; i++)
      {
         float v = samples[i];
         if (!std::isfinite(v))
            v = 0.0f;
         samples[i] = std::clamp(v, -kSafetyCeiling, kSafetyCeiling);
      }
   }
}

// ---- Arrangement Timeline (Turbo 0.43) -------------------------------------

namespace
{
   double ArrangeBlockStartBeat() { return Transport::Instance().Beats(); }
   double ArrangeBeatsPerSample(double sampleRate)
   {
      const double bpm = std::max(1.0, (double)Transport::Instance().Tempo());
      return sampleRate > 0.0 ? bpm / 60.0 / sampleRate : 0.0;
   }
}

// Before any node cooks: every Audio Sample window under this block tells its
// file player exactly which source position belongs here (upstream's
// position lock), so the sample is on the grid however the playhead got
// there, and tempo sync / pitch apply per block.
void AudioEngine::RunArrangeLookahead(ProcessList* list, int numFrames)
{
   Transport& transport = Transport::Instance();
   const double sr = mSampleRate.load(std::memory_order_relaxed);
   const double bps = ArrangeBeatsPerSample(sr > 0.0 ? sr : 48000.0);
   // Turbo 0.49: no clip pitch reset needed anywhere here: SetClipPitchOverride is
   // valid for one block only, so a block without the call (stopped, gap, timeline
   // off, lane gone after a rebuild) eases the node back to its own pitch.
   if (!transport.IsPlaying() || bps <= 0.0)
      return;
   const double bpm = std::max(1.0, (double)transport.Tempo());
   const double blockStart = ArrangeBlockStartBeat();
   const double blockEnd = blockStart + bps * (double)numFrames;
   const uint32_t serial = transport.SeekSerial();
   const ArrangeClipWindow* all = list->topology.arrangeWindows.data();
   for (const ArrangeTerminal& t : list->topology.arrangeTerminals)
   {
      if (t.source == nullptr || t.numWindows <= 0)
         continue;
      const bool jumped = serial != t.seenSeekSerial;
      t.seenSeekSerial = serial;
      const ArrangeClipWindow* w = all + t.windowOffset;
      int k = 0;
      while (k < t.numWindows && w[k].endBeat <= blockStart)
         k++;
      if (k >= t.numWindows || w[k].startBeat >= blockEnd)
      {
         t.activeWindow = -1;
         t.clipPitchActive = false;
         continue;
      }
      const bool entered = t.activeWindow != k;
      t.activeWindow = k;
      // Turbo 0.49 (upstream): per-clip pitch for every clip, every block, so a node
      // shared by several clips plays each at its own pitch (block-granular).
      // Applied below, once per source.
      t.clipPitchActive = true;
      t.clipPitch = w[k].pitch;
      if (!w[k].seekSource)
         continue;
      const double effBpm = (w[k].syncToTempo && w[k].sampleBpm > 0.0f) ? (double)w[k].sampleBpm : bpm;
      const double secs = w[k].sourceOffsetSeconds + (blockStart - w[k].startBeat) * 60.0 / effBpm;
      const double perSecond = bpm / effBpm;
      t.source->SetClipSamplePosition(secs, perSecond, w[k].pitch, jumped || entered);
   }

   // Turbo 0.49: one clip pitch per source per block. A source on several lanes
   // gets it from its first terminal under a clip on an audible lane (else the
   // first under a clip at all), applied once; a terminal outside its clips never
   // resets a pitch another lane of the same source is applying.
   const auto& terms = list->topology.arrangeTerminals;
   const size_t nt = terms.size();
   for (size_t i = 0; i < nt; i++)
   {
      const ArrangeTerminal& t = terms[i];
      if (t.source == nullptr || t.numWindows <= 0 || !t.clipPitchActive)
         continue;
      bool done = false;
      for (size_t j = 0; j < i && !done; j++)
         done = terms[j].source == t.source && terms[j].numWindows > 0 && terms[j].clipPitchActive;
      if (done)
         continue; // an earlier terminal of this source already applied it
      float pitch = t.clipPitch;
      for (size_t j = i; j < nt; j++)
      {
         const ArrangeTerminal& u = terms[j];
         if (u.source != t.source || u.numWindows <= 0 || !u.clipPitchActive)
            continue;
         const int slot = u.laneSlot;
         const bool audible = slot >= 0 && slot < kMaxArrangeLanes &&
                              mLaneGain[slot].load(std::memory_order_relaxed) > 0.0f;
         if (audible)
         {
            pitch = u.clipPitch;
            break;
         }
      }
      t.source->SetClipPitchOverride(pitch);
   }
}

void AudioEngine::MixArrangeTerminals(ProcessList* list, AudioBuffer& deviceBuffer, int numFrames, int numChannels)
{
   Transport& transport = Transport::Instance();
   const bool playing = transport.IsPlaying();
   const double sr = mSampleRate.load(std::memory_order_relaxed);
   const double bps = ArrangeBeatsPerSample(sr > 0.0 ? sr : 48000.0);
   const double bpm = std::max(1.0, (double)transport.Tempo());
   const double ramp = 0.003 * bpm / 60.0; // 3 ms declick, in beats
   const double blockStart = ArrangeBlockStartBeat();
   const ArrangeClipWindow* all = list->topology.arrangeWindows.data();
   float peaks[kMaxArrangeLanes];
   bool touched[kMaxArrangeLanes];
   for (int i = 0; i < kMaxArrangeLanes; i++)
   {
      peaks[i] = 0.0f;
      touched[i] = false;
   }

   for (const ArrangeTerminal& t : list->topology.arrangeTerminals)
   {
      if (t.bufferIndex < 0 || t.numWindows <= 0)
         continue;
      const int slot = std::clamp(t.laneSlot, 0, kMaxArrangeLanes - 1);
      touched[slot] = true;
      if (!playing)
         continue;
      const float laneGain = mLaneGain[slot].load(std::memory_order_relaxed);
      const float lanePanL = mLanePanL[slot].load(std::memory_order_relaxed);
      const float lanePanR = mLanePanR[slot].load(std::memory_order_relaxed);
      AudioBuffer src = list->buffers[t.bufferIndex].View(numFrames, numChannels);
      const ArrangeClipWindow* w = all + t.windowOffset;
      int cursor = std::clamp(t.cursor, 0, t.numWindows - 1);
      if (cursor > 0 && blockStart < w[cursor - 1].endBeat)
         cursor = 0; // moved backwards (seek / loop)
      float peak = 0.0f;
      for (int i = 0; i < numFrames; i++)
      {
         const double beat = blockStart + bps * (double)i;
         while (cursor + 1 < t.numWindows && beat >= w[cursor].endBeat)
            cursor++;
         const ArrangeClipWindow& cw = w[cursor];
         if (beat < cw.startBeat || beat >= cw.endBeat)
         {
            // Left every window: flush the bucket in progress, or a clip
            // followed by a gap keeps a flat notch at its right edge.
            if (t.peakClipId != 0)
            {
               if (t.peakBucket >= 0)
                  mClipPeaks.Write({ t.peakClipId, t.peakShape, t.peakBucket, t.peakMin, t.peakMax });
               t.peakClipId = 0;
               t.peakBucket = -1;
            }
            continue;
         }
         // Turbo 0.48 (upstream WP8): live waveform, signed min/max per 1/16
         // beat of the clip, measured before fades and gains so editing
         // those keeps what is drawn. Only complete buckets are published.
         {
            const int bucket = (int)((beat - cw.startBeat) * kClipPeakBucketsPerBeat);
            if (bucket != t.peakBucket || cw.clipId != t.peakClipId)
            {
               if (t.peakBucket >= 0 && t.peakClipId != 0)
                  mClipPeaks.Write({ t.peakClipId, t.peakShape, t.peakBucket, t.peakMin, t.peakMax });
               t.peakClipId = cw.clipId;
               t.peakShape = cw.shape;
               t.peakBucket = bucket;
               t.peakMin = 0.0f;
               t.peakMax = 0.0f;
            }
            for (int ch = 0; ch < std::min(numChannels, 2); ch++)
            {
               const float v = src.channels[ch][i];
               if (v < t.peakMin) t.peakMin = v;
               if (v > t.peakMax) t.peakMax = v;
            }
         }
         double g = (double)cw.gain;
         const double in = beat - cw.startBeat;
         const double out = cw.endBeat - beat;
         if (cw.fadeInBeats > 0.0 && in < cw.fadeInBeats)
            g *= in / cw.fadeInBeats;
         else if (!cw.abutsPrev && in < ramp)
            g *= in / ramp;
         if (cw.fadeOutBeats > 0.0 && out < cw.fadeOutBeats)
            g *= out / cw.fadeOutBeats;
         else if (!cw.abutsNext && out < ramp)
            g *= out / ramp;
         const float gl = (float)g * laneGain * cw.panL * lanePanL;
         const float gr = (float)g * laneGain * cw.panR * lanePanR;
         for (int ch = 0; ch < numChannels; ch++)
         {
            const float v = src.channels[ch][i] * (ch == 0 ? gl : (ch == 1 ? gr : (float)g * laneGain));
            deviceBuffer.channels[ch][i] += v;
            peak = std::max(peak, std::fabs(v));
         }
      }
      t.cursor = cursor;
      peaks[slot] = std::max(peaks[slot], peak);
   }
   for (int i = 0; i < kMaxArrangeLanes; i++)
      if (touched[i])
      {
         const float prev = mLanePeak[i].load(std::memory_order_relaxed);
         mLanePeak[i].store(std::max(peaks[i], prev * 0.85f), std::memory_order_relaxed);
      }
}

void AudioEngine::SetOfflineRender(bool on)
{
   mOffline.store(on, std::memory_order_release);
   if (!on)
   {
      // The device callback kept running but skipped its bookkeeping: start
      // the liveness / xrun clocks fresh so the render gap reads as neither.
      mLastCallbackMs.store(-1.0, std::memory_order_relaxed);
      mStartedAtMs.store(NowMs(), std::memory_order_relaxed);
   }
   if (on)
   {
      // Let a callback already inside the graph finish before the main
      // thread starts driving it (bounded wait: a stalled driver must not
      // hang the UI).
      for (int i = 0; i < 2000 && mInProcess.load(std::memory_order_acquire) != 0; i++)
         std::this_thread::sleep_for(std::chrono::microseconds(250));
   }
}

void AudioEngine::RenderOfflineBlock(AudioBuffer& buffer)
{
   Transport::Instance().AdvanceAudioClock(buffer.numFrames);
   ProcessList* list = mCurrent.load(std::memory_order_acquire);
   RunTopology(list, buffer);
}

void AudioEngine::RenderThunk(float** buffers, int numChannels, int numFrames, void* userData)
{
   static_cast<AudioEngine*>(userData)->Process(buffers, numChannels, numFrames);
}

void AudioEngine::Process(float** buffers, int numChannels, int numFrames)
{
   // x64 MSVC never defines __x86_64__ (it uses _M_X64), so the old guard
   // left denormal flushing OFF on Windows: reverb/filter tails decaying into
   // denormals then cost 10-100x CPU per sample. SSE is baseline on x64.
   _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ (bit 15) | DAZ (bit 6)

   // Turbo 0.43.1: the main thread owns the graph during an offline render.
   mInProcess.fetch_add(1, std::memory_order_acq_rel);
   if (mOffline.load(std::memory_order_acquire))
   {
      for (int ch = 0; ch < numChannels; ch++)
         if (buffers[ch] != nullptr)
            std::fill(buffers[ch], buffers[ch] + numFrames, 0.0f);
      mLastCallbackMs.store(NowMs(), std::memory_order_relaxed); // still alive
      mBlocksDone.fetch_add(1, std::memory_order_acq_rel);
      mInProcess.fetch_sub(1, std::memory_order_acq_rel);
      return;
   }
   struct InProcessGuard
   {
      std::atomic<int>& c;
      std::atomic<uint64_t>& done;
      ~InProcessGuard()
      {
         done.fetch_add(1, std::memory_order_acq_rel); // Turbo 0.47: see BlocksDone()
         c.fetch_sub(1, std::memory_order_acq_rel);
      }
   } inProcessGuard{ mInProcess, mBlocksDone };

   const double sampleRate = mSampleRate.load(std::memory_order_relaxed);
   const double nowMs = NowMs();
   const double lastMs = mLastCallbackMs.load(std::memory_order_relaxed);
   if (lastMs >= 0.0 && sampleRate > 0.0)
   {
      const double expectedGapMs = 1000.0 * (double)numFrames / sampleRate;
      const double actualGapMs = nowMs - lastMs;
      if (actualGapMs > expectedGapMs * kXrunGapMultiplier)
         mXrunCount.fetch_add(1, std::memory_order_relaxed);
   }
   mLastCallbackMs.store(nowMs, std::memory_order_relaxed);

   Transport::Instance().AdvanceAudioClock(numFrames);

   AudioBuffer buffer;
   buffer.channels = buffers;
   buffer.numChannels = numChannels;
   buffer.numFrames = numFrames;

   const double topologyStartMs = NowMs();
   ProcessList* list = mCurrent.load(std::memory_order_acquire);
   RunTopology(list, buffer);
   // Turbo 0.51: published only after RunTopology returns, see DrainRetired.
   if (list != nullptr)
      mCompletedGeneration.store(list->generation, std::memory_order_release);
   // Deliberately after RunTopology, not part of it: the preview is not in
   // the node topology at all, so it stays audible (and unaffected by
   // bypass/the transport) across a topology swap, and even with nothing
   // patched to an Audio Out.
   mPreviewPlayer.ProcessBlock(buffer);

   // Turbo: direct metronome, after the graph and the preview.
   {
      Transport& transport = Transport::Instance();
      const bool nodeClick = mDirectClick.load(std::memory_order_relaxed);
      if ((nodeClick || mTopBarClick.load(std::memory_order_relaxed)) && transport.IsPlaying() && sampleRate > 0.0)
      {
         const double spb = sampleRate * 60.0 / std::max(1.0, (double)transport.Tempo());
         mDirectClickGen.RenderAdd(buffers, std::min(numChannels, 2), numFrames, transport.Beats(), spb,
                                   transport.BeatsPerBar(), sampleRate,
                                   nodeClick ? mDirectClickVolume.load(std::memory_order_relaxed) : 0.5f,
                                   nodeClick ? mDirectClickAccent.load(std::memory_order_relaxed) : true);
      }
      else
         mDirectClickGen.Reset();
   }
   const double topologyMs = NowMs() - topologyStartMs;

   if (sampleRate > 0.0)
   {
      const double expectedGapMs = 1000.0 * (double)numFrames / sampleRate;
      const double instantLoad = expectedGapMs > 0.0 ? topologyMs / expectedGapMs : 0.0;
      const double prevLoad = mLastBlockLoad.load(std::memory_order_relaxed);
      mLastBlockLoad.store(prevLoad + kLoadSmoothing * (instantLoad - prevLoad), std::memory_order_relaxed);
   }
}

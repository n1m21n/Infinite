#include "AudioEngine.h"

#include <algorithm>
#include <chrono>

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

bool AudioEngine::Start(std::string& outError)
{
   double sampleRate = 0.0;
   if (!Platform::AudioDeviceOpen(&AudioEngine::RenderThunk, this, sampleRate, outError,
                                  mRequestedDeviceId, mRequestedSampleRate, mRequestedBufferFrames))
      return false;
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

   delete mRetiring; // safe: the audio thread finished with this one a full generation ago
   mRetiring = old;
}

double AudioEngine::SampleRate() const
{
   return mSampleRate.load(std::memory_order_relaxed);
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
   static thread_local float sCompScratch[kAudioMaxNodeInputs][kAudioMaxChannels][kAudioMaxBlockFrames];
   static thread_local float* sCompScratchChannels[kAudioMaxNodeInputs][kAudioMaxChannels];
   static thread_local bool sCompScratchInited = false;
   if (!sCompScratchInited)
   {
      for (int i = 0; i < kAudioMaxNodeInputs; i++)
         for (int ch = 0; ch < kAudioMaxChannels; ch++)
            sCompScratchChannels[i][ch] = sCompScratch[i][ch];
      sCompScratchInited = true;
   }

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
            delayed.channels = sCompScratchChannels[i];
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

   // Scratch interleave buffer for capture rings - thread_local so it's
   // allocated once per real-time thread rather than per block, and never
   // touched off the audio thread.
   static thread_local std::vector<float> sInterleaveScratch;

   // Terminal-summation PDC scratch: one shared landing spot, reused
   // terminal to terminal since each is summed into deviceBuffer (and
   // captured) immediately, before the next terminal's turn - never two
   // terminals' delayed copies needed live at once.
   static thread_local float sTerminalScratch[kAudioMaxChannels][kAudioMaxBlockFrames];
   static thread_local float* sTerminalScratchChannels[kAudioMaxChannels];
   static thread_local bool sTerminalScratchInited = false;
   if (!sTerminalScratchInited)
   {
      for (int ch = 0; ch < kAudioMaxChannels; ch++)
         sTerminalScratchChannels[ch] = sTerminalScratch[ch];
      sTerminalScratchInited = true;
   }

   for (AudioTerminal& terminal : list->topology.terminalBufferIndices)
   {
      AudioBuffer src = list->buffers[terminal.bufferIndex].View(numFrames, numChannels);
      if (terminal.compensation.IsActive())
      {
         AudioBuffer delayed;
         delayed.channels = sTerminalScratchChannels;
         delayed.numChannels = numChannels;
         delayed.numFrames = numFrames;
         terminal.compensation.ProcessBlock(src, delayed);
         src = delayed;
      }
      for (int ch = 0; ch < numChannels; ch++)
         for (int i = 0; i < numFrames; i++)
            deviceBuffer.channels[ch][i] += src.channels[ch][i];

      if (terminal.capture != nullptr && terminal.capture->enabled.load(std::memory_order_relaxed))
      {
         // Always interleaved stereo for the WAV writer, regardless of the
         // topology's actual channel count - mono sources duplicate to both
         // channels, anything wider than stereo is summed down to it.
         sInterleaveScratch.resize((size_t)numFrames * 2);
         for (int i = 0; i < numFrames; i++)
         {
            const float l = src.channels[0][i];
            const float r = numChannels > 1 ? src.channels[1][i] : l;
            sInterleaveScratch[(size_t)i * 2 + 0] = l;
            sInterleaveScratch[(size_t)i * 2 + 1] = r;
         }
         terminal.capture->Write(sInterleaveScratch.data(), numFrames * 2);
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
      const double prevLoad = mLastBlockLoad.load(std::memory_order_relaxed);
      mLastBlockLoad.store(prevLoad + kLoadSmoothing * (instantLoad - prevLoad), std::memory_order_relaxed);
   }
}

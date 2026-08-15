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
         else
         {
            inputViews[i] = list->buffers[idx].View(numFrames, numChannels);
            inputPtrs[i] = &inputViews[i];
         }
      }
      AudioBuffer output = list->buffers[entry.outputBufferIndex].View(numFrames, numChannels);
      entry.node->ProcessBlock(inputPtrs, entry.numInputs, output);
   }

   for (int bufIdx : list->topology.terminalBufferIndices)
   {
      AudioBuffer src = list->buffers[bufIdx].View(numFrames, numChannels);
      for (int ch = 0; ch < numChannels; ch++)
         for (int i = 0; i < numFrames; i++)
            deviceBuffer.channels[ch][i] += src.channels[ch][i];
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
   const double topologyMs = NowMs() - topologyStartMs;

   if (sampleRate > 0.0)
   {
      const double expectedGapMs = 1000.0 * (double)numFrames / sampleRate;
      const double instantLoad = expectedGapMs > 0.0 ? topologyMs / expectedGapMs : 0.0;
      const double prevLoad = mLastBlockLoad.load(std::memory_order_relaxed);
      mLastBlockLoad.store(prevLoad + kLoadSmoothing * (instantLoad - prevLoad), std::memory_order_relaxed);
   }
}

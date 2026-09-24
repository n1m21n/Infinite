// Linux implementation of the Platform facade's audio-device surface, via
// miniaudio (external/miniaudio/miniaudio.h, vendored - see
// MiniaudioImpl.cpp for the one TU that compiles its implementation and why
// it's dlopen, not link). Replaces the macOS AVAudioEngine/CoreAudio stack
// and the Windows WASAPI stack (AudioDeviceWin.cpp), matching their exact
// semantics:
//
//   - AudioDeviceOpen/Close: one ma_device (playback), requesting ma_format_f32
//     so miniaudio does any native<->float conversion internally - unlike
//     WASAPI shared mode, there is no manual PCM<->float path here (see
//     AudioDeviceWin.cpp's IsSupportedPcmFormat/PlanarFloatToInterleavedPcm;
//     Linux never needs that class of code). The app's render callback is
//     handed planar float buffers exactly like macOS/Windows, then
//     interleaved into miniaudio's callback buffer here.
//   - Backend selection: PulseAudio -> ALSA -> JACK, with a Null fallback
//     always appended so a machine (or CI runner) with none of those still
//     starts - overridable via INFINITE_AUDIO_BACKEND=null|alsa|pulse|jack.
//     See docs/plans/linux/phase-02-audio-midi.md 2.1 for why there's no
//     separate native-PipeWire path: this miniaudio version has none, only
//     PulseAudio-compat workarounds, confirmed by reading the vendored
//     header directly.
//   - AudioListDevices / AudioDeviceBufferFrames: ma_context_get_devices
//     enumeration, playback then capture, matching AudioDeviceWin.cpp's
//     combined 1-based index space (0 = default).
//   - Config-change recovery: miniaudio's notificationCallback (rerouted /
//     stopped) latches the same atomic PollAudioRecovery consumes - sleep/
//     wake flags stay false, same as Windows (Linux has no sleep/wake
//     notification concept main.cpp's PollAudioRecovery would need either).
//   - AudioStart/AudioRead: a separate default-input capture engine feeding
//     the same shared AudioAnalysisCommon:: math AudioDeviceWin.cpp uses
//     (src/platform/common/AudioAnalysis.h), so the two platforms can't
//     independently drift on the analysis formulas.
//   - AudioInputCapture*: refcounted default-input tap draining a lock-free
//     "most recent samples" ring for AudioNodes.cpp's AudioInputNode -
//     structurally identical LatestRing to AudioDeviceWin.cpp's.
//
// One structural simplification versus WASAPI: miniaudio negotiates the
// device's format synchronously inside ma_device_init() and returns only
// once that's settled, so there is no async poll-for-sampleRate handshake
// like CaptureEngineBase::Start() needs on Windows - OnFormat() runs
// immediately after a successful init, before ma_device_start().
//
// Everything else obeys the contract documented in ../Platform.h.

#include "../Platform.h"

#include "../common/AudioAnalysis.h"
#include "dsp/PortableFft.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
   constexpr int kMaxChannels = 8;       // matches kAudioMaxChannels in AudioEngine.h
   constexpr int kPlanarCapacity = 4096; // matches kAudioMaxBlockFrames

   // ---- miniaudio context / backend selection ------------------------------

   ma_context gContext{};
   bool gContextInited = false;
   std::string gBackendName = "Unknown";
   std::mutex gContextMutex;

   const char* BackendDisplayName(ma_backend backend)
   {
      switch (backend)
      {
         case ma_backend_pulseaudio: return "PulseAudio";
         case ma_backend_alsa:       return "ALSA";
         case ma_backend_jack:       return "JACK";
         case ma_backend_null:       return "Null";
         default:                    return "Unknown";
      }
   }

   bool EnsureContext(std::string& outError)
   {
      std::lock_guard<std::mutex> lock(gContextMutex);
      if (gContextInited)
         return true;

      ma_backend backends[8];
      ma_uint32 n = 0;

      const char* forced = std::getenv("INFINITE_AUDIO_BACKEND");
      if (forced != nullptr)
      {
         const std::string f = forced;
         if (f == "null")       backends[n++] = ma_backend_null;
         else if (f == "alsa")  backends[n++] = ma_backend_alsa;
         else if (f == "pulse") backends[n++] = ma_backend_pulseaudio;
         else if (f == "jack")  backends[n++] = ma_backend_jack;
      }
      if (n == 0)
      {
         // Plan-mandated order: PulseAudio -> ALSA -> JACK.
         backends[n++] = ma_backend_pulseaudio;
         backends[n++] = ma_backend_alsa;
         backends[n++] = ma_backend_jack;
      }
      // Always available as the last resort, so a machine (or CI runner)
      // with none of the above still starts - matches the plan's
      // INFINITE_AUDIO_BACKEND=null deterministic-test mode being reachable
      // even when it wasn't explicitly requested.
      bool haveNull = false;
      for (ma_uint32 i = 0; i < n; i++)
         if (backends[i] == ma_backend_null) haveNull = true;
      if (!haveNull)
         backends[n++] = ma_backend_null;

      ma_context_config cfg = ma_context_config_init();
      if (ma_context_init(backends, n, &cfg, &gContext) != MA_SUCCESS)
      {
         outError = "miniaudio: could not initialize any audio backend";
         return false;
      }
      gContextInited = true;
      gBackendName = BackendDisplayName(gContext.backend);
      return true;
   }

   // ---- device enumeration ---------------------------------------------------

   struct DeviceEntry
   {
      ma_device_id id{};
      std::string name;
      bool isInput = false;
      int channels = 0;
   };

   std::mutex gDevicesMutex;
   std::vector<DeviceEntry> gDevices;

   int NativeChannelsOf(ma_device_type type, const ma_device_id& id)
   {
      ma_device_info info{};
      if (ma_context_get_device_info(&gContext, type, &id, &info) == MA_SUCCESS &&
          info.nativeDataFormatCount > 0)
         return (int)info.nativeDataFormats[0].channels;
      return 2;
   }

   void RefreshDeviceList()
   {
      std::string err;
      if (!EnsureContext(err))
         return;

      ma_device_info* playback = nullptr;
      ma_uint32 playbackCount = 0;
      ma_device_info* capture = nullptr;
      ma_uint32 captureCount = 0;
      if (ma_context_get_devices(&gContext, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS)
         return;

      std::vector<DeviceEntry> list;
      for (ma_uint32 i = 0; i < playbackCount; i++)
      {
         DeviceEntry entry;
         entry.id = playback[i].id;
         entry.name = std::string("[") + gBackendName + "] " + playback[i].name;
         entry.isInput = false;
         entry.channels = NativeChannelsOf(ma_device_type_playback, playback[i].id);
         list.push_back(entry);
      }
      for (ma_uint32 i = 0; i < captureCount; i++)
      {
         DeviceEntry entry;
         entry.id = capture[i].id;
         entry.name = std::string("[") + gBackendName + "] " + capture[i].name;
         entry.isInput = true;
         entry.channels = NativeChannelsOf(ma_device_type_capture, capture[i].id);
         list.push_back(entry);
      }

      std::lock_guard<std::mutex> lock(gDevicesMutex);
      gDevices = std::move(list);
   }

   // Same 1-based combined index space as AudioDeviceWin.cpp's
   // ResolveEndpoint: 0 = default, 1..N = gDevices in enumeration order
   // (playback entries first, then capture). outIsInput, when given, lets a
   // caller reject a stale index that now resolves to the wrong device kind
   // (see AudioDeviceOpen's fallback-to-default comment) without a second,
   // deadlocking lock of gDevicesMutex.
   bool ResolveDevice(uint32_t deviceId, ma_device_id& outId, std::string& outName, bool* outIsInput = nullptr)
   {
      std::lock_guard<std::mutex> lock(gDevicesMutex);
      if (deviceId == 0 || deviceId > gDevices.size())
         return false;
      outId = gDevices[deviceId - 1].id;
      outName = gDevices[deviceId - 1].name;
      if (outIsInput != nullptr)
         *outIsInput = gDevices[deviceId - 1].isInput;
      return true;
   }

   std::atomic<bool> gConfigChangedFlag{ false };

   void NotificationCallback(const ma_device_notification* pNotification)
   {
      if (pNotification->type == ma_device_notification_type_rerouted ||
          pNotification->type == ma_device_notification_type_stopped)
         gConfigChangedFlag.store(true, std::memory_order_release);
   }

   // ---- render device ---------------------------------------------------------

   struct RenderState
   {
      ma_device device{};
      bool deviceInited = false;

      Platform::AudioRenderCallback callback = nullptr;
      void* userData = nullptr;

      double sampleRate = 0.0;
      int channels = 0;
      uint32_t bufferFrames = 0;

      // Planar scratch handed to the callback, then interleaved into
      // miniaudio's output buffer. Sized once at Open, never in the
      // real-time callback (windows-parity SS3's "no allocation on the
      // audio thread" rule, restated for Linux by linux-parity).
      std::vector<float> planarScratch;

      void Stop()
      {
         if (deviceInited)
         {
            // ma_device_uninit() stops the device and joins miniaudio's own
            // internal worker thread before returning - there is no
            // std::thread of ours to manage here, so windows-parity SS3.1's
            // "never gate join() on your own running flag" simply doesn't
            // apply: we never own the join in the first place, and
            // miniaudio's own teardown is unconditional and always runs.
            ma_device_uninit(&device);
            deviceInited = false;
         }
         callback = nullptr;
         userData = nullptr;
         sampleRate = 0.0;
         channels = 0;
         bufferFrames = 0;
      }
   };

   RenderState gRender;

   void RenderDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
   {
      RenderState* self = static_cast<RenderState*>(pDevice->pUserData);
      float* out = static_cast<float*>(pOutput);
      const int chs = self->channels;

      // Never hand the callback more than our planar scratch holds; excess
      // frames (should not happen in practice - periodSizeInFrames is
      // negotiated at Open) are silenced rather than read out of bounds.
      const int frames = (int)std::min<ma_uint32>(frameCount, (ma_uint32)kPlanarCapacity);

      float* planar[kMaxChannels];
      for (int ch = 0; ch < chs; ch++)
         planar[ch] = self->planarScratch.data() + (size_t)ch * kPlanarCapacity;
      std::fill(self->planarScratch.begin(), self->planarScratch.begin() + (size_t)chs * frames, 0.0f);

      if (self->callback != nullptr)
         self->callback(planar, chs, frames, self->userData);

      for (int i = 0; i < frames; i++)
         for (int ch = 0; ch < chs; ch++)
            out[(size_t)i * chs + ch] = planar[ch][i];
      for (ma_uint32 i = (ma_uint32)frames; i < frameCount; i++)
         for (int ch = 0; ch < chs; ch++)
            out[(size_t)i * chs + ch] = 0.0f;
   }

   // ---- capture engines (analyser + input-tap share this base) ---------------

   std::atomic<uint32_t> gRequestedInputDeviceId{ 0 };

   struct LatestRing
   {
      static constexpr int kCapacity = 1 << 16; // 65536 frames/channel

      std::vector<float> buffers; // channels * kCapacity
      std::atomic<uint64_t> writeIndex{ 0 };
      int channels = 0;

      void Init(int chs)
      {
         channels = chs;
         buffers.assign((size_t)std::max(1, channels) * kCapacity, 0.0f);
         writeIndex.store(0, std::memory_order_release);
      }

      void PushFrame(const float* interleaved)
      {
         const uint64_t idx = writeIndex.load(std::memory_order_relaxed);
         float* base = buffers.data() + (idx % kCapacity);
         for (int ch = 0; ch < channels; ch++)
            base[(size_t)ch * kCapacity] = interleaved[ch];
         writeIndex.store(idx + 1, std::memory_order_release);
      }
   };

   // Common plumbing for a capture-only ma_device delivering interleaved
   // float frames to a virtual OnFrames(). Unlike CaptureEngineBase on
   // Windows, there's no async setup handshake to poll for: ma_device_init
   // negotiates the format synchronously, so OnFormat() runs right after a
   // successful init and before ma_device_start().
   struct CaptureEngineBase
   {
      ma_device device{};
      bool deviceInited = false;
      double sampleRate = 0.0;
      int channels = 0;
      std::string error;

      virtual ~CaptureEngineBase() = default;
      virtual void OnFormat(double rate, int chs) = 0;
      virtual void OnFrames(const float* interleaved, int frames) = 0;

      static void DataCallback(ma_device* pDevice, void* /*pOutput*/, const void* pInput, ma_uint32 frameCount)
      {
         CaptureEngineBase* self = static_cast<CaptureEngineBase*>(pDevice->pUserData);
         if (pInput != nullptr && frameCount > 0)
            self->OnFrames(static_cast<const float*>(pInput), (int)frameCount);
      }

      bool Start(std::string& outError)
      {
         Stop();
         if (!EnsureContext(outError))
            return false;

         ma_device_id devId{};
         ma_device_id* pDevId = nullptr;
         const uint32_t reqId = gRequestedInputDeviceId.load(std::memory_order_relaxed);
         if (reqId != 0)
         {
            std::string name;
            if (ResolveDevice(reqId, devId, name))
               pDevId = &devId;
         }

         ma_device_config config = ma_device_config_init(ma_device_type_capture);
         config.capture.pDeviceID = pDevId;
         config.capture.format = ma_format_f32;
         config.capture.channels = 0; // 0 = use the device's native channel count
         config.dataCallback = &CaptureEngineBase::DataCallback;
         config.pUserData = this;

         if (ma_device_init(&gContext, &config, &device) != MA_SUCCESS)
         {
            outError = "could not initialize audio input device";
            return false;
         }
         deviceInited = true;

         channels = std::clamp((int)device.capture.channels, 1, kMaxChannels);
         sampleRate = (double)device.sampleRate;
         OnFormat(sampleRate, channels);

         if (ma_device_start(&device) != MA_SUCCESS)
         {
            outError = "could not start audio input device";
            ma_device_uninit(&device);
            deviceInited = false;
            sampleRate = 0.0;
            return false;
         }
         return true;
      }

      void Stop()
      {
         if (deviceInited)
         {
            // Same note as RenderState::Stop(): ma_device_uninit() owns and
            // joins miniaudio's internal capture thread unconditionally, so
            // there is no self-owned running flag to gate a join on here.
            ma_device_uninit(&device);
            deviceInited = false;
         }
         sampleRate = 0.0;
      }

      bool IsRunning() const { return deviceInited && sampleRate > 0.0; }
   };

   // ---- analyser (AudioStart/AudioRead family) --------------------------------

   struct AnalyserEngine : CaptureEngineBase
   {
      static constexpr int kFftLog2 = AudioAnalysisCommon::kFftLog2;
      static constexpr int kFftSize = AudioAnalysisCommon::kFftSize;
      static constexpr int kBins = AudioAnalysisCommon::kBins;

      PortableFft::RealFft fft;
      float window[kFftSize] = {};
      float ring[kFftSize] = {};
      int ringFill = 0;
      float prevMagnitude[kBins] = {};
      float prevFlux = 0.0f;

      std::atomic<float> gain{ 1.0f };
      std::atomic<float> attack{ 0.5f };
      std::atomic<float> release{ 0.12f };

      std::mutex levelsMutex;
      Platform::AudioLevels levels;

      void OnFormat(double rate, int chs) override
      {
         sampleRate = rate;
         channels = std::min(chs, 2); // analyse mono/stereo mixdown
         PortableFft::HannWindowNorm(window, kFftSize);
      }

      // Ring accumulation + FFT/band/onset math live in
      // ../common/AudioAnalysis.h, shared verbatim with Windows.
      void OnFrames(const float* interleaved, int frames) override
      {
         const float g = gain.load(std::memory_order_relaxed);
         Platform::AudioLevels next;
         bool changed = false;
         AudioAnalysisCommon::PushFrames(interleaved, frames, channels, g, sampleRate, ring, ringFill,
                                         fft, window, prevMagnitude, prevFlux, next, changed);
         if (changed)
         {
            std::lock_guard<std::mutex> lock(levelsMutex);
            levels = next;
         }
      }
   };

   AnalyserEngine gAnalyser;

   // ---- input capture tap ------------------------------------------------------

   struct CaptureTapEngine : CaptureEngineBase
   {
      LatestRing ring;
      // Allocate first, publish last - same reasoning as
      // AudioDeviceWin.cpp's CaptureTapEngine::ready: AudioInputCaptureRead
      // runs on the audio render thread and must never observe a
      // partially-(re)allocated ring.
      std::atomic<bool> ready{ false };

      void OnFormat(double rate, int chs) override
      {
         ready.store(false, std::memory_order_release);
         channels = std::clamp(chs, 1, kMaxChannels);
         ring.Init(channels);
         sampleRate = rate;
         ready.store(true, std::memory_order_release);
      }

      void OnFrames(const float* interleaved, int frames) override
      {
         const int chs = channels;
         for (int i = 0; i < frames; i++)
            ring.PushFrame(interleaved + (size_t)i * chs);
      }
   };

   CaptureTapEngine gTap;
   std::atomic<int> gTapRefs{ 0 };
   uint32_t gActiveInputDeviceId = 0;
}

namespace Platform
{
   // ---- file dialog / P0 spike / sleep-wake ---------------------------------
   // OpenAudioDialog, AudioSpikeStart/Stop/GetStats, AudioWillSleep/DidWake
   // are unchanged from the pre-existing stub - see git history for
   // AudioDeviceLinux.cpp before Phase 2. They live in a separate TU-local
   // block below, kept exactly as they were.

   bool AudioPcmConversionSelfTest()
   {
      // miniaudio negotiates ma_format_f32 directly and does its own
      // native<->float conversion internally (see this file's header
      // comment) - there is no hand-rolled PCM<->float path on Linux for
      // this test to exercise, unlike WASAPI shared mode on Windows
      // (AudioDeviceWin.cpp's IsSupportedPcmFormat/PlanarFloatToInterleavedPcm).
      // Trivially true, same as macOS's Platform.mm, which never negotiates
      // PCM here either.
      std::printf("AUDIOPCMTEST OK\n");
      return true;
   }

   // ---- audio engine render callback bridge ---------------------------------

   bool AudioDeviceOpen(AudioRenderCallback callback, void* userData, double& outSampleRate,
                        std::string& outError, uint32_t requestedDeviceId,
                        double requestedSampleRate, int requestedBufferFrames)
   {
      outError.clear();
      outSampleRate = 0.0;

      AudioDeviceClose(); // idempotent restart path, matches Windows/macOS

      if (!EnsureContext(outError))
      {
         outSampleRate = 48000.0;
         return false;
      }
      RefreshDeviceList();

      // requestedDeviceId is the same 1-based combined-index scheme as
      // Windows's ResolveEndpoint (see ResolveDevice's comment above) -
      // persisted verbatim in Infinite.audio-settings, so it goes stale the
      // moment the device list reorders (unplug/replug, or any device
      // added/removed shifts every index after it), same hazard as macOS's
      // stale AudioObjectID. An out-of-range index, or one that now
      // resolves to a capture-only device, falls back to miniaudio's
      // default playback device (pDevId left null) rather than failing the
      // whole engine start - requestedDeviceId itself (main.cpp's
      // gAudioOutputDeviceId) is left untouched, same as macOS/Windows.
      ma_device_id devId{};
      ma_device_id* pDevId = nullptr;
      if (requestedDeviceId != 0)
      {
         std::string name;
         bool isInput = false;
         if (ResolveDevice(requestedDeviceId, devId, name, &isInput) && !isInput)
            pDevId = &devId;
      }

      gRender.callback = callback;
      gRender.userData = userData;
      gRender.planarScratch.assign((size_t)kMaxChannels * kPlanarCapacity, 0.0f);

      ma_device_config config = ma_device_config_init(ma_device_type_playback);
      config.playback.pDeviceID = pDevId;
      config.playback.format = ma_format_f32;
      config.playback.channels = 0; // 0 = device's native channel count
      // requestedSampleRate/requestedBufferFrames are hints (Platform.h's
      // contract): 0 means "whatever the device already runs at". miniaudio
      // treats sampleRate/periodSizeInFrames the same way - 0 lets the
      // backend pick its native rate/period - and what actually gets
      // negotiated is read back below into outSampleRate /
      // AudioDeviceBufferFrames, exactly like the Windows shared-mode path.
      config.sampleRate = requestedSampleRate > 0.0 ? (ma_uint32)requestedSampleRate : 0;
      if (requestedBufferFrames > 0)
         config.periodSizeInFrames = (ma_uint32)requestedBufferFrames;
      config.dataCallback = RenderDataCallback;
      config.notificationCallback = NotificationCallback;
      config.pUserData = &gRender;

      if (ma_device_init(&gContext, &config, &gRender.device) != MA_SUCCESS)
      {
         outError = "could not initialize audio output device";
         outSampleRate = 48000.0;
         gRender.callback = nullptr;
         gRender.userData = nullptr;
         return false;
      }
      gRender.deviceInited = true;
      gRender.channels = std::clamp((int)gRender.device.playback.channels, 1, kMaxChannels);
      gRender.sampleRate = (double)gRender.device.sampleRate;
      gRender.bufferFrames = gRender.device.playback.internalPeriodSizeInFrames;

      if (ma_device_start(&gRender.device) != MA_SUCCESS)
      {
         outError = "could not start audio output device";
         AudioDeviceClose();
         outSampleRate = 48000.0;
         return false;
      }

      outSampleRate = gRender.sampleRate;
      return true;
   }

   void AudioDeviceClose()
   {
      gRender.Stop();
   }

   uint32_t AudioDeviceBufferFrames(uint32_t deviceId)
   {
      if (deviceId == 0 && gRender.deviceInited && gRender.bufferFrames > 0)
         return gRender.bufferFrames;

      std::string err;
      if (!EnsureContext(err))
         return 0;

      ma_device_id devId{};
      bool haveId = false;
      if (deviceId != 0)
      {
         std::string name;
         haveId = ResolveDevice(deviceId, devId, name);
         if (!haveId)
            return 0;
      }

      ma_device_info info{};
      const ma_result res = ma_context_get_device_info(
         &gContext, ma_device_type_playback, haveId ? &devId : nullptr, &info);
      if (res != MA_SUCCESS)
         return 0;
      // Best-effort: report the device's default/native sample rate's
      // implied period is not directly exposed pre-open, so fall back to a
      // representative 512-frame hint (matches this file's pre-Phase-2
      // stub's constant and AudioDeviceWin.cpp's "just tell the caller
      // something plausible before a device is actually open" precedent).
      (void)info;
      return 512;
   }

   bool AudioDeviceConfigDidChange()
   {
      return gConfigChangedFlag.exchange(false, std::memory_order_acq_rel);
   }

   void AudioDeviceDebugSimulateConfigChange()
   {
      gConfigChangedFlag.store(true, std::memory_order_release);
   }

   std::vector<AudioDeviceInfo> AudioListDevices()
   {
      std::string err;
      EnsureContext(err); // best-effort; an empty list is a legitimate answer
      RefreshDeviceList();

      std::vector<AudioDeviceInfo> out;
      std::lock_guard<std::mutex> lock(gDevicesMutex);
      for (size_t i = 0; i < gDevices.size(); i++)
      {
         AudioDeviceInfo info;
         info.name = gDevices[i].name;
         info.deviceId = (uint32_t)(i + 1);
         info.isInput = gDevices[i].isInput;
         info.isOutput = !gDevices[i].isInput;
         info.inputChannels = gDevices[i].isInput ? gDevices[i].channels : 0;
         info.outputChannels = !gDevices[i].isInput ? gDevices[i].channels : 0;
         out.push_back(std::move(info));
      }
      return out;
   }

   // ---- audio input analysis -------------------------------------------------

   bool AudioStart(std::string& outError)
   {
      return gAnalyser.Start(outError);
   }

   void AudioStop()
   {
      gAnalyser.Stop();
   }

   bool AudioIsRunning()
   {
      return gAnalyser.IsRunning();
   }

   std::string AudioDeviceName()
   {
      return AudioIsRunning() ? "system default input" : std::string();
   }

   bool AudioRead(AudioLevels& out)
   {
      if (!AudioIsRunning())
      {
         out = AudioLevels{};
         return false;
      }

      std::lock_guard<std::mutex> lock(gAnalyser.levelsMutex);
      out = gAnalyser.levels;
      gAnalyser.levels.onset = false; // consumed on read, like macOS/Windows

      // Smoothing on read, same coefficients/fields as
      // AudioDeviceWin.cpp::AudioRead (which itself matches Platform.mm's
      // Analyser::Smooth - rms/peak/low/mid/high AND bands[], not bands[]
      // alone).
      const float a = gAnalyser.attack.load(std::memory_order_relaxed);
      const float r = gAnalyser.release.load(std::memory_order_relaxed);
      auto smooth = [a, r](float& prev, float target) {
         prev += (target - prev) * (target > prev ? a : r);
         return prev;
      };
      static float sPrev[kAudioBands] = {};
      static float sPrevRms = 0.0f, sPrevPeak = 0.0f;
      static float sPrevLow = 0.0f, sPrevMid = 0.0f, sPrevHigh = 0.0f;
      for (int b = 0; b < kAudioBands; b++)
         out.bands[b] = smooth(sPrev[b], out.bands[b]);
      out.rms = smooth(sPrevRms, out.rms);
      out.peak = smooth(sPrevPeak, out.peak);
      out.low = smooth(sPrevLow, out.low);
      out.mid = smooth(sPrevMid, out.mid);
      out.high = smooth(sPrevHigh, out.high);
      return true;
   }

   void AudioSetSmoothing(float smoothingAttack, float smoothingRelease)
   {
      gAnalyser.attack.store(std::clamp(smoothingAttack, 0.001f, 1.0f), std::memory_order_relaxed);
      gAnalyser.release.store(std::clamp(smoothingRelease, 0.001f, 1.0f), std::memory_order_relaxed);
   }

   void AudioSetGain(float gainDbOrLinear)
   {
      gAnalyser.gain.store(gainDbOrLinear, std::memory_order_relaxed);
   }

   // ---- audio input capture (Audio In node) ----------------------------------

   void AudioInputCaptureAddRef()
   {
      gTapRefs.fetch_add(1, std::memory_order_relaxed);
   }

   void AudioInputCaptureRemoveRef()
   {
      const int prev = gTapRefs.fetch_sub(1, std::memory_order_relaxed);
      if (prev <= 1 && gTap.deviceInited)
         gTap.Stop(); // last consumer gone - tear the tap down promptly
   }

   void AudioInputCaptureSetDevice(uint32_t deviceId)
   {
      gRequestedInputDeviceId.store(deviceId, std::memory_order_relaxed);
   }

   uint32_t AudioInputCaptureGetDevice()
   {
      return gRequestedInputDeviceId.load(std::memory_order_relaxed);
   }

   void AudioInputCapturePump(std::string& outError)
   {
      outError.clear();
      if (gTapRefs.load(std::memory_order_relaxed) <= 0)
         return;
      const uint32_t reqDevice = gRequestedInputDeviceId.load(std::memory_order_relaxed);
      if (gTap.IsRunning())
      {
         if (gActiveInputDeviceId == reqDevice)
            return; // healthy
         gTap.Stop();
      }
      gActiveInputDeviceId = reqDevice;
      std::string err;
      if (!gTap.Start(err))
         outError = err; // reported by the node's status text, retried next frame
   }

   bool AudioInputCaptureIsRunning()
   {
      return gTap.IsRunning() && gTap.ready.load(std::memory_order_acquire);
   }

   int AudioInputCaptureRead(float* const* outChannels, int numFrames, int maxChannels,
                             uint64_t& readerCursor, int channelOffset, bool isMono)
   {
      if (outChannels == nullptr || numFrames <= 0 || maxChannels <= 0)
         return 0;
      if (!AudioInputCaptureIsRunning() || gTap.ring.channels == 0)
      {
         for (int ch = 0; ch < maxChannels; ch++)
            std::fill(outChannels[ch], outChannels[ch] + numFrames, 0.0f);
         return 0;
      }

      const int availableChannels = gTap.ring.channels;
      const uint64_t w = gTap.ring.writeIndex.load(std::memory_order_acquire);
      uint64_t cursor = readerCursor;

      if (cursor == 0 || w - cursor > (uint64_t)LatestRing::kCapacity || cursor > w)
         cursor = (w > (uint64_t)numFrames) ? w - numFrames : 0;

      const uint64_t available = w - cursor;
      const int n = (int)std::min<uint64_t>(numFrames, available);
      const int cap = LatestRing::kCapacity;

      if (isMono)
      {
         const int ch = std::clamp(channelOffset, 0, availableChannels - 1);
         for (int i = 0; i < n; i++)
         {
            const uint64_t slot = (cursor + i) % cap;
            outChannels[0][i] = gTap.ring.buffers[(size_t)ch * cap + slot];
         }
         for (int i = n; i < numFrames; i++)
            outChannels[0][i] = 0.0f;
         if (maxChannels > 1)
            std::copy(outChannels[0], outChannels[0] + numFrames, outChannels[1]);
         readerCursor = cursor + n;
         return n > 0 ? 1 : 0;
      }
      else
      {
         const int ch0 = std::clamp(channelOffset, 0, availableChannels - 1);
         const int ch1 = (channelOffset + 1 < availableChannels) ? (channelOffset + 1) : ch0;
         for (int i = 0; i < n; i++)
         {
            const uint64_t slot = (cursor + i) % cap;
            outChannels[0][i] = gTap.ring.buffers[(size_t)ch0 * cap + slot];
            if (maxChannels > 1)
               outChannels[1][i] = gTap.ring.buffers[(size_t)ch1 * cap + slot];
         }
         for (int i = n; i < numFrames; i++)
         {
            outChannels[0][i] = 0.0f;
            if (maxChannels > 1)
               outChannels[1][i] = 0.0f;
         }
         readerCursor = cursor + n;
         return (n > 0) ? (ch0 != ch1 ? 2 : 1) : 0;
      }
   }

   int AudioInputCaptureRead(float* const* outChannels, int numFrames, int maxChannels)
   {
      static uint64_t sLegacyCursor = 0;
      return AudioInputCaptureRead(outChannels, numFrames, maxChannels, sLegacyCursor, 0, false);
   }
}

// ---------------------------------------------------------------------------
// Unchanged from the pre-Phase-2 stub: file dialog, the P0 audio-synthesis
// spike (never wired into product UI on any platform - PlatformWin.cpp:662
// and Platform.mm decline it the same way), and sleep/wake (Linux has no
// such notification concept, same as Windows).
// ---------------------------------------------------------------------------
#include "tinyfiledialogs.h"

namespace Platform
{
   std::string OpenAudioDialog()
   {
      if (std::getenv("INFINITE_EXITAFTER") != nullptr) return "";
      const char* disp = std::getenv("DISPLAY");
      const char* wayland = std::getenv("WAYLAND_DISPLAY");
      if ((!disp || disp[0] == '\0') && (!wayland || wayland[0] == '\0')) return "";

      const char* const filterPatterns[] = {
         "*.wav", "*.aif", "*.aiff", "*.mp3", "*.flac"
      };
      const char* res = tinyfd_openFileDialog(
         "Choose Audio File",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "Audio files (*.wav, *.aif, *.aiff, *.mp3, *.flac)",
         0
      );
      return res ? std::string(res) : std::string();
   }

   bool AudioSpikeStart(std::string& outError)
   {
      // Not a phase stub: this is a P0 throwaway that was never wired into
      // the product UI, and PlatformWin.cpp:662 declines it for the same
      // reason. No phase tag, because no phase will implement it.
      outError = "not supported on Linux";
      return false;
   }

   void AudioSpikeStop()
   {
   }

   AudioSpikeStats AudioSpikeGetStats()
   {
      return AudioSpikeStats{};
   }

   bool AudioWillSleep()
   {
      return false;
   }

   bool AudioDidWake()
   {
      return false;
   }
}

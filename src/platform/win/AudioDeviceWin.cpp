// Windows implementation of the Platform facade's audio-device surface,
// replacing the macOS AVAudioEngine/CoreAudio stack:
//
//   - AudioDeviceOpen/Close: WASAPI shared-mode, event-driven render loop on
//     a dedicated thread; the app's render callback is handed planar float
//     buffers exactly like the macOS version did, then interleaved into the
//     WASAPI endpoint buffer here. Optional opt-in modes (Platform::
//     AudioSetOutputMode): IAudioClient3 minimum-period shared stream, and
//     WASAPI exclusive. Every failure falls back one step toward the default
//     shared path, so a request can never leave the app without audio.
//   - AudioRoundTripLatencyFrames: output stream latency (measured off the
//     open stream) + input endpoint period (cached), for the settings readout.
//   - AudioListDevices / AudioDeviceBufferFrames: MMDevice enumeration.
//   - Config-change recovery: an IMMNotificationClient latches the same
//     atomic PollAudioRecovery consumes (sleep/wake flags stay false - those
//     were NSWorkspace notifications).
//   - AudioStart/AudioRead: a separate default-input capture engine feeding
//     the same smoothed level/band/onset analysis the old live analyser ran.
//   - AudioInputCapture*: refcounted default-input tap draining a lock-free
//     "most recent samples" ring for AudioNodes.cpp's AudioInputNode.
//
// Everything else obeys the contract documented in ../Platform.h.

#include "../Platform.h"

#include "WinCommon.h"

#include "../common/AudioAnalysis.h"
#include "dsp/PortableFft.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
// Must follow mmdeviceapi.h: it leans on propkeydef.h's DEFINE_PROPERTYKEY
// declaration that mmdeviceapi's include chain sets up.
#include <functiondiscoverykeys_devpkey.h>

// MMCSS, for the render thread's scheduling priority (see ProAudioScope).
#ifdef _MSC_VER
#pragma comment(lib, "avrt.lib")
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// Defined in AudioEngine.cpp (the macOS backend reaches it the same way, see
// Platform.mm). Bumps AudioEngine's OS-reported xrun counter; atomic-only.
// This file is not compiled into infinite-vst3-scanner, so the symbol always
// links.
extern "C" void AudioEngine_NotifyProcessorOverload(void* engineInstance);

namespace
{
   constexpr int kMaxChannels = 8;          // matches kAudioMaxChannels in AudioEngine.h
   constexpr int kPlanarCapacity = 4096;    // matches kAudioMaxBlockFrames

   // Synthetic device ids handed out by AudioListDevices: 0 is always "system
   // default", real devices are 1..N in enumeration order. The mapping is
   // refreshed on every enumeration/open; endpoints are identified internally
   // by their immutable IMMDevice id string.
   struct DeviceEntry
   {
      std::wstring endpointId;
      std::string name;
      bool isInput = false;
      int channels = 0;
   };

   std::mutex gDevicesMutex;
   std::vector<DeviceEntry> gDevices;

   std::atomic<bool> gConfigChangedFlag{ false };

   // Output-stream mode (Platform::AudioSetOutputMode): 0 standard shared
   // (today's behaviour, the default), 1 IAudioClient3 low-latency shared,
   // 2 WASAPI exclusive. `Active` is what the open stream really runs after
   // fallback. `Disabled` latches when an exclusive stream dies at runtime
   // (device invalidated, format changed under us) so the recovery restart
   // that follows does not walk straight back into the same failure; it is
   // cleared whenever the user changes the setting.
   std::atomic<int> gOutputModeRequested{ 0 };
   std::atomic<int> gOutputModeActive{ 0 };
   std::atomic<bool> gExclusiveDisabled{ false };
   // Output-side latency estimate of the open stream, in frames at its rate
   // (0 = none open / unknown). Written once by the render thread at setup.
   std::atomic<uint32_t> gOutputLatencyFrames{ 0 };

   class NotificationClient : public IMMNotificationClient
   {
   public:
      // IUnknown
      STDMETHODIMP QueryInterface(REFIID riid, void** out) override
      {
         if (out == nullptr)
            return E_POINTER;
         if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
         {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
         }
         *out = nullptr;
         return E_NOINTERFACE;
      }
      STDMETHODIMP_(ULONG) AddRef() override { return mRef.fetch_add(1) + 1; }
      STDMETHODIMP_(ULONG) Release() override
      {
         const ULONG r = mRef.fetch_sub(1) - 1;
         return r; // process-lifetime singleton: never delete
      }

      // IMMNotificationClient - everything we don't care about succeeds idle.
      STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
      STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
      STDMETHODIMP OnDeviceRemoved(LPCWSTR) override
      {
         gConfigChangedFlag.store(true, std::memory_order_release);
         return S_OK;
      }
      STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
      {
         // Only console/multimedia roles on the flows we use matter.
         if ((flow == eRender || flow == eCapture) &&
             (role == eConsole || role == eMultimedia))
            gConfigChangedFlag.store(true, std::memory_order_release);
         return S_OK;
      }
      // PROPERTYKEY is passed BY VALUE in the SDK declaration (unlike most
      // COM interfaces) - a const& here silently fails to override and leaves
      // the class abstract.
      STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

   private:
      std::atomic<ULONG> mRef{ 1 };
   };

   NotificationClient gNotifyClient;

   // Every helper here runs inside an STA-neutral worker or the main thread;
   // MTA init per-thread is the safe common denominator for MMDevice/WASAPI.
   struct ComScope
   {
      bool ok = false;
      bool owned = false; // did WE initialize COM (so must balance CoUninitialize)?
      explicit ComScope()
      {
         const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
         if (SUCCEEDED(hr))
         {
            // S_OK or S_FALSE (already MTA): we now hold a COM reference to
            // release on scope exit.
            ok = true;
            owned = true;
         }
         else if (hr == RPC_E_CHANGED_MODE)
         {
            // COM is already initialized on this thread in a DIFFERENT apartment
            // (STA) - this happens once the JUCE plugin backend puts the main
            // thread into STA at startup. MMDevice/WASAPI works fine in STA, so
            // use the existing apartment; do NOT CoUninitialize (it isn't ours).
            ok = true;
            owned = false;
         }
      }
      ~ComScope() { if (owned) CoUninitialize(); }
   };

   // Opts the render thread into the Multimedia Class Scheduler Service for
   // the duration of its life. Without this it is an ordinary thread at
   // ordinary priority, freely preempted by our own UI thread compiling a
   // shader, a plugin scan, or anything else the machine feels like running -
   // and every preemption that outlasts the buffer deadline is an audible
   // click, because WASAPI plays whatever is in the buffer regardless.
   //
   // macOS never needed an equivalent: CoreAudio invokes the render callback
   // on its own HAL I/O thread, which the kernel has already granted
   // time-constraint scheduling. On Windows the deadline guarantee is opt-in,
   // and "Pro Audio" is the MMCSS profile meant for exactly this.
   //
   // Failure is deliberately non-fatal. MMCSS can decline (it is a system
   // service and can be disabled), and audio at ordinary priority is still
   // audio - it just glitches under load, which is what we had before.
   struct ProAudioScope
   {
      HANDLE task = nullptr;
      ProAudioScope()
      {
         DWORD taskIndex = 0;
         task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
         if (task == nullptr)
            task = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
      }
      ~ProAudioScope() { if (task != nullptr) AvRevertMmThreadCharacteristics(task); }
      ProAudioScope(const ProAudioScope&) = delete;
      ProAudioScope& operator=(const ProAudioScope&) = delete;
   };

   void RefreshDeviceList(IMMDeviceEnumerator* enumerator)
   {
      std::vector<DeviceEntry> list;
      auto collect = [&](EDataFlow flow, bool isInput) {
         IMMDeviceCollection* collection = nullptr;
         if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) ||
             collection == nullptr)
            return;
         UINT count = 0;
         collection->GetCount(&count);
         for (UINT i = 0; i < count; i++)
         {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)) || device == nullptr)
               continue;
            LPWSTR id = nullptr;
            IPropertyStore* props = nullptr;
            DeviceEntry entry;
            entry.isInput = isInput;
            if (SUCCEEDED(device->GetId(&id)) && id != nullptr)
            {
               entry.endpointId = id;
               CoTaskMemFree(id);
            }
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props != nullptr)
            {
               PROPVARIANT var;
               PropVariantInit(&var);
               if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) &&
                   var.vt == VT_LPWSTR)
               {
                  entry.name = WinCommon::WideToUtf8(var.pwszVal);
               }
               PropVariantClear(&var);
               props->Release();
            }
            IAudioClient* queryClient = nullptr;
            if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(&queryClient))) && queryClient != nullptr)
            {
               WAVEFORMATEX* queryFmt = nullptr;
               if (SUCCEEDED(queryClient->GetMixFormat(&queryFmt)) && queryFmt != nullptr)
               {
                  entry.channels = queryFmt->nChannels;
                  CoTaskMemFree(queryFmt);
               }
               queryClient->Release();
            }
            if (!entry.endpointId.empty())
               list.push_back(std::move(entry));
            device->Release();
         }
         if (collection != nullptr)
            collection->Release();
      };

      collect(eRender, false);
      collect(eCapture, true);

      std::lock_guard<std::mutex> lock(gDevicesMutex);
      gDevices = std::move(list);
   }

   // outIsInput, when given, lets a caller reject a stale index that now
   // resolves to the wrong device kind (see AudioDeviceOpen's
   // fallback-to-default comment) without a second lock of gDevicesMutex.
   bool ResolveEndpoint(uint32_t deviceId, std::wstring& outEndpointId, std::string& outName,
                        bool* outIsInput = nullptr)
   {
      std::lock_guard<std::mutex> lock(gDevicesMutex);
      if (deviceId == 0 || deviceId > gDevices.size())
         return false;
      outEndpointId = gDevices[deviceId - 1].endpointId;
      outName = gDevices[deviceId - 1].name;
      if (outIsInput != nullptr)
         *outIsInput = gDevices[deviceId - 1].isInput;
      return true;
   }

   bool IsFloatFormat(const WAVEFORMATEX* fmt)
   {
      if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
         return true;
      if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
      {
         const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
         return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
      }
      return false;
   }

   // Some shared-mode endpoints negotiate a PCM mix format instead of float
   // (docs/plans/windows-render/FIX_BRIEF.md addendum A1) - WASAPI shared mode
   // won't let us demand float, so the render/capture paths must convert
   // rather than refuse. Only container sizes of 16 and 32 bits are accepted:
   // 16-bit PCM, plain 32-bit PCM, and 24-bit-in-32-container PCM (the driver
   // reports wBitsPerSample == 32 for the last two either way; both left-
   // justify their significant bits in the 32-bit word, so they convert
   // identically as full-range int32 - there is nothing container-size 24
   // (tightly packed, 3 bytes/sample) to worry about here, and this codebase
   // has never seen a driver report one).
   bool IsSupportedPcmFormat(const WAVEFORMATEX* fmt)
   {
      if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
      {
         const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
         if (!IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
            return false;
      }
      else if (fmt->wFormatTag != WAVE_FORMAT_PCM)
         return false;
      return fmt->wBitsPerSample == 16 || fmt->wBitsPerSample == 32;
   }

   // planar float (engine's internal contract) -> interleaved PCM bytes for
   // the WASAPI render buffer. `bits` is the negotiated container size (16 or
   // 32; see IsSupportedPcmFormat for why 32 covers both int32 and 24-in-32).
   void PlanarFloatToInterleavedPcm(const float* const* planar, int channels, int frames,
                                    WORD bits, BYTE* dest)
   {
      if (bits == 16)
      {
         int16_t* out = reinterpret_cast<int16_t*>(dest);
         for (int i = 0; i < frames; i++)
            for (int ch = 0; ch < channels; ch++)
            {
               const float v = std::clamp(planar[ch][i], -1.0f, 1.0f);
               out[(size_t)i * channels + ch] = (int16_t)std::lround(v * 32767.0f);
            }
      }
      else
      {
         int32_t* out = reinterpret_cast<int32_t*>(dest);
         for (int i = 0; i < frames; i++)
            for (int ch = 0; ch < channels; ch++)
            {
               const float v = std::clamp(planar[ch][i], -1.0f, 1.0f);
               out[(size_t)i * channels + ch] = (int32_t)std::lround((double)v * 2147483647.0);
            }
      }
   }

   // Inverse of the above: interleaved WASAPI PCM bytes -> interleaved float,
   // for the capture path (CaptureEngineBase::OnFrames still receives float).
   void InterleavedPcmToFloat(const BYTE* src, int channels, int frames, WORD bits,
                              std::vector<float>& outInterleaved)
   {
      outInterleaved.resize((size_t)frames * channels);
      const size_t count = (size_t)frames * channels;
      if (bits == 16)
      {
         const int16_t* in = reinterpret_cast<const int16_t*>(src);
         for (size_t i = 0; i < count; i++)
            outInterleaved[i] = in[i] / 32768.0f;
      }
      else
      {
         const int32_t* in = reinterpret_cast<const int32_t*>(src);
         for (size_t i = 0; i < count; i++)
            outInterleaved[i] = (float)(in[i] / 2147483648.0);
      }
   }

   // ---- render device ------------------------------------------------------

   struct RenderState
   {
      // Callback wiring
      Platform::AudioRenderCallback callback = nullptr;
      void* userData = nullptr;

      // Negotiated format
      double sampleRate = 0.0;
      int channels = 0;
      UINT32 bufferFrames = 0;
      WORD pcmBits = 0;    // 0 = float mix format; 16/32 = PCM, see IsSupportedPcmFormat
      int mode = 0;        // stream mode actually opened (see gOutputModeRequested)
      int requestedMode = 0; // snapshot of gOutputModeRequested taken by AudioDeviceOpen

      // Thread machinery
      std::thread thread;
      HANDLE stopEvent = nullptr;
      HANDLE bufferEvent = nullptr;
      std::atomic<bool> running{ false };
      // Latched by the render thread once WASAPI setup has either succeeded
      // or given up, so AudioDeviceOpen can stop polling. `running` cannot be
      // used for this - the thread clears it on the failure path, which is
      // exactly what used to make Stop() skip the join.
      std::atomic<bool> setupDone{ false };

      // Planar scratch handed to the callback, then interleaved into WASAPI.
      std::vector<float> planarScratch;    // channels * kPlanarCapacity
      std::vector<float> interleaveScratch;

      // COM objects owned by the render thread while it runs.
      IMMDeviceEnumerator* enumerator = nullptr;
      IAudioClient* client = nullptr;
      IAudioRenderClient* renderer = nullptr;

      ~RenderState() { Stop(); }

      // Idempotent, and safe to call after the render thread has already
      // exited on its own. The join is gated ONLY on joinable() - never on
      // `running`, which the thread clears itself when setup fails. Getting
      // that wrong left a joinable std::thread that nobody joined, so the
      // next `thread = std::thread(...)` in AudioDeviceOpen (or ~RenderState
      // at exit) called std::terminate() and the process vanished with no
      // dialog. CaptureEngineBase::Stop() below has always had this shape.
      void Stop()
      {
         running.store(false, std::memory_order_release);

         if (stopEvent != nullptr)
            SetEvent(stopEvent);
         if (thread.joinable())
            thread.join();
         setupDone.store(false, std::memory_order_release);

         if (client != nullptr)
            client->Release();
         if (renderer != nullptr)
            renderer->Release();
         if (enumerator != nullptr)
         {
            enumerator->UnregisterEndpointNotificationCallback(&gNotifyClient);
            enumerator->Release();
         }
         enumerator = nullptr;
         client = nullptr;
         renderer = nullptr;
         mode = 0;
         gOutputModeActive.store(0, std::memory_order_release);
         gOutputLatencyFrames.store(0, std::memory_order_release);

         if (stopEvent != nullptr)
         {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
         }
         if (bufferEvent != nullptr)
         {
            CloseHandle(bufferEvent);
            bufferEvent = nullptr;
         }
      }
   };

   RenderState gRender;

   // ---- render stream open attempts ------------------------------------------
   // Each attempt starts from a freshly activated IAudioClient (a client that
   // failed Initialize cannot be reused), leaves gRender.client/renderer null
   // on failure, and on success has the stream started with gRender.sampleRate,
   // channels, pcmBits, bufferFrames and mode filled in. Render thread only.

   // Frees a CoTaskMem-allocated WAVEFORMATEX on every exit path.
   struct MixFormatGuard
   {
      WAVEFORMATEX* fmt = nullptr;
      ~MixFormatGuard() { if (fmt != nullptr) CoTaskMemFree(fmt); }
   };

   void ReleaseRenderStream()
   {
      if (gRender.renderer != nullptr)
      {
         gRender.renderer->Release();
         gRender.renderer = nullptr;
      }
      if (gRender.client != nullptr)
      {
         gRender.client->Release();
         gRender.client = nullptr;
      }
      gRender.sampleRate = 0.0;
      gRender.channels = 0;
      gRender.bufferFrames = 0;
      gRender.pcmBits = 0;
   }

   bool ActivateRenderClient(IMMDevice* device)
   {
      ReleaseRenderStream();
      const HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**>(&gRender.client));
      if (FAILED(hr) || gRender.client == nullptr)
      {
         gRender.client = nullptr;
         return false;
      }
      return true;
   }

   // Common tail of every successful Initialize*: read back the buffer, bind
   // the event, fetch the render service and Start(). Returns false (with the
   // stream released) if any step fails so the caller can try the next mode.
   bool FinishRenderStream(double rate, int channels, WORD pcmBits, int mode)
   {
      UINT32 bufferSize = 0;
      if (FAILED(gRender.client->GetBufferSize(&bufferSize)) || bufferSize == 0)
      {
         ReleaseRenderStream();
         return false;
      }
      // Exclusive event mode needs the WHOLE endpoint buffer filled every
      // event; the render loop's scratch holds kPlanarCapacity frames, so a
      // larger device buffer cannot be served and is rejected up front.
      if (mode == 2 && bufferSize > (UINT32)kPlanarCapacity)
      {
         ReleaseRenderStream();
         return false;
      }
      gRender.sampleRate = rate;
      gRender.channels = channels;
      gRender.pcmBits = pcmBits;
      gRender.bufferFrames = bufferSize;

      HRESULT hr = gRender.client->SetEventHandle(gRender.bufferEvent);
      if (SUCCEEDED(hr))
         hr = gRender.client->GetService(__uuidof(IAudioRenderClient),
                                         reinterpret_cast<void**>(&gRender.renderer));
      if (SUCCEEDED(hr) && mode == 2)
      {
         // Exclusive event streams expect a primed buffer: hand the driver one
         // full buffer of silence before Start() so the first period is not
         // whatever the endpoint memory held.
         BYTE* silence = nullptr;
         hr = gRender.renderer->GetBuffer(bufferSize, &silence);
         if (SUCCEEDED(hr))
            hr = gRender.renderer->ReleaseBuffer(bufferSize, AUDCLNT_BUFFERFLAGS_SILENT);
      }
      if (SUCCEEDED(hr))
      {
         ResetEvent(gRender.bufferEvent);
         hr = gRender.client->Start();
      }
      if (FAILED(hr))
      {
         ReleaseRenderStream();
         return false;
      }

      // Output-side latency estimate: what the engine says the stream adds,
      // never less than one buffer. Approximate by nature (drivers differ in
      // what GetStreamLatency includes) - the readout labels it an estimate.
      REFERENCE_TIME streamLatency = 0;
      uint32_t latencyFrames = 0;
      if (SUCCEEDED(gRender.client->GetStreamLatency(&streamLatency)) && streamLatency > 0)
         latencyFrames = (uint32_t)std::llround((double)streamLatency * rate / 1e7);
      gOutputLatencyFrames.store(std::max<uint32_t>(latencyFrames, bufferSize),
                                 std::memory_order_release);
      gRender.mode = mode;
      return true;
   }

   // Mode 0: the original path - shared mode at the engine's default period.
   bool OpenSharedStandard(IMMDevice* device)
   {
      if (!ActivateRenderClient(device))
         return false;
      MixFormatGuard mix;
      if (FAILED(gRender.client->GetMixFormat(&mix.fmt)) || mix.fmt == nullptr)
      {
         ReleaseRenderStream();
         return false;
      }
      const bool isFloat = IsFloatFormat(mix.fmt);
      const bool isPcm = !isFloat && IsSupportedPcmFormat(mix.fmt);
      if (!((isFloat || isPcm) && mix.fmt->nChannels <= kMaxChannels))
      {
         ReleaseRenderStream();
         return false;
      }
      REFERENCE_TIME period = 0;
      gRender.client->GetDevicePeriod(nullptr, &period);
      if (FAILED(gRender.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                            period, 0, mix.fmt, nullptr)))
      {
         ReleaseRenderStream();
         return false;
      }
      return FinishRenderStream((double)mix.fmt->nSamplesPerSec, (int)mix.fmt->nChannels,
                                isPcm ? mix.fmt->wBitsPerSample : (WORD)0, 0);
   }

   // Mode 1: IAudioClient3 shared stream at the engine's minimum period
   // (Windows 10+, driver permitting). Same mix format and same pump as mode
   // 0, just a smaller period. Fails (so the caller falls back to mode 0)
   // when the interface is missing, the engine offers no smaller period, or
   // the stream cannot be initialised.
   bool OpenSharedLowLatency(IMMDevice* device)
   {
#if defined(__IAudioClient3_INTERFACE_DEFINED__)
      if (!ActivateRenderClient(device))
         return false;
      IAudioClient3* client3 = nullptr;
      if (FAILED(gRender.client->QueryInterface(__uuidof(IAudioClient3),
                                                reinterpret_cast<void**>(&client3))) ||
          client3 == nullptr)
      {
         ReleaseRenderStream();
         return false;
      }
      MixFormatGuard mix;
      const bool haveMix = SUCCEEDED(gRender.client->GetMixFormat(&mix.fmt)) && mix.fmt != nullptr;
      const bool isFloat = haveMix && IsFloatFormat(mix.fmt);
      const bool isPcm = haveMix && !isFloat && IsSupportedPcmFormat(mix.fmt);
      UINT32 defaultPeriod = 0, fundamentalPeriod = 0, minPeriod = 0, maxPeriod = 0;
      bool ok = haveMix && (isFloat || isPcm) && mix.fmt->nChannels <= kMaxChannels &&
                SUCCEEDED(client3->GetSharedModeEnginePeriod(mix.fmt, &defaultPeriod, &fundamentalPeriod,
                                                             &minPeriod, &maxPeriod)) &&
                minPeriod > 0 && minPeriod < defaultPeriod;
      if (ok)
         ok = SUCCEEDED(client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod,
                                                             mix.fmt, nullptr));
      client3->Release();
      if (!ok)
      {
         ReleaseRenderStream();
         return false;
      }
      return FinishRenderStream((double)mix.fmt->nSamplesPerSec, (int)mix.fmt->nChannels,
                                isPcm ? mix.fmt->wBitsPerSample : (WORD)0, 1);
#else
      (void)device;
      return false;
#endif
   }

   // Mode 2: WASAPI exclusive, event-driven, at the device's minimum period.
   // Exclusive mode has no mix format, so probe IsFormatSupported over a short
   // list of formats the render loop can convert (float32, 32-bit PCM incl.
   // 24-in-32, 16-bit PCM) at the shared mix rate first, then common rates.
   // Exclusive-only failure modes fall through to the caller's next mode:
   // device already in use, no supported format, unaligned/oversized buffer.
   bool OpenExclusive(IMMDevice* device)
   {
      if (!ActivateRenderClient(device))
         return false;

      // Hints from the shared mix format: rate, channel count, speaker mask.
      double mixRate = 48000.0;
      int channels = 2;
      DWORD channelMask = 0;
      {
         MixFormatGuard mix;
         if (SUCCEEDED(gRender.client->GetMixFormat(&mix.fmt)) && mix.fmt != nullptr)
         {
            mixRate = (double)mix.fmt->nSamplesPerSec;
            channels = (int)mix.fmt->nChannels;
            if (mix.fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
               channelMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix.fmt)->dwChannelMask;
         }
      }
      if (channels < 1 || channels > kMaxChannels)
      {
         ReleaseRenderStream();
         return false;
      }
      if (channelMask == 0)
         channelMask = channels == 1 ? 0x4u /* FRONT_CENTER */ : channels == 2 ? 0x3u /* FL|FR */ : 0u;

      struct Candidate { bool isFloat; WORD container; WORD valid; };
      static const Candidate kCandidates[] = {
         { true, 32, 32 }, { false, 32, 32 }, { false, 32, 24 }, { false, 16, 16 }
      };
      const double rates[] = { mixRate, 48000.0, 44100.0, 96000.0, 88200.0 };

      WAVEFORMATEXTENSIBLE chosen{};
      bool found = false;
      for (const double rate : rates)
      {
         for (const Candidate& c : kCandidates)
         {
            WAVEFORMATEXTENSIBLE w{};
            w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            w.Format.nChannels = (WORD)channels;
            w.Format.nSamplesPerSec = (DWORD)rate;
            w.Format.wBitsPerSample = c.container;
            w.Format.nBlockAlign = (WORD)(channels * c.container / 8);
            w.Format.nAvgBytesPerSec = w.Format.nSamplesPerSec * w.Format.nBlockAlign;
            w.Format.cbSize = 22;
            w.Samples.wValidBitsPerSample = c.valid;
            w.dwChannelMask = channelMask;
            w.SubFormat = c.isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
            if (gRender.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.Format, nullptr) == S_OK)
            {
               chosen = w;
               found = true;
               break;
            }
         }
         if (found)
            break;
      }
      if (!found)
      {
         ReleaseRenderStream();
         return false;
      }

      REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
      gRender.client->GetDevicePeriod(&defaultPeriod, &minPeriod);
      REFERENCE_TIME period = minPeriod > 0 ? minPeriod : defaultPeriod;
      if (period <= 0)
      {
         ReleaseRenderStream();
         return false;
      }

      HRESULT hr = gRender.client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                              period, period, &chosen.Format, nullptr);
      if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
      {
         // The driver wants a frame-aligned period: it reports the aligned
         // buffer size, then the client must be recreated to retry.
         UINT32 alignedFrames = 0;
         gRender.client->GetBufferSize(&alignedFrames);
         if (alignedFrames == 0 || !ActivateRenderClient(device))
         {
            ReleaseRenderStream();
            return false;
         }
         period = (REFERENCE_TIME)(10000000.0 / (double)chosen.Format.nSamplesPerSec * alignedFrames + 0.5);
         hr = gRender.client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                         period, period, &chosen.Format, nullptr);
      }
      if (FAILED(hr))
      {
         ReleaseRenderStream();
         return false;
      }
      const bool isFloat = IsEqualGUID(chosen.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
      return FinishRenderStream((double)chosen.Format.nSamplesPerSec, channels,
                                isFloat ? (WORD)0 : chosen.Format.wBitsPerSample, 2);
   }

   void RenderThreadMain(std::wstring endpointId, bool registerNotifications)
   {
      ComScope com;
      ProAudioScope proAudio; // reverted on every exit path, including the early return below

      if (com.ok)
      {
         HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&gRender.enumerator));
         if (SUCCEEDED(hr) && gRender.enumerator != nullptr)
         {
            if (registerNotifications)
               gRender.enumerator->RegisterEndpointNotificationCallback(&gNotifyClient);

            IMMDevice* device = nullptr;
            hr = gRender.enumerator->GetDevice(endpointId.c_str(), &device);
            if (SUCCEEDED(hr) && device != nullptr)
            {
               // Try the requested mode, then step down toward the standard
               // shared path (mode 0 is the original, always-attempted path).
               for (int m = gRender.requestedMode; m >= 0; --m)
               {
                  if (m == 2 && gExclusiveDisabled.load(std::memory_order_acquire))
                     continue;
                  const bool opened = m == 2 ? OpenExclusive(device)
                                             : m == 1 ? OpenSharedLowLatency(device)
                                                      : OpenSharedStandard(device);
                  if (opened)
                  {
                     gOutputModeActive.store(m, std::memory_order_release);
                     break;
                  }
               }
               device->Release();
            }
         }
      }

      // A null renderer means setup failed somewhere above. Publish the
      // outcome either way so the opener stops waiting; Stop() does the
      // cleanup and the join.
      if (gRender.renderer == nullptr)
      {
         gRender.running.store(false, std::memory_order_release);
         gRender.setupDone.store(true, std::memory_order_release);
         return;
      }
      gRender.setupDone.store(true, std::memory_order_release);

      // Pump until stopped.
      HANDLE handles[2] = { gRender.stopEvent, gRender.bufferEvent };
      // False until the first buffer is written: the endpoint buffer starts
      // empty after Start(), so padding 0 on the first event is expected,
      // not an underrun.
      bool primed = false;
      // Exclusive event mode: the whole endpoint buffer is ours on every
      // event (GetCurrentPadding is not meaningful), so write it all and skip
      // the shared-mode underrun heuristic below.
      const bool exclusive = gRender.mode == 2;
      // True when the pump ends because the stream died rather than because
      // Stop() asked - see the tail of this function.
      bool streamFailed = false;
      while (gRender.running.load(std::memory_order_acquire))
      {
         const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 2000);
         if (wait == WAIT_OBJECT_0)
            break; // stop requested
         if (wait != WAIT_OBJECT_0 + 1)
            continue; // timeout or error: re-check the running flag

         UINT32 padding = 0;
         if (!exclusive && FAILED(gRender.client->GetCurrentPadding(&padding)))
         {
            streamFailed = true;
            break;
         }
         const UINT32 capacity = gRender.bufferFrames;
         if (padding > capacity)
            continue;
         const UINT32 framesAvailable = capacity - padding;
         if (framesAvailable == 0)
            continue;
         // Shared-mode WASAPI has no underrun notification. Padding already
         // at 0 right before we write means the engine drained every frame we
         // gave it and played silence while waiting: count it as the "os"
         // xrun, the Windows counterpart of CoreAudio's processor overload.
         // One relaxed atomic increment - safe on this render thread.
         if (!exclusive && primed && padding == 0)
            AudioEngine_NotifyProcessorOverload(gRender.userData);

         BYTE* dest = nullptr;
         if (FAILED(gRender.renderer->GetBuffer(framesAvailable, &dest)))
         {
            streamFailed = true;
            break;
         }

         const int frames = (int)std::min<UINT32>(framesAvailable, kPlanarCapacity);
         float* planar[kMaxChannels];
         for (int ch = 0; ch < gRender.channels; ch++)
            planar[ch] = gRender.planarScratch.data() + (size_t)ch * kPlanarCapacity;

         // Silence any frames beyond what the caller will fill (callback gets
         // `frames`; we never hand WASAPI more than we asked for).
         std::fill(gRender.planarScratch.begin(),
                   gRender.planarScratch.begin() + (size_t)gRender.channels * frames, 0.0f);

         gRender.callback(planar, gRender.channels, frames, gRender.userData);

         const int chs = gRender.channels;
         if (gRender.pcmBits != 0)
         {
            PlanarFloatToInterleavedPcm(planar, chs, frames, gRender.pcmBits, dest);
         }
         else
         {
            float* out = reinterpret_cast<float*>(dest);
            for (int i = 0; i < frames; i++)
               for (int ch = 0; ch < chs; ch++)
                  out[(size_t)i * chs + ch] = planar[ch][i];
         }

         gRender.renderer->ReleaseBuffer(frames, 0);
         primed = true;
      }

      gRender.client->Stop();

      // Device loss / invalidation on an opt-in mode: the standard path keeps
      // its historical behaviour (IMMNotificationClient raises the flag for a
      // removed or changed default device), but a low-latency or exclusive
      // stream can also die on its own (exclusive format changed in the sound
      // control panel, driver reset). Latch the same flag PollAudioRecovery
      // consumes so the engine restarts, and stop trusting exclusive until the
      // user re-selects it, so the restart lands on a working stream.
      if (streamFailed && gRender.mode != 0)
      {
         if (gRender.mode == 2)
            gExclusiveDisabled.store(true, std::memory_order_release);
         gConfigChangedFlag.store(true, std::memory_order_release);
      }
   }
}

namespace Platform
{
   // ---- audio engine render callback bridge -------------------------------

   bool AudioDeviceOpen(AudioRenderCallback callback, void* userData, double& outSampleRate,
                        std::string& outError, uint32_t requestedDeviceId,
                        double requestedSampleRate, int requestedBufferFrames)
   {
      outError.clear();
      outSampleRate = 0.0;

      AudioDeviceClose(); // idempotent restart path

      {
         ComScope com;
         if (com.ok)
         {
            IMMDeviceEnumerator* enumerator = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           __uuidof(IMMDeviceEnumerator),
                                           reinterpret_cast<void**>(&enumerator))) &&
                enumerator != nullptr)
            {
               RefreshDeviceList(enumerator);
               enumerator->Release();
            }
         }
      }

      // Resolve which endpoint to run. 0 = system default render device.
      // requestedDeviceId is a 1-based index into gDevices as RefreshDeviceList
      // last built it, persisted verbatim in Infinite.audio-settings - it has
      // no relation to any stable OS identifier, so it goes stale the moment
      // the device list reorders (a device unplugged/replugged, or any device
      // added/removed changes every index after it), same hazard as macOS's
      // stale AudioObjectID (see Platform.mm's AudioDeviceOpen). An
      // out-of-range index, or one that now resolves to a capture-only
      // device, falls back to the system default output rather than failing
      // the whole engine start - requestedDeviceId itself (main.cpp's
      // gAudioOutputDeviceId) is left untouched, same as macOS.
      std::wstring endpointId;
      std::string endpointName;
      bool isInput = false;
      bool useRequestedDevice = requestedDeviceId != 0 &&
                                ResolveEndpoint(requestedDeviceId, endpointId, endpointName, &isInput) &&
                                !isInput;
      if (!useRequestedDevice)
      {
         endpointId.clear();
         endpointName.clear();

         ComScope com;
         IMMDeviceEnumerator* enumerator = nullptr;
         if (!com.ok ||
             FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                     __uuidof(IMMDeviceEnumerator),
                                     reinterpret_cast<void**>(&enumerator))) ||
             enumerator == nullptr)
         {
            outError = "could not create Windows audio enumerator";
            return false;
         }
         IMMDevice* device = nullptr;
         HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
         if (FAILED(hr) || device == nullptr)
         {
            outError = "no default output device";
            enumerator->Release();
            return false;
         }
         LPWSTR id = nullptr;
         if (SUCCEEDED(device->GetId(&id)) && id != nullptr)
         {
            endpointId = id;
            CoTaskMemFree(id);
         }
         device->Release();
         enumerator->Release();
         if (endpointId.empty())
         {
            outError = "no default output device";
            return false;
         }
      }

      gRender.callback = callback;
      gRender.userData = userData;
      gRender.planarScratch.assign((size_t)kMaxChannels * kPlanarCapacity, 0.0f);
      gRender.interleaveScratch.assign((size_t)kMaxChannels * kPlanarCapacity, 0.0f);
      gRender.sampleRate = 0.0;
      gRender.channels = 0;
      gRender.bufferFrames = 0;
      gRender.mode = 0;
      gRender.requestedMode = std::clamp(gOutputModeRequested.load(std::memory_order_acquire), 0, 2);

      gRender.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      gRender.bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (gRender.stopEvent == nullptr || gRender.bufferEvent == nullptr)
      {
         outError = "could not create sync events";
         AudioDeviceClose();
         return false;
      }

      gRender.running.store(true, std::memory_order_release);
      gRender.thread = std::thread(RenderThreadMain, endpointId, true);

      // The thread performs full WASAPI setup before its pump; give it a
      // moment and report what was negotiated. A slow COM bring-up should not
      // fail the open outright - poll briefly.
      // Wait on the thread's own handshake. The previous guard tested
      // !thread.joinable(), which can never be true here - joinable() stays
      // true until join() or detach() - so every failed open stalled the
      // caller for the whole 4000 ms before reporting.
      for (int waitedMs = 0; waitedMs < 4000; waitedMs += 10)
      {
         if (gRender.setupDone.load(std::memory_order_acquire))
            break;
         Sleep(10);
      }

      if (gRender.renderer == nullptr || gRender.sampleRate <= 0.0)
      {
         outError = "could not initialize WASAPI output (shared-mode float format)";
         AudioDeviceClose();
         return false;
      }

      // Shared mode structurally cannot honour a requested rate/buffer size -
      // it always runs at the device's own mix format and period, unlike
      // CoreAudio which actually applies these. Deliberately discarded, not
      // a TODO: the audio-settings UI (main.cpp) disables both controls on
      // Windows rather than let them sit live and silently do nothing - see
      // docs/plans/windows-render/FIX_BRIEF.md addendum A2. The opt-in
      // low-latency / exclusive modes (Platform::AudioSetOutputMode) pick the
      // device's minimum period instead of these two values, by design.
      (void)requestedSampleRate;
      (void)requestedBufferFrames;
      outSampleRate = gRender.sampleRate;
      return true;
   }

   void AudioDeviceClose()
   {
      gRender.Stop();
   }

   uint32_t AudioDeviceBufferFrames(uint32_t deviceId)
   {
      if (deviceId == 0 && gRender.running.load(std::memory_order_acquire) &&
          gRender.bufferFrames > 0)
         return gRender.bufferFrames;

      // Probe: default device period converted to frames.
      ComScope com;
      if (!com.ok)
         return 0;
      IMMDeviceEnumerator* enumerator = nullptr;
      if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator))) ||
          enumerator == nullptr)
         return 0;

      IMMDevice* device = nullptr;
      if (deviceId == 0)
         enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
      else
      {
         std::wstring endpointId;
         std::string name;
         if (ResolveEndpoint(deviceId, endpointId, name))
            enumerator->GetDevice(endpointId.c_str(), &device);
      }

      uint32_t result = 0;
      if (device != nullptr)
      {
         IAudioClient* client = nullptr;
         if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(&client))) &&
             client != nullptr)
         {
            REFERENCE_TIME defPeriod = 0;
            if (SUCCEEDED(client->GetDevicePeriod(&defPeriod, nullptr)) && defPeriod > 0)
            {
               WAVEFORMATEX* fmt = nullptr;
               if (SUCCEEDED(client->GetMixFormat(&fmt)) && fmt != nullptr)
               {
                  result = (uint32_t)std::llround((double)defPeriod * fmt->nSamplesPerSec / 1e7);
                  CoTaskMemFree(fmt);
               }
            }
            client->Release();
         }
         device->Release();
      }
      enumerator->Release();
      return result;
   }

   void AudioSetOutputMode(int mode)
   {
      const int clamped = std::clamp(mode, 0, 2);
      if (gOutputModeRequested.exchange(clamped, std::memory_order_acq_rel) != clamped)
         gExclusiveDisabled.store(false, std::memory_order_release); // a fresh choice deserves a fresh try
   }

   int AudioOutputModeActive()
   {
      if (!gRender.running.load(std::memory_order_acquire))
         return 0;
      return gOutputModeActive.load(std::memory_order_acquire);
   }

   // Headless round-trip check for the PCM<->float helpers above (FIX_BRIEF.md
   // addendum A1). No device needed, so unlike the rest of this file this is
   // actually CI-reachable - wired into main.cpp's env-var test dispatch as
   // INFINITE_AUDIOPCMTEST alongside the other INFINITE_*TEST fixtures.
   bool AudioPcmConversionSelfTest()
   {
      constexpr int kFrames = 8;
      constexpr int kChannels = 2;
      const float src[kChannels][kFrames] = {
         { -1.0f, -0.5f, -0.1f, 0.0f, 0.1f, 0.3f, 0.75f, 1.0f },
         { 0.9f, -0.9f, 0.42f, -0.42f, 0.0f, 1.0f, -1.0f, 0.05f },
      };
      const float* planar[kChannels] = { src[0], src[1] };

      bool ok = true;
      for (const WORD bits : { (WORD)16, (WORD)32 })
      {
         const size_t bytesPerSample = bits / 8;
         std::vector<BYTE> interleaved(bytesPerSample * kChannels * kFrames);
         PlanarFloatToInterleavedPcm(planar, kChannels, kFrames, bits, interleaved.data());

         std::vector<float> roundTrip;
         InterleavedPcmToFloat(interleaved.data(), kChannels, kFrames, bits, roundTrip);

         // Quantization tolerance: one LSB of the negotiated container size.
         const float tolerance = bits == 16 ? (1.0f / 32768.0f) * 1.5f : (1.0f / 2147483648.0f) * 2.0f;
         for (int i = 0; i < kFrames && ok; i++)
         {
            for (int ch = 0; ch < kChannels; ch++)
            {
               const float expected = src[ch][i];
               const float got = roundTrip[(size_t)i * kChannels + ch];
               if (std::fabs(got - expected) > tolerance)
               {
                  ok = false;
                  break;
               }
            }
         }
      }

      printf("%s\n", ok ? "AUDIOPCMTEST OK" : "AUDIOPCMTEST FAIL");
      return ok;
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
      std::vector<AudioDeviceInfo> out;

      {
         ComScope com;
         if (com.ok)
         {
            IMMDeviceEnumerator* enumerator = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           __uuidof(IMMDeviceEnumerator),
                                           reinterpret_cast<void**>(&enumerator))) &&
                enumerator != nullptr)
            {
               RefreshDeviceList(enumerator);
               enumerator->Release();
            }
         }
      }

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
}

// ---------------------------------------------------------------------------
// Live input analysis + input capture share one small capture-engine base.
// ---------------------------------------------------------------------------
namespace
{
   // Shared with namespace Platform below via unqualified lookup through the
   // anonymous namespace's implicit using-directive at global scope; the
   // capture thread here and Platform::AudioInputCapture*() both need it.
   std::atomic<uint32_t> gRequestedInputDeviceId{ 0 };

   // Lock-free "most recent" ring: one buffer, one monotonically increasing
   // frame counter. Writer publishes frame f at (f % capacity); readers take
   // a snapshot of the counter, clamp their lag, and copy. A reader lapped by
   // the writer sees a torn frame at worst - the same tolerance the macOS
   // tap documented.
   struct LatestRing
   {
      static constexpr int kCapacity = 1 << 16; // 65536 frames/channel

      std::vector<float> buffers;              // channels * kCapacity
      std::atomic<uint64_t> writeIndex{ 0 };   // frames committed
      int channels = 0;

      void Init(int chs)
      {
         channels = chs;
         buffers.assign((size_t)std::max(1, channels) * kCapacity, 0.0f);
         writeIndex.store(0, std::memory_order_release);
      }

      // Capture thread: commit one interleaved frame.
      void PushFrame(const float* interleaved)
      {
         const uint64_t idx = writeIndex.load(std::memory_order_relaxed);
         float* base = buffers.data() + (idx % kCapacity);
         for (int ch = 0; ch < channels; ch++)
            base[(size_t)ch * kCapacity] = interleaved[ch];
         writeIndex.store(idx + 1, std::memory_order_release);
      }
   };

   // Common plumbing: own the default capture endpoint on a thread, deliver
   // interleaved float frames to a lambda-ish virtual.
   struct CaptureEngineBase
   {
      std::thread thread;
      HANDLE stopEvent = nullptr;
      std::atomic<bool> running{ false };
      double sampleRate = 0.0;
      int channels = 0;
      std::string error;
      WORD pcmBits = 0;    // 0 = float mix format; 16/32 = PCM, see IsSupportedPcmFormat
      // Scratch for ThreadMain's PCM conversion and silence fill. Per-instance,
      // NOT function-local statics: gAnalyser and gTap are two separate
      // CaptureEngineBase objects, each with its own ThreadMain thread, and
      // they can be live at the same time (an Audio In node while audio-
      // reactive analysis runs). A shared static would have both threads
      // resize/write the same vector, which is a data race and, on a
      // reallocation, a use-after-free in whichever thread is mid-read.
      std::vector<float> pcmScratch;
      std::vector<float> silenceScratch;

      virtual ~CaptureEngineBase() = default;
      virtual void OnFormat(double rate, int chs) = 0;
      virtual void OnFrames(const float* interleaved, int frames) = 0;

      bool Start(std::string& outError)
      {
         Stop();
         stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
         if (stopEvent == nullptr)
         {
            outError = "could not create stop event";
            return false;
         }
         running.store(true, std::memory_order_release);
         thread = std::thread([this] { ThreadMain(); });

         for (int waitedMs = 0; waitedMs < 3000; waitedMs += 10)
         {
            if (!running.load(std::memory_order_acquire) || sampleRate > 0.0)
               break;
            Sleep(10);
         }

         if (!running.load(std::memory_order_acquire) || sampleRate <= 0.0)
         {
            outError = error.empty() ? "could not open input device" : error;
            Stop();
            return false;
         }
         return true;
      }

      void Stop()
      {
         if (!running.exchange(false))
         {
            if (stopEvent != nullptr)
            {
               CloseHandle(stopEvent);
               stopEvent = nullptr;
            }
            if (thread.joinable())
               thread.join();
            return;
         }
         if (stopEvent != nullptr)
            SetEvent(stopEvent);
         if (thread.joinable())
            thread.join();
         if (stopEvent != nullptr)
         {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
         }
         sampleRate = 0.0;
      }

      void ThreadMain()
      {
         ComScope com;
         if (!com.ok)
         {
            error = "COM initialization failed";
            running.store(false, std::memory_order_release);
            return;
         }

         IMMDeviceEnumerator* enumerator = nullptr;
         IAudioClient* client = nullptr;
         IAudioCaptureClient* capture = nullptr;
         WAVEFORMATEX* fmt = nullptr;
         // Declared out here, not in the SUCCEEDED(Initialize) block below,
         // so cleanup() can close it. As a local in that block it was leaked
         // on every path out - one event handle per capture start/stop cycle,
         // and PollAudioRecovery restarts the engine on every device change.
         HANDLE bufferEvent = nullptr;

         auto cleanup = [&]() {
            if (bufferEvent != nullptr)
               CloseHandle(bufferEvent);
            if (fmt != nullptr)
               CoTaskMemFree(fmt);
            if (capture != nullptr)
               capture->Release();
            if (client != nullptr)
            {
               client->Stop();
               client->Release();
            }
            if (enumerator != nullptr)
               enumerator->Release();
         };

         HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&enumerator));
         if (FAILED(hr) || enumerator == nullptr)
         {
            error = "no audio enumerator";
            running.store(false, std::memory_order_release);
            cleanup();
            return;
         }

         IMMDevice* device = nullptr;
         const uint32_t reqId = gRequestedInputDeviceId.load(std::memory_order_relaxed);
         if (reqId != 0)
         {
            std::wstring endpointId;
            std::string name;
            if (ResolveEndpoint(reqId, endpointId, name))
               enumerator->GetDevice(endpointId.c_str(), &device);
         }
         if (device == nullptr)
            hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
         if (FAILED(hr) || device == nullptr)
         {
            error = "no audio input device";
            running.store(false, std::memory_order_release);
            cleanup();
            return;
         }

         hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(&client));
         device->Release();
         if (FAILED(hr) || client == nullptr)
         {
            error = WinCommon::HrToString("input Activate", hr);
            running.store(false, std::memory_order_release);
            cleanup();
            return;
         }

         hr = client->GetMixFormat(&fmt);
         if (FAILED(hr) || fmt == nullptr)
         {
            error = "no mix format on input device";
            running.store(false, std::memory_order_release);
            cleanup();
            return;
         }

         // Analysis/capture both want interleaved float delivered to
         // OnFrames; PCM mix formats are converted on the fly below rather
         // than refused (FIX_BRIEF.md addendum A1). nChannels > kMaxChannels
         // is still a genuine limit.
         const bool isFloat = IsFloatFormat(fmt);
         const bool isPcm = !isFloat && IsSupportedPcmFormat(fmt);
         if ((!isFloat && !isPcm) || fmt->nChannels > kMaxChannels)
         {
            error = "input device format unsupported";
            running.store(false, std::memory_order_release);
            cleanup();
            return;
         }
         pcmBits = isPcm ? fmt->wBitsPerSample : 0;

         REFERENCE_TIME period = 0;
         client->GetDevicePeriod(nullptr, &period);
         hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 period, 0, fmt, nullptr);
         if (SUCCEEDED(hr))
         {
            bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            hr = client->SetEventHandle(bufferEvent);
            if (SUCCEEDED(hr))
               hr = client->GetService(__uuidof(IAudioCaptureClient),
                                       reinterpret_cast<void**>(&capture));
            if (SUCCEEDED(hr))
               hr = client->Start();
            if (FAILED(hr))
            {
               error = WinCommon::HrToString("capture start", hr);
               running.store(false, std::memory_order_release);
               cleanup();
               return;
            }

            OnFormat((double)fmt->nSamplesPerSec, fmt->nChannels);

            HANDLE handles[2] = { stopEvent, bufferEvent };
            while (running.load(std::memory_order_acquire))
            {
               const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 2000);
               if (wait == WAIT_OBJECT_0)
                  break;
               if (wait != WAIT_OBJECT_0 + 1)
                  continue;

               for (;;)
               {
                  UINT32 packetFrames = 0;
                  if (FAILED(capture->GetNextPacketSize(&packetFrames)) || packetFrames == 0)
                     break;

                  BYTE* data = nullptr;
                  DWORD flags = 0;
                  if (FAILED(capture->GetBuffer(&data, &packetFrames, &flags, nullptr, nullptr)))
                     break;

                  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr)
                  {
                     if (pcmBits != 0)
                     {
                        InterleavedPcmToFloat(data, fmt->nChannels, (int)packetFrames, pcmBits,
                                              pcmScratch);
                        OnFrames(pcmScratch.data(), (int)packetFrames);
                     }
                     else
                     {
                        OnFrames(reinterpret_cast<const float*>(data), (int)packetFrames);
                     }
                  }
                  else
                  {
                     // Silence packet still advances the analysis clock.
                     silenceScratch.assign((size_t)packetFrames * fmt->nChannels, 0.0f);
                     OnFrames(silenceScratch.data(), (int)packetFrames);
                  }

                  capture->ReleaseBuffer(packetFrames);
               }
            }
         }
         else
         {
            error = WinCommon::HrToString("input Initialize", hr);
            running.store(false, std::memory_order_release);
         }

         cleanup();
      }
   };

   // ---- analyser (AudioStart/AudioRead family) -----------------------------

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
      // ../common/AudioAnalysis.h, shared with Linux, so the two platforms
      // can't independently drift on the formulas (see that header's comment
      // for the four-way divergence this used to hide).
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

   // ---- input capture tap ---------------------------------------------------

   struct CaptureTapEngine : CaptureEngineBase
   {
      LatestRing ring;
      std::atomic<bool> wanted{ false };
      // Allocate first, publish last. AudioInputCaptureRead runs on the AUDIO
      // RENDER THREAD and used to be gated on `running && sampleRate > 0.0`,
      // with OnFormat writing sampleRate BEFORE ring.Init() - so between those
      // two statements the render thread could pass the gate and index
      // ring.buffers while assign() was reallocating it. A use-after-free on
      // the audio thread, and the window reopens on every input-engine
      // restart, which PollAudioRecovery triggers on any device change.
      //
      // `running` alone is not enough either: Start() sets it before the
      // thread has negotiated a format at all. This flag is the single thing
      // the read path gates on, and it is only ever set true once the ring
      // behind it is fully allocated.
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
   std::atomic<uint64_t> gTapReadCursor{ 0 };
}

namespace Platform
{
   // ---- audio input analysis -----------------------------------------------

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
      return gAnalyser.running.load(std::memory_order_acquire) && gAnalyser.sampleRate > 0.0;
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
      gAnalyser.levels.onset = false; // consumed on read, like macOS

      // Smoothing applied on read against the freshly sampled snapshot.
      //
      // macOS smooths rms, peak, low, mid, high AND bands[] (Platform.mm's
      // ProcessInto, via Analyser::Smooth). This loop used to touch bands[]
      // only, so the five summary outputs - the ones most patches actually
      // cable up - jittered on Windows where they glide on macOS. Same
      // attack/release coefficients, applied to the same set of fields.
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

   // ---- audio input capture (Audio In node) ---------------------------------

   uint32_t gActiveInputDeviceId = 0;

   void AudioInputCaptureAddRef()
   {
      gTapRefs.fetch_add(1, std::memory_order_relaxed);
   }

   void AudioInputCaptureRemoveRef()
   {
      const int prev = gTapRefs.fetch_sub(1, std::memory_order_relaxed);
      if (prev <= 1 && gTap.running.load(std::memory_order_acquire))
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

   uint32_t AudioRoundTripLatencyFrames(uint32_t /*outputDeviceId*/)
   {
      // Output side: measured off the open stream by the render thread.
      // Nothing open means nothing to report.
      const uint32_t outFrames = gOutputLatencyFrames.load(std::memory_order_acquire);
      const double outRate = gRender.running.load(std::memory_order_acquire) ? gRender.sampleRate : 0.0;
      if (outFrames == 0 || outRate <= 0.0)
         return 0;

      // Input side: the input endpoint's shared-mode device period, probed
      // once per (input device, output rate) and cached - probing costs a COM
      // round trip and this is polled by a UI every frame. Main thread only,
      // so the cache needs no lock. A failed probe caches 0 (input unknown).
      static uint32_t sCachedInputId = 0xFFFFFFFFu;
      static double sCachedRate = 0.0;
      static uint32_t sCachedInputFrames = 0;
      const uint32_t inputId = gRequestedInputDeviceId.load(std::memory_order_relaxed);
      if (inputId != sCachedInputId || outRate != sCachedRate)
      {
         sCachedInputId = inputId;
         sCachedRate = outRate;
         sCachedInputFrames = 0;

         ComScope com;
         IMMDeviceEnumerator* enumerator = nullptr;
         if (com.ok &&
             SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator),
                                        reinterpret_cast<void**>(&enumerator))) &&
             enumerator != nullptr)
         {
            IMMDevice* device = nullptr;
            std::wstring endpointId;
            std::string name;
            bool isInput = false;
            if (inputId != 0 && ResolveEndpoint(inputId, endpointId, name, &isInput) && isInput)
               enumerator->GetDevice(endpointId.c_str(), &device);
            else
               enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
            if (device != nullptr)
            {
               IAudioClient* client = nullptr;
               if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                              reinterpret_cast<void**>(&client))) &&
                   client != nullptr)
               {
                  REFERENCE_TIME defPeriod = 0;
                  if (SUCCEEDED(client->GetDevicePeriod(&defPeriod, nullptr)) && defPeriod > 0)
                     sCachedInputFrames = (uint32_t)std::llround((double)defPeriod * outRate / 1e7);
                  client->Release();
               }
               device->Release();
            }
            enumerator->Release();
         }
      }
      return outFrames + sCachedInputFrames;
   }

   void AudioInputCapturePump(std::string& outError)
   {
      outError.clear();
      if (gTapRefs.load(std::memory_order_relaxed) <= 0)
         return;
      const uint32_t reqDevice = gRequestedInputDeviceId.load(std::memory_order_relaxed);
      if (gTap.running.load(std::memory_order_acquire) && gTap.sampleRate > 0.0)
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
      // gTap.ready, not gTap.sampleRate: sampleRate is a plain double written
      // by the capture thread, and it used to open this gate before the ring
      // behind it existed - see CaptureTapEngine::ready.
      return gTap.running.load(std::memory_order_acquire) &&
             gTap.ready.load(std::memory_order_acquire);
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

      // First read jumps straight to the live edge; a stalled consumer that
      // fell further behind than the ring holds drops the missed frames.
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

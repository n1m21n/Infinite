// Windows implementation of the Platform facade's audio-device surface,
// replacing the macOS AVAudioEngine/CoreAudio stack:
//
//   - AudioDeviceOpen/Close: WASAPI shared-mode, event-driven render loop on
//     a dedicated thread; the app's render callback is handed planar float
//     buffers exactly like the macOS version did, then interleaved into the
//     WASAPI endpoint buffer here.
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
   };

   std::mutex gDevicesMutex;
   std::vector<DeviceEntry> gDevices;

   std::atomic<bool> gConfigChangedFlag{ false };

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
      explicit ComScope()
      {
         const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
         ok = SUCCEEDED(hr); // S_FALSE (already initialized) counts as success
      }
      ~ComScope() { if (ok) CoUninitialize(); }
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

   bool ResolveEndpoint(uint32_t deviceId, std::wstring& outEndpointId, std::string& outName)
   {
      std::lock_guard<std::mutex> lock(gDevicesMutex);
      if (deviceId == 0 || deviceId > gDevices.size())
         return false;
      outEndpointId = gDevices[deviceId - 1].endpointId;
      outName = gDevices[deviceId - 1].name;
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
            hr = FAILED(hr) ? hr : gRender.enumerator->GetDevice(endpointId.c_str(), &device);
            if (SUCCEEDED(hr) && device != nullptr)
            {
               hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(&gRender.client));
               device->Release();
            }
            if (SUCCEEDED(hr) && gRender.client != nullptr)
            {
               WAVEFORMATEX* mixFormat = nullptr;
               hr = gRender.client->GetMixFormat(&mixFormat);
               if (SUCCEEDED(hr) && mixFormat != nullptr)
               {
                  const bool isFloat = IsFloatFormat(mixFormat);
                  const bool isPcm = !isFloat && IsSupportedPcmFormat(mixFormat);
                  if ((isFloat || isPcm) && mixFormat->nChannels <= kMaxChannels)
                  {
                     REFERENCE_TIME period = 0;
                     gRender.client->GetDevicePeriod(nullptr, &period);

                     hr = gRender.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                     AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                     period, 0, mixFormat, nullptr);
                     if (SUCCEEDED(hr))
                     {
                        gRender.sampleRate = (double)mixFormat->nSamplesPerSec;
                        gRender.channels = (int)mixFormat->nChannels;
                        gRender.pcmBits = isPcm ? mixFormat->wBitsPerSample : 0;

                        UINT32 bufferSize = 0;
                        gRender.client->GetBufferSize(&bufferSize);
                        gRender.bufferFrames = bufferSize;

                        hr = gRender.client->SetEventHandle(gRender.bufferEvent);
                        if (SUCCEEDED(hr))
                           hr = gRender.client->GetService(__uuidof(IAudioRenderClient),
                                                           reinterpret_cast<void**>(&gRender.renderer));
                        if (SUCCEEDED(hr))
                           hr = gRender.client->Start();
                     }
                  }
                  else
                  {
                     hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
                  }
                  CoTaskMemFree(mixFormat);
               }
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
      while (gRender.running.load(std::memory_order_acquire))
      {
         const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 2000);
         if (wait == WAIT_OBJECT_0)
            break; // stop requested
         if (wait != WAIT_OBJECT_0 + 1)
            continue; // timeout or error: re-check the running flag

         UINT32 padding = 0;
         if (FAILED(gRender.client->GetCurrentPadding(&padding)))
            break;
         const UINT32 capacity = gRender.bufferFrames;
         if (padding > capacity)
            continue;
         const UINT32 framesAvailable = capacity - padding;
         if (framesAvailable == 0)
            continue;

         BYTE* dest = nullptr;
         if (FAILED(gRender.renderer->GetBuffer(framesAvailable, &dest)))
            break;

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
      }

      gRender.client->Stop();
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
      std::wstring endpointId;
      std::string endpointName;
      if (requestedDeviceId != 0)
      {
         if (!ResolveEndpoint(requestedDeviceId, endpointId, endpointName))
         {
            outError = "audio device not found";
            return false;
         }
      }
      else
      {
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
      // docs/plans/windows-render/FIX_BRIEF.md addendum A2 for the tradeoff
      // against AUDCLNT_SHAREMODE_EXCLUSIVE, which was not taken.
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
         hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
         if (FAILED(hr) || device == nullptr)
         {
            error = "no default input device";
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
      static constexpr int kFftLog2 = 10;
      static constexpr int kFftSize = 1 << kFftLog2;
      static constexpr int kBins = kFftSize / 2;

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

      void OnFrames(const float* interleaved, int frames) override
      {
         const int chs = std::max(1, channels);
         const float g = gain.load(std::memory_order_relaxed);

         for (int i = 0; i < frames; i++)
         {
            // Mix down to mono for the spectrum; RMS/peak track the same mix.
            float mono = 0.0f;
            for (int c = 0; c < chs; c++)
               mono += interleaved[(size_t)i * chs + c];
            mono *= g / (float)chs;

            ring[ringFill] = mono;
            ringFill++;

            if (ringFill >= kFftSize)
            {
               ringFill = 0;
               RunAnalysis();
            }
         }
      }

      void RunAnalysis()
      {
         float rms = 0.0f, peak = 0.0f;
         float spectrum[kBins] = {};

         // Window + forward transform.
         float windowed[kFftSize];
         for (int i = 0; i < kFftSize; i++)
            windowed[i] = ring[i] * window[i];

         float real[kBins], imag[kBins];
         fft.Forward(windowed, kFftLog2, real, imag);
         const float norm = 2.0f / (float)kFftSize;
         for (int k = 0; k < kBins; k++)
            spectrum[k] = std::sqrt(real[k] * real[k] + imag[k] * imag[k]) * norm;

         for (int i = 0; i < kFftSize; i++)
         {
            const float v = ring[i];
            rms += v * v;
            peak = std::max(peak, std::fabs(v));
         }
         rms = std::sqrt(rms / (float)kFftSize);

         // Spectral flux onset, same shape as the file-source analyser.
         float flux = 0.0f;
         for (int k = 0; k < kBins; k++)
            flux += std::max(0.0f, spectrum[k] - prevMagnitude[k]);
         const bool onset = flux > prevFlux * 1.6f && flux > 0.02f;
         prevFlux = prevFlux * 0.7f + flux * 0.3f;
         std::memcpy(prevMagnitude, spectrum, sizeof(prevMagnitude));

         // Log-spaced band energies, ~20 Hz..16 kHz.
         const double nyquist = sampleRate * 0.5;
         const double fMin = 20.0, fMax = std::min(16000.0, nyquist);
         Platform::AudioLevels next;
         next.rms = rms;
         next.peak = peak;
         for (int b = 0; b < Platform::kAudioBands; b++)
         {
            const double lo = fMin * std::pow(fMax / fMin, (double)b / Platform::kAudioBands);
            const double hi = fMin * std::pow(fMax / fMin, (double)(b + 1) / Platform::kAudioBands);
            int klo = std::clamp((int)(lo / nyquist * kBins), 1, kBins - 1);
            int khi = std::clamp((int)(hi / nyquist * kBins), klo + 1, kBins);
            float energy = 0.0f;
            for (int k = klo; k < khi; k++)
               energy += spectrum[k];
            energy /= (float)std::max(1, khi - klo);
            next.bands[b] = std::clamp(energy * 4.0f, 0.0f, 1.0f);
         }
         next.low = 0.5f * (next.bands[0] + next.bands[1]) + 0.25f * next.bands[2] + 0.25f * next.bands[3];
         next.mid = 0.25f * next.bands[4] + 0.5f * next.bands[5] + 0.25f * next.bands[6];
         next.high = 0.5f * next.bands[11] + 0.5f * next.bands[12];
         next.onset = onset;

         std::lock_guard<std::mutex> lock(levelsMutex);
         levels = next;
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
      const float a = gAnalyser.attack.load(std::memory_order_relaxed);
      const float r = gAnalyser.release.load(std::memory_order_relaxed);
      static float sPrev[kAudioBands] = {};
      for (int b = 0; b < kAudioBands; b++)
      {
         const float target = out.bands[b];
         const float coeff = target > sPrev[b] ? a : r;
         sPrev[b] += (target - sPrev[b]) * coeff;
         out.bands[b] = sPrev[b];
      }
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

   void AudioInputCapturePump(std::string& outError)
   {
      outError.clear();
      if (gTapRefs.load(std::memory_order_relaxed) <= 0)
         return;
      if (gTap.running.load(std::memory_order_acquire) && gTap.sampleRate > 0.0)
         return; // healthy
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

   int AudioInputCaptureRead(float* const* outChannels, int numFrames, int maxChannels)
   {
      if (outChannels == nullptr || numFrames <= 0 || maxChannels <= 0)
         return 0;
      if (!AudioInputCaptureIsRunning() || gTap.ring.channels == 0)
      {
         for (int ch = 0; ch < maxChannels; ch++)
            std::fill(outChannels[ch], outChannels[ch] + numFrames, 0.0f);
         return 0;
      }

      const int chs = std::min(gTap.ring.channels, maxChannels);
      const uint64_t w = gTap.ring.writeIndex.load(std::memory_order_acquire);
      uint64_t cursor = gTapReadCursor.load(std::memory_order_relaxed);

      // First read jumps straight to the live edge; a stalled consumer that
      // fell further behind than the ring holds drops the missed frames.
      if (cursor == 0 || w - cursor > (uint64_t)LatestRing::kCapacity)
         cursor = (w > (uint64_t)numFrames) ? w - numFrames : 0;

      const uint64_t available = w - cursor;
      const int n = (int)std::min<uint64_t>(numFrames, available);
      const int cap = LatestRing::kCapacity;

      for (int i = 0; i < n; i++)
      {
         const uint64_t slot = (cursor + i) % cap;
         for (int ch = 0; ch < chs; ch++)
            outChannels[ch][i] = gTap.ring.buffers[(size_t)ch * cap + slot];
      }
      for (int i = n; i < numFrames; i++) // underrun zero-fill
         for (int ch = 0; ch < chs; ch++)
            outChannels[ch][i] = 0.0f;

      gTapReadCursor.store(cursor + n, std::memory_order_relaxed);
      return chs;
   }
}

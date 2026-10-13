// Turbo 0.53: camera / capture-card input on Media Foundation.
//
// OpenCV's DirectShow backend could not be trusted to switch a device to
// MJPG: it only reapplies the format when the frame size changes, so HDMI-USB
// capture sticks stayed on their default YUY2 mode, which USB 2.0 cannot carry
// at full rate. Here every native mode the device advertises is listed (and
// logged), the best one for the requested size and frame rate is chosen
// explicitly, and the source reader decodes MJPG to NV12 (hardware MFT when
// available). NV12 goes to the GPU as-is (core/VideoTexture converts it).

#include "Platform.h"

#include "core/RuntimeLog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Platform
{
   namespace
   {
      template <class T> void SafeRelease(T*& p)
      {
         if (p != nullptr)
         {
            p->Release();
            p = nullptr;
         }
      }

      std::string WideToUtf8Local(const wchar_t* w, int len = -1)
      {
         if (w == nullptr) return {};
         const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
         std::string out(n > 0 ? n : 0, '\0');
         if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), n, nullptr, nullptr);
         while (!out.empty() && out.back() == '\0') out.pop_back();
         return out;
      }

      std::wstring Utf8ToWideLocal(const std::string& s)
      {
         const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
         std::wstring out(n > 0 ? n : 0, L'\0');
         if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
         return out;
      }

      // COM on the calling thread; tolerates the STA JUCE already set up.
      struct ComInit
      {
         HRESULT hr;
         ComInit() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
         ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
      };

      void EnsureMfStarted()
      {
         static std::once_flag once;
         std::call_once(once, []() { MFStartup(MF_VERSION, MFSTARTUP_LITE); });
      }

      std::string SubtypeName(const GUID& g)
      {
         if (g == MFVideoFormat_MJPG) return "MJPG";
         if (g == MFVideoFormat_YUY2) return "YUY2";
         if (g == MFVideoFormat_NV12) return "NV12";
         if (g == MFVideoFormat_RGB24) return "RGB24";
         if (g == MFVideoFormat_RGB32) return "RGB32";
         if (g == MFVideoFormat_I420) return "I420";
         if (g == MFVideoFormat_H264) return "H264";
         char c[5] = { (char)(g.Data1 & 0xFF), (char)((g.Data1 >> 8) & 0xFF), (char)((g.Data1 >> 16) & 0xFF), (char)((g.Data1 >> 24) & 0xFF), 0 };
         for (char& ch : c) if (ch != 0 && (ch < 32 || ch > 126)) ch = '?';
         return c;
      }

      struct DeviceEntry
      {
         std::wstring symbolicLink;
         std::string name;
      };

      std::vector<DeviceEntry> EnumerateDevices()
      {
         EnsureMfStarted();
         std::vector<DeviceEntry> out;
         IMFAttributes* attrs = nullptr;
         if (FAILED(MFCreateAttributes(&attrs, 1)))
            return out;
         attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
         IMFActivate** devices = nullptr;
         UINT32 count = 0;
         if (SUCCEEDED(MFEnumDeviceSources(attrs, &devices, &count)))
         {
            for (UINT32 i = 0; i < count; ++i)
            {
               DeviceEntry e;
               WCHAR* str = nullptr;
               UINT32 len = 0;
               if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &str, &len)))
               {
                  e.name = WideToUtf8Local(str, (int)len);
                  CoTaskMemFree(str);
               }
               if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &str, &len)))
               {
                  e.symbolicLink.assign(str, len);
                  CoTaskMemFree(str);
               }
               if (e.name.empty()) e.name = "Camera " + std::to_string(i);
               out.push_back(std::move(e));
               devices[i]->Release();
            }
            CoTaskMemFree(devices);
         }
         attrs->Release();
         return out;
      }

      struct NativeMode
      {
         DWORD index = 0;
         UINT32 w = 0, h = 0;
         double fps = 0.0;
         GUID subtype {};
      };

      // Lower is better. Frame rate below the target costs the most (that is
      // what "dropped" looks like on stage), then distance from the wanted
      // height. Ties go to MJPG at 720p and up: an uncompressed mode at that
      // size is the one USB 2.0 capture sticks advertise but cannot carry.
      long long ScoreMode(const NativeMode& m, int targetH, int targetFps)
      {
         if (m.w == 0 || m.h == 0 || m.fps <= 0.0)
            return LLONG_MAX;
         const bool known = m.subtype == MFVideoFormat_MJPG || m.subtype == MFVideoFormat_YUY2 ||
                            m.subtype == MFVideoFormat_NV12 || m.subtype == MFVideoFormat_RGB24 ||
                            m.subtype == MFVideoFormat_RGB32 || m.subtype == MFVideoFormat_I420;
         if (!known)
            return LLONG_MAX;
         const double fps = std::min(m.fps, 240.0);
         long long score = 0;
         if (fps + 0.5 < targetFps)
            score += (long long)((targetFps - fps) * 100.0);
         else
            score += (long long)(fps - targetFps); // prefer the closest rate at/above target
         score += (long long)std::abs((int)m.h - targetH) * 10;
         const bool mjpg = m.subtype == MFVideoFormat_MJPG;
         if (m.h >= 720 ? !mjpg : mjpg)
            score += 1;
         return score;
      }
   }

   struct CameraHandle
   {
      IMFMediaSource* source = nullptr;
      IMFSourceReader* reader = nullptr;
      std::atomic<bool> running { false };
      std::atomic<bool> mirror { false };
      CameraResolution resolution = CameraResolution::Auto;
      int requestedFps = 0;
      std::thread worker;
      std::mutex frameMutex;
      VideoFrame latest;
      unsigned long long seq = 0;
      unsigned long long deliveredSeq = 0;
      std::string negotiated;
      std::atomic<float> measuredFps { 0.0f };
      UINT32 outW = 0, outH = 0;
      bool outNv12 = true;
      bool yuv709 = true;

      ~CameraHandle()
      {
         running.store(false);
         // Shutdown first: a blocked ReadSample returns MF_E_SHUTDOWN, so a
         // capture card that stopped sending never hangs the app on close.
         if (source != nullptr)
            source->Shutdown();
         if (worker.joinable())
            worker.join();
         SafeRelease(reader);
         SafeRelease(source);
      }
   };

   std::vector<CameraDeviceInfo> CameraListDevices()
   {
      std::vector<CameraDeviceInfo> out;
      const std::vector<DeviceEntry> devices = EnumerateDevices();
      for (size_t i = 0; i < devices.size(); ++i)
         out.push_back({ WideToUtf8Local(devices[i].symbolicLink.c_str()), devices[i].name, i == 0 });
      RuntimeLog::Write("camera enumeration (Media Foundation): %d device(s)", (int)out.size());
      return out;
   }

   namespace
   {
      bool ConfigureReader(CameraHandle* h, std::string& error)
      {
         int targetH = 1080;
         if (h->resolution == CameraResolution::Res720p) targetH = 720;
         else if (h->resolution == CameraResolution::Res480p) targetH = 480;
         const int targetFps = h->requestedFps > 0 ? h->requestedFps : 60;

         std::vector<NativeMode> modes;
         for (DWORD i = 0;; ++i)
         {
            IMFMediaType* t = nullptr;
            const HRESULT hr = h->reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t);
            if (hr == MF_E_NO_MORE_TYPES || FAILED(hr))
               break;
            NativeMode m;
            m.index = i;
            MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &m.w, &m.h);
            UINT32 num = 0, den = 0;
            if (SUCCEEDED(MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den)) && den != 0)
               m.fps = (double)num / (double)den;
            t->GetGUID(MF_MT_SUBTYPE, &m.subtype);
            modes.push_back(m);
            t->Release();
         }
         if (modes.empty())
         {
            error = "camera offers no video format";
            return false;
         }

         // The full list goes to the log once per open: it is the first
         // thing to look at when a device misbehaves.
         std::string list;
         for (const NativeMode& m : modes)
         {
            char b[48];
            std::snprintf(b, sizeof(b), "%s%ux%u %s %.0f", list.empty() ? "" : ", ", m.w, m.h,
                          SubtypeName(m.subtype).c_str(), m.fps);
            list += b;
         }
         RuntimeLog::Write("camera modes: %s", list.c_str());

         const NativeMode* best = nullptr;
         long long bestScore = LLONG_MAX;
         for (const NativeMode& m : modes)
         {
            const long long s = ScoreMode(m, targetH, targetFps);
            if (s < bestScore) { bestScore = s; best = &m; }
         }
         if (best == nullptr)
         {
            error = "camera offers no usable video format";
            return false;
         }

         IMFMediaType* native = nullptr;
         HRESULT hr = h->reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, best->index, &native);
         if (SUCCEEDED(hr))
            hr = h->reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, native);
         // Output: NV12 (the reader adds the MJPG decoder / converter), RGB32 as fallback.
         const GUID outputs[2] = { MFVideoFormat_NV12, MFVideoFormat_RGB32 };
         bool set = false;
         for (int k = 0; k < 2 && SUCCEEDED(hr) && !set; ++k)
         {
            if (best->subtype == outputs[k])
            {
               set = true;
               h->outNv12 = k == 0;
               break;
            }
            IMFMediaType* out = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&out)))
            {
               native->CopyAllItems(out);
               out->SetGUID(MF_MT_SUBTYPE, outputs[k]);
               out->DeleteItem(MF_MT_DEFAULT_STRIDE);
               out->DeleteItem(MF_MT_SAMPLE_SIZE);
               out->DeleteItem(MF_MT_COMPRESSED);
               out->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
               if (SUCCEEDED(h->reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out)))
               {
                  set = true;
                  h->outNv12 = k == 0;
               }
               out->Release();
            }
         }
         SafeRelease(native);
         if (FAILED(hr) || !set)
         {
            error = "could not configure the camera format";
            return false;
         }

         IMFMediaType* cur = nullptr;
         if (SUCCEEDED(h->reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)))
         {
            MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &h->outW, &h->outH);
            UINT32 matrix = 0;
            h->yuv709 = SUCCEEDED(cur->GetUINT32(MF_MT_YUV_MATRIX, &matrix))
                           ? matrix == MFVideoTransferMatrix_BT709 : h->outH >= 720;
            cur->Release();
         }
         char line[96];
         std::snprintf(line, sizeof(line), "%ux%u %s %.0f fps", best->w, best->h,
                       SubtypeName(best->subtype).c_str(), best->fps);
         RuntimeLog::Write("camera negotiated: %s -> %s (asked %dp %d fps)", line,
                           h->outNv12 ? "NV12" : "RGB32", targetH, targetFps);
         std::lock_guard<std::mutex> lock(h->frameMutex);
         h->negotiated = line;
         return true;
      }

      void CaptureLoop(CameraHandle* h)
      {
         ComInit com;
         VideoFrame staging;
         int windowFrames = 0;
         int failures = 0;
         auto windowStart = std::chrono::steady_clock::now();
         while (h->running.load(std::memory_order_acquire))
         {
            DWORD stream = 0, flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            const HRESULT hr = h->reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                                     &stream, &flags, &ts, &sample);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
            {
               SafeRelease(sample);
               if (++failures == 60)
                  RuntimeLog::Write("camera capture is not delivering frames (hr=0x%08lx)", (unsigned long)hr);
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
               continue;
            }
            if (sample == nullptr)
               continue; // stream tick / format change notification
            failures = 0;

            IMFMediaBuffer* buf = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)))
            {
               BYTE* data = nullptr;
               LONG pitch = 0;
               DWORD len = 0;
               IMF2DBuffer* buf2d = nullptr;
               bool locked2d = false;
               if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) &&
                   SUCCEEDED(buf2d->Lock2D(&data, &pitch)))
               {
                  locked2d = true;
                  buf->GetCurrentLength(&len);
               }
               else if (SUCCEEDED(buf->Lock(&data, nullptr, &len)))
                  pitch = (LONG)(h->outNv12 ? h->outW : h->outW * 4);
               else
                  data = nullptr;

               const int w = (int)h->outW, ht = (int)h->outH;
               if (data != nullptr && w > 0 && ht > 0)
               {
                  // Lock2D hands back the top row and a negative pitch for
                  // bottom-up RGB32; row y is always data + y * pitch.
                  const LONG absPitch = pitch < 0 ? -pitch : pitch;
                  if (h->outNv12)
                  {
                     // Rows between the planes when the decoder pads the
                     // height (1080 -> 1088): derive from the buffer size.
                     LONG yRows = ht;
                     if (len > 0 && absPitch > 0)
                        yRows = std::max<LONG>(ht, (LONG)((len / (DWORD)absPitch) * 2 / 3));
                     staging.format = VideoPixelFormat::NV12;
                     staging.plane[0].resize((size_t)w * ht);
                     staging.plane[1].resize((size_t)((w + 1) / 2) * 2 * ((ht + 1) / 2));
                     for (int y = 0; y < ht; ++y)
                        std::memcpy(staging.plane[0].data() + (size_t)y * w, data + (size_t)y * absPitch, (size_t)w);
                     const BYTE* uv = data + (size_t)yRows * absPitch;
                     const size_t uvRow = (size_t)((w + 1) / 2) * 2;
                     for (int y = 0; y < (ht + 1) / 2; ++y)
                        std::memcpy(staging.plane[1].data() + (size_t)y * uvRow, uv + (size_t)y * absPitch, uvRow);
                     staging.fullRange = false;
                     staging.yuv709 = h->yuv709;
                     staging.topDown = true;
                  }
                  else
                  {
                     staging.format = VideoPixelFormat::BGRA8;
                     staging.plane[0].resize((size_t)w * ht * 4);
                     for (int y = 0; y < ht; ++y)
                        std::memcpy(staging.plane[0].data() + (size_t)y * w * 4, data + (LONGLONG)y * pitch, (size_t)w * 4);
                     staging.topDown = true;
                  }
                  staging.width = w;
                  staging.height = ht;
                  staging.opaque = true;
                  staging.mirrorX = h->mirror.load(std::memory_order_relaxed);

                  ++windowFrames;
                  const auto now = std::chrono::steady_clock::now();
                  const double secs = std::chrono::duration<double>(now - windowStart).count();
                  if (secs >= 1.0)
                  {
                     h->measuredFps.store((float)(windowFrames / secs), std::memory_order_relaxed);
                     windowFrames = 0;
                     windowStart = now;
                  }
                  std::lock_guard<std::mutex> lock(h->frameMutex);
                  std::swap(h->latest, staging);
                  ++h->seq;
               }
               if (locked2d) buf2d->Unlock2D();
               else if (data != nullptr) buf->Unlock();
               SafeRelease(buf2d);
               buf->Release();
            }
            sample->Release();
         }
      }
   }

   CameraHandle* CameraOpen(const std::string& id, CameraResolution res, int fps, bool mirror, std::string& error)
   {
      EnsureMfStarted();
      ComInit com;
      const std::vector<DeviceEntry> devices = EnumerateDevices();
      if (devices.empty())
      {
         error = "no camera found";
         return nullptr;
      }
      // Ids are symbolic links; older patches stored a list index.
      size_t pick = 0;
      bool found = false;
      const std::wstring wid = Utf8ToWideLocal(id);
      for (size_t i = 0; i < devices.size(); ++i)
         if (!wid.empty() && _wcsicmp(devices[i].symbolicLink.c_str(), wid.c_str()) == 0) { pick = i; found = true; break; }
      if (!found && !id.empty() && id.find_first_not_of("0123456789") == std::string::npos)
         pick = std::min<size_t>((size_t)std::atoi(id.c_str()), devices.size() - 1);

      auto h = std::make_unique<CameraHandle>();
      h->resolution = res;
      h->requestedFps = fps;
      h->mirror.store(mirror, std::memory_order_relaxed);

      IMFAttributes* attrs = nullptr;
      HRESULT hr = MFCreateAttributes(&attrs, 2);
      if (SUCCEEDED(hr))
      {
         attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
         attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, devices[pick].symbolicLink.c_str());
         hr = MFCreateDeviceSource(attrs, &h->source);
         attrs->Release();
      }
      IMFAttributes* readerAttrs = nullptr;
      if (SUCCEEDED(hr))
         hr = MFCreateAttributes(&readerAttrs, 3);
      if (SUCCEEDED(hr))
      {
         readerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
         readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
         hr = MFCreateSourceReaderFromMediaSource(h->source, readerAttrs, &h->reader);
         readerAttrs->Release();
      }
      if (FAILED(hr))
      {
         error = "could not open camera";
         RuntimeLog::Write("camera open failed: %s (hr=0x%08lx)", devices[pick].name.c_str(), (unsigned long)hr);
         return nullptr;
      }
      if (!ConfigureReader(h.get(), error))
      {
         RuntimeLog::Write("camera open failed: %s: %s", devices[pick].name.c_str(), error.c_str());
         return nullptr;
      }
      h->running.store(true, std::memory_order_release);
      CameraHandle* raw = h.get();
      h->worker = std::thread(CaptureLoop, raw);
      RuntimeLog::Write("camera opened: %s", devices[pick].name.c_str());
      return h.release();
   }

   void CameraClose(CameraHandle* h)
   {
      if (h == nullptr) return;
      delete h;
      RuntimeLog::Write("camera closed");
   }

   bool CameraIsRunning(CameraHandle* h)
   {
      return h != nullptr && h->running.load(std::memory_order_acquire);
   }

   void CameraSetMirror(CameraHandle* h, bool mirror)
   {
      if (h != nullptr) h->mirror.store(mirror, std::memory_order_relaxed);
   }

   // A running camera is reopened by its node when these change.
   void CameraSetResolution(CameraHandle* h, CameraResolution r) { if (h != nullptr) h->resolution = r; }
   void CameraSetFrameRate(CameraHandle* h, int fps) { if (h != nullptr) h->requestedFps = fps; }

   std::string CameraStatus(CameraHandle* h)
   {
      if (h == nullptr) return {};
      std::string line;
      {
         std::lock_guard<std::mutex> lock(h->frameMutex);
         line = h->negotiated;
      }
      char buf[48];
      std::snprintf(buf, sizeof(buf), ", %.1f delivered", h->measuredFps.load(std::memory_order_relaxed));
      return line + buf;
   }

   bool CameraReadFrame(CameraHandle* h, VideoFrame& out, unsigned long long& seq)
   {
      if (h == nullptr) return false;
      std::lock_guard<std::mutex> lock(h->frameMutex);
      if (h->latest.empty() || h->seq == h->deliveredSeq) return false;
      std::swap(out, h->latest); // the old buffer goes back to the capture thread
      seq = h->seq;
      h->deliveredSeq = h->seq;
      return true;
   }
}

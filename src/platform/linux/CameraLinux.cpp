// Linux implementation of the Platform facade's camera surface, replacing
// AVFoundation (macOS) / Media Foundation (Windows) with V4L2 - see
// docs/plans/linux/phase-03-media.md.
//
//   - Enumeration: VIDIOC_QUERYCAP over /dev/video0.. /dev/video63, skipping
//     anything that doesn't advertise V4L2_CAP_VIDEO_CAPTURE (metadata and
//     control-only nodes many webcams also expose).
//   - Capture: VIDIOC_S_FMT negotiates YUYV (preferred - a cheap BT.601
//     conversion) or, failing that, MJPEG (decoded via stb_image's
//     from-memory JPEG path - STBI_NO_STDIO is set process-wide, see
//     linux-parity §3.8, so stbi_load_from_memory is the only entry point
//     available). Four mmap'd buffers, a poll()-based capture thread, and a
//     single-slot "latest frame wins" mailbox - mirrors MediaWin.cpp's
//     CameraThreadMain shape (continuous capture thread, UI drains at its
//     own pace via CameraReadFrame) rather than macOS's delegate callback.
//   - Synthetic-buffer self-test: INFINITE_CAMERACONVTEST exercises the
//     YUYV->RGBA and MJPEG->RGBA converters directly on hand-built buffers,
//     since neither CI nor this container has a real camera device.

#include "../Platform.h"

#include "stb_image.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Platform
{
   namespace
   {
      constexpr int kNumBuffers = 4;

      int XIoctl(int fd, unsigned long request, void* arg)
      {
         int r;
         do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
         return r;
      }

      struct MappedBuffer
      {
         void* start = nullptr;
         size_t length = 0;
      };
   }

   // ---- converters (also exercised directly by the self-test) -----------

   namespace
   {
      inline unsigned char ClampByte(int v)
      {
         return (unsigned char)std::clamp(v, 0, 255);
      }

      // BT.601 YUYV (Y0 U Y1 V, 2 pixels per 4 bytes) -> RGBA8.
      void YuyvToRgba(const unsigned char* yuyv, int width, int height,
                      std::vector<unsigned char>& outRgba)
      {
         outRgba.resize((size_t)width * height * 4);
         for (int y = 0; y < height; ++y)
         {
            const unsigned char* row = yuyv + (size_t)y * width * 2;
            unsigned char* outRow = outRgba.data() + (size_t)y * width * 4;
            for (int x = 0; x < width; x += 2)
            {
               const int y0 = row[x * 2 + 0];
               const int u = row[x * 2 + 1] - 128;
               const int y1 = row[x * 2 + 2];
               const int v = row[x * 2 + 3] - 128;

               auto writePixel = [&](int px, int yy) {
                  const int r = yy + ((91881 * v) >> 16);
                  const int g = yy - ((22554 * u + 46802 * v) >> 16);
                  const int b = yy + ((116130 * u) >> 16);
                  unsigned char* p = outRow + (size_t)px * 4;
                  p[0] = ClampByte(r);
                  p[1] = ClampByte(g);
                  p[2] = ClampByte(b);
                  p[3] = 255;
               };
               writePixel(x, y0);
               if (x + 1 < width)
                  writePixel(x + 1, y1);
            }
         }
      }

      bool MjpegToRgba(const unsigned char* data, size_t size, int expectedWidth, int expectedHeight,
                       std::vector<unsigned char>& outRgba)
      {
         int w = 0, h = 0, channels = 0;
         unsigned char* decoded = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
         if (decoded == nullptr)
            return false;
         outRgba.assign(decoded, decoded + (size_t)w * h * 4);
         stbi_image_free(decoded);
         (void)expectedWidth; (void)expectedHeight;
         return true;
      }

      void MirrorRgbaInPlace(std::vector<unsigned char>& pixels, int width, int height)
      {
         for (int y = 0; y < height; ++y)
         {
            unsigned char* row = pixels.data() + (size_t)y * width * 4;
            for (int x = 0; x < width / 2; ++x)
            {
               unsigned char* a = row + (size_t)x * 4;
               unsigned char* b = row + (size_t)(width - 1 - x) * 4;
               unsigned char tmp[4];
               std::memcpy(tmp, a, 4);
               std::memcpy(a, b, 4);
               std::memcpy(b, tmp, 4);
            }
         }
      }
   }

   struct CameraHandle
   {
      std::string devicePath;
      int fd = -1;
      bool useMjpeg = false;
      MappedBuffer buffers[kNumBuffers];
      int bufferCount = 0;

      std::atomic<bool> stop{ false };
      std::atomic<bool> running{ false };
      std::thread thread;
      std::string error;

      std::mutex frameMutex;
      std::vector<unsigned char> pixels;
      int width = 0;
      int height = 0;
      unsigned long long seq = 0;
      unsigned long long lastReadSeq = 0;

      std::atomic<bool> mirrorX{ false };
      std::atomic<int> pendingResolution{ -1 }; // CameraResolution cast to int, -1 = none pending

      ~CameraHandle()
      {
         stop.store(true);
         if (thread.joinable())
            thread.join();
         for (int i = 0; i < bufferCount; ++i)
            if (buffers[i].start != nullptr && buffers[i].start != MAP_FAILED)
               munmap(buffers[i].start, buffers[i].length);
         if (fd >= 0)
            close(fd);
      }
   };

   namespace
   {
      void ResolutionToWH(CameraResolution res, int& w, int& h)
      {
         switch (res)
         {
            case CameraResolution::Res1080p: w = 1920; h = 1080; break;
            case CameraResolution::Res720p:  w = 1280; h = 720;  break;
            case CameraResolution::Res480p:  w = 640;  h = 480;  break;
            case CameraResolution::Auto:
            default:                         w = 1280; h = 720;  break;
         }
      }

      // Negotiates YUYV first, MJPEG second, at the closest resolution the
      // driver reports for the requested one. Returns false with the fd
      // still valid but format unset on total failure.
      bool NegotiateFormat(CameraHandle* h, int wantW, int wantH)
      {
         v4l2_format fmt{};
         fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         fmt.fmt.pix.width = wantW;
         fmt.fmt.pix.height = wantH;
         fmt.fmt.pix.field = V4L2_FIELD_ANY;

         fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
         if (XIoctl(h->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
         {
            h->useMjpeg = false;
            h->width = fmt.fmt.pix.width;
            h->height = fmt.fmt.pix.height;
            return true;
         }

         fmt.fmt.pix.width = wantW;
         fmt.fmt.pix.height = wantH;
         fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
         if (XIoctl(h->fd, VIDIOC_S_FMT, &fmt) == 0 && fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
         {
            h->useMjpeg = true;
            h->width = fmt.fmt.pix.width;
            h->height = fmt.fmt.pix.height;
            return true;
         }

         return false;
      }

      bool StartCapture(CameraHandle* h)
      {
         v4l2_requestbuffers req{};
         req.count = kNumBuffers;
         req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         req.memory = V4L2_MEMORY_MMAP;
         if (XIoctl(h->fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1)
            return false;

         h->bufferCount = (int)req.count;
         for (unsigned i = 0; i < req.count; ++i)
         {
            v4l2_buffer buf{};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (XIoctl(h->fd, VIDIOC_QUERYBUF, &buf) < 0)
               return false;

            h->buffers[i].length = buf.length;
            h->buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, h->fd, buf.m.offset);
            if (h->buffers[i].start == MAP_FAILED)
               return false;

            if (XIoctl(h->fd, VIDIOC_QBUF, &buf) < 0)
               return false;
         }

         v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         return XIoctl(h->fd, VIDIOC_STREAMON, &type) == 0;
      }

      void ApplyPendingResolution(CameraHandle* h)
      {
         const int pending = h->pendingResolution.exchange(-1);
         if (pending < 0)
            return;

         v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         XIoctl(h->fd, VIDIOC_STREAMOFF, &type);
         for (int i = 0; i < h->bufferCount; ++i)
            if (h->buffers[i].start != nullptr && h->buffers[i].start != MAP_FAILED)
               munmap(h->buffers[i].start, h->buffers[i].length);
         h->bufferCount = 0;

         int w = 0, hh = 0;
         ResolutionToWH((CameraResolution)pending, w, hh);
         if (NegotiateFormat(h, w, hh))
            StartCapture(h);
      }

      void CameraThreadMain(CameraHandle* h)
      {
         h->running.store(true);
         while (!h->stop.load())
         {
            ApplyPendingResolution(h);

            pollfd pfd{ h->fd, POLLIN, 0 };
            const int pr = poll(&pfd, 1, 200);
            if (pr <= 0)
               continue;

            v4l2_buffer buf{};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (XIoctl(h->fd, VIDIOC_DQBUF, &buf) < 0)
               continue;

            std::vector<unsigned char> rgba;
            bool ok = false;
            if (h->useMjpeg)
               ok = MjpegToRgba((const unsigned char*)h->buffers[buf.index].start, buf.bytesused,
                               h->width, h->height, rgba);
            else
            {
               YuyvToRgba((const unsigned char*)h->buffers[buf.index].start, h->width, h->height, rgba);
               ok = true;
            }

            if (ok)
            {
               if (h->mirrorX.load())
                  MirrorRgbaInPlace(rgba, h->width, h->height);

               std::lock_guard<std::mutex> lock(h->frameMutex);
               h->pixels = std::move(rgba);
               ++h->seq;
            }

            XIoctl(h->fd, VIDIOC_QBUF, &buf);
         }
         v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         XIoctl(h->fd, VIDIOC_STREAMOFF, &type);
         h->running.store(false);
      }
   }

   // V4L2 has no per-app consent prompt; a device the user cannot open
   // fails in CameraOpen instead.
   CameraAuthorization CameraAuthorizationStatus()
   {
      return CameraAuthorization::Authorized;
   }

   std::vector<CameraDeviceInfo> CameraListDevices()
   {
      std::vector<CameraDeviceInfo> devices;
      for (int i = 0; i < 64; ++i)
      {
         const std::string path = "/dev/video" + std::to_string(i);
         const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
         if (fd < 0)
            continue;

         v4l2_capability cap{};
         if (XIoctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
             (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
         {
            CameraDeviceInfo info;
            info.uniqueId = path;
            info.localizedName = std::string(reinterpret_cast<const char*>(cap.card));
            info.isDefault = devices.empty();
            devices.push_back(std::move(info));
         }
         close(fd);
      }
      return devices;
   }

   CameraHandle* CameraOpen(const std::string& deviceId, CameraResolution res, bool mirrorX, std::string& outError)
   {
      const std::string path = deviceId.empty() ? "/dev/video0" : deviceId;
      const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
      if (fd < 0)
      {
         outError = "could not open " + path;
         return nullptr;
      }

      v4l2_capability cap{};
      if (XIoctl(fd, VIDIOC_QUERYCAP, &cap) < 0 || !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
      {
         outError = path + " is not a video capture device";
         close(fd);
         return nullptr;
      }

      CameraHandle* h = new CameraHandle();
      h->devicePath = path;
      h->fd = fd;
      h->mirrorX.store(mirrorX);

      int w = 0, hh = 0;
      ResolutionToWH(res, w, hh);
      if (!NegotiateFormat(h, w, hh))
      {
         outError = "no supported pixel format (need YUYV or MJPEG)";
         delete h;
         return nullptr;
      }
      if (!StartCapture(h))
      {
         outError = "could not start capture streaming";
         delete h;
         return nullptr;
      }

      h->thread = std::thread(CameraThreadMain, h);
      for (int i = 0; i < 100 && !h->running.load(); ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(10));

      return h;
   }

   void CameraClose(CameraHandle* handle)
   {
      delete handle;
   }

   bool CameraIsRunning(CameraHandle* handle)
   {
      return handle != nullptr && handle->running.load();
   }

   void CameraSetMirror(CameraHandle* handle, bool mirrorX)
   {
      if (handle) handle->mirrorX.store(mirrorX);
   }

   void CameraSetResolution(CameraHandle* handle, CameraResolution res)
   {
      if (handle) handle->pendingResolution.store((int)res);
   }

   bool CameraReadFrame(CameraHandle* handle, std::vector<unsigned char>& outPixels,
                        int& outWidth, int& outHeight, unsigned long long& outFrameSeq)
   {
      if (handle == nullptr)
         return false;
      std::lock_guard<std::mutex> lock(handle->frameMutex);
      if (handle->seq == handle->lastReadSeq || handle->pixels.empty())
         return false;
      outPixels = handle->pixels;
      outWidth = handle->width;
      outHeight = handle->height;
      outFrameSeq = handle->seq;
      handle->lastReadSeq = handle->seq;
      return true;
   }

   // Exposed for INFINITE_CAMERACONVTEST in main.cpp - not part of the
   // Platform:: facade surface, declared here since these converters are
   // otherwise anonymous-namespace-private to this TU.
   namespace CameraLinuxTest
   {
      void YuyvToRgbaForTest(const unsigned char* yuyv, int width, int height,
                             std::vector<unsigned char>& outRgba)
      {
         YuyvToRgba(yuyv, width, height, outRgba);
      }

      bool MjpegToRgbaForTest(const unsigned char* data, size_t size, int expectedWidth, int expectedHeight,
                              std::vector<unsigned char>& outRgba)
      {
         return MjpegToRgba(data, size, expectedWidth, expectedHeight, outRgba);
      }
   }
}

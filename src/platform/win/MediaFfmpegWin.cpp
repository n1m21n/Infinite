// Windows video-picture backend built on OpenCV's VideoCapture (FFmpeg
// backend). Selected with -DINFINITE_WIN_VIDEO_BACKEND=FFMPEG (or the
// performance profile); the CMake then compiles this file, defines
// INFINITE_WIN_VIDEO_FFMPEG (which compiles the matching functions out of
// MediaWin.cpp), and links OpenCV.
//
// Why this exists: Media Foundation only decodes formats Windows has a codec
// for, and it decodes forward-only, so reverse/scrub stutter. OpenCV+FFmpeg
// reads virtually any container and seeks by frame index, so reverse, scrub
// and looping are all clean random-access operations.
//
// Scope: this file implements ONLY the picture side of playback. Camera
// capture, the recorder, DecodeVideoAudioTrackToBuffer (the clip's audio
// track) and image/EXR decode all stay on Media Foundation in MediaWin.cpp /
// MediaDecodeWin.cpp and are compiled in alongside this file. So a clip's
// audio still comes through MF; only its picture comes through OpenCV.
//
// The public contract matches Platform.h exactly, so nothing above the
// platform seam (nodes, UI, VideoSourceNode's loop/reverse/scrub/trim logic)
// changes. Handles are opaque; VideoHandleCv is our concrete type.

#include "../Platform.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
   struct VideoHandleCv
   {
      cv::VideoCapture capture;
      std::atomic<int> width{ 0 };
      std::atomic<int> height{ 0 };
      double duration = 0.0; // seconds; 0 if the container reports no frame count
      double fps = 30.0;

      // Decoding runs on its own worker; the render thread only ever posts a
      // target frame and consumes the newest finished RGBA buffer, never waits.
      std::mutex frameMutex;
      std::condition_variable frameReady;
      std::thread decoder;
      bool stopping = false;
      bool requestPending = false;
      long long requestedFrame = 0;
      long long lastQueuedFrame = -1;
      long long lastDecodedFrame = -1;
      std::uint64_t requestSerial = 0;
      std::uint64_t publishedSerial = 0;
      std::uint64_t consumedSerial = 0;
      std::vector<unsigned char> publishedPixels;
      std::atomic<bool> atEnd{ false }; // last read hit end-of-stream; cleared on a seek

      ~VideoHandleCv()
      {
         {
            std::lock_guard<std::mutex> lock(frameMutex);
            stopping = true;
         }
         frameReady.notify_one();
         if (decoder.joinable())
            decoder.join();
      }
   };
}

namespace Platform
{
   VideoHandle* VideoOpen(const std::string& path, std::string& outError)
   {
      outError.clear();
      auto handle = std::make_unique<VideoHandleCv>();

      // Try hardware-accelerated decode first (D3D11VA/DXVA through FFmpeg),
      // the big framerate win over software decode for HD/4K - this is what
      // makes OpenCV match the Media Foundation path, which was GPU-decoding.
      // Fall back to plain FFmpeg, then OpenCV's default backend.
      const std::vector<int> hwParams = { cv::CAP_PROP_HW_ACCELERATION,
                                          cv::VIDEO_ACCELERATION_ANY };
      if (!handle->capture.open(path, cv::CAP_FFMPEG, hwParams))
      {
         if (!handle->capture.open(path, cv::CAP_FFMPEG))
         {
            if (!handle->capture.open(path))
            {
               outError = "OpenCV/FFmpeg could not open this video";
               return nullptr;
            }
         }
      }

      handle->width.store((int)handle->capture.get(cv::CAP_PROP_FRAME_WIDTH), std::memory_order_relaxed);
      handle->height.store((int)handle->capture.get(cv::CAP_PROP_FRAME_HEIGHT), std::memory_order_relaxed);
      handle->fps = handle->capture.get(cv::CAP_PROP_FPS);
      if (!(handle->fps > 0.0))
         handle->fps = 30.0;
      const double frames = handle->capture.get(cv::CAP_PROP_FRAME_COUNT);
      handle->duration = frames > 0.0 ? frames / handle->fps : 0.0;

      if (handle->width.load() <= 0 || handle->height.load() <= 0)
      {
         outError = "video has no decodable picture";
         return nullptr;
      }

      VideoHandleCv* raw = handle.get();
      raw->decoder = std::thread([raw]()
      {
         for (;;)
         {
            long long targetFrame = 0;
            std::uint64_t serial = 0;
            {
               std::unique_lock<std::mutex> lock(raw->frameMutex);
               raw->frameReady.wait(lock, [raw]() { return raw->stopping || raw->requestPending; });
               if (raw->stopping)
                  return;
               targetFrame = raw->requestedFrame;
               serial = raw->requestSerial;
               raw->requestPending = false;
            }

            // Sequential playback reads the next frame; any other target is a
            // seek. This is what makes reverse and scrub cheap: OpenCV/FFmpeg
            // seeks to an arbitrary frame index instead of forward-decoding.
            if (targetFrame != raw->lastDecodedFrame + 1)
            {
               raw->capture.set(cv::CAP_PROP_POS_FRAMES, (double)targetFrame);
               raw->atEnd.store(false, std::memory_order_release);
            }

            cv::Mat bgr;
            if (!raw->capture.read(bgr) || bgr.empty())
            {
               // End of stream (or a decode gap). Leave the last frame up and
               // flag the end so a looping player can wrap on it. A later seek
               // (target != last+1) clears the flag above.
               raw->atEnd.store(true, std::memory_order_release);
               continue;
            }

            cv::Mat rgba;
            cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
            cv::flip(rgba, rgba, 0); // GL expects bottom-up, same as the MF path
            std::vector<unsigned char> pixels(rgba.datastart, rgba.dataend);
            raw->lastDecodedFrame = targetFrame;

            {
               std::lock_guard<std::mutex> lock(raw->frameMutex);
               raw->width.store(rgba.cols, std::memory_order_relaxed);
               raw->height.store(rgba.rows, std::memory_order_relaxed);
               raw->publishedPixels.swap(pixels);
               raw->publishedSerial = serial;
            }
         }
      });

      return reinterpret_cast<VideoHandle*>(handle.release());
   }

   void VideoClose(VideoHandle* handle)
   {
      delete reinterpret_cast<VideoHandleCv*>(handle);
   }

   int VideoWidth(VideoHandle* handle)
   {
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      return v != nullptr ? v->width.load(std::memory_order_relaxed) : 0;
   }

   int VideoHeight(VideoHandle* handle)
   {
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      return v != nullptr ? v->height.load(std::memory_order_relaxed) : 0;
   }

   double VideoDuration(VideoHandle* handle)
   {
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      return v != nullptr ? v->duration : 0.0;
   }

   bool VideoAtEnd(VideoHandle* handle)
   {
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      return v != nullptr && v->atEnd.load(std::memory_order_acquire);
   }

   double VideoObservedEndSeconds(VideoHandle* handle)
   {
      // OpenCV reports the frame count up front, so VideoDuration is reliable
      // and this fallback is not needed. Report the known end if we have it.
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      return (v != nullptr && v->duration > 0.0) ? v->duration : -1.0;
   }

   bool VideoFrameAt(VideoHandle* handle, double seconds, std::vector<unsigned char>& outPixels)
   {
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      if (v == nullptr || seconds < 0.0)
         return false;

      const long long targetFrame = std::max(0LL, (long long)std::floor(seconds * v->fps));
      bool produced = false;
      {
         std::lock_guard<std::mutex> lock(v->frameMutex);
         if (targetFrame != v->lastQueuedFrame)
         {
            v->requestedFrame = targetFrame;
            v->lastQueuedFrame = targetFrame;
            ++v->requestSerial;
            v->requestPending = true;
         }
         if (v->publishedSerial != v->consumedSerial && !v->publishedPixels.empty())
         {
            // Swap instead of copy: the caller uploads this straight to a
            // texture and never needs it again, so handing over the buffer
            // avoids a full-frame memcpy on the render thread every frame
            // (that copy is real cost at 4K).
            outPixels.swap(v->publishedPixels);
            v->consumedSerial = v->publishedSerial;
            produced = true;
         }
      }
      v->frameReady.notify_one();
      return produced;
   }

   bool VideoDecodeIsCatchingUp(VideoHandle* handle)
   {
      auto* v = reinterpret_cast<VideoHandleCv*>(handle);
      if (v == nullptr)
         return false;
      if (v->atEnd.load(std::memory_order_acquire))
         return false; // nothing more is coming for this position
      std::lock_guard<std::mutex> lock(v->frameMutex);
      // Still catching up while a requested frame has not yet been published.
      return v->requestPending || v->publishedSerial < v->requestSerial;
   }
}

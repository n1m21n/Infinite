// Linux implementation of the Platform facade's video/recorder surface,
// replacing AVFoundation (macOS) / Media Foundation (Windows) with FFmpeg's
// libavformat/libavcodec/libswscale/libswresample - see
// docs/plans/linux/phase-03-media.md for the licence decision
// (INFINITE_ENABLE_GPL_CODECS) and CMakeLists.txt for how these libraries are
// vendored.
//
//   - Video decode: avformat/avcodec on a dedicated decode thread, output
//     converted to RGBA8 via swscale, row-flipped for GL - mirrors
//     MediaWin.cpp's IMFSourceReader thread/readahead-queue shape rather than
//     macOS's synchronous AVAssetReader, since decoding here is also
//     asynchronous relative to the render thread.
//   - DecodeVideoAudioTrackToBuffer / the FFmpeg audio-file fallback
//     (FfmpegAudioDecodeHook.h): demux+decode via avformat/avcodec, resample
//     to interleaved float via swresample, deinterleave into SampleBuffer.
//   - Recorder: avformat muxer (MP4) with an H.264 video stream
//     (INFINITE_ENABLE_GPL_CODECS=ON: libx264 through avcodec; OFF: encoding
//     is unavailable, matching the LGPL build's promise in LICENSE) and an
//     AAC audio stream via avcodec's native "aac" encoder. Producer/bounded
//     queue/encoder-thread/pool/cancel structure mirrors MediaWin.cpp.
//   - InspectMovie: opens the written file read-only and walks the actual
//     encoded video packet stream for frame count, not duration*fps.
//
// FFmpeg's own headers are C; wrap every include in extern "C".

#include "../Platform.h"
#include "BenchMediaIo.h"
#include "../common/FfmpegAudioDecodeHook.h"
#include "tinyfiledialogs.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Platform
{
   namespace
   {
      // ---- shared small helpers ---------------------------------------

      std::string AvErrToString(int err)
      {
         char buf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
         av_strerror(err, buf, sizeof(buf));
         return std::string(buf);
      }

      // Finds the "best" stream of a given type, or -1. Thin wrapper so
      // every call site reads the same way.
      int FindBestStream(AVFormatContext* fmt, AVMediaType type)
      {
         return av_find_best_stream(fmt, type, -1, -1, nullptr, 0);
      }

      // BGRA/RGBA row-flip - every consumer of VideoFrameAt/CameraReadFrame
      // and every producer feeding RecorderAppend expects GL's bottom-up
      // convention; swscale/avcodec both produce top-down.
      void FlipRowsInPlace(std::vector<unsigned char>& pixels, int width, int height)
      {
         const size_t stride = (size_t)width * 4;
         std::vector<unsigned char> row(stride);
         for (int y = 0; y < height / 2; ++y)
         {
            unsigned char* a = pixels.data() + (size_t)y * stride;
            unsigned char* b = pixels.data() + (size_t)(height - 1 - y) * stride;
            std::memcpy(row.data(), a, stride);
            std::memcpy(a, b, stride);
            std::memcpy(b, row.data(), stride);
         }
      }
   }

   // =====================================================================
   // Video decode
   // =====================================================================

   namespace
   {
      constexpr int kVideoReadaheadFrames = 4;
      constexpr double kVideoEpsilonSeconds = 0.001;     // 1ms
      constexpr double kVideoForwardSeekSeconds = 1.0;   // beyond this, seek instead of decode-through

      // How far behind a backward-seek target the decode thread starts
      // decoding from, so the frames in between land in frameCache - ported
      // from Platform.mm's identical kReverseLookbackSeconds/frameCache
      // design (see the comment there). Without this, every single-frame
      // reverse step pays a fresh av_seek_frame + avcodec_flush_buffers
      // (confirmed via INFINITE_VIDEOSPEEDTEST: shrinking the encoder's GOP
      // did not help, because the cost is the seek/flush itself, not
      // redecode distance from the nearest keyframe) - too small a lookback
      // and reverse playback pays a fresh reader rebuild almost every frame.
      constexpr double kReverseLookbackSeconds = 2.0;

      struct DecodedVideoFrame
      {
         double pts = 0.0;
         std::vector<unsigned char> rgba;
      };

      struct CachedVideoFrame
      {
         double pts = 0.0;
         std::vector<unsigned char> rgba;
      };
   }

   struct VideoHandle
   {
      std::string path;
      int width = 0;
      int height = 0;
      double duration = 0.0;

      std::atomic<double> targetSeconds{ 0.0 };
      std::atomic<double> decodeHeadSeconds{ 0.0 };
      std::atomic<bool> endOfStream{ false };

      std::deque<DecodedVideoFrame> ready;
      std::deque<DecodedVideoFrame> recycle;
      // Furthest pts actually handed to the caller by VideoFrameAt - distinct
      // from decodeHeadSeconds (how far the decode thread has *buffered*
      // ahead, via the readahead queue). A real backward seek must be judged
      // against what the caller has been shown, not against decode's own
      // lookahead: comparing target to decode progress instead made the
      // decode thread treat its own readahead buffering (which intentionally
      // races ahead of target by design) as the caller seeking backward,
      // forcing a seek-to-keyframe on nearly every iteration. See
      // RunRecExportTest's video onset count (was 35-54 instead of 5).
      std::atomic<double> deliveredSeconds{ -1.0 };

      // Reverse-playback cache: every frame the decode thread produces while
      // recovering from a backward seek is kept here (FIFO by pts, capped by
      // byte size) so the next several single-frame reverse steps can be
      // served directly instead of each paying for a fresh seek. Guarded by
      // `mutex`, same as `ready`/`recycle`. See kReverseLookbackSeconds.
      std::deque<CachedVideoFrame> frameCache;
      size_t frameCacheBytes = 0;
      static constexpr size_t kMaxFrameCacheBytes = 256 * 1024 * 1024;

      std::thread thread;
      std::atomic<bool> stop{ false };
      std::atomic<bool> running{ false };
      std::atomic<bool> threadDone{ false };
      std::string error;

      std::mutex mutex;
      std::condition_variable cv;

      // B8 bench only (nullptr otherwise). Created before the decode thread
      // starts, which is the only writer of its decode-side fields.
      std::unique_ptr<Bench::MediaDecodeStats> bench;

      ~VideoHandle()
      {
         stop.store(true);
         cv.notify_all();
         if (thread.joinable())
            thread.join();
      }
   };

   namespace
   {
      // Appends a delivered frame to the reverse-playback cache, evicting the
      // oldest entries (FIFO) to stay under the byte cap. Caller must hold
      // h->mutex. Mirrors Platform.mm's PushCacheFrame.
      void PushCacheFrameLocked(VideoHandle* h, double pts, const std::vector<unsigned char>& rgba)
      {
         const size_t frameBytes = rgba.size();
         if (frameBytes == 0 || frameBytes > VideoHandle::kMaxFrameCacheBytes)
            return;
         if (!h->frameCache.empty() && pts <= h->frameCache.back().pts + kVideoEpsilonSeconds)
            return; // avoid duplicate/out-of-order timestamps

         h->frameCache.push_back({ pts, rgba });
         h->frameCacheBytes += frameBytes;

         while (h->frameCacheBytes > VideoHandle::kMaxFrameCacheBytes && !h->frameCache.empty())
         {
            h->frameCacheBytes -= h->frameCache.front().rgba.size();
            h->frameCache.pop_front();
         }
      }

      // Serves a request directly from the reverse-playback cache when
      // possible, without touching the decode thread at all. Caller must
      // hold h->mutex. Mirrors Platform.mm's TryUseCache, including its
      // "Same" case: a request that lands on the frame already delivered
      // is a no-op (false, outPixels untouched) so the caller skips the
      // re-upload, per the contract documented on VideoFrameAt in
      // Platform.h ("Returns true only when that is a new frame").
      bool TryUseCacheLocked(VideoHandle* h, double seconds, std::vector<unsigned char>& outPixels,
                             double& outPts)
      {
         if (h->frameCache.empty())
            return false;
         if (seconds < h->frameCache.front().pts - 0.01 || seconds > h->frameCache.back().pts + 0.04)
            return false; // outside the cached span

         const CachedVideoFrame* best = nullptr;
         for (auto it = h->frameCache.rbegin(); it != h->frameCache.rend(); ++it)
         {
            if (it->pts <= seconds + kVideoEpsilonSeconds)
            {
               best = &(*it);
               break;
            }
         }
         if (best == nullptr)
            best = &h->frameCache.front();

         outPts = best->pts;
         const double delivered = h->deliveredSeconds.load();
         if (delivered >= 0.0 && std::fabs(best->pts - delivered) < kVideoEpsilonSeconds)
            return false; // same frame already handed back - no copy, no re-upload

         outPixels = best->rgba; // copy - the cache keeps its own owning copy
         return true;
      }

      void VideoThreadMain(VideoHandle* h)
      {
         AVFormatContext* fmt = nullptr;
         if (avformat_open_input(&fmt, h->path.c_str(), nullptr, nullptr) < 0)
         {
            h->error = "could not open video file";
            h->threadDone.store(true);
            return;
         }
         if (avformat_find_stream_info(fmt, nullptr) < 0)
         {
            h->error = "could not read video stream info";
            avformat_close_input(&fmt);
            h->threadDone.store(true);
            return;
         }

         const int streamIndex = FindBestStream(fmt, AVMEDIA_TYPE_VIDEO);
         if (streamIndex < 0)
         {
            h->error = "no video track in this file";
            avformat_close_input(&fmt);
            h->threadDone.store(true);
            return;
         }

         AVStream* stream = fmt->streams[streamIndex];
         const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
         if (codec == nullptr)
         {
            h->error = "unsupported video codec";
            avformat_close_input(&fmt);
            h->threadDone.store(true);
            return;
         }

         AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
         avcodec_parameters_to_context(codecCtx, stream->codecpar);
         // Required for best_effort_timestamp's B-frame reorder math to be
         // correct - without it the decoder assumes the codec's own
         // time_base for incoming packet PTS/DTS, which does not match this
         // stream's muxed time_base and produces wrong presentation
         // timestamps once B-frames are in play (x264 defaults to using
         // them). This was the root cause of RunRecExportTest's video onset
         // count being wrong (40-54 instead of 5) despite the muxed file
         // itself being clean - confirmed by an independent ffmpeg-CLI
         // decode of the same file showing exactly 5 clean onsets.
         codecCtx->pkt_timebase = stream->time_base;
         if (avcodec_open2(codecCtx, codec, nullptr) < 0)
         {
            h->error = "could not open video decoder";
            avcodec_free_context(&codecCtx);
            avformat_close_input(&fmt);
            h->threadDone.store(true);
            return;
         }

         h->width = codecCtx->width;
         h->height = codecCtx->height;
         if (fmt->duration > 0)
            h->duration = (double)fmt->duration / (double)AV_TIME_BASE;
         else if (stream->duration > 0)
            h->duration = (double)stream->duration * av_q2d(stream->time_base);

         SwsContext* sws = sws_getContext(codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
                                          codecCtx->width, codecCtx->height, AV_PIX_FMT_RGBA,
                                          SWS_BILINEAR, nullptr, nullptr, nullptr);

         AVPacket* pkt = av_packet_alloc();
         AVFrame* frame = av_frame_alloc();
         AVFrame* rgbaFrame = av_frame_alloc();
         av_image_alloc(rgbaFrame->data, rgbaFrame->linesize, codecCtx->width, codecCtx->height,
                        AV_PIX_FMT_RGBA, 1);

         double lastDeliveredSeconds = -1.0;
         if (h->bench)
            h->bench->nominalFps = av_q2d(stream->avg_frame_rate);
         h->running.store(true);

         auto ptsToSeconds = [&](int64_t pts) -> double {
            return (double)pts * av_q2d(stream->time_base);
         };

         // Decodes the next frame and fills `out`. Returns false when no
         // more frames are available right now (either genuine EOF or a
         // decode error - both set h->endOfStream).
         auto decodeOneFrame = [&](DecodedVideoFrame& out) -> bool {
            for (;;)
            {
               int recvErr = avcodec_receive_frame(codecCtx, frame);
               if (recvErr == 0)
               {
                  const double pts = ptsToSeconds(frame->best_effort_timestamp != AV_NOPTS_VALUE
                                                      ? frame->best_effort_timestamp
                                                      : frame->pts);
                  sws_scale(sws, frame->data, frame->linesize, 0, codecCtx->height,
                           rgbaFrame->data, rgbaFrame->linesize);

                  out.pts = pts;
                  out.rgba.resize((size_t)codecCtx->width * codecCtx->height * 4);
                  // swscale honours whatever stride av_image_alloc picked;
                  // copy row by row rather than assume linesize == width*4
                  // (same rule Media Foundation needs - see windows-parity
                  // §3.5 - swscale's padding rules are the FFmpeg analogue).
                  const int rowBytes = codecCtx->width * 4;
                  for (int y = 0; y < codecCtx->height; ++y)
                  {
                     std::memcpy(out.rgba.data() + (size_t)y * rowBytes,
                                rgbaFrame->data[0] + (size_t)y * rgbaFrame->linesize[0],
                                rowBytes);
                  }
                  FlipRowsInPlace(out.rgba, codecCtx->width, codecCtx->height);

                  h->decodeHeadSeconds.store(pts);
                  lastDeliveredSeconds = pts;
                  return true;
               }
               if (recvErr == AVERROR(EAGAIN))
               {
                  const int readErr = av_read_frame(fmt, pkt);
                  if (readErr < 0)
                  {
                     avcodec_send_packet(codecCtx, nullptr); // flush
                     h->endOfStream.store(true);
                     return false;
                  }
                  if (pkt->stream_index == streamIndex)
                     avcodec_send_packet(codecCtx, pkt);
                  av_packet_unref(pkt);
                  continue;
               }
               // EOF or real error - nothing more to decode.
               h->endOfStream.store(true);
               return false;
            }
         };

         while (!h->stop.load())
         {
            const double target = h->targetSeconds.load();
            // Backward jump is judged against what has actually been
            // DELIVERED to the caller (h->deliveredSeconds), not against how
            // far the decode thread has buffered ahead (lastDeliveredSeconds)
            // - see the comment on deliveredSeconds's declaration above.
            const bool jumpedBackward = target < h->deliveredSeconds.load() - kVideoEpsilonSeconds;
            const bool jumpedForward = target > lastDeliveredSeconds + kVideoForwardSeekSeconds;

            bool coveredByCache = false;
            {
               std::unique_lock<std::mutex> lock(h->mutex);
               // A backward jump the reverse-playback cache already covers
               // needs no seek at all - VideoFrameAt serves it directly.
               // Without this check every single-frame reverse step here
               // would force a fresh av_seek_frame even though the caller
               // never actually touched the decode thread for it.
               coveredByCache = !h->frameCache.empty() &&
                               target >= h->frameCache.front().pts - kVideoEpsilonSeconds &&
                               target <= h->frameCache.back().pts + kVideoEpsilonSeconds;
               const bool haveEnoughReadahead = (int)h->ready.size() >= kVideoReadaheadFrames;
               const bool caughtUp = h->endOfStream.load() && !jumpedBackward;
               if ((!jumpedBackward && !jumpedForward && (haveEnoughReadahead || caughtUp)) ||
                   (jumpedBackward && !jumpedForward && coveredByCache))
               {
                  h->cv.wait_for(lock, std::chrono::milliseconds(4));
                  continue;
               }
            }

            if (jumpedBackward || jumpedForward)
            {
               // On a genuine backward jump (not already served from cache),
               // seek further back than the target by kReverseLookbackSeconds
               // so the frames decoded on the way to `target` populate the
               // cache and buy several more reverse steps before the next
               // real seek - ported from Platform.mm's identical
               // kReverseLookbackSeconds/frameCache design.
               const double seekSeconds = jumpedBackward
                  ? std::max(0.0, target - kReverseLookbackSeconds)
                  : target;
               const int64_t seekTarget = (int64_t)(seekSeconds / av_q2d(stream->time_base));
               if (h->bench)
               {
                  h->bench->readerRestarts.fetch_add(1, std::memory_order_relaxed);
                  h->bench->restartStartMs = Bench::MediaNowMs();
               }
               av_seek_frame(fmt, streamIndex, seekTarget, AVSEEK_FLAG_BACKWARD);
               avcodec_flush_buffers(codecCtx);
               h->endOfStream.store(false);
               {
                  std::lock_guard<std::mutex> lock(h->mutex);
                  h->ready.clear();
                  // The cache no longer has a contiguous, correctly-ordered
                  // relationship to what's about to be decoded - rebuild it
                  // from this seek point forward (mirrors Platform.mm, which
                  // clears frameCache whenever it rebuilds the reader).
                  h->frameCache.clear();
                  h->frameCacheBytes = 0;
               }
               lastDeliveredSeconds = -1.0;
            }

            DecodedVideoFrame slot;
            {
               std::lock_guard<std::mutex> lock(h->mutex);
               if (!h->recycle.empty())
               {
                  slot = std::move(h->recycle.back());
                  h->recycle.pop_back();
               }
            }

            const double benchDecodeStartMs = h->bench ? Bench::MediaNowMs() : 0.0;
            const bool decodedOne = decodeOneFrame(slot);
            if (h->bench && decodedOne)
            {
               const double endMs = Bench::MediaNowMs();
               h->bench->decodeMs.Push(endMs - benchDecodeStartMs);
               h->bench->decoded.fetch_add(1, std::memory_order_relaxed);
               if (h->bench->restartStartMs >= 0.0)
               {
                  h->bench->loopDecodeMs.Push(endMs - h->bench->restartStartMs);
                  h->bench->restartStartMs = -1.0;
               }
            }
            if (!decodedOne)
            {
               // Frame's buffer, if reused from the recycle pool, goes back
               // untouched - nothing decoded this iteration.
               {
                  std::lock_guard<std::mutex> lock(h->mutex);
                  h->recycle.push_back(std::move(slot));
               }
               if (h->endOfStream.load())
               {
                  std::unique_lock<std::mutex> ulock(h->mutex);
                  h->cv.wait_for(ulock, std::chrono::milliseconds(4));
               }
               continue;
            }

            {
               std::lock_guard<std::mutex> lock(h->mutex);
               h->ready.push_back(std::move(slot));
            }
            h->cv.notify_all();
         }

         av_freep(&rgbaFrame->data[0]);
         av_frame_free(&rgbaFrame);
         av_frame_free(&frame);
         av_packet_free(&pkt);
         sws_freeContext(sws);
         avcodec_free_context(&codecCtx);
         avformat_close_input(&fmt);
         h->running.store(false);
         h->threadDone.store(true);
      }
   }

   std::string OpenVideoDialog()
   {
      if (std::getenv("INFINITE_EXITAFTER") != nullptr) return "";
      const char* disp = std::getenv("DISPLAY");
      const char* wayland = std::getenv("WAYLAND_DISPLAY");
      if ((!disp || disp[0] == '\0') && (!wayland || wayland[0] == '\0')) return "";

      const char* const filterPatterns[] = {
         "*.mp4", "*.mov", "*.m4v", "*.avi", "*.mkv", "*.webm", "*.wmv"
      };
      const char* res = tinyfd_openFileDialog(
         "Choose Video",
         "",
         (int)(sizeof(filterPatterns) / sizeof(filterPatterns[0])),
         filterPatterns,
         "Video files",
         0
      );
      return res ? std::string(res) : std::string();
   }

   VideoHandle* VideoOpen(const std::string& path, std::string& outError)
   {
      VideoHandle* h = new VideoHandle();
      h->path = path;
      if (Bench::MediaIoEnabled().load(std::memory_order_relaxed))
         h->bench = std::make_unique<Bench::MediaDecodeStats>();
      h->thread = std::thread(VideoThreadMain, h);

      // Poll up to 4s for the thread to either start running or fail -
      // mirrors MediaWin.cpp's VideoOpen so callers on both platforms see
      // the same "open is synchronous from the caller's point of view, even
      // though decode itself is threaded" contract.
      for (int i = 0; i < 160; ++i)
      {
         if (h->running.load() || h->threadDone.load())
            break;
         std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }

      if (!h->running.load())
      {
         outError = h->error.empty() ? "failed to open video" : h->error;
         delete h;
         return nullptr;
      }
      return h;
   }

   void VideoClose(VideoHandle* handle)
   {
      delete handle;
   }

   int VideoWidth(VideoHandle* handle)
   {
      return handle ? handle->width : 0;
   }

   int VideoHeight(VideoHandle* handle)
   {
      return handle ? handle->height : 0;
   }

   double VideoDuration(VideoHandle* handle)
   {
      return handle ? handle->duration : 0.0;
   }

   bool VideoFrameAt(VideoHandle* handle, double seconds, std::vector<unsigned char>& outPixels)
   {
      if (handle == nullptr)
         return false;

      handle->targetSeconds.store(seconds);

      bool produced = false;
      Bench::MediaDecodeStats* bench = handle->bench.get();
      {
         std::lock_guard<std::mutex> lock(handle->mutex);
         uint32_t benchPopped = 0;
         // Pick the newest queued frame at or before target+epsilon;
         // recycle every older one (including a previously-picked one that
         // an even newer frame this call supersedes) into the pool instead
         // of freeing it.
         while (!handle->ready.empty() &&
                handle->ready.front().pts <= seconds + kVideoEpsilonSeconds)
         {
            DecodedVideoFrame frame = std::move(handle->ready.front());
            handle->ready.pop_front();
            outPixels.swap(frame.rgba);
            handle->deliveredSeconds = frame.pts;
            // Cache what's being delivered (a copy - `frame.rgba` now holds
            // whatever outPixels previously held, and is about to be reused
            // via recycle) so a subsequent single-frame reverse step can be
            // served without ever bothering the decode thread.
            PushCacheFrameLocked(handle, frame.pts, outPixels);
            handle->recycle.push_back(std::move(frame)); // now holds the previous outPixels contents (if any)
            produced = true;
            benchPopped++;
         }
         if (bench && benchPopped > 1)
            bench->dropped.fetch_add(benchPopped - 1, std::memory_order_relaxed);

         // Nothing newly ready (decode is monotonically forward, so a
         // backward step's target is almost never in `ready`) - try the
         // reverse-playback cache before telling the caller nothing's
         // available. This is what lets reverse/fast-scrub playback stay
         // smooth without a fresh seek on every single step.
         if (!produced)
         {
            double cachedPts = 0.0;
            const double benchCacheStartMs = bench ? Bench::MediaNowMs() : 0.0;
            if (TryUseCacheLocked(handle, seconds, outPixels, cachedPts))
            {
               handle->deliveredSeconds = cachedPts;
               produced = true;
               if (bench)
               {
                  bench->cacheHitMs.Push(Bench::MediaNowMs() - benchCacheStartMs);
                  bench->cacheHits.fetch_add(1, std::memory_order_relaxed);
               }
            }
         }
         if (bench && produced)
            bench->deliveredPts = handle->deliveredSeconds.load();
      }
      handle->cv.notify_all();
      return produced;
   }

   Bench::MediaDecodeStats* VideoBenchStats(VideoHandle* handle)
   {
      return handle ? handle->bench.get() : nullptr;
   }

   bool VideoDecodeIsCatchingUp(VideoHandle* handle)
   {
      if (handle == nullptr || !handle->running.load() || handle->endOfStream.load())
         return false;
      return handle->decodeHeadSeconds.load() < handle->targetSeconds.load();
   }

   // =====================================================================
   // Video-container audio-track decode, and the FFmpeg audio-file fallback
   // =====================================================================

   namespace
   {
      // Shared by DecodeVideoAudioTrackToBuffer and the FFmpeg fallback hook:
      // demux `path`, decode its best audio stream, resample to interleaved
      // float at the stream's native rate/channel count (clamped to stereo,
      // matching SampleBuffer's contract), deinterleave into outBuffer.
      bool FfmpegDecodeAudioTrack(const std::string& path, SampleBuffer& outBuffer,
                                 std::string& outError, bool noAudioIsExpected)
      {
         outBuffer = SampleBuffer();

         AVFormatContext* fmt = nullptr;
         if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
         {
            outError = "could not open file";
            return false;
         }
         if (avformat_find_stream_info(fmt, nullptr) < 0)
         {
            outError = "could not read stream info";
            avformat_close_input(&fmt);
            return false;
         }

         const int streamIndex = FindBestStream(fmt, AVMEDIA_TYPE_AUDIO);
         if (streamIndex < 0)
         {
            outError = noAudioIsExpected ? "no audio track in this file" : "file contains no audio";
            avformat_close_input(&fmt);
            return false;
         }

         AVStream* stream = fmt->streams[streamIndex];
         const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
         if (codec == nullptr)
         {
            outError = "unsupported audio codec";
            avformat_close_input(&fmt);
            return false;
         }

         AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
         avcodec_parameters_to_context(codecCtx, stream->codecpar);
         if (avcodec_open2(codecCtx, codec, nullptr) < 0)
         {
            outError = "could not open audio decoder";
            avcodec_free_context(&codecCtx);
            avformat_close_input(&fmt);
            return false;
         }

         const int outChannels = std::min(codecCtx->ch_layout.nb_channels > 0
                                             ? codecCtx->ch_layout.nb_channels : 2, 2);
         const int outRate = codecCtx->sample_rate > 0 ? codecCtx->sample_rate : 48000;

         AVChannelLayout outLayout;
         av_channel_layout_default(&outLayout, outChannels);

         SwrContext* swr = nullptr;
         swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, outRate,
                             &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
                             0, nullptr);
         if (swr == nullptr || swr_init(swr) < 0)
         {
            outError = "could not initialise resampler";
            av_channel_layout_uninit(&outLayout);
            if (swr) swr_free(&swr);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&fmt);
            return false;
         }

         std::vector<float> interleaved;
         AVPacket* pkt = av_packet_alloc();
         AVFrame* frame = av_frame_alloc();

         auto drainDecoder = [&]() {
            for (;;)
            {
               const int recvErr = avcodec_receive_frame(codecCtx, frame);
               if (recvErr != 0)
                  break;

               const int maxOutSamples = swr_get_out_samples(swr, frame->nb_samples);
               std::vector<float> chunk((size_t)maxOutSamples * outChannels);
               uint8_t* outPtrs[1] = { reinterpret_cast<uint8_t*>(chunk.data()) };
               const int gotSamples = swr_convert(swr, outPtrs, maxOutSamples,
                                                  (const uint8_t**)frame->data, frame->nb_samples);
               if (gotSamples > 0)
                  interleaved.insert(interleaved.end(), chunk.begin(),
                                    chunk.begin() + (size_t)gotSamples * outChannels);
            }
         };

         while (av_read_frame(fmt, pkt) >= 0)
         {
            if (pkt->stream_index == streamIndex)
            {
               if (avcodec_send_packet(codecCtx, pkt) == 0)
                  drainDecoder();
            }
            av_packet_unref(pkt);
         }
         avcodec_send_packet(codecCtx, nullptr);
         drainDecoder();

         // Flush any samples swresample buffered internally.
         for (;;)
         {
            const int maxOutSamples = swr_get_out_samples(swr, 0);
            if (maxOutSamples <= 0)
               break;
            std::vector<float> chunk((size_t)maxOutSamples * outChannels);
            uint8_t* outPtrs[1] = { reinterpret_cast<uint8_t*>(chunk.data()) };
            const int gotSamples = swr_convert(swr, outPtrs, maxOutSamples, nullptr, 0);
            if (gotSamples <= 0)
               break;
            interleaved.insert(interleaved.end(), chunk.begin(),
                              chunk.begin() + (size_t)gotSamples * outChannels);
         }

         av_frame_free(&frame);
         av_packet_free(&pkt);
         av_channel_layout_uninit(&outLayout);
         swr_free(&swr);
         avcodec_free_context(&codecCtx);
         avformat_close_input(&fmt);

         const uint64_t totalFrames = outChannels > 0 ? interleaved.size() / outChannels : 0;
         if (totalFrames == 0)
         {
            outError = "file contains no audio";
            return false;
         }

         outBuffer.channels = outChannels;
         outBuffer.numFrames = (int)totalFrames;
         outBuffer.sampleRate = (double)outRate;
         outBuffer.channelData.assign((size_t)totalFrames * outChannels, 0.0f);
         for (uint64_t f = 0; f < totalFrames; ++f)
            for (int ch = 0; ch < outChannels; ++ch)
               outBuffer.channelData[(size_t)ch * totalFrames + f] = interleaved[f * outChannels + ch];

         return true;
      }

      // Registers FfmpegDecodeAudioTrack as MediaDecodePortable.cpp's
      // fallback for containers dr_libs can't read (m4a/m4b/caf/ogg/opus).
      // Global-constructor registration is safe here: it only stores a
      // function pointer, no FFmpeg initialisation happens until the hook is
      // actually invoked.
      bool FfmpegAudioFileFallback(const std::string& path, SampleBuffer& outBuffer,
                                   std::string& outError)
      {
         return FfmpegDecodeAudioTrack(path, outBuffer, outError, /*noAudioIsExpected=*/false);
      }

      struct RegisterFfmpegAudioHook
      {
         RegisterFfmpegAudioHook() { SetFfmpegAudioDecodeHook(&FfmpegAudioFileFallback); }
      } gRegisterFfmpegAudioHook;
   }

   bool DecodeVideoAudioTrackToBuffer(const std::string& path, SampleBuffer& outBuffer,
                                      std::string& outError)
   {
      return FfmpegDecodeAudioTrack(path, outBuffer, outError, /*noAudioIsExpected=*/true);
   }

   // =====================================================================
   // Recorder
   // =====================================================================

   namespace
   {
      constexpr size_t kDefaultQueueByteBudget = 256ull * 1024 * 1024;

      struct QueuedFrame
      {
         std::vector<unsigned char> pixels; // BGRA8 or RGBA8, bottom-up
         int repeatCount = 1;
      };
   }

   struct RecorderHandle
   {
      std::string path;
      int width = 0;
      int height = 0;
      int fps = 30;
      bool inputIsBgra = true;

      AVFormatContext* fmt = nullptr;
      AVCodecContext* videoCodecCtx = nullptr;
      AVStream* videoStream = nullptr;
      SwsContext* toYuvSws = nullptr;

      AVCodecContext* audioCodecCtx = nullptr;
      AVStream* audioStream = nullptr;
      SwrContext* toAacSwr = nullptr;
      bool hasAudio = false;
      bool liveAudio = false;
      double liveAudioRate = 0.0;
      int liveAudioChannels = 2;
      int64_t audioSamplesWritten = 0;
      std::vector<float> pendingLiveAudio; // interleaved backlog

      // File-audio-source take: decoded eagerly at Start, written at Stop
      // once the final video duration is known (looped/truncated to match).
      std::string audioPath;
      bool loopAudio = true;
      SampleBuffer fileAudio;
      bool haveFileAudio = false;

      std::atomic<int64_t> frameCount{ 0 };
      std::atomic<int> pendingCount{ 0 };
      std::atomic<int> droppedCount{ 0 };
      int64_t nextPts = 0;

      std::deque<QueuedFrame> frameQueue;
      size_t queuedBytes = 0;
      size_t queueByteBudget = kDefaultQueueByteBudget;
      std::mutex queueMutex;
      std::condition_variable queueCv;
      std::atomic<bool> stopRequested{ false };

      std::vector<std::vector<unsigned char>> bufferPool;
      size_t poolBytes = 0;
      std::mutex poolMutex;
      static constexpr size_t kPoolByteBudget = 128ull * 1024 * 1024;

      std::mutex writerMutex;
      std::thread worker;
      bool finalized = false;
      bool videoOnlyFallback = false;

      ~RecorderHandle()
      {
         stopRequested.store(true);
         queueCv.notify_all();
         if (worker.joinable())
            worker.join();
         if (!finalized && fmt != nullptr)
         {
            // Destroyed without Stop/Cancel explicitly finalizing (e.g. an
            // exception path) - close as cleanly as possible without
            // pretending the file is a valid, playable movie.
            if (fmt->pb != nullptr)
               avio_closep(&fmt->pb);
         }
         if (videoCodecCtx) avcodec_free_context(&videoCodecCtx);
         if (audioCodecCtx) avcodec_free_context(&audioCodecCtx);
         if (toYuvSws) sws_freeContext(toYuvSws);
         if (toAacSwr) swr_free(&toAacSwr);
         if (fmt) avformat_free_context(fmt);
      }
   };

   namespace
   {
      // width*height*fps-scaled bitrate, same formula MediaWin.cpp uses so
      // the two platforms produce comparably-sized files for the same take.
      int64_t RecorderVideoBitrate(int width, int height, int fps)
      {
         const double target = (double)width * height * fps * 0.30;
         return (int64_t)std::min(80e6, std::max(2e6, target));
      }

      bool ConfigureRecorderVideo(RecorderHandle* h, std::string& outError)
      {
#if defined(INFINITE_ENABLE_GPL_CODECS)
         const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
#else
         const AVCodec* codec = nullptr;
#endif
         if (codec == nullptr)
         {
            outError = "H.264 encoding is unavailable in this build "
                       "(INFINITE_ENABLE_GPL_CODECS=OFF - see LICENSE)";
            return false;
         }

         h->videoStream = avformat_new_stream(h->fmt, nullptr);
         h->videoCodecCtx = avcodec_alloc_context3(codec);
         h->videoCodecCtx->width = h->width;
         h->videoCodecCtx->height = h->height;
         h->videoCodecCtx->time_base = AVRational{ 1, h->fps };
         h->videoStream->time_base = h->videoCodecCtx->time_base;
         h->videoCodecCtx->framerate = AVRational{ h->fps, 1 };
         h->videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
         h->videoCodecCtx->bit_rate = RecorderVideoBitrate(h->width, h->height, h->fps);
         // Tried shrinking this to bound reverse-scrub redecode distance
         // (a quarter-second GOP) while chasing INFINITE_VIDEOSPEEDTEST's low
         // FrameUpdateCount - measurably changed the encode (keyint 7 vs 60,
         // 9 I-frames vs 1) but made FrameUpdateCount *worse*, not better,
         // across repeated runs. That disproves sparse keyframes as the
         // dominant cost: the real bottleneck is that every single-frame
         // reverse step re-seeks and pays av_seek_frame/avcodec_flush_buffers
         // overhead regardless of how close the nearest keyframe is (see the
         // reverse-playback frame cache added to VideoThreadMain/VideoFrameAt
         // below, which is the actual fix - ported from Platform.mm's
         // kReverseLookbackSeconds/frameCache design). Left at the original
         // 2-second GOP.
         h->videoCodecCtx->gop_size = h->fps * 2;
         if (h->fmt->oformat->flags & AVFMT_GLOBALHEADER)
            h->videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

         av_opt_set(h->videoCodecCtx->priv_data, "preset", "veryfast", 0);
         av_opt_set(h->videoCodecCtx->priv_data, "crf", "20", 0);

         if (avcodec_open2(h->videoCodecCtx, codec, nullptr) < 0)
         {
            outError = "could not open H.264 encoder";
            return false;
         }
         avcodec_parameters_from_context(h->videoStream->codecpar, h->videoCodecCtx);

         h->toYuvSws = sws_getContext(h->width, h->height, AV_PIX_FMT_BGRA,
                                      h->width, h->height, AV_PIX_FMT_YUV420P,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
         return true;
      }

      bool AddRecorderAudioStream(RecorderHandle* h, int channels, double rate, std::string& outError)
      {
         const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
         if (codec == nullptr)
         {
            outError = "AAC encoder unavailable";
            return false;
         }

         // AAC via FFmpeg's native encoder only accepts a handful of
         // sample rates; 44100/48000 are always supported, matching
         // MediaWin.cpp's constraint on the Media Foundation AAC MFT.
         const int outRate = (rate == 44100.0 || rate == 48000.0) ? (int)rate : 48000;

         h->audioStream = avformat_new_stream(h->fmt, nullptr);
         h->audioCodecCtx = avcodec_alloc_context3(codec);
         h->audioCodecCtx->sample_rate = outRate;
         av_channel_layout_default(&h->audioCodecCtx->ch_layout, channels);
         h->audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
         h->audioCodecCtx->bit_rate = 192000;
         h->audioCodecCtx->time_base = AVRational{ 1, outRate };
         h->audioStream->time_base = h->audioCodecCtx->time_base;
         if (h->fmt->oformat->flags & AVFMT_GLOBALHEADER)
            h->audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

         if (avcodec_open2(h->audioCodecCtx, codec, nullptr) < 0)
         {
            outError = "could not open AAC encoder";
            return false;
         }
         avcodec_parameters_from_context(h->audioStream->codecpar, h->audioCodecCtx);

         AVChannelLayout inLayout;
         av_channel_layout_default(&inLayout, channels);
         swr_alloc_set_opts2(&h->toAacSwr, &h->audioCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, outRate,
                             &inLayout, AV_SAMPLE_FMT_FLT, (int)rate, 0, nullptr);
         av_channel_layout_uninit(&inLayout);
         if (h->toAacSwr == nullptr || swr_init(h->toAacSwr) < 0)
         {
            outError = "could not initialise audio resampler";
            return false;
         }

         h->hasAudio = true;
         return true;
      }

      // Encodes and mux's one already-prepared AVFrame (video or audio;
      // pkt->stream_index carries which) through the shared writer lock -
      // mirrors MediaWin.cpp's writerMutex serialising WriteSample calls
      // from both the worker thread (video) and the caller thread (live
      // audio).
      void EncodeAndWrite(RecorderHandle* h, AVCodecContext* codecCtx, AVStream* stream, AVFrame* frame)
      {
         std::lock_guard<std::mutex> lock(h->writerMutex);
         if (avcodec_send_frame(codecCtx, frame) < 0)
            return;
         AVPacket* pkt = av_packet_alloc();
         while (avcodec_receive_packet(codecCtx, pkt) == 0)
         {
            av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            av_interleaved_write_frame(h->fmt, pkt);
            av_packet_unref(pkt);
         }
         av_packet_free(&pkt);
      }

      void WriteVideoFrame(RecorderHandle* h, const std::vector<unsigned char>& pixels, int64_t pts)
      {
         // `pixels` is BGRA8 bottom-up (RecorderAppend already normalised
         // the RGBA fallback into BGRA); glReadPixels bottom-up rows need
         // flipping to the top-down rows swscale/x264 expect, done here via
         // a negative source stride/base-pointer offset rather than an
         // extra copy.
         const int rowBytes = h->width * 4;
         const uint8_t* srcData[1] = { pixels.data() + (size_t)(h->height - 1) * rowBytes };
         const int srcLinesize[1] = { -rowBytes };

         AVFrame* yuv = av_frame_alloc();
         yuv->format = AV_PIX_FMT_YUV420P;
         yuv->width = h->width;
         yuv->height = h->height;
         av_frame_get_buffer(yuv, 0);

         sws_scale(h->toYuvSws, srcData, srcLinesize, 0, h->height, yuv->data, yuv->linesize);
         yuv->pts = pts;

         EncodeAndWrite(h, h->videoCodecCtx, h->videoStream, yuv);

         av_frame_free(&yuv);
      }

      void RecorderWorkerThreadMain(RecorderHandle* h)
      {
         std::vector<unsigned char> topDown; // scratch, converted once per queued frame
         for (;;)
         {
            QueuedFrame item;
            {
               std::unique_lock<std::mutex> lock(h->queueMutex);
               h->queueCv.wait(lock, [&] { return !h->frameQueue.empty() || h->stopRequested.load(); });
               if (h->frameQueue.empty())
               {
                  if (h->stopRequested.load())
                     break;
                  continue;
               }
               item = std::move(h->frameQueue.front());
               h->frameQueue.pop_front();
               h->queuedBytes -= item.pixels.size();
            }
            h->pendingCount.fetch_sub(1);

            // Convert once; if BGRA already (the default), no conversion
            // needed - sws_scale's source format already says BGRA.
            for (int r = 0; r < item.repeatCount; ++r)
            {
               WriteVideoFrame(h, item.pixels, h->nextPts);
               h->nextPts += 1;
               h->frameCount.fetch_add(1);
            }

            // Recycle into the pool for RecorderAcquireFrameBuffer, if there
            // is budget left.
            {
               std::lock_guard<std::mutex> lock(h->poolMutex);
               if (h->poolBytes + item.pixels.size() <= RecorderHandle::kPoolByteBudget)
               {
                  h->poolBytes += item.pixels.size();
                  h->bufferPool.push_back(std::move(item.pixels));
               }
            }
         }
      }

      // Writes the whole file-audio-source track at Stop, once the final
      // video duration is known - looped or truncated to match, exactly as
      // MediaWin.cpp's WriteFileAudioTrack does.
      void WriteFileAudioTrack(RecorderHandle* h)
      {
         if (!h->haveFileAudio || h->fileAudio.numFrames <= 0)
            return;

         const double videoDurationSeconds = (double)h->frameCount.load() / (double)h->fps;
         const double srcRate = h->fileAudio.sampleRate;
         const int64_t neededSrcFrames = (int64_t)(videoDurationSeconds * srcRate);
         const int channels = h->fileAudio.channels;
         const int64_t srcFrames = h->fileAudio.numFrames;

         std::vector<float> interleaved;
         interleaved.reserve((size_t)neededSrcFrames * channels);
         int64_t written = 0;
         while (written < neededSrcFrames)
         {
            const int64_t remaining = neededSrcFrames - written;
            const int64_t chunk = std::min(remaining, srcFrames);
            for (int64_t f = 0; f < chunk; ++f)
               for (int ch = 0; ch < channels; ++ch)
                  interleaved.push_back(h->fileAudio.channelData[(size_t)ch * srcFrames + f]);
            written += chunk;
            if (!h->loopAudio)
               break;
         }

         if (!interleaved.empty())
            RecorderAppendAudio(h, interleaved.data(), (int)(interleaved.size() / channels));
         RecorderFinishAudioInput(h);
      }
   }

   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError,
                                 const std::string& audioPath,
                                 bool loopAudio,
                                 double liveAudioSampleRate,
                                 int liveAudioChannels)
   {
      RecorderHandle* h = new RecorderHandle();
      h->path = path;
      h->width = width;
      h->height = height;
      h->fps = fps;
      h->audioPath = audioPath;
      h->loopAudio = loopAudio;

      if (avformat_alloc_output_context2(&h->fmt, nullptr, "mp4", path.c_str()) < 0 || h->fmt == nullptr)
      {
         outError = "could not allocate output context";
         delete h;
         return nullptr;
      }

      if (!ConfigureRecorderVideo(h, outError))
      {
         delete h;
         return nullptr;
      }

      // Audio, if requested - added BEFORE avformat_write_header, same
      // constraint as MediaWin.cpp's AddStream/BeginWriting ordering.
      // On failure, fall back to a video-only take rather than failing the
      // whole recording.
      if (liveAudioSampleRate > 0.0)
      {
         h->liveAudio = true;
         h->liveAudioRate = liveAudioSampleRate;
         h->liveAudioChannels = liveAudioChannels;
         std::string audioErr;
         if (!AddRecorderAudioStream(h, liveAudioChannels, liveAudioSampleRate, audioErr))
         {
            h->liveAudio = false;
            h->hasAudio = false;
            h->videoOnlyFallback = true;
         }
      }
      else if (!audioPath.empty())
      {
         SampleBuffer buf;
         std::string decodeErr;
         if (DecodeAudioFileToBuffer(audioPath, buf, decodeErr) ||
             DecodeVideoAudioTrackToBuffer(audioPath, buf, decodeErr))
         {
            h->fileAudio = buf;
            h->haveFileAudio = true;
            std::string audioErr;
            if (!AddRecorderAudioStream(h, buf.channels, buf.sampleRate, audioErr))
            {
               h->haveFileAudio = false;
               h->hasAudio = false;
               h->videoOnlyFallback = true;
            }
         }
      }

      if (avio_open(&h->fmt->pb, path.c_str(), AVIO_FLAG_WRITE) < 0)
      {
         outError = "could not open output file";
         delete h;
         return nullptr;
      }

      AVDictionary* muxOpts = nullptr;
      av_dict_set(&muxOpts, "movflags", "+faststart", 0);
      if (avformat_write_header(h->fmt, &muxOpts) < 0)
      {
         av_dict_free(&muxOpts);
         outError = "could not write output header";
         delete h;
         return nullptr;
      }
      av_dict_free(&muxOpts);

      h->worker = std::thread(RecorderWorkerThreadMain, h);
      return h;
   }

   void RecorderSetInputIsBgra(RecorderHandle* handle, bool isBgra)
   {
      if (handle) handle->inputIsBgra = isBgra;
   }

   std::vector<unsigned char> RecorderAcquireFrameBuffer(RecorderHandle* handle)
   {
      if (handle == nullptr)
         return {};
      std::lock_guard<std::mutex> lock(handle->poolMutex);
      if (!handle->bufferPool.empty())
      {
         std::vector<unsigned char> buf = std::move(handle->bufferPool.back());
         handle->bufferPool.pop_back();
         handle->poolBytes -= buf.size();
         buf.resize((size_t)handle->width * handle->height * 4);
         return buf;
      }
      return std::vector<unsigned char>((size_t)handle->width * handle->height * 4);
   }

   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels)
   {
      if (handle == nullptr)
         return false;
      std::vector<unsigned char> copy = pixels;
      return RecorderAppend(handle, std::move(copy), 1);
   }

   bool RecorderAppend(RecorderHandle* handle, std::vector<unsigned char>&& pixels, int repeatCount)
   {
      if (handle == nullptr)
         return false;

      // RGBA fallback path: convert to BGRA up front so the worker thread
      // (and swscale, whose source format is fixed at AV_PIX_FMT_BGRA) never
      // needs to branch per-frame.
      if (!handle->inputIsBgra)
      {
         for (size_t i = 0; i + 3 < pixels.size(); i += 4)
            std::swap(pixels[i], pixels[i + 2]);
      }

      const size_t bytes = pixels.size();
      {
         std::lock_guard<std::mutex> lock(handle->queueMutex);
         if (handle->queuedBytes + bytes > handle->queueByteBudget)
         {
            handle->droppedCount.fetch_add(1);
            return false;
         }
         handle->queuedBytes += bytes;
         handle->frameQueue.push_back(QueuedFrame{ std::move(pixels), repeatCount });
      }
      handle->pendingCount.fetch_add(1);
      handle->queueCv.notify_all();
      return true;
   }

   int RecorderPendingFrameCount(RecorderHandle* handle)
   {
      return handle ? handle->pendingCount.load() : 0;
   }

   int RecorderDroppedFrameCount(RecorderHandle* handle)
   {
      return handle ? handle->droppedCount.load() : 0;
   }

   void RecorderSetTestQueueByteBudget(RecorderHandle* handle, size_t bytes)
   {
      if (handle == nullptr) return;
      std::lock_guard<std::mutex> lock(handle->queueMutex);
      handle->queueByteBudget = bytes;
   }

   bool RecorderAppendAudio(RecorderHandle* handle, const float* interleavedSamples, int numFrames)
   {
      if (handle == nullptr || !handle->hasAudio || handle->toAacSwr == nullptr)
         return false;

      const int channels = handle->audioCodecCtx->ch_layout.nb_channels;
      handle->pendingLiveAudio.insert(handle->pendingLiveAudio.end(), interleavedSamples,
                                      interleavedSamples + (size_t)numFrames * channels);
      RecorderFlushPendingAudio(handle);
      return true;
   }

   bool RecorderStop(RecorderHandle* handle, std::string& outError,
                     int* outFrameCount, int* outDroppedCount)
   {
      if (handle == nullptr)
      {
         outError = "null handle";
         if (outFrameCount) *outFrameCount = 0;
         if (outDroppedCount) *outDroppedCount = 0;
         return false;
      }

      handle->stopRequested.store(true);
      handle->queueCv.notify_all();
      if (handle->worker.joinable())
         handle->worker.join();

      if (outFrameCount) *outFrameCount = (int)handle->frameCount.load();
      if (outDroppedCount) *outDroppedCount = handle->droppedCount.load();

      if (handle->haveFileAudio)
         WriteFileAudioTrack(handle);
      else if (handle->liveAudio)
         RecorderFinishAudioInput(handle);

      // Flush the video encoder (send a null frame) so B-frames still
      // buffered internally are written before finalising.
      {
         std::lock_guard<std::mutex> lock(handle->writerMutex);
         if (handle->videoCodecCtx != nullptr)
         {
            avcodec_send_frame(handle->videoCodecCtx, nullptr);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(handle->videoCodecCtx, pkt) == 0)
            {
               av_packet_rescale_ts(pkt, handle->videoCodecCtx->time_base, handle->videoStream->time_base);
               pkt->stream_index = handle->videoStream->index;
               av_interleaved_write_frame(handle->fmt, pkt);
               av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
         }
      }

      av_write_trailer(handle->fmt);
      avio_closep(&handle->fmt->pb);
      handle->finalized = true;

      delete handle;
      return true;
   }

   void RecorderCancel(RecorderHandle* handle)
   {
      if (handle == nullptr)
         return;
      {
         std::lock_guard<std::mutex> lock(handle->queueMutex);
         handle->frameQueue.clear();
         handle->queuedBytes = 0;
      }
      handle->stopRequested.store(true);
      handle->queueCv.notify_all();
      if (handle->worker.joinable())
         handle->worker.join();

      // No flush/finalize - the file has no valid trailer/moov and is
      // therefore unplayable; delete it rather than leave a corrupt file.
      if (handle->fmt != nullptr && handle->fmt->pb != nullptr)
         avio_closep(&handle->fmt->pb);
      handle->finalized = true;
      std::remove(handle->path.c_str());

      delete handle;
   }

   bool RecorderQueueHasRoom(RecorderHandle* handle, size_t bytes)
   {
      if (handle == nullptr)
         return false;
      std::lock_guard<std::mutex> lock(handle->queueMutex);
      return handle->queuedBytes + bytes <= handle->queueByteBudget;
   }

   void RecorderFlushPendingAudio(RecorderHandle* handle)
   {
      if (handle == nullptr || !handle->hasAudio || handle->toAacSwr == nullptr)
         return;

      const int channels = handle->audioCodecCtx->ch_layout.nb_channels;
      const int frameSize = handle->audioCodecCtx->frame_size > 0 ? handle->audioCodecCtx->frame_size : 1024;

      while ((int)(handle->pendingLiveAudio.size() / channels) >= frameSize)
      {
         const uint8_t* srcPtrs[1] = { reinterpret_cast<const uint8_t*>(handle->pendingLiveAudio.data()) };

         AVFrame* frame = av_frame_alloc();
         frame->format = AV_SAMPLE_FMT_FLTP;
         frame->sample_rate = handle->audioCodecCtx->sample_rate;
         av_channel_layout_copy(&frame->ch_layout, &handle->audioCodecCtx->ch_layout);
         frame->nb_samples = frameSize;
         av_frame_get_buffer(frame, 0);

         uint8_t* dstPtrs[8] = { nullptr };
         for (int ch = 0; ch < channels; ++ch)
            dstPtrs[ch] = frame->data[ch];

         swr_convert(handle->toAacSwr, dstPtrs, frameSize, srcPtrs, frameSize);
         frame->pts = handle->audioSamplesWritten;
         handle->audioSamplesWritten += frameSize;

         EncodeAndWrite(handle, handle->audioCodecCtx, handle->audioStream, frame);
         av_frame_free(&frame);

         handle->pendingLiveAudio.erase(handle->pendingLiveAudio.begin(),
                                        handle->pendingLiveAudio.begin() + (size_t)frameSize * channels);
      }
   }

   void RecorderKickEncoder(RecorderHandle* handle)
   {
      // The avformat/avcodec push model has no not-ready callback to wait
      // on - avcodec_send_frame/receive_packet already drain everything the
      // encoder can produce synchronously on every write. Nothing to kick.
      (void)handle;
   }

   void RecorderFinishAudioInput(RecorderHandle* handle)
   {
      if (handle == nullptr || !handle->hasAudio)
         return;

      // Flush any final partial frame (padded with silence) plus the
      // encoder's internal buffering.
      const int channels = handle->audioCodecCtx->ch_layout.nb_channels;
      if (!handle->pendingLiveAudio.empty())
      {
         const int frameSize = handle->audioCodecCtx->frame_size > 0 ? handle->audioCodecCtx->frame_size : 1024;
         const int haveFrames = (int)(handle->pendingLiveAudio.size() / channels);
         handle->pendingLiveAudio.resize((size_t)frameSize * channels, 0.0f);
         (void)haveFrames;
         RecorderFlushPendingAudio(handle);
      }

      std::lock_guard<std::mutex> lock(handle->writerMutex);
      if (handle->audioCodecCtx != nullptr)
      {
         avcodec_send_frame(handle->audioCodecCtx, nullptr);
         AVPacket* pkt = av_packet_alloc();
         while (avcodec_receive_packet(handle->audioCodecCtx, pkt) == 0)
         {
            av_packet_rescale_ts(pkt, handle->audioCodecCtx->time_base, handle->audioStream->time_base);
            pkt->stream_index = handle->audioStream->index;
            av_interleaved_write_frame(handle->fmt, pkt);
            av_packet_unref(pkt);
         }
         av_packet_free(&pkt);
      }
   }

   std::string RecorderDebugState(RecorderHandle* handle)
   {
      if (handle == nullptr)
         return "null handle";
      return "frames=" + std::to_string(handle->frameCount.load()) +
             " pending=" + std::to_string(handle->pendingCount.load()) +
             " dropped=" + std::to_string(handle->droppedCount.load()) +
             " hasAudio=" + std::string(handle->hasAudio ? "yes" : "no") +
             " videoOnlyFallback=" + std::string(handle->videoOnlyFallback ? "yes" : "no");
   }

   int RecorderFrameCount(RecorderHandle* handle)
   {
      return handle ? (int)handle->frameCount.load() : 0;
   }

   MovieInfo InspectMovie(const std::string& path)
   {
      MovieInfo info;

      AVFormatContext* fmt = nullptr;
      if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
         return info;
      if (avformat_find_stream_info(fmt, nullptr) < 0)
      {
         avformat_close_input(&fmt);
         return info;
      }

      if (fmt->duration > 0)
         info.duration = (double)fmt->duration / (double)AV_TIME_BASE;

      const int videoIndex = FindBestStream(fmt, AVMEDIA_TYPE_VIDEO);
      const int audioIndex = FindBestStream(fmt, AVMEDIA_TYPE_AUDIO);
      info.hasVideo = videoIndex >= 0;
      info.hasAudio = audioIndex >= 0;

      if (videoIndex >= 0)
      {
         AVStream* s = fmt->streams[videoIndex];
         if (s->duration > 0)
            info.videoDuration = (double)s->duration * av_q2d(s->time_base);
      }
      if (audioIndex >= 0)
      {
         AVStream* s = fmt->streams[audioIndex];
         if (s->duration > 0)
            info.audioDuration = (double)s->duration * av_q2d(s->time_base);
      }

      // Frame count: walk the actual encoded video packet stream rather
      // than trusting duration*fps, same reasoning as MediaWin.cpp's
      // InspectMovie.
      if (videoIndex >= 0)
      {
         AVPacket* pkt = av_packet_alloc();
         int count = 0;
         while (av_read_frame(fmt, pkt) >= 0)
         {
            if (pkt->stream_index == videoIndex)
               ++count;
            av_packet_unref(pkt);
         }
         av_packet_free(&pkt);
         info.frameCount = count;
      }

      avformat_close_input(&fmt);
      return info;
   }
}

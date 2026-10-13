// Turbo 0.53: video file decoding on FFmpeg.
//
// One worker thread per clip owns the demuxer and decoder. It decodes the
// frame the render thread asked for and then reads ahead in the playback
// direction into a small cache, so steady playback finds its next frame
// already decoded instead of waiting a frame for it.
//  - forward: sequential read-ahead; small jumps (speed > 1, a slow UI
//    frame) are reached by decoding through instead of a keyframe seek.
//  - reverse: one seek per batch. The worker decodes the frames just below
//    the target in one forward run and serves them backwards.
//
// Frames leave here in their native layout (Platform::VideoFrame): NV12 or
// planar YUV straight from the decoder, RGBA for DXV, and for HAP the raw
// BC blocks (core/HapDecode). No colour conversion or row flip on the CPU;
// core/VideoTexture does that in one shader pass.

#include "Platform.h"

#include "core/HapDecode.h"
#include "core/RuntimeLog.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace Platform
{
   struct VideoHandle
   {
      struct CachedFrame
      {
         long long index = -1;
         VideoFrame frame;
      };

      // ---- decoder state (decoder thread only after open) ----
      AVFormatContext* fmt = nullptr;
      AVCodecContext* dec = nullptr;
      AVBufferRef* hwDevice = nullptr;
      AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
      SwsContext* sws = nullptr;
      AVPacket* pkt = nullptr;
      AVFrame* frame = nullptr;
      AVFrame* swFrame = nullptr;
      int stream = -1;
      AVRational timeBase { 1, 1 };
      int64_t startPts = 0;
      bool hap = false;
      bool inputEof = false;
      int codedW = 0;
      int codedH = 0;
      long long lastDecodedFrame = -1000000;
      std::string description;

      std::atomic<int> width { 0 };
      std::atomic<int> height { 0 };
      double duration = 0.0;
      double fps = 30.0;
      long long frameCount = 0;          // guarded by frameMutex (shrinks if the container lied)

      // ---- request / delivery ----
      std::mutex frameMutex;
      std::condition_variable frameReady;
      std::thread decoder;
      bool stopping = false;
      bool requestPending = false;
      long long requestedFrame = 0;
      long long lastQueuedFrame = -1;
      int direction = 1;
      uint64_t requestSerial = 0;
      uint64_t publishedSerial = 0;
      uint64_t consumedSerial = 0;
      VideoFrame published;
      std::deque<CachedFrame> cache;
      std::vector<VideoFrame> freeBuffers;
      size_t cacheCapacity = 4;

      ~VideoHandle()
      {
         {
            std::lock_guard<std::mutex> lock(frameMutex);
            stopping = true;
         }
         frameReady.notify_all();
         if (decoder.joinable())
            decoder.join();
         if (sws != nullptr) sws_freeContext(sws);
         av_frame_free(&swFrame);
         av_frame_free(&frame);
         av_packet_free(&pkt);
         avcodec_free_context(&dec);
         av_buffer_unref(&hwDevice);
         avformat_close_input(&fmt);
      }
   };

   namespace
   {
      constexpr long long kVideoDecodeThroughInsteadOfSeek = 12;
      constexpr size_t kVideoCacheBudgetBytes = 192ull * 1024ull * 1024ull;

      std::string AvError(int err)
      {
         char buf[AV_ERROR_MAX_STRING_SIZE] = {};
         av_strerror(err, buf, sizeof(buf));
         return buf;
      }

      AVPixelFormat PickHwFormat(AVCodecContext* ctx, const AVPixelFormat* fmts)
      {
         const auto* v = static_cast<const VideoHandle*>(ctx->opaque);
         for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == v->hwPixFmt)
               return *p;
         // The GPU can't take this stream (4:2:2, odd profile): first software format.
         for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
         {
            const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*p);
            if (d != nullptr && (d->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0)
               return *p;
         }
         return fmts[0];
      }

      bool OpenDecoder(VideoHandle* v, std::string& error)
      {
         const AVCodecParameters* par = v->fmt->streams[v->stream]->codecpar;
         const AVCodec* codec = avcodec_find_decoder(par->codec_id);
         if (codec == nullptr)
         {
            error = std::string("no decoder for ") + avcodec_get_name(par->codec_id);
            return false;
         }
         v->dec = avcodec_alloc_context3(codec);
         if (v->dec == nullptr || avcodec_parameters_to_context(v->dec, par) < 0)
         {
            error = "could not set up the decoder";
            return false;
         }
         v->dec->pkt_timebase = v->timeBase;

         bool hw = false;
         const char* hwEnv = std::getenv("INFINITE_VIDEO_HWACCEL");
         if (hwEnv == nullptr || std::strcmp(hwEnv, "0") != 0)
         {
            for (int i = 0;; ++i)
            {
               const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
               if (cfg == nullptr)
                  break;
               if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                   cfg->device_type == AV_HWDEVICE_TYPE_D3D11VA)
               {
                  if (av_hwdevice_ctx_create(&v->hwDevice, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) >= 0)
                  {
                     v->dec->hw_device_ctx = av_buffer_ref(v->hwDevice);
                     v->hwPixFmt = cfg->pix_fmt;
                     v->dec->opaque = v;
                     v->dec->get_format = PickHwFormat;
                     hw = true;
                  }
                  break;
               }
            }
         }
         if (!hw)
         {
            // Intra codecs (DXV, ProRes, MJPEG) and software H.264 scale
            // across cores this way.
            v->dec->thread_count = 0;
            v->dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
         }
         const int r = avcodec_open2(v->dec, codec, nullptr);
         if (r < 0)
         {
            error = "could not open decoder: " + AvError(r);
            return false;
         }
         v->description = std::string(codec->long_name ? codec->long_name : codec->name) + (hw ? " (GPU decode)" : "");
         return true;
      }

      long long IndexFromTs(const VideoHandle* v, int64_t ts)
      {
         if (ts == AV_NOPTS_VALUE)
            return v->lastDecodedFrame + 1;
         const double seconds = (double)(ts - v->startPts) * av_q2d(v->timeBase);
         return (long long)std::llround(seconds * v->fps);
      }

      void Seek(VideoHandle* v, long long target)
      {
         const double seconds = (double)target / v->fps;
         const int64_t ts = v->startPts + (int64_t)std::llround(seconds / av_q2d(v->timeBase));
         av_seek_frame(v->fmt, v->stream, ts, AVSEEK_FLAG_BACKWARD);
         if (v->dec != nullptr)
            avcodec_flush_buffers(v->dec);
         v->inputEof = false;
      }

      // HAP: next packet of our stream into v->pkt.
      bool ReadPacket(VideoHandle* v)
      {
         av_packet_unref(v->pkt);
         for (;;)
         {
            if (av_read_frame(v->fmt, v->pkt) < 0)
               return false;
            if (v->pkt->stream_index == v->stream)
               return true;
            av_packet_unref(v->pkt);
         }
      }

      // Codec path: next decoded picture into v->frame. 1 = frame, 0 = end, -1 = error.
      int ReceiveFrame(VideoHandle* v)
      {
         for (int guard = 0; guard < 100000; ++guard)
         {
            const int r = avcodec_receive_frame(v->dec, v->frame);
            if (r == 0)
               return 1;
            if (r == AVERROR_EOF)
               return 0;
            if (r != AVERROR(EAGAIN))
               return -1;
            if (v->inputEof)
               return 0;
            for (;;)
            {
               if (av_read_frame(v->fmt, v->pkt) < 0)
               {
                  avcodec_send_packet(v->dec, nullptr); // drain
                  v->inputEof = true;
                  break;
               }
               if (v->pkt->stream_index == v->stream)
               {
                  avcodec_send_packet(v->dec, v->pkt); // a corrupt packet is skipped
                  av_packet_unref(v->pkt);
                  break;
               }
               av_packet_unref(v->pkt);
            }
         }
         return -1;
      }

      void CopyPlane(std::vector<unsigned char>& dst, const uint8_t* src, int linesize, int rowBytes, int rows)
      {
         dst.resize((size_t)rowBytes * (size_t)rows);
         av_image_copy_plane(dst.data(), rowBytes, src, linesize, rowBytes, rows);
      }

      bool ConvertHap(VideoHandle* v, VideoFrame& out)
      {
         Hap::Image img[2];
         img[0].blocks.swap(out.plane[0]); // reuse the buffers' capacity
         img[1].blocks.swap(out.plane[1]);
         const int n = Hap::DecodeFrame(v->pkt->data, (size_t)v->pkt->size, v->codedW, v->codedH, img);
         out.plane[0].swap(img[0].blocks);
         out.plane[1].swap(img[1].blocks);
         if (n == 0)
            return false;
         switch (img[0].format)
         {
            case Hap::TexFormat::BC1: out.format = VideoPixelFormat::BC1; break;
            case Hap::TexFormat::BC3: out.format = VideoPixelFormat::BC3; break;
            case Hap::TexFormat::BC7: out.format = VideoPixelFormat::BC7; break;
            case Hap::TexFormat::BC3YCoCg:
               out.format = n == 2 ? VideoPixelFormat::BC3YCoCgAlpha : VideoPixelFormat::BC3YCoCg;
               break;
            default: return false;
         }
         out.width = v->codedW;
         out.height = v->codedH;
         out.topDown = true;
         out.mirrorX = false;
         out.opaque = img[0].format == Hap::TexFormat::BC1;
         return true;
      }

      bool ConvertFrame(VideoHandle* v, VideoFrame& out)
      {
         AVFrame* src = v->frame;
         if (v->hwPixFmt != AV_PIX_FMT_NONE && src->format == v->hwPixFmt)
         {
            av_frame_unref(v->swFrame);
            if (av_hwframe_transfer_data(v->swFrame, src, 0) < 0)
               return false;
            v->swFrame->colorspace = src->colorspace;
            v->swFrame->color_range = src->color_range;
            src = v->swFrame;
         }
         const int w = src->width;
         const int h = src->height;
         if (w <= 0 || h <= 0)
            return false;
         const int cw = (w + 1) / 2;
         const int ch = (h + 1) / 2;
         out.width = w;
         out.height = h;
         out.topDown = true;
         out.mirrorX = false;
         out.opaque = false;
         out.yuv709 = src->colorspace == AVCOL_SPC_BT709 ||
                      (src->colorspace == AVCOL_SPC_UNSPECIFIED && h >= 720);
         out.fullRange = src->color_range == AVCOL_RANGE_JPEG;

         switch (src->format)
         {
            case AV_PIX_FMT_NV12:
               out.format = VideoPixelFormat::NV12;
               CopyPlane(out.plane[0], src->data[0], src->linesize[0], w, h);
               CopyPlane(out.plane[1], src->data[1], src->linesize[1], cw * 2, ch);
               return true;
            case AV_PIX_FMT_YUVJ420P:
               out.fullRange = true;
               [[fallthrough]];
            case AV_PIX_FMT_YUV420P:
               out.format = VideoPixelFormat::YUV420P;
               CopyPlane(out.plane[0], src->data[0], src->linesize[0], w, h);
               CopyPlane(out.plane[1], src->data[1], src->linesize[1], cw, ch);
               CopyPlane(out.plane[2], src->data[2], src->linesize[2], cw, ch);
               return true;
            case AV_PIX_FMT_RGB0:
               out.opaque = true;
               [[fallthrough]];
            case AV_PIX_FMT_RGBA:
               out.format = VideoPixelFormat::RGBA8;
               CopyPlane(out.plane[0], src->data[0], src->linesize[0], w * 4, h);
               return true;
            case AV_PIX_FMT_BGR0:
               out.opaque = true;
               [[fallthrough]];
            case AV_PIX_FMT_BGRA:
               out.format = VideoPixelFormat::BGRA8;
               CopyPlane(out.plane[0], src->data[0], src->linesize[0], w * 4, h);
               return true;
            default:
            {
               // 10-bit, 4:2:2, 4:4:4, alpha YUV and the rest: one swscale pass to BGRA.
               v->sws = sws_getCachedContext(v->sws, w, h, (AVPixelFormat)src->format, w, h,
                                             AV_PIX_FMT_BGRA, SWS_POINT, nullptr, nullptr, nullptr);
               if (v->sws == nullptr)
                  return false;
               out.format = VideoPixelFormat::BGRA8;
               out.plane[0].resize((size_t)w * (size_t)h * 4);
               uint8_t* dstData[4] = { out.plane[0].data(), nullptr, nullptr, nullptr };
               int dstLines[4] = { w * 4, 0, 0, 0 };
               sws_scale(v->sws, src->data, src->linesize, 0, h, dstData, dstLines);
               return true;
            }
         }
      }

      // Decoder thread only. Positions on `target` as cheaply as possible and
      // converts that picture into `out`.
      bool VideoDecodeInto(VideoHandle* v, long long target, VideoFrame& out)
      {
         const long long gap = target - (v->lastDecodedFrame + 1);
         if (!(gap >= 0 && gap <= kVideoDecodeThroughInsteadOfSeek))
            Seek(v, target);

         for (int guard = 0; guard < 100000; ++guard)
         {
            long long idx = 0;
            if (v->hap)
            {
               if (!ReadPacket(v))
                  break;
               idx = IndexFromTs(v, v->pkt->pts != AV_NOPTS_VALUE ? v->pkt->pts : v->pkt->dts);
            }
            else
            {
               if (ReceiveFrame(v) <= 0)
                  break;
               idx = IndexFromTs(v, v->frame->best_effort_timestamp);
            }
            v->lastDecodedFrame = idx;
            if (idx < target)
               continue; // decode through (HAP: skip without unpacking)
            const bool ok = v->hap ? ConvertHap(v, out) : ConvertFrame(v, out);
            if (!v->hap)
               av_frame_unref(v->frame);
            if (ok)
            {
               v->width.store(out.width, std::memory_order_relaxed);
               v->height.store(out.height, std::memory_order_relaxed);
            }
            return ok;
         }
         v->lastDecodedFrame = -1000000; // force a real seek next time
         return false;
      }

      size_t FrameBytes(const VideoFrame& f)
      {
         return f.plane[0].capacity() + f.plane[1].capacity() + f.plane[2].capacity();
      }

      // Called with frameMutex held. Next frame worth decoding speculatively,
      // or -1 when the cache is full / playing backwards / at the end.
      long long VideoPrefetchTarget(const VideoHandle* v)
      {
         if (v->direction <= 0 || v->cache.size() >= v->cacheCapacity)
            return -1;
         long long base = v->lastQueuedFrame;
         if (!v->cache.empty())
            base = std::max(base, v->cache.back().index);
         const long long next = base + 1;
         if (next < 0 || (v->frameCount > 0 && next >= v->frameCount))
            return -1;
         return next;
      }

      // Called with frameMutex held.
      void VideoRecycle(VideoHandle* v, VideoFrame&& f)
      {
         if (v->freeBuffers.size() < 4 && FrameBytes(f) > 0)
            v->freeBuffers.push_back(std::move(f));
      }

      // Called with frameMutex held.
      VideoFrame VideoTakeBuffer(VideoHandle* v)
      {
         if (v->freeBuffers.empty())
            return {};
         VideoFrame f = std::move(v->freeBuffers.back());
         v->freeBuffers.pop_back();
         return f;
      }

      void Publish(VideoHandle* v, VideoFrame&& f, uint64_t serial)
      {
         VideoRecycle(v, std::move(v->published));
         v->published = std::move(f);
         v->publishedSerial = serial;
      }

      void VideoDecodeWorker(VideoHandle* v)
      {
         for (;;)
         {
            long long target = -1;
            uint64_t serial = 0;
            bool prefetch = false;
            int direction = 1;
            VideoFrame scratch;
            {
               std::unique_lock<std::mutex> lock(v->frameMutex);
               v->frameReady.wait(lock, [v]()
               {
                  return v->stopping || v->requestPending || VideoPrefetchTarget(v) >= 0;
               });
               if (v->stopping)
                  return;

               direction = v->direction;
               if (v->requestPending)
               {
                  target = v->requestedFrame;
                  serial = v->requestSerial;
                  v->requestPending = false;

                  // Serve from the cache when read-ahead already has it and
                  // drop entries the playhead has moved past.
                  bool served = false;
                  for (auto it = v->cache.begin(); it != v->cache.end();)
                  {
                     if (it->index == target && !served)
                     {
                        Publish(v, std::move(it->frame), serial);
                        served = true;
                        it = v->cache.erase(it);
                     }
                     else if ((direction > 0 && it->index < target) ||
                              (direction < 0 && it->index > target))
                     {
                        VideoRecycle(v, std::move(it->frame));
                        it = v->cache.erase(it);
                     }
                     else
                        ++it;
                  }
                  if (served)
                     continue;
               }
               else
               {
                  target = VideoPrefetchTarget(v);
                  prefetch = true;
               }
               scratch = VideoTakeBuffer(v);
            }

            // Reverse playback: decode [target - N + 1, target] in one forward
            // run, keep the lower frames for the next requests.
            if (!prefetch && direction < 0)
            {
               const long long batch = (long long)std::max<size_t>(1, v->cacheCapacity);
               const long long start = std::max(0LL, target - batch + 1);
               std::vector<VideoHandle::CachedFrame> decoded;
               for (long long f = start; f <= target; ++f)
               {
                  VideoHandle::CachedFrame cf;
                  cf.index = f;
                  if (f == target)
                     cf.frame = std::move(scratch);
                  else
                  {
                     std::lock_guard<std::mutex> lock(v->frameMutex);
                     cf.frame = VideoTakeBuffer(v);
                  }
                  if (VideoDecodeInto(v, f, cf.frame))
                     decoded.push_back(std::move(cf));
               }
               std::lock_guard<std::mutex> lock(v->frameMutex);
               for (auto& cf : decoded)
               {
                  if (cf.index == target)
                     Publish(v, std::move(cf.frame), serial);
                  else if (v->cache.size() < v->cacheCapacity)
                     v->cache.push_back(std::move(cf));
                  else
                     VideoRecycle(v, std::move(cf.frame));
               }
               continue;
            }

            const bool ok = VideoDecodeInto(v, target, scratch);
            std::lock_guard<std::mutex> lock(v->frameMutex);
            if (!ok)
            {
               // A failed read-ahead means the container over-reported its
               // length: stop speculating past this point.
               if (prefetch)
                  v->frameCount = target;
               VideoRecycle(v, std::move(scratch));
               continue;
            }
            if (prefetch)
            {
               VideoHandle::CachedFrame cf;
               cf.index = target;
               cf.frame = std::move(scratch);
               v->cache.push_back(std::move(cf));
            }
            else
               Publish(v, std::move(scratch), serial);
         }
      }

      size_t EstimatedFrameBytes(const VideoHandle* v, int w, int h)
      {
         const size_t px = (size_t)std::max(1, w) * (size_t)std::max(1, h);
         if (v->hap)
            return px; // BC1 0.5 B/px, BC3/BC7 1 B/px
         return v->hwPixFmt != AV_PIX_FMT_NONE ? px * 3 / 2 : px * 4;
      }
   }

   VideoHandle* VideoOpen(const std::string& path, std::string& error)
   {
      auto v = std::make_unique<VideoHandle>();
      int r = avformat_open_input(&v->fmt, path.c_str(), nullptr, nullptr); // UTF-8 on Windows
      if (r < 0)
      {
         error = "could not open video: " + AvError(r);
         return nullptr;
      }
      if (avformat_find_stream_info(v->fmt, nullptr) < 0)
      {
         error = "could not read video stream info";
         return nullptr;
      }
      v->stream = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
      if (v->stream < 0)
      {
         error = "no video stream";
         return nullptr;
      }
      for (unsigned i = 0; i < v->fmt->nb_streams; ++i)
         if ((int)i != v->stream)
            v->fmt->streams[i]->discard = AVDISCARD_ALL; // audio is decoded elsewhere

      AVStream* st = v->fmt->streams[v->stream];
      v->timeBase = st->time_base;
      v->startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
      v->codedW = st->codecpar->width;
      v->codedH = st->codecpar->height;
      double fps = av_q2d(st->avg_frame_rate);
      if (!(fps > 0.0 && fps < 1000.0))
         fps = av_q2d(st->r_frame_rate);
      v->fps = (fps > 0.0 && fps < 1000.0) ? fps : 30.0;
      double seconds = 0.0;
      if (st->duration != AV_NOPTS_VALUE)
         seconds = (double)st->duration * av_q2d(st->time_base);
      else if (v->fmt->duration != AV_NOPTS_VALUE)
         seconds = (double)v->fmt->duration / (double)AV_TIME_BASE;
      v->frameCount = st->nb_frames > 0 ? (long long)st->nb_frames : (long long)std::llround(seconds * v->fps);
      v->duration = v->frameCount > 0 ? (double)v->frameCount / v->fps : seconds;

      v->pkt = av_packet_alloc();
      v->frame = av_frame_alloc();
      v->swFrame = av_frame_alloc();

      // HAP goes straight to GPU blocks when the first frame unpacks;
      // anything odd falls back to libavcodec's own HAP decoder.
      if (st->codecpar->codec_id == AV_CODEC_ID_HAP && v->codedW > 0 && v->codedH > 0)
      {
         v->hap = true;
         VideoFrame probe;
         if (!ReadPacket(v.get()) || !ConvertHap(v.get(), probe))
            v->hap = false;
         Seek(v.get(), 0);
         v->lastDecodedFrame = -1000000;
         if (v->hap)
         {
            const char* tag = probe.format == VideoPixelFormat::BC1 ? "HAP"
                            : probe.format == VideoPixelFormat::BC3 ? "HAP Alpha"
                            : probe.format == VideoPixelFormat::BC7 ? "HAP R"
                            : probe.format == VideoPixelFormat::BC3YCoCgAlpha ? "HAP Q Alpha" : "HAP Q";
            v->description = std::string(tag) + " (GPU native)";
         }
      }
      if (!v->hap && !OpenDecoder(v.get(), error))
         return nullptr;

      v->width.store(v->codedW, std::memory_order_relaxed);
      v->height.store(v->codedH, std::memory_order_relaxed);
      v->cacheCapacity = std::clamp<size_t>(kVideoCacheBudgetBytes / EstimatedFrameBytes(v.get(), v->codedW, v->codedH), 2, 8);
      RuntimeLog::Write("video opened: %s %dx%d %.2f fps, %lld frames, read-ahead=%d",
                        v->description.c_str(), v->codedW, v->codedH, v->fps, v->frameCount,
                        (int)v->cacheCapacity);
      VideoHandle* raw = v.get();
      v->decoder = std::thread(VideoDecodeWorker, raw);
      return v.release();
   }

   void VideoClose(VideoHandle* h) { delete h; }
   int VideoWidth(VideoHandle* h) { return h ? h->width.load(std::memory_order_relaxed) : 0; }
   int VideoHeight(VideoHandle* h) { return h ? h->height.load(std::memory_order_relaxed) : 0; }
   double VideoDuration(VideoHandle* h) { return h ? h->duration : 0.0; }

   std::string VideoDescribe(VideoHandle* h)
   {
      if (h == nullptr)
         return {};
      char buf[160];
      std::snprintf(buf, sizeof(buf), "%s %dx%d %.3g fps", h->description.c_str(),
                    h->codedW, h->codedH, h->fps);
      return buf;
   }

   bool VideoFrameAt(VideoHandle* h, double seconds, VideoFrame& out)
   {
      if (!h) return false;
      const long long targetFrame = std::max(0LL, (long long)std::floor(std::max(0.0, seconds) * h->fps));
      bool produced = false;
      {
         std::lock_guard<std::mutex> lock(h->frameMutex);
         if (targetFrame != h->lastQueuedFrame)
         {
            if (h->lastQueuedFrame >= 0)
               h->direction = targetFrame < h->lastQueuedFrame ? -1 : 1;
            h->requestedFrame = targetFrame;
            h->lastQueuedFrame = targetFrame;
            ++h->requestSerial;
            h->requestPending = true;
         }
         if (h->publishedSerial != h->consumedSerial && !h->published.empty())
         {
            // Swap instead of copy: the caller's previous frame goes back to
            // the decoder as the next scratch buffer.
            std::swap(out, h->published);
            h->consumedSerial = h->publishedSerial;
            produced = true;
         }
      }
      h->frameReady.notify_one();
      return produced;
   }
}

// Windows implementation of the Platform facade's video surface, replacing
// AVFoundation:
//
//   - Video decode: IMFSourceReader in synchronous mode, output forced to
//     32bpp RGB and swizzled to RGBA8 row-flipped for GL, exactly what the
//     AVAssetReader path delivered. Seeking backwards repositions the reader
//     and forward-decodes from the nearest keyframe - which is how Video
//     nodes loop.
//   - Camera: MFEnumDeviceSources for enumeration; each open runs a dedicated
//     capture thread that continuously pulls frames into a latest-frame slot
//     guarded by a mutex (the UI drains at its own pace via CameraReadFrame).
//     Resolution changes are applied by the thread between frames so the
//     reader is never touched concurrently.
//   - Recorder: IMFSinkWriter into MP4 with H.264 video. The app hands us
//     glReadPixels-ordered RGBA8 (bottom-up); we flip and swizzle to the
//     top-down BGRA the encoder's converter wants. Audio muxing covers both
//     modes OutputNode uses: a decoded file track written at Stop (looped or
//     truncated to the video duration), and live PCM streamed in through
//     RecorderAppendAudio during the take.
//   - InspectMovie: a throwaway source reader over the finished file.
//
// Every entry point wraps its work in an MfScope: Media Foundation itself
// reference-counts MFStartup/MFShutdown pairs, so nesting scopes across the
// video/camera/recorder surfaces is safe.

#include "../Platform.h"

#include "WinCommon.h"

#include <codecapi.h> // eAVEncH264VProfile_High
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace
{
   // Nested MFStartup calls are refcounted inside MF; this just keeps every
   // entry/exit pair matched on all paths.
   struct MfScope
   {
      bool ok = false;
      MfScope()
      {
         ok = SUCCEEDED(MFStartup(MF_VERSION));
      }
      ~MfScope()
      {
         if (ok)
            MFShutdown();
      }
   };

   // COM init for the calling thread; matches AudioDeviceWin's pattern.
   struct ComScope
   {
      bool ok = false;
      ComScope()
      {
         ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
      }
      ~ComScope()
      {
         if (ok)
            CoUninitialize();
      }
   };

   template <typename T>
   void SafeRelease(T** p)
   {
      if (*p != nullptr)
      {
         (*p)->Release();
         *p = nullptr;
      }
   }

   // Reads an MF_MT_FRAME_SIZE UINT64 attribute.
   void UnpackFrameSize(UINT64 packed, UINT32& outWidth, UINT32& outHeight)
   {
      outWidth = (UINT32)(packed >> 32);
      outHeight = (UINT32)(packed & 0xFFFFFFFF);
   }

   bool MajorTypeIs(IMFMediaType* type, const GUID& category)
   {
      GUID major {};
      return type != nullptr && SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) &&
             major == category;
   }

   // Copies one decoded RGB32 buffer (top-down rows, byte order B,G,R,X) into
   // an RGBA8 GL texture buffer (bottom-up). `mirrorX` flips columns too.
   void Rgb32ToRgbaFlipped(const unsigned char* src, LONG pitch, int width, int height,
                           bool mirrorX, std::vector<unsigned char>& out)
   {
      out.resize((size_t)width * height * 4);
      const size_t rowBytes = (size_t)width * 4;
      for (int y = 0; y < height; y++)
      {
         const unsigned char* srcRow = src + (size_t)y * pitch;
         unsigned char* dstRow = out.data() + (size_t)(height - 1 - y) * rowBytes;
         for (int x = 0; x < width; x++)
         {
            const unsigned char* s = srcRow + (size_t)x * 4;
            unsigned char* d = dstRow + (size_t)x * 4;
            if (mirrorX)
               d = dstRow + (size_t)(width - 1 - x) * 4;
            d[0] = s[2]; // R
            d[1] = s[1]; // G
            d[2] = s[0]; // B
            d[3] = 255;
         }
      }
   }

   // Copies caller-supplied bottom-up pixels into a top-down BGRA8 staging
   // row layout for the sink writer's input type. `isBgra` selects between
   // the native path (the caller's glReadPixels already came back BGRA8, so
   // this is a pure row-reverse copy) and the fallback where it's RGBA8 and
   // still needs a per-pixel channel swizzle alongside the flip.
   void BgraBottomUpToTopDown(const unsigned char* src, int width, int height,
                              bool isBgra, std::vector<unsigned char>& out)
   {
      out.resize((size_t)width * height * 4);
      const size_t rowBytes = (size_t)width * 4;
      if (isBgra)
      {
         for (int y = 0; y < height; y++)
         {
            const unsigned char* srcRow = src + (size_t)(height - 1 - y) * rowBytes;
            memcpy(out.data() + (size_t)y * rowBytes, srcRow, rowBytes);
         }
         return;
      }
      for (int y = 0; y < height; y++)
      {
         const unsigned char* srcRow = src + (size_t)(height - 1 - y) * rowBytes;
         unsigned char* dstRow = out.data() + (size_t)y * rowBytes;
         for (int x = 0; x < width; x++)
         {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
            dstRow[x * 4 + 3] = 255;
         }
      }
   }

   // Wraps a contiguous system-memory block as an IMFSample for WriteSample.
   IMFMediaBuffer* BufferFromMemory(BYTE* data, DWORD size)
   {
      IMFMediaBuffer* buffer = nullptr;
      if (FAILED(MFCreateMemoryBuffer(size, &buffer)))
         return nullptr;
      BYTE* dst = nullptr;
      if (FAILED(buffer->Lock(&dst, nullptr, nullptr)))
      {
         buffer->Release();
         return nullptr;
      }
      memcpy(dst, data, size);
      buffer->Unlock();
      if (FAILED(buffer->SetCurrentLength(size)))
      {
         buffer->Release();
         return nullptr;
      }
      return buffer;
   }

   LONGLONG FrameNumberToHns(long long frame, double fps)
   {
      return (LONGLONG)((double)frame * 10000000.0 / fps);
   }

   // Attributes for source reader creation that turn on the Video Processor
   // MFT (MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING). Without this,
   // a reader can only hand out whatever subtype the raw decoder produces
   // (e.g. NV12 for H.264); a later SetCurrentMediaType asking for RGB32
   // fails with MF_E_INVALIDMEDIATYPE since there's no MFT to do the
   // conversion. Returns nullptr on failure - callers fall back to no
   // attributes rather than failing the open outright.
   IMFAttributes* CreateAdvancedVideoProcessingAttributes()
   {
      IMFAttributes* attrs = nullptr;
      if (FAILED(MFCreateAttributes(&attrs, 1)))
         return nullptr;
      if (FAILED(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)))
      {
         SafeRelease(&attrs);
         return nullptr;
      }
      return attrs;
   }

   // ---- video decode ---------------------------------------------------------

   struct VideoHandleMf
   {
      IMFSourceReader* reader = nullptr;
      int width = 0;
      int height = 0;
      LONG stride = 0; // bytes per row of the negotiated RGB32 type; negative means bottom-up
      double durationSeconds = 0.0;
      long long lastDeliveredFrameHns = -1; // timestamp of the frame in outPixels
      std::vector<unsigned char> currentPixels;
      bool haveCurrent = false;

      // Holds one MFStartup reference for the handle's lifetime: a per-call
      // scope shutting the platform down while this reader still exists would
      // pull the ground out from under it.
      VideoHandleMf() { mfActive = SUCCEEDED(MFStartup(MF_VERSION)); }
      ~VideoHandleMf()
      {
         if (reader != nullptr)
         {
            reader->Flush(0);
            SafeRelease(&reader);
         }
         if (mfActive)
            MFShutdown();
      }
      bool mfActive = false;
   };

   bool ConfigureVideoOutputType(VideoHandleMf* video, std::string& outError)
   {
      IMFMediaType* type = nullptr;
      HRESULT hr = MFCreateMediaType(&type);
      if (SUCCEEDED(hr))
         hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      if (SUCCEEDED(hr))
         hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
      if (SUCCEEDED(hr))
         hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
      if (SUCCEEDED(hr))
         hr = type->SetUINT32(MF_MT_INTERLACE_MODE, (UINT32)MFVideoInterlace_Progressive);
      if (SUCCEEDED(hr))
         hr = video->reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
      SafeRelease(&type);

      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("selecting decoded video format", hr);
         return false;
      }

      // Read back what the reader settled on for frame size.
      IMFMediaType* actual = nullptr;
      hr = video->reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual);
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("reading negotiated video format", hr);
         return false;
      }
      UINT64 size64 = 0;
      actual->GetUINT64(MF_MT_FRAME_SIZE, &size64);
      UnpackFrameSize(size64, (UINT32&)video->width, (UINT32&)video->height);

      // Some paths through the Video Processor MFT report a negative
      // (bottom-up) default stride rather than a tightly-packed positive
      // one - never assume width * 4.
      UINT32 strideU32 = 0;
      if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideU32)))
         video->stride = (LONG)strideU32;
      else
         video->stride = (LONG)video->width * 4;
      SafeRelease(&actual);
      return true;
   }

   // Pulls the next sample through the reader. Returns:
   //   1 -> frame delivered into outPixels/outTimeHns
   //   0 -> clean end of stream
   //  -1 -> error, outError filled
   int ReadNextVideoFrame(VideoHandleMf* video, std::vector<unsigned char>& outPixels,
                          LONGLONG& outTimeHns, std::string& outError)
   {
      IMFSample* sample = nullptr;
      DWORD flags = 0;
      HRESULT hr = video->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                                             &flags, nullptr, &sample);
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("video decode", hr);
         return -1;
      }
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM || sample == nullptr)
      {
         SafeRelease(&sample);
         return 0;
      }

      LONGLONG timeHns = 0;
      sample->GetSampleTime(&timeHns);

      IMFMediaBuffer* buffer = nullptr;
      hr = sample->ConvertToContiguousBuffer(&buffer);
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("locking video frame", hr);
         SafeRelease(&sample);
         return -1;
      }

      BYTE* data = nullptr;
      hr = buffer->Lock(&data, nullptr, nullptr);
      if (SUCCEEDED(hr))
      {
         LONG stride = video->stride;
         const unsigned char* rowData = data;
         if (stride < 0)
         {
            // Bottom-up: the pointer already refers to the image's top-left
            // corner in display order once we walk backwards from the last
            // row, so shift to that row and flip the stride sign to walk
            // forward from there.
            rowData = data + stride * (video->height - 1);
            stride = -stride;
         }
         Rgb32ToRgbaFlipped(rowData, stride, video->width, video->height, false, outPixels);
         buffer->Unlock();
      }
      SafeRelease(&buffer);
      SafeRelease(&sample);
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("locking video frame", hr);
         return -1;
      }

      outTimeHns = timeHns;
      return 1;
   }
}

namespace Platform
{
   VideoHandle* VideoOpen(const std::string& path, std::string& outError)
   {
      outError.clear();
      MfScope mf;
      ComScope com;
      if (!mf.ok || !com.ok)
      {
         outError = "could not initialize Media Foundation";
         return nullptr;
      }

      auto* video = new VideoHandleMf();
      IMFAttributes* readerAttrs = CreateAdvancedVideoProcessingAttributes();
      HRESULT hr = MFCreateSourceReaderFromURL(WinCommon::Utf8ToWide(path).c_str(), readerAttrs,
                                               &video->reader);
      SafeRelease(&readerAttrs);
      if (FAILED(hr))
      {
         delete video;
         outError = WinCommon::HrToString("opening video", hr);
         return nullptr;
      }

      PROPVARIANT position {};
      position.vt = VT_I8;
      position.hVal.QuadPart = 0;
      video->reader->SetCurrentPosition(GUID_NULL, position);

      if (!ConfigureVideoOutputType(video, outError))
      {
         delete video;
         return nullptr;
      }

      PROPVARIANT duration {};
      duration.vt = VT_EMPTY;
      if (SUCCEEDED(video->reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                            MF_PD_DURATION, &duration)) &&
          duration.vt == VT_I8)
         video->durationSeconds = (double)duration.hVal.QuadPart / 10000000.0;
      PropVariantClear(&duration);

      if (video->width <= 0 || video->height <= 0)
      {
         outError = "video has no decodable picture";
         delete video;
         return nullptr;
      }
      return reinterpret_cast<VideoHandle*>(video);
   }

   void VideoClose(VideoHandle* handle)
   {
      delete reinterpret_cast<VideoHandleMf*>(handle);
   }

   int VideoWidth(VideoHandle* handle)
   {
      auto* video = reinterpret_cast<VideoHandleMf*>(handle);
      return video != nullptr ? video->width : 0;
   }

   int VideoHeight(VideoHandle* handle)
   {
      auto* video = reinterpret_cast<VideoHandleMf*>(handle);
      return video != nullptr ? video->height : 0;
   }

   double VideoDuration(VideoHandle* handle)
   {
      auto* video = reinterpret_cast<VideoHandleMf*>(handle);
      return video != nullptr ? video->durationSeconds : 0.0;
   }

   bool VideoFrameAt(VideoHandle* handle, double seconds, std::vector<unsigned char>& outPixels)
   {
      auto* video = reinterpret_cast<VideoHandleMf*>(handle);
      if (video == nullptr || video->reader == nullptr || seconds < 0.0)
         return false;

      MfScope mf;
      ComScope com;
      if (!mf.ok || !com.ok)
         return false;

      // One-millisecond slop so float round-trip through the node's param
      // doesn't force a spurious seek when the same timestamp comes back.
      const LONGLONG targetHns = (LONGLONG)(seconds * 10000000.0);
      constexpr LONGLONG kEpsilonHns = 10000;

      if (targetHns < video->lastDeliveredFrameHns - kEpsilonHns)
      {
         // Backwards: restart the reader at the requested position and
         // forward-decode to it. This is the loop path.
         PROPVARIANT position {};
         position.vt = VT_I8;
         position.hVal.QuadPart = targetHns;
         if (FAILED(video->reader->SetCurrentPosition(GUID_NULL, position)))
            return false;
         video->lastDeliveredFrameHns = -1;
         video->haveCurrent = false;
      }

      bool produced = false;
      std::string error;
      for (;;)
      {
         if (video->haveCurrent && video->lastDeliveredFrameHns >= targetHns - kEpsilonHns)
            break; // the frame covering `seconds` is current

         LONGLONG timeHns = 0;
         std::vector<unsigned char> frame;
         const int got = ReadNextVideoFrame(video, frame, timeHns, error);
         if (got < 0)
         {
            // Decode error mid-stream: keep showing whatever we had.
            break;
         }
         if (got == 0)
         {
            if (!produced && !video->haveCurrent)
               return false; // nothing at all deliverable
            break;
         }

         // Guard against non-monotonic timestamps from sloppy containers:
         // never let the delivered position move backwards except via seek.
         if (timeHns >= video->lastDeliveredFrameHns)
         {
            video->currentPixels.swap(frame);
            video->lastDeliveredFrameHns = timeHns;
            video->haveCurrent = true;
            produced = true;
         }
      }

      if (produced)
         outPixels = video->currentPixels;
      return produced;
   }

   // Decodes a video container's audio track in full, up front, into a
   // planar-float SampleBuffer - the same shape DecodeAudioFileToBuffer
   // (MediaDecodeWin.cpp) produces for a plain audio file, so downstream
   // (SampleSlotT, the sampler playback path) needs no second buffer type.
   // Mirrors VideoOpen's IMFSourceReader-over-URL pattern above, just
   // selecting the audio stream and MFAudioFormat_Float instead of the video
   // stream and RGB32. Main-thread only, matching the macOS AVAssetReader
   // path - not real-time safe.
   bool DecodeVideoAudioTrackToBuffer(const std::string& path, SampleBuffer& outBuffer, std::string& outError)
   {
      outError.clear();
      MfScope mf;
      ComScope com;
      if (!mf.ok || !com.ok)
      {
         outError = "could not initialize Media Foundation";
         return false;
      }

      IMFSourceReader* reader = nullptr;
      HRESULT hr = MFCreateSourceReaderFromURL(WinCommon::Utf8ToWide(path).c_str(), nullptr, &reader);
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("opening file", hr);
         return false;
      }

      // Selecting the audio stream fails when the container has none - that
      // is a normal case for a lot of VJ footage, not an error. Callers check
      // for exactly this string to tell the two apart.
      if (FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)))
      {
         SafeRelease(&reader);
         outError = "no audio track in this file";
         return false;
      }

      IMFMediaType* type = nullptr;
      hr = MFCreateMediaType(&type);
      if (SUCCEEDED(hr))
         hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
      if (SUCCEEDED(hr))
         hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
      if (SUCCEEDED(hr))
         hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type);
      SafeRelease(&type);
      if (FAILED(hr))
      {
         SafeRelease(&reader);
         outError = "no audio track in this file";
         return false;
      }

      IMFMediaType* actual = nullptr;
      hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
      if (FAILED(hr))
      {
         SafeRelease(&reader);
         outError = WinCommon::HrToString("reading negotiated audio format", hr);
         return false;
      }
      UINT32 channels = 0, sampleRate = 0;
      actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
      actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
      SafeRelease(&actual);
      if (channels == 0 || sampleRate == 0)
      {
         SafeRelease(&reader);
         outError = "unreadable audio format";
         return false;
      }

      // Read the whole track, interleaved (Media Foundation's float PCM
      // decoders always deliver interleaved), then deinterleave into
      // SampleBuffer's planar layout below.
      std::vector<float> interleaved;
      for (;;)
      {
         DWORD flags = 0;
         IMFSample* sample = nullptr;
         hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
         if (FAILED(hr))
         {
            SafeRelease(&reader);
            outError = WinCommon::HrToString("audio decode", hr);
            return false;
         }
         if (flags & MF_SOURCE_READERF_ENDOFSTREAM || sample == nullptr)
         {
            SafeRelease(&sample);
            break;
         }

         IMFMediaBuffer* buffer = nullptr;
         hr = sample->ConvertToContiguousBuffer(&buffer);
         if (SUCCEEDED(hr))
         {
            BYTE* data = nullptr;
            DWORD dataLen = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &dataLen)))
            {
               const float* samples = reinterpret_cast<const float*>(data);
               const size_t count = dataLen / sizeof(float);
               interleaved.insert(interleaved.end(), samples, samples + count);
               buffer->Unlock();
            }
            SafeRelease(&buffer);
         }
         SafeRelease(&sample);
      }
      SafeRelease(&reader);

      if (interleaved.empty())
      {
         outError = "failed to decode audio track";
         return false;
      }

      const int frames = (int)(interleaved.size() / channels);
      outBuffer.channels = (int)channels;
      outBuffer.numFrames = frames;
      outBuffer.sampleRate = (double)sampleRate;
      outBuffer.channelData.assign((size_t)channels * (size_t)frames, 0.0f);
      for (int i = 0; i < frames; i++)
         for (UINT32 ch = 0; ch < channels; ch++)
            outBuffer.channelData[(size_t)ch * (size_t)frames + i] = interleaved[(size_t)i * channels + ch];

      outError.clear();
      return true;
   }

   // ---- camera ---------------------------------------------------------------

   namespace
   {
      int TargetHeightForResolution(CameraResolution res)
      {
         switch (res)
         {
            case CameraResolution::Res1080p: return 1080;
            case CameraResolution::Res720p: return 720;
            case CameraResolution::Res480p: return 480;
            default: return 0; // Auto: take the best the sensor offers
         }
      }

      // Scores one native media type against what was asked for. Lower wins.
      // Auto prefers the largest frame; explicit picks prefer the closest
      // height, breaking ties toward uncompressed formats (cheaper conversion).
      long ScoreNativeType(IMFMediaType* type, int targetHeight)
      {
         UINT64 size64 = 0;
         if (FAILED(type->GetUINT64(MF_MT_FRAME_SIZE, &size64)))
            return LONG_MAX;
         UINT32 w = 0, h = 0;
         UnpackFrameSize(size64, w, h);
         if (w == 0 || h == 0 || h > 2160)
            return LONG_MAX;

         GUID subtype {};
         type->GetGUID(MF_MT_SUBTYPE, &subtype);
         const bool uncompressed =
            subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_RGB24 ||
            subtype == MFVideoFormat_ARGB32 || subtype == MFVideoFormat_NV12 ||
            subtype == MFVideoFormat_YUY2;

         long score;
         if (targetHeight <= 0)
            score = (long)(2160 - h); // Auto: bigger is better
         else
            score = (long)std::abs((int)h - targetHeight);
         return uncompressed ? score : score + 1000;
      }

      // Picks the best native type and asks the reader for it converted to
      // RGB32. Returns false with outError if nothing usable exists.
      bool ConfigureCameraFormat(IMFSourceReader* reader, CameraResolution res,
                                 UINT32& outWidth, UINT32& outHeight, LONG& outStride,
                                 std::string& outError)
      {
         const int targetHeight = TargetHeightForResolution(res);
         long bestScore = LONG_MAX;
         DWORD bestIndex = 0;
         bool haveBest = false;

         for (DWORD i = 0;; i++)
         {
            IMFMediaType* native = nullptr;
            const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                          i, &native);
            if (hr == MF_E_NO_MORE_TYPES)
               break;
            if (FAILED(hr))
               break;
            const long score = MajorTypeIs(native, MFMediaType_Video)
                                  ? ScoreNativeType(native, targetHeight)
                                  : LONG_MAX;
            if (score < bestScore)
            {
               bestScore = score;
               bestIndex = i;
               haveBest = true;
            }
            SafeRelease(&native);
         }
         if (!haveBest)
         {
            outError = "camera offers no usable video format";
            return false;
         }

         IMFMediaType* native = nullptr;
         HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                 bestIndex, &native);
         if (SUCCEEDED(hr))
         {
            // Same frame geometry the sensor produces; only ask for the pixel
            // format we can swizzle. The reader inserts a color converter -
            // but never a scaler - which is why the size comes from native
            // types rather than being requested outright.
            hr = native->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            if (SUCCEEDED(hr))
               hr = native->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            if (SUCCEEDED(hr))
               hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                nullptr, native);
         }
         SafeRelease(&native);
         if (FAILED(hr))
         {
            outError = WinCommon::HrToString("configuring camera format", hr);
            return false;
         }

         IMFMediaType* actual = nullptr;
         UINT64 size64 = 0;
         UINT32 strideU32 = 0;
         bool haveStride = false;
         if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                   &actual)))
         {
            actual->GetUINT64(MF_MT_FRAME_SIZE, &size64);
            // Same rule as the file-decode path above: MF's video processor and
            // hardware decoders routinely pad each row out to an alignment
            // boundary, so the real stride is whatever MF_MT_DEFAULT_STRIDE
            // says, not width * 4. Assuming the packed value read every row
            // from a slightly wrong offset, with the error accumulating down
            // the image - the classic progressively-sheared frame. Widths that
            // happen to be well aligned (1920, 1280) looked fine, which is
            // exactly why this survived casual testing; 854x480 did not.
            haveStride = SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideU32));
            SafeRelease(&actual);
         }
         UnpackFrameSize(size64, outWidth, outHeight);
         outStride = haveStride ? (LONG)strideU32 : (LONG)outWidth * 4;
         return true;
      }

      struct CameraHandleMf
      {
         std::wstring symbolicLink;
         CameraResolution resolution = CameraResolution::Auto;

         std::thread thread;
         std::mutex frameMutex; // guards pixels/w/h/seq and lastReadSeq below
         std::vector<unsigned char> pixels;
         int width = 0;
         int height = 0;
         unsigned long long seq = 0;
         unsigned long long lastReadSeq = 0;

         std::atomic<bool> mirrorX{ false };
         std::atomic<int> pendingResolution{ -1 }; // CameraResolution value or -1
         std::atomic<bool> stop{ false };
         std::atomic<bool> running{ false };
         // Written by the capture thread, read by CameraOpen after threadDone's
         // release store - the release/acquire chain makes the plain string safe.
         std::string error;
         std::atomic<bool> threadDone{ false };

         ~CameraHandleMf()
         {
            stop.store(true, std::memory_order_release);
            if (thread.joinable())
               thread.join();
         }
      };

      void CameraThreadMain(CameraHandleMf* cam)
      {
         // Every exit path through this function publishes threadDone last;
         // CameraOpen waits on it before touching cam->error.
         struct DoneFlag
         {
            CameraHandleMf* cam;
            ~DoneFlag()
            {
               cam->threadDone.store(true, std::memory_order_release);
            }
         } done{ cam };

         ComScope com;
         MfScope mf;
         IMFMediaSource* source = nullptr;
         IMFSourceReader* reader = nullptr;

         auto fail = [&](const char* what, HRESULT hr) {
            cam->error = WinCommon::HrToString(what, hr);
            cam->running.store(false, std::memory_order_release);
         };

         if (!com.ok || !mf.ok)
         {
            cam->error = "could not initialize Media Foundation";
            cam->running.store(false, std::memory_order_release);
            return;
         }

         IMFAttributes* attrs = nullptr;
         HRESULT hr = MFCreateAttributes(&attrs, 2);
         if (SUCCEEDED(hr))
            hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
         if (SUCCEEDED(hr))
            hr = attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                  cam->symbolicLink.c_str());
         if (SUCCEEDED(hr))
            hr = MFCreateDeviceSource(attrs, &source);
         SafeRelease(&attrs);
         if (FAILED(hr))
         {
            fail("opening camera", hr);
            return;
         }

         IMFAttributes* readerAttrs = CreateAdvancedVideoProcessingAttributes();
         hr = MFCreateSourceReaderFromMediaSource(source, readerAttrs, &reader);
         SafeRelease(&readerAttrs);
         if (FAILED(hr))
         {
            fail("wrapping camera in a source reader", hr);
            source->Shutdown();
            SafeRelease(&source);
            return;
         }

         UINT32 w = 0, h = 0;
         LONG stride = 0;
         CameraResolution applied = cam->resolution;
         if (!ConfigureCameraFormat(reader, cam->resolution, w, h, stride, cam->error))
         {
            cam->running.store(false, std::memory_order_release);
            SafeRelease(&reader);
            source->Shutdown();
            SafeRelease(&source);
            return;
         }

         cam->running.store(true, std::memory_order_release);

         while (!cam->stop.load(std::memory_order_acquire))
         {
            // Apply a queued resolution change between frames, where nobody
            // else can be touching the reader.
            const int wanted = cam->pendingResolution.load(std::memory_order_acquire);
            if (wanted >= 0 && wanted != (int)applied && wanted < (int)CameraResolution::Count)
            {
               UINT32 nw = 0, nh = 0;
               LONG nstride = 0;
               std::string err;
               if (ConfigureCameraFormat(reader, (CameraResolution)wanted, nw, nh, nstride, err))
               {
                  applied = (CameraResolution)wanted;
                  stride = nstride;
                  std::lock_guard<std::mutex> lock(cam->frameMutex);
                  cam->width = (int)nw;
                  cam->height = (int)nh;
               }
               cam->pendingResolution.store(-1, std::memory_order_release);
            }

            IMFSample* sample = nullptr;
            DWORD flags = 0;
            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags,
                                    nullptr, &sample);
            if (FAILED(hr))
            {
               cam->error = WinCommon::HrToString("camera read", hr);
               break;
            }
            if (sample == nullptr)
               continue;

            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)))
            {
               BYTE* data = nullptr;
               if (SUCCEEDED(buffer->Lock(&data, nullptr, nullptr)))
               {
                  // Negotiated stride, never an assumed width * 4. A negative
                  // value means bottom-up, which is RGB32's default and what
                  // the "Flipped" in this helper already handles.
                  std::vector<unsigned char> rgba;
                  Rgb32ToRgbaFlipped(data, stride != 0 ? stride : (LONG)(w * 4), (int)w, (int)h,
                                     cam->mirrorX.load(std::memory_order_relaxed), rgba);
                  buffer->Unlock();

                  std::lock_guard<std::mutex> lock(cam->frameMutex);
                  cam->pixels.swap(rgba);
                  cam->width = (int)w;
                  cam->height = (int)h;
                  cam->seq++;
               }
               SafeRelease(&buffer);
            }
            SafeRelease(&sample);

            // Re-read the negotiated size if a resolution change landed. The
            // stride has to be re-read with it: a new frame size renegotiates
            // the row padding too, and a stale stride shears every frame after
            // the switch.
            IMFMediaType* actual = nullptr;
            UINT64 size64 = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                      &actual)))
            {
               actual->GetUINT64(MF_MT_FRAME_SIZE, &size64);
               UINT32 strideU32 = 0;
               const bool haveStride =
                  SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideU32));
               SafeRelease(&actual);
               UnpackFrameSize(size64, w, h);
               stride = haveStride ? (LONG)strideU32 : (LONG)w * 4;
            }
         }

         cam->running.store(false, std::memory_order_release);
         SafeRelease(&reader);
         source->Shutdown();
         SafeRelease(&source);
      }
   }

   std::vector<CameraDeviceInfo> CameraListDevices()
   {
      std::vector<CameraDeviceInfo> devices;
      ComScope com;
      MfScope mf;
      if (!com.ok || !mf.ok)
         return devices;

      IMFAttributes* attrs = nullptr;
      if (FAILED(MFCreateAttributes(&attrs, 1)) ||
          FAILED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
      {
         SafeRelease(&attrs);
         return devices;
      }

      IMFActivate** activates = nullptr;
      UINT32 count = 0;
      if (SUCCEEDED(MFEnumDeviceSources(attrs, &activates, &count)))
      {
         for (UINT32 i = 0; i < count; i++)
         {
            CameraDeviceInfo info;
            WCHAR* name = nullptr;
            WCHAR* link = nullptr;
            UINT32 nameLen = 0, linkLen = 0;
            if (SUCCEEDED(activates[i]->GetAllocatedString(
                   MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen)))
               info.localizedName = WinCommon::WideToUtf8(name);
            if (SUCCEEDED(activates[i]->GetAllocatedString(
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &linkLen)))
               info.uniqueId = WinCommon::WideToUtf8(link);
            CoTaskMemFree(name);
            CoTaskMemFree(link);
            info.isDefault = i == 0; // Windows has no system-default camera concept
            if (!info.uniqueId.empty())
               devices.push_back(info);
            activates[i]->Release();
         }
         CoTaskMemFree(activates);
      }
      SafeRelease(&attrs);
      return devices;
   }

   CameraHandle* CameraOpen(const std::string& deviceId, CameraResolution res, bool mirrorX,
                            std::string& outError)
   {
      outError.clear();
      if (deviceId.empty())
      {
         outError = "no camera selected";
         return nullptr;
      }

      auto* cam = new CameraHandleMf();
      cam->symbolicLink = WinCommon::Utf8ToWide(deviceId);
      cam->resolution = res;
      cam->mirrorX.store(mirrorX, std::memory_order_relaxed);

      try
      {
         cam->thread = std::thread(CameraThreadMain, cam);
      }
      catch (...)
      {
         delete cam;
         outError = "could not start camera thread";
         return nullptr;
      }

      // Wait briefly for the capture thread's verdict so open errors surface
      // through this call's outError like they did on macOS.
      for (int waitedMs = 0; waitedMs < 4000; waitedMs += 25)
      {
         if (cam->running.load(std::memory_order_acquire))
            return reinterpret_cast<CameraHandle*>(cam);
         if (cam->threadDone.load(std::memory_order_acquire))
            break; // thread failed during setup; error is published
         Sleep(25);
      }

      if (cam->running.load(std::memory_order_acquire))
         return reinterpret_cast<CameraHandle*>(cam);
      outError = cam->error.empty() ? std::string("camera failed to start") : cam->error;
      delete cam; // dtor signals stop and joins
      return nullptr;
   }

   void CameraClose(CameraHandle* handle)
   {
      delete reinterpret_cast<CameraHandleMf*>(handle);
   }

   bool CameraIsRunning(CameraHandle* handle)
   {
      auto* cam = reinterpret_cast<CameraHandleMf*>(handle);
      return cam != nullptr && cam->running.load(std::memory_order_acquire);
   }

   void CameraSetMirror(CameraHandle* handle, bool mirrorX)
   {
      auto* cam = reinterpret_cast<CameraHandleMf*>(handle);
      if (cam != nullptr)
         cam->mirrorX.store(mirrorX, std::memory_order_relaxed);
   }

   void CameraSetResolution(CameraHandle* handle, CameraResolution res)
   {
      auto* cam = reinterpret_cast<CameraHandleMf*>(handle);
      if (cam != nullptr && res >= CameraResolution::Auto && res < CameraResolution::Count)
         cam->pendingResolution.store((int)res, std::memory_order_release);
   }

   bool CameraReadFrame(CameraHandle* handle, std::vector<unsigned char>& outPixels,
                        int& outWidth, int& outHeight, unsigned long long& outFrameSeq)
   {
      auto* cam = reinterpret_cast<CameraHandleMf*>(handle);
      if (cam == nullptr)
         return false;

      std::lock_guard<std::mutex> lock(cam->frameMutex);
      if (cam->seq == cam->lastReadSeq || cam->pixels.empty())
         return false;
      cam->lastReadSeq = cam->seq;
      outPixels = cam->pixels;
      outWidth = cam->width;
      outHeight = cam->height;
      outFrameSeq = cam->seq;
      return true;
   }

   // ---- video recording ---------------------------------------------------

   namespace
   {
      // MF_SINK_WRITER_INVALID_STREAM_INDEX is an MIDL enum constant (0xffffffff).
      constexpr DWORD kInvalidStreamId = (DWORD)MF_SINK_WRITER_INVALID_STREAM_INDEX;
      constexpr int kFileAudioChunkFrames = 4096;

      struct RecorderHandleMf
      {
         IMFSinkWriter* writer = nullptr;
         DWORD videoStreamId = kInvalidStreamId;
         DWORD audioStreamId = kInvalidStreamId;

         int width = 0;
         int height = 0;
         double fps = 30.0;
         std::atomic<long long> frameCount { 0 }; // written only on the encoder thread

         // Whether frames handed to RecorderAppend are BGRA8 (the default,
         // matching a native glReadPixels) or RGBA8 (the caller's fallback).
         // Set once, before the first RecorderAppend of the take, by
         // RecorderSetInputIsBgra - never touched after that, so a plain read
         // on `worker` is safe.
         bool inputIsBgra = true;

         // Live-audio streaming mode (RecorderAppendAudio).
         double liveRate = 0.0;
         int liveChannels = 0;
         long long liveSamplesWritten = 0;

         // File-mux mode: decoded once, written at Stop when the final video
         // duration is known (looped or truncated to match). Written on the
         // encoder thread too, from WriteFileAudioTrack at Stop.
         std::string audioPath;
         bool loopAudio = true;
         SampleBuffer fileAudio;

         bool finalized = false;
         std::string stopError;

         // ---- encoder worker --------------------------------------------
         // The render thread (OutputNode) only ever touches queueMutex/
         // poolMutex and the atomics below; WriteSample and the RGBA->BGRA
         // swizzle run entirely on `worker`.
         // A frame-count cap is resolution-blind: 4 frames of slack was 33MB
         // at 1080p but only ~2MB at 320x240. A byte budget scales with
         // resolution instead. 256MB is a starting value, not a measured
         // one - it buys ~32 frames of elasticity at 1080p RGBA.
         static constexpr size_t kMaxQueueBytes = 256ull * 1024 * 1024;
         // Overridable per-instance for RecorderSetTestQueueByteBudget - see
         // the matching macOS comment in Platform.mm.
         size_t queueByteBudget = kMaxQueueBytes;

         struct QueuedFrame
         {
            std::vector<unsigned char> pixels;
            int repeatCount = 1;
         };

         std::mutex queueMutex;
         std::condition_variable queueCv;
         std::deque<QueuedFrame> frameQueue;
         size_t queuedBytes = 0; // guarded by queueMutex
         bool stopRequested = false;

         std::mutex poolMutex;
         std::vector<std::vector<unsigned char>> bufferPool;
         size_t poolBytes = 0; // guarded by poolMutex

         std::atomic<int> pendingCount { 0 };
         std::atomic<int> droppedCount { 0 };

         std::thread worker;

         // IMFSinkWriter::WriteSample calls must be serialized - previously
         // true by accident because every call came from the same (render)
         // thread; now video writes happen on `worker` while live audio
         // (RecorderAppendAudio) still writes from whichever thread the
         // caller is on, so both paths take this before touching `writer`.
         std::mutex writerMutex;

         ~RecorderHandleMf()
         {
            // Should already be stopped by RecorderStop by the time this
            // runs; this only covers the abnormal-teardown path (e.g. the
            // app exiting mid-recording without a clean Stop).
            {
               std::lock_guard<std::mutex> lock(queueMutex);
               stopRequested = true;
            }
            queueCv.notify_one();
            if (worker.joinable())
               worker.join();

            if (writer != nullptr)
            {
               if (!finalized)
                  writer->Finalize();
               SafeRelease(&writer);
            }
            MFShutdown(); // pairs the MFStartup in RecorderStart
         }
      };

      // Builds the H.264 output + RGB32 input pair for the video stream.
      bool ConfigureRecorderVideo(RecorderHandleMf* rec, std::string& outError)
      {
         IMFMediaType* outType = nullptr;
         IMFMediaType* inType = nullptr;
         HRESULT hr = MFCreateMediaType(&outType);
         if (SUCCEEDED(hr))
            hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
         if (SUCCEEDED(hr))
            hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
         if (SUCCEEDED(hr))
         {
            // Quality ladder: ~0.30 bits per pixel per frame, clamped to a
            // sane range. This is deliberately generous - Media Foundation's
            // H.264 encoder is noticeably weaker than VideoToolbox at the
            // same bitrate, and the material here (hard-edged pixel art, flat
            // colour fields, dithering, high-frequency generative detail) is
            // the worst case for a deblocking-heavy encoder. The old 0.08
            // gave 1080p30 barely 5 Mbps, which visibly mushed pixel art.
            // The encoder clamps further if it has to.
            const UINT32 bitrate = (UINT32)std::min(
               80000000.0, std::max(2000000.0,
                                    (double)rec->width * rec->height * rec->fps * 0.30));
            hr = outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
         }
         if (SUCCEEDED(hr))
         {
            // High profile enables 8x8 transform and CABAC. Baseline (the
            // default some MFTs pick) has neither, and costs real quality at
            // any bitrate. Not fatal if the encoder refuses it, so the result
            // is deliberately not folded into `hr`.
            outType->SetUINT32(MF_MT_MPEG2_PROFILE, (UINT32)eAVEncH264VProfile_High);
         }
         if (SUCCEEDED(hr))
            hr = outType->SetUINT64(MF_MT_FRAME_SIZE,
                                    ((UINT64)rec->width << 32) | (UINT32)rec->height);
         if (SUCCEEDED(hr))
            hr = outType->SetUINT64(MF_MT_FRAME_RATE,
                                    (UINT64)llround(rec->fps * 1000) << 32 | 1000);
         if (SUCCEEDED(hr))
            hr = outType->SetUINT32(MF_MT_INTERLACE_MODE,
                                    (UINT32)MFVideoInterlace_Progressive);
         if (SUCCEEDED(hr))
            hr = rec->writer->AddStream(outType, &rec->videoStreamId);

         if (SUCCEEDED(hr))
            hr = MFCreateMediaType(&inType);
         if (SUCCEEDED(hr))
            hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
         if (SUCCEEDED(hr))
            hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT64(MF_MT_FRAME_SIZE,
                                   ((UINT64)rec->width << 32) | (UINT32)rec->height);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT64(MF_MT_FRAME_RATE,
                                   (UINT64)llround(rec->fps * 1000) << 32 | 1000);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_INTERLACE_MODE,
                                   (UINT32)MFVideoInterlace_Progressive);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
         if (SUCCEEDED(hr))
         {
            // MUST be set, and MUST be positive. RGB32's *default* orientation
            // in Media Foundation is bottom-up (negative stride, the GDI
            // BITMAPINFOHEADER convention), so with this attribute absent the
            // sink writer reads our top-down buffer upside down and the whole
            // movie comes out vertically mirrored. RgbaBottomUpToBgraTopDown
            // already flips GL's bottom-up readback into top-down rows, so the
            // stride we hand over is a positive, tightly-packed width * 4.
            hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(rec->width * 4));
         }
         if (SUCCEEDED(hr))
            hr = rec->writer->SetInputMediaType(rec->videoStreamId, inType, nullptr);

         SafeRelease(&outType);
         SafeRelease(&inType);
         if (FAILED(hr))
         {
            outError = WinCommon::HrToString("configuring video encoder", hr);
            return false;
         }
         return true;
      }

      // Adds the AAC audio stream. `inputRate` is whatever PCM we'll actually
      // feed; the output AAC rate matches when standard so no resampling is
      // needed, else 48k and the sink writer's inserted converter handles it.
      bool AddRecorderAudioStream(RecorderHandleMf* rec, double inputRate, int channels,
                                   std::string& outError)
      {
         if (channels <= 0)
            channels = 2;
         const bool standardRate =
            inputRate == 44100.0 || inputRate == 48000.0 || inputRate == 32000.0;
         const UINT32 outRate = (UINT32)(standardRate ? inputRate : 48000.0);
         const UINT32 outBytesPerSec = 24000; // 192 kbps

         IMFMediaType* outType = nullptr;
         IMFMediaType* inType = nullptr;
         HRESULT hr = MFCreateMediaType(&outType);
         if (SUCCEEDED(hr))
            hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
         if (SUCCEEDED(hr))
            hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
         if (SUCCEEDED(hr))
            hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
         if (SUCCEEDED(hr))
            hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, outRate);
         if (SUCCEEDED(hr))
            hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
         if (SUCCEEDED(hr))
            hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, outBytesPerSec);
         if (SUCCEEDED(hr))
            hr = rec->writer->AddStream(outType, &rec->audioStreamId);

         if (SUCCEEDED(hr))
            hr = MFCreateMediaType(&inType);
         if (SUCCEEDED(hr))
            hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
         if (SUCCEEDED(hr))
            hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)inputRate);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
                                   (UINT32)(channels * sizeof(float)));
         if (SUCCEEDED(hr))
            hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                   (UINT32)((double)inputRate * channels * sizeof(float)));
         if (SUCCEEDED(hr))
            hr = rec->writer->SetInputMediaType(rec->audioStreamId, inType, nullptr);

         SafeRelease(&outType);
         SafeRelease(&inType);
         if (FAILED(hr))
         {
            rec->audioStreamId = kInvalidStreamId; // video-only recording still works
            outError = WinCommon::HrToString("configuring audio encoder", hr);
            return false;
         }
         return true;
      }

      bool WriteInterleavedFloatAudio(RecorderHandleMf* rec, const float* interleaved,
                                      long long frames, double rate, int channels)
      {
         if (rec->audioStreamId == kInvalidStreamId || interleaved == nullptr ||
             frames <= 0 || channels <= 0)
            return true;

         const LONGLONG startHns = (LONGLONG)((double)rec->liveSamplesWritten * 10000000.0 / rate);
         IMFMediaBuffer* buffer =
            BufferFromMemory((BYTE*)interleaved, (DWORD)((size_t)frames * channels * sizeof(float)));
         if (buffer == nullptr)
            return false;
         IMFSample* sample = nullptr;
         HRESULT hr = MFCreateSample(&sample);
         if (SUCCEEDED(hr))
            hr = sample->AddBuffer(buffer);
         if (SUCCEEDED(hr))
            hr = sample->SetSampleTime(startHns);
         if (SUCCEEDED(hr))
            hr = sample->SetSampleDuration(
               (LONGLONG)((double)frames * 10000000.0 / rate));
         if (SUCCEEDED(hr))
         {
            std::lock_guard<std::mutex> lock(rec->writerMutex);
            hr = rec->writer->WriteSample(rec->audioStreamId, sample);
         }
         SafeRelease(&sample);
         SafeRelease(&buffer);
         if (FAILED(hr))
            return false;
         rec->liveSamplesWritten += frames;
         return true;
      }

      // Writes the decoded file track through Stop time, looped or truncated
      // to the movie's actual length.
      bool WriteFileAudioTrack(RecorderHandleMf* rec, std::string& outError)
      {
         if (rec->audioStreamId == kInvalidStreamId || rec->fileAudio.numFrames <= 0)
            return true;

         const double movieSeconds = (double)rec->frameCount / rec->fps;
         const double rate = rec->fileAudio.sampleRate > 0.0 ? rec->fileAudio.sampleRate : 44100.0;
         const int channels = rec->fileAudio.channels;
         const long long srcFrames = rec->fileAudio.numFrames;

         // Planar -> interleaved once.
         std::vector<float> interleaved((size_t)srcFrames * channels);
         for (int c = 0; c < channels; c++)
            for (long long f = 0; f < srcFrames; f++)
               interleaved[(size_t)f * channels + c] = rec->fileAudio.channelData[(size_t)c * srcFrames + f];

         const long long neededFrames = (long long)(movieSeconds * rate);
         long long written = 0;
         while (written < neededFrames)
         {
            const long long copyFrom = rec->loopAudio ? written % srcFrames : written;
            if (!rec->loopAudio && copyFrom >= srcFrames)
               break;
            const long long chunk =
               std::min<long long>(kFileAudioChunkFrames, srcFrames - copyFrom);
            const long long remaining = neededFrames - written;
            const long long framesThisCall = std::min(chunk, remaining);
            if (!WriteInterleavedFloatAudio(rec, interleaved.data() + copyFrom * channels,
                                            framesThisCall, rate, channels))
            {
               outError = "writing muxed audio track failed";
               return false;
            }
            written += framesThisCall;
         }
         return true;
      }

      // Runs for the lifetime of the recording on `rec->worker`. Blocks on
      // `queueCv` between frames - the queue is a push producer (OutputNode),
      // not something this loop can poll cheaply, so a condition variable is
      // the natural fit (same shape as CameraThreadMain's pull loop, just
      // waiting on data instead of a stop flag alone).
      void RecorderWorkerThreadMain(RecorderHandleMf* rec)
      {
         ComScope com;
         MfScope mf;
         if (!com.ok || !mf.ok)
            return;

         std::vector<unsigned char> bgraScratch((size_t)rec->width * rec->height * 4);

         // The most recently *processed* frame's converted bytes (not
         // necessarily the most recently written one - see the padding
         // comment below). Local to this thread: it is the only one that
         // ever touches the encoder's frame queue output.
         std::vector<unsigned char> prevBgra;
         bool hasPrevBgra = false;

         for (;;)
         {
            RecorderHandleMf::QueuedFrame frame;
            {
               std::unique_lock<std::mutex> lock(rec->queueMutex);
               rec->queueCv.wait(lock, [rec] { return !rec->frameQueue.empty() || rec->stopRequested; });
               if (rec->frameQueue.empty())
                  break; // stopRequested and fully drained
               frame = std::move(rec->frameQueue.front());
               rec->frameQueue.pop_front();
               rec->queuedBytes -= frame.pixels.size();
            }

            // Convert once per queued frame, not once per repeat - padding a
            // constant-frame-rate take used to re-pay this conversion for
            // byte-identical pixels on every one of its repeats.
            BgraBottomUpToTopDown(frame.pixels.data(), rec->width, rec->height,
                                 rec->inputIsBgra, bgraScratch);

            for (int i = 0; i < frame.repeatCount; i++)
            {
               // Padding fills the earlier repeats with the *previous*
               // frame's content and only the final repeat with this frame's
               // own - so a padded repeat never stamps new pixels at a PTS
               // earlier than when they were actually rendered. The first
               // frame of a take has no previous frame to pad with, so every
               // one of its repeats uses its own content.
               const bool isFinalRepeat = (i == frame.repeatCount - 1);
               const bool useNewFrame = isFinalRepeat || !hasPrevBgra;
               const std::vector<unsigned char>& toWrite = useNewFrame ? bgraScratch : prevBgra;

               IMFMediaBuffer* buffer = BufferFromMemory((BYTE*)toWrite.data(), (DWORD)toWrite.size());
               if (buffer != nullptr)
               {
                  IMFSample* sample = nullptr;
                  HRESULT hr = MFCreateSample(&sample);
                  if (SUCCEEDED(hr))
                     hr = sample->AddBuffer(buffer);
                  if (SUCCEEDED(hr))
                  {
                     const LONGLONG t = FrameNumberToHns(rec->frameCount, rec->fps);
                     hr = sample->SetSampleTime(t);
                     if (SUCCEEDED(hr))
                        hr = sample->SetSampleDuration(FrameNumberToHns(rec->frameCount + 1, rec->fps) - t);
                  }
                  if (SUCCEEDED(hr))
                  {
                     std::lock_guard<std::mutex> lock(rec->writerMutex);
                     hr = rec->writer->WriteSample(rec->videoStreamId, sample);
                  }
                  SafeRelease(&sample);
                  SafeRelease(&buffer);
                  if (SUCCEEDED(hr))
                     rec->frameCount++;
               }
               rec->pendingCount.fetch_sub(1, std::memory_order_relaxed);
            }

            // This frame becomes "the previous frame" for whatever comes
            // next, regardless of whether every repeat above actually made
            // it to the writer.
            prevBgra.swap(bgraScratch);
            hasPrevBgra = true;

            std::lock_guard<std::mutex> poolLock(rec->poolMutex);
            const size_t bytes = frame.pixels.size();
            if (rec->poolBytes + bytes <= rec->queueByteBudget)
            {
               rec->poolBytes += bytes;
               rec->bufferPool.push_back(std::move(frame.pixels));
            }
         }
      }
   }

   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError,
                                 const std::string& audioPath, bool loopAudio,
                                 double liveAudioSampleRate, int liveAudioChannels)
   {
      outError.clear();
      ComScope com;
      if (!com.ok || FAILED(MFStartup(MF_VERSION)))
      {
         outError = "could not initialize Media Foundation";
         return nullptr;
      }

      auto* rec = new RecorderHandleMf(); // dtor pairs the MFStartup above
      rec->width = width & ~1;
      rec->height = height & ~1;
      rec->fps = fps > 0 ? (double)fps : 30.0;
      rec->audioPath = audioPath;
      rec->loopAudio = loopAudio;

      HRESULT hr = MFCreateSinkWriterFromURL(WinCommon::Utf8ToWide(path).c_str(), nullptr,
                                             nullptr, &rec->writer);
      if (FAILED(hr))
      {
         // The handle's destructor pairs the MFStartup above; don't shut MF
         // down here as well.
         outError = WinCommon::HrToString("creating MP4 writer", hr);
         delete rec;
         return nullptr;
      }

      if (!ConfigureRecorderVideo(rec, outError))
      {
         delete rec;
         return nullptr;
      }

      if (liveAudioSampleRate > 0.0)
      {
         rec->liveRate = liveAudioSampleRate;
         rec->liveChannels = liveAudioChannels > 0 ? liveAudioChannels : 2;
         std::string audioErr;
         if (!AddRecorderAudioStream(rec, liveAudioSampleRate, rec->liveChannels, audioErr))
            outError = "audio track unavailable: " + audioErr; // record video anyway
      }
      else if (!audioPath.empty())
      {
         // Decode up front; the samples are written at Stop once the real
         // video duration is known. Recording does not depend on playback.
         std::string decodeErr;
         if (!DecodeAudioFileToBuffer(audioPath, rec->fileAudio, decodeErr))
            outError = "audio muxing unavailable: " + decodeErr;
      }

      hr = rec->writer->BeginWriting();
      if (FAILED(hr))
      {
         outError = WinCommon::HrToString("starting writer", hr);
         delete rec;
         return nullptr;
      }

      rec->worker = std::thread(RecorderWorkerThreadMain, rec);
      return reinterpret_cast<RecorderHandle*>(rec);
   }

   std::vector<unsigned char> RecorderAcquireFrameBuffer(RecorderHandle* handle)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      if (rec == nullptr)
         return {};
      const size_t bytes = (size_t)rec->width * rec->height * 4;
      std::lock_guard<std::mutex> lock(rec->poolMutex);
      if (!rec->bufferPool.empty())
      {
         std::vector<unsigned char> buf = std::move(rec->bufferPool.back());
         rec->bufferPool.pop_back();
         rec->poolBytes -= buf.size();
         buf.resize(bytes);
         return buf;
      }
      return std::vector<unsigned char>(bytes);
   }

   bool RecorderAppend(RecorderHandle* handle, std::vector<unsigned char>&& pixels, int repeatCount)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      if (rec == nullptr || rec->writer == nullptr || rec->videoStreamId == kInvalidStreamId)
         return false;
      if ((int)pixels.size() < rec->width * rec->height * 4)
         return false;
      if (repeatCount < 1)
         repeatCount = 1;

      std::lock_guard<std::mutex> lock(rec->queueMutex);
      // Byte-budgeted rather than frame-count-capped so elasticity scales
      // with resolution. The frameQueue.empty() escape hatch guarantees a
      // single frame larger than the whole budget is still admitted rather
      // than permanently rejected.
      if (rec->queuedBytes + pixels.size() > rec->queueByteBudget &&
          !rec->frameQueue.empty())
      {
         rec->droppedCount.fetch_add(1, std::memory_order_relaxed);
         std::lock_guard<std::mutex> poolLock(rec->poolMutex);
         if (rec->poolBytes + pixels.size() <= rec->queueByteBudget)
         {
            rec->poolBytes += pixels.size();
            rec->bufferPool.push_back(std::move(pixels));
         }
         return false;
      }

      rec->pendingCount.fetch_add(repeatCount, std::memory_order_relaxed);
      rec->queuedBytes += pixels.size();
      rec->frameQueue.push_back({ std::move(pixels), repeatCount });
      rec->queueCv.notify_one();
      return true;
   }

   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels)
   {
      std::vector<unsigned char> copy = pixels;
      return RecorderAppend(handle, std::move(copy), 1);
   }

   void RecorderSetInputIsBgra(RecorderHandle* handle, bool isBgra)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      if (rec != nullptr)
         rec->inputIsBgra = isBgra;
   }

   int RecorderPendingFrameCount(RecorderHandle* handle)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      return rec ? rec->pendingCount.load(std::memory_order_relaxed) : 0;
   }

   void RecorderSetTestQueueByteBudget(RecorderHandle* handle, size_t bytes)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      if (rec == nullptr)
         return;
      std::lock_guard<std::mutex> lock(rec->queueMutex);
      rec->queueByteBudget = bytes;
   }

   int RecorderDroppedFrameCount(RecorderHandle* handle)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      return rec ? rec->droppedCount.load(std::memory_order_relaxed) : 0;
   }

   bool RecorderAppendAudio(RecorderHandle* handle, const float* interleavedSamples,
                            int numFrames)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      if (rec == nullptr || interleavedSamples == nullptr || numFrames <= 0)
         return false;
      if (rec->audioStreamId == kInvalidStreamId)
         return false; // not configured for live audio
      ComScope com;
      if (!com.ok)
         return false;
      return WriteInterleavedFloatAudio(rec, interleavedSamples, numFrames, rec->liveRate,
                                        rec->liveChannels);
   }

   bool RecorderStop(RecorderHandle* handle, std::string& outError,
                     int* outFrameCount, int* outDroppedCount)
   {
      outError.clear();
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      if (rec == nullptr)
         return false;
      ComScope com;
      if (!com.ok)
      {
         outError = "COM unavailable";
         return false;
      }

      // Let the worker drain whatever is still queued before touching
      // frameCount or the writer - WriteFileAudioTrack below needs the final
      // video duration, and Finalize must not race the worker's WriteSample
      // calls. A stop is allowed to take a few milliseconds; the alternative
      // is a truncated movie.
      {
         std::lock_guard<std::mutex> lock(rec->queueMutex);
         rec->stopRequested = true;
      }
      rec->queueCv.notify_one();
      if (rec->worker.joinable())
         rec->worker.join();

      // Read after the join above, not before - the worker's last few
      // writes land between the caller's pre-stop snapshot and here.
      if (outFrameCount != nullptr)
         *outFrameCount = (int)rec->frameCount;
      if (outDroppedCount != nullptr)
         *outDroppedCount = rec->droppedCount.load(std::memory_order_relaxed);

      // File-audio mode muxes here, now that the movie's real duration is known.
      if (!rec->audioPath.empty() && !rec->finalized)
      {
         std::string audioErr;
         if (!AddRecorderAudioStream(rec, rec->fileAudio.sampleRate, rec->fileAudio.channels,
                                     audioErr))
            outError = "audio muxing skipped: " + audioErr;
         else if (!WriteFileAudioTrack(rec, audioErr))
            outError = audioErr;
      }

      HRESULT hr = rec->writer->Flush(0);
      if (rec->audioStreamId != kInvalidStreamId)
         rec->writer->Flush(rec->audioStreamId);
      hr = rec->writer->Finalize();
      rec->finalized = true;

      const bool ok = SUCCEEDED(hr);
      if (!ok && outError.empty())
         outError = WinCommon::HrToString("finalizing movie", hr);
      delete rec;
      return ok && outError.empty();
   }

   int RecorderFrameCount(RecorderHandle* handle)
   {
      auto* rec = reinterpret_cast<RecorderHandleMf*>(handle);
      return rec != nullptr ? (int)rec->frameCount : 0;
   }

   MovieInfo InspectMovie(const std::string& path)
   {
      MovieInfo info;
      ComScope com;
      MfScope mf;
      if (!com.ok || !mf.ok)
         return info;

      IMFSourceReader* reader = nullptr;
      if (FAILED(MFCreateSourceReaderFromURL(WinCommon::Utf8ToWide(path).c_str(), nullptr,
                                             &reader)))
         return info;

      PROPVARIANT duration {};
      duration.vt = VT_EMPTY;
      if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                     MF_PD_DURATION, &duration)) &&
          duration.vt == VT_I8)
         info.duration = (double)duration.hVal.QuadPart / 10000000.0;
      PropVariantClear(&duration);

      // Streams are dense from 0; the first missing one ends the walk.
      for (DWORD stream = 0; stream < 63; stream++)
      {
         IMFMediaType* native = nullptr;
         const HRESULT hr = reader->GetNativeMediaType(stream, 0, &native);
         if (hr == MF_E_INVALIDSTREAMNUMBER || FAILED(hr))
            break;
         if (MajorTypeIs(native, MFMediaType_Video))
            info.hasVideo = true;
         else if (MajorTypeIs(native, MFMediaType_Audio))
            info.hasAudio = true;
         SafeRelease(&native);
      }
      SafeRelease(&reader);

      if (info.hasVideo)
      {
         // Walks the actual encoded sample stream rather than deriving a
         // count from duration*fps - that would report the requested rate
         // even for a take that silently dropped frames and padded to length.
         IMFSourceReader* countReader = nullptr;
         if (SUCCEEDED(MFCreateSourceReaderFromURL(WinCommon::Utf8ToWide(path).c_str(), nullptr,
                                                    &countReader)))
         {
            countReader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
            countReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

            int count = 0;
            for (;;)
            {
               DWORD streamIndex = 0, flags = 0;
               LONGLONG ts = 0;
               IMFSample* sample = nullptr;
               const HRESULT readHr = countReader->ReadSample(
                  (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &ts, &sample);
               if (FAILED(readHr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
               {
                  SafeRelease(&sample);
                  break;
               }
               if (sample != nullptr)
               {
                  count++;
                  SafeRelease(&sample);
               }
            }
            info.frameCount = count;
            SafeRelease(&countReader);
         }
      }
      return info;
   }
}

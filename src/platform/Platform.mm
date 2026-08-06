#include "Platform.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <Accelerate/Accelerate.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>

namespace Platform
{
   std::string OpenImageDialog()
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:YES];
         [panel setCanChooseDirectories:NO];
         [panel setAllowsMultipleSelection:NO];
         [panel setResolvesAliases:YES];
         [panel setTitle:@"Open image"];

         // Everything ImageIO can decode - png, jpeg, tiff, heic, webp, raw, ...
         NSArray<NSString*>* types = [NSImage imageTypes];
         if (@available(macOS 11.0, *))
         {
            NSMutableArray* contentTypes = [NSMutableArray array];
            for (NSString* identifier in types)
            {
               UTType* type = [UTType typeWithIdentifier:identifier];
               if (type != nil)
                  [contentTypes addObject:type];
            }
            if ([contentTypes count] > 0)
               [panel setAllowedContentTypes:contentTypes];
         }

         if ([panel runModal] != NSModalResponseOK)
            return std::string();

         NSURL* url = [[panel URLs] firstObject];
         if (url == nil)
            return std::string();
         return std::string([[url path] UTF8String]);
      }
   }

   bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& outPixels,
                      int& outWidth, int& outHeight, std::string& outError)
   {
      @autoreleasepool
      {
         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         if (nsPath == nil)
         {
            outError = "bad path";
            return false;
         }

         NSImage* image = [[NSImage alloc] initWithContentsOfFile:nsPath];
         if (image == nil)
         {
            outError = "not an image this Mac can read";
            return false;
         }

         CGImageRef cgImage = [image CGImageForProposedRect:NULL context:nil hints:nil];
         if (cgImage == NULL)
         {
            outError = "could not decode image";
            return false;
         }

         const int w = (int)CGImageGetWidth(cgImage);
         const int h = (int)CGImageGetHeight(cgImage);
         if (w <= 0 || h <= 0)
         {
            outError = "empty image";
            return false;
         }

         std::vector<unsigned char> topDown((size_t)w * h * 4, 0);
         CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
         CGContextRef ctx = CGBitmapContextCreate(topDown.data(), w, h, 8, (size_t)w * 4,
                                                  colorSpace, kCGImageAlphaPremultipliedLast);
         CGColorSpaceRelease(colorSpace);
         if (ctx == NULL)
         {
            outError = "could not allocate bitmap";
            return false;
         }

         CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImage);
         CGContextRelease(ctx);

         // CG fills the buffer top row first; GL wants row 0 at the bottom.
         outPixels.assign((size_t)w * h * 4, 0);
         const size_t stride = (size_t)w * 4;
         for (int y = 0; y < h; y++)
            memcpy(&outPixels[y * stride], &topDown[(h - 1 - y) * stride], stride);

         outWidth = w;
         outHeight = h;
         outError.clear();
         return true;
      }
   }
}

// ============================================================ video decoding

namespace Platform
{
   struct VideoHandle
   {
      AVAsset* asset = nil;
      AVAssetReader* reader = nil;
      AVAssetReaderTrackOutput* output = nil;
      AVAssetTrack* track = nil;
      int width = 0;
      int height = 0;
      double duration = 0.0;
      double currentPts = -1.0;  // presentation time of the frame we last handed out
      double nextPts = -1.0;     // pts of the decoded-but-not-yet-current frame
      std::vector<unsigned char> pending; // that frame's pixels
      bool finished = false;
   };

   namespace
   {
      bool StartReader(VideoHandle* h, double fromSeconds, std::string& outError)
      {
         NSError* err = nil;
         h->reader = [[AVAssetReader alloc] initWithAsset:h->asset error:&err];
         if (h->reader == nil)
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "reader failed";
            return false;
         }

         NSDictionary* settings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
         };
         h->output = [[AVAssetReaderTrackOutput alloc] initWithTrack:h->track outputSettings:settings];
         h->output.alwaysCopiesSampleData = NO;
         if (![h->reader canAddOutput:h->output])
         {
            outError = "cannot add video output";
            return false;
         }
         [h->reader addOutput:h->output];

         if (fromSeconds > 0.0)
         {
            CMTime start = CMTimeMakeWithSeconds(fromSeconds, 600);
            h->reader.timeRange = CMTimeRangeMake(start, kCMTimePositiveInfinity);
         }

         if (![h->reader startReading])
         {
            outError = "startReading failed";
            return false;
         }
         h->finished = false;
         h->currentPts = -1.0;
         h->nextPts = -1.0;
         return true;
      }

      // Pulls exactly one frame into h->pending / h->nextPts.
      bool DecodeNext(VideoHandle* h)
      {
         if (h->reader == nil || h->finished)
            return false;

         CMSampleBufferRef sample = [h->output copyNextSampleBuffer];
         if (sample == NULL)
         {
            h->finished = true;
            return false;
         }

         CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
         h->nextPts = CMTimeGetSeconds(pts);

         CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sample);
         if (pixelBuffer == NULL)
         {
            CFRelease(sample);
            return false;
         }

         CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
         const int w = (int)CVPixelBufferGetWidth(pixelBuffer);
         const int h_ = (int)CVPixelBufferGetHeight(pixelBuffer);
         const size_t srcStride = CVPixelBufferGetBytesPerRow(pixelBuffer);
         const unsigned char* src = (const unsigned char*)CVPixelBufferGetBaseAddress(pixelBuffer);

         h->width = w;
         h->height = h_;
         h->pending.assign((size_t)w * h_ * 4, 0);

         // BGRA -> RGBA, and flip rows for GL's bottom-up textures
         for (int y = 0; y < h_; y++)
         {
            const unsigned char* srcRow = src + (size_t)y * srcStride;
            unsigned char* dstRow = h->pending.data() + (size_t)(h_ - 1 - y) * w * 4;
            for (int x = 0; x < w; x++)
            {
               dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
               dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
               dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
               dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
         }

         CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
         CFRelease(sample);
         return true;
      }
   }

   std::string OpenVideoDialog()
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:YES];
         [panel setCanChooseDirectories:NO];
         [panel setAllowsMultipleSelection:NO];
         [panel setTitle:@"Open video"];

         if (@available(macOS 11.0, *))
            [panel setAllowedContentTypes:@[ UTTypeMovie, UTTypeVideo, UTTypeQuickTimeMovie, UTTypeMPEG4Movie ]];

         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [[panel URLs] firstObject];
         if (url == nil)
            return std::string();
         return std::string([[url path] UTF8String]);
      }
   }

   VideoHandle* VideoOpen(const std::string& path, std::string& outError)
   {
      @autoreleasepool
      {
         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         NSURL* url = [NSURL fileURLWithPath:nsPath];
         AVAsset* asset = [AVAsset assetWithURL:url];
         if (asset == nil)
         {
            outError = "could not open file";
            return nullptr;
         }

         NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
         if ([tracks count] == 0)
         {
            outError = "no video track in this file";
            return nullptr;
         }

         VideoHandle* h = new VideoHandle();
         h->asset = asset;
         h->track = [tracks firstObject];
         CGSize size = [h->track naturalSize];
         h->width = (int)std::abs(size.width);
         h->height = (int)std::abs(size.height);
         h->duration = CMTimeGetSeconds([asset duration]);

         if (!StartReader(h, 0.0, outError))
         {
            delete h;
            return nullptr;
         }
         return h;
      }
   }

   void VideoClose(VideoHandle* handle)
   {
      if (handle == nullptr)
         return;
      @autoreleasepool
      {
         if (handle->reader != nil)
            [handle->reader cancelReading];
         handle->reader = nil;
         handle->output = nil;
         handle->track = nil;
         handle->asset = nil;
      }
      delete handle;
   }

   int VideoWidth(VideoHandle* handle) { return handle ? handle->width : 0; }
   int VideoHeight(VideoHandle* handle) { return handle ? handle->height : 0; }
   double VideoDuration(VideoHandle* handle) { return handle ? handle->duration : 0.0; }

   bool VideoFrameAt(VideoHandle* handle, double seconds, std::vector<unsigned char>& outPixels)
   {
      if (handle == nullptr)
         return false;

      @autoreleasepool
      {
         // going backwards (loop or scrub) means rebuilding the sequential reader
         if (seconds + 0.001 < handle->currentPts)
         {
            std::string err;
            if (handle->reader != nil)
               [handle->reader cancelReading];
            handle->reader = nil;
            handle->output = nil;
            if (!StartReader(handle, 0.0, err))
               return false;
         }

         bool produced = false;
         // decode forward until the pending frame is in the future
         for (int guard = 0; guard < 240; guard++)
         {
            if (handle->nextPts < 0.0)
            {
               if (!DecodeNext(handle))
                  break;
            }
            if (handle->nextPts > seconds)
               break;

            outPixels = handle->pending;
            handle->currentPts = handle->nextPts;
            handle->nextPts = -1.0;
            produced = true;
         }
         return produced;
      }
   }

   // =========================================================== recording

   struct RecorderHandle
   {
      AVAssetWriter* writer = nil;
      AVAssetWriterInput* input = nil;
      AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
      int width = 0;
      int height = 0;
      int fps = 30;
      long long frameIndex = 0;
   };

   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError)
   {
      @autoreleasepool
      {
         // H.264 requires even dimensions
         width &= ~1;
         height &= ~1;
         if (width <= 0 || height <= 0)
         {
            outError = "invalid recording size";
            return nullptr;
         }

         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         NSURL* url = [NSURL fileURLWithPath:nsPath];
         [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

         NSError* err = nil;
         AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL:url
                                                           fileType:AVFileTypeQuickTimeMovie
                                                              error:&err];
         if (writer == nil)
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "writer failed";
            return nullptr;
         }

         NSDictionary* videoSettings = @{
            AVVideoCodecKey  : AVVideoCodecTypeH264,
            AVVideoWidthKey  : @(width),
            AVVideoHeightKey : @(height)
         };
         AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                       outputSettings:videoSettings];
         input.expectsMediaDataInRealTime = NO;

         NSDictionary* attrs = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey           : @(width),
            (id)kCVPixelBufferHeightKey          : @(height)
         };
         AVAssetWriterInputPixelBufferAdaptor* adaptor =
            [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                                                          sourcePixelBufferAttributes:attrs];

         if (![writer canAddInput:input])
         {
            outError = "cannot add writer input";
            return nullptr;
         }
         [writer addInput:input];

         if (![writer startWriting])
         {
            outError = "startWriting failed";
            return nullptr;
         }
         [writer startSessionAtSourceTime:kCMTimeZero];

         RecorderHandle* h = new RecorderHandle();
         h->writer = writer;
         h->input = input;
         h->adaptor = adaptor;
         h->width = width;
         h->height = height;
         h->fps = fps > 0 ? fps : 30;
         return h;
      }
   }

   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels)
   {
      if (handle == nullptr)
         return false;

      @autoreleasepool
      {
         if (!handle->input.isReadyForMoreMediaData)
            return false;

         CVPixelBufferRef buffer = NULL;
         CVReturn status = CVPixelBufferPoolCreatePixelBuffer(NULL, handle->adaptor.pixelBufferPool, &buffer);
         if (status != kCVReturnSuccess || buffer == NULL)
            return false;

         CVPixelBufferLockBaseAddress(buffer, 0);
         unsigned char* dst = (unsigned char*)CVPixelBufferGetBaseAddress(buffer);
         const size_t dstStride = CVPixelBufferGetBytesPerRow(buffer);
         const int w = handle->width;
         const int h = handle->height;

         // incoming is RGBA bottom-up from glReadPixels; the writer wants BGRA top-down
         for (int y = 0; y < h; y++)
         {
            const unsigned char* srcRow = pixels.data() + (size_t)(h - 1 - y) * w * 4;
            unsigned char* dstRow = dst + (size_t)y * dstStride;
            for (int x = 0; x < w; x++)
            {
               dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
               dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
               dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
               dstRow[x * 4 + 3] = 255;
            }
         }
         CVPixelBufferUnlockBaseAddress(buffer, 0);

         CMTime when = CMTimeMake(handle->frameIndex, handle->fps);
         BOOL ok = [handle->adaptor appendPixelBuffer:buffer withPresentationTime:when];
         CVPixelBufferRelease(buffer);
         if (ok)
            handle->frameIndex++;
         return ok == YES;
      }
   }

   bool RecorderStop(RecorderHandle* handle, std::string& outError)
   {
      if (handle == nullptr)
         return false;

      __block bool done = false;
      @autoreleasepool
      {
         [handle->input markAsFinished];
         [handle->writer finishWritingWithCompletionHandler:^{ done = true; }];

         // finishWriting is async; the encoder is fast enough that a short spin is fine
         for (int i = 0; i < 2000 && !done; i++)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];

         if (handle->writer.status == AVAssetWriterStatusFailed)
         {
            NSError* err = handle->writer.error;
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "write failed";
            delete handle;
            return false;
         }
      }
      delete handle;
      outError.clear();
      return true;
   }

   int RecorderFrameCount(RecorderHandle* handle)
   {
      return handle ? (int)handle->frameIndex : 0;
   }
}

// ==================================================== background removal

namespace Platform
{
   bool SubjectMask(const std::vector<unsigned char>& rgbaPixels, int width, int height,
                    MattingMode mode, std::vector<unsigned char>& outMask,
                    std::string& outError)
   {
      if (width <= 0 || height <= 0 || rgbaPixels.size() < (size_t)width * height * 4)
      {
         outError = "bad image";
         return false;
      }

      @autoreleasepool
      {
         // Incoming pixels are bottom-up (GL order); Vision wants top-down.
         const size_t stride = (size_t)width * 4;
         std::vector<unsigned char> topDown(rgbaPixels.size());
         for (int y = 0; y < height; y++)
            memcpy(&topDown[y * stride], &rgbaPixels[(height - 1 - y) * stride], stride);

         CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
         CGContextRef ctx = CGBitmapContextCreate(topDown.data(), width, height, 8, stride, cs,
                                                  kCGImageAlphaPremultipliedLast);
         CGColorSpaceRelease(cs);
         if (ctx == NULL)
         {
            outError = "could not wrap image";
            return false;
         }
         CGImageRef cgImage = CGBitmapContextCreateImage(ctx);
         CGContextRelease(ctx);
         if (cgImage == NULL)
         {
            outError = "could not build CGImage";
            return false;
         }

         VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];

         CVPixelBufferRef maskBuffer = NULL;
         NSError* err = nil;

         if (mode == MattingMode::Subject)
         {
            if (@available(macOS 14.0, *))
            {
               VNGenerateForegroundInstanceMaskRequest* request =
                  [[VNGenerateForegroundInstanceMaskRequest alloc] init];
               if (![handler performRequests:@[ request ] error:&err])
               {
                  outError = err ? std::string([[err localizedDescription] UTF8String]) : "vision failed";
                  CGImageRelease(cgImage);
                  return false;
               }
               VNInstanceMaskObservation* obs = [[request results] firstObject];
               if (obs == nil)
               {
                  outError = "no subject found in this image";
                  CGImageRelease(cgImage);
                  return false;
               }
               maskBuffer = [obs generateScaledMaskForImageForInstances:obs.allInstances
                                                     fromRequestHandler:handler
                                                                  error:&err];
            }
            else
            {
               outError = "subject masking needs macOS 14 - try Person mode";
               CGImageRelease(cgImage);
               return false;
            }
         }
         else
         {
            if (@available(macOS 12.0, *))
            {
               VNGeneratePersonSegmentationRequest* request =
                  [[VNGeneratePersonSegmentationRequest alloc] init];
               request.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelAccurate;
               request.outputPixelFormat = kCVPixelFormatType_OneComponent8;
               if (![handler performRequests:@[ request ] error:&err])
               {
                  outError = err ? std::string([[err localizedDescription] UTF8String]) : "vision failed";
                  CGImageRelease(cgImage);
                  return false;
               }
               VNPixelBufferObservation* obs = [[request results] firstObject];
               if (obs == nil)
               {
                  outError = "no person found in this image";
                  CGImageRelease(cgImage);
                  return false;
               }
               maskBuffer = obs.pixelBuffer;
               CVPixelBufferRetain(maskBuffer);
            }
            else
            {
               outError = "person segmentation needs macOS 12";
               CGImageRelease(cgImage);
               return false;
            }
         }

         CGImageRelease(cgImage);

         if (maskBuffer == NULL)
         {
            if (outError.empty())
               outError = err ? std::string([[err localizedDescription] UTF8String]) : "no mask produced";
            return false;
         }

         CVPixelBufferLockBaseAddress(maskBuffer, kCVPixelBufferLock_ReadOnly);
         const int mw = (int)CVPixelBufferGetWidth(maskBuffer);
         const int mh = (int)CVPixelBufferGetHeight(maskBuffer);
         const size_t mStride = CVPixelBufferGetBytesPerRow(maskBuffer);
         const unsigned char* mSrc = (const unsigned char*)CVPixelBufferGetBaseAddress(maskBuffer);

         // Rescale (nearest) to the requested size and flip back to GL order.
         outMask.assign((size_t)width * height, 0);
         for (int y = 0; y < height; y++)
         {
            const int sy = std::min(mh - 1, y * mh / height);
            unsigned char* dstRow = &outMask[(size_t)(height - 1 - y) * width];
            const unsigned char* srcRow = mSrc + (size_t)sy * mStride;
            for (int x = 0; x < width; x++)
               dstRow[x] = srcRow[std::min(mw - 1, x * mw / width)];
         }

         CVPixelBufferUnlockBaseAddress(maskBuffer, kCVPixelBufferLock_ReadOnly);
         CVPixelBufferRelease(maskBuffer);
         outError.clear();
         return true;
      }
   }
}

// =============================================================== audio input

namespace Platform
{
   namespace
   {
      const int kFftLog2 = 10;              // 1024-point FFT
      const int kFftSize = 1 << kFftLog2;
      const int kSpectrumSize = kFftSize / 2;

      AVAudioEngine* gEngine = nil;
      bool gRunning = false;
      std::string gDeviceName;

      std::mutex gAudioMutex;
      AudioLevels gLevels;
      float gAttack = 0.5f;
      float gRelease = 0.12f;
      float gGain = 1.0f;
      float gPrevFlux = 0.0f;
      std::vector<float> gPrevMagnitude;

      FFTSetup gFftSetup = nullptr;
      std::vector<float> gWindow;
      std::vector<float> gSampleRing;

      float Smooth(float previous, float target)
      {
         const float rate = (target > previous) ? gAttack : gRelease;
         return previous + (target - previous) * rate;
      }

      void ProcessBuffer(const float* samples, int frameCount, double sampleRate)
      {
         if (frameCount <= 0)
            return;

         // Keep a rolling window so the FFT always sees a full frame even when
         // CoreAudio hands us short buffers.
         gSampleRing.insert(gSampleRing.end(), samples, samples + frameCount);
         if ((int)gSampleRing.size() < kFftSize)
            return;
         if ((int)gSampleRing.size() > kFftSize * 4)
            gSampleRing.erase(gSampleRing.begin(), gSampleRing.end() - kFftSize);

         const float* frame = gSampleRing.data() + (gSampleRing.size() - kFftSize);

         float rms = 0.0f, peak = 0.0f;
         for (int i = 0; i < kFftSize; i++)
         {
            const float v = frame[i] * gGain;
            rms += v * v;
            peak = std::max(peak, std::fabs(v));
         }
         rms = std::sqrt(rms / (float)kFftSize);

         std::vector<float> windowed(kFftSize);
         vDSP_vmul(frame, 1, gWindow.data(), 1, windowed.data(), 1, kFftSize);

         std::vector<float> real(kSpectrumSize, 0.0f);
         std::vector<float> imag(kSpectrumSize, 0.0f);
         DSPSplitComplex split = { real.data(), imag.data() };
         vDSP_ctoz((const DSPComplex*)windowed.data(), 2, &split, 1, kSpectrumSize);
         vDSP_fft_zrip(gFftSetup, &split, 1, kFftLog2, FFT_FORWARD);

         std::vector<float> magnitude(kSpectrumSize, 0.0f);
         vDSP_zvabs(&split, 1, magnitude.data(), 1, kSpectrumSize);
         const float norm = 2.0f / (float)kFftSize;
         for (int i = 0; i < kSpectrumSize; i++)
            magnitude[i] *= norm * gGain;

         if (gPrevMagnitude.size() != magnitude.size())
            gPrevMagnitude.assign(magnitude.size(), 0.0f);

         // Spectral flux: sum of positive frame-to-frame change, the standard
         // cheap onset detector.
         float flux = 0.0f;
         for (int i = 0; i < kSpectrumSize; i++)
            flux += std::max(0.0f, magnitude[i] - gPrevMagnitude[i]);
         const bool onset = flux > gPrevFlux * 1.6f && flux > 0.02f;
         gPrevFlux = gPrevFlux * 0.7f + flux * 0.3f;
         gPrevMagnitude = magnitude;

         // Log-spaced bands: linear bins would put almost everything in band 0.
         const double nyquist = sampleRate * 0.5;
         float bands[kAudioBands] = { 0 };
         for (int b = 0; b < kAudioBands; b++)
         {
            const double loHz = 20.0 * std::pow(nyquist / 20.0, (double)b / kAudioBands);
            const double hiHz = 20.0 * std::pow(nyquist / 20.0, (double)(b + 1) / kAudioBands);
            const int lo = std::max(1, (int)(loHz / nyquist * kSpectrumSize));
            const int hi = std::min(kSpectrumSize - 1, (int)(hiHz / nyquist * kSpectrumSize));
            float sum = 0.0f;
            int count = 0;
            for (int i = lo; i <= hi; i++) { sum += magnitude[i]; count++; }
            bands[b] = count > 0 ? sum / count : 0.0f;
         }

         auto rangeEnergy = [&](double fromHz, double toHz) {
            const int lo = std::max(1, (int)(fromHz / nyquist * kSpectrumSize));
            const int hi = std::min(kSpectrumSize - 1, (int)(toHz / nyquist * kSpectrumSize));
            float sum = 0.0f; int count = 0;
            for (int i = lo; i <= hi; i++) { sum += magnitude[i]; count++; }
            return count > 0 ? sum / count : 0.0f;
         };

         // Magnitudes are tiny; a compressive curve maps them into a usable 0..1
         // without the user having to ride a gain slider constantly.
         auto shape = [](float v) { return std::min(1.0f, std::sqrt(v * 12.0f)); };

         std::lock_guard<std::mutex> lock(gAudioMutex);
         gLevels.rms = Smooth(gLevels.rms, std::min(1.0f, rms * 3.0f));
         gLevels.peak = Smooth(gLevels.peak, std::min(1.0f, peak));
         gLevels.low = Smooth(gLevels.low, shape(rangeEnergy(20.0, 250.0)));
         gLevels.mid = Smooth(gLevels.mid, shape(rangeEnergy(250.0, 2000.0)));
         gLevels.high = Smooth(gLevels.high, shape(rangeEnergy(2000.0, 16000.0)));
         for (int b = 0; b < kAudioBands; b++)
            gLevels.bands[b] = Smooth(gLevels.bands[b], shape(bands[b]));
         if (onset)
            gLevels.onset = true;
      }
   }

   bool AudioStart(std::string& outError)
   {
      if (gRunning)
         return true;

      @autoreleasepool
      {
         if (gFftSetup == nullptr)
         {
            gFftSetup = vDSP_create_fftsetup(kFftLog2, FFT_RADIX2);
            gWindow.resize(kFftSize);
            vDSP_hann_window(gWindow.data(), kFftSize, vDSP_HANN_NORM);
         }

         gEngine = [[AVAudioEngine alloc] init];
         AVAudioInputNode* input = [gEngine inputNode];
         AVAudioFormat* format = [input inputFormatForBus:0];
         if (format == nil || format.sampleRate <= 0 || format.channelCount == 0)
         {
            outError = "no audio input available (check System Settings > Privacy > Microphone)";
            gEngine = nil;
            return false;
         }

         gDeviceName = "default input";
         const double sampleRate = format.sampleRate;

         [input installTapOnBus:0
                     bufferSize:1024
                         format:format
                          block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
            const float* const* channels = buffer.floatChannelData;
            if (channels == nullptr)
               return;
            ProcessBuffer(channels[0], (int)buffer.frameLength, sampleRate);
         }];

         NSError* err = nil;
         [gEngine prepare];
         if (![gEngine startAndReturnError:&err])
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "engine failed to start";
            [input removeTapOnBus:0];
            gEngine = nil;
            return false;
         }

         gRunning = true;
         outError.clear();
         return true;
      }
   }

   void AudioStop()
   {
      if (!gRunning)
         return;
      @autoreleasepool
      {
         [[gEngine inputNode] removeTapOnBus:0];
         [gEngine stop];
         gEngine = nil;
      }
      gRunning = false;
      std::lock_guard<std::mutex> lock(gAudioMutex);
      gLevels = AudioLevels();
   }

   bool AudioIsRunning() { return gRunning; }
   std::string AudioDeviceName() { return gDeviceName; }

   void AudioSetSmoothing(float attack, float release)
   {
      std::lock_guard<std::mutex> lock(gAudioMutex);
      gAttack = std::min(1.0f, std::max(0.01f, attack));
      gRelease = std::min(1.0f, std::max(0.005f, release));
   }

   void AudioSetGain(float gain)
   {
      std::lock_guard<std::mutex> lock(gAudioMutex);
      gGain = std::max(0.0f, gain);
   }

   bool AudioRead(AudioLevels& out)
   {
      if (!gRunning)
      {
         out = AudioLevels();
         return false;
      }
      std::lock_guard<std::mutex> lock(gAudioMutex);
      out = gLevels;
      gLevels.onset = false; // consume the flag so each onset fires once
      return true;
   }
}

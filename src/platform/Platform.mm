#include "Platform.h"
#include <atomic>

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <AVFoundation/AVFoundation.h>
#import <ModelIO/ModelIO.h>
#import <Vision/Vision.h>
#import <Accelerate/Accelerate.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMIDI/CoreMIDI.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>
// Plugin hosting only: AUAudioUnit's view-controller category and the
// AUGenericViewController fallback both live here, not in AudioToolbox.
#import <CoreAudioKit/CoreAudioKit.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <Syphon/SyphonOpenGLServer.h>
#import <Syphon/SyphonOpenGLClient.h>
#import <Syphon/SyphonServerDirectory.h>
#import <Syphon/SyphonOpenGLImage.h>

#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h> // _NSGetExecutablePath, for ExecutablePath()
#include <cmath>
#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Platform
{
   void PreventAppNap()
   {
      // The returned token must be retained for the activity to stay in
      // effect - letting it deallocate ends the opt-out immediately, so it
      // is stashed in a static rather than a local.
      static id sActivityToken = nil;
      if (sActivityToken != nil)
         return;
      sActivityToken = [[NSProcessInfo processInfo]
         beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                            reason:@"continuous node-graph rendering"];
   }

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

   std::string OpenHdrDialog()
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:YES];
         [panel setCanChooseDirectories:NO];
         [panel setAllowsMultipleSelection:NO];
         [panel setTitle:@"Open HDRI"];

         if (@available(macOS 11.0, *))
         {
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            for (NSString* ext in @[ @"hdr", @"exr" ])
            {
               UTType* t = [UTType typeWithFilenameExtension:ext];
               if (t != nil)
                  [types addObject:t];
            }
            if ([types count] > 0)
               [panel setAllowedContentTypes:types];
         }

         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [[panel URLs] firstObject];
         if (url == nil)
            return std::string();
         return std::string([[url path] UTF8String]);
      }
   }

   bool LoadImageFloatRGB(const std::string& path, std::vector<float>& outPixels,
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
         NSURL* url = [NSURL fileURLWithPath:nsPath];

         CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
         if (source == NULL)
         {
            outError = "not an image this Mac can read";
            return false;
         }
         CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, NULL);
         CFRelease(source);
         if (cgImage == NULL)
         {
            outError = "could not decode image";
            return false;
         }

         const int w = (int)CGImageGetWidth(cgImage);
         const int h = (int)CGImageGetHeight(cgImage);
         if (w <= 0 || h <= 0)
         {
            CGImageRelease(cgImage);
            outError = "empty image";
            return false;
         }

         CGColorSpaceRef linearSpace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
         if (linearSpace == NULL)
         {
            CGImageRelease(cgImage);
            outError = "no extended-range linear colour space available";
            return false;
         }

         std::vector<float> topDown((size_t)w * h * 4, 0.0f);
         CGContextRef ctx = CGBitmapContextCreate(topDown.data(), w, h, 32, (size_t)w * 4 * sizeof(float),
                                                  linearSpace,
                                                  kCGImageAlphaPremultipliedLast | kCGBitmapFloatComponents |
                                                     kCGBitmapByteOrder32Host);
         CGColorSpaceRelease(linearSpace);
         if (ctx == NULL)
         {
            CGImageRelease(cgImage);
            outError = "could not allocate float bitmap";
            return false;
         }

         CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImage);
         CGContextRelease(ctx);
         CGImageRelease(cgImage);

         // Same bottom-up flip as LoadImageRGBA, and drop the alpha channel -
         // it was only there because CGBitmapContext requires one.
         outPixels.assign((size_t)w * h * 3, 0.0f);
         const size_t srcStride = (size_t)w * 4;
         for (int y = 0; y < h; y++)
         {
            const float* srcRow = &topDown[(size_t)(h - 1 - y) * srcStride];
            float* dstRow = &outPixels[(size_t)y * w * 3];
            for (int x = 0; x < w; x++)
            {
               dstRow[x * 3 + 0] = srcRow[x * 4 + 0];
               dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
               dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
            }
         }

         outWidth = w;
         outHeight = h;
         outError.clear();
         return true;
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

   std::string OpenPatchDialog()
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:YES];
         [panel setCanChooseDirectories:NO];
         [panel setAllowsMultipleSelection:NO];
         [panel setTitle:@"Open patch"];
         if (@available(macOS 11.0, *))
         {
            // ".infinite" is accepted too so patches saved by older builds
            // still open - the app now only writes ".inf", but never stops
            // reading its own prior format.
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            UTType* infType = [UTType typeWithIdentifier:@"com.namansoni.infinite.patch"];
            if (infType != nil)
               [types addObject:infType];
            UTType* legacyType = [UTType typeWithFilenameExtension:@"infinite"];
            if (legacyType != nil)
               [types addObject:legacyType];
            if (types.count > 0)
               [panel setAllowedContentTypes:types];
         }
         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [[panel URLs] firstObject];
         return url ? std::string([[url path] UTF8String]) : std::string();
      }
   }

   std::string SavePatchDialog(const std::string& suggestedName)
   {
      @autoreleasepool
      {
         NSSavePanel* panel = [NSSavePanel savePanel];
         [panel setTitle:@"Save patch"];
         [panel setNameFieldStringValue:
            [NSString stringWithUTF8String:suggestedName.empty() ? "Untitled.inf"
                                                                 : suggestedName.c_str()]];
         if (@available(macOS 11.0, *))
         {
            UTType* type = [UTType typeWithIdentifier:@"com.namansoni.infinite.patch"];
            if (type != nil)
               [panel setAllowedContentTypes:@[ type ]];
         }
         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [panel URL];
         return url ? std::string([[url path] UTF8String]) : std::string();
      }
   }

   std::string OpenModelDialog()
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:YES];
         [panel setCanChooseDirectories:NO];
         [panel setAllowsMultipleSelection:NO];
         [panel setTitle:@"Open 3D model"];

         if (@available(macOS 11.0, *))
         {
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            // USD has first-class UTTypes; the rest are matched by extension
            // because macOS declares no universal type for them.
            if (UTTypeUSD != nil) [types addObject:UTTypeUSD];
            if (UTTypeUSDZ != nil) [types addObject:UTTypeUSDZ];
            for (NSString* ext in @[ @"obj", @"ply", @"stl", @"abc" ])
            {
               UTType* t = [UTType typeWithFilenameExtension:ext];
               if (t != nil)
                  [types addObject:t];
            }
            if ([types count] > 0)
               [panel setAllowedContentTypes:types];
         }

         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [[panel URLs] firstObject];
         if (url == nil)
            return std::string();
         return std::string([[url path] UTF8String]);
      }
   }

   namespace
   {
      struct OutlineBuilder
      {
         std::vector<Platform::TextContour>* contours = nullptr;
         Platform::TextContour current;
         float lastX = 0.0f, lastY = 0.0f;
         float startX = 0.0f, startY = 0.0f;
         float offsetX = 0.0f, offsetY = 0.0f;
         float scale = 1.0f;

         void Push(float x, float y)
         {
            current.points.push_back((x + offsetX) * scale);
            current.points.push_back((y + offsetY) * scale);
            lastX = x;
            lastY = y;
         }

         void Close()
         {
            // Under four points is a degenerate contour - two points and a
            // close, say - and would only produce zero-area triangles.
            if (current.points.size() >= 6)
               contours->push_back(current);
            current.points.clear();
         }
      };

      // Fixed subdivision rather than adaptive: glyph curves are short and the
      // step is chosen so the error stays under a pixel at any sane text size.
      const int kCurveSteps = 12;

      void OutlineApply(void* info, const CGPathElement* element)
      {
         OutlineBuilder* b = (OutlineBuilder*)info;
         const CGPoint* p = element->points;
         switch (element->type)
         {
            case kCGPathElementMoveToPoint:
               b->Close();
               b->startX = (float)p[0].x;
               b->startY = (float)p[0].y;
               b->Push((float)p[0].x, (float)p[0].y);
               break;
            case kCGPathElementAddLineToPoint:
               b->Push((float)p[0].x, (float)p[0].y);
               break;
            case kCGPathElementAddQuadCurveToPoint:
            {
               const float x0 = b->lastX, y0 = b->lastY;
               for (int i = 1; i <= kCurveSteps; i++)
               {
                  const float t = (float)i / (float)kCurveSteps;
                  const float u = 1.0f - t;
                  const float x = u*u*x0 + 2*u*t*(float)p[0].x + t*t*(float)p[1].x;
                  const float y = u*u*y0 + 2*u*t*(float)p[0].y + t*t*(float)p[1].y;
                  b->Push(x, y);
               }
               break;
            }
            case kCGPathElementAddCurveToPoint:
            {
               const float x0 = b->lastX, y0 = b->lastY;
               for (int i = 1; i <= kCurveSteps; i++)
               {
                  const float t = (float)i / (float)kCurveSteps;
                  const float u = 1.0f - t;
                  const float x = u*u*u*x0 + 3*u*u*t*(float)p[0].x +
                                  3*u*t*t*(float)p[1].x + t*t*t*(float)p[2].x;
                  const float y = u*u*u*y0 + 3*u*u*t*(float)p[0].y +
                                  3*u*t*t*(float)p[1].y + t*t*t*(float)p[2].y;
                  b->Push(x, y);
               }
               break;
            }
            case kCGPathElementCloseSubpath:
               b->Close();
               break;
         }
      }
   }

   const std::vector<std::string>& AvailableFontFamilies()
   {
      // Enumerated once and cached: the query walks every installed font and is
      // far too slow to run while drawing a params panel each frame.
      static std::vector<std::string> sFamilies;
      static bool sLoaded = false;
      if (sLoaded)
         return sFamilies;
      sLoaded = true;

      @autoreleasepool
      {
         CFArrayRef names = CTFontManagerCopyAvailableFontFamilyNames();
         if (names != nullptr)
         {
            const CFIndex count = CFArrayGetCount(names);
            for (CFIndex i = 0; i < count; i++)
            {
               NSString* name = (__bridge NSString*)CFArrayGetValueAtIndex(names, i);
               // Families beginning with a dot are Apple's internal system
               // faces; they are not meant to be selected by name.
               if (name == nil || [name hasPrefix:@"."])
                  continue;
               sFamilies.push_back(std::string([name UTF8String]));
            }
            CFRelease(names);
         }
      }

      std::sort(sFamilies.begin(), sFamilies.end());
      if (sFamilies.empty())
         sFamilies.push_back("Helvetica");
      return sFamilies;
   }

   bool GetTextOutlines(const std::string& text, const std::string& fontName,
                        float letterSpacing, std::vector<TextContour>& outContours,
                        std::string& outError)
   {
      @autoreleasepool
      {
         outContours.clear();
         if (text.empty())
         {
            outError = "no text";
            return false;
         }

         // A fixed large point size keeps the outlines well away from the font's
         // integer hinting grid; the result is normalised by units-per-em below.
         const CGFloat kPointSize = 256.0;
         NSString* nsFont = fontName.empty()
            ? @"Helvetica"
            : [NSString stringWithUTF8String:fontName.c_str()];

         CTFontRef font = CTFontCreateWithName((__bridge CFStringRef)nsFont, kPointSize, nullptr);
         if (font == nullptr)
            font = CTFontCreateWithName(CFSTR("Helvetica"), kPointSize, nullptr);
         if (font == nullptr)
         {
            outError = "could not create font";
            return false;
         }

         NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
         if (nsText == nil)
         {
            CFRelease(font);
            outError = "text is not valid UTF-8";
            return false;
         }

         // CTLine rather than per-character positioning, so kerning, ligatures
         // and non-Latin shaping come out right instead of being approximated
         // by stacking advances.
         NSDictionary* attributes = @{ (__bridge NSString*)kCTFontAttributeName: (__bridge id)font };
         NSAttributedString* attributed =
            [[NSAttributedString alloc] initWithString:nsText attributes:attributes];
         CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed);
         if (line == nullptr)
         {
            CFRelease(font);
            outError = "could not lay out text";
            return false;
         }

         // Normalise so the design is about one unit tall, matching the scale
         // the primitives use.
         const float scale = 1.0f / (float)CTFontGetCapHeight(font);

         OutlineBuilder builder;
         builder.contours = &outContours;
         builder.scale = scale;

         CFArrayRef runs = CTLineGetGlyphRuns(line);
         const CFIndex runCount = CFArrayGetCount(runs);
         float extraAdvance = 0.0f;

         for (CFIndex r = 0; r < runCount; r++)
         {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
            const CFIndex glyphCount = CTRunGetGlyphCount(run);
            if (glyphCount == 0)
               continue;

            std::vector<CGGlyph> glyphs((size_t)glyphCount);
            std::vector<CGPoint> positions((size_t)glyphCount);
            CTRunGetGlyphs(run, CFRangeMake(0, glyphCount), glyphs.data());
            CTRunGetPositions(run, CFRangeMake(0, glyphCount), positions.data());

            CFDictionaryRef runAttributes = CTRunGetAttributes(run);
            CTFontRef runFont = (CTFontRef)CFDictionaryGetValue(runAttributes, kCTFontAttributeName);
            if (runFont == nullptr)
               runFont = font;

            for (CFIndex g = 0; g < glyphCount; g++)
            {
               CGPathRef path = CTFontCreatePathForGlyph(runFont, glyphs[(size_t)g], nullptr);
               if (path == nullptr)
                  continue; // spaces have no outline

               builder.offsetX = (float)positions[(size_t)g].x + extraAdvance * (float)g;
               builder.offsetY = (float)positions[(size_t)g].y;
               CGPathApply(path, &builder, OutlineApply);
               builder.Close();
               CGPathRelease(path);
            }
            extraAdvance += letterSpacing * (float)CTFontGetSize(runFont);
         }

         CFRelease(line);
         CFRelease(font);

         if (outContours.empty())
         {
            outError = "text produced no outlines";
            return false;
         }

         // Centre horizontally and vertically on the origin so rotation and
         // scaling behave predictably.
         float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
         for (const TextContour& c : outContours)
         {
            for (size_t i = 0; i + 1 < c.points.size(); i += 2)
            {
               lo[0] = std::min(lo[0], c.points[i]);
               hi[0] = std::max(hi[0], c.points[i]);
               lo[1] = std::min(lo[1], c.points[i + 1]);
               hi[1] = std::max(hi[1], c.points[i + 1]);
            }
         }
         const float cx = (lo[0] + hi[0]) * 0.5f, cy = (lo[1] + hi[1]) * 0.5f;
         for (TextContour& c : outContours)
         {
            for (size_t i = 0; i + 1 < c.points.size(); i += 2)
            {
               c.points[i] -= cx;
               c.points[i + 1] -= cy;
            }
         }
         return true;
      }
   }

   bool LoadModel(const std::string& path, std::vector<ModelVertex>& outVertices,
                  std::vector<unsigned int>& outIndices, std::string& outError)
   {
      @autoreleasepool
      {
         outVertices.clear();
         outIndices.clear();

         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         NSURL* url = [NSURL fileURLWithPath:nsPath];
         if (![[NSFileManager defaultManager] fileExistsAtPath:nsPath])
         {
            outError = "file not found";
            return false;
         }

         // Ask ModelIO for exactly the layout core/Mesh.h uses, so the vertex
         // data can be memcpy'd out rather than converted attribute by attribute.
         MDLVertexDescriptor* descriptor = [[MDLVertexDescriptor alloc] init];
         descriptor.attributes[0] = [[MDLVertexAttribute alloc]
            initWithName:MDLVertexAttributePosition format:MDLVertexFormatFloat3 offset:0 bufferIndex:0];
         descriptor.attributes[1] = [[MDLVertexAttribute alloc]
            initWithName:MDLVertexAttributeNormal format:MDLVertexFormatFloat3 offset:12 bufferIndex:0];
         descriptor.attributes[2] = [[MDLVertexAttribute alloc]
            initWithName:MDLVertexAttributeTextureCoordinate format:MDLVertexFormatFloat2 offset:24 bufferIndex:0];
         descriptor.layouts[0] = [[MDLVertexBufferLayout alloc] initWithStride:32];

         NSError* error = nil;
         MDLAsset* asset = [[MDLAsset alloc] initWithURL:url
                                       vertexDescriptor:descriptor
                                        bufferAllocator:nil
                                       preserveTopology:NO
                                                  error:&error];
         if (asset == nil)
         {
            outError = error != nil ? std::string([[error localizedDescription] UTF8String])
                                    : std::string("could not read model");
            return false;
         }

         // Flatten the scene graph: a file can hold many meshes under nested
         // transforms, and the graph here has no concept of an object hierarchy.
         NSArray<MDLObject*>* meshes = [asset childObjectsOfClass:[MDLMesh class]];
         if ([meshes count] == 0)
         {
            outError = "file contains no mesh";
            return false;
         }

         for (MDLObject* object in meshes)
         {
            MDLMesh* mesh = (MDLMesh*)object;

            // Files often ship without normals - most STLs, plenty of OBJs -
            // and without these the surface renders unlit and flat black.
            if ([mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributeNormal] == nil)
               [mesh addNormalsWithAttributeNamed:MDLVertexAttributeNormal creaseThreshold:0.5f];
            if ([mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributeTextureCoordinate] == nil)
            {
               // Wrapped in a try: the generator throws on degenerate geometry,
               // and a model without UVs is still worth showing.
               @try
               {
                  [mesh addUnwrappedTextureCoordinatesForAttributeNamed:MDLVertexAttributeTextureCoordinate];
               }
               @catch (NSException*) {}
            }

            MDLVertexAttributeData* positions =
               [mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributePosition];
            MDLVertexAttributeData* normals =
               [mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributeNormal];
            MDLVertexAttributeData* uvs =
               [mesh vertexAttributeDataForAttributeNamed:MDLVertexAttributeTextureCoordinate];
            if (positions == nil)
               continue;

            const unsigned int base = (unsigned int)outVertices.size();
            const NSUInteger vertexCount = mesh.vertexCount;
            for (NSUInteger i = 0; i < vertexCount; i++)
            {
               ModelVertex v;
               const char* p = (const char*)positions.dataStart + i * positions.stride;
               std::memcpy(&v.px, p, sizeof(float) * 3);
               if (normals != nil)
               {
                  const char* n = (const char*)normals.dataStart + i * normals.stride;
                  std::memcpy(&v.nx, n, sizeof(float) * 3);
               }
               if (uvs != nil)
               {
                  const char* t = (const char*)uvs.dataStart + i * uvs.stride;
                  std::memcpy(&v.u, t, sizeof(float) * 2);
               }
               outVertices.push_back(v);
            }

            for (MDLSubmesh* submesh in mesh.submeshes)
            {
               // preserveTopology:NO above asks ModelIO to triangulate, but a
               // submesh can still arrive as something else; anything that is
               // not triangles is skipped rather than read as garbage indices.
               if (submesh.geometryType != MDLGeometryTypeTriangles)
                  continue;

               const NSUInteger count = submesh.indexCount;
               const void* raw = submesh.indexBuffer.map.bytes;
               if (raw == nullptr)
                  continue;

               switch (submesh.indexType)
               {
                  case MDLIndexBitDepthUInt32:
                  case MDLIndexBitDepthInvalid:
                     for (NSUInteger i = 0; i < count; i++)
                        outIndices.push_back(base + ((const uint32_t*)raw)[i]);
                     break;
                  case MDLIndexBitDepthUInt16:
                     for (NSUInteger i = 0; i < count; i++)
                        outIndices.push_back(base + ((const uint16_t*)raw)[i]);
                     break;
                  case MDLIndexBitDepthUInt8:
                     for (NSUInteger i = 0; i < count; i++)
                        outIndices.push_back(base + ((const uint8_t*)raw)[i]);
                     break;
               }
            }
         }

         if (outVertices.empty() || outIndices.empty())
         {
            outError = "model produced no triangles";
            return false;
         }
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

      // Audio, all optional - nil/zero when the recording is video-only.
      AVAssetWriterInput* audioInput = nil;
      AVAudioFile* audioFile = nil;
      AVAudioFormat* audioFormat = nil;
      AVAudioPCMBuffer* audioScratch = nil; // reused read buffer
      double audioSampleRate = 44100.0;
      int64_t audioFramesWritten = 0;
      bool audioLoop = true;
      bool audioExhausted = false; // file ended and looping is off
   };

   namespace
   {
      // AVAssetWriterInput can accept linear-PCM CMSampleBuffers even when its
      // outputSettings ask for compressed AAC - it transcodes internally, the
      // same mechanism a microphone-to-.m4a recorder relies on. This is what
      // makes muxing an arbitrary source file painless: no encoder to drive by
      // hand, just PCM in.
      CMSampleBufferRef PCMBufferToSampleBuffer(AVAudioPCMBuffer* buffer, CMTime presentationTime)
      {
         if (buffer == nil || buffer.frameLength == 0)
            return NULL;

         CMFormatDescriptionRef formatDesc = NULL;
         OSStatus status = CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault, buffer.format.streamDescription, 0, NULL, 0, NULL, NULL, &formatDesc);
         if (status != noErr || formatDesc == NULL)
            return NULL;

         CMSampleTimingInfo timing = {
            .duration = CMTimeMake(1, (int32_t)buffer.format.sampleRate),
            .presentationTimeStamp = presentationTime,
            .decodeTimeStamp = kCMTimeInvalid
         };

         CMSampleBufferRef sampleBuffer = NULL;
         status = CMSampleBufferCreate(kCFAllocatorDefault, NULL, false, NULL, NULL, formatDesc,
                                       (CMItemCount)buffer.frameLength, 1, &timing, 0, NULL, &sampleBuffer);
         CFRelease(formatDesc);
         if (status != noErr || sampleBuffer == NULL)
            return NULL;

         status = CMSampleBufferSetDataBufferFromAudioBufferList(
            sampleBuffer, kCFAllocatorDefault, kCFAllocatorDefault, 0, buffer.audioBufferList);
         if (status != noErr)
         {
            CFRelease(sampleBuffer);
            return NULL;
         }
         return sampleBuffer;
      }

      // Reads and appends whatever audio is needed to catch up to
      // targetSampleFrames, looping or stopping at the source's end per
      // audioLoop. Called once per video frame, so it never has more than a
      // fraction of a second to make up.
      void AppendAudioUpTo(RecorderHandle* h, int64_t targetSampleFrames)
      {
         if (h->audioInput == nil || h->audioExhausted)
            return;

         while (h->audioFramesWritten < targetSampleFrames)
         {
            if (!h->audioInput.isReadyForMoreMediaData)
               return; // try again on the next video frame

            AVAudioFramePosition remaining = h->audioFile.length - h->audioFile.framePosition;
            if (remaining <= 0)
            {
               if (!h->audioLoop)
               {
                  h->audioExhausted = true;
                  return;
               }
               h->audioFile.framePosition = 0;
               remaining = h->audioFile.length;
            }
            if (remaining <= 0)
               return; // a zero-length file: nothing to loop either

            const AVAudioFrameCount wanted = (AVAudioFrameCount)std::min<int64_t>(
               4096, targetSampleFrames - h->audioFramesWritten);
            const AVAudioFrameCount toRead =
               (AVAudioFrameCount)std::min<AVAudioFramePosition>(wanted, remaining);

            if (h->audioScratch == nil)
               h->audioScratch = [[AVAudioPCMBuffer alloc] initWithPCMFormat:h->audioFormat
                                                              frameCapacity:4096];
            h->audioScratch.frameLength = 0;

            NSError* err = nil;
            if (![h->audioFile readIntoBuffer:h->audioScratch frameCount:toRead error:&err] ||
                h->audioScratch.frameLength == 0)
               return;

            CMTime pts = CMTimeMake(h->audioFramesWritten, (int32_t)h->audioSampleRate);
            CMSampleBufferRef sb = PCMBufferToSampleBuffer(h->audioScratch, pts);
            if (sb != NULL)
            {
               [h->audioInput appendSampleBuffer:sb];
               CFRelease(sb);
            }
            h->audioFramesWritten += h->audioScratch.frameLength;
         }
      }
   }

   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError,
                                 const std::string& audioPath, bool loopAudio,
                                 double liveAudioSampleRate, int liveAudioChannels)
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

         // Audio has to be resolved and added *before* startWriting: an
         // AVAssetWriter refuses addInput: once writing has begun, so building
         // this after the video's startWriting call (as an earlier version of
         // this function did) silently produced a video-only file every time.
         AVAssetWriterInput* audioInput = nil;
         AVAudioFile* audioFile = nil;
         AVAudioFormat* audioFormat = nil;
         double audioSampleRate = 44100.0;

         if (!audioPath.empty())
         {
            NSString* audioNsPath = [NSString stringWithUTF8String:audioPath.c_str()];
            NSURL* audioUrl = [NSURL fileURLWithPath:audioNsPath];
            NSError* audioErr = nil;
            audioFile = [[AVAudioFile alloc] initForReading:audioUrl error:&audioErr];
            if (audioFile == nil)
            {
               // Video-only rather than failing the whole recording: a bad
               // audio source should not cost the user the video they came for.
               fprintf(stderr, "recorder: could not open audio '%s': %s\n", audioPath.c_str(),
                      audioErr ? [[audioErr localizedDescription] UTF8String] : "unknown error");
            }
            else
            {
               NSDictionary* audioSettings = @{
                  AVFormatIDKey         : @(kAudioFormatMPEG4AAC),
                  AVSampleRateKey       : @(audioFile.processingFormat.sampleRate),
                  AVNumberOfChannelsKey : @(std::min((unsigned int)2, audioFile.processingFormat.channelCount)),
                  AVEncoderBitRateKey   : @(160000)
               };
               audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                                                               outputSettings:audioSettings];
               audioInput.expectsMediaDataInRealTime = NO;

               if ([writer canAddInput:audioInput])
               {
                  [writer addInput:audioInput];
                  audioSampleRate = audioFile.processingFormat.sampleRate;
                  audioFormat = audioFile.processingFormat;
               }
               else
               {
                  fprintf(stderr, "recorder: could not add audio track, continuing video-only\n");
                  audioInput = nil;
                  audioFile = nil;
               }
            }
         }
         else if (liveAudioSampleRate > 0.0)
         {
            const int ch = std::max(1, std::min(2, liveAudioChannels));
            NSDictionary* audioSettings = @{
               AVFormatIDKey         : @(kAudioFormatMPEG4AAC),
               AVSampleRateKey       : @(liveAudioSampleRate),
               AVNumberOfChannelsKey : @(ch),
               AVEncoderBitRateKey   : @(160000)
            };
            audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                                                            outputSettings:audioSettings];
            audioInput.expectsMediaDataInRealTime = NO;

            if ([writer canAddInput:audioInput])
            {
               [writer addInput:audioInput];
               audioSampleRate = liveAudioSampleRate;
               audioFormat = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:liveAudioSampleRate
                                                                            channels:(AVAudioChannelCount)ch];
            }
            else
            {
               fprintf(stderr, "recorder: could not add live audio track, continuing video-only\n");
               audioInput = nil;
            }
         }

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
         h->audioLoop = loopAudio;
         h->audioInput = audioInput;
         h->audioFile = audioFile;
         h->audioFormat = audioFormat;
         h->audioSampleRate = audioSampleRate;

         return h;
      }
   }

   bool RecorderAppendAudio(RecorderHandle* h, const float* interleavedSamples, int numFrames)
   {
      if (h == nullptr || h->audioInput == nil || interleavedSamples == nullptr || numFrames <= 0)
         return false;

      @autoreleasepool
      {
         if (h->audioScratch == nil || h->audioScratch.frameCapacity < (AVAudioFrameCount)numFrames)
         {
            h->audioScratch = [[AVAudioPCMBuffer alloc] initWithPCMFormat:h->audioFormat
                                                           frameCapacity:(AVAudioFrameCount)std::max(4096, numFrames)];
         }

         h->audioScratch.frameLength = (AVAudioFrameCount)numFrames;
         float* left = h->audioScratch.floatChannelData[0];
         float* right = h->audioFormat.channelCount > 1 ? h->audioScratch.floatChannelData[1] : nullptr;

         if (right != nullptr)
         {
            for (int i = 0; i < numFrames; i++)
            {
               left[i] = interleavedSamples[i * 2 + 0];
               right[i] = interleavedSamples[i * 2 + 1];
            }
         }
         else
         {
            for (int i = 0; i < numFrames; i++)
               left[i] = interleavedSamples[i * 2 + 0];
         }

         CMTime pts = CMTimeMake(h->audioFramesWritten, (int32_t)h->audioSampleRate);
         CMSampleBufferRef sb = PCMBufferToSampleBuffer(h->audioScratch, pts);
         if (sb != NULL)
         {
            [h->audioInput appendSampleBuffer:sb];
            CFRelease(sb);
         }
         h->audioFramesWritten += numFrames;
         return true;
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

         if (ok && handle->audioInput != nil)
         {
            // Catches audio up to the end of the video frame just written, so
            // the two tracks cannot drift apart by more than one video frame.
            const int64_t targetFrames =
               (int64_t)((double)handle->frameIndex / (double)handle->fps * handle->audioSampleRate);
            AppendAudioUpTo(handle, targetFrames);
         }
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
         if (handle->audioInput != nil)
            [handle->audioInput markAsFinished];
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

   MovieInfo InspectMovie(const std::string& path)
   {
      MovieInfo info;
      @autoreleasepool
      {
         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:nsPath] options:nil];
         info.hasVideo = [asset tracksWithMediaType:AVMediaTypeVideo].count > 0;
         info.hasAudio = [asset tracksWithMediaType:AVMediaTypeAudio].count > 0;
         info.duration = CMTimeGetSeconds(asset.duration);
      }
      return info;
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

      FFTSetup gFftSetup = nullptr;
      std::vector<float> gWindow;

      void EnsureFftSetup()
      {
         if (gFftSetup != nullptr)
            return;
         gFftSetup = vDSP_create_fftsetup(kFftLog2, FFT_RADIX2);
         gWindow.resize(kFftSize);
         vDSP_hann_window(gWindow.data(), kFftSize, vDSP_HANN_NORM);
      }

      // One analyser per audio source. The live input owns one; every file
      // player owns its own, so several sources can be analysed at once.
      struct Analyser
      {
         std::mutex mutex;
         AudioLevels levels;
         float attack = 0.5f;
         float release = 0.12f;
         float gain = 1.0f;
         float prevFlux = 0.0f;
         std::vector<float> prevMagnitude;
         std::vector<float> ring;

         float Smooth(float previous, float target) const
         {
            const float rate = (target > previous) ? attack : release;
            return previous + (target - previous) * rate;
         }
      };

      void ProcessInto(Analyser& a, const float* samples, int frameCount, double sampleRate)
      {
         if (frameCount <= 0 || samples == nullptr)
            return;
         EnsureFftSetup();

         float gain;
         {
            std::lock_guard<std::mutex> lock(a.mutex);
            gain = a.gain;
         }

         // Keep a rolling window so the FFT always sees a full frame even when
         // CoreAudio hands us short buffers.
         a.ring.insert(a.ring.end(), samples, samples + frameCount);
         if ((int)a.ring.size() < kFftSize)
            return;
         if ((int)a.ring.size() > kFftSize * 4)
            a.ring.erase(a.ring.begin(), a.ring.end() - kFftSize);

         const float* frame = a.ring.data() + (a.ring.size() - kFftSize);

         float rms = 0.0f, peak = 0.0f;
         for (int i = 0; i < kFftSize; i++)
         {
            const float v = frame[i] * gain;
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
            magnitude[i] *= norm * gain;

         if (a.prevMagnitude.size() != magnitude.size())
            a.prevMagnitude.assign(magnitude.size(), 0.0f);

         // Spectral flux: sum of positive frame-to-frame change, the standard
         // cheap onset detector.
         float flux = 0.0f;
         for (int i = 0; i < kSpectrumSize; i++)
            flux += std::max(0.0f, magnitude[i] - a.prevMagnitude[i]);
         const bool onset = flux > a.prevFlux * 1.6f && flux > 0.02f;
         a.prevFlux = a.prevFlux * 0.7f + flux * 0.3f;
         a.prevMagnitude = magnitude;

         const double nyquist = sampleRate * 0.5;
         auto rangeEnergy = [&](double fromHz, double toHz) {
            const int lo = std::max(1, (int)(fromHz / nyquist * kSpectrumSize));
            const int hi = std::min(kSpectrumSize - 1, (int)(toHz / nyquist * kSpectrumSize));
            float sum = 0.0f; int count = 0;
            for (int i = lo; i <= hi; i++) { sum += magnitude[i]; count++; }
            return count > 0 ? sum / count : 0.0f;
         };

         // Log-spaced bands: linear bins would put almost everything in band 0.
         float bands[kAudioBands] = { 0 };
         for (int b = 0; b < kAudioBands; b++)
         {
            const double loHz = 20.0 * std::pow(nyquist / 20.0, (double)b / kAudioBands);
            const double hiHz = 20.0 * std::pow(nyquist / 20.0, (double)(b + 1) / kAudioBands);
            bands[b] = rangeEnergy(loHz, hiHz);
         }

         // Magnitudes are tiny; a compressive curve maps them into a usable 0..1.
         auto shape = [](float v) { return std::min(1.0f, std::sqrt(v * 12.0f)); };

         std::lock_guard<std::mutex> lock(a.mutex);
         a.levels.rms = a.Smooth(a.levels.rms, std::min(1.0f, rms * 3.0f));
         a.levels.peak = a.Smooth(a.levels.peak, std::min(1.0f, peak));
         a.levels.low = a.Smooth(a.levels.low, shape(rangeEnergy(20.0, 250.0)));
         a.levels.mid = a.Smooth(a.levels.mid, shape(rangeEnergy(250.0, 2000.0)));
         a.levels.high = a.Smooth(a.levels.high, shape(rangeEnergy(2000.0, 16000.0)));
         for (int b = 0; b < kAudioBands; b++)
            a.levels.bands[b] = a.Smooth(a.levels.bands[b], shape(bands[b]));
         if (onset)
            a.levels.onset = true;
      }

      bool ReadFrom(Analyser& a, AudioLevels& out)
      {
         std::lock_guard<std::mutex> lock(a.mutex);
         out = a.levels;
         a.levels.onset = false; // consume the flag so each onset fires once
         return true;
      }

      AVAudioEngine* gEngine = nil;
      bool gRunning = false;
      std::string gDeviceName;
      Analyser gLiveAnalyser;
   }

   bool AudioStart(std::string& outError)
   {
      if (gRunning)
         return true;

      @autoreleasepool
      {
         EnsureFftSetup();
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

         [input installTapOnBus:0 bufferSize:1024 format:format
                          block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
            const float* const* channels = buffer.floatChannelData;
            if (channels != nullptr)
               ProcessInto(gLiveAnalyser, channels[0], (int)buffer.frameLength, sampleRate);
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
      std::lock_guard<std::mutex> lock(gLiveAnalyser.mutex);
      gLiveAnalyser.levels = AudioLevels();
   }

   bool AudioIsRunning() { return gRunning; }
   std::string AudioDeviceName() { return gDeviceName; }

   void AudioSetSmoothing(float attack, float release)
   {
      std::lock_guard<std::mutex> lock(gLiveAnalyser.mutex);
      gLiveAnalyser.attack = std::min(1.0f, std::max(0.01f, attack));
      gLiveAnalyser.release = std::min(1.0f, std::max(0.005f, release));
   }

   void AudioSetGain(float gain)
   {
      std::lock_guard<std::mutex> lock(gLiveAnalyser.mutex);
      gLiveAnalyser.gain = std::max(0.0f, gain);
   }

   bool AudioRead(AudioLevels& out)
   {
      if (!gRunning)
      {
         out = AudioLevels();
         return false;
      }
      return ReadFrom(gLiveAnalyser, out);
   }

   // ------------------------------------------------- synthesis spike (P0)

   struct AudioSpikeHandle
   {
      AVAudioEngine* engine = nil;
      AVAudioSourceNode* source = nil;
      double phase = 0.0;
      double sampleRate = 44100.0;

      std::atomic<double> sampleRateOut { 0.0 };
      std::atomic<int> blockSize { 0 };
      std::atomic<double> maxJitterMs { 0.0 };
      std::atomic<uint64_t> callbackCount { 0 };
      std::atomic<double> lastEntryMs { -1.0 };
   };

   namespace
   {
      AudioSpikeHandle* gSpikeHandle = nullptr;

      double NowMs()
      {
         using namespace std::chrono;
         return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
      }
   }

   bool AudioSpikeStart(std::string& outError)
   {
      if (gSpikeHandle != nullptr)
         return true;

      @autoreleasepool
      {
         AudioSpikeHandle* h = new AudioSpikeHandle();
         h->engine = [[AVAudioEngine alloc] init];
         AVAudioMixerNode* mixer = [h->engine mainMixerNode];
         AVAudioFormat* format = [mixer outputFormatForBus:0];
         if (format == nil || format.sampleRate <= 0)
            format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100.0 channels:2];

         h->sampleRate = format.sampleRate;
         const double sampleRate = h->sampleRate;
         AudioSpikeHandle* raw = h;

         h->source = [[AVAudioSourceNode alloc]
             initWithFormat:format
             renderBlock:^OSStatus(BOOL* isSilence, const AudioTimeStamp* timestamp,
                                    AVAudioFrameCount frameCount, AudioBufferList* outputData) {
            const double nowMs = NowMs();
            const double lastMs = raw->lastEntryMs.load(std::memory_order_relaxed);
            if (lastMs >= 0.0)
            {
               const double expectedGapMs = 1000.0 * (double)frameCount / sampleRate;
               const double actualGapMs = nowMs - lastMs;
               const double jitterMs = std::fabs(actualGapMs - expectedGapMs);
               double prevMax = raw->maxJitterMs.load(std::memory_order_relaxed);
               if (jitterMs > prevMax)
                  raw->maxJitterMs.store(jitterMs, std::memory_order_relaxed);
            }
            raw->lastEntryMs.store(nowMs, std::memory_order_relaxed);
            raw->blockSize.store((int)frameCount, std::memory_order_relaxed);
            raw->callbackCount.fetch_add(1, std::memory_order_relaxed);

            *isSilence = NO;
            const double phaseStep = 2.0 * M_PI * 440.0 / sampleRate;
            double phase = raw->phase;
            for (UInt32 ch = 0; ch < outputData->mNumberBuffers; ++ch)
            {
               float* buf = (float*)outputData->mBuffers[ch].mData;
               double p = phase;
               for (AVAudioFrameCount i = 0; i < frameCount; ++i)
               {
                  buf[i] = (float)(0.2 * sin(p));
                  p += phaseStep;
               }
            }
            phase += phaseStep * frameCount;
            phase = fmod(phase, 2.0 * M_PI);
            raw->phase = phase;

            return noErr;
         }];

         [h->engine attachNode:h->source];
         [h->engine connect:h->source to:mixer format:format];

         NSError* err = nil;
         [h->engine prepare];
         if (![h->engine startAndReturnError:&err])
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "audio spike engine failed to start";
            [h->engine detachNode:h->source];
            delete h;
            return false;
         }

         h->sampleRateOut.store(sampleRate, std::memory_order_relaxed);
         gSpikeHandle = h;
         outError.clear();
         return true;
      }
   }

   void AudioSpikeStop()
   {
      if (gSpikeHandle == nullptr)
         return;
      AudioSpikeHandle* h = gSpikeHandle;
      gSpikeHandle = nullptr;
      @autoreleasepool
      {
         // Same teardown hazard as AudioFileClose above: AVAudioEngine throws
         // an NSException on bad teardown state instead of returning an
         // error, and an uncaught one aborts the process.
         @try
         {
            [h->engine stop];
            [h->engine detachNode:h->source];
         }
         @catch (NSException* exception)
         {
            fprintf(stderr, "audio spike teardown: %s\n",
                    [[exception reason] UTF8String] ? [[exception reason] UTF8String] : "unknown");
         }
         h->source = nil;
         h->engine = nil;
      }
      delete h;
   }

   AudioSpikeStats AudioSpikeGetStats()
   {
      AudioSpikeStats stats;
      if (gSpikeHandle == nullptr)
         return stats;
      stats.sampleRate = gSpikeHandle->sampleRateOut.load(std::memory_order_relaxed);
      stats.blockSize = gSpikeHandle->blockSize.load(std::memory_order_relaxed);
      stats.maxJitterMs = gSpikeHandle->maxJitterMs.load(std::memory_order_relaxed);
      stats.callbackCount = gSpikeHandle->callbackCount.load(std::memory_order_relaxed);
      return stats;
   }

   // ------------------------------------------------- engine render bridge

   struct AudioDeviceHandle
   {
      AVAudioEngine* engine = nil;
      AVAudioSourceNode* source = nil;
      // Bound to this specific `engine` object, so it must be removed before
      // the engine is released (AudioDeviceClose) - a token left registered
      // against a freed AVAudioEngine is exactly the dangling-observer crash
      // this recovery work is supposed to prevent, not introduce. See
      // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md
      // rule 3.
      id configChangeObserver = nil;
   };

   namespace
   {
      AudioDeviceHandle* gDeviceHandle = nullptr;
      AudioRenderCallback gDeviceCallback = nullptr;
      void* gDeviceUserData = nullptr;

      // AVAudioSourceNode hands us AudioBufferList, not a float**; cap the
      // channel count so unpacking it into the C-callback shape needs no
      // heap allocation on the audio thread.
      constexpr int kMaxDeviceChannels = 8;

      // ---- device-change / sleep-wake recovery flags ----
      // Set from whatever thread the corresponding notification lands on;
      // consumed (cleared) by main.cpp's per-frame PollAudioRecovery via the
      // accessors below. See Platform.h's doc comment on
      // AudioDeviceConfigDidChange for why this is a flag and not a
      // callback into graph code.
      std::atomic<bool> gAudioConfigChanged { false };
      std::atomic<bool> gAudioWillSleep { false };
      std::atomic<bool> gAudioDidWake { false };

      // NSWorkspace's sleep/wake notifications aren't tied to any particular
      // AVAudioEngine instance (unlike the per-engine config-change
      // observer above) - they matter for the whole process, so they're
      // installed once, lazily, the first time an audio device is opened,
      // and never removed. That's a deliberate asymmetry with
      // AudioDeviceHandle::configChangeObserver, not an oversight: there is
      // no "this object is about to be freed" moment for the process itself
      // to hang the removal off of, and NSWorkspace's own notification
      // center outlives everything else in this program regardless.
      bool gWorkspaceObserversInstalled = false;

      void InstallWorkspaceObserversOnce()
      {
         if (gWorkspaceObserversInstalled)
            return;
         gWorkspaceObserversInstalled = true;

         NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
         [center addObserverForName:NSWorkspaceWillSleepNotification
                              object:nil
                               queue:nil
                          usingBlock:^(NSNotification*) {
            gAudioWillSleep.store(true, std::memory_order_relaxed);
         }];
         [center addObserverForName:NSWorkspaceDidWakeNotification
                              object:nil
                               queue:nil
                          usingBlock:^(NSNotification*) {
            gAudioDidWake.store(true, std::memory_order_relaxed);
         }];
      }
   }

   bool AudioDeviceConfigDidChange() { return gAudioConfigChanged.exchange(false, std::memory_order_relaxed); }
   bool AudioWillSleep() { return gAudioWillSleep.exchange(false, std::memory_order_relaxed); }
   bool AudioDidWake() { return gAudioDidWake.exchange(false, std::memory_order_relaxed); }

   void AudioDeviceDebugSimulateConfigChange()
   {
      gAudioConfigChanged.store(true, std::memory_order_relaxed);
   }

   namespace
   {
   // Returns true (and > 0 channels on the given scope) if the device
   // participates in that scope at all - a device with 0 input streams is
   // output-only and vice versa. Used both by AudioListDevices to classify
   // each device and to size AudioObjectGetPropertyDataSize's buffer.
   bool DeviceHasScope(AudioObjectID deviceId, AudioObjectPropertyScope scope, uint32_t& outChannels)
   {
      outChannels = 0;
      AudioObjectPropertyAddress addr {
         kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain
      };
      UInt32 dataSize = 0;
      if (AudioObjectGetPropertyDataSize(deviceId, &addr, 0, nullptr, &dataSize) != noErr || dataSize == 0)
         return false;

      std::vector<uint8_t> buf(dataSize);
      AudioBufferList* list = reinterpret_cast<AudioBufferList*>(buf.data());
      if (AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &dataSize, list) != noErr)
         return false;

      uint32_t channels = 0;
      for (UInt32 i = 0; i < list->mNumberBuffers; i++)
         channels += list->mBuffers[i].mNumberChannels;
      outChannels = channels;
      return channels > 0;
   }

   std::string DeviceName(AudioObjectID deviceId)
   {
      AudioObjectPropertyAddress addr {
         kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
      };
      CFStringRef name = nullptr;
      UInt32 size = sizeof(name);
      if (AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &size, &name) != noErr || name == nullptr)
         return "Unknown device";

      char buf[256] = {};
      CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
      CFRelease(name);
      return std::string(buf);
   }
   }

   std::vector<AudioDeviceInfo> AudioListDevices()
   {
      std::vector<AudioDeviceInfo> result;

      AudioObjectPropertyAddress addr {
         kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
      };
      UInt32 dataSize = 0;
      if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &dataSize) != noErr)
         return result;

      const size_t count = dataSize / sizeof(AudioObjectID);
      std::vector<AudioObjectID> deviceIds(count);
      if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &dataSize, deviceIds.data()) != noErr)
         return result;

      for (AudioObjectID deviceId : deviceIds)
      {
         uint32_t inChannels = 0, outChannels = 0;
         const bool isInput = DeviceHasScope(deviceId, kAudioObjectPropertyScopeInput, inChannels);
         const bool isOutput = DeviceHasScope(deviceId, kAudioObjectPropertyScopeOutput, outChannels);
         if (!isInput && !isOutput)
            continue;

         AudioDeviceInfo info;
         info.name = DeviceName(deviceId);
         info.deviceId = (uint32_t)deviceId;
         info.isInput = isInput;
         info.isOutput = isOutput;
         result.push_back(std::move(info));
      }

      return result;
   }

   uint32_t AudioDeviceBufferFrames(uint32_t deviceId)
   {
      AudioObjectID target = (AudioObjectID)deviceId;
      if (target == 0)
      {
         AudioObjectPropertyAddress defaultAddr {
            kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
         };
         UInt32 idSize = sizeof(target);
         if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &idSize, &target) != noErr)
            return 0;
      }

      AudioObjectPropertyAddress bufferAddr {
         kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
      };
      UInt32 frames = 0;
      UInt32 size = sizeof(frames);
      if (AudioObjectGetPropertyData(target, &bufferAddr, 0, nullptr, &size, &frames) != noErr)
         return 0;
      return (uint32_t)frames;
   }

   // ------------------------------------------- input capture (Audio In node)
   namespace
   {
      // Lock-free single-producer (the tap block, called on whatever thread
      // AVAudioEngine services its input node from)/single-consumer (the
      // device's real-time render thread, inside AudioCaptureNode::ProcessBlock)
      // ring - same head/tail-mod-capacity shape as MeterRing, just sized for
      // raw waveform rather than decimated meter data. One per channel; a mono
      // input device writes the same sample into both so a consumer can always
      // ask for stereo.
      constexpr size_t kInputRingCapacity = 1 << 15; // ~0.74s at 44.1kHz - generous cushion, cheap to hold
      struct InputRing
      {
         float entries[kInputRingCapacity] {};
         std::atomic<size_t> head { 0 };
         std::atomic<size_t> tail { 0 };

         void Write(const float* samples, int count)
         {
            size_t t = tail.load(std::memory_order_relaxed);
            const size_t h = head.load(std::memory_order_acquire);
            for (int i = 0; i < count; i++)
            {
               const size_t next = (t + 1) % kInputRingCapacity;
               if (next == h)
                  break; // full: consumer isn't draining fast enough, drop the rest
               entries[t] = samples[i];
               t = next;
            }
            tail.store(t, std::memory_order_release);
         }

         int Read(float* out, int maxCount)
         {
            size_t h = head.load(std::memory_order_relaxed);
            const size_t t = tail.load(std::memory_order_acquire);
            int n = 0;
            while (h != t && n < maxCount)
            {
               out[n++] = entries[h];
               h = (h + 1) % kInputRingCapacity;
            }
            head.store(h, std::memory_order_release);
            return n;
         }

         void Clear()
         {
            head.store(0, std::memory_order_relaxed);
            tail.store(0, std::memory_order_relaxed);
         }
      };

      InputRing gInputRing[2];
      int gInputCaptureWantCount = 0; // how many AudioInputNode instances are alive
      bool gInputTapInstalled = false; // whether the tap is actually live on the CURRENT engine
   }

   void AudioInputCaptureAddRef() { gInputCaptureWantCount++; }

   void AudioInputCaptureRemoveRef()
   {
      if (gInputCaptureWantCount > 0)
         gInputCaptureWantCount--;
   }

   void AudioInputCapturePump(std::string& outError)
   {
      if (gInputCaptureWantCount <= 0)
      {
         if (gInputTapInstalled && gDeviceHandle != nullptr)
         {
            @autoreleasepool
            {
               [[gDeviceHandle->engine inputNode] removeTapOnBus:0];
            }
         }
         gInputTapInstalled = false;
         return;
      }

      if (gInputTapInstalled)
         return; // already live on the current engine - nothing to do

      if (gDeviceHandle == nullptr)
      {
         outError = "output device not open yet";
         return;
      }

      @autoreleasepool
      {
         AVAudioInputNode* input = [gDeviceHandle->engine inputNode];
         AVAudioFormat* format = [input inputFormatForBus:0];
         if (format == nil || format.sampleRate <= 0 || format.channelCount == 0)
         {
            outError = "no audio input available (check System Settings > Privacy > Microphone)";
            return;
         }

         gInputRing[0].Clear();
         gInputRing[1].Clear();

         [input installTapOnBus:0 bufferSize:1024 format:format
                          block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
            const float* const* channels = buffer.floatChannelData;
            if (channels == nullptr)
               return;
            const int numFrames = (int)buffer.frameLength;
            gInputRing[0].Write(channels[0], numFrames);
            gInputRing[1].Write(buffer.format.channelCount > 1 ? channels[1] : channels[0], numFrames);
         }];

         gInputTapInstalled = true;
         outError.clear();
      }
   }

   bool AudioInputCaptureIsRunning() { return gInputTapInstalled; }

   int AudioInputCaptureRead(float* const* outChannels, int numFrames, int maxChannels)
   {
      if (!gInputTapInstalled)
         return 0;

      const int channels = std::min(maxChannels, 2);
      for (int ch = 0; ch < channels; ch++)
      {
         const int got = gInputRing[ch].Read(outChannels[ch], numFrames);
         for (int i = got; i < numFrames; i++)
            outChannels[ch][i] = 0.0f; // underrun tail: zero-fill rather than repeat stale samples
      }
      return channels;
   }

   bool AudioDeviceOpen(AudioRenderCallback callback, void* userData, double& outSampleRate, std::string& outError,
                        uint32_t requestedDeviceId, double requestedSampleRate, int requestedBufferFrames)
   {
      if (gDeviceHandle != nullptr)
         return true;

      @autoreleasepool
      {
         AudioDeviceHandle* h = new AudioDeviceHandle();
         h->engine = [[AVAudioEngine alloc] init];
         AVAudioMixerNode* mixer = [h->engine mainMixerNode];

         // Device/rate/buffer selection all act on the underlying
         // AudioObjectID, so they must happen before the engine (and its
         // output AudioUnit) starts pulling format info from it.
         // Policy for a user-selected requestedDeviceId that no longer
         // exists (unplugged since it was chosen): AudioUnitSetProperty
         // below simply fails for an invalid AudioObjectID, its result is
         // not checked, and the output AudioUnit is left on whatever it
         // already defaults to - the system default output. This is a
         // silent fallback, not a silent *ignore* of the user's choice: the
         // request itself (gAudioOutputDeviceId in main.cpp) is untouched,
         // so the device picker still shows what the user picked, and the
         // next PollAudioRecovery-driven restart tries that same
         // requestedDeviceId again rather than having quietly forgotten it.
         // Deliberately not surfacing a dedicated "your device vanished,
         // using default" message here - see
         // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md
         // rule 4.
         AudioObjectID targetDevice = (AudioObjectID)requestedDeviceId;
         if (targetDevice != 0)
         {
            AudioUnit outputUnit = h->engine.outputNode.audioUnit;
            AudioUnitSetProperty(outputUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                 &targetDevice, sizeof(targetDevice));
         }
         else
         {
            AudioObjectPropertyAddress defaultAddr {
               kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
               kAudioObjectPropertyElementMain
            };
            UInt32 idSize = sizeof(targetDevice);
            AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, nullptr, &idSize, &targetDevice);
         }

         if (targetDevice != 0 && requestedSampleRate > 0.0)
         {
            AudioObjectPropertyAddress rateAddr {
               kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
               kAudioObjectPropertyElementMain
            };
            Float64 rate = requestedSampleRate;
            AudioObjectSetPropertyData(targetDevice, &rateAddr, 0, nullptr, sizeof(rate), &rate);
         }

         if (targetDevice != 0 && requestedBufferFrames > 0)
         {
            AudioObjectPropertyAddress bufferAddr {
               kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
               kAudioObjectPropertyElementMain
            };
            UInt32 frames = (UInt32)requestedBufferFrames;
            AudioObjectSetPropertyData(targetDevice, &bufferAddr, 0, nullptr, sizeof(frames), &frames);
         }

         AVAudioFormat* format = [mixer outputFormatForBus:0];
         if (format == nil || format.sampleRate <= 0)
            format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:44100.0 channels:2];

         const double sampleRate = format.sampleRate;
         gDeviceCallback = callback;
         gDeviceUserData = userData;

         h->source = [[AVAudioSourceNode alloc]
             initWithFormat:format
             renderBlock:^OSStatus(BOOL* isSilence, const AudioTimeStamp* timestamp,
                                    AVAudioFrameCount frameCount, AudioBufferList* outputData) {
            *isSilence = NO;

            float* chans[kMaxDeviceChannels];
            int numChannels = (int)outputData->mNumberBuffers;
            if (numChannels > kMaxDeviceChannels)
               numChannels = kMaxDeviceChannels;
            for (int ch = 0; ch < numChannels; ++ch)
               chans[ch] = (float*)outputData->mBuffers[ch].mData;

            if (gDeviceCallback != nullptr)
               gDeviceCallback(chans, numChannels, (int)frameCount, gDeviceUserData);

            return noErr;
         }];

         [h->engine attachNode:h->source];
         [h->engine connect:h->source to:mixer format:format];

         NSError* err = nil;
         [h->engine prepare];
         if (![h->engine startAndReturnError:&err])
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "audio engine failed to start";
            [h->engine detachNode:h->source];
            gDeviceCallback = nullptr;
            gDeviceUserData = nullptr;
            delete h;
            return false;
         }

         // Bound to h->engine specifically (not `nil`/global) so a
         // configuration change on some other, unrelated AVAudioEngine
         // instance elsewhere in the process (there is at least one more -
         // AudioSpikeStart's) can't spuriously flag this one's recovery.
         h->configChangeObserver =
            [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioEngineConfigurationChangeNotification
                                                                object:h->engine
                                                                 queue:nil
                                                            usingBlock:^(NSNotification*) {
            gAudioConfigChanged.store(true, std::memory_order_relaxed);
         }];
         InstallWorkspaceObserversOnce();

         gDeviceHandle = h;
         outSampleRate = sampleRate;
         outError.clear();
         return true;
      }
   }

   void AudioDeviceClose()
   {
      if (gDeviceHandle == nullptr)
         return;
      AudioDeviceHandle* h = gDeviceHandle;
      gDeviceHandle = nullptr;
      @autoreleasepool
      {
         // Remove before the engine goes away - see AudioDeviceHandle's
         // comment on this field. Removing a nil observer is a documented
         // no-op, so this is safe even if AudioDeviceOpen never got as far
         // as installing one.
         if (h->configChangeObserver != nil)
         {
            [[NSNotificationCenter defaultCenter] removeObserver:h->configChangeObserver];
            h->configChangeObserver = nil;
         }

         // Same teardown hazard as AudioSpikeStop/AudioFileClose: AVAudioEngine
         // throws an NSException on bad teardown state instead of returning an
         // error, and an uncaught one aborts the process.
         @try
         {
            // Any live AudioInputCaptureStart tap is on this same engine's
            // inputNode - drop it before the engine goes away, or the tap
            // block would keep firing (or dangle) against a stopped engine.
            if (gInputTapInstalled)
            {
               [[h->engine inputNode] removeTapOnBus:0];
               gInputTapInstalled = false;
            }
            [h->engine stop];
            [h->engine detachNode:h->source];
         }
         @catch (NSException* exception)
         {
            fprintf(stderr, "audio device teardown: %s\n",
                    [[exception reason] UTF8String] ? [[exception reason] UTF8String] : "unknown");
         }
         h->source = nil;
         h->engine = nil;
      }
      gDeviceCallback = nullptr;
      gDeviceUserData = nullptr;
      delete h;
   }

   // ---------------------------------------------------------- file players

   struct AudioPlayerHandle
   {
      AVAudioEngine* engine = nil;
      AVAudioPlayerNode* player = nil;
      AVAudioFile* file = nil;
      AVAudioFormat* format = nil;
      double duration = 0.0;
      double sampleRate = 44100.0;
      bool loop = true;
      bool playing = false;
      bool monitor = true;
      Analyser analyser;
   };

   namespace
   {
      void ScheduleFile(AudioPlayerHandle* h)
      {
         if (h == nullptr || h->file == nil)
            return;
         h->file.framePosition = 0;
         AVAudioPlayerNode* player = h->player;
         AudioPlayerHandle* handle = h;
         [player scheduleFile:h->file
                       atTime:nil
        completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
             completionHandler:^(AVAudioPlayerNodeCompletionCallbackType) {
            // Re-arm on the main queue: the callback fires on an audio thread
            // and AVAudioFile is not safe to touch from there.
            dispatch_async(dispatch_get_main_queue(), ^{
               if (handle->loop && handle->playing)
               {
                  ScheduleFile(handle);
                  [handle->player play];
               }
               else
               {
                  handle->playing = false;
               }
            });
         }];
      }
   }

   std::string OpenAudioDialog()
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:YES];
         [panel setCanChooseDirectories:NO];
         [panel setAllowsMultipleSelection:NO];
         [panel setTitle:@"Open audio"];
         if (@available(macOS 11.0, *))
            [panel setAllowedContentTypes:@[ UTTypeAudio, UTTypeMP3, UTTypeWAV, UTTypeAIFF, UTTypeMPEG4Audio ]];
         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [[panel URLs] firstObject];
         return url ? std::string([[url path] UTF8String]) : std::string();
      }
   }

   std::string OpenFolderDialog(const char* title, const std::string& initialDir)
   {
      @autoreleasepool
      {
         NSOpenPanel* panel = [NSOpenPanel openPanel];
         [panel setCanChooseFiles:NO];
         [panel setCanChooseDirectories:YES];
         [panel setAllowsMultipleSelection:NO];
         [panel setTitle:[NSString stringWithUTF8String:title]];
         if (!initialDir.empty())
         {
            NSURL* dirURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:initialDir.c_str()]
                                        isDirectory:YES];
            [panel setDirectoryURL:dirURL];
         }
         if ([panel runModal] != NSModalResponseOK)
            return std::string();
         NSURL* url = [[panel URLs] firstObject];
         return url ? std::string([[url path] UTF8String]) : std::string();
      }
   }

   bool DecodeAudioFileToBuffer(const std::string& path, SampleBuffer& outBuffer, std::string& outError)
   {
      @autoreleasepool
      {
         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         NSURL* url = [NSURL fileURLWithPath:nsPath];
         NSError* err = nil;
         AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
         if (file == nil)
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "could not open audio file";
            return false;
         }

         AVAudioFormat* format = file.processingFormat;
         const AVAudioFrameCount numFrames = (AVAudioFrameCount)file.length;
         if (numFrames == 0)
         {
            outError = "empty audio file";
            return false;
         }

         AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:numFrames];
         if (buffer == nil)
         {
            outError = "could not allocate decode buffer";
            return false;
         }

         if (![file readIntoBuffer:buffer error:&err])
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "failed to decode audio file";
            return false;
         }

         const int channels = (int)format.channelCount;
         const int frames = (int)buffer.frameLength;
         const float* const* channelData = buffer.floatChannelData;
         if (channelData == nullptr || channels <= 0 || frames <= 0)
         {
            outError = "decoded buffer had no float channel data";
            return false;
         }

         outBuffer.channels = channels;
         outBuffer.numFrames = frames;
         outBuffer.sampleRate = format.sampleRate;
         outBuffer.channelData.resize((size_t)channels * (size_t)frames);
         for (int ch = 0; ch < channels; ++ch)
            std::copy(channelData[ch], channelData[ch] + frames,
                      outBuffer.channelData.begin() + (size_t)ch * (size_t)frames);

         outError.clear();
         return true;
      }
   }

   AudioPlayerHandle* AudioFileOpen(const std::string& path, std::string& outError)
   {
      @autoreleasepool
      {
         EnsureFftSetup();

         NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
         NSURL* url = [NSURL fileURLWithPath:nsPath];
         NSError* err = nil;
         AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
         if (file == nil)
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "could not open audio file";
            return nullptr;
         }

         AudioPlayerHandle* h = new AudioPlayerHandle();
         h->file = file;
         h->format = file.processingFormat;
         h->sampleRate = h->format.sampleRate;
         h->duration = (double)file.length / std::max(1.0, h->sampleRate);

         h->engine = [[AVAudioEngine alloc] init];
         h->player = [[AVAudioPlayerNode alloc] init];
         [h->engine attachNode:h->player];
         [h->engine connect:h->player to:[h->engine mainMixerNode] format:h->format];

         const double rate = h->sampleRate;
         AudioPlayerHandle* raw = h;
         [h->player installTapOnBus:0 bufferSize:1024 format:h->format
                              block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
            const float* const* channels = buffer.floatChannelData;
            if (channels != nullptr)
               ProcessInto(raw->analyser, channels[0], (int)buffer.frameLength, rate);
         }];

         [h->engine prepare];
         if (![h->engine startAndReturnError:&err])
         {
            outError = err ? std::string([[err localizedDescription] UTF8String]) : "audio engine failed";
            delete h;
            return nullptr;
         }

         ScheduleFile(h);
         outError.clear();
         return h;
      }
   }

   void AudioFileClose(AudioPlayerHandle* handle)
   {
      if (handle == nullptr)
         return;
      @autoreleasepool
      {
         // Order matters, and so does the @try. AVAudioEngine reports state
         // errors by throwing an ObjC exception, and an uncaught one aborts the
         // whole process - closing an audio file was killing the app outright
         // with "nodeBussesVec.size() >= (inBus + 1)" when the player had
         // already lost its busses. There is no API to ask whether a tap is
         // still installed, so the only way to make teardown safe is to catch.
         @try
         {
            [handle->player stop];
            [handle->engine stop];
            [handle->player removeTapOnBus:0];
            [handle->engine detachNode:handle->player];
         }
         @catch (NSException* exception)
         {
            fprintf(stderr, "audio teardown: %s\n",
                    [[exception reason] UTF8String] ? [[exception reason] UTF8String] : "unknown");
         }
         handle->player = nil;
         handle->engine = nil;
         handle->file = nil;
         handle->format = nil;
      }
      delete handle;
   }

   void AudioFilePlay(AudioPlayerHandle* handle)
   {
      if (handle == nullptr)
         return;
      handle->playing = true;
      [handle->player play];
   }

   void AudioFilePause(AudioPlayerHandle* handle)
   {
      if (handle == nullptr)
         return;
      handle->playing = false;
      [handle->player pause];
   }

   void AudioFileRestart(AudioPlayerHandle* handle)
   {
      if (handle == nullptr)
         return;
      [handle->player stop];
      ScheduleFile(handle);
      if (handle->playing)
         [handle->player play];
   }

   bool AudioFileIsPlaying(AudioPlayerHandle* handle)
   {
      return handle != nullptr && handle->playing;
   }

   void AudioFileSetLoop(AudioPlayerHandle* handle, bool loop)
   {
      if (handle != nullptr)
         handle->loop = loop;
   }

   void AudioFileSetVolume(AudioPlayerHandle* handle, float volume)
   {
      if (handle != nullptr)
         handle->player.volume = std::max(0.0f, std::min(1.0f, volume));
   }

   void AudioFileSetMonitor(AudioPlayerHandle* handle, bool audible)
   {
      if (handle == nullptr)
         return;
      handle->monitor = audible;
      // Muting the mixer keeps the tap running, so analysis continues while
      // the file is silent - useful when driving visuals from a backing track.
      handle->engine.mainMixerNode.outputVolume = audible ? 1.0f : 0.0f;
   }

   double AudioFileDuration(AudioPlayerHandle* handle)
   {
      return handle ? handle->duration : 0.0;
   }

   double AudioFilePosition(AudioPlayerHandle* handle)
   {
      if (handle == nullptr || handle->player == nil)
         return 0.0;
      AVAudioTime* nodeTime = [handle->player lastRenderTime];
      if (nodeTime == nil)
         return 0.0;
      AVAudioTime* playerTime = [handle->player playerTimeForNodeTime:nodeTime];
      if (playerTime == nil)
         return 0.0;
      return (double)playerTime.sampleTime / std::max(1.0, playerTime.sampleRate);
   }

   bool AudioFileRead(AudioPlayerHandle* handle, AudioLevels& out)
   {
      if (handle == nullptr)
      {
         out = AudioLevels();
         return false;
      }
      return ReadFrom(handle->analyser, out);
   }

   void AudioFileSetSmoothing(AudioPlayerHandle* handle, float attack, float release)
   {
      if (handle == nullptr)
         return;
      std::lock_guard<std::mutex> lock(handle->analyser.mutex);
      handle->analyser.attack = std::min(1.0f, std::max(0.01f, attack));
      handle->analyser.release = std::min(1.0f, std::max(0.005f, release));
   }

   void AudioFileSetGain(AudioPlayerHandle* handle, float gain)
   {
      if (handle == nullptr)
         return;
      std::lock_guard<std::mutex> lock(handle->analyser.mutex);
      handle->analyser.gain = std::max(0.0f, gain);
   }
}

// ================================================================ midi input

namespace Platform
{
   namespace
   {
      struct MidiKey
      {
         MidiDeviceId device = 0;
         int channel = 0;
         int controller = 0;
         bool isNote = false;

         bool operator==(const MidiKey& o) const
         {
            return device == o.device && channel == o.channel && controller == o.controller && isNote == o.isNote;
         }
      };

      struct MidiKeyHash
      {
         size_t operator()(const MidiKey& k) const
         {
            size_t h = std::hash<MidiDeviceId>()(k.device);
            h ^= (size_t)((k.channel << 16) | (k.isNote ? 0x100 : 0) | k.controller) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
         }
      };

      struct MidiDeviceChannelKey
      {
         MidiDeviceId device = 0;
         int channel = 0;

         bool operator==(const MidiDeviceChannelKey& o) const
         {
            return device == o.device && channel == o.channel;
         }
      };

      struct MidiDeviceChannelKeyHash
      {
         size_t operator()(const MidiDeviceChannelKey& k) const
         {
            return std::hash<MidiDeviceId>()(k.device) ^ ((size_t)k.channel << 1);
         }
      };

      struct MidiState
      {
         std::mutex mutex;
         std::unordered_map<MidiKey, float, MidiKeyHash> values;
         std::unordered_map<MidiKey, unsigned int, MidiKeyHash> noteHitCounts;
         std::unordered_map<MidiDeviceChannelKey, MidiLastNote, MidiDeviceChannelKeyHash> lastNotePerChannel;
         std::unordered_map<MidiDeviceId, std::string> deviceNames;
         MidiCCValue lastTouched;
         bool lastTouchedPending = false;
      };

      MidiState gMidiState;

      // Live note ring - see Platform.h's "live note stream" section for why
      // this sits outside gMidiState's mutex entirely. Producer: the CoreMIDI
      // read thread (single). Consumers: any number, each holding its own
      // cursor. 512 entries is ~40 seconds of the fastest realistic playing.
      constexpr size_t kMidiNoteRingCapacity = 512;
      struct MidiNoteRing
      {
         MidiNoteMessage entries[kMidiNoteRingCapacity];
         std::atomic<unsigned long long> write { 0 };
      };
      MidiNoteRing gMidiNoteRing;

      void PushMidiNote(MidiDeviceId device, int channel, int note, float velocity01, bool isNoteOn)
      {
         const unsigned long long w = gMidiNoteRing.write.load(std::memory_order_relaxed);
         MidiNoteMessage& slot = gMidiNoteRing.entries[w % kMidiNoteRingCapacity];
         slot.device = device;
         slot.channel = channel;
         slot.note = note;
         slot.velocity01 = velocity01;
         slot.isNoteOn = isNoteOn;
         // Release: everything written to the slot above is visible to any
         // consumer that acquires this counter and then reads the slot.
         gMidiNoteRing.write.store(w + 1, std::memory_order_release);
      }

      MIDIClientRef gMidiClient = 0;
      MIDIPortRef gMidiInPort = 0;
      bool gMidiRunning = false;
      std::string gMidiDeviceSummary;

      // MIDI Clock (0xF8) pulse timestamps, for deriving BPM from pulse spacing.
      // A window of 24 pulses is one quarter note - enough to smooth out jitter
      // between individual pulses without lagging a real tempo change for long.
      constexpr size_t kMidiClockWindow = 24;
      constexpr double kMidiClockTimeoutSeconds = 2.0;

      struct MidiClockState
      {
         std::mutex mutex;
         std::deque<std::chrono::steady_clock::time_point> pulseTimes;
         std::chrono::steady_clock::time_point lastPulseTime{};
      };
      MidiClockState gMidiClockState;

      void HandleMidiRealtimeByte(unsigned char status)
      {
         if (status == 0xF8) // Clock pulse
         {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(gMidiClockState.mutex);
            gMidiClockState.pulseTimes.push_back(now);
            if (gMidiClockState.pulseTimes.size() > kMidiClockWindow)
               gMidiClockState.pulseTimes.pop_front();
            gMidiClockState.lastPulseTime = now;
         }
         else if (status == 0xFA || status == 0xFC) // Start / Stop
         {
            // A stopped-then-restarted clock shouldn't average its BPM across
            // the gap; Continue (0xFB) deliberately leaves the window alone.
            std::lock_guard<std::mutex> lock(gMidiClockState.mutex);
            gMidiClockState.pulseTimes.clear();
         }
      }

      void HandleMidiBytes(const unsigned char* data, size_t len, MidiDeviceId device)
      {
         if (len < 2)
            return;
         const unsigned char status = data[0];
         const unsigned char hiNibble = status & 0xF0;
         const int channel = status & 0x0F;

         if (hiNibble == 0xB0 && len >= 3)
         {
            // Control Change
            MidiKey key{ device, channel, (int)data[1], false };
            const float v = (float)data[2] / 127.0f;
            std::lock_guard<std::mutex> lock(gMidiState.mutex);
            gMidiState.values[key] = v;
            gMidiState.lastTouched = MidiCCValue{ device, channel, (int)data[1], false, v };
            gMidiState.lastTouchedPending = true;
         }
         else if (hiNibble == 0x80 && len >= 3)
         {
            // Note Off. Deliberately absent from the polled maps above (a
            // modulator's "held value" should stay where the key left it), but
            // the note stream must carry it or every voice it opened sticks on.
            PushMidiNote(device, channel, (int)data[1], (float)data[2] / 127.0f, false);
         }
         else if (hiNibble == 0x90 && len >= 3)
         {
            // Note On; velocity 0 is the standard running-status spelling of a
            // Note Off, so it reaches the note stream as one and is ignored for
            // the polled maps - the held value stays where it was, same as
            // every DAW's MIDI learn.
            if (data[2] == 0)
            {
               PushMidiNote(device, channel, (int)data[1], 0.0f, false);
               return;
            }
            PushMidiNote(device, channel, (int)data[1], (float)data[2] / 127.0f, true);
            MidiKey key{ device, channel, (int)data[1], true };
            const float v = (float)data[2] / 127.0f;
            std::lock_guard<std::mutex> lock(gMidiState.mutex);
            gMidiState.values[key] = v;
            gMidiState.noteHitCounts[key]++;
            MidiLastNote& last = gMidiState.lastNotePerChannel[MidiDeviceChannelKey{ device, channel }];
            last.note = (int)data[1];
            last.velocity01 = v;
            last.hitSeq++;
            gMidiState.lastTouched = MidiCCValue{ device, channel, (int)data[1], true, v };
            gMidiState.lastTouchedPending = true;
         }
      }

      void HandleMidiPacketList(const MIDIPacketList* pktList, MidiDeviceId device)
      {
         const MIDIPacket* packet = &pktList->packet[0];
         for (UInt32 i = 0; i < pktList->numPackets; i++)
         {
            // A single packet can contain more than one MIDI message back to
            // back; walk it a status byte at a time.
            size_t offset = 0;
            while (offset < packet->length)
            {
               const unsigned char status = packet->data[offset];
               if ((status & 0x80) == 0)
                  break; // not a status byte - malformed / running status we don't handle
               if (status >= 0xF8)
               {
                  // System realtime (Clock/Start/Continue/Stop/...): always a
                  // single byte, and can be interleaved anywhere in the stream,
                  // even inside another message - handle it and keep walking.
                  HandleMidiRealtimeByte(status);
                  offset += 1;
                  continue;
               }
               size_t msgLen = 3;
               if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
                  msgLen = 2;
               if (offset + msgLen > packet->length)
                  break;
               HandleMidiBytes(packet->data + offset, msgLen, device);
               offset += msgLen;
            }
            packet = MIDIPacketNext(packet);
         }
      }

      std::string SourceDisplayName(MIDIEndpointRef source)
      {
         CFStringRef name = nullptr;
         if (MIDIObjectGetStringProperty(source, kMIDIPropertyDisplayName, &name) != noErr || name == nullptr)
         {
            if (MIDIObjectGetStringProperty(source, kMIDIPropertyName, &name) != noErr || name == nullptr)
               return "unknown device";
         }
         std::string result = [(__bridge NSString*)name UTF8String];
         CFRelease(name);
         return result;
      }

      void ConnectAllSources()
      {
         std::vector<std::string> names;
         std::unordered_map<MidiDeviceId, std::string> deviceNames;
         const ItemCount count = MIDIGetNumberOfSources();
         for (ItemCount i = 0; i < count; i++)
         {
            MIDIEndpointRef source = MIDIGetSource(i);
            if (source == 0)
               continue;
            // The endpoint ref itself, passed back as connRefCon on every packet
            // from this source, is what lets HandleMidiBytes tell two connected
            // controllers apart even when both broadcast on the same channel.
            MIDIPortConnectSource(gMidiInPort, source, (void*)(uintptr_t)source);
            std::string name = SourceDisplayName(source);
            deviceNames[(MidiDeviceId)source] = name;
            names.push_back(name);
         }
         std::string summary;
         for (size_t i = 0; i < names.size(); i++)
         {
            if (i > 0)
               summary += ", ";
            summary += names[i];
         }
         gMidiDeviceSummary = summary;
         std::lock_guard<std::mutex> lock(gMidiState.mutex);
         gMidiState.deviceNames = std::move(deviceNames);
      }
   }

   bool MidiStart(std::string& outError)
   {
      if (gMidiRunning)
         return true;

      OSStatus st = MIDIClientCreateWithBlock(CFSTR("Infinite"), &gMidiClient,
         ^(const MIDINotification* message) {
            if (message->messageID == kMIDIMsgObjectAdded || message->messageID == kMIDIMsgObjectRemoved)
               ConnectAllSources();
         });
      if (st != noErr)
      {
         outError = "failed to create Core MIDI client";
         gMidiClient = 0;
         return false;
      }

      st = MIDIInputPortCreateWithBlock(gMidiClient, CFSTR("Infinite Input"), &gMidiInPort,
         ^(const MIDIPacketList* pktList, void* srcConnRefCon) {
            HandleMidiPacketList(pktList, (MidiDeviceId)(uintptr_t)srcConnRefCon);
         });
      if (st != noErr)
      {
         outError = "failed to create Core MIDI input port";
         MIDIClientDispose(gMidiClient);
         gMidiClient = 0;
         return false;
      }

      ConnectAllSources();
      gMidiRunning = true;
      outError.clear();
      return true;
   }

   void MidiStop()
   {
      if (!gMidiRunning)
         return;
      if (gMidiInPort != 0)
      {
         MIDIPortDispose(gMidiInPort);
         gMidiInPort = 0;
      }
      if (gMidiClient != 0)
      {
         MIDIClientDispose(gMidiClient);
         gMidiClient = 0;
      }
      gMidiRunning = false;
      gMidiDeviceSummary.clear();
      {
         std::lock_guard<std::mutex> lock(gMidiState.mutex);
         gMidiState.values.clear();
         gMidiState.noteHitCounts.clear();
         gMidiState.lastNotePerChannel.clear();
         gMidiState.deviceNames.clear();
         gMidiState.lastTouchedPending = false;
      }
      {
         std::lock_guard<std::mutex> lock(gMidiClockState.mutex);
         gMidiClockState.pulseTimes.clear();
      }
   }

   bool MidiIsRunning() { return gMidiRunning; }

   std::string MidiDeviceSummary() { return gMidiDeviceSummary; }

   std::string MidiDeviceName(MidiDeviceId device)
   {
      std::lock_guard<std::mutex> lock(gMidiState.mutex);
      auto it = gMidiState.deviceNames.find(device);
      return it == gMidiState.deviceNames.end() ? "" : it->second;
   }

   bool MidiRead(MidiDeviceId device, int channel, int controller, bool isNote, float& outValue01)
   {
      if (!gMidiRunning)
      {
         outValue01 = 0.0f;
         return false;
      }
      MidiKey key{ device, channel, controller, isNote };
      std::lock_guard<std::mutex> lock(gMidiState.mutex);
      auto it = gMidiState.values.find(key);
      if (it == gMidiState.values.end())
      {
         outValue01 = 0.0f;
         return false;
      }
      outValue01 = it->second;
      return true;
   }

   bool MidiPollLastTouched(MidiCCValue& outLast)
   {
      std::lock_guard<std::mutex> lock(gMidiState.mutex);
      if (!gMidiState.lastTouchedPending)
         return false;
      outLast = gMidiState.lastTouched;
      gMidiState.lastTouchedPending = false;
      return true;
   }

   unsigned int MidiNoteHitCount(MidiDeviceId device, int channel, int note)
   {
      MidiKey key{ device, channel, note, true };
      std::lock_guard<std::mutex> lock(gMidiState.mutex);
      auto it = gMidiState.noteHitCounts.find(key);
      return it == gMidiState.noteHitCounts.end() ? 0 : it->second;
   }

   bool MidiChannelLastNote(MidiDeviceId device, int channel, MidiLastNote& out)
   {
      std::lock_guard<std::mutex> lock(gMidiState.mutex);
      auto it = gMidiState.lastNotePerChannel.find(MidiDeviceChannelKey{ device, channel });
      if (it == gMidiState.lastNotePerChannel.end())
         return false;
      out = it->second;
      return true;
   }

   unsigned long long MidiNoteStreamPosition()
   {
      return gMidiNoteRing.write.load(std::memory_order_acquire);
   }

   int MidiReadNotesSince(unsigned long long& cursor, MidiNoteMessage* out, int maxCount)
   {
      const unsigned long long w = gMidiNoteRing.write.load(std::memory_order_acquire);
      // Fallen further behind than the ring is long (or a fresh cursor of 0
      // against a long-running stream): skip to the oldest entry still intact
      // rather than reading slots the producer has already recycled.
      if (cursor + kMidiNoteRingCapacity < w)
         cursor = w - kMidiNoteRingCapacity;
      if (cursor > w)
         cursor = w; // ring was reset under us (MidiStop/MidiStart)

      int n = 0;
      while (cursor < w && n < maxCount)
      {
         out[n++] = gMidiNoteRing.entries[cursor % kMidiNoteRingCapacity];
         cursor++;
      }
      return n;
   }

   bool MidiClockIsPresent()
   {
      std::lock_guard<std::mutex> lock(gMidiClockState.mutex);
      if (gMidiClockState.pulseTimes.empty())
         return false;
      const auto now = std::chrono::steady_clock::now();
      const double secondsSinceLastPulse =
         std::chrono::duration<double>(now - gMidiClockState.lastPulseTime).count();
      return secondsSinceLastPulse < kMidiClockTimeoutSeconds;
   }

   float MidiClockBpm()
   {
      std::lock_guard<std::mutex> lock(gMidiClockState.mutex);
      if (gMidiClockState.pulseTimes.size() < 2)
         return 0.0f;
      const double totalSeconds =
         std::chrono::duration<double>(gMidiClockState.pulseTimes.back() - gMidiClockState.pulseTimes.front()).count();
      if (totalSeconds <= 0.0)
         return 0.0f;
      const size_t intervals = gMidiClockState.pulseTimes.size() - 1;
      const double secondsPerPulse = totalSeconds / (double)intervals;
      return (float)(60.0 / (24.0 * secondsPerPulse));
   }

   // ---- audio plugin hosting (Audio Units) ---------------------------------
   // See Platform.h's plugin section for the contract. Every Objective-C
   // object involved is confined below this line; the only function here that
   // the audio thread ever calls is PluginRender, and it does nothing but
   // invoke a block cached on the main thread.

   namespace
   {
      std::string FourCCToString(OSType code)
      {
         const char chars[5] = { (char)((code >> 24) & 0xFF), (char)((code >> 16) & 0xFF),
                                 (char)((code >> 8) & 0xFF), (char)(code & 0xFF), '\0' };
         return std::string(chars);
      }

      OSType StringToFourCC(const std::string& s)
      {
         if (s.size() != 4)
            return 0;
         return ((OSType)(unsigned char)s[0] << 24) | ((OSType)(unsigned char)s[1] << 16) |
                ((OSType)(unsigned char)s[2] << 8) | (OSType)(unsigned char)s[3];
      }

      std::string MakeAuIdentifier(const AudioComponentDescription& d)
      {
         return "au:" + FourCCToString(d.componentType) + ":" + FourCCToString(d.componentSubType) +
                ":" + FourCCToString(d.componentManufacturer);
      }

      // "au:aufx:dely:appl" -> the component description it names. Returns
      // false for anything that isn't exactly four colon-separated fields with
      // four-character codes, which is what a patch written by a future
      // (vst3) build would look like to this build.
      bool ParseAuIdentifier(const std::string& id, AudioComponentDescription& out)
      {
         std::vector<std::string> parts;
         size_t start = 0;
         while (true)
         {
            const size_t sep = id.find(':', start);
            parts.push_back(id.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
            if (sep == std::string::npos)
               break;
            start = sep + 1;
         }
         if (parts.size() != 4 || parts[0] != "au")
            return false;
         out = AudioComponentDescription {};
         out.componentType = StringToFourCC(parts[1]);
         out.componentSubType = StringToFourCC(parts[2]);
         out.componentManufacturer = StringToFourCC(parts[3]);
         if (out.componentType == 0 || out.componentSubType == 0 || out.componentManufacturer == 0)
            return false;
         return true;
      }

      // Effects, music effects, and instruments - the three component types
      // AudioPluginNode can host now that it has a note input pin. See
      // EnumerateAudioUnits' doc comment in Platform.h.
      bool IsHostablePluginType(OSType type)
      {
         return type == kAudioUnitType_Effect || type == kAudioUnitType_MusicEffect ||
                type == kAudioUnitType_MusicDevice;
      }

      bool ComponentTypeAcceptsNotes(OSType type)
      {
         return type == kAudioUnitType_MusicEffect || type == kAudioUnitType_MusicDevice;
      }

      // AVAudioUnitComponent.name is usually "Manufacturer: Plugin"; the
      // manufacturer is already its own column in the panel, so strip it
      // rather than printing it twice on every row.
      std::string StripManufacturerPrefix(const std::string& name, const std::string& manufacturer)
      {
         const std::string prefix = manufacturer + ": ";
         if (!manufacturer.empty() && name.rfind(prefix, 0) == 0)
            return name.substr(prefix.size());
         return name;
      }

      constexpr int kPluginMaxChannels = 8;
   }

   struct PluginHandle;

   // Process-wide count of open plugin editor windows, kept in lockstep with
   // every PluginHandle's editorOpen flag (see SetPluginEditorOpen below) so
   // AnyPluginEditorOpen() is a cheap atomic read rather than a walk over
   // every handle.
   std::atomic<int> gOpenPluginEditorCount { 0 };

   // The single place editorOpen is ever written. Keeps gOpenPluginEditorCount
   // accurate no matter which of the several call sites (open, soft-close,
   // user closing the window) flips the flag.
   void SetPluginEditorOpen(PluginHandle* h, bool open);
}

#include "PluginHandleInternal.h"

namespace Platform
{

   void SetPluginEditorOpen(PluginHandle* h, bool open)
   {
      bool was = h->editorOpen.exchange(open, std::memory_order_acq_rel);
      if (was == open)
         return;
      if (open)
         gOpenPluginEditorCount.fetch_add(1, std::memory_order_relaxed);
      else
         gOpenPluginEditorCount.fetch_sub(1, std::memory_order_relaxed);
   }
}

@implementation InfinitePluginEditorDelegate
// User clicked the window's own close button. Treated the same as the node's
// "close" toggle (PluginCloseEditor): the window/controller are cached, not
// torn down, so reopening shows the plugin's real editor again rather than
// re-requesting a view controller a second time. Real teardown only happens
// in PluginDestroy.
- (void)windowWillClose:(NSNotification*)notification
{
   (void)notification;
   Platform::PluginHandle* h = (Platform::PluginHandle*)self.handle;
   if (h == nullptr)
      return;
   Platform::SetPluginEditorOpen(h, false);
}

// Distinguishes the user dragging the window's own resize handle from the
// programmatic resize PresentEditorWindow's size observer performs once the
// remote view's real size arrives.
- (void)windowDidResize:(NSNotification*)notification
{
   (void)notification;
   Platform::PluginHandle* h = (Platform::PluginHandle*)self.handle;
   if (h == nullptr || h->programmaticResize)
      return;
   h->editorUserResized = true;
}
@end

namespace Platform
{
   void EnumerateAudioUnits(std::vector<PluginDesc>& out)
   {
      out.clear();
      @autoreleasepool
      {
         // A zeroed description is the documented wildcard: every installed
         // component, v2 and v3 alike, in one registry query. There is no
         // directory walk here at all, which is why PluginScanner has no
         // folder list.
         AudioComponentDescription wildcard = {};
         AVAudioUnitComponentManager* manager = [AVAudioUnitComponentManager sharedAudioUnitComponentManager];
         NSArray<AVAudioUnitComponent*>* components = [manager componentsMatchingDescription:wildcard];

         for (AVAudioUnitComponent* component in components)
         {
            const AudioComponentDescription d = component.audioComponentDescription;
            if (!IsHostablePluginType(d.componentType))
               continue;

            PluginDesc desc;
            desc.format = "au";
            desc.manufacturer = component.manufacturerName != nil
                                   ? std::string([component.manufacturerName UTF8String])
                                   : std::string();
            const std::string rawName = component.name != nil ? std::string([component.name UTF8String])
                                                              : std::string();
            desc.name = StripManufacturerPrefix(rawName, desc.manufacturer);
            desc.identifier = MakeAuIdentifier(d);
            desc.acceptsNotes = ComponentTypeAcceptsNotes(d.componentType);
            if (desc.name.empty() || desc.identifier.empty())
               continue;
            out.push_back(std::move(desc));
         }
      }
   }

   bool DescribeAudioUnitBundle(const std::string& bundlePath, std::vector<PluginDesc>& out)
   {
      out.clear();
      @autoreleasepool
      {
         NSString* path = [NSString stringWithUTF8String:bundlePath.c_str()];
         NSBundle* bundle = [NSBundle bundleWithPath:path];
         if (bundle == nil)
            return false;
         // An audio component bundle declares its components in Info.plist
         // under AudioComponents, one dictionary each with type/subtype/manu
         // as four-character strings. This is the only way to identify a
         // dropped .component that the registry hasn't picked up yet.
         id components = [bundle objectForInfoDictionaryKey:@"AudioComponents"];
         if (![components isKindOfClass:[NSArray class]])
            return false;

         for (id entry in (NSArray*)components)
         {
            if (![entry isKindOfClass:[NSDictionary class]])
               continue;
            NSDictionary* dict = (NSDictionary*)entry;
            NSString* type = dict[@"type"];
            NSString* subtype = dict[@"subtype"];
            NSString* manu = dict[@"manufacturer"];
            if (![type isKindOfClass:[NSString class]] || ![subtype isKindOfClass:[NSString class]] ||
                ![manu isKindOfClass:[NSString class]])
               continue;

            AudioComponentDescription d = {};
            d.componentType = StringToFourCC(std::string([type UTF8String]));
            d.componentSubType = StringToFourCC(std::string([subtype UTF8String]));
            d.componentManufacturer = StringToFourCC(std::string([manu UTF8String]));
            if (!IsHostablePluginType(d.componentType))
               continue;

            PluginDesc desc;
            desc.format = "au";
            desc.identifier = MakeAuIdentifier(d);
            desc.acceptsNotes = ComponentTypeAcceptsNotes(d.componentType);
            NSString* name = dict[@"name"];
            const std::string rawName = [name isKindOfClass:[NSString class]]
                                           ? std::string([name UTF8String])
                                           : std::string();
            // Info.plist's "name" is also "Manufacturer: Plugin" by
            // convention, but with no separate manufacturer key to strip with,
            // so split on the colon here instead.
            const size_t colon = rawName.find(": ");
            if (colon != std::string::npos)
            {
               desc.manufacturer = rawName.substr(0, colon);
               desc.name = rawName.substr(colon + 2);
            }
            else
            {
               desc.name = rawName;
            }
            if (desc.name.empty())
               desc.name = FourCCToString(d.componentSubType);
            out.push_back(std::move(desc));
         }
      }
      return !out.empty();
   }

   std::string ExecutablePath()
   {
      uint32_t size = 0;
      _NSGetExecutablePath(nullptr, &size); // sets size to what's needed
      std::string buf(size, '\0');
      if (_NSGetExecutablePath(&buf[0], &size) != 0)
         return std::string();
      buf.resize(strlen(buf.c_str()));
      return buf;
   }

   PluginHandle* PluginCreate(const PluginDesc& desc, double sampleRate, int maxBlockFrames)
   {
      PluginHandle* h = new PluginHandle();
      h->desc = desc;
      h->sampleRate = sampleRate;
      h->maxBlockFrames = maxBlockFrames > 0 ? maxBlockFrames : 512;

      AudioComponentDescription cd = {};
      if (desc.format != "au" || !ParseAuIdentifier(desc.identifier, cd))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = "unsupported plugin format: " + desc.format;
         h->arrived.store(true, std::memory_order_release);
         return h;
      }

      AudioComponent comp = AudioComponentFindNext(nullptr, &cd);
      if (comp == nullptr)
      {
         h->state = PluginLoadState::Failed;
         h->loadError = "Audio Unit not found on system: " + desc.identifier;
         h->arrived.store(true, std::memory_order_release);
         return h;
      }

      // Asynchronous instantiation via AUv3 API. The completion handler can
      // be called on an internal queue, so arrival fields are guarded by
      // arrivalMutex.
      [AUAudioUnit instantiateWithComponentDescription:cd
                                               options:0
                                     completionHandler:^(AUAudioUnit* au, NSError* error) {
         std::lock_guard<std::mutex> lock(h->arrivalMutex);
         h->arrivedUnit = au;
         h->arrivedError = error;
         h->arrived.store(true, std::memory_order_release);
      }];

      return h;
   }

   namespace
   {
      bool PluginConfigure(PluginHandle* h, std::string& outError)
      {
         if (h == nullptr || h->unit == nil)
         {
            outError = "no plugin instance";
            return false;
         }

         @autoreleasepool
         {
            AUAudioUnit* au = h->unit;
            if (h->resourcesAllocated)
            {
               [au deallocateRenderResources];
               h->resourcesAllocated = false;
            }

            const double rate = h->sampleRate > 0.0 ? h->sampleRate : 48000.0;
            const int frames = std::min(std::max(h->maxBlockFrames, 1), 4096);

            // Try stereo, fall back to mono: plenty of effects are mono-only,
            // and a rejected format is a normal outcome, not an error.
            int negotiatedIn = 0;
            int negotiatedOut = 0;
            for (int channels = 2; channels >= 1 && negotiatedOut == 0; channels--)
            {
               AVAudioFormat* format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:rate
                                                                                       channels:(AVAudioChannelCount)channels];
               if (format == nil)
                  continue;

               NSError* busError = nil;
               bool ok = true;
               if (au.inputBusses.count > 0)
               {
                  AUAudioUnitBus* inBus = [au.inputBusses objectAtIndexedSubscript:0];
                  ok = [inBus setFormat:format error:&busError];
                  if (ok)
                  {
                     inBus.enabled = YES;
                     negotiatedIn = channels;
                  }
               }
               if (ok && au.outputBusses.count > 0)
               {
                  AUAudioUnitBus* outBus = [au.outputBusses objectAtIndexedSubscript:0];
                  ok = [outBus setFormat:format error:&busError];
                  if (ok)
                  {
                     outBus.enabled = YES;
                     negotiatedOut = channels;
                  }
               }
               if (!ok)
                  negotiatedIn = 0;
            }

            if (negotiatedOut == 0)
            {
               outError = "plugin rejected both stereo and mono float formats";
               return false;
            }
            h->pluginInChannels = negotiatedIn;
            h->pluginOutChannels = negotiatedOut;

            au.maximumFramesToRender = (AUAudioFrameCount)frames;

            NSError* allocError = nil;
            if (![au allocateRenderResourcesAndReturnError:&allocError])
            {
               outError = allocError != nil ? std::string([[allocError localizedDescription] UTF8String])
                                            : std::string("allocateRenderResources failed");
               return false;
            }
            h->resourcesAllocated = true;

            // Cached once, here, on the main thread. ProcessBlock calls the
            // block; it never messages the AUAudioUnit.
            h->renderBlockStrong = au.renderBlock;
            h->renderBlockRaw = (__bridge void*)h->renderBlockStrong;

            // Same, for MIDI scheduling - nil for a plugin that publishes none
            // (see PluginScheduleMIDIEvent).
            h->scheduleMIDIEventBlockStrong = au.scheduleMIDIEventBlock;
            h->scheduleMIDIEventBlockRaw = (__bridge void*)h->scheduleMIDIEventBlockStrong;

            // Fixed-capacity render scratch, allocated here so the render path
            // never does. mBuffers is a trailing array, hence the manual size.
            if (h->outAbl == nullptr)
            {
               const size_t bytes = sizeof(AudioBufferList) + sizeof(::AudioBuffer) * (kPluginMaxChannels - 1);
               h->outAbl = (AudioBufferList*)std::calloc(1, bytes);
            }
            h->inScratch.assign((size_t)kPluginMaxChannels * (size_t)frames, 0.0f);
            h->outScratch.assign((size_t)kPluginMaxChannels * (size_t)frames, 0.0f);
            h->renderSampleTime = 0.0;

            if (h->learnToken == nullptr && au.parameterTree != nil)
            {
               PluginHandle* raw = h;
               h->learnToken = [au.parameterTree
                  tokenByAddingParameterObserver:^(AUParameterAddress address, AUValue value) {
                     (void)value;
                     if (!raw->learning.load(std::memory_order_relaxed))
                        return;
                     raw->learnedAddress.store((unsigned long long)address, std::memory_order_relaxed);
                     raw->learnedValid.store(true, std::memory_order_release);
                  }];
            }
         }

         return true;
      }
   }

   PluginLoadState PluginPoll(PluginHandle* h, std::string& outError)
   {
      if (h == nullptr)
      {
         outError = "null plugin handle";
         return PluginLoadState::Failed;
      }

      if (h->state != PluginLoadState::Pending)
      {
         outError = h->loadError;
         return h->state;
      }
      if (!h->arrived.load(std::memory_order_acquire))
         return PluginLoadState::Pending;

      AUAudioUnit* au = nil;
      NSError* error = nil;
      {
         std::lock_guard<std::mutex> lock(h->arrivalMutex);
         au = h->arrivedUnit;
         error = h->arrivedError;
         h->arrivedUnit = nil;
         h->arrivedError = nil;
      }

      if (au == nil)
      {
         h->state = PluginLoadState::Failed;
         if (h->loadError.empty())
         {
            h->loadError = error != nil ? std::string([[error localizedDescription] UTF8String])
                                        : std::string("plugin failed to instantiate");
         }
         outError = h->loadError;
         return h->state;
      }

      h->unit = au;
      std::string configError;
      if (!PluginConfigure(h, configError))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = configError;
         outError = configError;
         return h->state;
      }

      h->state = PluginLoadState::Ready;
      outError.clear();
      return h->state;
   }

   bool PluginPrepare(PluginHandle* h, double sampleRate, int maxBlockFrames, std::string& outError)
   {
      if (h == nullptr)
      {
         outError = "null plugin handle";
         return false;
      }

      if (h->state != PluginLoadState::Ready)
      {
         outError = "plugin not ready";
         return false;
      }
      const int frames = maxBlockFrames > 0 ? maxBlockFrames : h->maxBlockFrames;
      if (sampleRate <= 0.0)
         return true; // no device yet - keep whatever we configured with
      if (std::abs(sampleRate - h->sampleRate) < 1e-6 && frames == h->maxBlockFrames && h->resourcesAllocated)
         return true;

      h->sampleRate = sampleRate;
      h->maxBlockFrames = frames;
      if (!PluginConfigure(h, outError))
      {
         h->state = PluginLoadState::Failed;
         h->loadError = outError;
         return false;
      }
      return true;
   }

   PluginDesc PluginDescriptionOf(PluginHandle* h)
   {
      return h != nullptr ? h->desc : PluginDesc();
   }

   void PluginDestroy(PluginHandle* h)
   {
      if (h == nullptr)
         return;

      // Real teardown of the editor window/controller/observer. PluginCloseEditor
      // only hides the window (it is cached across open/close - see its
      // comment), so this handle's last use has to release it for real.
      if (h->editorSizeObserver != nil)
      {
         [[NSNotificationCenter defaultCenter] removeObserver:h->editorSizeObserver];
         h->editorSizeObserver = nil;
      }
      if (h->editorWindow != nil)
      {
         NSWindow* window = h->editorWindow;
         // Clearing the delegate first: -close fires windowWillClose, which
         // would otherwise re-enter this handle's fields while we are tearing
         // them down.
         window.delegate = nil;
         h->editorDelegate = nil;
         h->editorWindow = nil;
         h->editorController = nil;
         SetPluginEditorOpen(h, false);
         [window close];
      }

      // Main thread, so a bounded spin is legal (and this is the only place
      // the audio thread's use of the handle is actually fenced off - the
      // node has already unpublished the pointer by the time this runs, so
      // the wait is for a render that started before that store).
      for (int spins = 0; spins < 100000 && h->inRender.load(std::memory_order_acquire); spins++)
      {
      }

      @autoreleasepool
      {
         if (h->unit != nil)
         {
            if (h->learnToken != nullptr && h->unit.parameterTree != nil)
               [h->unit.parameterTree removeParameterObserver:h->learnToken];
            if (h->resourcesAllocated)
               [h->unit deallocateRenderResources];
         }
      }
      h->learnToken = nullptr;
      h->renderBlockRaw = nullptr;
      h->renderBlockStrong = nil;
      h->scheduleMIDIEventBlockStrong = nil;
      h->scheduleMIDIEventBlockRaw = nullptr;
      h->unit = nil;
      h->arrivedUnit = nil;
      h->arrivedError = nil;
      if (h->outAbl != nullptr)
      {
         std::free(h->outAbl);
         h->outAbl = nullptr;
      }
      delete h;
   }

   void PluginRender(PluginHandle* h, const float* const* in, int inChannels,
                     float* const* out, int outChannels, int numFrames)
   {
      if (h == nullptr)
         return;

      // Everything below is on the real-time thread: no Objective-C message
      // send, no ARC traffic, no allocation, no lock. The two blocks involved
      // are a cached one (bridged back as __unsafe_unretained, which ARC does
      // not retain) and a stack literal (which is not copied, because
      // AURenderBlock's pullInputBlock parameter is not a __strong parameter).
      if (h->renderBlockRaw == nullptr || out == nullptr || numFrames <= 0)
         return;

      h->inRender.store(true, std::memory_order_release);

      const int pluginOut = h->pluginOutChannels;
      const int pluginIn = h->pluginInChannels;
      const int frames = numFrames > h->maxBlockFrames ? h->maxBlockFrames : numFrames;
      AudioBufferList* abl = h->outAbl;
      if (abl == nullptr || pluginOut <= 0)
      {
         h->inRender.store(false, std::memory_order_release);
         return;
      }

      // Point the plugin's output buffers straight at the caller's channels
      // when the counts agree; otherwise render into scratch and fan out
      // afterwards (a mono effect feeding a stereo chain).
      const bool direct = (pluginOut == outChannels);
      float* outScratchBase = h->outScratch.empty() ? nullptr : h->outScratch.data();
      if (!direct && outScratchBase == nullptr)
      {
         h->inRender.store(false, std::memory_order_release);
         return;
      }

      abl->mNumberBuffers = (UInt32)pluginOut;
      for (int ch = 0; ch < pluginOut; ch++)
      {
         abl->mBuffers[ch].mNumberChannels = 1;
         abl->mBuffers[ch].mDataByteSize = (UInt32)(frames * (int)sizeof(float));
         abl->mBuffers[ch].mData = direct ? (void*)out[ch]
                                          : (void*)(outScratchBase + (size_t)ch * (size_t)h->maxBlockFrames);
      }

      float* inScratchBase = h->inScratch.empty() ? nullptr : h->inScratch.data();
      const int maxFrames = h->maxBlockFrames;

      // Stack block, and __unsafe_unretained on purpose: assigning a block
      // literal to a __strong variable makes ARC emit Block_copy, which is a
      // malloc - on the render thread. An unretained local keeps it on the
      // stack, which is exactly why AURenderBlock's pullInputBlock parameter
      // is designed to be handed a stack block.
      __unsafe_unretained AURenderPullInputBlock pull = ^AUAudioUnitStatus(AudioUnitRenderActionFlags* flags,
                                                        const AudioTimeStamp* timestamp,
                                                        AUAudioFrameCount frameCount, NSInteger bus,
                                                        AudioBufferList* inputData) {
         (void)flags;
         (void)timestamp;
         (void)bus;
         if (inputData == nullptr)
            return noErr;
         const int n = (int)frameCount;
         for (UInt32 i = 0; i < inputData->mNumberBuffers; i++)
         {
            float* dst = (float*)inputData->mBuffers[i].mData;
            if (dst == nullptr)
            {
               // The AU handed us an empty buffer to fill in - point it at our
               // own pre-allocated scratch, as the pull-block contract allows.
               if (inScratchBase == nullptr || (int)i >= kPluginMaxChannels)
                  return kAudioUnitErr_InvalidParameter;
               dst = inScratchBase + (size_t)i * (size_t)maxFrames;
               inputData->mBuffers[i].mData = dst;
               inputData->mBuffers[i].mDataByteSize = (UInt32)(n * (int)sizeof(float));
            }
            // Fewer upstream channels than the plugin wants: repeat the last
            // one (mono source into a stereo effect), rather than leaving a
            // silent right channel.
            const float* src = (in != nullptr && inChannels > 0)
                                  ? in[(int)i < inChannels ? (int)i : inChannels - 1]
                                  : nullptr;
            if (src != nullptr)
               std::memcpy(dst, src, (size_t)n * sizeof(float));
            else
               std::memset(dst, 0, (size_t)n * sizeof(float));
         }
         return noErr;
      };

      AudioUnitRenderActionFlags flags = 0;
      AudioTimeStamp timestamp = {};
      timestamp.mFlags = kAudioTimeStampSampleTimeValid;
      timestamp.mSampleTime = h->renderSampleTime;
      h->renderSampleTime += (double)frames;

      // An effect with no input bus (nothing negotiated) is pulled with no
      // input block at all, which is what the AU contract asks for.
      __unsafe_unretained AURenderPullInputBlock pullArg = nil;
      if (pluginIn > 0)
         pullArg = pull;

      __unsafe_unretained AURenderBlock render = (__bridge AURenderBlock)h->renderBlockRaw;
      const AUAudioUnitStatus status =
         render(&flags, &timestamp, (AUAudioFrameCount)frames, 0, abl, pullArg);

      if (status != noErr)
      {
         for (int ch = 0; ch < outChannels; ch++)
            if (out[ch] != nullptr)
               std::memset(out[ch], 0, (size_t)frames * sizeof(float));
         h->inRender.store(false, std::memory_order_release);
         return;
      }

      if (!direct)
      {
         for (int ch = 0; ch < outChannels; ch++)
         {
            if (out[ch] == nullptr)
               continue;
            const int srcCh = ch < pluginOut ? ch : pluginOut - 1;
            std::memcpy(out[ch], outScratchBase + (size_t)srcCh * (size_t)maxFrames,
                        (size_t)frames * sizeof(float));
         }
      }
      else
      {
         // An AU is allowed to ignore the buffers we handed it and swap in its
         // own pointers; when it does, the result is in mData, not in out[].
         for (int ch = 0; ch < pluginOut; ch++)
         {
            float* produced = (float*)abl->mBuffers[ch].mData;
            if (produced != nullptr && produced != out[ch] && out[ch] != nullptr)
               std::memcpy(out[ch], produced, (size_t)frames * sizeof(float));
         }
      }

      // Any tail channels the plugin doesn't drive stay silent rather than
      // holding the previous block's contents.
      for (int ch = pluginOut; ch < outChannels; ch++)
         if (out[ch] != nullptr && !direct)
            std::memset(out[ch], 0, (size_t)frames * sizeof(float));

      h->inRender.store(false, std::memory_order_release);
   }

   void PluginScheduleMIDIEvent(PluginHandle* h, int frameOffset, const unsigned char* bytes, int byteCount)
   {
      if (h == nullptr)
         return;

      if (h->scheduleMIDIEventBlockRaw == nullptr || bytes == nullptr || byteCount <= 0)
         return;
      __unsafe_unretained AUScheduleMIDIEventBlock schedule =
         (__bridge AUScheduleMIDIEventBlock)h->scheduleMIDIEventBlockRaw;
      const AUEventSampleTime sampleTime =
         AUEventSampleTimeImmediate + (AUEventSampleTime)std::max(0, frameOffset);
      schedule(sampleTime, 0, byteCount, bytes);
   }

   int PluginParameterCount(PluginHandle* h)
   {
      if (h == nullptr)
         return 0;

      if (h->unit == nil || h->unit.parameterTree == nil)
         return 0;
      return (int)h->unit.parameterTree.allParameters.count;
   }

   namespace
   {
      void FillParamInfo(AUParameter* p, PluginParamInfo& out)
      {
         out.address = (unsigned long long)p.address;
         out.displayName = p.displayName != nil ? std::string([p.displayName UTF8String]) : std::string();
         if (out.displayName.empty() && p.identifier != nil)
            out.displayName = std::string([p.identifier UTF8String]);
         out.minValue = (float)p.minValue;
         out.maxValue = (float)p.maxValue;
         out.defaultValue = (float)p.value;
         out.unit = p.unitName != nil ? std::string([p.unitName UTF8String]) : std::string();
         // A parameter whose range is degenerate would make a slider that
         // can't move and a divide-by-zero in the fill calc; widen it.
         if (!(out.maxValue > out.minValue))
            out.maxValue = out.minValue + 1.0f;
      }
   }

   bool PluginParameterInfo(PluginHandle* h, int index, PluginParamInfo& out)
   {
      if (h == nullptr)
         return false;

      if (h->unit == nil || h->unit.parameterTree == nil)
         return false;
      NSArray<AUParameter*>* all = h->unit.parameterTree.allParameters;
      if (index < 0 || index >= (int)all.count)
         return false;
      FillParamInfo(all[(NSUInteger)index], out);
      return true;
   }

   bool PluginParameterInfoByAddress(PluginHandle* h, unsigned long long address, PluginParamInfo& out)
   {
      if (h == nullptr)
         return false;

      if (h->unit == nil || h->unit.parameterTree == nil)
         return false;
      AUParameter* p = [h->unit.parameterTree parameterWithAddress:(AUParameterAddress)address];
      if (p == nil)
         return false;
      FillParamInfo(p, out);
      return true;
   }

   void PluginSetParameter(PluginHandle* h, unsigned long long address, float value)
   {
      if (h == nullptr)
         return;

      if (h->unit == nil || h->unit.parameterTree == nil)
         return;
      AUParameter* p = [h->unit.parameterTree parameterWithAddress:(AUParameterAddress)address];
      if (p == nil)
         return;
      // Tagged with our own observer token so this write never comes back
      // through the learn observer as "the user touched a control".
      if (h->learnToken != nullptr)
         [p setValue:(AUValue)value originator:h->learnToken];
      else
         p.value = (AUValue)value;
   }

   bool PluginGetParameter(PluginHandle* h, unsigned long long address, float& outValue)
   {
      if (h == nullptr)
         return false;

      if (h->unit == nil || h->unit.parameterTree == nil)
         return false;
      AUParameter* p = [h->unit.parameterTree parameterWithAddress:(AUParameterAddress)address];
      if (p == nil)
         return false;
      outValue = (float)p.value;
      return true;
   }

   void PluginBeginLearn(PluginHandle* h)
   {
      if (h == nullptr)
         return;

      h->learnedValid.store(false, std::memory_order_relaxed);
      h->learning.store(true, std::memory_order_release);
   }

   void PluginEndLearn(PluginHandle* h)
   {
      if (h == nullptr)
         return;

      h->learning.store(false, std::memory_order_release);
      h->learnedValid.store(false, std::memory_order_relaxed);
   }

   bool PluginPollLearned(PluginHandle* h, unsigned long long& outAddress)
   {
      if (h == nullptr)
         return false;

      if (!h->learnedValid.load(std::memory_order_acquire))
         return false;
      outAddress = h->learnedAddress.load(std::memory_order_relaxed);
      h->learnedValid.store(false, std::memory_order_relaxed);
      return true;
   }

   namespace
   {
      // Shared tail of the editor-open path: wraps whatever view controller we
      // ended up with in an NSWindow. Main thread only.
      // True if `size` is usable as a window content size - the shared
      // rejection test for every candidate in the preference order below.
      bool IsUsableEditorSize(NSSize size)
      {
         return size.width >= 120.0 && size.height >= 80.0;
      }

      void PresentEditorWindow(PluginHandle* h, NSViewController* controller)
      {
         if (h == nullptr || controller == nil)
            return;
         if (h->editorWindow != nil)
         {
            [h->editorWindow makeKeyAndOrderFront:nil];
            SetPluginEditorOpen(h, true);
            return;
         }

         NSView* view = controller.view;

         // Preference order: preferredContentSize (what a well-behaved AUv3
         // sets) -> the view's own frame -> fittingSize -> a hard floor. For
         // an out-of-process AUv3 the view handed back at this instant is
         // often an NSRemoteView placeholder whose fittingSize is not yet the
         // plugin's real editor size, which is why fittingSize alone (the old
         // behaviour) produced a cropped window.
         NSSize size = controller.preferredContentSize;
         if (!IsUsableEditorSize(size))
            size = view.frame.size;
         if (!IsUsableEditorSize(size))
            size = view.fittingSize;
         if (!IsUsableEditorSize(size))
            size = NSMakeSize(std::max(size.width, 480.0), std::max(size.height, 320.0));
         [view setFrameSize:size];

         NSWindow* window = [[NSWindow alloc]
             initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                       styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                         backing:NSBackingStoreBuffered
                           defer:NO];
         window.releasedWhenClosed = NO;
         window.title = [NSString stringWithUTF8String:h->desc.name.c_str()];
         window.contentViewController = controller;

         InfinitePluginEditorDelegate* delegate = [[InfinitePluginEditorDelegate alloc] init];
         delegate.handle = (void*)h;
         window.delegate = delegate;

         [window center];
         [window makeKeyAndOrderFront:nil];

         h->editorWindow = window;
         h->editorController = controller;
         h->editorDelegate = delegate;
         SetPluginEditorOpen(h, true);

         // The remote view's real size for an out-of-process AUv3 typically
         // arrives after the view controller does. Watch for it and resize +
         // re-center the window once, unless the user has since resized the
         // window themselves (editorUserResized).
         view.postsFrameChangedNotifications = YES;
         PluginHandle* raw = h;
         h->editorSizeObserver = [[NSNotificationCenter defaultCenter]
             addObserverForName:NSViewFrameDidChangeNotification
                         object:view
                          queue:nil
                     usingBlock:^(NSNotification* note) {
                        (void)note;
                        if (raw->editorWindow == nil || raw->editorUserResized)
                           return;
                        NSSize newSize = controller.preferredContentSize;
                        if (!IsUsableEditorSize(newSize))
                           newSize = view.frame.size;
                        if (!IsUsableEditorSize(newSize))
                           return;
                        raw->programmaticResize = true;
                        [raw->editorWindow setContentSize:newSize];
                        [raw->editorWindow center];
                        raw->programmaticResize = false;
                     }];
      }
   }

   bool PluginOpenEditor(PluginHandle* h, std::string& outError)
   {
      if (h == nullptr)
      {
         outError = "null plugin handle";
         return false;
      }

      if (h->unit == nil || h->state != PluginLoadState::Ready)
      {
         outError = "plugin not loaded";
         return false;
      }
      // Cached from a previous open (still open, or soft-closed by
      // PluginCloseEditor): reuse it rather than requesting a view controller
      // a second time. For an out-of-process AUv3, a second
      // requestViewControllerWithCompletionHandler: after the first one was
      // released commonly delivers nil, which used to fall back to the
      // generic AUGenericViewController slider list instead of the plugin's
      // real GUI.
      if (h->editorWindow != nil)
      {
         [h->editorWindow makeKeyAndOrderFront:nil];
         SetPluginEditorOpen(h, true);
         return true;
      }
      if (h->editorRequestInFlight)
         return true;

      h->editorRequestInFlight = true;
      PluginHandle* raw = h;
      // Asynchronous, and the completion handler is documented as running on
      // an internal queue - so it hops to the main queue before touching any
      // AppKit object. glfwPollEvents drains the main queue, verified by the
      // NSWindow smoke test that gated this whole feature.
      [h->unit requestViewControllerWithCompletionHandler:^(AUViewControllerBase* controller) {
         dispatch_async(dispatch_get_main_queue(), ^{
            raw->editorRequestInFlight = false;
            NSViewController* vc = controller;
            if (vc == nil)
            {
               // No editor of the plugin's own. AUGenericViewController builds
               // one from the parameter tree against this same instance (not a
               // second one), which is exactly what a generic view should be.
               if (@available(macOS 13.0, *))
               {
                  AUGenericViewController* generic = [[AUGenericViewController alloc] init];
                  generic.auAudioUnit = raw->unit;
                  vc = generic;
               }
            }
            if (vc == nil)
               return;
            PresentEditorWindow(raw, vc);
         });
      }];
      return true;
   }

   void PluginCloseEditor(PluginHandle* h)
   {
      if (h == nullptr)
         return;

      if (h->editorWindow == nil)
         return;
      // Soft close: hide the window but keep it (and its view controller)
      // alive so the next PluginOpenEditor shows the plugin's real GUI again
      // instead of re-requesting a view controller. Real teardown - closing
      // the window for good - only happens in PluginDestroy.
      [h->editorWindow orderOut:nil];
      SetPluginEditorOpen(h, false);
   }

   bool PluginEditorIsOpen(PluginHandle* h)
   {
      if (h == nullptr)
         return false;

      return h->editorOpen.load(std::memory_order_acquire);
   }

   bool AnyPluginEditorOpen()
   {
      return gOpenPluginEditorCount.load(std::memory_order_relaxed) > 0;
   }

   bool PumpPluginEditorEvents()
   {
      @autoreleasepool
      {
         return CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, true) == kCFRunLoopRunHandledSource;
      }
   }

   bool PluginSaveState(PluginHandle* h, std::string& outBase64)
   {
      outBase64.clear();
      if (h == nullptr)
         return false;

      if (h->unit == nil)
         return false;
      @autoreleasepool
      {
         NSDictionary* state = h->unit.fullState;
         if (state == nil)
            return false;
         NSError* error = nil;
         NSData* data = [NSKeyedArchiver archivedDataWithRootObject:state
                                             requiringSecureCoding:NO
                                                             error:&error];
         if (data == nil)
            return false;
         NSString* encoded = [data base64EncodedStringWithOptions:0];
         if (encoded == nil)
            return false;
         outBase64 = std::string([encoded UTF8String]);
      }
      return true;
   }

   bool PluginRestoreState(PluginHandle* h, const std::string& base64)
   {
      if (h == nullptr || base64.empty())
         return false;

      if (h->unit == nil)
         return false;
      @autoreleasepool
      {
         NSString* encoded = [NSString stringWithUTF8String:base64.c_str()];
         if (encoded == nil)
            return false;
         NSData* data = [[NSData alloc] initWithBase64EncodedString:encoded options:0];
         if (data == nil)
            return false;
         NSError* error = nil;
         NSKeyedUnarchiver* unarchiver = [[NSKeyedUnarchiver alloc] initForReadingFromData:data error:&error];
         if (unarchiver == nil)
            return false;
         unarchiver.requiresSecureCoding = NO;
         id state = [unarchiver decodeObjectForKey:NSKeyedArchiveRootObjectKey];
         [unarchiver finishDecoding];
         if (![state isKindOfClass:[NSDictionary class]])
            return false;
         h->unit.fullState = (NSDictionary*)state;
      }
      return true;
   }

   // ---- Syphon inter-app video sharing ------------------------------------
   struct SyphonServerHandle
   {
      SyphonOpenGLServer* server = nil;
      std::string currentName;
   };

   struct SyphonClientHandle
   {
      SyphonOpenGLClient* client = nil;
      SyphonOpenGLImage* currentImage = nil;
      std::string currentAppName;
      std::string currentServerName;
      std::string currentUuid;
   };

   SyphonServerHandle* SyphonServerCreate(const std::string& serverName)
   {
      @autoreleasepool
      {
         CGLContextObj cglContext = CGLGetCurrentContext();
         if (!cglContext) return nullptr;

         NSString* nsName = serverName.empty() ? @"Infinite Output" : [NSString stringWithUTF8String:serverName.c_str()];
         SyphonOpenGLServer* s = [[SyphonOpenGLServer alloc] initWithName:nsName context:cglContext options:nil];
         if (s == nil) return nullptr;

         auto* h = new SyphonServerHandle();
         h->server = s;
         h->currentName = serverName;
         return h;
      }
   }

   void SyphonServerUpdateName(SyphonServerHandle* handle, const std::string& serverName)
   {
      if (!handle || !handle->server) return;
      if (handle->currentName == serverName) return;
      @autoreleasepool
      {
         NSString* nsName = serverName.empty() ? @"Infinite Output" : [NSString stringWithUTF8String:serverName.c_str()];
         handle->server.name = nsName;
         handle->currentName = serverName;
      }
   }

   void SyphonServerPublish(SyphonServerHandle* handle, unsigned int textureId, int width, int height, bool flipped)
   {
      if (!handle || !handle->server || textureId == 0 || width <= 0 || height <= 0) return;
      @autoreleasepool
      {
         [handle->server publishFrameTexture:textureId
                               textureTarget:GL_TEXTURE_2D
                                 imageRegion:NSMakeRect(0, 0, width, height)
                           textureDimensions:NSMakeSize(width, height)
                                     flipped:flipped ? YES : NO];
      }
   }

   bool SyphonServerHasClients(SyphonServerHandle* handle)
   {
      if (!handle || !handle->server) return false;
      return handle->server.hasClients;
   }

   void SyphonServerDestroy(SyphonServerHandle* handle)
   {
      if (!handle) return;
      @autoreleasepool
      {
         if (handle->server)
         {
            [handle->server stop];
            handle->server = nil;
         }
      }
      delete handle;
   }

   std::vector<SyphonServerInfo> SyphonGetAvailableServers()
   {
      std::vector<SyphonServerInfo> results;
      @autoreleasepool
      {
         NSArray* servers = [[SyphonServerDirectory sharedDirectory] servers];
         for (NSDictionary* desc in servers)
         {
            if (![desc isKindOfClass:[NSDictionary class]]) continue;
            NSString* app = [desc objectForKey:SyphonServerDescriptionAppNameKey];
            NSString* name = [desc objectForKey:SyphonServerDescriptionNameKey];
            NSString* uuid = [desc objectForKey:SyphonServerDescriptionUUIDKey];
            SyphonServerInfo info;
            info.appName = app ? [app UTF8String] : "";
            info.serverName = name ? [name UTF8String] : "";
            info.uuid = uuid ? [uuid UTF8String] : "";
            results.push_back(info);
         }
      }
      return results;
   }

   SyphonClientHandle* SyphonClientCreate()
   {
      auto* h = new SyphonClientHandle();
      return h;
   }

   bool SyphonClientConnect(SyphonClientHandle* handle, const std::string& appName, const std::string& serverName, const std::string& uuid)
   {
      if (!handle) return false;
      @autoreleasepool
      {
         if (handle->currentImage)
         {
            handle->currentImage = nil;
         }
         if (handle->client)
         {
            [handle->client stop];
            handle->client = nil;
         }

         CGLContextObj cglContext = CGLGetCurrentContext();
         if (!cglContext) return false;

         NSArray* servers = [[SyphonServerDirectory sharedDirectory] servers];
         NSDictionary* matchedDesc = nil;
         for (NSDictionary* desc in servers)
         {
            NSString* u = [desc objectForKey:SyphonServerDescriptionUUIDKey];
            if (!uuid.empty() && u && [u isEqualToString:[NSString stringWithUTF8String:uuid.c_str()]])
            {
               matchedDesc = desc;
               break;
            }
            NSString* a = [desc objectForKey:SyphonServerDescriptionAppNameKey];
            NSString* n = [desc objectForKey:SyphonServerDescriptionNameKey];
            if ((!appName.empty() && a && [a isEqualToString:[NSString stringWithUTF8String:appName.c_str()]]) &&
                (!serverName.empty() && n && [n isEqualToString:[NSString stringWithUTF8String:serverName.c_str()]]))
            {
               matchedDesc = desc;
               break;
            }
         }

         if (!matchedDesc && servers.count > 0 && appName.empty() && serverName.empty() && uuid.empty())
         {
            matchedDesc = [servers firstObject];
         }
         if (!matchedDesc) return false;

         handle->client = [[SyphonOpenGLClient alloc] initWithServerDescription:matchedDesc context:cglContext options:nil newFrameHandler:nil];
         if (!handle->client || !handle->client.isValid)
         {
            if (handle->client) [handle->client stop];
            handle->client = nil;
            return false;
         }

         handle->currentAppName = appName;
         handle->currentServerName = serverName;
         handle->currentUuid = uuid;
         return true;
      }
   }

   bool SyphonClientIsConnected(SyphonClientHandle* handle)
   {
      if (!handle || !handle->client) return false;
      return handle->client.isValid;
   }

   bool SyphonClientHasNewFrame(SyphonClientHandle* handle)
   {
      if (!handle || !handle->client) return false;
      return handle->client.hasNewFrame;
   }

   unsigned int SyphonClientGetFrameTexture(SyphonClientHandle* handle, int& outWidth, int& outHeight)
   {
      outWidth = 0;
      outHeight = 0;
      if (!handle || !handle->client || !handle->client.isValid) return 0;

      @autoreleasepool
      {
         SyphonOpenGLImage* img = [handle->client newFrameImage];
         if (img != nil)
         {
            handle->currentImage = img;
         }

         if (handle->currentImage != nil)
         {
            NSSize sz = handle->currentImage.textureSize;
            outWidth = (int)sz.width;
            outHeight = (int)sz.height;
            return handle->currentImage.textureName;
         }
         return 0;
      }
   }

   void SyphonClientDestroy(SyphonClientHandle* handle)
   {
      if (!handle) return;
      @autoreleasepool
      {
         handle->currentImage = nil;
         if (handle->client)
         {
            [handle->client stop];
            handle->client = nil;
         }
      }
      delete handle;
   }
}


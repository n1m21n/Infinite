#include "Platform.h"

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

#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>

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
                                 const std::string& audioPath, bool loopAudio)
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
               }
               else
               {
                  fprintf(stderr, "recorder: could not add audio track, continuing video-only\n");
                  audioInput = nil;
                  audioFile = nil;
               }
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
         if (audioFile != nil)
         {
            h->audioFormat = audioFile.processingFormat;
            h->audioSampleRate = audioSampleRate;
         }

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

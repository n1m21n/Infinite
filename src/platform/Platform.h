#pragma once

#include <string>
#include <vector>

// Thin macOS shims kept out of the C++ translation units.
namespace Platform
{
   // Native open panel filtered to image types. Returns "" if cancelled.
   std::string OpenImageDialog();

   // Decodes any image format the OS understands (png/jpeg/tiff/heic/webp/raw/...)
   // into tightly packed RGBA8, already row-flipped for OpenGL's bottom-up
   // texture convention. Returns false and fills outError on failure.
   bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& outPixels,
                      int& outWidth, int& outHeight, std::string& outError);

   // ---- video decoding ----------------------------------------------------
   // Opaque handle around an AVAssetReader that decodes frames in order.
   // Frames are delivered as RGBA8, already row-flipped for OpenGL.
   struct VideoHandle;

   std::string OpenVideoDialog();

   VideoHandle* VideoOpen(const std::string& path, std::string& outError);
   void VideoClose(VideoHandle* handle);
   int VideoWidth(VideoHandle* handle);
   int VideoHeight(VideoHandle* handle);
   double VideoDuration(VideoHandle* handle);

   // Decodes forward until the frame covering `seconds` is current. Returns true
   // when a new frame was produced (so the caller can skip re-uploading).
   // Seeking backwards restarts the reader, which is how looping works.
   bool VideoFrameAt(VideoHandle* handle, double seconds, std::vector<unsigned char>& outPixels);

   // ---- video recording ---------------------------------------------------
   struct RecorderHandle;

   RecorderHandle* RecorderStart(const std::string& path, int width, int height,
                                 int fps, std::string& outError);
   // `pixels` is RGBA8 bottom-up, exactly as glReadPixels returns it.
   bool RecorderAppend(RecorderHandle* handle, const std::vector<unsigned char>& pixels);
   bool RecorderStop(RecorderHandle* handle, std::string& outError);
   int RecorderFrameCount(RecorderHandle* handle);
}

#pragma once

// Turbo 0.53: one GPU path for every decoded video/camera frame.
// Uploads a Platform::VideoFrame's planes as they come from the decoder
// (YUV planes, packed BGR, or HAP's compressed BC blocks) through orphaned
// pixel-unpack buffers, then converts them in a single shader pass into the
// caller's RGBA8 texture in GL bottom-up order: YUV->RGB, BGR swizzle,
// HAP Q YCoCg decode, vertical flip and horizontal mirror all happen there.
// Main GL context only.

#include "platform/Platform.h"

class VideoTexture
{
public:
   VideoTexture() = default;
   ~VideoTexture();
   VideoTexture(const VideoTexture&) = delete;
   VideoTexture& operator=(const VideoTexture&) = delete;

   // Writes `frame` into dstTex (created/resized to frame.width x frame.height
   // GL_RGBA8 when needed; dstW/dstH track its allocated size). Returns false
   // and leaves dstTex untouched when the frame is empty or unsupported.
   bool Upload(const Platform::VideoFrame& frame, unsigned int& dstTex, int& dstW, int& dstH);

   // Frees the staging textures and buffers (dstTex belongs to the caller).
   void Release();

private:
   struct Plane
   {
      unsigned int tex = 0;
      unsigned int pbo[2] = { 0, 0 };
      int pboIndex = 0;
      int w = 0;
      int h = 0;
      unsigned int internalFormat = 0;
   };

   bool UploadPlane(Plane& p, int w, int h, unsigned int internalFormat, unsigned int format,
                    unsigned int type, bool compressed, const unsigned char* data, size_t bytes,
                    int alignment, bool linear);

   Plane mPlanes[3];
   unsigned int mFbo = 0;
};

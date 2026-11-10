#pragma once
// Real-time engine RHI (docs/plans/realtime-engine, stage R1): the thin layer between the engine and a GPU API.
// Metal first (MetalDevice.mm), Vulkan at R6; elsewhere CreateDevice() returns null and the GL renderer stays.
// Kept to the few objects the engine needs, so each backend is buildable and testable on its own.

#include <memory>
#include <string>
#include <vector>

namespace rhi
{
   enum class Format
   {
      RGBA8,  // 8-bit, sRGB-encoded by the caller
      RGBA16F // linear HDR
   };

   // A colour image both the engine and the GL app can use: the engine renders into it, the app's GL code samples it.
   // On macOS this is an IOSurface, seen by Metal as a texture and by GL as a rectangle texture (no copy).
   class SharedImage
   {
   public:
      virtual ~SharedImage() = default;
      virtual int Width() const = 0;
      virtual int Height() const = 0;
      // GL_TEXTURE_RECTANGLE name in the app's GL context, or 0 if the GL side could not bind it. Texel coordinates
      // are in pixels, not 0..1.
      virtual unsigned int GlTexture() const = 0;
      // Test aid: draws a coloured triangle over a clear colour with the backend, and blocks until the GPU is done.
      virtual bool DrawTestTriangle(const float clearRgba[4], const float triRgb[3]) = 0;
   };

   class Device
   {
   public:
      virtual ~Device() = default;
      virtual std::string Name() const = 0;
      virtual std::unique_ptr<SharedImage> CreateSharedImage(int width, int height, Format format) = 0;
   };

   // The backend for this platform, or null when there is none yet. Must be called with the app's GL context current.
   std::unique_ptr<Device> CreateDevice();
}

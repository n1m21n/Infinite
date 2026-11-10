#pragma once
// Real-time engine RHI (docs/plans/realtime-engine, stage R1): the thin layer between the engine and a GPU API.
// Metal first (MetalDevice.mm), Vulkan at R6; elsewhere CreateDevice() returns null and the GL renderer stays.
// Kept to the few objects the engine needs, so each backend is buildable and testable on its own.

#include <memory>
#include <string>
#include <vector>

#include "engine/Scene.h"

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

   // Forward raster of an engine::Scene into a SharedImage: lit PBR (GGX), up to 8 lights, gradient sky, tonemap.
   class RasterRenderer
   {
   public:
      virtual ~RasterRenderer() = default;
      // Static meshes, referenced by Instance::mesh. Replaces any earlier set.
      virtual bool UploadMeshes(const std::vector<engine::MeshData>& meshes) = 0;
      // Draws one frame and blocks until the GPU is done. gpuMs, if given, gets the GPU's own time for it.
      virtual bool Render(SharedImage& target, const engine::Frame& frame, double* gpuMs) = 0;
      // Progressive path tracer: adds `spp` samples per pixel to the running average and writes the picture.
      // reset = true starts over (new camera/scene). Returns false when the device has no ray tracing.
      virtual bool PathTrace(SharedImage&, const engine::Frame&, int /*spp*/, int /*bounces*/, bool /*reset*/, double* /*gpuMs*/) { return false; }
   };

   class Device
   {
   public:
      virtual ~Device() = default;
      virtual std::string Name() const = 0;
      virtual std::unique_ptr<SharedImage> CreateSharedImage(int width, int height, Format format) = 0;
      virtual std::unique_ptr<RasterRenderer> CreateRasterRenderer() = 0;
   };

   // The backend for this platform, or null when there is none yet. Must be called with the app's GL context current.
   std::unique_ptr<Device> CreateDevice();
}

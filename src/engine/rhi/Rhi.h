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
      // Replaces one mesh uploaded earlier (an animated source), rebuilding only its acceleration structure.
      virtual bool UpdateMesh(int /*index*/, const engine::MeshData& /*mesh*/) { return false; }
      // Draws one frame and blocks until the GPU is done. gpuMs, if given, gets the GPU's own time for it.
      virtual bool Render(SharedImage& target, const engine::Frame& frame, double* gpuMs) = 0;
      // Progressive path tracer: adds `spp` samples per pixel to the running average and writes the picture.
      // reset = true starts over (new camera/scene). Returns false when the device has no ray tracing.
      virtual bool PathTrace(SharedImage&, const engine::Frame&, int /*spp*/, int /*bounces*/, bool /*reset*/, double* /*gpuMs*/) { return false; }
      // After PathTrace: the 95th-percentile relative error of the accumulated mean over the picture (0.02 = 2 %),
      // or -1 when it was not measured. The export stops sampling a frame once this is small.
      virtual float PathTraceNoise() const { return -1.0f; }
      // Row 8.4: temporal upscale of the last Render (into `low`, drawn with Frame::keepDepth and a sub-pixel jitter
      // of jx, jy pixels) to `out`. reset = new camera or scene. False when the device has no temporal scaler.
      virtual bool Upscale(SharedImage& /*low*/, SharedImage& /*out*/, float /*jx*/, float /*jy*/, bool /*reset*/) { return false; }
      // Times the shader was rebuilt for a new set of Field material programs (Frame::fieldMslKey).
      virtual int FieldLibraryBuilds() const { return 0; }
      // REALISM R18: bytes this renderer's GPU device has allocated now (meshes, BVHs, textures, targets), 0 = unknown.
      virtual size_t GpuBytes() const { return 0; }
   };

   class Device
   {
   public:
      virtual ~Device() = default;
      virtual std::string Name() const = 0;
      virtual std::unique_ptr<SharedImage> CreateSharedImage(int width, int height, Format format) = 0;
      virtual std::unique_ptr<RasterRenderer> CreateRasterRenderer() = 0;
      // Field Pixel on Metal, a test aid for now (OPUS_PLAN row 1.1): compiles `msl` (an EmitMsl result, fast math
      // on, as GL) and, when w > 0, draws fld_fs over a w x h RGBA32F target. `slots` are the float4 uniform slots, the
      // source is a 1x1 black texture and the state bank is cleared to bankInit. px gets w*h*4 floats, row 0 first
      // (uv.y near 0, the same memory order as a GL readback). err gets the compiler's message on failure.
      virtual bool RunFieldPixel(const std::string& /*msl*/, int /*w*/, int /*h*/, const std::vector<float>& /*slots*/,
                                 const float* /*bankInit*/, std::vector<float>& /*px*/, std::string& err)
      {
         err = "no Field Pixel backend";
         return false;
      }
   };

   // The backend for this platform, or null when there is none yet. Must be called with the app's GL context current.
   std::unique_ptr<Device> CreateDevice();

   // The first shader or pipeline that failed to build ("pt pipeline: <compiler line>"), empty while none has.
   // A failed pipeline is an error, not a quiet fallback (DESIGN.md L2): Render 3D shows it on the node.
   const std::string& LastError();
}

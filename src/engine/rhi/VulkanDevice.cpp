// Real-time engine on Vulkan (OPUS_PLAN row 9.2): the skeleton. Design and the Metal -> Vulkan table:
// docs/plans/realtime-engine/designs/9-vulkan.md. Built only with INFINITE_VULKAN (needs the Vulkan SDK, not yet
// approved), so without it this file is empty and RhiStub.cpp stays the backend on Windows and Linux.
// Sections follow MetalDevice.mm in order: shared image, raster renderer (upload, render passes, path trace), device.
#ifdef INFINITE_VULKAN

#include "engine/rhi/Rhi.h"

#include <vulkan/vulkan.h>

namespace rhi
{
namespace
{
   std::string gLastError;

   // ---- shared image (MetalImage): VkImage on exported memory, imported by GL (GL_EXT_memory_object)
   class VulkanImage final : public SharedImage
   {
   public:
      VulkanImage(int w, int h, Format f) : mW(w), mH(h), mFormat(f) {}
      int Width() const override { return mW; }
      int Height() const override { return mH; }
      unsigned int GlTexture() const override { return mGl; }
      bool DrawTestTriangle(const float[4], const float[3]) override { return false; } // 9.3: tri.vert + clear

   private:
      int mW = 0, mH = 0;
      Format mFormat = Format::RGBA8;
      VkImage mImage = VK_NULL_HANDLE;
      VkDeviceMemory mMemory = VK_NULL_HANDLE;
      unsigned int mGl = 0;
   };

   // ---- raster renderer (MetalRaster)
   class VulkanRaster final : public RasterRenderer
   {
   public:
      // MakeGpu / BuildBlas: device-local buffers per stream; BLAS by ray query or the compute LBVH (9.4).
      bool UploadMeshes(const std::vector<engine::MeshData>&) override { return false; }
      // Render: maps, TLAS, cull.comp + args.comp, shadow, prepass, vis, r.frag MRT, tr, res, a-trous, DOF, bloom.
      bool Render(SharedImage&, const engine::Frame&, double*) override { return false; }
      // PathTrace: pt.comp with rayQueryEXT, or traceRay() over the compute BVH.
   };

   // ---- device (MetalDevice)
   class VulkanDevice final : public Device
   {
   public:
      std::string Name() const override { return "Vulkan (skeleton)"; }
      std::unique_ptr<SharedImage> CreateSharedImage(int w, int h, Format f) override { return std::make_unique<VulkanImage>(w, h, f); }
      std::unique_ptr<RasterRenderer> CreateRasterRenderer() override { return std::make_unique<VulkanRaster>(); }

   private:
      VkInstance mInstance = VK_NULL_HANDLE;
      VkPhysicalDevice mPhys = VK_NULL_HANDLE;
      VkDevice mDev = VK_NULL_HANDLE;
      VkQueue mQueue = VK_NULL_HANDLE;
   };
}

// 9.3 returns a VulkanDevice once SharedImage and Render work; until then the GL renderer stays.
std::unique_ptr<Device> CreateDevice() { return nullptr; }
const std::string& LastError() { return gLastError; }
}

#endif

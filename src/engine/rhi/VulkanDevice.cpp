// Real-time engine on Vulkan (OPUS_PLAN rows 9.2, 9.3): the raster renderer. Design and the Metal -> Vulkan table:
// docs/plans/realtime-engine/designs/9-vulkan.md. Built only with INFINITE_VULKAN (Vulkan 1.3; MoltenVK on macOS,
// picked with INFINITE_RHI=vulkan), so without it this file is empty and Metal / RhiStub.cpp stay the backends.
// Sections follow MetalDevice.mm in order: context, shared image, raster renderer, device.
// The picture is MetalDevice.mm's no-ray direct pass (raster.frag, written by vk/VkShaderGen.cpp from the same
// shading source) plus its shadow map and HDRI background. Not here yet: ray passes (row 9.4), hair, Field material
// programs, the light froxels past 8 lights, fog, instance batches, depth of field, bloom.
// Orientation: no y flip. Vulkan's framebuffer row 0 is memory row 0, which GL reads as its bottom row, so a GL
// projection lands in memory as Metal's flipped one does; the GL counter-clockwise front face reads clockwise here.
// MoltenVK ships arm64 only, so a universal macOS build has no Vulkan in its x86_64 slice.
#if defined(INFINITE_VULKAN) && (!defined(__APPLE__) || defined(__arm64__))

#include "engine/rhi/RasterRecords.h"
#include "engine/rhi/Rhi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

#include "vkshaders/bg_frag.h"
#include "vkshaders/bg_vert.h"
#include "vkshaders/caus_comp.h"
#include "vkshaders/dnatrous_comp.h"
#include "vkshaders/dnprep_comp.h"
#include "vkshaders/pt_comp.h"
#include "vkshaders/ptres_frag.h"
#include "vkshaders/raster_frag.h"
#include "vkshaders/raster_vert.h"
#include "vkshaders/shadow_vert.h"
#include "vkshaders/tri_frag.h"
#include "vkshaders/tri_vert.h"

#ifdef __APPLE__
#include <IOSurface/IOSurface.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include "gl3.h"
#endif

namespace rhi
{
void VulkanFail(const std::string& what); // MetalDevice.mm / RhiStub.cpp: the node's error line (LastError)

namespace
{
   using namespace rr;

   // ---- context: one instance, device and queue, shared by the images and renderers made from it
   struct Ctx
   {
      VkInstance inst = VK_NULL_HANDLE;
      VkPhysicalDevice phys = VK_NULL_HANDLE;
      VkDevice dev = VK_NULL_HANDLE;
      VkQueue queue = VK_NULL_HANDLE;
      uint32_t qfam = 0;
      VkCommandPool pool = VK_NULL_HANDLE;
      VkPhysicalDeviceMemoryProperties mem = {};
      VkPhysicalDeviceProperties props = {};
      std::string name;
      // The test triangle's pipelines, one per target format.
      VkPipelineLayout triLayout = VK_NULL_HANDLE;
      VkPipeline triPso[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

      ~Ctx()
      {
         if (dev)
         {
            vkDeviceWaitIdle(dev);
            for (VkPipeline p : triPso)
               if (p) vkDestroyPipeline(dev, p, nullptr);
            if (triLayout) vkDestroyPipelineLayout(dev, triLayout, nullptr);
            if (pool) vkDestroyCommandPool(dev, pool, nullptr);
            vkDestroyDevice(dev, nullptr);
         }
         if (inst)
            vkDestroyInstance(inst, nullptr);
      }
      int MemType(uint32_t bits, VkMemoryPropertyFlags want) const
      {
         for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want)
               return (int)i;
         return -1;
      }
      VkCommandBuffer Begin() const
      {
         VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
         ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
         VkCommandBuffer cb = VK_NULL_HANDLE;
         if (vkAllocateCommandBuffers(dev, &ai, &cb) != VK_SUCCESS)
            return VK_NULL_HANDLE;
         VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
         bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
         vkBeginCommandBuffer(cb, &bi);
         return cb;
      }
      // Ends, submits and waits; frees the command buffer.
      bool Submit(VkCommandBuffer cb) const
      {
         vkEndCommandBuffer(cb);
         VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
         VkFence fence = VK_NULL_HANDLE;
         vkCreateFence(dev, &fi, nullptr, &fence);
         VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
         si.commandBufferCount = 1; si.pCommandBuffers = &cb;
         bool ok = vkQueueSubmit(queue, 1, &si, fence) == VK_SUCCESS && vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
         vkDestroyFence(dev, fence, nullptr);
         vkFreeCommandBuffers(dev, pool, 1, &cb);
         return ok;
      }
   };

   // A host-visible buffer (unified memory on Apple; on a discrete GPU this is the upload heap).
   struct Buf
   {
      VkBuffer buf = VK_NULL_HANDLE;
      VkDeviceMemory mem = VK_NULL_HANDLE;
      void* map = nullptr;
      VkDeviceSize size = 0;
   };
   bool MakeBuf(const Ctx& c, VkDeviceSize size, VkBufferUsageFlags use, Buf& b)
   {
      size = std::max<VkDeviceSize>(size, 16);
      VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bi.size = size; bi.usage = use; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (vkCreateBuffer(c.dev, &bi, nullptr, &b.buf) != VK_SUCCESS)
         return false;
      VkMemoryRequirements mr;
      vkGetBufferMemoryRequirements(c.dev, b.buf, &mr);
      const int mt = c.MemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
      ai.allocationSize = mr.size; ai.memoryTypeIndex = (uint32_t)std::max(mt, 0);
      if (mt < 0 || vkAllocateMemory(c.dev, &ai, nullptr, &b.mem) != VK_SUCCESS)
         return false;
      vkBindBufferMemory(c.dev, b.buf, b.mem, 0);
      vkMapMemory(c.dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map);
      b.size = size;
      return b.map != nullptr;
   }
   void FreeBuf(const Ctx& c, Buf& b)
   {
      if (b.mem) { vkUnmapMemory(c.dev, b.mem); vkFreeMemory(c.dev, b.mem, nullptr); }
      if (b.buf) vkDestroyBuffer(c.dev, b.buf, nullptr);
      b = Buf{};
   }
   bool BufWith(const Ctx& c, const void* data, size_t bytes, VkBufferUsageFlags use, Buf& b)
   {
      if (!MakeBuf(c, bytes, use, b))
         return false;
      if (data && bytes) std::memcpy(b.map, data, bytes);
      else std::memset(b.map, 0, (size_t)b.size);
      return true;
   }

   // A device-local image with its view and the layout it was last left in.
   struct Img
   {
      VkImage img = VK_NULL_HANDLE;
      VkDeviceMemory mem = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
      VkFormat fmt = VK_FORMAT_UNDEFINED;
      VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
      int w = 0, h = 0;
      uint32_t mips = 1;
      unsigned long long key = 0;
      bool used = false;
   };
   bool IsDepth(VkFormat f) { return f == VK_FORMAT_D32_SFLOAT; }
   bool MakeImg(const Ctx& c, int w, int h, VkFormat fmt, uint32_t mips, VkImageUsageFlags use, Img& o)
   {
      VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent = { (uint32_t)w, (uint32_t)h, 1 };
      ii.mipLevels = mips; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
      ii.usage = use; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      if (vkCreateImage(c.dev, &ii, nullptr, &o.img) != VK_SUCCESS)
         return false;
      VkMemoryRequirements mr;
      vkGetImageMemoryRequirements(c.dev, o.img, &mr);
      const int mt = c.MemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
      ai.allocationSize = mr.size; ai.memoryTypeIndex = (uint32_t)std::max(mt, 0);
      if (mt < 0 || vkAllocateMemory(c.dev, &ai, nullptr, &o.mem) != VK_SUCCESS)
         return false;
      vkBindImageMemory(c.dev, o.img, o.mem, 0);
      VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      vi.image = o.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
      vi.subresourceRange = { (VkImageAspectFlags)(IsDepth(fmt) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, mips, 0, 1 };
      if (vkCreateImageView(c.dev, &vi, nullptr, &o.view) != VK_SUCCESS)
         return false;
      o.fmt = fmt; o.w = w; o.h = h; o.mips = mips; o.layout = VK_IMAGE_LAYOUT_UNDEFINED;
      return true;
   }
   void FreeImg(const Ctx& c, Img& o)
   {
      if (o.view) vkDestroyImageView(c.dev, o.view, nullptr);
      if (o.img) vkDestroyImage(c.dev, o.img, nullptr);
      if (o.mem) vkFreeMemory(c.dev, o.mem, nullptr);
      o = Img{};
   }
   // One barrier covering every mip, all prior work before all later work (the frame is small and blocking).
   void ToLayout(VkCommandBuffer cb, Img& o, VkImageLayout to)
   {
      if (o.layout == to)
         return;
      VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
      b.oldLayout = o.layout; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = o.img;
      b.subresourceRange = { (VkImageAspectFlags)(IsDepth(o.fmt) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, o.mips, 0, 1 };
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
      o.layout = to;
   }
   // Uploads level 0 from host bytes and builds the mip chain by linear blits (Metal's generateMipmapsForTexture).
   bool UploadMipped(const Ctx& c, Img& o, const void* bytes, size_t len)
   {
      Buf st;
      if (!BufWith(c, bytes, len, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, st))
         return false;
      VkCommandBuffer cb = c.Begin();
      ToLayout(cb, o, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkBufferImageCopy r = {};
      r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      r.imageExtent = { (uint32_t)o.w, (uint32_t)o.h, 1 };
      vkCmdCopyBufferToImage(cb, st.buf, o.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
      int w = o.w, h = o.h;
      for (uint32_t l = 1; l < o.mips; l++)
      {
         VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
         b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
         b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         b.image = o.img; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 1, 0, 1 };
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
         const int nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
         VkImageBlit bl = {};
         bl.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 0, 1 };
         bl.srcOffsets[1] = { w, h, 1 };
         bl.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1 };
         bl.dstOffsets[1] = { nw, nh, 1 };
         vkCmdBlitImage(cb, o.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, o.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
         w = nw; h = nh;
      }
      // Levels 0..n-2 are TRANSFER_SRC now, the last TRANSFER_DST: move each to shader read.
      for (uint32_t l = 0; l < o.mips; l++)
      {
         VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
         b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
         b.oldLayout = l + 1 < o.mips ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
         b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         b.image = o.img; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, l, 1, 0, 1 };
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
      }
      o.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      const bool ok = c.Submit(cb);
      FreeBuf(c, st);
      return ok;
   }
   uint32_t MipCount(int w, int h) { uint32_t n = 1; for (int m = std::max(w, h); m > 1; m >>= 1) n++; return n; }
   uint16_t Half(float f)
   {
      uint32_t x; std::memcpy(&x, &f, 4);
      const uint32_t s = (x >> 16) & 0x8000u;
      int e = (int)((x >> 23) & 0xff) - 127 + 15;
      uint32_t m = x & 0x7fffffu;
      if (e <= 0)
      {
         if (e < -10) return (uint16_t)s;
         m |= 0x800000u;
         const uint32_t sh = (uint32_t)(14 - e);
         uint32_t hm = m >> sh;
         if ((m >> (sh - 1)) & 1u) hm++; // round half up, as the hardware's conversion near enough
         return (uint16_t)(s | hm);
      }
      if (e >= 31) return (uint16_t)(s | 0x7c00u);
      uint32_t h = s | ((uint32_t)e << 10) | (m >> 13);
      if (m & 0x1000u) h++; // round to nearest
      return (uint16_t)h;
   }
   VkShaderModule Module(const Ctx& c, const uint32_t* code, size_t bytes)
   {
      VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      ci.codeSize = bytes; ci.pCode = code;
      VkShaderModule m = VK_NULL_HANDLE;
      vkCreateShaderModule(c.dev, &ci, nullptr, &m);
      return m;
   }
   // A graphics pipeline for dynamic rendering: viewport, scissor and cull mode are set per draw.
   struct PsoDesc
   {
      VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
      VkPipelineLayout layout = VK_NULL_HANDLE;
      VkFormat color = VK_FORMAT_UNDEFINED, depth = VK_FORMAT_UNDEFINED;
      bool meshInput = false, posOnly = false, depthTest = true, depthWrite = true;
   };
   VkPipeline MakePso(const Ctx& c, const PsoDesc& d)
   {
      VkPipelineShaderStageCreateInfo st[2] = {};
      st[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, d.vs, "main", nullptr };
      st[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, d.fs, "main", nullptr };
      // Streams as Metal's buffers 0, 12, 13: pos+normal (24 bytes), uv (8), wear (12).
      const VkVertexInputBindingDescription bind[3] = { { 0, 24, VK_VERTEX_INPUT_RATE_VERTEX }, { 1, 8, VK_VERTEX_INPUT_RATE_VERTEX },
                                                        { 2, 12, VK_VERTEX_INPUT_RATE_VERTEX } };
      const VkVertexInputAttributeDescription attr[4] = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
                                                          { 2, 1, VK_FORMAT_R32G32_SFLOAT, 0 }, { 3, 2, VK_FORMAT_R32G32B32_SFLOAT, 0 } };
      VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
      if (d.meshInput)
      {
         vi.vertexBindingDescriptionCount = d.posOnly ? 1 : 3; vi.pVertexBindingDescriptions = bind;
         vi.vertexAttributeDescriptionCount = d.posOnly ? 1 : 4; vi.pVertexAttributeDescriptions = attr;
      }
      VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
      ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
      vp.viewportCount = 1; vp.scissorCount = 1;
      VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
      rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE; rs.lineWidth = 1.0f;
      VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
      ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
      ds.depthTestEnable = d.depthTest ? VK_TRUE : VK_FALSE; ds.depthWriteEnable = d.depthWrite ? VK_TRUE : VK_FALSE;
      ds.depthCompareOp = VK_COMPARE_OP_LESS;
      VkPipelineColorBlendAttachmentState ba = {};
      ba.colorWriteMask = 0xf;
      VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
      cbs.attachmentCount = d.color != VK_FORMAT_UNDEFINED ? 1 : 0; cbs.pAttachments = &ba;
      const VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE };
      VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
      dy.dynamicStateCount = 3; dy.pDynamicStates = dyn;
      VkPipelineRenderingCreateInfo ri = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
      ri.colorAttachmentCount = d.color != VK_FORMAT_UNDEFINED ? 1 : 0; ri.pColorAttachmentFormats = &d.color;
      ri.depthAttachmentFormat = d.depth;
      VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
      pi.pNext = &ri;
      pi.stageCount = d.fs ? 2 : 1; pi.pStages = st;
      pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp; pi.pRasterizationState = &rs;
      pi.pMultisampleState = &ms; pi.pDepthStencilState = d.depth != VK_FORMAT_UNDEFINED ? &ds : nullptr; pi.pColorBlendState = &cbs;
      pi.pDynamicState = &dy; pi.layout = d.layout;
      VkPipeline p = VK_NULL_HANDLE;
      if (vkCreateGraphicsPipelines(c.dev, VK_NULL_HANDLE, 1, &pi, nullptr, &p) != VK_SUCCESS)
         return VK_NULL_HANDLE;
      return p;
   }
   void Viewport(VkCommandBuffer cb, int w, int h)
   {
      const VkViewport v = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
      const VkRect2D s = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
      vkCmdSetViewport(cb, 0, 1, &v);
      vkCmdSetScissor(cb, 0, 1, &s);
   }

   // ---- shared image (MetalImage): the IOSurface GL samples, filled from a Vulkan colour image after each frame.
   // A copy (image -> buffer -> surface), not the zero-copy import (VK_EXT_metal_objects); the copy is what works on
   // every driver, and 9.3's gate is the picture.
   class VulkanImage final : public SharedImage
   {
   public:
      VulkanImage(std::shared_ptr<Ctx> c, int w, int h, Format f) : mC(std::move(c)), mW(w), mH(h), mHalf(f == Format::RGBA16F)
      {
         const Ctx& C = *mC;
         if (!MakeImg(C, w, h, Fmt(), 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mImg) ||
             !MakeBuf(C, (VkDeviceSize)w * h * Bpp(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, mRead))
            return;
#ifdef __APPLE__
         NSDictionaryLess();
#else
         // No shared surface here: a plain GL rectangle texture that Publish() uploads the readback into.
         glGenTextures(1, &mGl);
         glBindTexture(GL_TEXTURE_RECTANGLE, mGl);
         if (mHalf)
            glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
         else
            glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
         glBindTexture(GL_TEXTURE_RECTANGLE, 0);
#endif
         mOk = true;
      }
      ~VulkanImage() override
      {
         vkDeviceWaitIdle(mC->dev);
         FreeImg(*mC, mImg);
         FreeBuf(*mC, mRead);
#ifdef __APPLE__
         if (mGl) glDeleteTextures(1, &mGl);
         if (mSurface) CFRelease(mSurface);
#else
         if (mGl) glDeleteTextures(1, &mGl);
#endif
      }
      bool Ok() const { return mOk && mGl != 0; }
      VkFormat Fmt() const { return mHalf ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B8G8R8A8_UNORM; }
      int Bpp() const { return mHalf ? 8 : 4; }
      Img& Target() { return mImg; }
      int Width() const override { return mW; }
      int Height() const override { return mH; }
      unsigned int GlTexture() const override { return mGl; }
      // Records the copy to the readback buffer; Publish() moves it to the surface once the GPU is done.
      void RecordReadback(VkCommandBuffer cb)
      {
         ToLayout(cb, mImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
         VkBufferImageCopy r = {};
         r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
         r.imageExtent = { (uint32_t)mW, (uint32_t)mH, 1 };
         vkCmdCopyImageToBuffer(cb, mImg.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mRead.buf, 1, &r);
         VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
         mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
         vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
      }
      void Publish()
      {
#ifdef __APPLE__
         if (!mSurface)
            return;
         IOSurfaceLock(mSurface, 0, nullptr);
         uint8_t* dst = (uint8_t*)IOSurfaceGetBaseAddress(mSurface);
         const size_t stride = IOSurfaceGetBytesPerRow(mSurface), row = (size_t)mW * Bpp();
         for (int y = 0; y < mH; y++)
            std::memcpy(dst + (size_t)y * stride, (const uint8_t*)mRead.map + (size_t)y * row, row);
         IOSurfaceUnlock(mSurface, 0, nullptr);
#else
         if (!mGl)
            return;
         glBindTexture(GL_TEXTURE_RECTANGLE, mGl);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         if (mHalf)
            glTexSubImage2D(GL_TEXTURE_RECTANGLE, 0, 0, 0, mW, mH, GL_RGBA, GL_HALF_FLOAT, mRead.map);
         else
            glTexSubImage2D(GL_TEXTURE_RECTANGLE, 0, 0, 0, mW, mH, GL_BGRA, GL_UNSIGNED_BYTE, mRead.map);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
         glBindTexture(GL_TEXTURE_RECTANGLE, 0);
#endif
      }
      bool DrawTestTriangle(const float clear[4], const float tri[3]) override
      {
         Ctx& C = *mC;
         const int fi = mHalf ? 1 : 0;
         if (!C.triPso[fi])
         {
            if (!C.triLayout)
            {
               VkPushConstantRange pr = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
               VkPipelineLayoutCreateInfo li = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
               li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pr;
               vkCreatePipelineLayout(C.dev, &li, nullptr, &C.triLayout);
            }
            PsoDesc d;
            d.vs = Module(C, kVkTriVert, sizeof(kVkTriVert)); d.fs = Module(C, kVkTriFrag, sizeof(kVkTriFrag));
            d.layout = C.triLayout; d.color = Fmt();
            C.triPso[fi] = MakePso(C, d);
            vkDestroyShaderModule(C.dev, d.vs, nullptr);
            vkDestroyShaderModule(C.dev, d.fs, nullptr);
            if (!C.triPso[fi])
            {
               VulkanFail("vulkan triangle pipeline: not built");
               return false;
            }
         }
         VkCommandBuffer cb = C.Begin();
         ToLayout(cb, mImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
         VkRenderingAttachmentInfo ca = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
         ca.imageView = mImg.view; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
         std::memcpy(ca.clearValue.color.float32, clear, 16);
         VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
         ri.renderArea = { { 0, 0 }, { (uint32_t)mW, (uint32_t)mH } }; ri.layerCount = 1;
         ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
         vkCmdBeginRendering(cb, &ri);
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, C.triPso[fi]);
         Viewport(cb, mW, mH);
         vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
         const float pc[4] = { tri[0], tri[1], tri[2], 1.0f };
         vkCmdPushConstants(cb, C.triLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
         vkCmdDraw(cb, 3, 1, 0, 0);
         vkCmdEndRendering(cb);
         RecordReadback(cb);
         if (!C.Submit(cb))
            return false;
         Publish();
         return true;
      }

   private:
#ifdef __APPLE__
      // The surface and its GL rectangle texture, as MetalImage makes them.
      void NSDictionaryLess()
      {
         const int bpe = Bpp();
         const int pf = mHalf ? 'RGhA' : 'BGRA';
         CFMutableDictionaryRef props = CFDictionaryCreateMutable(nullptr, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
         auto put = [&](CFStringRef k, int v) {
            CFNumberRef n = CFNumberCreate(nullptr, kCFNumberIntType, &v);
            CFDictionarySetValue(props, k, n);
            CFRelease(n);
         };
         put(kIOSurfaceWidth, mW); put(kIOSurfaceHeight, mH); put(kIOSurfaceBytesPerElement, bpe); put(kIOSurfacePixelFormat, pf);
         mSurface = IOSurfaceCreate(props);
         CFRelease(props);
         CGLContextObj ctx = CGLGetCurrentContext();
         if (!mSurface || !ctx)
            return;
         glGenTextures(1, &mGl);
         glBindTexture(GL_TEXTURE_RECTANGLE, mGl);
         const CGLError err = mHalf ? CGLTexImageIOSurface2D(ctx, GL_TEXTURE_RECTANGLE, GL_RGBA16F, mW, mH, GL_RGBA, GL_HALF_FLOAT, mSurface, 0)
                                    : CGLTexImageIOSurface2D(ctx, GL_TEXTURE_RECTANGLE, GL_RGBA, mW, mH, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mSurface, 0);
         if (err != kCGLNoError)
         {
            glDeleteTextures(1, &mGl);
            mGl = 0;
         }
         glBindTexture(GL_TEXTURE_RECTANGLE, 0);
      }
      IOSurfaceRef mSurface = nullptr;
#endif
      std::shared_ptr<Ctx> mC;
      int mW, mH;
      bool mHalf;
      bool mOk = false;
      Img mImg;
      Buf mRead;
      unsigned int mGl = 0;
   };

   // ---- raster renderer (MetalRaster, its no-ray direct pass)
   struct FramePush { float lvp[16]; float bg[4]; float clr[4]; float invVP[16]; };
   static_assert(sizeof(FramePush) == 160, "raster PC block");

   class VulkanRaster final : public RasterRenderer
   {
   public:
      explicit VulkanRaster(std::shared_ptr<Ctx> c) : mC(std::move(c)) { mOk = Init(); }
      ~VulkanRaster() override
      {
         const Ctx& C = *mC;
         vkDeviceWaitIdle(C.dev);
         for (Gpu& g : mMeshes) FreeGpu(g);
         for (Img& m : mMapCache) FreeImg(C, m);
         FreeImg(C, mWhite); FreeImg(C, mEnv); FreeImg(C, mEnvDummy); FreeImg(C, mShadow); FreeImg(C, mDepth);
         FreeBuf(C, mFrameBuf); FreeBuf(C, mInstBuf);
         for (VkPipeline p : { mPso[0], mPso[1], mBgPso[0], mBgPso[1], mShPso })
            if (p) vkDestroyPipeline(C.dev, p, nullptr);
         for (VkSampler s : { mSmpShadow, mSmpEnv, mSmpC, mSmpR, mSmpM })
            if (s) vkDestroySampler(C.dev, s, nullptr);
         if (mLayout) vkDestroyPipelineLayout(C.dev, mLayout, nullptr);
         if (mPool) vkDestroyDescriptorPool(C.dev, mPool, nullptr);
         if (mSetLayout) vkDestroyDescriptorSetLayout(C.dev, mSetLayout, nullptr);
         if (mQuery) vkDestroyQueryPool(C.dev, mQuery, nullptr);
         PtFree();
      }
      bool Ok() const { return mOk; }

      bool UploadMeshes(const std::vector<engine::MeshData>& meshes) override
      {
         vkDeviceWaitIdle(mC->dev);
         for (Gpu& g : mMeshes) FreeGpu(g);
         mMeshes.assign(meshes.size(), Gpu{});
         mCpu.assign(meshes.size(), Cpu{});
         mMeshGen++;
         for (size_t i = 0; i < meshes.size(); i++)
         {
            KeepCpu(meshes[i], mCpu[i]);
            if (!MakeGpu(meshes[i], mMeshes[i]))
               return false;
         }
         return true;
      }
      bool UpdateMesh(int index, const engine::MeshData& mesh) override
      {
         if (index < 0 || index >= (int)mMeshes.size())
            return false;
         vkDeviceWaitIdle(mC->dev);
         FreeGpu(mMeshes[(size_t)index]);
         KeepCpu(mesh, mCpu[(size_t)index]);
         mMeshGen++;
         return MakeGpu(mesh, mMeshes[(size_t)index]);
      }

      bool Render(SharedImage& target, const engine::Frame& fr, double* gpuMs) override
      {
         if (!mOk)
            return false;
         const Ctx& C = *mC;
         VulkanImage& img = static_cast<VulkanImage&>(target);
         const int fi = img.Bpp() == 8 ? 1 : 0;
         if (!Pipelines(fi, img.Fmt()) || !EnsureDepth(img.Width(), img.Height()))
            return false;
         FrameU f = {};
         FillFrameRecord(f, fr, EnsureEnv(fr), mLightAll);
         const size_t ni = std::max<size_t>(fr.instances.size(), 1);
         if (mInstBuf.size < ni * sizeof(InstU))
         {
            vkDeviceWaitIdle(C.dev);
            FreeBuf(C, mInstBuf);
            if (!MakeBuf(C, ni * sizeof(InstU) * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mInstBuf))
               return false;
         }
         BuildRunRecords(fr, (int)mMeshes.size(), (InstU*)mInstBuf.map, mRuns);
         PrepareMaps(fr);

         // CPU first: the shadow fit decides which depth image the main pass samples, and both descriptor sets are
         // written before recording (a set may not change once a command buffer has bound it).
         FramePush pc = {};
         const bool shadow = ShadowPrepare(fr, f, pc.lvp);
         f.gi[0] = 0.0f; f.gi[1] = fr.giStrength; f.gi[2] = (float)fr.frameIndex; f.gi[3] = (float)fr.giRays;
         f.shQ[2] = 0.0f; f.shQ[3] = fr.rayBias;
         std::memcpy(mFrameBuf.map, &f, sizeof(f));
         if (!EnsureDummyShadow())
            return false;
         WriteSet(mSet[0], shadow ? mShadow : mShadowDum);
         WriteSet(mSet[1], mShadowDum);

         VkCommandBuffer cb = C.Begin();
         if (mQuery) { vkCmdResetQueryPool(cb, mQuery, 0, 2); vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mQuery, 0); }
         if (shadow)
            ShadowRecord(cb, fr, pc.lvp);
         ToLayout(cb, shadow ? mShadow : mShadowDum, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

         Img& tgt = img.Target();
         ToLayout(cb, tgt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
         ToLayout(cb, mDepth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
         VkRenderingAttachmentInfo ca = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
         ca.imageView = tgt.view; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
         ca.clearValue.color.float32[0] = fr.clearSrgb[0]; ca.clearValue.color.float32[1] = fr.clearSrgb[1];
         ca.clearValue.color.float32[2] = fr.clearSrgb[2]; ca.clearValue.color.float32[3] = 1.0f;
         VkRenderingAttachmentInfo da = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
         da.imageView = mDepth.view; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
         da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
         da.clearValue.depthStencil.depth = 1.0f;
         VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
         ri.renderArea = { { 0, 0 }, { (uint32_t)img.Width(), (uint32_t)img.Height() } }; ri.layerCount = 1;
         ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca; ri.pDepthAttachment = &da;
         vkCmdBeginRendering(cb, &ri);
         Viewport(cb, img.Width(), img.Height());
         vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mLayout, 0, 1, &mSet[0], 0, nullptr);
         const bool envBg = fr.envBackground && (int(f.envm[0]) & 2) != 0;
         if (envBg && Inv4(fr.viewProj, pc.invVP))
         {
            pc.bg[0] = (float)img.Width(); pc.bg[1] = (float)img.Height();
            Lin3(fr.clearSrgb, pc.clr);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mBgPso[fi]);
            vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
            vkCmdPushConstants(cb, mLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDraw(cb, 3, 1, 0, 0);
         }
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mPso[fi]);
         vkCmdPushConstants(cb, mLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
         for (const auto& r : mRuns)
         {
            const Gpu& g = mMeshes[(size_t)fr.instances[r.first].mesh];
            if (!g.count)
               continue;
            vkCmdSetCullMode(cb, g.Flat() ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            const VkBuffer vbs[3] = { g.vb.buf, g.uvb.buf, g.wb.buf };
            const VkDeviceSize offs[3] = { 0, 0, 0 };
            vkCmdBindVertexBuffers(cb, 0, 3, vbs, offs);
            vkCmdBindIndexBuffer(cb, g.ib.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, g.count, r.second, 0, 0, r.first);
         }
         vkCmdEndRendering(cb);
         img.RecordReadback(cb);
         if (mQuery) vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQuery, 1);
         if (!C.Submit(cb))
            return false;
         img.Publish();
         if (gpuMs)
         {
            *gpuMs = 0.0;
            uint64_t ts[2] = {};
            if (mQuery && vkGetQueryPoolResults(C.dev, mQuery, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
               *gpuMs = (double)(ts[1] - ts[0]) * C.props.limits.timestampPeriod * 1e-6;
         }
         return true;
      }

      // Row 9.4: MetalRaster::PathTrace on compute. MoltenVK has no ray query, so rays walk a BVH built here on the
      // CPU (binned SAH over world-space triangles, rebuilt when meshes, transforms or shadow flags change); pt.comp
      // adds one sample per dispatch to the running sums, dnprep / dnatrous denoise them, ptres.frag resolves.
      // Not ported (as the raster): Field material programs, motion blur, instance clusters / batches, bloom.
      bool PathTrace(SharedImage& target, const engine::Frame& fr, int spp, int bounces, bool reset, double* gpuMs) override
      {
         if (!mOk)
            return false;
         const Ctx& C = *mC;
         VulkanImage& img = static_cast<VulkanImage&>(target);
         const int fi = img.Bpp() == 8 ? 1 : 0;
         const int W = img.Width(), H = img.Height();
         if (!PtPipelines(fi, img.Fmt()) || !PtTargets(W, H) || !EnsureDummyShadow())
            return false;
         FrameU f = {};
         FillFrameRecord(f, fr, EnsureEnv(fr), mLightAll);
         f.shQ[3] = fr.rayBias;
         f.shP[3] = fr.shadow.strength; // the live picture's shadow strength, so settling keeps the same shadows
         std::memcpy(mFrameBuf.map, &f, sizeof(f));
         float inv[16];
         if (!Inv4(fr.viewProj, inv))
            return false;
         if (reset) mPtN = 0;
         const size_t ni = std::max<size_t>(fr.instances.size(), 1);
         if (mInstBuf.size < ni * sizeof(InstU))
         {
            vkDeviceWaitIdle(C.dev);
            FreeBuf(C, mInstBuf);
            if (!MakeBuf(C, ni * sizeof(InstU) * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mInstBuf))
               return false;
         }
         InstU* recs = (InstU*)mInstBuf.map;
         BuildRunRecords(fr, (int)mMeshes.size(), recs, mRuns);
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            recs[i].mat.emTri = -1.0f;
            const int m = fr.instances[i].mesh;
            if (m >= 0 && m < (int)mCpu.size() && mCpu[(size_t)m].trace) recs[i].mat.mapNormal = 0.0f; // relief is geometry now
         }
         PrepareMaps(fr);
         WriteSet(mSet[0], mShadowDum);
         if (!PtGeometry(fr))
            return false;
         const float pEm = PtEmitters(fr, recs);
         if (mEnvBound == &mEnv && mCdfRev != mEnvRev)
            BuildEnvCdf(fr);
         const bool envLight = mEnvBound == &mEnv && mCdf.img && mCdfRev == mEnvRev; // the HDRI can be sampled as a light
         if (!mELut.img && !MakeELut())
            return false;
         if (!PtBuf(mLightB, mLightAll.data(), mLightAll.size() * sizeof(LightU)))
            return false;

         struct PtUH { float invVP[16]; float info[4]; float em[4]; float lens[4], lr[4], lu[4], lf[4], fog[4], fogC[4], fogLo[4], fogHi[4]; } pu = {};
         pu.fog[0] = fr.fogDensity; pu.fog[1] = fr.fogAnisotropy; pu.fog[2] = fr.fogFalloff;
         for (int k = 0; k < 3; k++) { pu.fogC[k] = fr.fogColor[k]; pu.fogLo[k] = fr.fogMin[k]; pu.fogHi[k] = fr.fogMax[k]; }
         pu.lens[0] = fr.lensRadius; pu.lens[1] = fr.focusDist; pu.lens[2] = fr.bokeh[0]; pu.lens[3] = fr.bokeh[1];
         for (int k = 0; k < 3; k++) { pu.lr[k] = fr.camRight[k]; pu.lu[k] = fr.camUp[k]; pu.lf[k] = fr.camFwd[k]; }
         pu.lr[3] = fr.bokeh[2]; pu.lu[3] = fr.lensCatEye; pu.lf[3] = fr.lensDistortion; pu.fogC[3] = fr.lensCA;
         std::memcpy(pu.invVP, inv, 64);
         pu.info[1] = (float)std::max(1, bounces); pu.info[3] = (float)W;
         pu.em[0] = (float)mEmCount; pu.em[1] = pEm; pu.em[2] = envLight ? 1.0f : 0.0f;
         if (!PtBuf(mPtUB, &pu, sizeof(pu)))
            return false;
         std::memset(mHistB.map, 0, 64 * sizeof(uint32_t));
         // Row 4.4: caustics, as MetalRaster::PathTrace: the glass's world bounds aim the photons and size the cells.
         float cLo[3] = { 1e30f, 1e30f, 1e30f }, cHi[3] = { -1e30f, -1e30f, -1e30f };
         bool glass = false;
         for (const engine::Instance& in : fr.instances)
         {
            if (in.transmission <= 0.0f || in.mesh < 0 || in.mesh >= (int)mCpu.size())
               continue;
            const std::vector<float>& vp = mCpu[(size_t)in.mesh].pn; // pos + normal, stride 6
            for (size_t v = 0; v + 5 < vp.size(); v += 6)
               for (int a = 0; a < 3; a++)
               {
                  const float w = in.model[a] * vp[v] + in.model[4 + a] * vp[v + 1] + in.model[8 + a] * vp[v + 2] + in.model[12 + a];
                  cLo[a] = std::min(cLo[a], w); cHi[a] = std::max(cHi[a], w);
               }
            glass = cLo[0] <= cHi[0];
         }
         const bool caus = glass && f.fld[1] > 0.5f && mCausPso;
         FramePush cpc = {};
         if (caus)
         {
            float rad = 0.0f;
            for (int a = 0; a < 3; a++) { cpc.clr[a] = 0.5f * (cLo[a] + cHi[a]); rad += (cHi[a] - cLo[a]) * (cHi[a] - cLo[a]); }
            cpc.clr[3] = 0.5f * std::sqrt(rad) * 1.02f;
            cpc.bg[0] = cpc.clr[3] / 48.0f; cpc.bg[1] = 256.0f * 256.0f; cpc.bg[3] = f.fld[1];
            pu.em[3] = cpc.bg[0];
            if (!PtBuf(mPtUB, &pu, sizeof(pu)))
               return false;
         }
         PtWriteSet(envLight);

         VkCommandBuffer cb = C.Begin();
         if (mQuery) { vkCmdResetQueryPool(cb, mQuery, 0, 2); vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mQuery, 0); }
         const VkShaderStageFlags pcs = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
         const VkDescriptorSet sets[2] = { mSet[0], mPtSet };
         vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mPtLayout, 0, 2, sets, 0, nullptr);
         const auto barrier = [&](VkPipelineStageFlags dst) {
            VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
         };
         const uint32_t gx = (uint32_t)(W + 7) / 8, gy = (uint32_t)(H + 7) / 8;
         FramePush pc = {};
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mPtPso);
         for (int s = 0; s < std::max(1, spp); s++)
         {
            if (caus)
            {
               vkCmdFillBuffer(cb, mCausB.buf, 0, VK_WHOLE_SIZE, 0);
               VkMemoryBarrier fb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
               fb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; fb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
               vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fb, 0, nullptr, 0, nullptr);
               vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mCausPso);
               cpc.bg[2] = (float)mPtN;
               vkCmdPushConstants(cb, mPtLayout, pcs, 0, sizeof(cpc), &cpc);
               vkCmdDispatch(cb, 32, 32, 1);
               barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
               vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mPtPso);
            }
            pc.bg[0] = (float)mPtN; pc.bg[1] = (float)(mPtFrame++);
            vkCmdPushConstants(cb, mPtLayout, pcs, 0, sizeof(pc), &pc);
            vkCmdDispatch(cb, gx, gy, 1);
            barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            mPtN++;
         }
         int dnIdx = -1;
         if (mDenoise)
         {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mDnPrepPso);
            vkCmdDispatch(cb, gx, gy, 1);
            barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, mDnAtrousPso);
            int c = 0;
            for (int i = 0; i < mDnSteps; i++)
            {
               pc = {};
               pc.bg[0] = (float)(1 << i); pc.bg[1] = mDnSigL; pc.bg[2] = (float)c;
               vkCmdPushConstants(cb, mPtLayout, pcs, 0, sizeof(pc), &pc);
               vkCmdDispatch(cb, gx, gy, 1);
               barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
               c = 1 - c;
            }
            dnIdx = c;
         }
         barrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
         Img& tgt = img.Target();
         ToLayout(cb, tgt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
         VkRenderingAttachmentInfo ca = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
         ca.imageView = tgt.view; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         ca.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
         VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
         ri.renderArea = { { 0, 0 }, { (uint32_t)W, (uint32_t)H } }; ri.layerCount = 1;
         ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
         vkCmdBeginRendering(cb, &ri);
         Viewport(cb, W, H);
         vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mPtResPso[fi]);
         vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mPtLayout, 0, 2, sets, 0, nullptr);
         pc = {};
         pc.bg[0] = (float)mPtN; pc.bg[1] = (float)fr.tonemap; pc.bg[2] = fr.exposure; pc.bg[3] = dnIdx >= 0 ? 1.0f : 0.0f;
         pc.clr[0] = fr.clearSrgb[0]; pc.clr[1] = fr.clearSrgb[1]; pc.clr[2] = fr.clearSrgb[2]; pc.clr[3] = (float)std::max(dnIdx, 0);
         vkCmdPushConstants(cb, mPtLayout, pcs, 0, sizeof(pc), &pc);
         vkCmdDraw(cb, 3, 1, 0, 0);
         vkCmdEndRendering(cb);
         img.RecordReadback(cb);
         if (mQuery) vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQuery, 1);
         if (!C.Submit(cb))
            return false;
         img.Publish();
         if (gpuMs)
         {
            *gpuMs = 0.0;
            uint64_t ts[2] = {};
            if (mQuery && vkGetQueryPoolResults(C.dev, mQuery, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
               *gpuMs = (double)(ts[1] - ts[0]) * C.props.limits.timestampPeriod * 1e-6;
         }
         mPtNoise = -1.0f;
         if (dnIdx >= 0)
         {
            const uint32_t* h = (const uint32_t*)mHistB.map;
            uint64_t total = 0, run = 0;
            for (int i = 0; i < 64; i++) total += h[i];
            for (int i = 0; i < 64 && total > 0; i++)
            {
               run += h[i];
               if (run * 100 >= total * 95) { mPtNoise = (float)(i + 1) / 400.0f; break; }
            }
         }
         return true;
      }
      float PathTraceNoise() const override { return mPtNoise; }

   private:
      struct Gpu
      {
         Buf vb, ib, uvb, wb;
         uint32_t count = 0;
         float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
         bool Flat() const { const float e[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] }; const float mx = std::max({ e[0], e[1], e[2] }); return mx > 0.0f && std::min({ e[0], e[1], e[2] }) <= mx * 1e-4f; }
      };
      void FreeGpu(Gpu& g) { FreeBuf(*mC, g.vb); FreeBuf(*mC, g.ib); FreeBuf(*mC, g.uvb); FreeBuf(*mC, g.wb); g = Gpu{}; }
      bool MakeGpu(const engine::MeshData& m, Gpu& g)
      {
         const Ctx& C = *mC;
         const VkBufferUsageFlags vu = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
         const size_t nv = m.posNormal.size() / 6;
         if (!BufWith(C, m.posNormal.data(), m.posNormal.size() * 4, vu, g.vb) ||
             !BufWith(C, m.indices.data(), m.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, g.ib) ||
             !BufWith(C, m.uv.size() == nv * 2 ? m.uv.data() : nullptr, std::max<size_t>(nv * 8, 8), vu, g.uvb) ||
             !BufWith(C, m.wear.size() == nv * 3 ? m.wear.data() : nullptr, std::max<size_t>(nv * 12, 12), vu, g.wb))
            return false;
         g.count = (uint32_t)m.indices.size();
         for (int k = 0; k < 3; k++) { g.lo[k] = 1e30f; g.hi[k] = -1e30f; }
         for (size_t i = 0; i + 5 < m.posNormal.size(); i += 6)
            for (int k = 0; k < 3; k++)
            {
               g.lo[k] = std::min(g.lo[k], m.posNormal[i + k]);
               g.hi[k] = std::max(g.hi[k], m.posNormal[i + k]);
            }
         return true;
      }

      bool Init()
      {
         const Ctx& C = *mC;
         VkDescriptorSetLayoutBinding b[8] = {};
         // Compute too: the path tracer (row 9.4) binds this set beside its own.
         const VkShaderStageFlags vf = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
         b[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, vf, nullptr };
         b[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, vf, nullptr };
         b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, vf, nullptr };
         b[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, vf, nullptr };
         b[4] = { 4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (uint32_t)engine::kMaxTextures, vf, nullptr };
         b[5] = { 5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, vf, nullptr };
         b[6] = { 6, VK_DESCRIPTOR_TYPE_SAMPLER, 1, vf, nullptr };
         b[7] = { 7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, vf, nullptr };
         VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
         li.bindingCount = 8; li.pBindings = b;
         if (vkCreateDescriptorSetLayout(C.dev, &li, nullptr, &mSetLayout) != VK_SUCCESS)
            return false;
         const VkDescriptorPoolSize ps[4] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
                                              { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2u * engine::kMaxTextures }, { VK_DESCRIPTOR_TYPE_SAMPLER, 6 } };
         VkDescriptorPoolCreateInfo pi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
         pi.maxSets = 2; pi.poolSizeCount = 4; pi.pPoolSizes = ps;
         if (vkCreateDescriptorPool(C.dev, &pi, nullptr, &mPool) != VK_SUCCESS)
            return false;
         VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
         const VkDescriptorSetLayout two[2] = { mSetLayout, mSetLayout };
         ai.descriptorPool = mPool; ai.descriptorSetCount = 2; ai.pSetLayouts = two;
         if (vkAllocateDescriptorSets(C.dev, &ai, mSet) != VK_SUCCESS)
            return false;
         VkPushConstantRange pr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(FramePush) };
         VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
         pl.setLayoutCount = 1; pl.pSetLayouts = &mSetLayout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr;
         if (vkCreatePipelineLayout(C.dev, &pl, nullptr, &mLayout) != VK_SUCCESS)
            return false;
         // Samplers as MetalDevice.mm's: the shadow compare (cs), the HDRI (es) and the maps (kMapC / R / M).
         VkSamplerCreateInfo s = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
         s.magFilter = s.minFilter = VK_FILTER_LINEAR; s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
         s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         s.compareEnable = VK_TRUE; s.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; s.maxLod = 0.0f;
         vkCreateSampler(C.dev, &s, nullptr, &mSmpShadow);
         s.compareEnable = VK_FALSE; s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; s.maxLod = VK_LOD_CLAMP_NONE;
         s.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
         vkCreateSampler(C.dev, &s, nullptr, &mSmpEnv);
         s.anisotropyEnable = VK_TRUE; s.maxAnisotropy = std::min(8.0f, C.props.limits.maxSamplerAnisotropy);
         s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
         vkCreateSampler(C.dev, &s, nullptr, &mSmpC);
         s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
         vkCreateSampler(C.dev, &s, nullptr, &mSmpR);
         s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
         vkCreateSampler(C.dev, &s, nullptr, &mSmpM);
         if (!MakeBuf(C, sizeof(FrameU), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mFrameBuf) ||
             !MakeBuf(C, sizeof(InstU) * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mInstBuf))
            return false;
         const uint8_t white[4] = { 255, 255, 255, 255 };
         if (!MakeImg(C, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mWhite) ||
             !UploadMipped(C, mWhite, white, 4))
            return false;
         const uint16_t black[4] = { 0, 0, 0, Half(1.0f) };
         if (!MakeImg(C, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mEnvDummy) ||
             !UploadMipped(C, mEnvDummy, black, 8))
            return false;
         if (C.props.limits.timestampComputeAndGraphics)
         {
            VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2;
            vkCreateQueryPool(C.dev, &qi, nullptr, &mQuery);
         }
         return true;
      }
      bool Pipelines(int fi, VkFormat fmt)
      {
         if (mPso[fi] && mBgPso[fi] && mShPso)
            return true;
         const Ctx& C = *mC;
         VkShaderModule rv = Module(C, kVkRasterVert, sizeof(kVkRasterVert)), rf = Module(C, kVkRasterFrag, sizeof(kVkRasterFrag));
         VkShaderModule sv = Module(C, kVkShadowVert, sizeof(kVkShadowVert));
         VkShaderModule bv = Module(C, kVkBgVert, sizeof(kVkBgVert)), bf = Module(C, kVkBgFrag, sizeof(kVkBgFrag));
         PsoDesc d;
         d.layout = mLayout; d.depth = VK_FORMAT_D32_SFLOAT;
         if (!mPso[fi]) { d.vs = rv; d.fs = rf; d.color = fmt; d.meshInput = true; mPso[fi] = MakePso(C, d); }
         if (!mBgPso[fi]) { PsoDesc b = d; b.vs = bv; b.fs = bf; b.color = fmt; b.meshInput = false; b.depthTest = false; b.depthWrite = false; mBgPso[fi] = MakePso(C, b); }
         if (!mShPso) { PsoDesc s = d; s.vs = sv; s.fs = VK_NULL_HANDLE; s.color = VK_FORMAT_UNDEFINED; s.meshInput = true; s.posOnly = true; mShPso = MakePso(C, s); }
         for (VkShaderModule m : { rv, rf, sv, bv, bf })
            if (m) vkDestroyShaderModule(C.dev, m, nullptr);
         if (!mPso[fi] || !mBgPso[fi] || !mShPso)
         {
            VulkanFail(std::string("vulkan raster pipeline: not built") + (mPso[fi] ? "" : " (raster)") + (mBgPso[fi] ? "" : " (bg)") + (mShPso ? "" : " (shadow)"));
            return false;
         }
         return true;
      }
      bool EnsureDepth(int w, int h)
      {
         if (mDepth.img && mDepth.w == w && mDepth.h == h)
            return true;
         vkDeviceWaitIdle(mC->dev);
         FreeImg(*mC, mDepth);
         return MakeImg(*mC, w, h, VK_FORMAT_D32_SFLOAT, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, mDepth);
      }
      // MetalRaster::EnsureEnv: RGBA16F with a full mip chain, rebuilt when the frame's HDRI changes.
      bool EnsureEnv(const engine::Frame& fr)
      {
         const bool want = fr.envPixels != nullptr && fr.envW > 0 && fr.envH > 0;
         if (want && (!mEnv.img || mEnvRev != fr.envRev || mEnv.w != fr.envW || mEnv.h != fr.envH))
         {
            vkDeviceWaitIdle(mC->dev);
            FreeImg(*mC, mEnv);
            const uint32_t mips = MipCount(fr.envW, fr.envH);
            if (!MakeImg(*mC, fr.envW, fr.envH, VK_FORMAT_R16G16B16A16_SFLOAT, mips,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mEnv))
               return false;
            std::vector<uint16_t> h((size_t)fr.envW * fr.envH * 4);
            for (size_t i = 0, n = (size_t)fr.envW * fr.envH; i < n; i++)
            {
               for (int k = 0; k < 3; k++)
               {
                  const float v = fr.envPixels[i * 3 + k];
                  h[i * 4 + k] = Half(std::isfinite(v) ? std::min(std::max(v, 0.0f), 65504.0f) : 0.0f);
               }
               h[i * 4 + 3] = Half(1.0f);
            }
            if (!UploadMipped(*mC, mEnv, h.data(), h.size() * 2))
               return false;
            mEnvRev = fr.envRev;
         }
         mEnvBound = want && mEnv.img ? &mEnv : &mEnvDummy;
         return want && mEnv.img;
      }
      // MetalRaster::PrepareMaps: RGBA8 with mips, cached by TextureData::key; unused slots a 1x1 white.
      void PrepareMaps(const engine::Frame& fr)
      {
         for (Img& c : mMapCache) c.used = false;
         for (int i = 0; i < engine::kMaxTextures; i++)
         {
            mMapSlot[i] = -1;
            if (i >= (int)fr.textures.size())
               continue;
            const engine::TextureData& t = fr.textures[(size_t)i];
            if (!t.rgba || t.w <= 0 || t.h <= 0)
               continue;
            for (size_t k = 0; k < mMapCache.size(); k++)
               if (mMapCache[k].key == t.key) { mMapSlot[i] = (int)k; break; }
            if (mMapSlot[i] < 0)
            {
               Img e;
               if (!MakeImg(*mC, t.w, t.h, VK_FORMAT_R8G8B8A8_UNORM, MipCount(t.w, t.h),
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, e) ||
                   !UploadMipped(*mC, e, t.rgba, (size_t)t.w * t.h * 4))
               {
                  FreeImg(*mC, e);
                  continue;
               }
               e.key = t.key;
               mMapCache.push_back(e);
               mMapSlot[i] = (int)mMapCache.size() - 1;
            }
            mMapCache[(size_t)mMapSlot[i]].used = true;
         }
         if (std::all_of(mMapCache.begin(), mMapCache.end(), [](const Img& m) { return m.used; }))
            return;
         vkDeviceWaitIdle(mC->dev);
         std::vector<int> remap(mMapCache.size(), -1);
         std::vector<Img> keep;
         for (size_t k = 0; k < mMapCache.size(); k++)
            if (mMapCache[k].used) { remap[k] = (int)keep.size(); keep.push_back(mMapCache[k]); }
            else FreeImg(*mC, mMapCache[k]);
         mMapCache.swap(keep);
         for (int& s : mMapSlot)
            if (s >= 0) s = remap[(size_t)s];
      }
      // A 4x4 depth image cleared to 1 (lit everywhere): bound when there is no shadow map, and always in the
      // shadow pass's own set (that pass reads only inst[]).
      bool EnsureDummyShadow()
      {
         if (mShadowDum.img)
            return true;
         if (!MakeImg(*mC, 4, 4, VK_FORMAT_D32_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mShadowDum))
            return false;
         VkCommandBuffer cb = mC->Begin();
         ToLayout(cb, mShadowDum, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
         const VkClearDepthStencilValue one = { 1.0f, 0 };
         const VkImageSubresourceRange r = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
         vkCmdClearDepthStencilImage(cb, mShadowDum.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &r);
         ToLayout(cb, mShadowDum, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
         return mC->Submit(cb);
      }
      // MetalRaster::ShadowPass, CPU half: light 0's orthographic fit over the scene bounds (rr::ShadowFit).
      bool ShadowPrepare(const engine::Frame& fr, FrameU& f, float* lvp)
      {
         const engine::Shadow& sh = fr.shadow;
         if (fr.rtShadows || !sh.enabled || fr.lights.empty() || !fr.lights[0].castShadow || fr.lights[0].type == engine::LightType::Point ||
             fr.lights[0].type == engine::LightType::Area || fr.lights[0].type == engine::LightType::Ambient)
            return false;
         if (!mShadow.img || mShadow.w != sh.size)
         {
            vkDeviceWaitIdle(mC->dev);
            FreeImg(*mC, mShadow);
            if (!MakeImg(*mC, sh.size, sh.size, VK_FORMAT_D32_SFLOAT, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mShadow))
               return false;
         }
         const auto bounds = [&](int mesh, float* lo, float* hi) {
            if (mesh < 0 || mesh >= (int)mMeshes.size())
               return false;
            std::memcpy(lo, mMeshes[(size_t)mesh].lo, 12); std::memcpy(hi, mMeshes[(size_t)mesh].hi, 12);
            return true;
         };
         return ShadowFit(fr, bounds, f, lvp);
      }
      // GPU half: the depth map, no cull, instances that cast.
      void ShadowRecord(VkCommandBuffer cb, const engine::Frame& fr, const float* lvp)
      {
         const int size = mShadow.w;
         ToLayout(cb, mShadow, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
         VkRenderingAttachmentInfo da = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
         da.imageView = mShadow.view; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
         da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
         da.clearValue.depthStencil.depth = 1.0f;
         VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
         ri.renderArea = { { 0, 0 }, { (uint32_t)size, (uint32_t)size } }; ri.layerCount = 1; ri.pDepthAttachment = &da;
         vkCmdBeginRendering(cb, &ri);
         Viewport(cb, size, size);
         vkCmdSetCullMode(cb, VK_CULL_MODE_NONE);
         vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mShPso);
         vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mLayout, 0, 1, &mSet[1], 0, nullptr);
         FramePush pc = {};
         std::memcpy(pc.lvp, lvp, 64);
         vkCmdPushConstants(cb, mLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
         for (const auto& r : mRuns)
         {
            if (!fr.instances[r.first].castShadow || (fr.instances[r.first].noShadowMask & 1u)) // row 4.2: shadow linking
               continue;
            const Gpu& g = mMeshes[(size_t)fr.instances[r.first].mesh];
            if (!g.count)
               continue;
            const VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &g.vb.buf, &off);
            vkCmdBindIndexBuffer(cb, g.ib.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, g.count, r.second, 0, 0, r.first);
         }
         vkCmdEndRendering(cb);
      }
      // Points a descriptor set at this frame's buffers and images. The GPU is idle between frames, so the sets
      // are rewritten in place before recording.
      void WriteSet(VkDescriptorSet set, Img& shadow)
      {
         const Ctx& C = *mC;
         VkDescriptorBufferInfo bf = { mFrameBuf.buf, 0, VK_WHOLE_SIZE }, bi = { mInstBuf.buf, 0, VK_WHOLE_SIZE };
         VkDescriptorImageInfo sh = { mSmpShadow, shadow.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
         VkDescriptorImageInfo en = { mSmpEnv, mEnvBound ? mEnvBound->view : mEnvDummy.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
         VkDescriptorImageInfo mp[engine::kMaxTextures];
         for (int i = 0; i < engine::kMaxTextures; i++)
            mp[i] = { VK_NULL_HANDLE, (mMapSlot[i] >= 0 ? mMapCache[(size_t)mMapSlot[i]] : mWhite).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
         VkDescriptorImageInfo s3[3] = { { mSmpC, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED }, { mSmpR, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
                                         { mSmpM, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED } };
         VkWriteDescriptorSet w[8] = {};
         for (int i = 0; i < 8; i++) { w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = set; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1; }
         w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].pBufferInfo = &bf;
         w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[1].pBufferInfo = &bi;
         w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].pImageInfo = &sh;
         w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[3].pImageInfo = &en;
         w[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[4].descriptorCount = (uint32_t)engine::kMaxTextures; w[4].pImageInfo = mp;
         for (int i = 0; i < 3; i++) { w[5 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; w[5 + i].pImageInfo = &s3[i]; }
         vkUpdateDescriptorSets(C.dev, 8, w, 0, nullptr);
      }

      // ---- path tracer (row 9.4)
      // The rays' copy of a mesh: the trace mesh when the source gave one (row 4.3), else the drawn one.
      struct Cpu
      {
         std::vector<float> pn, uv;
         std::vector<uint32_t> ix;
         bool trace = false;
      };
      static void KeepCpu(const engine::MeshData& m, Cpu& c)
      {
         c.trace = !m.tracePosNormal.empty() && !m.traceIndices.empty();
         c.pn = c.trace ? m.tracePosNormal : m.posNormal;
         c.ix = c.trace ? m.traceIndices : m.indices;
         const std::vector<float>& uv = c.trace ? m.traceUv : m.uv;
         c.uv = uv.size() == c.pn.size() / 6 * 2 ? uv : std::vector<float>();
      }
      // Grows a host-visible storage buffer as needed and copies bytes in (the GPU is idle between frames).
      bool PtBuf(Buf& b, const void* data, size_t bytes)
      {
         if (b.size < std::max<size_t>(bytes, 16))
         {
            vkDeviceWaitIdle(mC->dev);
            FreeBuf(*mC, b);
            if (!MakeBuf(*mC, std::max<size_t>(bytes, 16) * 3 / 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, b))
               return false;
         }
         if (data && bytes) std::memcpy(b.map, data, bytes);
         return true;
      }
      static void Xf(const float* m, const float* a, float* o)
      {
         for (int k = 0; k < 3; k++) o[k] = m[k] * a[0] + m[4 + k] * a[1] + m[8 + k] * a[2] + m[12 + k];
      }
      static float AsF(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
      // Mesh streams (object space, all meshes end to end) and the BVH over every instance's world-space triangles,
      // rebuilt only when what they depend on changes.
      bool PtGeometry(const engine::Frame& fr)
      {
         uint64_t key = 1469598103934665603ull;
         const auto mix = [&](const void* p, size_t n) {
            const uint8_t* b = (const uint8_t*)p;
            for (size_t i = 0; i < n; i++) { key ^= b[i]; key *= 1099511628211ull; }
         };
         mix(&mMeshGen, sizeof(mMeshGen));
         for (const engine::Instance& in : fr.instances)
         {
            mix(&in.mesh, sizeof(in.mesh)); mix(in.model, 64);
            const uint8_t cs = in.castShadow ? 1 : 0; mix(&cs, 1);
         }
         if (key == mBvhKey && mNodeB.buf)
            return true;
         // Mesh streams: vertex v of mesh m is global vertex vOff[m] + v; indices are stored already offset.
         std::vector<size_t> vOff(mCpu.size()), iOff(mCpu.size());
         std::vector<float> vb, ub;
         std::vector<uint32_t> ib;
         for (size_t m = 0; m < mCpu.size(); m++)
         {
            const Cpu& c = mCpu[m];
            const size_t nv = c.pn.size() / 6;
            vOff[m] = vb.size() / 6; iOff[m] = ib.size();
            vb.insert(vb.end(), c.pn.begin(), c.pn.begin() + (ptrdiff_t)(nv * 6));
            if (c.uv.empty()) ub.insert(ub.end(), nv * 2, 0.0f);
            else ub.insert(ub.end(), c.uv.begin(), c.uv.end());
            for (size_t t = 0; t + 2 < c.ix.size(); t += 3)
               for (int k = 0; k < 3; k++)
                  ib.push_back(c.ix[t + k] < nv ? (uint32_t)(vOff[m] + c.ix[t + k]) : (uint32_t)vOff[m]);
         }
         std::vector<uint32_t> base(fr.instances.size() * 4 + 4, 0u);
         // World-space triangles with their instance, primitive and shadow flag.
         struct Ref { float lo[3], hi[3], c[3]; };
         std::vector<float> tv; // 9 floats per triangle: v0, e1, e2
         std::vector<uint32_t> tinfo; // inst, prim, flags
         std::vector<Ref> refs;
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            if (in.mesh < 0 || in.mesh >= (int)mCpu.size())
               continue;
            const Cpu& c = mCpu[(size_t)in.mesh];
            base[i * 4] = (uint32_t)iOff[(size_t)in.mesh]; base[i * 4 + 1] = c.uv.empty() ? 0u : 1u;
            const size_t nv = c.pn.size() / 6;
            for (size_t t = 0; t + 2 < c.ix.size(); t += 3)
            {
               float p[3][3];
               for (int k = 0; k < 3; k++) Xf(in.model, &c.pn[std::min<size_t>(c.ix[t + k], nv ? nv - 1 : 0) * 6], p[k]);
               Ref r;
               for (int a = 0; a < 3; a++)
               {
                  r.lo[a] = std::min({ p[0][a], p[1][a], p[2][a] }); r.hi[a] = std::max({ p[0][a], p[1][a], p[2][a] });
                  r.c[a] = (r.lo[a] + r.hi[a]) * 0.5f;
               }
               refs.push_back(r);
               for (int a = 0; a < 3; a++) tv.push_back(p[0][a]);
               for (int a = 0; a < 3; a++) tv.push_back(p[1][a] - p[0][a]);
               for (int a = 0; a < 3; a++) tv.push_back(p[2][a] - p[0][a]);
               tinfo.push_back((uint32_t)i); tinfo.push_back((uint32_t)(t / 3)); tinfo.push_back(in.castShadow ? 1u : 0u);
            }
         }
         // Binned SAH (16 bins per axis), leaves of up to 4 triangles; children of a node are adjacent.
         struct Node { float lo[3], hi[3]; uint32_t a = 0, n = 0; };
         std::vector<Node> nodes(1);
         std::vector<uint32_t> order(refs.size());
         for (size_t k = 0; k < order.size(); k++) order[k] = (uint32_t)k;
         const auto area = [](const float* lo, const float* hi) {
            const float e[3] = { std::max(hi[0] - lo[0], 0.0f), std::max(hi[1] - lo[1], 0.0f), std::max(hi[2] - lo[2], 0.0f) };
            return e[0] * e[1] + e[1] * e[2] + e[2] * e[0];
         };
         struct Job { uint32_t node, b, e; };
         std::vector<Job> jobs = { { 0u, 0u, (uint32_t)order.size() } };
         while (!jobs.empty())
         {
            const Job j = jobs.back(); jobs.pop_back();
            Node nd;
            float clo[3] = { 1e30f, 1e30f, 1e30f }, chi[3] = { -1e30f, -1e30f, -1e30f };
            for (int a = 0; a < 3; a++) { nd.lo[a] = 1e30f; nd.hi[a] = -1e30f; }
            for (uint32_t k = j.b; k < j.e; k++)
            {
               const Ref& r = refs[order[k]];
               for (int a = 0; a < 3; a++)
               {
                  nd.lo[a] = std::min(nd.lo[a], r.lo[a]); nd.hi[a] = std::max(nd.hi[a], r.hi[a]);
                  clo[a] = std::min(clo[a], r.c[a]); chi[a] = std::max(chi[a], r.c[a]);
               }
            }
            const uint32_t n = j.e - j.b;
            int bestA = -1, bestS = 0;
            float bestC = 1e38f;
            if (n > 4)
               for (int a = 0; a < 3; a++)
               {
                  const float ext = chi[a] - clo[a];
                  if (!(ext > 1e-12f))
                     continue;
                  struct Bin { float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f }; uint32_t n = 0; } bins[16];
                  for (uint32_t k = j.b; k < j.e; k++)
                  {
                     const Ref& r = refs[order[k]];
                     Bin& bn = bins[std::min(15, (int)((r.c[a] - clo[a]) / ext * 16.0f))];
                     bn.n++;
                     for (int q = 0; q < 3; q++) { bn.lo[q] = std::min(bn.lo[q], r.lo[q]); bn.hi[q] = std::max(bn.hi[q], r.hi[q]); }
                  }
                  float rA[16]; uint32_t rN[16];
                  Bin acc;
                  for (int s = 15; s > 0; s--)
                  {
                     acc.n += bins[s].n;
                     for (int q = 0; q < 3; q++) { acc.lo[q] = std::min(acc.lo[q], bins[s].lo[q]); acc.hi[q] = std::max(acc.hi[q], bins[s].hi[q]); }
                     rA[s] = area(acc.lo, acc.hi); rN[s] = acc.n;
                  }
                  Bin l;
                  for (int s = 1; s < 16; s++)
                  {
                     l.n += bins[s - 1].n;
                     for (int q = 0; q < 3; q++) { l.lo[q] = std::min(l.lo[q], bins[s - 1].lo[q]); l.hi[q] = std::max(l.hi[q], bins[s - 1].hi[q]); }
                     if (l.n == 0 || rN[s] == 0)
                        continue;
                     const float cst = area(l.lo, l.hi) * (float)l.n + rA[s] * (float)rN[s];
                     if (cst < bestC) { bestC = cst; bestA = a; bestS = s; }
                  }
               }
            uint32_t mid = j.b;
            if (bestA >= 0)
            {
               const float ext = chi[bestA] - clo[bestA];
               mid = (uint32_t)(std::partition(order.begin() + j.b, order.begin() + j.e, [&](uint32_t k) {
                                   return std::min(15, (int)((refs[k].c[bestA] - clo[bestA]) / ext * 16.0f)) < bestS;
                                }) - order.begin());
            }
            else if (n > 4)
               mid = j.b + n / 2; // every centroid in one place: split the list in half
            if (n <= 4 || mid == j.b || mid == j.e)
            {
               nd.a = j.b; nd.n = std::max(n, 1u) == n ? n : 0u;
               if (n == 0) { for (int a = 0; a < 3; a++) { nd.lo[a] = 1e30f; nd.hi[a] = -1e30f; } }
               nodes[j.node] = nd;
               continue;
            }
            nd.a = (uint32_t)nodes.size(); nd.n = 0;
            nodes[j.node] = nd;
            nodes.resize(nodes.size() + 2);
            jobs.push_back({ nd.a, j.b, mid });
            jobs.push_back({ nd.a + 1, mid, j.e });
         }
         std::vector<float> nb(nodes.size() * 8);
         for (size_t k = 0; k < nodes.size(); k++)
         {
            const Node& nd = nodes[k];
            float* o = &nb[k * 8];
            for (int a = 0; a < 3; a++) { o[a] = nd.lo[a]; o[4 + a] = nd.hi[a]; }
            o[3] = AsF(nd.a); o[7] = AsF(nd.n);
         }
         std::vector<float> tb(std::max<size_t>(order.size(), 1) * 12, 0.0f);
         for (size_t k = 0; k < order.size(); k++)
         {
            const uint32_t t = order[k];
            float* o = &tb[k * 12];
            for (int a = 0; a < 3; a++) { o[a] = tv[t * 9 + a]; o[4 + a] = tv[t * 9 + 3 + a]; o[8 + a] = tv[t * 9 + 6 + a]; }
            o[3] = AsF(tinfo[t * 3]); o[7] = AsF(tinfo[t * 3 + 1]); o[11] = AsF(tinfo[t * 3 + 2]);
         }
         if (!PtBuf(mVtxB, vb.data(), vb.size() * 4) || !PtBuf(mIdxB, ib.data(), ib.size() * 4) || !PtBuf(mUvB, ub.data(), ub.size() * 4) ||
             !PtBuf(mBaseB, base.data(), base.size() * 4) || !PtBuf(mTriB, tb.data(), tb.size() * 4) || !PtBuf(mNodeB, nb.data(), nb.size() * 4))
            return false;
         mBvhKey = key;
         return true;
      }
      // Emissive triangles in world space for next-event estimation, chosen by power (luminance * area), as
      // MetalRaster::PathTrace. Returns the chance a light sample picks one.
      float PtEmitters(const engine::Frame& fr, InstU* recs)
      {
         struct EmTriH { float p0[4], e1[4], e2[4], em[4]; };
         std::vector<EmTriH> ems;
         double emTotal = 0.0;
         for (size_t i = 0; i < fr.instances.size(); i++)
         {
            const engine::Instance& in = fr.instances[i];
            const float lum = 0.2126f * in.emission[0] + 0.7152f * in.emission[1] + 0.0722f * in.emission[2];
            if (lum <= 0.0f || in.mesh < 0 || in.mesh >= (int)mCpu.size())
               continue;
            const Cpu& c = mCpu[(size_t)in.mesh];
            const size_t tris = c.ix.size() / 3, nv = c.pn.size() / 6;
            if (tris == 0 || nv == 0 || ems.size() + tris > 65536)
               continue;
            recs[i].mat.emTri = (float)ems.size();
            for (size_t t = 0; t < tris; t++)
            {
               EmTriH e = {};
               float a[3], b[3], d[3];
               Xf(in.model, &c.pn[std::min<size_t>(c.ix[t * 3], nv - 1) * 6], a);
               Xf(in.model, &c.pn[std::min<size_t>(c.ix[t * 3 + 1], nv - 1) * 6], b);
               Xf(in.model, &c.pn[std::min<size_t>(c.ix[t * 3 + 2], nv - 1) * 6], d);
               for (int k = 0; k < 3; k++) { e.p0[k] = a[k]; e.e1[k] = b[k] - a[k]; e.e2[k] = d[k] - a[k]; e.em[k] = in.emission[k]; }
               const float cx = e.e1[1] * e.e2[2] - e.e1[2] * e.e2[1], cy = e.e1[2] * e.e2[0] - e.e1[0] * e.e2[2], cz = e.e1[0] * e.e2[1] - e.e1[1] * e.e2[0];
               const float ar = 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
               e.e2[3] = ar;
               emTotal += (double)ar * lum;
               e.p0[3] = (float)emTotal;
               ems.push_back(e);
            }
         }
         if (emTotal > 0.0)
            for (size_t k = 0; k < ems.size(); k++)
            {
               const double prevC = k == 0 ? 0.0 : ems[k - 1].p0[3] * emTotal;
               const double pw = (double)ems[k].p0[3] - prevC;
               ems[k].p0[3] = (float)(ems[k].p0[3] / emTotal);
               ems[k].e1[3] = ems[k].e2[3] > 0.0f ? (float)(pw / emTotal / ems[k].e2[3]) : 0.0f;
            }
         else
         {
            ems.clear();
            for (size_t i = 0; i < fr.instances.size(); i++) recs[i].mat.emTri = -1.0f;
         }
         if (!ems.empty()) ems.back().p0[3] = 1.0f;
         mEmCount = (int)ems.size();
         PtBuf(mEmB, ems.data(), ems.size() * sizeof(EmTriH));
         int analytic = 0;
         for (const engine::Light& l : fr.lights) analytic += l.type != engine::LightType::Ambient ? 1 : 0;
         return ems.empty() ? 0.0f : (analytic > 0 ? 0.5f : 1.0f);
      }
      // E(mu, r) of the tracer's specular lobe (MetalRaster::ELut, the same quadrature), R32F 16x16.
      bool MakeELut()
      {
         const int kN = 16;
         std::vector<float> e((size_t)kN * kN);
         for (int y = 0; y < kN; y++)
            for (int x = 0; x < kN; x++)
            {
               const float mu = (x + 0.5f) / kN, r = (y + 0.5f) / kN;
               const float a = std::max(0.002f, r * r), a2 = a * a;
               const int n1 = 64, n2 = 128;
               double sum = 0.0;
               for (int i = 0; i < n1; i++)
                  for (int j = 0; j < n2; j++)
                  {
                     const float u1 = (i + 0.5f) / n1, ph = 6.2831853f * (j + 0.5f) / n2;
                     const float ct = std::sqrt((1.0f - u1) / (1.0f + (a2 - 1.0f) * u1)), st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
                     const float vh = std::sqrt(std::max(0.0f, 1.0f - mu * mu)) * st * std::cos(ph) + mu * ct;
                     const float nl = 2.0f * vh * ct - mu;
                     if (vh <= 0.0f || nl <= 0.0f)
                        continue;
                     const float gV = nl * std::sqrt(a2 + (1.0f - a2) * mu * mu), gL = mu * std::sqrt(a2 + (1.0f - a2) * nl * nl);
                     sum += (0.5f / (gV + gL)) * nl * 4.0f * vh / std::max(ct, 1e-6f);
                  }
               // Metal stores R16Unorm; the same rounding keeps the two tracers' tables equal.
               e[(size_t)y * kN + x] = (float)std::lround(std::min(1.0, std::max(0.0, sum / (n1 * n2))) * 65535.0) / 65535.0f;
            }
         return MakeImg(*mC, kN, kN, VK_FORMAT_R32_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mELut) &&
                UploadMipped(*mC, mELut, e.data(), e.size() * 4);
      }
      // The HDRI's sampling table (MetalRaster::BuildEnvCdf): 256 x 128 cells of luminance * sin(theta).
      void BuildEnvCdf(const engine::Frame& fr)
      {
         mCdfRev = mEnvRev;
         const int NX = 256, NY = 128;
         std::vector<double> w((size_t)NX * NY, 0.0);
         double total = 0.0;
         for (int cy = 0; cy < NY; cy++)
         {
            const int y0 = cy * fr.envH / NY, y1 = std::max(y0 + 1, (cy + 1) * fr.envH / NY);
            const double st = std::sin(((double)cy + 0.5) / NY * 3.14159265358979);
            for (int cx = 0; cx < NX; cx++)
            {
               const int x0 = cx * fr.envW / NX, x1 = std::max(x0 + 1, (cx + 1) * fr.envW / NX);
               double sum = 0.0;
               for (int y = y0; y < y1 && y < fr.envH; y++)
                  for (int x = x0; x < x1 && x < fr.envW; x++)
                  {
                     const float* px = fr.envPixels + ((size_t)y * fr.envW + x) * 3;
                     const float l = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
                     sum += std::isfinite(l) ? std::min(std::max(l, 0.0f), 65504.0f) : 0.0f;
                  }
               w[(size_t)cy * NX + cx] = sum / ((double)(y1 - y0) * (x1 - x0)) * st;
               total += w[(size_t)cy * NX + cx];
            }
         }
         vkDeviceWaitIdle(mC->dev);
         FreeImg(*mC, mCdf);
         if (!(total > 0.0))
            return;
         const double floorW = total / ((double)NX * NY) * 1e-3;
         total += floorW * NX * NY;
         std::vector<float> t((size_t)NX * (2 * NY + 1), 0.0f);
         double rows = 0.0;
         for (int cy = 0; cy < NY; cy++)
         {
            double row = 0.0;
            for (int cx = 0; cx < NX; cx++) row += w[(size_t)cy * NX + cx] + floorW;
            double run = 0.0;
            for (int cx = 0; cx < NX; cx++)
            {
               const double c = w[(size_t)cy * NX + cx] + floorW;
               run += c;
               t[(size_t)cy * NX + cx] = cx == NX - 1 ? 1.0f : (float)(run / row);
               t[(size_t)(NY + 1 + cy) * NX + cx] = (float)(c / total * NX * NY);
            }
            rows += row;
            t[(size_t)NY * NX + cy] = cy == NY - 1 ? 1.0f : (float)(rows / total);
         }
         if (!MakeImg(*mC, NX, 2 * NY + 1, VK_FORMAT_R32_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mCdf) ||
             !UploadMipped(*mC, mCdf, t.data(), t.size() * 4))
            FreeImg(*mC, mCdf);
      }
      // The running sums and the denoiser's images, RGBA32F storage, kept in GENERAL. A new size starts over.
      bool PtTargets(int w, int h)
      {
         if (mPtImg[0].img && mPtImg[0].w == w && mPtImg[0].h == h)
            return true;
         vkDeviceWaitIdle(mC->dev);
         for (Img& m : mPtImg) FreeImg(*mC, m);
         for (Img& m : mPtImg)
            if (!MakeImg(*mC, w, h, VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_IMAGE_USAGE_STORAGE_BIT, m))
               return false;
         VkCommandBuffer cb = mC->Begin();
         for (Img& m : mPtImg) ToLayout(cb, m, VK_IMAGE_LAYOUT_GENERAL);
         mPtN = 0;
         return mC->Submit(cb);
      }
      VkPipeline Compute(VkShaderModule m)
      {
         VkComputePipelineCreateInfo ci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
         ci.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, m, "main", nullptr };
         ci.layout = mPtLayout;
         VkPipeline p = VK_NULL_HANDLE;
         vkCreateComputePipelines(mC->dev, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
         return p;
      }
      bool PtPipelines(int fi, VkFormat fmt)
      {
         if (mPtPso && mDnPrepPso && mDnAtrousPso && mPtResPso[fi])
            return true;
         const Ctx& C = *mC;
         if (!mPtSetLayout)
         {
            const VkShaderStageFlags cf = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutBinding b[20] = {};
            for (uint32_t i = 0; i < 20; i++)
            {
               b[i].binding = i; b[i].descriptorCount = 1; b[i].stageFlags = cf;
               b[i].descriptorType = i <= 8 || i >= 18 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                     : i <= 10       ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                     : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            li.bindingCount = 20; li.pBindings = b;
            if (vkCreateDescriptorSetLayout(C.dev, &li, nullptr, &mPtSetLayout) != VK_SUCCESS)
               return false;
            const VkDescriptorPoolSize ps[3] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2 },
                                                 { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7 } };
            VkDescriptorPoolCreateInfo pi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = ps;
            if (vkCreateDescriptorPool(C.dev, &pi, nullptr, &mPtPool) != VK_SUCCESS)
               return false;
            VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = mPtPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &mPtSetLayout;
            if (vkAllocateDescriptorSets(C.dev, &ai, &mPtSet) != VK_SUCCESS)
               return false;
            const VkDescriptorSetLayout two[2] = { mSetLayout, mPtSetLayout };
            VkPushConstantRange pr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FramePush) };
            VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pl.setLayoutCount = 2; pl.pSetLayouts = two; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pr;
            if (vkCreatePipelineLayout(C.dev, &pl, nullptr, &mPtLayout) != VK_SUCCESS)
               return false;
            if (!MakeBuf(C, 64 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mHistB) ||
                !MakeBuf(C, (VkDeviceSize)(1u << 19) * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, mCausB))
               return false;
            const float zero = 0.0f;
            if (!MakeImg(C, 1, 1, VK_FORMAT_R32_SFLOAT, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mCdfDummy) ||
                !UploadMipped(C, mCdfDummy, &zero, 4))
               return false;
         }
         if (!mPtPso) { VkShaderModule m = Module(C, kVkPtComp, sizeof(kVkPtComp)); mPtPso = Compute(m); vkDestroyShaderModule(C.dev, m, nullptr); }
         if (!mDnPrepPso) { VkShaderModule m = Module(C, kVkDnprepComp, sizeof(kVkDnprepComp)); mDnPrepPso = Compute(m); vkDestroyShaderModule(C.dev, m, nullptr); }
         if (!mDnAtrousPso) { VkShaderModule m = Module(C, kVkDnatrousComp, sizeof(kVkDnatrousComp)); mDnAtrousPso = Compute(m); vkDestroyShaderModule(C.dev, m, nullptr); }
         if (!mCausPso) { VkShaderModule m = Module(C, kVkCausComp, sizeof(kVkCausComp)); mCausPso = Compute(m); vkDestroyShaderModule(C.dev, m, nullptr); } // row 4.4; none = no caustics
         if (!mPtResPso[fi])
         {
            VkShaderModule bv = Module(C, kVkBgVert, sizeof(kVkBgVert)), rf = Module(C, kVkPtresFrag, sizeof(kVkPtresFrag));
            PsoDesc d;
            d.vs = bv; d.fs = rf; d.layout = mPtLayout; d.color = fmt; d.depthTest = false; d.depthWrite = false;
            mPtResPso[fi] = MakePso(C, d);
            vkDestroyShaderModule(C.dev, bv, nullptr); vkDestroyShaderModule(C.dev, rf, nullptr);
         }
         if (!mPtPso || !mDnPrepPso || !mDnAtrousPso || !mPtResPso[fi])
         {
            VulkanFail(std::string("vulkan path tracer pipeline: not built") + (mPtPso ? "" : " (pt)") + (mDnPrepPso ? "" : " (dnprep)") +
                       (mDnAtrousPso ? "" : " (dnatrous)") + (mPtResPso[fi] ? "" : " (ptres)"));
            return false;
         }
         return true;
      }
      void PtWriteSet(bool envLight)
      {
         const Buf* bufs[9] = { &mPtUB, &mLightB, &mEmB, &mBaseB, &mTriB, &mNodeB, &mVtxB, &mIdxB, &mUvB };
         VkDescriptorBufferInfo bi[11];
         for (int i = 0; i < 9; i++) bi[i] = { bufs[i]->buf, 0, VK_WHOLE_SIZE };
         bi[9] = { mHistB.buf, 0, VK_WHOLE_SIZE };
         bi[10] = { mCausB.buf, 0, VK_WHOLE_SIZE };
         VkDescriptorImageInfo si[2] = { { VK_NULL_HANDLE, (envLight ? mCdf : mCdfDummy).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                         { VK_NULL_HANDLE, mELut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
         VkDescriptorImageInfo ii[7];
         for (int i = 0; i < 7; i++) ii[i] = { VK_NULL_HANDLE, mPtImg[i].view, VK_IMAGE_LAYOUT_GENERAL };
         VkWriteDescriptorSet w[20] = {};
         for (uint32_t i = 0; i < 20; i++)
         {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = mPtSet; w[i].dstBinding = i; w[i].descriptorCount = 1;
            if (i <= 8) { w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[i].pBufferInfo = &bi[i]; }
            else if (i >= 18) { w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[i].pBufferInfo = &bi[i - 9]; }
            else if (i <= 10) { w[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[i].pImageInfo = &si[i - 9]; }
            else { w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[i].pImageInfo = &ii[i - 11]; }
         }
         vkUpdateDescriptorSets(mC->dev, 20, w, 0, nullptr);
      }
      void PtFree()
      {
         const Ctx& C = *mC;
         for (Buf* b : { &mPtUB, &mLightB, &mEmB, &mBaseB, &mTriB, &mNodeB, &mVtxB, &mIdxB, &mUvB, &mHistB, &mCausB }) FreeBuf(C, *b);
         for (Img& m : mPtImg) FreeImg(C, m);
         FreeImg(C, mCdf); FreeImg(C, mCdfDummy); FreeImg(C, mELut);
         for (VkPipeline p : { mPtPso, mDnPrepPso, mDnAtrousPso, mCausPso, mPtResPso[0], mPtResPso[1] })
            if (p) vkDestroyPipeline(C.dev, p, nullptr);
         if (mPtLayout) vkDestroyPipelineLayout(C.dev, mPtLayout, nullptr);
         if (mPtPool) vkDestroyDescriptorPool(C.dev, mPtPool, nullptr);
         if (mPtSetLayout) vkDestroyDescriptorSetLayout(C.dev, mPtSetLayout, nullptr);
      }
      std::vector<Cpu> mCpu;
      uint64_t mMeshGen = 0, mBvhKey = 0;
      VkDescriptorSetLayout mPtSetLayout = VK_NULL_HANDLE;
      VkDescriptorPool mPtPool = VK_NULL_HANDLE;
      VkDescriptorSet mPtSet = VK_NULL_HANDLE;
      VkPipelineLayout mPtLayout = VK_NULL_HANDLE;
      VkPipeline mPtPso = VK_NULL_HANDLE, mDnPrepPso = VK_NULL_HANDLE, mDnAtrousPso = VK_NULL_HANDLE, mCausPso = VK_NULL_HANDLE, mPtResPso[2] = {};
      Buf mPtUB, mLightB, mEmB, mBaseB, mTriB, mNodeB, mVtxB, mIdxB, mUvB, mHistB, mCausB;
      Img mPtImg[7]; // acc, aux, gd, dnC0, dnC1, dnG, dnA (set 1, bindings 11..17)
      Img mCdf, mCdfDummy, mELut;
      unsigned long long mCdfRev = ~0ull - 1;
      int mEmCount = 0, mPtN = 0, mPtFrame = 0;
      float mPtNoise = -1.0f;
      // On by default; INFINITE_ENGINE_DENOISE=0 turns it off, _DNSTEPS (5) and _DNSIGL (4) are A/B knobs (as Metal).
      const bool mDenoise = [] { const char* e = getenv("INFINITE_ENGINE_DENOISE"); return !e || atoi(e) > 0; }();
      const int mDnSteps = [] { const char* e = getenv("INFINITE_ENGINE_DNSTEPS"); return e ? atoi(e) : 5; }();
      const float mDnSigL = [] { const char* e = getenv("INFINITE_ENGINE_DNSIGL"); return e ? (float)atof(e) : 4.0f; }();

      std::shared_ptr<Ctx> mC;
      bool mOk = false;
      std::vector<Gpu> mMeshes;
      std::vector<std::pair<uint32_t, uint32_t>> mRuns;
      std::vector<LightU> mLightAll;
      VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
      VkDescriptorPool mPool = VK_NULL_HANDLE;
      VkDescriptorSet mSet[2] = {}; // [0] main pass, [1] shadow pass
      VkPipelineLayout mLayout = VK_NULL_HANDLE;
      VkPipeline mPso[2] = {}, mBgPso[2] = {}, mShPso = VK_NULL_HANDLE;
      VkSampler mSmpShadow = VK_NULL_HANDLE, mSmpEnv = VK_NULL_HANDLE, mSmpC = VK_NULL_HANDLE, mSmpR = VK_NULL_HANDLE, mSmpM = VK_NULL_HANDLE;
      VkQueryPool mQuery = VK_NULL_HANDLE;
      Buf mFrameBuf, mInstBuf;
      Img mWhite, mEnv, mEnvDummy, mShadow, mShadowDum, mDepth;
      Img* mEnvBound = nullptr;
      unsigned long long mEnvRev = ~0ull;
      std::vector<Img> mMapCache;
      int mMapSlot[engine::kMaxTextures] = {}; // index into mMapCache, -1 = white
   };

   // ---- device (MetalDevice)
   class VulkanDevice final : public Device
   {
   public:
      explicit VulkanDevice(std::shared_ptr<Ctx> c) : mC(std::move(c)) {}
      std::string Name() const override { return "Vulkan (" + mC->name + ")"; }
      std::unique_ptr<SharedImage> CreateSharedImage(int w, int h, Format f) override
      {
         auto img = std::make_unique<VulkanImage>(mC, w, h, f);
         return img->Ok() ? std::unique_ptr<SharedImage>(std::move(img)) : nullptr;
      }
      std::unique_ptr<RasterRenderer> CreateRasterRenderer() override
      {
         auto r = std::make_unique<VulkanRaster>(mC);
         return r->Ok() ? std::unique_ptr<RasterRenderer>(std::move(r)) : nullptr;
      }

   private:
      std::shared_ptr<Ctx> mC;
   };

   bool HasExt(const std::vector<VkExtensionProperties>& v, const char* n)
   {
      for (const auto& e : v)
         if (std::strcmp(e.extensionName, n) == 0)
            return true;
      return false;
   }
   std::shared_ptr<Ctx> MakeCtx()
   {
      auto c = std::make_shared<Ctx>();
      uint32_t n = 0;
      vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
      std::vector<VkExtensionProperties> ie(n);
      vkEnumerateInstanceExtensionProperties(nullptr, &n, ie.data());
      std::vector<const char*> iext;
      VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
      if (HasExt(ie, "VK_KHR_portability_enumeration"))
      {
         iext.push_back("VK_KHR_portability_enumeration");
         ici.flags |= 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
      }
      VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
      app.pApplicationName = "Infinite"; app.pEngineName = "Infinite engine"; app.apiVersion = VK_API_VERSION_1_3;
      ici.pApplicationInfo = &app; ici.enabledExtensionCount = (uint32_t)iext.size(); ici.ppEnabledExtensionNames = iext.data();
      if (vkCreateInstance(&ici, nullptr, &c->inst) != VK_SUCCESS)
      {
         VulkanFail("vulkan: no instance");
         return nullptr;
      }
      n = 0;
      vkEnumeratePhysicalDevices(c->inst, &n, nullptr);
      std::vector<VkPhysicalDevice> pds(n);
      vkEnumeratePhysicalDevices(c->inst, &n, pds.data());
      for (VkPhysicalDevice pd : pds)
      {
         VkPhysicalDeviceProperties p;
         vkGetPhysicalDeviceProperties(pd, &p);
         if (p.apiVersion < VK_API_VERSION_1_3)
            continue;
         uint32_t qn = 0;
         vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
         std::vector<VkQueueFamilyProperties> qf(qn);
         vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf.data());
         for (uint32_t q = 0; q < qn; q++)
            if (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
               c->phys = pd; c->qfam = q; c->props = p;
               break;
            }
         if (c->phys)
            break;
      }
      if (!c->phys)
      {
         VulkanFail("vulkan: no Vulkan 1.3 device with a graphics queue");
         return nullptr;
      }
      c->name = c->props.deviceName;
      vkGetPhysicalDeviceMemoryProperties(c->phys, &c->mem);
      n = 0;
      vkEnumerateDeviceExtensionProperties(c->phys, nullptr, &n, nullptr);
      std::vector<VkExtensionProperties> de(n);
      vkEnumerateDeviceExtensionProperties(c->phys, nullptr, &n, de.data());
      std::vector<const char*> dext;
      if (HasExt(de, "VK_KHR_portability_subset"))
         dext.push_back("VK_KHR_portability_subset");
      VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
      VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
      f12.pNext = &f13;
      VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
      f2.pNext = &f12;
      vkGetPhysicalDeviceFeatures2(c->phys, &f2);
      if (!f12.scalarBlockLayout || !f13.dynamicRendering || !f2.features.shaderSampledImageArrayDynamicIndexing)
      {
         VulkanFail("vulkan: device lacks scalar block layout, dynamic rendering or sampled-image array indexing");
         return nullptr;
      }
      VkPhysicalDeviceVulkan13Features w13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
      w13.dynamicRendering = VK_TRUE;
      VkPhysicalDeviceVulkan12Features w12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
      w12.pNext = &w13; w12.scalarBlockLayout = VK_TRUE;
      VkPhysicalDeviceFeatures2 w2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
      w2.pNext = &w12;
      w2.features.samplerAnisotropy = f2.features.samplerAnisotropy;
      w2.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
      w2.features.fragmentStoresAndAtomics = f2.features.fragmentStoresAndAtomics; // ptres.frag declares the PT images
      const float prio = 1.0f;
      VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
      qi.queueFamilyIndex = c->qfam; qi.queueCount = 1; qi.pQueuePriorities = &prio;
      VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
      di.pNext = &w2; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
      di.enabledExtensionCount = (uint32_t)dext.size(); di.ppEnabledExtensionNames = dext.data();
      if (vkCreateDevice(c->phys, &di, nullptr, &c->dev) != VK_SUCCESS)
      {
         VulkanFail("vulkan: device creation failed");
         return nullptr;
      }
      vkGetDeviceQueue(c->dev, c->qfam, 0, &c->queue);
      VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = c->qfam;
      if (vkCreateCommandPool(c->dev, &pi, nullptr, &c->pool) != VK_SUCCESS)
         return nullptr;
      return c;
   }
}

// The Vulkan backend, or null (with LastError set) when no usable device. Must be called with the app's GL context
// current, as CreateDevice.
std::unique_ptr<Device> CreateVulkanDevice()
{
   std::shared_ptr<Ctx> c = MakeCtx();
   return c ? std::make_unique<VulkanDevice>(c) : nullptr;
}
}

#endif

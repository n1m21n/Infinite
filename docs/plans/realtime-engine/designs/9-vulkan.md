# 9.2 Vulkan backend: design (skeleton only, SDK not approved)

`src/engine/rhi/VulkanDevice.cpp` sits behind `#ifdef INFINITE_VULKAN`. Without it the file compiles to nothing, and
the macOS build stays as it is. The layout follows `MetalDevice.mm` section by section. 9.3 and later rows wait for
the Vulkan SDK, MoltenVK and glslang downloads (OPUS_PLAN section 3).

## Every RHI method: Metal call to Vulkan call

| RHI method | Metal today | Vulkan |
|---|---|---|
| `CreateDevice()` | `MTLCreateSystemDefaultDevice`, `newCommandQueue` | `vkCreateInstance` with `VK_KHR_external_memory_capabilities`; pick the first device with `VK_KHR_external_memory_fd` (Linux) or `_win32` (Windows), `VK_KHR_ray_query` optional; one graphics+compute queue |
| `Device::Name()` | `MTLDevice.name` | `VkPhysicalDeviceProperties::deviceName` |
| `CreateSharedImage(w,h,fmt)` | IOSurface-backed `MTLTexture`, GL rectangle texture over the same surface | `VkImage` with `VkExternalMemoryImageCreateInfo`; `vkGetMemoryFdKHR` / `vkGetMemoryWin32HandleKHR`; GL side `glCreateMemoryObjectsEXT` + `glImportMemoryFdEXT` + `glTexStorageMem2DEXT`; one `VkSemaphore` exported to GL (`GL_EXT_semaphore`) for each direction |
| `SharedImage::GlTexture()` | the rectangle texture | the `GL_TEXTURE_2D` made by `glTexStorageMem2DEXT` (texel coords 0..1, so `GlTexture` callers sample a 2D texture: one flag in the image) |
| `SharedImage::DrawTestTriangle` | `tri_vs` + clear pass, `waitUntilCompleted` | dynamic rendering (`vkCmdBeginRendering`), the same triangle, `vkQueueSubmit` + fence wait |
| `CreateRasterRenderer()` | `newLibraryWithSource` (MSL text) + pipelines | SPIR-V from GLSL 450 (glslang at build time, or `shaderc` at run time for Field programs); `VkPipelineCache` |
| `UploadMeshes` | `newBufferWithBytes` per stream (`MakeGpu`), BLAS per mesh (`BuildBlas`) | `VkBuffer` per stream in device-local memory through a staging buffer; BLAS via `vkCmdBuildAccelerationStructuresKHR` when ray query is there, else the compute LBVH (row 9.4) |
| `UpdateMesh` | rewrites one `Gpu`, refits that BLAS | same buffers rewritten; BLAS rebuilt with `VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR` |
| `Render` | one command buffer: maps, TLAS, cull (`cull_k`, `args_k`), shadow, prepass, vis mask, `r_fs` MRT, trace, resolve, a-trous, DOF, bloom; `commit` + `waitUntilCompleted` | one `VkCommandBuffer`, the same passes as dynamic-rendering passes and compute dispatches, image barriers between them; fence wait; `gpuMs` from two timestamp queries |
| `PathTrace` | `pt_fs` full-screen pass with the inline intersector | `pt` as a compute shader with `rayQueryEXT`; without ray query, the compute BVH `traceRay()` |
| `PathTraceNoise` | CPU readback of the running-variance target | same readback (`vkCmdCopyImageToBuffer`) |
| `FieldLibraryBuilds` | MSL library rebuilt per `fieldMslKey` | SPIR-V module rebuilt per key (Field's GLSL backend already exists; no MSL step) |
| `RunFieldPixel` | `fld_fs` over an RGBA32F target | the GLSL program the GL path already runs, compiled to SPIR-V, same target |
| `LastError()` | first failed pipeline's compiler line | first failed `vkCreate*Pipelines` / shaderc message |

## Shader entry points

| Metal entry | Stage | Vulkan GLSL 450 twin | Notes |
|---|---|---|---|
| `tri_vs` | vertex | `tri.vert` | test triangle |
| `r_vs`, `ri_vs`, `rr_vs`, `rp_vs` | vertex | `r.vert` with specialization constants for instanced / ribbon / point | `[[buffer(n)]]` becomes one descriptor set: set 0 frame, set 1 instances, set 2 mesh streams (buffer device address, as `RInstH` already stores 64-bit addresses) |
| `r_fs` | fragment | `r.frag` | MRT, 4 colour targets; `discard_fragment` becomes `discard`; maps as a descriptor array `sampler2D maps[kMaxTextures]` |
| `vis_fs` | fragment | `vis.frag` + `GL_EXT_ray_query` | half-res shadow mask |
| `tr_fs` | fragment | `tr.frag` + ray query | GI and reflection trace |
| `res_fs` | fragment | `res.frag` | resolve and history |
| `pt_fs` | fragment | `pt.comp` | path tracer; compute is the natural form on Vulkan |
| `cull_k`, `args_k` | compute | `cull.comp`, `args.comp` | `atomic_uint` becomes `atomicAdd` on an SSBO; output is `VkDrawIndexedIndirectCommand` for `vkCmdDrawIndexedIndirectCount` |
| DOF, bloom, a-trous, composite | fragment | same names `.frag` | plain post passes |

## What is shared

Maths sits in the `Render3D*.h` macro headers (`Render3DBsdf.h`, `Render3DThinFilm.h`, ...). They are written once
with `vec3`-is-`float3` defines and already compiled by GL GLSL and Metal MSL. The Vulkan GLSL includes the same
headers with the GLSL defines. Row 9.1 counts how much of the Metal source is shared and how much is Metal-only glue.

## Order once the downloads are approved

1. 9.3: `SharedImage` and the raster `Render` (no rays). Test on this Mac through MoltenVK against the Metal live
   picture.
2. 9.4: ray query, with the compute BVH as the fallback.
3. 9.5: lavapipe in the Linux container.
4. 9.6: owner hardware (`9-vulkan-bench.md`).

// Platforms without a default engine backend (Windows and Linux): the GL renderer stays. Built with INFINITE_VULKAN,
// INFINITE_RHI=vulkan picks the Vulkan backend (VulkanDevice.cpp; row 9.5 runs it on lavapipe in the Linux container).
#include "engine/rhi/Rhi.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace rhi
{
namespace
{
   std::string gLastError;
}
void VulkanFail(const std::string& what)
{
   std::fprintf(stderr, "%s\n", what.c_str());
   if (gLastError.empty())
      gLastError = what;
}
#if defined(INFINITE_VULKAN)
std::unique_ptr<Device> CreateVulkanDevice();
#endif
std::unique_ptr<Device> CreateDevice()
{
#if defined(INFINITE_VULKAN)
   if (const char* r = getenv("INFINITE_RHI"); r && std::string(r) == "vulkan")
      return CreateVulkanDevice();
#endif
   return nullptr;
}
const std::string& LastError() { return gLastError; }
}

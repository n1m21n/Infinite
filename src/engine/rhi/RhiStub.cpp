// Platforms without an engine backend yet (Windows and Linux wait for Vulkan, stage R6): the GL renderer stays.
#include "engine/rhi/Rhi.h"

namespace rhi
{
std::unique_ptr<Device> CreateDevice() { return nullptr; }
}

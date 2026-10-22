// Turbo 0.62: the RTX entry points for builds without the module (the MIT
// edition, or no CUDA Toolkit at configure time). Render 3D then draws
// raster in RTX mode and says why.

#include "RtxBackend.h"

std::unique_ptr<RtxBackend> CreateRtxBackend(std::string& why)
{
   why = "RTX is not in this build (needs the full edition built with the CUDA Toolkit)";
   return nullptr;
}

bool RtxModuleBuilt()
{
   return false;
}

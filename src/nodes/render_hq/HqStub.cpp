// Turbo 0.71 (S1): the Render 3D HQ entry points for builds without the module
// (INFINITE_ENABLE_RENDER_HQ=OFF). The node then draws through Render 3D and
// says why.

#include "HqRenderer.h"

std::unique_ptr<HqRenderer> CreateHqRenderer(std::string& why)
{
   why = "Render 3D HQ is not in this build (INFINITE_ENABLE_RENDER_HQ=OFF): drawing as Render 3D";
   return nullptr;
}

bool HqModuleBuilt()
{
   return false;
}

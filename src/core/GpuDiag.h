// fork: GPU renderer diagnostics. Returns a warning (or nullptr) when the GL renderer looks like an
// integrated / software GPU. Windows-only heuristic, NOT verified on Windows.
#pragma once
#include <cstring>
#include <string>

inline const char* GpuDiagWarning(const char* renderer)
{
#if defined(_WIN32)
   if (renderer == nullptr)
      return nullptr;
   std::string r(renderer);
   if (r.find("Microsoft Basic") != std::string::npos || r.find("GDI Generic") != std::string::npos ||
       r.find("llvmpipe") != std::string::npos)
      return "Software renderer in use - install the graphics driver.";
   if (r.find("Intel") != std::string::npos)
      return "Integrated Intel GPU in use. If this PC has a discrete GPU, set Graphics processor to High performance and restart.";
#else
   (void)renderer;
#endif
   return nullptr;
}

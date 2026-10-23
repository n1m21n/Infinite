#pragma once

// Turbo 0.70 (F2): Tracy profiler hooks (https://github.com/wolfpld/tracy, BSD-3).
// Built only with -DINFINITE_ENABLE_TRACY=ON (CMake defines INFINITE_TRACY);
// otherwise every macro is empty and nothing of Tracy is compiled in.
// Connect the Tracy profiler GUI (same version as the client) to a running
// Infinite-Turbo.exe to see CPU zones per frame stage and node cook, and GPU
// zones from GL timer queries.
//
//   INF_PROFILE_ZONE("name")             CPU zone for the enclosing scope (literal name)
//   INF_PROFILE_ZONE_DYN("kind", text)   CPU zone named "kind", tagged with a std::string
//   INF_PROFILE_GPU_ZONE("name")         GPU zone (GL timer queries), literal name
//   INF_PROFILE_GPU_ZONE_DYN(text)       GPU zone with a runtime name (std::string)
//   INF_PROFILE_GPU_CONTEXT()            once, after the GL context is current
//   INF_PROFILE_GPU_COLLECT()            once per frame, after the swap
//   INF_PROFILE_FRAME()                  end of a main-loop frame

#ifdef INFINITE_TRACY

#include "platform/OpenGLHeaders.h" // TracyOpenGL needs the GL entry points first
#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>
#include <string>

#define INF_PROFILE_ZONE(name) ZoneScopedN(name)
#define INF_PROFILE_ZONE_DYN(kind, text)                                                                          \
   ZoneScopedN(kind);                                                                                             \
   ZoneText((text).c_str(), (text).size())
#define INF_PROFILE_GPU_ZONE(name) TracyGpuZone(name)
#define INF_PROFILE_GPU_ZONE_DYN(text) TracyGpuZoneTransient(infTracyGpuZone, (text).c_str(), true)
#define INF_PROFILE_GPU_CONTEXT() TracyGpuContext
#define INF_PROFILE_GPU_COLLECT() TracyGpuCollect
#define INF_PROFILE_FRAME() FrameMark

#else

#define INF_PROFILE_ZONE(name)
#define INF_PROFILE_ZONE_DYN(kind, text)
#define INF_PROFILE_GPU_ZONE(name)
#define INF_PROFILE_GPU_ZONE_DYN(text)
#define INF_PROFILE_GPU_CONTEXT()
#define INF_PROFILE_GPU_COLLECT()
#define INF_PROFILE_FRAME()

#endif

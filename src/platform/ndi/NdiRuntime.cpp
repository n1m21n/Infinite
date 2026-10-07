#include "NdiRuntime.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
   std::once_flag gOnce;
   const NDIlib_v6* gApi = nullptr;
   std::string gVersion;

   // Candidate library locations, most specific first. The bare names at the
   // end defer to the platform's default search (rpath / LD_LIBRARY_PATH / PATH).
   std::vector<std::string> Candidates()
   {
      std::vector<std::string> out;
      auto addDir = [&](const char* env, const char* file)
      {
         const char* dir = std::getenv(env);
         if (dir != nullptr && *dir != '\0')
            out.push_back(std::string(dir) + "/" + file);
      };
#if defined(_WIN32)
      const char* names[] = {"Processing.NDI.Lib.x64.dll"};
#elif defined(__APPLE__)
      const char* names[] = {"libndi.dylib"};
#else
      const char* names[] = {"libndi.so.6", "libndi.so.5", "libndi.so"};
#endif
      for (const char* n : names)
      {
         addDir("NDI_RUNTIME_DIR_V6", n);
         addDir("NDI_RUNTIME_DIR_V5", n);
#if defined(_WIN32)
         addDir("NDILIB_REDIST_FOLDER", n);
#endif
      }
#if defined(__APPLE__)
      out.push_back("/usr/local/lib/libndi.dylib");
      out.push_back("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib");
#elif !defined(_WIN32)
      out.push_back("/usr/local/lib/libndi.so.6");
      out.push_back("/usr/local/lib/libndi.so");
#endif
      for (const char* n : names)
         out.push_back(n);
      return out;
   }

   void* OpenLibrary(const std::string& path)
   {
#if defined(_WIN32)
      return (void*)LoadLibraryA(path.c_str());
#else
      return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
   }

   void* Symbol(void* lib, const char* name)
   {
#if defined(_WIN32)
      return (void*)GetProcAddress((HMODULE)lib, name);
#else
      return dlsym(lib, name);
#endif
   }

   void LoadOnce()
   {
      for (const std::string& path : Candidates())
      {
         void* lib = OpenLibrary(path);
         if (lib == nullptr)
            continue;
         using LoadFn = const NDIlib_v6* (*)(void);
         auto load = (LoadFn)Symbol(lib, "NDIlib_v6_load");
         const NDIlib_v6* api = load != nullptr ? load() : nullptr;
         if (api != nullptr && api->initialize != nullptr && api->initialize())
         {
            gApi = api;
            const char* v = api->version != nullptr ? api->version() : nullptr;
            gVersion = v != nullptr ? v : "";
            return; // kept loaded for the life of the process
         }
         // Not usable (too old / unsupported CPU): leave it loaded, try the next.
      }
   }
}

namespace Ndi
{
   bool Available()
   {
      std::call_once(gOnce, LoadOnce);
      return gApi != nullptr;
   }

   const NDIlib_v6* Api()
   {
      return Available() ? gApi : nullptr;
   }

   std::string Version()
   {
      return Available() ? gVersion : std::string();
   }

   std::string StatusLine()
   {
      if (!Available())
         return "NDI runtime not found (install from ndi.video)";
      return "NDI " + gVersion;
   }

   std::string ShortSourceName(const std::string& fullName)
   {
      const size_t open = fullName.rfind('(');
      const size_t close = fullName.rfind(')');
      if (open == std::string::npos || close == std::string::npos || close < open)
         return fullName;
      return fullName.substr(open + 1, close - open - 1);
   }

   void CopyRowsFlipped(const unsigned char* src, int srcStride, unsigned char* dst, int dstStride,
                        int rowBytes, int rows)
   {
      for (int y = 0; y < rows; y++)
         std::memcpy(dst + (size_t)y * dstStride, src + (size_t)(rows - 1 - y) * srcStride, (size_t)rowBytes);
   }
}

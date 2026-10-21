#pragma once

#include <string>

#include "Processing.NDI.Lib.h"
#include "Processing.NDI.DynamicLoad.h"

// NDI runtime loader. The NDI library is never bundled: it is found at run
// time (NDI_RUNTIME_DIR_V6/V5 and the standard install paths, then the default
// loader search) and its function table is read through NDIlib_v6_load(), so
// Infinite builds and runs with no NDI installed. One implementation for all
// three OSes (dlopen / LoadLibrary hidden in NdiRuntime.cpp), so the nodes
// carry no platform code. NDI(R) is a registered trademark of Vizrt NDI AB.
namespace Ndi
{
   // Loads and initialises once (thread-safe); later calls are cheap.
   // Returns false when the runtime is missing or the CPU is unsupported.
   bool Available();
   // The function table, or nullptr when !Available().
   const NDIlib_v6* Api();
   // "" when unavailable, else the NDI library's version string.
   std::string Version();
   // One-line status for node bodies / help: version or the install hint.
   std::string StatusLine();

   // Pure helpers, split out so INFINITE_NDITEST can assert them headless.
   // "MACHINE (Source)" -> returns the part inside the last "(...)", or the
   // whole name when there are no parentheses.
   std::string ShortSourceName(const std::string& fullName);
   // Pixel-format shuffle: tightly packed rows <-> padded stride.
   void CopyRowsFlipped(const unsigned char* src, int srcStride, unsigned char* dst, int dstStride,
                        int rowBytes, int rows);
}

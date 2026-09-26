// Out-of-process VST3 bundle probe, mirroring src/scanner_main_win.cpp's
// argv contract exactly: argv[1] is either a single bundle path, or
// "--batch" followed by any number of bundle paths, each probed in order.
// Output is the same tab-separated wire format Platform::ParseProbeOutput
// (in PluginVST3Linux.cpp) parses back on the parent side.
//
// stdout on Linux needs no text-mode fixup - unlike Windows, there is no
// '\n' -> '\r\n' translation to defeat, so this omits the
// _setmode(_fileno(stdout), _O_BINARY) call scanner_main_win.cpp needs.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform/Platform.h"
#include "platform/PluginVST3.h"

int main(int argc, char** argv)
{
   if (argc < 2)
      return 1;

   auto sanitize = [](std::string s)
   {
      for (char& c : s)
         if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
      return s;
   };

   int startIdx = 1;
   if (std::strcmp(argv[1], "--batch") == 0)
      startIdx = 2;

   for (int i = startIdx; i < argc; i++)
   {
      const char* bundlePath = argv[i];
      if (bundlePath == nullptr || bundlePath[0] == '\0')
         continue;

      std::vector<Platform::PluginDesc> descs;
      Platform::DescribeVST3Bundle(bundlePath, descs);
      for (const Platform::PluginDesc& d : descs)
      {
         std::printf("%s\t%s\t%s\t%s\t%s\t%d\n", sanitize(d.format).c_str(), sanitize(d.name).c_str(),
                     sanitize(d.manufacturer).c_str(), sanitize(d.identifier).c_str(), sanitize(d.path).c_str(),
                     d.acceptsNotes ? 1 : 0);
      }
      // Progress marker, printed even when the bundle described nothing:
      // the parent's timeout is idle-based and resets on each of these, and
      // the first bundle without one is the one the child died inside.
      std::printf("@done\t%s\n", bundlePath);
      std::fflush(stdout);
   }
   return 0;
}

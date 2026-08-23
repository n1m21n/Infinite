#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <io.h>

#include "Platform.h"
#include "PluginVST3.h"

int main(int argc, char** argv)
{
   // stdout defaults to text mode, which translates '\n' to '\r\n' and
   // corrupts the tab-separated wire format the parent parses by comparing
   // fields to exact strings like "1" (see PluginVST3Win.cpp's
   // ParseProbeOutput, which strips a trailing '\r' defensively as the other
   // half of this fix).
   _setmode(_fileno(stdout), _O_BINARY);

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
      std::fflush(stdout);
   }
   return 0;
}

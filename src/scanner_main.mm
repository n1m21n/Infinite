#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#import <Cocoa/Cocoa.h>

#include "Platform.h"
#include "PluginVST3.h"

int main(int argc, char** argv)
{
   @autoreleasepool
   {
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];

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
   }
   return 0;
}

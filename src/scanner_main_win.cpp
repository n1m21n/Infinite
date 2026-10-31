#include <cstdio>
#include <string>
#include <vector>

#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <objbase.h>
#include <ole2.h>

#include "Platform.h"
#include "PluginVST3.h"

int main()
{
   // argv is in the ANSI code page, which cannot hold every plugin path, and
   // the parent matches each "@done" line against the UTF-8 path it sent.
   const std::vector<std::string> args = Platform::CommandLineUtf8();

   // stdout defaults to text mode, which translates '\n' to '\r\n' and
   // corrupts the tab-separated wire format the parent parses by comparing
   // fields to exact strings like "1" (see PluginVST3Win.cpp's
   // ParseProbeOutput, which strips a trailing '\r' defensively as the other
   // half of this fix).
   _setmode(_fileno(stdout), _O_BINARY);

   if (args.size() < 2)
      return 1;

   // Parent's health check: proves this exe launches and can write to the
   // pipe, without touching any plugin.
   if (args[1] == "--selftest")
   {
      std::printf("@ok\n");
      std::fflush(stdout);
      return 0;
   }

   auto sanitize = [](std::string s)
   {
      for (char& c : s)
         if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
      return s;
   };

   // A plugin host loads plugins on a thread with COM/OLE initialised (JUCE's
   // message thread does). Some plugins register OLE services or call into COM
   // while loading, and fault in a bare console process where a real host
   // would not.
   OleInitialize(nullptr);

   size_t startIdx = 1;
   if (args[1] == "--batch")
      startIdx = 2;

   for (size_t i = startIdx; i < args.size(); i++)
   {
      const std::string& bundlePath = args[i];
      if (bundlePath.empty())
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
      std::printf("@done\t%s\n", bundlePath.c_str());
      std::fflush(stdout);
   }
   // Leave without running static destructors or unloading the plugin DLLs.
   // Several plugins (notably Native Instruments) crash while being torn down,
   // which turned a successful scan into a non-zero exit that the parent reads
   // as "the bundle crashed" and blocklists. Everything useful has already
   // been written and flushed above.
   std::fflush(stdout);
   TerminateProcess(GetCurrentProcess(), 0);
   return 0;
}

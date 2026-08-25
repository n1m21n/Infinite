#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include <juce_events/juce_events.h>

#include "platform/Platform.h"

namespace Platform
{
   void EnsureJuceInitialised()
   {
      static std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juceGui;
      if (!juceGui)
         juceGui = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
   }
}

namespace
{
   std::string Utf8(const wchar_t* text)
   {
      if (text == nullptr || text[0] == L'\0')
         return {};
      const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
      if (count <= 1)
         return {};
      std::string out((size_t)count, '\0');
      WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), count, nullptr, nullptr);
      out.resize((size_t)count - 1);
      return out;
   }

   std::string Sanitized(std::string text)
   {
      for (char& c : text)
         if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
      return text;
   }
}

int wmain(int argc, wchar_t** argv)
{
   if (argc < 2)
      return 2;

   Platform::SuppressAppUIForScanChild();
   int firstPath = 1;
   if (std::wcscmp(argv[1], L"--vst3-scan-bundle") == 0 || std::wcscmp(argv[1], L"--batch") == 0)
      firstPath = 2;
   if (firstPath >= argc)
      return 2;

   for (int i = firstPath; i < argc; ++i)
   {
      const std::string path = Utf8(argv[i]);
      if (path.empty())
         continue;
      std::vector<Platform::PluginDesc> descriptions;
      Platform::DescribeVST3Bundle(path, descriptions);
      for (const Platform::PluginDesc& d : descriptions)
      {
         std::printf("%s\t%s\t%s\t%s\t%s\t%d\n",
                     Sanitized(d.format).c_str(), Sanitized(d.name).c_str(),
                     Sanitized(d.manufacturer).c_str(), Sanitized(d.identifier).c_str(),
                     Sanitized(d.path).c_str(), d.acceptsNotes ? 1 : 0);
      }
      std::fflush(stdout);
   }
   return 0;
}

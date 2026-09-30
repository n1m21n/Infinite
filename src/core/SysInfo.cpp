#include "SysInfo.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/OpenGLHeaders.h"
#include "platform/Platform.h"

namespace SysInfo
{
   void PrintAndExit(GLFWwindow* /*window*/, const char* version)
   {
      std::printf("================ Infinite-Turbo System Info ================\n");
      std::printf("Version: %s\n", version ? version : "dev");
      const GLubyte* vendor = glGetString(GL_VENDOR);
      const GLubyte* renderer = glGetString(GL_RENDERER);
      const GLubyte* glVersion = glGetString(GL_VERSION);
      const GLubyte* glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
      std::printf("GL Vendor:    %s\n", vendor ? (const char*)vendor : "null");
      std::printf("GL Renderer:  %s\n", renderer ? (const char*)renderer : "null");
      std::printf("GL Version:   %s\n", glVersion ? (const char*)glVersion : "null");
      std::printf("GLSL Version: %s\n", glsl ? (const char*)glsl : "null");

      std::printf("Audio driver: %s\n", Platform::AudioCurrentDriverName().c_str());
      const std::vector<Platform::AudioDeviceInfo> devices = Platform::AudioListDevices();
      std::printf("Audio devices: %d\n", (int)devices.size());
      for (const auto& dev : devices)
         std::printf("  [%u] %s%s%s\n", dev.deviceId, dev.name.c_str(), dev.isInput ? " [input]" : "",
                     dev.isOutput ? " [output]" : "");

      const bool midiWasRunning = Platform::MidiIsRunning();
      std::string midiError;
      if (!midiWasRunning)
         Platform::MidiStart(midiError);
      std::printf("MIDI ports: %s\n", Platform::MidiDeviceSummary().c_str());
      if (!midiWasRunning)
         Platform::MidiStop();

      const std::vector<std::string> blocklist = Platform::VST3Blocklist();
      std::printf("VST3 blocklist: %d\n", (int)blocklist.size());
      for (const std::string& b : blocklist)
         std::printf("  - %s\n", b.c_str());

      std::printf("Crash reports: %s\n", Platform::CrashReportDirectory().c_str());
      std::printf("============================================================\n");
      std::fflush(stdout);
      std::exit(0);
   }

   void CrashTest()
   {
      volatile int* bad = nullptr;
      *bad = 42;
   }
}

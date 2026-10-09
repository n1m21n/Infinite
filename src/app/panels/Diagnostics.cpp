// Help > Copy system info. One plain-text block for a bug report: version, OS, GPU, audio, plugins, recent problems.
// It only reads state the app already holds (plus one device list) and goes to the clipboard; nothing is sent anywhere.
#include "app/AppShared.h"
#include "audio/AudioEngine.h"
#include "audio/PluginScanner.h"
#include "core/Notices.h"
#include "core/gl3.h"
#include "platform/Platform.h"

#if !defined(_WIN32)
#include <sys/utsname.h>
#endif

namespace app
{
namespace
{
   std::string OsLine()
   {
#if defined(_WIN32)
      return "Windows";
#else
      struct utsname un {};
      if (uname(&un) != 0)
         return "POSIX";
      return std::string(un.sysname) + " " + un.release + " (" + un.machine + ")";
#endif
   }

   std::string GlString(unsigned int name)
   {
      const GLubyte* s = glGetString(name);
      return s != nullptr ? std::string((const char*)s) : std::string("unknown");
   }
}

std::string BuildSystemInfo()
{
   std::string out;
   out += "Infinite " INFINITE_VERSION_STRING "\n";
#ifdef NDEBUG
   out += "Build: release\n";
#else
   out += "Build: debug\n";
#endif
   out += "OS: " + OsLine() + "\n";
   out += "GPU: " + GlString(GL_RENDERER) + " (" + GlString(GL_VENDOR) + ")\n";
   out += "OpenGL: " + GlString(GL_VERSION) + "\n";

   const double rate = AudioEngine::Instance().SampleRate();
   if (rate > 0.0)
      out += "Audio: running at " + std::to_string((int)rate) + " Hz\n";
   else
      out += "Audio: stopped" + (gAudioStartError.empty() ? std::string() : " (" + gAudioStartError + ")") + "\n";
   {
      std::string outs;
      int n = 0;
      for (const Platform::AudioDeviceInfo& d : Platform::AudioListDevices())
         if (d.isOutput && n++ < 6)
            outs += (outs.empty() ? "" : ", ") + d.name;
      out += "Audio outputs: " + (outs.empty() ? std::string("none found") : outs) + "\n";
   }
   out += "Plugins indexed: " + std::to_string(gPluginScanner.Index().size()) + "\n";
   out += "Nodes in patch: " + std::to_string(gNodes.size()) + "\n";

   const std::vector<std::string> problems = Notices::RecentProblems();
   out += problems.empty() ? "Recent problems: none\n" : "Recent problems:\n";
   for (const std::string& p : problems)
      out += "  " + p + "\n";
   return out;
}

void CopySystemInfo()
{
   ImGui::SetClipboardText(BuildSystemInfo().c_str());
   Notices::Post(Notices::Level::Info, "sysinfo.copied", "System info copied",
                 "Paste it into your bug report. Nothing was sent anywhere.");
}
}

// Help > Copy system info. One plain-text block for a bug report: version, OS, GPU, audio, plugins, recent problems.
// It only reads state the app already holds (plus one device list) and goes to the clipboard; nothing is sent anywhere.
#include "app/AppShared.h"
#include "audio/AudioEngine.h"
#include "audio/PluginScanner.h"
#include "core/Notices.h"
#include "core/gl3.h"
#include "platform/AppPaths.h"
#include "platform/Platform.h"
#include "miniz.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

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

namespace
{
   std::string ReadFileBytes(const std::filesystem::path& p, size_t maxBytes)
   {
      std::ifstream f(p, std::ios::binary);
      if (!f)
         return {};
      f.seekg(0, std::ios::end);
      const std::streamoff size = f.tellg();
      // A long log keeps its newest part.
      const std::streamoff start = size > (std::streamoff)maxBytes ? size - (std::streamoff)maxBytes : 0;
      f.seekg(start);
      std::string data((size_t)(size - start), '\0');
      f.read(&data[0], (std::streamsize)data.size());
      return data;
   }

   // "/Users/name/..." -> "~/...": prefs and logs name folders, and the user name is no one else's business.
   std::string RedactHome(std::string text)
   {
      std::string home = AppPaths::HomeDir();
      while (home.size() > 1 && (home.back() == '/' || home.back() == '\\'))
         home.pop_back();
      if (home.size() < 2)
         return text;
      for (std::string h : { home, [&] { std::string s2 = home; std::replace(s2.begin(), s2.end(), '\\', '/'); return s2; }() })
      {
         for (size_t at = text.find(h); at != std::string::npos; at = text.find(h, at + 1))
            text.replace(at, h.size(), "~");
      }
      return text;
   }

   // Crash reports the OS or our own handler left, newest first, from the last 14 days.
   std::vector<std::filesystem::path> RecentCrashReports()
   {
      namespace fs = std::filesystem;
      std::vector<std::pair<fs::file_time_type, fs::path>> found;
      std::vector<std::pair<fs::path, std::string>> dirs; // folder, file name prefix ("" = any)
      const std::string appDir = AppPaths::AppSupportDir();
      if (!appDir.empty())
         dirs.emplace_back(AppPaths::FsPath(appDir + "/crash"), ""); // Windows minidumps + text (InstallCrashHandler)
#if defined(__APPLE__)
      const std::string home = AppPaths::HomeDir();
      if (!home.empty())
         dirs.emplace_back(AppPaths::FsPath(home + "/Library/Logs/DiagnosticReports"), "Infinite");
#endif
      const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * 14);
      std::error_code ec;
      for (const auto& d : dirs)
      {
         for (fs::directory_iterator it(d.first, ec), end; it != end && !ec; it.increment(ec))
         {
            if (!it->is_regular_file(ec))
               continue;
            if (!d.second.empty() && it->path().filename().u8string().rfind(d.second, 0) != 0)
               continue;
            const auto t = it->last_write_time(ec);
            if (!ec && t >= cutoff)
               found.emplace_back(t, it->path());
         }
         ec.clear();
      }
      std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
      std::vector<fs::path> out;
      for (size_t i = 0; i < found.size() && i < 10; i++)
         out.push_back(found[i].second);
      return out;
   }
}

bool WriteDiagnosticsZip(const std::string& zipPath, const std::string& systemInfo, const std::string& patchPath,
                         std::string& error)
{
   namespace fs = std::filesystem;
   std::vector<std::pair<std::string, std::string>> entries;
   entries.emplace_back("system-info.txt", RedactHome(systemInfo));

   const std::string appDir = AppPaths::AppSupportDir();
   if (!appDir.empty())
   {
      // The rolling log and its rotated copies (log.txt, log.txt.1, ...): the newest 2 MB of each.
      std::error_code ec;
      for (fs::directory_iterator it(AppPaths::FsPath(appDir), ec), end; it != end && !ec; it.increment(ec))
      {
         const std::string name = it->path().filename().u8string();
         if (it->is_regular_file(ec) && name.rfind("log.txt", 0) == 0)
            entries.emplace_back("logs/" + name, RedactHome(ReadFileBytes(it->path(), 2u << 20)));
         else if (it->is_regular_file(ec) && name == "plugin-crashed.txt")
            entries.emplace_back("logs/" + name, RedactHome(ReadFileBytes(it->path(), 1u << 20)));
         // Settings: the small Infinite.* prefs files, paths redacted. Never indexes, tokens or recordings.
         else if (it->is_regular_file(ec) && name.rfind("Infinite.", 0) == 0 && name != "Infinite.recents" &&
                  it->file_size(ec) < (256u << 10))
            entries.emplace_back("prefs/" + name, RedactHome(ReadFileBytes(it->path(), 256u << 10)));
      }
   }
   for (const fs::path& crash : RecentCrashReports())
      entries.emplace_back("crash/" + crash.filename().u8string(), RedactHome(ReadFileBytes(crash, 16u << 20)));
   if (!patchPath.empty())
   {
      const fs::path pp = AppPaths::FsPath(patchPath);
      const std::string data = ReadFileBytes(pp, 32u << 20);
      if (!data.empty())
         entries.emplace_back("patch/" + pp.filename().u8string(), data);
   }

   // Built in memory and written through FsPath, so a non-ASCII Desktop path works on Windows too.
   mz_zip_archive zip {};
   if (!mz_zip_writer_init_heap(&zip, 0, 1u << 20))
   {
      error = "could not start the zip";
      return false;
   }
   bool ok = true;
   for (const auto& e : entries)
      if (!mz_zip_writer_add_mem(&zip, e.first.c_str(), e.second.data(), e.second.size(), MZ_DEFAULT_LEVEL))
      {
         error = "could not add " + e.first;
         ok = false;
         break;
      }
   void* buf = nullptr;
   size_t size = 0;
   if (ok && !mz_zip_writer_finalize_heap_archive(&zip, &buf, &size))
   {
      error = "could not finish the zip";
      ok = false;
   }
   if (ok)
   {
      std::ofstream out(AppPaths::FsPath(zipPath), std::ios::binary | std::ios::trunc);
      out.write((const char*)buf, (std::streamsize)size);
      if (!out)
      {
         error = "could not write " + zipPath;
         ok = false;
      }
   }
   mz_zip_writer_end(&zip);
   return ok;
}

void CollectDiagnostics(bool includePatch)
{
   std::string dir = AppPaths::DesktopDir();
   if (dir.empty())
      dir = AppPaths::HomeDir();
   char stamp[32];
   const time_t now = time(nullptr);
   struct tm tmv {};
#if defined(_WIN32)
   localtime_s(&tmv, &now);
#else
   localtime_r(&now, &tmv);
#endif
   strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M%S", &tmv);
   const std::string zipPath = dir + "/Infinite-diagnostics-" + stamp + ".zip";
   std::string error;
   if (WriteDiagnosticsZip(zipPath, BuildSystemInfo(), includePatch ? gPatchPath : std::string(), error))
      Notices::Post(Notices::Level::Info, "diagnostics.saved", "Diagnostics saved",
                    "Infinite-diagnostics-" + std::string(stamp) + ".zip is on your Desktop. Attach it to your bug report; nothing was sent anywhere.",
                    "Reveal", [zipPath] { Platform::RevealInFileManager(zipPath); });
   else
      Notices::Post(Notices::Level::Error, "diagnostics.failed", "Could not collect diagnostics", error);
}
}


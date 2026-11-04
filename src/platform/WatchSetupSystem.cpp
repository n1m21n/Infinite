// The real WatchSetup::IToolchain: shells out to xcode-select, defaults, xcrun devicectl and the bundled
// install_watch_app.sh. macOS only; elsewhere the checklist reports "needs a Mac". Selftests never construct this.
#include "platform/WatchSetup.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#if defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace WatchSetup
{
   namespace
   {
#if defined(__APPLE__)
      std::string Quote(const std::string& s)
      {
         std::string o = "'";
         for (char c : s)
         {
            if (c == '\'') o += "'\\''";
            else o += c;
         }
         return o + "'";
      }

      std::string Capture(const std::string& cmd)
      {
         std::string out;
         if (FILE* p = popen(cmd.c_str(), "r"))
         {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, p)) > 0)
               out.append(buf, n);
            pclose(p);
         }
         return out;
      }

      std::string FirstLine(const std::string& s) { return s.substr(0, s.find('\n')); }

      bool Exists(const std::string& path)
      {
         struct stat st;
         return stat(path.c_str(), &st) == 0;
      }

      class SystemToolchain : public IToolchain
      {
      public:
         SystemToolchain(std::string src, RunInTerminal run) : mSource(std::move(src)), mRun(std::move(run)) {}

         Probe Scan() override
         {
            Probe p;
            p.projectBundled = Exists(mSource + "/InfiniteRemote.xcodeproj");
            const std::string dev = FirstLine(Capture("xcode-select -p 2>/dev/null"));
            // the command line tools alone cannot build a watch app: it has to be an Xcode.app
            if (dev.find(".app/Contents/Developer") != std::string::npos)
            {
               const std::string v = FirstLine(Capture("xcodebuild -version 2>/dev/null"));
               if (v.rfind("Xcode ", 0) == 0)
               {
                  p.xcodeInstalled = true;
                  p.xcodeVersion = v.substr(6);
               }
            }
            p.teams = ParseTeams(Capture("defaults read com.apple.dt.Xcode IDEProvisioningTeams 2>/dev/null"));
            if (p.xcodeInstalled)
            {
               char tmpl[] = "/tmp/infinite-devices-XXXXXX";
               const int fd = mkstemp(tmpl);
               if (fd >= 0)
               {
                  close(fd);
                  Capture("xcrun devicectl list devices --json-output " + Quote(tmpl) + " >/dev/null 2>&1");
                  std::ifstream in(tmpl);
                  std::stringstream ss;
                  ss << in.rdbuf();
                  p.devices = ParseDevices(ss.str());
                  std::remove(tmpl);
               }
            }
            return p;
         }

         bool LaunchInstall() override
         {
            return mRun && mRun("Watch app", Quote(mSource + "/install_watch_app.sh") + " --source " + Quote(mSource));
         }

         void OpenXcodeAccounts() override { std::system("open -a Xcode >/dev/null 2>&1"); }
         void OpenXcode() override { std::system("open https://apps.apple.com/app/xcode/id497799835 >/dev/null 2>&1"); }
         void OpenDeviceHelp() override
         {
            std::system("open https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device >/dev/null 2>&1");
         }

      private:
         std::string mSource;
         RunInTerminal mRun;
      };
#endif

      class UnsupportedToolchain : public IToolchain
      {
      public:
         Probe Scan() override { Probe p; p.supported = false; return p; }
         bool LaunchInstall() override { return false; }
         void OpenXcodeAccounts() override {}
         void OpenXcode() override {}
         void OpenDeviceHelp() override {}
      };
   }

   std::unique_ptr<IToolchain> MakeSystemToolchain(const std::string& bundledSource, RunInTerminal runInTerminal)
   {
#if defined(__APPLE__)
      return std::unique_ptr<IToolchain>(new SystemToolchain(bundledSource, std::move(runInTerminal)));
#else
      (void)bundledSource;
      (void)runInTerminal;
      return std::unique_ptr<IToolchain>(new UnsupportedToolchain());
#endif
   }
}

#include "core/DawPluginInstall.h"
#include "core/License.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <sys/xattr.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace fs = std::filesystem;

namespace DawPlugin
{
   namespace
   {
      fs::path P(const std::string& utf8) { return fs::u8path(utf8); }
      std::string S(const fs::path& p) { return p.u8string(); }

      std::string Env_(const char* name)
      {
         const char* v = getenv(name);
         return v ? std::string(v) : std::string();
      }

      long Pid()
      {
#if defined(_WIN32)
         return long(_getpid());
#else
         return long(getpid());
#endif
      }

#if defined(__APPLE__)
      // A browser-downloaded app may carry the quarantine flag onto copies; a DAW then refuses them.
      void ClearQuarantine(const fs::path& root)
      {
         std::error_code ec;
         removexattr(root.c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
         for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            removexattr(it->path().c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
      }
#endif

      bool ReadRecord(std::string& version, std::string& engine)
      {
         std::ifstream f(P(RecordPath()), std::ios::binary);
         if (!f)
            return false;
         std::getline(f, version);
         std::getline(f, engine);
         auto trim = [](std::string& s) { while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back(); };
         trim(version);
         trim(engine);
         return !version.empty();
      }

      bool WriteRecord(const Env& env, std::string& error)
      {
         std::error_code ec;
         fs::create_directories(P(License::Dir()), ec);
         const fs::path dst = P(RecordPath());
         fs::path tmp = dst; // dst.string() would be ANSI on Windows
         tmp += ".tmp";
         {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << env.version << "\n" << env.engine << "\n";
            if (!f)
            {
               error = "Could not write " + S(dst);
               return false;
            }
         }
         fs::rename(tmp, dst, ec);
         if (ec)
         {
            fs::remove(dst, ec);
            fs::rename(tmp, dst, ec);
         }
         if (ec)
         {
            error = "Could not write " + S(dst);
            return false;
         }
         return true;
      }

      // Old copies set aside by an earlier install (".<name>.old-<pid>"), left when a DAW held them.
      void RemoveLeftovers(const fs::path& folder)
      {
         std::error_code ec;
         for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
         {
            const std::string n = it->path().filename().u8string();
            if (n.size() > 1 && n[0] == '.' && (n.find(".old-") != std::string::npos || n.find(".new-") != std::string::npos) &&
                n.rfind(".Infinite ", 0) == 0)
            {
               std::error_code rec;
               fs::remove_all(it->path(), rec);
            }
         }
      }

      bool CopyOne(const Bundle& b, std::string& error)
      {
         std::error_code ec;
         const fs::path target = P(b.target);
         const fs::path folder = target.parent_path();
         fs::create_directories(folder, ec);
         RemoveLeftovers(folder);
         const std::string tag = std::to_string(Pid());
         const fs::path fresh = folder / ("." + b.name + ".new-" + tag);
         const fs::path old = folder / ("." + b.name + ".old-" + tag);
         fs::remove_all(fresh, ec);
         fs::copy(P(b.source), fresh, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
         if (ec)
         {
            error = "Could not copy " + b.name + " to " + S(folder) + ": " + ec.message();
            std::error_code rec;
            fs::remove_all(fresh, rec);
            return false;
         }
#if defined(__APPLE__)
         ClearQuarantine(fresh);
#endif
         const bool had = fs::exists(target, ec);
         if (had)
         {
            fs::rename(target, old, ec);
            if (ec)
            {
               error = "Close your DAW to finish the plugin update (" + b.name + " is in use)";
               std::error_code rec;
               fs::remove_all(fresh, rec);
               return false;
            }
         }
         fs::rename(fresh, target, ec);
         if (ec)
         {
            error = "Could not place " + b.name + ": " + ec.message();
            std::error_code rec;
            if (had)
               fs::rename(old, target, rec);
            fs::remove_all(fresh, rec);
            return false;
         }
         if (had)
         {
            std::error_code rec;
            fs::remove_all(old, rec); // a loaded DLL keeps this from finishing; RemoveLeftovers retries
         }
         return true;
      }
   }

   Env CurrentEnv(const std::string& exe, const std::string& version)
   {
      Env e;
      // Through a symlink (Homebrew's `infinite`) the bundle is where the link points.
      std::error_code ec;
      const fs::path real = exe.empty() ? fs::path() : fs::weakly_canonical(P(exe), ec);
      e.exe = (ec || real.empty()) ? exe : S(real);
      e.version = version;
      e.home = Env_("HOME");
#if defined(_WIN32)
      e.home = Env_("USERPROFILE");
      e.localAppData = Env_("LOCALAPPDATA");
#endif
      e.appDir = Env_("APPDIR");
      const std::string appImage = Env_("APPIMAGE");
      e.engine = (!appImage.empty() && !e.appDir.empty()) ? appImage : e.exe;
      return e;
   }

   std::string SourceDir(const Env& env)
   {
      std::error_code ec;
      const fs::path exeDir = P(env.exe).parent_path();
#if defined(__APPLE__)
      return S(exeDir.parent_path() / "Resources" / "Plugins");
#elif defined(_WIN32)
      return S(exeDir / "Plugins");
#else
      if (!env.appDir.empty() && fs::is_directory(P(env.appDir) / "usr/share/infinite/Plugins", ec))
         return S(P(env.appDir) / "usr/share/infinite/Plugins");
      if (fs::is_directory(exeDir / "Plugins", ec))
         return S(exeDir / "Plugins");
      return S(exeDir.parent_path() / "share/infinite/Plugins");
#endif
   }

   std::vector<Bundle> Bundles(const Env& env)
   {
      std::vector<Bundle> out;
      const fs::path src = P(SourceDir(env));
#if defined(__APPLE__)
      const fs::path vst3 = P(env.home) / "Library/Audio/Plug-Ins/VST3";
      const fs::path au = P(env.home) / "Library/Audio/Plug-Ins/Components";
#elif defined(_WIN32)
      const fs::path vst3 = P(env.localAppData) / "Programs" / "Common" / "VST3";
#else
      const fs::path vst3 = P(env.home) / ".vst3";
#endif
      for (const char* base : { "Infinite Instrument", "Infinite FX" })
      {
         const std::string v = std::string(base) + ".vst3";
         out.push_back({ v, S(src / v), S(vst3 / v) });
#if defined(__APPLE__)
         const std::string c = std::string(base) + ".component";
         out.push_back({ c, S(src / c), S(au / c) });
#endif
      }
      return out;
   }

   Status Describe(const Env& env)
   {
      Status st;
      st.bundles = Bundles(env);
      std::error_code ec;
      st.bundled = !st.bundles.empty();
      bool allThere = true;
      for (const Bundle& b : st.bundles)
      {
         if (!fs::exists(P(b.source), ec))
            st.bundled = false;
         if (!fs::exists(P(b.target), ec))
            allThere = false;
         const std::string folder = S(P(b.target).parent_path());
         bool seen = false;
         for (const std::string& f : st.folders)
            seen = seen || f == folder;
         if (!seen)
            st.folders.push_back(folder);
      }
      std::string version, engine;
      if (ReadRecord(version, engine))
      {
         st.installedVersion = version;
         st.installed = allThere;
         st.current = allThere && version == env.version && engine == env.engine;
      }
      return st;
   }

   bool Install(const Env& env, std::string& error)
   {
      const Status st = Describe(env);
      if (!st.bundled)
      {
         error = "This build of Infinite carries no DAW plugin (" + SourceDir(env) + ")";
         return false;
      }
      for (const Bundle& b : st.bundles)
         if (!CopyOne(b, error))
            return false;
      return WriteRecord(env, error);
   }

   bool Uninstall(const Env& env, std::string& error)
   {
      std::error_code ec;
      bool ok = true;
      for (const Bundle& b : Bundles(env))
      {
         fs::remove_all(P(b.target), ec);
         if (ec)
         {
            error = "Could not remove " + b.name + " (close your DAW first)";
            ok = false;
         }
      }
      fs::remove(P(RecordPath()), ec);
      return ok;
   }

   bool RecopyIfStale(const Env& env, std::string& error)
   {
      std::string version, engine;
      if (!ReadRecord(version, engine))
         return true; // never installed: the user chooses in Settings
      const Status st = Describe(env);
      if (st.current || !st.bundled)
         return true;
      return Install(env, error);
   }

   std::string RecordPath()
   {
      return S(P(License::Dir()) / "plugin-install.txt");
   }

   std::string RecordedEngine()
   {
      std::string version, engine;
      ReadRecord(version, engine);
      return engine;
   }
}

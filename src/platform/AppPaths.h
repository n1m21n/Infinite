#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
   #include <direct.h>
   #define APPPATHS_MKDIR(p) _mkdir(p)
   #define APPPATHS_STAT_T struct _stat
   #define APPPATHS_STAT(p, sb) _stat(p, sb)
   #define APPPATHS_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
   #define APPPATHS_MKDIR(p) mkdir(p, 0755)
   #define APPPATHS_STAT_T struct stat
   #define APPPATHS_STAT(p, sb) stat(p, sb)
   #define APPPATHS_ISDIR(m) S_ISDIR(m)
#endif

// Portable replacements for the getenv("HOME")-based paths the macOS build
// used. Everything is header-only inline so callers keep their existing
// structure (small local helpers that append their own filename).
//
// Layout mirrors the macOS one semantically: a single per-user directory the
// app owns. macOS keeps ~/Library/Application Support/Infinite; Windows uses
// %APPDATA%\Infinite (roaming, like Application Support).
#if defined(_WIN32)
namespace AppPaths
{
   // Defined in src/platform/win/PlatformWin.cpp (SHGetKnownFolderPath, which
   // needs windows.h - kept out of this widely-included header). Returns a
   // UTF-8 path, or "" when the shell has no Desktop for this user.
   std::string DesktopDirWin();
}
#endif

namespace AppPaths
{

   // The user's home directory. HOME first (set by shells and CI), then
   // Windows' USERPROFILE. Empty string if neither is set.
   inline std::string HomeDir()
   {
      if (const char* home = std::getenv("HOME"))
         return home;
      if (const char* profile = std::getenv("USERPROFILE"))
         return profile;
      return {};
   }

   // Engine paths are UTF-8 std::string everywhere. std::filesystem::path's
   // string constructor reads them in the ANSI code page on Windows, which
   // mangles any non-ASCII folder (a user called "Müller", a Cyrillic
   // Desktop) - exists() then lies and create_directories() makes the wrong
   // folder. Route every std::filesystem call on an engine path through this.
   inline std::filesystem::path FsPath(const std::string& utf8)
   {
      return std::filesystem::u8path(utf8);
   }

   // The user's Desktop. Windows asks the shell, because the folder is often
   // redirected (OneDrive "Backup Desktop") and then is NOT under
   // USERPROFILE; macOS/Linux use ~/Desktop. Falls back to the home folder
   // when there is no Desktop at all, and to "." with no home either.
   inline std::string DesktopDir()
   {
#if defined(_WIN32)
      const std::string known = DesktopDirWin();
      if (!known.empty())
         return known;
#endif
      const std::string home = HomeDir();
      return home.empty() ? std::string(".") : home + "/Desktop";
   }

   // True when `path` exists and is a directory.
   inline bool DirExists(const std::string& path)
   {
      if (path.empty())
         return false;
      std::error_code ec;
      return std::filesystem::is_directory(FsPath(path), ec);
   }

   // Creates a directory and any missing parent directories (no-op if it already exists).
   // Returns true when the directory exists afterwards.
   inline bool EnsureDir(const std::string& path)
   {
      if (path.empty())
         return false;
      if (DirExists(path))
         return true;
      std::error_code ec;
      std::filesystem::create_directories(FsPath(path), ec);
      return DirExists(path);
   }

   // The app's per-user settings directory, created if missing. Callers that
   // need a specific behavior when creation fails should check with their own
   // filesystem call; historically these paths were used optimistically.
   inline std::string AppSupportDir()
   {
#if defined(_WIN32)
      // %APPDATA% is the roaming profile (CSIDL_APPDATA); fall back to
      // USERPROFILE\AppData\Roaming when the variable is missing, which is
      // effectively never on real installs.
      std::string base;
      if (const char* appdata = std::getenv("APPDATA"))
         base = appdata;
      else if (const char* profile = std::getenv("USERPROFILE"))
         base = std::string(profile) + "\\AppData\\Roaming";
      if (base.empty())
         return {};
      std::string dir = base + "/Infinite";
#elif defined(__APPLE__)
      const std::string home = HomeDir();
      if (home.empty())
         return {};
      std::string dir = home + "/Library/Application Support/Infinite";
#else
      // Linux / XDG Base Directory specification:
      // $XDG_CONFIG_HOME/Infinite or ~/.config/Infinite
      std::string base;
      if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
      {
         if (xdg[0] != '\0')
            base = xdg;
      }
      if (base.empty())
      {
         const std::string home = HomeDir();
         if (home.empty())
            return {};
         base = home + "/.config";
      }
      std::string dir = base + "/Infinite";
#endif
      // An unusable settings directory returns empty rather than a path that
      // silently swallows every write. Every caller already handles the empty
      // case (they all guard with `dir.empty()`), so this turns a silent
      // data-loss mode into the no-settings-available path they already have.
      if (!EnsureDir(dir))
         return {};
      return dir;
   }

   // One-shot migration for state that predates the AppSupportDir() layout.
   //
   // Routing every settings path through AppSupportDir() relocated two files
   // that already existed on every macOS install:
   //     ~/Library/Application Support/Infinite.theme
   //     ~/Library/Application Support/Infinite.recents
   // which moved into the new per-app subdirectory. The new layout is better,
   // so this migrates rather than reverts: if the new path is absent and the
   // old one is present, move it across once. Rename first (atomic, and free
   // within a filesystem), copy+unlink as a fallback for the cross-device
   // case. On Windows there is no old path, so this is a no-op there.
   //
   // Safe to call on every load: it does nothing once the new file exists.
   inline void MigrateLegacyFile(const std::string& oldPath, const std::string& newPath)
   {
      if (oldPath.empty() || newPath.empty())
         return;

      APPPATHS_STAT_T sb {};
      if (APPPATHS_STAT(newPath.c_str(), &sb) == 0)
         return; // already migrated, or the user has newer state - leave it
      if (APPPATHS_STAT(oldPath.c_str(), &sb) != 0)
         return; // nothing to migrate

      if (std::rename(oldPath.c_str(), newPath.c_str()) == 0)
         return;

      std::ifstream in(oldPath, std::ios::binary);
      if (!in)
         return;
      std::ofstream out(newPath, std::ios::binary);
      if (!out)
         return;
      out << in.rdbuf();
      if (out.good())
      {
         out.close();
         in.close();
         std::remove(oldPath.c_str());
      }
   }

   // A writable scratch directory for test fixtures. POSIX builds kept using
   // /tmp directly; Windows maps to %TEMP% (or the working directory as a
   // last resort).
   inline std::string TempDir()
   {
#if defined(_WIN32)
      if (const char* temp = std::getenv("TEMP"))
         return temp;
      if (const char* tmp = std::getenv("TMP"))
         return tmp;
      return ".";
#else
      return "/tmp";
#endif
   }
}

#undef APPPATHS_MKDIR

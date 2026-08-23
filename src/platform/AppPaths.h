#pragma once

#include <cstdlib>
#include <string>

#if defined(_WIN32)
   #include <direct.h>
   #define APPPATHS_MKDIR(p) _mkdir(p)
#else
   #include <sys/stat.h>
   #define APPPATHS_MKDIR(p) mkdir(p, 0755)
#endif

// Portable replacements for the getenv("HOME")-based paths the macOS build
// used. Everything is header-only inline so callers keep their existing
// structure (small local helpers that append their own filename).
//
// Layout mirrors the macOS one semantically: a single per-user directory the
// app owns. macOS keeps ~/Library/Application Support/Infinite; Windows uses
// %APPDATA%\Infinite (roaming, like Application Support).
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

   // Creates a directory (no-op if it already exists). Returns true when the
   // directory exists afterwards.
   inline bool EnsureDir(const std::string& path)
   {
      if (path.empty())
         return false;
      APPPATHS_MKDIR(path.c_str());
      return true;
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
#else
      const std::string home = HomeDir();
      if (home.empty())
         return {};
      std::string dir = home + "/Library/Application Support/Infinite";
#endif
      EnsureDir(dir);
      return dir;
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

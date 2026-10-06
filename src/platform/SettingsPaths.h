#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

// Turbo 0.50: environment paths are read wide on Windows. getenv() returns
// them in the ANSI code page, which u8path() misreads (or throws on) for a
// user name with non-ASCII letters.
inline std::filesystem::path InfiniteEnvPath(const char* name)
{
#ifdef _WIN32
   const std::wstring wide(name, name + std::char_traits<char>::length(name));
   const wchar_t* value = _wgetenv(wide.c_str());
   return value != nullptr && *value != 0 ? std::filesystem::path(value) : std::filesystem::path();
#else
   const char* value = std::getenv(name);
   return value != nullptr && *value != 0 ? std::filesystem::u8path(value) : std::filesystem::path();
#endif
}

inline std::string InfiniteSettingsDirectory()
{
   std::filesystem::path root = InfiniteEnvPath("LOCALAPPDATA");
   if (root.empty())
      root = InfiniteEnvPath("APPDATA");
   if (root.empty())
      return {};
   std::filesystem::path dir = root / "Infinite";
   std::error_code error;
   std::filesystem::create_directories(dir, error);
   return error ? std::string() : dir.u8string();
}

inline std::string InfiniteDesktopDirectory()
{
   const std::filesystem::path profile = InfiniteEnvPath("USERPROFILE");
   if (profile.empty())
      return {};
   return (profile / "Desktop").u8string();
}

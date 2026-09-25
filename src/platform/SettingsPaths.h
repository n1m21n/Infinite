#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

inline std::string InfiniteSettingsDirectory()
{
   const char* root = std::getenv("LOCALAPPDATA");
   if (root == nullptr)
      root = std::getenv("APPDATA");
   if (root == nullptr)
      return {};
   std::filesystem::path dir = std::filesystem::u8path(root) / "Infinite";
   std::error_code error;
   std::filesystem::create_directories(dir, error);
   return error ? std::string() : dir.u8string();
}

inline std::string InfiniteDesktopDirectory()
{
   const char* profile = std::getenv("USERPROFILE");
   if (profile == nullptr)
      return {};
   return (std::filesystem::u8path(profile) / "Desktop").u8string();
}

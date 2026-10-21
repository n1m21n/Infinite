#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "SettingsPaths.h"

// Turbo 0.60: the slice of upstream's AppPaths that ported nodes call
// (Predictive Coloring / Modulator store what they learn under the app's
// support folder). Upstream's own layout lives in its platform layer; here it
// is Turbo's settings folder (%LOCALAPPDATA%\Infinite).
namespace AppPaths
{
   inline bool DirExists(const std::string& path)
   {
      std::error_code ec;
      return !path.empty() && std::filesystem::is_directory(std::filesystem::u8path(path), ec);
   }

   inline bool EnsureDir(const std::string& path)
   {
      if (DirExists(path))
         return true;
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::u8path(path), ec);
      return DirExists(path);
   }

   inline std::string AppSupportDir() { return InfiniteSettingsDirectory(); }
}

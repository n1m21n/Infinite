#pragma once

// Infinite-Turbo 0.46 (upstream R500, Windows part): single-instance "open
// this patch". The first Infinite-Turbo to start holds a per-user named mutex
// for its whole life. A later launch given a .inf writes the path into a spool
// folder (%LOCALAPPDATA%\Infinite\open-requests) and exits; the running one
// polls that folder and opens the file. A request left by an instance that
// died is harmless: the next one just opens it.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef NOMINMAX
   #define NOMINMAX
#endif
#include <windows.h>

namespace SingleInstance
{
   inline std::filesystem::path SpoolDir(const std::string& appDir)
   {
      return std::filesystem::u8path(appDir) / "open-requests";
   }

   // True when this process is the primary instance. Never fails closed.
   inline bool BecomePrimary()
   {
      static bool sPrimary = false;
      if (sPrimary)
         return true;
      HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\InfiniteTurbo.SingleInstance");
      if (mutex == nullptr)
         return sPrimary = true; // never fail closed
      if (GetLastError() == ERROR_ALREADY_EXISTS)
      {
         // Not the owner: let go, so this window cannot keep the name alive
         // after the first one closes. It may ask again later and take over.
         CloseHandle(mutex);
         return false;
      }
      return sPrimary = true; // held (never closed) for the life of the process
   }

   // Hands `path` to the running primary (made absolute: its working folder
   // is not ours).
   inline bool Forward(const std::string& appDir, const std::string& path)
   {
      std::error_code ec;
      std::filesystem::path abs;
      try
      {
         abs = std::filesystem::absolute(std::filesystem::u8path(path), ec);
      }
      catch (...)
      {
         return false;
      }
      if (ec)
         return false;
      const std::filesystem::path dir = SpoolDir(appDir);
      std::filesystem::create_directories(dir, ec);
      if (ec)
         return false;
      const long long stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count();
      char name[64];
      std::snprintf(name, sizeof(name), "%020lld-%lu", stamp, (unsigned long)GetCurrentProcessId());
      // Written under a name the reader ignores, then renamed, so the primary
      // never reads a half-written request.
      const std::filesystem::path tmp = dir / (std::string(name) + ".tmp");
      const std::filesystem::path req = dir / (std::string(name) + ".req");
      {
         std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
         if (!f)
            return false;
         const std::string utf8 = abs.u8string();
         f.write(utf8.data(), (std::streamsize)utf8.size());
         if (!f)
            return false;
      }
      std::filesystem::rename(tmp, req, ec);
      // Let the running instance bring its window to the front.
      AllowSetForegroundWindow(ASFW_ANY);
      return !ec;
   }

   // Oldest pending request, removed as it is returned.
   inline bool Poll(const std::string& appDir, std::string& outPath)
   {
      std::error_code ec;
      const std::filesystem::path dir = SpoolDir(appDir);
      if (!std::filesystem::is_directory(dir, ec))
         return false;
      std::vector<std::filesystem::path> reqs;
      for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
         if (it->path().extension() == ".req")
            reqs.push_back(it->path());
      if (reqs.empty())
         return false;
      std::sort(reqs.begin(), reqs.end());
      for (const auto& req : reqs)
      {
         std::string path;
         {
            std::ifstream f(req, std::ios::binary);
            std::getline(f, path);
         }
         std::filesystem::remove(req, ec);
         if (!path.empty())
         {
            outPath = path;
            return true;
         }
      }
      return false;
   }
}

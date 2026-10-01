#pragma once

// Single-instance "open this patch" for Windows and Linux (macOS gets the same
// from Launch Services: a second `open` is delivered to the running app).
//
// The first Infinite to start becomes the primary by holding a per-user lock for
// its whole life. A later launch given a patch path writes the path into a spool
// directory and exits; the primary polls that directory and loads the file. A
// spool directory rather than a socket or pipe keeps one code path for both
// platforms, and a request left by a primary that died is harmless: the next
// primary just opens it.
//
// Header-only and free of Infinite types so it can be tested on its own.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <windows.h>
#else
   #include <fcntl.h>
   #include <sys/file.h>
   #include <unistd.h>
#endif

namespace SingleInstance
{
   inline std::filesystem::path SpoolDir(const std::string& appDir)
   {
      return std::filesystem::path(appDir) / "open-requests";
   }

   // True when this process is now the primary instance. `appDir` is the
   // per-user app directory; the lock lives in it. Never fails closed: when the
   // lock cannot be made at all (read-only home, no HOME) the caller runs as a
   // primary rather than refusing to start.
   inline bool BecomePrimary(const std::string& appDir)
   {
#if defined(_WIN32)
      // One named mutex per user session; Windows releases it when we exit.
      static HANDLE sMutex = nullptr;
      sMutex = CreateMutexW(nullptr, FALSE, L"Local\\Infinite.SingleInstance");
      if (sMutex == nullptr)
         return true;
      return GetLastError() != ERROR_ALREADY_EXISTS;
#else
      static int sFd = -1;
      std::error_code ec;
      std::filesystem::create_directories(appDir, ec);
      const std::string lockPath = (std::filesystem::path(appDir) / "instance.lock").string();
      sFd = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
      if (sFd < 0)
         return true;
      // flock is released by the kernel when the process dies, so a crash never
      // leaves a stale "already running" behind.
      return ::flock(sFd, LOCK_EX | LOCK_NB) == 0;
#endif
   }

   // Hands `path` to the running primary. The path is made absolute here,
   // because the primary's working directory is not ours.
   inline bool Forward(const std::string& appDir, const std::string& path)
   {
      std::error_code ec;
      const std::filesystem::path abs = std::filesystem::absolute(path, ec);
      if (ec)
         return false;
      const std::filesystem::path dir = SpoolDir(appDir);
      std::filesystem::create_directories(dir, ec);
      if (ec)
         return false;
      const long long stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count();
#if defined(_WIN32)
      const long pid = (long)GetCurrentProcessId();
#else
      const long pid = (long)::getpid();
#endif
      char name[64];
      std::snprintf(name, sizeof(name), "%020lld-%ld", stamp, pid);
      // Written under a name the reader ignores, then renamed, so the primary
      // never reads a half-written request.
      const std::filesystem::path tmp = dir / (std::string(name) + ".tmp");
      const std::filesystem::path req = dir / (std::string(name) + ".req");
      {
         std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
         if (!f)
            return false;
         const std::string utf8 = abs.generic_string();
         f.write(utf8.data(), (std::streamsize)utf8.size());
         if (!f)
            return false;
      }
      std::filesystem::rename(tmp, req, ec);
      return !ec;
   }

   // Oldest pending request, removed as it is returned. Cheap when the spool is
   // absent, but it is a directory scan, so callers throttle it.
   inline bool Poll(const std::string& appDir, std::string& outPath)
   {
      std::error_code ec;
      const std::filesystem::path dir = SpoolDir(appDir);
      if (!std::filesystem::is_directory(dir, ec))
         return false;
      std::vector<std::filesystem::path> reqs;
      for (const auto& e : std::filesystem::directory_iterator(dir, ec))
         if (e.path().extension() == ".req")
            reqs.push_back(e.path());
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

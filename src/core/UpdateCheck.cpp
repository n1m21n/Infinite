#include "UpdateCheck.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "json.hpp"
#include "platform/AppPaths.h"
#include "platform/Platform.h"

#ifndef INFINITE_VERSION_STRING
#define INFINITE_VERSION_STRING "0.0.0"
#endif

using json = nlohmann::json;

namespace UpdateCheck
{
namespace
{
   std::thread gWorker;
   std::atomic<bool> gRunning { false };
   std::atomic<bool> gResultReady { false };
   std::mutex gResultMutex;
   std::string gPendingLatest;      // guarded by gResultMutex; empty = no update found
   bool gHaveResult = false;
   std::string gLatestVersion;      // main-thread copy, only touched by Poll()/Dismiss()
   bool gUpdateAvailable = false;

   // Mirrors CategoryColors::ThemePath()'s comment: one flat preference file
   // next to the app's other Application Support state, not a bundled
   // settings format.
   std::string DismissalPath()
   {
      std::string dir = AppPaths::AppSupportDir();
      return dir.empty() ? std::string() : dir + "/Infinite.update";
   }

   std::string LoadDismissedVersion()
   {
      std::string path = DismissalPath();
      if (path.empty())
         return {};
      FILE* f = fopen(path.c_str(), "rb");
      if (!f)
         return {};
      char buf[64] = {};
      size_t n = fread(buf, 1, sizeof(buf) - 1, f);
      fclose(f);
      std::string s(buf, n);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
         s.pop_back();
      return s;
   }

   void SaveDismissedVersion(const std::string& version)
   {
      std::string path = DismissalPath();
      if (path.empty())
         return;
      if (FILE* f = fopen(path.c_str(), "wb"))
      {
         fwrite(version.data(), 1, version.size(), f);
         fclose(f);
      }
   }

   // Strips a leading 'v' and truncates at the first character that isn't a
   // digit or '.', so "v0.2-preview" -> "0.2". Real tags in this repo carry a
   // "-preview" suffix that strict semver would treat as *older* than the
   // bare version - see the version-comparison section of
   // docs/plans/update-checker-prompt.md for why that's the wrong answer
   // here.
   std::vector<int> ParseVersion(const std::string& raw)
   {
      std::string s = raw;
      if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
         s.erase(0, 1);

      size_t cut = 0;
      while (cut < s.size() && (isdigit((unsigned char)s[cut]) || s[cut] == '.'))
         ++cut;
      s = s.substr(0, cut);

      std::vector<int> parts;
      size_t start = 0;
      while (start <= s.size())
      {
         size_t dot = s.find('.', start);
         std::string component = (dot == std::string::npos) ? s.substr(start) : s.substr(start, dot - start);
         if (!component.empty())
            parts.push_back(atoi(component.c_str()));
         if (dot == std::string::npos)
            break;
         start = dot + 1;
      }
      return parts;
   }

   // Component-wise compare, treating a missing component as 0. Returns true
   // when `remote` is strictly newer than `local`.
   bool IsNewer(const std::vector<int>& local, const std::vector<int>& remote)
   {
      size_t n = std::max(local.size(), remote.size());
      for (size_t i = 0; i < n; ++i)
      {
         int l = i < local.size() ? local[i] : 0;
         int r = i < remote.size() ? remote[i] : 0;
         if (r != l)
            return r > l;
      }
      return false;
   }

   void WorkerMain()
   {
      std::string body, error;
      std::string userAgent = std::string("Infinite/") + INFINITE_VERSION_STRING;
      bool ok = Platform::HttpGet("https://api.github.com/repos/n1m21n/Infinite/releases/latest",
                                   userAgent, body, error, 10);

      std::string latestTag;
      if (ok)
      {
         // A rate-limit or error response is still valid JSON, just a
         // different shape - treat any parse/lookup failure as "no result"
         // rather than letting it propagate as a crash.
         try
         {
            json j = json::parse(body);
            if (j.contains("tag_name") && j["tag_name"].is_string())
               latestTag = j["tag_name"].get<std::string>();
         }
         catch (...)
         {
            latestTag.clear();
         }
      }

      if (!latestTag.empty())
      {
         std::vector<int> local = ParseVersion(INFINITE_VERSION_STRING);
         std::vector<int> remote = ParseVersion(latestTag);
         if (!IsNewer(local, remote))
            latestTag.clear();
      }

      if (!latestTag.empty())
      {
         std::string dismissed = LoadDismissedVersion();
         if (!dismissed.empty() && !IsNewer(ParseVersion(dismissed), ParseVersion(latestTag)))
            latestTag.clear(); // already dismissed this version (or newer)
      }

      {
         std::lock_guard<std::mutex> lock(gResultMutex);
         gPendingLatest = latestTag;
      }
      gResultReady.store(true, std::memory_order_release);
      gRunning.store(false, std::memory_order_relaxed);
   }
}

void Start()
{
   if (getenv("INFINITE_NO_UPDATE_CHECK") != nullptr)
      return;
   if (getenv("IMAGERESYNTH_SELFTEST") != nullptr)
      return;
   if (gRunning.exchange(true, std::memory_order_acq_rel))
      return; // already in flight

   if (gWorker.joinable())
      gWorker.join(); // previous run finished; reap before starting a new one

   gWorker = std::thread(WorkerMain);
}

void Poll()
{
   if (!gResultReady.load(std::memory_order_acquire))
      return;

   std::unique_lock<std::mutex> lock(gResultMutex, std::try_to_lock);
   if (!lock.owns_lock())
      return; // worker mid-write; try again next frame

   gLatestVersion = gPendingLatest;
   gUpdateAvailable = !gLatestVersion.empty();
   gHaveResult = true;
   gResultReady.store(false, std::memory_order_relaxed);
}

bool UpdateAvailable()
{
   return gUpdateAvailable;
}

const std::string& LatestVersion()
{
   return gLatestVersion;
}

void Dismiss()
{
   if (!gHaveResult || gLatestVersion.empty())
      return;
   SaveDismissedVersion(gLatestVersion);
   gUpdateAvailable = false;
}

void Shutdown()
{
   if (gWorker.joinable())
      gWorker.join();
}

}

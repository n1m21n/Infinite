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

   // What one WorkerMain run produces, guarded by gResultMutex until Poll()
   // copies it out.
   struct PendingResult
   {
      bool ok = false;              // fetch + parse succeeded
      std::string error;            // set when !ok
      std::string latestTag;        // raw tag_name from GitHub; set when ok
      bool isNewer = false;         // latestTag is newer than the running version
      std::string downloadUrl;      // resolved platform asset URL; set when isNewer
      std::string badgeLatest;      // latestTag if isNewer and not dismissed, else empty
   };

   std::mutex gResultMutex;
   PendingResult gPending;          // guarded by gResultMutex
   bool gHaveResult = false;
   std::string gLatestVersion;      // main-thread copy, only touched by Poll()/Dismiss()
   bool gUpdateAvailable = false;

   // Modal-facing state - main thread only, touched by Poll()/Start().
   Status gModalStatus = Status::Idle;
   std::string gModalVersion;
   std::string gModalError;
   std::string gModalDownloadUrl;

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

   // Picks the release asset name for the platform/arch this binary was
   // built for. Empty when this platform has no packaged asset (falls back
   // to the release page).
   std::string ExpectedAssetName()
   {
#if defined(__APPLE__)
      return "Infinite.dmg";
#elif defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
      return "Infinite-windows-ARM64.zip";
#else
      return "Infinite-windows-x64.zip";
#endif
#else
      return {};
#endif
   }

   // assets[].browser_download_url for the platform asset, else the
   // release's html_url, else the website's download section - in that
   // order, per docs/plans/update-checker-prompt.md.
   std::string ResolveDownloadUrl(const json& release)
   {
      std::string expected = ExpectedAssetName();
      if (!expected.empty() && release.contains("assets") && release["assets"].is_array())
      {
         for (const json& asset : release["assets"])
         {
            if (!asset.contains("name") || !asset["name"].is_string())
               continue;
            if (asset["name"].get<std::string>() != expected)
               continue;
            if (asset.contains("browser_download_url") && asset["browser_download_url"].is_string())
               return asset["browser_download_url"].get<std::string>();
         }
      }
      if (release.contains("html_url") && release["html_url"].is_string())
         return release["html_url"].get<std::string>();
      return "https://n1m21n.github.io/Infinite/#download";
   }

   void WorkerMain()
   {
      std::string body, error;
      std::string userAgent = std::string("Infinite/") + INFINITE_VERSION_STRING;
      bool ok = Platform::HttpGet("https://api.github.com/repos/n1m21n/Infinite/releases/latest",
                                   userAgent, body, error, 10);

      PendingResult result;
      json release;
      if (!ok)
      {
         result.error = error.empty() ? "network request failed" : error;
      }
      else
      {
         // A rate-limit or error response is still valid JSON, just a
         // different shape - treat any parse/lookup failure as a reportable
         // Failed result rather than letting it propagate as a crash or
         // silently look like "up to date".
         try
         {
            release = json::parse(body);
            if (release.contains("tag_name") && release["tag_name"].is_string())
            {
               result.ok = true;
               result.latestTag = release["tag_name"].get<std::string>();
            }
            else if (release.contains("message") && release["message"].is_string())
            {
               result.error = release["message"].get<std::string>();
            }
            else
            {
               result.error = "unexpected response from GitHub";
            }
         }
         catch (...)
         {
            result.error = "invalid response from GitHub";
         }
      }

      if (result.ok)
      {
         std::vector<int> local = ParseVersion(INFINITE_VERSION_STRING);
         std::vector<int> remote = ParseVersion(result.latestTag);
         result.isNewer = IsNewer(local, remote);

         if (result.isNewer)
         {
            result.downloadUrl = ResolveDownloadUrl(release);

            std::string dismissed = LoadDismissedVersion();
            if (dismissed.empty() || IsNewer(ParseVersion(dismissed), remote))
               result.badgeLatest = result.latestTag; // not dismissed (or a newer one arrived since)
         }
      }

      {
         std::lock_guard<std::mutex> lock(gResultMutex);
         gPending = result;
      }
      gResultReady.store(true, std::memory_order_release);
      gRunning.store(false, std::memory_order_relaxed);
   }
}

void Start()
{
   if (getenv("INFINITE_NO_UPDATE_CHECK") != nullptr || getenv("IMAGERESYNTH_SELFTEST") != nullptr)
   {
      // Resolve synchronously rather than leaving GetStatus() at Idle - a
      // modal driven off it must not spin on "Checking..." forever, and the
      // self-test harness must never make a network call.
      gModalStatus = Status::Failed;
      gModalError = "Update checks are disabled in this build.";
      return;
   }
   if (gRunning.exchange(true, std::memory_order_acq_rel))
      return; // already in flight; GetStatus() reports Checking via gRunning

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

   PendingResult result = gPending;
   lock.unlock();
   gResultReady.store(false, std::memory_order_relaxed);

   // Badge - unchanged behaviour, always dismissal-filtered.
   gLatestVersion = result.badgeLatest;
   gUpdateAvailable = !gLatestVersion.empty();
   gHaveResult = true;

   // Modal - always the true state, dismissal-independent.
   if (!result.ok)
   {
      gModalStatus = Status::Failed;
      gModalError = result.error.empty() ? "update check failed" : result.error;
   }
   else if (result.isNewer)
   {
      gModalStatus = Status::UpdateAvailable;
      gModalVersion = result.latestTag;
      gModalDownloadUrl = result.downloadUrl;
   }
   else
   {
      gModalStatus = Status::UpToDate;
      gModalVersion = result.latestTag;
   }
}

Status GetStatus()
{
   if (gRunning.load(std::memory_order_relaxed))
      return Status::Checking;
   return gModalStatus;
}

const std::string& ResultVersion()
{
   return gModalVersion;
}

const std::string& LastError()
{
   return gModalError;
}

const std::string& DownloadUrl()
{
   return gModalDownloadUrl;
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

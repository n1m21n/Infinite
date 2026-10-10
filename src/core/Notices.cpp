#include "core/Notices.h"
#include <algorithm>
#include <deque>
#include "platform/Platform.h"

namespace Notices
{
   namespace
   {
      std::vector<Notice> gActive;
      std::deque<std::string> gRecent;
      uint64_t gNextId = 1;
      double gNow = 0.0;
      constexpr size_t kMaxActive = 4;
      constexpr size_t kMaxRecent = 8;
   }

   void Post(Level level, const std::string& key, const std::string& title, const std::string& detail,
             const std::string& actionLabel, std::function<void()> action)
   {
      Notice n;
      n.level = level;
      n.key = key;
      n.title = title;
      n.detail = detail;
      n.actionLabel = actionLabel;
      n.action = std::move(action);
      n.bornSeconds = gNow;

      auto same = std::find_if(gActive.begin(), gActive.end(), [&](const Notice& o) { return !key.empty() && o.key == key; });
      if (same != gActive.end())
      {
         // Repeats of the same state keep the card where it is and do not restart its timer or the log.
         const bool changed = same->title != title || same->detail != detail;
         n.id = same->id;
         n.bornSeconds = changed ? gNow : same->bornSeconds;
         *same = std::move(n);
         if (!changed)
            return;
      }
      else
      {
         n.id = gNextId++;
         gActive.push_back(std::move(n));
         if (gActive.size() > kMaxActive)
            gActive.erase(gActive.begin());
      }

      if (level != Level::Info)
      {
         const std::string line = std::string(level == Level::Error ? "[error] " : "[warning] ") + title +
                                  (detail.empty() ? "" : " - " + detail);
         gRecent.push_back(line);
         if (gRecent.size() > kMaxRecent)
            gRecent.pop_front();
         Platform::AppendLogLine(line);
      }
   }

   void Dismiss(const std::string& key)
   {
      gActive.erase(std::remove_if(gActive.begin(), gActive.end(), [&](const Notice& o) { return o.key == key; }), gActive.end());
   }

   void DismissId(uint64_t id)
   {
      gActive.erase(std::remove_if(gActive.begin(), gActive.end(), [&](const Notice& o) { return o.id == id; }), gActive.end());
   }

   void Tick(double nowSeconds)
   {
      gNow = nowSeconds;
      gActive.erase(std::remove_if(gActive.begin(), gActive.end(),
                                   [&](const Notice& o) {
                                      if (o.level == Level::Error)
                                         return false;
                                      return nowSeconds - o.bornSeconds > (o.level == Level::Info ? 5.0 : 12.0);
                                   }),
                    gActive.end());
   }

   const std::vector<Notice>& Active() { return gActive; }

   std::vector<std::string> RecentProblems() { return std::vector<std::string>(gRecent.begin(), gRecent.end()); }
}

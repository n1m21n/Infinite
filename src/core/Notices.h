#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The one app-wide notice queue: what a toast, a recovery prompt or a "something did not work" line is made of.
// Wording rule (Block F): say what happened, what is safe, what to do next - in that order, calm, no jargon.
// Posting never draws; app/panels/NoticeStack.cpp draws whatever is active. Same `key` replaces, so a state
// that repeats every frame (device lost) shows once. Errors and warnings are also logged (Platform::AppendLogLine).
namespace Notices
{
   enum class Level { Info, Warning, Error };

   struct Notice
   {
      uint64_t id = 0;
      Level level = Level::Info;
      std::string key;
      std::string title;       // what happened
      std::string detail;      // what is safe, what to do next
      std::string actionLabel; // optional single action
      std::function<void()> action;
      double bornSeconds = 0.0;
   };

   void Post(Level level, const std::string& key, const std::string& title, const std::string& detail = "",
             const std::string& actionLabel = "", std::function<void()> action = {});
   void Dismiss(const std::string& key);
   void DismissId(uint64_t id);

   // Expire what has run its time: Info 5 s, Warning 12 s; Errors stay until dismissed.
   void Tick(double nowSeconds);
   const std::vector<Notice>& Active();

   // The last few Error/Warning titles with their details, oldest first, for "Copy system info".
   std::vector<std::string> RecentProblems();
}

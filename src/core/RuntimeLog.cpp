#include "RuntimeLog.h"

#include "platform/SettingsPaths.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace RuntimeLog
{
namespace
{
   std::atomic<bool> sEnabled{true};
   std::mutex sMutex;

   void Timestamp(char* out, size_t size)
   {
      const std::time_t now = std::time(nullptr);
      std::tm local{};
#if defined(_WIN32)
      localtime_s(&local, &now);
#else
      localtime_r(&now, &local);
#endif
      std::strftime(out, size, "%Y-%m-%d %H:%M:%S", &local);
   }
}

std::string Path()
{
   const std::string dir = InfiniteSettingsDirectory();
   return dir.empty() ? std::string("Infinite.log") : dir + "/Infinite.log";
}

void Initialize(bool enabled)
{
   sEnabled.store(enabled, std::memory_order_release);
   if (enabled) Write("session started");
}

void SetEnabled(bool enabled)
{
   const bool previous = sEnabled.exchange(enabled, std::memory_order_acq_rel);
   if (enabled && !previous) Write("diagnostic logging enabled");
}

bool Enabled()
{
   return sEnabled.load(std::memory_order_acquire);
}

void Write(const char* format, ...)
{
   if (!Enabled() || format == nullptr) return;
   char message[2048];
   va_list args;
   va_start(args, format);
   std::vsnprintf(message, sizeof(message), format, args);
   va_end(args);

   char timestamp[32];
   Timestamp(timestamp, sizeof(timestamp));
   std::lock_guard<std::mutex> lock(sMutex);
   if (FILE* file = std::fopen(Path().c_str(), "ab"))
   {
      std::fprintf(file, "[%s] %s\n", timestamp, message);
      std::fclose(file);
   }
}

void Clear()
{
   std::lock_guard<std::mutex> lock(sMutex);
   if (FILE* file = std::fopen(Path().c_str(), "wb"))
      std::fclose(file);
}
}

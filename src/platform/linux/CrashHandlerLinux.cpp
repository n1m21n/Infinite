// Linux crash and fatal diagnostics:
// Installs signal handlers for SEGV/BUS/ILL/FPE/ABRT with an alternate signal stack,
// writes a backtrace to AppSupportDir()/crash/crash-<timestamp>.txt and stderr,
// then restores SIG_DFL and re-raises.

#include "platform/Platform.h"
#include "platform/AppPaths.h"
#include "platform/PluginCrashGuard.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
   char gCrashDir[512] = "";
   constexpr size_t kAltStackSize = 64 * 1024;
   uint8_t gAltStack[kAltStackSize];

   void SafeWrite(int fd, const char* str)
   {
      if (str && fd >= 0)
      {
         size_t len = strlen(str);
         while (len > 0)
         {
            ssize_t w = write(fd, str, len);
            if (w <= 0) break;
            str += w;
            len -= w;
         }
      }
   }

   void SafeWriteHex(int fd, uintptr_t val)
   {
      char buf[32];
      buf[0] = '0';
      buf[1] = 'x';
      int pos = 2;
      for (int shift = (int)(sizeof(uintptr_t) * 8 - 4); shift >= 0; shift -= 4)
      {
         int nibble = (int)((val >> shift) & 0xF);
         buf[pos++] = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
      }
      buf[pos++] = '\n';
      buf[pos] = '\0';
      SafeWrite(fd, buf);
   }

   void CrashSignalHandler(int sig, siginfo_t* info, void* /*ucontext*/)
   {
      PluginCrashGuard::NoteCrash(); // first: names the plugin this thread was inside, if any
      if (Platform::gCrashHook != nullptr)
         Platform::gCrashHook(sig);
      int fd = -1;
      if (gCrashDir[0] != '\0')
      {
         char path[600];
         time_t now = time(nullptr);
         struct tm tmBuf;
         localtime_r(&now, &tmBuf);
         char stamp[32];
         strftime(stamp, sizeof(stamp), "crash-%Y%m%d-%H%M%S.txt", &tmBuf);
         snprintf(path, sizeof(path), "%s/%s", gCrashDir, stamp);
         fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      }

      auto writeBoth = [&](const char* msg) {
         SafeWrite(STDERR_FILENO, msg);
         if (fd >= 0) SafeWrite(fd, msg);
      };

      writeBoth("\n=== Infinite Fatal Crash Signal ===\n");
      const char* sigName = "UNKNOWN";
      if (sig == SIGSEGV) sigName = "SIGSEGV (Segmentation fault)";
      else if (sig == SIGBUS) sigName = "SIGBUS (Bus error)";
      else if (sig == SIGILL) sigName = "SIGILL (Illegal instruction)";
      else if (sig == SIGFPE) sigName = "SIGFPE (Floating point exception)";
      else if (sig == SIGABRT) sigName = "SIGABRT (Abort)";

      writeBoth("Signal: ");
      writeBoth(sigName);
      writeBoth("\nFault Address: ");
      if (info)
      {
         SafeWriteHex(STDERR_FILENO, (uintptr_t)info->si_addr);
         if (fd >= 0) SafeWriteHex(fd, (uintptr_t)info->si_addr);
      }
      else
      {
         writeBoth("unknown\n");
      }

      writeBoth("Backtrace:\n");
      void* callstack[64];
      int frames = backtrace(callstack, 64);
      backtrace_symbols_fd(callstack, frames, STDERR_FILENO);
      if (fd >= 0)
      {
         backtrace_symbols_fd(callstack, frames, fd);
         close(fd);
      }

      // Re-raise with default handler so standard exit status / core dump occurs
      struct sigaction sa{};
      sa.sa_handler = SIG_DFL;
      sigaction(sig, &sa, nullptr);
      raise(sig);
   }
}

namespace Platform
{
   void InstallCrashHandler()
   {
      std::string dir = AppPaths::AppSupportDir();
      if (!dir.empty())
      {
         std::string crashDir = dir + "/crash";
         AppPaths::EnsureDir(crashDir);
         strncpy(gCrashDir, crashDir.c_str(), sizeof(gCrashDir) - 1);
         gCrashDir[sizeof(gCrashDir) - 1] = '\0';
      }

      stack_t ss{};
      ss.ss_sp = gAltStack;
      ss.ss_size = sizeof(gAltStack);
      ss.ss_flags = 0;
      sigaltstack(&ss, nullptr);

      struct sigaction sa{};
      sa.sa_sigaction = CrashSignalHandler;
      sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
      sigemptyset(&sa.sa_mask);

      sigaction(SIGSEGV, &sa, nullptr);
      sigaction(SIGBUS, &sa, nullptr);
      sigaction(SIGILL, &sa, nullptr);
      sigaction(SIGFPE, &sa, nullptr);
      sigaction(SIGABRT, &sa, nullptr);
   }
}

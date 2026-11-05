#include "platform/common/ProcessTree.h"

#include <cerrno>
#include <cstdlib>
#include <string>
#if defined(_WIN32)
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <windows.h>
   #include <tlhelp32.h>
#elif defined(__APPLE__)
   #include <signal.h>
   #include <sys/sysctl.h>
   #include <unistd.h>
#else
   #include <fstream>
   #include <signal.h>
   #include <unistd.h>
#endif

namespace ProcessTree
{
   long Self()
   {
#if defined(_WIN32)
      return (long)GetCurrentProcessId();
#else
      return (long)::getpid();
#endif
   }

   long ParentPid(long pid)
   {
#if defined(_WIN32)
      HANDLE all = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (all == INVALID_HANDLE_VALUE)
         return 0;
      PROCESSENTRY32W entry{};
      entry.dwSize = sizeof entry;
      long parent = 0;
      for (BOOL more = Process32FirstW(all, &entry); more; more = Process32NextW(all, &entry))
         if ((long)entry.th32ProcessID == pid)
         {
            parent = (long)entry.th32ParentProcessID;
            break;
         }
      CloseHandle(all);
      return parent;
#elif defined(__APPLE__)
      int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
      struct kinfo_proc info{};
      size_t size = sizeof info;
      if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0)
         return 0;
      return (long)info.kp_eproc.e_ppid;
#else
      // /proc/<pid>/stat: "pid (comm) state ppid ..."; comm may hold spaces and parentheses, so read after the last ')'.
      std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
      std::string line;
      if (!std::getline(in, line))
         return 0;
      const size_t close = line.rfind(')');
      if (close == std::string::npos || close + 4 > line.size())
         return 0;
      return std::strtol(line.c_str() + close + 4, nullptr, 10);
#endif
   }

   bool Alive(long pid)
   {
      if (pid <= 0)
         return false;
#if defined(_WIN32)
      HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
      if (h == nullptr)
         return false;
      DWORD code = 0;
      const bool running = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
      CloseHandle(h);
      return running;
#else
      return ::kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
   }
}

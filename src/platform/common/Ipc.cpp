#include "platform/common/Ipc.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <windows.h>
#else
   #include <cerrno>
   #include <dlfcn.h>
   #include <fcntl.h>
   #include <pthread.h>
   #include <signal.h>
   #include <spawn.h>
   #include <sys/mman.h>
   #include <sys/wait.h>
   #include <thread>
   #include <time.h>
   #include <unistd.h>
extern char** environ;
#endif

#if defined(__APPLE__)
   #include <mach/mach.h>
   #include <mach/mach_time.h>
   #include <mach/thread_policy.h>
   #include <sys/event.h>
   #if __has_include(<os/os_sync_wait_on_address.h>)
      #include <os/os_sync_wait_on_address.h>
      #define IPC_HAS_OS_SYNC 1
   #endif
#elif defined(__linux__)
   #include <linux/futex.h>
   #include <sched.h>
   #include <poll.h>
   #include <sys/syscall.h>
   #ifndef SYS_pidfd_open
      #define SYS_pidfd_open 434
   #endif
#endif

namespace Ipc
{
   namespace
   {
      std::atomic<uint32_t> gNameCounter{0};

#if defined(_WIN32)
      std::wstring Widen(const std::string& s)
      {
         if (s.empty()) return {};
         const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
         std::wstring w((size_t)n, L'\0');
         MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
         return w;
      }

      std::string Narrow(const std::wstring& w)
      {
         if (w.empty()) return {};
         const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
         std::string s((size_t)n, '\0');
         WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
         return s;
      }

      // One argv entry, quoted the way CommandLineToArgvW reads it back.
      void AppendQuoted(std::wstring& cmd, const std::wstring& arg)
      {
         if (!cmd.empty()) cmd += L' ';
         if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
         {
            cmd += arg;
            return;
         }
         cmd += L'"';
         size_t slashes = 0;
         for (wchar_t ch : arg)
         {
            if (ch == L'\\') { ++slashes; continue; }
            if (ch == L'"') cmd.append(slashes * 2 + 1, L'\\');
            else cmd.append(slashes, L'\\');
            slashes = 0;
            cmd += ch;
         }
         cmd.append(slashes * 2, L'\\');
         cmd += L'"';
      }
#endif
   }

   int64_t SelfPid()
   {
#if defined(_WIN32)
      return (int64_t)GetCurrentProcessId();
#else
      return (int64_t)getpid();
#endif
   }

   uint64_t MonotonicNs()
   {
#if defined(_WIN32)
      static const LONGLONG freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
      LARGE_INTEGER c;
      QueryPerformanceCounter(&c);
      return (uint64_t)((double)c.QuadPart * 1e9 / (double)freq);
#elif defined(__APPLE__)
      return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
      timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
   }

   // ---- shared memory ------------------------------------------------------

   std::string SharedMemoryUniqueName(const char* prefix)
   {
      char buf[64];
#if defined(_WIN32)
      snprintf(buf, sizeof buf, "Local\\%s.%lld.%u", prefix, (long long)SelfPid(), gNameCounter.fetch_add(1));
#else
      snprintf(buf, sizeof buf, "/%s.%lld.%u", prefix, (long long)SelfPid(), gNameCounter.fetch_add(1));
#endif
      return buf;
   }

   static bool MapRegion(const std::string& name, size_t size, bool create, SharedMemory* out)
   {
      *out = SharedMemory{};
#if defined(_WIN32)
      const std::wstring wname = Widen(name);
      HANDLE h = nullptr;
      if (create)
      {
         h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)((uint64_t)size >> 32),
                                (DWORD)(size & 0xffffffffu), wname.c_str());
         if (h && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return false; }
      }
      else
      {
         h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
      }
      if (!h) return false;
      void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
      if (!p) { CloseHandle(h); return false; }
      out->handle = h;
      out->data = p;
#else
      const int fd = create ? shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600) : shm_open(name.c_str(), O_RDWR, 0);
      if (fd < 0) return false;
      if (create && ftruncate(fd, (off_t)size) != 0)
      {
         close(fd);
         shm_unlink(name.c_str());
         return false;
      }
      void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      close(fd);
      if (p == MAP_FAILED)
      {
         if (create) shm_unlink(name.c_str());
         return false;
      }
      out->data = p;
#endif
      out->size = size;
      out->name = name;
      out->owner = create;
      return true;
   }

   bool SharedMemoryCreate(const std::string& name, size_t size, SharedMemory* out)
   {
      return MapRegion(name, size, true, out); // both sides hand back zero-filled pages
   }

   bool SharedMemoryOpen(const std::string& name, size_t size, SharedMemory* out)
   {
      return MapRegion(name, size, false, out);
   }

   void SharedMemoryClose(SharedMemory* shm)
   {
      if (!shm || !shm->data) return;
#if defined(_WIN32)
      UnmapViewOfFile(shm->data);
      if (shm->handle) CloseHandle((HANDLE)shm->handle);
#else
      munmap(shm->data, shm->size);
      if (shm->owner) shm_unlink(shm->name.c_str());
#endif
      *shm = SharedMemory{};
   }

   // ---- wake word ----------------------------------------------------------

   bool SharedWait(std::atomic<uint32_t>* word, uint32_t expected, uint64_t timeoutNs)
   {
      const uint64_t start = MonotonicNs();
      while (word->load(std::memory_order_acquire) == expected)
      {
         uint64_t left = 0;
         if (timeoutNs)
         {
            const uint64_t el = MonotonicNs() - start;
            if (el >= timeoutNs) return false;
            left = timeoutNs - el;
         }
#if defined(__APPLE__) && defined(IPC_HAS_OS_SYNC)
         if (__builtin_available(macOS 14.4, *))
         {
            if (timeoutNs)
               os_sync_wait_on_address_with_timeout(word, expected, sizeof(uint32_t), OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                                    OS_CLOCK_MACH_ABSOLUTE_TIME, left);
            else
               os_sync_wait_on_address(word, expected, sizeof(uint32_t), OS_SYNC_WAIT_ON_ADDRESS_SHARED);
            continue;
         }
#elif defined(__linux__)
         {
            timespec ts{(time_t)(left / 1000000000ull), (long)(left % 1000000000ull)};
            syscall(SYS_futex, reinterpret_cast<uint32_t*>(word), FUTEX_WAIT, expected, timeoutNs ? &ts : nullptr,
                    nullptr, 0);
            continue;
         }
#endif
         // macOS before 14.4, and Windows until step 3.1: a short poll.
#if defined(_WIN32)
         (void)left;
         SwitchToThread();
#else
         timespec ts{0, 50000};
         nanosleep(&ts, nullptr);
#endif
      }
      return true;
   }

   void SharedWake(std::atomic<uint32_t>* word)
   {
#if defined(__APPLE__) && defined(IPC_HAS_OS_SYNC)
      if (__builtin_available(macOS 14.4, *))
         os_sync_wake_by_address_any(word, sizeof(uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
#elif defined(__linux__)
      syscall(SYS_futex, reinterpret_cast<uint32_t*>(word), FUTEX_WAKE, 1, nullptr, nullptr, 0);
#else
      (void)word;
#endif
   }

   // ---- child processes ----------------------------------------------------

   bool Spawn(const std::string& path, const std::vector<std::string>& args, Child* out)
   {
      *out = Child{};
      if (path.empty()) return false;
#if defined(_WIN32)
      std::wstring cmd;
      AppendQuoted(cmd, Widen(path));
      for (const auto& a : args) AppendQuoted(cmd, Widen(a));
      STARTUPINFOW si{};
      si.cb = sizeof si;
      PROCESS_INFORMATION pi{};
      if (!CreateProcessW(Widen(path).c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                          nullptr, &si, &pi))
         return false;
      CloseHandle(pi.hThread);
      out->pid = (int64_t)pi.dwProcessId;
      out->handle = pi.hProcess;
      return true;
#else
      std::vector<char*> argv;
      argv.push_back(const_cast<char*>(path.c_str()));
      for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
      argv.push_back(nullptr);
      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
   #if defined(__APPLE__)
      // Do not leak the host's open files (project, audio devices) into the helper.
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
   #endif
      pid_t pid = -1;
      const int rc = posix_spawn(&pid, path.c_str(), nullptr, &attr, argv.data(), environ);
      posix_spawnattr_destroy(&attr);
      if (rc != 0) return false;
      out->pid = (int64_t)pid;
      return true;
#endif
   }

   bool ChildAlive(const Child& c)
   {
      if (c.pid <= 0) return false;
#if defined(_WIN32)
      return c.handle && WaitForSingleObject((HANDLE)c.handle, 0) == WAIT_TIMEOUT;
#else
      // A zombie still answers kill(0); waitpid in ChildWait / ChildKill reaps it.
      return kill((pid_t)c.pid, 0) == 0;
#endif
   }

   bool ChildWait(Child* c, int timeoutMs)
   {
      if (!c || c->pid <= 0) return true;
#if defined(_WIN32)
      if (WaitForSingleObject((HANDLE)c->handle, (DWORD)(timeoutMs < 0 ? 0 : timeoutMs)) != WAIT_OBJECT_0)
         return false;
      CloseHandle((HANDLE)c->handle);
      *c = Child{};
      return true;
#else
      const uint64_t end = MonotonicNs() + (uint64_t)(timeoutMs < 0 ? 0 : timeoutMs) * 1000000ull;
      for (;;)
      {
         int status = 0;
         const pid_t r = waitpid((pid_t)c->pid, &status, WNOHANG);
         if (r == (pid_t)c->pid || (r < 0 && errno == ECHILD))
         {
            *c = Child{};
            return true;
         }
         if (MonotonicNs() >= end) return false;
         usleep(2000);
      }
#endif
   }

   void ChildKill(Child* c)
   {
      if (!c || c->pid <= 0) return;
#if defined(_WIN32)
      TerminateProcess((HANDLE)c->handle, 1);
      WaitForSingleObject((HANDLE)c->handle, INFINITE);
      CloseHandle((HANDLE)c->handle);
#else
      kill((pid_t)c->pid, SIGKILL);
      int status = 0;
      waitpid((pid_t)c->pid, &status, 0);
#endif
      *c = Child{};
   }

   std::string HelperPathBesideModule(const char* helperName)
   {
#if defined(_WIN32)
      HMODULE mod = nullptr;
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&HelperPathBesideModule), &mod))
         return {};
      wchar_t buf[MAX_PATH * 4];
      const DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof buf / sizeof buf[0]));
      if (n == 0) return {};
      std::wstring p(buf, n);
      const size_t cut = p.find_last_of(L"\\/");
      if (cut == std::wstring::npos) return {};
      return Narrow(p.substr(0, cut + 1)) + helperName + ".exe";
#else
      Dl_info info{};
      if (!dladdr(reinterpret_cast<const void*>(&HelperPathBesideModule), &info) || !info.dli_fname) return {};
      std::string p = info.dli_fname;
   #if defined(__APPLE__)
      const size_t macos = p.rfind("/Contents/MacOS/");
      if (macos != std::string::npos) return p.substr(0, macos) + "/Contents/Helpers/" + helperName;
   #endif
      const size_t cut = p.rfind('/');
      if (cut == std::string::npos) return {};
      return p.substr(0, cut + 1) + helperName;
#endif
   }

   void ExitWhenParentDies(int64_t parentPid)
   {
#if defined(_WIN32)
      HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)parentPid);
      if (!h) ExitProcess(0);
      std::thread([h] {
         WaitForSingleObject(h, INFINITE);
         ExitProcess(0);
      }).detach();
#elif defined(__APPLE__)
      const int kq = kqueue();
      struct kevent ev;
      EV_SET(&ev, (uintptr_t)parentPid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
      if (kq < 0 || kevent(kq, &ev, 1, nullptr, 0, nullptr) == -1) _exit(0); // already gone
      std::thread([kq] {
         struct kevent out;
         while (kevent(kq, nullptr, 0, &out, 1, nullptr) < 0 && errno == EINTR) {}
         _exit(0);
      }).detach();
#else
      // Not PR_SET_PDEATHSIG: it fires when the *thread* that spawned us exits, and a DAW may call
      // setActive from a short-lived thread. A pidfd becomes readable when the process exits
      // (Linux 5.3+); older kernels fall back to watching getppid() change.
      if ((int64_t)getppid() != parentPid) _exit(0);
      const int pidfd = (int)syscall(SYS_pidfd_open, (pid_t)parentPid, 0);
      std::thread([pidfd, parentPid] {
         if (pidfd >= 0)
         {
            pollfd pfd{pidfd, POLLIN, 0};
            while (poll(&pfd, 1, -1) < 0 && errno == EINTR) {}
            _exit(0);
         }
         while ((int64_t)getppid() == parentPid) usleep(100000);
         _exit(0);
      }).detach();
#endif
   }

   bool SetThreadRealtime(double sampleRate, uint32_t blockFrames)
   {
      if (sampleRate <= 0.0 || blockFrames == 0) return false;
#if defined(__APPLE__)
      mach_timebase_info_data_t tb;
      mach_timebase_info(&tb);
      const double nsPerTick = (double)tb.numer / (double)tb.denom;
      const double periodNs = 1e9 * blockFrames / sampleRate;
      thread_time_constraint_policy_data_t p;
      p.period = (uint32_t)(periodNs / nsPerTick);
      p.computation = (uint32_t)(periodNs * 0.5 / nsPerTick);
      p.constraint = (uint32_t)(periodNs * 0.9 / nsPerTick);
      p.preemptible = 1;
      return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                               (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
#elif defined(__linux__)
      // Needs RLIMIT_RTPRIO (most audio setups grant it); false otherwise and the thread stays normal.
      sched_param sp{};
      sp.sched_priority = 70;
      return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
#else
      return false; // MMCSS "Pro Audio" lands in step 3.1
#endif
   }
}

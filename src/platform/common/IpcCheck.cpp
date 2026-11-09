// ipc-check: headless proof of src/platform/common/Ipc.h (docs/plans/vst-au/README.md, step 1.2).
// Spawns itself as the helper, so it needs nothing else on disk. Exit 0 = every check passed.
//
//   ipc-check            run all checks
//   ipc-check --echo <shm> <parent-pid>     (internal) helper that answers pings
//   ipc-check --middle <shm> <parent-pid>   (internal) spawns an echo grandchild, then dies
#include "platform/common/Ipc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <windows.h>
#else
   #include <signal.h>
#endif

namespace
{
   struct Block
   {
      std::atomic<uint32_t> ping;   // parent bumps, child waits on it
      std::atomic<uint32_t> pong;   // child bumps, parent waits on it
      std::atomic<uint64_t> value;  // parent writes
      std::atomic<uint64_t> echoed; // child writes value + 1
      std::atomic<uint32_t> quit;
      std::atomic<uint32_t> attached;
      std::atomic<int64_t> grandchild;
   };

   int gFails = 0;

   void Check(bool ok, const char* what)
   {
      printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
      if (!ok) ++gFails;
   }

   bool ProcessGone(int64_t pid)
   {
#if defined(_WIN32)
      HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
      if (!h) return true;
      const bool gone = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
      CloseHandle(h);
      return gone;
#else
      return kill((pid_t)pid, 0) != 0;
#endif
   }

   bool WaitFor(const std::atomic<uint32_t>& flag, int ms)
   {
      for (int i = 0; i < ms && !flag.load(std::memory_order_acquire); ++i)
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return flag.load(std::memory_order_acquire) != 0;
   }

   int RunEcho(const std::string& name, int64_t parent)
   {
      Ipc::ExitWhenParentDies(parent);
      Ipc::SharedMemory shm;
      if (!Ipc::SharedMemoryOpen(name, sizeof(Block), &shm)) return 3;
      Block* b = static_cast<Block*>(shm.data);
      Ipc::SetThreadRealtime(48000.0, 128);
      b->attached.store(1, std::memory_order_release);
      uint32_t seen = b->ping.load(std::memory_order_acquire);
      while (!b->quit.load(std::memory_order_acquire))
      {
         Ipc::SharedWait(&b->ping, seen, 100000000ull);
         const uint32_t now = b->ping.load(std::memory_order_acquire);
         if (now == seen) continue;
         seen = now;
         b->echoed.store(b->value.load(std::memory_order_acquire) + 1, std::memory_order_release);
         b->pong.fetch_add(1, std::memory_order_release);
         Ipc::SharedWake(&b->pong);
      }
      Ipc::SharedMemoryClose(&shm);
      return 0;
   }

   int RunMiddle(const std::string& name, int64_t parent)
   {
      Ipc::ExitWhenParentDies(parent);
      Ipc::SharedMemory shm;
      if (!Ipc::SharedMemoryOpen(name, sizeof(Block), &shm)) return 3;
      Block* b = static_cast<Block*>(shm.data);
      Ipc::Child gc;
      if (!Ipc::Spawn(Ipc::HelperPathBesideModule("ipc-check"),
                      {"--echo", name, std::to_string(Ipc::SelfPid())}, &gc))
         return 4;
      b->grandchild.store(gc.pid, std::memory_order_release);
      WaitFor(b->attached, 5000);
      std::exit(0); // die without reaping or killing the grandchild: it must notice by itself
   }
}

int main(int argc, char** argv)
{
   if (argc == 4 && std::string(argv[1]) == "--echo") return RunEcho(argv[2], std::atoll(argv[3]));
   if (argc == 4 && std::string(argv[1]) == "--middle") return RunMiddle(argv[2], std::atoll(argv[3]));

   const std::string self = Ipc::HelperPathBesideModule("ipc-check");
   Check(!self.empty(), "HelperPathBesideModule finds this executable's folder");
   const std::string pid = std::to_string(Ipc::SelfPid());

   // 1. shared memory: create, clash, open from a child
   const std::string name = Ipc::SharedMemoryUniqueName("ipck");
   Ipc::SharedMemory shm, clash;
   Check(Ipc::SharedMemoryCreate(name, sizeof(Block), &shm), "SharedMemoryCreate");
   if (!shm.data) return 1;
   Check(!Ipc::SharedMemoryCreate(name, sizeof(Block), &clash), "SharedMemoryCreate refuses an existing name");
   Block* b = new (shm.data) Block();

   // 2. spawn + round trips over the wake words
   Ipc::Child child;
   Check(Ipc::Spawn(self, {"--echo", name, pid}, &child), "Spawn the echo helper");
   Check(WaitFor(b->attached, 5000), "helper opened the region (attached within 5 s)");
   const int kTrips = 2000;
   std::vector<double> us;
   us.reserve(kTrips);
   bool allRight = true, noTimeout = true;
   for (int i = 0; i < kTrips; ++i)
   {
      const uint32_t before = b->pong.load(std::memory_order_acquire);
      b->value.store((uint64_t)i * 7, std::memory_order_release);
      const uint64_t t0 = Ipc::MonotonicNs();
      b->ping.fetch_add(1, std::memory_order_release);
      Ipc::SharedWake(&b->ping);
      while (b->pong.load(std::memory_order_acquire) == before)
         if (!Ipc::SharedWait(&b->pong, before, 1000000000ull)) { noTimeout = false; break; }
      us.push_back((double)(Ipc::MonotonicNs() - t0) / 1000.0);
      allRight &= b->echoed.load(std::memory_order_acquire) == (uint64_t)i * 7 + 1;
      if (!noTimeout) break;
   }
   std::sort(us.begin(), us.end());
   Check(noTimeout && allRight, "2000 ping/pong round trips, every answer correct");
   if (!us.empty())
      printf("  round trip p50 %.1f us, p99 %.1f us, max %.1f us\n", us[us.size() / 2], us[us.size() * 99 / 100],
             us.back());

   // 3. clean stop
   Check(Ipc::ChildAlive(child), "ChildAlive while running");
   b->quit.store(1, std::memory_order_release);
   b->ping.fetch_add(1, std::memory_order_release);
   Ipc::SharedWake(&b->ping);
   Check(Ipc::ChildWait(&child, 2000), "helper exits on quit within 2 s");
   Check(!Ipc::ChildAlive(child), "ChildAlive false after exit");

   // 4. kill
   b->quit.store(0);
   b->attached.store(0);
   Check(Ipc::Spawn(self, {"--echo", name, pid}, &child) && WaitFor(b->attached, 5000), "respawn");
   Ipc::ChildKill(&child);
   Check(!Ipc::ChildAlive(child) && child.pid == -1, "ChildKill");

   // 5. parent death: middle spawns a grandchild that watches the middle, then the middle exits
   b->attached.store(0);
   b->grandchild.store(0);
   Ipc::Child middle;
   Check(Ipc::Spawn(self, {"--middle", name, pid}, &middle), "Spawn the middle process");
   const bool middleDone = Ipc::ChildWait(&middle, 5000);
   const int64_t gc = b->grandchild.load();
   bool gcGone = false;
   for (int i = 0; i < 200 && gc > 0 && !(gcGone = ProcessGone(gc)); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
   Check(middleDone && gc > 0 && gcGone, "ExitWhenParentDies: grandchild exits within 2 s");

   // 6. close removes the name
   Ipc::SharedMemoryClose(&shm);
   Ipc::SharedMemory again;
   const bool reopened = Ipc::SharedMemoryOpen(name, sizeof(Block), &again);
   Ipc::SharedMemoryClose(&again);
#if defined(_WIN32)
   Check(!reopened, "owner close removes the region (last handle)");
#else
   Check(!reopened, "owner close unlinks the name");
#endif

   printf("%s\n", gFails ? "IPC CHECK: FAIL" : "IPC CHECK: PASS");
   return gFails ? 1 : 0;
}

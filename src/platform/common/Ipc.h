#pragma once
// Process plumbing for the plugin build (docs/plans/vst-au/README.md, step 1.2): the plugin shell
// in the DAW spawns one engine helper per instance and talks to it over shared memory.
//
// Kept apart from Platform.h on purpose: the shell links this file and nothing else from the app,
// so it stays small and pulls in no GL, audio device or UI code.
//
// Sides: macOS is the real one (POSIX shm, posix_spawn, kqueue, os_sync, Mach time constraint).
// Linux uses POSIX shm, posix_spawn, prctl(PR_SET_PDEATHSIG) and a shared futex. Windows uses a
// named file mapping and CreateProcess; its wake word polls and RT priority is a no-op until step
// 3.1 (MMCSS + a named event).
//
// Rules for the audio thread: SharedWake, MonotonicNs, ChildAlive are realtime safe (one syscall,
// no allocation, no lock). Everything else is for the main / control thread.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ipc
{
   struct SharedMemory
   {
      void* data = nullptr;
      size_t size = 0;
      std::string name;
      bool owner = false;     // creator unlinks on close
      void* handle = nullptr; // Windows mapping handle
   };

   // A name no other live process uses: "<prefix>.<pid>.<counter>" (POSIX names get a leading '/';
   // Windows names live in the Local\ namespace). Keep the prefix short: macOS caps names at 31.
   std::string SharedMemoryUniqueName(const char* prefix);

   // Creates a new zero-filled region; fails if the name already exists.
   bool SharedMemoryCreate(const std::string& name, size_t size, SharedMemory* out);
   // Maps an existing region created by another process.
   bool SharedMemoryOpen(const std::string& name, size_t size, SharedMemory* out);
   // Unmaps; the owner also removes the name. Safe on an empty SharedMemory.
   void SharedMemoryClose(SharedMemory* shm);

   // Wake word living inside shared memory. SharedWait returns when *word != expected (true) or after
   // timeoutNs (false); 0 waits forever. Spurious early returns are allowed: re-check your condition.
   bool SharedWait(std::atomic<uint32_t>* word, uint32_t expected, uint64_t timeoutNs);
   void SharedWake(std::atomic<uint32_t>* word);

   uint64_t MonotonicNs();

   struct Child
   {
      int64_t pid = -1;
      void* handle = nullptr; // Windows process handle
   };

   // Starts `path` with `args` (argv[1..]); inherits the environment, no shell, no handles.
   bool Spawn(const std::string& path, const std::vector<std::string>& args, Child* out);
   bool ChildAlive(const Child& c);
   // Waits up to timeoutMs for the child to exit and reaps it. True when it has exited.
   bool ChildWait(Child* c, int timeoutMs);
   // Kills and reaps. Safe on an already-exited or empty Child.
   void ChildKill(Child* c);
   // Sends the kill only: no reap, `c` untouched, so another thread may own the Child (the plugin
   // shell's watchdog). False when the signal was refused (sandbox).
   bool ChildSignalKill(const Child& c);

   // Path of `helperName` beside the module (dylib, bundle, .so, .dll) that contains this code:
   //   macOS  X.vst3/Contents/MacOS/X  ->  X.vst3/Contents/Helpers/<helperName>
   //   Linux  X.vst3/Contents/<arch>-linux/X.so  ->  same folder
   //   Windows X.vst3/Contents/x86_64-win/X.vst3  ->  same folder, <helperName>.exe
   // Empty when the module path cannot be read.
   std::string HelperPathBesideModule(const char* helperName);

   // In the helper: exit (code 0) as soon as the parent process dies, however it dies. Starts a
   // watcher thread where the OS needs one. Exits at once if the parent is already gone.
   void ExitWhenParentDies(int64_t parentPid);

   int64_t SelfPid();

   // Puts the calling thread in the realtime class for a period of blockFrames at sampleRate.
   // False where the OS refused or the side is not built yet (Windows until 3.1).
   bool SetThreadRealtime(double sampleRate, uint32_t blockFrames);
}

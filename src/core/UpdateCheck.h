#pragma once

#include <string>

// Background check against the GitHub Releases API for a newer build than
// the one currently running, surfaced as a small badge in the menu bar (see
// main.cpp). Same thread + mutex + Poll() try_lock shape as PluginScanner
// (src/audio/PluginScanner.h) - a worker thread hands a result to the main
// thread, which only ever reads it via a non-blocking try_lock.
namespace UpdateCheck
{
   // Spawns the worker. Safe to call once at startup; a second call while
   // one is in flight is a no-op. No-op entirely when INFINITE_NO_UPDATE_CHECK
   // or IMAGERESYNTH_SELFTEST is set in the environment, so headless/CI runs
   // never make a network call.
   void Start();

   // Main thread, once a frame. Cheap; try_lock, never blocks.
   void Poll();

   // True once Poll() has seen a result reporting a newer version that the
   // user hasn't dismissed.
   bool UpdateAvailable();

   // Empty until UpdateAvailable() is true.
   const std::string& LatestVersion();

   // Writes the current latest version into the dismissal pref so the badge
   // stays hidden until a newer one appears.
   void Dismiss();

   // Joins the worker if running. Call before the process exits.
   void Shutdown();
}

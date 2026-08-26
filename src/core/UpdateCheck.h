#pragma once

#include <string>

// Background check against the GitHub Releases API for a newer build than
// the one currently running, surfaced as a small badge in the menu bar (see
// main.cpp). Same thread + mutex + Poll() try_lock shape as PluginScanner
// (src/audio/PluginScanner.h) - a worker thread hands a result to the main
// thread, which only ever reads it via a non-blocking try_lock.
namespace UpdateCheck
{
   // Result of the most recent check, driving the "Check for updates" modal.
   // Idle: never checked. Checking: a request is in flight (also true right
   // after Start() when one was already running). UpToDate/UpdateAvailable/
   // Failed: the last request finished with that outcome.
   enum class Status { Idle, Checking, UpToDate, UpdateAvailable, Failed };

   // Spawns the worker. Safe to call once at startup; a second call while
   // one is in flight is a no-op (GetStatus() still reports Checking, not a
   // stale/empty result). No-op entirely when INFINITE_NO_UPDATE_CHECK or
   // IMAGERESYNTH_SELFTEST is set in the environment - GetStatus() resolves
   // straight to Failed with an explanatory LastError() so headless/CI runs
   // never make a network call and a modal can never spin forever.
   void Start();

   // Main thread, once a frame. Cheap; try_lock, never blocks.
   void Poll();

   // GetStatus()/ResultVersion()/LastError()/DownloadUrl() always report the
   // true state of the latest GitHub release, independent of whether the
   // user previously dismissed that version - unlike UpdateAvailable()/
   // LatestVersion() below, they are not filtered by the dismissal pref.
   Status GetStatus();

   // Valid when GetStatus() is UpToDate or UpdateAvailable: the latest tag
   // GitHub reports, regardless of dismissal.
   const std::string& ResultVersion();

   // Valid when GetStatus() is Failed.
   const std::string& LastError();

   // Valid when GetStatus() is UpdateAvailable: the best URL to open for the
   // running platform - a matching release asset, else the release page,
   // else the website's download section.
   const std::string& DownloadUrl();

   // True once Poll() has seen a result reporting a newer version that the
   // user hasn't dismissed. Drives the passive menu-bar badge only.
   bool UpdateAvailable();

   // Empty until UpdateAvailable() is true.
   const std::string& LatestVersion();

   // Writes the current latest version into the dismissal pref so the badge
   // stays hidden until a newer one appears.
   void Dismiss();

   // Joins the worker if running. Call before the process exits.
   void Shutdown();
}

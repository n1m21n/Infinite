#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/Platform.h"

// Background scanner backing the Plugins mode of the docked node-browser
// panel. Same thread + mutex + PollResults() try_lock shape as SampleScanner,
// and the same contract that matters most to the user: loading from disk
// shows the existing index instantly, and a scan only ever happens from an
// explicit StartScan() - opening the app never rescans.
class PluginScanner
{
public:
   // Bumped whenever Entry's shape or meaning changes; a cache written by a
   // different schema version is dropped rather than migrated, forcing one
   // clean rescan. Bumped 4 -> 5 to force exactly that: schema 4 indexes
   // predate the format=="au" load filter removal below and can hold zero
   // VST3 entries even on a machine with VST3 plugins installed.
   static constexpr int kIndexSchemaVersion = 5;

   using Entry = Platform::PluginDesc;

   PluginScanner() = default;
   ~PluginScanner();

   // Main thread only.
   void AddFolder(const std::string& path);
   void RemoveFolder(const std::string& path);
   const std::vector<std::string>& Folders() const { return mFolders; }

   // Main thread only. A scan already in flight is left to finish; gate the
   // Rescan button on IsScanning() rather than queueing another.
   void StartScan(const std::string& folder = std::string());
   bool IsScanning() const { return mScanning.load(std::memory_order_relaxed); }
   int PluginsFoundSoFar() const { return mFound.load(std::memory_order_relaxed); }

   // Main thread only, call once per frame: cheap (try_lock), never blocks on
   // the worker.
   void PollResults();

   const std::vector<Entry>& Index() const { return mIndex; }

   // Bundles the last scan could not describe, by path: each one crashed or
   // hung its child process rather than returning a description. Surfaced in
   // the Plugins panel because the alternative - silently showing a shorter
   // list - is what made a damaged plugin look like a bug in this app.
   const std::vector<std::string>& FailedBundles() const { return mFailed; }

   // Looks an identifier up in the cached index. Used by patch load and by the
   // Finder-drop path to recover a display name for an identifier without
   // instantiating anything.
   const Entry* FindByIdentifier(const std::string& identifier) const;

   // Disk persistence, mirroring SampleScanner's. LoadFromDisk never scans.
   void LoadFromDisk();
   void SaveFoldersToDisk() const;
   void SaveIndexToDisk() const;

private:
   void ScanThreadMain(std::vector<std::string> vst3Folders);

   std::vector<std::string> mFolders;
   std::vector<Entry> mIndex;
   std::vector<std::string> mFailed;

   std::thread mScanThread;
   std::mutex mResultMutex;
   std::vector<Entry> mPendingResult;         // guarded by mResultMutex
   std::vector<std::string> mPendingFailed;   // guarded by mResultMutex
   std::atomic<bool> mResultReady { false };
   std::atomic<bool> mScanning { false };
   std::atomic<int> mFound { 0 };
};

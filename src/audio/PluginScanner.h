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
//
// Two deliberate differences from SampleScanner:
//
//  - No user-managed folder list. AU discovery is a registry query
//    (AVAudioUnitComponentManager), not a directory walk, so there is nothing
//    to add a folder to - the panel gets a single Rescan button instead of
//    Add-folder/per-folder-refresh. VST3, when it lands, is the format that
//    needs folders; that is when the folder machinery should appear.
//  - The cache carries a schema version. The entry shape is expected to grow
//    when a second plugin format arrives, and discarding a stale cache is
//    strictly better than hand-migrating one.
class PluginScanner
{
public:
   // Bumped whenever Entry's shape or meaning changes; a cache written by a
   // different version is discarded rather than migrated.
   static constexpr int kIndexSchemaVersion = 1;

   using Entry = Platform::PluginDesc;

   PluginScanner() = default;
   ~PluginScanner();

   // Main thread only. A scan already in flight is left to finish; gate the
   // Rescan button on IsScanning() rather than queueing another.
   void StartScan();
   bool IsScanning() const { return mScanning.load(std::memory_order_relaxed); }
   int PluginsFoundSoFar() const { return mFound.load(std::memory_order_relaxed); }

   // Main thread only, call once per frame: cheap (try_lock), never blocks on
   // the worker.
   void PollResults();

   const std::vector<Entry>& Index() const { return mIndex; }

   // Looks an identifier up in the cached index. Used by patch load and by the
   // Finder-drop path to recover a display name for an identifier without
   // instantiating anything.
   const Entry* FindByIdentifier(const std::string& identifier) const;

   // Disk persistence, mirroring SampleScanner's. LoadFromDisk never scans.
   void LoadFromDisk();
   void SaveIndexToDisk() const;

private:
   void ScanThreadMain();

   std::vector<Entry> mIndex;

   std::thread mScanThread;
   std::mutex mResultMutex;
   std::vector<Entry> mPendingResult; // guarded by mResultMutex
   std::atomic<bool> mResultReady { false };
   std::atomic<bool> mScanning { false };
   std::atomic<int> mFound { 0 };
};

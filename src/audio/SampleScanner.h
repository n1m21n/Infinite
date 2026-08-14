#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Background sample library for the Samples search panel: a list of folders
// the user has added, a recursive scan of those folders for audio files, and
// disk persistence so re-opening the app shows the existing index
// immediately instead of rescanning (see docs/plans/audio/README.md P3e).
//
// The scan itself runs on a plain std::thread - the first of its kind in
// this codebase, since folder scanning is unrelated main-thread/UI work, not
// audio-thread work, and has none of AudioNode::ProcessBlock's real-time
// constraints. It never touches ImGui, the audio graph, or anything the
// render thread reads; the only shared state is a mutex-guarded result
// vector the main thread polls once per frame with a non-blocking try_lock,
// so a slow scan of a huge folder never stalls a frame.
class SampleScanner
{
public:
   struct Entry
   {
      std::string path;       // full path, used to load/drag
      std::string fileName;   // display name
      std::string folderRoot; // which added folder this came from
   };

   SampleScanner();
   ~SampleScanner();

   // Main thread only.
   void AddFolder(const std::string& path);
   void RemoveFolder(const std::string& path);
   const std::vector<std::string>& Folders() const { return mFolders; }

   // Kicks off (or restarts) a scan of every added folder on a background
   // thread. A scan already in flight is left to finish; call IsScanning()
   // to gate the UI's Refresh button instead of queuing another one.
   void StartScan();
   bool IsScanning() const { return mScanning.load(std::memory_order_relaxed); }
   int FilesFoundSoFar() const { return mFilesFound.load(std::memory_order_relaxed); }

   // Main thread only, call once per frame: cheap (try_lock), picks up a
   // finished scan's results without ever blocking on the worker thread.
   void PollResults();

   const std::vector<Entry>& Index() const { return mIndex; }

   // Disk persistence, mirroring Patch.cpp's RecentsPath/settings-dir
   // pattern but serialized with crude_json (already vendored for
   // imgui-node-editor's own settings, so no new JSON dependency). Loading
   // the index restores it immediately with no scan - a scan only ever
   // happens from an explicit StartScan() call.
   void LoadFromDisk();
   void SaveFoldersToDisk() const;
   void SaveIndexToDisk() const;

private:
   void ScanThreadMain(std::vector<std::string> folders);

   std::vector<std::string> mFolders;
   std::vector<Entry> mIndex;

   std::thread mScanThread;
   std::mutex mResultMutex;
   std::vector<Entry> mPendingResult; // guarded by mResultMutex
   std::atomic<bool> mResultReady { false };
   std::atomic<bool> mScanning { false };
   std::atomic<int> mFilesFound { 0 };
};

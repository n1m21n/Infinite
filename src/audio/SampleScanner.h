#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Background library scanner backing both the Samples and Media modes of the
// docked node-browser panel: a list of folders the user has added, a
// recursive scan of those folders for the kind's extensions, and disk
// persistence so re-opening the app shows the existing index immediately
// instead of rescanning (see docs/plans/audio/README.md P3e).
//
// The scan itself runs on a plain std::thread - the first of its kind in
// this codebase, since folder scanning is unrelated main-thread/UI work, not
// audio-thread work, and has none of AudioNode::ProcessBlock's real-time
// constraints. It never touches ImGui, the audio graph, or anything the
// render thread reads; the only shared state is a mutex-guarded result
// vector the main thread polls once per frame with a non-blocking try_lock,
// so a slow scan of a huge folder never stalls a frame.
//
// Turbo 0.49: library roots carry an optional label, roots are stored with
// normalised separators and no trailing separator, never resolved to another
// spelling (a mapped drive stays a drive letter); "C:/Samples" and
// "c:\samples\" are one root by PathKey, a link to a root is caught by
// RootIdentity when adding. Every
// root change rescans everything in the background, and the scan dedupes:
// a root nested inside another root is only walked as its own root, a
// directory reached twice (symlink, junction, loop) is walked once, and the
// final index never holds two entries with the same PathKey.
class SampleScanner
{
public:
   enum class Kind { Audio, Media };

   struct Entry
   {
      std::string path;          // full path, used to load/drag
      std::string fileName;      // display name
      std::string fileNameLower; // lowercased display name for fast filter matching
      std::string folderRoot;    // which added folder this came from
      // Turbo 0.49: folder of the file relative to folderRoot, '/'-separated,
      // original case, empty for files directly in the root. Derived from
      // path at scan and load time, not persisted.
      std::string relDir;
   };

   explicit SampleScanner(Kind kind = Kind::Audio);
   ~SampleScanner();

   // Main thread only. AddFolder / RemoveFolder save the folder list and
   // queue a full rescan; SetFolderLabel only saves.
   void AddFolder(const std::string& path, const std::string& label = std::string());
   void RemoveFolder(const std::string& path);
   void SetFolderLabel(const std::string& path, const std::string& label);
   const std::vector<std::string>& Folders() const { return mFolders; }
   // The user's label for a root, or "" when it has none.
   std::string FolderLabel(const std::string& path) const;
   // Label, else the folder's last path component.
   std::string FolderDisplayName(const std::string& path) const;

   // Turbo 0.49: adds default roots once per settings directory (a marker in
   // the folders file remembers it was done, so a default the user removed
   // stays removed). Skips empty paths.
   void SeedDefaultFolders(const std::vector<std::pair<std::string, std::string>>& pathAndLabel);

   // Kicks off a scan on a background thread: every added folder by default,
   // or just `folder` when given, in which case the other folders' existing
   // index entries are left untouched (see PollResults). A scan requested
   // while one is in flight is queued (as a full rescan) and starts when
   // that one has been picked up by PollResults.
   void StartScan(const std::string& folder = std::string());
   // In flight from StartScan until PollResults has merged the result (or a
   // queued rescan is pending); always cleared by PollResults.
   bool IsScanning() const { return mScanning.load(std::memory_order_relaxed) || mRescanQueued; }
   int FilesFoundSoFar() const { return mFilesFound.load(std::memory_order_relaxed); }

   // Main thread only, call once per frame: cheap (try_lock), picks up a
   // finished scan's results without ever blocking on the worker thread.
   void PollResults();

   const std::vector<Entry>& Index() const { return mIndex; }
   uint64_t IndexVersion() const { return mIndexVersion; }

   // Disk persistence, mirroring Patch.cpp's RecentsPath/settings-dir
   // pattern but serialized with crude_json (already vendored for
   // imgui-node-editor's own settings, so no new JSON dependency). Loading
   // the index restores it immediately with no scan - a scan only happens
   // from StartScan() or a root change.
   void LoadFromDisk();
   void SaveFoldersToDisk() const;
   void SaveIndexToDisk() const;

   // Comparison key for a path: '/' separators, no trailing separator,
   // lowercase on Windows. Pure string work (no disk access).
   static std::string PathKey(const std::string& path);
   // Absolute + lexically normal + native separators + no trailing
   // separator. Does not resolve links or drive mappings. Never throws.
   static std::string CanonicalFolder(const std::string& path);
   // Native separators + no trailing separator only (pure string work).
   static std::string NormalizeFolderSpelling(const std::string& path);
   // Comparison key of the folder a path really is (fs::canonical, so links,
   // junctions, subst and mapped drives collapse), PathKey when unresolvable.
   // Disk access: main thread use only when adding a root. Never throws.
   static std::string RootIdentity(const std::string& path);

private:
   void StartScanRoots(const std::vector<std::string>& roots);
   void ScanThreadMain(std::vector<std::string> folders, std::vector<std::string> allRoots);
   void ScanFolders(const std::vector<std::string>& folders, const std::vector<std::string>& allRoots,
                    std::vector<Entry>& found);
   // FolderIndex, else a root that resolves to the same folder.
   int SameRootIndex(const std::string& path) const;
   // Drops entries whose root is gone and entries whose PathKey repeats,
   // re-attaches entries to their root, fills relDir. Main thread.
   void SanitizeIndex();
   int FolderIndex(const std::string& path) const;

   Kind mKind;
   std::vector<std::string> mFolders;
   std::vector<std::string> mLabels; // parallel to mFolders
   bool mDefaultsSeeded = false;
   std::vector<Entry> mIndex;
   uint64_t mIndexVersion { 1 };

   // Which folders the in-flight (or just-finished) scan covers - main
   // thread only, set in StartScan and read back in PollResults to know
   // which part of mIndex to replace vs. leave alone.
   std::vector<std::string> mScanningFolders;
   bool mRescanQueued = false; // main thread only

   std::thread mScanThread;
   std::mutex mResultMutex;
   std::vector<Entry> mPendingResult; // guarded by mResultMutex
   bool mPendingFailed = false;       // guarded by mResultMutex: the scan threw, keep the old index
   std::atomic<bool> mResultReady { false };
   std::atomic<bool> mScanning { false };
   std::atomic<bool> mAbort { false };
   std::atomic<int> mFilesFound { 0 };
};

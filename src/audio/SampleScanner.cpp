#include "SampleScanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

#include "crude_json.h"

namespace
{
   namespace fs = std::filesystem;

   // Mirrors Patch.cpp's RecentsPath / main.cpp's settings-dir setup: same
   // directory, same "getenv(HOME) or give up" fallback. Duplicated rather
   // than shared because Patch.cpp's helper isn't exposed outside main.cpp's
   // translation unit, and this is three lines.
   std::string SettingsDir()
   {
      const char* home = getenv("HOME");
      if (home == nullptr)
         return std::string();
      std::string dir = std::string(home) + "/Library/Application Support/Infinite";
      // Mirrors main.cpp's INFINITE_DRAGTEST throwaway-settings-file pattern:
      // INFINITE_SAMPLERDRAGTEST drives real AddFolder/RemoveFolder/StartScan
      // calls against whatever this resolves to, and without this override it
      // was doing that against the user's actual SampleFolders.json/
      // SampleIndex.json - wiping their real library folder on every hygiene
      // run. Route it to a throwaway subdirectory instead.
      if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr)
         dir += "/sampler_drag_test";
      mkdir(dir.c_str(), 0755);
      return dir;
   }

   std::string FoldersPath()
   {
      const std::string dir = SettingsDir();
      return dir.empty() ? std::string() : dir + "/SampleFolders.json";
   }

   std::string IndexPath()
   {
      const std::string dir = SettingsDir();
      return dir.empty() ? std::string() : dir + "/SampleIndex.json";
   }

   std::string ToLower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }

   // Discoverable, not a decode guarantee - AVAudioFile (the only decoder
   // Platform::DecodeAudioFileToBuffer uses) reads most of these reliably,
   // but FLAC support varies by macOS version. A file that lists here but
   // fails to decode later surfaces through SamplerNode::Status() rather
   // than being silently excluded from the index.
   bool HasAudioExtension(const fs::path& p)
   {
      static const char* kExts[] = { ".wav", ".aif", ".aiff", ".caf", ".m4a", ".mp3", ".flac" };
      const std::string ext = ToLower(p.extension().string());
      for (const char* e : kExts)
         if (ext == e)
            return true;
      return false;
   }
}

SampleScanner::SampleScanner() = default;

SampleScanner::~SampleScanner()
{
   if (mScanThread.joinable())
      mScanThread.join();
}

void SampleScanner::AddFolder(const std::string& path)
{
   if (std::find(mFolders.begin(), mFolders.end(), path) != mFolders.end())
      return;
   mFolders.push_back(path);
   SaveFoldersToDisk();
}

void SampleScanner::RemoveFolder(const std::string& path)
{
   mFolders.erase(std::remove(mFolders.begin(), mFolders.end(), path), mFolders.end());
   SaveFoldersToDisk();
   // Entries from the removed folder stay in the index until the next
   // Refresh - matching "manual refresh only" (no implicit rescan on every
   // folder-list edit), same as the plan's Refresh-button-only contract.
}

void SampleScanner::StartScan(const std::string& folder)
{
   if (mScanning.exchange(true, std::memory_order_relaxed))
      return; // already in flight

   if (mScanThread.joinable())
      mScanThread.join(); // previous scan already finished; reap it before starting a new one

   mFilesFound.store(0, std::memory_order_relaxed);
   mScanningFolders = folder.empty() ? mFolders : std::vector<std::string> { folder };
   mScanThread = std::thread(&SampleScanner::ScanThreadMain, this, mScanningFolders);
}

void SampleScanner::ScanThreadMain(std::vector<std::string> folders)
{
   std::vector<Entry> found;

   for (const std::string& root : folders)
   {
      std::error_code ec;
      fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
      const fs::recursive_directory_iterator end;
      for (; it != end && !ec; it.increment(ec))
      {
         const fs::directory_entry& entry = *it;
         std::error_code fileEc;
         if (!entry.is_regular_file(fileEc) || fileEc)
            continue;
         if (!HasAudioExtension(entry.path()))
            continue;

         Entry e;
         e.path = entry.path().string();
         e.fileName = entry.path().filename().string();
         e.folderRoot = root;
         found.push_back(std::move(e));
         mFilesFound.fetch_add(1, std::memory_order_relaxed);
      }
   }

   {
      std::lock_guard<std::mutex> lock(mResultMutex);
      mPendingResult = std::move(found);
   }
   mResultReady.store(true, std::memory_order_release);
   mScanning.store(false, std::memory_order_relaxed);
}

void SampleScanner::PollResults()
{
   if (!mResultReady.load(std::memory_order_acquire))
      return;

   std::unique_lock<std::mutex> lock(mResultMutex, std::try_to_lock);
   if (!lock.owns_lock())
      return; // worker thread mid-write to mPendingResult; try again next frame

   // Replace only the entries that came from a folder this scan covered;
   // a single-folder Refresh must leave every other folder's index alone
   // rather than wiping the whole thing down to just what it found.
   std::vector<Entry> merged;
   merged.reserve(mIndex.size() + mPendingResult.size());
   for (Entry& e : mIndex)
      if (std::find(mScanningFolders.begin(), mScanningFolders.end(), e.folderRoot) == mScanningFolders.end())
         merged.push_back(std::move(e));
   for (Entry& e : mPendingResult)
      merged.push_back(std::move(e));

   mIndex = std::move(merged);
   mPendingResult.clear();
   mResultReady.store(false, std::memory_order_relaxed);
   lock.unlock();

   SaveIndexToDisk();
}

void SampleScanner::LoadFromDisk()
{
   const std::string foldersPath = FoldersPath();
   if (!foldersPath.empty())
   {
      auto [json, ok] = crude_json::value::load(foldersPath);
      if (ok && json.is_array())
      {
         for (const crude_json::value& v : json.get<crude_json::array>())
            if (v.is_string())
               mFolders.push_back(v.get<crude_json::string>());
      }
   }

   const std::string indexPath = IndexPath();
   if (!indexPath.empty())
   {
      auto [json, ok] = crude_json::value::load(indexPath);
      if (ok && json.is_array())
      {
         for (const crude_json::value& v : json.get<crude_json::array>())
         {
            if (!v.is_object())
               continue;
            Entry e;
            if (v["path"].is_string())
               e.path = v["path"].get<crude_json::string>();
            if (v["fileName"].is_string())
               e.fileName = v["fileName"].get<crude_json::string>();
            if (v["folderRoot"].is_string())
               e.folderRoot = v["folderRoot"].get<crude_json::string>();
            if (!e.path.empty())
               mIndex.push_back(std::move(e));
         }
      }
   }
}

void SampleScanner::SaveFoldersToDisk() const
{
   const std::string path = FoldersPath();
   if (path.empty())
      return;

   crude_json::value json = crude_json::array{};
   for (const std::string& f : mFolders)
      json.push_back(crude_json::value(f));
   json.save(path, 2);
}

void SampleScanner::SaveIndexToDisk() const
{
   const std::string path = IndexPath();
   if (path.empty())
      return;

   crude_json::value json = crude_json::array{};
   for (const Entry& e : mIndex)
   {
      crude_json::value entry = crude_json::object{};
      entry["path"] = e.path;
      entry["fileName"] = e.fileName;
      entry["folderRoot"] = e.folderRoot;
      json.push_back(std::move(entry));
   }
   json.save(path, 2);
}

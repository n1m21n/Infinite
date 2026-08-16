#include "PluginScanner.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include <sys/stat.h>

#include "crude_json.h"

namespace
{
   namespace fs = std::filesystem;

   // Same directory and the same "getenv(HOME) or give up" fallback as
   // SampleScanner::SettingsDir, including its throwaway-subdirectory
   // override: INFINITE_PLUGINDRAGTEST drives a real StartScan/SaveIndexToDisk
   // against whatever this resolves to, and without the override a hygiene run
   // would overwrite the user's actual plugin index.
   std::string SettingsDir()
   {
      const char* home = getenv("HOME");
      if (home == nullptr)
         return std::string();
      std::string dir = std::string(home) + "/Library/Application Support/Infinite";
      if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
         dir += "/plugin_drag_test";
      mkdir(dir.c_str(), 0755);
      return dir;
   }

   std::string FoldersPath()
   {
      const std::string dir = SettingsDir();
      if (dir.empty())
         return std::string();
      return dir + "/PluginFolders.json";
   }

   std::string IndexPath()
   {
      const std::string dir = SettingsDir();
      if (dir.empty())
         return std::string();
      return dir + "/PluginIndex.json";
   }
}

PluginScanner::~PluginScanner()
{
   if (mScanThread.joinable())
      mScanThread.join();
}

void PluginScanner::AddFolder(const std::string& path)
{
   if (std::find(mFolders.begin(), mFolders.end(), path) != mFolders.end())
      return;
   mFolders.push_back(path);
   SaveFoldersToDisk();
   Platform::SetVST3SearchFolders(mFolders);
}

void PluginScanner::RemoveFolder(const std::string& path)
{
   mFolders.erase(std::remove(mFolders.begin(), mFolders.end(), path), mFolders.end());
   SaveFoldersToDisk();
   Platform::SetVST3SearchFolders(mFolders);
}

void PluginScanner::StartScan(const std::string& folder)
{
   if (mScanning.exchange(true, std::memory_order_relaxed))
      return; // already in flight

   if (mScanThread.joinable())
      mScanThread.join(); // previous scan already finished; reap it first

   mFound.store(0, std::memory_order_relaxed);

   std::vector<std::string> vst3Folders;
   if (!folder.empty())
   {
      vst3Folders.push_back(folder);
   }
   else
   {
      // Standard macOS VST3 paths
      vst3Folders.push_back("/Library/Audio/Plug-Ins/VST3");
      const char* home = getenv("HOME");
      if (home != nullptr)
         vst3Folders.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
      for (const std::string& userFolder : mFolders)
         if (std::find(vst3Folders.begin(), vst3Folders.end(), userFolder) == vst3Folders.end())
            vst3Folders.push_back(userFolder);
   }

   mScanThread = std::thread(&PluginScanner::ScanThreadMain, this, std::move(vst3Folders));
}

void PluginScanner::ScanThreadMain(std::vector<std::string> vst3Folders)
{
   std::vector<Entry> found;

   // 1. Audio Units registry enumeration
   Platform::EnumerateAudioUnits(found);
   mFound.store((int)found.size(), std::memory_order_relaxed);

   // 2. VST3 directory walk
   Platform::EnumerateVST3Plugins(vst3Folders, found);
   mFound.store((int)found.size(), std::memory_order_relaxed);

   {
      std::lock_guard<std::mutex> lock(mResultMutex);
      mPendingResult = std::move(found);
   }
   mResultReady.store(true, std::memory_order_release);
   mScanning.store(false, std::memory_order_relaxed);
}

void PluginScanner::PollResults()
{
   if (!mResultReady.load(std::memory_order_acquire))
      return;

   std::unique_lock<std::mutex> lock(mResultMutex, std::try_to_lock);
   if (!lock.owns_lock())
      return; // worker mid-write; try again next frame

   mIndex = std::move(mPendingResult);
   mPendingResult.clear();
   mResultReady.store(false, std::memory_order_relaxed);
   lock.unlock();

   SaveIndexToDisk();
}

const PluginScanner::Entry* PluginScanner::FindByIdentifier(const std::string& identifier) const
{
   if (identifier.empty())
      return nullptr;
   for (const Entry& e : mIndex)
      if (e.identifier == identifier)
         return &e;
   return nullptr;
}

void PluginScanner::LoadFromDisk()
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
      Platform::SetVST3SearchFolders(mFolders);
   }

   const std::string path = IndexPath();
   if (path.empty())
      return;

   auto [json, ok] = crude_json::value::load(path);
   if (!ok || !json.is_object())
      return;

   // A cache from a different schema is dropped, not migrated - the user's
   // next Rescan rebuilds it in a second or two.
   if (!json["schema"].is_number())
      return;
   if ((int)json["schema"].get<crude_json::number>() != kIndexSchemaVersion)
      return;
   if (!json["plugins"].is_array())
      return;

   for (const crude_json::value& v : json["plugins"].get<crude_json::array>())
   {
      if (!v.is_object())
         continue;
      Entry e;
      // Every read goes through contains() first: crude_json's *const*
      // operator[] asserts (and aborts the process) on a missing key rather
      // than returning null, and SaveIndexToDisk deliberately omits "path" for
      // AU entries - so a bare v["path"] killed the app at launch as soon as
      // the index held a single Audio Unit, taking the VST3 bundle-path cache
      // seeded below down with it.
      auto str = [&v](const char* key, std::string& dst)
      {
         if (v.contains(key) && v[key].is_string())
            dst = v[key].get<crude_json::string>();
      };
      str("format", e.format);
      str("name", e.name);
      str("manufacturer", e.manufacturer);
      str("identifier", e.identifier);
      str("path", e.path);
      if (v.contains("acceptsNotes") && v["acceptsNotes"].is_boolean())
         e.acceptsNotes = v["acceptsNotes"].get<crude_json::boolean>();
      if (!e.identifier.empty())
      {
         if (e.format == "vst3" && !e.path.empty())
            Platform::CacheVST3BundlePath(e.identifier, e.path);
         mIndex.push_back(std::move(e));
      }
   }
}

void PluginScanner::SaveFoldersToDisk() const
{
   const std::string path = FoldersPath();
   if (path.empty())
      return;

   crude_json::value json = crude_json::array{};
   for (const std::string& f : mFolders)
      json.push_back(crude_json::value(f));
   json.save(path, 2);
}

void PluginScanner::SaveIndexToDisk() const
{
   const std::string path = IndexPath();
   if (path.empty())
      return;

   crude_json::value plugins = crude_json::array{};
   for (const Entry& e : mIndex)
   {
      crude_json::value entry = crude_json::object{};
      entry["format"] = e.format;
      entry["name"] = e.name;
      entry["manufacturer"] = e.manufacturer;
      entry["identifier"] = e.identifier;
      if (!e.path.empty())
         entry["path"] = e.path;
      entry["acceptsNotes"] = e.acceptsNotes;
      plugins.push_back(std::move(entry));
   }

   crude_json::value json = crude_json::object{};
   json["schema"] = (crude_json::number)kIndexSchemaVersion;
   json["plugins"] = std::move(plugins);
   json.save(path, 2);
}

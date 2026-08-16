#include "PluginScanner.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include "crude_json.h"

namespace
{
   namespace fs = std::filesystem;

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
}

void PluginScanner::RemoveFolder(const std::string& path)
{
   mFolders.erase(std::remove(mFolders.begin(), mFolders.end(), path), mFolders.end());
   SaveFoldersToDisk();
}

void PluginScanner::StartScan(const std::string& folder)
{
   (void)folder;
   if (mScanning.exchange(true, std::memory_order_relaxed))
      return; // already in flight

   if (mScanThread.joinable())
      mScanThread.join(); // previous scan already finished; reap it first

   mFound.store(0, std::memory_order_relaxed);
   mScanThread = std::thread(&PluginScanner::ScanThreadMain, this);
}

void PluginScanner::ScanThreadMain()
{
   std::vector<Entry> found;
   std::vector<std::string> failed;

   // Audio Units registry enumeration. Safe in-process: this is a registry
   // query against cached metadata, it does not load any plugin binary.
   Platform::EnumerateAudioUnits(found);
   mFound.store((int)found.size(), std::memory_order_relaxed);

   {
      std::lock_guard<std::mutex> lock(mResultMutex);
      mPendingResult = std::move(found);
      mPendingFailed = std::move(failed);
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
   mFailed = std::move(mPendingFailed);
   mPendingFailed.clear();
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
   }

   const std::string path = IndexPath();
   if (path.empty())
      return;

   auto [json, ok] = crude_json::value::load(path);
   if (!ok || !json.is_object())
      return;

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
      if (!e.identifier.empty() && e.format == "au")
      {
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

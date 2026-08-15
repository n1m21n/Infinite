#include "PluginScanner.h"

#include <cstdlib>

#include <sys/stat.h>

#include "crude_json.h"

namespace
{
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

void PluginScanner::StartScan()
{
   if (mScanning.exchange(true, std::memory_order_relaxed))
      return; // already in flight

   if (mScanThread.joinable())
      mScanThread.join(); // previous scan already finished; reap it first

   mFound.store(0, std::memory_order_relaxed);
   mScanThread = std::thread(&PluginScanner::ScanThreadMain, this);
}

void PluginScanner::ScanThreadMain()
{
   // Deliberately off the main thread even though it is a single call: a cold
   // component registry (first launch after installing plugins, or after an
   // OS update) can take several seconds to answer, and the app must stay
   // responsive through it. Platform::EnumerateAudioUnits touches nothing this
   // process shares - no ImGui, no audio graph, no GL.
   std::vector<Entry> found;
   Platform::EnumerateAudioUnits(found);
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

   // A registry query always returns the complete set (unlike SampleScanner's
   // per-folder refresh), so this is a straight replace, not a merge.
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
      if (v["format"].is_string())
         e.format = v["format"].get<crude_json::string>();
      if (v["name"].is_string())
         e.name = v["name"].get<crude_json::string>();
      if (v["manufacturer"].is_string())
         e.manufacturer = v["manufacturer"].get<crude_json::string>();
      if (v["identifier"].is_string())
         e.identifier = v["identifier"].get<crude_json::string>();
      if (v["acceptsNotes"].is_boolean())
         e.acceptsNotes = v["acceptsNotes"].get<crude_json::boolean>();
      if (!e.identifier.empty())
         mIndex.push_back(std::move(e));
   }
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
      entry["acceptsNotes"] = e.acceptsNotes;
      plugins.push_back(std::move(entry));
   }

   crude_json::value json = crude_json::object{};
   json["schema"] = (crude_json::number)kIndexSchemaVersion;
   json["plugins"] = std::move(plugins);
   json.save(path, 2);
}

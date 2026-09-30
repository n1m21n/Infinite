#pragma once

#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>

// Process-wide, path-keyed cache for decoded asset data (image pixels, parsed
// meshes, audio sample buffers). Undo/redo and multi-node delete/spawn
// respawn nodes from scratch, and every respawn re-decodes its source file
// from disk even though the bytes on disk haven't changed - see
// docs/plans/undo-delete-perf-prompt.md Part B. This cache lets a node ask
// "have I already decoded this exact file?" before paying for another decode.
//
// Keyed by path, validated by mtime+size (not a content hash - too expensive
// to be worth it for the case this exists to speed up) so an edit-in-place
// invalidates the entry. Bounded by a byte budget with LRU eviction so a
// session that touches many large assets doesn't grow this without limit.
//
// One AssetCache<T> instance per decoded-value type; nodes share the same
// instance across their whole process lifetime via a function-local static
// (see e.g. ImageSourceNode.cpp's GetImageCache()).
template <typename T>
class AssetCache
{
public:
   explicit AssetCache(size_t budgetBytes) : mBudgetBytes(budgetBytes) {}

   // Returns the cached value for `path` if present and still valid (mtime
   // and size unchanged since it was cached), else nullptr. The returned
   // pointer is only valid until the next Get()/Put() call on this cache.
   const T* Get(const std::string& path)
   {
      auto it = mEntries.find(path);
      if (it == mEntries.end())
         return nullptr;

      std::error_code ec;
      const auto mtime = std::filesystem::last_write_time(path, ec);
      const auto size = std::filesystem::file_size(path, ec);
      if (ec || mtime != it->second.mtime || size != it->second.size)
      {
         // Stale - drop it rather than serving decoded data for bytes that
         // no longer exist on disk.
         mLru.erase(it->second.lruIt);
         mEntries.erase(it);
         return nullptr;
      }

      mLru.erase(it->second.lruIt);
      mLru.push_front(path);
      it->second.lruIt = mLru.begin();
      return &it->second.value;
   }

   // Stores `value` (decoded from `path`, costing `bytes` towards the
   // budget) and evicts the least-recently-used entries until back under
   // budget. Overwrites any existing entry for the same path.
   void Put(const std::string& path, T value, size_t bytes)
   {
      std::error_code ec;
      const auto mtime = std::filesystem::last_write_time(path, ec);
      const auto size = std::filesystem::file_size(path, ec);
      if (ec)
         return; // file vanished between decode and cache - not cacheable

      auto existing = mEntries.find(path);
      if (existing != mEntries.end())
      {
         mTotalBytes -= existing->second.bytes;
         mLru.erase(existing->second.lruIt);
         mEntries.erase(existing);
      }

      mLru.push_front(path);
      Entry entry{ std::move(value), mtime, size, bytes, mLru.begin() };
      mEntries.emplace(path, std::move(entry));
      mTotalBytes += bytes;

      while (mTotalBytes > mBudgetBytes && !mLru.empty())
      {
         const std::string& lruPath = mLru.back();
         auto lruIt = mEntries.find(lruPath);
         if (lruIt != mEntries.end())
         {
            mTotalBytes -= lruIt->second.bytes;
            mEntries.erase(lruIt);
         }
         mLru.pop_back();
      }
   }

private:
   struct Entry
   {
      T value;
      std::filesystem::file_time_type mtime;
      uintmax_t size = 0;
      size_t bytes = 0;
      std::list<std::string>::iterator lruIt;
   };

   size_t mBudgetBytes;
   size_t mTotalBytes = 0;
   std::unordered_map<std::string, Entry> mEntries;
   std::list<std::string> mLru; // front = most recently used
};

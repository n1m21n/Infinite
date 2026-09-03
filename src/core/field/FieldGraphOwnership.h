#pragma once

#include <map>
#include <string>

// Field 'graph' domain (build step 10): the persisted key -> node-index
// ownership map a FieldGraphNode carries across compiles/undo/redo/save-
// load (doc §5.3.1, §5.6.3). Node index is NOT a stable identity by itself
// (it shifts on undo/redo and on delete-elsewhere) - RemapFieldGraphOwnership
// in main.cpp keeps this map's indices in sync whenever indices are
// renumbered; the map's own *keys* are the stable identity.
namespace Field
{
   class GraphOwnershipMap
   {
   public:
      bool Has(const std::string& key) const { return mEntries.find(key) != mEntries.end(); }
      int Get(const std::string& key) const
      {
         auto it = mEntries.find(key);
         return it != mEntries.end() ? it->second : -1;
      }
      void Set(const std::string& key, int nodeIndex) { mEntries[key] = nodeIndex; }
      void Erase(const std::string& key) { mEntries.erase(key); }

      const std::map<std::string, int>& Entries() const { return mEntries; }

      // Renumbers every stored index via `remap` (oldIndex -> newIndex);
      // an entry whose old index has no remap entry is dropped (the node it
      // pointed to no longer exists).
      void Remap(const std::map<int, int>& remap)
      {
         std::map<std::string, int> next;
         for (const auto& kv : mEntries)
         {
            auto it = remap.find(kv.second);
            if (it != remap.end())
               next[kv.first] = it->second;
         }
         mEntries = std::move(next);
      }

      // One space-separated "escapedKey=index" pair per entry - persisted as
      // a single Text param on the owning FieldGraphNode (doc §5.6.3: no new
      // Patch line kind).
      std::string ToText() const;
      static GraphOwnershipMap FromText(const std::string& text);

   private:
      std::map<std::string, int> mEntries;
   };
}

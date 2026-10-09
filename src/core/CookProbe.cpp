#include "CookProbe.h"
#include <unordered_map>

namespace CookProbe
{
   std::atomic<bool> gOn { false };

   namespace
   {
      struct Entry
      {
         double frameMs = 0.0;   // summed this frame
         float smoothed = 0.0f;
      };
      std::unordered_map<const INode*, Entry>& Map()
      {
         static std::unordered_map<const INode*, Entry> m;
         return m;
      }
   }

   void Add(const INode* node, double ms)
   {
      Map()[node].frameMs += ms < 0.0 ? 0.0 : ms;
   }

   float Ms(const INode* node)
   {
      auto it = Map().find(node);
      return it == Map().end() ? -1.0f : it->second.smoothed;
   }

   void EndFrame()
   {
      auto& m = Map();
      for (auto it = m.begin(); it != m.end();)
      {
         Entry& e = it->second;
         e.smoothed += 0.1f * ((float)e.frameMs - e.smoothed);
         e.frameMs = 0.0;
         if (e.smoothed < 0.0005f) it = m.erase(it);
         else ++it;
      }
   }

   void Clear()
   {
      Map().clear();
   }
}

#pragma once

// Turbo 0.72 (U3, not upstream): automation lanes for the Arrangement
// Timeline - one node parameter driven by keyframes over time, in the spirit
// of TiXL's keyframe animation and a DAW's automation lanes. Pure data and
// evaluation (no ImGui, no GL, no audio), so it is testable headless and can
// move to upstream as is. The timeline UI (ArrangeUi.inl) draws and edits
// the lanes; the patch saves them as `autolane` / `autokey` lines.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "arrange/ArrangeModel.h"

namespace ArrangeAuto
{
   // How a key's value travels to the next key.
   enum Interp { kStep = 0, kLinear, kSmooth, kInterpCount };

   struct Key
   {
      Arrange::Tick t = 0;
      float v = 0.0f;
      int interp = kLinear;
   };

   struct Lane
   {
      uint64_t id = 0;
      uint64_t nodeUid = 0;   // GraphNode::uid of the node it drives
      std::string param;      // the parameter's VisitParams name
      float lo = 0.0f;        // the value at the bottom of the lane
      float hi = 1.0f;        // the value at the top
      bool enabled = true;
      std::vector<Key> keys;  // sorted by t, one key per tick
   };

   inline void SortKeys(Lane& lane)
   {
      std::stable_sort(lane.keys.begin(), lane.keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
   }

   // Adds `k` (replacing a key already at its tick) and returns its index.
   inline int InsertKey(Lane& lane, const Key& k)
   {
      for (size_t i = 0; i < lane.keys.size(); i++)
         if (lane.keys[i].t == k.t)
         {
            lane.keys[i] = k;
            return (int)i;
         }
      lane.keys.push_back(k);
      SortKeys(lane);
      for (size_t i = 0; i < lane.keys.size(); i++)
         if (lane.keys[i].t == k.t)
            return (int)i;
      return -1;
   }

   // The lane's value at tick `t`: held before the first key and after the
   // last, interpolated between (the earlier key's interp decides how).
   // False when the lane has no key.
   inline bool Eval(const Lane& lane, Arrange::Tick t, float& out)
   {
      const std::vector<Key>& k = lane.keys;
      if (k.empty())
         return false;
      if (t <= k.front().t)
      {
         out = k.front().v;
         return true;
      }
      if (t >= k.back().t)
      {
         out = k.back().v;
         return true;
      }
      size_t i = 0;
      while (i + 1 < k.size() && k[i + 1].t <= t)
         i++;
      const Key& a = k[i];
      const Key& b = k[i + 1];
      float u = b.t > a.t ? (float)(t - a.t) / (float)(b.t - a.t) : 1.0f;
      switch (a.interp)
      {
         case kStep:
            out = a.v;
            return true;
         case kSmooth:
            u = u * u * (3.0f - 2.0f * u);
            break;
         default:
            break;
      }
      out = a.v + (b.v - a.v) * u;
      return true;
   }
}

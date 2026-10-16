#pragma once

// CPU working copies of PointZ data for the topology / simulation operators:
// every attribute as a float array, plus builders that make new points as
// copies or weighted mixes of old ones, carrying every attribute along.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "PopData.h"
#include "PopOps.h"

namespace pop
{
   struct Arrays
   {
      int count = 0;
      std::vector<std::string> names;
      std::vector<int> comps;
      std::vector<std::vector<float>> data;

      int Index(const std::string& name) const;
      // Adds the attribute (filled with its default) when missing; returns its index.
      int Ensure(const std::string& name, int comps);
      float* At(int attrib, int point) { return &data[(size_t)attrib][(size_t)point * comps[(size_t)attrib]]; }
      const float* At(int attrib, int point) const { return &data[(size_t)attrib][(size_t)point * comps[(size_t)attrib]]; }
      // Position shortcut (attribute 0 is always P after Load).
      float* Pos(int point) { return At(0, point); }
      const float* Pos(int point) const { return At(0, point); }
   };

   Arrays Load(const PopData& d);
   // A PopData from arrays + topology (attributes uploaded lazily).
   std::shared_ptr<PopData> Store(Arrays&& a, std::shared_ptr<PopTopology> topo);

   // Builds a new point set with the same attribute layout as `src`.
   class PointBuilder
   {
   public:
      explicit PointBuilder(const Arrays& src);
      int Copy(int i);
      int Lerp(int i, int j, float t);
      int Bary(int i, int j, int k, float u, float v); // (1-u-v) i + u j + v k
      int Weighted(const int* idx, const float* w, int n);
      // Overrides P of the last added point.
      void SetPos(int point, float x, float y, float z);
      void Set(int point, const std::string& name, const float* v, int comps);
      int Count() const { return mOut.count; }
      Arrays& Out() { return mOut; }
      Arrays Take() { return std::move(mOut); }

   private:
      int Begin();
      const Arrays& mSrc;
      Arrays mOut;
   };

   // Merges attribute layouts: every attribute of any part, defaults where missing.
   Arrays Concat(const std::vector<const Arrays*>& parts);

   // Runs fn(first, last) over [0, count) split across worker threads (one
   // range on the calling thread when count < minPerTask * 2). fn must only
   // write disjoint data per range.
   void ParallelRanges(int count, int minPerTask, const std::function<void(int, int)>& fn);

   // `pieces` back-to-back copies of src with its layout, in one allocation per
   // attribute (Copy / GLSL Copy: thousands of pieces, so no per-piece Arrays).
   Arrays Replicate(const Arrays& src, int pieces);

   // Topology of `pieces` back-to-back copies of a topology whose points come
   // `per` at a time. Kept in the operator's state and handed out again while
   // the source topology and the counts stay the same, so a Copy fed by an
   // animated template (new points every frame, same shape) uploads its index
   // buffers once instead of every frame.
   struct ReplicaTopoCache : OpState
   {
      TopologyPtr src;
      int pieces = -1;
      int per = -1;
      std::shared_ptr<PopTopology> topo;
   };
   std::shared_ptr<PopTopology> ReplicateTopo(const TopologyPtr& src, int pieces, int per, ReplicaTopoCache* cache);
}

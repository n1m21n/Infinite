#pragma once
// Per-node CPU cook time for the canvas "Cook times" overlay (View > Cook times). Main thread only.
// Off by default: Scope's constructor then costs one relaxed load and a branch, nothing else.
// Times are exclusive (a node's own work, not the upstream cooks it pulled) and smoothed over ~10 frames.
#include <atomic>
#include <chrono>

class INode;

namespace CookProbe
{
   extern std::atomic<bool> gOn;

   // Smoothed exclusive cook time of `node` in ms; < 0 when it was never timed.
   float Ms(const INode* node);
   // Fold this frame's sums into the smoothed values. Call once per frame, only while gOn.
   void EndFrame();
   void Clear();

   void Add(const INode* node, double ms);

   class Scope
   {
   public:
      explicit Scope(const INode* node)
      {
         if (!gOn.load(std::memory_order_relaxed))
            return;
         mNode = node;
         mParent = sTop;
         sTop = this;
         mStart = std::chrono::steady_clock::now();
      }
      ~Scope()
      {
         if (mNode == nullptr)
            return;
         const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mStart).count();
         sTop = mParent;
         if (mParent != nullptr)
            mParent->mChildMs += total;
         Add(mNode, total - mChildMs);
      }
      Scope(const Scope&) = delete;
      Scope& operator=(const Scope&) = delete;

   private:
      const INode* mNode = nullptr;
      Scope* mParent = nullptr;
      double mChildMs = 0.0;
      std::chrono::steady_clock::time_point mStart;
      static inline Scope* sTop = nullptr;
   };
}

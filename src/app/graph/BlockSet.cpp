// Set blocks: groups of nodes that are the sections of a live set. The Set list
// window steps through them in order; a block that is off stops its members
// cooking (pictures hold black, sound fades out). Membership is the group's own
// (whatever sits inside its frame), so a node is in one block at most and blocks
// save, load, copy and undo as the groups they are.
// The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this is a fresh
// implementation.
#include "app/AppShared.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "core/CategoryColors.h"
#include "core/Transport.h"
#include <algorithm>

namespace app
{
   bool gSetListOpen = false;
   bool gSetListFocused = false;

   namespace
   {
      GroupNode* sCurrent = nullptr;   // the block the last GO landed on
      int sPending = -1;               // index waiting for the launch grid
      int sStandby = -1;               // index Previous / Next picked for GO, -1 = the one after current
      double sPendingBeat = 0.0;

      bool Alive(GroupNode* g)
      {
         if (g == nullptr)
            return false;
         for (const GraphNode& gn : gNodes)
            if (gn.node.get() == g)
               return true;
         return false;
      }

      void ApplyGo(int index)
      {
         const std::vector<GroupNode*> blocks = SetBlocks();
         if (index < 0 || index >= (int)blocks.size())
            return;
         for (int i = 0; i < (int)blocks.size(); i++)
            blocks[(size_t)i]->on = i == index;
         sCurrent = blocks[(size_t)index];
         sPending = -1;
         sStandby = -1;
      }
   }

   std::vector<GroupNode*> SetBlocks()
   {
      std::vector<GroupNode*> blocks;
      for (const GraphNode& gn : gNodes)
         if (auto* g = dynamic_cast<GroupNode*>(gn.node.get()); g != nullptr && g->block)
            blocks.push_back(g);
      std::stable_sort(blocks.begin(), blocks.end(), [](GroupNode* a, GroupNode* b) { return a->order < b->order; });
      return blocks;
   }

   int SetBlockCurrent()
   {
      const std::vector<GroupNode*> blocks = SetBlocks();
      if (!Alive(sCurrent))
         sCurrent = nullptr;
      for (int i = 0; i < (int)blocks.size(); i++)
         if (blocks[(size_t)i] == sCurrent)
            return i;
      // No GO yet (a patch just loaded): the first block that is on.
      for (int i = 0; i < (int)blocks.size(); i++)
         if (blocks[(size_t)i]->on)
            return i;
      return -1;
   }

   int SetBlockPending() { return sPending; }

   int SetBlockNext()
   {
      const int n = (int)SetBlocks().size();
      if (n == 0)
         return -1;
      if (sPending >= 0)
         return sPending;
      if (sStandby >= 0 && sStandby < n)
         return sStandby;
      const int cur = SetBlockCurrent();
      return cur < 0 ? 0 : (cur + 1) % n;
   }

   void SetBlockGo(int index)
   {
      if (index < 0 || index >= (int)SetBlocks().size())
         return;
      Transport& tr = Transport::Instance();
      const int q = tr.LaunchQuantize();
      if (!tr.IsPlaying() || q <= MusicTime::kLaunchNone)
      {
         ApplyGo(index);
         return;
      }
      // Pressing GO again for the block already waiting cancels the wait.
      if (sPending == index)
      {
         sPending = -1;
         return;
      }
      sPending = index;
      sPendingBeat = MusicTime::NextLaunchBeat(tr.Beats(), q, 1e-3);
   }

   void SetBlockGoNext() { SetBlockGo(SetBlockNext()); }

   void SetBlockCue(int direction)
   {
      const int n = (int)SetBlocks().size();
      if (n == 0 || sPending >= 0)
         return;
      sStandby = ((SetBlockNext() + direction) % n + n) % n;
   }

   void SetBlockStep(int direction)
   {
      const int n = (int)SetBlocks().size();
      if (n == 0)
         return;
      const int from = sPending >= 0 ? sPending : SetBlockCurrent();
      SetBlockGo(from < 0 ? 0 : ((from + direction) % n + n) % n);
   }

   void SetBlockMove(int index, int direction)
   {
      std::vector<GroupNode*> blocks = SetBlocks();
      const int to = index + direction;
      if (index < 0 || to < 0 || index >= (int)blocks.size() || to >= (int)blocks.size())
         return;
      PushUndoCheckpoint("Reorder blocks");
      std::swap(blocks[(size_t)index], blocks[(size_t)to]);
      for (int i = 0; i < (int)blocks.size(); i++)
         blocks[(size_t)i]->order = i;
      sPending = -1;
      sStandby = -1;
      gPatchDirty = true;
   }

   void MakeBlock(GroupNode* g, bool block)
   {
      if (g == nullptr || g->block == block)
         return;
      PushUndoCheckpoint(block ? "Make block" : "Remove block");
      g->block = block;
      g->on = true;
      if (block)
      {
         const std::vector<GroupNode*> blocks = SetBlocks();
         int order = 0;
         for (GroupNode* other : blocks)
            if (other != g)
               order = std::max(order, other->order + 1);
         g->order = order;
         // One category colour per block, in turn, so neighbours read apart.
         const std::vector<std::string>& cats = CategoryColors::CategoryNames();
         const CategoryColors::Color& c = CategoryColors::ColorFor(cats[(size_t)order % cats.size()]);
         g->color[0] = c.r;
         g->color[1] = c.g;
         g->color[2] = c.b;
         if (g->label == "Group")
            g->label = "Block " + std::to_string((int)blocks.size());
         gSetListOpen = true;
      }
      sPending = -1;
      gPatchDirty = true;
   }

   void BlockSetUpdate()
   {
      Transport& tr = Transport::Instance();
      if (sPending >= 0 && (!tr.IsPlaying() || tr.Beats() >= sPendingBeat - 1e-6))
         ApplyGo(sPending);

      const double secondsPerBar = MusicTime::BarsToBeats(1.0) * 60.0 / std::max(1.0f, tr.Tempo());
      for (GraphNode& gn : gNodes)
      {
         if (!gn.node || dynamic_cast<GroupNode*>(gn.node.get()) != nullptr)
            continue;
         GroupNode* owner = GroupOwning(gn.index);
         const bool inBlock = owner != nullptr && owner->block;
         const bool off = inBlock && !owner->on;
         gn.node->blockOff = off;
         if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
         {
            const float fadeSeconds = inBlock && owner->fade > 0 ? (float)(owner->fade * secondsPerBar) : 0.01f;
            an->blockFadeSeconds.store(fadeSeconds, std::memory_order_relaxed);
            an->blockGainTarget.store(off ? 0.0f : 1.0f, std::memory_order_relaxed);
         }
      }
   }
}

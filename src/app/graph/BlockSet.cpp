// Set blocks: groups of nodes that are the sections of a patch in a live set.
// The Show Set window lists the playing patch's blocks under it; Next / Previous
// (PageDown / PageUp, OSC) step through the blocks first, then on to the next
// patch, which starts on its first block. A switch lands on Show Set's quantize
// grid; a block that is off stops its members cooking (pictures hold black) and
// its sound fades out over Show Set's fade time. Membership is the group's own
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
   namespace
   {
      GroupNode* sCurrent = nullptr;   // the block the last switch landed on
      int sPending = -1;               // index waiting for the grid
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
      // No switch yet (a patch just opened): the first block that is on.
      for (int i = 0; i < (int)blocks.size(); i++)
         if (blocks[(size_t)i]->on)
            return i;
      return -1;
   }

   int SetBlockPending() { return sPending; }

   void SetBlockGo(int index)
   {
      if (ShowPanicLatched() || index < 0 || index >= (int)SetBlocks().size())
         return;
      Transport& tr = Transport::Instance();
      const int q = ShowSetQuantize();
      if (!tr.IsPlaying() || q <= MusicTime::kLaunchNone)
      {
         ApplyGo(index);
         return;
      }
      // Asking again for the block already waiting cancels the wait.
      if (sPending == index)
      {
         sPending = -1;
         return;
      }
      sPending = index;
      sPendingBeat = MusicTime::NextLaunchBeat(tr.Beats(), q, 1e-3);
   }

   bool SetBlockStep(int direction)
   {
      const int n = (int)SetBlocks().size();
      if (n == 0)
         return false;
      const int from = sPending >= 0 ? sPending : SetBlockCurrent();
      const int to = from < 0 ? (direction > 0 ? 0 : -1) : from + direction;
      if (to < 0 || to >= n)
         return false;   // past either end: the show set moves to the next / previous patch
      SetBlockGo(to);
      return true;
   }

   void SetBlockLandFirst()
   {
      sCurrent = nullptr;
      sPending = -1;
      ApplyGo(0);
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
         gShowSetOpen = true;   // where blocks are played from
      }
      sPending = -1;
      gPatchDirty = true;
   }

   void BlockSetUpdate()
   {
      Transport& tr = Transport::Instance();
      if (sPending >= 0 && (!tr.IsPlaying() || tr.Beats() >= sPendingBeat - 1e-6))
         ApplyGo(sPending);

      const float fadeSeconds = std::max(0.01f, ShowSetFadeMs() * 0.001f);
      for (GraphNode& gn : gNodes)
      {
         if (!gn.node || dynamic_cast<GroupNode*>(gn.node.get()) != nullptr)
            continue;
         GroupNode* owner = GroupOwning(gn.index);
         const bool off = owner != nullptr && owner->block && !owner->on;
         gn.node->blockOff = off;
         if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
         {
            an->blockFadeSeconds.store(fadeSeconds, std::memory_order_relaxed);
            an->blockGainTarget.store(off ? 0.0f : 1.0f, std::memory_order_relaxed);
            // A node the patch just made (open, undo, a show set switch) starts where its
            // block is, rather than fading out from full.
            if (!gn.node->blockSeen)
            {
               an->blockSnap.store(true, std::memory_order_release);   // after the target it snaps to
               gn.node->blockSeen = true;
            }
         }
      }
   }
}

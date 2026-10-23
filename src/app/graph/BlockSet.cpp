// Set blocks: a group marked as a block gets a power button in its frame.
// Switched off, its members stop cooking (pictures hold black) and their sound
// fades out; switched back on, it fades in. Membership is the group's own
// (whatever sits inside its frame), so a node is in one block at most and blocks
// save, load, copy and undo as the groups they are.
// The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this is a fresh
// implementation.
#include "app/AppShared.h"
#include "audio/AudioNode.h"
#include "core/CategoryColors.h"
#include <algorithm>

namespace app
{
   namespace
   {
      // Long enough not to click, short enough to read as a switch.
      constexpr float kBlockFadeSeconds = 0.1f;
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
      }
      gPatchDirty = true;
   }

   void BlockSetUpdate()
   {
      for (GraphNode& gn : gNodes)
      {
         if (!gn.node || dynamic_cast<GroupNode*>(gn.node.get()) != nullptr)
            continue;
         GroupNode* owner = GroupOwning(gn.index);
         const bool off = owner != nullptr && owner->block && !owner->on;
         gn.node->blockOff = off;
         if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
         {
            an->blockFadeSeconds.store(kBlockFadeSeconds, std::memory_order_relaxed);
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

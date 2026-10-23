// Group power: every group has a power button in its frame. Switched off, its
// members stop cooking (pictures hold black) and their sound fades out;
// switched back on, it fades in. Membership is the group's own (whatever sits
// inside its frame), and the switch saves, loads, copies and undoes with the
// group.
// The idea comes from Ricardo Palmieri's Infinite-Turbo fork; this is a fresh
// implementation.
#include "app/AppShared.h"
#include "audio/AudioNode.h"

namespace app
{
   namespace
   {
      // Long enough not to click, short enough to read as a switch.
      constexpr float kGroupFadeSeconds = 0.1f;
   }

   void GroupPowerUpdate()
   {
      for (GraphNode& gn : gNodes)
      {
         if (!gn.node || dynamic_cast<GroupNode*>(gn.node.get()) != nullptr)
            continue;
         GroupNode* owner = GroupOwning(gn.index);
         const bool off = owner != nullptr && !owner->on;
         gn.node->blockOff = off;
         if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
         {
            an->blockFadeSeconds.store(kGroupFadeSeconds, std::memory_order_relaxed);
            an->blockGainTarget.store(off ? 0.0f : 1.0f, std::memory_order_relaxed);
            // A node the patch just made (open, undo, a show set switch) starts where its
            // group is, rather than fading out from full.
            if (!gn.node->blockSeen)
            {
               an->blockSnap.store(true, std::memory_order_release);   // after the target it snaps to
               gn.node->blockSeen = true;
            }
         }
      }
   }
}

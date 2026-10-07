// Node removal and graph hosts (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   void RemoveNodeByIndex(int index)
   {
      GraphNode* victim = FindNodeByIndex(index);
      if (victim == nullptr)
         return;
      PushUndoCheckpoint();
      Modulation::Instance().UnbindAllFor(index);
      PaletteBinding::Instance().UnbindAllFor(index);
      // Third line, same reason as the two above: without it a recording
      // outlives its node, and node indices are reused (gNextIndex restarts
      // on NewPatch, and Undo respawns everything), so a stale key can start
      // driving an unrelated param.
      GestureRecorder::Instance().ClearForNode(index);
      // Fourth: a clip points at this node. The clip is NOT deleted - it goes
      // offline (srcUid 0), draws "Unassigned" with a hatch, and is silent
      // and invisible until something is assigned to it. Deleting it instead
      // is what used to make "delete a node, undo" silently lose the
      // arrangement around it, since the clip's own edits had no way back.
      //
      // The link comes back through the undo entry PushUndoCheckpoint pushed
      // above: it snapshots gArrange with srcUid intact, and the node's uid
      // is persisted, so undo restores both ends (WP5 owner decision).
      // ClearSource bumps revision when it touched a clip; the topology
      // rebuild at the end of this function records it.
      Arrange::ClearSource(gArrange, victim->uid);
      // A timeline gesture in flight rebuilds gArrange from its snapshot;
      // clear the source there too or the next drag frame re-links the clip
      // to a node that no longer exists.
      if (gArrangeGestureOpen)
         Arrange::ClearSource(gArrangeGestureBefore, victim->uid);
      ForgetDiscreteSlots(index);
      gModHistory.erase(index);
      DisconnectAllTo(victim->node.get());
      // A deleted Group's membership set must go with it - otherwise its
      // GroupNode* stays around as a dangling map key that a future
      // allocation could reuse, silently handing a stranger's members to a
      // brand new group. Every other group also drops the index in case it
      // was one of its members.
      if (auto* deadGroup = dynamic_cast<GroupNode*>(victim->node.get()))
         gGroupMembers.erase(deadGroup);
      for (auto& entry : gGroupMembers)
         entry.second.erase(index);

      // Don't destroy the node/viewport here: this frame's ImGui draw list may
      // already contain AddImage() calls (queued while drawing this node's
      // body/mini-viewport earlier this frame) referencing their GL output
      // textures. Deleting those textures now, before
      // ImGui_ImplOpenGL3_RenderDrawData() actually submits the draw list at
      // the end of the frame, frees a GL name that the pending draw commands
      // still reference - producing a one-frame flash of garbage/reused-texture
      // content. Retire ownership instead; actual teardown is gated on both
      // the next frame's presentation AND the audio thread confirming it's
      // done with this node - see gRetiredNodes' declaration and its drain
      // next to glfwPollEvents().
      if (auto vp = gNodeViewports.extract(index); !vp.empty())
         gRetiredViewports.push_back(std::move(vp));
      // Read before RebuildAudioTopology() below republishes: this is "the
      // last topology generation that can still reach victim's AudioNode".
      gRetiredNodes.push_back({ std::move(victim->node), AudioEngine::Instance().CurrentGeneration() });

      gNodes.erase(std::remove_if(gNodes.begin(), gNodes.end(),
                                  [index](const GraphNode& g) { return g.index == index; }),
                   gNodes.end());
      InvalidateNodeByUid(); // every node after the victim moved down a slot

      // After, not before: victim is still in gNodes up to the erase() just
      // above, and rebuilding earlier would publish a topology that can
      // include the about-to-be-freed node's own AudioNode - exactly the
      // "pointer to a freed node crashes on the next cook" hazard
      // DisconnectAllTo's own comment warns about, just on the audio thread.
      RebuildAudioTopology();
   }


   bool IsKernelDrivenParam(int nodeIndex, int paramIndex)
   {
      GraphNode* gn = FindNodeByIndex(nodeIndex);
      if (!gn || !gn->node) return false;
      struct ParamNameVisitor : public ParamVisitor {
         int target;
         int cur = 0;
         std::string name;
         ParamNameVisitor(int t) : target(t) {}
         void Float(const char* n, float&) override { if (cur++ == target) name = n; }
         void Int(const char* n, int&) override { if (cur++ == target) name = n; }
         void Bool(const char* n, bool&) override { if (cur++ == target) name = n; }
         void Text(const char* n, std::string&) override { if (cur++ == target) name = n; }
         void Color(const char* n, float[3]) override { if (cur++ == target) name = n; }
      } v(paramIndex);
      gn->node->VisitParams(v);
      if (v.name.empty()) return false;

      for (const GraphNode& node : gNodes)
      {
         if (auto* fgn = dynamic_cast<FieldGraphNode*>(node.node.get()))
         {
            if (fgn->DrivesParam(nodeIndex, v.name))
               return true;
         }
      }
      return false;
   }


   bool CanBindModulation(int dstNodeIndex, int paramIndex)
   {
      return !IsKernelDrivenParam(dstNodeIndex, paramIndex);
   }
}

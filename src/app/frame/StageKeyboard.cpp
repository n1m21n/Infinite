// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawKeyboard(FrameCtx& fc)
{
   ImGuiIO& io = ImGui::GetIO();
   auto& searchBuf = fc.searchBuf;
   auto& searchJustOpened = fc.searchJustOpened;
   auto& searchPopupCentered = fc.searchPopupCentered;
   auto& searchPopupOpen = fc.searchPopupOpen;
   auto& searchRequestClose = fc.searchRequestClose;
   auto& clipboard = fc.clipboard;
   auto& clipboardSources = fc.clipboardSources;
   auto& clipboardOrigIndex = fc.clipboardOrigIndex;
   auto& clipboardOrigGroup = fc.clipboardOrigGroup;
   auto& clipboardCluster = fc.clipboardCluster;
   auto& typing = fc.typing;
   auto& cmdOrCtrl = fc.cmdOrCtrl;


      // "/" drops a comment under the pointer and puts the caret straight into
      // it, so annotating a patch is one keystroke and then typing. Not gated on
      // Shift, so "?" does not leave a stray comment behind, and not on a
      // modifier, so Cmd-/ stays free for a binding later.
      const bool doAddComment = gRequestAddComment || (!typing && gCommentEdit.target == nullptr && !cmdOrCtrl && !io.KeyShift &&
                                                      ImGui::IsKeyPressed(ImGuiKey_Slash, false));
      gRequestAddComment = false;
      if (doAddComment)
      {
         // The pointer is only meaningful over the canvas; anywhere else (the
         // node panel, off the window entirely) the middle of the view is the
         // only sensible place for it.
         const ImVec2 mouse = ImGui::GetMousePos();
         const bool overGraph = mouse.x >= gGraphScreenTL.x &&
                                mouse.y >= gGraphScreenTL.y &&
                                mouse.x <= gGraphScreenTL.x + gGraphScreenSize.x &&
                                mouse.y <= gGraphScreenTL.y + gGraphScreenSize.y;
         const ImVec2 at = overGraph ? ed::ScreenToCanvas(mouse) : gViewCenterCanvas;
         if (GraphNode* gn = SpawnNode("Comment", "Compositing", at.x, at.y))
         {
            gCommentEdit.target = static_cast<CommentNode*>(gn->node.get());
            gCommentEdit.justOpened = true;
            // The '/' itself is already in the queue for this frame; without
            // this it lands in the note that is about to take the keyboard and
            // every comment starts with a slash.
            io.InputQueueCharacters.resize(0);
         }
      }

      // Space toggles play/pause on the transport, mirroring the Play/Pause
      // button, so the timeline can be started or paused without reaching
      // for the mouse.
      if (!typing && !cmdOrCtrl && !io.KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_Space, false))
         Transport::Instance().TogglePlay();

      // ---- Shift+<letter> canvas shortcuts ----
      // One family, all reachable with the left hand while the right stays on
      // the trackpad: V/X act on the selection, M/N/H/K are canvas-wide
      // toggles. All of them are gated on Shift alone (never Cmd/Ctrl), so
      // they can't collide with the system/menu bindings above.
      const bool shiftOnly = !cmdOrCtrl && io.KeyShift;

      // Shift+V:
      // - When one or more eligible nodes are selected: opens the viewport panel
      //   with those nodes (adds missing cards and ensures panel is open; if the panel
      //   is already open and all selected cards are already docked, toggles them off).
      // - When no node is selected (e.g. after clicking on blank canvas): toggles the
      //   whole viewport panel view on / off.
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_V, false))
      {
         const int count = ed::GetSelectedObjectCount();
         std::vector<int> eligible;
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               // Same gate as the "Open in viewport panel" context-menu entry,
               // so the keyboard can never open a card the menu wouldn't -
               // modulators, cameras/lights, comments and audio nodes have no
               // texture to show and would render an empty box.
               if (gn != nullptr && CanShowInViewportPanel(*gn))
                  eligible.push_back(gn->index);
            }
         }

         if (!eligible.empty())
         {
            if (!gViewportPanelOpen)
            {
               for (int idx : eligible)
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx) ==
                      gViewportPanelNodes.end())
                     gViewportPanelNodes.push_back(idx);
               }
               gViewportPanelOpen = true;
            }
            else
            {
               bool allOpen = true;
               for (int idx : eligible)
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx) ==
                      gViewportPanelNodes.end())
                     allOpen = false;
               }

               if (allOpen)
               {
                  for (int idx : eligible)
                  {
                     auto it = std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx);
                     if (it != gViewportPanelNodes.end())
                        gViewportPanelNodes.erase(it);
                  }
                  if (gViewportPanelNodes.empty())
                     gViewportPanelOpen = false;
               }
               else
               {
                  for (int idx : eligible)
                  {
                     if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx) ==
                         gViewportPanelNodes.end())
                        gViewportPanelNodes.push_back(idx);
                  }
                  gViewportPanelOpen = true;
               }
            }
         }
         else
         {
            gViewportPanelOpen = !gViewportPanelOpen;
         }
      }

      // Shift+M: the docked modulation matrix, same panel the modulator
      // context menu's "Show modulation matrix" opens.
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_M, false))
         gModMatrixOpen = !gModMatrixOpen;

      // Shift+P: the docked performance matrix
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_P, false))
         gPerfPanelOpen = !gPerfPanelOpen;

      // Shift+T: the docked arrangement timeline
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_T, false))
         gArrangePanelOpen = !gArrangePanelOpen;

      // Shift+Y: fit view to content, replacing the old menu-only entry
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_Y, false))
         gRequestFitView = true;

      // Shift+N: the type-to-filter node picker, without having to find empty
      // canvas to double-click. Pressed again it closes, so the same key gets
      // you back out. The close has to be deferred into the popup body (see
      // searchRequestClose) and is deliberately NOT gated on `typing`, since
      // the picker's own text field owns the keyboard the whole time it's up.
      // Safe to claim: the picker lowercases both query and candidate names,
      // so a capital letter is never needed to find a node. Gated off while
      // the arrangement panel has focus - Shift+N there is "add track"
      // (DrawArrangePanelContent's own keyboard block), a different action
      // that must not also pop the canvas node picker open underneath it.
      const bool doAddNode = gRequestAddNode ||
         (!cmdOrCtrl && io.KeyShift && (!typing || searchPopupOpen) && gCommentEdit.target == nullptr &&
          ImGui::IsKeyPressed(ImGuiKey_N, false));
      gRequestAddNode = false;
      if (doAddNode)
      {
         if (searchPopupOpen)
         {
            searchRequestClose = true;
         }
         else
         {
            // Spawn node and search panel in the middle of the view/screen
            gSpawnPos = gViewCenterCanvas;
            gLinkDragSourcePin = -1;
            gLinkDragSuggestions.clear();
            searchBuf[0] = '\0';
            searchJustOpened = true;
            searchPopupCentered = true;
            ImGui::OpenPopup("search");
         }
         // The 'N' is already queued as a character for this frame; without
         // this every picker opened from the keyboard starts pre-filled with
         // it (and the closing press would type into whatever takes focus next).
         io.InputQueueCharacters.resize(0);
      }

      // Shift+H: hide/show node params. With a selection it toggles just those
      // nodes; with nothing selected it acts on the whole canvas, mirroring the
      // menu's "Show all params"/"Hide all params" pair as one key. Audio nodes
      // are skipped - their params are always visible by design (see the
      // context menu's identical carve-out), so they must not count toward
      // "is anything showing?" either, or a canvas of audio nodes would make
      // the first press a no-op.
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_H, false))
      {
         std::vector<GraphNode*> targets;
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn != nullptr && !IsAudioBodyNode(gn->node.get()))
                  targets.push_back(gn);
            }
         }
         else
         {
            for (GraphNode& gn : gNodes)
               if (!IsAudioBodyNode(gn.node.get()))
                  targets.push_back(&gn);
         }

         if (!targets.empty())
         {
            // "Anything still showing" -> hide, else show. A mixed selection
            // therefore collapses first and expands second, which is the
            // behaviour that reads as a toggle rather than a shuffle.
            bool anyShown = false;
            for (GraphNode* gn : targets)
               anyShown = anyShown || gn->showParams;
            for (GraphNode* gn : targets)
               gn->showParams = !anyShown;
         }
      }

      // Shift+K: the audio engine, same on/off the toolbar's Start/Stop Audio
      // button drives - routed through StartAudioEngine rather than
      // AudioEngine::Start() so a patch loaded while the engine was off gets
      // re-prepared at the device's rate (see StartAudioEngine's comment).
      if (!typing && !gArrangeFocused && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_K, false))
      {
         if (AudioEngine::Instance().SampleRate() > 0.0)
         {
            AudioEngine::Instance().Stop();
         }
         else
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
               fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
         }
      }

      // X on its own deletes selected cables and nothing else, so a mis-aimed
      // click on a node can't silently take the node with it. Shift+X is the
      // broader "delete what's selected", handled by the Delete/Backspace
      // block below.
      if (!typing && !cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false))
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::LinkId> selLinks(count);
            const int linkCount = ed::GetSelectedLinks(selLinks.data(), count);
            if (linkCount > 0)
            {
               // One checkpoint for the batch - DisconnectLinkById pushes its
               // own otherwise, so undoing a multi-cable delete would claw
               // back one cable per press (same fix as the block below).
               PushUndoCheckpoint();
               gSuppressUndoCheckpoints = true;
               for (int i = 0; i < linkCount; i++)
               {
                  DisconnectLinkById((int)selLinks[i].Get());
                  ed::DeleteLink(selLinks[i]);
               }
               gSuppressUndoCheckpoints = false;
               ed::ClearSelection();
            }
         }
      }

      // gPerfMatrixFocused: the performance matrix is in edit mode and has
      // keyboard focus, so these keys belong to its controls, not to the graph.
      // It is set from the matrix window, which draws later in the frame than
      // this block, so a closed matrix would otherwise keep the last value it
      // wrote.
      if (!gPerfPanelOpen)
      {
         gPerfMatrixClaimedKeys = false;
         gPerfMatrixFocused = false;
      }
      else
      {
         gPerfMatrixFocused = gPerfMatrixClaimedKeys && gPerfEditMode;
      }

      // Arrangement Timeline focus guard: when arrangement timeline owns the
      // keyboard focus, keystrokes (Delete, Backspace, Cmd+C, Cmd+V, Cmd+D)
      // belong strictly to the timeline clips, not the graph canvas.
      if (!gArrangePanelOpen)
      {
         gArrangeClaimedKeys = false;
         gArrangeFocused = false;
      }
      else
      {
         gArrangeFocused = gArrangeClaimedKeys;
      }

      const bool doDelete = gRequestDelete ||
         (!typing && !gPerfMatrixFocused && !gArrangeFocused && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
                      (shiftOnly && ImGui::IsKeyPressed(ImGuiKey_X, false))));
      gRequestDelete = false;
      if (doDelete)
      {
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            std::vector<ed::LinkId> selLinks(count);
            int linkCount = ed::GetSelectedLinks(selLinks.data(), count);

            // A selected group takes its members with it - otherwise "delete"
            // on a group would silently do no more than an ungroup.
            std::set<int> toDelete;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toDelete.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toDelete.insert(it->second.begin(), it->second.end());
               }
            }

            // One checkpoint for the whole batch: RemoveNodeByIndex (and the
            // per-link path below) each push their own by default, which
            // would otherwise turn "delete this group" into a checkpoint per
            // node - so a single Undo only clawed back the last one removed
            // instead of the whole cluster.
            if (linkCount > 0 || !toDelete.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            // One topology rebuild for the whole batch too - RemoveNodeByIndex
            // rebuilds on every call by default, which turned deleting an
            // N-node group into N full audio-graph rebuilds (each re-preparing
            // every audio node in the order, resetting reverb tails/delay
            // lines along the way).
            gDeferAudioRebuild = true;

            for (int i = 0; i < linkCount; i++)
            {
               DisconnectLinkById((int)selLinks[i].Get());
               ed::DeleteLink(selLinks[i]);
            }
            for (int index : toDelete)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }

            // After the loop, not before: RemoveNodeByIndex's own ordering
            // rule (rebuild only after the victim is erased from gNodes)
            // still has to hold for every victim, and deferring the rebuild
            // to here preserves that - every victim is already erased by now.
            gDeferAudioRebuild = false;
            RebuildAudioTopology();
            gSuppressUndoCheckpoints = false;
            ed::ClearSelection();
         }
      }

      // Shift+D (or Cmd/Ctrl+D) duplicates whatever is selected without
      // touching the clipboard.
      const bool doDuplicate = gRequestDuplicate ||
         (!typing && !gPerfMatrixFocused && !gArrangeFocused && (io.KeyShift || cmdOrCtrl) && ImGui::IsKeyPressed(ImGuiKey_D, false));
      gRequestDuplicate = false;
      if (doDuplicate)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // A selected group brings its members along, even if they are not
            // individually part of the editor's own selection set.
            std::set<int> toDup;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toDup.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toDup.insert(it->second.begin(), it->second.end());
               }
            }

            // Captured now, against the still-live selection, before any
            // SpawnNode call below can reallocate gNodes.
            ClusterClipboard dupCluster;
            CaptureClusterLinks(toDup, dupCluster);

            // Resolve everything first: SpawnNode can reallocate gNodes.
            const ImVec2 off = ClusterOffset(toDup);
            struct DupItem
            {
               std::string type; std::string category; INode* src; ImVec2 pos; bool params;
               bool miniViewport;
               bool advancedParams;
               int origIndex; int origGroup;
            };
            std::vector<DupItem> items;
            for (int index : toDup)
            {
               if (GraphNode* gn = FindNodeByIndex(index))
               {
                  const ImVec2 p = ed::GetNodePosition(gn->NodeId());
                  items.push_back({ gn->typeName, gn->category, gn->node.get(),
                                    ImVec2(p.x + off.x, p.y + off.y), gn->showParams,
                                    gn->showMiniViewport, gn->showAdvancedParams,
                                    gn->index, IndexOfGroupNode(GroupOwning(gn->index)) });
               }
            }

            ed::ClearSelection();
            // One checkpoint for the whole duplicate: SpawnNode pushes its
            // own by default, which would otherwise scatter a multi-node
            // duplicate across several undo steps instead of one.
            if (!items.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            std::map<int, GraphNode*> newByOrig;
            for (const DupItem& item : items)
            {
               if (GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y))
               {
                  CopyParams(copy->node.get(), item.src);
                  if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
                     rn->seed = RandomNode::NextSeed();
                  // A duplicate's ownership map, just copied verbatim by
                  // CopyParams, still points at the ORIGINAL's live nodes -
                  // the copy's identity must diverge (doc §5.7.3) so its
                  // next Regenerate treats every emit() as fresh (mount, not
                  // "I already own that node") instead of fighting the
                  // original over the same indices.
                  if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
                  {
                     fgn->SetUid(FieldGraphNode::NewUid());
                     fgn->Ownership() = Field::GraphOwnershipMap();
                     fgn->ownershipText.clear();
                  }
                  copy->showParams = item.params;
                  copy->showMiniViewport = item.miniViewport;
                  copy->showAdvancedParams = item.advancedParams;
                  newByOrig[item.origIndex] = copy;
                  gPendingSelect.push_back(copy->NodeId());
               }
            }
            // Re-establish group membership among the duplicates.
            for (const DupItem& item : items)
            {
               if (item.origGroup < 0)
                  continue;
               auto groupIt = newByOrig.find(item.origGroup);
               auto memberIt = newByOrig.find(item.origIndex);
               if (groupIt == newByOrig.end() || memberIt == newByOrig.end())
                  continue;
               if (auto* g = dynamic_cast<GroupNode*>(groupIt->second->node.get()))
                  gGroupMembers[g].insert(memberIt->second->index);
            }
            ApplyClusterLinks(newByOrig, dupCluster);
            gSuppressUndoCheckpoints = false;
         }
      }

      // !gArrangeFocused: Cmd+G / Cmd+Shift+G group timeline clips while the
      // Arrangement panel has focus, and must not also group canvas nodes.
      const bool doGroup =
         gRequestGroup ||
         (!typing && !gArrangeFocused && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      const bool doUngroup =
         gRequestUngroup ||
         (!typing && !gArrangeFocused && cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      gRequestGroup = false;
      gRequestUngroup = false;

      // Cmd/Ctrl+Shift+G is member-scoped: selecting a group's header
      // dissolves that whole group (nodes go away as members, the group node
      // itself goes away via RemoveNodeByIndex, freeing them for another
      // group to adopt), but selecting one or more ordinary member nodes
      // detaches only those nodes from their group - same as each member's
      // own right-click "Ungroup" - leaving the rest of the cluster intact.
      // A selection can mix both kinds at once (e.g. one whole group plus a
      // lone member of a different group), so both paths run in the same
      // pass.
      if (doUngroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // Resolved up front: RemoveNodeByIndex erases from gNodes, which
            // invalidates every GraphNode* taken before it.
            std::set<int> doomedGroups;
            std::vector<int> detachMembers;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* sel = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (sel == nullptr)
                  continue;
               if (dynamic_cast<GroupNode*>(sel->node.get()) != nullptr)
               {
                  doomedGroups.insert(sel->index);
                  continue;
               }
               if (GroupNode* owner = GroupOwning(sel->index))
               {
                  // A member whose owning group is also directly selected
                  // (and about to dissolve entirely) doesn't need its own
                  // detach - that would just nudge it right before the group
                  // node vanishes anyway.
                  bool ownerAlsoSelected = false;
                  for (GraphNode& g : gNodes)
                  {
                     if (g.node.get() == owner && doomedGroups.count(g.index))
                        ownerAlsoSelected = true;
                  }
                  if (!ownerAlsoSelected)
                     detachMembers.push_back(sel->index);
               }
            }

            const bool anyWork = !doomedGroups.empty() || !detachMembers.empty();
            // One checkpoint for the whole batch - see the Delete-key handler
            // above for why (several groups/members touched at once would
            // otherwise leave Undo only able to claw back the last one).
            if (anyWork)
               PushUndoCheckpoint();

            for (int memberIndex : detachMembers)
            {
               GraphNode* gn = FindNodeByIndex(memberIndex);
               if (gn == nullptr)
                  continue;
               GroupNode* owner = GroupOwning(memberIndex);
               if (owner == nullptr)
                  continue;
               gGroupMembers[owner].erase(memberIndex);
               // Membership here is purely geometric - anything fully inside
               // the group's box gets adopted right back in next frame.
               // Nudging the node just past the box's bottom edge is what
               // makes removing it actually stick (same as the per-node
               // right-click "Ungroup" above).
               if (int ownerIndex = IndexOfGroupNode(owner); ownerIndex >= 0)
               {
                  if (GraphNode* ownerGn = FindNodeByIndex(ownerIndex))
                  {
                     const ImVec2 gp = ed::GetNodePosition(ownerGn->NodeId());
                     const ImVec2 gs = ed::GetNodeSize(ownerGn->NodeId());
                     const ImVec2 mp = ed::GetNodePosition(gn->NodeId());
                     ed::SetNodePosition(gn->NodeId(), ImVec2(mp.x, gp.y + gs.y + 40.0f));
                  }
               }
            }

            gSuppressUndoCheckpoints = true;
            for (int index : doomedGroups)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }
            gSuppressUndoCheckpoints = false;
            if (anyWork)
               ed::ClearSelection();
         }
      }

      // Cmd/Ctrl+G wraps the current selection in a Group node sized to its
      // bounding box, so a cluster of existing nodes sticks together and
      // drags as one without having to hand-drag the group's edges around
      // them first.
      if (doGroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            bool any = false;
            ImVec2 bmin(0.0f, 0.0f), bmax(0.0f, 0.0f);
            std::set<int> picked;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* member = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (member == nullptr || dynamic_cast<GroupNode*>(member->node.get()) != nullptr)
                  continue; // grouping a group isn't supported
               // Membership is exclusive, so a node that already belongs
               // somewhere stays where it is rather than being pulled into a
               // second group that would then fight the first one for it.
               if (GroupOwning(member->index) != nullptr)
                  continue;
               picked.insert(member->index);
               const ImVec2 p = ed::GetNodePosition(member->NodeId());
               const ImVec2 s = ed::GetNodeSize(member->NodeId());
               if (!any)
               {
                  bmin = p;
                  bmax = ImVec2(p.x + s.x, p.y + s.y);
                  any = true;
               }
               else
               {
                  bmin.x = std::min(bmin.x, p.x);
                  bmin.y = std::min(bmin.y, p.y);
                  bmax.x = std::max(bmax.x, p.x + s.x);
                  bmax.y = std::max(bmax.y, p.y + s.y);
               }
            }

            if (any)
            {
               const float kPad = 32.0f;
               const float kHeader = 24.0f;
               const float gx = bmin.x - kPad;
               const float gy = bmin.y - kPad - kHeader;
               const float gw = (bmax.x - bmin.x) + kPad * 2.0f;
               const float gh = (bmax.y - bmin.y) + kPad * 2.0f + kHeader;

               PushUndoCheckpoint();
               gSuppressUndoCheckpoints = true;
               if (GraphNode* ggn = SpawnNode("Group", "Compositing", gx, gy))
               {
                  if (auto* grp = dynamic_cast<GroupNode*>(ggn->node.get()))
                  {
                     grp->width = gw;
                     grp->height = gh;
                     gGroupMembers[grp] = picked;
                  }
                  ed::ClearSelection();
                  gPendingSelect.push_back(ggn->NodeId());
               }
               gSuppressUndoCheckpoints = false;
            }
         }
      }

      // !gPerfMatrixFocused for the same reason Copy/Paste check it: while the
      // performance matrix owns the keyboard, a bare letter belongs to that
      // panel, not to the canvas selection behind it.
      const bool doBypass =
         gRequestBypass ||
         (!typing && !gPerfMatrixFocused && !gArrangeFocused && !cmdOrCtrl && !io.KeyShift && !io.KeyAlt &&
          ImGui::IsKeyPressed(ImGuiKey_B, false));
      gRequestBypass = false;

      if (doBypass)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            if (nodeCount > 0)
            {
               PushUndoCheckpoint();
               bool needsAudioRebuild = false;
               for (int i = 0; i < nodeCount; i++)
               {
                  GraphNode* sel = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
                  if (sel == nullptr || !CanBypass(*sel))
                     continue;
                  sel->node->bypassed = !sel->node->bypassed;
                  if (dynamic_cast<IAudioSource*>(sel->node.get()) != nullptr ||
                      dynamic_cast<INoteSource*>(sel->node.get()) != nullptr)
                  {
                     needsAudioRebuild = true;
                  }
               }
               if (needsAudioRebuild)
                  RebuildAudioTopology();
            }
         }
      }

      const bool doCopy = gRequestCopy || (!typing && !gPerfMatrixFocused && !gArrangeFocused && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false));
      gRequestCopy = false;
      if (doCopy)
      {
         clipboard.clear();
         clipboardSources.clear();
         clipboardOrigIndex.clear();
         clipboardOrigGroup.clear();
         clipboardCluster = ClusterClipboard();
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // A selected group brings its members along, even if they are not
            // individually part of the editor's own selection set.
            std::set<int> toCopy;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toCopy.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toCopy.insert(it->second.begin(), it->second.end());
               }
            }

            for (int index : toCopy)
            {
               GraphNode* gn = FindNodeByIndex(index);
               if (gn == nullptr)
                  continue;
               clipboard.push_back(gn->typeName);
               clipboardSources.push_back(gn->node.get());
               clipboardOrigIndex.push_back(gn->index);
               clipboardOrigGroup.push_back(IndexOfGroupNode(GroupOwning(gn->index)));
            }
            CaptureClusterLinks(toCopy, clipboardCluster);
         }
      }

      const bool doPaste = (gRequestPaste || (!typing && !gPerfMatrixFocused && !gArrangeFocused && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))) && !clipboard.empty();
      gRequestPaste = false;
      if (doPaste)
      {
         // Recomputed fresh against the canvas as it stands right now, so a
         // second Cmd+V (which already sees the first paste sitting on the
         // canvas) lands clear of that too, not on top of it.
         const ImVec2 off =
            ClusterOffset(std::set<int>(clipboardOrigIndex.begin(), clipboardOrigIndex.end()));

         // resolve sources first: SpawnNode can reallocate gNodes and invalidate pointers
         struct PasteItem
         {
            std::string type; std::string category; INode* src; ImVec2 pos;
            int origIndex; int origGroup;
         };
         std::vector<PasteItem> items;
         for (size_t i = 0; i < clipboard.size(); i++)
         {
            for (GraphNode& gn : gNodes)
            {
               if (gn.node.get() == clipboardSources[i])
               {
                  ImVec2 p = ed::GetNodePosition(gn.NodeId());
                  items.push_back({ clipboard[i], gn.category, gn.node.get(),
                                    ImVec2(p.x + off.x, p.y + off.y),
                                    clipboardOrigIndex[i], clipboardOrigGroup[i] });
                  break;
               }
            }
         }
         // One checkpoint for the whole paste: SpawnNode pushes its own by
         // default, which would otherwise scatter a multi-node paste across
         // several undo steps instead of one.
         if (!items.empty())
            PushUndoCheckpoint();
         gSuppressUndoCheckpoints = true;
         ed::ClearSelection();
         std::map<int, GraphNode*> newByOrig;
         for (const PasteItem& item : items)
         {
            if (GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y))
            {
               CopyParams(copy->node.get(), item.src);
               if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
                  rn->seed = RandomNode::NextSeed();
               // See the duplicate path's identical comment above (doc
               // §5.7.3) - a paste needs the same fresh-identity treatment.
               if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
               {
                  fgn->SetUid(FieldGraphNode::NewUid());
                  fgn->Ownership() = Field::GraphOwnershipMap();
                  fgn->ownershipText.clear();
               }
               newByOrig[item.origIndex] = copy;
            }
         }
         // Re-establish group membership among the pasted copies.
         for (const PasteItem& item : items)
         {
            if (item.origGroup < 0)
               continue;
            auto groupIt = newByOrig.find(item.origGroup);
            auto memberIt = newByOrig.find(item.origIndex);
            if (groupIt == newByOrig.end() || memberIt == newByOrig.end())
               continue;
            if (auto* g = dynamic_cast<GroupNode*>(groupIt->second->node.get()))
               gGroupMembers[g].insert(memberIt->second->index);
         }
         ApplyClusterLinks(newByOrig, clipboardCluster);
         gSuppressUndoCheckpoints = false;
      }

      // ---- handle deletions raised by the editor itself ----
      if (ed::BeginDelete())
      {
         ed::LinkId linkId;
         while (ed::QueryDeletedLink(&linkId))
         {
            if (ed::AcceptDeletedItem())
            {
               PushUndoCheckpoint();
               DisconnectLinkById((int)linkId.Get());
            }
         }

         ed::NodeId nodeId;
         while (ed::QueryDeletedNode(&nodeId))
         {
            if (ed::AcceptDeletedItem())
               RemoveNodeByIndex((int)nodeId.Get() / GraphNode::kStride);
         }
      }
      ed::EndDelete();

      // ---- undo checkpoint for node drags ----
      // Only worth capturing a snapshot (BuildPatchData() walks every node
      // and cable) when this click could actually turn into a node drag -
      // not on every click anywhere, including knobs, buttons, menus and
      // empty canvas, which used to pay this cost on every single click.
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         bool couldStartNodeDrag = (bool)ed::GetHoveredNode();
         if (!couldStartNodeDrag)
         {
            const int selCount = ed::GetSelectedObjectCount();
            if (selCount > 0)
            {
               std::vector<ed::NodeId> selNodes(selCount);
               couldStartNodeDrag = ed::GetSelectedNodes(selNodes.data(), selCount) > 0;
            }
         }
         if (couldStartNodeDrag)
         {
            gDragStartSnapshot = BuildPatchData();
            gDragSnapshotValid = true;
            gDragSnapshotPushed = false;
         }
      }
      if (gDragSnapshotValid && !gDragSnapshotPushed &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
      {
         bool moved = false;
         for (const Patch::NodeRecord& rec : gDragStartSnapshot.nodes)
         {
            GraphNode* gn = FindNodeByIndex(rec.index);
            if (gn != nullptr &&
                (std::fabs(gn->liveX - rec.x) > 0.5f || std::fabs(gn->liveY - rec.y) > 0.5f))
            {
               moved = true;
               break;
            }
         }
         if (moved)
         {
            PushUndoSnapshot(std::move(gDragStartSnapshot));
            gDragSnapshotPushed = true;
         }
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         gDragSnapshotValid = false;

      // ---- snap to grid once the drag finishes ----
      if (gSnapToGrid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         for (GraphNode& gn : gNodes)
         {
            ImVec2 p = ed::GetNodePosition(gn.NodeId());
            ImVec2 snapped(std::round(p.x / gGridSnap) * gGridSnap,
                           std::round(p.y / gGridSnap) * gGridSnap);
            if (std::fabs(snapped.x - p.x) > 0.01f || std::fabs(snapped.y - p.y) > 0.01f)
               ed::SetNodePosition(gn.NodeId(), snapped);
         }
      }

      // ---- popups: search, spawn menu, dropdown ----
      ed::Suspend();

      DrawMinimap();
      DrawFind();
      DrawCookOverlay();
      DrawNotices();

      // Deferred from GlobalScaleToggle() above - see comment on
      // gGlobalScaleTooltipHovered. This is the first point after the
      // per-node draw loop where an ed::Suspend()'d tooltip is safe to draw.
      if (gGlobalScaleTooltipHovered)
      {
         ImGui::BeginTooltip();
         ImGui::TextUnformatted(gGlobalScaleTooltipEnabled ? "Global Scale: ON (click to disable)"
                                                            : "Global Scale: OFF (click to enable)");
         ImGui::EndTooltip();
      }

      // Right-click (two-finger click on a Mac trackpad) opens the same
      // type-to-filter picker as double-click, so the keyboard works either way.
      if (ed::ShowBackgroundContextMenu() && gCommentEdit.target == nullptr)
      {
         gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // Right-click (two-finger click on a Mac trackpad) a node or group ->
      // a menu of actions specific to what got clicked, rather than only the
      // menu-bar/shortcut routes to the same operations.
      {
         ed::NodeId contextNodeId = 0;
         // A right-click already claimed by a param this frame (see
         // gParamRightClickConsumedThisFrame) opens that param's text field
         // instead - node editor still reports the click as a node context
         // menu request, so it has to be swallowed here rather than upstream.
         if (ed::ShowNodeContextMenu(&contextNodeId) && !gParamRightClickConsumedThisFrame)
         {
            gContextMenuNodeIndex = (int)contextNodeId.Get() / GraphNode::kStride;
            ImGui::OpenPopup("##nodecontext");
         }
      }}
}

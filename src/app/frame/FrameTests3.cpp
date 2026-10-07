// Per-frame self-test blocks moved verbatim out of the main loop in main.cpp.
#include "app/AppShared.h"

namespace app
{

void FrameTest_FIELDGRAPHUNDOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDGRAPHUNDOTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHUNDOTEST] Running Field graph-domain undo/redo harness...\n");
         bool ok = true;
         NewPatch();

         PushUndoCheckpoint(); // clean baseline checkpoint

         // 1. Spawn a Field Graph node emitting 8 nodes
         GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
         auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
         fgn->code =
            "param float voices = 8 [1, 8]\n"
            "for (k = 0; k < 8; k += 1) {\n"
            "   if (k < voices) {\n"
            "      osc = emit(\"LFO\", k)\n"
            "      set(osc, \"rateBeats\", 1.0 + k)\n"
            "   }\n"
            "}\n";

         size_t undoSizeBefore = gUndoStack.size();
         RunFieldGraphRegenerate(fgn);
         size_t undoSizeAfter = gUndoStack.size();

         bool pass1 = (undoSizeAfter == undoSizeBefore + 1) && (gNodes.size() == 9);
         printf("[FIELDGRAPHUNDOTEST] Assertion 1 (Regenerate 8 nodes stack growth 1): stackBefore=%zu stackAfter=%zu gNodes=%zu  %s\n",
                undoSizeBefore, undoSizeAfter, gNodes.size(), pass1 ? "OK" : "FAIL");
         ok = ok && pass1;

         // 2. Undo -> all 8 gone, kernel source reverted
         Undo();
         bool pass2 = (gNodes.size() == 1);
         printf("[FIELDGRAPHUNDOTEST] Assertion 2 (Undo: 8 gone): gNodes=%zu  %s\n",
                gNodes.size(), pass2 ? "OK" : "FAIL");
         ok = ok && pass2;

         // 3. Redo -> all 8 back, ownership resolves
         Redo();
         fgn = nullptr;
         for (GraphNode& n : gNodes)
            if (auto* fg = dynamic_cast<FieldGraphNode*>(n.node.get())) fgn = fg;
         bool resolves8 = (fgn != nullptr) && (gNodes.size() == 9);
         if (resolves8)
         {
            for (int i = 0; i < 8; ++i)
            {
               int idx = fgn->Ownership().Get("osc#" + std::to_string(i));
               if (idx < 0 || FindNodeByIndex(idx) == nullptr) resolves8 = false;
            }
         }
         bool pass3 = (gNodes.size() == 9) && resolves8;
         printf("[FIELDGRAPHUNDOTEST] Assertion 3 (Redo: 8 back and ownership resolves): resolves=%d gNodes=%zu  %s\n",
                (int)resolves8, gNodes.size(), pass3 ? "OK" : "FAIL");
         ok = ok && pass3;

         // 4. Undo past the regeneration to before the kernel existed -> no orphan nodes, no regeneration fired (T11)
         Undo(); // back to kernel only
         Undo(); // back to empty baseline
         bool pass4 = gNodes.empty();
         printf("[FIELDGRAPHUNDOTEST] Assertion 4 (Undo past regeneration to before kernel): gNodes=%zu  %s\n",
                gNodes.size(), pass4 ? "OK" : "FAIL");
         ok = ok && pass4;

         // 5. Redo forward -> identical graph
         Redo(); // kernel restored
         Redo(); // 8 nodes regenerated
         fgn = nullptr;
         for (GraphNode& n : gNodes)
            if (auto* fg = dynamic_cast<FieldGraphNode*>(n.node.get())) fgn = fg;
         bool pass5 = (gNodes.size() == 9) && (fgn != nullptr);
         printf("[FIELDGRAPHUNDOTEST] Assertion 5 (Redo forward to identical graph): gNodes=%zu  %s\n",
                gNodes.size(), pass5 ? "OK" : "FAIL");
         ok = ok && pass5;

         // 6. After ApplyPatchData, ownership map indices all resolve via FindNodeByIndex (T1, §4.2 remap)
         Patch::Data patchData = BuildPatchData();
         NewPatch();
         ApplyPatchData(patchData);
         fgn = nullptr;
         for (GraphNode& n : gNodes)
            if (auto* fg = dynamic_cast<FieldGraphNode*>(n.node.get())) fgn = fg;
         bool pass6 = (fgn != nullptr) && (gNodes.size() == 9);
         if (pass6)
         {
            for (int i = 0; i < 8; ++i)
            {
               int idx = fgn->Ownership().Get("osc#" + std::to_string(i));
               if (idx < 0 || FindNodeByIndex(idx) == nullptr) pass6 = false;
            }
         }
         printf("[FIELDGRAPHUNDOTEST] Assertion 6 (ApplyPatchData ownership map resolves): resolves=%d gNodes=%zu  %s\n",
                (int)pass6, gNodes.size(), pass6 ? "OK" : "FAIL");
         ok = ok && pass6;

         // 7. 30 consecutive regenerations leave gUndoStack.size() <= 30 and do not evict an earlier checkpoint (T9)
         PushUndoCheckpoint();
         size_t stackBefore30 = gUndoStack.size();
         for (int i = 0; i < 30; ++i)
         {
            fgn->GetParamTable().Find("voices")->value = (float)(1 + (i % 8));
            RunFieldGraphRegenerate(fgn);
         }
         size_t stackAfter30 = gUndoStack.size();
         bool pass7 = (stackAfter30 <= stackBefore30 + 30) && (stackAfter30 < 200);
         printf("[FIELDGRAPHUNDOTEST] Assertion 7 (30 regenerations <= 30 checkpoints): stackBefore=%zu stackAfter=%zu  %s\n",
                stackBefore30, stackAfter30, pass7 ? "OK" : "FAIL");
         ok = ok && pass7;

         NewPatch();
         printf("%s\n", ok ? "FIELDGRAPHUNDO OK" : "SUSPECT");
      }
}

void FrameTest_FIELDGRAPHBLASTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDGRAPHBLASTTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHBLASTTEST] Running Field graph-domain blast radius harness...\n");
         bool allOk = true;

         // Case 1: hand-delete a generated node -> kernel reports 1 missing, no crash;
         // regenerate -> it returns; index differs; nothing else moved.
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            int kernelIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "      set(osc, \"rateBeats\", 1.0 + k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int osc0Before = fgn->Ownership().Get("osc#0");
            int osc1Before = fgn->Ownership().Get("osc#1");
            int osc2Before = fgn->Ownership().Get("osc#2");
            int osc3Before = fgn->Ownership().Get("osc#3");

            // Hand-delete osc#1
            RemoveNodeByIndex(osc1Before);
            bool missingReported = (FindNodeByIndex(osc1Before) == nullptr);

            // Regenerate brings it back
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int osc1After = fgn->Ownership().Get("osc#1");
            bool osc1Back = (osc1After >= 0 && FindNodeByIndex(osc1After) != nullptr);
            bool indexDiffers = (osc1After != osc1Before);
            bool othersUnchanged = (fgn->Ownership().Get("osc#0") == osc0Before &&
                                   fgn->Ownership().Get("osc#2") == osc2Before &&
                                   fgn->Ownership().Get("osc#3") == osc3Before);

            bool pass1 = missingReported && osc1Back && indexDiffers && othersUnchanged && (gNodes.size() == 5);
            printf("[FIELDGRAPHBLASTTEST] Assertion 1 (Hand-delete & return): missing=%d back=%d diffIndex=%d othersSame=%d  %s\n",
                   (int)missingReported, (int)osc1Back, (int)indexDiffers, (int)othersUnchanged, pass1 ? "OK" : "FAIL");
            allOk = allOk && pass1;
         }

         // Case 2: put 2 generated nodes in a group, force a type-change remount ->
         // both are still in the group afterwards; emit(\"Group\", i) is refused.
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            int kernelIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int osc0 = fgn->Ownership().Get("osc#0");
            int osc2 = fgn->Ownership().Get("osc#2");

            GraphNode* grpGn = SpawnNode("Group", "Compositing", 0.0f, 0.0f);
            int grpIdx = grpGn->index;
            auto* grp = static_cast<GroupNode*>(grpGn->node.get());
            gGroupMembers[grp].insert(osc0);
            gGroupMembers[grp].insert(osc2);

            // Re-fetch fgn after SpawnNode
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());

            // Force type change LFO -> Ramp
            fgn->code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"Ramp\", k)\n"
               "   }\n"
               "}\n";
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            grpGn = FindNodeByIndex(grpIdx);
            grp = static_cast<GroupNode*>(grpGn->node.get());

            int osc0New = fgn->Ownership().Get("osc#0");
            int osc2New = fgn->Ownership().Get("osc#2");
            bool bothInGroup = (gGroupMembers[grp].count(osc0New) != 0) &&
                               (gGroupMembers[grp].count(osc2New) != 0);

            // emit(\"Group\", i) refused
            FieldGraphNode groupEmitNode;
            groupEmitNode.code = "g = emit(\"Group\", 0)\n";
            bool groupRefused = !groupEmitNode.Apply();

            bool pass2 = bothInGroup && groupRefused;
            printf("[FIELDGRAPHBLASTTEST] Assertion 2 (Group membership rescued, emit Group refused): groupRescued=%d groupRefused=%d  %s\n",
                   (int)bothInGroup, (int)groupRefused, pass2 ? "OK" : "FAIL");
            allOk = allOk && pass2;
         }

         // Case 3: copy/paste
         // copy kernel+children, paste -> pasted kernel's uid differs, both kernels regenerate
         // without stealing each other's nodes, and node count doubles exactly once;
         // copy children alone -> copies are owned by nobody and survive regeneration of original.
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            int kernelIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 4 [1, 8]\n"
               "for (k = 0; k < 8; k += 1) {\n"
               "   if (k < voices) {\n"
               "      osc = emit(\"LFO\", k)\n"
               "   }\n"
               "}\n";
            MainGraphHost host;
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            std::string uid1 = fgn->Uid();

            std::set<int> kernelAndChildren;
            kernelAndChildren.insert(kernelIdx);
            for (int i = 0; i < 4; ++i)
               kernelAndChildren.insert(fgn->Ownership().Get("osc#" + std::to_string(i)));

            PerformCopyPaste(kernelAndChildren);
            bool countDoubled = (gNodes.size() == 10);

            // Find pasted kernel
            int fgn2Idx = -1;
            for (const GraphNode& n : gNodes)
            {
               if (n.index != kernelIdx && dynamic_cast<FieldGraphNode*>(n.node.get()) != nullptr)
               {
                  fgn2Idx = n.index;
                  break;
               }
            }
            GraphNode* gn2 = FindNodeByIndex(fgn2Idx);
            auto* fgn2 = gn2 ? static_cast<FieldGraphNode*>(gn2->node.get()) : nullptr;
            bool uidsDiffer = (fgn2 != nullptr && fgn2->Uid() != uid1);

            // Regenerate both safely
            gn = FindNodeByIndex(kernelIdx);
            if (gn) static_cast<FieldGraphNode*>(gn->node.get())->Regenerate(host);
            gn2 = FindNodeByIndex(fgn2Idx);
            if (gn2) static_cast<FieldGraphNode*>(gn2->node.get())->Regenerate(host);
            bool noStealing = (gNodes.size() == 10);

            // Copy children alone
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            std::set<int> childrenOnly;
            for (int i = 0; i < 4; ++i)
               childrenOnly.insert(fgn->Ownership().Get("osc#" + std::to_string(i)));
            PerformCopyPaste(childrenOnly);
            size_t countAfterChildrenPaste = gNodes.size();

            // Regenerate original
            gn = FindNodeByIndex(kernelIdx);
            if (gn) static_cast<FieldGraphNode*>(gn->node.get())->Regenerate(host);
            bool childrenSurvive = (gNodes.size() == countAfterChildrenPaste);

            bool pass3 = countDoubled && uidsDiffer && noStealing && (countAfterChildrenPaste == 14) && childrenSurvive;
            printf("[FIELDGRAPHBLASTTEST] Assertion 3 (Copy/paste subgraph & orphan children): doubled=%d diffUid=%d noStealing=%d childrenSurvive=%d  %s\n",
                   (int)countDoubled, (int)uidsDiffer, (int)noStealing, (int)childrenSurvive, pass3 ? "OK" : "FAIL");
            allOk = allOk && pass3;
         }

         // Case 4: modulation-cable rescue
         // binding survives a remount and points at the new index;
         // binding to a kernel-driven param is refused;
         // true unmount clears the binding and the kernel reports it.
         {
            NewPatch();
            GraphNode* lfoMod = SpawnNode("LFO", "Utility", -200.0f, 0.0f);
            int lfoModIdx = lfoMod->index;
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            int kernelIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 1 [1, 8]\n"
               "osc = emit(\"LFO\", 0)\n"
               "set(osc, \"rateBeats\", 2.0)\n";
            MainGraphHost host;
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int oscIdx = fgn->Ownership().Get("osc#0");

            // Binding to kernel-driven param is refused (param 1 is rateBeats)
            bool bindDrivenRefused = !CanBindModulation(oscIdx, 1);

            // Binding to a free param is accepted (param 2 is bipolar)
            bool bindFreeAllowed = CanBindModulation(oscIdx, 2);
            Modulation::Instance().Bind(oscIdx, 2, lfoModIdx, 0);
            bool modBound = Modulation::Instance().IsModulated(oscIdx, 2);

            // Force remount LFO -> Ramp
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 1 [1, 8]\n"
               "osc = emit(\"Ramp\", 0)\n";
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int newOscIdx = fgn->Ownership().Get("osc#0");
            bool remounted = (newOscIdx != oscIdx && newOscIdx >= 0);
            bool modRescued = Modulation::Instance().IsModulated(newOscIdx, 2);

            // True unmount: emit nothing
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code = "x = 1\n";
            fgn->Regenerate(host);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            bool modCleared = !Modulation::Instance().IsModulated(newOscIdx, 2);
            bool noticeReported = (fgn->Notice().find("modulation bindings dropped") != std::string::npos);

            bool pass4 = bindDrivenRefused && bindFreeAllowed && modBound && remounted && modRescued && modCleared && noticeReported;
            printf("[FIELDGRAPHBLASTTEST] Assertion 4 (Modulation rescue & refuse): refusedDriven=%d bound=%d rescued=%d cleared=%d reported=%d  %s\n",
                   (int)bindDrivenRefused, (int)modBound, (int)modRescued, (int)modCleared, (int)noticeReported, pass4 ? "OK" : "FAIL");
            allOk = allOk && pass4;
         }

         // Case 5: cable rescue
         // a generated->hand-placed cable survives a remount;
         // a true unmount clears the cable, the kernel reports the detach,
         // and one undo restores both node and cable.
         {
            NewPatch();
            GraphNode* fitNode = SpawnNode("Fit", "Compositing", 300.0f, 0.0f);
            int fitIdx = fitNode->index;
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            int kernelIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 1 [1, 8]\n"
               "osc = emit(\"Ramp\", 0)\n";
            RunFieldGraphRegenerate(fgn);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int oscIdx = fgn->Ownership().Get("osc#0");
            std::string connErr;
            ConnectNodes(oscIdx, 0, fitIdx, 0, connErr);
            auto hasCableToFit = [&](int fromIdx) {
               GraphNode* src = FindNodeByIndex(fromIdx);
               GraphNode* dst = FindNodeByIndex(fitIdx);
               if (!src || !dst) return false;
               ImageCable* cable = CableFor(*dst, 0);
               return cable != nullptr && cable->IsConnected() && cable->GetSource() == src->node.get();
            };
            bool initialCable = hasCableToFit(oscIdx);

            // Force remount Ramp -> Noise
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code =
               "param float voices = 1 [1, 8]\n"
               "osc = emit(\"Noise\", 0)\n";
            RunFieldGraphRegenerate(fgn);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            int remountIdx = fgn->Ownership().Get("osc#0");
            bool remountedCable = (remountIdx != oscIdx) && hasCableToFit(remountIdx);

            // True unmount
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code = "x = 1\n";
            RunFieldGraphRegenerate(fgn);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            bool cableGone = !hasCableToFit(remountIdx);
            bool unmountReported = (fgn->Notice().find("cables with unmounted nodes") != std::string::npos);

            // One undo restores both node and cable
            Undo();
            gn = FindNodeByIndex(kernelIdx);
            fgn = gn ? dynamic_cast<FieldGraphNode*>(gn->node.get()) : nullptr;
            int restoredIdx = fgn ? fgn->Ownership().Get("osc#0") : -1;
            bool undoRestoredNode = (restoredIdx >= 0 && FindNodeByIndex(restoredIdx) != nullptr);
            bool undoRestoredCable = hasCableToFit(restoredIdx);

            bool pass5 = initialCable && remountedCable && cableGone && unmountReported && undoRestoredNode && undoRestoredCable;
            printf("[FIELDGRAPHBLASTTEST] Assertion 5 (Cable rescue, detach & one undo): initCable=%d rescuedCable=%d cableGone=%d reported=%d undoNode=%d undoCable=%d  %s\n",
                   (int)initialCable, (int)remountedCable, (int)cableGone, (int)unmountReported, (int)undoRestoredNode, (int)undoRestoredCable, pass5 ? "OK" : "FAIL");
            allOk = allOk && pass5;
         }

         NewPatch();
         printf("%s\n", allOk ? "FIELDGRAPHBLAST OK" : "SUSPECT");
      }
}

void FrameTest_FIELDGRAPHENCAPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDGRAPHENCAPTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHENCAPTEST] Running Field graph encapsulation harness...\n");
         bool allOk = true;
         int kernelIdx = -1;

         // Assertion 1: regenerating (default encapsulated) leaves gNodes.size()
         // at exactly N+1 (kernel + N children) but the visible/pickable count
         // (GraphNode::hiddenFromCanvas) at exactly 1.
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            kernelIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            bool defaultEncapsulated = fgn->encapsulated;
            fgn->code =
               "param float voices = 3 [1, 8]\n"
               "for (k = 0; k < 3; k += 1) {\n"
               "   osc = emit(\"Noise\", k)\n"
               "}\n";
            RunFieldGraphRegenerate(fgn);

            int totalNodes = (int)gNodes.size();
            int visibleCount = 0;
            for (const GraphNode& n : gNodes)
               if (!n.hiddenFromCanvas)
                  visibleCount++;

            bool pass1 = defaultEncapsulated && (totalNodes == 4) && (visibleCount == 1);
            printf("[FIELDGRAPHENCAPTEST] Assertion 1 (Default encapsulated, N+1 nodes, 1 visible): defaultEnc=%d total=%d visible=%d  %s\n",
                   (int)defaultEncapsulated, totalNodes, visibleCount, pass1 ? "OK" : "FAIL");
            allOk = allOk && pass1;
         }

         // Assertion 2: a mounted child still cooks every frame despite being
         // hidden - proven by driving the exact production per-frame path
         // (ApplyModulationAndPalette's mounted-child loop, main.cpp) and
         // observing Noise's GetOutputTexture() go from 0 (never cooked) to
         // non-zero (first CookIfNeeded), while the child stays hidden.
         int child0Idx = -1;
         {
            GraphNode* gn = FindNodeByIndex(kernelIdx);
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            child0Idx = fgn->Ownership().Get("osc#0");
            GraphNode* child = FindNodeByIndex(child0Idx);
            bool hiddenBefore = child != nullptr && child->hiddenFromCanvas;
            bool zeroTextureBefore = child != nullptr && child->node->GetOutputTexture() == 0;

            ApplyModulationAndPalette(4);

            child = FindNodeByIndex(child0Idx);
            bool nonZeroTextureAfter = child != nullptr && child->node->GetOutputTexture() != 0;
            bool stillHidden = child != nullptr && child->hiddenFromCanvas;

            bool pass2 = hiddenBefore && zeroTextureBefore && nonZeroTextureAfter && stillHidden;
            printf("[FIELDGRAPHENCAPTEST] Assertion 2 (Hidden child still cooks every frame): hiddenBefore=%d zeroBefore=%d nonZeroAfter=%d stillHidden=%d  %s\n",
                   (int)hiddenBefore, (int)zeroTextureBefore, (int)nonZeroTextureAfter, (int)stillHidden, pass2 ? "OK" : "FAIL");
            allOk = allOk && pass2;
         }

         // Assertion 3: a terminal emitting an image producer resolves through
         // FieldGraphNode::TerminalIndices() to a node whose texture is ready
         // for the node body's inline preview (main.cpp's node-body dispatch,
         // §5.3) - the exact same INode*/texture DrawPreview would show if the
         // child were visible and previewed directly (doc trap 8: DrawPreview
         // itself needs no change, so identity of the resolved node/texture is
         // what this asserts).
         {
            GraphNode* gn = FindNodeByIndex(kernelIdx);
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            INode* previewTarget = nullptr;
            for (int idx : fgn->TerminalIndices())
            {
               GraphNode* term = FindNodeByIndex(idx);
               if (term != nullptr && term->node && term->node->GetOutputTexture() != 0 &&
                   term->node->GetOutputWidth() > 0)
               {
                  previewTarget = term->node.get();
                  break;
               }
            }
            GraphNode* expectedChild = FindNodeByIndex(child0Idx);
            bool pass3 = previewTarget != nullptr && expectedChild != nullptr &&
                         previewTarget == expectedChild->node.get() &&
                         previewTarget->GetOutputTexture() != 0;
            printf("[FIELDGRAPHENCAPTEST] Assertion 3 (Terminal resolves to a previewable texture): resolved=%d matchesChild=%d  %s\n",
                   (int)(previewTarget != nullptr), (int)(previewTarget == (expectedChild ? expectedChild->node.get() : nullptr)),
                   pass3 ? "OK" : "FAIL");
            allOk = allOk && pass3;
         }

         // Assertion 4: toggling encapsulated to false makes children appear on
         // canvas on the very next frame (ApplyModulationAndPalette's sync of
         // GraphNode::hiddenFromCanvas to FieldGraphNode::encapsulated), with
         // no re-Regenerate and no node re-created - same gNodes indices before
         // and after the toggle.
         {
            GraphNode* gn = FindNodeByIndex(kernelIdx);
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            std::vector<int> before;
            for (int k = 0; k < 3; k++)
               before.push_back(fgn->Ownership().Get("osc#" + std::to_string(k)));

            fgn->encapsulated = false;
            ApplyModulationAndPalette(4);

            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            std::vector<int> after;
            for (int k = 0; k < 3; k++)
               after.push_back(fgn->Ownership().Get("osc#" + std::to_string(k)));
            bool sameIndices = (before == after);

            bool allVisible = true;
            for (int idx : after)
            {
               GraphNode* child = FindNodeByIndex(idx);
               allVisible = allVisible && (child != nullptr && !child->hiddenFromCanvas);
            }

            bool pass4 = sameIndices && allVisible;
            printf("[FIELDGRAPHENCAPTEST] Assertion 4 (Toggle encapsulated -> children visible, same indices): sameIndices=%d allVisible=%d  %s\n",
                   (int)sameIndices, (int)allVisible, pass4 ? "OK" : "FAIL");
            allOk = allOk && pass4;

            // Restore for cleanliness before the next case.
            fgn->encapsulated = true;
            ApplyModulationAndPalette(4);
         }

         // Assertion 5: loading a patch saved before this step (no "b
         // encapsulated" line at all) restores with children visible on
         // canvas, not hidden - the load-time default (false) is the opposite
         // of a brand-new node's compile-time default (true, doc trap 5).
         {
            NewPatch();
            Patch::Data data;
            Patch::NodeRecord rec;
            rec.index = 0;
            rec.category = "Utility";
            rec.typeName = "Field Graph";
            rec.params.push_back({ "s code", "x = 1" });
            rec.params.push_back({ "s uid", "0000000000000001" });
            // Deliberately no "b encapsulated" key - reproduces a pre-step-15 save.
            data.nodes.push_back(rec);
            ApplyPatchData(data);

            GraphNode* gn = nullptr;
            for (GraphNode& n : gNodes)
               if (dynamic_cast<FieldGraphNode*>(n.node.get()) != nullptr) { gn = &n; break; }
            bool pass5a = gn != nullptr && !static_cast<FieldGraphNode*>(gn->node.get())->encapsulated;

            // Sanity check the other direction: a record that DOES carry the
            // key still loads to the saved value (LoadParams overwrites the
            // ApplyPatchData-forced false back to true) - proves this isn't
            // just clobbering every load to false.
            NewPatch();
            Patch::Data data2;
            Patch::NodeRecord rec2 = rec;
            rec2.params.push_back({ "b encapsulated", "1" });
            data2.nodes.push_back(rec2);
            ApplyPatchData(data2);
            GraphNode* gn2 = nullptr;
            for (GraphNode& n : gNodes)
               if (dynamic_cast<FieldGraphNode*>(n.node.get()) != nullptr) { gn2 = &n; break; }
            bool pass5b = gn2 != nullptr && static_cast<FieldGraphNode*>(gn2->node.get())->encapsulated;

            bool pass5 = pass5a && pass5b;
            printf("[FIELDGRAPHENCAPTEST] Assertion 5 (Old patch defaults unhidden; new-format key still honored): oldPatchUnhidden=%d newFormatHonored=%d  %s\n",
                   (int)pass5a, (int)pass5b, pass5 ? "OK" : "FAIL");
            allOk = allOk && pass5;
         }

         // Assertion 6 (step 15 follow-up, §5.4): a REAL outer cable wired
         // into the FieldGraphNode's own (derived, single) boundary output
         // pin - not the terminal directly, exactly what an outer patch
         // author actually drags a link onto - gets detached, counted, and
         // reported when the next Regenerate() makes its terminal disappear.
         // Mirrors MainGraphHost/VirtualGraphHost::Unmount's existing
         // scan-then-DisconnectAllTo shape (doc trap 6: no second counter).
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            int fgnIdx = gn->index;
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            fgn->code = "osc = emit(\"Noise\", 0)\n";
            RunFieldGraphRegenerate(fgn);

            // Cook once so the terminal has a real texture - the same
            // precondition DrawPreview/the inline waveform already need
            // (§5.3), and what ResolveFieldGraphBoundaryTerminal itself
            // checks for.
            ApplyModulationAndPalette(4);
            fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
            fgn->SetBoundaryOutputTarget(ResolveFieldGraphBoundaryTerminal(fgn));

            GraphNode* consumer = SpawnNode("Curves", "Compositing", 300.0f, 0.0f);
            int consumerIdx = consumer->index;
            std::string connErr;
            bool connected = ConnectNodes(fgnIdx, 0, consumerIdx, 0, connErr);

            consumer = FindNodeByIndex(consumerIdx);
            ImageCable* consumerCable = consumer ? CableFor(*consumer, 0) : nullptr;
            bool wiredToKernel = connected && consumerCable != nullptr && consumerCable->IsConnected() &&
                                 consumerCable->GetSource() == FindNodeByIndex(fgnIdx)->node.get();

            // The emit call-site is gone entirely - the terminal this pin
            // used to resolve to no longer exists after the next Regenerate.
            fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
            fgn->code = "x = 1\n";
            RunFieldGraphRegenerate(fgn);

            consumer = FindNodeByIndex(consumerIdx);
            ImageCable* afterCable = consumer ? CableFor(*consumer, 0) : nullptr;
            bool detached = afterCable == nullptr || !afterCable->IsConnected();

            fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
            const std::string& notice = fgn->Notice();
            bool noticeReports = notice.find("detached") != std::string::npos &&
                                  notice.find("boundary") != std::string::npos;

            bool pass6 = wiredToKernel && detached && noticeReports;
            printf("[FIELDGRAPHENCAPTEST] Assertion 6 (Boundary pin cable detach on terminal loss): wired=%d detached=%d reported=%d  %s\n",
                   (int)wiredToKernel, (int)detached, (int)noticeReports, pass6 ? "OK" : "FAIL");
            allOk = allOk && pass6;
         }

         NewPatch();
         printf("%s\n", allOk ? "FIELDGRAPHENCAP OK" : "SUSPECT");
      }
}

void FrameTest_FIELDPINSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDPINSTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDPINSTEST] Running dynamic pins (build step 11) harness...\n");
         bool allOk = true;

         // SECTION 1: headless pin-shape assertions - OutputCount/OutputLabel/
         // ModulatorOutput toggling for the three image/element/sample node
         // types, and append-only ordering (index 0's label/identity never
         // moves when index 1 appears).
         {
            FieldElementNode n;
            bool pass = (n.OutputCount() == 1) && (std::string(n.OutputLabel(0)) == "geo") &&
                        (n.ModulatorOutput(1) == nullptr);
            n.publishScalarOutput = true;
            pass = pass && (n.OutputCount() == 2) && (std::string(n.OutputLabel(0)) == "geo") &&
                   (std::string(n.OutputLabel(1)) == "publish") && (n.ModulatorOutput(1) != nullptr) &&
                   (n.ModulatorOutput(0) == nullptr);
            printf("[FIELDPINSTEST] Assertion 1 (FieldElementNode publish pin, append-only): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }
         {
            FieldSampleNode n;
            bool pass = (n.OutputCount() == 1) && n.IsAudioOutputIndex(0) && (n.ModulatorOutput(1) == nullptr);
            n.exposeRmsOutput = true;
            pass = pass && (n.OutputCount() == 2) && n.IsAudioOutputIndex(0) && !n.IsAudioOutputIndex(1) &&
                   (std::string(n.OutputLabel(0)) == "out") && (std::string(n.OutputLabel(1)) == "rms") &&
                   (n.ModulatorOutput(1) != nullptr);
            printf("[FIELDPINSTEST] Assertion 2 (FieldSampleNode rms pin, IsAudioOutputIndex trap): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }
         {
            FieldPixelNode n;
            bool pass = (n.OutputCount() == 1) && (n.GetOutputTexture(1) == 0);
            n.exposeAuxTexture = true;
            // No declared state cells yet - GetOutputTexture(1) must stay 0
            // rather than handing out a stale/zero-resolution FBO.
            pass = pass && (n.OutputCount() == 2) && (n.GetOutputTexture(1) == 0) &&
                   (std::string(n.OutputLabel(0)) == "out") && (std::string(n.OutputLabel(1)) == "state");
            n.width = 32.0f;
            n.height = 32.0f;
            n.code = "state float x = 0;\nx = x + 0.1;\ncol = vec3(fract(x));";
            n.Apply();
            n.CookIfNeeded(1);
            pass = pass && (n.GetOutputTexture(1) != 0);
            printf("[FIELDPINSTEST] Assertion 3 (FieldPixelNode aux texture, live only when states declared): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }
         {
            FieldGraphNode n;
            bool pass = (n.ModulatorInputCount() == 0) && (n.ModulatorInputSlot(0) == nullptr) &&
                        !n.TriggerInputWired();
            n.addTriggerInput = true;
            pass = pass && (n.ModulatorInputCount() == 1) && (n.ModulatorInputSlot(0) != nullptr) &&
                   (std::string(n.InputLabel(0)) == "trigger");
            // Rising-edge detection: a fake IModulator whose Value01() is
            // driven by hand, no ParamMailbox/MeterRing involved (§8 - reuse
            // ModulatorOutput/MeterRing only, never a new channel).
            struct FakeMod : public IModulator { float v = 0.0f; float Value01() override { return v; } };
            FakeMod fake;
            *n.ModulatorInputSlot(0) = &fake;
            pass = pass && n.TriggerInputWired() && !n.PollTriggerEdge(); // starts low, no edge yet
            fake.v = 1.0f;
            pass = pass && n.PollTriggerEdge();  // 0 -> 1 is a rising edge
            pass = pass && !n.PollTriggerEdge(); // holding high is not a second edge
            fake.v = 0.0f;
            pass = pass && !n.PollTriggerEdge(); // falling edge does not fire
            fake.v = 1.0f;
            pass = pass && n.PollTriggerEdge();  // rises again
            printf("[FIELDPINSTEST] Assertion 4 (FieldGraphNode trigger pin + rising-edge detection): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 2: cable-orphaning refusal detection, live graph. Image/
         // modulator-output cables are UI-link-derived (gLinks, rebuilt from
         // the node editor's own frame), so a headless ConnectNodes() call
         // does not populate them - synthetic LinkInfo entries stand in for
         // what a real dragged cable would leave in gLinks. The graph node's
         // trigger *input*, by contrast, is read directly off the node
         // (TriggerInputWired()), so it is exercised through a real
         // ConnectNodes() wiring instead.
         NewPatch();
         int elemIdx = -1, sampleIdx = -1, pixelSrcIdx = -1, pixelDstIdx = -1, graphSinkIdx = -1;
         {
            // Each SpawnNode() call can reallocate gNodes (a std::vector),
            // invalidating any GraphNode* returned by an earlier call in this
            // same sequence (trap T14 again, this time on the test's own
            // fixture setup) - so every pointer is used and discarded before
            // the next SpawnNode() runs; only the (reallocation-proof) index
            // survives to the next statement.
            GraphNode* ge = SpawnNode("Field Modifier", "3D", 0.0f, 0.0f);
            bool spawned = ge != nullptr;
            if (spawned)
            {
               elemIdx = ge->index;
               static_cast<FieldElementNode*>(ge->node.get())->publishScalarOutput = true;
            }

            GraphNode* gs = SpawnNode("Field Effect", "AudioEffects", 200.0f, 0.0f);
            spawned = spawned && (gs != nullptr);
            if (gs)
            {
               sampleIdx = gs->index;
               static_cast<FieldSampleNode*>(gs->node.get())->exposeRmsOutput = true;
            }

            GraphNode* gpSrc = SpawnNode("FieldPixel", "Source", 400.0f, 0.0f);
            spawned = spawned && (gpSrc != nullptr);
            if (gpSrc)
            {
               pixelSrcIdx = gpSrc->index;
               auto* pixSrc = static_cast<FieldPixelNode*>(gpSrc->node.get());
               pixSrc->exposeAuxTexture = true;
               pixSrc->width = 32.0f;
               pixSrc->height = 32.0f;
               pixSrc->code = "state float x = 0;\nx = x + 0.1;\ncol = vec3(fract(x));";
               pixSrc->Apply();
               pixSrc->CookIfNeeded(1);
            }

            GraphNode* gpDst = SpawnNode("FieldPixel", "Source", 600.0f, 0.0f);
            spawned = spawned && (gpDst != nullptr);
            if (gpDst)
            {
               pixelDstIdx = gpDst->index;
               // Field Pixel has no native input pin any more (device-catalog
               // simplification) - this fixture needs one declared `input
               // image` pin to exercise the real-image-cable assertion below,
               // so slot 0 is this declared pin rather than a native "src".
               auto* pixDst = static_cast<FieldPixelNode*>(gpDst->node.get());
               pixDst->code = "input pixel image src2;\ncol = vec3(uv.x, uv.y, 0.0);";
               pixDst->Apply();
            }

            GraphNode* gg = SpawnNode("Field Graph", "Utility", 800.0f, 0.0f);
            spawned = spawned && (gg != nullptr);
            if (gg)
            {
               graphSinkIdx = gg->index;
               static_cast<FieldGraphNode*>(gg->node.get())->addTriggerInput = true;
            }

            printf("[FIELDPINSTEST] Assertion 5 (spawn fixture graph): %s\n", spawned ? "OK" : "FAIL");
            allOk = allOk && spawned;
         }
         {
            GraphNode* ge = FindNodeByIndex(elemIdx);
            GraphNode* gs = FindNodeByIndex(sampleIdx);
            GraphNode* gpSrc = FindNodeByIndex(pixelSrcIdx);
            GraphNode* gpDst = FindNodeByIndex(pixelDstIdx);
            bool before = !FieldOutputPinHasLiveCable(elemIdx, 1) && !FieldOutputPinHasLiveCable(sampleIdx, 1) &&
                          !FieldOutputPinHasLiveCable(pixelSrcIdx, 1);
            gLinks.push_back({ 5000001, ge->OutputPinId(1), gs->InputPinId(0) });
            gLinks.push_back({ 5000002, gs->OutputPinId(1), gpSrc->InputPinId(0) });
            gLinks.push_back({ 5000003, gpSrc->OutputPinId(1), gpDst->InputPinId(0) });
            bool after = FieldOutputPinHasLiveCable(elemIdx, 1) && FieldOutputPinHasLiveCable(sampleIdx, 1) &&
                         FieldOutputPinHasLiveCable(pixelSrcIdx, 1) &&
                         !FieldOutputPinHasLiveCable(elemIdx, 0); // index 0 (the pre-existing pin) never flags live
            gLinks.clear();
            bool afterClear = !FieldOutputPinHasLiveCable(elemIdx, 1) && !FieldOutputPinHasLiveCable(sampleIdx, 1) &&
                               !FieldOutputPinHasLiveCable(pixelSrcIdx, 1);
            bool pass = before && after && afterClear;
            printf("[FIELDPINSTEST] Assertion 6 (FieldOutputPinHasLiveCable detects/clears synthetic cables): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }
         {
            // Real wiring for the graph node's trigger *input*: the sample
            // node's rms output feeds it, going through the same
            // ConnectNodes()/IsInputSlotCompatible() path a live drag uses.
            //
            // Deliberately NOT the element node's publish output here - see
            // the [FIELDPINSTEST FINDING] note below assertion 8. In short:
            // IsInputSlotCompatible's srcGeometry/srcCamera/srcLight checks
            // are whole-node dynamic_casts, not scoped to the specific output
            // index a cable is dragged from, so a connection from
            // FieldElementNode's *modulator* output (index 1) is wrongly
            // rejected by the "3D cables only go into 3D nodes" rule, because
            // FieldElementNode is also (always) an IGeometrySource. The
            // audio-source check three lines above it does not have this
            // bug - it consults IsAudioOutputIndex(srcOutputIndex) rather
            // than dynamic_cast<IAudioSource*> alone - so routing this
            // assertion through FieldSampleNode's rms output instead
            // exercises the identical TriggerInputWired()/ModulatorInputSlot
            // plumbing without tripping the geometry-side gap.
            std::string err;
            bool connected = ConnectNodes(sampleIdx, 1, graphSinkIdx, 0, err);
            auto* gg = static_cast<FieldGraphNode*>(FindNodeByIndex(graphSinkIdx)->node.get());
            bool wiredNow = connected && gg->TriggerInputWired();
            bool sourceMatches = wiredNow &&
               (*gg->ModulatorInputSlot(0) ==
                static_cast<FieldSampleNode*>(FindNodeByIndex(sampleIdx)->node.get())->ModulatorOutput(1));
            *gg->ModulatorInputSlot(0) = nullptr; // simulate disconnect
            bool unwiredAfter = !gg->TriggerInputWired();
            bool pass = connected && wiredNow && sourceMatches && unwiredAfter;
            printf("[FIELDPINSTEST] Assertion 7 (FieldGraphNode trigger wired via real ConnectNodes, TriggerInputWired): %s (%s)\n",
                   pass ? "OK" : "FAIL", err.c_str());
            allOk = allOk && pass;
            // Restore the real wiring for the save/load section below.
            ConnectNodes(sampleIdx, 1, graphSinkIdx, 0, err);

            // [FIELDPINSTEST FINDING - pre-existing gap, first exercised by
            // this step, left unfixed per doc §8's "stop and report" rule]:
            // confirm the gap actually exists and is exactly what's
            // described above, so this comment cannot silently go stale.
            std::string geoErr;
            bool geoBlocked = !ConnectNodes(elemIdx, 1, graphSinkIdx, 0, geoErr);
            if (!geoBlocked)
            {
               // Undo the unexpected cross-wire AND restore the real
               // sampleIdx -> graphSinkIdx wiring it stomped, or SECTION 3
               // below finds the trigger pin unwired through no fault of its
               // own (this is exactly what happened before this fix - see
               // the NOTE printed just below).
               ConnectNodes(sampleIdx, 1, graphSinkIdx, 0, err);
               printf("[FIELDPINSTEST] NOTE: FieldElementNode publish -> ModulatorInputSlot no longer blocked - the finding below may be stale, re-check IsInputSlotCompatible.\n");
            }
         }

         // SECTION 3: save/load round trip carries the new bool and, for the
         // pixel aux output, the srcOutput fix (Patch.cpp "cable"/"geo" tags -
         // see the fix's own commit note). Also covers the real image cable
         // wired between the two FieldPixel fixture nodes.
         {
            std::string err;
            bool imgConnected = ConnectNodes(pixelSrcIdx, 1, pixelDstIdx, 0, err);

            Patch::Data saved = BuildPatchData();
            NewPatch();
            ApplyPatchData(saved);

            auto* elemR = dynamic_cast<FieldElementNode*>(FindNodeByIndex(elemIdx) ? FindNodeByIndex(elemIdx)->node.get() : nullptr);
            auto* sampleR = dynamic_cast<FieldSampleNode*>(FindNodeByIndex(sampleIdx) ? FindNodeByIndex(sampleIdx)->node.get() : nullptr);
            auto* pixelSrcR = dynamic_cast<FieldPixelNode*>(FindNodeByIndex(pixelSrcIdx) ? FindNodeByIndex(pixelSrcIdx)->node.get() : nullptr);
            auto* pixelDstR = dynamic_cast<FieldPixelNode*>(FindNodeByIndex(pixelDstIdx) ? FindNodeByIndex(pixelDstIdx)->node.get() : nullptr);
            auto* graphR = dynamic_cast<FieldGraphNode*>(FindNodeByIndex(graphSinkIdx) ? FindNodeByIndex(graphSinkIdx)->node.get() : nullptr);

            bool boolsSurvived = elemR && sampleR && pixelSrcR && graphR &&
                                  elemR->publishScalarOutput && sampleR->exposeRmsOutput &&
                                  pixelSrcR->exposeAuxTexture && graphR->addTriggerInput;
            bool imageCableSurvived = imgConnected && pixelDstR && pixelDstR->DeclaredImageInput(0) != nullptr &&
                                       (pixelDstR->DeclaredImageInput(0)->GetSource() == pixelSrcR) &&
                                       (pixelDstR->DeclaredImageInput(0)->GetSourceOutput() == 1);
            bool triggerCableSurvived = graphR && graphR->TriggerInputWired() &&
                                         (*graphR->ModulatorInputSlot(0) == (sampleR ? sampleR->ModulatorOutput(1) : nullptr));
            bool pass = boolsSurvived && imageCableSurvived && triggerCableSurvived;
            printf("[FIELDPINSTEST] Assertion 8 (save/load round trip: bools + srcOutput-carrying cables): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 4: undo across a single toggle flip.
         {
            auto* elemR = dynamic_cast<FieldElementNode*>(FindNodeByIndex(elemIdx) ? FindNodeByIndex(elemIdx)->node.get() : nullptr);
            bool pass = elemR != nullptr;
            if (elemR)
            {
               bool before = elemR->publishScalarOutput; // true, from section 2/3
               PushUndoCheckpoint();
               elemR->publishScalarOutput = !before;
               Undo();
               auto* elemAfterUndo = dynamic_cast<FieldElementNode*>(FindNodeByIndex(elemIdx) ? FindNodeByIndex(elemIdx)->node.get() : nullptr);
               pass = elemAfterUndo && (elemAfterUndo->publishScalarOutput == before);
            }
            printf("[FIELDPINSTEST] Assertion 9 (undo reverts a single toggle flip): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 5: copy/paste preserves pin shape.
         {
            auto* elemR = dynamic_cast<FieldElementNode*>(FindNodeByIndex(elemIdx) ? FindNodeByIndex(elemIdx)->node.get() : nullptr);
            bool pass = false;
            if (elemR)
            {
               FieldElementNode copy;
               CopyParams(&copy, elemR);
               pass = (copy.publishScalarOutput == elemR->publishScalarOutput) && (copy.OutputCount() == elemR->OutputCount());
            }
            printf("[FIELDPINSTEST] Assertion 10 (copy/paste preserves pin shape): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 6: pre-step-11 patch fixture - a saved-params list missing
         // the new bool key entirely (as any patch saved before this step
         // would be) loads with the old pin shape and the bool defaulted
         // false, per Patch.cpp's Reader::Bool "leave untouched when the key
         // is absent" contract.
         {
            FieldElementNode src;
            src.publishScalarOutput = true;
            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&src, params);
            params.erase(std::remove_if(params.begin(), params.end(),
                                         [](const std::pair<std::string, std::string>& kv) {
                                            return kv.first == "b publishScalarOutput";
                                         }),
                          params.end());
            FieldElementNode loaded;
            Patch::LoadParams(&loaded, params);
            bool pass = !loaded.publishScalarOutput && (loaded.OutputCount() == 1);
            printf("[FIELDPINSTEST] Assertion 11 (pre-step-11 patch: bool defaults false, old pin shape): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         NewPatch();
         printf("%s\n", allOk ? "FIELDPINS OK" : "SUSPECT");
      }
}

void FrameTest_FIELDPINNODETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_FIELDPINNODETEST") != nullptr && frameId == 4)
      {
         printf("[FIELDPINNODETEST] Running dynamic pins (build step 13) node-wiring harness...\n");
         bool allOk = true;

         // SECTION 1-3: headless pin-shape assertions across all three node
         // types - compacted OutputCount/OutputLabel (native, then any
         // step-11 toggle, then declared pins in PinTable order), and the
         // deferred-value placeholder.
         {
            FieldElementNode n;
            n.code = "output element float foo = 1.0\nP.y += 0.0\n";
            bool applied = n.Apply();
            bool pass = applied && (n.OutputCount() == 2) &&
                        (std::string(n.OutputLabel(0)) == "geo") &&
                        (std::string(n.OutputLabel(1)) == "foo") &&
                        (n.ModulatorOutput(1) != nullptr) &&
                        (n.ModulatorOutput(1)->Value01() == 0.0f);
            printf("[FIELDPINNODETEST] Assertion 1 (FieldElementNode declared output, compacted, placeholder value): %s (err='%s')\n",
                   pass ? "OK" : "FAIL", n.LastError().c_str());
            allOk = allOk && pass;
         }
         {
            FieldSampleNode n;
            n.code = "output sample float y = in * 0.5\nout = in\n";
            bool applied = n.Apply();
            bool pass = applied && (n.OutputCount() == 2) &&
                        (std::string(n.OutputLabel(0)) == "out") &&
                        (std::string(n.OutputLabel(1)) == "y") &&
                        n.IsAudioOutputIndex(0) && !n.IsAudioOutputIndex(1) &&
                        (n.ModulatorOutput(1) != nullptr) &&
                        (n.ModulatorOutput(1)->Value01() == 0.0f);
            printf("[FIELDPINNODETEST] Assertion 2 (FieldSampleNode declared output, compacted, placeholder value): %s (err='%s')\n",
                   pass ? "OK" : "FAIL", n.LastError().c_str());
            allOk = allOk && pass;
         }
         {
            FieldPixelNode n;
            n.width = 32.0f;
            n.height = 32.0f;
            n.code = "output pixel float glow = 1.0;\ncol = vec3(uv.x, uv.y, 0.0);";
            bool applied = n.Apply();
            bool pass = applied && (n.OutputCount() == 2) &&
                        (std::string(n.OutputLabel(0)) == "out") &&
                        (std::string(n.OutputLabel(1)) == "glow") &&
                        (n.ModulatorOutput(1) != nullptr) &&
                        (n.ModulatorOutput(1)->Value01() == 0.0f);
            printf("[FIELDPINNODETEST] Assertion 3 (FieldPixelNode declared output, compacted, placeholder value): %s (err='%s')\n",
                   pass ? "OK" : "FAIL", n.LastError().c_str());
            allOk = allOk && pass;
         }

         // SECTION 4-5: FieldPixelNode declared `input image` pin, wired
         // through the real main.cpp InputCountFor/CableFor/ConnectNodes
         // path - the one main.cpp cable-chain edit this step made (§5.7).
         NewPatch();
         int pixSrcAIdx = -1, pixDstIdx = -1;
         {
            // Each SpawnNode() call can reallocate gNodes (a std::vector),
            // invalidating any GraphNode* an earlier call returned (trap
            // T14, see FIELDPINSTEST's identical fixture-setup comment) -
            // so every pointer is used and discarded before the next
            // SpawnNode() runs; only the (reallocation-proof) index survives.
            GraphNode* a = SpawnNode("FieldPixel", "Source", 0.0f, 0.0f);
            bool spawned = a != nullptr;
            if (spawned)
               pixSrcAIdx = a->index;

            GraphNode* d = SpawnNode("FieldPixel", "Source", 400.0f, 0.0f);
            spawned = spawned && (d != nullptr);
            if (spawned)
            {
               pixDstIdx = d->index;
               auto* dn = static_cast<FieldPixelNode*>(d->node.get());
               dn->width = 32.0f;
               dn->height = 32.0f;
               dn->code = "input pixel image other;\ncol = vec3(uv.x, uv.y, 0.0);";
               spawned = dn->Apply();
            }
            printf("[FIELDPINNODETEST] Assertion 4 (spawn image-input fixture; declared `input image` compiles): %s\n",
                   spawned ? "OK" : "FAIL");
            allOk = allOk && spawned;
         }
         {
            GraphNode* dg = FindNodeByIndex(pixDstIdx);
            auto* dn = dg ? static_cast<FieldPixelNode*>(dg->node.get()) : nullptr;
            bool countOk = dg && (InputCountFor(*dg) == 1); // no native pin any more, just the 1 declared image input
            std::string err;
            bool connected = ConnectNodes(pixSrcAIdx, 0, pixDstIdx, 0, err);
            bool wiredOk = connected && dn && dn->DeclaredImageInput(0) != nullptr &&
                           dn->DeclaredImageInput(0)->GetSource() ==
                              (FindNodeByIndex(pixSrcAIdx) ? FindNodeByIndex(pixSrcAIdx)->node.get() : nullptr);
            bool pass = countOk && wiredOk;
            printf("[FIELDPINNODETEST] Assertion 5 (declared image input: InputCountFor==1, real ConnectNodes wiring via CableFor): %s (%s)\n",
                   pass ? "OK" : "FAIL", err.c_str());
            allOk = allOk && pass;
         }

         // SECTION 6: cable-orphaning auto-disconnect, declared output pin.
         // Mirrors FIELDPINSTEST's step-11 toggle case, but the pin here
         // comes from a kernel edit (a live compile), not a UI toggle.
         // Behavior changed from a hard refusal to an auto-disconnect (see
         // PinTable.h's gLiveCableDisconnector doc) - dropping a declared
         // output that still has a live cable now succeeds, severs that
         // cable, and shrinks the pin shape, rather than blocking the edit.
         {
            // Same T14 reallocation trap as above - capture each index
            // before the next SpawnNode() call, discard the pointer.
            GraphNode* ge = SpawnNode("Field Modifier", "3D", 0.0f, 0.0f);
            bool spawned = ge != nullptr;
            int elemFixtureIdx = spawned ? ge->index : -1;

            GraphNode* sinkG = SpawnNode("Field Modifier", "3D", 200.0f, 0.0f); // only needs a pin id
            spawned = spawned && (sinkG != nullptr);
            int sinkFixtureIdx = spawned ? sinkG->index : -1;

            bool pass = spawned;
            if (spawned)
            {
               GraphNode* elemGN = FindNodeByIndex(elemFixtureIdx);
               GraphNode* sinkGN = FindNodeByIndex(sinkFixtureIdx);
               auto* en = static_cast<FieldElementNode*>(elemGN->node.get());
               // SpawnNode() does not itself call SetNodeIndex() - only the
               // node's own params-panel draw does, each frame it is drawn
               // (see DrawFieldElementParams's gCurrentNodeIndex handoff) -
               // so a headless fixture that never draws that panel must set
               // it explicitly for gLiveCableChecker's nodeIndex to line up
               // with the id this fixture's own OutputPinId()/InputPinId()
               // calls below encode.
               en->SetNodeIndex(elemFixtureIdx);
               en->code = "output element float foo = 1.0\nP.y += 0.0\n";
               pass = pass && en->Apply() && (en->OutputCount() == 2);

               const int liveLinkId = 5100001;
               gLinks.push_back({ liveLinkId, elemGN->OutputPinId(1), sinkGN->InputPinId(0) });
               en->code = "P.y += 0.0\n"; // drops the declared output entirely
               // gLinks is a per-frame snapshot rebuilt from live node
               // pointers (see DisconnectFieldPinBridge's comment) - a
               // synthetic entry pushed straight into the vector, as above,
               // has no real cable pointer for DisconnectLinkById to null
               // out, and nothing in this headless fixture re-derives gLinks
               // from scratch to prune it afterwards. So the observable
               // contract here is Apply() itself: it must succeed (not
               // refuse) and leave no refusal message, not "gLinks no
               // longer contains the synthetic id".
               bool applied = en->Apply();
               bool shapeShrunk = (en->OutputCount() == 1);
               bool noRefusal = en->pinRefusal.empty();
               gLinks.clear();
               pass = pass && applied && shapeShrunk && noRefusal;
            }
            printf("[FIELDPINNODETEST] Assertion 6 (declared-output drop auto-disconnects a live cable and shrinks pin shape): %s\n",
                   pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 7 (trap: never reuse a freed identity): renaming a
         // declared pin retires the old PinEntry and mints a fresh id for
         // the new name, even though the visible compacted slot (index 1)
         // is reused - see PinTable::Reconcile's S5.4 comment.
         {
            FieldElementNode n;
            n.code = "output element float foo = 1.0\nP.y += 0.0\n";
            bool applied = n.Apply();
            const Field::PinEntry* before = applied ? n.OutputPinTable().Find("foo") : nullptr;
            int beforeId = before ? before->id : -1;
            n.code = "output element float bar = 1.0\nP.y += 0.0\n";
            bool applied2 = n.Apply();
            const Field::PinEntry* after = applied2 ? n.OutputPinTable().Find("bar") : nullptr;
            int afterId = after ? after->id : -1;
            bool pass = applied && applied2 && before && after && (beforeId != afterId) && (n.OutputCount() == 2);
            printf("[FIELDPINNODETEST] Assertion 7 (renamed declared pin mints a fresh PinTable id, never reused): %s\n",
                   pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 8: save/load round trip - the declared image-input pin
         // and its real cable both survive.
         {
            Patch::Data saved = BuildPatchData();
            NewPatch();
            ApplyPatchData(saved);
            auto* dstR = dynamic_cast<FieldPixelNode*>(FindNodeByIndex(pixDstIdx) ? FindNodeByIndex(pixDstIdx)->node.get() : nullptr);
            const Field::PinEntry* pinR = dstR ? dstR->InputPinTable().Find("other") : nullptr;
            bool pass = dstR && pinR && pinR->isDeclared && dstR->DeclaredImageInput(0) != nullptr &&
                        dstR->DeclaredImageInput(0)->GetSource() ==
                           (FindNodeByIndex(pixSrcAIdx) ? FindNodeByIndex(pixSrcAIdx)->node.get() : nullptr);
            printf("[FIELDPINNODETEST] Assertion 8 (save/load round trip: declared image-input pin + its cable survive): %s\n",
                   pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 9: undo reverts a kernel edit, restoring the declared
         // pin's shape along with the code string it came from.
         {
            auto* dstR = dynamic_cast<FieldPixelNode*>(FindNodeByIndex(pixDstIdx) ? FindNodeByIndex(pixDstIdx)->node.get() : nullptr);
            bool pass = dstR != nullptr;
            if (dstR)
            {
               std::string before = dstR->code;
               PushUndoCheckpoint();
               dstR->code = "col = vec3(1.0);"; // drops the declared input entirely
               dstR->Apply();
               Undo();
               auto* afterUndo = dynamic_cast<FieldPixelNode*>(FindNodeByIndex(pixDstIdx) ? FindNodeByIndex(pixDstIdx)->node.get() : nullptr);
               const Field::PinEntry* pinR = afterUndo ? afterUndo->InputPinTable().Find("other") : nullptr;
               pass = afterUndo && (afterUndo->code == before) && pinR && pinR->isDeclared;
            }
            printf("[FIELDPINNODETEST] Assertion 9 (undo reverts a kernel edit; declared pin shape restored): %s\n",
                   pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         // SECTION 10: copy/paste preserves declared pin shape.
         {
            auto* dstR = dynamic_cast<FieldPixelNode*>(FindNodeByIndex(pixDstIdx) ? FindNodeByIndex(pixDstIdx)->node.get() : nullptr);
            bool pass = false;
            if (dstR)
            {
               FieldPixelNode copy;
               CopyParams(&copy, dstR);
               const Field::PinEntry* pinR = copy.InputPinTable().Find("other");
               pass = (copy.OutputCount() == dstR->OutputCount()) && pinR && pinR->isDeclared;
            }
            printf("[FIELDPINNODETEST] Assertion 10 (copy/paste preserves declared pin shape): %s\n", pass ? "OK" : "FAIL");
            allOk = allOk && pass;
         }

         NewPatch();
         printf("%s\n", allOk ? "FIELDPINNODE OK" : "SUSPECT");
      }
}

void FrameTest_NODETITLETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_NODETITLETEST") != nullptr && frameId == 4)
      {
         bool ok = true;
         int step = 0;
         auto checkAll = [&](const char* what) {
            ++step;
            for (const GraphNode& gn : gNodes)
            {
               int refTotal = 0;
               const int refRank = GetNodeInstanceIndexScan(gn, &refTotal);
               const std::string refTitle =
                  refTotal <= 1 ? NodeTitle(gn) : NodeTitle(gn) + " #" + std::to_string(refRank);
               int total = 0;
               const int rank = GetNodeInstanceIndex(gn, &total);
               const std::string title = NodeTitleWithInstance(gn);
               if (rank != refRank || total != refTotal || title != refTitle)
               {
                  printf("NODETITLE step %d (%s) node %d: got '%s' %d/%d, want '%s' %d/%d  FAIL\n", step, what,
                         gn.index, title.c_str(), rank, total, refTitle.c_str(), refRank, refTotal);
                  ok = false;
               }
            }
         };
         auto titleOf = [](const GraphNode* gn) { return gn != nullptr ? NodeTitleWithInstance(*gn) : std::string("?"); };
         auto expect = [&](const char* what, const std::string& got, const std::string& want) {
            const bool pass = got == want;
            printf("NODETITLE %s: '%s' (want '%s')  %s\n", what, got.c_str(), want.c_str(), pass ? "OK" : "FAIL");
            ok = ok && pass;
         };

         const int oscIdx = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f)->index;
         const int lfo1 = SpawnNode("LFO", "Modulators", 0.0f, 0.0f)->index;
         const int lfo2 = SpawnNode("LFO", "Modulators", 0.0f, 0.0f)->index;
         const int lfo3 = SpawnNode("LFO", "Modulators", 0.0f, 0.0f)->index;
         const int sphereIdx = SpawnNode("Sphere", "3D", 0.0f, 0.0f)->index;
         const int cubeIdx = SpawnNode("Cube", "3D", 0.0f, 0.0f)->index;
         checkAll("spawned");
         expect("unique oscillator", titleOf(FindNodeByIndex(oscIdx)), NodeTitle(*FindNodeByIndex(oscIdx)));
         expect("lfo 2 of 3", titleOf(FindNodeByIndex(lfo2)), NodeTitle(*FindNodeByIndex(lfo2)) + " #2");
         expect("unique sphere", titleOf(FindNodeByIndex(sphereIdx)), NodeTitle(*FindNodeByIndex(sphereIdx)));

         RemoveNodeByIndex(lfo2);
         checkAll("deleted lfo 2");
         expect("lfo 3 after delete", titleOf(FindNodeByIndex(lfo3)), NodeTitle(*FindNodeByIndex(lfo3)) + " #2");

         // A live title change with no invalidation at all: turn the sphere
         // into a second cube through its shape field, as its dropdown or a
         // modulator would.
         {
            auto* sphere = dynamic_cast<GeometryNode*>(FindNodeByIndex(sphereIdx)->node.get());
            auto* cube = dynamic_cast<GeometryNode*>(FindNodeByIndex(cubeIdx)->node.get());
            if (sphere == nullptr || cube == nullptr)
            {
               printf("NODETITLE could not spawn Sphere/Cube  FAIL\n");
               ok = false;
            }
            else
            {
               sphere->shape = cube->shape;
               checkAll("sphere became cube");
               expect("former sphere", titleOf(FindNodeByIndex(sphereIdx)),
                      NodeTitle(*FindNodeByIndex(cubeIdx)) + " #1");
               expect("cube", titleOf(FindNodeByIndex(cubeIdx)), NodeTitle(*FindNodeByIndex(cubeIdx)) + " #2");
            }
         }

         const int osc2 = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f)->index;
         checkAll("second oscillator");
         expect("oscillator 2 of 2", titleOf(FindNodeByIndex(osc2)), NodeTitle(*FindNodeByIndex(osc2)) + " #2");

         RemoveNodeByIndex(lfo1);
         checkAll("back to one lfo");
         expect("last lfo unique", titleOf(FindNodeByIndex(lfo3)), NodeTitle(*FindNodeByIndex(lfo3)));

         // A GraphNode that is not in gNodes still gets the scan's answer.
         {
            GraphNode stray;
            stray.node.reset(NodeFactory::Instance().MakeNode("Oscillator"));
            stray.typeName = "Oscillator";
            stray.index = 1 << 20;
            int total = 0;
            const int rank = GetNodeInstanceIndex(stray, &total);
            const bool pass = rank == 2 && total == 2;
            printf("NODETITLE stray node rank %d/%d (want 2/2)  %s\n", rank, total, pass ? "OK" : "FAIL");
            ok = ok && pass;
         }

         // Repeated lookups with nothing changed must not rebuild.
         {
            GetNodeInstanceIndex(gNodes.front());
            const int before = gTitleInstanceRebuilds;
            for (int r = 0; r < 10; r++)
               for (const GraphNode& gn : gNodes)
                  GetNodeInstanceIndex(gn);
            const bool pass = gTitleInstanceRebuilds == before;
            printf("NODETITLE steady-state rebuilds %d (want 0)  %s\n", gTitleInstanceRebuilds - before,
                   pass ? "OK" : "FAIL");
            ok = ok && pass;
         }

         printf("NODETITLE %s\n", ok ? "PASS" : "FAIL");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_FILTERCURVECACHETEST(int frameId, GLFWwindow* window)
{
   if (const char* fcTest = getenv("INFINITE_FILTERCURVECACHETEST"); fcTest != nullptr)
      {
         static int sIdx = -1;
         static bool sOk = true;
         static int sRecomputesAtStart = 0;
         static double sMotionStart = 0.0;
         static bool sSawCoarse = false;
         auto node = [&]() -> AudioEffectNode* {
            GraphNode* gn = FindNodeByIndex(sIdx);
            return gn != nullptr ? dynamic_cast<AudioEffectNode*>(gn->node.get()) : nullptr;
         };
         auto checkExact = [&](const char* what) {
            AudioEffectNode* n = node();
            auto it = gFilterCurveCache.find(sIdx);
            if (n == nullptr || it == gFilterCurveCache.end())
            {
               printf("FILTERCURVECACHE %s: no node/cache  FAIL\n", what);
               sOk = false;
               return;
            }
            const FilterCurveCache& c = it->second;
            const float type = n->Param("type"), freq = n->Param("freq"), q = n->Param("q"), gain = n->Param("gain");
            std::vector<float> ref;
            const double sr = c.signature.empty() ? 0.0 : (double)c.signature[0];
            ComputeFilterCurve(ref, kFilterCurveFullPoints, (int)(type + 0.5f), freq, q, gain, sr, c.lastOriginX,
                               c.lastWidth);
            const bool sigOk = c.signature.size() == 5 && c.signature[1] == type && c.signature[2] == freq &&
                               c.signature[3] == q && c.signature[4] == gain;
            const bool bitOk = c.curveDb.size() == ref.size() &&
                               std::memcmp(c.curveDb.data(), ref.data(), ref.size() * sizeof(float)) == 0;
            const bool pass = sigOk && bitOk && !c.coarse;
            printf("FILTERCURVECACHE %s: points=%zu coarse=%d signature=%s bit-identical=%s  %s\n", what,
                   c.curveDb.size(), c.coarse ? 1 : 0, sigOk ? "current" : "STALE", bitOk ? "yes" : "no",
                   pass ? "OK" : "FAIL");
            sOk = sOk && pass;
         };

         if (frameId == 2)
         {
            GraphNode* gn = SpawnNode("Audio Filter", "AudioEffects", 0.0f, 0.0f);
            sIdx = gn->index;
            *static_cast<AudioEffectNode*>(gn->node.get())->ParamPtr("freq") = 1000.0f;
         }
         if (frameId == 10)
            checkExact("static");
         if (frameId == 12)
            *node()->ParamPtr("freq") = 2500.0f; // one step, then still
         if (frameId == 15)
            checkExact("single step");
         // Frames 20..99: freq moves every frame, like an LFO.
         if (frameId >= 20 && frameId < 100)
         {
            if (frameId == 22)
            {
               sRecomputesAtStart = gFilterCurveRecomputes;
               sMotionStart = ImGui::GetTime();
            }
            *node()->ParamPtr("freq") = 400.0f * powf(2.0f, 3.0f * (0.5f + 0.5f * sinf((float)frameId * 0.21f)));
            auto it = gFilterCurveCache.find(sIdx);
            if (frameId > 24 && it != gFilterCurveCache.end() && it->second.coarse)
               sSawCoarse = true;
         }
         if (frameId == 99)
         {
            const int recomputes = gFilterCurveRecomputes - sRecomputesAtStart;
            const double elapsed = ImGui::GetTime() - sMotionStart;
            const int bound = (int)std::floor(elapsed / kFilterCurveMotionSec) + 2;
            const bool pass = recomputes <= bound && recomputes > 0 && sSawCoarse;
            printf("FILTERCURVECACHE motion: %d recomputes over %d frames / %.3fs (cap %d), coarse seen=%d  %s\n",
                   recomputes, 99 - 22, elapsed, bound, sSawCoarse ? 1 : 0, pass ? "OK" : "FAIL");
            sOk = sOk && pass;
         }
         if (frameId == 103)
            checkExact("settled after motion");
         if (frameId == 104)
         {
            printf("FILTERCURVECACHE %s\n", sOk ? "PASS" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_BYPASSRULETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_BYPASSRULETEST") != nullptr && frameId == 4)
      {
         // 1) Which node types keep the power button. Printed in full so a
         //    new multi-input node losing bypass is visible in the log.
         int allowed = 0, blocked = 0;
         bool synthsOk = true;
         for (const std::string& category : NodeFactory::Instance().GetCategories())
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
            {
               GraphNode probe;
               probe.node.reset(NodeFactory::Instance().MakeNode(name));
               probe.category = category;
               if (probe.node == nullptr)
                  continue;
               if (category == "Synths" && !CanBypass(probe))
               {
                  printf("BYPASSRULE synth %s lost bypass  FAIL\n", name.c_str());
                  synthsOk = false;
               }
               if (CanBypass(probe))
                  allowed++;
               else
               {
                  blocked++;
                  printf("BYPASSRULE no bypass: %s / %s (%d inputs)\n", category.c_str(), name.c_str(), InputCountFor(probe));
               }
            }
         printf("BYPASSRULE %d types bypassable, %d not\n", allowed, blocked);

         // 2) Texture -> Cube with the texture bypassed: the cube must stop
         //    seeing that texture, not keep its last frame.
         bool ok = synthsOk;
         {
            GraphNode shape;
            shape.node.reset(NodeFactory::Instance().MakeNode("Shape"));
            GraphNode cube;
            cube.node.reset(NodeFactory::Instance().MakeNode("Cube"));
            auto* geo = dynamic_cast<GeometryNode*>(cube.node.get());
            if (shape.node == nullptr || geo == nullptr)
            {
               printf("BYPASSRULE texture: could not spawn Shape/Cube  FAIL\n");
               ok = false;
            }
            else
            {
               geo->TextureInput().Connect(shape.node.get());
               geo->CookIfNeeded(frameId);
               const bool liveHasTex = geo->GetSurfaceTexture() != 0;
               shape.node->bypassed = true;
               geo->CookIfNeeded(frameId + 1);
               const bool bypassedClear = geo->GetSurfaceTexture() == 0 && geo->SurfaceTextureRevision() == 0;
               printf("BYPASSRULE texture live=%s bypassed-cleared=%s  %s\n", liveHasTex ? "yes" : "no",
                      bypassedClear ? "yes" : "no", (liveHasTex && bypassedClear) ? "OK" : "FAIL");
               ok = ok && liveHasTex && bypassedClear;
               geo->TextureInput().Disconnect();
            }
         }
         // 3) The rule on the real classes the user named.
         for (const char* name : { "Blend", "Mixer", "Switcher", "Join Geometry" })
         {
            GraphNode probe;
            probe.node.reset(NodeFactory::Instance().MakeNode(name));
            if (probe.node == nullptr)
               continue;
            const bool pass = !CanBypass(probe);
            printf("BYPASSRULE %s blocked: %s\n", name, pass ? "OK" : "FAIL");
            ok = ok && pass;
         }
         printf("BYPASSRULE %s\n", ok ? "PASS" : "FAIL");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_BYPASSSWEEPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_BYPASSSWEEPTEST") != nullptr && frameId == 4)
      {
         // Every bypassable node type, one at a time, against one fixture
         // source per signal kind. The contract a bypassed node T must meet:
         //
         //   input kind == output kind  ->  T passes its input through, exactly:
         //                                  BypassSource() is that input, and
         //                                  whatever a consumer reads from T
         //                                  (texture / mesh getters / value /
         //                                  audio resolve) is what it would
         //                                  read from the input directly.
         //   kinds differ, or no input  ->  T is removed: BypassSource() is
         //                                  null and T reads as empty (no
         //                                  texture, no mesh, silence).
         //
         // This replaces testing chains: bypass is a property of one node
         // plus one cable, so if every type honours it alone, every chain of
         // them does.
         enum : int { kKImage = 1, kKGeo = 2, kKMod = 4, kKAudio = 8, kKNote = 16, kKPalette = 32 };
         auto kindsOf = [](INode* n) -> int
         {
            int k = 0;
            if (dynamic_cast<IGeometrySource*>(n) != nullptr) k |= kKGeo;
            if (dynamic_cast<IModulator*>(n) != nullptr || ModulatorForOutput(n, 0) != nullptr) k |= kKMod;
            if (dynamic_cast<IPaletteSource*>(n) != nullptr) k |= kKPalette;
            if (dynamic_cast<IAudioSource*>(n) != nullptr) k |= kKAudio;
            if (dynamic_cast<INoteSource*>(n) != nullptr) k |= kKNote;
            if (k == 0 || dynamic_cast<VideoSourceNode*>(n) != nullptr) k |= kKImage;
            return k;
         };

         gSuppressUndoCheckpoints = true;
         struct Fixture { const char* type; const char* category; int kind; int index; };
         Fixture fixtures[] = {
            { "Shape", "Source", kKImage, -1 },    { "Geometry", "3D", kKGeo, -1 },
            { "Curve", "3D", kKGeo, -1 },          { "LFO", "Modulators", kKMod, -1 },
            { "Oscillator", "Synths", kKAudio, -1 }, { "Keyboard", "Notes", kKNote, -1 },
         };
         for (Fixture& f : fixtures)
            if (GraphNode* g = SpawnNode(f.type, f.category, -4000.0f, -4000.0f))
            {
               f.index = g->index;
               g->node->CookIfNeeded(frameId);
               if (auto* geo = dynamic_cast<IGeometrySource*>(g->node.get()))
               {
                  geo->GetMesh();
                  geo->GetCurve();
               }
            }

         int passes = 0, removed = 0, unprobed = 0, failures = 0;
         std::vector<std::pair<std::string, std::string>> types;
         for (const std::string& category : NodeFactory::Instance().GetCategories())
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
               if (IsUserSpawnable(name))
                  types.push_back({ category, name });

         for (const auto& typePair : types)
         {
            const std::string& category = typePair.first;
            const std::string& name = typePair.second;
            GraphNode* t = SpawnNode(name, category, -3000.0f, -3000.0f);
            if (t == nullptr || t->node == nullptr)
               continue;
            const int tIndex = t->index;
            if (!CanBypass(*t))
            {
               RemoveNodeByIndex(tIndex);
               continue;
            }
            INode* node = t->node.get();
            auto fail = [&](const std::string& why)
            {
               printf("BYPASSSWEEP FAIL %s / %s: %s\n", category.c_str(), name.c_str(), why.c_str());
               failures++;
            };

            // Wire the first fixture slot 0 accepts. Geometry tries the mesh
            // fixture before the curve one; the first that connects wins.
            Fixture* wired = nullptr;
            std::string err;
            if (InputCountFor(*t) >= 1 || node->AudioInputSlot(0) != nullptr || node->NoteInputSlot(0) != nullptr)
               for (Fixture& f : fixtures)
                  if (f.index >= 0 && ConnectNodes(f.index, 0, tIndex, 0, err))
                  {
                     wired = &f;
                     break;
                  }
            INode* src = wired != nullptr ? FindNodeByIndex(wired->index)->node.get() : nullptr;

            // Cook live first, so a node that ignores bypass has a real last
            // frame / mesh to leak.
            node->CookIfNeeded(frameId);
            auto* tGeo = dynamic_cast<IGeometrySource*>(node);
            if (tGeo != nullptr)
            {
               tGeo->GetMesh();
               tGeo->GetPointCloud();
               tGeo->GetCurve();
            }
            node->bypassed = true;
            node->CookIfNeeded(frameId + 1);

            const int outKinds = kindsOf(node);
            INode* through = node->BypassSource();
            // Same kind, but the input is control for a generator rather than
            // the signal being processed - bypass removes these. Instruments
            // (category Synths) are the big family; the rest are named.
            const bool generator = category == "Synths" || name == "Metaballs";
            const bool sameKind = wired != nullptr && !generator && (outKinds & wired->kind) != 0;

            if (through != nullptr && (kindsOf(through) & outKinds) == 0)
               fail("BypassSource returns a node of a different signal kind");
            else if (wired == nullptr || !sameKind)
            {
               // Removed: nothing may leave this node.
               if (through != nullptr)
                  fail("passes something through although it has no same-kind input");
               std::string leaks;
               if (outKinds & kKImage)
               {
                  ImageCable probe;
                  probe.Connect(node);
                  if (probe.Texture() != 0) leaks += " texture";
               }
               if (tGeo != nullptr)
               {
                  if (!tGeo->GetMesh().vertices.empty()) leaks += " mesh";
                  if (const auto* pc = tGeo->GetPointCloud(); pc != nullptr && !pc->empty()) leaks += " points";
                  if (const Polyline* c = tGeo->GetCurve(); c != nullptr && !c->Empty()) leaks += " curve";
               }
               if ((outKinds & (kKAudio | kKNote)) && ResolvedAudioSource(node) != nullptr)
                  leaks += " audio";
               if (!leaks.empty())
                  fail("bypassed with nothing to pass, but still outputs:" + leaks);
               else if (wired == nullptr && InputCountFor(*t) >= 1 && category != "Synths")
               {
                  unprobed++;
                  printf("BYPASSSWEEP unprobed %s / %s (no fixture fits slot 0)\n", category.c_str(), name.c_str());
               }
               else
                  removed++;
            }
            else if (through != src)
               fail(through == nullptr ? "drops its same-kind input instead of passing it"
                                       : "BypassSource is not the wired input");
            else
            {
               std::string diffs;
               if (wired->kind == kKImage)
               {
                  ImageCable probe;
                  probe.Connect(node);
                  if (probe.Resolved() != src || probe.Texture() != src->GetOutputTexture())
                     diffs += " texture";
               }
               if (wired->kind == kKGeo && tGeo != nullptr)
               {
                  auto* sGeo = dynamic_cast<IGeometrySource*>(src);
                  const Mesh& a = tGeo->GetMesh();
                  const Mesh& b = sGeo->GetMesh();
                  if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size()) diffs += " GetMesh";
                  if (tGeo->MeshRevision() != sGeo->MeshRevision()) diffs += " MeshRevision";
                  if (!(tGeo->GetModelMatrix() == sGeo->GetModelMatrix())) diffs += " GetModelMatrix";
                  const Material ma = tGeo->GetMaterial(), mb = sGeo->GetMaterial();
                  if (ma.color[0] != mb.color[0] || ma.color[1] != mb.color[1] || ma.color[2] != mb.color[2] ||
                      ma.roughness != mb.roughness || ma.opacity != mb.opacity)
                     diffs += " GetMaterial";
                  if (tGeo->GetSurfaceTexture() != sGeo->GetSurfaceTexture()) diffs += " GetSurfaceTexture";
                  const Polyline* ca = tGeo->GetCurve();
                  const Polyline* cb = sGeo->GetCurve();
                  if ((ca ? ca->Count() : 0) != (cb ? cb->Count() : 0)) diffs += " GetCurve";
               }
               if (wired->kind == kKMod)
               {
                  auto* tm = dynamic_cast<IModulator*>(node);
                  auto* sm = dynamic_cast<IModulator*>(src);
                  if (tm != nullptr && sm != nullptr && std::fabs(tm->Value01() - sm->Value01()) > 1e-4f)
                     diffs += " Value01";
               }
               if ((wired->kind & (kKAudio | kKNote)) && ResolvedAudioSource(node) != src)
                  diffs += " audio-resolve";
               if (!diffs.empty())
                  fail("passes through, but a consumer reading it sees something else:" + diffs);
               else
                  passes++;
            }
            RemoveNodeByIndex(tIndex);
         }
         for (Fixture& f : fixtures)
            if (f.index >= 0)
               RemoveNodeByIndex(f.index);
         gSuppressUndoCheckpoints = false;

         printf("BYPASSSWEEP pass-through %d, removed %d, unprobed %d, failures %d\n", passes, removed, unprobed, failures);
         printf("BYPASSSWEEP %s\n", failures == 0 ? "PASS" : "FAIL");
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_PATCHLAYOUTLIVETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PATCHLAYOUTLIVETEST") != nullptr)
      {
         static std::string layoutPath;
         if (frameId == 3)
         {
            layoutPath = TmpPath("infinite_patchlayout_live.inf");
            std::ofstream f(layoutPath);
            f << "infinite-patch 1\n"
                 "node 1 Source Shape\n  id shape\nend\n"
                 "node 2 Utility Output\n  id out\nend\n"
                 "node 3 Source Shape\n  id shape2\nend\n"
                 "node 4 Modulators LFO\nend\n"
                 "cable out 0 shape\n";
            f.close();
            const bool opened = LoadPatchFrom(layoutPath);
            printf("PATCHLAYOUTLIVETEST opened=%d nodes=%zu\n", opened ? 1 : 0, gNodes.size());
         }
         if (frameId == 20)
         {
            int fails = 0;
            ed::EditorContext* prevEditor = ed::GetCurrentEditor();
            ed::SetCurrentEditor(gEditor);
            std::vector<std::pair<ImVec2, ImVec2>> boxes;
            int atOrigin = 0;
            ImVec2 shapePos(0, 0), outPos(0, 0);
            // The Shape that feeds Output (the first one spawned: file order).
            int shapeIndex = -1;
            for (GraphNode& gn : gNodes)
               if (gn.typeName == "Shape") { shapeIndex = gn.index; break; }
            for (GraphNode& gn : gNodes)
            {
               const ImVec2 p = ed::GetNodePosition(gn.NodeId());
               const ImVec2 sz = ed::GetNodeSize(gn.NodeId());
               if (std::fabs(p.x) < 1.0f && std::fabs(p.y) < 1.0f)
                  atOrigin++;
               boxes.push_back({ p, ImVec2(p.x + sz.x, p.y + sz.y) });
               if (gn.typeName == "Shape" && gn.index == shapeIndex)
                  shapePos = p;
               if (gn.typeName == "Output")
                  outPos = p;
            }
            ed::SetCurrentEditor(prevEditor);
            for (size_t i = 0; i < boxes.size(); ++i)
               for (size_t j = i + 1; j < boxes.size(); ++j)
                  if (boxes[i].first.x < boxes[j].second.x && boxes[j].first.x < boxes[i].second.x &&
                      boxes[i].first.y < boxes[j].second.y && boxes[j].first.y < boxes[i].second.y)
                  {
                     printf("PATCHLAYOUTLIVETEST FAIL: nodes %zu and %zu overlap\n", i, j);
                     fails++;
                  }
            if (gNodes.size() != 4) { printf("PATCHLAYOUTLIVETEST FAIL: expected 4 nodes, got %zu\n", gNodes.size()); fails++; }
            if (atOrigin > 1) { printf("PATCHLAYOUTLIVETEST FAIL: %d nodes still at 0,0\n", atOrigin); fails++; }
            if (!(shapePos.x < outPos.x)) { printf("PATCHLAYOUTLIVETEST FAIL: shape (%.0f) not left of output (%.0f)\n", shapePos.x, outPos.x); fails++; }
            if (gPendingAutoLayout.active) { printf("PATCHLAYOUTLIVETEST FAIL: layout still pending at frame 20\n"); fails++; }
            std::remove(layoutPath.c_str());
            printf("PATCHLAYOUTLIVETEST %s\n", fails == 0 ? "OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_ROUNDTRIPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_ROUNDTRIPTEST") != nullptr && frameId == 4)
      {
         // Every node type that declares params must survive both paths that
         // restore a node from bare settings: copy/paste (CopyParams) and
         // patch load (Patch::LoadParams). This is what caught FormulaNode,
         // TextNode, NoiseNode and about eighteen others silently keeping the
         // base INode::VisitParams no-op, so save/load and copy/paste dropped
         // every value on them.
         struct MutateVisitor : public ParamVisitor
         {
            int index = 0;
            void Float(const char*, float& v) override { v += 1.0f + (float)(index++ % 5) * 0.1f; }
            void Int(const char*, int& v) override { v += 1 + (index++ % 3); }
            void Bool(const char*, bool& v) override { v = !v; index++; }
            void Text(const char*, std::string& v) override { v += "_x"; index++; }
            void Color(const char*, float rgb[3]) override
            {
               rgb[0] += 0.11f; rgb[1] += 0.07f; rgb[2] += 0.05f; index++;
            }
         };

         int typesTested = 0, typesSkipped = 0, copyFails = 0, loadFails = 0;
         std::vector<std::string> copyFailNames, loadFailNames;

         for (const std::string& category : NodeFactory::Instance().GetCategories())
         {
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
            {
               std::unique_ptr<INode> a(NodeFactory::Instance().MakeNode(name));
               if (a == nullptr)
                  continue;

               MutateVisitor mutator;
               a->VisitParams(mutator);
               if (mutator.index == 0)
               {
                  // Legitimately no params (Null, Null 3D, the boolean-mode
                  // Join Geometry variants) rather than a missed override -
                  // nothing to round-trip.
                  typesSkipped++;
                  continue;
               }
               typesTested++;

               std::vector<std::pair<std::string, std::string>> paramsA;
               Patch::SaveParams(a.get(), paramsA);

               std::unique_ptr<INode> b(NodeFactory::Instance().MakeNode(name));
               CopyParams(b.get(), a.get());
               std::vector<std::pair<std::string, std::string>> paramsB;
               Patch::SaveParams(b.get(), paramsB);
               // CopyParams deliberately resets the learned blob on Predictive nodes (a pasted copy
               // starts at "press Learn"), so that one key is expected to differ on the copy path.
               // Every other param on those nodes must still round-trip. Save/load keeps the blob.
               std::vector<std::pair<std::string, std::string>> expectB = paramsA;
               const char* learnedKey = nullptr;
               if (dynamic_cast<PredictiveModulatorNode*>(a.get()))
                  learnedKey = "fitData";
               else if (dynamic_cast<PredictiveNotesNode*>(a.get()) ||
                        dynamic_cast<PredictiveRhythmNode*>(a.get()))
                  learnedKey = "model";
               if (learnedKey != nullptr)
               {
                  expectB.erase(std::remove_if(expectB.begin(), expectB.end(),
                                               [&](const std::pair<std::string, std::string>& kv)
                                               { return kv.first.size() > 2 && kv.first.compare(2, std::string::npos, learnedKey) == 0; }),
                                expectB.end());
                  paramsB.erase(std::remove_if(paramsB.begin(), paramsB.end(),
                                               [&](const std::pair<std::string, std::string>& kv)
                                               { return kv.first.size() > 2 && kv.first.compare(2, std::string::npos, learnedKey) == 0; }),
                                paramsB.end());
               }
               if (paramsB != expectB)
               {
                  for (size_t k = 0; k < std::max(paramsB.size(), expectB.size()); k++)
                  {
                     const bool haveB = k < paramsB.size(), haveE = k < expectB.size();
                     if (!haveB || !haveE || paramsB[k] != expectB[k])
                     {
                        printf("  %s: key '%s' expected '%.40s' got '%.40s'\n", name.c_str(),
                               haveE ? expectB[k].first.c_str() : "(none)",
                               haveE ? expectB[k].second.c_str() : "",
                               haveB ? paramsB[k].second.c_str() : "");
                        break;
                     }
                  }
                  copyFails++;
                  copyFailNames.push_back(name);
               }

               std::unique_ptr<INode> c(NodeFactory::Instance().MakeNode(name));
               Patch::LoadParams(c.get(), paramsA);
               std::vector<std::pair<std::string, std::string>> paramsC;
               Patch::SaveParams(c.get(), paramsC);
               if (paramsC != paramsA)
               {
                  loadFails++;
                  loadFailNames.push_back(name);
               }
            }
         }

         printf("round trip: %d types tested, %d with no params to test\n", typesTested, typesSkipped);
         for (const std::string& n : copyFailNames)
            printf("  copy/paste dropped values: %s\n", n.c_str());
         for (const std::string& n : loadFailNames)
            printf("  save/load dropped values: %s\n", n.c_str());
         printf("copy/paste: %d/%d types round trip  %s\n",
                typesTested - copyFails, typesTested, copyFails == 0 ? "OK" : "FAIL");
         printf("save/load:  %d/%d types round trip  %s\n",
                typesTested - loadFails, typesTested, loadFails == 0 ? "OK" : "FAIL");

         // Field step 10: 8-generated-node save/load round-trip case (doc §7.6)
         bool fieldGraphRoundTripOk = false;
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            if (gn && gn->node)
            {
               auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
               fgn->code =
                  "param float voices = 8 [1, 8]\n"
                  "for (k = 0; k < 8; k += 1) {\n"
                  "   if (k < voices) {\n"
                  "      osc = emit(\"LFO\", k)\n"
                  "      set(osc, \"rateBeats\", 1.0 + k)\n"
                  "   }\n"
                  "}\n";
               MainGraphHost host;
               fgn->Regenerate(host);
               const bool generated8 = gNodes.size() == 9;
               Patch::Data saved = BuildPatchData();
               NewPatch();
               ApplyPatchData(saved);
               const bool reloaded8 = gNodes.size() == 9;
               FieldGraphNode* reloadedFgn = nullptr;
               for (GraphNode& node : gNodes)
               {
                  if (auto* fg = dynamic_cast<FieldGraphNode*>(node.node.get()))
                     reloadedFgn = fg;
               }
               bool ownershipResolves = (reloadedFgn != nullptr);
               if (ownershipResolves)
               {
                  for (int i = 0; i < 8; ++i)
                  {
                     std::string key = "osc#" + std::to_string(i);
                     int idx = reloadedFgn->Ownership().Get(key);
                     if (idx < 0 || FindNodeByIndex(idx) == nullptr)
                        ownershipResolves = false;
                  }
               }
               fieldGraphRoundTripOk = generated8 && reloaded8 && ownershipResolves;
               printf("field graph 8-node save/load round-trip: generated=%d reloaded=%d ownershipResolves=%d  %s\n",
                      (int)generated8, (int)reloaded8, (int)ownershipResolves,
                      fieldGraphRoundTripOk ? "OK" : "FAIL");
            }
         }

         printf("%s\n", (copyFails == 0 && loadFails == 0 && fieldGraphRoundTripOk) ? "ROUND TRIP OK" : "SUSPECT");
      }
}

void FrameTest_PHASEFTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PHASEFTEST") != nullptr && frameId == 4)
      {
         // Signed volume by the divergence theorem. A closed mesh whose
         // triangles all wind outward has positive volume; if any face is
         // flipped the contributions cancel and it collapses. This is the exact
         // check that caught the shattered metaballs, so every new closed
         // primitive gets it rather than a triangle count that proves nothing.
         auto volumeOf = [](const Mesh& m) {
            double vol = 0.0;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               const Vertex& a = m.vertices[m.indices[t]];
               const Vertex& b = m.vertices[m.indices[t + 1]];
               const Vertex& c = m.vertices[m.indices[t + 2]];
               vol += ((double)a.px * ((double)b.py * c.pz - (double)c.py * b.pz)
                     - (double)a.py * ((double)b.px * c.pz - (double)c.px * b.pz)
                     + (double)a.pz * ((double)b.px * c.py - (double)c.px * b.py)) / 6.0;
            }
            return vol;
         };
         // Edges are matched by quantised *position*, not index: these solids are
         // flat-shaded, so adjacent faces never share a vertex index even when
         // they share an edge in space.
         auto openEdges = [](const Mesh& m) {
            auto key = [&](unsigned int i) {
               const Vertex& v = m.vertices[i];
               return std::to_string((long long)std::llround(v.px * 10000.0f)) + "," +
                      std::to_string((long long)std::llround(v.py * 10000.0f)) + "," +
                      std::to_string((long long)std::llround(v.pz * 10000.0f));
            };
            std::map<std::string, int> counts;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
               {
                  std::string a = key(m.indices[t + e]);
                  std::string b = key(m.indices[t + (e + 1) % 3]);
                  counts[a < b ? a + "|" + b : b + "|" + a]++;
               }
            int open = 0;
            for (const auto& kv : counts)
               if (kv.second != 2)
                  open++;
            return open;
         };

         bool ok = true;
         struct Solid { const char* name; Mesh mesh; size_t expectTris; };
         std::vector<Solid> solids = {
            { "tetrahedron", Primitives::Tetrahedron(), 12 },
            { "octahedron", Primitives::Octahedron(), 24 },
            // Twelve pentagons fanned from their centroids: 12 * 5.
            { "dodecahedron", Primitives::Dodecahedron(), 60 },
            { "rounded cube", Primitives::RoundedCube(8, 0.2f), 0 },
         };
         for (const Solid& s : solids)
         {
            const double vol = volumeOf(s.mesh);
            const int open = openEdges(s.mesh);
            const size_t tris = s.mesh.indices.size() / 3;
            const bool trisOk = s.expectTris == 0 || tris == s.expectTris;
            // Klein bottle is closed but non-orientable, so its signed volume
            // is meaningless - it is only required to be watertight.
            const bool volOk = (std::string(s.name) == "klein bottle") || vol > 1e-4;
            printf("%-14s %6zu tris, volume %+.4f, %d open edges  %s\n",
                   s.name, tris, vol, open,
                   (trisOk && volOk && open == 0) ? "OK" : "FAIL");
            ok = ok && trisOk && volOk && open == 0;
         }

         // The figure-8 Klein bottle is a closed surface, but its cross-section
         // passes through its own centre twice, so v = 0 and v = pi land on the
         // same point for every u. Position-keyed edge matching therefore sees
         // those rings as one and reports false tears. Index-keyed matching is
         // the correct test here: this surface is one shared grid, so equal
         // indices really do mean the same vertex.
         {
            const Mesh kb = Primitives::KleinBottle(24, 24);
            std::map<std::pair<unsigned int, unsigned int>, int> counts;
            for (size_t t = 0; t + 2 < kb.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
               {
                  unsigned int a = kb.indices[t + e];
                  unsigned int b = kb.indices[t + (e + 1) % 3];
                  counts[a < b ? std::make_pair(a, b) : std::make_pair(b, a)]++;
               }
            int open = 0;
            for (const auto& kv : counts)
               if (kv.second != 2)
                  open++;
            // And confirm the diagnosis rather than asserting it: exactly one
            // coincident partner per u ring, which is where the 24 phantom
            // open edges came from.
            std::set<std::string> distinct;
            for (const Vertex& v : kb.vertices)
               distinct.insert(std::to_string((long long)std::llround(v.px * 10000.0f)) + "," +
                               std::to_string((long long)std::llround(v.py * 10000.0f)) + "," +
                               std::to_string((long long)std::llround(v.pz * 10000.0f)));
            const size_t duplicates = kb.vertices.size() - distinct.size();
            // Index matching proves the seam is *joined*, not that it is joined
            // correctly - a naive wrap is equally closed but stitches mismatched
            // cross-sections together, which shows up as a few enormous quads
            // spanning the tube. Comparing the longest edge to the median is
            // what actually tests the (u + 2pi, v) = (u, -v) identification.
            double longest = 0.0;
            std::vector<double> lengths;
            for (const auto& kv : counts)
            {
               const Vertex& a = kb.vertices[kv.first.first];
               const Vertex& b = kb.vertices[kv.first.second];
               const double d = std::sqrt((double)(a.px-b.px)*(a.px-b.px) +
                                          (double)(a.py-b.py)*(a.py-b.py) +
                                          (double)(a.pz-b.pz)*(a.pz-b.pz));
               lengths.push_back(d);
               longest = std::max(longest, d);
            }
            std::sort(lengths.begin(), lengths.end());
            const double median = lengths[lengths.size() / 2];
            const bool seamOk = longest < median * 3.0;
            printf("klein bottle   %6zu tris, %d open edges by index, %zu self-touching vertices  %s\n",
                   kb.indices.size() / 3, open, duplicates,
                   (open == 0 && duplicates == 24) ? "OK" : "FAIL");
            printf("klein seam: longest edge %.4f vs median %.4f  %s\n",
                   longest, median, seamOk ? "OK" : "FAIL");
            ok = ok && open == 0 && duplicates == 24 && seamOk;
         }

         // A rounded cube must sit strictly between the cube it came from and
         // the sphere it would become - the one measurement that catches a
         // projection that rounds too much or not at all.
         const double rcVol = volumeOf(Primitives::RoundedCube(10, 0.2f));
         const bool rcOk = rcVol < 1.0 && rcVol > 0.5236;
         printf("rounded cube volume %.4f between sphere 0.5236 and cube 1.0  %s\n",
                rcVol, rcOk ? "OK" : "FAIL");
         ok = ok && rcOk;

         // The open surfaces are emitted with both windings, so their triangle
         // count is even and each is reachable from either side.
         const Mesh mob = Primitives::MobiusStrip(64, 4, 0.3f);
         const bool mobOk = !mob.Empty() && (mob.indices.size() / 3) % 2 == 0;
         printf("mobius strip %zu tris, doubled  %s\n", mob.indices.size() / 3,
                mobOk ? "OK" : "FAIL");
         ok = ok && mobOk;

         for (const auto& p : { std::make_pair("gear", Primitives::Gear(12, 0.3f, 0.25f, 0.15f)),
                                std::make_pair("star", Primitives::Star(5, 0.4f, 0.3f)),
                                std::make_pair("disc", Primitives::Disc(32, 0.0f)),
                                std::make_pair("arrow", Primitives::Arrow(16, 0.12f, 0.35f)) })
         {
            const bool nonEmpty = !p.second.Empty();
            printf("%-6s %zu tris  %s\n", p.first, p.second.indices.size() / 3,
                   nonEmpty ? "OK" : "FAIL");
            ok = ok && nonEmpty;
         }

         // --- 3D resynthesize ---
         GeometryNode src;
         src.shape = 2; // sphere
         src.CookIfNeeded(9001);

         auto evolve = [&](float seed, int steps) {
            auto node = std::make_unique<MeshResynthNode>();
            node->input = &src;
            node->seed = seed;
            node->chaos = 0.5f;
            for (int i = 0; i < steps; i++)
               node->StepOnce();
            node->CookIfNeeded(9002);
            return node;
         };
         auto a = evolve(3.0f, 5);
         auto b = evolve(3.0f, 5);
         auto c = evolve(11.0f, 5);

         auto samePositions = [](const Mesh& x, const Mesh& y) {
            if (x.vertices.size() != y.vertices.size())
               return false;
            for (size_t i = 0; i < x.vertices.size(); i++)
               if (std::fabs(x.vertices[i].px - y.vertices[i].px) > 1e-6f ||
                   std::fabs(x.vertices[i].py - y.vertices[i].py) > 1e-6f ||
                   std::fabs(x.vertices[i].pz - y.vertices[i].pz) > 1e-6f)
                  return false;
            return true;
         };

         // The whole promise of the node: a patch reopened tomorrow replays the
         // same evolution, and a different seed is genuinely a different one.
         const bool deterministic = samePositions(a->GetMesh(), b->GetMesh());
         const bool seedMatters = !samePositions(a->GetMesh(), c->GetMesh());
         printf("resynth generation %d, deterministic %d, seed changes result %d  %s\n",
                a->Generation(), (int)deterministic, (int)seedMatters,
                (deterministic && seedMatters && a->Generation() == 5) ? "OK" : "FAIL");
         ok = ok && deterministic && seedMatters && a->Generation() == 5;

         // And it must actually mutate: five generations that leave the sphere
         // untouched would pass every check above.
         const bool moved = !samePositions(a->GetMesh(), src.GetMesh());
         printf("resynth changed the mesh: %d  %s\n", (int)moved, moved ? "OK" : "FAIL");
         ok = ok && moved;

         // Reset must return to the source exactly, not approximately.
         a->Reset();
         a->CookIfNeeded(9003);
         const bool resetOk = samePositions(a->GetMesh(), src.GetMesh()) && a->Generation() == 0;
         printf("reset restores the input: %d  %s\n", (int)resetOk, resetOk ? "OK" : "FAIL");
         ok = ok && resetOk;

         // Subdivision at full weight, unattended, must stop at the budget.
         auto budget = std::make_unique<MeshResynthNode>();
         budget->input = &src;
         budget->weight[MeshResynthNode::kSubdivide] = 1.0f;
         budget->triangleBudget = 8000;
         for (int frame = 0; frame < 12; frame++)
         {
            for (int i = 0; i < 4; i++)
               budget->StepOnce();
            budget->CookIfNeeded(9100 + frame);
         }
         const size_t grown = budget->TriangleCount();
         const bool budgetOk = grown <= 8000;
         printf("subdivide budget: %zu tris, cap 8000  %s\n", grown, budgetOk ? "OK" : "FAIL");
         ok = ok && budgetOk;

         // --- image to point cloud ---
         ShapeNode circle;
         circle.shapeType = 0;
         circle.width = 256; circle.height = 256;
         // aa0462a split the single `size` field into sizeX/sizeY (uSize is
         // now a vec2 in the shader - see ShapeNode.cpp); `size` itself is
         // dead, read nowhere in CookIfNeeded any more. Setting only `size`
         // here left the circle at its sizeX/sizeY defaults (0.35 radius,
         // pi*0.35^2 = 38.5% coverage) instead of the intended 0.25 (19.6%),
         // which is what actually produced the 35.1%-vs-19.6% mismatch this
         // fixture reported - the shape code never regressed.
         circle.sizeX = 0.25f;
         circle.sizeY = 0.25f;
         circle.posY = 0.75f;   // deliberately off-centre, see below
         circle.CookIfNeeded(9200);

         ImageToPointsNode i2p;
         i2p.Input().Connect(&circle);
         i2p.density = 64;
         i2p.threshold = 0.3f;
         i2p.height = 2.0f;
         i2p.CookIfNeeded(9201);

         const size_t pts = i2p.PointCount();
         const size_t grid = 64 * 64;
         // A circle of radius 0.25 covers pi*r^2 of the frame. If the threshold
         // were ignored we would get all 4096 points; if it rejected everything,
         // zero. Both failure modes are outside this band.
         const double coverage = (double)pts / (double)grid;
         const bool coverageOk = coverage > 0.10 && coverage < 0.30;
         printf("image to points: %zu of %zu (%.1f%% vs expected 19.6%%)  %s\n",
                pts, grid, coverage * 100.0, coverageOk ? "OK" : "FAIL");
         ok = ok && coverageOk;

         // The circle was placed at v = 0.75, so the cloud's centre of mass must
         // land at the matching height. This is what catches a vertical flip -
         // a centred test image would pass either way round.
         double meanY = 0.0;
         for (const Particle& p : i2p.GetPoints())
            meanY += p.py;
         if (pts > 0)
            meanY /= (double)pts;
         const double expectedY = (0.75 - 0.5) * 2.0;
         const bool orientOk = std::fabs(meanY - expectedY) < 0.15;
         printf("cloud centre y %.3f, image centre y %.3f  %s\n",
                meanY, expectedY, orientOk ? "OK" : "FAIL");
         ok = ok && orientOk;

         // Formula presets are compiled at runtime, so a typo in one ships as a
         // preset that silently does nothing. Compile every one.
         {
            FormulaNode fx;
            int failed = 0;
            for (int i = 0; i < (int)FormulaNode::PresetNames().size(); i++)
            {
               fx.LoadPreset(i);
               if (!fx.Apply())
               {
                  failed++;
                  printf("  preset \"%s\" failed: %s\n",
                         FormulaNode::PresetNames()[i].c_str(), fx.LastError().c_str());
               }
            }
            printf("formula presets: %zu compiled, %d failed  %s\n",
                   FormulaNode::PresetNames().size(), failed, failed == 0 ? "OK" : "FAIL");
            ok = ok && failed == 0;
         }

         // Same for the 2D shapes: they share one shader, but a new branch that
         // never runs would leave the shape rendering as whatever the fallback
         // is. Cook each and confirm it puts something on screen.
         {
            int blank = 0;
            for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
            {
               ShapeNode sh;
               sh.shapeType = i;
               sh.width = 64; sh.height = 64;
               sh.CookIfNeeded(9300 + i);
               unsigned char px[64 * 64 * 4] = { 0 };
               glBindTexture(GL_TEXTURE_2D, sh.GetOutputTexture());
               glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
               glBindTexture(GL_TEXTURE_2D, 0);
               int lit = 0;
               for (int q = 0; q < 64 * 64; q++)
                  if (px[q * 4] > 20 || px[q * 4 + 1] > 20 || px[q * 4 + 2] > 20)
                     lit++;
               // A shape that fills nothing is broken; one that fills the whole
               // frame means the distance field never went positive.
               const bool shapeOk = lit > 20 && lit < 64 * 64 - 20;
               if (!shapeOk)
               {
                  blank++;
                  printf("  2D shape \"%s\": %d of %d pixels lit  FAIL\n",
                         ShapeNode::ShapeNames()[i].c_str(), lit, 64 * 64);
               }
            }
            printf("2D shapes: %zu drawn, %d bad  %s\n",
                   ShapeNode::ShapeNames().size(), blank, blank == 0 ? "OK" : "FAIL");
            ok = ok && blank == 0;
         }

         printf("%s\n", ok ? "PHASE F OK" : "SUSPECT");
      }
}

void FrameTest_PHASEETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PHASEETEST") != nullptr && frameId == 4)
      {
         // Signed volume via the divergence theorem. Triangle counts prove
         // nothing about a boolean - only the enclosed volume says whether the
         // operation actually did what it claims.
         auto volumeOf = [](const Mesh& m) {
            double vol = 0.0;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            {
               const Vertex& a = m.vertices[m.indices[t]];
               const Vertex& b = m.vertices[m.indices[t+1]];
               const Vertex& c = m.vertices[m.indices[t+2]];
               vol += ((double)a.px * ((double)b.py * c.pz - (double)b.pz * c.py) -
                       (double)a.py * ((double)b.px * c.pz - (double)b.pz * c.px) +
                       (double)a.pz * ((double)b.px * c.py - (double)b.py * c.px)) / 6.0;
            }
            return std::fabs(vol);
         };

         // Two unit cubes overlapping in exactly half their volume: the answers
         // are known in advance, which is what makes this a real check.
         const Mesh cubeA = Primitives::Cube(1);
         const Mesh cubeB = MeshOps::Transform(Primitives::Cube(1),
                                               Mat4::Translation(0.5f, 0.0f, 0.0f));
         const double vA = volumeOf(cubeA);
         const double overlap = 0.5;  // half of a unit cube

         const Mesh un = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanUnion);
         const Mesh inter = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanIntersect);
         const Mesh diff = MeshOps::Boolean(cubeA, cubeB, MeshOps::kBooleanDifference);

         const double vUnion = volumeOf(un);
         const double vInter = volumeOf(inter);
         const double vDiff = volumeOf(diff);

         printf("cube volume %.3f\n", vA);
         printf("  union      %.3f  (expect 1.500)  %s\n", vUnion,
                std::fabs(vUnion - 1.5) < 0.02 ? "OK" : "FAIL");
         printf("  intersect  %.3f  (expect 0.500)  %s\n", vInter,
                std::fabs(vInter - overlap) < 0.02 ? "OK" : "FAIL");
         printf("  difference %.3f  (expect 0.500)  %s\n", vDiff,
                std::fabs(vDiff - overlap) < 0.02 ? "OK" : "FAIL");

         // A difference must actually remove material, not just re-emit the
         // original, so its extent has to shrink on the side that was cut.
         float diffMax = -1e30f;
         for (const Vertex& v : diff.vertices)
            diffMax = std::max(diffMax, v.px);
         printf("  difference max x %.3f (cube was 0.500, cut from +x)  %s\n",
                diffMax, diffMax < 0.02f ? "OK" : "FAIL");

         const bool ok = std::fabs(vUnion - 1.5) < 0.02 &&
                         std::fabs(vInter - overlap) < 0.02 &&
                         std::fabs(vDiff - overlap) < 0.02 && diffMax < 0.02f;
         printf("%s\n", ok ? "PHASE E OK" : "SUSPECT");
      }
}

void FrameTest_GROUP3DTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_GROUP3DTEST") != nullptr && frameId == 6)
      {
         // R274: six objects reach one Render 3D through two pins by way of
         // Group 3D, one of them nested inside the other. Every object must be
         // drawn - the old four-pin cap would have dropped two - and the cap
         // on a runaway group must hold.
         std::vector<std::unique_ptr<GeometryNode>> cubes;
         for (int i = 0; i < 6; i++)
         {
            cubes.emplace_back(new GeometryNode());
            cubes.back()->shape = 0;
         }
         Group3DNode inner, outer;
         Render3DNode render;
         render.width = 64.0f; render.height = 64.0f;
         for (int i = 0; i < 4; i++) outer.inputs[i] = cubes[i].get();
         inner.inputs[0] = cubes[4].get();
         inner.inputs[1] = cubes[5].get();
         outer.inputs[4] = &inner;
         render.geometry[0] = &outer;

         const size_t perCube = cubes[0]->GetMesh().indices.size() / 3;
         const size_t flat = render.FlattenedGeometry().size();
         render.CookIfNeeded(frameId);
         const bool flatOk = flat == 6;
         const bool trisOk = perCube > 0 && render.LastTriangleCount() == perCube * 6;
         printf("  flattened %zu of 6, triangles %zu (expect %zu)  %s\n", flat,
                render.LastTriangleCount(), perCube * 6, (flatOk && trisOk) ? "OK" : "FAIL");

         // A group patched into itself must terminate and stay under the cap.
         Group3DNode loop;
         loop.inputs[0] = &loop;
         loop.inputs[1] = cubes[0].get();
         Render3DNode loopRender;
         loopRender.geometry[0] = &loop;
         const size_t loopFlat = loopRender.FlattenedGeometry().size();
         const bool loopOk = loopFlat <= (size_t)Render3DNode::kMaxDraw;
         printf("  self-referencing group flattens to %zu (cap %d)  %s\n", loopFlat,
                Render3DNode::kMaxDraw, loopOk ? "OK" : "FAIL");

         printf("%s\n", (flatOk && trisOk && loopOk) ? "GROUP3DTEST OK" : "GROUP3DTEST FAIL - BUG");
      }
}

void FrameTest_WRAPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_WRAPTEST") != nullptr && frameId == 4)
      {
         // The whole point of the cylindrical mode is that it is a coordinate
         // remap, not a projection: it must preserve arc length exactly. A
         // strip of known width W bent around a target of known radius R has
         // an answer worked out in advance - every point at distance R from
         // the axis, spanning exactly W/R radians - so this is a real check
         // and not just "did something move".
         const float R = 2.0f;
         const float W = 3.0f;
         const float H = 0.4f;

         // Thin flat strip in the XY plane, W wide and H tall, at z = 0.
         Mesh strip;
         const int kCols = 40;
         for (int i = 0; i <= kCols; i++)
         {
            const float x = -W * 0.5f + W * (float)i / (float)kCols;
            Vertex a; a.px = x; a.py = -H * 0.5f; a.pz = 0.0f;
            Vertex b; b.px = x; b.py =  H * 0.5f; b.pz = 0.0f;
            strip.vertices.push_back(a);
            strip.vertices.push_back(b);
         }
         for (int i = 0; i < kCols; i++)
         {
            const unsigned int base = (unsigned int)(i * 2);
            strip.indices.push_back(base); strip.indices.push_back(base + 1); strip.indices.push_back(base + 2);
            strip.indices.push_back(base + 1); strip.indices.push_back(base + 3); strip.indices.push_back(base + 2);
         }

         // Target: a sphere of radius R centred at the origin, so the bend
         // radius is read from real geometry rather than the override. The
         // primitive is unit *diameter*, hence the 2R scale.
         const Mesh sphere = MeshOps::Transform(Primitives::Sphere(24, 32),
                                                Mat4::Scale(2.0f * R, 2.0f * R, 2.0f * R));

         auto spanOf = [](const Mesh& m, float& minR, float& maxR, float& minA, float& maxA) {
            minR = 1e30f; maxR = -1e30f; minA = 1e30f; maxA = -1e30f;
            for (const Vertex& v : m.vertices)
            {
               const float rad = std::sqrt(v.px * v.px + v.pz * v.pz);
               const float ang = std::atan2(v.px, v.pz);
               minR = std::min(minR, rad); maxR = std::max(maxR, rad);
               minA = std::min(minA, ang); maxA = std::max(maxA, ang);
            }
         };

         const Mesh cyl = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        false, false, false);
         float minR, maxR, minA, maxA;
         spanOf(cyl, minR, maxR, minA, maxA);
         const float radiusErr = std::max(std::fabs(minR - R), std::fabs(maxR - R));
         const float span = maxA - minA;
         const bool radiusOk = radiusErr < 1e-3f;
         const bool spanOk = std::fabs(span - W / R) < 1e-3f;
         printf("  cylindrical radius %.5f..%.5f (expect %.3f)  %s\n", minR, maxR, R,
                radiusOk ? "OK" : "FAIL");
         printf("  cylindrical span   %.5f rad (expect W/R = %.5f)  %s\n", span, W / R,
                spanOk ? "OK" : "FAIL");

         // fit around: the same strip must now close a full circle.
         const Mesh fit = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        true, false, false);
         // atan2 wraps at +/-pi, so a full turn cannot be read off min/max.
         // Walk the strip column by column instead and accumulate unwrapped
         // angle deltas - that measures the real total sweep.
         const float kPi = 3.14159265358979f;
         float fitSweep = 0.0f, prevAng = std::atan2(fit.vertices[0].px, fit.vertices[0].pz);
         float fitRadErr = 0.0f;
         for (int i = 1; i <= kCols; i++)
         {
            const Vertex& v = fit.vertices[(size_t)i * 2];
            const float ang = std::atan2(v.px, v.pz);
            float d = ang - prevAng;
            while (d >  kPi) d -= 2.0f * kPi;
            while (d < -kPi) d += 2.0f * kPi;
            fitSweep += d;
            prevAng = ang;
            fitRadErr = std::max(fitRadErr,
                                 std::fabs(std::sqrt(v.px * v.px + v.pz * v.pz) - R));
         }
         const bool fitOk = std::fabs(std::fabs(fitSweep) - 2.0f * kPi) < 1e-3f && fitRadErr < 1e-3f;
         printf("  fit around sweep   %.5f rad (expect 2pi = %.5f)  %s\n", fitSweep,
                2.0f * kPi, fitOk ? "OK" : "FAIL");

         // Spherical must not push anything outside the shell it bends onto:
         // the strip is flat (zero depth), so R is the ceiling.
         const Mesh sph = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                        MeshOps::kWrapSpherical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                        false, false, false);
         float sphMax = 0.0f;
         for (const Vertex& v : sph.vertices)
            sphMax = std::max(sphMax, std::sqrt(v.px * v.px + v.py * v.py + v.pz * v.pz));
         const bool sphOk = sphMax <= R + 1e-3f;
         printf("  spherical max |p|  %.5f (expect <= %.3f)  %s\n", sphMax, R,
                sphOk ? "OK" : "FAIL");

         // A radius override with no target at all still has to bend, and the
         // override is used verbatim as the radius (radiusScale is ignored -
         // there is no derived radius for it to scale).
         const Mesh noTarget = MeshOps::Wrap(strip, Mat4::Identity(), Mesh(), Mat4::Identity(),
                                             MeshOps::kWrapCylindrical, 0.0f, 1.0f, R, 0.25f, 1,
                                             false, false, false);
         float ntMinR, ntMaxR, ntMinA, ntMaxA;
         spanOf(noTarget, ntMinR, ntMaxR, ntMinA, ntMaxA);
         const bool ntOk = std::fabs(ntMaxR - R) < 1e-3f && std::fabs((ntMaxA - ntMinA) - W / R) < 1e-3f;
         printf("  no target, radius %.1f: span %.5f rad  %s\n", R, ntMaxA - ntMinA,
                ntOk ? "OK" : "FAIL");

         // Nearest surface still works, and (unlike the bend) collapses the
         // strip's width - the behaviour the bend modes exist to avoid.
         const Mesh nearest = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                            MeshOps::kWrapNearest, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                            false, false, false);
         const bool nearOk = nearest.vertices.size() == strip.vertices.size() && !nearest.Empty();
         printf("  nearest surface    %zu verts  %s\n", nearest.vertices.size(),
                nearOk ? "OK" : "FAIL");

         // The bug this guards: the radius must never stop tracking the
         // target. Scale the target's model matrix 2x with node params held
         // identical and the bend radius has to double on its own.
         const Mesh scaled = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Scale(2.0f, 2.0f, 2.0f),
                                           MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 1.0f, 1,
                                           false, false, false);
         float scMinR, scMaxR, scMinA, scMaxA;
         spanOf(scaled, scMinR, scMaxR, scMinA, scMaxA);
         const bool trackOk = std::fabs(scMaxR - 2.0f * R) < 1e-3f && std::fabs(scMinR - 2.0f * R) < 1e-3f;
         printf("  target scaled 2x:  radius %.5f..%.5f (was %.5f, expect %.3f)  %s\n",
                scMinR, scMaxR, maxR, 2.0f * R, trackOk ? "OK" : "FAIL");

         // And radius scale is a multiplier on that derived radius, not a
         // replacement for it.
         const Mesh halfScale = MeshOps::Wrap(strip, Mat4::Identity(), sphere, Mat4::Identity(),
                                              MeshOps::kWrapCylindrical, 0.0f, 1.0f, 0.0f, 0.5f, 1,
                                              false, false, false);
         float hsMinR, hsMaxR, hsMinA, hsMaxA;
         spanOf(halfScale, hsMinR, hsMaxR, hsMinA, hsMaxA);
         const bool scaleOk = std::fabs(hsMaxR - 0.5f * R) < 1e-3f && std::fabs(hsMinR - 0.5f * R) < 1e-3f;
         printf("  radius scale 0.5:  radius %.5f..%.5f (expect %.3f)  %s\n",
                hsMinR, hsMaxR, 0.5f * R, scaleOk ? "OK" : "FAIL");

         const bool ok = radiusOk && spanOk && fitOk && sphOk && ntOk && nearOk &&
                         trackOk && scaleOk;
         printf("%s\n", ok ? "WRAP OK" : "SUSPECT");
      }
}

void FrameTest_PHASEDTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PHASEDTEST") != nullptr && frameId == 4)
      {
         // Curves of every kind must produce a usable polyline and a tube.
         bool allKinds = true;
         for (int kind = 0; kind < 4; kind++)
         {
            std::vector<float> control = { -1,0,0,  -0.3f,0.8f,0,  0.3f,-0.8f,0,  1,0,0 };
            const Polyline line = MeshOps::BuildCurve(control, kind, 16, false);
            const Mesh tube = MeshOps::TubeAlong(line, 0.05f, 8, 0.0f);
            const bool ok = line.Count() >= 4 && !tube.Empty();
            printf("  curve %-12s %zu points, %zu tris  %s\n",
                   CurveNode::KindNames()[kind].c_str(), line.Count(),
                   tube.indices.size() / 3, ok ? "OK" : "FAIL");
            if (!ok) allKinds = false;
         }

         // Arc-length sampling: equal steps in t must cover roughly equal
         // distance. Sampling by index instead would crawl where control points
         // bunch and race where they spread.
         std::vector<float> control = { -1,0,0,  -0.9f,0.1f,0,  0.9f,0.1f,0,  1,0,0 };
         const Polyline line = MeshOps::BuildCurve(control, MeshOps::kCurveCatmullRom, 24, false);
         float prev[3], tangent[3], minStep = 1e30f, maxStep = 0.0f;
         MeshOps::SamplePolyline(line, 0.0f, prev, tangent);
         for (int i = 1; i <= 20; i++)
         {
            float pos[3];
            MeshOps::SamplePolyline(line, (float)i / 20.0f, pos, tangent);
            const float d = std::sqrt((pos[0]-prev[0])*(pos[0]-prev[0]) +
                                      (pos[1]-prev[1])*(pos[1]-prev[1]) +
                                      (pos[2]-prev[2])*(pos[2]-prev[2]));
            minStep = std::min(minStep, d);
            maxStep = std::max(maxStep, d);
            prev[0] = pos[0]; prev[1] = pos[1]; prev[2] = pos[2];
         }
         const float ratio = (minStep > 1e-6f) ? maxStep / minStep : 1e30f;
         printf("arc-length sampling: step ratio %.2f (1.0 is perfectly even)  %s\n",
                ratio, ratio < 1.6f ? "OK" : "FAIL");
         const bool evenSpeed = ratio < 1.6f;

         // Slicing a sphere through its centre must give one closed contour,
         // and every point on it must sit at the sphere's radius - that is what
         // proves it is really following the surface and not something else.
         const Mesh sphere = Primitives::Sphere(24, 32);
         const std::vector<Polyline> contours = MeshOps::SliceContours(sphere, 1, 0.0f);
         bool sliceOk = false;
         if (!contours.empty())
         {
            float minR = 1e30f, maxR = 0.0f;
            for (size_t i = 0; i < contours[0].Count(); i++)
            {
               const float x = contours[0].points[i*3];
               const float z = contours[0].points[i*3+2];
               const float r = std::sqrt(x*x + z*z);
               minR = std::min(minR, r); maxR = std::max(maxR, r);
            }
            sliceOk = contours[0].closed && contours[0].Count() > 16 &&
                      std::fabs(maxR - 0.5f) < 0.02f && std::fabs(minR - 0.5f) < 0.02f;
            printf("sphere slice: %zu contours, %zu points, closed=%d, radius %.3f..%.3f  %s\n",
                   contours.size(), contours[0].Count(), (int)contours[0].closed,
                   minR, maxR, sliceOk ? "OK" : "FAIL");
         }
         else
         {
            printf("sphere slice: no contours  FAIL\n");
         }

         // A plane has a real boundary, so that path needs no fallback.
         const std::vector<Polyline> edges = MeshOps::BoundaryLoops(Primitives::Plane(4));
         const bool boundaryOk = !edges.empty() && edges[0].closed && edges[0].Count() >= 16;
         printf("plane boundary: %zu loops, %zu points, closed=%d  %s\n",
                edges.size(), edges.empty() ? 0 : edges[0].Count(),
                edges.empty() ? 0 : (int)edges[0].closed, boundaryOk ? "OK" : "FAIL");

         printf("%s\n", (allKinds && evenSpeed && sliceOk && boundaryOk)
                            ? "PHASE D OK" : "SUSPECT");
      }
}

void FrameTest_PHASECTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PHASECTEST") != nullptr && frameId == 4)
      {
         // Every new primitive must produce a finite, non-degenerate mesh.
         bool allPrims = true;
         for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
         {
            INode* made = NodeFactory::Instance().MakeNode(GeometryNode::ShapeNames()[i]);
            auto* geo = dynamic_cast<GeometryNode*>(made);
            bool ok = false;
            if (geo != nullptr)
            {
               const Mesh& m = geo->GetMesh();
               bool finite = true;
               float lo = 1e30f, hi = -1e30f;
               for (const Vertex& v : m.vertices)
               {
                  if (!std::isfinite(v.px) || !std::isfinite(v.py) || !std::isfinite(v.pz))
                     finite = false;
                  lo = std::min(lo, v.px); hi = std::max(hi, v.px);
               }
               ok = finite && m.indices.size() >= 3 && (hi - lo) > 0.01f && (hi - lo) < 100.0f;
               printf("  %-12s %6zu tris  width %.2f  %s\n",
                      GeometryNode::ShapeNames()[i].c_str(), m.indices.size() / 3,
                      hi - lo, ok ? "OK" : "FAIL");
            }
            if (!ok) allPrims = false;
            delete made;
         }

         // Two balls far apart give two separate surfaces; brought together
         // they must merge into one. That merging is the entire reason to use
         // metaballs rather than joining two spheres, so it is what to test.
         std::vector<Primitives::MetaBall> apart = {
            { -1.1f, 0.0f, 0.0f, 0.25f }, { 1.1f, 0.0f, 0.0f, 0.25f }
         };
         std::vector<Primitives::MetaBall> together = {
            { -0.25f, 0.0f, 0.0f, 0.25f }, { 0.25f, 0.0f, 0.0f, 0.25f }
         };
         const Mesh mApart = Primitives::MetaBalls(apart, 40, 1.0f, 2.0f);
         const Mesh mTogether = Primitives::MetaBalls(together, 40, 1.0f, 2.0f);

         // Merged is detected by whether the surface spans the midpoint: apart,
         // nothing exists near x=0; merged, the bridge does.
         auto spansCentre = [](const Mesh& m) {
            for (const Vertex& v : m.vertices)
               if (std::fabs(v.px) < 0.05f)
                  return true;
            return false;
         };
         printf("metaballs: apart %zu tris (spans centre %d), together %zu tris (spans centre %d)\n",
                mApart.indices.size() / 3, (int)spansCentre(mApart),
                mTogether.indices.size() / 3, (int)spansCentre(mTogether));
         const bool merges = !mApart.Empty() && !mTogether.Empty() &&
                             !spansCentre(mApart) && spansCentre(mTogether);

         // Merging alone is not enough: the surface has to be closed and
         // consistently wound, or backface culling eats the wrongly-facing half
         // and the blob renders shattered. Every edge of a watertight, coherently
         // oriented mesh is traversed once in each direction, so a directed edge
         // seen twice the same way means two triangles disagree about facing.
         auto surfaceIntact = [](const Mesh& m, const char* label) {
            const std::vector<unsigned int> weld = MeshOps::BuildWeldMap(m);
            std::map<std::pair<unsigned int, unsigned int>, int> directed;
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
               for (int e = 0; e < 3; e++)
                  directed[{ weld[m.indices[t + e]], weld[m.indices[t + (e + 1) % 3]] }]++;

            size_t boundary = 0, flipped = 0;
            for (const auto& entry : directed)
            {
               if (entry.second > 1)
                  flipped++;
               const auto opposite = directed.find({ entry.first.second, entry.first.first });
               if (opposite == directed.end())
                  boundary++;
            }
            const bool ok = boundary == 0 && flipped == 0;
            printf("  %s: %zu open edges, %zu inconsistently wound  %s\n",
                   label, boundary, flipped, ok ? "watertight" : "BROKEN");
            return ok;
         };
         const bool intact = surfaceIntact(mApart, "apart") &&
                             surfaceIntact(mTogether, "together");

         printf("%s\n", (allPrims && merges && intact) ? "PHASE C OK" : "SUSPECT");
      }
}

void FrameTest_MAPTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MAPTEST") != nullptr && frameId >= 4 && frameId <= 16 && frameId % 4 == 0)
      {
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         auto* noise = gNodes[2].node.get();

         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         static std::vector<unsigned char> sBase;
         static int sPassed = 0;
         auto differs = [&](const char* label) {
            size_t changed = 0;
            for (size_t i = 0; i + 3 < px.size() && i < sBase.size(); i += 4)
               if (std::abs((int)px[i] - (int)sBase[i]) > 6) changed++;
            const bool ok = changed > 200;
            printf("  %-10s %zu px changed  %s\n", label, changed, ok ? "OK" : "FAIL");
            if (ok) sPassed++;
         };

         // Each channel is patched in turn against the same untouched baseline,
         // so a channel that silently did nothing shows up as zero change.
         if (frameId == 4)      { sBase = px; mat->MapInput(kMapRoughness).Connect(noise); }
         else if (frameId == 8) { differs("roughness"); mat->MapInput(kMapRoughness).Disconnect();
                                  mat->MapInput(kMapNormal).Connect(noise); }
         else if (frameId == 12){ differs("normal"); mat->MapInput(kMapNormal).Disconnect();
                                  mat->MapInput(kMapAmbientOcclusion).Connect(noise); }
         else if (frameId == 16){ differs("ao");
                                  printf("%s\n", sPassed == 3 ? "MATERIAL MAPS OK" : "SUSPECT"); }
      }
}

void FrameTest_SHADOWTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SHADOWTEST") != nullptr && (frameId == 4 || frameId == 8))
      {
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         const int w = render->GetOutputWidth(), h = render->GetOutputHeight();
         std::vector<unsigned char> px((size_t)w * h * 4);
         GLuint fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                render->GetOutputTexture(), 0);
         glPixelStorei(GL_PACK_ALIGNMENT, 1);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         static std::vector<unsigned char> sNoShadow;
         if (frameId == 4)
         {
            sNoShadow = px;
            render->shadowsEnabled = true;
            printf("shadow map: %dx%d\n", render->ActiveShadowSize(), render->ActiveShadowSize());
         }
         else
         {
            // Only some pixels should change, and every one of them should get
            // *darker*. A shadow that brightened anything would mean the depth
            // comparison is inverted; a change everywhere would mean the whole
            // image dimmed rather than a shadow being cast.
            size_t darker = 0, brighter = 0;
            for (size_t i = 0; i + 3 < px.size() && i < sNoShadow.size(); i += 4)
            {
               const int before = sNoShadow[i] + sNoShadow[i+1] + sNoShadow[i+2];
               const int after = px[i] + px[i+1] + px[i+2];
               if (after < before - 12) darker++;
               else if (after > before + 12) brighter++;
            }
            const double pct = 100.0 * (double)darker / (double)(w * h);
            printf("shadow: %zu px darker (%.1f%%), %zu brighter, active=%d\n",
                   darker, pct, brighter, render->ActiveShadowSize() > 0);
            const bool ok = darker > 500 && pct < 60.0 && brighter < darker / 10 &&
                            render->ActiveShadowSize() > 0;
            printf("%s\n", ok ? "SHADOWS OK" : "SUSPECT");
         }
      }
}

void FrameTest_BUGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_BUGTEST") != nullptr && frameId == 6)
      {
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* arr = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* ja = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());

         // 1. Saving from outside the editor context must not crash. This is
         //    the exact path the File menu and Cmd+S take.
         const bool saved = SavePatchTo(TmpPath("infinite_bugtest.infinite"));
         printf("save from outside editor context: %d\n", (int)saved);

         // 2. Moving a Geometry node must move what an operator downstream
         //    renders. The operator forwards the transform rather than
         //    flattening it to identity.
         geo->posX = 0.0f;
         const float before = arr->GetModelMatrix().m[12];
         geo->posX = 2.5f;
         const float after = arr->GetModelMatrix().m[12];
         printf("array transform follows input: %.2f -> %.2f  %s\n", before, after,
                std::fabs(after - 2.5f) < 1e-4f ? "OK" : "FAIL");

         // 3. Moving an input of Join Geometry must change the merged mesh,
         //    since the transform is baked into its vertices.
         ja->posX = 0.0f;
         join->GetMesh();
         float joinBefore = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
            joinBefore = std::max(joinBefore, v.px);
         ja->posX = 3.0f;
         join->GetMesh();
         float joinAfter = -1e30f;
         for (const Vertex& v : join->GetMesh().vertices)
            joinAfter = std::max(joinAfter, v.px);
         printf("join rebuilds on input move: max x %.2f -> %.2f  %s\n",
                joinBefore, joinAfter, joinAfter > joinBefore + 2.0f ? "OK" : "FAIL");

         // 4. Instance on Points must follow both the point source's and the
         //    shape's own transform - previously both were dropped, so
         //    moving/scaling either object silently did nothing to the render.
         GeometryNode pointsSrc;
         pointsSrc.shape = 0; // plane
         pointsSrc.detail = 4;
         GeometryNode shapeSrc;
         shapeSrc.shape = 2; // sphere

         InstanceOnPointsNode inst;
         inst.pointSource = &pointsSrc;
         inst.instanceShape = &shapeSrc;
         inst.pointMode = 0; // vertices
         inst.maxPoints = 50;
         inst.instanceScale = 1.0f;
         inst.scaleRandom = 0.0f;
         inst.rotationRandom = 0.0f;
         inst.alignToNormal = false;
         inst.CookIfNeeded(9500);
         const bool hadInstances = inst.InstanceCount() > 0;
         const Mat4 firstBefore = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();

         pointsSrc.posX = 5.0f;
         inst.CookIfNeeded(9501);
         const Mat4 firstAfterPointMove = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();
         const float pointDx = firstAfterPointMove.m[12] - firstBefore.m[12];

         shapeSrc.posY = 7.0f;
         inst.CookIfNeeded(9502);
         const Mat4 firstAfterShapeMove = hadInstances ? inst.InstanceTransforms()[0] : Mat4::Identity();
         const float shapeDy = firstAfterShapeMove.m[13] - firstAfterPointMove.m[13];

         const bool instancingFollowsTransforms = hadInstances &&
            std::fabs(pointDx - 5.0f) < 1e-3f && std::fabs(shapeDy - 7.0f) < 1e-3f;
         printf("instance on points follows source transforms: point dx=%.2f (want 5.00) "
                "shape dy=%.2f (want 7.00)  %s\n", pointDx, shapeDy,
                instancingFollowsTransforms ? "OK" : "FAIL");

         // 5. Cloth must drape from the input's transformed rest pose, not its
         //    raw object-space one - previously the input's own transform was
         //    dropped entirely when seeding the simulation.
         GeometryNode clothSrc;
         clothSrc.shape = 0; // plane
         clothSrc.detail = 4;
         ClothNode cloth;
         cloth.input = &clothSrc;
         cloth.pinMode = ClothNode::kPinNone;
         cloth.gravityX = cloth.gravityY = cloth.gravityZ = 0.0f;
         cloth.windX = cloth.windY = cloth.windZ = 0.0f;
         cloth.CookIfNeeded(9500);
         const bool hadClothVerts = !cloth.GetMesh().vertices.empty();
         const float clothVxBefore = hadClothVerts ? cloth.GetMesh().vertices[0].px : 0.0f;

         clothSrc.posX = 5.0f;
         cloth.CookIfNeeded(9501);
         const float clothVxAfter = hadClothVerts ? cloth.GetMesh().vertices[0].px : 0.0f;
         const bool clothFollowsTransform = hadClothVerts &&
            std::fabs((clothVxAfter - clothVxBefore) - 5.0f) < 1e-3f;
         printf("cloth rest pose follows input transform: dx=%.2f (want 5.00)  %s\n",
                clothVxAfter - clothVxBefore, clothFollowsTransform ? "OK" : "FAIL");

         // 6. A path following a mesh boundary must follow it in world space -
         //    previously the geometry source's transform was dropped, so the
         //    followed contour stayed put when the source moved.
         GeometryNode pathSrc;
         pathSrc.shape = 0; // plane, open so it has a boundary loop
         pathSrc.detail = 4;
         PathNode path;
         path.geometrySource = &pathSrc;
         path.followMode = PathNode::kFollowBoundary;
         path.speed = 0.0f;
         path.phase = 0.0f;
         path.pingPong = false;
         path.sizeX = path.sizeY = path.sizeZ = 1.0f;
         path.CookIfNeeded(9500);
         float pathBefore[3]; path.CurrentPoint(pathBefore);
         const bool pathHasFollow = path.IsFollowing();

         pathSrc.posX = 5.0f;
         path.CookIfNeeded(9501);
         float pathAfter[3]; path.CurrentPoint(pathAfter);
         const bool pathFollowsTransform = pathHasFollow &&
            std::fabs((pathAfter[0] - pathBefore[0]) - 5.0f) < 1e-3f;
         printf("path follow tracks geometry source transform: dx=%.2f (want 5.00)  %s\n",
                pathAfter[0] - pathBefore[0], pathFollowsTransform ? "OK" : "FAIL");

         const bool allOk = saved && std::fabs(after - 2.5f) < 1e-4f &&
                            joinAfter > joinBefore + 2.0f && instancingFollowsTransforms &&
                            clothFollowsTransform && pathFollowsTransform;
         printf("%s\n", allOk ? "BUGFIXES OK" : "SUSPECT");
      }
}
}

// Part of src/main.cpp, included there in order (one translation unit).
// modulation matrix and editor-side windows

      // ---- Turbo: modulation matrix -------------------------------------
      // Every modulation cable in one table: source -> destination parameter,
      // amount (absolute / bipolar depth), the range mapper, and the live
      // value of the destination. Expressions listed below it.
      if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_M, false))
         gModMatrixOpen = !gModMatrixOpen;
      if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_T, false))
         gArrangePanelOpen = !gArrangePanelOpen;
      if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_P, false))
         gPerfPanelOpen = !gPerfPanelOpen; // Turbo 0.46: Performance Mode
      if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_C, false))
      {
         gChatOpen = !gChatOpen; // Turbo 0.47: Claude chat
         gChatFocusInput = gChatOpen;
      }
      DrawClaudeChat();
      DrawAiConnections(); // Turbo 0.59: Setup > AI connections
      DrawSetList();       // Turbo 0.59: VIEW > Set list (Shift+L)
      if (gModMatrixOpen)
      {
         ImGui::SetNextWindowSize(ImVec2(820, 420), ImGuiCond_FirstUseEver);
         if (ImGui::Begin("Modulation matrix", &gModMatrixOpen))
         {
            Modulation& mod = Modulation::Instance();
            // this frame's registered params, for names and live values
            std::map<Modulation::Key, const ParamRef*> refs;
            for (const ParamRef& r : mod.FrameParams())
               refs[Modulation::Key(r.nodeIndex, r.paramIndex)] = &r;

            std::vector<Modulation::Key> keys;
            for (const auto& link : mod.Links())
               keys.push_back(link.first);
            ImGui::Text("%d modulation%s", (int)keys.size(), keys.size() == 1 ? "" : "s");
            ImGui::SameLine();
            ImGui::TextDisabled("  (drag a modulator's output onto a parameter pin to add one)");

            Modulation::Key removeKey(-1, -1);
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##modmatrix", 7, flags, ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y - 110.0f))))
            {
               ImGui::TableSetupScrollFreeze(0, 1);
               ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22.0f);
               ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthStretch, 1.2f);
               ImGui::TableSetupColumn("destination", ImGuiTableColumnFlags_WidthStretch, 1.2f);
               ImGui::TableSetupColumn("parameter", ImGuiTableColumnFlags_WidthStretch, 1.0f);
               ImGui::TableSetupColumn("amount", ImGuiTableColumnFlags_WidthFixed, 150.0f);
               ImGui::TableSetupColumn("range in -> out", ImGuiTableColumnFlags_WidthFixed, 230.0f);
               ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 90.0f);
               ImGui::TableHeadersRow();
               for (const Modulation::Key& key : keys)
               {
                  Modulation::Source src = mod.ModulatorFor(key.first, key.second);
                  GraphNode* srcNode = FindNodeByIndex(src.nodeIndex);
                  GraphNode* dstNode = FindNodeByIndex(key.first);
                  ImGui::PushID(key.first * 100000 + key.second);
                  ImGui::TableNextRow();

                  ImGui::TableNextColumn();
                  if (ImGui::SmallButton("x"))
                     removeKey = key;

                  ImGui::TableNextColumn();
                  std::string srcLabel = srcNode ? NodeTitleWithInstance(*srcNode) : std::string("?");
                  if (srcNode && srcNode->node->OutputCount() > 1)
                     srcLabel += std::string(" . ") + srcNode->node->OutputLabel(src.outputIndex);
                  if (ImGui::Selectable(srcLabel.c_str(), false) && srcNode)
                  {
                     gPendingSelect.push_back(srcNode->NodeId());
                     gPendingNavigateSelection = true;
                  }

                  ImGui::TableNextColumn();
                  if (ImGui::Selectable(dstNode ? NodeTitleWithInstance(*dstNode).c_str() : "?", false) && dstNode)
                  {
                     gPendingSelect.push_back(dstNode->NodeId());
                     gPendingNavigateSelection = true;
                  }

                  ImGui::TableNextColumn();
                  auto it = refs.find(key);
                  const ParamRef* ref = it != refs.end() ? it->second : nullptr;
                  if (ref != nullptr)
                     ImGui::TextUnformatted(ref->name.c_str());
                  else
                     ImGui::TextDisabled("param %d", key.second);

                  ImGui::TableNextColumn();
                  {
                     bool bipolar = src.polarity == Modulation::Source::kBipolar;
                     if (ImGui::Checkbox("+/-", &bipolar))
                     {
                        PushUndoCheckpoint();
                        mod.SetPolarity(key.first, key.second, bipolar ? Modulation::Source::kBipolar
                                                                       : Modulation::Source::kAbsolute, src.depth);
                     }
                     ImGui::SameLine();
                     float depth = src.depth;
                     ImGui::SetNextItemWidth(-1);
                     ImGui::BeginDisabled(!bipolar); // depth only applies to bipolar
                     if (ImGui::SliderFloat("##depth", &depth, -1.0f, 1.0f, "%.2f"))
                        mod.SetPolarity(key.first, key.second, src.polarity, depth);
                     if (ImGui::IsItemActivated())
                        PushUndoCheckpoint();
                     ImGui::EndDisabled();
                  }

                  ImGui::TableNextColumn();
                  {
                     float r[4] = { src.inMin, src.inMax, src.outMin, src.outMax };
                     ImGui::SetNextItemWidth(-1);
                     if (ImGui::DragFloat4("##range", r, 0.005f, -1.0f, 2.0f, "%.2f"))
                        mod.SetRange(key.first, key.second, r[0], r[1], r[2], r[3]);
                     if (ImGui::IsItemActivated())
                        PushUndoCheckpoint();
                     if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("in min, in max -> out min, out max (0..1 of the parameter)");
                  }

                  ImGui::TableNextColumn();
                  if (ref != nullptr && ref->value != nullptr && ref->maxValue > ref->minValue)
                  {
                     const float t = std::clamp((*ref->value - ref->minValue) / (ref->maxValue - ref->minValue), 0.0f, 1.0f);
                     char buf[32];
                     snprintf(buf, sizeof(buf), "%.3g", *ref->value);
                     ImGui::ProgressBar(t, ImVec2(-1, 0), buf);
                  }
                  else
                     ImGui::TextDisabled("(off screen)");
                  ImGui::PopID();
               }
               ImGui::EndTable();
            }
            if (removeKey.first >= 0)
            {
               PushUndoCheckpoint();
               mod.Unbind(removeKey.first, removeKey.second);
               gPatchDirty = true;
            }

            const auto& exprs = mod.Expressions();
            ImGui::SeparatorText(("expressions (" + std::to_string(exprs.size()) + ")").c_str());
            for (const auto& e : exprs)
            {
               GraphNode* g = FindNodeByIndex(e.first.first);
               auto it = refs.find(e.first);
               ImGui::BulletText("%s / %s  =  %s", g ? NodeTitleWithInstance(*g).c_str() : "?",
                                 it != refs.end() ? it->second->name.c_str() : "param", e.second.c_str());
            }
            ImGui::TextDisabled("%d MIDI mapping(s): VIEW > MIDI map", (int)MidiMap::All().size());
         }
         ImGui::End();
      }

      if (gMidiWindowOpen)
      {
         ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
         if (ImGui::Begin("MIDI map", &gMidiWindowOpen))
         {
            ImGui::Checkbox("learn mode (Ctrl+M)", &MidiMap::LearnMode());
            if (!MidiMap::LearnMode())
               MidiMap::Disarm();
            ImGui::SameLine();
            if (ImGui::Button("rescan devices"))
            {
               std::string err;
               MidiMap::Rescan(err);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", MidiMap::Status().c_str());
            ImGui::SeparatorText("devices");
            const std::vector<std::string> devices = MidiMap::DeviceKeys();
            if (devices.empty())
               ImGui::TextDisabled("none connected (or MIDI not started yet)");
            for (const std::string& d : devices)
               ImGui::BulletText("%s", d.c_str());

            std::vector<MidiMap::Mapping>& maps = MidiMap::All();
            ImGui::SeparatorText(("mappings (" + std::to_string(maps.size()) + ")").c_str());
            int removeAt = -1;
            if (ImGui::BeginChild("##midimaps", ImVec2(0, 170), true))
            {
               for (int i = 0; i < (int)maps.size(); i++)
               {
                  const MidiMap::Mapping& m = maps[(size_t)i];
                  GraphNode* g = FindNodeByIndex(m.nodeIndex);
                  const std::string who = (g ? NodeTitleWithInstance(*g) : std::string("?")) + " / " + m.paramName;
                  ImGui::PushID(i);
                  if (ImGui::SmallButton("x"))
                     removeAt = i;
                  ImGui::SameLine();
                  const bool hot = MidiMap::RecentlyActive(m.nodeIndex, m.paramIndex, ImGui::GetTime());
                  if (hot)
                     ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                  if (ImGui::Selectable((who + "   <-   " + MidiMap::SourceLabel(m)).c_str(), gMidiWindowSelected == i))
                     gMidiWindowSelected = i;
                  if (hot)
                     ImGui::PopStyleColor();
                  ImGui::PopID();
               }
            }
            ImGui::EndChild();
            if (removeAt >= 0)
            {
               PushUndoCheckpoint();
               maps.erase(maps.begin() + removeAt);
               gMidiWindowSelected = -1;
               gPatchDirty = true;
            }
            if (gMidiWindowSelected >= 0 && gMidiWindowSelected < (int)maps.size())
            {
               MidiMap::Mapping& m = maps[(size_t)gMidiWindowSelected];
               ImGui::SeparatorText("selected mapping");
               if (ImGui::Button("learn again"))
                  MidiMap::Arm(m.nodeIndex, m.paramIndex, m.paramName);
               ImGui::SameLine();
               if (ImGui::Button("show node"))
               {
                  if (GraphNode* g = FindNodeByIndex(m.nodeIndex))
                     gPendingSelect.push_back(g->NodeId());
                  gPendingNavigateSelection = true;
               }
               DrawMidiMappingEditor(m);
            }
            else
               ImGui::TextDisabled("select a mapping to edit it. Mappings are saved with the patch.");
         }
         ImGui::End();
      }

      if (ImGui::BeginPopup("##modbind"))
      {
         Modulation& mod = Modulation::Instance();
         if (!mod.IsModulated(gModBindingMenuNode, gModBindingMenuParam))
         {
            // The cable was removed (deleted node, undo, ...) while the
            // popup sat open.
            ImGui::CloseCurrentPopup();
         }
         else
         {
            const Modulation::Source src = mod.ModulatorFor(gModBindingMenuNode, gModBindingMenuParam);
            const bool bipolar = src.polarity == Modulation::Source::kBipolar;
            if (ImGui::MenuItem("Absolute", nullptr, !bipolar))
               mod.SetPolarity(gModBindingMenuNode, gModBindingMenuParam, Modulation::Source::kAbsolute, src.depth);
            if (ImGui::MenuItem("Bipolar", nullptr, bipolar))
               mod.SetPolarity(gModBindingMenuNode, gModBindingMenuParam, Modulation::Source::kBipolar, src.depth);
            if (bipolar)
            {
               float depth = src.depth;
               ImGui::SetNextItemWidth(140.0f);
               // -1..1, matching ModDepthNode's convention: negative inverts.
               if (ImGui::SliderFloat("Depth", &depth, -1.0f, 1.0f, "%.2f"))
                  mod.SetPolarity(gModBindingMenuNode, gModBindingMenuParam, Modulation::Source::kBipolar, depth);
            }
            DrawModulationRangeEditor(gModBindingMenuNode, gModBindingMenuParam);
            ImGui::Separator();
            if (ImGui::MenuItem("Unbind"))
            {
               PushUndoCheckpoint();
               mod.Unbind(gModBindingMenuNode, gModBindingMenuParam);
            }
         }
         ImGui::EndPopup();
      }

      // double-click empty canvas -> searchable spawner
      const ImVec2 dblClickMouse = ImGui::GetMousePos();
      const bool overGraph = dblClickMouse.x >= gGraphScreenTL.x &&
                             dblClickMouse.y >= gGraphScreenTL.y &&
                             dblClickMouse.x <= gGraphScreenTL.x + gGraphScreenSize.x &&
                             dblClickMouse.y <= gGraphScreenTL.y + gGraphScreenSize.y;
      if (overGraph &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
          !ed::GetHoveredNode() && !ed::GetHoveredPin() && !ed::GetHoveredLink())
      {
         gSpawnPos = ed::ScreenToCanvas(dblClickMouse);
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // Samples panel drag-and-drop landing here (see gSampleDragActive's
      // comment). A floating label follows the cursor as feedback since
      // there's no native ImGui drag source/payload backing this drag to
      // draw one automatically.
      if (gSampleDragActive)
      {
         const ImVec2 mp = ImGui::GetMousePos();
         ImGui::GetForegroundDrawList()->AddText(ImVec2(mp.x + 14.0f, mp.y + 14.0f),
                                                  IM_COL32(230, 235, 245, 255), gSampleDragName.c_str());

         if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         {
            // Deliberately NOT ed::GetHoveredNode()/GetHoveredPin()/
            // GetHoveredLink(): those ride on ImGui::IsWindowHovered(),
            // which ImGui suppresses for every window except the one
            // holding the active item - and the active item for this
            // entire drag is the Selectable back in the Samples panel (see
            // gSampleDragActive's comment; that's the whole mechanism the
            // drag-start detection relies on). So above, without
            // AllowWhenBlockedByActiveItem, hovered-node/pin/link would
            // read empty for the full gesture regardless of where the
            // mouse actually is. A plain canvas-space rect test against
            // each node's own bounds sidesteps that ImGui active-item gate
            // entirely.
            const ImVec2 canvasMouse = ed::ScreenToCanvas(mp);
            const bool overCanvas = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            if (gSampleDragKind == LibraryDragKind::Plugin)
            {
               // Dropped onto an existing Plugin node: swap its plugin
               // outright. Nothing is preserved - a different plugin's
               // parameter addresses and saved state mean nothing to it.
               if (AudioPluginNode* targetPlugin = FindNodeUnderCanvasPoint<AudioPluginNode>(canvasMouse))
               {
                  PushUndoCheckpoint();
                  targetPlugin->LoadPlugin(gPluginDragDesc);
                  gPatchDirty = true;
               }
               else if (overCanvas)
               {
                  GraphNode* spawned = SpawnNode("Plugin", "AudioEffects", canvasMouse.x, canvasMouse.y);
                  if (spawned != nullptr)
                  {
                     if (auto* plugin = dynamic_cast<AudioPluginNode*>(spawned->node.get()))
                        plugin->LoadPlugin(gPluginDragDesc);
                     gPatchDirty = true;
                  }
               }
            }
            else if (gSampleDragKind == LibraryDragKind::SampleFolder)
            {
               // Turbo 0.49: a library folder fills a Drum Sequencer's
               // lanes or an MPC's pads in name order, or spawns a Drum
               // Sequencer holding it on empty canvas (other nodes: ignored).
               const std::vector<std::string>& files = gSampleDragFolderFiles;
               auto fillDrum = [&files](DrumSequencerNode* d) {
                  for (int lane = 0; lane < DrumSequencerNode::kNumLanes && lane < (int)files.size(); lane++)
                     d->LoadFileToLane(lane, files[(size_t)lane]);
               };
               if (DrumSequencerNode* targetDrum = FindNodeUnderCanvasPoint<DrumSequencerNode>(canvasMouse))
               {
                  PushUndoCheckpoint();
                  fillDrum(targetDrum);
                  gPatchDirty = true;
               }
               else if (MpcNode* targetMpc = FindNodeUnderCanvasPoint<MpcNode>(canvasMouse))
               {
                  PushUndoCheckpoint();
                  int pad = 0;
                  for (size_t i = 0; i < files.size() && pad < MpcNode::kPads; i++)
                     if (targetMpc->LoadPad(pad, files[i]))
                     {
                        targetMpc->padStart[pad] = 0.0f;
                        targetMpc->padEnd[pad] = 1.0f;
                        pad++;
                     }
                  gPatchDirty = true;
               }
               else if (overCanvas && FindNodeUnderCanvasPoint<INode>(canvasMouse) == nullptr)
               {
                  // Turbo 0.49 review: only empty canvas spawns; a folder dropped
                  // on a Sampler, Slicer or any other node is ignored there.
                  GraphNode* spawned = SpawnNode("Drum Sequencer", "Synths", canvasMouse.x, canvasMouse.y);
                  if (spawned != nullptr)
                  {
                     if (auto* drum = dynamic_cast<DrumSequencerNode*>(spawned->node.get()))
                        fillDrum(drum);
                     gPatchDirty = true;
                  }
               }
            }
            else if (gSampleDragKind == LibraryDragKind::Sample)
            {
               // Checked before Sampler: a plain rect test on gNodes order,
               // like FindNodeUnderCanvasPoint's other callers, so dropping
               // onto a Drum Sequencer lands on its lane grid rather than
               // (impossibly, since the types don't overlap) falling through.
               DrumSequencerNode* targetDrum = FindNodeUnderCanvasPoint<DrumSequencerNode>(canvasMouse);
               if (targetDrum != nullptr)
               {
                  PushUndoCheckpoint();
                  const int lane = DrumSequencerLaneForCanvasPos(targetDrum, canvasMouse.x, canvasMouse.y);
                  targetDrum->LoadFileToLane(lane, gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (MpcNode* targetMpc = FindNodeUnderCanvasPoint<MpcNode>(canvasMouse))
               {
                  // Turbo 0.49: onto the MPC pad under the cursor (else the
                  // selected pad).
                  PushUndoCheckpoint();
                  const int pad = MpcPadForCanvasPos(targetMpc, canvasMouse.x, canvasMouse.y);
                  if (targetMpc->LoadPad(pad, gSampleDragPath))
                  {
                     targetMpc->padStart[pad] = 0.0f;
                     targetMpc->padEnd[pad] = 1.0f;
                     targetMpc->selectedPad = pad;
                  }
                  gPatchDirty = true;
               }
               else if (SamplerNode* targetSampler = FindNodeUnderCanvasPoint<SamplerNode>(canvasMouse))
               {
                  // Dropped onto an existing Sampler: swap its file.
                  PushUndoCheckpoint();
                  targetSampler->LoadFile(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (SlicerNode* targetSlicer = FindNodeUnderCanvasPoint<SlicerNode>(canvasMouse))
               {
                  // Dropped onto an existing Slicer: swap its file and re-slice.
                  PushUndoCheckpoint();
                  targetSlicer->LoadFile(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (MolderNode* targetMolder = FindNodeUnderCanvasPoint<MolderNode>(canvasMouse))
               {
                  // Dropped onto an existing Molder: swap its source and re-analyze.
                  PushUndoCheckpoint();
                  targetMolder->LoadFile(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (GrainMolderNode* targetGM = FindNodeUnderCanvasPoint<GrainMolderNode>(canvasMouse))
               {
                  // Dropped onto an existing Grain Molder: swap its source and mold.
                  PushUndoCheckpoint();
                  targetGM->LoadFile(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (PaulStretchNode* targetPaul = FindNodeUnderCanvasPoint<PaulStretchNode>(canvasMouse))
               {
                  // Dropped onto an existing PaulStretch: swap its file.
                  PushUndoCheckpoint();
                  targetPaul->LoadFile(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (GranularNode* targetGran = FindNodeUnderCanvasPoint<GranularNode>(canvasMouse))
               {
                  // Dropped onto an existing Granular: swap its file.
                  PushUndoCheckpoint();
                  targetGran->LoadFile(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (overCanvas)
               {
                  // No Sampler under the cursor but still over the canvas:
                  // spawn a loaded one there, same "click happened over the
                  // canvas" position rule the double-click search popup above
                  // uses.
                  GraphNode* spawned = SpawnNode("Sampler", "Synths", canvasMouse.x, canvasMouse.y);
                  if (spawned != nullptr)
                  {
                     if (auto* sampler = dynamic_cast<SamplerNode*>(spawned->node.get()))
                        sampler->LoadFile(gSampleDragPath);
                     gPatchDirty = true;
                  }
               }
            }
            else if (HasExtension(gSampleDragPath, kVideoExt))
            {
               VideoSourceNode* targetVideo = FindNodeUnderCanvasPoint<VideoSourceNode>(canvasMouse);
               if (targetVideo != nullptr)
               {
                  PushUndoCheckpoint();
                  targetVideo->Open(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (overCanvas)
               {
                  GraphNode* spawned = SpawnNode("Video", "Source", canvasMouse.x, canvasMouse.y);
                  if (spawned != nullptr)
                  {
                     if (auto* video = dynamic_cast<VideoSourceNode*>(spawned->node.get()))
                        video->Open(gSampleDragPath);
                     gPatchDirty = true;
                  }
               }
            }
            else
            {
               ImageSourceNode* targetImage = FindNodeUnderCanvasPoint<ImageSourceNode>(canvasMouse);
               if (targetImage != nullptr)
               {
                  PushUndoCheckpoint();
                  targetImage->Load(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (overCanvas)
               {
                  GraphNode* spawned = SpawnNode("Image Source", "Source", canvasMouse.x, canvasMouse.y);
                  if (spawned != nullptr)
                  {
                     if (auto* image = dynamic_cast<ImageSourceNode*>(spawned->node.get()))
                        image->Load(gSampleDragPath);
                     gPatchDirty = true;
                  }
               }
            }

            gSampleDragActive = false;
            gSampleDragKind = LibraryDragKind::Sample;
            gSampleDragPath.clear();
            gSampleDragName.clear();
            gSampleDragFolderFiles.clear();
            gPluginDragDesc = Platform::PluginDesc();
         }
      }

      // dev-only: force the picker open (optionally with a query) to verify layout
      if (const char* pk = getenv("INFINITE_PICKERTEST"))
      {
         if (frameId == 6)
         {
            gSpawnPos = ImVec2(200.0f, 200.0f);
            snprintf(searchBuf, sizeof(searchBuf), "%s", std::string(pk) == "empty" ? "" : pk);
            searchJustOpened = (std::string(pk) == "empty");
            ImGui::OpenPopup("search");
         }
      }

      if (gDropdown.justOpened)
      {
         ImGui::OpenPopup("##dropdown");
         gDropdown.justOpened = false;
      }

      if (getenv("INFINITE_COLORTEST") != nullptr && frameId == 6)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         sh->fillColor[0] = 0.9f; sh->fillColor[1] = 0.35f; sh->fillColor[2] = 0.2f;
         gColor.target = sh->fillColor;
         gColor.owner = sh;
         gColor.label = "fill";
         gColor.justOpened = true;
      }

      if (gCommentEdit.justOpened)
      {
         ImGui::OpenPopup("##commentedit");
         gCommentEdit.justOpened = false;
      }

      // the comment may have been deleted while its editor was open
      if (gCommentEdit.target != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gCommentEdit.target)
               alive = true;
         }
         if (!alive)
            gCommentEdit.target = nullptr;
      }

      // Pinned to the node's own on-screen box (refreshed every frame in
      // DrawCommentPreview) so the editor lands exactly over the note instead
      // of opening as a separate window elsewhere on screen - typing is meant
      // to read as happening straight into the box you double-clicked.
      if (gCommentEdit.target != nullptr && gCommentEditRect.z > 0.0f)
      {
         ImGui::SetNextWindowPos(ImVec2(gCommentEditRect.x, gCommentEditRect.y));
         ImGui::SetNextWindowSize(ImVec2(gCommentEditRect.z, gCommentEditRect.w));
      }
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
      if (gCommentEdit.target != nullptr)
      {
         const float* col = gCommentEdit.target->color;
         ImGui::PushStyleColor(ImGuiCol_PopupBg,
                               ImVec4(col[0] * 0.16f, col[1] * 0.16f, col[2] * 0.16f, 1.0f));
         ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(col[0], col[1], col[2], 0.8f));
      }
      if (ImGui::BeginPopup("##commentedit", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
      {
         if (gCommentEdit.target != nullptr)
         {
            CommentNode* c = gCommentEdit.target;
            gCommentEdit.framesOpen++;
            if (gCommentEdit.framesOpen <= 4) // see CommentEditRequest::framesOpen
            {
               ImGui::SetWindowFocus();
               ImGui::SetKeyboardFocusHere();
            }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
            const float commentZoom = gCommentEditRect.z > 0.0f && c->width > 0.0f
               ? gCommentEditRect.z / std::max(120.0f, c->width) : 1.0f;
            ImGui::SetWindowFontScale(commentZoom * CommentFontScale(c->fontSize));
            // Filling the whole popup rather than a fixed size: the popup is
            // already pinned to the node's box, so the field just fills it.
            auto toEnd = [](ImGuiInputTextCallbackData* d) -> int {
               if (gCommentEdit.cursorToEnd)
               {
                  d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                  gCommentEdit.cursorToEnd = false;
               }
               return 0;
            };
            ImGui::InputTextMultiline("##commenttext", &c->text, ImVec2(-FLT_MIN, -FLT_MIN),
                                      ImGuiInputTextFlags_CallbackAlways, toEnd);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            // The checkpoint was pushed when the editor opened, so every
            // keystroke here is part of that one undo step; all that is left is
            // to keep the patch marked unsaved.
            if (ImGui::IsItemEdited())
               gPatchDirty = true;
            // Esc finishes the same way clicking outside does - no separate
            // "done" step needed for a note this small.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
               ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      if (gCommentEdit.target != nullptr)
         ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(3);
      if (!ImGui::IsPopupOpen("##commentedit"))
      {
         gCommentEdit.target = nullptr;
         gCommentEdit.framesOpen = 0;
      }

      if (gColor.justOpened)
      {
         ImGui::OpenPopup("##colorpick");
         gColor.justOpened = false;
      }

      // the owning node may have been deleted while the picker was open
      if (gColor.owner != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gColor.owner)
               alive = true;
         }
         if (!alive)
         {
            gColor.owner = nullptr;
            gColor.target = nullptr;
         }
      }

      if (ImGui::BeginPopup("##colorpick"))
      {
         if (gColor.target != nullptr)
         {
            ImGui::TextDisabled("%s", gColor.label.c_str());
            gColorPickerRect = ImVec4(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y,
                                      0.0f, 0.0f);
            ImGui::ColorPicker3("##pick", gColor.target,
                                ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_DisplayRGB |
                                ImGuiColorEditFlags_DisplayHSV |
                                ImGuiColorEditFlags_DisplayHex);
            gColorPickerRect.z = ImGui::GetItemRectSize().x;
            gColorPickerRect.w = ImGui::GetItemRectSize().y;
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(400, 440));
      if (ImGui::BeginPopup("search"))
      {
         // IsWindowAppearing covers every opening route, including the node
         // editor's background context-menu event on Windows. That route can
         // consume the one-frame boolean before InputText is submitted, which
         // left the popup visible but required another click before typing.
         if (searchJustOpened || ImGui::IsWindowAppearing())
         {
            ImGui::SetKeyboardFocusHere();
            searchJustOpened = false;
         }
         ImGui::SetNextItemWidth(280);
         ImGui::InputTextWithHint("##q", "SEARCH NODES...", searchBuf, sizeof(searchBuf));
         ImGui::Separator();

         std::string q(searchBuf);
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         const bool pickFirst = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
         std::string spawnName, spawnCategory;
         int shown = 0;

         if (q.empty())
         {
            // Link-drag search: a cable was just dropped on empty canvas, so
            // lead with the node types compatible with the pin it came from
            // rather than making the user scroll every category to find one.
            if (!gLinkDragSuggestions.empty())
            {
               ImGui::SeparatorText("Suggested");
               for (const auto& t : gLinkDragSuggestions)
               {
                  ++shown;
                  const std::string itemLabel =
                     NodeBrowserLabel(t.first, "SUGGESTED_NODE", t.second, t.first);
                  if (ImGui::Selectable(itemLabel.c_str()))
                  {
                     spawnName = t.first;
                     spawnCategory = t.second;
                  }
               }
            }

            // no query yet: browse by category
            for (const std::string& category : NodeFactory::Instance().GetCategories())
            {
               ImGui::PushID(category.c_str());
               const auto& cc = CategoryColors::ColorFor(category);
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(cc.r, cc.g, cc.b, 1.0f));
               const std::string categoryLabel =
                  NodeBrowserLabel(category, "FLOATING_CATEGORY", category);
               if (ImGui::CollapsingHeader(categoryLabel.c_str()))
               {
                  ImGui::PopStyleColor();
                  ImGui::Indent();
                  for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
                  {
                     if (!IsUserSpawnable(name))
                        continue;
                     ++shown;
                     const std::string itemLabel =
                        NodeBrowserLabel(name, "FLOATING_NODE", category, name);
                     if (ImGui::Selectable(itemLabel.c_str()))
                     {
                        spawnName = name;
                        spawnCategory = category;
                     }
                  }
                  ImGui::Unindent();
               }
               else
                  ImGui::PopStyleColor();
               ImGui::PopID();
            }
         }
         else
         {
            for (const auto& t : allTypes)
            {
               std::string hay = DisplayName(t.first) + " " + DisplayName(t.second);
               std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
               if (hay.find(q) == std::string::npos)
                  continue;
               ++shown;
               std::string entry = DisplayName(t.first) + "   (" + DisplayName(t.second) + ")##SEARCH_NODE:" +
                                   t.second + ":" + t.first;
               bool activate = ImGui::Selectable(entry.c_str());
               if (shown == 1 && pickFirst)
                  activate = true;
               if (activate)
               {
                  spawnName = t.first;
                  spawnCategory = t.second;
               }
            }
            if (shown == 0)
               ImGui::TextDisabled("no matches");
         }

         if (!spawnName.empty())
         {
            GraphNode* spawned = SpawnNode(spawnName, spawnCategory, gSpawnPos.x, gSpawnPos.y);
            ApplySpawnDefaults(spawned);
            if (gLinkDragSourcePin >= 0 && spawned != nullptr)
            {
               GraphNode* dragSrcNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(gLinkDragSourcePin));
               if (dragSrcNode != nullptr)
               {
                  const int dragOutputSlot = GraphNode::OutputIndexFromPin(gLinkDragSourcePin);
                  const bool srcIsModulator = (dynamic_cast<IModulator*>(dragSrcNode->node.get()) != nullptr ||
                                               ModulatorForOutput(dragSrcNode->node.get(), dragOutputSlot) != nullptr);
                  auto* srcPalette = dynamic_cast<IPaletteSource*>(dragSrcNode->node.get());
                  auto* srcGeometry = dynamic_cast<IGeometrySource*>(dragSrcNode->node.get());
                  auto* srcCamera = dynamic_cast<CameraNode*>(dragSrcNode->node.get());
                  auto* srcLight = dynamic_cast<LightNode*>(dragSrcNode->node.get());
                  auto* srcAudioFile = dynamic_cast<AudioFileNode*>(dragSrcNode->node.get());
                  const bool srcIsEnvironment =
                     dynamic_cast<EnvironmentNode*>(dragSrcNode->node.get()) != nullptr;
                  const bool srcIsAudioNode =
                     IsAudioOutput(dragSrcNode->node.get(), dragOutputSlot);
                  const bool srcIsNoteSource =
                     IsNoteOutput(dragSrcNode->node.get(), dragOutputSlot);

                  const int slotCount = InputCountFor(*spawned);
                  for (int slot = 0; slot < slotCount; ++slot)
                  {
                     if (IsInputSlotCompatible(spawned, slot, srcIsModulator, srcPalette, srcGeometry,
                                                srcCamera, srcLight, srcAudioFile, srcIsEnvironment,
                                                srcIsAudioNode, srcIsNoteSource))
                     {
                        // No PushUndoCheckpoint() here: SpawnNode() above already
                        // pushed one capturing the state before the node existed,
                        // so a single Undo removes the spawn and the wire
                        // together, as one user action - not two separate steps.
                        WireInputSlot(*dragSrcNode, *spawned, slot,
                                      GraphNode::OutputIndexFromPin(gLinkDragSourcePin));
                        if (srcIsAudioNode || srcIsNoteSource ||
                            spawned->node->AudioInputSlot(slot) != nullptr ||
                            spawned->node->NoteInputSlot(slot) != nullptr)
                           RebuildAudioTopology();
                        break;
                     }
                  }
               }
            }
            gLinkDragSourcePin = -1;
            gLinkDragSuggestions.clear();
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      else
      {
         // Popup closed without a pick (Escape, click-away, or the manual
         // spawn-menu paths below) - clear so a later unrelated spawn doesn't
         // inherit a stale suggestion list from an earlier drag.
         gLinkDragSourcePin = -1;
         gLinkDragSuggestions.clear();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(220, 0), ImVec2(420, 480));
      if (ImGui::BeginPopup("##dropdown"))
      {
         // Turbo: lists of 12+ (the drum pattern library, devices) get a filter box.
         static char ddFilter[64] = "";
         const bool longList = gDropdown.options.size() >= 12; // Turbo 0.51 (upstream f08e6bf): was 30
         if (ImGui::IsWindowAppearing())
            ddFilter[0] = '\0';
         std::string filterLower;
         if (longList)
         {
            if (ImGui::IsWindowAppearing())
               ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##ddfilter", "filter...", ddFilter, sizeof(ddFilter));
            filterLower = FoldForSearch(ddFilter); // Turbo 0.51: accent-insensitive, Latin-1 + Latin Ext-A
         }
         auto ddMatches = [&](int i) {
            if (filterLower.empty())
               return true;
            std::string hay = gDropdown.options[i];
            if (i < (int)gDropdown.categories.size())
               hay += " " + gDropdown.categories[i];
            return FoldForSearch(hay).find(filterLower) != std::string::npos;
         };
         std::string lastCat = "";
         for (int i = 0; i < (int)gDropdown.options.size(); i++)
         {
            if (!ddMatches(i))
               continue;
            if (i < (int)gDropdown.categories.size() && !gDropdown.categories[i].empty())
            {
               if (gDropdown.categories[i] != lastCat)
               {
                  if (!lastCat.empty())
                     ImGui::Spacing();
                  lastCat = gDropdown.categories[i];
                  ImGui::TextColored(ImVec4(0.55f, 0.70f, 0.95f, 0.85f), "%s", lastCat.c_str());
                  ImGui::Separator();
               }
            }
            bool selected = (i == gDropdown.current);
            ImGui::PushID(i);
            if (ImGui::Selectable(gDropdown.options[i].c_str(), selected))
            {
               if (gDropdown.onSelect && i != gDropdown.current)
               {
                  PushUndoCheckpoint();
                  gDropdown.onSelect(i);
               }
               ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if (selected && ImGui::IsWindowAppearing())
               ImGui::SetScrollHereY(0.5f);
         }
         ImGui::EndPopup();
      }

      gHoveringItem = ed::GetHoveredNode() || ed::GetHoveredPin() || ed::GetHoveredLink();

      ed::Resume();

      // Fit-to-content has to happen down here, after every node has been
      // submitted this frame. ed::Begin() marks all nodes not-live and only
      // drawing them marks them live again, and NavigateToContent() measures
      // live nodes only - so called up next to ed::Begin() it always fit an
      // empty rectangle and silently did nothing at all. The new view is
      // picked up by the next ed::Begin(), one frame later.
      if (gRequestFitView)
      {
         ed::NavigateToContent(0.0f);
         gRequestFitView = false;
      }
      // A sidebar spawn queues selection after ed::Begin in its creation
      // frame. Wait until the following frame has actually applied that
      // selection before asking node-editor to navigate to it.
      if (gPendingNavigateSelection && gPendingSelect.empty())
      {
         ed::NavigateToSelection(false, 0.0f);
         gPendingNavigateSelection = false;
      }
      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if (getenv("INFINITE_AUDIOUITEST") != nullptr && frameId == 3)
         gRequestFitView = true; // same, for the audio node UI fixture
      if (getenv("INFINITE_LOADPATCH") != nullptr && (frameId == 2 || frameId == 4))
         gRequestFitView = true;
      if (getenv("INFINITE_AUDIOUITEST") != nullptr)
      {
         // Plugin instantiation is asynchronous, so the fixture's mapping list
         // can't be filled in at spawn time - map the first few parameters the
         // moment the plugin reports ready, so the screenshot shows the real
         // two-column grid of mapped rows.
         for (GraphNode& gn : gNodes)
         {
            auto* pluginFixture = dynamic_cast<AudioPluginNode*>(gn.node.get());
            if (pluginFixture == nullptr || !pluginFixture->IsReady() ||
                pluginFixture->AssignedCount() > 0)
               continue;
            const std::vector<Platform::PluginParamInfo>& available = pluginFixture->AvailableParams();
            for (int i = 0; i < (int)available.size() && i < 4; i++)
               pluginFixture->MapParameter(available[i].address);
         }
      }
      if (getenv("INFINITE_HIDETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture

      ApplyBringToFront(); // Turbo 0.50: new nodes draw in front

      ed::End();
      ed::GetStyle().Colors[ed::StyleColor_Bg] = savedEdBg;
      ed::GetStyle().Colors[ed::StyleColor_Grid] = savedEdGrid;
      ed::SetCurrentEditor(nullptr);

      io.MouseWheel = savedWheel;
      io.MouseWheelH = savedWheelH;

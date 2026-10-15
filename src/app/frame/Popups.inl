// Part of src/main.cpp, included there in order (one translation unit).
// search, spawn menu, dropdown popups

      // ---- popups: search, spawn menu, dropdown ----
      ed::Suspend();
      DrawClipMatrixDeferredPopups(); // Turbo 0.44
      DrawChordSlotDeferredMenu(); // Turbo 0.55
      DrawReloadBanner(); // Turbo 0.45
      DrawHandoverBanner(); // Turbo 0.46
      if (!gLiveIssueTip.empty() && !ImGui::IsAnyItemActive())
         ImGui::SetTooltip("%s", gLiveIssueTip.c_str()); // Turbo 0.45
      gLiveIssueTip.clear();

      DrawMinimap();

      // Right-click (two-finger click on a Mac trackpad) opens the same
      // type-to-filter picker as double-click, so the keyboard works either way.
      if (ed::ShowBackgroundContextMenu())
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
      }
      if (ImGui::BeginPopup("##nodecontext"))
      {
         GraphNode* gn = FindNodeByIndex(gContextMenuNodeIndex);
         if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
         {
            if (ImGui::MenuItem("Rename"))
            {
               PushUndoCheckpoint();
               g->renaming = true;
               g->renameJustStarted = true;
            }
            if (ImGui::MenuItem("Ungroup"))
            {
               ed::ClearSelection();
               ed::SelectNode(gn->NodeId());
               gRequestUngroup = true;
            }
         }
         else
         {
            // Audio nodes have no hide-able params state at all any more -
            // every param is always visible (horizontal-layout redesign,
            // docs/plans/audio/audio-node-ui-system.md §1/§4), so they skip
            // this menu entry entirely rather than offering a toggle that
            // would do nothing. They still get Help/Ungroup below, same as
            // every other node type (§4's baseline).
            if (!IsAudioBodyNode(gn->node.get()))
            {
               if (gn->showParams)
               {
                  if (ImGui::MenuItem("Hide params"))
                     gn->showParams = false;
               }
               else
               {
                  if (ImGui::MenuItem("Show params"))
                     gn->showParams = true;
               }
            }
            if (CanToggleInlinePreview(*gn))
            {
               if (InlinePreviewEnabled(*gn))
               {
                  if (ImGui::MenuItem("Hide preview"))
                     SetInlinePreviewEnabled(*gn, false);
               }
               else
               {
                  if (ImGui::MenuItem("Show preview"))
                     SetInlinePreviewEnabled(*gn, true);
               }
            }
            // Turbo 0.43: Arrangement Timeline.
            {
               const bool fitsVideo = ArrangeNodeVideoCompatible(*gn);
               const bool fitsAudio = ArrangeNodeAudioCompatible(*gn);
               if (fitsVideo && fitsAudio)
               {
                  if (ImGui::BeginMenu("Add to Timeline"))
                  {
                     if (ImGui::MenuItem("video track"))
                        ArrangeAddNodeToTimeline(gn->index, Arrange::kLaneVideo);
                     if (ImGui::MenuItem("audio track"))
                        ArrangeAddNodeToTimeline(gn->index, Arrange::kLaneAudio);
                     ImGui::EndMenu();
                  }
               }
               else if ((fitsVideo || fitsAudio) && ImGui::MenuItem("Add to Timeline"))
                  ArrangeAddNodeToTimeline(gn->index);
            }
            if (CanShowInViewportPanel(*gn))
            {
               // Adds a card rather than replacing whatever is already shown -
               // the panel holds any number of nodes at once, each closable
               // on its own. A no-op if this node already has a card open.
               if (ImGui::MenuItem("Open in viewport panel"))
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn->index) ==
                      gViewportPanelNodes.end())
                     gViewportPanelNodes.push_back(gn->index);
               }
            }
            // Same gate as "Open in viewport panel": modulators, meters,
            // cameras/lights and comments don't produce an image, so opening
            // one in its own window would just show a blank/garbage texture.
            ProjectorWindow* projector = FindProjectorWindow(gn->index);
            if (CanShowInViewportPanel(*gn) && projector != nullptr)
            {
               if (ImGui::BeginMenu("Output window"))
               {
                  if (ImGui::MenuItem("Fullscreen", "F11", projector->fullscreen))
                     ToggleProjectorFullscreen(*projector);
                  if (ImGui::BeginMenu("Display"))
                  {
                     int monitorCount = 0;
                     GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
                     for (int monitorIndex = 0; monitorIndex < monitorCount; monitorIndex++)
                     {
                        const char* name = glfwGetMonitorName(monitors[monitorIndex]);
                        char label[256];
                        snprintf(label, sizeof(label), "%d: %s", monitorIndex + 1,
                                 name != nullptr ? name : "Display");
                        if (ImGui::MenuItem(label, nullptr, projector->monitorIndex == monitorIndex))
                           MoveProjectorToMonitor(*projector, monitorIndex);
                     }
                     ImGui::EndMenu();
                  }
                  // Turbo: exact size in pixels + how the image maps to it.
                  if (ImGui::BeginMenu("Size (pixels)"))
                  {
                     int fbW = 0, fbH = 0;
                     glfwGetFramebufferSize(projector->window, &fbW, &fbH);
                     ImGui::TextDisabled("current: %d x %d px", fbW, fbH);
                     static int sReqW = 1920, sReqH = 1080;
                     static int sReqFor = -1;
                     if (sReqFor != gn->index)
                     {
                        sReqW = projector->pixelW > 0 ? projector->pixelW : fbW;
                        sReqH = projector->pixelH > 0 ? projector->pixelH : fbH;
                        sReqFor = gn->index;
                     }
                     ImGui::SetNextItemWidth(90.0f);
                     ImGui::InputInt("W##outw", &sReqW, 0, 0);
                     ImGui::SameLine();
                     ImGui::SetNextItemWidth(90.0f);
                     ImGui::InputInt("H##outh", &sReqH, 0, 0);
                     if (ImGui::Button("Apply"))
                        SetProjectorPixelSize(*projector, sReqW, sReqH);
                     ImGui::Separator();
                     struct Preset { const char* label; int w, h; };
                     static const Preset kPresets[] = {
                        { "1920 x 1080 (Full HD)", 1920, 1080 }, { "1280 x 720 (HD)", 1280, 720 },
                        { "3840 x 2160 (4K UHD)", 3840, 2160 }, { "2560 x 1440 (QHD)", 2560, 1440 },
                        { "1080 x 1920 (vertical)", 1080, 1920 }, { "1080 x 1080 (square)", 1080, 1080 },
                        { "1024 x 768 (4:3)", 1024, 768 }, { "800 x 600 (4:3)", 800, 600 } };
                     for (const Preset& p : kPresets)
                        if (ImGui::MenuItem(p.label))
                        {
                           sReqW = p.w; sReqH = p.h;
                           SetProjectorPixelSize(*projector, p.w, p.h);
                        }
                     const int srcW = gn->node->GetOutputWidth(), srcH = gn->node->GetOutputHeight();
                     if (srcW > 0 && srcH > 0)
                     {
                        char label[64];
                        snprintf(label, sizeof(label), "Source size (%d x %d)", srcW, srcH);
                        if (ImGui::MenuItem(label))
                        {
                           sReqW = srcW; sReqH = srcH;
                           SetProjectorPixelSize(*projector, srcW, srcH);
                        }
                     }
                     ImGui::EndMenu();
                  }
                  if (ImGui::BeginMenu("Scaling"))
                  {
                     static const char* kFitNames[] = { "Fit (letterbox, black bars)", "Real pixels 1:1 (centred)",
                                                        "Fill (crop to cover)", "Stretch" };
                     for (int m = 0; m < 4; m++)
                        if (ImGui::MenuItem(kFitNames[m], nullptr, projector->fitMode == m))
                        {
                           projector->fitMode = m;
                           gProjectorDefaultFitMode = m;
                        }
                     ImGui::EndMenu();
                  }
                  ImGui::Separator();
                  if (ImGui::MenuItem("Close output window"))
                     CloseProjectorWindowFor(gn->index);
                  ImGui::EndMenu();
               }
            }
            else if (CanShowInViewportPanel(*gn))
            {
               if (ImGui::MenuItem("Open output window"))
                  OpenProjectorWindow(window, *gn);
            }
            if (CanShowBehindCanvas(*gn))
            {
               if (ImGui::MenuItem("Show behind the canvas", "Ctrl+Shift+B", gCanvasBgNodeIndex == gn->index))
                  ToggleCanvasBackground(gn->index);
            }
            if (ImGui::BeginMenu("Color marker"))
            {
               for (int t = 0; t < kNodeTagCount; t++)
               {
                  ImGui::PushID(t);
                  const ImVec2 p0 = ImGui::GetCursorScreenPos();
                  const float sz = ImGui::GetTextLineHeight();
                  if (t > 0)
                     ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + sz, p0.y + sz), NodeTagColor(t), 3.0f);
                  ImGui::Dummy(ImVec2(sz, sz));
                  ImGui::SameLine();
                  if (ImGui::MenuItem(NodeTagName(t), nullptr, gn->colorTag == t))
                     SetNodeColorTag(*gn, t);
                  ImGui::PopID();
               }
               ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Help"))
            {
               gHelpPopupNodeIndex = gn->index;
               ImGui::CloseCurrentPopup();
               gOpenNodeHelpPopup = true;
            }
            if (GroupNode* owner = GroupOwning(gn->index))
            {
               // Unlike the group's own "Ungroup" (which dissolves the whole
               // cluster), this detaches just the one node that was
               // right-clicked - its groupmates stay put.
               if (ImGui::MenuItem("Ungroup"))
               {
                  PushUndoCheckpoint();
                  gGroupMembers[owner].erase(gn->index);
                  // Membership here is purely geometric - anything fully
                  // inside the group's box gets adopted right back in next
                  // frame. Nudging the node just past the box's bottom edge
                  // is what makes removing it actually stick.
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
            }
         }
         ImGui::EndPopup();
      }

      // Node "Help" popup, opened via the context menu above. A separate
      // OpenPopup call (rather than nesting it inside "##nodecontext") because
      // that popup already closed itself this frame - reopening a still-being-
      // closed popup by the same ID silently no-ops in Dear ImGui.
      if (gOpenNodeHelpPopup)
      {
         ImGui::OpenPopup("##nodehelp");
         gOpenNodeHelpPopup = false;
      }
      ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(420, FLT_MAX));
      if (ImGui::BeginPopup("##nodehelp"))
      {
         GraphNode* gn = FindNodeByIndex(gHelpPopupNodeIndex);
         if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::TextUnformatted(NodeTitleWithInstance(*gn).c_str());
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
            ImGui::TextWrapped("%s", NodeHelpText(*gn));
            ImGui::PopTextWrapPos();
         }
         ImGui::EndPopup();
      }

      // Modulation-binding popup (Absolute/Bipolar/depth/Unbind), requested by
      // DrawModulationBindingMenu from deep inside a param slider/knob - see
      // gModBindingMenuNode's comment for why it can only actually open here.
      if (gOpenModBindingMenu)
      {
         ImGui::OpenPopup("##modbind");
         gOpenModBindingMenu = false;
      }
      // Turbo MIDI learn: the parameter's MIDI menu (right-click its pin).
      if (gOpenMidiMenu)
      {
         ImGui::OpenPopup("##midiparam");
         gOpenMidiMenu = false;
      }
      if (ImGui::BeginPopup("##midiparam"))
      {
         GraphNode* target = FindNodeByIndex(gMidiMenuNode);
         if (target == nullptr)
            ImGui::CloseCurrentPopup();
         else
         {
            auto spot = gParamPinSpots.find({ gMidiMenuNode, gMidiMenuParam });
            const std::string pname = spot != gParamPinSpots.end() ? spot->second.name : std::string("parameter");
            ImGui::TextUnformatted((NodeTitleWithInstance(*target) + " / " + pname).c_str());
            ImGui::Separator();
            if (MidiMap::IsArmed(gMidiMenuNode, gMidiMenuParam))
            {
               ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "move a control on your MIDI device...");
               if (ImGui::Button("cancel learn"))
                  MidiMap::Disarm();
            }
            else if (ImGui::Button(MidiMap::Find(gMidiMenuNode, gMidiMenuParam) ? "MIDI learn again" : "MIDI learn"))
               MidiMap::Arm(gMidiMenuNode, gMidiMenuParam, pname);
            // Turbo 0.46: put this parameter on the Performance Mode panel
            // (a toggle for a checkbox, a selector for a dropdown, else a knob).
            {
               const ParamRef* known = Modulation::Instance().KnownParam(gMidiMenuNode, gMidiMenuParam);
               const int perfKind = known != nullptr ? (known->isBool ? 3 : known->isEnum ? 7 : known->momentary ? 5 : 0) : 0;
               if (ImGui::Button("add to Performance"))
               {
                  AddToPerformanceMatrix(gMidiMenuNode, gMidiMenuParam, perfKind);
                  ImGui::CloseCurrentPopup();
               }
            }
            if (MidiMap::Mapping* m = MidiMap::Find(gMidiMenuNode, gMidiMenuParam))
            {
               ImGui::SameLine();
               if (ImGui::Button("clear MIDI"))
               {
                  PushUndoCheckpoint();
                  MidiMap::Remove(gMidiMenuNode, gMidiMenuParam);
                  gPatchDirty = true;
               }
               else
               {
                  ImGui::TextDisabled("%s", MidiMap::SourceLabel(*m).c_str());
                  DrawMidiMappingEditor(*m);
               }
            }
         }
         ImGui::EndPopup();
      }

      if (MidiMap::LearnMode())
      {
         // Learn-mode banner over the canvas.
         const char* msg = MidiMap::AnyArmed()
            ? "MIDI LEARN - now move a control on your MIDI device  (Esc / Ctrl+M: exit)"
            : "MIDI LEARN - click a parameter, then move a control  (right-click a pin: options, Esc / Ctrl+M: exit)";
         ImDrawList* fg = ImGui::GetForegroundDrawList();
         const ImVec2 at(gGraphScreenTL.x + 12.0f, gGraphScreenTL.y + 10.0f);
         const ImVec2 sz = ImGui::CalcTextSize(msg);
         fg->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 5.0f), ImVec2(at.x + sz.x + 8.0f, at.y + sz.y + 5.0f),
                           IM_COL32(120, 60, 10, 225), 5.0f);
         fg->AddText(at, IM_COL32(255, 230, 200, 255), msg);
      }

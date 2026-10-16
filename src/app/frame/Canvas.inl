// Part of src/main.cpp, included there in order (one translation unit).
// canvas layout, docked panels, node editor begin, drops

      // The canvas zoomed wildly on a trackpad because it consumes raw wheel
      // deltas; damp them for the duration of the editor, then restore.
      ImGuiIO& io = ImGui::GetIO();
      const float savedWheel = io.MouseWheel;
      const float savedWheelH = io.MouseWheelH;
      io.MouseWheel *= gZoomSensitivity;
      io.MouseWheelH *= gZoomSensitivity;

      ed::SetCurrentEditor(gEditor);

      // Dragging empty canvas should pan, but dragging a node should move it.
      // NavigateAction claims any drag on its configured button regardless of
      // what is underneath, so flip the button per-gesture using last frame's
      // hover state. Shift+drag still gives a rubber-band selection.
      {
         ed::Config& liveCfg = const_cast<ed::Config&>(ed::GetConfig(gEditor));
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gHoveringItem && !io.KeyShift)
            gPanWithLeft = true;
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            gPanWithLeft = false;
         // A right or middle drag that starts on a Render 3D viewport is that
         // viewport's (dolly / pan); park the canvas pan on an unused button
         // until it ends. gViewport3DHoveredNow is last frame's hover.
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            gViewport3DOwnsDrag = false;
         if ((ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) &&
             gViewport3DHoveredNow)
            gViewport3DOwnsDrag = true;
         gViewport3DHoveredNow = false;
         liveCfg.NavigateButtonIndex = gPanWithLeft ? 0 : (gViewport3DOwnsDrag ? 4 : 1);
      }

      const bool viewportPanelOpen = !gViewportPanelNodes.empty();
      const bool viewportBottom = viewportPanelOpen && gViewportPanelDock == 0;
      const bool viewportRight = viewportPanelOpen && gViewportPanelDock == 1;
      const bool viewportLeft = viewportPanelOpen && gViewportPanelDock == 2;
      const bool viewportTop = viewportPanelOpen && gViewportPanelDock == 3;

      // Drop the 3D render state of any node no longer in the panel. Done
      // here, at the top of the next frame, rather than at the moment its
      // card was closed: that card had already submitted its texture to that
      // frame's draw list, and destroying the FBO first would leave the draw
      // list blitting a deleted texture.
      for (auto it = gPanelViewports.begin(); it != gPanelViewports.end(); )
      {
         const bool stillShown = std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(),
                                           it->first) != gViewportPanelNodes.end();
         it = stillShown ? std::next(it) : gPanelViewports.erase(it);
      }

      // Clamp the (drag-resizable) panel against the window before anything
      // reserves space from it, so dragging the grip can never squeeze the
      // canvas out of existence or push a panel off the far edge. Runs every
      // frame regardless of whether a drag is in progress - previously the
      // floor (kViewportPanelMin*) was only re-applied inside the grip's own
      // active-drag branch, so this upper-bound-only clamp could hold the
      // panel below its minimum indefinitely once something pushed it there.
      {
         const ImVec2 room = ImGui::GetContentRegionAvail();
         const float maxHeight = std::max(kViewportPanelMinHeight, room.y - 150.0f);
         const float maxNodeWidth = std::max(kNodePanelMinWidth, room.x - 200.0f -
                                              (viewportRight ? gViewportPanelWidth : 0.0f));
         gNodePanelWidth = std::min(std::max(gNodePanelWidth, kNodePanelMinWidth),
                                    std::min(720.0f, maxNodeWidth));
         const float maxWidth = std::max(kViewportPanelMinWidth,
                                         room.x - 200.0f - (gNodePanelOpen ? gNodePanelWidth : 0.0f));
         gViewportPanelHeight = std::min(std::max(gViewportPanelHeight, kViewportPanelMinHeight), maxHeight);
         gViewportPanelWidth = std::min(std::max(gViewportPanelWidth, kViewportPanelMinWidth), maxWidth);
      }

      // Measured before the top/left panels below consume any of it, so the
      // canvas gets what is left after every reservation rather than after
      // only the ones that happen to draw first.
      //
      // A top/bottom-docked panel is its own row, separate from the canvas
      // row below/above it - unlike right/left, which share the canvas's row
      // via SameLine() - so it costs one extra ItemSpacing.y that a same-line
      // dock never does. That gap is easy to forget in this budget; forgetting
      // it is exactly what grows the outer window's own scrollbar (see the
      // NoScrollbar comment on its Begin() call above).
      const float topBottomGap = (viewportTop || viewportBottom) ? ImGui::GetStyle().ItemSpacing.y : 0.0f;
      // Turbo 0.43: the arrangement panel docks under everything else.
      if (gArrangePanelOpen)
         gArrangePanelHeight = std::clamp(gArrangePanelHeight, 140.0f,
                                          std::max(140.0f, ImGui::GetContentRegionAvail().y - 220.0f));
      const float arrangeReserve = gArrangePanelOpen ? gArrangePanelHeight + ImGui::GetStyle().ItemSpacing.y : 0.0f;
      // Turbo 0.46: Performance Mode panel, docked on any side.
      const bool perfTop = gPerfPanelOpen && gPerfPanelDock == 3;
      const bool perfBottom = gPerfPanelOpen && gPerfPanelDock == 0;
      const bool perfLeft = gPerfPanelOpen && gPerfPanelDock == 2;
      const bool perfRight = gPerfPanelOpen && gPerfPanelDock == 1;
      if (perfTop || perfBottom)
         gPerfPanelHeight = std::clamp(gPerfPanelHeight, kPerfPanelMinHeight,
                                       std::max(kPerfPanelMinHeight, ImGui::GetContentRegionAvail().y - arrangeReserve - 200.0f));
      if (perfLeft || perfRight)
         gPerfPanelWidth = std::clamp(gPerfPanelWidth, kPerfPanelMinWidth,
                                      std::max(kPerfPanelMinWidth, ImGui::GetContentRegionAvail().x - 300.0f));
      const float perfReserve = (perfTop || perfBottom) ? gPerfPanelHeight + ImGui::GetStyle().ItemSpacing.y : 0.0f;
      const float graphHeight =
         std::max(150.0f, ImGui::GetContentRegionAvail().y - arrangeReserve - perfReserve -
                             ((viewportTop || viewportBottom) ? gViewportPanelHeight + topBottomGap : 0.0f));
      if (perfTop)
         DrawPerfPanelDocked("##perfpanel_top", ImVec2(0, gPerfPanelHeight));

      // Top- and left-docked viewport panels draw before the canvas: nothing
      // else in this window reserves space above or left of it, so each has
      // to consume its own room here, before the canvas cursor position (and
      // gGraphScreenTL below) reflect it.
      if (viewportTop)
         DrawViewportPanelDocked("##viewportpanel_top", ImVec2(0, gViewportPanelHeight));
      // Turbo 0.46: after every top panel, so it shares the canvas row.
      if (perfLeft)
      {
         DrawPerfPanelDocked("##perfpanel_left", ImVec2(gPerfPanelWidth, graphHeight));
         ImGui::SameLine();
      }
      if (viewportLeft)
      {
         DrawViewportPanelDocked("##viewportpanel_left", ImVec2(gViewportPanelWidth, graphHeight));
         ImGui::SameLine();
      }

      // One combined reservation for both right-docked panels, computed
      // together so ImGui's SameLine() chaining after ed::End() lays them out
      // side by side instead of one clipping the other or the two overlapping.
      float rightReserved = 0.0f;
      if (gNodePanelOpen) rightReserved += gNodePanelWidth;
      if (viewportRight) rightReserved += gViewportPanelWidth;
      if (perfRight) rightReserved += gPerfPanelWidth + ImGui::GetStyle().ItemSpacing.x;
      const float graphWidth = rightReserved > 0.0f
                                  ? std::max(200.0f, ImGui::GetContentRegionAvail().x - rightReserved)
                                  : 0.0f;

      // The canvas rect, captured here rather than from inside the editor:
      // ed::Begin does not open a child window (ImGuiEx::Canvas draws straight
      // into the current one), so GetWindowPos/GetWindowSize in there report
      // the whole app window - menu bar and docked module panel included.
      // Anything positioned against that, the minimap especially, would sit
      // outside the graph and over a panel - which is also why the left panel
      // above has to draw before this capture rather than after.
      gGraphScreenTL = ImGui::GetCursorScreenPos();
      gGraphScreenSize = ImVec2(graphWidth > 0.0f ? graphWidth : ImGui::GetContentRegionAvail().x,
                                graphHeight);

      ed::GetStyle().GridSpacing = gGridSnap;

      // Turbo: output preview behind the canvas.
      gCanvasBgActive = false;
      if (gCanvasBgNodeIndex >= 0)
      {
         GraphNode* bgNode = FindNodeByIndex(gCanvasBgNodeIndex);
         if (bgNode == nullptr)
            gCanvasBgNodeIndex = -1;
         else
            gCanvasBgActive = DrawCanvasBackground(*bgNode, gGraphScreenTL, gGraphScreenSize);
      }
      const ImVec4 savedEdBg = ed::GetStyle().Colors[ed::StyleColor_Bg];
      const ImVec4 savedEdGrid = ed::GetStyle().Colors[ed::StyleColor_Grid];
      if (gCanvasBgActive)
      {
         ed::GetStyle().Colors[ed::StyleColor_Bg].w = 0.0f;
         if (!gCanvasBgGrid)
            ed::GetStyle().Colors[ed::StyleColor_Grid].w = 0.0f;
      }

      ed::Begin("graph", ImVec2(graphWidth, graphHeight));

      if (!gPendingSelect.empty())
      {
         bool first = true;
         for (int nodeId : gPendingSelect)
         {
            ed::SelectNode(nodeId, !first);
            first = false;
         }
         gPendingSelect.clear();
      }

      // Where a panel-spawned node should land. ScreenToCanvas is only valid
      // inside the editor, so it is captured here and used after ed::End().
      gViewCenterCanvas = ed::ScreenToCanvas(
         ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                gGraphScreenTL.y + gGraphScreenSize.y * 0.5f));

      // Consume persistent-browser requests while the editor is active and
      // before the node submission loop. The old after-ed::End path could
      // create a graph entry without a live editor item until a later frame;
      // Feedback's zero-sized empty texture made that look like a failed
      // insertion. This path always gives the new node a visible centre
      // position on its first submitted frame.
      if (!gPendingSidebarSpawns.empty())
      {
         const SidebarSpawnRequest request = gPendingSidebarSpawns.front();
         gPendingSidebarSpawns.pop_front();
         // Always begin inside the visible viewport. A small cascade keeps
         // repeated clicks distinct without letting a free-space search push
         // sparse nodes such as Feedback/Output outside the current view.
         const float cascade = 22.0f * (float)(gSidebarSpawnCascade++ % 7);
         const ImVec2 pos(gViewCenterCanvas.x - 125.0f + cascade,
                          gViewCenterCanvas.y - 115.0f + cascade);
         GraphNode* spawned = SpawnNode(request.name, request.category,
                                        pos.x, pos.y);
         ApplySpawnDefaults(spawned);
         if (spawned != nullptr)
         {
            ed::SetNodePosition(spawned->NodeId(), pos);
            spawned->needsPosition = false;
            gPendingSidebarRevealNodeId = spawned->NodeId();
            gPatchDirty = true;
            RuntimeLog::Write("sidebar spawn: %s [%s] at %.1f, %.1f",
                              request.name.c_str(), request.category.c_str(), pos.x, pos.y);
         }
         else
         {
            RuntimeLog::Write("sidebar spawn failed: %s [%s]",
                              request.name.c_str(), request.category.c_str());
         }
      }

      // Dropping a file on the canvas spawns the matching source node, already
      // loaded, at the drop point.
      // Turbo 0.43: files dropped on the timeline panel become Sample clips.
      if (!gDroppedFiles.empty() && ArrangeHandleFileDrop(gDroppedFiles, gDropPos))
         gDroppedFiles.clear();
      // Turbo 0.44: a file dropped on a Clip Matrix cell loads into it
      // (several files fill the following cells of the row).
      if (!gDroppedFiles.empty())
      {
         const ImVec2 cp = ed::ScreenToCanvas(gDropPos);
         if (ClipMatrixHandleDrop(gDroppedFiles[0], cp))
         {
            for (GraphNode& g : gNodes)
               if (auto* m = dynamic_cast<ClipMatrixNode*>(g.node.get()))
               {
                  const int r = m->selRow;
                  int c = m->selCol;
                  bool mine = m->cellRect[r][c][0] <= cp.x && cp.x <= m->cellRect[r][c][2] &&
                              m->cellRect[r][c][1] <= cp.y && cp.y <= m->cellRect[r][c][3];
                  if (!mine)
                     continue;
                  for (size_t k = 1; k < gDroppedFiles.size() && c + 1 < ClipMatrixNode::kMaxCols; k++)
                     m->LoadCell(r, ++c, gDroppedFiles[k]);
                  if (c >= m->cols)
                     m->cols = std::min(ClipMatrixNode::kMaxCols, c + 1);
               }
            gDroppedFiles.clear();
         }
      }
      if (!gDroppedFiles.empty())
      {
         // Everything ModelIO reads. Checked before video because "usdz" and
         // "abc" would otherwise fall through to the image branch and fail.
         static const std::vector<std::string> kAudioExt = {
            "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg"
         };
         static const std::vector<std::string> kModelExt = {
            "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc",
            "fbx", "dae", "3ds" // Turbo: Assimp reads these on Windows
         };
         // Plugin bundles, not files - see the branch that consumes this.
         static const std::vector<std::string> kPluginBundleExt = { "component", "vst3" };
         ImVec2 canvasPos = ed::ScreenToCanvas(gDropPos);
         DrumSequencerNode* dropTargetDrum = FindNodeUnderCanvasPoint<DrumSequencerNode>(canvasPos);
         int dropTargetLane =
            dropTargetDrum != nullptr ? DrumSequencerLaneForCanvasPos(dropTargetDrum, canvasPos.x, canvasPos.y) : 0;
         float offset = 0.0f;
         bool droppedCheckpointPushed = false;
         auto ensureDroppedCheckpoint = [&]()
         {
            if (!droppedCheckpointPushed)
            {
               PushUndoCheckpoint();
               droppedCheckpointPushed = true;
            }
         };
         for (std::string path : gDroppedFiles)
         {
            while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
               path.pop_back();

            if (HasExtension(path, std::vector<std::string> { "inf", "infinite" }))
            {
               GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
               gRequestFitView = true;
               continue;
            }

            if (dropTargetDrum != nullptr && HasExtension(path, kAudioExt))
            {
               ensureDroppedCheckpoint();
               dropTargetDrum->LoadFileToLane(dropTargetLane, path);
               dropTargetLane = (dropTargetLane + 1) % DrumSequencerNode::kNumLanes;
               gPatchDirty = true;
               continue;
            }

            // Turbo 0.50: a .mid dropped on a Drum Sequencer imports its
            // drum notes into the live part.
            if (dropTargetDrum != nullptr && HasExtension(path, std::vector<std::string> { "mid", "midi", "rmi" }))
            {
               ensureDroppedCheckpoint();
               std::string msg;
               dropTargetDrum->ImportMidiFile(path, msg);
               gPatchDirty = true;
               continue;
            }

            // Turbo 0.48: a MIDI file loads into the MIDI File node it is
            // dropped on, or spawns a new one already loaded.
            if (HasExtension(path, std::vector<std::string> { "mid", "midi" }))
            {
               if (MidiFileNode* target = FindNodeUnderCanvasPoint<MidiFileNode>(canvasPos))
               {
                  ensureDroppedCheckpoint();
                  target->LoadFile(path);
               }
               else if (GraphNode* g = SpawnNode("MIDI File", "Notes", canvasPos.x + offset, canvasPos.y))
               {
                  static_cast<MidiFileNode*>(g->node.get())->LoadFile(path);
                  offset += 240.0f;
               }
               gPatchDirty = true;
               continue;
            }

            GraphNode* spawned = nullptr;
            if (HasExtension(path, kPluginBundleExt))
            {
               const bool isVst3 = HasExtension(path, std::vector<std::string> { "vst3" });
#if !INFINITE_ENABLE_VST3
               if (isVst3)
               {
                  printf("VST3 support is not compiled into this build (build with "
                         "-DINFINITE_ENABLE_VST3=ON): %s\n", path.c_str());
                  continue;
               }
#endif
               std::vector<Platform::PluginDesc> found;
               const bool resolved = isVst3
                  ? (Platform::DescribeVST3Bundle(path, found) && !found.empty())
                  : (Platform::DescribeAudioUnitBundle(path, found) && !found.empty());
               if (!resolved)
               {
                  printf("dropped plugin bundle could not be resolved to a plugin: %s\n",
                         path.c_str());
                  continue;
               }
               // Prefer whatever the scanner already knows about this identity:
               // its display name came from the component registry, which is
               // better than the bundle's own Info.plist string.
               Platform::PluginDesc desc = found.front();
               if (const PluginScanner::Entry* known = gPluginScanner.FindByIdentifier(desc.identifier))
               {
                  const std::string origPath = desc.path;
                  desc = *known;
                  if (desc.path.empty() && !origPath.empty())
                     desc.path = origPath;
               }

               if (AudioPluginNode* target = FindNodeUnderCanvasPoint<AudioPluginNode>(canvasPos))
               {
                  ensureDroppedCheckpoint();
                  target->LoadPlugin(desc);
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Plugin", "AudioEffects", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<AudioPluginNode*>(spawned->node.get())->LoadPlugin(desc);
            }
            else if (HasExtension(path, kAudioExt))
            {
               spawned = SpawnNode("Audio File", "Modulators", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<AudioFileNode*>(spawned->node.get())->Open(path);
            }
            else if (HasExtension(path, std::vector<std::string> { "gltf", "glb" }))
            {
               // Turbo (from upstream): a glTF/GLB drop builds the whole rig -
               // Model 3D + Material + one Image Source per texture map,
               // already wired - as one undo step. Dropped onto an existing
               // Model 3D it just reloads that node.
               if (ModelSourceNode* target = FindNodeUnderCanvasPoint<ModelSourceNode>(canvasPos))
               {
                  ensureDroppedCheckpoint();
                  target->Load(path);
                  gPatchDirty = true;
                  continue;
               }
               ensureDroppedCheckpoint();
               gSuppressUndoCheckpoints = true;
               // Indices, not GraphNode*: a later SpawnNode can grow gNodes.
               int modelIndex = -1;
               if (GraphNode* modelNode = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y))
               {
                  modelIndex = modelNode->index;
                  static_cast<ModelSourceNode*>(modelNode->node.get())->Load(path);
               }
               int materialIndex = -1;
               if (GraphNode* materialNode = SpawnNode("Material", "3D", canvasPos.x + offset + 260.0f, canvasPos.y))
                  materialIndex = materialNode->index;
               if (modelIndex != -1 && materialIndex != -1)
               {
                  std::string wireErr;
                  ConnectNodes(modelIndex, 0, materialIndex, 0, wireErr);
               }
               std::string gltfErr;
               const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(path, gltfErr);
               if (pkg != nullptr && materialIndex != -1)
               {
                  struct MapSlot { const GltfImport::GltfDecodedImage* img; int mapIndex; const char* slot; };
                  const MapSlot maps[] = {
                     { &pkg->albedo, kMapAlbedo, "albedo" },
                     { &pkg->roughness, kMapRoughness, "roughness" },
                     { &pkg->metallic, kMapMetallic, "metallic" },
                     { &pkg->normalMap, kMapNormal, "normal" },
                     { &pkg->occlusion, kMapAmbientOcclusion, "ao" },
                     { &pkg->emissive, kMapEmission, "emission" },
                  };
                  const float texX = canvasPos.x + offset + 560.0f;
                  float texY = canvasPos.y;
                  for (const MapSlot& m : maps)
                  {
                     if (m.img->pixels.empty())
                        continue;
                     if (GraphNode* texNode = SpawnNode("Image Source", "Source", texX, texY))
                     {
                        static_cast<ImageSourceNode*>(texNode->node.get())
                           ->LoadFromDecoded(m.img->pixels, m.img->width, m.img->height,
                                             std::string("gltf://") + path + "#" + m.slot);
                        std::string wireErr;
                        ConnectNodes(texNode->index, 0, materialIndex, 1 + m.mapIndex, wireErr);
                     }
                     texY += 160.0f;
                  }
               }
               if (GraphNode* modelNode = (modelIndex != -1) ? FindNodeByIndex(modelIndex) : nullptr)
                  modelNode->showParams = true;
               if (GraphNode* materialNode = (materialIndex != -1) ? FindNodeByIndex(materialIndex) : nullptr)
                  materialNode->showParams = true;
               gSuppressUndoCheckpoints = false;
               gPatchDirty = true;
               RebuildAudioTopology();
               offset += 240.0f;
               continue;
            }
            else if (HasExtension(path, kModelExt))
            {
               spawned = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ModelSourceNode*>(spawned->node.get())->Load(path);
            }
            else if (HasExtension(path, kVideoExt))
            {
               spawned = SpawnNode("Video", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<VideoSourceNode*>(spawned->node.get())->Open(path);
            }
            else
            {
               spawned = SpawnNode("Image Source", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ImageSourceNode*>(spawned->node.get())->Load(path);
            }
            if (spawned != nullptr)
               spawned->showParams = true;
            offset += 240.0f;
         }
         gDroppedFiles.clear();
      }

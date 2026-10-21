// Part of src/main.cpp, included there in order (one translation unit).
// MIDI learn pick, cables, splicing, new links, keyboard, deletions, snapping

      // ---- Turbo MIDI learn: pick a parameter / open its MIDI menu ----------
      {
         const int hoveredPin = (int)ed::GetHoveredPin().Get();
         if (hoveredPin != 0 && GraphNode::IsParamPin(hoveredPin) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            gMidiMenuNode = GraphNode::NodeIndexFromPin(hoveredPin);
            gMidiMenuParam = GraphNode::ParamIndexFromPin(hoveredPin);
            gOpenMidiMenu = true;
            gParamRightClickConsumedThisFrame = true;
         }
         // Turbo 0.46: Performance Mode "Assign Parameter...": the parameter
         // nearest the mouse in the hovered node lights up; a click binds it.
         if (gPerfAssigningElemIdx >= 0 && gPerfAssigningElemIdx < (int)gPerfElements.size())
         {
            const int hoveredNode = (int)ed::GetHoveredNode().Get() / GraphNode::kStride;
            const ImVec2 mouse = ImGui::GetMousePos();
            float best = 1e30f;
            std::pair<int, int> pick(-1, -1);
            const ParamPinSpot* pickSpot = nullptr;
            for (const auto& spot : gParamPinSpots)
            {
               if (spot.first.first != hoveredNode)
                  continue;
               const float dx = spot.second.c.x - mouse.x, dy = spot.second.c.y - mouse.y;
               const float d = dx * dx * 0.15f + dy * dy;
               if (d < best)
               {
                  best = d;
                  pick = spot.first;
                  pickSpot = &spot.second;
               }
            }
            if (pickSpot != nullptr)
            {
               ImDrawList* hl = ImGui::GetWindowDrawList();
               hl->AddCircleFilled(pickSpot->c, 11.0f, IM_COL32(0, 230, 255, 40), 20);
               hl->AddCircle(pickSpot->c, 11.0f, IM_COL32(0, 230, 255, 200), 20, 2.0f);
               const Patch::PerfRecord& el = gPerfElements[(size_t)gPerfAssigningElemIdx];
               GraphNode* tn = FindNodeByIndex(pick.first);
               gLiveIssueTip = "Assign '" + el.label + "' " + (gPerfAssigningAxis == 1 ? "(Y axis) " : gPerfAssigningAxis == 2 ? "(extra target) " : "") + "-> " +
                               (tn ? NodeTitleWithInstance(*tn) : std::string("node")) + " / " + pickSpot->name +
                               "\n(click to assign, Esc or right-click cancels)";
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
               {
                  PushUndoCheckpoint();
                  Patch::PerfRecord& e = gPerfElements[(size_t)gPerfAssigningElemIdx];
                  if (gPerfAssigningAxis == 2)
                  {
                     // Turbo 0.50: "+ Add Another Parameter": appended, no duplicates.
                     bool dup = e.dstIndex == pick.first && e.dstParam == pick.second;
                     for (const auto& t : e.targets)
                        dup = dup || (t.dstIndex == pick.first && t.dstParam == pick.second);
                     if (!dup)
                        e.targets.push_back({ pick.first, pick.second, std::string() });
                  }
                  else if (gPerfAssigningAxis == 0)
                  {
                     e.dstIndex = pick.first;
                     e.dstParam = pick.second;
                     e.boolName.clear();
                     e.targets.clear(); // extra destinations only; the primary is dstIndex/dstParam
                     static const char* kDefaults[] = { "Knob", "Fader", "Slider", "Toggle", "XY Pad", "Trigger",
                                                        "NumBox", "Selector", "Pan/Detent", "Step Gate" };
                     bool isDefault = e.label.empty();
                     for (const char* d : kDefaults)
                        isDefault = isDefault || e.label == d;
                     if (isDefault)
                        e.label = pickSpot->name;
                  }
                  else
                  {
                     if (e.dstIndex < 0)
                        e.dstIndex = pick.first;
                     e.dstParam2 = pick.second;
                     e.targetsY.clear();
                  }
                  gPerfAssigningElemIdx = -1;
               }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
               gPerfAssigningElemIdx = -1;
         }
         if (MidiMap::LearnMode() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            const int hoveredNode = (int)ed::GetHoveredNode().Get() / GraphNode::kStride;
            const ImVec2 mouse = ImGui::GetMousePos();
            float best = 1e30f;
            const std::pair<int, int>* pick = nullptr;
            const ParamPinSpot* pickSpot = nullptr;
            for (const auto& spot : gParamPinSpots)
            {
               if (spot.first.first != hoveredNode)
                  continue;
               // Rows first: a parameter's pin sits on its own row, so vertical
               // distance matters more than horizontal.
               const float dx = spot.second.c.x - mouse.x, dy = spot.second.c.y - mouse.y;
               const float d = dx * dx * 0.15f + dy * dy;
               if (d < best)
               {
                  best = d;
                  pick = &spot.first;
                  pickSpot = &spot.second;
               }
            }
            if (pick != nullptr)
               MidiMap::Arm(pick->first, pick->second, pickSpot->name);
         }
      }

      // ---- draw existing links ----
      // Link ids are derived from the destination pin id (kLinkIdBase +
      // dstPin), not from this vector's insertion position. A pin can carry
      // at most one incoming cable, so dstPin is already unique per link and
      // - critically - stable across frames: deleting one cable no longer
      // shifts every link *after* it in gNodes/slot iteration order down to
      // a lower position and hence a different id. That shift used to hand a
      // just-deleted link's id to a completely unrelated surviving link on
      // the very next frame (e.g. deleting a Sampler's note cable could
      // reassign its id to the Sampler's own audio-out cable, which
      // imgui-node-editor - having just processed a deletion for that same
      // id - then also treated as deleted). Positional ids still fit in one
      // collision-free space across image/audio/note/modulation/palette
      // cables, same as before; only the offset changed.
      gLinks.clear();

      // Turbo: resolve "which node owns this pointer" through maps built once
      // per frame instead of scanning every node (with a dynamic_cast each)
      // for every connected input - that was O(N^2) casts per frame. Keys are
      // the INode*, IGeometrySource* and IPaletteSource* addresses of each
      // node (all distinct objects/sub-objects); emplace keeps the first
      // node in gNodes order, matching the old first-match scans.
      std::unordered_map<const void*, GraphNode*> linkOwner;
      std::unordered_map<const IModulator*, std::pair<GraphNode*, int>> linkModOwner;
      linkOwner.reserve(gNodes.size() * 2);
      for (GraphNode& src : gNodes)
      {
         INode* n = src.node.get();
         linkOwner.emplace((const void*)n, &src);
         if (const IGeometrySource* geo = dynamic_cast<IGeometrySource*>(n))
            linkOwner.emplace((const void*)geo, &src);
         if (const IPaletteSource* pal = dynamic_cast<IPaletteSource*>(n))
            linkOwner.emplace((const void*)pal, &src);
         for (int o = 0; o < std::max(1, n->OutputCount()); o++)
            if (const IModulator* m = ModulatorForOutput(n, o))
               linkModOwner.emplace(m, std::make_pair(&src, o));
      }
      auto ownerOf = [&linkOwner](const void* p) -> GraphNode*
      {
         if (p == nullptr)
            return nullptr;
         auto it = linkOwner.find(p);
         return it == linkOwner.end() ? nullptr : it->second;
      };

      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(gn, slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;

            if (GraphNode* src = ownerOf((const void*)cable->GetSource()))
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                  src->OutputPinId(cable->GetOutputSlot()), gn.InputPinId(slot) });
         }
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            if (GraphNode* src = ownerOf((const void*)cable->GetSource()))
            {
               const int audioOutput = cable->GetOutputSlot() > 0 ? cable->GetOutputSlot()
                                       : dynamic_cast<TransportControlNode*>(src->node.get()) != nullptr ? 4
                                       : (dynamic_cast<VideoSourceNode*>(src->node.get()) != nullptr ||
                                          dynamic_cast<VmpcNode*>(src->node.get()) != nullptr ||
                                          dynamic_cast<ClipMatrixNode*>(src->node.get()) != nullptr) ? 1 : 0;
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                  src->OutputPinId(audioOutput), gn.InputPinId(slot) });
            }
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            if (GraphNode* src = ownerOf((const void*)cable->GetSource()))
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                  src->OutputPinId(cable->GetOutputSlot()), gn.InputPinId(slot) });
         }
      }
      for (GraphNode& gn : gNodes)
      {
         auto linkFromNode = [&](const void* wanted, int slot)
         {
            if (wanted == nullptr)
               return;
            // linkOwner holds both the INode* and the IGeometrySource* address
            // of every node (they differ under multiple inheritance).
            if (GraphNode* src = ownerOf(wanted))
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                  src->OutputPinId(), gn.InputPinId(slot) });
         };

         // Render3DNode's geometry slots are found generically below via
         // GeometryInputSlot() too - only its camera/light pins, which aren't
         // geometry, need special-casing here.
         if (auto* render = dynamic_cast<Render3DNode*>(gn.node.get()))
         {
            linkFromNode(render->camera, Render3DNode::kSlots);
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               linkFromNode(render->lights[i], Render3DNode::kSlots + 1 + i);
         }
         for (int slot = 0; slot < kMaxGeometrySlots; slot++)
            if (IGeometrySource** field = gn.node->GeometryInputSlot(slot))
               linkFromNode(*field, slot);
         if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
         {
            if (GraphNode* src = ownerOf((const void*)setColor->paletteInput))
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(2),
                                  src->OutputPinId(), gn.InputPinId(2) });
         }

         auto* audio = dynamic_cast<AudioAnalyzeNode*>(gn.node.get());
         if (audio != nullptr && audio->fileSource != nullptr)
         {
            if (GraphNode* src = ownerOf((const void*)static_cast<INode*>(audio->fileSource)))
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(0),
                                  src->OutputPinId(), gn.InputPinId(0) });
         }

         int modCount = gn.node->ModulatorInputCount();
         if (modCount == 0)
            continue;
         for (int slot = 0; slot < modCount; slot++)
         {
            IModulator* wanted = *gn.node->ModulatorInputSlot(slot);
            if (wanted == nullptr)
               continue;
            auto mod = linkModOwner.find(wanted);
            if (mod != linkModOwner.end())
               gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                  mod->second.first->OutputPinId(mod->second.second), gn.InputPinId(slot) });
         }
      }

      for (const auto& link : Modulation::Instance().Links())
      {
         GraphNode* target = FindNodeByIndex(link.first.first);
         GraphNode* source = FindNodeByIndex(link.second.nodeIndex);
         if (target == nullptr || source == nullptr)
            continue;
         const int paramPin = target->ParamPinId(link.first.second);
         if (gDrawnParamPins.count(paramPin) == 0)
            continue; // no pin declared this frame: emitting the link would kill it
         gLinks.push_back({ kLinkIdBase + paramPin,
                            source->OutputPinId(link.second.outputIndex), paramPin });
      }

      for (const auto& link : PaletteBinding::Instance().Links())
      {
         GraphNode* target = FindNodeByIndex(link.first.first);
         GraphNode* source = FindNodeByIndex(link.second.nodeIndex);
         if (target == nullptr || source == nullptr)
            continue;
         const int colorPin = target->ColorPinId(link.first.second);
         if (gDrawnColorPins.count(colorPin) == 0)
            continue; // no pin declared this frame: emitting the link would kill it
         gLinks.push_back({ kLinkIdBase + colorPin,
                            source->OutputPinId(0), colorPin });
      }

      // ---- drop a node on a cable to splice it in ----
      // While a single node is being dragged, find an image cable passing under
      // it. The link is approximated as a straight line between the two nodes'
      // facing edges rather than the bezier actually drawn: close enough to feel
      // right, and it avoids reaching into the editor's internal curve geometry.
      const bool isLight = IsThemeLight();
      for (const LinkInfo& link : gLinks)
      {
         const bool isMod = GraphNode::IsParamPin(link.dstPin);
         if (isMod)
         {
            ed::Link(link.id, link.srcPin, link.dstPin,
                     isLight ? ImColor(215, 120, 10) : ImColor(255, 190, 90), 2.0f);
            continue;
         }
         // Audio = blue, Note = green (docs/plans/audio/README.md's colour
         // scheme). Only ordinary input pins can be audio/note - param/colour
         // pins are already handled above.
         bool tinted = false;
         if (GraphNode::IsInputPin(link.dstPin))
         {
            GraphNode* dst = FindNodeByIndex(GraphNode::NodeIndexFromPin(link.dstPin));
            if (dst != nullptr)
            {
               const int slot = GraphNode::InputSlotFromPin(link.dstPin);
               if (dst->node->AudioInputSlot(slot) != nullptr)
               {
                  ed::Link(link.id, link.srcPin, link.dstPin,
                           isLight ? ImColor(30, 100, 230) : ImColor(90, 150, 255), 2.0f);
                  tinted = true;
               }
               else if (dst->node->NoteInputSlot(slot) != nullptr)
               {
                  ed::Link(link.id, link.srcPin, link.dstPin,
                           isLight ? ImColor(20, 150, 60) : ImColor(90, 220, 130), 2.0f);
                  tinted = true;
               }
            }
         }
         if (!tinted)
            ed::Link(link.id, link.srcPin, link.dstPin,
                     isLight ? ImColor(55, 62, 78) : ImColor(230, 235, 245), 2.0f);
      }

      // ---- handle new connections ----
      // Turbo 0.59: no new cables under the show lock.
      if (!gShowLock && ed::BeginCreate(isLight ? ImColor(55, 62, 78) : ImColor(255, 255, 255), 2.0f))
      {
         ed::PinId startPin, endPin;
         if (ed::QueryNewLink(&startPin, &endPin))
         {
            if (startPin && endPin)
            {
               int a = (int)startPin.Get();
               int b = (int)endPin.Get();

               // normalize so `a` is the output side
               if (!GraphNode::IsOutputPin(a))
                  std::swap(a, b);

               GraphNode* srcNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(a));
               GraphNode* dstNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(b));
               const bool differentNodes = GraphNode::NodeIndexFromPin(a) != GraphNode::NodeIndexFromPin(b);
               const int srcOutputIndex = GraphNode::OutputIndexFromPin(a);
               const bool srcIsModulator = srcNode != nullptr &&
                                           (dynamic_cast<IModulator*>(srcNode->node.get()) != nullptr ||
                                            ModulatorForOutput(srcNode->node.get(), srcOutputIndex) != nullptr);
               auto* srcPalette = srcNode ? dynamic_cast<IPaletteSource*>(srcNode->node.get()) : nullptr;
               auto* srcAudioFile = srcNode ? dynamic_cast<AudioFileNode*>(srcNode->node.get()) : nullptr;
               auto* srcGeometry = srcNode ? dynamic_cast<IGeometrySource*>(srcNode->node.get()) : nullptr;
               auto* srcCamera = srcNode ? dynamic_cast<CameraNode*>(srcNode->node.get()) : nullptr;
               auto* srcLight = srcNode ? dynamic_cast<LightNode*>(srcNode->node.get()) : nullptr;
               const bool srcIsEnvironment = srcNode != nullptr &&
                                             dynamic_cast<EnvironmentNode*>(srcNode->node.get()) != nullptr;
               const bool srcIsAudioNode = srcNode != nullptr &&
                                           IsAudioOutput(srcNode->node.get(), srcOutputIndex);
               const bool srcIsNoteSource = srcNode != nullptr &&
                                            IsNoteOutput(srcNode->node.get(), srcOutputIndex);

               bool valid = false;
               const char* rejectReason = nullptr;
               if (GraphNode::IsOutputPin(a) && srcNode != nullptr && dstNode != nullptr && differentNodes)
               {
                  // modulators patch into parameters and into Math's inputs;
                  // image nodes patch into image inputs
                  if (GraphNode::IsParamPin(b))
                     valid = srcIsModulator;
                  else if (GraphNode::IsColorPin(b))
                     valid = srcPalette != nullptr;
                  else if (GraphNode::IsInputPin(b))
                  {
                     valid = IsInputSlotCompatible(dstNode, GraphNode::InputSlotFromPin(b),
                                                    srcIsModulator, srcPalette, srcGeometry, srcCamera,
                                                    srcLight, srcAudioFile, srcIsEnvironment,
                                                    srcIsAudioNode, srcIsNoteSource);
                     if (valid && srcIsAudioNode &&
                         WouldCreateAudioCycle(srcNode->node.get(), dstNode->node.get()))
                     {
                        valid = false;
                        rejectReason = "Cannot connect: this would create an audio feedback loop";
                     }
                     if (valid && srcIsNoteSource &&
                         WouldCreateNoteCycle(srcNode->node.get(), dstNode->node.get()))
                     {
                        valid = false;
                        rejectReason = "Cannot connect: this would create a note feedback loop";
                     }
                     if (valid && srcGeometry != nullptr &&
                         dstNode->node->GeometryInputSlot(GraphNode::InputSlotFromPin(b)) != nullptr &&
                         WouldCreateGeometryCycle(srcNode->node.get(), dstNode->node.get()))
                     {
                        valid = false;
                        rejectReason = "Cannot connect: this would create a geometry loop";
                     }
                  }
               }

               // Why a refused drag was refused, surfaced as a tooltip below
               // (audio-node-ui-system §6a, extended across all 3D/image/signal pins).
               if (!valid && rejectReason == nullptr && dstNode != nullptr && differentNodes)
               {
                  if (GraphNode::IsColorPin(b))
                  {
                     rejectReason = "This color slot only accepts a Palette node";
                  }
                  else if (GraphNode::IsParamPin(b))
                  {
                     if (srcIsAudioNode || srcIsNoteSource)
                        rejectReason = "Audio/note signals can't drive a parameter pin - only a modulator can";
                     else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
                        rejectReason = "3D objects cannot drive a parameter pin - only a modulator can";
                     else if (!srcIsModulator)
                        rejectReason = "Only modulator nodes (LFO, Envelope, Formula, etc.) can drive a parameter pin";
                  }
                  else if (GraphNode::IsInputPin(b))
                  {
                     const int slot = GraphNode::InputSlotFromPin(b);
                     const bool dstWantsAudio = dstNode->node->AudioInputSlot(slot) != nullptr;
                     const bool dstWantsNote = dstNode->node->NoteInputSlot(slot) != nullptr;
                     auto* dstRenderNode = dynamic_cast<Render3DNode*>(dstNode->node.get());
                     auto* dstMaterialNode = dynamic_cast<MaterialNode*>(dstNode->node.get());
                     auto* dstDispNode = dynamic_cast<DisplacementNode*>(dstNode->node.get());
                     auto* dstSetColorNode = dynamic_cast<SetColorNode*>(dstNode->node.get());
                     auto* dstMappingNode = dynamic_cast<MappingNode*>(dstNode->node.get());

                     if (dstWantsAudio && !srcIsAudioNode)
                        rejectReason = srcIsModulator
                           ? "A modulator can't drive an audio signal pin - only another audio source can"
                           : "This pin only accepts an audio source";
                     else if (dstWantsNote && !srcIsNoteSource)
                        rejectReason = "This pin only accepts a note source";
                     else if ((srcIsAudioNode || srcIsNoteSource) && !dstWantsAudio && !dstWantsNote)
                        rejectReason = "Audio/note signals only connect to a matching audio/note pin";
                     else if (dstRenderNode != nullptr)
                     {
                        if (slot < Render3DNode::kSlots)
                        {
                           if (srcCamera != nullptr)
                              rejectReason = "Camera connects to the Camera slot (slot 5), not geometry slots";
                           else if (srcLight != nullptr)
                              rejectReason = "Light connects to the Light slots (slots 6-8), not geometry slots";
                           else if (srcIsEnvironment)
                              rejectReason = "HDRI connects to the Environment slot (slot 9), not geometry slots";
                           else
                              rejectReason = "Render 3D geometry slots only accept 3D geometry sources";
                        }
                        else if (slot == Render3DNode::kSlots)
                           rejectReason = "This slot only accepts a Camera 3D node";
                        else if (slot == Render3DNode::kEnvSlot)
                           rejectReason = "Environment slot only accepts an HDRI Environment node";
                        else
                           rejectReason = "This slot only accepts a Light 3D node";
                     }
                     else if (dstMaterialNode != nullptr && slot >= 1 && slot <= kMapCount)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = "Material map slots accept 2D images or textures, not 3D geometry";
                        else if (srcIsModulator)
                           rejectReason = "Material map slots accept 2D images or textures, not modulators";
                        else
                           rejectReason = "Material map slots accept 2D images or textures";
                     }
                     else if (dstDispNode != nullptr && slot == 1)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = "Displacement height slot accepts a 2D image or texture map, not 3D geometry";
                        else
                           rejectReason = "Displacement height slot accepts a 2D image or texture map";
                     }
                     else if (dstSetColorNode != nullptr && slot == 2)
                     {
                        rejectReason = "Set Vertex Color palette slot only accepts a Palette node";
                     }
                     else if (dstSetColorNode != nullptr && slot == 1)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = "Set Vertex Color texture slot accepts a 2D image or texture map, not 3D geometry";
                        else
                           rejectReason = "Set Vertex Color texture slot accepts a 2D image or texture map";
                     }
                     else if (dstMappingNode != nullptr)
                     {
                        rejectReason = "Mapping transforms 3D surface coordinates. Wire 3D geometry into Mapping, then into Material or Render 3D.";
                     }
                     else if (dstNode->node->GeometryInputSlot(slot) != nullptr)
                     {
                        if (srcCamera != nullptr || srcLight != nullptr)
                           rejectReason = "Camera and Light nodes connect to Render 3D, not geometry operators";
                        else
                           rejectReason = "This pin requires a 3D geometry source, not a 2D image";
                     }
                     else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
                     {
                        rejectReason = "3D geometry cannot be connected directly to a 2D image node. Connect geometry into a Render 3D node first.";
                     }
                     else if (dynamic_cast<AudioAnalyzeNode*>(dstNode->node.get()) != nullptr)
                     {
                        rejectReason = "Audio Analyze only accepts an audio cable";
                     }
                     else if (dstNode->node->ModulatorInputSlot(slot) != nullptr && dynamic_cast<ImageAnalyzeNode*>(dstNode->node.get()) == nullptr)
                     {
                        rejectReason = "This pin only accepts a modulator source";
                     }
                     else if (srcIsModulator)
                     {
                        rejectReason = "Image inputs accept 2D image sources, not modulators";
                     }
                     else
                     {
                        rejectReason = "Incompatible connection";
                     }
                  }
               }

               if (valid && ed::AcceptNewItem())
               {
                  PushUndoCheckpoint();
                  if (GraphNode::IsParamPin(b))
                  {
                     Modulation::Instance().Bind(dstNode->index,
                                                 GraphNode::ParamIndexFromPin(b),
                                                 srcNode->index,
                                                 GraphNode::OutputIndexFromPin(a));
                  }
                  else if (GraphNode::IsColorPin(b))
                  {
                     // Hand out a different swatch each time rather than the
                     // same one: dragging a palette onto a ramp's five stops in
                     // turn should lay the palette across the gradient, which
                     // is the whole point, not paint it a flat colour five
                     // times over.
                     PaletteBinding& palette = PaletteBinding::Instance();
                     const int used = palette.BindingCountFrom(srcNode->index, dstNode->index);
                     const int count = std::max(1, srcPalette->SwatchCount());
                     palette.Bind(dstNode->index, GraphNode::ColorIndexFromPin(b),
                                  srcNode->index, used % count);
                  }
                  else
                  {
                     WireInputSlot(*srcNode, *dstNode, GraphNode::InputSlotFromPin(b),
                                   GraphNode::OutputIndexFromPin(a));
                     const int wiredSlot = GraphNode::InputSlotFromPin(b);
                     if (srcIsAudioNode || srcIsNoteSource ||
                         dstNode->node->AudioInputSlot(wiredSlot) != nullptr ||
                         dstNode->node->NoteInputSlot(wiredSlot) != nullptr)
                        RebuildAudioTopology();
                  }
               }
               else if (!valid)
               {
                  ed::RejectNewItem(ImColor(255, 80, 80), 2.0f);
                  if (rejectReason != nullptr)
                  {
                     // Suspended: a tooltip submitted between ed::Begin and
                     // ed::End inherits the canvas transform and lands offset
                     // from the cursor by an amount that grows with zoom and
                     // pan (v3 §1a - same bug the knob tooltip had).
                     ed::Suspend();
                     ImGui::SetTooltip("%s", rejectReason);
                     ed::Resume();
                  }
               }
            }
         }

         ed::PinId newNodePin;
         if (ed::QueryNewNode(&newNodePin) && ed::AcceptNewItem())
         {
            gLinkDragSourcePin = (int)newNodePin.Get();
            gLinkDragSuggestions.clear();
            // Do not instantiate every registered node merely to rank this
            // popup. Several node constructors probe devices or initialise
            // heavy runtimes, which caused a multi-second UI stall when a
            // cable was released on empty canvas. The normal filtered search
            // still opens immediately, and the eventual connection is
            // validated before it is made.
            gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
            searchBuf[0] = '\0';
            searchJustOpened = true;
            ImGui::OpenPopup("search");
         }
      }
      ed::EndCreate();

      // ---- keyboard: delete + copy/paste ----
      const bool typing = io.WantTextInput || gArrangeKeysOwned || gPerfMatrixFocused || gChordSlotsKeysOwned; // Turbo 0.46, 0.51
      gChordSlotsKeysOwned = false;
      const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;

      // Shift+Cmd+Z is the Mac convention for redo; Ctrl+Y also works for
      // anyone used to the Windows/Linux binding.
      // Undo/redo and Space keep working with the timeline focused.
      // Turbo 0.59: not under the show lock (an undo can delete or move nodes).
      if (!gShowLock && !io.WantTextInput && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
         Undo();
      if (!gShowLock && !io.WantTextInput && ((cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
                      (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))))
         Redo();

      // Shift+A selects every node on the canvas.
      if (!typing && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false))
      {
         ed::ClearSelection();
         for (GraphNode& gn : gNodes)
            ed::SelectNode(gn.NodeId(), true);
      }

      // "/" drops a comment under the pointer and puts the caret straight into
      // it, so annotating a patch is one keystroke and then typing. Not gated on
      // Shift, so "?" does not leave a stray comment behind, and not on a
      // modifier, so Cmd-/ stays free for a binding later.
      if (!typing && !cmdOrCtrl && !io.KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_Slash, false))
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
      if (!io.WantTextInput && !cmdOrCtrl && !io.KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_Space, false))
         Transport::Instance().TogglePlay();

      if (!typing && !gShowLock && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
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

            gDeferAudioRebuild = false;
            if (linkCount > 0 || !toDelete.empty())
               RebuildAudioTopology();
            gSuppressUndoCheckpoints = false;
            ed::ClearSelection();
         }
      }

      // Shift+D duplicates whatever is selected without touching the clipboard.
      if (!typing && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false))
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

            // Resolve everything first: SpawnNode can reallocate gNodes.
            const ImVec2 off = ClusterOffset(toDup);
            struct DupItem
            {
               std::string type; std::string category; INode* src; ImVec2 pos; bool params;
               bool miniViewport;
               bool preview;
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
                                    gn->showMiniViewport, gn->showPreview, gn->showAdvancedParams,
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
                  copy->showParams = item.params;
                  copy->showMiniViewport = item.miniViewport;
                  copy->showPreview = item.preview;
                  copy->showAdvancedParams = item.advancedParams;
                  if (GraphNode* orig = FindNodeByIndex(item.origIndex))
                     copy->colorTag = orig->colorTag;
                  // A duplicated Predictive node starts fresh instead of
                  // inheriting the source's learned model (upstream rule).
                  if (auto* pn = dynamic_cast<PredictiveNotesNode*>(copy->node.get()))
                     pn->ResetLearnedState();
                  else if (auto* pr = dynamic_cast<PredictiveRhythmNode*>(copy->node.get()))
                     pr->ResetLearnedState();
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
            gSuppressUndoCheckpoints = false;
         }
      }

      const bool doGroup =
         gRequestGroup ||
         (!typing && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      const bool doUngroup =
         gRequestUngroup ||
         (!typing && cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      gRequestGroup = false;
      gRequestUngroup = false;

      // Cmd/Ctrl+Shift+G dissolves a group, leaving its nodes exactly where
      // they sit. Only the group node goes away - RemoveNodeByIndex drops the
      // membership set with it, so the freed nodes are immediately available
      // for another group to adopt.
      //
      // Selecting a member counts as selecting its group: having to click the
      // header first would mean the one gesture that says "this cluster" is
      // not the gesture that can dissolve it.
      if (doUngroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // Resolved up front: RemoveNodeByIndex erases from gNodes, which
            // invalidates every GraphNode* taken before it.
            std::set<int> doomed;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* sel = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (sel == nullptr)
                  continue;
               if (dynamic_cast<GroupNode*>(sel->node.get()) != nullptr)
               {
                  doomed.insert(sel->index);
                  continue;
               }
               if (GroupNode* owner = GroupOwning(sel->index))
               {
                  for (GraphNode& g : gNodes)
                  {
                     if (g.node.get() == owner)
                        doomed.insert(g.index);
                  }
               }
            }

            // One checkpoint for the whole batch - see the Delete-key handler
            // above for why (multiple groups dissolved at once would
            // otherwise leave Undo only able to claw back the last one).
            if (!doomed.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            gDeferAudioRebuild = true;
            for (int index : doomed)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }
            gDeferAudioRebuild = false;
            if (!doomed.empty())
               RebuildAudioTopology();
            gSuppressUndoCheckpoints = false;
            if (!doomed.empty())
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
               // Space for the label row above the box - AutoFitGroupToMembers
               // corrects both this and the size from the real measured header
               // on the group's first drawn frame, so it only has to be close.
               const float headerAllowance = 34.0f;
               if (GraphNode* gn = SpawnNode("Group", "Compositing",
                                             bmin.x - kGroupPadding,
                                             bmin.y - kGroupPadding - headerAllowance))
               {
                  auto* g = static_cast<GroupNode*>(gn->node.get());
                  g->width = (bmax.x - bmin.x) + kGroupPadding * 2.0f;
                  g->height = (bmax.y - bmin.y) + kGroupPadding * 2.0f;
                  // Claim the selection explicitly rather than leaving it to
                  // the geometric adoption pass: these are the nodes the user
                  // pointed at, whether or not the initial box happens to
                  // cover every one of them exactly.
                  gGroupMembers[g] = picked;
                  ed::ClearSelection();
                  gPendingSelect.push_back(gn->NodeId());
               }
            }
         }
      }

      if (!typing && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
      {
         clipboard.clear();
         clipboardSources.clear();
         clipboardOrigIndex.clear();
         clipboardOrigGroup.clear();
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
         }
      }

      if (!typing && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard.empty())
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
         std::map<int, GraphNode*> newByOrig;
         for (const PasteItem& item : items)
         {
            GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y);
            if (copy != nullptr)
            {
               CopyParams(copy->node.get(), item.src);
               if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
                  rn->seed = RandomNode::NextSeed();
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
         gSuppressUndoCheckpoints = false;
      }

      // ---- handle deletions raised by the editor itself ----
      // Turbo 0.59: the show lock refuses every deletion the editor raises.
      if (gShowLock && ed::BeginDelete())
      {
         ed::LinkId lockedLink;
         while (ed::QueryDeletedLink(&lockedLink))
            ed::RejectDeletedItem();
         ed::NodeId lockedNode;
         while (ed::QueryDeletedNode(&lockedNode))
            ed::RejectDeletedItem();
         ed::EndDelete();
      }
      else if (!gShowLock && ed::BeginDelete())
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
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         gDragStartSnapshot = BuildPatchData();
         gDragSnapshotValid = true;
         gDragSnapshotPushed = false;
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
            PushUndoSnapshot(gDragStartSnapshot);
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

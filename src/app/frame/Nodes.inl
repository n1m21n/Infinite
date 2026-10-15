// Part of src/main.cpp, included there in order (one translation unit).
// per-node drawing loop

      PruneDeadGroups();

      // Turbo: nodes whose parameters must be registered every frame even when
      // scrolled off screen (see the off-screen culling below): anything with
      // a MIDI mapping or an expression, since both only reach a parameter in
      // the frames its node draws.
      std::unordered_set<int> cullExemptNodes;
      for (const MidiMap::Mapping& m : MidiMap::All())
         cullExemptNodes.insert(m.nodeIndex);
      for (const auto& e : Modulation::Instance().Expressions())
         cullExemptNodes.insert(e.first.first);
      // Turbo 0.46: Performance Mode writes only land on drawn params.
      for (const Patch::PerfRecord& el : gPerfElements)
      {
         if (el.dstIndex >= 0)
            cullExemptNodes.insert(el.dstIndex);
         for (const auto& t : el.targets)
            cullExemptNodes.insert(t.dstIndex);
         for (const auto& t : el.targetsY)
            cullExemptNodes.insert(t.dstIndex);
      }
      const bool cullingAllowed = !MidiMap::LearnMode() && gPerfAssigningElemIdx < 0 && getenv("INFINITE_NO_CULL") == nullptr;

      for (GraphNode& gn : gNodes)
      {
         if (gn.needsPosition)
         {
            ed::SetNodePosition(gn.NodeId(), ImVec2(gn.spawnX, gn.spawnY));
            gn.needsPosition = false;
         }

         // Cached here, inside the editor context, for anything that needs a
         // position later in the frame when the context is gone.
         {
            const ImVec2 live = ed::GetNodePosition(gn.NodeId());
            if (std::isfinite(live.x) && std::abs(live.x) <= 1e6f && live.x > -2e9f)
               gn.liveX = live.x;
            else if (!std::isfinite(gn.liveX) || std::abs(gn.liveX) > 1e6f || gn.liveX <= -2e9f)
               gn.liveX = gn.spawnX;

            if (std::isfinite(live.y) && std::abs(live.y) <= 1e6f && live.y > -2e9f)
               gn.liveY = live.y;
            else if (!std::isfinite(gn.liveY) || std::abs(gn.liveY) > 1e6f || gn.liveY <= -2e9f)
               gn.liveY = gn.spawnY;
         }

         if (auto* group = dynamic_cast<GroupNode*>(gn.node.get()))
         {
            DrawGroupNode(gn, group);
            continue;
         }

         // Off-screen culling (ported from upstream): a node well outside the
         // view keeps the box and pins of its last real layout (so cables to it
         // still land, see ed::KeepOffscreenNodeAlive) and skips its body.
         // Always drawn: nodes with modulation / palette / MIDI / expression
         // bindings (a parameter only exists in the frames its node draws),
         // everything while a popup is open or in MIDI learn mode, the
         // Keyboard node (key releases are handled in its body), and every
         // node once per kCullRefresh frames, staggered, so size or pin
         // changes made while it is away show up. INFINITE_NO_CULL=1 disables it.
         if (cullingAllowed)
         {
            constexpr int kCullRefresh = 30;
            constexpr float kCullMargin = 64.0f;
            const bool mustDraw = gn.hasModulatedParams || gn.hasPaletteColors ||
                                  cullExemptNodes.count(gn.index) > 0 ||
                                  ImGui::GetCurrentContext()->OpenPopupStack.Size > 0 ||
                                  dynamic_cast<KeyboardNode*>(gn.node.get()) != nullptr ||
                                  // Turbo 0.50: Scenes ticks its quantize grid and pulses in its body.
                                  dynamic_cast<ScenesNode*>(gn.node.get()) != nullptr ||
                                  gn.NodeId() == gPendingSidebarRevealNodeId ||
                                  ((frameId + gn.index) % kCullRefresh) == 0;
            if (!mustDraw && ed::KeepOffscreenNodeAlive(gn.NodeId(), kCullMargin))
               continue;
         }

         // Category tint: same idea as DrawGroupNode's stored colour, but from
         // the static per-category table since categories are a fixed
         // vocabulary, not something a user repicks per node. Blended into the
         // library's own default NodeBg rather than replacing it outright, so
         // a node still reads as "the same kind of card", just tinted.
         const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
         const CategoryColors::Color& catColor = CategoryColors::ColorFor(gn.category);
         const bool isLight = (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);
         const float kTintWeight = isLight ? 0.12f : 0.16f;
         ed::PushStyleColor(ed::StyleColor_NodeBg,
                            ImColor(t.panelBg.r * (1.0f - kTintWeight) + catColor.r * kTintWeight,
                                    t.panelBg.g * (1.0f - kTintWeight) + catColor.g * kTintWeight,
                                    t.panelBg.b * (1.0f - kTintWeight) + catColor.b * kTintWeight,
                                    (isLight ? 0.95f : 0.784f) * (gCanvasBgActive ? gCanvasBgNodeAlpha : 1.0f)));
         // Turbo 0.45: a live validation hint turns the border amber.
         const bool hasLiveIssue = gLiveIssues.count(gn.index) != 0;
         ed::PushStyleColor(ed::StyleColor_NodeBorder,
                            hasLiveIssue ? ImColor(0.95f, 0.65f, 0.15f, 0.95f)
                                         : ImColor(catColor.r, catColor.g, catColor.b, isLight ? 0.75f : 0.55f));
         if (hasLiveIssue)
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f);

         ed::BeginNode(gn.NodeId());
         ImGui::PushID(gn.index);
         const bool midiLearnLock = MidiMap::LearnMode() || gPerfAssigningElemIdx >= 0; // Turbo 0.46: assign mode too
         if (midiLearnLock)
            ImGui::BeginDisabled();
         const bool dimmed = gn.node->bypassed;
         if (dimmed)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);

         // --- inputs spread along the top edge ---
         int inputs = InputCountFor(gn);
         const float pinSpacing = (inputs > 4) ? 8.0f : 12.0f;
         for (int slot = 0; slot < inputs; slot++)
         {
            char label[24];
            if (const char* named = gn.node->InputLabel(slot))
               snprintf(label, sizeof(label), "%s", named);
            else if (inputs == 1)
               label[0] = '\0';
            else
               snprintf(label, sizeof(label), "%c", 'A' + slot);
            DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
            if (slot + 1 < inputs)
               ImGui::SameLine(0.0f, pinSpacing);
         }

         // Group the body so its measured width can right-align the out pin.
         // ed::GetNodeSize() is scaled by the current zoom, so feeding it back
         // into padding inflated the node a little more every frame until it
         // covered the canvas and swallowed every click.
         ImGui::BeginGroup();

         ImGui::TextUnformatted(NodeTitle(gn).c_str());
         {
            int instanceTotal = 0;
            const int instanceIdx = GetNodeInstanceIndex(gn, &instanceTotal);
            if (instanceTotal > 1)
            {
               ImGui::SameLine(0.0f, 4.0f);
               ImGui::TextDisabled("#%d", instanceIdx);
            }
         }
         if (CategoryColors::IsDesignTheme())
            // Turbo 0.57: Infinite Dark keeps the category as a quiet caption
            // tinted by its colour, so the title carries the header.
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(catColor.r * 0.45f + 0.30f, catColor.g * 0.45f + 0.30f,
                                         catColor.b * 0.45f + 0.30f, 0.70f));
         else if (isLight)
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(catColor.r * 0.75f, catColor.g * 0.75f,
                                         catColor.b * 0.75f, 1.0f));
         else
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(catColor.r * 0.6f + 0.4f, catColor.g * 0.6f + 0.4f,
                                         catColor.b * 0.6f + 0.4f, 1.0f));
         ImGui::TextUnformatted(gn.category.c_str());
         ImGui::PopStyleColor();

         // Moved ahead of the old call site (right before the showParams
         // dispatch below) so DrawAudioNodeBody's ModSlider calls - which
         // now run inside the preview/body section below, not the params
         // section - get correct pin ids/param registration. Harmless for
         // every other node type: nothing between here and the old call
         // site used gCurrentNodeIndex/gParamCounter before this moved.
         BeginNodeParams(gn.index);

         // --- preview: image for image nodes, a value meter for modulators ---
         const bool multiOutModulator =
            dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<OscToCvNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<GeometryTableNode*>(gn.node.get()) != nullptr;
         IGeometrySource* geoSourceForViewport = dynamic_cast<IGeometrySource*>(gn.node.get());
         const bool isAudioBodyNode = IsAudioBodyNode(gn.node.get());
         const bool canTogglePreview = CanToggleInlinePreview(gn);
         const bool inlinePreviewEnabled = !canTogglePreview || InlinePreviewEnabled(gn);
         if (auto* scenesNode = dynamic_cast<ScenesNode*>(gn.node.get()))
            DrawScenesBody(scenesNode, gn.index); // Turbo 0.50
         else if (multiOutModulator)
            ; // these draw their own meters in the params panel
         else if (auto* uiButton = dynamic_cast<UIButtonNode*>(gn.node.get()))
            DrawUIButtonBody(uiButton, gn.index, frameId, inlinePreviewEnabled);
         else if (auto* uiSlider = dynamic_cast<UISliderNode*>(gn.node.get()))
            DrawUISliderBody(uiSlider, gn.index, inlinePreviewEnabled);
         else if (auto* uiXy = dynamic_cast<UIXYNode*>(gn.node.get()))
            DrawUIXYBody(uiXy, gn.index, inlinePreviewEnabled);
         else if (auto* m = dynamic_cast<MacroSliderNode*>(gn.node.get()))
            DrawMacroSliderBody(m);
         else if (auto* m = dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()))
            DrawMacroBipolarKnobBody(m);
         else if (auto* m = dynamic_cast<MacroToggleNode*>(gn.node.get()))
            DrawMacroToggleBody(m);
         else if (auto* m = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
            DrawMacroTriggerBody(m);
         else if (auto* m = dynamic_cast<MacroNumBoxNode*>(gn.node.get()))
            DrawMacroNumBoxBody(m);
         else if (auto* m = dynamic_cast<MacroRadioSelectorNode*>(gn.node.get()))
            DrawMacroRadioSelectorBody(m);
         else if (auto* m = dynamic_cast<MacroStepGateNode*>(gn.node.get()))
            DrawMacroStepGateBody(m);
         else if (!isAudioBodyNode && dynamic_cast<IModulator*>(gn.node.get()) != nullptr)
         {
            // Audio/note nodes are excluded here even when they implement
            // IModulator: EnvelopeNode does (its output is a modulator
            // value, see audio-graph-semantics.md §6), and this branch used
            // to win the dispatch race against the IsAudioBodyNode branch
            // below - so Envelope silently rendered a generic modulator
            // meter and DrawEnvelopeBody never ran at all.
            DrawModulatorMeter(dynamic_cast<IModulator*>(gn.node.get()), gn.index);
         }
         else if (inlinePreviewEnabled && gn.showMiniViewport && geoSourceForViewport != nullptr &&
                  HasUsefulMiniViewport(gn.node.get()))
            DrawMiniViewport(gn, geoSourceForViewport);
         else if (!inlinePreviewEnabled)
         {
            const bool isWide = (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr ||
                                 dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
            const float boxW = isWide ? kWideNodeWidth : kPreviewSize;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float h = 52.0f;
            ImGui::Dummy(ImVec2(boxW, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 br(origin.x + boxW, origin.y + h);
            dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);
            dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
            dl->AddText(ImVec2(origin.x + 12, origin.y + 10), IM_COL32(200, 206, 226, 255),
                        NodeTitleWithInstance(gn).c_str());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 29), IM_COL32(130, 136, 156, 255),
                        "preview disabled");
         }
         else if (dynamic_cast<GeometryOpNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<InstanceOnPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ModelSourceNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Text3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<OceanNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<AudioDisplacementNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<AudioRibbonNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<SetColorNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ParticleSystemNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<DepthProjectionNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
         {
            const bool isWide = (dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
            const float boxW = isWide ? kWideNodeWidth : kPreviewSize;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            const float h = 52.0f;
            ImGui::Dummy(ImVec2(boxW, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + boxW, origin.y + h);
            dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);
            dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
            char line[64] = "";
            if (auto* o = dynamic_cast<GeometryOpNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", o->TriangleCount());
            else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu instances", inst->InstanceCount());
            else if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", model->TriangleCount());
            else if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", t3d->TriangleCount());
            else if (auto* n3d = dynamic_cast<Null3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", n3d->TriangleCount());
            else if (auto* mapn = dynamic_cast<MappingNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", mapn->TriangleCount());
            else if (auto* m2p = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points", m2p->PointCount());
            else if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", oc->TriangleCount());
            else if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", mat->TriangleCount());
            else if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", disp->TriangleCount());
            else if (auto* adisp = dynamic_cast<AudioDisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", adisp->TriangleCount());
            else if (auto* arib = dynamic_cast<AudioRibbonNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", arib->TriangleCount());
            else if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", setColor->TriangleCount());
            else if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu particles", ps->AliveCount());
            else if (auto* cv = dynamic_cast<CurveNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points, %zu tris", cv->PointCount(), cv->TriangleCount());
            else if (auto* mb = dynamic_cast<MetaBallNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu balls, %zu tris", mb->BallCount(), mb->TriangleCount());
            else if (auto* jn = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%d inputs, %zu tris", jn->ConnectedCount(), jn->TriangleCount());
            else if (auto* wr = dynamic_cast<WrapNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu tris", wr->TriangleCount());
            else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "showing input %c", 'A' + sw3->ActiveSlot());
            else if (auto* cl = dynamic_cast<ClothNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu tris, %zu links", cl->TriangleCount(), cl->ConstraintCount());
            else if (auto* mrs = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "gen %d, %zu tris", mrs->Generation(), mrs->TriangleCount());
            else if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points", i2p->PointCount());
            else if (auto* dp = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
            {
               if (dp->outputType == DepthProjectionNode::kPoints)
                  snprintf(line, sizeof(line), "%zu points", dp->PointCount());
               else
                  snprintf(line, sizeof(line), "%zu triangles", dp->TriangleCount());
            }
            else
               snprintf(line, sizeof(line), "scene node");
            dl->AddText(ImVec2(origin.x + 12, origin.y + 10), IM_COL32(200, 206, 226, 255),
                        NodeTitleWithInstance(gn).c_str());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 28), IM_COL32(130, 136, 156, 255), line);
         }
         else if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         {
            // Geometry emits a mesh, not a picture: show what it is instead of
            // an empty preview box.
            auto* geo = static_cast<GeometryNode*>(gn.node.get());
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize * 0.45f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + kPreviewSize, origin.y + kPreviewSize * 0.45f);
            dl->AddRectFilled(origin, br, IM_COL32(18, 18, 24, 255), 4.0f);
            dl->AddRect(origin, br, IM_COL32(70, 74, 90, 255), 4.0f);
            const std::string& name = GeometryNode::ShapeNames()[
               std::max(0, std::min(geo->shape, (int)GeometryNode::ShapeNames().size() - 1))];
            dl->AddText(ImVec2(origin.x + 12, origin.y + 14), IM_COL32(200, 206, 226, 255), name.c_str());
            char tris[48];
            snprintf(tris, sizeof(tris), "%zu triangles", geo->TriangleCount());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 34), IM_COL32(130, 136, 156, 255), tris);
            dl->AddText(ImVec2(origin.x + 12, origin.y + 54), IM_COL32(130, 136, 156, 255), "geometry -> Render 3D");
         }
         else if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
            DrawPaintablePreview(draw);
         else if (auto* comment = dynamic_cast<CommentNode*>(gn.node.get()))
            DrawCommentPreview(comment);
         else if (auto* palette = dynamic_cast<PaletteNode*>(gn.node.get()))
            DrawPalettePreview(palette);
         else if (auto* proj = dynamic_cast<ProjectionNode*>(gn.node.get()))
            DrawProjectionPreview(proj);
         else if (isAudioBodyNode)
            DrawAudioNodeBody(gn);
         else
            DrawPreview(gn.node.get());

         if (isAudioBodyNode)
         {
            bool requestedBypass = gn.node->bypassed;
            bool bypassFromCv = false;
            if (BypassToggle(gn.node->bypassed, requestedBypass, bypassFromCv))
            {
               if (!bypassFromCv)
                  PushUndoCheckpoint();
               gn.node->bypassed = requestedBypass;
               RebuildAudioTopology();
               gPatchDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s module", gn.node->bypassed ? "Enable" : "Bypass");
         }

         // --- params (eye) ---
         // Audio nodes skip the eye toggle entirely: DrawAudioNodeBody above
         // already shows every param, Tier 1 and Tier 2 alike, unconditionally
         // - see docs/plans/audio/audio-node-ui-system.md §1.
         const bool isAudioBody = IsAudioBodyNode(gn.node.get());
         // The bypass (power) toggle used to live here; removed because it was
         // confusing and unreliable. The underlying bypassed flag, its cable-walk
         // logic, and patch serialization are untouched, so patches saved with a
         // bypassed node still load and dim correctly - there just isn't a UI
         // control to set it anymore.
         const bool isComment = dynamic_cast<CommentNode*>(gn.node.get()) != nullptr;
         // Audio nodes have nothing this row would add: Tier 1 is never
         // collapsed (so the "mod"/"pal" collapsed-tag affordance has
         // nothing to stand in for), and there is no mesh for the viewport
         // toggle - see the comment above isAudioBody.
         if (!isAudioBody)
         {
            const bool isWide = (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr ||
                                 dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
            if (isWide)
            {
               const float offset = std::max(0.0f, (kWideNodeWidth - kViewportSize) * 0.5f);
               if (offset > 0.0f)
                  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            }
            if (EyeToggle(gn.showParams))
               gn.showParams = !gn.showParams;
            // The monitor is independent from the eye. It stops only this
            // card's preview work, never the node's downstream graph output.
            if (canTogglePreview)
            {
               ImGui::SameLine();
               const bool previewEnabled = InlinePreviewEnabled(gn);
               if (ViewportToggle(previewEnabled))
                  SetInlinePreviewEnabled(gn, !previewEnabled);
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s inline preview", previewEnabled ? "Disable" : "Enable");
            }
            ImGui::SameLine();
            bool requestedBypass = gn.node->bypassed;
            bool bypassFromCv = false;
            if (BypassToggle(gn.node->bypassed, requestedBypass, bypassFromCv))
            {
               if (!bypassFromCv)
                  PushUndoCheckpoint();
               gn.node->bypassed = requestedBypass;
               RebuildAudioTopology();
               gPatchDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s module", gn.node->bypassed ? "Enable" : "Bypass");
            ImVec2 modTagMin(0.0f, 0.0f), modTagMax(0.0f, 0.0f);
            ImVec2 palTagMin(0.0f, 0.0f), palTagMax(0.0f, 0.0f);
            const bool modTag = !gn.showParams && gn.hasModulatedParams;
            const bool palTag = !gn.showParams && gn.hasPaletteColors;
            if (modTag)
            {
               // make it obvious a collapsed node still has live modulation,
               // and whether any of it is bipolar (swinging around the knob)
               // rather than absolute (overriding it) - see 00-modulation-
               // polarity.md.
               ImGui::SameLine();
               ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), gn.hasBipolarParams ? "mod\xc2\xb1" : "mod");
               modTagMin = ImGui::GetItemRectMin();
               modTagMax = ImGui::GetItemRectMax();
            }
            if (palTag)
            {
               ImGui::SameLine();
               ImGui::TextColored(ImVec4(0.5f, 0.86f, 0.74f, 1.0f), "pal");
               palTagMin = ImGui::GetItemRectMin();
               palTagMax = ImGui::GetItemRectMax();
            }
            // Only once the whole row is laid out: the stubs move the cursor.
            if (modTag)
               CollapsedBindingPins(gn.index, modTagMin, modTagMax, false);
            if (palTag)
               CollapsedBindingPins(gn.index, palTagMin, palTagMax, true);
         }

         // BeginNodeParams(gn.index) already ran above, ahead of the preview/
         // body dispatch - see the comment at that call site.
         if (!isAudioBody && gn.showParams)
         {
            if (auto* n = dynamic_cast<ImageSourceNode*>(gn.node.get()))
               DrawImageSourceParams(n);
            else if (auto* n = dynamic_cast<SlideshowNode*>(gn.node.get()))
               DrawSlideshowParams(n);
            else if (auto* n = dynamic_cast<SyphonInNode*>(gn.node.get()))
               DrawSyphonInParams(n);
            else if (auto* n = dynamic_cast<EnvironmentNode*>(gn.node.get()))
               DrawEnvironmentParams(n);
            else if (auto* n = dynamic_cast<VideoSourceNode*>(gn.node.get()))
               DrawVideoParams(n);
            else if (auto* n = dynamic_cast<TimelineNode*>(gn.node.get()))
            {
               // Turbo 0.43: resolution of the arrangement composite.
               ImGui::SetNextItemWidth(kParamWidth);
               int wh[2] = { n->width, n->height };
               if (ImGui::InputInt2("size##timelinesize", wh, ImGuiInputTextFlags_EnterReturnsTrue))
               {
                  PushUndoCheckpoint();
                  n->width = std::clamp(wh[0], 16, 8192);
                  n->height = std::clamp(wh[1], 16, 8192);
               }
               if (ImGui::Button(gArrangePanelOpen ? "Hide timeline" : "Open timeline", ImVec2(kPreviewSize, 0)))
                  gArrangePanelOpen = !gArrangePanelOpen;
            }
            else if (auto* n = dynamic_cast<VideoInNode*>(gn.node.get()))
               DrawVideoInParams(n);
            else if (auto* n = dynamic_cast<FitNode*>(gn.node.get()))
               DrawFitParams(n);
            else if (auto* n = dynamic_cast<ProjectionNode*>(gn.node.get()))
            {
               DrawProjectionParams(n);
               DrawOutputWindowPanel(gn);
            }
            else if (auto* n = dynamic_cast<LFONode*>(gn.node.get()))
               DrawLFOParams(n);
            else if (auto* n = dynamic_cast<RandomNode*>(gn.node.get()))
               DrawRandomParams(n);
            else if (auto* n = dynamic_cast<PatternNode*>(gn.node.get()))
               DrawPatternParams(n);
            else if (auto* n = dynamic_cast<MathNode*>(gn.node.get()))
               DrawMathParams(n);
            else if (auto* n = dynamic_cast<CompareNode*>(gn.node.get()))
               DrawCompareParams(n);
            else if (auto* n = dynamic_cast<RangeToRangeNode*>(gn.node.get()))
               DrawRangeToRangeParams(n);
            else if (auto* n = dynamic_cast<SmoothNode*>(gn.node.get()))
               DrawSmoothParams(n);
            else if (auto* n = dynamic_cast<CVRecorderNode*>(gn.node.get()))
               DrawCVRecorderParams(n);
            else if (auto* n = dynamic_cast<InvertNode*>(gn.node.get()))
               DrawInvertParams(n);
            else if (auto* n = dynamic_cast<ModDepthNode*>(gn.node.get()))
               DrawModDepthParams(n);
            else if (auto* n = dynamic_cast<ModCurveNode*>(gn.node.get()))
               DrawModCurveParams(n);
            else if (auto* n = dynamic_cast<CVToPitchNode*>(gn.node.get()))
               DrawCVToPitchParams(n);
            else if (auto* n = dynamic_cast<MacroKnobNode*>(gn.node.get()))
               DrawMacroKnobParams(n);
            else if (auto* n = dynamic_cast<MacroXYNode*>(gn.node.get()))
               DrawMacroXYParams(n);
            else if (auto* n = dynamic_cast<MacroSliderNode*>(gn.node.get()))
               DrawMacroSliderParams(n);
            else if (auto* n = dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()))
               DrawMacroBipolarKnobParams(n);
            else if (auto* n = dynamic_cast<MacroToggleNode*>(gn.node.get()))
               DrawMacroToggleParams(n);
            else if (auto* n = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
               DrawMacroTriggerParams(n);
            else if (auto* n = dynamic_cast<MacroNumBoxNode*>(gn.node.get()))
               DrawMacroNumBoxParams(n);
            else if (auto* n = dynamic_cast<MacroRadioSelectorNode*>(gn.node.get()))
               DrawMacroRadioSelectorParams(n);
            else if (auto* n = dynamic_cast<MacroStepGateNode*>(gn.node.get()))
               DrawMacroStepGateParams(n);
            else if (auto* n = dynamic_cast<ScenesNode*>(gn.node.get()))
               DrawScenesParams(n); // Turbo 0.50
            else if (auto* n = dynamic_cast<DriftNode*>(gn.node.get()))
               DrawDriftParams(n);
            else if (auto* n = dynamic_cast<UIButtonNode*>(gn.node.get()))
               DrawUIButtonOptions(n);
            else if (auto* n = dynamic_cast<UISliderNode*>(gn.node.get()))
               DrawUISliderOptions(n);
            else if (auto* n = dynamic_cast<UIXYNode*>(gn.node.get()))
               DrawUIXYOptions(n);
            else if (auto* n = dynamic_cast<MidiCCNode*>(gn.node.get()))
               DrawMidiCCParams(n);
            else if (auto* n = dynamic_cast<MidiTriggerNode*>(gn.node.get()))
               DrawMidiTriggerParams(n);
            else if (auto* n = dynamic_cast<NoiseNode*>(gn.node.get()))
               DrawNoiseParams(n);
            else if (auto* n = dynamic_cast<TextureNode*>(gn.node.get()))
               DrawTextureParams(n);
            else if (auto* n = dynamic_cast<RampNode*>(gn.node.get()))
               DrawRampParams(n);
            else if (auto* n = dynamic_cast<PaletteNode*>(gn.node.get()))
               DrawPaletteParams(n);
            else if (auto* n = dynamic_cast<GeometryNode*>(gn.node.get()))
               DrawGeometryParams(n);
            else if (auto* n = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               DrawModelParams(n);
            else if (auto* n = dynamic_cast<Text3DNode*>(gn.node.get()))
               DrawText3DParams(n);
            else if (auto* n = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               DrawMeshToPointsParams(n);
            else if (auto* n = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               DrawMeshResynthParams(n);
            else if (auto* n = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               DrawImageToPointsParams(n);
            else if (auto* n = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
               DrawDepthProjectionParams(n);
            else if (auto* n = dynamic_cast<GeometryTableNode*>(gn.node.get()))
               DrawGeometryTableParams(n);
            else if (auto* n = dynamic_cast<CommentNode*>(gn.node.get()))
               DrawCommentParams(n);
            else if (auto* n = dynamic_cast<PathNode*>(gn.node.get()))
               DrawPathParams(n);
            else if (auto* n = dynamic_cast<ConstantNode*>(gn.node.get()))
            {
               ModSlider("value", &n->value, 0.0f, 1.0f);
            }
            else if (auto* n = dynamic_cast<OscReceiveNode*>(gn.node.get()))
               DrawOscReceiveParams(n);
            else if (auto* n = dynamic_cast<OscToCvNode*>(gn.node.get()))
               DrawOscToCvParams(n);
            else if (auto* n = dynamic_cast<OscSendNode*>(gn.node.get()))
               DrawOscSendParams(n);
            else if (auto* n = dynamic_cast<WirelessNode*>(gn.node.get()))
               DrawWirelessParams(n);
            else if (auto* n = dynamic_cast<MaterialNode*>(gn.node.get()))
               DrawMaterialParams(n);
            else if (auto* n = dynamic_cast<MappingNode*>(gn.node.get()))
               DrawMappingParams(n);
            else if (auto* n = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               DrawParticleSystemParams(n);
            else if (auto* n = dynamic_cast<ClothNode*>(gn.node.get()))
               DrawClothParams(n);
            else if (auto* n = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               DrawJoinGeometryParams(n);
            else if (auto* n = dynamic_cast<MetaBallNode*>(gn.node.get()))
               DrawMetaBallParams(n);
            else if (auto* n = dynamic_cast<CurveNode*>(gn.node.get()))
               DrawCurveParams(n);
            else if (auto* n = dynamic_cast<OceanNode*>(gn.node.get()))
               DrawOceanParams(n);
            else if (dynamic_cast<NullNode*>(gn.node.get()) != nullptr ||
                     dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr)
               ImGui::TextDisabled("pass-through");
            else if (auto* n = dynamic_cast<GeometryOpNode*>(gn.node.get()))
               DrawGeometryOpParams(n);
            else if (auto* n = dynamic_cast<DisplacementNode*>(gn.node.get()))
               DrawDisplacementParams(n);
            else if (auto* n = dynamic_cast<AudioDisplacementNode*>(gn.node.get()))
               DrawAudioDisplacementParams(n);
            else if (auto* n = dynamic_cast<AudioRibbonNode*>(gn.node.get()))
               DrawAudioRibbonParams(n);
            else if (auto* n = dynamic_cast<AudioTextureNode*>(gn.node.get()))
               DrawAudioTextureParams(n);
            else if (auto* n = dynamic_cast<AudioColorRampNode*>(gn.node.get()))
               DrawAudioColorRampParams(n);
            else if (auto* n = dynamic_cast<SetColorNode*>(gn.node.get()))
               DrawSetColorParams(n);
            else if (auto* n = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               DrawInstanceParams(n);
            else if (auto* n = dynamic_cast<WrapNode*>(gn.node.get()))
               DrawWrapParams(n);
            else if (auto* n = dynamic_cast<DistributePointsOnFacesNode*>(gn.node.get()))
               DrawDistributePointsOnFacesParams(n);
            else if (auto* n = dynamic_cast<PointsToVerticesNode*>(gn.node.get()))
               DrawPointsToVerticesParams(n);
            else if (auto* n = dynamic_cast<DistributePointsInGridNode*>(gn.node.get()))
               DrawDistributePointsInGridParams(n);
            else if (auto* n = dynamic_cast<MergeByDistanceNode*>(gn.node.get()))
               DrawMergeByDistanceParams(n);
            else if (auto* n = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               DrawSwitcher3DParams(n);
            else if (auto* n = dynamic_cast<CameraNode*>(gn.node.get()))
               DrawCameraParams(n);
            else if (auto* n = dynamic_cast<LightNode*>(gn.node.get()))
               DrawLightParams(n);
            else if (auto* n = dynamic_cast<Render3DNode*>(gn.node.get()))
               DrawRender3DParams(n);
            else if (auto* n = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
               DrawImageAnalyzeParams(n);
            else if (auto* n = dynamic_cast<NullModulatorNode*>(gn.node.get()))
               DrawNullModulatorParams(n);
            else if (auto* n = dynamic_cast<AudioFileNode*>(gn.node.get()))
               DrawAudioFileParams(n);
            else if (auto* n = dynamic_cast<AudioAnalyzeNode*>(gn.node.get()))
               DrawAudioAnalyzeParams(n);
            else if (auto* n = dynamic_cast<ResynthNode*>(gn.node.get()))
               DrawResynthParams(n);
            else if (auto* n = dynamic_cast<CurvesNode*>(gn.node.get()))
               DrawCurvesParams(n);
            else if (auto* n = dynamic_cast<ColorRampNode*>(gn.node.get()))
               DrawColorRampParams(n);
            else if (auto* n = dynamic_cast<RemoveBgNode*>(gn.node.get()))
               DrawRemoveBgParams(n);
            else if (auto* n = dynamic_cast<DrawNode*>(gn.node.get()))
               DrawDrawParams(n);
            else if (auto* n = dynamic_cast<FeedbackNode*>(gn.node.get()))
               DrawFeedbackParams(n);
            else if (auto* n = dynamic_cast<TrailsNode*>(gn.node.get()))
               DrawTrailsParams(n);
            else if (auto* n = dynamic_cast<DatamoshNode*>(gn.node.get()))
               DrawDatamoshParams(n);
            else if (auto* n = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
               DrawReactionDiffusionParams(n);
            else if (auto* n = dynamic_cast<SwitcherNode*>(gn.node.get()))
               DrawSwitcherParams(n);
            else if (auto* n = dynamic_cast<ShapeNode*>(gn.node.get()))
               DrawShapeParams(n);
            else if (auto* n = dynamic_cast<FormulaNode*>(gn.node.get()))
               DrawFormulaParams(n);
            else if (auto* n = dynamic_cast<TextNode*>(gn.node.get()))
               DrawTextParams(n);
            else if (auto* n = dynamic_cast<LayerStackNode*>(gn.node.get()))
               DrawLayerStackParams(n);
            else if (auto* n = dynamic_cast<LayoutNode*>(gn.node.get()))
               DrawLayoutParams(gn, n);
            else if (auto* n = dynamic_cast<VmpcNode*>(gn.node.get()))
               DrawVmpcParams(gn, n);
            else if (auto* n = dynamic_cast<ClipMatrixNode*>(gn.node.get()))
               DrawClipMatrixParams(gn, n);
            else if (auto* n = dynamic_cast<ClipMatrixOutNode*>(gn.node.get()))
               DrawClipMatrixOutParams(n);
            else if (auto* n = dynamic_cast<BlendNode*>(gn.node.get()))
               DrawBlendParams(n);
            else if (auto* n = dynamic_cast<FilterNode*>(gn.node.get()))
               DrawFilterParams(n);
            else if (auto* n = dynamic_cast<OutputNode*>(gn.node.get()))
            {
               ImGui::TextDisabled("resolution: %d x %d", n->GetOutputWidth(), n->GetOutputHeight());
               ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
               ImGui::TextDisabled("Output keeps the input resolution. Insert fit / resize before it to choose width, height and aspect mode for image or video.");
               ImGui::PopTextWrapPos();
               DrawOutputWindowPanel(gn);
               ImGui::Dummy(ImVec2(0, 3));

               if (n->exportImagePath.empty())
               {
                  const std::string desktop = InfiniteDesktopDirectory();
                  if (!desktop.empty())
                     n->exportImagePath = desktop + "/infinite_output." + (n->imageFormat == 1 ? "jpg" : "png");
                  else
                     n->exportImagePath = "infinite_output." + std::string(n->imageFormat == 1 ? "jpg" : "png");
               }
               char imgBuf[512];
               snprintf(imgBuf, sizeof(imgBuf), "%s", n->exportImagePath.c_str());
               ImGui::SetNextItemWidth(kPreviewSize);
               if (ImGui::InputText("##imagePath", imgBuf, sizeof(imgBuf)))
               {
                  n->exportImagePath = imgBuf;
                  std::string low = n->exportImagePath;
                  for (char& c : low) c = (char)tolower((unsigned char)c);
                  if (low.length() >= 4 && (low.rfind(".jpg") == low.length() - 4 || low.rfind(".jpeg") == low.length() - 5))
                     n->imageFormat = 1;
                  else if (low.length() >= 4 && low.rfind(".png") == low.length() - 4)
                     n->imageFormat = 0;
                  gPatchDirty = true;
               }

               DropdownButton("##outputImageFormat", {"PNG", "JPG"}, n->imageFormat,
                  [n](int format) {
                     n->imageFormat = format;
                     const char* extension = format == 1 ? ".jpg" : ".png";
                     const size_t dot = n->exportImagePath.rfind('.');
                     if (dot != std::string::npos)
                        n->exportImagePath = n->exportImagePath.substr(0, dot) + extension;
                     else
                        n->exportImagePath += extension;
                     gPatchDirty = true;
                  }, kPreviewSize);

               if (ModTriggerButton("EXPORT IMAGE##outputExportImage", ImVec2(kPreviewSize, 0)))
                  ExportImage(n, n->exportImagePath);

               ImGui::Dummy(ImVec2(0, 4));

               ImGui::BeginDisabled(n->IsRecording());
               if (ImGui::Button(n->recordVideoPath.empty() ? "CHOOSE VIDEO FILE..." : "CHANGE VIDEO FILE...",
                                 ImVec2(kPreviewSize, 0)))
               {
                  std::string suggested = "infinite-output." + std::string(n->videoFormat == 1 ? "mov" : "mp4");
                  std::string initialDirectory = InfiniteDesktopDirectory();
                  if (!n->recordVideoPath.empty())
                  {
                     const std::filesystem::path current = std::filesystem::u8path(n->recordVideoPath);
                     if (!current.filename().empty()) suggested = current.filename().u8string();
                     if (!current.parent_path().empty()) initialDirectory = current.parent_path().u8string();
                  }
                  StartNodeFileDialog(n, [suggested, initialDirectory] { return Platform::SaveVideoDialog(suggested, initialDirectory); },
                     [](auto* m, const std::string& chosen) {
                        std::string selected = chosen;
                        std::string low = selected;
                        for (char& c : low) c = (char)tolower((unsigned char)c);
                        const bool hasMov = low.length() >= 4 && low.rfind(".mov") == low.length() - 4;
                        const bool hasMp4 = low.length() >= 4 && low.rfind(".mp4") == low.length() - 4;
                        if (!hasMov && !hasMp4)
                           selected += m->videoFormat == 1 ? ".mov" : ".mp4";
                        else
                           m->videoFormat = hasMov ? 1 : 0;
                        m->recordVideoPath = selected;
                        gPatchDirty = true;
                     });
               }
               ImGui::EndDisabled();
               if (n->recordVideoPath.empty())
                  ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "CHOOSE WHERE TO SAVE THE VIDEO");
               else
               {
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
                  ImGui::TextDisabled("%s", n->recordVideoPath.c_str());
                  ImGui::PopTextWrapPos();
               }

               DropdownButton("##outputVideoFormat", {"MP4", "MOV"}, n->videoFormat,
                  [n](int format) {
                     if (n->IsRecording())
                        return;
                     n->videoFormat = format;
                     if (!n->recordVideoPath.empty())
                     {
                        const char* extension = format == 1 ? ".mov" : ".mp4";
                        const size_t dot = n->recordVideoPath.rfind('.');
                        if (dot != std::string::npos)
                           n->recordVideoPath = n->recordVideoPath.substr(0, dot) + extension;
                        else
                           n->recordVideoPath += extension;
                     }
                     gPatchDirty = true;
                  }, kPreviewSize);

               ModSliderInt("FPS", &n->recordFps, 1, 60);

               ModCheckbox("INCLUDE AUDIO", &n->includeAudio);
               if (n->includeAudio && n->AudioInput().IsConnected())
               {
                  INode* src = n->AudioInput().GetSource();
                  std::string srcName = "connected audio";
                  if (auto* af = dynamic_cast<AudioFileNode*>(src))
                     srcName = af->FileName().empty() ? "Audio File" : af->FileName();
                  else
                  {
                     for (GraphNode& srcGn : gNodes)
                     {
                        if (srcGn.node.get() == src)
                        {
                           srcName = NodeTitleWithInstance(srcGn);
                           break;
                        }
                     }
                  }
                  ImGui::TextDisabled("FROM: %s", srcName.c_str());
               }

               const bool recording = n->IsRecording();
               bool requestedRecording = recording;
               if (recording)
                  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
               if (ModStateButton(recording ? "STOP RECORDING##outputRecordVideo"
                                            : "RECORD VIDEO##outputRecordVideo",
                                  recording, requestedRecording, ImVec2(kPreviewSize, 0)))
               {
                  if (!requestedRecording)
                     n->StopRecording();
                  else if (!n->recordVideoPath.empty())
                     n->StartRecording(n->recordVideoPath);
               }
               if (recording)
                  ImGui::PopStyleColor();
               if (n->IsRecording())
               {
                  ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "REC  %d FRAMES", n->RecordedFrames());
                  const int queued = n->PendingFrames();
                  if (queued > 0)
                     ImGui::TextDisabled("ENCODER BUFFER: %d", queued);
                  if (n->DroppedFrames() > 0)
                     ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), "ENCODER OVERFLOW: %d", n->DroppedFrames());
               }
               if (!n->RecordStatus().empty())
               {
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
                  ImGui::TextDisabled("%s", n->RecordStatus().c_str());
                  ImGui::PopTextWrapPos();
               }
            }
            else if (auto* n = dynamic_cast<SyphonOutNode*>(gn.node.get()))
               DrawSyphonOutParams(n);
         }

         ImGui::EndGroup();
         const float contentW = ImGui::GetItemRectSize().x;

         // --- output dots, bottom-right: cables start here ---
         // A comment is not in the signal graph, and an out pin on one is worse
         // than useless: link validation only asks whether a source is an image
         // node, so a comment would happily patch into any image input and feed
         // it a blank texture. No pin, no way to make that mistake.
         // A Send has no out pin either: what it carries leaves by name, to
         // every Receive on its channel.
         const auto* wireless = dynamic_cast<WirelessNode*>(gn.node.get());
         if (dynamic_cast<OutputNode*>(gn.node.get()) == nullptr && !isComment &&
             !(wireless != nullptr && wireless->IsSend()))
         {
            // Geometry Table draws its row pins (index 4 and up) inline in its
            // table grid; only the four aggregates go through this row.
            const int outputs = dynamic_cast<GeometryTableNode*>(gn.node.get()) != nullptr
                                   ? 4 : std::max(1, gn.node->OutputCount());
            float itemW = 0.0f;
            for (int o = 0; o < outputs; o++)
               itemW += kPinHit + 4.0f + ImGui::CalcTextSize(gn.node->OutputLabel(o)).x + (o ? 10.0f : 0.0f);
            float pad = std::max(0.0f, contentW - itemW);
            ImGui::Dummy(ImVec2(pad, 1.0f));
            for (int o = 0; o < outputs; o++)
            {
               ImGui::SameLine(0.0f, o == 0 ? 0.0f : 10.0f);
               DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
            }
         }

         if (dimmed)
            ImGui::PopStyleVar();
         ImGui::PopID();
         if (midiLearnLock)
            ImGui::EndDisabled();
         ed::EndNode();
         gCurrentNodeIndex = -1;
         ed::PopStyleColor(2);
         if (hasLiveIssue)
         {
            ed::PopStyleVar();
            if (ed::GetHoveredNode() == ed::NodeId(gn.NodeId()))
               gLiveIssueTip = LiveIssueTooltip(gn.index);
         }
         // Turbo: colour marker - a band across the top and a coloured frame.
         if (gn.colorTag > 0)
         {
            const ImU32 tag = NodeTagColor(gn.colorTag);
            if (ImDrawList* bg = ed::GetNodeBackgroundDrawList(gn.NodeId()))
            {
               const ImVec2 np = ed::GetNodePosition(gn.NodeId());
               const ImVec2 ns = ed::GetNodeSize(gn.NodeId());
               const float rounding = ed::GetStyle().NodeRounding;
               bg->AddRectFilled(np, ImVec2(np.x + ns.x, np.y + 7.0f), tag, rounding, ImDrawFlags_RoundCornersTop);
               bg->AddRect(np, ImVec2(np.x + ns.x, np.y + ns.y), tag, rounding, 0, 2.5f);
            }
         }
         if (gn.NodeId() == gPendingSidebarRevealNodeId)
         {
            ed::ClearSelection();
            ed::SelectNode(gn.NodeId(), false);
            gPendingSidebarRevealNodeId = -1;
            gPendingNavigateSelection = true;
         }
      }

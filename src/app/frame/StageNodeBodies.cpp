// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/FieldWell.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/components/NodeHeader.h"
#include "app/frame/FrameCtx.h"

namespace app
{

// A click on a node's error/warning badge: the one place each NodeIssue::Fix is carried out.
static void RunNodeIssueFix(GraphNode& gn, const NodeIssue& issue)
{
   switch (issue.fix)
   {
   case NodeIssue::Fix::Relink:
      if (gn.node->Relink())
         gPatchStatus = std::string(T("Relinked ")) + NodeTitle(gn);
      break;
   case NodeIssue::Fix::AudioSettings:
      gSettingsOpen = true;
      break;
   case NodeIssue::Fix::Plugins:
      gNodePanelOpen = true;
      gSearchPanelMode = 3;
      break;
   case NodeIssue::Fix::ShowNode:
      ed::SelectNode(gn.NodeId(), false);
      ed::NavigateToSelection(false, 0.2f);
      break;
   case NodeIssue::Fix::None:
      break;
   }
}
void DrawNodeBodies(FrameCtx& fc)
{
   ImGuiIO& io = ImGui::GetIO();
   auto& frameId = fc.frameId;
   auto& benchB6Stages = fc.benchB6Stages;
   auto& benchStagesSample = fc.benchStagesSample;
   auto& benchStagesCpuSample = fc.benchStagesCpuSample;
   auto& timerNodeBodies = *fc.timerNodeBodies;
   auto& timerNodeBodiesGpu = *fc.timerNodeBodiesGpu;
   auto& b6TrackVis = fc.b6TrackVis;
   auto& b6FrameVisibleCount = fc.b6FrameVisibleCount;
   auto& b6FrameBodiesDrawnCount = fc.b6FrameBodiesDrawnCount;
   auto& b6FrameOffscreenMs = fc.b6FrameOffscreenMs;

      for (GraphNode& gn : gNodes)
      {
         // Build step 15 ("Instrument Mode"): a node mounted by an
         // encapsulated FieldGraphNode is a real gNodes entry - it still
         // cooks, still counts toward audio topology, and its own params
         // still resolve via VisitParams (see ApplyModulationAndPalette's
         // per-mounted-child loop and RebuildAudioTopology, both of which
         // walk gNodes unconditionally, hidden or not) - but it gets no
         // ed::BeginNode/EndNode this frame at all, so it is neither drawn
         // nor pickable/selectable/draggable in the node editor (doc §3.2).
         // Known, deliberately scoped gap (flagged, not silently built
         // partial): a hidden child's OWN param pins do not re-register
         // for direct modulation while hidden, because doing so safely
         // would require running the ~500-line per-node param dispatch
         // chain outside its normal ed::BeginNode/ImGui-window context,
         // which is a much larger refactor than this step's exit criterion
         // requires (nothing in the FIELDGRAPHENCAPTEST assertions tests
         // it) - a pre-existing direct binding on a child stays wired but
         // stops being driven for as long as that child is hidden. The
         // FieldGraphNode's OWN declared params (the normal way to modulate
         // an encapsulated instrument, §4) are unaffected - those register
         // normally since the FieldGraphNode box itself is never hidden.
         if (gn.hiddenFromCanvas)
            continue;

         bool b6NodeIsVisible = true;
         double b6NodeDrawStartMs = 0.0;
         const double tailNodeStartMs = Bench::Tail().active ? Bench::ScopedStageTimer::NowMs() : 0.0;
         if (b6TrackVis)
         {
            const ImVec2 np = ed::GetNodePosition(gn.NodeId());
            ImVec2 ns = ed::GetNodeSize(gn.NodeId());
            if (ns.x <= 0.0f || ns.y <= 0.0f)
               ns = ImVec2(200.0f, 150.0f);
            const ImVec2 pMinScreen = ed::CanvasToScreen(np);
            const ImVec2 pMaxScreen = ed::CanvasToScreen(ImVec2(np.x + ns.x, np.y + ns.y));
            const ImGuiIO& io = ImGui::GetIO();
            b6NodeIsVisible = !(pMaxScreen.x < 0.0f || pMinScreen.x > io.DisplaySize.x ||
                                pMaxScreen.y < 0.0f || pMinScreen.y > io.DisplaySize.y);
            if (b6NodeIsVisible)
               b6FrameVisibleCount++;
            b6FrameBodiesDrawnCount++;
            if (!b6NodeIsVisible)
               b6NodeDrawStartMs = Bench::ScopedStageTimer::NowMs();
         }

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

         // Off-screen culling: a node well outside the view keeps the box and
         // pins it had last time it was laid out (so cables to it still land,
         // see KeepOffscreenNodeAlive) and skips its body. Same gate as the
         // collapsed node's register-only pass below: a parameter only exists
         // for anything that writes it (GraphNode::IsParamDriven - modulation,
         // palette, expressions, the performance panel, gesture loops) in the
         // frames it draws, so a driven node is always drawn. So is anything while a popup is open (its contents are
         // submitted from the body), and every node once per kCullRefresh
         // frames, staggered, so size or pin changes made while it is away
         // (a new param, an input count) show up within half a second.
         {
            constexpr int kCullRefresh = 30;
            constexpr float kCullMargin = 64.0f;
            const bool mustDraw = gn.IsParamDriven() || gHeadlessProbeAll ||
                                  ImGui::GetCurrentContext()->OpenPopupStack.Size > 0 ||
                                  ((frameId + gn.index) % kCullRefresh) == 0;
            if (!mustDraw && ed::KeepOffscreenNodeAlive(gn.NodeId(), kCullMargin))
            {
               if (b6TrackVis)
               {
                  b6FrameBodiesDrawnCount--;
                  if (!b6NodeIsVisible)
                     b6FrameOffscreenMs += (Bench::ScopedStageTimer::NowMs() - b6NodeDrawStartMs);
               }
               continue;
            }
            if (gHeadlessProbeAll)
               gHeadlessDrawn.insert(gn.index);
         }

         const bool paramsOpen = gn.showParams;

         // Category tint: same idea as DrawGroupNode's stored colour, but from
         // the static per-category table since categories are a fixed
         // vocabulary, not something a user repicks per node. Blended into the
         // library's own default NodeBg rather than replacing it outright, so
         // a node still reads as "the same kind of card", just tinted.
         const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
         const CategoryColors::Color& catColor = CategoryColors::ColorFor(gn.category);
         const bool isLight = CategoryColors::IsThemeLight();
         const float kTintWeight = CategoryColors::GetTintWeight();
         const float nodeAlpha = CategoryColors::GetNodeOpacity();
         const bool isComment = dynamic_cast<CommentNode*>(gn.node.get()) != nullptr;
         // D1 (geometry-domains audit, Phase 4): a node whose geometry input
         // doesn't satisfy what it asked for (see DescribeGeometryMismatch,
         // Geometry3DNodes.h) gets a red border instead of its usual category
         // tint, with the message on hover - the "red node" the plan's D1
         // decision called for, in place of a connect-time refusal.
         const auto* warnSrc = dynamic_cast<ICookWarningSource*>(gn.node.get());
         const bool hasCookWarning = warnSrc != nullptr && !warnSrc->CookWarning().empty();
         // R30: schema warnings from the last live validate pass. A cook
         // warning is the louder claim (the node is doing the wrong thing now),
         // so it keeps the red border and the amber one shows only without it.
         const auto liveIt = gLiveIssues.find(gn.index);
         const bool hasLiveIssue = !hasCookWarning && liveIt != gLiveIssues.end();
         if (isComment)
         {
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
         }
         else
         {
            ed::PushStyleColor(ed::StyleColor_NodeBg,
                               ImColor(t.panelBg.r * (1.0f - kTintWeight) + catColor.r * kTintWeight,
                                       t.panelBg.g * (1.0f - kTintWeight) + catColor.g * kTintWeight,
                                       t.panelBg.b * (1.0f - kTintWeight) + catColor.b * kTintWeight,
                                       nodeAlpha));
            if (hasCookWarning)
            {
               ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0.95f, 0.25f, 0.2f, 0.9f));
               ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f);
            }
            else if (hasLiveIssue)
            {
               ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0.95f, 0.65f, 0.15f, 0.9f));
               ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f);
            }
            else
            {
               // Hairline: the category tint fill carries identity; selection (the editor's own colour) is the loud state.
               ed::PushStyleColor(ed::StyleColor_NodeBorder,
                                  ImColor(catColor.r, catColor.g, catColor.b, isLight ? 0.55f : 0.40f));
               ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);
            }
         }

         ed::BeginNode(gn.NodeId());
         gInsideNodeCanvas = true;
         ImGui::PushID(gn.index);
         const bool dimmed = gn.node->bypassed;
         if (dimmed)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);

         const bool isAudioBody = IsAudioBodyNode(gn.node.get());
         auto* mixerNode = dynamic_cast<MixerNode*>(gn.node.get());

         // --- inputs spread along the top edge ---
         const ImVec2 topRowPos = ImGui::GetCursorPos();
         int inputs = InputCountFor(gn);
         float maxInputY = topRowPos.y;
         float headerRightX = 0.0f;   // screen x of the pin header's right edge: a long header sets the node width, so `out` aligns to it too

         if (mixerNode != nullptr && mixerNode->numChannels > 0)
         {
            const float expectedW = AudioNodeWidth(gn.node.get());
            const float cellW = expectedW / (float)mixerNode->numChannels;
            for (int slot = 0; slot < inputs; slot++)
            {
               char label[24];
               if (const char* named = gn.node->InputLabel(slot))
                  snprintf(label, sizeof(label), "%s", named);
               else
                  snprintf(label, sizeof(label), "%d", slot + 1);

               const float pinW = kPinHit + 4.0f + ImGui::CalcTextSize(label).x;
               const float pinX = topRowPos.x + ((float)slot + 0.5f) * cellW - pinW * 0.5f;
               ImGui::SetCursorPos(ImVec2(pinX, topRowPos.y));
               DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
            }
            maxInputY = std::max(maxInputY, ImGui::GetCursorPosY());
         }
         else
         {
            const float pinSpacing = (inputs > 4) ? 8.0f : 12.0f;
            // A long pin header (Render 3D: 9 pins, Material: 9) would otherwise set the node's width and leave an
            // empty strip beside the params columns. Wrap it into rows no wider than two param columns.
            const float wrapW = kParamWidth * 2.0f + 16.0f + 48.0f;
            float rowUsed = 0.0f;
            for (int slot = 0; slot < inputs; slot++)
            {
               char label[24];
               if (const char* named = gn.node->InputLabel(slot))
                  snprintf(label, sizeof(label), "%s", named);
               else if (inputs == 1)
                  snprintf(label, sizeof(label), "in"); // R6: no bare pin dot
               else
                  snprintf(label, sizeof(label), "%c", 'A' + slot);
               const float thisW = kPinHit + 4.0f + ImGui::CalcTextSize(label).x;
               if (slot > 0)
               {
                  if (rowUsed + pinSpacing + thisW > wrapW)
                     rowUsed = 0.0f;   // next pin starts a new row (no SameLine)
                  else
                  {
                     ImGui::SameLine(0.0f, pinSpacing);
                     rowUsed += pinSpacing;
                  }
               }
               DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
               headerRightX = std::max(headerRightX, ImGui::GetItemRectMax().x);
               rowUsed += thisW;
            }
            if (inputs > 0)
               maxInputY = std::max(maxInputY, ImGui::GetCursorPosY());
         }

         if (isAudioBody && !isComment)
         {
            const float expectedW = AudioNodeWidth(gn.node.get());
            bool* globalScalePtr = GetNodeGlobalScaleFlag(gn.node.get());
            if (globalScalePtr != nullptr)
            {
               ImGui::SetCursorPos(ImVec2(topRowPos.x + expectedW - 44.0f, topRowPos.y));
               if (GlobalScaleToggle(*globalScalePtr))
               {
                  PushUndoCheckpoint();
                  *globalScalePtr = !(*globalScalePtr);
               }
            }

            ImGui::SetCursorPos(ImVec2(topRowPos.x + expectedW - 22.0f, topRowPos.y));
            if (CanBypass(gn) && BypassToggle(gn.node->bypassed))
            {
               PushUndoCheckpoint();
               gn.node->bypassed = !gn.node->bypassed;
               RebuildAudioTopology();
            }
            maxInputY = std::max(maxInputY, topRowPos.y + 18.0f);
         }

         if (isComment)
            ImGui::SetCursorPos(topRowPos);
         else if (inputs > 0 || isAudioBody)
            ImGui::SetCursorPos(ImVec2(topRowPos.x, maxInputY + 4.0f));

         // Group the body so its measured width can right-align the out pin.
         // ed::GetNodeSize() is scaled by the current zoom, so feeding it back
         // into padding inflated the node a little more every frame until it
         // covered the canvas and swallowed every click.
         gParamWidthLive = std::max(kParamWidthBase, headerRightX - ImGui::GetCursorScreenPos().x);
         ImGui::BeginGroup();

         if (!isComment)
         {
            {
               UiType::Scope titleType(UiType::Size::Title, UiType::Weight::Medium);
               ImGui::TextUnformatted(NodeTitle(gn).c_str());
            }
            int instanceTotal = 0;
            const int instanceIdx = GetNodeInstanceIndex(gn, &instanceTotal);
            if (instanceTotal > 1)
            {
               ImGui::SameLine(0.0f, 4.0f);
               ImGui::TextDisabled("#%d", instanceIdx);
            }
            // One header row: title, instance number, then the category dimmed to its right.
            const ImVec4 catText = isLight
               ? ImVec4(catColor.r * 0.75f, catColor.g * 0.75f, catColor.b * 0.75f, 1.0f)
               : ImVec4(catColor.r * 0.6f + 0.4f, catColor.g * 0.6f + 0.4f, catColor.b * 0.6f + 0.4f, 0.75f);
            NodeHeader::Category(gn.category.c_str(), catText);
            if (const NodeIssue issue = gn.node->Issue())
            {
               if (NodeHeader::IssueBadge(issue))
                  RunNodeIssueFix(gn, issue);
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
         }

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
            dynamic_cast<TrackNodeBase*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<GeometryTableNode*>(gn.node.get()) != nullptr;
         // Senders out of the app (MIDI device, OSC socket) make no picture: without this they fall
         // through to DrawPreview and show an empty "no input" box. Their status line is in the params.
         const bool externalSender =
            dynamic_cast<MidiOutNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<OscSendNode*>(gn.node.get()) != nullptr;
         IGeometrySource* geoSourceForViewport = dynamic_cast<IGeometrySource*>(gn.node.get());
         // Breathing room between the title bar and a macro's main control.
         if (dynamic_cast<MacroKnobNode*>(gn.node.get()) || dynamic_cast<MacroSliderNode*>(gn.node.get()) ||
             dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()) || dynamic_cast<MacroXYNode*>(gn.node.get()) ||
             dynamic_cast<MacroToggleNode*>(gn.node.get()) || dynamic_cast<MacroTriggerNode*>(gn.node.get()) ||
             dynamic_cast<MacroNumBoxNode*>(gn.node.get()) || dynamic_cast<MacroRadioSelectorNode*>(gn.node.get()) ||
             dynamic_cast<MacroStepGateNode*>(gn.node.get()))
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
         if (multiOutModulator || externalSender)
            ; // these draw their own meters / status in the params panel
         else if (auto* macroKnob = dynamic_cast<MacroKnobNode*>(gn.node.get()))
            DrawMacroKnobBody(macroKnob);
         else if (auto* macroSlider = dynamic_cast<MacroSliderNode*>(gn.node.get()))
            DrawMacroSliderBody(macroSlider);
         else if (auto* macroBipolar = dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()))
            DrawMacroBipolarKnobBody(macroBipolar);
         else if (auto* macroXY = dynamic_cast<MacroXYNode*>(gn.node.get()))
            DrawMacroXYBody(macroXY);
         else if (auto* macroToggle = dynamic_cast<MacroToggleNode*>(gn.node.get()))
            DrawMacroToggleBody(macroToggle);
         else if (auto* macroTrigger = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
            DrawMacroTriggerBody(macroTrigger);
         else if (auto* macroNumBox = dynamic_cast<MacroNumBoxNode*>(gn.node.get()))
            DrawMacroNumBoxBody(macroNumBox);
         else if (auto* macroRadio = dynamic_cast<MacroRadioSelectorNode*>(gn.node.get()))
            DrawMacroRadioSelectorBody(macroRadio);
         else if (auto* macroStepGate = dynamic_cast<MacroStepGateNode*>(gn.node.get()))
            DrawMacroStepGateBody(macroStepGate);
         else if (!isAudioBody && dynamic_cast<DriftNode*>(gn.node.get()) != nullptr)
         {
            // Ahead of the generic IModulator branch: Drift drives one independent value per
            // destination, so the single-history meter below would have to pick one and present
            // it as the node's output. DrawDriftMeter draws the same box with every line in it.
            DrawDriftMeter(dynamic_cast<DriftNode*>(gn.node.get()), gn.index);
         }
         else if (!isAudioBody && dynamic_cast<IModulator*>(gn.node.get()) != nullptr)
         {
            // Audio/note nodes are excluded here even when they implement
            // IModulator: EnvelopeNode does (its output is a modulator
            // value, see audio-graph-semantics.md §6), and this branch used
            // to win the dispatch race against the IsAudioBodyNode branch
            // below - so Envelope silently rendered a generic modulator
            // meter and DrawEnvelopeBody never ran at all.
            DrawModulatorMeter(dynamic_cast<IModulator*>(gn.node.get()), gn.index);
         }
         else if (gn.showMiniViewport && geoSourceForViewport != nullptr &&
                  HasUsefulMiniViewport(gn.node.get()))
            DrawMiniViewport(gn, geoSourceForViewport);
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
                  dynamic_cast<Group3DNode*>(gn.node.get()) != nullptr ||
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
            AudioViz::Fill(dl, origin, br);
            AudioViz::Border(dl, origin, br);
            char line[64] = "";
            if (auto* o = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            {
               // Same frame-of-reference call-out as DrawGeometryOpParams'
               // status line, just shorter - this is the one place a
               // collapsed node's stamp-vs-group behaviour is visible at all.
               if (o->ActsOnInstanceStamp())
               {
                  if (o->op == GeometryOpNode::kTransform)
                     snprintf(line, sizeof(line), T("moving group (%zu copies)"), o->UpstreamInstanceCount());
                  else
                     snprintf(line, sizeof(line), T("%zu tris, stamped x%zu"), o->TriangleCount(), o->UpstreamInstanceCount());
               }
               else
                  snprintf(line, sizeof(line), T("%zu triangles"), o->TriangleCount());
            }
            else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu instances"), inst->InstanceCount());
            else if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), model->TriangleCount());
            else if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), t3d->TriangleCount());
            else if (auto* n3d = dynamic_cast<Null3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), n3d->TriangleCount());
            else if (auto* mapn = dynamic_cast<MappingNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), mapn->TriangleCount());
            else if (auto* m2p = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu points"), m2p->PointCount());
            else if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), oc->TriangleCount());
            else if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), mat->TriangleCount());
            else if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), disp->TriangleCount());
            else if (auto* adisp = dynamic_cast<AudioDisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), adisp->TriangleCount());
            else if (auto* arib = dynamic_cast<AudioRibbonNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), arib->TriangleCount());
            else if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu triangles"), setColor->TriangleCount());
            else if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu particles"), ps->AliveCount());
            else if (auto* cv = dynamic_cast<CurveNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu points, %zu tris"), cv->PointCount(), cv->TriangleCount());
            else if (auto* mb = dynamic_cast<MetaBallNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu balls, %zu tris"), mb->BallCount(), mb->TriangleCount());
            else if (auto* jn = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%d inputs, %zu tris"), jn->ConnectedCount(), jn->TriangleCount());
            else if (auto* wr = dynamic_cast<WrapNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu tris"), wr->TriangleCount());
            else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("showing input %c"), 'A' + sw3->ActiveSlot());
            else if (auto* grp = dynamic_cast<Group3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%d in group"), grp->GroupChildCount());
            else if (auto* cl = dynamic_cast<ClothNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu tris, %zu links"), cl->TriangleCount(), cl->ConstraintCount());
            else if (auto* mrs = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("gen %d, %zu tris"), mrs->Generation(), mrs->TriangleCount());
            else if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), T("%zu points"), i2p->PointCount());
            else if (auto* dp = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
            {
               if (dp->outputType == DepthProjectionNode::kPoints)
                  snprintf(line, sizeof(line), T("%zu points"), dp->PointCount());
               else
                  snprintf(line, sizeof(line), T("%zu triangles"), dp->TriangleCount());
            }
            else
               snprintf(line, sizeof(line), "%s", T("scene node"));
            dl->AddText(ImVec2(origin.x + 12, origin.y + 10),
                        isLight ? tok::U32(tok::pal::c_1E2434FF) : tok::U32(tok::pal::c_C8CEE2FF),
                        NodeTitleWithInstance(gn).c_str());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 28),
                        isLight ? tok::U32(tok::pal::c_5F697DFF) : tok::U32(tok::pal::c_82889CFF), line);
            // R11: scene nodes (camera / light / particles) get a small gizmo on the right instead of an empty box.
            const ImU32 gz = isLight ? tok::U32(tok::pal::c_5F697DFF) : tok::U32(tok::pal::c_82889CFF);
            const ImVec2 gc(br.x - 30.0f, origin.y + h * 0.5f);
            if (dynamic_cast<CameraNode*>(gn.node.get()) != nullptr)
            {
               dl->AddRect(ImVec2(gc.x - 14, gc.y - 8), ImVec2(gc.x + 4, gc.y + 8), gz, 2.0f, 0, 1.5f);
               dl->AddTriangle(ImVec2(gc.x + 4, gc.y), ImVec2(gc.x + 14, gc.y - 8), ImVec2(gc.x + 14, gc.y + 8), gz, 1.5f);
            }
            else if (dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
            {
               dl->AddCircle(gc, 5.0f, gz, 0, 1.5f);
               for (int r = 0; r < 8; ++r)
               {
                  const float a = (float)r * 0.7853982f;
                  dl->AddLine(ImVec2(gc.x + cosf(a) * 8.0f, gc.y + sinf(a) * 8.0f),
                              ImVec2(gc.x + cosf(a) * 13.0f, gc.y + sinf(a) * 13.0f), gz, 1.5f);
               }
            }
            else if (dynamic_cast<ParticleSystemNode*>(gn.node.get()) != nullptr)
            {
               static const float kDots[7][3] = { {-12,6,1.5f}, {-6,-5,2.0f}, {0,3,1.5f}, {4,-8,1.5f}, {9,2,2.0f}, {13,-4,1.5f}, {-2,10,1.0f} };
               for (const auto& d : kDots)
                  dl->AddCircleFilled(ImVec2(gc.x + d[0], gc.y + d[1] * 0.8f), d[2], gz);
            }
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
            AudioViz::Fill(dl, origin, br);
            AudioViz::Border(dl, origin, br);
            const std::string& name = GeometryNode::ShapeNames()[
               std::max(0, std::min(geo->shape, (int)GeometryNode::ShapeNames().size() - 1))];
            dl->AddText(ImVec2(origin.x + 12, origin.y + 14),
                        isLight ? tok::U32(tok::pal::c_1E2434FF) : tok::U32(tok::pal::c_C8CEE2FF), name.c_str());
            char tris[48];
            snprintf(tris, sizeof(tris), "%zu triangles", geo->TriangleCount());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 34),
                        isLight ? tok::U32(tok::pal::c_5F697DFF) : tok::U32(tok::pal::c_82889CFF), tris);
            dl->AddText(ImVec2(origin.x + 12, origin.y + 54),
                        isLight ? tok::U32(tok::pal::c_5F697DFF) : tok::U32(tok::pal::c_82889CFF), T("geometry -> Render 3D"));
         }
         else if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
            DrawPaintablePreview(draw);
         else if (auto* comment = dynamic_cast<CommentNode*>(gn.node.get()))
            DrawCommentPreview(comment);
         else if (auto* palette = dynamic_cast<PaletteNode*>(gn.node.get()))
            DrawPalettePreview(palette);
         else if (auto* proj = dynamic_cast<ProjectionNode*>(gn.node.get()))
            DrawProjectionPreview(proj);
         else if (auto* fgnPreview = dynamic_cast<FieldGraphNode*>(gn.node.get()))
         {
            // Build step 15 §5.1/§5.3: an encapsulated FieldGraphNode's
            // boundary output is whatever its terminal emit()-ed node(s)
            // produce - resolve each terminal (in emits order) and preview
            // the first one that actually has a texture, via the same
            // DrawPreview every other image-producing node's body already
            // uses (doc trap 8: no signature change, no second widget).
            // A texture-less terminal falls through to §5.3.1's audio-domain
            // case (RequiresAudioProcessing(), no texture) below; a geometry-
            // only terminal (neither) falls through to "nothing to show",
            // same as an empty/uncompiled program.
            INode* previewTarget = nullptr;
            for (int idx : fgnPreview->TerminalIndices())
            {
               GraphNode* term = FindNodeByIndex(idx);
               if (term != nullptr && term->node && term->node->GetOutputTexture() != 0 &&
                   term->node->GetOutputWidth() > 0)
               {
                  previewTarget = term->node.get();
                  break;
               }
            }
            // §5.4: this is also the node's single (derived) boundary output
            // pin's target - refresh it every draw frame so an outer cable
            // plugged into that pin (main.cpp's ordinary ImageCable/cable-
            // record machinery, resolved generically by pin, not specially
            // for FieldGraphNode) reads whichever terminal currently backs
            // it, re-resolved fresh rather than held stale across a
            // Regenerate()'s own SpawnNode/RemoveNodeByIndex churn.
            fgnPreview->SetBoundaryOutputTarget(previewTarget);
            if (previewTarget != nullptr)
               DrawPreview(previewTarget);
            else
            {
               INode* audioTerminal = nullptr;
               for (int idx : fgnPreview->TerminalIndices())
               {
                  GraphNode* term = FindNodeByIndex(idx);
                  if (term != nullptr && term->node && term->node->RequiresAudioProcessing() &&
                      term->node->GetOutputTexture() == 0)
                  {
                     audioTerminal = term->node.get();
                     break;
                  }
               }
               if (audioTerminal != nullptr)
                  DrawFieldGraphWaveform(fgnPreview, audioTerminal);
            }
         }
         else if (auto* fsnPreview = dynamic_cast<FieldSampleNode*>(gn.node.get()))
            DrawFieldSampleScope(fsnPreview, 60.0f, kPreviewSize);
         else if (auto* fspPreview = dynamic_cast<FieldSynthNode*>(gn.node.get()))
            DrawFieldSynthScope(fspPreview, 60.0f, kPreviewSize);
         else if (auto* fnnPreview = dynamic_cast<FieldNotesNode*>(gn.node.get()))
            DrawFieldNotesRoll(fnnPreview, 60.0f, kPreviewSize);
         else if (isAudioBody)
            DrawAudioNodeBody(gn);
         else
            DrawPreview(gn.node.get());

         // --- params (eye) ---
         // Audio nodes skip the eye toggle entirely: DrawAudioNodeBody above
         // already shows every param, Tier 1 and Tier 2 alike, unconditionally
         // - see docs/plans/audio/audio-node-ui-system.md §1.
         // Audio nodes have nothing this row would add: Tier 1 is never
         // collapsed (so the "mod"/"pal" collapsed-tag affordance has
         // nothing to stand in for), and there is no mesh for the viewport
         // toggle - see the comment above isAudioBody.
         // Name-only macros have nothing behind the eye, so their toggle row would hold bypass alone. Bypass
         // then shares the output pin's row (left edge vs right edge), which makes the body symmetrical and
         // drops a row. A modulated macro keeps the toggle row: its "mod" tag lives there.
         const bool isMacroNode =
            dynamic_cast<MacroKnobNode*>(gn.node.get()) || dynamic_cast<MacroSliderNode*>(gn.node.get()) ||
            dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()) || dynamic_cast<MacroTriggerNode*>(gn.node.get()) ||
            dynamic_cast<MacroToggleNode*>(gn.node.get()) || dynamic_cast<MacroNumBoxNode*>(gn.node.get());
         if (isMacroNode && !isComment)
            gn.showParams = false;
         const auto drawBypassToggle = [&]()
         {
            if (!BypassToggle(gn.node->bypassed))
               return;
            PushUndoCheckpoint();
            gn.node->bypassed = !gn.node->bypassed;
            if (dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr ||
                dynamic_cast<INoteSource*>(gn.node.get()) != nullptr)
               RebuildAudioTopology();
         };
         const bool bypassOnOutputRow = isMacroNode && !isComment && CanBypass(gn) && !gn.hasModulatedParams &&
                                        !gn.hasPaletteColors;
         if (bypassOnOutputRow)
            ImGui::Dummy(ImVec2(0.0f, 4.0f));   // the gap the toggle row gave; also ends the body on an item, not a SetCursorPos
         if (!isAudioBody && !isComment && !bypassOnOutputRow)
         {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            const bool isWide = (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr ||
                                 dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
            if (isWide)
            {
               const float offset = WideNodeCentreOffset(gn.node.get(), kViewportSize);
               if (offset > 0.0f)
                  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            }
            // Name-only macros have nothing behind the eye, so the
            // toggle is dropped and bypass is the row's first control.
            if (!isMacroNode)
            {
               if (EyeToggle(gn.showParams))
                  gn.showParams = !gn.showParams;
            }
            // R8: a node that cannot bypass (2+ inputs) has no hole where the power icon would be; the icons pack left.
            // The SameLine lives here, not after the eye: with nothing to follow, it would glue the next control
            // (Output's path field) onto the eye's row.
            if (CanBypass(gn))
            {
               if (!isMacroNode)
                  ImGui::SameLine();
               drawBypassToggle();
            }
            // Mini viewport toggle, only for nodes that actually have a mesh to
            // show - excludes CameraNode/LightNode, which appear in the stat-box
            // branch above but implement no geometry interface, and
            // ParticleSystemNode, whose mini viewport never frames usefully
            // (see HasUsefulMiniViewport) - a toggle that does nothing is worse
            // than no toggle.
            if (geoSourceForViewport != nullptr && HasUsefulMiniViewport(gn.node.get()))
            {
               ImGui::SameLine();
               if (ViewportToggle(gn.showMiniViewport))
                  gn.showMiniViewport = !gn.showMiniViewport;
            }
            ImVec2 modTagMin(0.0f, 0.0f), modTagMax(0.0f, 0.0f);
            ImVec2 palTagMin(0.0f, 0.0f), palTagMax(0.0f, 0.0f);
            const bool modTag = !paramsOpen && gn.hasModulatedParams;
            const bool palTag = !paramsOpen && gn.hasPaletteColors;
            if (modTag)
            {
               // make it obvious a collapsed node still has live modulation,
               // and whether any of it is bipolar (swinging around the knob)
               // rather than absolute (overriding it) - see 00-modulation-
               // polarity.md.
               ImGui::SameLine();
               const char* tagText = gn.hasBipolarParams ? "mod\xc2\xb1" : "mod";
               const ImVec2 txtSz = ImGui::CalcTextSize(tagText);
               const ImVec2 p = ImGui::GetCursorScreenPos();
               const ImVec2 tagSz(txtSz.x + 8.0f, txtSz.y + 2.0f);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               dl->AddRectFilled(p, ImVec2(p.x + tagSz.x, p.y + tagSz.y),
                                 isLight ? tok::U32(tok::pal::c_FFEBC8C8) : tok::U32(tok::pal::c_463214B4), 3.0f);
               dl->AddText(ImVec2(p.x + 4.0f, p.y + 1.0f),
                           isLight ? tok::U32(tok::pal::c_B46414FF) : tok::U32(tok::pal::c_FFBE5AFF), tagText);
               ImGui::Dummy(tagSz);
               modTagMin = p;
               modTagMax = ImVec2(p.x + tagSz.x, p.y + tagSz.y);
            }
            if (palTag)
            {
               ImGui::SameLine();
               const char* tagText = "pal";
               const ImVec2 txtSz = ImGui::CalcTextSize(tagText);
               const ImVec2 p = ImGui::GetCursorScreenPos();
               const ImVec2 tagSz(txtSz.x + 8.0f, txtSz.y + 2.0f);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               dl->AddRectFilled(p, ImVec2(p.x + tagSz.x, p.y + tagSz.y),
                                 isLight ? tok::U32(tok::pal::c_C8F5EBC8) : tok::U32(tok::pal::c_143C32B4), 3.0f);
               dl->AddText(ImVec2(p.x + 4.0f, p.y + 1.0f),
                           isLight ? tok::U32(tok::pal::c_148C6EFF) : tok::U32(tok::pal::c_80DCBEFF), tagText);
               ImGui::Dummy(tagSz);
               palTagMin = p;
               palTagMax = ImVec2(p.x + tagSz.x, p.y + tagSz.y);
            }
            // Only once the whole row is laid out: the stubs move the cursor.
            if (modTag)
               CollapsedBindingPins(gn.index, modTagMin, modTagMax, false);
            if (palTag)
               CollapsedBindingPins(gn.index, palTagMin, palTagMax, true);
         }

         // BeginNodeParams(gn.index) already ran above, ahead of the preview/
         // body dispatch - see the comment at that call site.
         // A parameter only exists, as far as modulation is concerned, for the
         // frames it draws in: ModSlider/ModKnob hand the apply pass a raw
         // float* through RegisterParam, and the pass writes through exactly
         // the pointers registered this frame. So closing a node's eye used to
         // silently freeze every modulator and expression patched into it -
         // the cable stayed, the binding stayed, the matrix still listed it,
         // and nothing moved.
         //
         // Fix: a collapsed node that actually has something driving it still
         // runs this whole dispatch, with the UI suppressed three ways -
         // gParamRegisterOnly makes every Mod* widget register its ParamRef
         // and return before it draws or declares a pin (the pins a collapsed
         // node needs come from CollapsedBindingPins instead), SkipItems makes
         // every plain ImGui widget a no-op so the node's size and layout are
         // untouched, and an empty clip rect swallows any raw draw-list work
         // the params body does around them. Gated on there being a binding at
         // all so the common collapsed node costs exactly what it did before.
         const bool registerOnlyParams = !isAudioBody && !isComment && !paramsOpen &&
                                         (gn.IsParamDriven() || gHeadlessProbeAll);
         ImGuiWindow* paramsWindow = ImGui::GetCurrentWindow();
         const bool savedSkipItems = paramsWindow->SkipItems;
         if (registerOnlyParams)
         {
            gParamRegisterOnly = true;
            paramsWindow->SkipItems = true;
            ImGui::GetWindowDrawList()->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), false);
         }
         if (!isAudioBody && !isComment && (paramsOpen || registerOnlyParams))
         {
            bool matched = true;
            if (auto* n = dynamic_cast<ImageSourceNode*>(gn.node.get()))
               DrawImageSourceParams(n);
            else if (auto* n = dynamic_cast<SlideshowNode*>(gn.node.get()))
               DrawSlideshowParams(n);
            else if (auto* n = dynamic_cast<SyphonInNode*>(gn.node.get()))
               DrawSyphonInParams(n);
            else if (auto* n = dynamic_cast<NdiInNode*>(gn.node.get()))
               DrawNdiInParams(n);
            else if (auto* n = dynamic_cast<EnvironmentNode*>(gn.node.get()))
               DrawEnvironmentParams(n);
            else if (auto* n = dynamic_cast<VideoSourceNode*>(gn.node.get()))
               DrawVideoParams(n);
            else if (auto* n = dynamic_cast<VideoInNode*>(gn.node.get()))
               DrawVideoInParams(n);
            else if (auto* n = dynamic_cast<FitNode*>(gn.node.get()))
               DrawFitParams(n);
            else if (auto* n = dynamic_cast<ProjectionNode*>(gn.node.get()))
               DrawProjectionParams(n);
            else if (auto* n = dynamic_cast<LFONode*>(gn.node.get()))
               DrawLFOParams(n);
            else if (auto* n = dynamic_cast<RandomNode*>(gn.node.get()))
               DrawRandomParams(n);
            else if (auto* n = dynamic_cast<DriftNode*>(gn.node.get()))
               DrawDriftParams(gn, n);
            else if (auto* n = dynamic_cast<MovesNode*>(gn.node.get()))
               DrawMovesParams(n);
            else if (auto* n = dynamic_cast<PredictiveModulatorNode*>(gn.node.get()))
               DrawPredictiveModulatorParams(n);
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
            else if (auto* n = dynamic_cast<InvertNode*>(gn.node.get()))
               DrawInvertParams(n);
            else if (auto* n = dynamic_cast<ModDepthNode*>(gn.node.get()))
               DrawModDepthParams(n);
            else if (auto* n = dynamic_cast<ModCurveNode*>(gn.node.get()))
               DrawModCurveParams(n);
            else if (auto* n = dynamic_cast<CVToPitchNode*>(gn.node.get()))
               DrawCVToPitchParams(n);
            else if (auto* n = dynamic_cast<NoteToCVNode*>(gn.node.get()))
               DrawNoteToCVParams(n);
            else if (auto* n = dynamic_cast<VelocityToCVNode*>(gn.node.get()))
               DrawVelocityToCVParams(n);
            else if (auto* n = dynamic_cast<MidiOutNode*>(gn.node.get()))
               DrawMidiOutParams(n);
            else if (auto* n = dynamic_cast<CVRecorderNode*>(gn.node.get()))
               DrawCVRecorderParams(n);
            else if (auto* n = dynamic_cast<MacroKnobNode*>(gn.node.get()))
               DrawMacroKnobParams(n);
            else if (auto* n = dynamic_cast<MacroSliderNode*>(gn.node.get()))
               DrawMacroSliderParams(n);
            else if (auto* n = dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()))
               DrawMacroBipolarKnobParams(n);
            else if (auto* n = dynamic_cast<MacroXYNode*>(gn.node.get()))
               DrawMacroXYParams(n);
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
            else if (auto* n = dynamic_cast<CommentNode*>(gn.node.get()))
               DrawCommentParams(n);
            else if (auto* n = dynamic_cast<PathNode*>(gn.node.get()))
               DrawPathParams(n);
            else if (auto* n = dynamic_cast<GeometryTableNode*>(gn.node.get()))
               DrawGeometryTableParams(n);
            else if (auto* n = dynamic_cast<ConstantNode*>(gn.node.get()))
            {
               ModSlider("value", &n->value, 0.0f, 1.0f);
            }
            else if (auto* n = dynamic_cast<OscReceiveNode*>(gn.node.get()))
               DrawOscReceiveParams(n);
            else if (auto* n = dynamic_cast<OscSendNode*>(gn.node.get()))
               DrawOscSendParams(n);
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
               ImGui::TextDisabled("%s", T("pass-through"));
            else
               matched = false;
            // MSVC caps nested blocks at 128; one long else-if chain overflows it, so the chain is split in two.
            if (!matched)
            {
               if (auto* n = dynamic_cast<GeometryOpNode*>(gn.node.get()))
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
               else if (auto* n = dynamic_cast<CurveOpsNode*>(gn.node.get()))
                  DrawCurveOpsParams(n);
               else if (auto* n = dynamic_cast<DelaunayMeshNode*>(gn.node.get()))
                  DrawDelaunayParams(n);
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
               else if (auto* n = dynamic_cast<TrackNodeBase*>(gn.node.get()))
                  DrawTrackParams(n);
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
               else if (auto* n = dynamic_cast<PredictiveColoringNode*>(gn.node.get()))
                  DrawPredictiveColoringParams(n);
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
               else if (auto* n = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
                  DrawReactionDiffusionParams(n);
               else if (auto* n = dynamic_cast<SwitcherNode*>(gn.node.get()))
                  DrawSwitcherParams(n);
               else if (auto* n = dynamic_cast<ShapeNode*>(gn.node.get()))
                  DrawShapeParams(n);
               else if (auto* n = dynamic_cast<FormulaNode*>(gn.node.get()))
                  DrawFormulaParams(n);
               else if (auto* n = dynamic_cast<FieldElementNode*>(gn.node.get()))
                  DrawFieldElementParams(n);
               else if (auto* n = dynamic_cast<FieldPrimitiveNode*>(gn.node.get()))
                  DrawFieldPrimitiveParams(n);
               else if (auto* n = dynamic_cast<FieldPixelNode*>(gn.node.get()))
                  DrawFieldPixelParams(n);
               else if (auto* n = dynamic_cast<SketchNode*>(gn.node.get()))
                  DrawSketchParams(n);
               else if (auto* n = dynamic_cast<Sketch3DNode*>(gn.node.get()))
                  DrawSketch3DParams(n);
               else if (auto* n = dynamic_cast<FieldSampleNode*>(gn.node.get()))
                  DrawFieldSampleParams(n);
               else if (auto* n = dynamic_cast<FieldSynthNode*>(gn.node.get()))
                  DrawFieldSynthParams(n);
               else if (auto* n = dynamic_cast<FieldNotesNode*>(gn.node.get()))
                  DrawFieldNotesParams(n);
               else if (auto* n = dynamic_cast<FieldGraphNode*>(gn.node.get()))
                  DrawFieldGraphParams(n);
               else if (auto* n = dynamic_cast<TextNode*>(gn.node.get()))
                  DrawTextParams(n);
               else if (auto* n = dynamic_cast<LayerStackNode*>(gn.node.get()))
                  DrawLayerStackParams(n);
               else if (auto* n = dynamic_cast<BlendNode*>(gn.node.get()))
                  DrawBlendParams(n);
               else if (auto* n = dynamic_cast<FilterNode*>(gn.node.get()))
                  DrawFilterParams(n);
               else if (auto* n = dynamic_cast<OutputNode*>(gn.node.get()))
               {
                  if (n->exportImagePath.empty())
                  {
                     n->exportImagePath = AppPaths::DesktopDir() + "/infinite_output." + (n->imageFormat == 1 ? "jpg" : "png");
                  }
                  if (n->recordVideoPath.empty())
                  {
                     n->recordVideoPath = AppPaths::DesktopDir() + "/infinite_output." + (n->videoFormat == 1 ? "mov" : "mp4");
                  }

                  char imgBuf[512];
                  snprintf(imgBuf, sizeof(imgBuf), "%s", n->exportImagePath.c_str());
                  ImGui::SetNextItemWidth(kPreviewSize);
                  if (FieldWell::InputText("##imagePath", imgBuf, sizeof(imgBuf)))
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

                  const float halfBtnW = (kPreviewSize - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
                  const bool pngActive = (n->imageFormat == 0);
                  if (pngActive)
                     PushSelectedButtonColors();
                  if (ActionButton::Draw(L(".png##imgPng"), ImVec2(halfBtnW, 0)))
                  {
                     n->imageFormat = 0;
                     size_t dot = n->exportImagePath.rfind('.');
                     if (dot != std::string::npos)
                        n->exportImagePath = n->exportImagePath.substr(0, dot) + ".png";
                     else
                        n->exportImagePath += ".png";
                     gPatchDirty = true;
                  }
                  if (pngActive)
                     PopSelectedButtonColors();
                  ImGui::SameLine();

                  const bool jpgActive = (n->imageFormat == 1);
                  if (jpgActive)
                     PushSelectedButtonColors();
                  if (ActionButton::Draw(L(".jpg##imgJpg"), ImVec2(halfBtnW, 0)))
                  {
                     n->imageFormat = 1;
                     size_t dot = n->exportImagePath.rfind('.');
                     if (dot != std::string::npos)
                        n->exportImagePath = n->exportImagePath.substr(0, dot) + ".jpg";
                     else
                        n->exportImagePath += ".jpg";
                     gPatchDirty = true;
                  }
                  if (jpgActive)
                     PopSelectedButtonColors();

                  if (ActionButton::Draw(L("Export Image"), ImVec2(kPreviewSize, 0)))
                     ExportImage(n, n->exportImagePath);

                  ImGui::Dummy(ImVec2(0, 4));

                  char vidBuf[512];
                  snprintf(vidBuf, sizeof(vidBuf), "%s", n->recordVideoPath.c_str());
                  ImGui::SetNextItemWidth(kPreviewSize);
                  if (FieldWell::InputText("##videoPath", vidBuf, sizeof(vidBuf)))
                  {
                     n->recordVideoPath = vidBuf;
                     std::string low = n->recordVideoPath;
                     for (char& c : low) c = (char)tolower((unsigned char)c);
                     if (low.length() >= 4 && low.rfind(".mov") == low.length() - 4)
                        n->videoFormat = 1;
                     else if (low.length() >= 4 && low.rfind(".mp4") == low.length() - 4)
                        n->videoFormat = 0;
                     gPatchDirty = true;
                  }

                  ImGui::BeginDisabled(n->IsRecording());
                  const bool mp4Active = (n->videoFormat == 0);
                  if (mp4Active)
                     PushSelectedButtonColors();
                  if (ActionButton::Draw(L(".mp4##vidMp4"), ImVec2(halfBtnW, 0)))
                  {
                     n->videoFormat = 0;
                     size_t dot = n->recordVideoPath.rfind('.');
                     if (dot != std::string::npos)
                        n->recordVideoPath = n->recordVideoPath.substr(0, dot) + ".mp4";
                     else
                        n->recordVideoPath += ".mp4";
                     gPatchDirty = true;
                  }
                  if (mp4Active)
                     PopSelectedButtonColors();
                  ImGui::SameLine();

                  const bool movActive = (n->videoFormat == 1);
                  if (movActive)
                     PushSelectedButtonColors();
                  if (ActionButton::Draw(L(".mov##vidMov"), ImVec2(halfBtnW, 0)))
                  {
                     n->videoFormat = 1;
                     size_t dot = n->recordVideoPath.rfind('.');
                     if (dot != std::string::npos)
                        n->recordVideoPath = n->recordVideoPath.substr(0, dot) + ".mov";
                     else
                        n->recordVideoPath += ".mov";
                     gPatchDirty = true;
                  }
                  if (movActive)
                     PopSelectedButtonColors();
                  ImGui::EndDisabled();

                  // Both of these are read once, at StartRecording, and latched
                  // for the take - the recorder fixes its frame rate and its
                  // audio track up front and cannot change either mid-stream.
                  // Leaving them live meant dragging fps during a take silently
                  // did nothing; now it also has to not desync the pacing that
                  // reads it, so say plainly that the take owns them.
                  ImGui::BeginDisabled(n->IsRecording());
                  ImGui::SetNextItemWidth(kParamWidth);
                  ImGui::SliderInt("##recfps", &n->recordFps, 1, 60, "%d fps");

                  ModCheckbox(L("include audio"), &n->includeAudio);
                  ImGui::EndDisabled();
                  if (n->IsRecording() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                     ImGui::SetTooltip("%s", T("locked for the current take"));
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
                     ImGui::TextDisabled(T("from: %s"), srcName.c_str());
                  }

                  if (n->StopRequested() || n->IsFinalizing())
                  {
                     // StopRequested(): the actual StopRecordingAsync() call
                     // runs at the top of next frame, once this "finalizing"
                     // state has had a chance to reach the screen - see the
                     // pump next to glfwPollEvents(). IsFinalizing(): the
                     // encoder join + movie finalize is running on a background
                     // thread and can take a while on a long/backlogged take,
                     // but doesn't block this UI - a new take can't be started
                     // here (the button stays disabled) since StartRecording()
                     // would otherwise briefly block on WaitForFinalize().
                     ImGui::BeginDisabled();
                     ActionButton::Draw(L("Finalizing..."), ImVec2(kPreviewSize, 0));
                     ImGui::EndDisabled();
                     // PendingFrames() reads the live handle, which has already
                     // been handed off to the background thread once
                     // IsFinalizing() is true - nothing left here to report.
                     const int pending = n->StopRequested() ? n->PendingFrames() : 0;
                     if (pending > 0)
                        ImGui::TextDisabled(T("finishing up, %d frames left"), pending);
                  }
                  else if (n->IsRecording())
                  {
                     if (ActionButton::Draw(L("Stop recording"), ImVec2(kPreviewSize, 0), ActionButton::Kind::Record))
                        n->RequestStopRecording();
                     ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), T("REC  %d frames"), n->RecordedFrames());
                     const int pending = n->PendingFrames();
                     const int dropped = n->DroppedFrames();
                     if (pending > 0)
                     {
                        ImGui::SameLine();
                        ImGui::TextDisabled(T("(%d pending)"), pending);
                     }
                     if (dropped > 0)
                     {
                        // Same orange as the VST3 blocklist warning - "this is a
                        // problem, not an error": the encoder is losing frames,
                        // but recording is continuing.
                        ImGui::TextColored(tok::V4(tok::palf::v_900_550_250_1000), T("%d frames dropped - encoder can't keep up"), dropped);
                     }
                  }
                  else
                  {
                     if (ActionButton::Draw(L("Record video"), ImVec2(kPreviewSize, 0)))
                        n->StartRecording(n->recordVideoPath);
                  }
                  if (!n->RecordStatus().empty())
                  {
                     ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
                     ImGui::TextDisabled("%s", n->RecordStatus().c_str());
                     ImGui::PopTextWrapPos();
                  }

                  ImGui::Dummy(ImVec2(0, 8));
                  NodeSeparator();
                  ImGui::TextDisabled("%s", T("Offline Render"));

                  // A take drives the whole patch's Transport/AudioEngine, not
                  // just this node - only one can ever be in flight regardless
                  // of which OutputNode started it, and it can't overlap this
                  // node's own live recording either (both would fight over the
                  // same recordVideoPath/EnsureFbo-sized mOut).
                  const bool thisNodeRendering = gOfflineRender.node == n;
                  const bool otherSessionActive = gOfflineRender.active && !thisNodeRendering;

                  // Unit/caption lives inside the field's right edge (R1), not outside the node.
                  auto fieldUnit = [](const char* unit)
                  {
                     const ImVec2 mn = ImGui::GetItemRectMin();
                     const ImVec2 mx = ImGui::GetItemRectMax();
                     const ImVec2 ts = ImGui::CalcTextSize(unit);
                     ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                     dim.w *= 0.55f;
                     ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ts.x - 8.0f, mn.y + (mx.y - mn.y - ts.y) * 0.5f),
                                                         ImGui::GetColorU32(dim), unit);
                  };
                  ImGui::BeginDisabled(n->IsRecording() || n->IsFinalizing() || gOfflineRender.active);
                  ImGui::SetNextItemWidth(kParamWidth);
                  FieldWell::InputInt("##renderfps", &n->offlineFps);
                  fieldUnit(T("render fps"));
                  n->offlineFps = std::clamp(n->offlineFps, 1, 240);

                  // Duration is typed, not dragged: a render queue's length is a
                  // number the user knows ("give me 45 seconds"), and hitting an
                  // exact value on a 1..600 slider is fiddly. The presets are
                  // the common takes; the field takes anything up to an hour.
                  ImGui::SetNextItemWidth(kParamWidth);
                  FieldWell::InputInt("##renderdur", &n->offlineDurationSeconds);
                  fieldUnit(T("duration (s)"));
                  n->offlineDurationSeconds = std::clamp(n->offlineDurationSeconds, 1, 3600);
                  for (int preset : { 15, 30, 45, 60 })
                  {
                     ImGui::PushID(preset);
                     const bool selected = n->offlineDurationSeconds == preset;
                     if (ActionButton::Draw((std::to_string(preset) + "s").c_str(), ImVec2(kParamWidth * 0.22f, 0),
                                            selected ? ActionButton::Kind::Selected : ActionButton::Kind::Plain))
                        n->offlineDurationSeconds = preset;
                     ImGui::PopID();
                     if (preset != 60)
                        ImGui::SameLine();
                  }

                  ImGui::SetNextItemWidth(kParamWidth);
                  FieldWell::InputInt("##renderpre", &n->offlinePrerollFrames);
                  fieldUnit(T("preroll frames"));
                  n->offlinePrerollFrames = std::clamp(n->offlinePrerollFrames, 0, 600);
                  ImGui::EndDisabled();

                  ImGui::TextDisabled(T("%d frames @ %dfps"), n->offlineDurationSeconds * n->offlineFps, n->offlineFps);

                  if (thisNodeRendering)
                  {
                     // The floating DrawOfflineRenderProgressWindow carries the
                     // live progress/Cancel button; this is just a disabled
                     // placeholder so the button doesn't visually disappear.
                     ImGui::BeginDisabled();
                     ActionButton::Draw(L("Rendering..."), ImVec2(kPreviewSize, 0));
                     ImGui::EndDisabled();
                  }
                  else
                  {
                     ImGui::BeginDisabled(n->IsRecording() || n->IsFinalizing() || otherSessionActive);
                     if (ActionButton::Draw(L("Render"), ImVec2(kPreviewSize, 0)))
                        StartOfflineRenderSession(n);
                     ImGui::EndDisabled();
                  }
               }
               else if (auto* n = dynamic_cast<SyphonOutNode*>(gn.node.get()))
                  DrawSyphonOutParams(n);
               else if (auto* n = dynamic_cast<NdiOutNode*>(gn.node.get()))
                  DrawNdiOutParams(n);
            }
         }
         if (registerOnlyParams)
         {
            ImGui::GetWindowDrawList()->PopClipRect();
            paramsWindow->SkipItems = savedSkipItems;
            gParamRegisterOnly = false;
         }

         ImGui::EndGroup();
         gParamWidthLive = kParamWidthBase;
         const float contentW = std::max(ImGui::GetItemRectSize().x, headerRightX - ImGui::GetItemRectMin().x);

         // --- output dots, bottom-right: cables start here ---
         // A comment is not in the signal graph, and an out pin on one is worse
         // than useless: link validation only asks whether a source is an image
         // node, so a comment would happily patch into any image input and feed
         // it a blank texture. No pin, no way to make that mistake.
         // FieldGraphNode is a meta-node - it spawns/wires other real nodes at
         // edit time and never produces a picture of its own (GetOutputTexture
         // always returns 0), so an out pin on it is exactly as misleading as
         // one on a comment would be.
         // Audio Out and Spatial Mixer are terminals: nothing can patch from them.
         if (dynamic_cast<OutputNode*>(gn.node.get()) == nullptr && !isComment &&
             dynamic_cast<FieldGraphNode*>(gn.node.get()) == nullptr &&
             dynamic_cast<AudioOutputNode*>(gn.node.get()) == nullptr &&
             dynamic_cast<SpatialMixerNode*>(gn.node.get()) == nullptr)
         {
            // GeometryTableNode draws its row pins (index 4 and up) itself,
            // inline in the table grid in its params panel - only the four
            // aggregates (cx/cy/cz/spread) go through the generic row here.
            // Drawing the same pin id through ed::BeginPin() twice in one
            // frame is not something imgui-node-editor supports.
            // Section gap: the out pins are their own group, never flush against the last param row.
            ImGui::Dummy(ImVec2(0.0f, tok::space_2));
            auto* geoTable = dynamic_cast<GeometryTableNode*>(gn.node.get());
            auto* drumSeq = dynamic_cast<DrumSequencerNode*>(gn.node.get());
            const int outputs = geoTable != nullptr ? 4 : (drumSeq != nullptr ? 1 : std::max(1, gn.node->OutputCount()));
            std::vector<float> pinW(outputs);
            float itemW = 0.0f;
            for (int o = 0; o < outputs; o++)
            {
               pinW[o] = kPinHit + 4.0f + ImGui::CalcTextSize(gn.node->OutputLabel(o)).x;
               itemW += pinW[o] + (o ? 10.0f : 0.0f);
            }
            if (itemW <= contentW)
            {
               float pad = std::max(0.0f, contentW - itemW);
               if (bypassOnOutputRow)
               {
                  // Bypass at the row's left edge, level with the output pin; the pad shrinks by its footprint.
                  drawBypassToggle();
                  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y));   // beside the toggle; the pad Dummy follows
                  pad = std::max(0.0f, pad - 22.0f);
               }
               ImGui::Dummy(ImVec2(pad, 1.0f));
               for (int o = 0; o < outputs; o++)
               {
                  ImGui::SameLine(0.0f, o == 0 ? 0.0f : 10.0f);
                  DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
               }
            }
            else
            {
               // Too many pins for one row: an equal-width cell grid (label, pin at the cell's right edge) so the
               // columns line up, the readout lives in the node body (DrawOutputMeters).
               float cellW0 = 0.0f;
               for (int o = 0; o < outputs; o++)
                  cellW0 = std::max(cellW0, pinW[o]);
               const float gap = 10.0f;
               const int cols = std::clamp((int)((contentW + gap) / (cellW0 + gap)), 1, outputs);
               const float cellW = (contentW - gap * (float)(cols - 1)) / (float)cols;
               const float rowH = kPinHit + 8.0f;
               const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();
               for (int o = 0; o < outputs; o++)
               {
                  const float x = gridOrigin.x + (float)(o % cols) * (cellW + gap);
                  const float y = gridOrigin.y + (float)(o / cols) * rowH;
                  ImGui::SetCursorScreenPos(ImVec2(x + cellW - pinW[o], y));
                  DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
               }
               ImGui::SetCursorScreenPos(ImVec2(gridOrigin.x, gridOrigin.y + (float)((outputs + cols - 1) / cols) * rowH));
               ImGui::Dummy(ImVec2(contentW, 1.0f));
            }
         }

         if (dimmed)
            ImGui::PopStyleVar();
         ImGui::PopID();
         gInsideNodeCanvas = false;
         ed::EndNode();
         CacheNodeWidth(gn.node.get(), ed::GetNodeSize(gn.NodeId()).x);
         if (hasCookWarning && ed::GetHoveredNode() == ed::NodeId(gn.NodeId()))
            gNodeHoverTip = warnSrc->CookWarning(); // a real fault, so shown even with Help tooltips off
         else if (hasLiveIssue && CategoryColors::GetTooltips() && ed::GetHoveredNode() == ed::NodeId(gn.NodeId()))
         {
            std::string tip;
            for (const Headless::Issue& w : liveIt->second)
            {
               if (!tip.empty())
                  tip += "\n";
               tip += w.message;
               if (!w.hint.empty())
                  tip += "\n  -> " + w.hint;
            }
            gNodeHoverTip = tip;
         }
         ed::PopStyleColor(2);
         if (isComment)
            ed::PopStyleVar(3);
         else
            ed::PopStyleVar();  // border width: warning widths, or the 1 px hairline

         if (b6TrackVis && !b6NodeIsVisible)
         {
            b6FrameOffscreenMs += (Bench::ScopedStageTimer::NowMs() - b6NodeDrawStartMs);
         }
         if (Bench::Tail().active)
            Bench::Tail().AddNode(gn.typeName, Bench::ScopedStageTimer::NowMs() - tailNodeStartMs);
      }

      // B6 only: end node_bodies here so links get their own stage. Other
      // fixtures keep the old span (bodies through the arrange overlay) so
      // their recorded baselines stay comparable; a GL timer query can't
      // nest, so the links GPU timer must not start inside that span either.
      if (benchB6Stages)
      {
         timerNodeBodies.Stop();
         timerNodeBodiesGpu.Stop();
      }
      if (b6TrackVis)
      {
         sBenchB6VisibleNodesSum += (double)b6FrameVisibleCount;
         sBenchB6BodiesDrawnSum += (double)b6FrameBodiesDrawnCount;
         sBenchB6OffscreenBodyMsSum += b6FrameOffscreenMs;
         sBenchB6SampledFrames++;
      }

      fc.timerLinks.emplace((benchB6Stages && benchStagesCpuSample) ? &sStageLinks : nullptr, Bench::FrameTail::kLinks);
   auto& timerLinks = *fc.timerLinks;
      fc.timerLinksGpu.emplace((benchB6Stages && benchStagesSample) ? &sGpuTimerRing : nullptr, "links", frameId);
   auto& timerLinksGpu = *fc.timerLinksGpu;

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
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(gn, slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;

            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(cable->GetSourceOutput()), gn.InputPinId(slot) });
                  break;
               }
            }
         }
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(cable->GetOutputSlot()), gn.InputPinId(slot) });
                  break;
               }
            }
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(cable->GetOutputSlot()), gn.InputPinId(slot) });
                  break;
               }
            }
         }
      }}
}

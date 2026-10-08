// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawLinks(FrameCtx& fc)
{
   ImGuiIO& io = ImGui::GetIO();
   auto& searchBuf = fc.searchBuf;
   auto& searchJustOpened = fc.searchJustOpened;
   auto& searchPopupCentered = fc.searchPopupCentered;
   auto& timerLinks = *fc.timerLinks;
   auto& timerLinksGpu = *fc.timerLinksGpu;
   auto& typing = fc.typing;
   auto& cmdOrCtrl = fc.cmdOrCtrl;

      for (GraphNode& gn : gNodes)
      {
         auto linkFromNode = [&](const void* wanted, int slot)
         {
            if (wanted == nullptr)
               return;
            for (GraphNode& src : gNodes)
            {
               // Compared against each interface separately: with multiple
               // inheritance an IGeometrySource* and an INode* into the same
               // object are different addresses, so one comparison is not enough.
               const void* asGeo = dynamic_cast<IGeometrySource*>(src.node.get());
               if (asGeo == wanted || (const void*)src.node.get() == wanted)
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(), gn.InputPinId(slot) });
                  return;
               }
            }
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
            if (setColor->paletteInput != nullptr)
               for (GraphNode& src : gNodes)
                  if (dynamic_cast<IPaletteSource*>(src.node.get()) == setColor->paletteInput)
                  {
                     gLinks.push_back({ kLinkIdBase + gn.InputPinId(2),
                                         src.OutputPinId(), gn.InputPinId(2) });
                     break;
                  }
         }

         // Audio Analyze's own link used to be drawn by hand here from its
         // fileSource pointer; it is an AudioCable now, so the generic
         // AudioInputSlot pass above already draws it.
         int modCount = gn.node->ModulatorInputCount();
         if (modCount == 0)
            continue;
         for (int slot = 0; slot < modCount; slot++)
         {
            IModulator* wanted = *gn.node->ModulatorInputSlot(slot);
            if (wanted == nullptr)
               continue;
            bool found = false;
            for (GraphNode& src : gNodes)
            {
               for (int o = 0; o < std::max(1, src.node->OutputCount()) && !found; o++)
               {
                  if (ModulatorForOutput(src.node.get(), o) == wanted)
                  {
                     gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                        src.OutputPinId(o), gn.InputPinId(slot) });
                     found = true;
                  }
               }
               if (found)
                  break;
            }
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
         {
            // No pin declared this frame: emitting the link would kill it. If the
            // body told us where the hidden control lives, show the connection
            // as a dotted cable to that spot instead.
            const int srcPin = source->OutputPinId(link.second.outputIndex);
            auto from = gPinAnchors.find(srcPin);
            auto alias = gPinAlias.find(paramPin);
            auto to = gPinAnchors.find(alias != gPinAlias.end() ? alias->second : paramPin);
            if (from != gPinAnchors.end() && to != gPinAnchors.end() && (gCableVisibilityMask & 0x4))
            {
               CategoryColors::Color c = CategoryColors::CableColorFor(CategoryColors::CableType::Modulation);
               if (source->category == "Prediction" || dynamic_cast<IPredictor*>(source->node.get()) != nullptr)
                  c = CategoryColors::ColorFor("Prediction");
               const ImU32 col = ImColor(c.r, c.g, c.b, 0.8f);
               const ImVec2 a = from->second, b = to->second;
               const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
               const ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
               ImDrawList* ddl = ImGui::GetWindowDrawList();
               // Dashes by arc length, so dash size does not stretch with the cable.
               constexpr int kSamples = 240;
               constexpr float kDash = 6.0f, kGap = 5.0f;
               ImVec2 prev = a;
               float acc = 0.0f;   // distance along the cable since the last phase flip
               bool on = true;
               for (int i = 1; i <= kSamples; i++)
               {
                  const float t = (float)i / kSamples, u = 1.0f - t;
                  const ImVec2 pt(u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x,
                                  u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y);
                  float seg = std::hypot(pt.x - prev.x, pt.y - prev.y);
                  ImVec2 from = prev;
                  while (seg > 0.0f)
                  {
                     const float room = (on ? kDash : kGap) - acc;
                     const float step = std::min(seg, room);
                     const float f = step / seg;
                     const ImVec2 to(from.x + (pt.x - from.x) * f, from.y + (pt.y - from.y) * f);
                     if (on)
                        ddl->AddLine(from, to, col, 2.0f);
                     from = to;
                     seg -= step;
                     acc += step;
                     if (acc >= (on ? kDash : kGap) - 1e-4f)
                     {
                        acc = 0.0f;
                        on = !on;
                     }
                  }
                  prev = pt;
               }
            }
            continue;
         }
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
      for (const LinkInfo& link : gLinks)
      {
         const bool isMod = GraphNode::IsParamPin(link.dstPin);
         if (isMod)
         {
            if (gCableVisibilityMask & 0x4)
            {
               CategoryColors::Color c = CategoryColors::CableColorFor(CategoryColors::CableType::Modulation);
               const int srcNodeIdx = GraphNode::NodeIndexFromPin(link.srcPin);
               GraphNode* srcNode = FindNodeByIndex(srcNodeIdx);
               if (srcNode != nullptr && (srcNode->category == "Prediction" || dynamic_cast<IPredictor*>(srcNode->node.get()) != nullptr))
               {
                  c = CategoryColors::ColorFor("Prediction");
               }
               ed::Link(link.id, link.srcPin, link.dstPin, ImColor(c.r, c.g, c.b, 1.0f), 2.0f);
            }
            continue;
         }

         const bool isColor = GraphNode::IsColorPin(link.dstPin);
         if (isColor)
         {
            if (gCableVisibilityMask & 0x1)
            {
               const CategoryColors::Color& c = CategoryColors::CableColorFor(CategoryColors::CableType::Palette);
               ed::Link(link.id, link.srcPin, link.dstPin, ImColor(c.r, c.g, c.b, 1.0f), 2.0f);
            }
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
                  if (gCableVisibilityMask & 0x2)
                  {
                     const CategoryColors::Color& c = CategoryColors::CableColorFor(CategoryColors::CableType::Audio);
                     ed::Link(link.id, link.srcPin, link.dstPin, ImColor(c.r, c.g, c.b, 1.0f), 2.0f);
                  }
                  tinted = true;
               }
               else if (dst->node->NoteInputSlot(slot) != nullptr)
               {
                  if (gCableVisibilityMask & 0x2)
                  {
                     const CategoryColors::Color& c = CategoryColors::CableColorFor(CategoryColors::CableType::Note);
                     ed::Link(link.id, link.srcPin, link.dstPin, ImColor(c.r, c.g, c.b, 1.0f), 2.0f);
                  }
                  tinted = true;
               }
            }
         }
         if (!tinted)
         {
            if (gCableVisibilityMask & 0x1)
            {
               const CategoryColors::Color& c = CategoryColors::CableColorFor(CategoryColors::CableType::Stream);
               ed::Link(link.id, link.srcPin, link.dstPin, ImColor(c.r, c.g, c.b, 1.0f), 2.0f);
            }
         }
      }

      timerLinks.Stop();
      timerLinksGpu.Stop();

      // ---- handle new connections ----
      const CategoryColors::Color& defStreamCol = CategoryColors::CableColorFor(CategoryColors::CableType::Stream);
      if (ed::BeginCreate(ImColor(defStreamCol.r, defStreamCol.g, defStreamCol.b, 1.0f), 2.0f))
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
                auto* srcGeometry = srcNode ? dynamic_cast<IGeometrySource*>(srcNode->node.get()) : nullptr;
                if (srcGeometry != nullptr && !srcGeometry->IsGeometryOutputIndex(srcOutputIndex))
                   srcGeometry = nullptr;
                auto* srcCamera = srcNode ? dynamic_cast<CameraNode*>(srcNode->node.get()) : nullptr;
                auto* srcLight = srcNode ? dynamic_cast<LightNode*>(srcNode->node.get()) : nullptr;
                const bool srcIsEnvironment = srcNode != nullptr &&
                                              dynamic_cast<EnvironmentNode*>(srcNode->node.get()) != nullptr;
                auto* srcAudioSource = srcNode ? dynamic_cast<IAudioSource*>(srcNode->node.get()) : nullptr;
                const bool srcIsAudioNode = srcAudioSource != nullptr &&
                                            srcAudioSource->IsAudioOutputIndex(srcOutputIndex);
                const bool srcIsNoteSource = srcNode != nullptr &&
                                             dynamic_cast<INoteSource*>(srcNode->node.get()) != nullptr;
                const bool srcIsPredictor = srcNode != nullptr &&
                                            dynamic_cast<IPredictor*>(srcNode->node.get()) != nullptr;

               bool valid = false;
               const char* rejectReason = nullptr;
               if (GraphNode::IsOutputPin(a) && srcNode != nullptr && dstNode != nullptr && differentNodes)
               {
                  // modulators patch into parameters and into Math's inputs;
                  // image nodes patch into image inputs
                  if (GraphNode::IsParamPin(b))
                  {
                     valid = srcIsModulator;
                     if (valid && IsKernelDrivenParam(dstNode->index, GraphNode::ParamIndexFromPin(b)))
                     {
                        valid = false;
                        rejectReason = T("Cannot modulate a parameter driven by a Field Graph kernel");
                     }
                     if (valid)
                     {
                        if (const char* why = PredictorBindRefusal(srcNode->node.get(), dstNode->index, GraphNode::ParamIndexFromPin(b)))
                        {
                           valid = false;
                           rejectReason = why;
                        }
                     }
                  }
                  else if (GraphNode::IsColorPin(b))
                     valid = srcPalette != nullptr;
                  else if (GraphNode::IsInputPin(b))
                  {
                     // The Predictive LFO / Macro refusal lives inside
                     // IsInputSlotCompatible now (it used to sit out here, where
                     // only this one path saw it); the wording for it is picked
                     // up by the reason chain below.
                     valid = IsInputSlotCompatible(dstNode, GraphNode::InputSlotFromPin(b),
                                                    srcIsModulator, srcPalette, srcGeometry, srcCamera,
                                                    srcLight, srcIsEnvironment,
                                                    srcIsAudioNode, srcIsNoteSource, srcIsPredictor);
                     if (valid && srcIsAudioNode &&
                         WouldCreateAudioCycle(srcNode->node.get(), dstNode->node.get()))
                     {
                        valid = false;
                        rejectReason = T("Cannot connect: this would create an audio feedback loop");
                     }
                     if (valid && srcIsNoteSource &&
                         WouldCreateNoteCycle(srcNode->node.get(), dstNode->node.get()))
                     {
                        valid = false;
                        rejectReason = T("Cannot connect: this would create a note feedback loop");
                     }
                  }
               }

               // Why a refused drag was refused, surfaced as a tooltip below
               // (audio-node-ui-system §6a, extended across all 3D/image/signal pins).
               if (!valid && rejectReason == nullptr && dstNode != nullptr)
               {
                  // These two used to fall through the whole chain below and
                  // leave the cable red with no tooltip at all, which reads as
                  // a bug rather than a rule. ConnectNodes() has always had
                  // wording for the self-connection case; the UI now says it too.
                  if (!differentNodes)
                  {
                     rejectReason = T("A node can't be connected to itself");
                  }
                  else if (!GraphNode::IsOutputPin(a) || GraphNode::IsOutputPin(b))
                  {
                     rejectReason = T("Drag from an output pin on the right of a node to an input pin on the left of another");
                  }
                  else if (GraphNode::IsColorPin(b))
                  {
                     rejectReason = T("This color slot only accepts a Palette node");
                  }
                  else if (GraphNode::IsParamPin(b))
                  {
                     if (srcIsAudioNode || srcIsNoteSource)
                        rejectReason = T("Audio/note signals can't drive a parameter pin - only a modulator can");
                     else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
                        rejectReason = T("3D objects cannot drive a parameter pin - only a modulator can");
                     else if (!srcIsModulator)
                        rejectReason = T("Only modulator nodes (LFO, Envelope, Formula, etc.) can drive a parameter pin");
                  }
                  else if (srcIsPredictor && GraphNode::IsInputPin(b))
                  {
                     // Source-driven refusal: it applies to every slot on every
                     // node, so it is read before any of the destination-shaped
                     // messages in the branch below.
                     rejectReason = T("Predictive LFO / Macro can only drive a parameter, knob or slider - not another node");
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
                           ? T("A modulator can't drive an audio signal pin - only another audio source can")
                           : T("This pin only accepts an audio source");
                     else if (dstWantsNote && !srcIsNoteSource)
                        rejectReason = T("This pin only accepts a note source");
                     else if ((srcIsAudioNode || srcIsNoteSource) && !dstWantsAudio && !dstWantsNote)
                        rejectReason = T("Audio/note signals only connect to a matching audio/note pin");
                     else if (dstRenderNode != nullptr)
                     {
                        if (slot < Render3DNode::kSlots)
                        {
                           if (srcCamera != nullptr)
                              rejectReason = T("Camera connects to the Camera slot (slot 5), not geometry slots");
                           else if (srcLight != nullptr)
                              rejectReason = T("Light connects to the Light slots (slots 6-8), not geometry slots");
                           else if (srcIsEnvironment)
                              rejectReason = T("HDRI connects to the Environment slot (slot 9), not geometry slots");
                           else
                              rejectReason = T("Render 3D geometry slots only accept 3D geometry sources");
                        }
                        else if (slot == Render3DNode::kSlots)
                           rejectReason = T("This slot only accepts a Camera 3D node");
                        else if (slot == Render3DNode::kEnvSlot)
                           rejectReason = T("Environment slot only accepts an HDRI Environment node");
                        else
                           rejectReason = T("This slot only accepts a Light 3D node");
                     }
                     else if (dstMaterialNode != nullptr && slot >= 1 && slot <= kMapCount)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = T("Material map slots accept 2D images or textures, not 3D geometry");
                        else if (srcIsModulator)
                           rejectReason = T("Material map slots accept 2D images or textures, not modulators");
                        else
                           rejectReason = T("Material map slots accept 2D images or textures");
                     }
                     else if (dstDispNode != nullptr && slot == 1)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = T("Displacement height slot accepts a 2D image or texture map, not 3D geometry");
                        else
                           rejectReason = T("Displacement height slot accepts a 2D image or texture map");
                     }
                     else if (dstSetColorNode != nullptr && slot == 2)
                     {
                        rejectReason = T("Set Vertex Color palette slot only accepts a Palette node");
                     }
                     else if (dstSetColorNode != nullptr && slot == 1)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = T("Set Vertex Color texture slot accepts a 2D image or texture map, not 3D geometry");
                        else
                           rejectReason = T("Set Vertex Color texture slot accepts a 2D image or texture map");
                     }
                     else if (dstMappingNode != nullptr)
                     {
                        rejectReason = T("Mapping transforms 3D surface coordinates. Wire 3D geometry into Mapping, then into Material or Render 3D.");
                     }
                     else if (dstNode->node->GeometryInputSlot(slot) != nullptr)
                     {
                        if (srcCamera != nullptr || srcLight != nullptr)
                           rejectReason = T("Camera and Light nodes connect to Render 3D, not geometry operators");
                        else
                           rejectReason = T("This pin requires a 3D geometry source, not a 2D image");
                     }
                     else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
                     {
                        rejectReason = T("3D geometry cannot be connected directly to a 2D image node. Connect geometry into a Render 3D node first.");
                     }
                     else if (dynamic_cast<AudioAnalyzeNode*>(dstNode->node.get()) != nullptr)
                     {
                        rejectReason = T("Audio Analyze accepts any audio source - Audio In, Audio File, an effect, a Mixer");
                     }
                     else if (dstNode->node->ModulatorInputSlot(slot) != nullptr && dynamic_cast<ImageAnalyzeNode*>(dstNode->node.get()) == nullptr)
                     {
                        rejectReason = T("This pin only accepts a modulator source");
                     }
                     else if (srcIsModulator)
                     {
                        rejectReason = T("Image inputs accept 2D image sources, not modulators");
                     }
                     else
                     {
                        rejectReason = T("Incompatible connection");
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
            if (GraphNode::IsOutputPin(gLinkDragSourcePin))
            {
               GraphNode* dragSrc = FindNodeByIndex(GraphNode::NodeIndexFromPin(gLinkDragSourcePin));
               if (dragSrc != nullptr)
                  gLinkDragSuggestions = RecommendedNodeTypesForOutput(
                     dragSrc, GraphNode::OutputIndexFromPin(gLinkDragSourcePin));
            }
            gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
            searchBuf[0] = '\0';
            searchJustOpened = true;
            // Opening at the raw mouse/drop position (ImGui's default for a
            // plain OpenPopup) has no on-screen clamping, so a drop near the
            // canvas edge pins the popup flush against it and clips whatever
            // doesn't fit - especially bad here since the Suggested list can
            // be tall (many recommended node types) before any filtering.
            // Shift+N already avoids this by centering; do the same here
            // rather than trusting the drop point to have room around it.
            searchPopupCentered = true;
            ImGui::OpenPopup("search");
         }
      }
      ed::EndCreate();

      // ---- keyboard: delete + copy/paste ----
      typing = io.WantTextInput || gNavOwnsKeys;
      cmdOrCtrl = io.KeyCtrl || io.KeySuper;

      // Shift+Cmd+Z is the Mac convention for redo; Ctrl+Y also works for
      // anyone used to the Windows/Linux binding.
      if (!typing && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
         Undo();
      if (!typing && ((cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
                      (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))))
         Redo();

      // Shift+A selects every node on the canvas. !gArrangeFocused: the
      // timeline has its own Shift+A (select all clips) while it owns the
      // keyboard, same as its sibling shortcuts below.
      const bool doSelectAll = gRequestSelectAll ||
         (!typing && !gArrangeFocused && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false));
      gRequestSelectAll = false;
      if (doSelectAll)
      {
         ed::ClearSelection();
         for (GraphNode& gn : gNodes)
            ed::SelectNode(gn.NodeId(), true);
      }

      // ---- keyboard model (R571) ----
      // Click a node to make it the active node, then:
      //   Tab / Shift+Tab   walk that node's params in a loop (never other nodes)
      //   Left/Right (param focused) nudge the value; Alt = x10; digits type a value
      //   Arrows            move the selected nodes one grid step
      //   Shift+Arrows      select the neighbouring node in that direction
      //   Shift+Enter / Enter  zoom into the node / back out
      //   H help, B bypass, Cmd/Ctrl+U ungroup, F frame everything, W A S D pan
      // All gated like the other plain-key canvas shortcuts: never while a text
      // field, popup, the timeline or a hovered audio keyboard owns the keys.
      {
         const bool kbFree = !typing && !cmdOrCtrl && !gArrangeFocused && !gPerfMatrixFocused &&
                             gCommentEdit.target == nullptr && gTypedParam.empty() &&
                             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

         // The active node: exactly one node selected and nothing else.
         GraphNode* active = nullptr;
         {
            const int selObj = ed::GetSelectedObjectCount();
            if (selObj == 1)
            {
               ed::NodeId one;
               if (ed::GetSelectedNodes(&one, 1) == 1)
                  for (GraphNode& gn : gNodes)
                     if (gn.NodeId() == (int)one.Get())
                        active = &gn;
            }
         }
         if (gKbFocusNode >= 0 && (active == nullptr || active->index != gKbFocusNode))
         {
            gKbFocusNode = -1;
            gKbFocusParam = -1;
         }

         auto dirKey = [&](float& dx, float& dy) {
            dx = dy = 0.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dx = -1.0f;
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx = 1.0f;
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) dy = -1.0f;
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) dy = 1.0f;
            return dx != 0.0f || dy != 0.0f;
         };
         // One undo entry per burst of presses, like one entry per mouse drag.
         auto burstUndo = [&]() {
            static double lastTime = -10.0;
            const double now = ImGui::GetTime();
            if (now - lastTime > 0.6)
               PushUndoCheckpoint();
            lastTime = now;
         };

         gKbOwnTab = kbFree && active != nullptr;
         if (kbFree && !io.KeyAlt)
         {
            // ---- Tab: params of the active node, looped ----
            if (active != nullptr && ImGui::IsKeyPressed(ImGuiKey_Tab, ImGuiInputFlags_Repeat, kKbTabOwner))
            {
               std::vector<int> mine;
               for (const KbParamEntry& e : gKbParams)
                  if (e.node == active->index)
                     mine.push_back(e.param);
               if (!mine.empty())
               {
                  const int n = static_cast<int>(mine.size());
                  int pos = -1;
                  for (int i = 0; i < n; ++i)
                     if (gKbFocusNode == active->index && mine[i] == gKbFocusParam) pos = i;
                  const int next = (pos < 0) ? (io.KeyShift ? n - 1 : 0) : ((pos + (io.KeyShift ? n - 1 : 1)) % n);
                  gKbFocusNode = active->index;
                  gKbFocusParam = mine[next];
               }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && gKbFocusNode >= 0)
            {
               gKbFocusNode = -1;
               gKbFocusParam = -1;
            }

            float dx = 0.0f, dy = 0.0f;
            if (active != nullptr && dirKey(dx, dy))
            {
               if (io.KeyShift)
               {
                  // ---- Shift+arrow: neighbour node in that direction ----
                  const ImVec2 ap = ed::GetNodePosition(active->NodeId());
                  const ImVec2 as = ed::GetNodeSize(active->NodeId());
                  const ImVec2 from(ap.x + as.x * 0.5f, ap.y + as.y * 0.5f);
                  GraphNode* best = nullptr;
                  float bestScore = 1e30f;
                  for (GraphNode& gn : gNodes)
                  {
                     if (&gn == active) continue;
                     const ImVec2 p = ed::GetNodePosition(gn.NodeId());
                     const ImVec2 sz = ed::GetNodeSize(gn.NodeId());
                     const float cx = p.x + sz.x * 0.5f - from.x, cy = p.y + sz.y * 0.5f - from.y;
                     const float along = cx * dx + cy * dy;
                     const float across = std::fabs(cx * dy - cy * dx);
                     if (along <= 0.0f || across > along * 2.0f) continue; // outside a ~63 degree cone
                     const float score = along + across * 2.0f;
                     if (score < bestScore) { bestScore = score; best = &gn; }
                  }
                  if (best != nullptr)
                  {
                     ed::ClearSelection();
                     ed::SelectNode(best->NodeId(), false);
                     const ImVec2 bp = ed::CanvasToScreen(ed::GetNodePosition(best->NodeId()));
                     const bool onScreen = bp.x >= gGraphScreenTL.x && bp.y >= gGraphScreenTL.y &&
                                           bp.x <= gGraphScreenTL.x + gGraphScreenSize.x &&
                                           bp.y <= gGraphScreenTL.y + gGraphScreenSize.y;
                     if (!onScreen)
                        ed::NavigateToSelection(false, 0.2f);
                  }
               }
               else if (gKbFocusNode >= 0)
               {
                  // ---- arrows on a focused param: nudge its value ----
                  burstUndo();
                  gKbNudge = ((dx > 0.0f || dy < 0.0f) ? 1 : -1) * (io.KeyShift ? 1 : 1);
               }
               else
               {
                  // ---- arrows: move the node one grid step ----
                  burstUndo();
                  const float kStep = gGridSnap > 0.0f ? gGridSnap : 40.0f;
                  const int selCount = ed::GetSelectedObjectCount();
                  std::vector<ed::NodeId> selNodes(selCount);
                  const int nSel = ed::GetSelectedNodes(selNodes.data(), selCount);
                  auto snapStep = [&](float v, float dir) {
                     return dir > 0.0f ? (std::floor(v / kStep + 0.001f) + 1.0f) * kStep
                                       : (std::ceil(v / kStep - 0.001f) - 1.0f) * kStep;
                  };
                  std::set<int> selIdx;
                  for (int i = 0; i < nSel; ++i)
                     selIdx.insert((int)selNodes[i].Get());
                  for (int i = 0; i < nSel; ++i)
                  {
                     const ImVec2 p = ed::GetNodePosition(selNodes[i]);
                     const ImVec2 np(dx != 0.0f ? snapStep(p.x, dx) : p.x, dy != 0.0f ? snapStep(p.y, dy) : p.y);
                     ed::SetNodePosition(selNodes[i], np);
                     // A group box refits to its members every frame, so moving
                     // the box alone would snap straight back: carry the members
                     // (unless they are selected themselves and move on their own).
                     for (GraphNode& gn : gNodes)
                     {
                        GroupNode* grp = dynamic_cast<GroupNode*>(gn.node.get());
                        if (grp == nullptr || gn.NodeId() != (int)selNodes[i].Get())
                           continue;
                        const auto it = gGroupMembers.find(grp);
                        if (it == gGroupMembers.end())
                           break;
                        for (int memberIdx : it->second)
                        {
                           GraphNode* member = FindNodeByIndex(memberIdx);
                           if (member == nullptr || selIdx.count((int)member->NodeId()) != 0)
                              continue;
                           const ImVec2 mp = ed::GetNodePosition(member->NodeId());
                           ed::SetNodePosition(member->NodeId(), ImVec2(mp.x + (np.x - p.x), mp.y + (np.y - p.y)));
                        }
                        break;
                     }
                  }
               }
            }
            // Arrow keys on a focused param with Alt held: coarse nudge.
         }
         else if (kbFree && io.KeyAlt && gKbFocusNode >= 0 && !io.KeyShift)
         {
            float dx = 0.0f, dy = 0.0f;
            if (dirKey(dx, dy))
            {
               burstUndo();
               gKbNudge = ((dx > 0.0f || dy < 0.0f) ? 10 : -10);
            }
         }

         // ---- Shift+Enter zooms into the node, Enter zooms back out ----
         if (kbFree && !io.KeyAlt)
         {
            const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
            if (enter && gKbZoomed)
            {
               gKbViewRestore = true;
               gKbZoomed = false;
            }
            else if (enter && io.KeyShift && active != nullptr)
            {
               gKbSavedScroll = ed::GetViewScroll();
               gKbSavedZoom = ed::GetViewZoom();
               ed::NavigateToSelection(true, 0.0f);
               gKbZoomed = true;
            }
         }

         // ---- H again closes the help popup (kbFree is off while any popup is open) ----
         if (gNodeHelpShown && !typing && !cmdOrCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_H, false))
            gCloseNodeHelp = true;
         gNodeHelpShown = false;

         // ---- single-key node commands ----
         const bool plain = kbFree && !io.KeyAlt && !io.KeyShift && !gComputerKeyboardHot;
         if (plain && active != nullptr && ImGui::IsKeyPressed(ImGuiKey_H, false))
         {
            gHelpPopupNodeIndex = active->index;
            gOpenNodeHelpPopup = true;
         }
         if (plain && ImGui::IsKeyPressed(ImGuiKey_F, false))
            gRequestFitView = true;
         // Cmd/Ctrl+U ungroups (kbFree excludes Cmd/Ctrl, so it is gated on its own).
         if (!typing && !gArrangeFocused && cmdOrCtrl && !io.KeyShift && !io.KeyAlt && !gComputerKeyboardHot &&
             gKbFocusNode < 0 && ImGui::IsKeyPressed(ImGuiKey_U, false))
            gRequestUngroup = true;

         // ---- W A S D pan the canvas while held ----
         if (plain)
         {
            float px = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
            float py = (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f);
            if (px != 0.0f || py != 0.0f)
            {
               const float perFrame = 900.0f * io.DeltaTime / std::max(0.05f, ed::GetCurrentZoom());
               gKbPan = ImVec2(gKbPan.x + px * perFrame, gKbPan.y + py * perFrame);
               gKbZoomed = false; // the saved view no longer matches what is on screen
            }
         }

         // A canvas click puts the mouse back in charge of param focus.
         if (gKbFocusNode >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
         {
            gKbFocusNode = -1;
            gKbFocusParam = -1;
         }
         gKbParams.clear();
         gComputerKeyboardHot = false;
      }}
}

// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawPopupsA(FrameCtx& fc)
{
   ImGuiIO& io = ImGui::GetIO();
   auto& window = fc.window;
   auto& searchBuf = fc.searchBuf;
   auto& searchJustOpened = fc.searchJustOpened;

      if (ImGui::BeginPopup("##nodecontext"))
      {
         GraphNode* gn = FindNodeByIndex(gContextMenuNodeIndex);
         if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
         {
            if (ImGui::MenuItem(L("Rename")))
            {
               PushUndoCheckpoint();
               g->renaming = true;
               g->renameJustStarted = true;
            }
            if (ImGui::MenuItem(L("Ungroup")))
            {
               ed::ClearSelection();
               ed::SelectNode(gn->NodeId());
               gRequestUngroup = true;
            }
            if (ImGui::MenuItem(L("Duplicate"), MODKEY "+D"))
            {
               if (!ed::IsNodeSelected(gn->NodeId()))
               {
                  ed::ClearSelection();
                  ed::SelectNode(gn->NodeId());
               }
               gRequestDuplicate = true;
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.85f, 0.20f, 0.20f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.85f, 0.20f, 0.20f, 0.40f));
            if (ImGui::MenuItem(L("Delete Group"), "Backspace"))
            {
               if (!ed::IsNodeSelected(gn->NodeId()))
               {
                  ed::ClearSelection();
                  ed::SelectNode(gn->NodeId());
               }
               gRequestDelete = true;
            }
            ImGui::PopStyleColor(3);
         }
         else if (auto* c = dynamic_cast<CommentNode*>(gn->node.get()))
         {
            if (ImGui::MenuItem(L("Edit Note")))
            {
               PushUndoCheckpoint();
               gCommentEdit.target = c;
               gCommentEdit.justOpened = true;
            }
            if (ImGui::BeginMenu(L("Font Size")))
            {
               const char* sizeLabels[] = { "Small", "Normal", "Large", "Extra Large" };
               for (int sIdx = 0; sIdx < 4; sIdx++)
               {
                  const bool selected = (c->fontSize == sIdx);
                  if (ImGui::MenuItem(sizeLabels[sIdx], nullptr, selected))
                  {
                     PushUndoCheckpoint();
                     c->fontSize = sIdx;
                     gPatchDirty = true;
                  }
               }
               ImGui::EndMenu();
            }
            if (ImGui::MenuItem(L("Change Colour...")))
            {
               PushUndoCheckpoint();
               gColor.target = c->color;
               gColor.owner = c;
               gColor.label = "colour";
               gColor.justOpened = true;
            }
            if (ImGui::MenuItem(L("Duplicate"), MODKEY "+D"))
            {
               if (!ed::IsNodeSelected(gn->NodeId()))
               {
                  ed::ClearSelection();
                  ed::SelectNode(gn->NodeId());
               }
               gRequestDuplicate = true;
            }
            if (GroupNode* owner = GroupOwning(gn->index))
            {
               if (ImGui::MenuItem(L("Ungroup")))
               {
                  PushUndoCheckpoint();
                  gGroupMembers[owner].erase(gn->index);
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
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.85f, 0.20f, 0.20f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.85f, 0.20f, 0.20f, 0.40f));
            if (ImGui::MenuItem(L("Delete Note"), "Backspace"))
            {
               if (!ed::IsNodeSelected(gn->NodeId()))
               {
                  ed::ClearSelection();
                  ed::SelectNode(gn->NodeId());
               }
               gRequestDelete = true;
            }
            ImGui::PopStyleColor(3);
         }
         else
         {
            // Primary Actions
            const char* bypassLabel = gn->node->bypassed ? "Enable Node" : "Bypass Node";
            if (ImGui::MenuItem(bypassLabel, "B"))
            {
               PushUndoCheckpoint();
               gn->node->bypassed = !gn->node->bypassed;
               if (dynamic_cast<IAudioSource*>(gn->node.get()) != nullptr ||
                   dynamic_cast<INoteSource*>(gn->node.get()) != nullptr)
               {
                  RebuildAudioTopology();
               }
            }
            if (ImGui::MenuItem(L("Duplicate"), MODKEY "+D"))
            {
               if (!ed::IsNodeSelected(gn->NodeId()))
               {
                  ed::ClearSelection();
                  ed::SelectNode(gn->NodeId());
               }
               gRequestDuplicate = true;
            }

            // View & Panel Actions
            if (!IsAudioBodyNode(gn->node.get()))
            {
               if (gn->showParams)
               {
                  if (ImGui::MenuItem(L("Hide params")))
                     gn->showParams = false;
               }
               else
               {
                  if (ImGui::MenuItem(L("Show params")))
                     gn->showParams = true;
               }
            }
            if (dynamic_cast<IGeometrySource*>(gn->node.get()) != nullptr)
            {
               if (gn->showMiniViewport)
               {
                  if (ImGui::MenuItem(L("Hide viewport")))
                     gn->showMiniViewport = false;
               }
               else
               {
                  if (ImGui::MenuItem(L("Show viewport")))
                     gn->showMiniViewport = true;
               }
            }
            if (CanShowInViewportPanel(*gn))
            {
               if (ImGui::MenuItem(L("Open in viewport panel")))
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn->index) ==
                      gViewportPanelNodes.end())
                     gViewportPanelNodes.push_back(gn->index);
                  gViewportPanelOpen = true;
               }
            }
            if (dynamic_cast<IModulator*>(gn->node.get()) != nullptr)
            {
               if (ImGui::MenuItem(L("Show modulation matrix")))
                  gModMatrixOpen = true;
            }
            if (IsNodeVideoCompatible(*gn) && IsNodeAudioCompatible(*gn))
            {
               // Picture and sound from one node (Video Source): the user
               // picks the lane; the audio clip reads its audio output.
               if (ImGui::BeginMenu(L("Add to Timeline")))
               {
                  if (ImGui::MenuItem(L("Video")))
                     AddNodeToArrangeTimeline(gn->index, Arrange::kLaneVideo);
                  if (ImGui::MenuItem(L("Audio")))
                     AddNodeToArrangeTimeline(gn->index, Arrange::kLaneAudio);
                  ImGui::EndMenu();
               }
            }
            else if (IsNodeVideoCompatible(*gn) || IsNodeAudioCompatible(*gn))
            {
               if (ImGui::MenuItem(L("Add to Timeline")))
                  AddNodeToArrangeTimeline(gn->index);
            }
            ProjectorWindow* projector = FindProjectorWindow(gn->index);
            if (CanShowInViewportPanel(*gn) && projector != nullptr)
            {
               if (ImGui::BeginMenu(L("Output window")))
               {
                  if (ImGui::MenuItem(L("Fullscreen"), "F11", projector->fullscreen))
                     ToggleProjectorFullscreen(*projector);
                  if (ImGui::BeginMenu(L("Display")))
                  {
                     int monitorCount = 0;
                     GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
                     for (int i = 0; i < monitorCount; i++)
                     {
                        const char* name = glfwGetMonitorName(monitors[i]);
                        char label[64];
                        snprintf(label, sizeof(label), "%d: %s", i + 1, name != nullptr ? name : "Display");
                        if (ImGui::MenuItem(label, nullptr, projector->monitorIndex == i))
                           MoveProjectorToMonitor(*projector, i);
                     }
                     ImGui::EndMenu();
                  }
                  ImGui::Separator();
                  if (ImGui::MenuItem(L("Close window")))
                     CloseProjectorWindowFor(gn->index);
                  ImGui::EndMenu();
               }
            }
            else if (CanShowInViewportPanel(*gn))
            {
               if (ImGui::MenuItem(L("Open in new window")))
                  OpenProjectorWindow(window, *gn);
            }

            ImGui::Separator();

            // Information & Hierarchy
            if (ImGui::MenuItem(L("Help")))
            {
               gHelpPopupNodeIndex = gn->index;
               ImGui::CloseCurrentPopup();
               gOpenNodeHelpPopup = true;
            }
            if (GroupNode* owner = GroupOwning(gn->index))
            {
               if (ImGui::MenuItem(L("Ungroup")))
               {
                  PushUndoCheckpoint();
                  gGroupMembers[owner].erase(gn->index);
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

            ImGui::Separator();

            // Destructive Action: Delete Node with HIG danger hover styling
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.85f, 0.20f, 0.20f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.85f, 0.20f, 0.20f, 0.40f));
            if (ImGui::MenuItem(L("Delete Node"), "Backspace"))
            {
               if (!ed::IsNodeSelected(gn->NodeId()))
               {
                  ed::ClearSelection();
                  ed::SelectNode(gn->NodeId());
               }
               gRequestDelete = true;
            }
            ImGui::PopStyleColor(3);
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
         gNodeHelpShown = true;
         GraphNode* gn = FindNodeByIndex(gHelpPopupNodeIndex);
         if (gCloseNodeHelp)
         {
            gCloseNodeHelp = false;
            ImGui::CloseCurrentPopup();
         }
         else if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::TextUnformatted(NodeTitle(*gn).c_str());
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
         gModRangeTypedField = -1;
         gModRangeTypedText.clear();
         gModRangeTypedPendingInit = false;
         gModRangeTypedNoAutoSelect = false;
      }
      if (ImGui::BeginPopup("##modbind"))
      {
         Modulation& mod = Modulation::Instance();
         GestureRecorder& rec = GestureRecorder::Instance();
         const int nodeIndex = gModBindingMenuNode;
         const int paramIndex = gModBindingMenuParam;
         const bool modulated = mod.IsModulated(nodeIndex, paramIndex);
         const bool hasExpr = !modulated && mod.HasExpression(nodeIndex, paramIndex);
         const GestureRecorder::Key gestureKey(nodeIndex, paramIndex);
         const bool hasPlayback = rec.Playbacks().count(gestureKey) > 0;
         const bool isRecordingState = !modulated && !hasExpr && (rec.IsArmed(nodeIndex, paramIndex) || hasPlayback);

         // The destination's own declared min/max/step, looked up the same
         // way Bind() does - this frame's FrameParams, keyed by (nodeIndex,
         // paramIndex). Every branch below needs it (Enter Value's seed, the
         // modulated/expression/recording Range fields, ...), so it's looked
         // up once here instead of once per branch.
         const ParamRef* destRef = nullptr;
         for (const ParamRef& r : mod.FrameParams())
         {
            if (r.nodeIndex == nodeIndex && r.paramIndex == paramIndex)
            {
               destRef = &r;
               break;
            }
         }
         const bool isInt = destRef != nullptr && destRef->step > 0.0f;
         const char* valueFmt = isInt ? "%.0f" : "%.3f";
         const std::pair<int, int> editKey(nodeIndex, paramIndex);

         // Shared hover-and-type drag field, matching the convention every
         // other param field in this file uses (see gTypedParam/
         // BeginTypedEditFromCurrent) instead of depending on DragFloat's own
         // double-click/Ctrl+click text-entry, which nothing here hints
         // exists. field 0=lo, 1=hi - the modulated/expression/recording
         // Range editors below never appear at once for a given param, so
         // sharing gModRangeTypedField's single slot across them is safe.
         bool rangeChanged = false;
         auto drawRangeField = [&](int field, const char* dragId, const char* typedId,
                                   const char* dragFmt, float minV, float maxV, float& v,
                                   float step = -1.0f, float width = 90.0f)
         {
            if (step < 0.0f)
               step = isInt ? 1.0f : (maxV - minV) * 0.01f;
            if (gModRangeTypedField == field)
            {
               ImGui::SetNextItemWidth(width);
               if (gModRangeTypedJustOpened)
               {
                  ImGui::SetKeyboardFocusHere();
                  gModRangeTypedJustOpened = false;
               }
               char buf[64];
               snprintf(buf, sizeof(buf), "%s", gModRangeTypedText.c_str());
               const bool entered = ImGui::InputText(typedId, buf, sizeof(buf),
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
               gModRangeTypedText = buf;
               // Same race BeginTypedEditFromCurrent's comment describes:
               // InputText's own select-all only runs once it's confirmed
               // active, which can land a frame after SetKeyboardFocusHere -
               // relying on that timing instead of driving selection
               // explicitly is what made typing over the seeded digit
               // unreliable (append instead of replace, needing extra
               // digits to "overwrite" it).
               if (gModRangeTypedPendingInit && ImGui::IsItemActive())
               {
                  if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID()))
                  {
                     if (gModRangeTypedNoAutoSelect)
                     {
                        state->Stb.cursor = state->CurLenW;
                        state->ClearSelection();
                     }
                     else
                     {
                        state->SelectAll();
                     }
                  }
                  gModRangeTypedPendingInit = false;
               }
               if (entered || ImGui::IsItemDeactivated())
               {
                  char* end = nullptr;
                  const float parsed = strtof(gModRangeTypedText.c_str(), &end);
                  if (end != gModRangeTypedText.c_str())
                  {
                     v = parsed;
                     rangeChanged = true;
                  }
                  gModRangeTypedField = -1;
                  gModRangeTypedText.clear();
                  gModRangeTypedPendingInit = false;
                  gModRangeTypedNoAutoSelect = false;
               }
            }
            else
            {
               ImGui::SetNextItemWidth(width);
               rangeChanged |= ImGui::DragFloat(dragId, &v, step, minV, maxV, dragFmt);
               if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               {
                  char seed[64];
                  snprintf(seed, sizeof(seed), step >= 1.0f ? "%.0f" : "%.3f", v);
                  gModRangeTypedField = field;
                  gModRangeTypedText = seed;
                  gModRangeTypedJustOpened = true;
                  gModRangeTypedPendingInit = true;
                  gModRangeTypedNoAutoSelect = false;
               }
               else if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && !io.KeyCtrl && !io.KeySuper)
               {
                  std::string seed;
                  for (int k = 0; k < 10; k++)
                  {
                     if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + k), false))
                     {
                        seed = std::string(1, char('0' + k));
                        break;
                     }
                  }
                  if (seed.empty() && ImGui::IsKeyPressed(ImGuiKey_Minus, false))
                     seed = "-";
                  if (!seed.empty())
                  {
                     gModRangeTypedField = field;
                     gModRangeTypedText = seed;
                     gModRangeTypedJustOpened = true;
                     gModRangeTypedPendingInit = true;
                     gModRangeTypedNoAutoSelect = true;
                  }
               }
            }
         };

         if (modulated)
         {
            if (destRef == nullptr)
            {
               // The param didn't draw this frame (e.g. a collapsed node) -
               // nothing to edit against, so just offer Unbind.
               ImGui::TextDisabled("%s", T("(parameter not visible)"));
            }
            else
            {
               const Modulation::Source src = mod.ResolvedSourceFor(*destRef);
               float lo = src.lo, hi = src.hi;
               // A bool wants a toggle and an enum wants a selector; a knob
               // is only the right default for a continuous param.
               const int perfKind = destRef->isBool ? 3 : (destRef->isEnum ? 7 : 0);
               if (ImGui::MenuItem(L("Add to Performance Matrix")))
                  AddToPerformanceMatrix(nodeIndex, paramIndex, perfKind);
               DrawParamMidiLearnMenuItem(nodeIndex, paramIndex);
               if (ImGui::MenuItem(L("View in Modulation Matrix")))
               {
                  gModMatrixOpen = true;
                  gModMatrixHighlightNode = nodeIndex;
                  gModMatrixHighlightParam = paramIndex;
                  gModMatrixHighlightUntil = ImGui::GetTime() + 1.5;
                  gModMatrixScrollPending = true;
               }
               ImGui::Separator();

               // Double-clicking a field, or hovering it and typing a
               // digit/'-', swaps it for a focused text box seeded from
               // either the current value or the keystroke.
               drawRangeField(0, "##lo", "##lotyped", isInt ? "lo %.0f" : "lo %.3f",
                              destRef->minValue, destRef->maxValue, lo);
               ImGui::SameLine();
               drawRangeField(1, "##hi", "##hityped", isInt ? "hi %.0f" : "hi %.3f",
                              destRef->minValue, destRef->maxValue, hi);

               if (rangeChanged)
               {
                  lo = std::clamp(lo, destRef->minValue, destRef->maxValue);
                  hi = std::clamp(hi, destRef->minValue, destRef->maxValue);
                  if (isInt)
                  {
                     lo = std::round(lo);
                     hi = std::round(hi);
                  }
                  mod.SetRange(nodeIndex, paramIndex, lo, hi);
               }
               ImGui::Separator();
               if (ImGui::MenuItem(L("Full range")))
                  mod.SetRange(nodeIndex, paramIndex, destRef->minValue, destRef->maxValue);
               if (ImGui::MenuItem(L("Around current")))
               {
                  const float span = (destRef->maxValue - destRef->minValue) * 0.25f;
                  const float centre = destRef->value != nullptr ? *destRef->value : src.centre;
                  mod.SetRange(nodeIndex, paramIndex,
                              std::clamp(centre - span, destRef->minValue, destRef->maxValue),
                              std::clamp(centre + span, destRef->minValue, destRef->maxValue));
               }
               if (ImGui::MenuItem(L("Invert")))
                  mod.SetRange(nodeIndex, paramIndex, src.hi, src.lo);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("Unbind")))
            {
               PushUndoCheckpoint();
               mod.Unbind(nodeIndex, paramIndex);
            }
         }
         else if (hasExpr)
         {
            if (destRef == nullptr)
            {
               ImGui::TextDisabled("%s", T("(parameter not visible)"));
            }
            else
            {
               if (ImGui::MenuItem(L("Edit Expression")))
                  BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, destRef->value, valueFmt, /*hasExpr=*/true);
               const int perfKind = destRef->isBool ? 3 : (destRef->isEnum ? 7 : 0);
               if (ImGui::MenuItem(L("Add to Performance Matrix")))
                  AddToPerformanceMatrix(nodeIndex, paramIndex, perfKind);
               ImGui::Separator();
               float lo, hi;
               if (!mod.ExpressionRangeFor(nodeIndex, paramIndex, lo, hi))
               {
                  lo = destRef->minValue;
                  hi = destRef->maxValue;
               }
               drawRangeField(0, "##elo", "##elotyped", isInt ? "lo %.0f" : "lo %.3f",
                              destRef->minValue, destRef->maxValue, lo);
               ImGui::SameLine();
               drawRangeField(1, "##ehi", "##ehityped", isInt ? "hi %.0f" : "hi %.3f",
                              destRef->minValue, destRef->maxValue, hi);
               if (rangeChanged)
               {
                  lo = std::clamp(lo, destRef->minValue, destRef->maxValue);
                  hi = std::clamp(hi, destRef->minValue, destRef->maxValue);
                  mod.SetExpressionRange(nodeIndex, paramIndex, lo, hi);
               }
               ImGui::Separator();
               if (ImGui::MenuItem(L("Full range")))
                  mod.ClearExpressionRange(nodeIndex, paramIndex);
               if (ImGui::MenuItem(L("Invert")))
                  mod.SetExpressionRange(nodeIndex, paramIndex, hi, lo);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("Unbind")))
            {
               PushUndoCheckpoint();
               mod.ClearExpression(nodeIndex, paramIndex);
            }
         }
         else if (isRecordingState)
         {
            if (!hasPlayback)
            {
               // Armed but nothing dragged yet - nothing to configure
               // speed/range against, only the option to back out.
               ImGui::TextDisabled("%s", T("Waiting for movement..."));
               ImGui::Separator();
               if (ImGui::MenuItem(L("Cancel Recording")))
                  rec.CancelArm(nodeIndex, paramIndex);
            }
            else
            {
               // A full-width drag field with the label baked into its own
               // format string, matching the lo/hi fields' "lo -1.000" look,
               // and reusing the same hover-and-type-a-digit convenience
               // (field 3, since 0/1 are lo/hi and this popup never shows
               // both at once with the modulated/expression Range editors).
               float speed = rec.PlaybackSpeedFor(nodeIndex, paramIndex);
               drawRangeField(3, "##recspeed", "##recspeedtyped", "Speed %.2fx", 0.05f, 4.0f, speed, 0.01f, -FLT_MIN);
               if (rangeChanged)
               {
                  rec.SetPlaybackSpeed(nodeIndex, paramIndex, speed);
                  rangeChanged = false;
               }
               ImGui::Separator();
               const int perfKind = destRef != nullptr ? (destRef->isBool ? 3 : (destRef->isEnum ? 7 : 0)) : 0;
               if (ImGui::MenuItem(L("Add to Performance Matrix")))
                  AddToPerformanceMatrix(nodeIndex, paramIndex, perfKind);
               ImGui::Separator();
               const float defLo = destRef != nullptr ? destRef->minValue : 0.0f;
               const float defHi = destRef != nullptr ? destRef->maxValue : 1.0f;
               float lo, hi;
               if (!rec.PlaybackRangeFor(nodeIndex, paramIndex, lo, hi))
               {
                  lo = defLo;
                  hi = defHi;
               }
               drawRangeField(0, "##rlo", "##rlotyped", isInt ? "lo %.0f" : "lo %.3f", defLo, defHi, lo);
               ImGui::SameLine();
               drawRangeField(1, "##rhi", "##rhityped", isInt ? "hi %.0f" : "hi %.3f", defLo, defHi, hi);
               if (rangeChanged)
                  rec.SetPlaybackRange(nodeIndex, paramIndex, lo, hi);
               ImGui::Separator();
               if (ImGui::MenuItem(L("Full range")))
                  rec.ClearPlaybackRange(nodeIndex, paramIndex);
               if (ImGui::MenuItem(L("Invert")))
                  rec.SetPlaybackRange(nodeIndex, paramIndex, hi, lo);
               ImGui::Separator();
               if (ImGui::MenuItem(L("Record Again")))
                  rec.ArmParam(nodeIndex, paramIndex);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("Unbind")))
            {
               rec.CancelArm(nodeIndex, paramIndex);
               rec.StopPlayback(nodeIndex, paramIndex);
            }
         }
         else
         {
            if (destRef == nullptr)
            {
               ImGui::TextDisabled("%s", T("(parameter not visible)"));
            }
            else
            {
               // Enter Value/Expression and Start Recording only make sense
               // for a continuous param being typed or dragged - a checkbox
               // has nothing to type in or drag, so it only gets the
               // performance-matrix entry.
               const int perfKind = destRef->isBool ? 3 : (destRef->isEnum ? 7 : 0);
               if (!destRef->isBool)
               {
                  if (ImGui::MenuItem(L("Enter Value")))
                     BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, destRef->value, valueFmt, /*hasExpr=*/false);
               }
               if (ImGui::MenuItem(L("Add to Performance Matrix")))
                  AddToPerformanceMatrix(nodeIndex, paramIndex, perfKind);
               DrawParamMidiLearnMenuItem(nodeIndex, paramIndex);
               if (!destRef->isBool)
               {
                  if (ImGui::MenuItem(L("Enter Expression")))
                     BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, destRef->value, valueFmt, /*hasExpr=*/true);
                  if (ImGui::MenuItem(L("Start Recording")))
                  {
                     PushUndoCheckpoint();
                     rec.ArmParam(nodeIndex, paramIndex);
                  }
               }
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
      // IsWindowHovered() is false while a floating window (Settings, panels) covers the cursor, so a
      // double-click on a control inside one does not also open the spawner behind it.
      if (overGraph && ImGui::IsWindowHovered() &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
          !ed::GetHoveredNode() && !ed::GetHoveredPin() && !ed::GetHoveredLink() &&
          gCommentEdit.target == nullptr)
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
         const std::string dragDisplayName = (gSampleDragKind == LibraryDragKind::FieldPreset)
            ? gFieldDragPresetName : gSampleDragName;
         ImGui::GetForegroundDrawList()->AddText(ImVec2(mp.x + 14.0f, mp.y + 14.0f),
                                                  IM_COL32(230, 235, 245, 255), dragDisplayName.c_str());

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
            const bool overCanvas = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
               (mp.x >= gGraphScreenTL.x && mp.y >= gGraphScreenTL.y &&
                mp.x <= gGraphScreenTL.x + gGraphScreenSize.x && mp.y <= gGraphScreenTL.y + gGraphScreenSize.y);

            // Released over the Arrange panel instead of the canvas: route
            // to the timeline-drop import path (stashed for
            // DrawArrangePanelContent to resolve into a lane+tick - see
            // gArrangePendingBrowserDrop's own comment) rather than any of
            // the canvas-node-target dispatch below. Only Sample/Media drags
            // carry a real file path Arrange import understands; Plugin and
            // FieldPreset drags have no Arrange meaning and fall through to
            // their usual canvas-only handling untouched.
            const bool overArrangePanelForDrop = gArrangePanelOpen &&
               mp.x >= gArrangePanelRectMin.x && mp.x < gArrangePanelRectMax.x &&
               mp.y >= gArrangePanelRectMin.y && mp.y < gArrangePanelRectMax.y;
            if (overArrangePanelForDrop &&
                (gSampleDragKind == LibraryDragKind::Sample || gSampleDragKind == LibraryDragKind::Media) &&
                !gSampleDragPath.empty())
            {
               gArrangePendingBrowserDrop.pending = true;
               gArrangePendingBrowserDrop.screenPos = mp;
               gArrangePendingBrowserDrop.path = gSampleDragPath;
               // Drag-state reset (gSampleDragActive etc.) happens once,
               // unconditionally, right after this whole if/else-if chain -
               // no need to repeat it here.
            }
            else if (gSampleDragKind == LibraryDragKind::FieldPreset)
            {
               FieldSearchEntry entry;
               entry.name = gFieldDragPresetName;
               entry.nodeType = gFieldDragNodeType;
               entry.nodeCategory = gFieldDragNodeCategory;
               entry.presetIndex = gFieldDragIndex;

               bool handled = false;
               if (entry.nodeType == "Field Synth")
               {
                  if (FieldSynthNode* target = FindNodeUnderCanvasPoint<FieldSynthNode>(canvasMouse))
                  {
                     PushUndoCheckpoint();
                     target->presetIndex = entry.presetIndex;
                     target->LoadPreset(entry.presetIndex);
                     gPatchDirty = true;
                     handled = true;
                  }
               }
               else if (entry.nodeType == "Field Effect")
               {
                  if (FieldSampleNode* target = FindNodeUnderCanvasPoint<FieldSampleNode>(canvasMouse))
                  {
                     PushUndoCheckpoint();
                     target->presetIndex = entry.presetIndex;
                     target->LoadPreset(entry.presetIndex);
                     gPatchDirty = true;
                     handled = true;
                  }
               }
               else if (entry.nodeType == "Field Modifier")
               {
                  if (FieldElementNode* target = FindNodeUnderCanvasPoint<FieldElementNode>(canvasMouse))
                  {
                     PushUndoCheckpoint();
                     target->presetIndex = entry.presetIndex;
                     target->LoadPreset(entry.presetIndex);
                     gPatchDirty = true;
                     handled = true;
                  }
               }
               else if (entry.nodeType == "Field Primitive")
               {
                  if (FieldPrimitiveNode* target = FindNodeUnderCanvasPoint<FieldPrimitiveNode>(canvasMouse))
                  {
                     PushUndoCheckpoint();
                     target->presetIndex = entry.presetIndex;
                     target->LoadPreset(entry.presetIndex);
                     gPatchDirty = true;
                     handled = true;
                  }
               }
               else if (entry.nodeType == "FieldPixel")
               {
                  if (FieldPixelNode* target = FindNodeUnderCanvasPoint<FieldPixelNode>(canvasMouse))
                  {
                     PushUndoCheckpoint();
                     target->presetIndex = entry.presetIndex;
                     target->LoadPreset(entry.presetIndex);
                     gPatchDirty = true;
                     handled = true;
                  }
               }

               if (!handled && overCanvas)
               {
                  SpawnFieldPresetNode(entry, canvasMouse.x, canvasMouse.y);
               }
            }
            else if (gSampleDragKind == LibraryDragKind::Plugin)
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
                  // Dropped onto an MPC: onto the pad under the cursor, else the next empty one.
                  PushUndoCheckpoint();
                  MpcDropFiles(targetMpc, canvasMouse.x, canvasMouse.y, { gSampleDragPath });
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
               else if (AudioFileNode* targetAudioFile = FindNodeUnderCanvasPoint<AudioFileNode>(canvasMouse))
               {
                  // Dropped onto an existing Audio File: swap its file.
                  PushUndoCheckpoint();
                  targetAudioFile->Open(gSampleDragPath);
                  gPatchDirty = true;
               }
               else if (overCanvas)
               {
                  // Released on empty canvas: prompt the user with a choice of
                  // node to spawn and load the sample into.
                  gAudioDropPicker.justOpened = true;
                  gAudioDropPicker.canvasPos = canvasMouse;
                  gAudioDropPicker.screenPos = mp;
                  gAudioDropPicker.paths = { gSampleDragPath };
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
            gFieldDragPresetName.clear();
            gFieldDragNodeType.clear();
            gFieldDragNodeCategory.clear();
            gFieldDragIndex = -1;
            gPluginDragDesc = Platform::PluginDesc();
         }
      }}
}

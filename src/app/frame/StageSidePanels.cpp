// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/TokenColors.h"
#include "app/ui/design/components/PillGroup.h"
#include "app/ui/design/components/LibraryParts.h"
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawSidePanels(FrameCtx& fc)
{
   ImGuiIO& io = ImGui::GetIO();
   auto& frameId = fc.frameId;
   auto& benchStagesSample = fc.benchStagesSample;
   auto& benchStagesCpuSample = fc.benchStagesCpuSample;
   auto& savedWheel = fc.savedWheel;
   auto& savedWheelH = fc.savedWheelH;
   auto& kNodePanelWidth = fc.kNodePanelWidth;
   auto& viewportBottom = fc.viewportBottom;
   auto& viewportRight = fc.viewportRight;
   auto& matrixBottom = fc.matrixBottom;
   auto& matrixRight = fc.matrixRight;
   auto& perfBottom = fc.perfBottom;
   auto& perfRight = fc.perfRight;
   auto& arrangeBottom = fc.arrangeBottom;
   auto& arrangeRight = fc.arrangeRight;
   auto& graphHeight = fc.graphHeight;
   auto& timerNodeBodies = *fc.timerNodeBodies;
   auto& timerNodeBodiesGpu = *fc.timerNodeBodiesGpu;


      // Arrangement clip "Assign Node..." canvas picker - same click-to-assign
      // UX as gPerfAssigningElemIdx above, but whole-node instead of
      // per-parameter: hovering a compatible node glows its whole bounding
      // box (via ed::GetNodePosition/GetNodeSize, both canvas-space here
      // just like the param picker's mp above) and a click assigns it as the
      // clip's source.
      if (gArrangeAssigningClipId != 0 && !Arrange::Find(gArrange, gArrangeAssigningClipId).Valid())
         gArrangeAssigningClipId = 0;
      if (gArrangeAssigningClipId != 0)
      {
         const Arrange::Loc assignLoc = Arrange::Find(gArrange, gArrangeAssigningClipId);
         const bool assignIsVideo = gArrange.lanes[assignLoc.lane].type == Arrange::kLaneVideo;
         const ImVec2 mp = ImGui::GetMousePos();

         GraphNode* hoveredCompatible = nullptr;
         ImVec2 hoveredP(0, 0), hoveredS(0, 0);
         for (GraphNode& gn : gNodes)
         {
            const bool match = assignIsVideo ? IsNodeVideoCompatible(gn) : IsNodeAudioCompatible(gn);
            if (!match)
               continue;
            const ImVec2 p = ed::GetNodePosition(gn.NodeId());
            const ImVec2 s = ed::GetNodeSize(gn.NodeId());
            if (mp.x >= p.x && mp.x <= p.x + s.x && mp.y >= p.y && mp.y <= p.y + s.y)
            {
               hoveredCompatible = &gn;
               hoveredP = p;
               hoveredS = s;
               break;
            }
         }

         if (hoveredCompatible != nullptr)
         {
            ImDrawList* hoverDl = ImGui::GetWindowDrawList();
            const ImU32 glowCol = tok::U32(tok::pal::c_00E6FF28);
            const ImU32 ringCol = tok::U32(tok::pal::c_00E6FFC8);
            hoverDl->AddRectFilled(hoveredP, ImVec2(hoveredP.x + hoveredS.x, hoveredP.y + hoveredS.y), glowCol, 6.0f);
            hoverDl->AddRect(hoveredP, ImVec2(hoveredP.x + hoveredS.x, hoveredP.y + hoveredS.y), ringCol, 6.0f, 0, 2.0f);

            ed::Suspend();
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip(T("Assign clip source -> %s"), NodeTitleWithInstance(*hoveredCompatible).c_str());
            ed::Resume();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
               // By uid, through the model - one call (one undo entry) per
               // target clip, so a multi-select Assign Node... points every
               // selected clip of this lane type at the same node.
               ArrangeAssignClipSource(gArrangeAssigningClipId, hoveredCompatible->uid);
               for (uint64_t targetId : gArrangeAssignTargetIds)
                  ArrangeAssignClipSource(targetId, hoveredCompatible->uid);
               gArrangeAssigningClipId = 0;
               gArrangeAssignTargetIds.clear();
            }
         }

         if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            gArrangeAssigningClipId = 0;
            gArrangeAssignTargetIds.clear();
         }
      }

      timerNodeBodies.Stop();
      timerNodeBodiesGpu.Stop();

      // [edperf] BuildControl's per-frame hit-test walk is the one part of the
      // editor whose cost scales with patch size; a spindump that lands here
      // is indistinguishable from a freeze, so keep it measurable.
      static const bool kEdPerf = getenv("INFINITE_EDPERF") != nullptr ||
                                  getenv("INFINITE_EDPERFTEST") != nullptr;
      const auto edEndStart = kEdPerf ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
      {
         ConditionalStageTimer timerEditorEnd(benchStagesCpuSample ? &sStageEditorEnd : nullptr, Bench::FrameTail::kEditorEnd);
         Bench::ConditionalGpuStageTimer timerEditorEndGpu(benchStagesSample ? &sGpuTimerRing : nullptr, "editor_end", frameId);
         // Flush against any bottom-docked panel, for the same reason as the
         // Draw*Docked EndChild calls above.
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
         // imgui-node-editor's own ed::End() unconditionally strokes a rect
         // around the whole canvas using ImGuiCol_Border/BorderShadow (see
         // "Draw border" in imgui_node_editor.cpp) - unlike every other border
         // in this app, it isn't gated by style.WindowBorderSize/ChildBorderSize
         // (both zeroed in ApplyTheme), so it painted a thin line around the
         // canvas that scaled with the canvas rect itself regardless of that
         // setting. Barely visible against the dark theme's border color, but a
         // clearly visible dark line in light mode. Suppressed the same way the
         // menu-bar/canvas seam was: make the two colors it reads transparent
         // for just this call.
         ImGui::PushStyleColor(ImGuiCol_Border, tok::V4(tok::palf::v_0_0_0_0));
         ImGui::PushStyleColor(ImGuiCol_BorderShadow, tok::V4(tok::palf::v_0_0_0_0));
         ed::End();
         ImGui::PopStyleColor(2);
         ImGui::PopStyleVar();
      }
      if (kEdPerf)
      {
         const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - edEndStart).count();
         printf("[edperf] frame=%d nodes=%zu ed::End=%.2fms\n", frameId, gNodes.size(), ms);
      }
      ed::SetCurrentEditor(nullptr);

      if (getenv("INFINITE_PINDUPTEST") != nullptr && frameId == 6)
      {
         // Counted inside NodeBuilder::BeginPin (imgui_node_editor.cpp) - see
         // the fixture above for what a non-zero value means and why it used
         // to be a hang rather than a warning. Every offending id is already
         // on stderr by the time this runs.
         using ::g_InfiniteDuplicatePinIds;
         for (const auto& gn : gNodes)
         {
            if (gn.NodeId() == 120750 || gn.index == 115)
            {
               printf("Suspect node: index=%d type=%s nodeId=%d\n", gn.index, gn.typeName.c_str(), gn.NodeId());
            }
         }
         fflush(stdout);
         if (g_InfiniteDuplicatePinIds == 0)
            printf("PINDUPTEST %zu nodes, 0 duplicate pin ids  OK\n", gNodes.size());
         else
            printf("PINDUPTEST FAIL: %d node(s) emitted a pin id twice in one frame "
                   "- see the [node editor] lines above for which\n",
                   g_InfiniteDuplicatePinIds);
      }

      // Node drawing is done. Everything below (docked panels, dialogs) must
      // not be mistaken for the last-drawn node's param block - see
      // EndNodeParams.
      EndNodeParams();

      // Field 'graph' domain (build step 10, trap T14): a "Regenerate"
      // button click sets this flag from inside DrawFieldGraphParams, which
      // runs nested in the ed::Begin()/ed::End() pass just closed above -
      // SpawnNode/RemoveNodeByIndex there would mutate gNodes (reallocating
      // its storage) while imgui-node-editor is still mid-frame over it.
      // Draining here, once per frame, right after that pass ends, is safe.
      if (gFieldGraphPendingRegenerate != nullptr)
      {
         RunFieldGraphRegenerate(gFieldGraphPendingRegenerate);
         gFieldGraphPendingRegenerate = nullptr;
      }

      // Build step 16 ("Unpack to Canvas"): same trap-T14 deferral as the
      // Regenerate drain just above - phase 1 reveals real gNodes entries
      // and phase 2 (ticked unconditionally below, while armed) eventually
      // spawns a GroupNode, neither of which is safe nested inside the
      // ed::Begin()/ed::End() pass just closed.
      if (gFieldGraphPendingUnpack != nullptr)
      {
         RunFieldGraphUnpackPhase1(gFieldGraphPendingUnpack);
         gFieldGraphPendingUnpack = nullptr;
      }
      RunFieldGraphUnpackPhase2Tick();
      RunAutoLayoutTick();

      // Dynamic pins, Phase 1 (build step 11, §5.5): trigger-pin edge
      // detection for every FieldGraphNode, polled once per frame right
      // here - same safe-to-mutate-gNodes location as the drain just above
      // (trap T14). Firing nodes are collected first and regenerated in a
      // second pass, deliberately not called from inside the gNodes range-for:
      // Regenerate() can spawn/remove nodes, which reallocates gNodes'
      // storage and would invalidate that loop's iterator/reference mid-walk.
      // Collected as raw INode-owning pointers (not GraphNode&), which stay
      // valid across such a reallocation since gNodes holds them by
      // unique_ptr, not by value.
      {
         std::vector<FieldGraphNode*> firing;
         for (GraphNode& gn : gNodes)
         {
            if (auto* fgn = dynamic_cast<FieldGraphNode*>(gn.node.get()))
            {
               if (fgn->PollTriggerEdge())
                  firing.push_back(fgn);
            }
         }
         for (FieldGraphNode* fgn : firing)
            RunFieldGraphRegenerate(fgn);
      }

      // Build step 15 §4.2: live parameter forwarding. Never spawns/removes/
      // reconnects a node - only ever calls host.SetParam on already-mounted
      // children - so unlike Regenerate() this has no trap-T14 ordering
      // requirement, but is driven from this same post-ed::End() tick for
      // consistency with the rest of this doc's flow. A no-op call
      // (mLiveForward empty, or nothing changed since last frame) is cheap,
      // so this runs for every FieldGraphNode unconditionally.
      for (GraphNode& gn : gNodes)
      {
         if (auto* fgn = dynamic_cast<FieldGraphNode*>(gn.node.get()))
         {
            if (fgn->encapsulated)
            {
               VirtualGraphHost host;
               host.owner = fgn;
               fgn->PushLiveParams(host);
            }
            else
            {
               MainGraphHost host;
               fgn->PushLiveParams(host);
            }
         }
      }

      io.MouseWheel = savedWheel;
      io.MouseWheelH = savedWheelH;

      // Right-docked viewport panel, chained via SameLine after the canvas
      if (viewportRight)
      {
         ImGui::SameLine(0.0f, 0.0f);
         DrawViewportPanelDocked("##viewportpanel_right", ImVec2(gViewportPanelWidth, graphHeight));
      }

      // Right-docked matrix panel
      if (matrixRight)
      {
         ImGui::SameLine(0.0f, 0.0f);
         DrawModMatrixDocked("##modmatrix_right", ImVec2(gModMatrixWidth, graphHeight));
      }

      // Right-docked performance matrix
      if (perfRight)
      {
         ImGui::SameLine(0.0f, 0.0f);
         DrawPerfPanelDocked("##perfpanel_right", ImVec2(gPerfPanelWidth, graphHeight));
      }

      // Right-docked arrangement timeline
      if (arrangeRight)
      {
         ImGui::SameLine(0.0f, 0.0f);
         DrawArrangePanelDocked("##arrangepanel_right", ImVec2(gArrangePanelWidth, graphHeight));
      }

      // ---- node browser / search panel ----
      // Always sticks to the rightmost edge of the window
      if (gNodePanelOpen)
      {
         ImGui::SameLine(0.0f, 0.0f);
         PushDockedPanelStyle(/*isChild=*/true);
         // Floating card: the child itself is transparent; the rounded surface and its soft shadow are drawn
         // on the parent list underneath, inset by the gap so the card never touches the window, top bar or canvas.
         const float cardGap = tok::space_2;
         {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 c0(p0.x + cardGap, p0.y + cardGap);
            const ImVec2 c1(p0.x + kNodePanelWidth - cardGap, p0.y + graphHeight - cardGap);
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            for (int i = 6; i >= 1; --i)   // soft shadow: stacked, widening, fainter rings
               pdl->AddRectFilled(ImVec2(c0.x - i, c0.y - i + 2.0f), ImVec2(c1.x + i, c1.y + i + 2.0f),
                                  IM_COL32(0, 0, 0, 7), tok::radius_group + i);
            pdl->AddRectFilled(c0, c1, ImGui::GetColorU32(ImGuiCol_ChildBg), tok::radius_group);
         }
         ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(cardGap + tok::space_2, cardGap + tok::space_2));
         ImGui::BeginChild("##nodepanel", ImVec2(kNodePanelWidth, graphHeight),
                           ImGuiChildFlags_AlwaysUseWindowPadding);
         ImGui::PopStyleVar();
         ImGui::PopStyleColor();
         PopDockedPanelStyle();

         // The five modes as one segmented control (the accent pill slides between them). No title: the top bar
         // button already names the panel.
         {
            static const int kModes[5] = { 0, 4, 1, 2, 3 };   // gSearchPanelMode value per segment
            const PillGroup::Segment segs[5] = { { "lib.modules", T("Modules") }, { "lib.field", T("Field") },
                                                 { "lib.samples", T("Samples") }, { "lib.media", T("Media") },
                                                 { "lib.plugins", T("Plugins") } };
            int sel = 0;
            for (int i = 0; i < 5; ++i)
               if (kModes[i] == gSearchPanelMode)
                  sel = i;
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const UiLayout::Rect r{ cp.x, cp.y, ImGui::GetContentRegionAvail().x, 32.0f };
            const int hit = PillGroup::Draw("lib.tabs", r, segs, 5, sel, UiType::Size::Title);
            if (hit >= 0)
               gSearchPanelMode = kModes[hit];
            ImGui::SetCursorScreenPos(cp);
            ImGui::Dummy(ImVec2(r.w, r.h));
         }
         ImGui::Dummy(ImVec2(0, tok::space_2));

         if (gSearchPanelMode == 0)
         {
            // Category-filter dropdown options, ordered by
            // CategoryColors::SemanticRank (2D/video, 3D, audio, then
            // utility) rather than NodeFactory's registration order - a
            // dropdown of a dozen-plus categories in arbitrary order is
            // hard to scan. NodeFactory's own GetCategories() order (what
            // the grouped Category view below iterates) is untouched; this
            // reordering is only the filter dropdown's option list.
            std::vector<std::string> categoryIds; // index 0 is "All" (empty id)
            std::vector<std::string> categoryNames;
            {
               std::vector<std::string> cats = NodeFactory::Instance().GetCategories();
               std::stable_sort(cats.begin(), cats.end(), [](const std::string& a, const std::string& b) {
                  return CategoryColors::SemanticRank(a) < CategoryColors::SemanticRank(b);
               });
               categoryIds.push_back(std::string());
               categoryNames.push_back("All");
               // No "Favourites" entry here - the sort dropdown beside this
               // one already has a Favourites option, and showing it in both
               // was confusing.
               for (const std::string& c : cats)
               {
                  categoryIds.push_back(c);
                  categoryNames.push_back(DisplayName(c));
               }
            }

            static const std::vector<std::string> kModuleSortNames = { "Category", "Name", "Favourites" };
            if (DrawBrowserFilterStrip(gModulesFilter, T("search modules..."), kModuleSortNames, categoryNames))
               SaveBrowserFilterPrefs();

            std::string q = gModulesFilter.query;
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            const bool sortByName = (gModulesFilter.sortMode == 1);
            const bool sortByFav = (gModulesFilter.sortMode == 2);
            const std::string categoryFilter =
               (gModulesFilter.typeFilter > 0 && gModulesFilter.typeFilter < (int)categoryIds.size())
                  ? categoryIds[gModulesFilter.typeFilter] : std::string();

            std::string spawnName, spawnCategory;
            // The list sits in a recessed well, so the controls above read as a header and the list as its own
            // surface: darker than the card, rounded, with a faint rim.
            {
               const ImVec2 w0 = ImGui::GetCursorScreenPos();
               const ImVec2 w1(w0.x + ImGui::GetContentRegionAvail().x,
                               w0.y + ImGui::GetContentRegionAvail().y);
               const bool light = CategoryColors::IsThemeLight();
               ImDrawList* wdl = ImGui::GetWindowDrawList();
               wdl->AddRectFilled(w0, w1, light ? IM_COL32(0, 0, 0, 10) : IM_COL32(0, 0, 0, 70), tok::radius_pill);
               wdl->AddRect(w0, w1, light ? IM_COL32(0, 0, 0, 14) : IM_COL32(255, 255, 255, 10), tok::radius_pill);
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tok::space_1, tok::space_1));
            ImGui::BeginChild("##nodepanellist", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            // No cache: at ~170 entries, filtering+sorting this list from
            // NodeFactory every frame is well under the cost that made
            // LibraryFilterCache necessary for the Samples/Media/Plugins
            // modes' thousands-of-entries case (see that struct's comment).
            // Measured, not assumed - revisit if this mode's entry count
            // grows by an order of magnitude.
            if (sortByName || sortByFav)
            {
               // Flat alphabetical/favourites list across all categories, no headings -
               // a flat list interrupted by category headings is neither
               // one thing nor the other.
               std::vector<std::pair<std::string, std::string>> matches; // name, category
               for (const std::string& category : NodeFactory::Instance().GetCategories())
               {
                  if (!categoryFilter.empty() && category != categoryFilter)
                     continue;
                  for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
                  {
                     if (!IsUserSpawnable(name))
                        continue;
                     if (!q.empty())
                     {
                        std::string hay = NodeSearchHaystack(name, category);
                        if (!NodeSearchMatches(hay, q))
                           continue;
                     }
                     matches.emplace_back(name, category);
                  }
               }
               if (sortByFav)
               {
                  std::stable_sort(matches.begin(), matches.end(),
                                    [](const std::pair<std::string, std::string>& a,
                                       const std::pair<std::string, std::string>& b) {
                     const bool favA = gBrowserFavorites.IsFavoriteModule(a.first);
                     const bool favB = gBrowserFavorites.IsFavoriteModule(b.first);
                     if (favA != favB)
                        return favA > favB;
                     return ILess(a.first, b.first);
                  });
               }
               else
               {
                  // Case-insensitive fold, with a stable tiebreak on the raw
                  // name (see ILess) so entries differing only in case don't
                  // shuffle between frames.
                  std::stable_sort(matches.begin(), matches.end(),
                                    [](const std::pair<std::string, std::string>& a,
                                       const std::pair<std::string, std::string>& b) {
                     return ILess(a.first, b.first);
                  });
               }
               if (gModulesFilter.descending)
                  std::reverse(matches.begin(), matches.end());

               for (const auto& match : matches)
               {
                  ImGui::PushID(match.first.c_str());
                  const bool isFav = gBrowserFavorites.IsFavoriteModule(match.first);
                  const float availW = ImGui::GetContentRegionAvail().x;
                  const std::string rowLabel =
                     TruncateWithEllipsis(DisplayName(match.first), std::max(20.0f, availW - 64.0f));
                  const LibraryParts::RowResult row = LibraryParts::Row("row", rowLabel, nullptr, isFav);
                  if (row.clicked)
                  {
                     spawnName = match.first;
                     spawnCategory = match.second;
                  }
                  if (row.hovered && rowLabel != DisplayName(match.first))
                     ImGui::SetTooltip("%s", DisplayName(match.first).c_str());
                  if (ImGui::BeginPopupContextItem("##mod_ctx"))
                  {
                     if (ImGui::MenuItem(isFav ? L("Remove from favourites") : L("Add to favourites")))
                        gBrowserFavorites.ToggleModule(match.first);
                     if (ImGui::MenuItem(L("Add to canvas")))
                     {
                        spawnName = match.first;
                        spawnCategory = match.second;
                     }
                     ImGui::EndPopup();
                  }
                  ImGui::PopID();
               }
            }
            else
            {
               // Category view (default - today's behaviour, unchanged for
               // people who don't touch the sort control): grouped
               // headings in NodeFactory's own registration order.
               bool firstSection = true;
               for (const std::string& category : NodeFactory::Instance().GetCategories())
               {
                  if (!categoryFilter.empty() && category != categoryFilter)
                     continue;
                  // With a query the categories are only drawn when something in them
                  // matches, so an empty heading never sits there on its own.
                  std::vector<std::string> matches;
                  for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
                  {
                     if (!IsUserSpawnable(name))
                        continue;
                     if (q.empty())
                     {
                        matches.push_back(name);
                        continue;
                     }
                     std::string hay = NodeSearchHaystack(name, category);
                     if (NodeSearchMatches(hay, q))
                        matches.push_back(name);
                  }
                  if (matches.empty())
                     continue;
                  if (gModulesFilter.descending)
                     std::reverse(matches.begin(), matches.end());

                  LibraryParts::SectionHeader(DisplayName(category).c_str(), (int)matches.size(), firstSection);
                  firstSection = false;
                  for (const std::string& name : matches)
                  {
                     ImGui::PushID(name.c_str());
                     const bool isFav = gBrowserFavorites.IsFavoriteModule(name);
                     const float availW = ImGui::GetContentRegionAvail().x;
                     const std::string rowLabel =
                        TruncateWithEllipsis(DisplayName(name), std::max(20.0f, availW - 64.0f));
                     const LibraryParts::RowResult row = LibraryParts::Row("row", rowLabel, nullptr, isFav);
                     if (row.clicked)
                     {
                        spawnName = name;
                        spawnCategory = category;
                     }
                     if (row.hovered && rowLabel != DisplayName(name))
                        ImGui::SetTooltip("%s", DisplayName(name).c_str());
                     if (ImGui::BeginPopupContextItem("##mod_cat_ctx"))
                     {
                        if (ImGui::MenuItem(isFav ? L("Remove from favourites") : L("Add to favourites")))
                           gBrowserFavorites.ToggleModule(name);
                        if (ImGui::MenuItem(L("Add to canvas")))
                        {
                           spawnName = name;
                           spawnCategory = category;
                        }
                        ImGui::EndPopup();
                     }
                     ImGui::PopID();
                  }
               }
            }
            ImGui::EndChild();

            if (!spawnName.empty())
            {
               // Aimed at the middle of the view rather than at the mouse: the
               // click happened over the panel, not over the canvas. Landing
               // exactly on the center every time would stack repeated clicks on
               // top of each other, so nudge to the nearest spot around the
               // center that isn't already covered by another node.
               ImVec2 pos = FindFreeSpawnPosition(gViewCenterCanvas);
               SpawnNode(spawnName, spawnCategory, pos.x, pos.y);
               gPatchDirty = true;
            }
         }
         else if (gSearchPanelMode == 1)
         {
            DrawLibrarySearchPanel(gSampleScanner, "##samples", T("search samples..."), false);
         }
         else if (gSearchPanelMode == 2)
         {
            DrawLibrarySearchPanel(gMediaScanner, "##media", T("search media..."), true);
         }
         else if (gSearchPanelMode == 4)
         {
            DrawFieldSearchPanel();
         }
         else
         {
            DrawPluginSearchPanel();
         }

         // Same reasoning as the "Zeroed only around EndChild" comment in
         // DrawViewportPanelDocked/DrawModMatrixDocked: ImGui bakes the gap
         // that follows a child into that child's OWN EndChild() call (see
         // ItemSize() in imgui.cpp, which reads style.ItemSpacing at the
         // moment the item finishes, not when the next one starts) - so a
         // PushStyleVar placed later, right before the bottom-docked row
         // below, is too late to zero this gap. This panel is almost always
         // the last item of the top row (it "always sticks to the rightmost
         // edge"), which is exactly why the stray windowBg seam kept showing
         // above every kind of bottom-docked panel regardless of which one -
         // it never had anything to do with the bottom panel at all.
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
         ImGui::EndChild();
         ImGui::PopStyleVar();
      }

      // Bottom-docked viewport panel: a fresh, full-width row below the
      // canvas (and below the row above, if that one drew anything) rather
      // than same-line - see the graphHeight calc above ed::Begin(), which
      // already reserved this space.
      //
      // The seam this row sits under is handled at its SOURCE now: whatever
      // ends up being the last item of the row above (ed::End()'s canvas, a
      // right-docked panel's own EndChild, or the node/search panel's own
      // EndChild) zeroes ItemSpacing tightly around just that one call - see
      // the "Zeroed only around EndChild" comments there and in
      // DrawViewportPanelDocked/DrawModMatrixDocked. A PushStyleVar wrapped
      // around this whole block used to do that job instead, but it held
      // ItemSpacing at zero for the ENTIRE draw of each bottom panel, not
      // just its trailing gap - which also zeroed it for every popup/menu
      // opened *inside* that panel (e.g. its right-click dock menu), and was
      // why those popups rendered with no padding only when bottom-docked.
      if (viewportBottom)
         DrawViewportPanelDocked("##viewportpanel_bottom", ImVec2(0, gViewportPanelHeight));
      if (matrixBottom)
         DrawModMatrixDocked("##modmatrix_bottom", ImVec2(0, gModMatrixHeight));
      if (perfBottom)
         DrawPerfPanelDocked("##perfpanel_bottom", ImVec2(0, gPerfPanelHeight));
      if (arrangeBottom)
         DrawArrangePanelDocked("##arrangepanel_bottom", ImVec2(0, gArrangePanelHeight));

      ImGui::End();

      // ---- windows that must live outside the node canvas ----
      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         // guard against the node being deleted while its editor is open
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFormulaEditor)
               alive = true;
         }
         if (!alive)
         {
            gFormulaEditor = nullptr;
            gFormulaEditorOpen = false;
         }
      }

      if (gFormulaEditorOpen && gFormulaEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Formula editor"), &gFormulaEditorOpen))
         {
            ImGui::TextDisabled("%s", T("body of  vec4 shape(vec2 uv, vec2 p, float t)"));
            ImGui::TextDisabled("%s", T("p is centred (-0.5..0.5), t is transport seconds, uA-uD are the knobs"));
            ImGui::Separator();

            static char editBuf[8192];
            static FormulaNode* lastEdited = nullptr;
            if (lastEdited != gFormulaEditor)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFormulaEditor->formula.c_str());
               lastEdited = gFormulaEditor;
            }

            ImGui::InputTextMultiline("##glsl", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 70));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               gFormulaEditor->formula = editBuf;
               gFormulaEditor->Apply();
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFormulaEditor->formula.c_str());

            if (!gFormulaEditor->LastError().empty())
            {
               ImGui::TextWrapped("%s", gFormulaEditor->LastError().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      if (gFieldElementEditorOpen && gFieldElementEditor != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFieldElementEditor)
               alive = true;
         }
         if (!alive)
         {
            gFieldElementEditor = nullptr;
            gFieldElementEditorOpen = false;
         }
      }

      if (gFieldElementEditorOpen && gFieldElementEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Field element editor"), &gFieldElementEditorOpen))
         {
            ImGui::TextDisabled("%s", T("Field element-domain kernel (per-vertex). Reserved: P (vec3), N (vec3), uv (vec2), Cd (vec3), i, count, t"));
            ImGui::TextDisabled("%s", T("User attributes: 'attrib float heat = 0'. Frame rate expressions are automatically hoisted."));
            ImGui::Separator();

            // gCurrentNodeIndex is -1 here (EndNodeParams() reset it once the
            // node canvas finished drawing for this frame - this window draws
            // after that). DrawFieldDeviceControls captures gCurrentNodeIndex
            // into its deferred onSelect closure, so it must be pointed at
            // this editor's own node for the duration of the call, using the
            // index the node cached the last time its compact body drew
            // (NodeT::SetNodeIndex, see DrawFieldElementParams above).
            gCurrentNodeIndex = gFieldElementEditor->NodeIndex();
            DrawFieldDeviceControls<FieldElementNode>(gFieldElementEditor, "element", &FieldElementNode::PresetNames(),
                                                      [](FieldElementNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });
            gCurrentNodeIndex = -1;

            static char editBuf[8192];
            static FieldElementNode* lastEdited = nullptr;
            static std::string lastKnownCode;
            if (lastEdited != gFieldElementEditor || gFieldElementEditor->code != lastKnownCode)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldElementEditor->code.c_str());
               lastEdited = gFieldElementEditor;
               lastKnownCode = gFieldElementEditor->code;
            }

            ImGui::InputTextMultiline("##fieldCode", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 35));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               gFieldElementEditor->code = editBuf;
               gFieldElementEditor->Apply();
               lastKnownCode = gFieldElementEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldElementEditor->code.c_str());

            if (!gFieldElementEditor->LastError().empty())
            {
               ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gFieldElementEditor->LastError().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      if (gFieldPrimitiveEditorOpen && gFieldPrimitiveEditor != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFieldPrimitiveEditor)
               alive = true;
         }
         if (!alive)
         {
            gFieldPrimitiveEditor = nullptr;
            gFieldPrimitiveEditorOpen = false;
         }
      }

      if (gFieldPrimitiveEditorOpen && gFieldPrimitiveEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Field primitive editor"), &gFieldPrimitiveEditorOpen))
         {
            ImGui::TextDisabled("%s", T("Field primitive generator (from scratch). Reserved: P (vec3), N (vec3), uv (vec2), Cd (vec3), i, count, t"));
            ImGui::TextDisabled("%s", T("Pure 3D geometry generator. Frame rate expressions are automatically hoisted."));
            ImGui::Separator();

            gCurrentNodeIndex = gFieldPrimitiveEditor->NodeIndex();
            DrawFieldDeviceControls<FieldPrimitiveNode>(gFieldPrimitiveEditor, "primitive", &FieldPrimitiveNode::PresetNames(),
                                                        [](FieldPrimitiveNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });
            gCurrentNodeIndex = -1;

            static char editBuf[8192];
            static FieldPrimitiveNode* lastEdited = nullptr;
            static std::string lastKnownCode;
            if (lastEdited != gFieldPrimitiveEditor || gFieldPrimitiveEditor->code != lastKnownCode)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldPrimitiveEditor->code.c_str());
               lastEdited = gFieldPrimitiveEditor;
               lastKnownCode = gFieldPrimitiveEditor->code;
            }

            ImGui::InputTextMultiline("##fieldPrimitiveCode", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 35));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               gFieldPrimitiveEditor->code = editBuf;
               gFieldPrimitiveEditor->Apply();
               lastKnownCode = gFieldPrimitiveEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldPrimitiveEditor->code.c_str());

            if (!gFieldPrimitiveEditor->LastError().empty())
            {
               ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gFieldPrimitiveEditor->LastError().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      if (gFieldPixelEditorOpen && gFieldPixelEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Field pixel editor"), &gFieldPixelEditorOpen))
         {
            ImGui::TextDisabled("%s", T("Field pixel-domain kernel (per-pixel fragment shader)."));
            ImGui::TextDisabled("%s", T("Reserved: uv (vec2), xy (vec2), res (vec2), aspect, col (vec3), alpha, t, dt, frame"));
            ImGui::Separator();

            gCurrentNodeIndex = gFieldPixelEditor->NodeIndex();
            DrawFieldDeviceControls<FieldPixelNode>(gFieldPixelEditor, "pixel", &FieldPixelNode::PresetNames(),
                                                    [](FieldPixelNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });
            gCurrentNodeIndex = -1;

            static char editBuf[8192];
            static FieldPixelNode* lastEdited = nullptr;
            static std::string lastKnownCode;
            if (lastEdited != gFieldPixelEditor || gFieldPixelEditor->code != lastKnownCode)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldPixelEditor->code.c_str());
               lastEdited = gFieldPixelEditor;
               lastKnownCode = gFieldPixelEditor->code;
            }

            ImGui::InputTextMultiline("##fieldPixelCode", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 35));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               gFieldPixelEditor->code = editBuf;
               gFieldPixelEditor->Apply();
               lastKnownCode = gFieldPixelEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldPixelEditor->code.c_str());

            if (!gFieldPixelEditor->LastError().empty())
            {
               ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gFieldPixelEditor->LastError().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      if (gFieldSampleEditorOpen && gFieldSampleEditor != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFieldSampleEditor)
               alive = true;
         }
         if (!alive)
         {
            gFieldSampleEditor = nullptr;
            gFieldSampleEditorOpen = false;
         }
      }

      if (gFieldSampleEditorOpen && gFieldSampleEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Field effect editor"), &gFieldSampleEditorOpen))
         {
            ImGui::TextDisabled("%s", T("Field effect kernel (per-sample, per-voice, audio thread). Reserved: in, sr, n, out"));
            ImGui::TextDisabled("%s", T("'state float x = 0' declares per-voice memory (resets on note-on/steal). 'param float p = 0..1' exposes a modulatable knob."));
            ImGui::Separator();

            gCurrentNodeIndex = gFieldSampleEditor->NodeIndex();
            DrawFieldDeviceControls<FieldSampleNode>(gFieldSampleEditor, "sample", &FieldSampleNode::PresetNames(),
                                                     [](FieldSampleNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });
            gCurrentNodeIndex = -1;

            static char editBuf[8192];
            static FieldSampleNode* lastEdited = nullptr;
            static std::string lastKnownCode;
            if (lastEdited != gFieldSampleEditor || gFieldSampleEditor->code != lastKnownCode)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldSampleEditor->code.c_str());
               lastEdited = gFieldSampleEditor;
               lastKnownCode = gFieldSampleEditor->code;
            }

            ImGui::InputTextMultiline("##fieldSampleCode", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 35));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               gFieldSampleEditor->code = editBuf;
               gFieldSampleEditor->Apply();
               lastKnownCode = gFieldSampleEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldSampleEditor->code.c_str());

            if (!gFieldSampleEditor->LastError().empty())
            {
               ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gFieldSampleEditor->LastError().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      if (gFieldSynthEditorOpen && gFieldSynthEditor != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFieldSynthEditor)
               alive = true;
         }
         if (!alive)
         {
            gFieldSynthEditor = nullptr;
            gFieldSynthEditorOpen = false;
         }
      }

      if (gFieldSynthEditorOpen && gFieldSynthEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Field synth editor"), &gFieldSynthEditorOpen))
         {
            ImGui::TextDisabled("%s", T("Field polyphonic synth kernel (per-sample, per-voice, audio thread). Reserved: in, sr, n, freq, gate, out"));
            ImGui::TextDisabled("%s", T("'state float x = 0' declares per-voice memory (resets on note-on/steal). 'param float p = 0..1' exposes a modulatable knob."));
            ImGui::Separator();

            gCurrentNodeIndex = gFieldSynthEditor->NodeIndex();
            DrawFieldDeviceControls<FieldSynthNode>(gFieldSynthEditor, "synth", &FieldSynthNode::PresetNames(),
                                                    [](FieldSynthNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });
            gCurrentNodeIndex = -1;

            static char editBuf[8192];
            static FieldSynthNode* lastEdited = nullptr;
            static std::string lastKnownCode;
            if (lastEdited != gFieldSynthEditor || gFieldSynthEditor->code != lastKnownCode)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldSynthEditor->code.c_str());
               lastEdited = gFieldSynthEditor;
               lastKnownCode = gFieldSynthEditor->code;
            }

            ImGui::InputTextMultiline("##fieldSynthCode", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 35));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               gFieldSynthEditor->code = editBuf;
               gFieldSynthEditor->Apply();
               lastKnownCode = gFieldSynthEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldSynthEditor->code.c_str());

            if (!gFieldSynthEditor->LastError().empty())
            {
               ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gFieldSynthEditor->LastError().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      if (gFieldGraphEditorOpen && gFieldGraphEditor != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gFieldGraphEditor)
               alive = true;
         }
         if (!alive)
         {
            gFieldGraphEditor = nullptr;
            gFieldGraphEditorOpen = false;
         }
      }

      if (gFieldGraphEditorOpen && gFieldGraphEditor != nullptr)
      {
         ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
         PushElevatedPanelStyle(/*isChild=*/false);
         if (ImGui::Begin(L("Field graph editor"), &gFieldGraphEditorOpen))
         {
            ImGui::TextDisabled("%s", T("Field graph-domain kernel (edit-time, runs once). emit(\"Type Name\", k0, k1, ...) -> handle"));
            ImGui::TextDisabled("%s", T("connect(src, srcSlot, dst, dstSlot)   set(handle, \"paramName\", value)   place(handle, x, y)"));
            ImGui::Separator();

            gCurrentNodeIndex = gFieldGraphEditor->NodeIndex();
            DrawFieldDeviceControls<FieldGraphNode>(gFieldGraphEditor, "graph", &FieldGraphNode::PresetNames(),
                                                    [](FieldGraphNode* n2, int i) { n2->presetIndex = i; n2->LoadPreset(i); });
            gCurrentNodeIndex = -1;

            static char editBuf[8192];
            static FieldGraphNode* lastEdited = nullptr;
            static std::string lastKnownCode;
            if (lastEdited != gFieldGraphEditor || gFieldGraphEditor->code != lastKnownCode)
            {
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldGraphEditor->code.c_str());
               lastEdited = gFieldGraphEditor;
               lastKnownCode = gFieldGraphEditor->code;
            }

            ImGui::InputTextMultiline("##fieldGraphCode", editBuf, sizeof(editBuf),
                                      ImVec2(-1, ImGui::GetContentRegionAvail().y - 35));

            PushPrimaryButtonStyle();
            if (ImGui::Button(L("Apply"), ImVec2(120, 0)))
            {
               // Compile-only (T11): never mutates the real graph on its own -
               // see FieldGraphNode::Apply()'s doc comment. Regenerate (below)
               // is the only path that does.
               gFieldGraphEditor->code = editBuf;
               gFieldGraphEditor->Apply();
               lastKnownCode = gFieldGraphEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(L("Revert"), ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldGraphEditor->code.c_str());
            ImGui::SameLine();
            if (ImGui::Button(L("Regenerate"), ImVec2(120, 0)))
            {
               // Safe to call directly (not deferred) here: this window draws
               // after ed::End() has already returned for the frame, unlike
               // DrawFieldGraphParams' Regenerate button (see trap T14 there).
               gFieldGraphEditor->code = editBuf;
               RunFieldGraphRegenerate(gFieldGraphEditor);
            }

            if (!gFieldGraphEditor->LastError().empty())
            {
               ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", gFieldGraphEditor->LastError().c_str());
            }
            if (!gFieldGraphEditor->Notice().empty())
            {
               ImGui::TextColored(tok::V4(tok::palf::v_1000_700_200_1000), "%s", gFieldGraphEditor->Notice().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }}
}

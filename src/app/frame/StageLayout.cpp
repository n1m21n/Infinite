// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawLayout(FrameCtx& fc)
{
   auto& frameId = fc.frameId;
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
   auto& dropInsideArrangePanel = fc.dropInsideArrangePanel;


      // The canvas zoomed wildly on a trackpad because it consumes raw wheel
      // deltas; damp them for the duration of the editor, then restore.
      ImGuiIO& io = ImGui::GetIO();
      savedWheel = io.MouseWheel;
      savedWheelH = io.MouseWheelH;
      io.MouseWheel *= gZoomSensitivity;
      io.MouseWheelH *= gZoomSensitivity;

      // Re-derive the node-editor canvas style (Bg/Grid/NodeBg/NodeBorder)
      // from the live theme every frame, the same reason glClearColor below
      // reads CurrentUiTheme() every frame instead of once: a preset switch
      // only reaches ed::Style through this call, and if that one call is
      // ever missed or lands before gEditor exists, the canvas is left
      // showing imgui-node-editor's own hardcoded constructor default -
      // Bg (60,60,70,200) - a flat, theme-independent grey that doesn't
      // match either preset. Cheap (a handful of style-table writes, no
      // allocation), so there is no reason to gate it behind the rare
      // preset-change event instead of just always being correct.
      ApplyTheme();
      ed::SetCurrentEditor(gEditor);

      // Dragging empty canvas should pan, but dragging a node should move it.
      // NavigateAction claims any drag on its configured button regardless of
      // what is underneath, so flip the button per-gesture using last frame's
      // hover state. Shift+drag still gives a rubber-band selection.
      //
      // Any floating top-level window (Settings, a Field editor dialog,
      // Offline Render, the shortcuts/help windows, ...) drawn on top of the
      // canvas is a separate concern from the above: the node-editor's own
      // background hit-test in BuildControl (imgui_node_editor.cpp) uses
      // ImGuiHoveredFlags_RectOnly, which ignores window overlap entirely,
      // so a click on a floating window's title bar still reads as
      // "background click" to the canvas. Rather than lean on ImGui's own
      // (order-sensitive) window-hover resolution, test the mouse directly
      // against each such window's own last-known rect via FindWindowByName
      // - same-frame-accurate and independent of Begin() call order, so it
      // works even on the very next click right after a canvas pan.
      bool overFieldEditorWindow = false;
      {
         static const char* kFloatingWindowNames[] = {
            "Field element editor", "Field primitive editor", "Field pixel editor",
            "Field effect editor", "Field synth editor", "Field graph editor",
            "Settings", "All Shortcuts", "Infinite - help & module reference",
            "Offline Render"
         };
         const ImVec2 mp = ImGui::GetMousePos();
         for (const char* wname : kFloatingWindowNames)
         {
            ImGuiWindow* w = ImGui::FindWindowByName(I18n::L(wname));
            if (w != nullptr && w->WasActive)
            {
               ImRect r(w->Pos, w->Pos + w->Size);
               if (r.Contains(mp)) { overFieldEditorWindow = true; break; }
            }
         }
      }

      {
         ed::Config& liveCfg = const_cast<ed::Config&>(ed::GetConfig(gEditor));
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gHoveringItem &&
             !overFieldEditorWindow && !io.KeyShift)
            gPanWithLeft = true;
         if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            gPanWithLeft = false;
         liveCfg.NavigateButtonIndex = gPanWithLeft ? 0 : 1;
      }

      kNodePanelWidth = 360.0f;
      const bool viewportPanelOpen = gViewportPanelOpen;
      viewportBottom = viewportPanelOpen && gViewportPanelDock == 0;
      viewportRight = viewportPanelOpen && gViewportPanelDock == 1;
      const bool viewportLeft = viewportPanelOpen && gViewportPanelDock == 2;
      const bool viewportTop = viewportPanelOpen && gViewportPanelDock == 3;
      matrixBottom = gModMatrixOpen && gModMatrixDock == 0;
      matrixRight = gModMatrixOpen && gModMatrixDock == 1;
      const bool matrixLeft = gModMatrixOpen && gModMatrixDock == 2;
      const bool matrixTop = gModMatrixOpen && gModMatrixDock == 3;
      perfBottom = gPerfPanelOpen && gPerfPanelDock == 0;
      perfRight = gPerfPanelOpen && gPerfPanelDock == 1;
      const bool perfLeft = gPerfPanelOpen && gPerfPanelDock == 2;
      const bool perfTop = gPerfPanelOpen && gPerfPanelDock == 3;
      arrangeBottom = gArrangePanelOpen && ArrangePanelDock() == 0;
      arrangeRight = gArrangePanelOpen && ArrangePanelDock() == 1;
      const bool arrangeLeft = gArrangePanelOpen && ArrangePanelDock() == 2;
      const bool arrangeTop = gArrangePanelOpen && ArrangePanelDock() == 3;

      // Maintain keyboard focus states across docked panels and canvas regardless of dock order
      if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
          !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const bool inArrange = gArrangePanelOpen &&
            (m.x >= gArrangePanelRectMin.x && m.x <= gArrangePanelRectMax.x &&
             m.y >= gArrangePanelRectMin.y && m.y <= gArrangePanelRectMax.y);
         const bool inPerf = gPerfPanelOpen &&
            (m.x >= gPerfPanelRectMin.x && m.x <= gPerfPanelRectMax.x &&
             m.y >= gPerfPanelRectMin.y && m.y <= gPerfPanelRectMax.y);

         gArrangeClaimedKeys = inArrange;
         gArrangeFocused = inArrange;

         gPerfMatrixClaimedKeys = inPerf;
         gPerfMatrixFocused = inPerf && gPerfEditMode;
      }
      else
      {
         if (!gArrangePanelOpen)
         {
            gArrangeClaimedKeys = false;
            gArrangeFocused = false;
         }
         else
         {
            gArrangeFocused = gArrangeClaimedKeys;
         }

         if (!gPerfPanelOpen)
         {
            gPerfMatrixClaimedKeys = false;
            gPerfMatrixFocused = false;
         }
         else
         {
            gPerfMatrixFocused = gPerfMatrixClaimedKeys && gPerfEditMode;
         }
      }

      // Drop the 3D render state of any node no longer in the panel. Done
      // here, at the top of the next frame, rather than at the moment its
      // card was closed: that card had already submitted its texture to that
      // frame's draw list, and destroying the FBO first would leave the draw
      // list blitting a deleted texture.
      for (auto it = gPanelViewports.begin(); it != gPanelViewports.end(); )
      {
         const bool stillShown = gViewportPanelOpen && (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(),
                                           it->first) != gViewportPanelNodes.end());
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
         // The panels compete for the same room, so each panel's clamp
         // subtracts the other panels' current footprint when they'd otherwise
         // share a row/column - a same-side or same-row pair (e.g. both
         // right-docked) still fits because the draw order below chains
         // them with SameLine rather than overlapping.
         const bool matrixHorizontal = gModMatrixOpen && (gModMatrixDock == 1 || gModMatrixDock == 2);
         const bool matrixVertical = gModMatrixOpen && (gModMatrixDock == 0 || gModMatrixDock == 3);
         const bool perfHorizontal = gPerfPanelOpen && (gPerfPanelDock == 1 || gPerfPanelDock == 2);
         const bool perfVertical = gPerfPanelOpen && (gPerfPanelDock == 0 || gPerfPanelDock == 3);
         const bool viewportHorizontal = viewportPanelOpen && (gViewportPanelDock == 1 || gViewportPanelDock == 2);
         const bool viewportVertical = viewportPanelOpen && (gViewportPanelDock == 0 || gViewportPanelDock == 3);
         const bool arrangeHorizontal = gArrangePanelOpen && (ArrangePanelDock() == 1 || ArrangePanelDock() == 2);
         const bool arrangeVertical = gArrangePanelOpen && (ArrangePanelDock() == 0 || ArrangePanelDock() == 3);

         const float maxHeight = std::max(kViewportPanelMinHeight,
                                          room.y - 150.0f - (matrixVertical ? gModMatrixHeight : 0.0f)
                                                          - (perfVertical ? gPerfPanelHeight : 0.0f)
                                                          - (arrangeVertical ? gArrangePanelHeight : 0.0f));
         const float maxWidth = std::max(kViewportPanelMinWidth,
                                         room.x - 200.0f - (gNodePanelOpen ? kNodePanelWidth : 0.0f) -
                                         (matrixHorizontal ? gModMatrixWidth : 0.0f) -
                                         (perfHorizontal ? gPerfPanelWidth : 0.0f) -
                                         (arrangeHorizontal ? gArrangePanelWidth : 0.0f));
         gViewportPanelHeight = std::min(std::max(gViewportPanelHeight, kViewportPanelMinHeight), maxHeight);
         gViewportPanelWidth = std::min(std::max(gViewportPanelWidth, kViewportPanelMinWidth), maxWidth);

         const float maxMatrixHeight = std::max(kModMatrixMinHeight,
                                                room.y - 150.0f - (viewportVertical ? gViewportPanelHeight : 0.0f)
                                                                - (perfVertical ? gPerfPanelHeight : 0.0f)
                                                                - (arrangeVertical ? gArrangePanelHeight : 0.0f));
         const float maxMatrixWidth = std::max(kModMatrixMinWidth,
                                               room.x - 200.0f - (gNodePanelOpen ? kNodePanelWidth : 0.0f) -
                                               (viewportHorizontal ? gViewportPanelWidth : 0.0f) -
                                               (perfHorizontal ? gPerfPanelWidth : 0.0f) -
                                               (arrangeHorizontal ? gArrangePanelWidth : 0.0f));
         gModMatrixHeight = std::min(std::max(gModMatrixHeight, kModMatrixMinHeight), maxMatrixHeight);
         gModMatrixWidth = std::min(std::max(gModMatrixWidth, kModMatrixMinWidth), maxMatrixWidth);

         const float maxPerfHeight = std::max(kPerfPanelMinHeight,
                                              room.y - 150.0f - (viewportVertical ? gViewportPanelHeight : 0.0f)
                                                              - (matrixVertical ? gModMatrixHeight : 0.0f)
                                                              - (arrangeVertical ? gArrangePanelHeight : 0.0f));
         const float maxPerfWidth = std::max(kPerfPanelMinWidth,
                                             room.x - 200.0f - (gNodePanelOpen ? kNodePanelWidth : 0.0f) -
                                             (viewportHorizontal ? gViewportPanelWidth : 0.0f) -
                                             (matrixHorizontal ? gModMatrixWidth : 0.0f) -
                                             (arrangeHorizontal ? gArrangePanelWidth : 0.0f));
         gPerfPanelHeight = std::min(std::max(gPerfPanelHeight, kPerfPanelMinHeight), maxPerfHeight);
         gPerfPanelWidth = std::min(std::max(gPerfPanelWidth, kPerfPanelMinWidth), maxPerfWidth);

         const float maxArrangeHeight = std::max(kArrangePanelMinHeight,
                                                 room.y - 150.0f - (viewportVertical ? gViewportPanelHeight : 0.0f)
                                                                 - (matrixVertical ? gModMatrixHeight : 0.0f)
                                                                 - (perfVertical ? gPerfPanelHeight : 0.0f));
         const float maxArrangeWidth = std::max(kArrangePanelMinWidth,
                                                room.x - 200.0f - (gNodePanelOpen ? kNodePanelWidth : 0.0f) -
                                                (viewportHorizontal ? gViewportPanelWidth : 0.0f) -
                                                (matrixHorizontal ? gModMatrixWidth : 0.0f) -
                                                (perfHorizontal ? gPerfPanelWidth : 0.0f));
         gArrangePanelHeight = std::min(std::max(gArrangePanelHeight, kArrangePanelMinHeight), maxArrangeHeight);
         gArrangePanelWidth = std::min(std::max(gArrangePanelWidth, kArrangePanelMinWidth), maxArrangeWidth);
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
      // Each top/bottom-docked panel is its own row, so each costs one extra
      // ItemSpacing.y that a SameLine'd left/right dock never does.
      // Undercounting this is exactly what grows the shell window's own
      // scrollbar.
      float topBottom = 0.0f;
      int   topBottomRows = 0;
      if (viewportTop || viewportBottom) { topBottom += gViewportPanelHeight; topBottomRows++; }
      if (matrixTop   || matrixBottom)   { topBottom += gModMatrixHeight;     topBottomRows++; }
      if (perfTop     || perfBottom)     { topBottom += gPerfPanelHeight;     topBottomRows++; }
      if (arrangeTop  || arrangeBottom)  { topBottom += gArrangePanelHeight;  topBottomRows++; }
      // No ItemSpacing term: every docked panel is laid out flush (its
      // Draw*Docked zeroes the spacing around its own outer child, and the
      // SameLine chaining below passes an explicit 0 gap), so reserving a
      // row gap here would leave an unused windowBg strip at the bottom -
      // which is the same visible bar, just moved.
      (void)topBottomRows;
      graphHeight = std::max(150.0f,
         ImGui::GetContentRegionAvail().y - topBottom);

      // Top- and left-docked panels draw before the canvas: nothing else in
      // this window reserves space above or left of it, so each has to
      // consume its own room here, before the canvas cursor position (and
      // gGraphScreenTL below) reflect it.
      if (viewportTop)
         DrawViewportPanelDocked("##viewportpanel_top", ImVec2(0, gViewportPanelHeight));
      if (matrixTop)
         DrawModMatrixDocked("##modmatrix_top", ImVec2(0, gModMatrixHeight));
      if (perfTop)
         DrawPerfPanelDocked("##perfpanel_top", ImVec2(0, gPerfPanelHeight));
      if (arrangeTop)
         DrawArrangePanelDocked("##arrangepanel_top", ImVec2(0, gArrangePanelHeight));
      if (viewportLeft)
      {
         DrawViewportPanelDocked("##viewportpanel_left", ImVec2(gViewportPanelWidth, graphHeight));
         ImGui::SameLine(0.0f, 0.0f);
      }
      if (matrixLeft)
      {
         DrawModMatrixDocked("##modmatrix_left", ImVec2(gModMatrixWidth, graphHeight));
         ImGui::SameLine(0.0f, 0.0f);
      }
      if (perfLeft)
      {
         DrawPerfPanelDocked("##perfpanel_left", ImVec2(gPerfPanelWidth, graphHeight));
         ImGui::SameLine(0.0f, 0.0f);
      }
      if (arrangeLeft)
      {
         DrawArrangePanelDocked("##arrangepanel_left", ImVec2(gArrangePanelWidth, graphHeight));
         ImGui::SameLine(0.0f, 0.0f);
      }

      // Cleared here rather than at the top of the frame: a top/left-docked
      // matrix panel draws before the graph below (see the comment above),
      // so it just read *last* frame's FrameParams above - clearing only
      // now, after that read and before the graph repopulates it, is what
      // lets that panel show a real (one-frame-stale) Value instead of "--"
      // every frame. A right/bottom-docked matrix draws after the graph and
      // is unaffected either way, since by then this frame's params are in.
      // B4 fixture: bind the LFO to the camera's "orbit" slider by name, from
      // last frame's registrations, just before they are cleared. Bind()
      // takes the UI's param index, which is draw order, not VisitParams order.
      if (frameId == 4 && sBenchB4LfoIdx >= 0)
      {
         bool bound = false;
         for (const ParamRef& ref : Modulation::Instance().FrameParams())
         {
            if (ref.nodeIndex == sBenchB4CamIdx && ref.name == "orbit")
            {
               Modulation::Instance().Bind(sBenchB4CamIdx, ref.paramIndex, sBenchB4LfoIdx, 0);
               bound = true;
               break;
            }
         }
         if (!bound)
            fprintf(stderr, "B4: camera orbit param not registered, camera will not move\n");
      }
      Modulation::Instance().ClearFrameParams();
      // The grab set is per frame too: a stale entry would freeze a param forever.
      gPredictorGrabsPrev.swap(gPredictorGrabs);
      gPredictorGrabs.clear();

      // One combined reservation for every right-docked panel, computed
      // together so ImGui's SameLine() chaining after ed::End() lays them out
      // side by side instead of one clipping the other or the two overlapping.
      float rightReserved = 0.0f;
      if (gNodePanelOpen) rightReserved += kNodePanelWidth;
      if (viewportRight) rightReserved += gViewportPanelWidth;
      if (matrixRight) rightReserved += gModMatrixWidth;
      if (perfRight) rightReserved += gPerfPanelWidth;
      if (arrangeRight) rightReserved += gArrangePanelWidth;
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
      if (gKbViewRestore)
      {
         ed::SetViewZoom(gKbSavedZoom);
         ed::SetViewScroll(gKbSavedScroll);
         gKbViewRestore = false;
      }
      if (gKbPan.x != 0.0f || gKbPan.y != 0.0f)
      {
         const ImVec2 sc = ed::GetViewScroll();
         ed::SetViewScroll(ImVec2(sc.x + gKbPan.x, sc.y + gKbPan.y));
         gKbPan = ImVec2(0.0f, 0.0f);
      }
      ed::Begin("graph", ImVec2(graphWidth, graphHeight));
      gParamPinScreenList.clear();
      // Shift-held movement is the sole trigger for gesture recording - see
      // GestureRecorder.h. Checked once per frame here (not per-widget) so
      // every param touched while Shift stays down joins the same session.
      // The clock itself only advances while Transport is playing, so
      // pausing (spacebar) freezes a looping recording in place instead of
      // letting it keep animating on wall-clock time - see AdvanceClock.
      GestureRecorder::Instance().AdvanceClock(ImGui::GetIO().DeltaTime, Transport::Instance().IsPlaying());
      GestureRecorder::Instance().BeginFrame(ImGui::GetIO().KeyShift, GesturePlaybackClock());
      gGlobalScaleTooltipHovered = false;

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

      // Dropping a file on the canvas spawns the matching source node, already
      // loaded, at the drop point. Skip entirely if the drop landed inside the
      // Arrange panel's own screen rect - that panel has its own row-level
      // drop handling (DrawArrangePanelContent) which is order-dependent on
      // dock side (it can run before OR after this block, depending on
      // Arrange::Settings::dockSide), so this canvas handler must not race it
      // for gDroppedFiles by consuming/clearing paths meant for the timeline.
      dropInsideArrangePanel = gDropPos.x >= gArrangePanelRectMin.x && gDropPos.x < gArrangePanelRectMax.x &&
         gDropPos.y >= gArrangePanelRectMin.y && gDropPos.y < gArrangePanelRectMax.y;}
}

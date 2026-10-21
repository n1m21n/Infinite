// Part of src/main.cpp, included there in order (one translation unit).
// minimap, save prompts, window title, projector windows

   void DrawMinimap()
   {
      if (!gMinimapEnabled || gNodes.empty())
         return;

      const float pad = 14.0f;
      const float w = gMinimapSize;
      const float h = gMinimapSize * 0.72f;

      ImVec2 origin;
      switch (gMinimapCorner)
      {
         case 0: origin = ImVec2(gGraphScreenTL.x + pad, gGraphScreenTL.y + pad); break;
         case 1: origin = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x - w - pad,
                                 gGraphScreenTL.y + pad); break;
         case 2: origin = ImVec2(gGraphScreenTL.x + pad,
                                 gGraphScreenTL.y + gGraphScreenSize.y - h - pad); break;
         default: origin = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x - w - pad,
                                  gGraphScreenTL.y + gGraphScreenSize.y - h - pad); break;
      }

      // World-space bounds of every node, in an assumed node footprint - the
      // editor does not expose node sizes outside BeginNode/EndNode, and a
      // minimap dot does not need to be pixel-accurate to be useful.
      //
      // Deliberately NOT including the current viewport in this box: before
      // the view has ever been fit to content (or after zooming way out) the
      // viewport can span a world area far larger than where the nodes
      // actually are, which would crush every node down into one corner to
      // make room for a mostly-empty viewport rectangle. The map fits the
      // content; the viewport indicator below is clipped to the panel
      // instead, the same way a game minimap lets the camera frustum run off
      // the edge rather than rescaling the whole map to contain it.
      const float kNodeW = 260.0f, kNodeH = 90.0f;
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      for (const GraphNode& gn : gNodes)
      {
         minX = std::min(minX, gn.liveX); maxX = std::max(maxX, gn.liveX + kNodeW);
         minY = std::min(minY, gn.liveY); maxY = std::max(maxY, gn.liveY + kNodeH);
      }

      const ImVec2 viewWorldTL = ed::ScreenToCanvas(gGraphScreenTL);
      const ImVec2 viewWorldBR = ed::ScreenToCanvas(
         ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));

      const float worldW = std::max(1.0f, maxX - minX);
      const float worldH = std::max(1.0f, maxY - minY);
      const float margin = 10.0f;
      // Uniform, not per-axis: a per-axis fit would stretch node rectangles
      // and make the layout unrecognisable against the real canvas.
      const float scale = std::min((w - margin * 2.0f) / worldW, (h - margin * 2.0f) / worldH);

      auto toMinimap = [&](ImVec2 world) -> ImVec2
      {
         return ImVec2(origin.x + margin + (world.x - minX) * scale,
                       origin.y + margin + (world.y - minY) * scale);
      };

      const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
      const bool isLight = (0.2126f * t.windowBg.r + 0.7152f * t.windowBg.g + 0.0722f * t.windowBg.b > 0.5f);

      ImDrawList* dl = ImGui::GetForegroundDrawList();
      dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                        IM_COL32((int)(t.panelBg.r * 255), (int)(t.panelBg.g * 255), (int)(t.panelBg.b * 255), (int)(gMinimapOpacity * 255)), 6.0f);
      dl->AddRect(origin, ImVec2(origin.x + w, origin.y + h),
                  IM_COL32((int)(t.border.r * 255), (int)(t.border.g * 255), (int)(t.border.b * 255), 200), 6.0f, 0, 1.5f);

      for (const GraphNode& gn : gNodes)
      {
         const ImVec2 a = toMinimap(ImVec2(gn.liveX, gn.liveY));
         const ImVec2 b = toMinimap(ImVec2(gn.liveX + kNodeW, gn.liveY + kNodeH));
         const bool selected = ed::IsNodeSelected(gn.NodeId());
         dl->AddRectFilled(a, b, selected ? IM_COL32(255, 190, 90, 230) : IM_COL32(110, 150, 210, 200), 2.0f);
      }

      // The current viewport, outlined - the one thing a static "map" view
      // gives you that scrolling around by feel does not. Clipped to the
      // panel: a viewport far outside the node bounds (unfit, or zoomed out
      // past every node) still draws as much of its edge as overlaps rather
      // than being allowed to warp the map's own scale.
      {
         dl->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
         const ImVec2 a = toMinimap(viewWorldTL);
         const ImVec2 b = toMinimap(viewWorldBR);
         dl->AddRect(a, b, isLight ? IM_COL32(40, 44, 55, 220) : IM_COL32(255, 255, 255, 220), 2.0f, 0, 1.5f);
         dl->PopClipRect();
      }

      // Click or drag inside the minimap to jump to whatever node is nearest
      // that point - there is no public API to pan to an arbitrary empty
      // spot, only to navigate to a node, so this snaps to the closest one.
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##minimap", ImVec2(w, h));
      if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         const ImVec2 mouse = ImGui::GetMousePos();
         const float worldX = minX + (mouse.x - origin.x - margin) / scale;
         const float worldY = minY + (mouse.y - origin.y - margin) / scale;

         const GraphNode* nearest = nullptr;
         float bestDist = 1e18f;
         for (const GraphNode& gn : gNodes)
         {
            const float dx = (gn.liveX + kNodeW * 0.5f) - worldX;
            const float dy = (gn.liveY + kNodeH * 0.5f) - worldY;
            const float dist = dx * dx + dy * dy;
            if (dist < bestDist) { bestDist = dist; nearest = &gn; }
         }
         if (nearest != nullptr)
         {
            ed::SelectNode(nearest->NodeId());
            ed::NavigateToSelection(false, 0.0f);
         }
      }
   }

   // `afterSaved` runs once the patch was written (right away, or when the
   // Save As dialog - which no longer blocks the frame - returns a path).
   void SavePatchInteractive(bool forceDialog, std::function<void()> afterSaved = {}, bool collectMedia = false)
   {
      if (!gPatchPath.empty() && !forceDialog)
      {
         if (SavePatchTo(gPatchPath) && afterSaved)
            afterSaved();
         return;
      }
      std::string suggested = "Untitled.inf";
      if (!gPatchPath.empty())
      {
         const size_t slash = gPatchPath.find_last_of("/\\");
         suggested = (slash == std::string::npos) ? gPatchPath : gPatchPath.substr(slash + 1);
      }
      StartFileDialog([suggested] { return Platform::SavePatchDialog(suggested); },
                      [afterSaved, collectMedia](const std::string& chosen) {
                         // Same .inf extension and text format as upstream Infinite.
                         std::string path = chosen;
                         std::string lower = path;
                         std::transform(lower.begin(), lower.end(), lower.begin(),
                                        [](unsigned char c) { return (char)std::tolower(c); });
                         if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".inf") != 0)
                            path += ".inf";
                         if (SavePatchTo(path, collectMedia) && afterSaved)
                            afterSaved();
                      });
   }

   // Set for one frame when an action was deferred because the patch has
   // unsaved changes, so the UI pass knows to pop the confirmation modal.
   bool gShowUnsavedChangesModal = false;
   // The action to run once the "Unsaved Changes" modal is resolved with
   // something other than Cancel (Save or Don't Save).
   std::function<void()> gPendingUnsavedAction;
   // Set by the modal's Save once the (possibly asynchronous) save finished.
   bool gUnsavedSaveDone = false;

   // The single gate every action that would discard the current patch
   // (New, Open, Open Recent, drag-drop, close/quit) should route through:
   // runs `action` immediately if the patch has no unsaved changes,
   // otherwise defers it behind the "Unsaved Changes" modal (Save / Don't
   // Save / Cancel) and runs it only once the user resolves that prompt
   // with something other than Cancel. Dev-harness exits
   // (INFINITE_EXITAFTER, selftest modes, screenshot mode) call
   // glfwSetWindowShouldClose directly and deliberately skip this - a
   // scripted run should never block on a modal nobody can see.
   void GuardUnsavedChanges(std::function<void()> action)
   {
      if (!gPatchDirty)
      {
         action();
         return;
      }
      gPendingUnsavedAction = std::move(action);
      gShowUnsavedChangesModal = true;
   }

   // The single gate every real close path (Quit menu, Cmd+Q, red button)
   // routes through.
   void RequestClose(GLFWwindow* window)
   {
      // GLFW already set shouldClose before invoking the close callback
      // that leads here (see _glfwInputWindowCloseRequest) - undo that so
      // the app stays open until we know whether it's safe to close.
      glfwSetWindowShouldClose(window, GLFW_FALSE);
      GuardUnsavedChanges([window]() { glfwSetWindowShouldClose(window, GLFW_TRUE); });
   }

   void UpdateMainWindowTitle(GLFWwindow* window)
   {
      std::string patchName = "Untitled";
      if (!gPatchPath.empty())
      {
         const size_t slash = gPatchPath.find_last_of("/\\");
         patchName = slash == std::string::npos ? gPatchPath : gPatchPath.substr(slash + 1);
      }
      std::string title = patchName + (gPatchDirty ? "* - " INFINITE_TURBO_TITLE : " - " INFINITE_TURBO_TITLE);
      static std::string previous;
      if (title != previous)
      {
         glfwSetWindowTitle(window, title.c_str());
         previous = std::move(title);
      }
   }

   ProjectorWindow* FindProjectorWindow(int nodeIndex)
   {
      for (ProjectorWindow& pw : gProjectorWindows)
         if (pw.nodeIndex == nodeIndex)
            return &pw;
      return nullptr;
   }

   int ProjectorMonitorIndex(GLFWwindow* window)
   {
      int count = 0;
      GLFWmonitor** monitors = glfwGetMonitors(&count);
      if (monitors == nullptr || count <= 0)
         return -1;

      int wx = 0, wy = 0, ww = 0, wh = 0;
      glfwGetWindowPos(window, &wx, &wy);
      glfwGetWindowSize(window, &ww, &wh);
      int best = 0;
      long long bestArea = -1;
      for (int i = 0; i < count; i++)
      {
         int mx = 0, my = 0, mw = 0, mh = 0;
         glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
         const int overlapW = std::max(0, std::min(wx + ww, mx + mw) - std::max(wx, mx));
         const int overlapH = std::max(0, std::min(wy + wh, my + mh) - std::max(wy, my));
         const long long area = (long long)overlapW * overlapH;
         if (area > bestArea)
         {
            bestArea = area;
            best = i;
         }
      }
      return best;
   }

   void SetProjectorFullscreen(ProjectorWindow& pw, int monitorIndex, bool fullscreen)
   {
      int count = 0;
      GLFWmonitor** monitors = glfwGetMonitors(&count);
      if (monitors == nullptr || count <= 0)
         return;
      monitorIndex = std::clamp(monitorIndex, 0, count - 1);

      if (fullscreen)
      {
         if (!pw.fullscreen)
         {
            glfwGetWindowPos(pw.window, &pw.windowedX, &pw.windowedY);
            glfwGetWindowSize(pw.window, &pw.windowedW, &pw.windowedH);
         }
         const GLFWvidmode* mode = glfwGetVideoMode(monitors[monitorIndex]);
         if (mode == nullptr)
            return;
         int mx = 0, my = 0;
         glfwGetMonitorPos(monitors[monitorIndex], &mx, &my);
         // Borderless-window fullscreen is intentional here. Exclusive GLFW
         // fullscreen is allowed to minimise or drop behind the editor when
         // focus moves to monitor 1. A Win32 topmost window remains visible
         // on the projector and SWP_NOACTIVATE lets the editor keep focus.
         glfwSetWindowPos(pw.window, mx, my);
         glfwSetWindowSize(pw.window, mode->width, mode->height);
         Platform::ConfigureOutputWindow(pw.window, true, true, true);
         pw.fullscreen = true;
         pw.monitorIndex = monitorIndex;
      }
      else
      {
         int mx = 0, my = 0, mw = 0, mh = 0;
         glfwGetMonitorWorkarea(monitors[monitorIndex], &mx, &my, &mw, &mh);
         const int width = std::clamp(pw.windowedW, 320, std::max(320, mw));
         const int height = std::clamp(pw.windowedH, 180, std::max(180, mh));
         int x = pw.windowedX;
         int y = pw.windowedY;
         if (x + width < mx || x > mx + mw || y + height < my || y > my + mh)
         {
            x = mx + (mw - width) / 2;
            y = my + (mh - height) / 2;
         }
         Platform::ConfigureOutputWindow(pw.window, false, false, false);
         glfwSetWindowPos(pw.window, x, y);
         glfwSetWindowSize(pw.window, width, height);
         pw.fullscreen = false;
         pw.monitorIndex = monitorIndex;
      }
      glfwShowWindow(pw.window);
      RuntimeLog::Write("output window mode: monitor=%d fullscreen=%s", monitorIndex,
                        pw.fullscreen ? "yes" : "no");
   }

   void MoveProjectorToMonitor(ProjectorWindow& pw, int monitorIndex)
   {
      if (pw.fullscreen)
      {
         SetProjectorFullscreen(pw, monitorIndex, true);
         return;
      }
      int count = 0;
      GLFWmonitor** monitors = glfwGetMonitors(&count);
      if (monitors == nullptr || monitorIndex < 0 || monitorIndex >= count)
         return;
      int mx = 0, my = 0, mw = 0, mh = 0;
      glfwGetMonitorWorkarea(monitors[monitorIndex], &mx, &my, &mw, &mh);
      int ww = 0, wh = 0;
      glfwGetWindowSize(pw.window, &ww, &wh);
      ww = std::min(ww, mw);
      wh = std::min(wh, mh);
      glfwSetWindowPos(pw.window, mx + (mw - ww) / 2, my + (mh - wh) / 2);
      pw.monitorIndex = monitorIndex;
   }

   // Turbo: sizes the window's drawable area to exactly w x h pixels (the
   // framebuffer, not the outer frame), leaving fullscreen first if needed.
   // GLFW sizes windows in screen coordinates; the framebuffer/window ratio
   // converts, so this stays exact under any Windows display scaling.
   void SetProjectorPixelSize(ProjectorWindow& pw, int w, int h)
   {
      w = std::clamp(w, 16, 16384);
      h = std::clamp(h, 16, 16384);
      if (pw.fullscreen)
         SetProjectorFullscreen(pw, std::max(0, pw.monitorIndex), false);
      int ww = 0, wh = 0, fw = 0, fh = 0;
      glfwGetWindowSize(pw.window, &ww, &wh);
      glfwGetFramebufferSize(pw.window, &fw, &fh);
      const double sx = (ww > 0 && fw > 0) ? (double)fw / (double)ww : 1.0;
      const double sy = (wh > 0 && fh > 0) ? (double)fh / (double)wh : 1.0;
      const int targetW = std::max(1, (int)std::lround(w / sx));
      const int targetH = std::max(1, (int)std::lround(h / sy));
      glfwSetWindowSize(pw.window, targetW, targetH);
      pw.windowedW = targetW;
      pw.windowedH = targetH;
      pw.pixelW = w;
      pw.pixelH = h;
      RuntimeLog::Write("output window size set to %dx%d px", w, h);
   }

   void ToggleProjectorFullscreen(ProjectorWindow& pw)
   {
      // On entry, use the monitor that contains the window now. `monitorIndex`
      // describes where it was created or last moved through our menu and is
      // stale after the user drags the window with the OS title bar.
      const int monitor = pw.fullscreen
         ? std::max(0, pw.monitorIndex)
         : std::max(0, ProjectorMonitorIndex(pw.window));
      SetProjectorFullscreen(pw, std::max(0, monitor), !pw.fullscreen);
   }

   // Turbo: borderless fullscreen for the editor itself, on the monitor that
   // holds it. GLFW's own decorated attribute is used (not a raw Win32 style
   // change) so GLFW keeps its state consistent; no display mode change.
   void SetMainWindowFullscreen(bool on)
   {
      if (gMainWindow == nullptr || on == gMainFullscreen)
         return;
      if (on)
      {
         int count = 0;
         GLFWmonitor** monitors = glfwGetMonitors(&count);
         if (monitors == nullptr || count <= 0)
            return;
         const int mi = std::clamp(ProjectorMonitorIndex(gMainWindow), 0, count - 1);
         const GLFWvidmode* mode = glfwGetVideoMode(monitors[mi]);
         if (mode == nullptr)
            return;
         gMainWasMaximized = glfwGetWindowAttrib(gMainWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
         if (gMainWasMaximized)
            glfwRestoreWindow(gMainWindow);
         glfwGetWindowPos(gMainWindow, &gMainWindowedX, &gMainWindowedY);
         glfwGetWindowSize(gMainWindow, &gMainWindowedW, &gMainWindowedH);
         int mx = 0, my = 0;
         glfwGetMonitorPos(monitors[mi], &mx, &my);
         glfwSetWindowAttrib(gMainWindow, GLFW_DECORATED, GLFW_FALSE);
         glfwSetWindowPos(gMainWindow, mx, my);
         // One pixel taller than the monitor on purpose: a borderless window
         // that exactly covers a monitor gets promoted by the driver/DWM to
         // exclusive-style fullscreen presentation while it has focus, which
         // flickers against the (topmost, unsynced) output windows. The extra
         // row keeps it a normal composed window.
         glfwSetWindowSize(gMainWindow, mode->width, mode->height + 1);
         gMainFullscreen = true;
         RuntimeLog::Write("editor fullscreen on monitor %d (%dx%d)", mi, mode->width, mode->height);
      }
      else
      {
         glfwSetWindowAttrib(gMainWindow, GLFW_DECORATED, GLFW_TRUE);
         glfwSetWindowPos(gMainWindow, gMainWindowedX, gMainWindowedY);
         glfwSetWindowSize(gMainWindow, std::max(640, gMainWindowedW), std::max(400, gMainWindowedH));
         if (gMainWasMaximized)
            glfwMaximizeWindow(gMainWindow);
         gMainFullscreen = false;
         RuntimeLog::Write("editor fullscreen off");
      }
   }

   // Nodes whose output can sit behind the canvas: anything with a real
   // image (geometry nodes go through a Render 3D node first).
   bool CanShowBehindCanvas(const GraphNode& gn)
   {
      return CanShowInViewportPanel(gn) && dynamic_cast<IGeometrySource*>(gn.node.get()) == nullptr;
   }

   void ToggleCanvasBackground(int nodeIndex)
   {
      gCanvasBgNodeIndex = (gCanvasBgNodeIndex == nodeIndex) ? -1 : nodeIndex;
   }

   int FirstSelectedImageNodeIndex()
   {
      if (gEditor == nullptr)
         return -1;
      ed::SetCurrentEditor(gEditor);
      const int count = ed::GetSelectedObjectCount();
      int found = -1;
      if (count > 0)
      {
         std::vector<ed::NodeId> sel(count);
         const int n = ed::GetSelectedNodes(sel.data(), count);
         for (int i = 0; i < n && found < 0; i++)
            if (GraphNode* gn = FindNodeByIndex((int)sel[i].Get() / GraphNode::kStride))
               if (CanShowBehindCanvas(*gn))
                  found = gn->index;
      }
      return found;
   }

   // Ctrl+Shift+B: preview on -> off; off -> the selected node.
   void ToggleCanvasBackgroundForSelection()
   {
      if (gCanvasBgNodeIndex >= 0)
      {
         gCanvasBgNodeIndex = -1;
         return;
      }
      gCanvasBgNodeIndex = FirstSelectedImageNodeIndex();
   }

   // Draws the preview into the canvas rect, under everything the node
   // editor draws afterwards (same draw list, earlier commands).
   bool DrawCanvasBackground(GraphNode& gn, ImVec2 tl, ImVec2 size)
   {
      if (!CanShowBehindCanvas(gn) || size.x <= 1.0f || size.y <= 1.0f)
         return false;
      const unsigned int tex = gn.node->GetOutputTexture();
      const int tw = gn.node->GetOutputWidth();
      const int th = gn.node->GetOutputHeight();
      if (tex == 0 || tw <= 0 || th <= 0)
         return false;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(tl.x + size.x, tl.y + size.y);
      dl->PushClipRect(tl, br, true);
      dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, 255));
      const float fbScale = ImGui::GetIO().DisplayFramebufferScale.x > 0.0f ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
      float w = size.x, h = size.y;
      switch (gCanvasBgFit)
      {
      case GLUtil::kFitPixel:
         w = (float)tw / fbScale;
         h = (float)th / fbScale;
         break;
      case GLUtil::kFitFill:
      {
         const float k = std::max(size.x / (float)tw, size.y / (float)th);
         w = (float)tw * k;
         h = (float)th * k;
         break;
      }
      case GLUtil::kFitStretch:
         break;
      default:
      {
         const float k = std::min(size.x / (float)tw, size.y / (float)th);
         w = (float)tw * k;
         h = (float)th * k;
         break;
      }
      }
      const ImVec2 a(std::floor(tl.x + (size.x - w) * 0.5f), std::floor(tl.y + (size.y - h) * 0.5f));
      dl->AddImage((ImTextureID)(intptr_t)tex, a, ImVec2(a.x + w, a.y + h), ImVec2(0, 1), ImVec2(1, 0));
      if (gCanvasBgDim > 0.0f)
         dl->AddRectFilled(tl, br, IM_COL32(0, 0, 0, (int)(std::clamp(gCanvasBgDim, 0.0f, 1.0f) * 255.0f)));
      dl->PopClipRect();
      return true;
   }

   // Settings shared by the VIEW menu.
   void DrawCanvasBackgroundSettings()
   {
      static const char* kFitNames[] = { "Fit (letterbox)", "Real pixels 1:1", "Fill (crop)", "Stretch" };
      ImGui::SetNextItemWidth(170);
      if (ImGui::BeginCombo("Scaling##canvasbg", kFitNames[std::clamp(gCanvasBgFit, 0, 3)]))
      {
         for (int m = 0; m < 4; m++)
            if (ImGui::Selectable(kFitNames[m], gCanvasBgFit == m))
               gCanvasBgFit = m;
         ImGui::EndCombo();
      }
      ImGui::SetNextItemWidth(170);
      ImGui::SliderFloat("Darken##canvasbg", &gCanvasBgDim, 0.0f, 0.9f, "%.2f");
      ImGui::SetNextItemWidth(170);
      ImGui::SliderFloat("Node opacity##canvasbg", &gCanvasBgNodeAlpha, 0.1f, 1.0f, "%.2f");
      ImGui::Checkbox("Grid over the preview##canvasbg", &gCanvasBgGrid);
   }

   void ProjectorKeyCallback(GLFWwindow* window, int key, int, int action, int)
   {
      if (action != GLFW_PRESS)
         return;
      for (ProjectorWindow& pw : gProjectorWindows)
      {
         if (pw.window != window)
            continue;
         if (key == GLFW_KEY_F11)
            ToggleProjectorFullscreen(pw);
         else if (key == GLFW_KEY_ESCAPE && pw.fullscreen)
            SetProjectorFullscreen(pw, std::max(0, pw.monitorIndex), false);
         return;
      }
   }

   // Closes and forgets one projector window by its position in gProjectorWindows.
   void CloseProjectorWindow(size_t i)
   {
      if (i >= gProjectorWindows.size())
         return;
      ProjectorWindow victim = gProjectorWindows[i];
      GLFWwindow* previous = glfwGetCurrentContext();
      glfwMakeContextCurrent(victim.window);
      // Drop this node's solo-render FBO too, if it had one (geometry nodes
      // only - see gProjectorViewports) while its owning GL context is still
      // current. The fullscreen quad VAO is context-local too.
      gProjectorViewports.erase(victim.nodeIndex);
      GLUtil::ForgetCurrentContextObjects();
      glfwMakeContextCurrent(previous == victim.window ? gMainWindow : previous);
      glfwDestroyWindow(victim.window);
      gProjectorWindows.erase(gProjectorWindows.begin() + (ptrdiff_t)i);
      RuntimeLog::Write("output window closed: node=%d", victim.nodeIndex);
   }

   // Closes the projector window showing this node index, if any is open.
   void CloseProjectorWindowFor(int nodeIndex)
   {
      for (size_t i = 0; i < gProjectorWindows.size(); i++)
         if (gProjectorWindows[i].nodeIndex == nodeIndex)
         {
            CloseProjectorWindow(i);
            return;
         }
   }

   void CloseAllProjectorWindows()
   {
      while (!gProjectorWindows.empty())
         CloseProjectorWindow(gProjectorWindows.size() - 1);
   }

   // Opens a decorated output window. F11 toggles fullscreen on its current
   // monitor; the node menu can move it directly to any connected display.
   void OpenProjectorWindow(GLFWwindow* mainWindow, GraphNode& gn)
   {
      // Already have one open for this node - nothing to do (the context menu
      // offers "Close window" instead once one exists).
      for (const ProjectorWindow& pw : gProjectorWindows)
         if (pw.nodeIndex == gn.index)
            return;

      int w = gn.node->GetOutputWidth();
      int h = gn.node->GetOutputHeight();
      if (w <= 0 || h <= 0)
      {
         w = 1280;
         h = 720;
      }
      // The output first opens as a manageable positioning window, not at the
      // source's potentially full 4K dimensions. F11 later uses the complete
      // monitor bounds without changing the monitor's display mode.
      const double fit = std::min({ 1.0, 960.0 / (double)w, 540.0 / (double)h });
      w = std::max(320, (int)std::lround((double)w * fit));
      h = std::max(180, (int)std::lround((double)h * fit));

      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, gGLContextMajor);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gGLContextMinor);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
      glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
      glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
      glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
      // Turbo 0.51 (upstream 8af69a1): alpha-capable framebuffer, so a node with a
      // transparent background can show the desktop through a windowed Output
      // (OBS window capture). Opaque output still writes alpha 1: unchanged.
      // Sticky hint, reset right after creation.
      glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
      GLFWwindow* projWindow = glfwCreateWindow(w, h, NodeTitleWithInstance(gn).c_str(), nullptr, mainWindow);
      glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
      if (projWindow == nullptr)
      {
         glfwMakeContextCurrent(mainWindow);
         return;
      }

      ProjectorWindow projector;
      projector.window = projWindow;
      projector.nodeIndex = gn.index;
      projector.windowedW = w;
      projector.windowedH = h;
      projector.monitorIndex = ProjectorMonitorIndex(mainWindow);
      projector.fitMode = gProjectorDefaultFitMode;
      gProjectorWindows.push_back(projector);
      glfwSetKeyCallback(projWindow, ProjectorKeyCallback);
      Platform::ConfigureOutputWindow(projWindow, false, false, false);
      int mainX = 0, mainY = 0;
      glfwGetWindowPos(mainWindow, &mainX, &mainY);
      glfwSetWindowPos(projWindow, mainX + 60, mainY + 60);
      gProjectorWindows.back().monitorIndex = ProjectorMonitorIndex(projWindow);
      glfwMakeContextCurrent(projWindow);
      // The main window owns VSync. Waiting for a second VSync on every
      // projector made a nominal 30fps render settle around 28-29fps.
      glfwSwapInterval(0);
      glfwMakeContextCurrent(mainWindow);
      RuntimeLog::Write("output window opened: node=%d %dx%d", gn.index, w, h);
   }

   // Turbo: the output-window controls, shown right in the parameter panel
   // of Output / Projection / Layout nodes (the node's right-click menu has
   // the same options once a window is open).
   void DrawOutputWindowPanel(GraphNode& gn)
   {
      ImGui::Dummy(ImVec2(0, 3));
      ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "OUTPUT WINDOW");
      if (CanShowBehindCanvas(gn))
      {
         const bool behind = gCanvasBgNodeIndex == gn.index;
         if (behind)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
         if (ImGui::Button(behind ? "BEHIND CANVAS: ON" : "SHOW BEHIND CANVAS", ImVec2(kPreviewSize, 0)))
            ToggleCanvasBackground(gn.index);
         if (behind)
            ImGui::PopStyleColor();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("draw this output under the node canvas (Ctrl+Shift+B)\nsettings in VIEW");
      }
      ProjectorWindow* pw = FindProjectorWindow(gn.index);
      if (pw == nullptr)
      {
         if (ImGui::Button("OPEN OUTPUT WINDOW", ImVec2(kPreviewSize, 0)))
            OpenProjectorWindow(gMainWindow, gn);
         return;
      }
      int fbW = 0, fbH = 0;
      glfwGetFramebufferSize(pw->window, &fbW, &fbH);
      ImGui::TextDisabled("window: %d x %d px%s", fbW, fbH, pw->fullscreen ? " (fullscreen)" : "");

      static std::map<int, std::pair<int, int>> sRequested;
      auto found = sRequested.find(gn.index);
      if (found == sRequested.end())
         found = sRequested.emplace(gn.index, std::make_pair(pw->pixelW > 0 ? pw->pixelW : fbW,
                                                             pw->pixelH > 0 ? pw->pixelH : fbH)).first;
      int& reqW = found->second.first;
      int& reqH = found->second.second;
      const float half = (kPreviewSize - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      ImGui::SetNextItemWidth(half);
      ImGui::InputInt("##outWinW", &reqW, 0, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(half);
      ImGui::InputInt("##outWinH", &reqH, 0, 0);
      if (ImGui::Button("APPLY SIZE (W x H px)", ImVec2(kPreviewSize, 0)))
         SetProjectorPixelSize(*pw, reqW, reqH);

      static const std::vector<std::string> kPresetNames = { "preset...", "1920 x 1080", "1280 x 720", "3840 x 2160",
                                                             "2560 x 1440", "1080 x 1920", "1080 x 1080",
                                                             "1024 x 768", "source size" };
      static const int kPresetW[] = { 0, 1920, 1280, 3840, 2560, 1080, 1080, 1024, -1 };
      static const int kPresetH[] = { 0, 1080, 720, 2160, 1440, 1920, 1080, 768, -1 };
      const int nodeIndex = gn.index;
      DropdownButton("##outWinPreset", kPresetNames, 0, [nodeIndex](int i) {
         ProjectorWindow* target = FindProjectorWindow(nodeIndex);
         GraphNode* g = FindNodeByIndex(nodeIndex);
         if (target == nullptr || g == nullptr || i <= 0)
            return;
         int w = kPresetW[i], h = kPresetH[i];
         if (w < 0)
         {
            w = g->node->GetOutputWidth();
            h = g->node->GetOutputHeight();
         }
         if (w > 0 && h > 0)
         {
            sRequested[nodeIndex] = std::make_pair(w, h);
            SetProjectorPixelSize(*target, w, h);
         }
      }, kPreviewSize, /*modulatable=*/false);

      static const std::vector<std::string> kFitNames = { "fit (black bars)", "real pixels 1:1", "fill (crop)", "stretch" };
      DropdownButton("##outWinFit", kFitNames, pw->fitMode, [nodeIndex](int m) {
         if (ProjectorWindow* target = FindProjectorWindow(nodeIndex))
         {
            target->fitMode = m;
            gProjectorDefaultFitMode = m;
         }
      }, kPreviewSize, /*modulatable=*/false);

      if (ImGui::Button(pw->fullscreen ? "EXIT FULLSCREEN (F11)" : "FULLSCREEN (F11)", ImVec2(kPreviewSize, 0)))
         ToggleProjectorFullscreen(*pw);
      if (ImGui::Button("CLOSE OUTPUT WINDOW", ImVec2(kPreviewSize, 0)))
         CloseProjectorWindowFor(gn.index);
   }

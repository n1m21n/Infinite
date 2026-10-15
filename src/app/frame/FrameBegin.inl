// Part of src/main.cpp, included there in order (one translation unit).
// frame start: events, RPC, file watch, audio recovery, projectors

      gFrameStart = glfwGetTime();
      glfwPollEvents();
      // A hosted plugin editor that renders with OpenGL can paint inside
      // this message pump and leave its own GL context current (see
      // GlContextKeeper in PluginVST3Windows.cpp): take ours back.
      if (Platform::AnyPluginEditorOpen())
         glfwMakeContextCurrent(window);
      UpdateMainWindowTitle(window);

      // Apply any RemoteControl RPC requests queued by the network thread
      // since last frame, before any ed:: drawing reads the graph this frame.
      RpcRefreshParamCache();   // Turbo 0.45: names of the params drawn last frame
      RemoteControl::DrainPending(HandleRpcCommand);
      RpcPumpPendingBinds();    // modulation / expressions waiting for a node to draw
      PumpAutoLayout();         // nodes placed by the auto layout once they have a size
      RefreshLiveIssues();      // live validation hints (debounced)
      PollPatchFileWatch();     // Turbo 0.45: the open file changed on disk
      // Turbo 0.46: the Performance panel keeps the edit shortcuts only while
      // it was the last thing clicked.
      if (ImGui::GetCurrentContext() != nullptr &&
          (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
      {
         const ImVec2 m = ImGui::GetIO().MousePos;
         const bool inPerf = gPerfPanelOpen && m.x >= gPerfPanelRectMin.x && m.x <= gPerfPanelRectMax.x &&
                             m.y >= gPerfPanelRectMin.y && m.y <= gPerfPanelRectMax.y;
         if (!inPerf)
            gPerfMatrixClaimedKeys = false;
      }
      if (!gPerfPanelOpen)
         gPerfMatrixClaimedKeys = false;
      gPerfMatrixFocused = gPerfMatrixClaimedKeys && gPerfEditMode;

      std::string pendingOpenPatch;
      while (Platform::PollPendingOpenFile(pendingOpenPatch))
      {
         // Turbo 0.46: handed over by a second launch (Explorer double-click).
         // With unsaved edits here, ask first (banner) instead of replacing them.
         if (gPatchDirty)
            gHandoverPath = pendingOpenPatch;
         else
         {
            LoadPatchFrom(pendingOpenPatch);
            gRequestFitView = true;
         }
         if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
            glfwRestoreWindow(window);
         glfwFocusWindow(window);
      }

      // Device-change/sleep-wake self-healing (docs/plans/optimization/
      // prompts/02-device-change-and-wake-recovery.md) - once a frame, main
      // thread only.
      PollAudioRecovery();
      if (gAudioAutoStartPending && frameId >= 2)
      {
         gAudioAutoStartPending = false;
         if (AudioEngine::Instance().SampleRate() <= 0.0)
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
               fprintf(stderr, "audio auto-start: %s\n", gAudioStartError.c_str());
         }
      }

      // Per-frame audio housekeeping off the audio thread (currently just
      // freeing sample-preview buffers the audio thread has retired) - see
      // AudioEngine::PumpMainThread.
      AudioEngine::Instance().PumpMainThread();

      // Projector windows are ordinary decorated windows - closing one via the
      // OS's own close button only sets its should-close flag, it doesn't
      // destroy it. Poll for that here so it gets cleaned up the same way the
      // "Close window" menu item does. Iterate backwards since closing erases.
      for (size_t i = gProjectorWindows.size(); i-- > 0; )
         if (glfwWindowShouldClose(gProjectorWindows[i].window))
            CloseProjectorWindow(i);

      // Actually tear down anything retired by RemoveNodeByIndex last frame.
      // Safe here: the frame that queued draw commands referencing these
      // textures has already been submitted and presented.
      if (!gRetiredNodes.empty() &&
          (AudioEngine::Instance().SampleRate() <= 0.0 || AudioEngine::Instance().InProcess() == 0 ||
           AudioEngine::Instance().BlocksDone() >= gRetiredSafeAfterBlocks))
         gRetiredNodes.clear();
      gRetiredViewports.clear();

      // dev-only: drive copy/paste/delete with synthetic key events so the
      // shortcuts can be verified without a human at the keyboard
      if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         auto key = [&tio](ImGuiKey k, bool down) { tio.AddKeyEvent(k, down); };
         switch (frameId)
         {
            case 3: key(ImGuiMod_Super, true); key(ImGuiKey_C, true); break;
            case 4: key(ImGuiKey_C, false); break;
            case 5: key(ImGuiKey_V, true); break;
            case 6: key(ImGuiKey_V, false); key(ImGuiMod_Super, false); break;
            case 8: key(ImGuiKey_Backspace, true); break;
            case 9: key(ImGuiKey_Backspace, false); break;
            default: break;
         }
      }

      PumpUiRescale(window); // Turbo 0.46
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();

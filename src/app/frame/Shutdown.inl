// Part of src/main.cpp, included there in order (one translation unit).
// shutdown with watchdog

   // Turbo 0.53: shutdown watchdog. Quitting must never leave a frozen
   // window on stage: every teardown step is logged, and if one blocks (an
   // audio driver that will not stop, a plugin that waits on the message
   // thread, a decoder or capture thread that does not return) the process
   // is terminated after a few seconds. The log names the step that hung.
   static std::atomic<const char*> sShutdownStep { "begin" };
   static std::string sShutdownNode;
   static std::mutex sShutdownNodeMutex;
   auto shutdownStep = [](const char* step)
   {
      sShutdownStep.store(step);
      RuntimeLog::Write("shutdown: %s", step);
   };
   RuntimeLog::Write("shutdown requested");
   std::thread([]()
   {
      constexpr int kShutdownTimeoutMs = 4000;
      std::this_thread::sleep_for(std::chrono::milliseconds(kShutdownTimeoutMs));
      std::string node;
      {
         std::lock_guard<std::mutex> lock(sShutdownNodeMutex);
         node = sShutdownNode;
      }
      RuntimeLog::Write("shutdown stuck for %d ms in '%s'%s%s; terminating",
                        kShutdownTimeoutMs, sShutdownStep.load(),
                        node.empty() ? "" : " at node ", node.c_str());
      std::error_code ec;
      std::filesystem::remove(std::filesystem::u8path(SessionMarkerPath()), ec);
      Platform::TerminateNow(0);
   }).detach();

   shutdownStep("save prediction profiles");
   SavePredictionProfiles();
   shutdownStep("close output windows");
   CloseAllProjectorWindows();
   shutdownStep("stop audio engine");
   AudioEngine::Instance().Stop();
   shutdownStep("clear media thumbnails");
   ClearMediaThumbnails();
   shutdownStep("delete nodes");
   for (GraphNode& gn : gNodes)
   {
      {
         std::lock_guard<std::mutex> lock(sShutdownNodeMutex);
         sShutdownNode = gn.typeName;
      }
      gn.node.reset();
   }
   {
      std::lock_guard<std::mutex> lock(sShutdownNodeMutex);
      sShutdownNode.clear();
   }
   gNodes.clear();
   shutdownStep("destroy editor");
   ed::DestroyEditor(gEditor);
   shutdownStep("update checker");
   TurboUpdate::Shutdown(); // Turbo 0.46
   MovementLog::Stop(); // Turbo 0.60
   shutdownStep("ui and window");
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
   glfwDestroyWindow(window);
   glfwTerminate();

   RuntimeLog::Write("clean shutdown");
   {
      std::error_code ec;
      std::filesystem::remove(std::filesystem::u8path(SessionMarkerPath()), ec);
      ec.clear();
      std::filesystem::remove(std::filesystem::u8path(RecoveryAutosavePath()), ec);
   }

   // Not `return 0`: that runs the full C++/ObjC static-destructor chain
   // (__cxa_finalize_ranges) before the process dies, which includes global
   // destructors belonging to any third-party plugin framework still mapped
   // into our address space. Seen live: Kilohearts' HeartCore crashes in its
   // own global teardown on ordinary quit (SIGSEGV in a stale objc_msgSend
   // "clear" call, called from exit() via __cxa_finalize_ranges) - nothing
   // we did wrong, just an unload-order bug in code we don't control that
   // has no business running in a process that's about to disappear anyway.
   // Everything we actually need torn down already happened above; _Exit
   // skips straight to process termination without running anyone else's
   // destructors.
   std::_Exit(0);

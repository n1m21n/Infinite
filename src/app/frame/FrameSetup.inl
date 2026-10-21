// Part of src/main.cpp, included there in order (one translation unit).
// ImGui frame, transport tick, per-frame resets

      ImGui::NewFrame();
      PollFileDialogs();

      Transport::Instance().Tick(ImGui::GetIO().DeltaTime);
      // Turbo 0.43: arrangement faders/schedule/video-sample sync, pre-cook.
      ArrangeFrameUpdate();
      ClipMatrixFrameUpdate(); // Turbo 0.44: matrix events -> arrangement recording
      // Turbo (from upstream): Shift-drag gesture recording. Loops only move
      // while the transport plays; releasing Shift turns each trace into a loop.
      GestureRecorder::Instance().AdvanceClock(ImGui::GetIO().DeltaTime, Transport::Instance().IsPlaying());
      GestureRecorder::Instance().BeginFrame(ImGui::GetIO().KeyShift, GesturePlaybackClock());

      if (getenv("INFINITE_TRANSPORTCLOCKTEST") != nullptr)
      {
         // P2.5: verifies Beats()/Seconds() are actually live off the audio
         // engine's sample counter once it's running, not frozen at
         // whatever Tick() last wrote. Requires a real audio device to have
         // opened at startup (see the Start() call a few hundred lines up) -
         // on a machine with none, this just confirms the fallback clock
         // still advances smoothly, which is also worth knowing.
         static double sBaselineSeconds = 0.0;
         static double sPauseSeconds = 0.0;
         static bool sSawAdvance = false;
         static bool sSawNonDecrease = true;
         static double sPrevSeconds = 0.0;

         const double sr = AudioEngine::Instance().SampleRate();
         const double seconds = Transport::Instance().Seconds();

         if (frameId == 2)
         {
            sBaselineSeconds = seconds;
            sPrevSeconds = seconds;
            printf("audio sample rate: %.0f (%s)\n", sr, sr > 0.0 ? "audio-driven" : "fallback");
         }
         if (frameId >= 3 && frameId <= 20)
         {
            if (seconds < sPrevSeconds)
               sSawNonDecrease = false;
            if (seconds > sBaselineSeconds)
               sSawAdvance = true;
            sPrevSeconds = seconds;
         }
         if (frameId == 20)
         {
            printf("seconds after 18 frames: %.4f -> %.4f  %s\n", sBaselineSeconds, seconds,
                   (sSawAdvance && sSawNonDecrease) ? "ADVANCING SMOOTHLY OK" : "STALLED OR JUMPED - BUG");
            Transport::Instance().SetPlaying(false);
         }
         if (frameId == 21)
            sPauseSeconds = Transport::Instance().Seconds();
         if (frameId == 30)
         {
            const double afterPause = Transport::Instance().Seconds();
            printf("paused at %.4f, still %.4f after 9 frames  %s\n", sPauseSeconds, afterPause,
                   std::fabs(afterPause - sPauseSeconds) < 1e-9 ? "FROZEN OK" : "ADVANCED WHILE PAUSED - BUG");
            Transport::Instance().SetPlaying(true);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Turbo 0.60 (upstream): the movement log diffs last frame's final values -
      // what no machine writer claimed (NoteWriter) was the hand.
      MovementLog::Capture(ImGui::GetTime(), true);
      gPredictorGrabsPrev.swap(gPredictorGrabs); // Turbo 0.60: last frame's Shift-grabs
      gPredictorGrabs.clear();
      PollPendingNamed(); // Turbo 0.60: before the clear - last frame's controls are still registered
      RefreshParamKeyCache();
      Modulation::Instance().ClearFrameParams();
      gCurrentNodeIndex = -1;
      PaletteBinding::Instance().ClearFrameColors();
      gDrawnParamPins.clear();
      gParamPinSpots.clear();
      gDrawnColorPins.clear();
      gParamRightClickConsumedThisFrame = false;
      {
         Modulation& modulation = Modulation::Instance();
         for (GraphNode& gn : gNodes)
         {
            gn.hasModulatedParams = false;
            gn.hasBipolarParams = false;
            gn.hasPaletteColors = false;
         }
         for (const auto& link : modulation.Links())
         {
            if (GraphNode* target = FindNodeByIndex(link.first.first))
            {
               target->hasModulatedParams = true;
               if (link.second.polarity == Modulation::Source::kBipolar)
                  target->hasBipolarParams = true;
            }
         }
         for (const auto& link : PaletteBinding::Instance().Links())
         {
            if (GraphNode* target = FindNodeByIndex(link.first.first))
               target->hasPaletteColors = true;
         }
         // Turbo 0.59: an expression is evaluated only for params registered
         // this frame, i.e. while its node draws. Off-screen culling skipped
         // such a node on 29 frames of 30, so a `=t...` param scrolled out of
         // view (or near the edge) updated twice a second and whatever it
         // drove jumped. It is a binding like a cable: never culled.
         for (const auto& expr : modulation.Expressions())
         {
            if (GraphNode* target = FindNodeByIndex(expr.first.first))
               target->hasModulatedParams = true;
         }
      }

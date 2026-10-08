// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawFramePump(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& frameId = fc.frameId;
   auto& isBenchB5c = fc.isBenchB5c;
   auto& isBenchB2 = fc.isBenchB2;
   auto& isBenchB7 = fc.isBenchB7;
   auto& isBenchB3 = fc.isBenchB3;
   auto& isBenchB4 = fc.isBenchB4;
   auto& isBenchB6 = fc.isBenchB6;
   auto& isBenchB9 = fc.isBenchB9;
   auto& isBenchB8 = fc.isBenchB8;
   auto& benchB6Stages = fc.benchB6Stages;
   auto& benchStagesSample = fc.benchStagesSample;
   auto& benchStagesCpuSample = fc.benchStagesCpuSample;
   auto& benchGpuPerNode = fc.benchGpuPerNode;

      
      
      
      
      
      
      
      
      
      
      
      
      
      
       // FboAllocationCount at the start of the sample window

      isBenchB5c = (getenv("INFINITE_BENCH_B5STAGES") != nullptr || getenv("INFINITE_BENCH_B5C") != nullptr);
      isBenchB2 = (getenv("INFINITE_BENCH_B2") != nullptr || getenv("INFINITE_BENCH_B2VISUALS") != nullptr || getenv("INFINITE_BENCH_B2SCALE") != nullptr);
      isBenchB7 = getenv("INFINITE_BENCH_B7") != nullptr;
      isBenchB3 = (getenv("INFINITE_BENCH_B3") != nullptr || getenv("INFINITE_BENCH_B3LIVE") != nullptr || getenv("INFINITE_BENCH_B3SCALE") != nullptr || isBenchB7);
      isBenchB4 = getenv("INFINITE_BENCH_B4SCALE") != nullptr;
      isBenchB6 = (getenv("INFINITE_BENCH_B6") != nullptr || getenv("INFINITE_BENCH_B6NODES") != nullptr || getenv("INFINITE_BENCH_B6MODE") != nullptr || getenv("INFINITE_BENCH_B6COLLAPSED") != nullptr);
      isBenchB9 = (getenv("INFINITE_BENCH_B9SCENE") != nullptr || getenv("INFINITE_BENCH_B9") != nullptr || getenv("INFINITE_BENCH_B9MEMORY") != nullptr);
      isBenchB8 = getenv("INFINITE_BENCH_B8") != nullptr;
      // B8 samples the same span as B6 and uses B6's stage split (links and
      // cook_all get their own stages).
      benchB6Stages = isBenchB6 || isBenchB8;
      sBenchB8Sampling = isBenchB8 && frameId >= 32 && frameId < sBenchB8TotalFrames;
      benchStagesSample = ((isBenchB5c || isBenchB2 || isBenchB4) && (frameId >= 32 && frameId < 152)) ||
                                     (isBenchB6 && (frameId >= 32 && frameId < sBenchB6TotalFrames)) ||
                                     sBenchB8Sampling;
      benchStagesCpuSample = ((isBenchB5c || isBenchB2 || isBenchB4 || isBenchB9 || isBenchB3) && (frameId >= 32 && frameId < 152)) ||
                                        (isBenchB6 && (frameId >= 32 && frameId < sBenchB6TotalFrames)) ||
                                        sBenchB8Sampling;
      // B2 per-node GPU split: time each Render 3D / filter draw by node type
      // instead of the enclosing "cook" stage (GL timer queries cannot nest).
      // B8 always splits: the clip and camera uploads run inside Output's
      // pull, i.e. inside the cook stage, so "media_upload"/"camera_upload"
      // can only be timed with the cook-stage query off.
      benchGpuPerNode = (isBenchB2 && getenv("INFINITE_BENCH_B2GPUNODES") != nullptr) ||
                                   (isBenchB4 && getenv("INFINITE_BENCH_B4PASSES") != nullptr) ||
                                   isBenchB8;

      if (isBenchB5c || isBenchB2 || isBenchB4 || isBenchB6 || isBenchB8)
         sGpuTimerRing.Poll(frameId);

      gFrameStart = glfwGetTime();
      {
         // Slow-frame attribution: B3 (not the B7 soak) and B8 while sampling.
         static double sTailLoopStart = -1.0;
         const double period = gProjectorPacer.refreshHz > 0.0
                                  ? 1000.0 * (double)gProjectorPacer.intervals / gProjectorPacer.refreshHz : 0.0;
         Bench::Tail().BeginFrame((isBenchB3 && !isBenchB7 && frameId >= 32) || sBenchB8Sampling, period,
                                   glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0);
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkFrameStart, Bench::ScopedStageTimer::NowMs());
         if (sTailLoopStart >= 0.0)
            Bench::Tail().cur.loopMs = (gFrameStart - sTailLoopStart) * 1000.0;
         sTailLoopStart = gFrameStart;
         if (Bench::Tail().active)
            PollTailFences();
      }
      // A projector opened or closed, or an export started or ended, since
      // last frame: hand pacing to the right owner before this frame swaps.
      ApplyCanvasSwapInterval();
      glfwPollEvents();
      if (Bench::Tail().active)
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkPolled, Bench::ScopedStageTimer::NowMs());

      // A recording Stop click sets StopRequested() rather than calling
      // OutputNode::StopRecordingAsync() directly, so that frame gets to
      // finish drawing and present its "finalizing..." state before the call
      // happens. This is the earliest point in the *next* frame - the one
      // right after that "finalizing" frame's glfwSwapBuffers - where it's
      // safe to actually run it: the user has already seen the app
      // acknowledge the click instead of appearing to hang mid-click.
      // StopRecordingAsync() itself only blocks briefly (GL-bound PBO/audio
      // drain); the potentially long part - encoder join + AVAssetWriter's
      // finishWriting - runs on a background thread and is picked up by
      // PollFinalize() below once it completes, so a long or heavily
      // backlogged take no longer freezes the app on Stop.
      for (GraphNode& gn : gNodes)
      {
         if (auto* on = dynamic_cast<OutputNode*>(gn.node.get()))
         {
            if (on->StopRequested())
               on->StopRecordingAsync();
            on->PollFinalize();
            on->PollOfflineFinalize();
         }
      }

      // Offline Render pump: drives the graph and (if the take includes
      // audio) AudioEngine::ProcessOffline in lockstep, a fixed step per
      // frame, completely independent of wall-clock time or vsync - the
      // TouchDesigner/After Effects/Final Cut non-realtime export model, see
      // OutputNode::CaptureOfflineFrame's doc comment. Bounded to a small
      // wall-clock budget per outer loop iteration (rather than looping until
      // the whole take is done) so glfwPollEvents/ImGui still run often
      // enough for the progress window to repaint and the Cancel button to
      // stay clickable during a long render.
      if (gOfflineRender.active)
      {
         OutputNode* on = gOfflineRender.node;
         if (on != nullptr)
         {
            if (on->StopRequested())
               on->StopRecordingAsync();
            on->PollFinalize();
            on->PollOfflineFinalize();
         }
         if (on->IsOfflineRendering())
         {
            const double budgetStart = glfwGetTime();

            // Generates graph audio up to the take's running cumulative
            // target, optionally `lookahead` video frames past the frame
            // about to be captured. Shared by the per-frame path below and
            // by the backpressure wait, which needs it for a different
            // reason - see the wait's own comment.
            auto pumpOfflineAudio = [on](int lookahead)
            {
               if (!on->OfflineNeedsGraphAudio())
               {
                  on->FlushOfflineEncoderAudio();
                  return;
               }
               // Fixed-capacity scratch, same discipline as
               // AudioEngine::RunTopology's own thread_local scratch -
               // allocated once, reused every step. Not thread_local: this
               // only ever runs on the main thread.
               static float sOfflineAudioL[kAudioMaxBlockFrames];
               static float sOfflineAudioR[kAudioMaxBlockFrames];
               static float* sOfflineAudioChannels[2] = { sOfflineAudioL, sOfflineAudioR };

               // Pumped at the device's own block size rather than at the
               // scratch capacity: node behaviour is block-granular, so a
               // 4096-frame slab renders something the user never heard
               // (see OfflineAudioBlockFrames).
               const int blockCap = OfflineAudioBlockFrames();
               int owed = on->OfflineAudioFramesOwed(lookahead);
               while (owed > 0)
               {
                  const int blockFrames = std::min(owed, blockCap);
                  AudioBuffer offlineBuf;
                  offlineBuf.channels = sOfflineAudioChannels;
                  offlineBuf.numChannels = 2;
                  offlineBuf.numFrames = blockFrames;
                  AudioEngine::Instance().ProcessOffline(offlineBuf);

                  if (gOfflineRender.arrangeDriven)
                  {
                     static std::vector<float> sArrangeInterleave;
                     sArrangeInterleave.resize((size_t)blockFrames * 2);
                     for (int i = 0; i < blockFrames; i++)
                     {
                        sArrangeInterleave[(size_t)i * 2 + 0] = sOfflineAudioChannels[0][i];
                        sArrangeInterleave[(size_t)i * 2 + 1] = sOfflineAudioChannels[1][i];
                     }
                     on->CaptureRing().Write(sArrangeInterleave.data(), blockFrames * 2);
                  }

                  on->NoteOfflineAudioGenerated(blockFrames);
                  owed -= blockFrames;
               }
               on->FlushOfflineEncoderAudio();
            };
            while (on->IsOfflineRendering() && on->OfflineFramesDone() < on->OfflineFramesTotal())
            {
               // Backpressure, checked before anything else in the step: the
               // pump can hand the encoder frames far faster than it drains
               // them (that is the whole point of rendering offline), and
               // RecorderAppend's live-path answer to a full queue is to drop
               // the frame. Dropping is right for a live take, which cannot
               // stall, and wrong for this one, which can - a dropped frame
               // leaves the video track permanently short against a
               // full-length audio track. So yield the batch instead and try
               // again next outer iteration, with the progress window and
               // Cancel button still live in between.
               if (!on->OfflineEncoderHasRoom())
               {
                  // Keep the audio moving even though the picture cannot.
                  //
                  // AVAssetWriter will not take more video until the take's
                  // audio track has run past the same point - so a pump that
                  // stops rendering because the encoder queue is full also
                  // stops the audio the encoder is waiting for, and the two
                  // sides wait on each other forever. Measured, not guessed:
                  // with the queue full the writer sat at videoReady=0,
                  // audioReady=1, every encoder thread idle, one frame
                  // queued, permanently. The same take renders to completion
                  // video-only. INFINITE_OFFLINERENDER_QUEUEBYTES reproduces
                  // it in seconds.
                  //
                  // A second of lookahead is enough to clear the writer's
                  // interleaving window, and OfflineAudioFramesOwed caps the
                  // audio at the take's own frame total regardless, so this
                  // can never make the audio track longer than the picture.
                  pumpOfflineAudio(on->offlineFps);
                  gOfflineRender.waitingOnEncoder = true;
                  break;
               }
               gOfflineRender.waitingOnEncoder = false;

               ++frameId;
               const double videoSec = gOfflineRender.startSeconds +
                  (double)on->OfflineFramesDone() / (double)std::max(1, on->offlineFps);
               Transport::Instance().SetOfflineVideoTime(videoSec);
               ApplyModulationAndPalette(frameId);

               // Must run before the cook loop just below - see
               // ArrangeSeekVideoSampleSources's own comment for why.
               ArrangeSeekVideoSampleSources(Transport::Instance().Beats());

               for (GraphNode& gn : gNodes)
                  if (!gn.node->bypassed)
                     gn.node->CookIfNeeded(frameId);

               // Arrangement render: composite every active video lane, bottom
               // lane first so the top lane is frontmost, directly onto
               // on->GetFbo() (each lane's blend mode, opacity and aspect fit).
               // Its own target, never the monitor's: the panel keeps drawing
               // under the progress window at a different size. Beats() here
               // reads the video time just set, on the same axis as the audio
               // envelope's clip windows.
               if (gOfflineRender.arrangeDriven && gOfflineRender.timelineVideo)
               {
                  CompositeArrangeTimelineVideo(gArrangeRenderTarget, &on->GetFbo(), Transport::Instance().Beats(),
                                                on->GetOutputWidth(), on->GetOutputHeight());
               }

               // INFINITE_OFFLINERENDER_COOKDELAYMS simulates a heavy
               // per-frame GPU cook (the user's real patch, not this
               // fixture's Shape+Oscillator) by burning wall-clock time here,
               // after the graph is cooked but before audio is pumped or the
               // frame captured - the same place a genuinely slow shader
               // would cost time. This changes backpressure dynamics: a slow
               // cook means video frames arrive far slower than the encoder
               // can drain them, so OfflineEncoderHasRoom() may never trip at
               // all, which points any audio-loss bug away from the
               // backpressure/lookahead path (§3.1/§3.3 in the offline-render
               // brief) and toward something that misbehaves independent of
               // encoder pressure.
               if (const char* cookDelayEnv = getenv("INFINITE_OFFLINERENDER_COOKDELAYMS"))
               {
                  static const int sCookDelayMs = std::max(0, atoi(cookDelayEnv));
                  if (sCookDelayMs > 0)
                     std::this_thread::sleep_for(std::chrono::milliseconds(sCookDelayMs));
               }

               if (on->IsPrerolling())
                  on->DecrementPreroll();
               else
               {
                  pumpOfflineAudio(0);
                  on->CaptureOfflineFrame();
               }

               // ~10Hz: with vsync off for the take (see
               // StartOfflineRenderSession) the only cost of a longer batch
               // is progress-window latency, and a 20ms batch against a
               // ~16.7ms redraw spent nearly half the take drawing UI. 100ms
               // still repaints and services the Cancel button ten times a
               // second, which is as responsive as a click needs.
               if (glfwGetTime() - budgetStart > 0.1)
                  break;
            }

            if (on->OfflineFramesDone() != gOfflineRender.lastFramesDone)
            {
               gOfflineRender.lastFramesDone = on->OfflineFramesDone();
               gOfflineRender.lastProgressTime = glfwGetTime();
            }

            if (on->OfflineFramesDone() >= on->OfflineFramesTotal())
               on->RequestFinishOfflineRender(false);
         }
         else if (!on->IsOfflineFinalizing())
         {
            // PollOfflineFinalize() above just picked up the completed
            // finalize - whether from reaching the frame count or from a
            // Cancel click - so it's safe to hand the app's audio device and
            // transport play-state back to whatever they were before this
            // take started.
            Transport::Instance().SetOfflineMode(false);
            // Arrangement render: SetOfflineMode(false) only clears the
            // offline-active flags - it doesn't resync mSeconds to wherever
            // the take's mOfflineVideoSeconds ended up, so left alone the
            // playhead would snap back to wherever it was before "Render
            // Now" was clicked. Seek() is the same primitive used to park
            // the transport at the range's start when the take began, and
            // handles both the audio-engine-running and stopped cases.
            if (gOfflineRender.arrangeDriven)
               Transport::Instance().Seek(gOfflineRender.endSeconds);
            if (gOfflineRender.deviceWasRunning)
               StartAudioEngine(gAudioStartError);
            else
               Transport::Instance().NotifyAudioEngineStopped();
            Transport::Instance().SetPlaying(gOfflineRender.wasPlaying);
            SetCanvasSwapInterval(gOfflineRender.vsyncWasOn ? 1 : 0);
            gOfflineRender.active = false;
            gOfflineRender.arrangeDriven = false;
            gOfflineRender.timelineVideo = false;
            gOfflineRender.timelineAudio = false;
            gOfflineRender.node = nullptr;
            gArrangeRenderActiveLaneScope.clear();
            RebuildAudioTopology();
         }
      }

      // Audio-only takes and the export queue: run after the video pump's
      // slice so a take that just finished is noticed this frame, and the
      // next queued job starts on the next one (WP7).
      ArrangeRenderQueueTick();

      if (HeadlessJobActive())
         HeadlessTick(frameId, window);

      // Same dev-harness carve-out as the startup check above.
      if (getenv("INFINITE_EXITAFTER") == nullptr && !HeadlessJobActive())
         PollAutosave();

      // Apply any RemoteControl RPC requests queued by the network thread
      // since last frame, before any ed:: drawing reads the graph this frame.
      RemoteControl::DrainPending(HandleRpcCommand);
      PollPendingKeyed(); // before ClearFrameParams: last frame's registered controls are still here

      std::string pendingOpenPatch;
      while (Platform::PollPendingOpenFile(pendingOpenPatch))
      {
         // A headless job owns the canvas: macOS hands the .inf argument to the app as
         // an open-file event too, and loading it here would replace the probe nodes
         // (or reload the graph a job has already loaded).
         if (HeadlessJobActive())
            continue;
         LoadPatchFrom(pendingOpenPatch);
         gRequestFitView = true;
         glfwRequestWindowAttention(window); // a second launch asked for this file; surface the window
      }

      // Device-change/sleep-wake self-healing (docs/plans/optimization/
      // prompts/02-device-change-and-wake-recovery.md) - once a frame, main
      // thread only.
      PollAudioRecovery();

      // Timeline audio schedule: rebuild the topology only when the schedule
      // itself changed, at most once a frame - see ArrangeAudioRebuildIfStale
      // for what counts. This replaced a per-frame std::set<int> of the clips
      // under the playhead, whose diff rebuilt the topology at every clip
      // boundary: that is what made the second of two adjacent clips of one
      // node silent (the set never changed, so the stale single-clip window
      // stayed), made onsets land a UI frame late, and reset PDC at every
      // boundary. One topology now covers the whole arrangement, and clip
      // boundaries never rebuild.
      ArrangeAudioRebuildIfStale();

      // Update-checker worker handoff - once a frame, main thread only.
      UpdateCheck::Poll();

      // Per-frame audio housekeeping off the audio thread (currently just
      // freeing sample-preview buffers the audio thread has retired) - see
      // AudioEngine::PumpMainThread.
      AudioEngine::Instance().PumpMainThread();
      // With Start Audio off nothing runs the graph, so note generators and the
      // CV they drive would freeze; this keeps just the note nodes going.
      AudioEngine::Instance().PumpNoteNodesWithoutDevice();

      // Projector windows are ordinary decorated windows - closing one via the
      // OS's own close button only sets its should-close flag, it doesn't
      // destroy it. Poll for that here so it gets cleaned up the same way the
      // "Close window" menu item does. Iterate backwards since closing erases.
      for (size_t i = gProjectorWindows.size(); i-- > 0; )
         if (glfwWindowShouldClose(gProjectorWindows[i].window))
            CloseProjectorWindow(i);

      // Actually tear down anything retired by RemoveNodeByIndex/NewPatch.
      // The GL-texture hazard is already clear here (the frame that queued
      // draw commands referencing these textures has been submitted and
      // presented), but a node can also own audio state the real-time audio
      // thread may still be mid-ProcessBlock on against an older topology -
      // only drop entries the audio thread has actually confirmed it's past
      // (or that no audio thread is currently running to race with at all).
      // See gRetiredNodes' declaration for why a frame boundary alone isn't
      // sufficient for that half of the hazard.
      {
         const bool audioRaceable = AudioEngine::Instance().SampleRate() > 0.0 && AudioEngine::Instance().IsAlive();
         const uint64_t completedGeneration = AudioEngine::Instance().CompletedGeneration();
         gRetiredNodes.erase(
            std::remove_if(gRetiredNodes.begin(), gRetiredNodes.end(),
                            [&](RetiredNode& retired)
                            {
                               return !audioRaceable || completedGeneration > retired.safeAfterGeneration;
                            }),
            gRetiredNodes.end());
      }
      gRetiredViewports.clear();

      // dev-only: drive copy/paste/delete with synthetic key events so the
      // shortcuts can be verified without a human at the keyboard
      FrameTest_INPUTTEST(frameId, window);

      // Live rescale between frames: monitor change, OS scale change, or the UI Scale slider.
      // A minimized window reports 0x0, which would misread Retina as pixel-for-pixel, so
      // the rescale waits until the window has a size again.
      if (UiScale::RescaleRequested())
      {
         int dirtyW = 0, dirtyH = 0;
         glfwGetWindowSize(window, &dirtyW, &dirtyH);
         if (dirtyW > 0 && dirtyH > 0)
         {
            UiScale::RescaleRequested() = false;
            ApplyUiScale(window, true);
         }
      }
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();

      // dev-only: click inside the colour picker to prove it is interactive now
      FrameTest_COLORTEST(frameId, window);

      // dev-only: the whole "/" flow with nothing else touched - press slash on
      // an empty canvas and type. This is the path that has to stay one
      // keystroke, so it is driven exactly as a person would drive it.
      FrameTest_COMMENTTEST(frameId, window);

      // dev-only: double-click an existing note, the way an edit (rather than a
      // fresh comment) starts.
      FrameTest_COMMENTTEST_2(frameId, window);

      // dev-only: synthetic mouse drags to prove empty-canvas drag pans the view
      // while a drag that starts on a node still moves that node. The position is
      // re-asserted every frame, otherwise the GLFW backend's real cursor wins on
      // frames we don't touch and the gesture breaks up.
      // Drags the Wavetable's own visualizers and checks that each one moves
      // *its own* engine's parameter and nothing else. This is the check the
      // column-scoping bug would have caught: every engine-B widget was drawn
      // on top of engine A's, so A's picture showed B's table, A's drags were
      // swallowed by B's invisible buttons, and both looked simply dead.
      FrameTest_WTDRAGTEST(frameId, window);

      // BuildControl early-outs the moment the cursor isn't over the canvas,
      // so an unattended run measures the cheap path and reports a healthy
      // number that means nothing. Park the cursor mid-canvas.
      FrameTest_EDPERFTEST(frameId, window);

      // Drags EQ's own handles and checks each gesture moves only the param
      // it targets - the same "picture in the right place" vs "dragging it
      // moves the engine's param" split WTDRAGTEST exists to cover, for the
      // per-band dot/diamond/double-click surface DrawEqVisualizer adds.
      // gEqTestScreen (built in the post-editor block below) is the previous
      // frame's rect, same lag WTDRAGTEST accepts for a static node.
      FrameTest_EQDRAGTEST(frameId, window);

#ifndef NDEBUG
      // INFINITE_PREDBINDTEST: synthetic plain-drag then Shift-drag on the Shape's "size x" slider,
      // which a stub predictor drives. Aimed from the slider's rect as drawn last frame.
      if (getenv("INFINITE_PREDBINDTEST") != nullptr && frameId >= 50 && gNodes.size() > 3)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true);
         static bool sFocused = false;
         if (!sFocused) { glfwFocusWindow(window); sFocused = true; }
         ImVec2 rmin(0, 0), rmax(0, 0);
         rmin = ImVec2(gPredTestSliderScreen.x, gPredTestSliderScreen.y);
         rmax = ImVec2(gPredTestSliderScreen.z, gPredTestSliderScreen.w);
         const float cy = (rmin.y + rmax.y) * 0.5f;
         const float x30 = rmin.x + (rmax.x - rmin.x) * 0.3f, x70 = rmin.x + (rmax.x - rmin.x) * 0.7f;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };
         switch (frameId)
         {
            case 60: gTestMouse = ImVec2(x30, cy); break;
            case 62: btn(true); break;
            case 64: gTestMouse = ImVec2(x70, cy); break;
            case 66: btn(false); break;
            case 72: gTestMouse = ImVec2(x30, cy); break;
            case 74: btn(true); break;
            case 76: gTestMouse = ImVec2(x70, cy); break;
            case 80: btn(false); break;
            default: break;
         }
         if (frameId == 70)
         {
            tio.AddKeyEvent(ImGuiKey_LeftShift, true);
            tio.AddKeyEvent(ImGuiMod_Shift, true);
         }
         if (frameId == 86)
         {
            tio.AddKeyEvent(ImGuiKey_LeftShift, false);
            tio.AddKeyEvent(ImGuiMod_Shift, false);
         }
         tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
         glfwSetCursorPos(window, (double)(gTestMouse.x * ImGui_ImplGlfw_GetPointScale()),
                             (double)(gTestMouse.y * ImGui_ImplGlfw_GetPointScale()));
      }
#endif

      if (getenv("INFINITE_DRAGTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         // ImGui normally spreads queued input across frames to preserve click
         // positions; that splits a synthetic gesture apart, so disable it here.
         tio.ConfigInputTrickleEventQueue = false;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };
         // Hardcoded absolute pixels (e.g. 1400,800) assumed the requested
         // 1600x1000 window; the actual window can come up smaller than
         // requested (display-clamped), which put the drag start and later
         // points off-screen entirely - ImGui never saw the canvas as
         // hovered, the window lost focus, and NavigateAction never armed.
         // Anchor to the real display size instead.
         const ImVec2 dispSize = tio.DisplaySize;
         const ImVec2 dragBase(dispSize.x * 0.75f, dispSize.y * 0.70f);
         switch (frameId)
         {
            // --- phase 1: drag empty canvas (should pan, not move nodes) ---
            case 3: gTestMouse = dragBase; break;
            case 4: btn(true); break;
            case 5: gTestMouse = ImVec2(dragBase.x + 40.0f, dragBase.y + 30.0f); break;
            case 6: gTestMouse = ImVec2(dragBase.x + 100.0f, dragBase.y + 80.0f); break;
            case 7: gTestMouse = ImVec2(dragBase.x + 160.0f, dragBase.y + 120.0f); break;
            case 8: btn(false); break;
            // --- phase 2: drag the node's title row (should move the node) ---
            case 12: gTestMouse = gDragTestNodeScreen; break;
            case 13: btn(true); break;
            case 14: gTestMouse = ImVec2(gDragTestNodeScreen.x + 40.0f, gDragTestNodeScreen.y + 30.0f); break;
            case 15: gTestMouse = ImVec2(gDragTestNodeScreen.x + 90.0f, gDragTestNodeScreen.y + 70.0f); break;
            case 16: gTestMouse = ImVec2(gDragTestNodeScreen.x + 140.0f, gDragTestNodeScreen.y + 110.0f); break;
            case 17: btn(false); break;
            default: break;
         }
         if (frameId >= 3)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }

      FrameTest_SAMPLERDRAGTEST(frameId, window);

      FrameTest_PLUGINDRAGTEST(frameId, window);

      FrameTest_MEDIADRAGTEST(frameId, window);

      // R571: while a node is the active node Tab belongs to the param walker,
      // not to ImGui's own tab navigation (which drops a slider into text edit).
      if (gKbOwnTab)
         ImGui::SetKeyOwner(ImGuiKey_Tab, kKbTabOwner, ImGuiInputFlags_LockUntilRelease);
      // R571 slice 4: ImGui keyboard nav (Tab / arrows / Enter / Space) only while a popup or a window other than
      // the canvas host has focus. The canvas shares the "Infinite" window with the menu bar and panels, so it
      // can't be opted out per window; toggling per frame keeps the node keyboard model in charge of the canvas.
      {
         ImGuiContext& navCtx = *ImGui::GetCurrentContext();
         const bool navPopup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
         const bool navPanel = navCtx.NavWindow != nullptr && navCtx.NavWindow->RootWindow != nullptr &&
                               std::strcmp(navCtx.NavWindow->RootWindow->Name, "Infinite") != 0;
         const bool navOn = navPopup || navPanel;
         ImGuiIO& nio = ImGui::GetIO();
         if (navOn) nio.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
         else nio.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
         gNavOwnsKeys = navOn && nio.NavVisible;
      }
      ImGui::NewFrame();
      if (Bench::Tail().active)
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkNewFrame, Bench::ScopedStageTimer::NowMs());

      Transport::Instance().Tick(ImGui::GetIO().DeltaTime);

      FrameTest_TRANSPORTCLOCKTEST(frameId, window);

      PaletteBinding::Instance().ClearFrameColors();
      gDrawnParamPins.clear();
      gPinAnchors.clear();
      gDrawnColorPins.clear();
      gParamRightClickConsumedThisFrame = false;
      RefreshParamDriverFlags();}
}

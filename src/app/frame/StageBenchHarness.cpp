// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawBenchHarness(FrameCtx& fc)
{
   auto& sMainRssStartMb = fc.sMainRssStartMb;
   auto& sMainFootStartMb = fc.sMainFootStartMb;
   auto& tPreWindow = fc.tPreWindow;
   auto& window = fc.window;
   auto& tWindowGl = fc.tWindowGl;
   auto& tImGuiFonts = fc.tImGuiFonts;
   auto& tScanners = fc.tScanners;
   auto& tSettingsInit = fc.tSettingsInit;
   auto& sFirstFrameEndMs = fc.sFirstFrameEndMs;
   auto& frameId = fc.frameId;
   auto& isBenchB2 = fc.isBenchB2;
   auto& isBenchB7 = fc.isBenchB7;
   auto& isBenchB3 = fc.isBenchB3;
   auto& isBenchB4 = fc.isBenchB4;
   auto& isBenchB6 = fc.isBenchB6;
   auto& isBenchB9 = fc.isBenchB9;
   auto& isBenchB8 = fc.isBenchB8;
   auto& benchStagesSample = fc.benchStagesSample;
   auto& benchStagesCpuSample = fc.benchStagesCpuSample;
   auto& b6TrackVis = fc.b6TrackVis;
   auto& b6FrameVisibleCount = fc.b6FrameVisibleCount;
   auto& b6FrameBodiesDrawnCount = fc.b6FrameBodiesDrawnCount;
   auto& b6FrameOffscreenMs = fc.b6FrameOffscreenMs;


      // B2 Heavy visuals fixture, measurement half (benchmark-suite.md §4).
      // Same warmup/sample window as B5(b)/B5(c) (frameId 32 to 152 = 120 frames).
      // Reports frame_ms percentiles, tris count, GPU and CPU stage breakdowns,
      // RSS memory, and output_hash quality guard.
      if (isBenchB2 || isBenchB4)
      {
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; sBenchB2RssStartMb = Bench::ProcessRssMb(); }
         if (frameId == 32)
            sBenchB2FboAtF32 = GLUtil::FboAllocationCount();
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
            sBenchB2FrameMs.Push(gLastFrameMs);
         if (frameId == 152)
         {
            Bench::BenchReport report;
            report.bench = isBenchB4 ? "B4_complex_3d" : "B2_heavy_visuals";
            // Render targets (re)allocated inside the sample window; a
            // steady-state chain should allocate none.
            report.fboAllocsSteady = (long long)(GLUtil::FboAllocationCount() - sBenchB2FboAtF32);
            report.variant = sBenchB2Variant;
            report.frames = 152;
            report.nodes = (int)gNodes.size();
            report.frameMs = sBenchB2FrameMs;
            report.stagesCpuMs = {
               { "modulation", sStageModulation.Percentile(50) },
               { "cook", sStageCook.Percentile(50) },
               { "node_bodies", sStageNodeBodies.Percentile(50) },
               { "editor_end", sStageEditorEnd.Percentile(50) },
               { "imgui_render", sStageImGuiRender.Percentile(50) },
               { "projectors", sStageProjectors.Percentile(50) },
               { "swap", sStageSwap.Percentile(50) },
            };
            sGpuTimerRing.Finish();
            report.stagesGpuMs = sGpuTimerRing.ToJsonP50();
            report.memRssStartMb = sBenchB2RssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();

            if (sBenchB2Render3DIdx >= 0)
            {
               if (auto* gn = FindNodeByIndex(sBenchB2Render3DIdx))
               {
                  if (auto* r = dynamic_cast<Render3DNode*>(gn->node.get()))
                  {
                     // B2 keeps its original per-slot mesh count so its
                     // baseline stays comparable; B4 reports what was drawn,
                     // instances included.
                     if (isBenchB4)
                        report.tris = (int)r->LastTriangleCount();
                     else
                     {
                        for (int s = 0; s < Render3DNode::kSlots; s++)
                        {
                           if (r->geometry[s])
                              report.tris += (int)r->geometry[s]->GetMesh().FaceCount();
                        }
                     }
                     report.drawCalls = (int)r->LastDrawCalls();
                  }
               }
            }

            if (sBenchB2OutputIdx >= 0)
            {
               if (auto* outGn = FindNodeByIndex(sBenchB2OutputIdx))
               {
                  if (auto* outNode = dynamic_cast<OutputNode*>(outGn->node.get()))
                  {
                     glBindFramebuffer(GL_READ_FRAMEBUFFER, outNode->GetFbo().fbo);
                     report.outputHash = Bench::HashFramebufferRGBA8(outNode->GetOutputWidth(), outNode->GetOutputHeight());
                     glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                  }
               }
            }

            report.Emit();
            printf(isBenchB4 ? "B4COMPLEX3D DONE\n" : "B2VISUALS DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B3 Live performance fixture (benchmark-suite.md §4).
      // B1-lite + B2-lite + one open projector window + simulated MIDI notes/CC +
      // gesture playback + Prediction modulators running.
      // Measures projector p99/jitter/missed-vsync, canvas frame p50/p99,
      // audio load/xruns, and input-to-photon latency (parameter change to Output revision change).
      if (isBenchB3)
      {
         // B7 has no frame count: it ends on elapsed time (b3TotalFrames is
         // set to the frame the time runs out on, below).
         int b3TotalFrames = isBenchB7 ? std::numeric_limits<int>::max() / 2
            : getenv("INFINITE_BENCH_B3FRAMES") ? std::max(60, std::atoi(getenv("INFINITE_BENCH_B3FRAMES"))) : 600;
         const double b7Minutes = getenv("INFINITE_BENCH_B7MINUTES") ? std::max(1.0, std::atof(getenv("INFINITE_BENCH_B7MINUTES"))) : 30.0;
         const bool b3EnableMidi = (getenv("INFINITE_BENCH_B3MIDI") == nullptr || strcmp(getenv("INFINITE_BENCH_B3MIDI"), "0") != 0);
         const bool b3EnableVisuals = (getenv("INFINITE_BENCH_B3VISUALS") == nullptr || strcmp(getenv("INFINITE_BENCH_B3VISUALS"), "0") != 0);

         if (frameId == 2)
         {
            // Keep vsync ON for B3 per §6 (do NOT disable vsync!)
            gVsync = true;
            SetCanvasSwapInterval(1);
            gTargetFps = 0;
            const double r2 = Bench::ProcessRssMb();
            const double f2 = Bench::ProcessFootprintMb();
            if (sBenchB3RssStartMb < 0.0) sBenchB3RssStartMb = r2;
            if (sBenchB3FootStartMb < 0.0) sBenchB3FootStartMb = f2;
            if (r2 > sBenchB3RssPeakMb) sBenchB3RssPeakMb = r2;
            if (f2 > sBenchB3FootPeakMb) sBenchB3FootPeakMb = f2;

            if (sBenchB3OutputIdx >= 0)
            {
               if (auto* outGn = FindNodeByIndex(sBenchB3OutputIdx))
                  OpenProjectorWindow(window, *outGn);
            }

            // Side by side, never overlapping: the projector context presents
            // with swap interval 0 and relies on the main window's vsync to
            // pace the loop, and macOS stops vsync-blocking a swap on an
            // occluded window. Opened at main-window pos + 60, the projector
            // covered the canvas and the whole loop ran unpaced (~8.6 ms).
            if (!gProjectorWindows.empty())
            {
               if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
               {
                  int wx = 0, wy = 0, ww = 0, wh = 0;
                  glfwGetMonitorWorkarea(mon, &wx, &wy, &ww, &wh);
                  if (ww > 0 && wh > 0)
                  {
                     const int half = ww / 2;
                     glfwRestoreWindow(window);
                     glfwSetWindowPos(window, wx, wy);
                     glfwSetWindowSize(window, half, wh);
                     GLFWwindow* pw = gProjectorWindows[0].window;
                     glfwSetWindowPos(pw, wx + half, wy);
                     glfwSetWindowSize(pw, ww - half, std::min(wh, (ww - half) * 9 / 16));
                  }
               }
            }
            // Behind another app counts as occluded too, so raise the canvas.
            // macOS may refuse activation to a background launch; the run
            // then gets flagged ",unfocused=1" below rather than trusted.
            glfwFocusWindow(window);

            int refreshHz = 60;
            if (!gProjectorWindows.empty())
            {
               const int monIdx = ProjectorMonitorIndex(gProjectorWindows[0].window);
               int monCount = 0;
               GLFWmonitor** monitors = glfwGetMonitors(&monCount);
               if (monIdx >= 0 && monIdx < monCount)
               {
                  const GLFWvidmode* mode = glfwGetVideoMode(monitors[monIdx]);
                  if (mode && mode->refreshRate > 0)
                     refreshHz = mode->refreshRate;
               }
               else
               {
                  GLFWmonitor* primMon = glfwGetPrimaryMonitor();
                  const GLFWvidmode* mode = primMon ? glfwGetVideoMode(primMon) : nullptr;
                  if (mode && mode->refreshRate > 0)
                     refreshHz = mode->refreshRate;
               }
            }
            else
            {
               GLFWmonitor* primMon = glfwGetPrimaryMonitor();
               const GLFWvidmode* mode = primMon ? glfwGetVideoMode(primMon) : nullptr;
               if (mode && mode->refreshRate > 0)
                  refreshHz = mode->refreshRate;
            }
            sBenchB3MonitorRefreshHz = refreshHz;
            sBenchB3TargetRateHz = sBenchB3MonitorRefreshHz;

            sBenchB3XrunBaseline = AudioEngine::Instance().Xruns();
            AudioEngine::Instance().RawLoadHistory().Reset();
            AudioEngine::Instance().ResetStageLoadHistory();
            sBenchB7StartS = sBenchB7LastSampleS = glfwGetTime();
            sBenchB7LoadMark = AudioEngine::Instance().RawLoadHistory().Written();
         }

         // A frame measured while the canvas wasn't the key window may be
         // unpaced (see the side-by-side layout above); taint the variant.
         if (frameId > 2 && !sBenchB3Unfocused && glfwGetWindowAttrib(window, GLFW_FOCUSED) == 0)
         {
            sBenchB3Unfocused = true;
            sBenchB3Variant += ",unfocused=1";
            fprintf(stderr, "BENCH: B3 main window lost focus at frame %d - frame/projector pacing is not trustworthy\n", frameId);
         }

         // MIDI injection every frame (if enabled)
         if (b3EnableMidi && frameId >= 2)
         {
            if (frameId % 15 == 0)
            {
               const int noteNum = 60 + ((frameId / 15) % 12);
               const unsigned char noteOn[3] = { 0x90, (unsigned char)noteNum, 100 };
               Platform::MidiInjectBytes(noteOn, 3, 0);
            }
            else if (frameId % 15 == 12)
            {
               const int noteNum = 60 + ((frameId / 15) % 12);
               const unsigned char noteOff[3] = { 0x80, (unsigned char)noteNum, 0 };
               Platform::MidiInjectBytes(noteOff, 3, 0);
            }

            const unsigned char ccVal = (unsigned char)((std::sin((double)frameId * 0.05) * 0.5 + 0.5) * 127.0);
            const unsigned char ccMsg[3] = { 0xB0, 74, ccVal };
            Platform::MidiInjectBytes(ccMsg, 3, 0);
         }

         // Input-to-photon latency: inject parameter change on Twist every 20 frames
         const int kProbeInterval = 20;
         if (b3EnableVisuals && frameId >= 32 && frameId <= b3TotalFrames - 10)
         {
            if (frameId % kProbeInterval == kProbeInterval - 1)
            {
               // 1 frame prior to injection: pause animation drivers so frame settles
               sBenchB3ProbePaused = true;
            }
            else if (frameId % kProbeInterval == 0 && sBenchB3PendingInputInjectFrame < 0)
            {
               sBenchB3ProbePaused = true;
               if (auto* outGn = FindNodeByIndex(sBenchB3OutputIdx))
               {
                  if (auto* outNode = dynamic_cast<OutputNode*>(outGn->node.get()))
                  {
                     sBenchB3RevBeforeInject = outNode->Input().Revision();
                     sBenchB3PendingInputInjectFrame = frameId;
                     if (!sBenchB3I2PDryRun)
                     {
                        if (auto* twistGn = FindNodeByIndex(sBenchB3TwistIdx))
                        {
                           if (auto* twist = dynamic_cast<GeometryOpNode*>(twistGn->node.get()))
                              twist->amount += 0.05f;
                        }
                     }
                  }
               }
            }
         }

         // Sample canvas frame time and memory
         if (frameId >= 32 && frameId <= b3TotalFrames)
         {
            if (gLastFrameMs > 0.0)
               sBenchB3FrameMs.Push(gLastFrameMs);
            const double curRss = Bench::ProcessRssMb();
            const double curFoot = Bench::ProcessFootprintMb();
            if (curRss > sBenchB3RssPeakMb) sBenchB3RssPeakMb = curRss;
            if (curFoot > sBenchB3FootPeakMb) sBenchB3FootPeakMb = curFoot;
         }

         // B7 soak: close a window every 10 s, stop when the time is up.
         if (isBenchB7 && frameId >= 32 && sBenchB7StartS >= 0.0)
         {
            if (gLastFrameMs > 0.0)
               sBenchB7WinFrameMs.Push(gLastFrameMs);
            const double nowS = glfwGetTime();
            if (nowS - sBenchB7LastSampleS >= 10.0)
            {
               sBenchB7LastSampleS = nowS;
               const Bench::AudioLoadRing& ring = AudioEngine::Instance().RawLoadHistory();
               const uint64_t written = ring.Written();
               const Bench::PercentileRing winLoad = ring.DrainRange(sBenchB7LoadMark, written);
               sBenchB7LoadMark = written;
               const AudioEngine::XrunCounts x = AudioEngine::Instance().Xruns();
               sBenchB7Samples.push_back({
                  { "t_s", nowS - sBenchB7StartS },
                  { "frame_ms_p50", sBenchB7WinFrameMs.Percentile(50) },
                  { "frame_ms_p99", sBenchB7WinFrameMs.Percentile(99) },
                  { "rss_mb", Bench::ProcessRssMb() },
                  { "footprint_mb", Bench::ProcessFootprintMb() },
                  { "cb_load_p99", winLoad.Empty() ? nlohmann::json(nullptr) : nlohmann::json(winLoad.Percentile(99)) },
                  { "xruns_deadline", x.deadline - sBenchB3XrunBaseline.deadline },
                  { "xruns_os", x.os - sBenchB3XrunBaseline.os },
                  { "xrun_gaps", x.gaps - sBenchB3XrunBaseline.gaps },
               });
               sBenchB7WinFrameMs = Bench::PercentileRing();
               if (nowS - sBenchB7StartS >= b7Minutes * 60.0)
                  b3TotalFrames = frameId;
            }
         }

         if (frameId == b3TotalFrames)
         {
            Bench::BenchReport report;
            report.bench = isBenchB7 ? "B7_soak" : "B3_live_performance";
            report.variant = sBenchB3Variant;
            if (isBenchB7)
            {
               char minutesStr[32];
               snprintf(minutesStr, sizeof(minutesStr), ",minutes=%g", b7Minutes);
               report.variant += minutesStr;
            }
            report.frames = b3TotalFrames;
            report.nodes = (int)gNodes.size();
            report.frameMs = sBenchB3FrameMs;

            report.stagesCpuMs = {
               { "modulation", sStageModulation.Percentile(50) },
               { "cook", sStageCook.Percentile(50) },
               { "node_bodies", sStageNodeBodies.Percentile(50) },
               { "editor_end", sStageEditorEnd.Percentile(50) },
               { "imgui_render", sStageImGuiRender.Percentile(50) },
               { "projectors", sStageProjectors.Percentile(50) },
               { "swap", sStageSwap.Percentile(50) },
            };

            if (b3EnableVisuals)
            {
               report.projectorMeasured = true;
               report.projectorRefreshHz = sBenchB3MonitorRefreshHz;
               report.projectorTargetRateHz = sBenchB3TargetRateHz;
               report.projectorPresentMs = sBenchB3ProjIntervalRing;
               report.projectorJitterStdDev = sBenchB3ProjIntervalRing.StdDev();
               report.projectorMissedVsyncPct = (sBenchB3TotalVsyncCount > 0)
                  ? ((double)sBenchB3MissedVsyncCount / (double)sBenchB3TotalVsyncCount * 100.0)
                  : 0.0;

               // Zero samples is "not measured" (null), never a latency of 0.
               report.inputToPhotonMeasured = sBenchB3InputToPhotonFrames.Count() > 0;
               report.inputToPhotonFrames = sBenchB3InputToPhotonFrames;
               if (!isBenchB7)
                  report.slowFrames = Bench::Tail().Report();
            }

            report.audioMeasured = AudioEngine::Instance().SampleRate() > 0.0;
            const char* bufEnv = getenv("INFINITE_BENCH_B3BUFFER");
            report.audioBuffer = bufEnv ? atoi(bufEnv) : 256;
            report.audioSampleRate = AudioEngine::Instance().SampleRate();
            report.audioLoad = AudioEngine::Instance().RawLoadHistory().Drain();
            BenchFillXruns(report, sBenchB3XrunBaseline);

            report.memRssStartMb = sBenchB3RssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            if (report.memRssEndMb > sBenchB3RssPeakMb) sBenchB3RssPeakMb = report.memRssEndMb;
            report.memRssPeakMb = sBenchB3RssPeakMb;

            report.memFootStartMb = sBenchB3FootStartMb;
            report.memFootEndMb = Bench::ProcessFootprintMb();
            if (report.memFootEndMb > sBenchB3FootPeakMb) sBenchB3FootPeakMb = report.memFootEndMb;
            report.memFootPeakMb = sBenchB3FootPeakMb;

            // §6 Target evaluations
            // No engine, no audio verdict: a stopped engine has 0 xruns and
            // 0 load, which would read as a vacuous PASS.
            if (report.audioMeasured)
            {
               report.targetsPass["audio_xruns_zero"] = (report.audioXruns == 0);
               report.targetsPass["audio_cb_load_p99_le_50"] = (report.audioLoad.Percentile(99) <= 0.50);
            }
            // B7 is judged on the soak verdicts below, not on B3's
            // projector/latency targets.
            if (b3EnableVisuals && !isBenchB7)
            {
               report.targetsPass["projector_missed_vsync_lt_half_pct"] = (report.projectorMissedVsyncPct < 0.5);
               report.targetsPass["projector_locked_rate"] = (report.projectorPresentMs.Percentile(99) <= 1.10 * (1000.0 / (double)std::max(1, report.projectorTargetRateHz)));
               // The dry run (no param change) checks the probe itself: any
               // sample there is a false positive. It is not a latency result,
               // so it gets its own key rather than a PASS on the i2p target.
               if (sBenchB3I2PDryRun)
                  report.targetsPass["i2p_dryrun_no_false_samples"] = (sBenchB3InputToPhotonFrames.Count() == 0);
               else
                  report.targetsPass["input_to_photon_le_2_frames"] =
                     (sBenchB3InputToPhotonFrames.Count() >= 20 && sBenchB3InputToPhotonFrames.Max() <= 2.0);
            }

            if (sBenchB3Render3DIdx >= 0)
            {
               if (auto* gn = FindNodeByIndex(sBenchB3Render3DIdx))
               {
                  if (auto* r = dynamic_cast<Render3DNode*>(gn->node.get()))
                  {
                     for (int s = 0; s < Render3DNode::kSlots; s++)
                     {
                        if (r->geometry[s])
                           report.tris += (int)r->geometry[s]->GetMesh().FaceCount();
                     }
                     report.drawCalls = (int)r->LastDrawCalls();
                  }
               }
            }

            if (sBenchB3OutputIdx >= 0)
            {
               if (auto* outGn = FindNodeByIndex(sBenchB3OutputIdx))
               {
                  if (auto* outNode = dynamic_cast<OutputNode*>(outGn->node.get()))
                  {
                     glBindFramebuffer(GL_READ_FRAMEBUFFER, outNode->GetFbo().fbo);
                     report.outputHash = Bench::HashFramebufferRGBA8(outNode->GetOutputWidth(), outNode->GetOutputHeight());
                     glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                  }
               }
            }

            if (isBenchB7)
            {
               // rss_growth_pct: median RSS of the last 5 min against the
               // first 5 min after a 2 min warm-up. Thermal fps drop: the
               // first 10 s window against the last (reported, not gated).
               constexpr double kWarmupS = 120.0, kSpanS = 300.0;
               const double endS = sBenchB7Samples.empty() ? 0.0 : sBenchB7Samples.back()["t_s"].get<double>();
               std::vector<double> firstRss, lastRss;
               for (const auto& smp : sBenchB7Samples)
               {
                  const double t = smp["t_s"].get<double>();
                  if (t >= kWarmupS && t < kWarmupS + kSpanS) firstRss.push_back(smp["rss_mb"].get<double>());
                  if (t > endS - kSpanS) lastRss.push_back(smp["rss_mb"].get<double>());
               }
               auto median = [](std::vector<double> v) {
                  std::sort(v.begin(), v.end());
                  const size_t n = v.size();
                  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
               };
               nlohmann::json growth = nullptr;
               if (!firstRss.empty() && !lastRss.empty())
               {
                  const double first = median(firstRss), last = median(lastRss);
                  if (first > 0.0) growth = (last - first) / first * 100.0;
               }
               nlohmann::json fpsFirst = nullptr, fpsLast = nullptr, fpsDrop = nullptr;
               if (sBenchB7Samples.size() >= 2)
               {
                  const double p50First = sBenchB7Samples.front()["frame_ms_p50"].get<double>();
                  const double p50Last = sBenchB7Samples.back()["frame_ms_p50"].get<double>();
                  if (p50First > 0.0 && p50Last > 0.0)
                  {
                     fpsFirst = 1000.0 / p50First;
                     fpsLast = 1000.0 / p50Last;
                     fpsDrop = (1000.0 / p50First - 1000.0 / p50Last) / (1000.0 / p50First) * 100.0;
                  }
               }
               report.soak = {
                  { "minutes", b7Minutes },
                  { "interval_s", 10 },
                  { "warmup_s", kWarmupS },
                  { "rss_growth_pct", growth },
                  { "xruns_total", report.audioXruns },
                  { "fps_first", fpsFirst },
                  { "fps_last", fpsLast },
                  { "thermal_fps_drop_pct", fpsDrop },
                  { "samples", sBenchB7Samples },
               };
               report.targetsPass["soak_rss_growth_lt_2pct"] = growth.is_null() ? nlohmann::json(nullptr) : nlohmann::json(growth.get<double>() < 2.0);
               report.targetsPass["soak_xruns_zero"] = report.audioMeasured ? nlohmann::json(report.audioXruns == 0) : nlohmann::json(nullptr);
            }

            report.Emit();
            printf(isBenchB7 ? "B7SOAK DONE\n" : "B3LIVE DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B6 Canvas navigation fixture (benchmark-suite.md §4).
      // Large patch (200-400 nodes spread out on a 2D grid).
      // Programmatic pan/zoom through node-editor API, programmatic node drag, dropdown open.
      if (isBenchB6)
      {
         const int b6TotalFrames = sBenchB6TotalFrames;

         if (frameId == 2)
         {
            gVsync = sBenchB6Vsync;
            SetCanvasSwapInterval(sBenchB6Vsync ? 1 : 0);
            gTargetFps = 0;
            const double r2 = Bench::ProcessRssMb();
            const double f2 = Bench::ProcessFootprintMb();
            if (sBenchB6RssStartMb < 0.0) sBenchB6RssStartMb = r2;
            if (sBenchB6FootStartMb < 0.0) sBenchB6FootStartMb = f2;
            if (f2 > sBenchB6FootPeakMb) sBenchB6FootPeakMb = f2;

            glfwFocusWindow(window);
            if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
               if (const GLFWvidmode* mode = glfwGetVideoMode(mon); mode && mode->refreshRate > 0)
                  sBenchB6RefreshMs = 1000.0 / (double)mode->refreshRate;
         }

         // Focus check (lessons from B3: an occluded/unfocused window is unpaced on macOS)
         if (frameId >= 2 && frameId < b6TotalFrames)
         {
            const bool isFocused = (glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0);
            if (!isFocused && !sBenchB6Unfocused)
            {
               sBenchB6Unfocused = true;
               sBenchB6Variant += ",unfocused=1";
               fprintf(stderr, "[bench B6] window lost focus at frame %d -> unfocused=1\n", frameId);
            }

            const double curFoot = Bench::ProcessFootprintMb();
            if (curFoot > sBenchB6FootPeakMb)
               sBenchB6FootPeakMb = curFoot;
         }

         // Motion script during the sampled window (frameId 32 to b6TotalFrames)
         if (frameId >= 32 && frameId < b6TotalFrames)
         {
            const int sampleStart = 32;
            const int totalSampleFrames = b6TotalFrames - sampleStart;
            const int relFrame = frameId - sampleStart;

            std::string activeMode = sBenchB6Mode;
            int modeRelFrame = relFrame;
            int modeFrameCount = totalSampleFrames;

            if (sBenchB6Mode == "all")
            {
               const int phaseLength = std::max(1, totalSampleFrames / 4);
               const int phase = std::min(3, relFrame / phaseLength);
               modeRelFrame = relFrame % phaseLength;
               modeFrameCount = phaseLength;
               switch (phase)
               {
                  case 0: activeMode = "pan"; break;
                  case 1: activeMode = "zoom"; break;
                  case 2: activeMode = "drag"; break;
                  case 3: activeMode = "dropdown"; break;
                  default: activeMode = "pan"; break;
               }
            }

            const float t = (modeFrameCount > 1) ? (float)modeRelFrame / (float)(modeFrameCount - 1) : 0.0f;

            if (activeMode == "pan")
            {
               // Constant-speed sweep across the whole grid and back: 0 -> 1 -> 0
               const float u = (t <= 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
               const float targetScrollX = u * sBenchB6GridMaxX;
               const float targetScrollY = u * sBenchB6GridMaxY;
               ed::SetViewScroll(ImVec2(targetScrollX, targetScrollY));
               ed::SetViewZoom(1.0f);
            }
            else if (activeMode == "zoom")
            {
               // Zoom: 0.25x -> 2.0x -> 0.25x
               const float u = (t <= 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
               const float targetZoom = 0.25f * std::pow(8.0f, u);
               ed::SetViewScroll(ImVec2(sBenchB6GridMaxX * 0.5f, sBenchB6GridMaxY * 0.5f));
               ed::SetViewZoom(targetZoom);
            }
            else if (activeMode == "drag")
            {
               ed::SetViewScroll(ImVec2(0.0f, 0.0f));
               ed::SetViewZoom(1.0f);

               // Moved through the same needsPosition path a load/paste uses,
               // not synthetic mouse events: the GLFW backend overwrites an
               // injected mouse position with the real cursor every frame
               // unless the fixture also warps the user's cursor
               // (INFINITE_DRAGTEST does), which a benchmark must not do.
               // What is measured is the canvas cost of a node that moves
               // every frame (its links re-route), not ImGui's input path.
               if (sBenchB6DragNodeIndex >= 0)
               {
                  if (GraphNode* dragGn = FindNodeByIndex(sBenchB6DragNodeIndex))
                  {
                     const ImVec2 nodePos = ed::GetNodePosition(dragGn->NodeId());
                     if (modeRelFrame == 0)
                        sBenchB6DragStartPos = nodePos;
                     const float moved = std::hypot(nodePos.x - sBenchB6DragStartPos.x, nodePos.y - sBenchB6DragStartPos.y);
                     if (moved > sBenchB6DragMovedPx)
                        sBenchB6DragMovedPx = moved;
                     const float angle = (float)modeRelFrame * (2.0f * 3.1415926535f / 60.0f);
                     const float radius = 75.0f;
                     dragGn->spawnX = sBenchB6DragStartPos.x + radius * (std::cos(angle) - 1.0f);
                     dragGn->spawnY = sBenchB6DragStartPos.y + radius * std::sin(angle);
                     dragGn->needsPosition = true;
                  }
               }
            }
            else if (activeMode == "dropdown")
            {
               ed::SetViewScroll(ImVec2(0.0f, 0.0f));
               ed::SetViewZoom(1.0f);

               const bool openDropdown = ((modeRelFrame / 30) % 2 == 0);
               if (openDropdown)
               {
                  gDropdown.options = MathNode::OpNames();
                  gDropdown.current = (modeRelFrame / 5) % gDropdown.options.size();
                  gDropdown.justOpened = (modeRelFrame % 30 == 0);
               }
               else if (modeRelFrame % 30 == 0)
               {
                  // Clearing options alone leaves an empty popup on screen.
                  if (GImGui->OpenPopupStack.Size > 0) // indexes [0]: crashes on an empty stack
                  ImGui::ClosePopupToLevel(0, false);
                  gDropdown.options.clear();
                  gDropdown.justOpened = false;
               }
               // Counted so the report proves the popup really opened.
               if (GImGui->OpenPopupStack.Size > 0)
                  sBenchB6DropdownOpenFrames++;
            }

            if (gLastFrameMs > 0.0)
            {
               // Paced frames land on a whole number of refresh periods. A
               // window that is focused but off-screen (another Space, fully
               // covered) is not vsync-blocked on macOS and runs free - the
               // focus check alone cannot see that.
               const double periods = gLastFrameMs / sBenchB6RefreshMs;
               const double k = std::max(1.0, std::round(periods));
               if (std::fabs(gLastFrameMs - k * sBenchB6RefreshMs) <= 1.5)
                  sBenchB6OnVsyncFrames++;
               sBenchB6IntervalFrames++;
               sBenchB6FrameMs.Push(gLastFrameMs);
               if (activeMode == "pan")
                  sBenchB6PanFrameMs.Push(gLastFrameMs);
               else if (activeMode == "zoom")
                  sBenchB6ZoomFrameMs.Push(gLastFrameMs);
               else if (activeMode == "drag")
                  sBenchB6DragFrameMs.Push(gLastFrameMs);
               else if (activeMode == "dropdown")
                  sBenchB6DropdownFrameMs.Push(gLastFrameMs);
            }
         }

         if (frameId == b6TotalFrames)
         {
            if (GImGui->OpenPopupStack.Size > 0) // indexes [0]: crashes on an empty stack
               ImGui::ClosePopupToLevel(0, false);
            gDropdown.options.clear();
            gDropdown.justOpened = false;

            Bench::BenchReport report;
            report.bench = "B6_canvas_nav";
            report.variant = sBenchB6Variant;
            report.frames = b6TotalFrames;
            report.nodes = (int)gNodes.size();
            report.frameMs = sBenchB6FrameMs;

            report.stagesCpuMs = {
               { "modulation", sStageModulation.Percentile(50) },
               { "cook", sStageCook.Percentile(50) },
               { "node_bodies", sStageNodeBodies.Percentile(50) },
               { "links", sStageLinks.Percentile(50) },
               { "cook_all", sStageCookAll.Percentile(50) },
               { "editor_end", sStageEditorEnd.Percentile(50) },
               { "imgui_render", sStageImGuiRender.Percentile(50) },
               { "swap", sStageSwap.Percentile(50) },
               { "pan_p50", sBenchB6PanFrameMs.Percentile(50) },
               { "zoom_p50", sBenchB6ZoomFrameMs.Percentile(50) },
               { "drag_p50", sBenchB6DragFrameMs.Percentile(50) },
               { "dropdown_p50", sBenchB6DropdownFrameMs.Percentile(50) },
            };

            sGpuTimerRing.Finish();
            report.stagesGpuMs = sGpuTimerRing.ToJsonP50();

            report.memRssStartMb = sBenchB6RssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            report.memFootStartMb = sBenchB6FootStartMb;
            report.memFootEndMb = Bench::ProcessFootprintMb();
            if (report.memFootEndMb > sBenchB6FootPeakMb) sBenchB6FootPeakMb = report.memFootEndMb;
            report.memFootPeakMb = sBenchB6FootPeakMb;

            report.canvasNavMeasured = true;
            report.visibleNodesAvg = (sBenchB6SampledFrames > 0) ? (sBenchB6VisibleNodesSum / (double)sBenchB6SampledFrames) : 0.0;
            report.bodiesDrawnAvg = (sBenchB6SampledFrames > 0) ? (sBenchB6BodiesDrawnSum / (double)sBenchB6SampledFrames) : 0.0;
            report.offscreenBodiesMsAvg = (sBenchB6SampledFrames > 0) ? (sBenchB6OffscreenBodyMsSum / (double)sBenchB6SampledFrames) : 0.0;
            report.panFrameMs = sBenchB6PanFrameMs;
            report.zoomFrameMs = sBenchB6ZoomFrameMs;
            report.dragFrameMs = sBenchB6DragFrameMs;
            report.dropdownFrameMs = sBenchB6DropdownFrameMs;
            report.dragNodeMovedPx = sBenchB6DragMovedPx;
            report.dropdownOpenFrames = sBenchB6DropdownOpenFrames;

            const double onVsyncFrac = (sBenchB6IntervalFrames > 0)
               ? (double)sBenchB6OnVsyncFrames / (double)sBenchB6IntervalFrames : 0.0;
            const bool unpaced = sBenchB6Vsync && onVsyncFrac < 0.80;
            if (unpaced)
            {
               report.variant += ",unpaced=1";
               fprintf(stderr, "[bench B6] vsync on but only %.0f%% of frames on a refresh boundary -> unpaced=1\n", onVsyncFrac * 100.0);
            }
            report.onVsyncFrac = onVsyncFrac;

            // A trusted run (focused, vsync on, paced) gets a verdict either
            // way. Otherwise the frame times are a lower bound - vsync can only
            // add waiting - so a miss is still a real FAIL, but a pass is
            // unproven and stays null.
            const bool trusted = !sBenchB6Unfocused && sBenchB6Vsync && !unpaced;
            auto verdict = [&](const char* key, double measured, double limit) {
               if (sBenchB6PanFrameMs.Empty())
                  report.targetsPass[key] = nullptr;
               else if (trusted || measured > limit)
                  report.targetsPass[key] = (measured <= limit);
               else
                  report.targetsPass[key] = nullptr;
            };
            verdict("canvas_pan_p50_ge_60fps", sBenchB6PanFrameMs.Percentile(50), 17.2);
            verdict("canvas_pan_p95_ge_45fps", sBenchB6PanFrameMs.Percentile(95), 22.8);

            report.Emit();
            printf("B6CANVASNAV DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B8 Media I/O fixture (benchmark-suite.md §4): clips playing and
      // looping into Outputs, optional projector windows, Syphon/Spout Out and
      // camera. Measures decode/upload per clip, present per window, publish
      // and camera cost. Setup is in the INFINITE_BENCH_B8 branch before the loop.
      if (isBenchB8)
      {
         struct B8ClipStart
         {
            Bench::MediaClipCounters counters;
            size_t uploadSamples = 0;
            uint64_t decodeSamples = 0, cacheSamples = 0, loopSamples = 0, pullSamples = 0, convertSamples = 0;
            uint32_t decoded = 0, cacheHits = 0, dropped = 0, restarts = 0;
         };
         static std::vector<B8ClipStart> sClipStart;
         static Bench::MediaCameraCounters sCamStart;
         static size_t sSyphonStart = 0;
         static double sSampleStartMs = -1.0;
         const int b8Total = sBenchB8TotalFrames;

         if (!sBenchB8SetupError.empty())
         {
            if (frameId == 2)
            {
               printf("B8MEDIAIO FAIL setup: %s\n", sBenchB8SetupError.c_str());
               fflush(stdout);
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }
         else
         {
            if (frameId == 2)
            {
               gVsync = true;
               SetCanvasSwapInterval(1);
               gTargetFps = 0;

               for (int w = 0; w < sBenchB8Windows && w < (int)sBenchB8OutputIdx.size(); w++)
                  if (GraphNode* outGn = FindNodeByIndex(sBenchB8OutputIdx[(size_t)w]))
                     OpenProjectorWindow(window, *outGn);

               // Canvas at the top-left of the primary display, projectors in
               // a column right of it. Sizes are kept modest (the canvas at
               // most 1280x800, projectors 480x270) so a bench run does not
               // cover the whole screen. The loop is paced by the primary
               // Output display's refresh clock, not the canvas's swap, so
               // occlusion no longer unpaces it; B8OVERLAP=1 skips this layout
               // (projectors stay where they open, over the canvas) to prove it.
               if (GLFWmonitor* mon = glfwGetPrimaryMonitor(); mon && !gProjectorWindows.empty() && !sBenchB8ForceOverlap)
               {
                  int wx = 0, wy = 0, ww = 0, wh = 0;
                  glfwGetMonitorWorkarea(mon, &wx, &wy, &ww, &wh);
                  if (ww > 0 && wh > 0)
                  {
                     const int pw = 480, ph = 270;
                     const int cw = std::min(1280, ww - pw - 16);
                     const int ch = std::min(800, wh - 32);
                     glfwRestoreWindow(window);
                     glfwSetWindowPos(window, wx, wy + 32);
                     glfwSetWindowSize(window, cw, ch);
                     for (size_t k = 0; k < gProjectorWindows.size(); k++)
                     {
                        GLFWwindow* pwin = gProjectorWindows[k].window;
                        glfwSetWindowPos(pwin, wx + cw + 16, wy + 32 + (int)k * (ph + 40));
                        glfwSetWindowSize(pwin, pw, ph);
                     }
                  }
               }

               // Where the windows actually ended up (title bars, minimum sizes,
               // the OS can move them): any overlap is flagged, not trusted.
               std::vector<std::array<int, 4>> rects;
               auto frameRect = [&](GLFWwindow* w) {
                  int x = 0, y = 0, cw = 0, ch = 0, l = 0, t = 0, r = 0, b = 0;
                  glfwGetWindowPos(w, &x, &y);
                  glfwGetWindowSize(w, &cw, &ch);
                  glfwGetWindowFrameSize(w, &l, &t, &r, &b);
                  rects.push_back({ x - l, y - t, x + cw + r, y + ch + b });
               };
               frameRect(window);
               for (const ProjectorWindow& pwin : gProjectorWindows)
                  frameRect(pwin.window);
               for (size_t a = 0; a < rects.size(); a++)
                  for (size_t b = a + 1; b < rects.size(); b++)
                     if (rects[a][0] < rects[b][2] && rects[b][0] < rects[a][2] &&
                         rects[a][1] < rects[b][3] && rects[b][1] < rects[a][3])
                        sBenchB8Overlap = true;
               if (sBenchB8Overlap)
               {
                  sBenchB8Variant += ",overlap=1";
                  fprintf(stderr, "[bench B8] windows overlap on this display -> overlap=1\n");
               }

               glfwFocusWindow(window);
               if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
                  if (const GLFWvidmode* mode = glfwGetVideoMode(mon); mode && mode->refreshRate > 0)
                     sBenchB8RefreshMs = 1000.0 / (double)mode->refreshRate;

               sBenchB8Win.assign(gProjectorWindows.size(), BenchB8Window{});
               int monCount = 0;
               GLFWmonitor** monitors = glfwGetMonitors(&monCount);
               for (size_t k = 0; k < gProjectorWindows.size(); k++)
               {
                  const int monIdx = ProjectorMonitorIndex(gProjectorWindows[k].window);
                  sBenchB8Win[k].monitorIndex = monIdx;
                  if (monIdx >= 0 && monIdx < monCount)
                     if (const GLFWvidmode* mode = glfwGetVideoMode(monitors[monIdx]); mode && mode->refreshRate > 0)
                        sBenchB8Win[k].refreshHz = mode->refreshRate;
               }
            }

            if (frameId >= 2 && frameId < b8Total)
            {
               // Focus is only checked over the sampled span: the window is
               // moved, resized and refocused at frame 2 and macOS reports the
               // new focus a few frames later.
               if (sBenchB8Sampling && glfwGetWindowAttrib(window, GLFW_FOCUSED) == 0 && !sBenchB8Unfocused)
               {
                  sBenchB8Unfocused = true;
                  sBenchB8Variant += ",unfocused=1";
                  fprintf(stderr, "[bench B8] window lost focus at frame %d -> unfocused=1\n", frameId);
               }
               const double curFoot = Bench::ProcessFootprintMb();
               if (curFoot > sBenchB8FootPeakMb)
                  sBenchB8FootPeakMb = curFoot;
            }

            // Everything a clip counted before the sampled span (open, first
            // decode, warm-up) is subtracted out at the end.
            if (frameId == 32)
            {
               sSampleStartMs = Bench::ScopedStageTimer::NowMs();
               sClipStart.assign(sBenchB8ClipIdx.size(), B8ClipStart{});
               for (size_t c = 0; c < sBenchB8ClipIdx.size(); c++)
               {
                  GraphNode* gn = FindNodeByIndex(sBenchB8ClipIdx[c]);
                  auto* vid = gn ? dynamic_cast<VideoSourceNode*>(gn->node.get()) : nullptr;
                  if (vid == nullptr)
                     continue;
                  B8ClipStart& st = sClipStart[c];
                  if (const Bench::MediaClipCounters* cc = vid->BenchCounters())
                  {
                     st.counters = *cc;
                     st.uploadSamples = cc->uploadCpuMs.size();
                  }
                  if (Bench::MediaDecodeStats* ds = Platform::VideoBenchStats(vid->BenchVideoHandle()))
                  {
                     st.decodeSamples = ds->decodeMs.Count();
                     st.cacheSamples = ds->cacheHitMs.Count();
                     st.pullSamples = ds->pullMs.Count();
                     st.convertSamples = ds->convertMs.Count();
                     st.loopSamples = ds->loopDecodeMs.Count();
                     st.decoded = ds->decoded.load();
                     st.cacheHits = ds->cacheHits.load();
                     st.dropped = ds->dropped.load();
                     st.restarts = ds->readerRestarts.load();
                  }
               }
               if (GraphNode* camGn = FindNodeByIndex(sBenchB8CameraIdx))
                  if (auto* cam = dynamic_cast<VideoInNode*>(camGn->node.get()))
                     if (const Bench::MediaCameraCounters* cc = cam->BenchCounters())
                        sCamStart = *cc;
               if (GraphNode* syGn = FindNodeByIndex(sBenchB8SyphonIdx))
                  if (auto* sy = dynamic_cast<SyphonOutNode*>(syGn->node.get()))
                     sSyphonStart = sy->BenchPublishMs().size();
            }

            if (sBenchB8Sampling && frameId > 32 && gLastFrameMs > 0.0)
            {
               // Same pacing test as B6: a paced frame lands on a whole number
               // of refresh periods of the canvas's display.
               const double periods = gLastFrameMs / sBenchB8RefreshMs;
               const double k = std::max(1.0, std::round(periods));
               if (std::fabs(gLastFrameMs - k * sBenchB8RefreshMs) <= 1.5)
                  sBenchB8OnVsyncFrames++;
               sBenchB8IntervalFrames++;
               sBenchB8FrameMs.Push(gLastFrameMs);
            }

            if (frameId == b8Total)
            {
               const double sampleSec = std::max(1e-3, (Bench::ScopedStageTimer::NowMs() - sSampleStartMs) / 1000.0);
               auto ringOf = [](const std::vector<double>& v, size_t from = 0) {
                  Bench::PercentileRing r;
                  for (size_t i = from; i < v.size(); i++)
                     r.Push(v[i]);
                  return r;
               };
               auto p5099max = [](const Bench::PercentileRing& r) -> nlohmann::json {
                  if (r.Empty())
                     return nullptr;
                  return { { "p50", r.Percentile(50) }, { "p99", r.Percentile(99) }, { "max", r.Max() }, { "n", (int)r.Count() } };
               };

               Bench::BenchReport report;
               report.bench = "B8_media_io";
               report.frames = b8Total;
               report.nodes = (int)gNodes.size();
               report.frameMs = sBenchB8FrameMs;
               if (!sBenchB8Win.empty())
                  report.slowFrames = Bench::Tail().Report();

               report.stagesCpuMs = {
                  { "modulation", sStageModulation.Percentile(50) },
                  { "cook", sStageCook.Percentile(50) },
                  { "node_bodies", sStageNodeBodies.Percentile(50) },
                  { "links", sStageLinks.Percentile(50) },
                  { "cook_all", sStageCookAll.Percentile(50) },
                  { "editor_end", sStageEditorEnd.Percentile(50) },
                  { "imgui_render", sStageImGuiRender.Percentile(50) },
                  { "swap", sStageSwap.Percentile(50) },
                  { "projectors", sStageProjectors.Percentile(50) },
               };
               sGpuTimerRing.Finish();
               report.stagesGpuMs = sGpuTimerRing.ToJsonP50();

               report.memRssStartMb = sBenchB8RssStartMb;
               report.memRssEndMb = Bench::ProcessRssMb();
               report.memFootStartMb = sBenchB8FootStartMb;
               report.memFootEndMb = Bench::ProcessFootprintMb();
               if (report.memFootEndMb > sBenchB8FootPeakMb)
                  sBenchB8FootPeakMb = report.memFootEndMb;
               report.memFootPeakMb = sBenchB8FootPeakMb;

               const double onVsyncFrac = (sBenchB8IntervalFrames > 0)
                  ? (double)sBenchB8OnVsyncFrames / (double)sBenchB8IntervalFrames : 0.0;
               const bool unpaced = onVsyncFrac < 0.80;
               if (unpaced)
               {
                  sBenchB8Variant += ",unpaced=1";
                  fprintf(stderr, "[bench B8] only %.0f%% of frames on a refresh boundary -> unpaced=1\n", onVsyncFrac * 100.0);
               }
               report.variant = sBenchB8Variant;
               const bool trusted = !sBenchB8Unfocused && !unpaced && !sBenchB8Overlap;
               // Proposed targets (README): a miss is a real FAIL in any run,
               // a pass only counts in a trusted one.
               auto verdict = [&](const std::string& key, bool measurable, bool pass) {
                  if (!measurable)
                     report.targetsPass[key] = nullptr;
                  else if (!pass || trusted)
                     report.targetsPass[key] = pass;
                  else
                     report.targetsPass[key] = nullptr;
               };

               nlohmann::json media = nlohmann::json::object();
               nlohmann::json clips = nlohmann::json::array();
               Bench::PercentileRing allUploadCpu;
               for (size_t c = 0; c < sBenchB8ClipIdx.size(); c++)
               {
                  GraphNode* gn = FindNodeByIndex(sBenchB8ClipIdx[c]);
                  auto* vid = gn ? dynamic_cast<VideoSourceNode*>(gn->node.get()) : nullptr;
                  const Bench::MediaClipCounters* cc = vid ? vid->BenchCounters() : nullptr;
                  Bench::MediaDecodeStats* ds = vid ? Platform::VideoBenchStats(vid->BenchVideoHandle()) : nullptr;
                  if (cc == nullptr || c >= sClipStart.size())
                  {
                     clips.push_back(nullptr);
                     continue;
                  }
                  const B8ClipStart& st = sClipStart[c];
                  const Bench::MediaClipCounters& s0 = st.counters;
                  const double fps = (ds && ds->nominalFps > 0.0) ? ds->nominalFps : 30.0;
                  const int requests = cc->requests - s0.requests;
                  const int newFrames = cc->newFrames - s0.newFrames;
                  const int repeats = cc->repeats - s0.repeats;
                  const int expectedRepeats = cc->expectedRepeats - s0.expectedRepeats;
                  const int skipped = cc->skipped - s0.skipped;
                  const Bench::PercentileRing uploadCpu = ringOf(cc->uploadCpuMs, st.uploadSamples);
                  for (double v : uploadCpu.Samples())
                     allUploadCpu.Push(v);

                  nlohmann::json cj = {
                     { "path", vid->LoadedPath() },
                     { "width", vid->GetOutputWidth() },
                     { "height", vid->GetOutputHeight() },
                     { "fps", fps },
                     { "requests", requests },
                     { "uploads", cc->uploads - s0.uploads },
                     { "new_frames", newFrames },
                     { "repeated", repeats },
                     { "expected_repeats", expectedRepeats },
                     { "extra_repeats", repeats - expectedRepeats },
                     { "reuploads", cc->reuploads - s0.reuploads },
                     { "failed", cc->failed - s0.failed },
                     { "skipped", skipped },
                     { "loop_wraps", cc->loopWraps - s0.loopWraps },
                     { "shown_fps", (double)newFrames / sampleSec },
                     { "upload_cpu_ms", p5099max(uploadCpu) },
                  };
                  int dropped = 0;
                  double decodedFps = 0.0;
                  if (ds != nullptr)
                  {
                     const int decoded = (int)(ds->decoded.load() - st.decoded);
                     dropped = (int)(ds->dropped.load() - st.dropped);
                     decodedFps = (double)decoded / sampleSec;
                     Bench::PercentileRing dec, hit, loopRing, pull, conv;
                     for (double v : ds->pullMs.SnapshotSince(st.pullSamples)) pull.Push(v);
                     for (double v : ds->convertMs.SnapshotSince(st.convertSamples)) conv.Push(v);
                     for (double v : ds->decodeMs.SnapshotSince(st.decodeSamples)) dec.Push(v);
                     for (double v : ds->cacheHitMs.SnapshotSince(st.cacheSamples)) hit.Push(v);
                     for (double v : ds->loopDecodeMs.SnapshotSince(st.loopSamples)) loopRing.Push(v);
                     cj["decode_ms"] = p5099max(dec);
                     cj["pull_ms"] = p5099max(pull);
                     cj["convert_ms"] = p5099max(conv);
                     cj["cache_hit_ms"] = p5099max(hit);
                     cj["loop_decode_ms"] = p5099max(loopRing);
                     cj["decoded"] = decoded;
                     cj["cache_hits"] = (int)(ds->cacheHits.load() - st.cacheHits);
                     cj["dropped"] = dropped;
                     cj["reader_restarts"] = (int)(ds->readerRestarts.load() - st.restarts);
                     cj["decoded_fps"] = decodedFps;
                  }
                  else
                  {
                     cj["decode_ms"] = nullptr;
                     cj["decoded"] = nullptr;
                     cj["dropped"] = nullptr;
                  }
                  clips.push_back(cj);

                  // Real-time decode: the decoder keeps up with the clip (the
                  // +1 absorbs a frame straddling the window edge), nothing is
                  // dropped or skipped, and no repeat beyond the clip's own.
                  const bool realtime = ds != nullptr &&
                                        ((double)(ds->decoded.load() - st.decoded) + 1.0) / sampleSec >= fps &&
                                        dropped == 0 && skipped == 0 && repeats - expectedRepeats <= 0;
                  verdict("clip" + std::to_string(c) + "_decode_realtime", ds != nullptr && requests > 0, realtime);
               }
               media["clips"] = clips;

               // A query that bracketed every upload yet never saw a nanosecond
               // did not measure the upload: Apple's GL copies the pixels on the
               // CPU and runs the blit later, outside the query. Null, not 0.
               auto gpuStage = [&](const char* name, const char*& note) -> nlohmann::json {
                  const Bench::PercentileRing* r = sGpuTimerRing.GetStage(name);
                  note = nullptr;
                  if (r == nullptr || r->Empty())
                  {
                     note = "gpu timers off (INFINITE_BENCH_GPUTIMERS=1 to time)";
                     return nullptr;
                  }
                  if (r->Max() <= 0.0)
                  {
                     note = "timer query read 0 ms on every frame: the driver defers the upload blit";
                     return nullptr;
                  }
                  return p5099max(*r);
               };
               const char* uploadGpuNote = nullptr;
               media["upload_ms"] = {
                  { "cpu", p5099max(allUploadCpu) },
                  { "gpu_per_frame", gpuStage("media_upload", uploadGpuNote) },
               };
               if (uploadGpuNote != nullptr)
                  media["upload_ms"]["gpu_note"] = uploadGpuNote;

               nlohmann::json wins = nlohmann::json::array();
               const int mainHz = (int)std::lround(1000.0 / sBenchB8RefreshMs);
               for (size_t k = 0; k < sBenchB8Win.size(); k++)
               {
                  const BenchB8Window& bw = sBenchB8Win[k];
                  const int hz = bw.refreshHz > 0 ? bw.refreshHz : mainHz;
                  // Projectors are paced to the primary Output display's
                  // refresh, every `paced_every` refreshes (1 at <= 75 Hz; see
                  // ProjectorPacer), so that is the period they must hold.
                  const int pacedEvery = std::max(1, gProjectorPacer.intervals);
                  const double periodMs = 1000.0 * (double)pacedEvery / (double)std::max(1, hz);
                  const size_t missed = bw.intervalMs.CountOver(1.5 * periodMs);
                  const double missedFrac = bw.intervalMs.Empty() ? 0.0 : (double)missed / (double)bw.intervalMs.Count();
                  const nlohmann::json onVsyncFrac = bw.intervalMs.Empty()
                     ? nlohmann::json(nullptr) : nlohmann::json((double)bw.onVsync / (double)bw.intervalMs.Count());
                  wins.push_back({
                     { "refresh_hz", hz },
                     { "monitor", bw.monitorIndex },
                     { "presents", (int)bw.presentMs.Count() },
                     { "present_ms", p5099max(bw.presentMs) },
                     { "interval_ms", p5099max(bw.intervalMs) },
                     { "jitter_stddev_ms", bw.intervalMs.StdDev() },
                     { "missed_vsync_frac", missedFrac },
                     { "on_vsync_frac", onVsyncFrac },
                     { "paced_every", pacedEvery },
                  });
                  const std::string key = "window" + std::to_string(k);
                  verdict(key + "_interval_p99_locked", !bw.intervalMs.Empty(),
                          bw.intervalMs.Percentile(99) <= 1.10 * periodMs);
                  verdict(key + "_missed_vsync_lt_half_pct", !bw.intervalMs.Empty(), missedFrac < 0.005);
               }
               media["windows"] = wins;
               media["main_window"] = { { "refresh_hz", mainHz }, { "on_vsync_frac", onVsyncFrac } };
               media["overlap"] = sBenchB8Overlap;

               if (!sBenchB8WantSyphon)
                  media["syphon"] = nullptr;
               else
               {
                  GraphNode* syGn = FindNodeByIndex(sBenchB8SyphonIdx);
                  auto* sy = syGn ? dynamic_cast<SyphonOutNode*>(syGn->node.get()) : nullptr;
                  if (sy == nullptr || !sy->BenchServerCreated())
                     media["syphon"] = "n/a"; // Linux, or the server could not be created
                  else
                  {
                     const Bench::PercentileRing pub = ringOf(sy->BenchPublishMs(), sSyphonStart);
                     nlohmann::json syj = { { "publish_ms", p5099max(pub) }, { "publishes", (int)pub.Count() } };
                     if (Platform::SyphonServerCanReportClients())
                        syj["has_clients"] = sy->HasClients();
                     else
                        syj["has_clients"] = nullptr; // Spout cannot say whether anyone is receiving
                     media["syphon"] = syj;
                  }
               }

               if (!sBenchB8WantCamera)
                  media["camera"] = nullptr;
               else
               {
                  GraphNode* camGn = FindNodeByIndex(sBenchB8CameraIdx);
                  auto* cam = camGn ? dynamic_cast<VideoInNode*>(camGn->node.get()) : nullptr;
                  const Bench::MediaCameraCounters* cc = cam ? cam->BenchCounters() : nullptr;
                  const int frames = cc ? cc->frames - sCamStart.frames : 0;
                  if (!sBenchB8CameraSkipReason.empty() || cc == nullptr || frames <= 0)
                  {
                     media["camera"] = "skipped";
                     media["camera_skip_reason"] = !sBenchB8CameraSkipReason.empty() ? sBenchB8CameraSkipReason
                                                   : (cam && !cam->LastError().empty()) ? cam->LastError()
                                                   : std::string("no_frames");
                  }
                  else
                  {
                     const Bench::PercentileRing interval = ringOf(cc->intervalMs, sCamStart.intervalMs.size());
                     const Bench::PercentileRing readUpload = ringOf(cc->readUploadMs, sCamStart.readUploadMs.size());
                     const char* camGpuNote = nullptr;
                     const nlohmann::json camGpu = gpuStage("camera_upload", camGpuNote);
                     media["camera"] = {
                        { "frames", frames },
                        { "fps", (double)frames / sampleSec },
                        { "frame_interval_ms", p5099max(interval) },
                        { "read_upload_cpu_ms", p5099max(readUpload) },
                        { "upload_gpu_per_frame", camGpu },
                     };
                     if (camGpuNote != nullptr)
                        media["camera"]["upload_gpu_note"] = camGpuNote;
                  }
               }
               media["sample_seconds"] = sampleSec;
               report.mediaIo = media;

               report.Emit();
               printf("B8MEDIAIO DONE\n");
               fflush(stdout);
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }
      }

      // B9 Memory footprint fixture (benchmark-suite.md §4).
      // Measures RSS and Physical Footprint growth rate (slope MB/100f), peak footprint,
      // startup/built/f32/f152 memory, and estimated GPU memory breakdown.
      if (isBenchB9)
      {
         const int b9TotalFrames = getenv("INFINITE_BENCH_B9FRAMES") ? std::max(60, std::atoi(getenv("INFINITE_BENCH_B9FRAMES"))) : 600;
         if (frameId == 2)
         {
            gVsync = false;
            SetCanvasSwapInterval(0);
            gTargetFps = 0;
            // "start" is process launch, not frame 2: by frame 2 the scene
            // is already built, and the peak has to include launch and the
            // build or it can come out below rss_built_mb.
            sBenchB9RssStartMb = sMainRssStartMb;
            sBenchB9RssPeakMb = std::max({ sMainRssStartMb, sBenchB9RssBuiltMb });
            sBenchB9FootPeakMb = std::max({ sMainFootStartMb, sBenchB9FootBuiltMb });
         }
         if (frameId >= 2 && frameId <= b9TotalFrames)
         {
            const double currentRss = Bench::ProcessRssMb();
            const double currentFoot = Bench::ProcessFootprintMb();
            sBenchB9RssPeakMb = std::max(sBenchB9RssPeakMb, currentRss);
            sBenchB9FootPeakMb = std::max(sBenchB9FootPeakMb, currentFoot);
            if (frameId >= 32)
            {
               if (gLastFrameMs > 0.0)
                  sBenchB9FrameMs.Push(gLastFrameMs);
               if (frameId == 32)
               {
                  sBenchB9RssF32Mb = currentRss;
                  sBenchB9FootF32Mb = currentFoot;
               }
               if (frameId == 152)
               {
                  sBenchB9RssF152Mb = currentRss;
                  sBenchB9FootF152Mb = currentFoot;
               }
               sBenchB9RssSamples.push_back({ frameId, currentRss });
               sBenchB9FootSamples.push_back({ frameId, currentFoot });
            }
         }
         if (frameId == b9TotalFrames)
         {
            Bench::BenchReport report;
            report.bench = "B9_memory_footprint";
            report.variant = sBenchB9Variant;
            report.frames = b9TotalFrames;
            report.nodes = (int)gNodes.size();
            report.frameMs = sBenchB9FrameMs;
            report.stagesCpuMs = {
               { "modulation", sStageModulation.Percentile(50) },
               { "cook", sStageCook.Percentile(50) },
               { "node_bodies", sStageNodeBodies.Percentile(50) },
               { "editor_end", sStageEditorEnd.Percentile(50) },
               { "imgui_render", sStageImGuiRender.Percentile(50) },
               { "projectors", sStageProjectors.Percentile(50) },
               { "swap", sStageSwap.Percentile(50) },
            };
            sGpuTimerRing.Finish();
            report.stagesGpuMs = sGpuTimerRing.ToJsonP50();
            report.memRssStartMb = sBenchB9RssStartMb;
            report.memRssBuiltMb = sBenchB9RssBuiltMb;
            report.memRssF32Mb = sBenchB9RssF32Mb;
            report.memRssF152Mb = sBenchB9RssF152Mb;
            report.memRssEndMb = Bench::ProcessRssMb();
            if (report.memRssEndMb > sBenchB9RssPeakMb)
               sBenchB9RssPeakMb = report.memRssEndMb;
            report.memRssPeakMb = sBenchB9RssPeakMb;
            report.memRssSlopeMbPer100f = Bench::CalculateRssSlopeMbPer100f(sBenchB9RssSamples);
            report.memFootStartMb = sMainFootStartMb;
            report.memFootBuiltMb = sBenchB9FootBuiltMb;
            report.memFootF32Mb = sBenchB9FootF32Mb;
            report.memFootF152Mb = sBenchB9FootF152Mb;
            report.memFootEndMb = Bench::ProcessFootprintMb();
            report.memFootPeakMb = std::max(sBenchB9FootPeakMb, report.memFootEndMb);
            report.memFootSlopeMbPer100f = Bench::CalculateRssSlopeMbPer100f(sBenchB9FootSamples);
            report.memDetailed = true;

            const Bench::GpuMemBreakdown gpuBd = Bench::GpuMem::GetBreakdown();
            report.memGpuEstMb = gpuBd.TotalMb();
            report.memGpuEstBreakdown = gpuBd.ToJson();

            if (sBenchB2Render3DIdx >= 0)
            {
               if (auto* gn = FindNodeByIndex(sBenchB2Render3DIdx))
               {
                  if (auto* r = dynamic_cast<Render3DNode*>(gn->node.get()))
                  {
                     if (sBenchB9Scene == "b4")
                        report.tris = (int)r->LastTriangleCount();
                     else
                     {
                        for (int s = 0; s < Render3DNode::kSlots; s++)
                        {
                           if (r->geometry[s])
                              report.tris += (int)r->geometry[s]->GetMesh().FaceCount();
                        }
                     }
                     report.drawCalls = (int)r->LastDrawCalls();
                  }
               }
            }

            if (sBenchB2OutputIdx >= 0)
            {
               if (auto* outGn = FindNodeByIndex(sBenchB2OutputIdx))
               {
                  if (auto* outNode = dynamic_cast<OutputNode*>(outGn->node.get()))
                  {
                     glBindFramebuffer(GL_READ_FRAMEBUFFER, outNode->GetFbo().fbo);
                     report.outputHash = Bench::HashFramebufferRGBA8(outNode->GetOutputWidth(), outNode->GetOutputHeight());
                     glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                  }
               }
            }

            report.Emit();
            printf("B9MEMORY DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B10 Offline render and A/V sync (docs/plans/perf/benchmark-suite.md
      // §4): the B3-shaped scene (built above, shared with B3/B7) rendered
      // for INFINITE_BENCH_B10SECONDS (default 30s) through the real
      // Arrangement offline path - StartOfflineRenderSession(out, 0, 0,
      // /*isArrange=*/true), the same call ArrangeRenderExecuteJob makes -
      // then the written movie is redecoded and its markers correlated
      // exactly the way INFINITE_RECEXPORTTEST does for its own synthetic
      // take. OutputNode.cpp's B10BenchActive()/B10MarkerAt() lay the
      // tone+flash pair over this scene's own real audio/video, strictly
      // under INFINITE_BENCH_B10, so B3/B7 (which build the identical scene)
      // are untouched.
      if (getenv("INFINITE_BENCH_B10") != nullptr)
      {
         static bool sB10Started = false;
         static bool sB10Done = false;
         static int sB10DoneFrame = -1;
         static double sB10WallStart = 0.0;
         static double sB10WallEnd = 0.0;
         static int sB10Fps = 30;
         static int sB10DurationSeconds = 30;
         static std::string sB10Path;
         static std::string sB10OutputHash;

         auto b10Output = []() -> OutputNode* {
            if (sBenchB3OutputIdx < 0)
               return nullptr;
            auto* gn = FindNodeByIndex(sBenchB3OutputIdx);
            return gn != nullptr ? dynamic_cast<OutputNode*>(gn->node.get()) : nullptr;
         };

         // Same steady-state-hash capture point B2/B9's anim=0 variant uses
         // (frameId 32->152 warmup/sample window), so this is at least
         // reported from settled content rather than mid-buildout. It is
         // informational only, not a determinism gate: unlike B2/B4's real
         // static variant, B10 renders the B3-shaped scene, whose
         // macro/gesture playback and Prediction modulators run on
         // wall-clock time regardless of B10's own anim=0/1 flag (that flag
         // only silences the B2-lite visual layer). Confirmed empirically -
         // two anim=0 runs of the same commit, both settled to frame 152,
         // produced different output_hash values - so scripts/bench/compare.py
         // classifies B10 as nondeterministic the same way it already does
         // for B3/B9, and never gates on its hash.
         if (frameId == 2)
         {
            gVsync = false;
            SetCanvasSwapInterval(0);
            gTargetFps = 0;
         }

         if (frameId == 152)
         {
            if (OutputNode* out = b10Output())
            {
               glBindFramebuffer(GL_READ_FRAMEBUFFER, out->GetFbo().fbo);
               sB10OutputHash = Bench::HashFramebufferRGBA8(out->GetOutputWidth(), out->GetOutputHeight());
               glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }
         }

         if (!sB10Started && frameId == 153)
         {
            sB10Started = true;
            OutputNode* out = b10Output();
            if (out == nullptr)
            {
               printf("[FAIL] B10: no Output node built for the B3-shaped scene\n");
               Bench::BenchReport report;
               report.bench = "B10_offline_av_sync";
               report.frames = 0;
               report.Emit();
               printf("B10 DONE\n");
               fflush(stdout);
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            else
            {
               sB10DurationSeconds = getenv("INFINITE_BENCH_B10SECONDS")
                  ? std::max(5, atoi(getenv("INFINITE_BENCH_B10SECONDS"))) : 30;
               sB10Fps = 30;
               sB10Path = TmpPath("infinite_bench_b10.mp4");
               std::remove(sB10Path.c_str());
               out->recordVideoPath = sB10Path;
               out->videoFormat = 0;
               out->offlineFps = sB10Fps;
               out->offlineDurationSeconds = sB10DurationSeconds;
               out->offlineTotalFramesOverride = sB10Fps * sB10DurationSeconds;
               out->includeAudio = true;
               sB10WallStart = glfwGetTime();
               StartOfflineRenderSession(out, 0, 0, /*isArrange=*/true);
               if (!gOfflineRender.active)
               {
                  printf("[FAIL] B10 could not start the offline take: %s\n", out->RecordStatus().c_str());
                  Bench::BenchReport report;
                  report.bench = "B10_offline_av_sync";
                  report.frames = 0;
                  report.Emit();
                  printf("B10 DONE\n");
                  fflush(stdout);
                  glfwSetWindowShouldClose(window, GLFW_TRUE);
               }
            }
         }

         if (sB10Started && !sB10Done && frameId > 3)
         {
            if (!gOfflineRender.active && b10Output() != nullptr && !b10Output()->IsOfflineFinalizing())
            {
               sB10Done = true;
               sB10DoneFrame = frameId;
               sB10WallEnd = glfwGetTime();
            }
         }

         // Same settle margin OFFLINERENDERTEST uses before inspecting the
         // file: AVFoundation's own asset metadata can lag the bytes this
         // same process just finished writing by a beat or two.
         if (sB10Done && sB10DoneFrame >= 0 && frameId == sB10DoneFrame + 60)
         {
            OutputNode* out = b10Output();
            Bench::BenchReport report;
            report.bench = "B10_offline_av_sync";
            // compare.py's deterministic()/normalize_variant() key off this
            // string the same way every other bench's variant does - needs
            // its own anim=/seconds= tags (b3IsAnim/sB10DurationSeconds),
            // not B3's "scale=s" wording, which carries neither.
            const bool b10AnimForReport =
               (getenv("INFINITE_BENCH_B10ANIM") == nullptr || strcmp(getenv("INFINITE_BENCH_B10ANIM"), "0") != 0);
            report.variant = "seconds=" + std::to_string(sB10DurationSeconds) +
                              ",anim=" + (b10AnimForReport ? std::string("1") : std::string("0"));
            report.frames = out != nullptr ? out->LastRecordedFrames() : 0;
            report.nodes = (int)gNodes.size();
            report.outputHash = sB10OutputHash;

            const int expectedFrames = sB10Fps * sB10DurationSeconds;
            const int wroteFrames = out != nullptr ? out->LastRecordedFrames() : 0;
            const int droppedFrames = out != nullptr ? out->LastDroppedFrames() : 0;
            const double wallSeconds = std::max(0.001, sB10WallEnd - sB10WallStart);
            const double takeSeconds = (double)sB10DurationSeconds;
            const double realtimeFactor = takeSeconds / wallSeconds;

            const Platform::MovieInfo info = Platform::InspectMovie(sB10Path);

            printf("[B10] wrote=%d expected=%d dropped=%d wall=%.2fs realtime_factor=%.2fx "
                   "file_duration=%.2fs\n",
                   wroteFrames, expectedFrames, droppedFrames, wallSeconds, realtimeFactor,
                   info.duration);

            // ---- redecode ----
            // Video markers reuse INFINITE_RECEXPORTTEST's plain luminance
            // threshold below (the flash dominates B3's own visuals). Audio
            // markers can't: B3's own mix is loud for most of the take, so a
            // broadband amplitude threshold produces a "loud" run for most
            // of the clip and misses the quiet-to-loud rising edge the tone
            // burst is supposed to create (this is exactly how the first
            // real run of this fixture failed - 1 of 5 audio onsets found).
            // Instead, look for narrowband energy at B10Bench::kToneHz via a
            // per-window Goertzel, which the ambient mix is very unlikely to
            // spike at the same instant, and threshold against a baseline
            // computed from the clip itself rather than a fixed constant.
            Platform::SampleBuffer audio;
            std::string audioErr;
            std::vector<double> audioOnsets;
            if (Platform::DecodeVideoAudioTrackToBuffer(sB10Path, audio, audioErr) && audio.numFrames > 0)
            {
               // 20ms window: at both 44.1kHz (882 samples) and 48kHz (960
               // samples) this lands B10Bench::kToneHz=1000Hz on an exact
               // Goertzel bin (k=20), so there's no spectral leakage to
               // widen the peak or bias the baseline.
               const int win = (int)std::lround(audio.sampleRate * 0.02);
               const double k = std::floor(0.5 + (double)win * B10Bench::kToneHz / audio.sampleRate);
               const double omega = (2.0 * M_PI / (double)win) * k;
               const double coeff = 2.0 * std::cos(omega);

               std::vector<double> power;
               std::vector<double> windowStartSec;
               for (int i = 0; win > 0 && i + win <= audio.numFrames; i += win)
               {
                  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
                  for (int n = 0; n < win; n++)
                  {
                     s0 = (double)audio.channelData[(size_t)(i + n)] + coeff * s1 - s2;
                     s2 = s1;
                     s1 = s0;
                  }
                  power.push_back(s2 * s2 + s1 * s1 - coeff * s1 * s2);
                  windowStartSec.push_back((double)i / audio.sampleRate);
               }

               if (!power.empty())
               {
                  // Baseline = median window power (robust to the 5 marker
                  // spikes themselves, which are a small fraction of a 30s
                  // clip's ~1500 windows).
                  std::vector<double> sorted = power;
                  std::sort(sorted.begin(), sorted.end());
                  const double median = sorted[sorted.size() / 2];
                  const double threshold = std::max(median * 8.0, 1e-6);

                  // A single-window threshold crossing isn't enough to call
                  // it a marker: real B3 content can clear the adaptive
                  // threshold for a window or two by coincidence (this is
                  // how the Goertzel-only version of this detector produced
                  // 6 onsets for 5 real bursts - one short spurious blip).
                  // Each real burst is B10Bench::kMarkerSeconds long, so
                  // require the "loud" run to sustain for most of that
                  // before counting it, with margin for window/burst
                  // boundary misalignment on either end.
                  const double windowSeconds = (double)win / audio.sampleRate;
                  const int minSustainWindows =
                     std::max(1, (int)std::floor(B10Bench::kMarkerSeconds / windowSeconds * 0.6));

                  bool loud = false;
                  size_t runStart = 0;
                  int runLen = 0;
                  for (size_t i = 0; i <= power.size(); i++)
                  {
                     const bool nowLoud = i < power.size() && power[i] > threshold;
                     if (nowLoud && !loud)
                     {
                        runStart = i;
                        runLen = 1;
                     }
                     else if (nowLoud && loud)
                     {
                        runLen++;
                     }
                     else if (!nowLoud && loud)
                     {
                        if (runLen >= minSustainWindows)
                           audioOnsets.push_back(windowStartSec[runStart]);
                        runLen = 0;
                     }
                     loud = nowLoud;
                  }
               }
            }
            else
            {
               printf("  [FAIL] B10 could not decode the take's audio track: %s\n", audioErr.c_str());
            }

            std::vector<double> videoOnsets;
            std::string videoErr;
            if (Platform::VideoHandle* vid = Platform::VideoOpen(sB10Path, videoErr))
            {
               const int vw = Platform::VideoWidth(vid);
               const int vh = Platform::VideoHeight(vid);
               std::vector<unsigned char> px;
               bool bright = false;
               const double stepSeconds = 1.0 / ((double)sB10Fps * 4.0);
               for (double t = 0.0; t < takeSeconds; t += stepSeconds)
               {
                  bool gotFrame = Platform::VideoFrameAt(vid, t, px);
                  for (int waitedMs = 0;
                       !gotFrame && waitedMs < 2000 && Platform::VideoDecodeIsCatchingUp(vid);
                       waitedMs++)
                  {
                     std::this_thread::sleep_for(std::chrono::milliseconds(1));
                     gotFrame = Platform::VideoFrameAt(vid, t, px);
                  }
                  if (!gotFrame && px.empty())
                     continue;
                  if ((int)px.size() < vw * vh * 4)
                     continue;
                  double sum = 0.0;
                  int count = 0;
                  for (int y = vh / 4; y < vh * 3 / 4; y += 4)
                     for (int x = vw / 4; x < vw * 3 / 4; x += 4)
                     {
                        sum += px[((size_t)y * vw + x) * 4];
                        count++;
                     }
                  const double lum = count > 0 ? sum / count / 255.0 : 0.0;
                  const bool nowBright = lum > 0.5;
                  if (nowBright && !bright)
                     videoOnsets.push_back(t);
                  bright = nowBright;
               }
               Platform::VideoClose(vid);
            }
            else
            {
               printf("  [FAIL] B10 could not open the take for decoding: %s\n", videoErr.c_str());
            }

            printf("[B10] markers found: audio=%d video=%d (expected %d)\n",
                   (int)audioOnsets.size(), (int)videoOnsets.size(), B10Bench::kMarkerCount);

            bool markersOk = (int)audioOnsets.size() == B10Bench::kMarkerCount &&
                              (int)videoOnsets.size() == B10Bench::kMarkerCount;
            double driftEndMs = 0.0, driftWorstMs = 0.0, ebuWorstEarlyMs = 0.0, ebuWorstLateMs = 0.0;
            nlohmann::json markerDeltasMs = nlohmann::json::array();
            if (markersOk)
            {
               std::vector<double> deltaMs(B10Bench::kMarkerCount);
               for (int m = 0; m < B10Bench::kMarkerCount; m++)
               {
                  deltaMs[m] = (videoOnsets[m] - audioOnsets[m]) * 1000.0;
                  markerDeltasMs.push_back(deltaMs[m]);
                  printf("  marker %d: audio %.3fs video %.3fs video-minus-audio %+.0fms\n",
                         m + 1, audioOnsets[m], videoOnsets[m], deltaMs[m]);
                  ebuWorstEarlyMs = std::max(ebuWorstEarlyMs, deltaMs[m]);   // audio early: delta > 0
                  ebuWorstLateMs = std::max(ebuWorstLateMs, -deltaMs[m]);   // audio late: delta < 0
               }
               driftEndMs = std::fabs(deltaMs.back() - deltaMs.front());
               for (int m = 0; m < B10Bench::kMarkerCount; m++)
                  driftWorstMs = std::max(driftWorstMs, std::fabs(deltaMs[m] - deltaMs.front()));
            }

            const double frameMs = 1000.0 / (double)sB10Fps;
            const bool avSyncEndOk = markersOk && driftEndMs <= frameMs * 1.0 + 1.0;
            const bool avSyncWorstOk = markersOk && driftWorstMs <= frameMs * 2.0 + 1.0;
            // EBU R37: audio at most 40ms early, 60ms late.
            const bool ebuR37Ok = markersOk && ebuWorstEarlyMs <= 40.0 && ebuWorstLateMs <= 60.0;
            const bool framesOk = wroteFrames == expectedFrames;
            const bool droppedOk = droppedFrames == 0;
            const bool overallOk = markersOk && avSyncEndOk && avSyncWorstOk && ebuR37Ok &&
                                    framesOk && droppedOk;

            printf("[%s] B10 AV SYNC END: %.0fms (tolerance %.0fms)\n",
                   avSyncEndOk ? "pass" : "FAIL", driftEndMs, frameMs * 1.0 + 1.0);
            printf("[%s] B10 AV SYNC WORST: %.0fms (tolerance %.0fms)\n",
                   avSyncWorstOk ? "pass" : "FAIL", driftWorstMs, frameMs * 2.0 + 1.0);
            printf("[%s] B10 EBU R37: worst early %.0fms (<=40) worst late %.0fms (<=60)\n",
                   ebuR37Ok ? "pass" : "FAIL", ebuWorstEarlyMs, ebuWorstLateMs);
            printf("[%s] B10 FRAMES: wrote=%d expected=%d dropped=%d\n",
                   (framesOk && droppedOk) ? "pass" : "FAIL", wroteFrames, expectedFrames, droppedFrames);

            nlohmann::json offline;
            offline["realtime_factor"] = realtimeFactor;
            offline["wall_seconds"] = wallSeconds;
            offline["take_seconds"] = takeSeconds;
            offline["fps"] = sB10Fps;
            offline["frames_written"] = wroteFrames;
            offline["frames_expected"] = expectedFrames;
            offline["dropped"] = droppedFrames;
            offline["file_duration_sec"] = info.duration;
            offline["markers_found_audio"] = (int)audioOnsets.size();
            offline["markers_found_video"] = (int)videoOnsets.size();
            offline["markers_expected"] = B10Bench::kMarkerCount;
            offline["marker_delta_ms"] = markerDeltasMs;
            offline["drift_end_ms"] = driftEndMs;
            offline["drift_end_frames"] = driftEndMs / frameMs;
            offline["drift_worst_ms"] = driftWorstMs;
            offline["drift_worst_frames"] = driftWorstMs / frameMs;
            offline["ebu_r37_worst_early_ms"] = ebuWorstEarlyMs;
            offline["ebu_r37_worst_late_ms"] = ebuWorstLateMs;
            offline["av_sync_end_ok"] = avSyncEndOk;
            offline["av_sync_worst_ok"] = avSyncWorstOk;
            offline["ebu_r37_ok"] = ebuR37Ok;
            offline["frames_ok"] = framesOk;
            offline["dropped_ok"] = droppedOk;
            offline["overall_ok"] = overallOk;
            report.offlineRender = offline;

            // Generic targets_pass, same mechanism every other fixture's
            // gated verdicts use (README/compare.py surface a false one as
            // a TARGET regardless of bench name) - null when the markers
            // didn't round-trip cleanly enough to judge at all, same
            // "unproven, not failed" convention B7's soak targets use.
            report.targetsPass["b10_av_sync_end_le_1_frame"] =
               markersOk ? nlohmann::json(avSyncEndOk) : nlohmann::json(nullptr);
            report.targetsPass["b10_av_sync_worst_le_2_frames"] =
               markersOk ? nlohmann::json(avSyncWorstOk) : nlohmann::json(nullptr);
            report.targetsPass["b10_ebu_r37"] =
               markersOk ? nlohmann::json(ebuR37Ok) : nlohmann::json(nullptr);
            report.targetsPass["b10_frames_exact"] = framesOk;
            report.targetsPass["b10_dropped_zero"] = droppedOk;
            report.targetsPass["b10_markers_roundtrip"] = markersOk;

            report.Emit();
            printf("%s\n", overallOk ? "B10 OK" : "B10 FAIL");
            printf("B10 DONE\n");
            fflush(stdout);
            std::remove(sB10Path.c_str());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(e) startup-breakdown fixture (benchmark-suite.md §4): measures
      // time spent across startup milestones (pre_window, window_gl, imgui_fonts,
      // scanners_load, settings_init, first_frame_render, total_to_first_frame).
      if (getenv("INFINITE_BENCH_B5STARTUP") != nullptr)
      {
         if (frameId == 1)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_startup";
            report.frames = 1;
            report.nodes = (int)gNodes.size();
            report.frameMs.Push(sFirstFrameEndMs - tPreWindow);
            report.stagesCpuMs = {
               { "pre_window", tWindowGl - tPreWindow },
               { "window_gl", tImGuiFonts - tWindowGl },
               { "imgui_fonts", tScanners - tImGuiFonts },
               { "scanners_load", tSettingsInit - tScanners },
               { "first_frame_render", sFirstFrameEndMs - tSettingsInit },
               { "total_to_first_frame", sFirstFrameEndMs - tPreWindow },
            };
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5STARTUP DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(f) patch load/save time fixture (benchmark-suite.md §4). Fires
      // once the mixed-node grid (INFINITE_BENCH_B5LOADSAVE's setup above)
      // has had a few frames to settle - same frameId margin B5(b)/B5(c)'s
      // sample windows use before trusting node/editor state. Times
      // SavePatchTo and LoadPatchFrom back to back on the real patch I/O
      // path (not a synthetic serialize-only call), so this includes
      // whatever ApplyPatchData/RemapFieldGraphOwnership does on load too.
      if (getenv("INFINITE_BENCH_B5LOADSAVE") != nullptr)
      {
         if (frameId == 32)
         {
            const std::string path = TmpPath("infinite_bench_b5loadsave.inf");
            const double tSaveStart = Bench::ScopedStageTimer::NowMs();
            const bool saved = SavePatchTo(path);
            const double tSaveEnd = Bench::ScopedStageTimer::NowMs();
            const bool loaded = saved && LoadPatchFrom(path);
            const double tLoadEnd = Bench::ScopedStageTimer::NowMs();

            Bench::BenchReport report;
            report.bench = "B5_fundamentals_loadsave";
            report.variant = getenv("INFINITE_BENCH_B5LOADSAVE") ? getenv("INFINITE_BENCH_B5LOADSAVE") : "";
            report.frames = 1;
            report.nodes = (int)gNodes.size();
            report.stagesCpuMs = {
               { "save", tSaveEnd - tSaveStart },
               { "load", tLoadEnd - tSaveEnd },
            };
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5LOADSAVE %s DONE\n", (saved && loaded) ? "OK" : "FAIL");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(g) undo-snapshot time fixture (benchmark-suite.md §4). Same
      // settle margin as B5(f). Times PushUndoCheckpoint (BuildPatchData +
      // push) and Undo (BuildPatchData for the redo entry + ApplyPatchData)
      // back to back on the real undo path, not a synthetic snapshot.
      if (getenv("INFINITE_BENCH_B5UNDO") != nullptr)
      {
         if (frameId == 32)
         {
            const size_t nodesBefore = gNodes.size();
            const double tPushStart = Bench::ScopedStageTimer::NowMs();
            PushUndoCheckpoint();
            const double tPushEnd = Bench::ScopedStageTimer::NowMs();
            Undo();
            const double tUndoEnd = Bench::ScopedStageTimer::NowMs();
            const bool restored = gNodes.size() == nodesBefore;

            Bench::BenchReport report;
            report.bench = "B5_fundamentals_undo";
            report.variant = getenv("INFINITE_BENCH_B5UNDO") ? getenv("INFINITE_BENCH_B5UNDO") : "";
            report.frames = 1;
            report.nodes = (int)nodesBefore;
            report.stagesCpuMs = {
               { "push_checkpoint", tPushEnd - tPushStart },
               { "undo_restore", tUndoEnd - tPushEnd },
            };
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5UNDO %s DONE\n", restored ? "OK" : "FAIL");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B1 Heavy audio fixture, measurement half - see
      // INFINITE_BENCH_B1VOICES's setup above. Windowed on wall-clock
      // (glfwGetTime), not frameId, because the real device callback thread
      // this benchmark exists to measure runs independently of the main
      // loop's frame pacing - a frameId-gated window would conflate video
      // frame rate with audio callback rate. 1s warmup lets the just-opened
      // device settle before sampling; default window is 60s
      // (INFINITE_BENCH_B1SECONDS overrides, for fast iteration while
      // building/debugging this fixture - the doc's own number is 60s).
      // cb_load is drained from AudioEngine::RawLoadHistory() (raw per-block
      // samples - see AudioEngine.h's comment on why LastBlockLoad()'s
      // smoothing is wrong for a percentile) and xruns from Xruns(),
      // baselined at the start of the measurement window so device-open
      // settling doesn't count against this run.
      if (getenv("INFINITE_BENCH_B1VOICES") != nullptr)
      {
         static double sStartTimeS = -1.0;
         static AudioEngine::XrunCounts sXrunBaseline;
         static Bench::PercentileRing sFrameMs;
         static double sRssStartMb = -1.0;
         const double nowS = glfwGetTime();
         const double windowS = getenv("INFINITE_BENCH_B1SECONDS") ? atof(getenv("INFINITE_BENCH_B1SECONDS")) : 60.0;
         if (sStartTimeS < 0.0 && nowS > 1.5)
         {
            sStartTimeS = nowS;
            sXrunBaseline = AudioEngine::Instance().Xruns();
            AudioEngine::Instance().RawLoadHistory().Reset();
            AudioEngine::Instance().ResetStageLoadHistory();
            sRssStartMb = Bench::ProcessRssMb();
         }
         if (sStartTimeS >= 0.0 && gLastFrameMs > 0.0)
            sFrameMs.Push(gLastFrameMs);
         if (sStartTimeS >= 0.0 && nowS - sStartTimeS >= windowS)
         {
            Bench::BenchReport report;
            report.bench = "B1_heavy_audio";
            const char* vArg = getenv("INFINITE_BENCH_B1VOICES");
            const char* bufArg = getenv("INFINITE_BENCH_B1BUFFER");
            report.variant = std::string("voices=") + (vArg ? vArg : "?") +
                              ",buffer=" + (bufArg ? bufArg : "default");
            report.frames = (int)sFrameMs.Count();
            report.nodes = (int)gNodes.size();
            report.frameMs = sFrameMs;
            report.audioMeasured = AudioEngine::Instance().SampleRate() > 0.0;
            report.audioBuffer = bufArg ? atoi(bufArg) : 0;
            report.audioSampleRate = AudioEngine::Instance().SampleRate();
            report.audioLoad = AudioEngine::Instance().RawLoadHistory().Drain();
            BenchFillXruns(report, sXrunBaseline);
            report.memRssStartMb = sRssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            for (int s = 0; s < kAudioStageCount; s++)
            {
               auto drained = AudioEngine::Instance().StageLoadHistory(s).Drain();
               if (!drained.Empty())
                  report.stagesCpuMs[AudioStageName(s)] = drained.Percentile(50);
            }
            report.Emit();
            printf("B1VOICES DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(a) empty-patch fixture, measurement half - see
      // INFINITE_BENCH_B5EMPTY's setup above. Same warmup/sample window as
      // B5(b) (INFINITE_BENCH_B5NODES) so the two are directly comparable -
      // this run's frame_ms percentiles are the floor B5(b)'s node-count
      // sweep is measured against.
      if (getenv("INFINITE_BENCH_B5EMPTY") != nullptr)
      {
         static Bench::PercentileRing sFrameMs;
         static double sRssStartMb = -1.0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; sRssStartMb = Bench::ProcessRssMb(); }
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
            sFrameMs.Push(gLastFrameMs);
         if (frameId == 152)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_empty";
            report.frames = 152;
            report.nodes = (int)gNodes.size();
            report.frameMs = sFrameMs;
            report.memRssStartMb = sRssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5EMPTY DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(d) audio-thread-alone fixture, measurement half - see
      // INFINITE_BENCH_B5AUDIOALONE's setup above. Same wall-clock-window
      // reasoning as B1's measurement half (the audio callback runs
      // independently of frameId) but no effects chain and no per-voice
      // sweep - this isolates the callback's fixed per-block overhead at one
      // buffer size from B1's DSP-graph cost.
      if (getenv("INFINITE_BENCH_B5AUDIOALONE") != nullptr)
      {
         static double sStartTimeS = -1.0;
         static AudioEngine::XrunCounts sXrunBaseline;
         const double nowS = glfwGetTime();
         const double windowS = getenv("INFINITE_BENCH_B5AUDIOALONE_SECONDS")
                                    ? atof(getenv("INFINITE_BENCH_B5AUDIOALONE_SECONDS")) : 30.0;
         if (sStartTimeS < 0.0 && nowS > 1.0)
         {
            sStartTimeS = nowS;
            sXrunBaseline = AudioEngine::Instance().Xruns();
            AudioEngine::Instance().RawLoadHistory().Reset();
         }
         if (sStartTimeS >= 0.0 && nowS - sStartTimeS >= windowS)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_audioalone";
            const char* bufArg = getenv("INFINITE_BENCH_B5AUDIOALONE");
            report.variant = std::string("buffer=") + bufArg;
            report.nodes = (int)gNodes.size();
            report.audioMeasured = AudioEngine::Instance().SampleRate() > 0.0;
            report.audioBuffer = atoi(bufArg);
            report.audioSampleRate = AudioEngine::Instance().SampleRate();
            report.audioLoad = AudioEngine::Instance().RawLoadHistory().Drain();
            BenchFillXruns(report, sXrunBaseline);
            report.Emit();
            printf("B5AUDIOALONE DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Node-chain cook-recursion stress fixture, measurement half - see
      // INFINITE_NODECHAINPERFTEST's setup above. The chain's Output node is
      // already cooked every frame by the ordinary per-frame cook pass above
      // ("apply modulation and palette"), so unlike GEOMDENSITYTEST/
      // RESSWEEPTEST there is no explicit CookIfNeeded call here - this is
      // deliberately measuring the real per-frame cost the reported bug is
      // about, not an isolated call. Same warmup/sample window as those
      // fixtures (32 frames warmup, 120 sampled) so results are comparable.
      if (getenv("INFINITE_NODECHAINPERFTEST") != nullptr)
      {
         static double sSum = 0.0, sMin = 1e30, sMax = 0.0;
         static int sSampleCount = 0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; }
         if (getenv("INFINITE_NODECHAINPERFTEST_VERBOSE") != nullptr)
         {
            fprintf(stderr, "[nodechain] frame=%d lastMs=%.2f\n", frameId, gLastFrameMs);
            fflush(stderr);
         }
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
         {
            sSum += gLastFrameMs;
            sMin = std::min(sMin, gLastFrameMs);
            sMax = std::max(sMax, gLastFrameMs);
            sSampleCount++;
         }
         if (frameId == 152)
         {
            const double avg = sSampleCount > 0 ? sSum / sSampleCount : 0.0;
            const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
            printf("NODECHAINPERFTEST chainLen=%d samples=%d avgMs=%.2f minMs=%.2f maxMs=%.2f avgFps=%.1f\n",
                   (int)gNodes.size() - 2, sSampleCount, avg, sMin, sMax, fps);
            printf("%s\n", sSampleCount > 0 ? "NODECHAINPERFTEST DONE" : "NODECHAINPERFTEST FAIL (no samples)");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Realistic-shape stress fixture, measurement half - see
      // INFINITE_MIXEDSTRESSTEST's setup above (before the main loop) and
      // its modulation-binding half above (frameId==1). Render 3D isn't an
      // Output/Syphon/OscSend node, so - same as GEOMDENSITYTEST - it needs
      // an explicit CookIfNeeded call here; the audio rack runs on its own
      // via the real-time audio device once RebuildAudioTopology() was
      // called in setup, so its CPU cost shows up as competition for the
      // same machine, not as anything this loop calls directly. The
      // Render3DNode* is found once by scanning gNodes (dynamic_cast) and
      // cached from then on - a raw pointer into the node's own heap
      // allocation (via unique_ptr), which stays valid even though the
      // frameId==1 binder above spawns more nodes and reallocates gNodes'
      // vector storage after this pointer is taken.
      if (getenv("INFINITE_MIXEDSTRESSTEST") != nullptr)
      {
         static double sSum = 0.0, sMin = 1e30, sMax = 0.0;
         static int sSampleCount = 0;
         static Render3DNode* sRender = nullptr;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; }
         if (getenv("INFINITE_MIXEDSTRESSTEST_VERBOSE") != nullptr)
         {
            fprintf(stderr, "[mixedstress] frame=%d lastMs=%.2f nodes=%zu\n",
                    frameId, gLastFrameMs, gNodes.size());
            fflush(stderr);
         }
         if (sRender == nullptr)
            for (GraphNode& gn : gNodes)
               if (auto* r = dynamic_cast<Render3DNode*>(gn.node.get())) { sRender = r; break; }
         if (sRender != nullptr && frameId >= 2)
            sRender->CookIfNeeded(frameId);
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
         {
            sSum += gLastFrameMs;
            sMin = std::min(sMin, gLastFrameMs);
            sMax = std::max(sMax, gLastFrameMs);
            sSampleCount++;
         }
         if (frameId == 152)
         {
            const double avg = sSampleCount > 0 ? sSum / sSampleCount : 0.0;
            const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
            printf("MIXEDSTRESSTEST nodes=%d avgMs=%.2f minMs=%.2f maxMs=%.2f avgFps=%.1f\n",
                   (int)gNodes.size(), avg, sMin, sMax, fps);
            printf("%s\n", sSampleCount > 0 ? "MIXEDSTRESSTEST DONE" : "MIXEDSTRESSTEST FAIL (no samples)");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Output-resolution stress fixture, measurement half - see
      // INFINITE_RESSWEEPTEST's setup above. Same warmup/sample/explicit-cook
      // scheme as GEOMDENSITYTEST.
      if (getenv("INFINITE_RESSWEEPTEST") != nullptr)
      {
         static double sSum = 0.0, sMin = 1e30, sMax = 0.0;
         static int sSampleCount = 0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; }
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         if (frameId >= 2)
            render->CookIfNeeded(frameId);
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
         {
            sSum += gLastFrameMs;
            sMin = std::min(sMin, gLastFrameMs);
            sMax = std::max(sMax, gLastFrameMs);
            sSampleCount++;
         }
         if (frameId == 152)
         {
            const double avg = sSampleCount > 0 ? sSum / sSampleCount : 0.0;
            const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
            printf("RESSWEEPTEST res=%dx%d avg=%.3fms min=%.3fms max=%.3fms fps=%.2f\n",
                   (int)render->width, (int)render->height, avg, sMin, sMax, fps);
            printf("%s\n", sSampleCount > 0 ? "RESSWEEPTEST DONE" : "RESSWEEPTEST FAIL (no samples)");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Render quality-knob (MSAA / shadow map size) stress fixture,
      // measurement half - see INFINITE_QUALITYSWEEPTEST's setup above. Same
      // warmup/sample/explicit-cook scheme as GEOMDENSITYTEST.
      if (getenv("INFINITE_QUALITYSWEEPTEST") != nullptr)
      {
         static double sSum = 0.0, sMin = 1e30, sMax = 0.0;
         static int sSampleCount = 0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; }
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         if (frameId >= 2)
            render->CookIfNeeded(frameId);
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
         {
            sSum += gLastFrameMs;
            sMin = std::min(sMin, gLastFrameMs);
            sMax = std::max(sMax, gLastFrameMs);
            sSampleCount++;
         }
         if (frameId == 152)
         {
            const double avg = sSampleCount > 0 ? sSum / sSampleCount : 0.0;
            const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
            printf("QUALITYSWEEPTEST samples=%s(%dx active) shadows=%d shadowSize=%d "
                   "avg=%.3fms min=%.3fms max=%.3fms fps=%.2f\n",
                   render->samples == 0 ? "Off" : render->samples == 1 ? "2x" :
                   render->samples == 2 ? "4x" : "8x",
                   render->ActiveSamples(), render->shadowsEnabled ? 1 : 0,
                   render->ActiveShadowSize(), avg, sMin, sMax, fps);
            printf("%s\n", sSampleCount > 0 ? "QUALITYSWEEPTEST DONE" : "QUALITYSWEEPTEST FAIL (no samples)");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // P0 audio feasibility spike: does a live AVAudioSourceNode synthesis
      // callback coexist with the render loop without an FPS hit or glitches?
      // The heavy visual patch is spawned unconditionally above (before the
      // loop starts) so this measurement has real GPU load from frame 0.
      if (getenv("INFINITE_AUDIOSPIKE") != nullptr)
      {
         static bool sVsyncOff = false;
         static bool sStarted = false;
         static bool sFailed = false;
         static double sStartWallTime = 0.0;
         static double sBeforeSum = 0.0;
         static int sBeforeCount = 0;
         static double sAfterSum = 0.0;
         static int sAfterCount = 0;

         if (!sVsyncOff && frameId == 2)
         {
            gVsync = false;
            SetCanvasSwapInterval(0);
            sVsyncOff = true;
         }

         if (!sStarted && !sFailed && frameId == 120)
         {
            std::string err;
            if (Platform::AudioSpikeStart(err))
            {
               sStarted = true;
               sStartWallTime = glfwGetTime();
               printf("AUDIOSPIKE: engine started at frame %d\n", frameId);
            }
            else
            {
               sFailed = true;
               printf("AUDIOSPIKE: engine failed to start: %s\n", err.c_str());
               printf("AUDIOSPIKE FAIL\n");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (frameId > 2 && gLastFrameMs > 0.0)
         {
            if (!sStarted)
            {
               sBeforeSum += gLastFrameMs;
               sBeforeCount++;
            }
            else
            {
               sAfterSum += gLastFrameMs;
               sAfterCount++;
            }
         }

         if (sStarted && !sFailed && (glfwGetTime() - sStartWallTime) >= 65.0)
         {
            const Platform::AudioSpikeStats stats = Platform::AudioSpikeGetStats();
            Platform::AudioSpikeStop();

            const double beforeAvg = sBeforeCount > 0 ? sBeforeSum / sBeforeCount : 0.0;
            const double afterAvg = sAfterCount > 0 ? sAfterSum / sAfterCount : 0.0;
            const double deltaMs = afterAvg - beforeAvg;
            const double deltaPct = beforeAvg > 0.0 ? (deltaMs / beforeAvg) * 100.0 : 0.0;
            // Half a block period of jitter is a dropped-callback-adjacent
            // wobble, not a glitch; a full block period or more means the
            // audio thread missed its deadline outright.
            const double blockPeriodMs = stats.sampleRate > 0.0
                                             ? 1000.0 * (double)stats.blockSize / stats.sampleRate
                                             : 0.0;

            printf("AUDIOSPIKE stats: sampleRate=%.1f Hz  blockSize=%d  maxJitterMs=%.3f  callbacks=%llu\n",
                   stats.sampleRate, stats.blockSize, stats.maxJitterMs,
                   (unsigned long long)stats.callbackCount);
            printf("AUDIOSPIKE fps: before=%.3f ms/frame (n=%d)  after=%.3f ms/frame (n=%d)  delta=%.3f ms (%.2f%%)\n",
                   beforeAvg, sBeforeCount, afterAvg, sAfterCount, deltaMs, deltaPct);

            const bool fpsOk = std::fabs(deltaPct) < 5.0;
            const bool jitterOk = blockPeriodMs > 0.0 && stats.maxJitterMs < blockPeriodMs;
            const bool ranLongEnough = stats.callbackCount > 0 && sAfterCount > 0;
            const bool ok = fpsOk && jitterOk && ranLongEnough;
            printf("%s\n", ok ? "AUDIOSPIKE OK" : "AUDIOSPIKE SUSPECT");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      FrameTest_GEOTEST(frameId, window);

      FrameTest_DISPLACETEST(frameId, window);

      // NodeViewport (per-node mini viewport) exercised directly rather than
      // through the ImGui toggle/icon plumbing: this is the part that
      // actually uploads a mesh, renders it and draws the selection overlay,
      // so it is what is worth an automated check. gNodes[2] is the Select
      // node (has a face mask), gNodes[4] is the plain untouched cube (none).
      FrameTest_MINIVIEWPORTTEST(frameId, window);

      FrameTest_DEPTHTEST(frameId, window);

      // Points-render-as-points, phase 1 - see the INFINITE_PHASE1TEST spawn
      // block above for the scene. Checked at two points in time so the
      // Particle System check can prove the render keeps advancing rather
      // than freezing on its first frame (the exact bug a missing PointRevision()
      // in the scene cache signature would cause).
      static unsigned long long sPhase1FirstTextureRev = 0;
      static unsigned long long sPhase1FirstParticleRev = 0;
      if (getenv("INFINITE_PHASE1TEST") != nullptr && (frameId == 6 || frameId == 30))
      {
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[1].node.get());
         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         auto* i2p = static_cast<ImageToPointsNode*>(gNodes[4].node.get());
         auto* r = static_cast<Render3DNode*>(gNodes[5].node.get());

         const unsigned long long textureRev = r->TextureRevision();
         const unsigned long long particleRev = particles->PointRevision();

         if (frameId == 6)
         {
            sPhase1FirstTextureRev = textureRev;
            sPhase1FirstParticleRev = particleRev;

            const bool cloudsWired = r->geometry[0] == static_cast<IGeometrySource*>(m2p) &&
                                     r->geometry[1] == static_cast<IGeometrySource*>(particles) &&
                                     r->geometry[2] == static_cast<IGeometrySource*>(i2p) &&
                                     r->geometry[0]->GetPointCloud() != nullptr &&
                                     r->geometry[1]->GetPointCloud() != nullptr &&
                                     r->geometry[2]->GetPointCloud() != nullptr;
            printf("phase1 clouds wired: slot0=%d slot1(pure cloud)=%d slot2=%d  %s\n",
                   (int)(r->geometry[0]->GetPointCloud() != nullptr),
                   (int)(r->geometry[1]->GetPointCloud() != nullptr),
                   (int)(r->geometry[2]->GetPointCloud() != nullptr), cloudsWired ? "OK" : "FAIL");

            const size_t pointCount = m2p->GetPoints().size();
            printf("phase1 mesh to points sprites: %zu points, drawcalls=%zu triangles=%zu  %s\n",
                   pointCount, r->LastDrawCalls(), r->LastTriangleCount(),
                   (pointCount > 0 && r->LastDrawCalls() >= 3 && r->LastTriangleCount() > 0)
                      ? "OK" : "FAIL");

            printf("phase1 image to points: %zu points  %s\n", i2p->PointCount(),
                   i2p->PointCount() > 0 ? "OK" : "FAIL");
         }
         else // frameId == 30
         {
            const bool particleAdvanced = particleRev != sPhase1FirstParticleRev;
            const bool renderAdvanced = textureRev != sPhase1FirstTextureRev;
            printf("phase1 particle system animates through render: particleRev %llu->%llu, "
                   "renderRev %llu->%llu  %s\n",
                   sPhase1FirstParticleRev, particleRev, sPhase1FirstTextureRev, textureRev,
                   (particleAdvanced && renderAdvanced) ? "OK" : "FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // antialiased silhouette from ordinary shading gradients; differencing
      // two renders of an identical scene can, because only the edges move.
      FrameTest_3DTEST(frameId, window);

      FrameTest_UISCALETEST(frameId, window);
      FrameTest_UITYPETEST(frameId, window);
      FrameTest_UILAYOUTTEST(frameId, window);

      FrameTest_SIZETEST(frameId, window);

      FrameTest_SAMPLERDRAGTEST_2(frameId, window);

      FrameTest_PLUGINDRAGTEST_2(frameId, window);

      FrameTest_MEDIADRAGTEST_2(frameId, window);

      FrameTest_WTDRAGTEST_2(frameId, window);

#ifndef NDEBUG
      if (getenv("INFINITE_PREDBINDTEST") != nullptr)
      {
         // gParamPinScreenList is canvas space; the synthetic mouse needs real pixels.
         const ImVec2 mn = ed::CanvasToScreen(ImVec2(gPredTestSliderCanvas.x, gPredTestSliderCanvas.y));
         const ImVec2 mx = ed::CanvasToScreen(ImVec2(gPredTestSliderCanvas.z, gPredTestSliderCanvas.w));
         gPredTestSliderScreen = ImVec4(mn.x, mn.y, mx.x, mx.y);
      }
#endif
      if (getenv("INFINITE_EQDRAGTEST") != nullptr)
      {
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }

         const ImVec2 mn = ed::CanvasToScreen(ImVec2(gEqTestRect.x, gEqTestRect.y));
         const ImVec2 mx = ed::CanvasToScreen(ImVec2(gEqTestRect.z, gEqTestRect.w));
         gEqTestScreen = ImVec4(mn.x, mn.y, mx.x, mx.y);

         static bool sEqDragOk = true;
         static float sSnap[25];
         AudioEffectNode* eq = gNodes.empty() ? nullptr : static_cast<AudioEffectNode*>(gNodes[0].node.get());
         auto snapshot = [&](float* dst) {
            int idx = 0;
            for (int b = 0; b < 5; b++)
            {
               dst[idx++] = eq->Param(kEqTypeParam[b]);
               dst[idx++] = eq->Param(kEqFreqParam[b]);
               dst[idx++] = eq->Param(kEqQParam[b]);
               dst[idx++] = eq->Param(kEqGainParam[b]);
               dst[idx++] = eq->Param(kEqOnParam[b]);
            }
         };
         // Per-param tolerance, not one blanket epsilon: type/on are
         // threshold-quantized (0/1 or an integer index) and must be exactly
         // unchanged, but freq/q/gain are read back off pixel-mapped mouse
         // coordinates through a log/pow round trip - even the intentionally-
         // held-constant axis of a drag (e.g. band3's own gain while only its
         // freq is being dragged, since UsesGain keeps rewriting gain from
         // the held Y every active frame) can drift by a fraction of a unit
         // from float/quantization noise alone, without indicating a real bug.
         auto sameExcept = [](const float* a, const float* b, int skipIdx) {
            static const float kTol[5] = { 1.0e-4f, 5.0f, 0.05f, 0.5f, 1.0e-4f }; // type,freq,q,gain,on
            for (int i = 0; i < 25; i++)
               if (i != skipIdx && std::fabs(a[i] - b[i]) > kTol[i % 5])
                  return false;
            return true;
         };
         const int band3FreqIdx = 2 * 5 + 1, band3QIdx = 2 * 5 + 2;
         const int band2OnIdx = 1 * 5 + 4;

         if (eq != nullptr && frameId == 53)
         {
            snapshot(sSnap);
            printf("EQDRAG start: rect=(%.0f,%.0f,%.0f,%.0f)\n", gEqTestScreen.x, gEqTestScreen.y,
                   gEqTestScreen.z, gEqTestScreen.w);
         }
         if (eq != nullptr && frameId == 60)
         {
            float now[25];
            snapshot(now);
            const bool ok = now[band3FreqIdx] > sSnap[band3FreqIdx] + 5.0f && sameExcept(sSnap, now, band3FreqIdx);
            printf("EQDRAG band3 dot drag: freq %.1f -> %.1f  %s\n", sSnap[band3FreqIdx], now[band3FreqIdx],
                   ok ? "OK" : "FAIL");
            sEqDragOk &= ok;
            std::copy(now, now + 25, sSnap);
         }
         if (eq != nullptr && frameId == 68)
         {
            float now[25];
            snapshot(now);
            const bool ok = now[band3QIdx] > sSnap[band3QIdx] + 0.1f && sameExcept(sSnap, now, band3QIdx);
            printf("EQDRAG band3 Shift-drag: Q %.2f -> %.2f  %s\n", sSnap[band3QIdx], now[band3QIdx],
                   ok ? "OK" : "FAIL");
            sEqDragOk &= ok;
            std::copy(now, now + 25, sSnap);
         }
         if (eq != nullptr && frameId == 78)
         {
            float now[25];
            snapshot(now);
            const bool ok =
               std::fabs(now[band2OnIdx] - sSnap[band2OnIdx]) > 0.5f && sameExcept(sSnap, now, band2OnIdx);
            printf("EQDRAG band2 double-click bypass: on %.0f -> %.0f  %s\n", sSnap[band2OnIdx], now[band2OnIdx],
                   ok ? "OK" : "FAIL");
            sEqDragOk &= ok;
            printf("%s\n", sEqDragOk ? "EQDRAG OK" : "EQDRAG FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      FrameTest_DRAGTEST(frameId, window);

      FrameTest_KBCURSORTEST(frameId, window);

      FrameTest_KBDISCRETETEST(frameId, window);

      FrameTest_INPUTTEST_2(frameId, window);

      fc.timerNodeBodies.emplace(benchStagesCpuSample ? &sStageNodeBodies : nullptr, Bench::FrameTail::kNodeBodies);
   auto& timerNodeBodies = *fc.timerNodeBodies;
      fc.timerNodeBodiesGpu.emplace(benchStagesSample ? &sGpuTimerRing : nullptr, "node_bodies", frameId);
   auto& timerNodeBodiesGpu = *fc.timerNodeBodiesGpu;
      PruneDeadGroups();

      b6TrackVis = isBenchB6 && (frameId >= 32 && frameId < sBenchB6TotalFrames);
      b6FrameVisibleCount = 0;
      b6FrameBodiesDrawnCount = 0;
      b6FrameOffscreenMs = 0.0;

      RefreshLiveIssues();}
}

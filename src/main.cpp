#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "app/AppShared.h"
#include "app/frame/FrameTests.h"

namespace app
{


}

using namespace app;


// Resolves the main window's DPI + the manual slider into UiScale::Current(), hands the point
// scale to the GLFW backend and (re)bakes the font atlas at the resulting physical size. Safe
// to call again between frames: the atlas is rebuilt from scratch and, when the GL renderer
// already exists, its font texture is recreated. Style metrics are never rescaled here -
// they are in points like everything else, so there is nothing to compound.
static void ApplyUiScale(GLFWwindow* window, bool rendererReady)
{
   float xscale = 1.0f, yscale = 1.0f;
   glfwGetWindowContentScale(window, &xscale, &yscale);
   int winW = 0, winH = 0, fbW = 0, fbH = 0;
   glfwGetWindowSize(window, &winW, &winH);
   glfwGetFramebufferSize(window, &fbW, &fbH);
   const UiScale::Result r = UiScale::Resolve(xscale, winW, fbW, CategoryColors::GetUiScale());
   UiScale::Current() = r;
   ImGui_ImplGlfw_SetPointScale(r.pointScale);

   ImGuiIO& io = ImGui::GetIO();
   if (rendererReady)
      ImGui_ImplOpenGL3_DestroyFontsTexture();
   io.Fonts->Clear();

   const float bakedPx = UiScale::BakedFontPx(r.bakeScale);
   const std::string bundledInter = BundledResourcePath("fonts/Inter-Regular.ttf");
   std::string chosenFontPath;
   {
      const std::string wanted = CategoryColors::GetUiFont();
      for (const InterfaceFont& f : kInterfaceFonts)
         if (wanted == f.id && wanted[0] != '\0')
            chosenFontPath = BundledResourcePath(f.file);
   }
   const char* candidates[] = {
      chosenFontPath.c_str(), // empty for the default, or if the bundled file is missing
      bundledInter.c_str(),
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/HelveticaNeue.ttc",
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
   };
   // ImGui bakes only Basic Latin + Latin-1 unless told otherwise, so names in other scripts
   // (ł, ő, Ж, λ, ạ) drew as '?'. Latin Extended-A/B, Greek, Cyrillic, Latin Extended
   // Additional, general punctuation and currency symbols are all in the bundled Inter; CJK is
   // not baked (thousands of glyphs, and no bundled face has them).
   static const ImWchar kUiGlyphRanges[] = {
      0x0020, 0x00FF, 0x0100, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF,
      0x1E00, 0x1EFF, 0x2000, 0x206F, 0x20A0, 0x20CF, 0
   };
   ImFont* uiFont = nullptr;
   const char* uiFontPath = nullptr;
   for (const char* path : candidates)
   {
      if (path[0] == '\0')
         continue;
      uiFont = io.Fonts->AddFontFromFileTTF(path, bakedPx, nullptr, kUiGlyphRanges);
      if (uiFont != nullptr)
      {
         uiFontPath = path;
         break;
      }
   }
   // A face that lacks those scripts (Atkinson has no Greek or Cyrillic) borrows Inter's glyphs
   // for the ones it is missing; merged glyphs never replace the primary face's own.
   if (uiFont != nullptr && !bundledInter.empty() && std::strcmp(uiFontPath, bundledInter.c_str()) != 0)
   {
      ImFontConfig fallbackCfg;
      fallbackCfg.MergeMode = true;
      io.Fonts->AddFontFromFileTTF(bundledInter.c_str(), bakedPx, &fallbackCfg, kUiGlyphRanges);
   }
   // Only a real TTF is baked at bakedPx; ImGui's bitmap fallback is 13 px at 1x and must
   // not be shrunk.
   io.FontGlobalScale = uiFont != nullptr ? r.fontGlobalScale : 1.0f;
   if (uiFont == nullptr)
      io.Fonts->AddFontDefault();

   // Merge a small slice of the Lucide icon font (external/icons/Lucide,
   // ISC license) into the same atlas at PUA codepoints, so icon glyphs
   // can be dropped into ordinary ImGui::Text/Button calls alongside UI
   // text (see IconsLucide.h). MergeMode=true means it rides the same
   // baseline/line-height as the font just loaded rather than becoming a
   // separate selectable font - the standard ImGui icon-font idiom.
   // Restricted to one explicit range (currently just the "search" glyph,
   // U+E151) rather than Lucide's full 1000+ icon set - the atlas only
   // pays texture memory for glyphs actually in use.
   if (uiFont != nullptr)
   {
      const std::string bundledLucide = BundledResourcePath("icons/lucide.ttf");
      if (!bundledLucide.empty())
      {
         static const ImWchar iconRanges[] = { 0xE151, 0xE151, 0 };
         ImFontConfig iconCfg;
         iconCfg.MergeMode = true;
         iconCfg.PixelSnapH = true;
         iconCfg.GlyphMinAdvanceX = bakedPx;
         io.Fonts->AddFontFromFileTTF(bundledLucide.c_str(), bakedPx, &iconCfg, iconRanges);
      }
   }
   if (rendererReady)
      ImGui_ImplOpenGL3_CreateFontsTexture();
}

int main(int argc, char** argv)
{
   // Where the Drum Sequencer finds its bundled kit (Resources/drumkits/
   // infinite-basic); empty when the folder is absent, which the node treats as
   // "no kit". Set first so headless jobs that load patches see it too.
   DrumSequencerNode::SetKitDir(BundledResourcePath("drumkits/infinite-basic"));
   const double sMainStartMs = Bench::ScopedStageTimer::NowMs();
   const double sMainRssStartMb = Bench::ProcessRssMb();
   const double sMainFootStartMb = Bench::ProcessFootprintMb();
   static int sBenchB2Render3DIdx = -1;
   static int sBenchB2OutputIdx = -1;
   static std::string sBenchB2Variant;
   static int sBenchB4CamIdx = -1;
   static int sBenchB4LfoIdx = -1;
   static std::string sBenchB9Scene;
   static std::string sBenchB9Variant;
   static double sBenchB9RssBuiltMb = -1.0;
   static double sBenchB9RssStartMb = -1.0;
   static double sBenchB9RssF32Mb = -1.0;
   static double sBenchB9RssF152Mb = -1.0;
   static double sBenchB9RssPeakMb = -1.0;
   static double sBenchB9FootBuiltMb = -1.0;
   static double sBenchB9FootF32Mb = -1.0;
   static double sBenchB9FootF152Mb = -1.0;
   static double sBenchB9FootPeakMb = -1.0;
   static Bench::PercentileRing sBenchB9FrameMs;
   static std::vector<std::pair<int, double>> sBenchB9RssSamples;
   static std::vector<std::pair<int, double>> sBenchB9FootSamples;
   static int sBenchB3OutputIdx = -1;
   static int sBenchB3Render3DIdx = -1;
   static int sBenchB3TwistIdx = -1;
   static int sBenchB3MatIdx = -1;
   static int sBenchB3CamIdx = -1;
   static std::string sBenchB3Variant;
   static int sBenchB3MonitorRefreshHz = 60;
   static bool sBenchB3Unfocused = false;
   static int sBenchB3TargetRateHz = 60;
   static Bench::PercentileRing sBenchB3FrameMs;
   static Bench::PercentileRing sBenchB3ProjIntervalRing;
   static Bench::PercentileRing sBenchB3InputToPhotonFrames;
   static double sBenchB3LastProjSwapMs = -1.0;
   static int sBenchB3MissedVsyncCount = 0;
   static int sBenchB3TotalVsyncCount = 0;
   static AudioEngine::XrunCounts sBenchB3XrunBaseline;
   static int sBenchB3PendingInputInjectFrame = -1;
   static unsigned long long sBenchB3RevBeforeInject = 0;
   static bool sBenchB3ProbePaused = false;
   static bool sBenchB3I2PDryRun = false;
   static double sBenchB3RssStartMb = -1.0;
   static double sBenchB3RssPeakMb = -1.0;
   static double sBenchB3FootStartMb = -1.0;
   static double sBenchB3FootPeakMb = -1.0;
   // B7 soak (the B3 fixture, run on wall-clock time): one sample per 10 s
   // window, verdicts at the end (benchmark-suite.md §6).
   static double sBenchB7StartS = -1.0;
   static double sBenchB7LastSampleS = -1.0;
   static uint64_t sBenchB7LoadMark = 0;
   static Bench::PercentileRing sBenchB7WinFrameMs;
   static nlohmann::json sBenchB7Samples = nlohmann::json::array();

   // B8 Media I/O fixture state (docs/plans/perf/benchmark-suite.md §4)
   static std::string sBenchB8Variant;
   static std::string sBenchB8SetupError;
   static int sBenchB8TotalFrames = 600;
   static int sBenchB8Clips = 2;
   static int sBenchB8Res = 1080;
   static int sBenchB8Windows = 0;
   static bool sBenchB8WantCamera = false;
   static bool sBenchB8WantSyphon = false;
   static std::string sBenchB8CameraSkipReason; // non-empty = "camera":"skipped"
   static std::vector<int> sBenchB8ClipIdx;
   static std::vector<int> sBenchB8OutputIdx;
   static int sBenchB8SyphonIdx = -1;
   static int sBenchB8CameraIdx = -1;
   static bool sBenchB8Unfocused = false;
   static bool sBenchB8Overlap = false;
   static double sBenchB8RefreshMs = 1000.0 / 60.0;
   static int sBenchB8OnVsyncFrames = 0;
   static int sBenchB8IntervalFrames = 0;
   static double sBenchB8FootStartMb = -1.0;
   static double sBenchB8FootPeakMb = -1.0;
   static double sBenchB8RssStartMb = -1.0;
   static Bench::PercentileRing sBenchB8FrameMs;
   // Per projector window, indexed like gProjectorWindows (none close mid-run).
   struct BenchB8Window
   {
      Bench::PercentileRing presentMs;
      Bench::PercentileRing intervalMs;
      double lastSwapMs = -1.0;
      int refreshHz = 0;
      int monitorIndex = -1;
      int onVsync = 0; // intervals within 1.5 ms of a whole number of this display's refreshes
   };
   static std::vector<BenchB8Window> sBenchB8Win;
   static bool sBenchB8ForceOverlap = false; // INFINITE_BENCH_B8OVERLAP=1: leave projectors on top of the canvas
   static bool sBenchB8Sampling = false;

   // B6 Canvas navigation fixture state (docs/plans/perf/benchmark-suite.md §4)
   static std::string sBenchB6Variant;
   static std::string sBenchB6Mode = "all";
   static int sBenchB6NodeCount = 300;
   static int sBenchB6TotalFrames = 600;
   static bool sBenchB6Collapsed = false;
   static bool sBenchB6Vsync = true;
   static bool sBenchB6Unfocused = false;
   static float sBenchB6GridMaxX = 0.0f;
   static float sBenchB6GridMaxY = 0.0f;
   static int sBenchB6DragNodeIndex = -1;
   static double sBenchB6RssStartMb = -1.0;
   static double sBenchB6FootStartMb = -1.0;
   static double sBenchB6FootPeakMb = -1.0;
   static Bench::PercentileRing sBenchB6FrameMs;
   static Bench::PercentileRing sBenchB6PanFrameMs;
   static Bench::PercentileRing sBenchB6ZoomFrameMs;
   static Bench::PercentileRing sBenchB6DragFrameMs;
   static Bench::PercentileRing sBenchB6DropdownFrameMs;
   static double sBenchB6VisibleNodesSum = 0.0;
   static double sBenchB6BodiesDrawnSum = 0.0;
   static double sBenchB6OffscreenBodyMsSum = 0.0;
   static int sBenchB6SampledFrames = 0;
   static int sBenchB6DropdownOpenFrames = 0;
   static double sBenchB6RefreshMs = 1000.0 / 60.0;
   static int sBenchB6OnVsyncFrames = 0;
   static int sBenchB6IntervalFrames = 0;
   static ImVec2 sBenchB6DragStartPos(0.0f, 0.0f);
   static float sBenchB6DragMovedPx = 0.0f;

   // No-op on macOS (which gets a `.ips` report for free); on Windows this is
   // the only thing standing between a crash and a completely silent exit,
   // since main.cpp links WIN32_EXECUTABLE (no console, stderr goes nowhere).
   // Installed before any of the INFINITE_*TEST branches below too, so a
   // crash in a headless CI fixture leaves a dump/log the same as a real run.
   Platform::InstallCrashHandler();

   if (getenv("INFINITE_CRASHTEST") != nullptr)
   {
      SysInfo::CrashTest();
      return 0;
   }

   // Dynamic pins, Phase 2b (build step 13, §5.1): install the live-cable
   // checker bridge (see CheckFieldLiveCableBridge's comment above) before
   // anything can call a Field*Node::Apply() - including the FIELDPINSTEST/
   // FIELDPINNODETEST fixtures below, which construct FieldElementNode/
   // FieldSampleNode/FieldPixelNode via SpawnNode and rely on this being
   // live for their refusal assertions.
   Field::gLiveCableChecker = &CheckFieldLiveCableBridge;
   Field::gLiveCableDisconnector = &DisconnectFieldPinBridge;

   // Old spelling kept as an alias: the panel was renamed to the performance
   // matrix, but a shell history full of INFINITE_PERFPANELTEST is not worth
   // breaking over it.
   if (getenv("INFINITE_PERFMATRIXTEST") != nullptr || getenv("INFINITE_PERFPANELTEST") != nullptr)
      return RunPerfPanelSelfTest() ? 0 : 1;
   if (getenv("INFINITE_AUDIOPARAMSWEEPTEST") != nullptr)
   {
      // Needs the registry populated (DiscoverAudioSweepCandidates walks
      // NodeFactory), unlike DSPTEST above which constructs its DspTest::*
      // fixture classes directly - RegisterNodes() itself touches no GL, so
      // it is safe to call this early, before glfwInit().
      RegisterNodes();
      RunAudioParamSweepTest();
      RunFmModeDebugCheck();
      RunFmRenderCheck();
      // Always 0, never the sweep's own pass/fail - the printf verdict line
      // is the only signal every other INFINITE_* fixture uses (see
      // run-infinite-hygiene/SKILL.md's "verdict lines aren't exit codes"
      // Gotcha); driver.sh greps for "AUDIO PARAM SWEEP FAIL", it doesn't
      // read $?. A real nonzero exit here would misreport a normal [FAIL] as
      // a [CRASH] in that harness's generic per-check loop.
      return 0;
   }

   if (getenv("INFINITE_BROWSERSORTTEST") != nullptr)
      return RunBrowserSortTest() ? 0 : 1;

   if (getenv("INFINITE_APPEARANCETEST") != nullptr)
      return RunAppearanceSelfTest() ? 0 : 1;

   if (getenv("INFINITE_PLUGINSCANTEST") != nullptr)
      return RunPluginScanTest();
   if (getenv("INFINITE_MIDIBENDTEST") != nullptr)
      return RunMidiBendTest();
   if (getenv("INFINITE_PLUGINNODETEST") != nullptr)
      return RunPluginNodeHandleTest();

#if INFINITE_ENABLE_VST3
   if (getenv("INFINITE_VST3SCANTEST") != nullptr)
      return RunVST3ScanTest();
#if defined(__linux__)
   if (getenv("INFINITE_VST3EDITORSHOTTEST") != nullptr)
      return RunVST3EditorShotTest();
   if (getenv("INFINITE_VST3BLOCKLISTTEST") != nullptr)
      return RunVST3BlocklistTest();
#endif
#endif

   if (getenv("INFINITE_AUTOSAVEMARKERTEST") != nullptr)
      return RunAutosaveMarkerTest();

   if (getenv("INFINITE_REMOVEBGTEST") != nullptr)
      return RunRemoveBgTest();

   if (getenv("INFINITE_NETWORKTEST") != nullptr)
      return RunNetworkTest();

   if (getenv("INFINITE_RESONATORTEST") != nullptr)
      return RunResonatorFixture() ? 0 : 1;

   if (getenv("INFINITE_METALLICDECAYTEST") != nullptr)
      return RunMetallicDecayFixture() ? 0 : 1;

   if (getenv("INFINITE_CYCLESHAPERTEST") != nullptr)
      return RunCycleShaperFixture() ? 0 : 1;

   if (getenv("INFINITE_SPECBLURTEST") != nullptr)
      return RunSpecBlurFixture() ? 0 : 1;

   if (getenv("INFINITE_KEYSNAPTEST") != nullptr)
      return RunKeySnapFixture() ? 0 : 1;

   if (getenv("INFINITE_SPECTRUMSLIDETEST") != nullptr)
      return RunSpectrumSlideFixture() ? 0 : 1;

   if (getenv("INFINITE_MIDIFILETEST") != nullptr)
      return RunMidiFileFixture() ? 0 : 1;

   if (getenv("INFINITE_MPETEST") != nullptr)
      return RunMpeFixture() ? 0 : 1;

   if (getenv("INFINITE_SHAPERESONATORTEST") != nullptr)
      return RunShapeResonatorFixture() ? 0 : 1;

   if (getenv("INFINITE_DSPTEST") != nullptr)
      return RunDspTest();

   // The --audio-summary maths on synthesized signals with known answers
   // (docs/fix-briefs/headless-engine.md 2.1).
   if (getenv("INFINITE_AUDIOSUMMARYTEST") != nullptr)
   {
      std::string report;
      const bool ok = AudioSummary::SelfTest(report);
      std::fputs(report.c_str(), stdout);
      std::printf("AUDIOSUMMARYTEST: %s\n", ok ? "PASS" : "FAIL");
      return ok ? 0 : 1;
   }

   if (getenv("INFINITE_FIELDTEST") != nullptr)
      return RunFieldTest();

   if (getenv("INFINITE_FIELDELEMENTTEST") != nullptr)
      return RunFieldElementTest();

   if (getenv("INFINITE_FIELDPARAMTEST") != nullptr)
      return RunFieldParamTest();

   if (getenv("INFINITE_FIELDSTATETEST") != nullptr)
      return RunFieldStateTest();

   if (getenv("INFINITE_FIELDTRANSFERTEST") != nullptr)
      return RunFieldTransferTest();

   if (getenv("INFINITE_FIELDSAMPLETEST") != nullptr)
      return RunFieldSampleTest();

   if (getenv("INFINITE_FIELDPINDECLTEST") != nullptr)
      return RunFieldPinDeclTest();

   if (getenv("INFINITE_MOLDERTEST") != nullptr)
      return RunMolderFixture() ? 0 : 1;

   if (getenv("INFINITE_GRAINMOLDERTEST") != nullptr)
      return RunGrainMolderFixture() ? 0 : 1;

   if (const char* recExportVariant = getenv("INFINITE_RECEXPORTTEST"))
   {
      // default/320x240 can never overrun the queue even at its old 4-frame
      // depth, so it can't exercise the drop path on its own - and at this
      // whole take's lifetime byte total (180 frames * ~300KB =~ 54MB) it is
      // structurally incapable of crossing the 256MB budget even with zero
      // draining, so "starved" runs at 720p instead (~648MB lifetime, well
      // over budget) to force real drops when combined with the removed
      // backpressure spin below. "720p" alone (spin still on) checks that
      // resolution still syncs cleanly under normal backpressure.
      if (std::strcmp(recExportVariant, "starved") == 0)
         RunRecExportTest(1280, 720, true, "starved");
      else if (std::strcmp(recExportVariant, "720p") == 0)
         RunRecExportTest(1280, 720, false, "720p");
      else
         RunRecExportTest(320, 240, false, "default");
      return 0; // verdict is the printf line, not $?
   }

   if (getenv("INFINITE_VIDEOEXACTTEST") != nullptr)
   {
      RunVideoExactTest();
      return 0; // verdict is the printf line, not $?
   }

   if (getenv("INFINITE_RECSYNCTEST") != nullptr)
   {
      RunRecSyncTest();
      return 0; // verdict is the printf line, not $? - see AUDIOPDCTEST below
   }

   if (getenv("INFINITE_AUDIORINGTEST") != nullptr)
   {
      RunAudioRingTest();
      return 0; // verdict is the printf line, not $?
   }

   if (getenv("INFINITE_AUDIOPDCTEST") != nullptr)
   {
      RunAudioPdcTest();
      // Always 0, same reasoning as AUDIOPARAMSWEEPTEST above: the printf
      // verdict line is driver.sh's only signal, not $? - a nonzero exit
      // here would misreport a normal [FAIL] as [CRASH].
      return 0;
   }

   if (getenv("INFINITE_AUDIOPCMTEST") != nullptr)
      return Platform::AudioPcmConversionSelfTest() ? 0 : 1;

   if (getenv("INFINITE_MIDICC14TEST") != nullptr)
      return RunMidiCC14Test();
   if (getenv("INFINITE_CVRECTEST") != nullptr)
      return RunCVRecorderTest();

#if defined(__linux__)
   if (getenv("INFINITE_MIDIPARSETEST") != nullptr)
      return RunMidiParseTest();
#endif

   if (getenv("INFINITE_SYPHONPATCHTEST") != nullptr)
      return RunSyphonPatchTest();
   if (getenv("INFINITE_PATCHLAYOUTTEST") != nullptr)
      return RunPatchLayoutTest();

#if defined(__linux__)
   if (getenv("INFINITE_CAMERACONVTEST") != nullptr)
      return RunCameraConvTest();
   if (getenv("INFINITE_HOSTENVTEST") != nullptr)
      return RunHostEnvTest();
#endif

   if (getenv("INFINITE_MOVELOGTEST") != nullptr || getenv("INFINITE_MOVEMENTLOGTEST") != nullptr)
      return MovementLog::RunMovementLogTest() ? 0 : 1;

   if (getenv("INFINITE_MOVESTATSTEST") != nullptr)
      return MovementStats::RunMovementStatsTest() ? 0 : 1;

   if (getenv("INFINITE_PREDMIDITEST") != nullptr)
      return PredictiveNotes::RunPredMidiTest() ? 0 : 1;
   if (getenv("INFINITE_PREDCOLORTEST") != nullptr)
      return PredictiveColoring::RunPredColorTest() ? 0 : 1;
   if (getenv("INFINITE_PREDQUANTIZETEST") != nullptr)
      return PredictiveQuantize::RunPredQuantizeTest() ? 0 : 1;
   if (getenv("INFINITE_PREDVELOCITYTEST") != nullptr)
      return PredictiveVelocity::RunPredVelocityTest() ? 0 : 1;
   if (getenv("INFINITE_PREDRHYTHMTEST") != nullptr)
      return PredictiveRhythm::RunPredRhythmTest() ? 0 : 1;
   if (getenv("INFINITE_DRIFTTEST") != nullptr)
      return PredictionNodes::RunDriftTest() ? 0 : 1;
   if (getenv("INFINITE_PREDFEEDBACKTEST") != nullptr)
      return PredictionNodes::RunPredFeedbackTest() ? 0 : 1;
   if (getenv("INFINITE_PREDV2TEST") != nullptr)
      return PredictionNodes::RunPredV2Test() ? 0 : 1;

   {
      std::string usageError;
      if (Headless::ParseArgs(argc, argv, gHeadlessJob, usageError))
      {
         if (gHeadlessJob.mode == Headless::Mode::Version)
         {
            std::printf(gHeadlessJob.json ? "{\"ok\":true,\"version\":\"%s\",\"headless\":1}\n" : "%s\n",
                        INFINITE_VERSION_STRING);
            return 0;
         }
         if (!usageError.empty())
         {
            Headless::Status st;
            st.mode = "usage";
            st.errors.push_back({ "E_USAGE", usageError, 0, -1 });
            return Headless::Emit(gHeadlessJob, st);
         }
         // Cocoa chdir's a bundled app into Contents/Resources at glfwInit, so
         // every path the caller typed has to be pinned to their cwd first.
         for (std::string* path : { &gHeadlessJob.patch, &gHeadlessJob.out, &gHeadlessJob.jsonPath,
                                    &gHeadlessJob.wavPath, &gHeadlessJob.contactSheet })
         {
            if (path->empty())
               continue;
            const bool keepSlash = path->back() == '/';
            std::error_code ec;
            *path = std::filesystem::absolute(*path, ec).lexically_normal().string();
            if (keepSlash && path->back() != '/')
               *path += '/';
         }
         gHeadlessAudioRate = gHeadlessJob.sampleRate;
         Platform::AttachConsoleForHeadless();
#if !defined(__APPLE__) && !defined(_WIN32)
         // GLFW needs a display server. Say so with the fix, instead of the
         // fatal-error path below (a dialog nobody can see on a headless box).
         if (getenv("DISPLAY") == nullptr && getenv("WAYLAND_DISPLAY") == nullptr)
         {
            Headless::Status st;
            st.mode = "no-display";
            st.errors.push_back({ "E_NO_DISPLAY",
                                  "no DISPLAY or WAYLAND_DISPLAY: Infinite needs a display server to render. "
                                  "Run it under a virtual one, e.g. xvfb-run -a Infinite --render ...",
                                  0, -1 });
            return Headless::Emit(gHeadlessJob, st);
         }
#endif
#if defined(_WIN32)
         _putenv_s("INFINITE_NO_UPDATE_CHECK", "1");
#else
         setenv("INFINITE_NO_UPDATE_CHECK", "1", 1);
#endif
      }
   }

   if (argc >= 3 && std::strcmp(argv[1], "--dump-movement-log") == 0)
   {
      MovementLog::DumpLog(argv[2], std::cout);
      return 0;
   }

   // Out-of-process half of the plugin scan: describe ONE bundle and exit. The
   // parent (Platform::EnumerateVST3Plugins) re-execs us once per bundle so
   // that a plugin which cannot be loaded - damaged code pages earn an
   // uncatchable SIGKILL from the kernel, and plenty of plugins simply crash
   // in their own static initialisers - costs us a dead child process instead
   // of the whole app. Output is one tab-separated record per class on
   // stdout; anything else the plugin decides to print goes to stderr, which
   // the parent redirects to /dev/null. No window, audio device, or node
   // registry needed for this, so it returns well before glfwInit().
   if (argc >= 3 && std::strcmp(argv[1], "--vst3-scan-bundle") == 0)
   {
#if INFINITE_ENABLE_VST3
      // Must happen before DescribeVST3Bundle touches any plugin code - see
      // the function's doc comment for why a scanned plugin can otherwise
      // make this child process pop up as a spurious extra Infinite window.
      Platform::SuppressAppUIForHeadlessProcess();

#if defined(_WIN32)
      // stdout defaults to text mode, which translates '\n' to '\r\n' and
      // corrupts the tab-separated wire format below (the parent's
      // ParseProbeOutput strips a trailing '\r' defensively as the other
      // half of this fix).
      _setmode(_fileno(stdout), _O_BINARY);
#endif

      // Guards the tab-separated wire format below against a plugin whose
      // self-reported name/vendor string happens to contain a tab or newline.
      auto sanitize = [](std::string s)
      {
         for (char& c : s)
            if (c == '\t' || c == '\n' || c == '\r')
               c = ' ';
         return s;
      };
      std::vector<Platform::PluginDesc> descs;
      Platform::DescribeVST3Bundle(argv[2], descs);
      for (const Platform::PluginDesc& d : descs)
      {
         std::printf("%s\t%s\t%s\t%s\t%s\t%d\n", sanitize(d.format).c_str(), sanitize(d.name).c_str(),
                     sanitize(d.manufacturer).c_str(), sanitize(d.identifier).c_str(), sanitize(d.path).c_str(),
                     d.acceptsNotes ? 1 : 0);
      }
      std::fflush(stdout);
#endif
      // Always 0: exit status communicates process health (did this bundle's
      // code crash us?) to the parent's waitpid, not whether any class was
      // found - a bundle with zero usable classes is a normal, clean miss.
      return 0;
   }

   // Dev/test harness runs (INFINITE_EXITAFTER) and screenshot mode
   // (IMAGERESYNTH_SCREENSHOT) create the window off-screen: a hidden window
   // still has a real GL context and still produces real ImGui frames, real GL
   // draws, and a real backbuffer glReadPixels can read from - verified
   // directly, a hidden-window screenshot is pixel-identical to a visible-
   // window one. Hiding it means the hygiene suite's visual smoke test no
   // longer flashes an app window on screen, never steals focus from whatever
   // the person running the suite is actually doing, and - because
   // WindowServer isn't waiting on it - a slow frame no longer trips the
   // 10-second unresponsive watchdog that writes a "hang" spindump to the
   // Desktop.
   // INFINITE_BENCH_VISIBLE opts a harness run back into a real window: the
   // B3/B6/B8 fixtures measure display pacing and focus, which a hidden
   // window can never have (they would always report unfocused/unpaced).
   const bool gHeadlessTestWindow = IsHeadlessProcess() && getenv("INFINITE_BENCH_VISIBLE") == nullptr;
   // The Dock icon comes from NSApplication's activation policy, not window
   // visibility, so GLFW_VISIBLE=false alone still leaves a headless test run
   // bouncing in the Dock. Must claim the shared-application singleton before
   // glfwInit() does (see the function's doc comment) - and glfwInit() itself
   // unconditionally resets the policy back to Regular unless the
   // GLFW_COCOA_MENUBAR init hint is off first (cocoa_init.m: "in case we are
   // unbundled, make us a proper UI application"), so both are needed or the
   // Prohibited policy above gets clobbered a few lines later.
   if (gHeadlessTestWindow)
   {
      Platform::SuppressAppUIForHeadlessProcess();
      glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
   }

#if !defined(__APPLE__) && !defined(_WIN32)
   if (getenv("INFINITE_WAYLAND") == nullptr && getenv("DISPLAY") != nullptr)
   {
      glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
   }
#endif

   // One Infinite per user on Windows/Linux: `Infinite song.inf` while the app is
   // already open hands the file to it and exits (macOS does this through Launch
   // Services). Dev harnesses that set INFINITE_EXITAFTER, and headless jobs, are
   // exempt so a test run never talks to the app someone has open.
   if (!HeadlessJobActive() && getenv("INFINITE_EXITAFTER") == nullptr && getenv("INFINITE_NO_SINGLE_INSTANCE") == nullptr)
   {
      std::string openArg;
      if (argc > 1 && argv[1] != nullptr && argv[1][0] != '-' &&
          HasExtension(argv[1], std::vector<std::string> { "inf", "infinite" }))
         openArg = argv[1];
      if (!Platform::ForwardOpenToRunningInstance(openArg))
         return 0;
   }

   const double tPreWindow = Bench::ScopedStageTimer::NowMs();
   Platform::InitDocumentHandlingPreGlfw();
   if (!glfwInit())
   {
      if (HeadlessJobActive())
      {
         Headless::Status st;
         st.mode = "startup";
         st.errors.push_back({ "E_RENDER", "glfwInit failed (no usable display or graphics driver)", 0, -1 });
         return Headless::Emit(gHeadlessJob, st);
      }
      Platform::ShowFatalError("Infinite failed to start", "glfwInit failed.");
      return 1;
   }
   Platform::InitDocumentHandlingPostGlfw();

   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
   // 3.3, not 3.2 - see the matching hint block in OpenProjectorWindow() for
   // why: glad only loads glVertexAttribDivisor (used by every instanced
   // draw) when the context actually reports 3.3, and some drivers honour a
   // literal 3.2 request instead of over-granting like NVIDIA/AMD desktop
   // drivers do.
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
   glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
   glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

   if (gHeadlessTestWindow)
   {
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
      glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
      glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
   }

   // Windows and X11 size windows in physical pixels: without this a 1600x1000 window is
   // half its intended size on a 200% monitor (no effect on macOS/Wayland, which are
   // already point-based). Headless fixtures keep exact pixel sizes.
   if (!gHeadlessTestWindow)
      glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
   GLFWwindow* window = glfwCreateWindow(1600, 1000, "Infinite", nullptr, nullptr);
   if (!window)
   {
      Platform::ShowFatalError("Infinite failed to start",
                               "glfwCreateWindow failed. This usually means the graphics "
                               "driver doesn't support the OpenGL version Infinite needs.");
      glfwTerminate();
      return 1;
   }
   SetWindowIcon(window);
   // SCALE_TO_MONITOR can make the window larger than the screen (1600x1000 at 150% on a
   // 1080p panel is 2400x1500 pixels). Fit it to the primary monitor's work area.
   if (!gHeadlessTestWindow)
   {
      if (GLFWmonitor* primary = glfwGetPrimaryMonitor())
      {
         int mx = 0, my = 0, mw = 0, mh = 0, ww = 0, wh = 0;
         glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
         glfwGetWindowSize(window, &ww, &wh);
         // Only when SCALE_TO_MONITOR actually enlarged it: a 1x window keeps today's size.
         const bool enlarged = ww > 1600 || wh > 1000;
         if (enlarged && mw > 0 && mh > 0 && (ww > mw || wh > mh))
         {
            int l = 0, t = 0, r = 0, b = 0;
            glfwGetWindowFrameSize(window, &l, &t, &r, &b);
            const int fitW = std::min(ww, mw - l - r);
            const int fitH = std::min(wh, mh - t - b);
            glfwSetWindowSize(window, fitW, fitH);
            glfwSetWindowPos(window, mx + l + (mw - l - r - fitW) / 2, my + t + (mh - t - b - fitH) / 2);
         }
      }
   }

   glfwMakeContextCurrent(window);
#if !defined(__APPLE__)
   // On Windows core/gl3.h resolves to the glad2 loader; every GL entry point
   // is a function pointer until it's populated. All other windows share this
   // context, so one load here covers the app.
   //
   // gladLoadGL's return value is the version it *found*, not a bool against
   // what was requested - a driver that honours a literal 3.2 hint above
   // returns a non-zero "3.2" here, which is a success as far as this call is
   // concerned even though glVertexAttribDivisor and the rest of GL 3.3
   // still come back null. GLAD_GL_VERSION_3_3 is the actual gate.
   const int gladVersion = gladLoadGL((GLADloadfunc)glfwGetProcAddress);
   if (!gladVersion || !GLAD_GL_VERSION_3_3)
   {
      Platform::ShowFatalError(
         "Infinite failed to start",
         "This graphics driver does not support OpenGL 3.3, which Infinite requires. "
         "Please update your graphics driver and try again.");
      glfwTerminate();
      return 1;
   }
#endif

   if (getenv("INFINITE_SYSINFO") != nullptr)
   {
      SysInfo::PrintAndExit(window);
   }

   if (getenv("INFINITE_SPOUTLOOPTEST") != nullptr)
   {
      // Needs a live GL context (textures, FBOs), unlike the pre-GLFW
      // fixtures dispatched above main()'s window setup - hence the check
      // lives here instead of alongside MOLDERTEST/DSPTEST/etc.
      bool ok = RunSpoutLoopTest();
      glfwDestroyWindow(window);
      glfwTerminate();
      return ok ? 0 : 1;
   }

   if (gHeadlessTestWindow)
      SetCanvasSwapInterval(0);
   else
      SetCanvasSwapInterval(1);
   Platform::PreventAppNap();

   // Covers both the red close button and Cmd+Q: GLFW's Cocoa backend routes
   // applicationShouldTerminate: through the same _glfwInputWindowCloseRequest
   // as the window's own close button, so one callback gates both.
   glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { RequestClose(w); });

   const double tWindowGl = Bench::ScopedStageTimer::NowMs();
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::StyleColorsDark();

   // Loaded here (rather than down with the other Load*Settings() calls)
   // because the font/DPI block right below needs gUiScale before it bakes
   // the font atlas - loading it after the atlas already exists is too late.
   CategoryColors::LoadPreference();

   // A proper UI typeface instead of ImGui's bitmap default, baked sharp for the display.
   //
   // The bundled Inter Regular (external/fonts/Inter, SIL OFL - see that
   // directory's LICENSE.txt) is tried first on every platform, resolved
   // relative to the executable's own location (BundledResourcePath) rather
   // than any particular working directory; the macOS system fonts after it
   // are a fallback chain for a dev build missing the bundled asset.
   //
   // DPI: the whole UI is laid out in points on every platform (core/UiScale.h).
   // On Retina/Wayland the OS already sizes the window in points; on Windows and
   // X11 the GLFW backend divides the pixel window by the monitor's scale. The
   // user's "UI Scale" slider multiplies on top. Issue #21 (Windows text stuck at
   // 15 px) and its Linux X11 twin were both the font half of this; node bodies
   // staying 440 px wide at 200% was the layout half.
   ApplyUiScale(window, false);
   glfwSetWindowContentScaleCallback(window, [](GLFWwindow*, float, float) { UiScale::RequestRescale(); });

   ImGuiStyle& style = ImGui::GetStyle();
   style.FrameRounding = 3.0f;
   style.GrabRounding = 3.0f;
   style.WindowRounding = 4.0f;
   style.ItemSpacing = ImVec2(6, 5);
   // Popups/context menus and scrollbars are chrome, not node bodies - they
   // don't share WindowRounding/FrameRounding's blast radius (node-ui-pillars
   // P1-P9 grid math), so they can read softer without touching a single
   // knob row. Left unset before this, which meant they inherited 0.
   style.PopupRounding = 12.0f;
   style.ScrollbarRounding = 10.0f;
   // No ScaleAllSizes: style metrics are in points, and the point scale (ApplyUiScale above)
   // grows them together with every other size on screen.

   ImGui_ImplGlfw_InitForOpenGL(window, true);
   // Installed after the backend so it chains rather than replacing ImGui's.
   glfwSetDropCallback(window, OnFilesDropped);
   ImGui_ImplOpenGL3_Init("#version 150");
   const double tImGuiFonts = Bench::ScopedStageTimer::NowMs();

   // Keep all mutable state (settings, indexes, imgui.ini) in the per-user
   // application-data directory rather than next to the executable.
   std::string settingsDir = AppPaths::AppSupportDir();
   // Restores whatever was indexed last run with no rescan - scanning only
   // ever happens from an explicit Refresh click in the Samples/Media panel.
   gSampleScanner.LoadFromDisk();
   gMediaScanner.LoadFromDisk();
   // Never a scan at launch, for the same reason the two above aren't:
   // the cached index is shown instantly and rebuilding it is the user's
   // explicit Rescan.
   gPluginScanner.LoadFromDisk();
   const double tScanners = Bench::ScopedStageTimer::NowMs();

   // One GitHub Releases request, once per launch - see src/core/UpdateCheck.h.
   // No-ops under the self-test env vars, so headless/CI runs never touch
   // the network.
   UpdateCheck::Start();

   static std::string iniPath = settingsDir.empty() ? std::string("imgui.ini")
                                                    : settingsDir + "/imgui.ini";
   static std::string graphPath = settingsDir.empty() ? std::string("Infinite.json")
                                                      : settingsDir + "/Infinite.json";
   if (getenv("INFINITE_DRAGTEST") != nullptr)
   {
      // DRAGTEST pans the view; SettingsFile persists that pan to disk, so
      // sharing the real settings file means every run starts from wherever
      // the last one left the camera, slowly walking the fixture node off
      // the visible window over repeated runs (e.g. one hygiene-check run
      // per commit). Use a throwaway path instead, reset before use, so the
      // test always starts from a known view.
      graphPath = settingsDir.empty() ? std::string("InfiniteDragTest.json")
                                       : settingsDir + "/InfiniteDragTest.json";
      remove(graphPath.c_str());
   }
   else if (getenv("INFINITE_WTDRAGTEST") != nullptr)
   {
      // Same reasoning as INFINITE_DRAGTEST above: whatever pan/zoom an
      // earlier self-test left in the shared SettingsFile gets restored
      // here, and phase 3 of the drag below moves the mouse by a fixed
      // *screen*-pixel delta that assumes the default 1.0 zoom. A leftover
      // zoom from a prior run (e.g. when this runs after other UI-group
      // tests in the same hygiene pass) shrinks that delta in canvas space
      // enough that the filter envelope's attack handle sees no change at
      // all - the test then fails only when run after other tests, never
      // standalone. A throwaway path keeps this test's view state isolated.
      graphPath = settingsDir.empty() ? std::string("InfiniteWtDragTest.json")
                                       : settingsDir + "/InfiniteWtDragTest.json";
      remove(graphPath.c_str());
   }
   else if (getenv("INFINITE_EQDRAGTEST") != nullptr)
   {
      graphPath = settingsDir.empty() ? std::string("InfiniteEqDragTest.json")
                                       : settingsDir + "/InfiniteEqDragTest.json";
      remove(graphPath.c_str());
   }
   else if (!graphPath.empty())
   {
      if (FILE* f = fopen(graphPath.c_str(), "rb"))
      {
         fseek(f, 0, SEEK_END);
         long sz = ftell(f);
         fseek(f, 0, SEEK_SET);
         if (sz > 0 && sz < 1024 * 1024)
         {
            std::string content(sz, '\0');
            if (fread(&content[0], 1, sz, f) == (size_t)sz)
            {
               if (content.find("-2147483648") != std::string::npos ||
                   content.find("2147483647") != std::string::npos ||
                   content.find("-152280448") != std::string::npos)
               {
                  fclose(f);
                  f = nullptr;
                  remove(graphPath.c_str());
               }
            }
         }
         if (f) fclose(f);
      }
   }
   ImGui::GetIO().IniFilename = iniPath.c_str();

   ed::Config config;
   config.SettingsFile = graphPath.c_str();
   config.EnableSmoothZoom = true; // trackpad momentum made stepped zoom feel jumpy
   Patch::LoadRecents();
   // CategoryColors::LoadPreference() already ran earlier, before the font/DPI
   // block, so gUiScale is available in time for font baking.
   LoadGeneralSettings();
   LoadWorkspaceSettings();
   LoadAudioSettings();
   // Startup never routes through NewPatch()'s fresh-document branch (the
   // graph just starts empty), so seed the default expression globals here
   // too - overwritten a moment later by CheckAutosaveRecovery() if there's
   // a patch to recover, same as NewPatch()'s own seeding is overwritten by
   // a genuine File > Open.
   LoadDefaultExprGlobals();
   SeedDefaultArrangeStreams();
   // Feed the loaded device/format prefs into the engine now, before the
   // user ever presses "Start Audio", so a persisted non-default choice
   // actually takes effect on the first Start rather than only after the
   // Settings dialog's "Apply audio settings" button is clicked again.
   AudioEngine::Instance().SetRequestedDevice(gAudioOutputDeviceId);
   AudioEngine::Instance().SetRequestedSampleRate(gAudioSampleRate);
   AudioEngine::Instance().SetRequestedBufferFrames(gAudioBufferFrames);
   Platform::AudioSetOutputMode(gAudioOutputMode);
   // The window was just created with vsync hardcoded on (above); apply the
   // persisted preference now that it's loaded. Headless test windows stay
   // uncapped regardless - they don't want to be paced by the display.
   if (!gHeadlessTestWindow)
      SetCanvasSwapInterval(gVsync ? 1 : 0);
   LoadBrowserFilterPrefs();
   gBrowserFavorites.Load();

   gEditor = ed::CreateEditor(&config);
   ed::SetCurrentEditor(gEditor); // ed::GetStyle() below needs a current editor

   RegisterNodes();
   MovementLog::SetNodeUidLookup([](int idx) -> uint64_t {
      if (GraphNode* gn = FindNodeByIndex(idx))
         return gn->uid;
      return 0;
   });
   MovementLog::SetNodeTypeLookup([](int idx) -> std::string {
      if (GraphNode* gn = FindNodeByIndex(idx))
         return gn->typeName;
      return std::string();
   });
   MovementLog::Start();
   ColorStats::Engine::Instance().Load(AppPaths::AppSupportDir() + "/prediction");
   PredictiveQuantizeProfile::Load(AppPaths::AppSupportDir() + "/prediction");
   PredictiveVelocityProfile::Load(AppPaths::AppSupportDir() + "/prediction");
   PredictiveNotesStyle::Load(AppPaths::AppSupportDir() + "/prediction");
   PredictiveRhythmStyle::Load(AppPaths::AppSupportDir() + "/prediction");
   ApplyTheme();

   // Skipped under the dev-test harness (INFINITE_EXITAFTER) so that running
   // the self-test suite never reads, consumes, or overwrites a genuine
   // crash marker/autosave left by the real app - see UsingAutosaveTestPaths
   // for the two tests that deliberately exercise this function directly
   // against their own redirected files instead.
   if (getenv("INFINITE_EXITAFTER") == nullptr && !HeadlessJobActive())
      CheckAutosaveRecovery();

   // Embedded local control server (see docs/plans - RemoteControl) - lets an
   // external tool (the Infinite MCP server) drive this running instance.
   // Loopback-only; port overridable for running more than one instance.
   // Skipped in headless/test modes so self-test runs do not bind/unbind port 7777.
   if (!IsHeadlessProcess())
   {
      int controlPort = 7777;
      if (const char* portEnv = getenv("INFINITE_CONTROL_PORT"))
         controlPort = atoi(portEnv);
      RemoteControl::Start(controlPort);
   }

   // The audio engine no longer auto-starts at launch: someone opening the
   // app to work on visuals shouldn't have audio hardware opened out from
   // under them. It's started explicitly from the toolbar's Audio toggle
   // (or "Apply audio settings" once it's already running) instead - see
   // the "Start Audio"/"Stop Audio" button below.

   // Flat list of every registered type, for the double-click search box.
   std::vector<std::pair<std::string, std::string>> allTypes; // (name, category)
   for (const std::string& category : NodeFactory::Instance().GetCategories())
   {
      for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
         if (IsUserSpawnable(name))
            allTypes.emplace_back(name, category);
   }

   const bool selfTest = getenv("IMAGERESYNTH_SELFTEST") != nullptr;
   if (selfTest)
   {
      // headless smoke test of the whole registry: spawn one of every registered
      // node type, feed every input-taking node from a real source so its shader
      // actually compiles and cooks, then report dimensions.
      for (const auto& t : allTypes)
         SpawnNode(t.first, t.second);

      for (GraphNode& gn : gNodes)
      {
         if (gn.typeName == "Shape")
            gSelfTestFeeder = &gn;
      }
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            if (ImageCable* cable = CableFor(gn, slot))
               cable->Connect(gSelfTestFeeder->node.get());
         }
      }
   }
   else
   {
      // The canvas starts empty; the dev test modes below need a fixture graph,
      // but a normal launch gives the user a blank patch.
      const bool wantsFixture =
         getenv("INFINITE_MOVELOGTEST") != nullptr ||
         getenv("INFINITE_RESYNTHTEST") != nullptr ||
         getenv("INFINITE_HIDETEST") != nullptr ||
         getenv("INFINITE_MACROTEST") != nullptr ||
         getenv("INFINITE_RECTEST") != nullptr || getenv("INFINITE_MODTEST") != nullptr ||
         getenv("INFINITE_RECTEARDOWNTEST") != nullptr ||
         getenv("INFINITE_SIZETEST") != nullptr || getenv("INFINITE_INPUTTEST") != nullptr ||
         getenv("INFINITE_DRAGTEST") != nullptr || getenv("INFINITE_COLORTEST") != nullptr ||
         getenv("INFINITE_PICKERTEST") != nullptr || getenv("INFINITE_OSCTEST") != nullptr ||
         getenv("INFINITE_MODBOUNDSTEST") != nullptr || getenv("INFINITE_MODMATRIXTEST") != nullptr ||
         getenv("INFINITE_MODCURVETEST") != nullptr ||
         getenv("INFINITE_GESTUREUNDOTEST") != nullptr ||
         getenv("INFINITE_OFFLINECLOCKTEST") != nullptr ||
         getenv("INFINITE_CULLDRIVENTEST") != nullptr ||
         getenv("INFINITE_PREDBINDTEST") != nullptr ||
         getenv("INFINITE_MPCMODTEST") != nullptr ||
         getenv("INFINITE_LOOPERTRIGTEST") != nullptr ||
         getenv("INFINITE_MIDILEARNTEST") != nullptr ||
         getenv("INFINITE_KBCURSORTEST") != nullptr ||
         getenv("INFINITE_KBDISCRETETEST") != nullptr ||
         getenv("INFINITE_MODMATRIXGEOM") != nullptr;

      if (getenv("INFINITE_AUDIOUITEST") != nullptr)
      {
         // Visual fixture for the audio node UI (audio-node-ui-system.md
         // v3): one of every audio/note node body the layout grammar
         // covers, wired into a working chain, so a UI change can be judged
         // by eye against all of them at once instead of by spawning them by
         // hand every time. Unlike the checks above this one does not exit -
         // it just leaves the window open on a populated canvas.
         // Laid out so nothing overlaps the Wavetable: it is kAudioWideWidth
         // (960) across and the tallest body in the app, so the column of
         // utility nodes has to clear its right edge, not start at 540.
         // Nodes past the first dozen are found by type, not by spawn index:
         // removing or adding a spawn above used to shift every hard-coded
         // index and static_cast the wrong node (R614 segfault).
         auto FixtureNodeByType = [](const char* type) -> INode*
         {
            for (GraphNode& gn : gNodes)
               if (gn.typeName == type)
                  return gn.node.get();
            return nullptr;
         };
         SpawnNode("MIDI Notes", "Notes", 20.0f, 20.0f);            // 0
         SpawnNode("Wavetable", "Synths", 20.0f, 440.0f);           // 1
         SpawnNode("Envelope", "Modulators", 540.0f, 20.0f);        // 2
         SpawnNode("Gain", "Utility", 1040.0f, 440.0f);        // 3
         SpawnNode("Mixer", "Utility", 1040.0f, 760.0f);       // 4
         SpawnNode("Splitter", "Utility", 1300.0f, 440.0f);    // 5
         SpawnNode("Audio Out", "Utility", 1300.0f, 580.0f);   // 6
         SpawnNode("Audio Filter", "AudioEffects", 1650.0f, 20.0f); // 7
         SpawnNode("Dynamics", "AudioEffects", 1650.0f, 780.0f);    // 8
         SpawnNode("Delay", "AudioEffects", 2160.0f, 20.0f);        // 9
         SpawnNode("Reverb", "AudioEffects", 2160.0f, 780.0f);      // 10
         SpawnNode("Stutter", "AudioEffects", 2470.0f, 20.0f);      // 11
         SpawnNode("Arpeggiator", "Notes", 2470.0f, 500.0f);        // 12
         SpawnNode("Bouncing Balls", "Notes", 2720.0f, 900.0f);     // 14
         SpawnNode("Note to CV", "Modulators", 2960.0f, 900.0f);    // 15
         SpawnNode("Note Transpose", "Notes", 2960.0f, 20.0f);      // 16
         SpawnNode("Pitch Bend", "Notes", 3170.0f, 20.0f);          // 17
         SpawnNode("Velocity Curve", "Notes", 3380.0f, 20.0f);      // 18
         SpawnNode("Gate", "Notes", 2960.0f, 300.0f);               // 19
         SpawnNode("Humanizer", "Notes", 3170.0f, 300.0f);          // 20
         SpawnNode("Quantizer", "Notes", 3380.0f, 300.0f);          // 21
         SpawnNode("Glide", "Notes", 2960.0f, 500.0f);              // 22
         SpawnNode("Vibrato", "Modulators", 3170.0f, 500.0f);       // 23
         SpawnNode("Sampler", "Synths", 3380.0f, 500.0f);           // 24
         SpawnNode("Wavetable Shaper", "AudioEffects", 3900.0f, 20.0f); // 25
         SpawnNode("EQ", "AudioEffects", 4360.0f, 20.0f);               // 26
         SpawnNode("Drum Sequencer", "Synths", 4360.0f, 500.0f);        // 27
         SpawnNode("Plugin", "AudioEffects", 4900.0f, 20.0f);           // 28
         {
            // Same idea as the Sampler/Drum Sequencer pre-loads above: give
            // the Plugin node a real plugin so the fixture shows a populated
            // mapping grid rather than the empty "drag one in" state forever.
            // AUDelay ships with macOS; if it somehow isn't there the node
            // just stays empty, which is still a valid thing to look at.
            Platform::PluginDesc fixturePlugin;
            fixturePlugin.format = "au";
            fixturePlugin.identifier = "au:aufx:dely:appl";
            fixturePlugin.name = "AUDelay";
            fixturePlugin.manufacturer = "Apple";
            if (auto* pluginFixture = dynamic_cast<AudioPluginNode*>(gNodes.back().node.get()))
               pluginFixture->LoadPlugin(fixturePlugin);
         }
         SpawnNode("Note Stack", "Notes", 4900.0f, 500.0f);             // 29
         SpawnNode("Keyboard", "Notes", 5400.0f, 500.0f);               // 29b
         SpawnNode("Equation Synth", "Synths", 5400.0f, 20.0f);         // 30
         {
            // A tiny synthetic WAV, loaded immediately, so the visual smoke
            // test's screenshot shows the interactive waveform (bars, the
            // start/end handles, the loop-range dimming) rather than the
            // empty "no sample loaded" placeholder every other run of this
            // fixture would otherwise show forever.
            const std::string fixtureWav = TmpPath("infinite_audiouitest_sampler.wav");
            const int fixtureFrames = 2205;
            std::vector<int16_t> fixturePcm(fixtureFrames);
            for (int i = 0; i < fixtureFrames; i++)
            {
               const float t = (float)i / (float)(fixtureFrames - 1);
               fixturePcm[i] = (int16_t)(sinf(t * 30.0f) * 30000.0f);
            }
            std::ofstream f(fixtureWav, std::ios::binary);
            auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
            auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
            const uint32_t dataSize = (uint32_t)(fixturePcm.size() * sizeof(int16_t));
            f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
            f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
            writeU32(44100); writeU32(44100 * 2); writeU16(2); writeU16(16);
            f.write("data", 4); writeU32(dataSize);
            f.write((const char*)fixturePcm.data(), dataSize);
            f.close();
            if (auto* samplerFixture = dynamic_cast<SamplerNode*>(FixtureNodeByType("Sampler")))
               samplerFixture->LoadFile(fixtureWav);
         }
         // Appended AFTER every earlier spawn on purpose: the block below
         // indexes gNodes[24] (Sampler), [26] (EQ), [1], [3], [4], [6], [7],
         // [8] by hard-coded number, so inserting anywhere earlier silently
         // breaks the fixture.
         SpawnNode("Key-Snap", "AudioEffects", 5900.0f, 700.0f);        // 31
         SpawnNode("Spectrum Slide", "AudioEffects", 6300.0f, 700.0f);   // 32
         SpawnNode("Shape Resonator", "AudioEffects", 6300.0f, 900.0f); // 33
         SpawnNode("Slicer", "Synths", 5900.0f, 20.0f);                 // 34
         {
            // Multi-transient WAV so the slicer's body draws real markers and
            // a real slice count rather than the empty placeholder.
            const std::string slicerWav = TmpPath("infinite_audiouitest_slicer.wav");
            const int slicerFrames = 44100;
            std::vector<int16_t> slicerPcm(slicerFrames, 0);
            for (int b = 0; b < 4; b++)
            {
               const int start = (int)(0.25 * b * 44100);
               for (int i = 0; i < 1323 && start + i < slicerFrames; i++)
               {
                  const float t = (float)i / 44100.0f;
                  const float env = expf(-t * 120.0f);
                  slicerPcm[start + i] = (int16_t)(env * sinf(2.0f * 3.14159265f * 220.0f * t) * 30000.0f);
               }
            }
            std::ofstream sf(slicerWav, std::ios::binary);
            auto writeU32s = [&](uint32_t v) { sf.write((const char*)&v, 4); };
            auto writeU16s = [&](uint16_t v) { sf.write((const char*)&v, 2); };
            const uint32_t slicerDataSize = (uint32_t)(slicerPcm.size() * sizeof(int16_t));
            sf.write("RIFF", 4); writeU32s(36 + slicerDataSize); sf.write("WAVE", 4);
            sf.write("fmt ", 4); writeU32s(16); writeU16s(1); writeU16s(1);
            writeU32s(44100); writeU32s(44100 * 2); writeU16s(2); writeU16s(16);
            sf.write("data", 4); writeU32s(slicerDataSize);
            sf.write((const char*)slicerPcm.data(), slicerDataSize);
            sf.close();
            if (auto* slicerFixture = dynamic_cast<SlicerNode*>(gNodes.back().node.get()))
               slicerFixture->LoadFile(slicerWav);
         }
         {
            // Same idea, for the Drum Sequencer's lane 0/1: a loaded sample
            // and an armed step or two so the fixture's screenshot shows the
            // grid's velocity fills and the strip's real filename instead of
            // the empty "no sample loaded" placeholder forever.
            const std::string drumWav = TmpPath("infinite_audiouitest_drum.wav");
            const int drumFrames = 400;
            std::vector<int16_t> drumPcm(drumFrames, 0);
            for (int i = 0; i < drumFrames && i < 200; i++)
               drumPcm[i] = (int16_t)(0.85f * 32000.0f);
            std::ofstream df(drumWav, std::ios::binary);
            auto writeU32 = [&](uint32_t v) { df.write((const char*)&v, 4); };
            auto writeU16 = [&](uint16_t v) { df.write((const char*)&v, 2); };
            const uint32_t drumDataSize = (uint32_t)(drumPcm.size() * sizeof(int16_t));
            df.write("RIFF", 4); writeU32(36 + drumDataSize); df.write("WAVE", 4);
            df.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
            writeU32(44100); writeU32(44100 * 2); writeU16(2); writeU16(16);
            df.write("data", 4); writeU32(drumDataSize);
            df.write((const char*)drumPcm.data(), drumDataSize);
            df.close();
            if (auto* drumFixture = dynamic_cast<DrumSequencerNode*>(FixtureNodeByType("Drum Sequencer")))
            {
               drumFixture->LoadFileToLane(0, drumWav);
               drumFixture->LoadFileToLane(1, drumWav);
               drumFixture->stepVel[0][0] = 0.8f;
               drumFixture->stepVel[0][4] = 0.5f;
               drumFixture->stepVel[1][2] = 0.7f;
            }
         }
         // Triangle, not the default Circle: the drawn outline and the
         // physics boundary used to disagree by exactly the centroid/bbox
         // shift below the shape's vertices (see DrawBouncingBallsVisualizer's
         // comment) - showing Triangle here means that regression can't
         // silently come back unnoticed the way Circle/Square wouldn't catch it.
         static_cast<BouncingBallsNode*>(FixtureNodeByType("Bouncing Balls"))->shape = BouncingBallsNode::kTriangle;

         auto* osc = static_cast<WavetableNode*>(gNodes[1].node.get());
         // Both engines on, each cross-modulated by the other, so the fixture
         // shows the states that only exist away from the defaults: engine B's
         // table drawn live rather than dimmed, and the warp dropdown's
         // per-engine source labels ("fm (b)" on A, "am (a)" on B).
         osc->engines[1].on = true;
         osc->engines[0].warpMode = SynthModes::kWarpFM;
         osc->engines[0].warpAmount = 0.35f;
         osc->engines[1].warpMode = SynthModes::kWarpAM;
         osc->engines[1].warpAmount = 0.5f;
         osc->engines[0].filterType = SynthModes::kFilterLP24;
         // A four-digit millisecond value, so the fixture shows the widest
         // label/value pair an envelope field ever has to fit.
         osc->engines[0].filterRelease = 2249.0f;
         auto* gain = static_cast<GainNode*>(gNodes[3].node.get());
         auto* mixer = static_cast<MixerNode*>(gNodes[4].node.get());
         auto* out = static_cast<AudioOutputNode*>(gNodes[6].node.get());
         auto* filt = static_cast<AudioEffectNode*>(gNodes[7].node.get());
         auto* dyn = static_cast<AudioEffectNode*>(gNodes[8].node.get());
         // A resonant peak, so the fixture shows the response curve away
         // from its flat default.
         *filt->ParamPtr("type") = (float)AudioFilterDsp::kPeak;
         *filt->ParamPtr("freq") = 800.0f;
         *filt->ParamPtr("q") = 3.0f;
         *filt->ParamPtr("gain") = 8.0f;
         // Threshold pulled up close to the oscillator's own level, so the
         // fixture shows the transfer curve's live operating point/GR bar
         // away from an idle 0 dB reading.
         *dyn->ParamPtr("threshold") = -12.0f;
         *dyn->ParamPtr("ratio") = 6.0f;
         // A couple of bands moved off 0 dB, so the fixture's screenshot
         // shows a real composite curve rather than a flat line at every
         // band's spawn default.
         auto* eq = static_cast<AudioEffectNode*>(FixtureNodeByType("EQ"));
         *eq->ParamPtr("band1Gain") = -6.0f;
         *eq->ParamPtr("band3Gain") = 6.0f;
         *eq->ParamPtr("band3Q") = 2.0f;
         *eq->ParamPtr("band5Gain") = 4.0f;
         *eq->ParamPtr("selectedBand") = 2.0f;
         filt->input.Connect(osc);
         dyn->input.Connect(filt);
         gain->input.Connect(dyn);
         mixer->inputs[0].Connect(gain);
         out->input.Connect(mixer);
         RebuildAudioTopology();
      }
      else if (getenv("INFINITE_WTDRAGTEST") != nullptr)
      {
         // One Wavetable, nothing else on the canvas, so the synthetic mouse
         // below can only be landing on it.
         SpawnNode("Wavetable", "Synths", 20.0f, 20.0f); // 0
      }
      else if (getenv("INFINITE_EQDRAGTEST") != nullptr)
      {
         // One EQ, nothing else on the canvas, left at its spawn defaults so
         // the synthetic mouse below can compute every band's handle
         // position from the known default freq/q/gain table.
         SpawnNode("EQ", "AudioEffects", 20.0f, 20.0f); // 0
      }
      else if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr)
      {
         // One Sampler far from the docked panel, and a folder holding
         // exactly one real, decodable file, so the synthetic drag below
         // has an unambiguous source (the panel row) and destination (the
         // node) - proving the gSampleDragActive release handling actually
         // resolves onto the node under the cursor rather than assuming it.
         SpawnNode("Sampler", "Synths", 900.0f, 60.0f); // 0
         gNodePanelOpen = true;
         gSearchPanelMode = 1;

         // Drop whatever folders/index a real prior session persisted to
         // disk (SampleFolders.json/SampleIndex.json, loaded at startup) -
         // otherwise the panel lists every previously-indexed file, not
         // just this fixture's one sample, and the row-rect capture below
         // can land on an unrelated entry.
         for (const std::string& stale : std::vector<std::string>(gSampleScanner.Folders()))
            gSampleScanner.RemoveFolder(stale);

         AppPaths::EnsureDir(TmpPath("infinite_samplerdrag"));
         const std::string wavPath = TmpPath("infinite_samplerdrag/sample.wav");
         const int numFrames = 4410;
         std::vector<int16_t> pcm(numFrames);
         for (int i = 0; i < numFrames; i++)
         {
            const float t = (float)i / (float)(numFrames - 1);
            pcm[i] = (int16_t)((t * 2.0f - 1.0f) * 32000.0f);
         }
         std::ofstream f(wavPath, std::ios::binary);
         auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
         auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
         const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
         f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
         f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
         writeU32(44100); writeU32(44100 * 2); writeU16(2); writeU16(16);
         f.write("data", 4); writeU32(dataSize);
         f.write((const char*)pcm.data(), dataSize);
         f.close();

         gSampleScanner.AddFolder(TmpPath("infinite_samplerdrag"));
         gSampleScanner.StartScan();
      }
      else if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
      {
         // Same shape as INFINITE_SAMPLERDRAGTEST below, for the Plugins mode:
         // one empty Plugin node far from the docked panel, and a real scan of
         // the installed Audio Units (there is no folder to fake - discovery is
         // a registry query). PluginScanner::SettingsDir redirects to a
         // throwaway subdirectory under this env var, so the scan that follows
         // cannot overwrite the user's real PluginIndex.json.
         SpawnNode("Plugin", "AudioEffects", 900.0f, 60.0f); // 0
         gNodePanelOpen = true;
         gSearchPanelMode = 3;
         AppPaths::EnsureDir(TmpPath("infinite_plugindrag"));
         gPluginScanner.StartScan(TmpPath("infinite_plugindrag"));
      }
      else if (getenv("INFINITE_MEDIADRAGTEST") != nullptr)
      {
         // Same shape as INFINITE_SAMPLERDRAGTEST above, but for the Media
         // mode: one Image Source far from the docked panel, and a folder
         // holding exactly one real, decodable PNG.
         SpawnNode("Image Source", "Source", 900.0f, 60.0f); // 0
         gNodePanelOpen = true;
         gSearchPanelMode = 2;

         // Drop whatever folders/index a real prior session persisted to
         // disk (MediaFolders.json/MediaIndex.json, loaded at startup) -
         // otherwise the panel lists every previously-indexed file, not
         // just this fixture's one image, and the row-rect capture below
         // can land on an unrelated entry.
         for (const std::string& stale : std::vector<std::string>(gMediaScanner.Folders()))
            gMediaScanner.RemoveFolder(stale);

         AppPaths::EnsureDir(TmpPath("infinite_mediadrag"));
         const std::string pngPath = TmpPath("infinite_mediadrag/fixture.png");
         const int side = 8;
         std::vector<unsigned char> pixels(side * side * 4, 255);
         stbi_write_png(pngPath.c_str(), side, side, 4, pixels.data(), side * 4);

         gMediaScanner.AddFolder(TmpPath("infinite_mediadrag"));
         gMediaScanner.StartScan();
      }
      else if (getenv("INFINITE_PINDUPTEST") != nullptr)
      {
         // Duplicate-pin-id regression fixture.
         //
         // A node that hands two of its own controls the same pin id used to
         // hang the whole app: imgui-node-editor links a node's pins into a
         // list as they are emitted, so emitting one id twice in a frame made
         // that list circular and every later walk of it (the hit-test pass,
         // the bounds pass, the drag scan) spun forever inside one frame -
         // 100% CPU, no crash, no visual artefact, reported by users only as
         // a system "hang". That is how the Wavetable node's two engine
         // columns shipped: both engines' "wtTable"/"wtOct"/"wtSemi"/
         // "wtFilter"/"wtWarp"/"##wtOn" hashed to one discrete slot.
         //
         // BeginPin now refuses the second link and counts it instead, so the
         // failure mode is a warning - and this fixture is what reads that
         // counter, because a warning nobody checks is a warning nobody sees.
         //
         // Deliberately spawns EVERY registered type with both param sections
         // forced open: the ids are only allocated by the code that draws the
         // controls, so a collapsed node proves nothing. ROUNDTRIPTEST covers
         // every type too but draws them collapsed, which is exactly why it
         // never saw this.
         float x = 40.0f, y = 40.0f;
         for (const std::string& category : NodeFactory::Instance().GetCategories())
            for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
            {
               GraphNode* gn = SpawnNode(name, category, x, y);
               if (gn == nullptr)
                  continue;
               gn->showParams = true;
               gn->showAdvancedParams = true;
               x += 320.0f;
               if (x > 320.0f * 24)
               {
                  x = 40.0f;
                  y += 700.0f;
               }
            }
      }
      else if (getenv("INFINITE_UISCALETEST") != nullptr)
      {
         // UI-scale fixture (docs/fix-briefs/ui-scale-all-displays.md): representative node
         // bodies - a synth, an audio effect, the mixer and the dense Render 3D - laid out at
         // UI scale 1.0, 1.5 and 2.0 through the live-rescale path. The per-frame half below
         // asserts every node keeps its 1x size in points, i.e. nothing that fits at 1x
         // clips or overflows at 2x. Stacked near the origin so none is culled off-screen
         // at 2x, where the headless window is only 800x500 points.
         const char* kTypes[][2] = {
            { "Wavetable", "Synths" }, { "Delay", "AudioEffects" },
            { "Mixer", "Utility" },    { "Render 3D", "3D" },
         };
         float y = 20.0f;
         for (const auto& t : kTypes)
         {
            GraphNode* gn = SpawnNode(t[0], t[1], 20.0f, y);
            if (gn == nullptr)
            {
               printf("UISCALETEST FAIL: could not spawn %s\n", t[0]);
               continue;
            }
            gn->showParams = true;
            y += 60.0f;
         }
      }
      else if (const char* stressN = getenv("INFINITE_EDPERFTEST"))
      {
         // Editor-scalability fixture: N nodes laid out in a grid, so the
         // per-frame cost of imgui-node-editor's hit-testing pass (ed::End()
         // -> BuildControl, which walks every live node and every live pin
         // once per frame) can be measured against node count instead of
         // guessed at. Paired with the [edperf] timing line printed around
         // ed::End() below.
         const int count = std::max(1, atoi(stressN));
         for (int i = 0; i < count; ++i)
         {
            const int col = i % 24, row = i / 24;
            SpawnNode(getenv("INFINITE_EDPERF_TYPE") ? getenv("INFINITE_EDPERF_TYPE") : "Gain",
                      getenv("INFINITE_EDPERF_CAT") ? getenv("INFINITE_EDPERF_CAT") : "Utility",
                      40.0f + col * 260.0f, 40.0f + row * 200.0f);
         }
      }
      else if (getenv("INFINITE_KBCURSORTEST") != nullptr)
      {
         // R571 slice 1: three nodes in a row, reading order = spawn order.
         SpawnNode("Shape", "Source", 40.0f, 40.0f);   // 0
         SpawnNode("Shape", "Source", 420.0f, 40.0f);  // 1
         SpawnNode("Shape", "Source", 800.0f, 40.0f);  // 2
         for (GraphNode& gn : gNodes)
            gn.showParams = true; // Tab walks params, so they have to be drawn
      }
      else if (getenv("INFINITE_KBDISCRETETEST") != nullptr)
      {
         // R587: Tab reaches dropdowns and checkboxes, Left/Right changes them.
         SpawnNode("Shape", "Source", 40.0f, 40.0f);    // 0: "shape" dropdown
         SpawnNode("Formula", "Source", 420.0f, 40.0f); // 1: "animate" checkbox (starts on)
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (getenv("INFINITE_BYPASSTEST") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);   // 0 white circle
         SpawnNode("invert", "Compositing", 320.0f, 40.0f);  // 1
         SpawnNode("Output", "Utility", 600.0f, 40.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[2], 0)->Connect(gNodes[1].node.get());
      }
      else if (getenv("INFINITE_CURVESLUTTEST") != nullptr)
      {
         // Regression guard for the LUT-not-rebuilt-while-editing bug: the UI
         // (DrawCurveEditor) mutates CurvesNode's CurveShape through the raw
         // reference returned by Shape(channel), never through the
         // AddPoint/MovePoint/RemovePoint wrappers or VisitParams. The
         // fixture reproduces exactly that path.
         SpawnNode("Shape", "Source", 40.0f, 40.0f);    // 0 white circle
         SpawnNode("Curves", "Compositing", 320.0f, 40.0f); // 1
         SpawnNode("Output", "Utility", 600.0f, 40.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[2], 0)->Connect(gNodes[1].node.get());
      }
      else if (getenv("INFINITE_MINIVIEWPORTTEST") != nullptr)
      {
         // Cube -> Select (the +Y side, by normal) -> Transform Selected. Slot B
         // is the same cube with no Select in front of it, so the mini viewport
         // test can compare a mesh carrying a face mask against one that never
         // went through a Select at all.
         // Render first and closest to the origin: the canvas opens at the
         // top-left, and the point of this fixture is to look at its output.
         SpawnNode("Render 3D", "3D", 40.0f, 40.0f);            // 0
         SpawnNode("Geometry", "3D", 1400.0f, 40.0f);           // 1
         SpawnNode("Select", "3D", 1700.0f, 40.0f);             // 2
         SpawnNode("Transform Selected", "3D", 2000.0f, 40.0f); // 3
         SpawnNode("Geometry", "3D", 1400.0f, 500.0f);          // 4

         auto* cube = static_cast<GeometryNode*>(gNodes[1].node.get());
         cube->shape = 1; cube->detail = 24; // 3x3 grid per side, 108 triangles
         auto* select = static_cast<GeometryOpNode*>(gNodes[2].node.get());
         select->input = cube;
         select->op = GeometryOpNode::kSelect;
         select->selectMode = MeshOps::kSelectNormal;
         select->axis = 1;          // Y
         select->selectA = 0.9f;    // facing
         select->selectC = 1.0f;    // +
         auto* move = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         move->input = select;
         move->op = GeometryOpNode::kTransformSelected;
         move->moveAlongNormals = true;
         move->normalAmount = 0.35f;
         move->offsetX = move->offsetY = move->offsetZ = 0.0f;
         move->rotX = move->rotY = move->rotZ = 0.0f;
         move->scaleX = move->scaleY = move->scaleZ = 1.0f;

         auto* plain = static_cast<GeometryNode*>(gNodes[4].node.get());
         plain->shape = 1; plain->detail = 24;
         plain->posX = 1.4f;

         auto* r = static_cast<Render3DNode*>(gNodes[0].node.get());
         r->geometry[0] = move;
         r->geometry[1] = plain;
         r->width = 700.0f; r->height = 700.0f;
         r->camDistance = 4.2f;
         r->targetX = 0.7f;
         for (GraphNode& gn : gNodes)
            gn.showParams = false;
      }
      else if (getenv("INFINITE_GEOTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);      // 0 points source
         SpawnNode("Geometry", "3D", 40.0f, 400.0f);     // 1 instance shape
         SpawnNode("Instance on Points", "3D", 320.0f, 40.0f); // 2
         // "Array" rather than "Geometry Op": the operators were split into ten
         // separately registered nodes, and spawning the old name silently
         // failed, leaving this fixture reading past the end of gNodes.
         SpawnNode("Array", "3D", 320.0f, 400.0f);       // 3
         SpawnNode("Camera", "3D", 620.0f, 400.0f);      // 4
         SpawnNode("Light", "3D", 620.0f, 620.0f);       // 5
         SpawnNode("Render 3D", "3D", 900.0f, 40.0f);    // 6

         auto* pts = static_cast<GeometryNode*>(gNodes[0].node.get());
         pts->shape = 3; pts->detail = 24;               // icosphere
         auto* shape = static_cast<GeometryNode*>(gNodes[1].node.get());
         shape->shape = 1;                               // cube
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         inst->pointSource = pts; inst->instanceShape = shape;
         inst->pointMode = 0; inst->instanceScale = 0.07f; inst->maxPoints = 5000;
         inst->color[0] = 1.0f; inst->color[1] = 0.55f; inst->color[2] = 0.25f;
         auto* op = static_cast<GeometryOpNode*>(gNodes[3].node.get());
         op->input = shape; op->op = GeometryOpNode::kArray;
         op->count = 8; op->radial = true; op->radius = 1.6f; op->scaleStep = 0.92f;
         op->inheritMaterial = false;
         op->color[0] = 0.35f; op->color[1] = 0.7f; op->color[2] = 1.0f;
         auto* cam = static_cast<CameraNode*>(gNodes[4].node.get());
         cam->distance = 5.0f; cam->elevation = 25.7831f;
         auto* light = static_cast<LightNode*>(gNodes[5].node.get());
         light->intensity = 1.6f;
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         r->geometry[0] = inst; r->geometry[1] = op;
         r->camera = cam; r->lights[0] = light;
         r->width = 700.0f; r->height = 700.0f;
         gNodes[2].showParams = true;
         gNodes[6].showParams = true;
      }
      else if (getenv("INFINITE_MAPTEST") != nullptr)
      {
         SpawnNode("Sphere", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Material", "3D", 400.0f, 40.0f);     // 1
         SpawnNode("Noise", "Source", 40.0f, 400.0f);    // 2
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);    // 3
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         mat->input = static_cast<GeometryNode*>(gNodes[0].node.get());
         mat->metallic = 0.4f;
         mat->roughness = 0.5f;
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         render->geometry[0] = mat;
         render->width = 300.0f; render->height = 300.0f;
         render->samples = 0;
      }
      else if (getenv("INFINITE_DISPLACETEST") != nullptr)
      {
         // A subdivided sphere pushed by a Noise texture, end to end through
         // the actual GPU readback path (not a synthetic buffer, unlike the
         // scalar/vector checks in MESHOPTEST) - this is what verifies the
         // texture-to-mesh plumbing itself, not just MeshOps::Displace.
         SpawnNode(getenv("INFINITE_DISPLACE_CUBE") ? "Cube" : "Sphere", "3D", 40.0f, 40.0f); // 0
         SpawnNode("Subdivide", "3D", 320.0f, 40.0f);      // 1
         SpawnNode("Displacement", "3D", 600.0f, 40.0f);   // 2
         SpawnNode("Noise", "Source", 40.0f, 400.0f);      // 3
         SpawnNode("Camera", "3D", 900.0f, 400.0f);        // 4
         SpawnNode("Light", "3D", 900.0f, 620.0f);         // 5
         SpawnNode("Render 3D", "3D", 900.0f, 40.0f);      // 6

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->detail = 24;
         auto* sub = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         sub->op = GeometryOpNode::kSubdivide; sub->input = geo; sub->levels = 2;
         auto* disp = static_cast<DisplacementNode*>(gNodes[2].node.get());
         disp->input = sub;
         disp->mode = DisplacementNode::kScalar;
         disp->strength = 0.35f;
         disp->TextureInput().Connect(gNodes[3].node.get());
         auto* cam = static_cast<CameraNode*>(gNodes[4].node.get());
         cam->distance = 4.0f;
         auto* r = static_cast<Render3DNode*>(gNodes[6].node.get());
         r->geometry[0] = disp;
         r->camera = cam; r->lights[0] = static_cast<LightNode*>(gNodes[5].node.get());
         r->width = 700.0f; r->height = 700.0f;
         gNodes[2].showParams = true;
         gNodes[6].showParams = true;
      }
      else if (getenv("INFINITE_MAPPINGVIZTEST") != nullptr)
      {
         // Dev-only visual check: a checkerboard (Image Source's built-in
         // fallback when no file is loaded) box-projected onto a cube through
         // Generated coordinates, next to the same cube left on plain UV, so a
         // screenshot shows the per-face orientation directly.
         SpawnNode("Cube", "3D", 40.0f, 40.0f);            // 0
         SpawnNode("Mapping", "3D", 320.0f, 40.0f);        // 1
         SpawnNode("Image Source", "Source", 40.0f, 400.0f); // 2
         SpawnNode("Material", "3D", 600.0f, 40.0f);       // 3
         SpawnNode("Cube", "3D", 40.0f, 700.0f);           // 4 plain UV comparison
         SpawnNode("Material", "3D", 320.0f, 700.0f);      // 5
         SpawnNode("Render 3D", "3D", 900.0f, 40.0f);      // 6
         SpawnNode("Noise", "Source", 40.0f, 1000.0f);     // 7 normal map source

         auto* cube = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mapping = static_cast<MappingNode*>(gNodes[1].node.get());
         mapping->input = cube;
         mapping->space = kMapSpaceGenerated;
         mapping->scaleX = 2.0f; mapping->scaleY = 2.0f; mapping->scaleZ = 2.0f;

         auto* img = static_cast<ImageSourceNode*>(gNodes[2].node.get());
         auto* noise = static_cast<NoiseNode*>(gNodes[7].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[3].node.get());
         mat->input = mapping;
         mat->TextureInput().Connect(img);
         mat->MapInput(kMapNormal).Connect(noise);
         mat->roughness = 0.6f;
         mat->normalStrength = 2.0f;

         auto* cubeB = static_cast<GeometryNode*>(gNodes[4].node.get());
         cubeB->posX = 2.5f;
         auto* matB = static_cast<MaterialNode*>(gNodes[5].node.get());
         matB->input = cubeB;
         matB->TextureInput().Connect(img);
         matB->roughness = 0.6f;

         auto* render = static_cast<Render3DNode*>(gNodes[6].node.get());
         render->geometry[0] = mat;
         render->geometry[1] = matB;
         render->width = 900.0f; render->height = 500.0f;
         render->samples = 0;
         render->camDistance = 6.0f;
         render->camAzimuth = 40.107f;
         render->camElevation = 20.0535f;
         render->targetX = 1.25f;
         gNodes[6].showParams = true;
      }
      else if (getenv("INFINITE_SHADOWTEST") != nullptr)
      {
         // A sphere above a wide flat plane: the arrangement where a shadow is
         // unmistakable if it works and obviously absent if it does not.
         SpawnNode("Plane", "3D", 40.0f, 40.0f);        // 0 ground
         SpawnNode("Sphere", "3D", 40.0f, 400.0f);      // 1 caster
         SpawnNode("Light", "3D", 40.0f, 760.0f);       // 2
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);   // 3

         auto* ground = static_cast<GeometryNode*>(gNodes[0].node.get());
         ground->rotX = -1.5707963f;   // lay it flat
         ground->uniformScale = 6.0f;
         ground->posY = -1.0f;
         auto* ball = static_cast<GeometryNode*>(gNodes[1].node.get());
         ball->posY = 0.4f;
         auto* light = static_cast<LightNode*>(gNodes[2].node.get());
         light->type = 0;              // directional
         light->elevation = 63.0254f;  // high, so the shadow lands on the plane
         light->intensity = 2.0f;

         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         render->geometry[0] = ground;
         render->geometry[1] = ball;
         render->lights[0] = light;
         render->width = 400.0f; render->height = 400.0f;
         render->samples = 0;
         render->shadowsEnabled = false; // turned on mid-test to compare
         render->camElevation = 40.107f;
         render->camDistance = 7.0f;
      }
      else if (getenv("INFINITE_BUGTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);       // 0 -> array -> render
         SpawnNode("Array", "3D", 400.0f, 40.0f);         // 1
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);     // 2
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);      // 3 -> join
         SpawnNode("Geometry", "3D", 40.0f, 760.0f);      // 4 -> join
         SpawnNode("Join Geometry", "3D", 400.0f, 500.0f); // 5

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* arr = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         arr->input = geo;
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] = arr;

         auto* ja = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* jb = static_cast<GeometryNode*>(gNodes[4].node.get());
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());
         join->inputs[0] = ja;
         join->inputs[1] = jb;
      }
      else if (getenv("INFINITE_FIXTEST") != nullptr)
      {
         SpawnNode("Random", "Modulators", 40.0f, 40.0f);   // 0
         SpawnNode("Random", "Modulators", 40.0f, 300.0f);  // 1
         SpawnNode("Random", "Modulators", 40.0f, 560.0f);  // 2
         SpawnNode("Geometry", "3D", 400.0f, 40.0f);        // 3
         SpawnNode("Geometry", "3D", 400.0f, 300.0f);       // 4
         SpawnNode("Join Geometry", "3D", 760.0f, 40.0f);   // 5
         SpawnNode("Render 3D", "3D", 1100.0f, 40.0f);      // 6

         auto* a = static_cast<GeometryNode*>(gNodes[3].node.get());
         auto* b = static_cast<GeometryNode*>(gNodes[4].node.get());
         a->posX = -1.0f;
         b->posX = 1.5f; b->shape = 2;
         auto* join = static_cast<JoinGeometryNode*>(gNodes[5].node.get());
         join->inputs[0] = a;
         join->inputs[1] = b;
         static_cast<Render3DNode*>(gNodes[6].node.get())->geometry[0] = join;
      }
      else if (getenv("INFINITE_CLOTHTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);     // 0
         SpawnNode("Cloth", "3D", 400.0f, 40.0f);       // 1
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);   // 2
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->shape = 0;    // plane
         geo->detail = 16;
         geo->rotX = -1.5707963f; // stand it up so gravity has something to do
         auto* cloth = static_cast<ClothNode*>(gNodes[1].node.get());
         cloth->input = geo;
         cloth->pinMode = ClothNode::kPinTop;
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] = cloth;
      }
      else if (getenv("INFINITE_PARTICLETEST") != nullptr)
      {
         SpawnNode("Particle System", "3D", 40.0f, 40.0f);      // 0
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);            // 1 instanced shape
         SpawnNode("Instance on Points", "3D", 400.0f, 40.0f);  // 2
         SpawnNode("Render 3D", "3D", 760.0f, 40.0f);           // 3

         auto* ps = static_cast<ParticleSystemNode*>(gNodes[0].node.get());
         ps->emitRate = 600.0f;
         ps->lifetime = 2.0f;
         ps->seed = 7.0f;
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         inst->cloudSource = ps;
         inst->instanceShape = static_cast<GeometryNode*>(gNodes[1].node.get());
         inst->instanceScale = 0.04f;
         static_cast<Render3DNode*>(gNodes[3].node.get())->geometry[0] = inst;
      }
      else if (getenv("INFINITE_AUDIORECTEST") != nullptr)
      {
         std::string audioPath = getenv("INFINITE_AUDIORECTEST");
         if (audioPath == "1" || !std::filesystem::exists(audioPath))
         {
            audioPath = TmpPath("infinite_audiorectest_tone.wav");
            constexpr double kToneHz = 440.0;
            constexpr double kToneSampleRate = 48000.0;
            constexpr double kToneSeconds = 3.0;
            constexpr float kToneAmplitude = 0.5f;
            const int toneFrames = (int)(kToneSampleRate * kToneSeconds);
            std::vector<float> tone(toneFrames);
            for (int i = 0; i < toneFrames; i++)
               tone[i] = kToneAmplitude * std::sin(2.0 * M_PI * kToneHz * (double)i / kToneSampleRate);
            AudioRecordings::WriteWav(audioPath, tone.data(), toneFrames, kToneSampleRate, 1);
         }

         SpawnNode("Shape", "Source", 40.0f, 40.0f);       // 0
         SpawnNode("Output", "Utility", 320.0f, 40.0f);     // 1
         SpawnNode("Audio File", "Modulators", 40.0f, 400.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* audio = static_cast<AudioFileNode*>(gNodes[2].node.get());
         audio->Open(audioPath);
         audio->monitor = false; // silent while the test runs
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         out->includeAudio = true;
         out->AudioInput().Connect(audio);
         out->recordFps = 30;
      }
      else if (getenv("INFINITE_VIDEOAUDIOTEST") != nullptr)
      {
         // Exercises VideoSourceNode's new audio output end-to-end without
         // depending on an external test-media file (none is committed to
         // the repo - see the note by the printf below): records a movie
         // whose audio track is a synthesized tone through the ordinary
         // OutputNode recording pipeline (same one INFINITE_AUDIORECTEST
         // drives), then opens that movie back through VideoSourceNode and
         // Platform::DecodeVideoAudioTrackToBuffer directly, checking both
         // that HasAudio() comes up true and that the decoded samples still
         // carry a detectable tone at the frequency that was recorded.
         constexpr double kToneHz = 440.0;
         constexpr double kToneSampleRate = 48000.0;
         constexpr double kToneSeconds = 1.5;
         constexpr float kToneAmplitude = 0.5f;
         const int toneFrames = (int)(kToneSampleRate * kToneSeconds);
         std::vector<float> tone(toneFrames);
         for (int i = 0; i < toneFrames; i++)
            tone[i] = kToneAmplitude * std::sin(2.0 * M_PI * kToneHz * (double)i / kToneSampleRate);
         AudioRecordings::WriteWav(TmpPath("infinite_videoaudiotest_tone.wav"), tone.data(), toneFrames,
                                    kToneSampleRate, 1);

         SpawnNode("Shape", "Source", 40.0f, 40.0f);           // 0
         SpawnNode("Output", "Output", 320.0f, 40.0f);         // 1
         SpawnNode("Audio File", "Modulators", 40.0f, 400.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* audio = static_cast<AudioFileNode*>(gNodes[2].node.get());
         audio->Open(TmpPath("infinite_videoaudiotest_tone.wav"));
         audio->monitor = false; // silent while the test runs
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         out->includeAudio = true;
         out->AudioInput().Connect(audio);
         out->recordFps = 30;
      }
      else if (getenv("INFINITE_VIDEOSPEEDTEST") != nullptr)
      {
         // Same minimal record-a-clip fixture as INFINITE_VIDEOAUDIOTEST -
         // this test only needs a real movie file to Open(), not the tone
         // itself, but recording is the simplest way to produce one.
         SpawnNode("Shape", "Source", 40.0f, 40.0f);       // 0
         SpawnNode("Output", "Output", 320.0f, 40.0f);     // 1
         SpawnNode("Audio File", "Modulators", 40.0f, 400.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         out->includeAudio = false;
         out->recordFps = 30;
      }
      else if (getenv("INFINITE_OFFLINERENDERTEST") != nullptr)
      {
         // Offline Render's whole point is that it drives the graph and audio
         // engine in lockstep, independent of wall-clock time - so unlike
         // every other recording test above, the audio source here is a live
         // synth (Oscillator) rather than an AudioFileNode. That matters
         // because StartOfflineRender special-cases AudioFileNode sources
         // (baking the file straight into the muxer) and only exercises the
         // AudioEngine::ProcessOffline + capture-ring path - the actually new
         // code - for everything else.
         SpawnNode("Shape", "Source", 40.0f, 40.0f);       // 0
         SpawnNode("Output", "Utility", 320.0f, 40.0f);    // 1
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         // INFINITE_OFFLINERENDER_NOAUDIO renders the same take video-only,
         // to tell a stall that involves the writer's audio input apart from
         // one in the video path alone.
         out->includeAudio = getenv("INFINITE_OFFLINERENDER_NOAUDIO") == nullptr;

         if (getenv("INFINITE_OFFLINERENDER_AUDIOFILE") != nullptr)
         {
            std::string audioPath = TmpPath("infinite_offlinerender_tone.wav");
            if (!std::filesystem::exists(audioPath))
            {
               constexpr double kToneHz = 440.0;
               constexpr double kToneSampleRate = 48000.0;
               constexpr double kToneSeconds = 5.0;
               constexpr float kToneAmplitude = 0.5f;
               const int toneFrames = (int)(kToneSampleRate * kToneSeconds);
               std::vector<float> tone(toneFrames);
               for (int i = 0; i < toneFrames; i++)
                  tone[i] = kToneAmplitude * std::sin(2.0 * M_PI * kToneHz * (double)i / kToneSampleRate);
               AudioRecordings::WriteWav(audioPath, tone.data(), toneFrames, kToneSampleRate, 1);
            }
            SpawnNode("Audio File", "Modulators", 40.0f, 400.0f); // 2
            auto* audio = static_cast<AudioFileNode*>(gNodes[2].node.get());
            audio->Open(audioPath);
            out->AudioInput().Connect(audio);
         }
         else
         {
            SpawnNode("Oscillator", "Synths", 40.0f, 400.0f); // 2
            auto* osc = static_cast<OscillatorNode*>(gNodes[2].node.get());
            if (getenv("INFINITE_OFFLINERENDER_MIXER") != nullptr)
            {
               SpawnNode("Mixer", "Utility", 180.0f, 400.0f); // 3
               INode* mixer = gNodes[3].node.get();
               if (AudioCable* c = mixer->AudioInputSlot(0))
                  c->Connect(osc);
               out->AudioInput().Connect(mixer);
            }
            else
            {
               out->AudioInput().Connect(osc);
            }
         }
         // INFINITE_OFFLINERENDER_RES=WxH sizes the source, and with it the
         // take: frame bytes are what decide whether the encoder queue's byte
         // budget is ever reached, so a stall that only shows up at 1080p is
         // invisible at the Shape node's 1024x1024 default.
         if (const char* resEnv = getenv("INFINITE_OFFLINERENDER_RES"))
         {
            int rw = 0, rh = 0;
            if (sscanf(resEnv, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0)
            {
               auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
               shape->width = (float)rw;
               shape->height = (float)rh;
            }
         }
         out->recordVideoPath = TmpPath("infinite_offlinerender.mov");
         // fps/duration are overridable so the same fixture can sweep rates
         // that don't divide the sample rate evenly (24fps @ 44100 =
         // 1837.5 samples/frame) and rates whose per-frame audio budget
         // exceeds one kAudioMaxBlockFrames block (anything below ~12fps) -
         // the two ways a per-frame audio quota goes wrong at some fps but
         // not others. Default 10fps/1s is the low-fps multi-block case.
         const char* fpsEnv = getenv("INFINITE_OFFLINERENDER_FPS");
         const char* secEnv = getenv("INFINITE_OFFLINERENDER_SECONDS");
         out->offlineFps = fpsEnv ? std::max(1, atoi(fpsEnv)) : 10;
         out->offlineDurationSeconds = secEnv ? std::max(1, atoi(secEnv)) : 1;
         out->offlinePrerollFrames = 2;

         // Re-open the device at a requested rate/buffer size, exactly as
         // the Audio settings menu's "Apply audio settings" does, so the
         // sweep can prove a take is budgeted and muxed at whatever the
         // device actually negotiates rather than at one hardcoded rate -
         // the defect this fixture exists to catch.
         if (const char* rateEnv = getenv("INFINITE_OFFLINERENDER_RATE"))
         {
            const char* bufEnv = getenv("INFINITE_OFFLINERENDER_BUFFER");
            AudioEngine::Instance().Stop();
            AudioEngine::Instance().SetRequestedSampleRate(atof(rateEnv));
            if (bufEnv != nullptr)
               AudioEngine::Instance().SetRequestedBufferFrames(atoi(bufEnv));
            std::string rateErr;
            if (!StartAudioEngine(rateErr))
               fprintf(stderr, "offline render test: could not reopen device: %s\n", rateErr.c_str());
         }
         RebuildAudioTopology();
      }
      else if (getenv("INFINITE_OFFLINERENDERREFUSETEST") != nullptr)
      {
         // A hardware-driven source (Video In) anywhere in the patch must
         // make StartOfflineRenderSession refuse up front - there is no
         // camera feed to replay deterministically outside wall-clock time.
         SpawnNode("Video In", "Source", 40.0f, 40.0f);    // 0
         SpawnNode("Output", "Utility", 320.0f, 40.0f);    // 1
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         out->recordVideoPath = TmpPath("infinite_offlinerenderrefuse.mov");
      }
      else if (getenv("INFINITE_PATCHTEST") != nullptr)
      {
         // A patch touching every kind of connection: image cables, a geometry
         // chain, camera and light pins, a modulation binding, and - since
         // Phase 2 collapsed IGeometrySource/IPointCloudSource/ICurveSource
         // into one interface - every shape that unification could get wrong:
         // two geometry slots on one Render 3D, both slots of Instance on
         // Points plus its cloud slot, Metaballs fed by a cloud, and Path
         // fed by a curve. CableRecord carries no type info, so a slot
         // numbering slip here would load silently wrong rather than failing.
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Smooth", "3D", 320.0f, 40.0f);         // 1
         SpawnNode("Material", "3D", 600.0f, 40.0f);       // 2
         SpawnNode("Camera", "3D", 320.0f, 400.0f);        // 3
         SpawnNode("Light", "3D", 320.0f, 620.0f);         // 4
         SpawnNode("Render 3D", "3D", 880.0f, 40.0f);      // 5
         SpawnNode("invert", "Compositing", 1160.0f, 40.0f);     // 6
         SpawnNode("Output", "Utility", 1440.0f, 40.0f);    // 7
         SpawnNode("Path", "Modulators", 40.0f, 800.0f);   // 8
         SpawnNode("Audio File", "Modulators", 40.0f, 1000.0f); // 9
         SpawnNode("Particle System", "3D", 40.0f, 1200.0f);    // 10
         SpawnNode("Metaballs", "3D", 320.0f, 1200.0f);         // 11
         SpawnNode("Curve", "3D", 40.0f, 1400.0f);              // 12
         SpawnNode("Instance on Points", "3D", 600.0f, 1200.0f); // 13
         SpawnNode("Geometry", "3D", 880.0f, 1200.0f);          // 14: instance shape

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* smooth = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[5].node.get());
         auto* path = static_cast<PathNode*>(gNodes[8].node.get());
         auto* audioFile = static_cast<AudioFileNode*>(gNodes[9].node.get());
         auto* particles = static_cast<ParticleSystemNode*>(gNodes[10].node.get());
         auto* meta = static_cast<MetaBallNode*>(gNodes[11].node.get());
         auto* curve = static_cast<CurveNode*>(gNodes[12].node.get());
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[13].node.get());
         auto* shape = static_cast<GeometryNode*>(gNodes[14].node.get());
         audioFile->Open(TmpPath("models/tone.wav"));
         audioFile->monitor = false;

         geo->shape = 4; geo->detail = 33; geo->posX = 1.25f;
         geo->color[0] = 0.11f; geo->color[1] = 0.22f; geo->color[2] = 0.33f;
         geo->emission = 2.5f;
         smooth->iterations = 7; smooth->amount = 0.66f;
         mat->metallic = 0.77f; mat->roughness = 0.11f;
         render->samples = 3; render->exposure = 1.8f; render->width = 512.0f;
         path->shape = PathNode::kHelix; path->turns = 5.0f; path->pingPong = true;

         smooth->input = geo;
         mat->input = smooth;
         render->geometry[0] = mat;
         render->geometry[1] = inst; // second geometry slot: an instanced cloud
         render->camera = static_cast<CameraNode*>(gNodes[3].node.get());
         render->lights[0] = static_cast<LightNode*>(gNodes[4].node.get());
         CableFor(gNodes[6], 0)->Connect(gNodes[5].node.get());
         CableFor(gNodes[7], 0)->Connect(gNodes[6].node.get());
         auto* outNode = static_cast<OutputNode*>(gNodes[7].node.get());
         outNode->includeAudio = true;
         outNode->AudioInput().Connect(audioFile);
         Modulation::Instance().Bind(gNodes[0].index, 6, gNodes[8].index, 2);

         meta->cloudSource = particles;
         path->curveSource = curve;
         inst->pointSource = geo;
         inst->instanceShape = shape;
         inst->cloudSource = particles;
      }
      else if (getenv("INFINITE_AUTOSAVETEST") != nullptr)
      {
         // One of each list BuildPatchData populates: an image cable, a
         // geometry chain with camera/light pins, an audio cable, a note
         // cable, a modulation link, a palette binding, a per-param
         // expression, and an expression global.
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Smooth", "3D", 320.0f, 40.0f);         // 1
         SpawnNode("Material", "3D", 600.0f, 40.0f);       // 2
         SpawnNode("Camera", "3D", 320.0f, 400.0f);        // 3
         SpawnNode("Light", "3D", 320.0f, 620.0f);         // 4
         SpawnNode("Render 3D", "3D", 880.0f, 40.0f);      // 5
         SpawnNode("Output", "Output", 1160.0f, 40.0f);    // 6
         SpawnNode("Path", "Modulators", 40.0f, 800.0f);   // 7: modulator source
         SpawnNode("Palette", "Modulators", 40.0f, 1000.0f); // 8: palette source
         SpawnNode("Ramp", "Source", 320.0f, 1000.0f);       // 9: palette target
         SpawnNode("Wavetable", "Synths", 40.0f, 1200.0f);   // 10: audio source
         SpawnNode("MIDI Notes", "Notes", 40.0f, 1400.0f);   // 11: note source
         SpawnNode("Note Filter", "Notes", 320.0f, 1400.0f); // 12: note dest

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* smooth = static_cast<GeometryOpNode*>(gNodes[1].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[5].node.get());
         auto* out = static_cast<OutputNode*>(gNodes[6].node.get());
         auto* path = static_cast<PathNode*>(gNodes[7].node.get());
         auto* palette = static_cast<PaletteNode*>(gNodes[8].node.get());
         auto* ramp = static_cast<RampNode*>(gNodes[9].node.get());
         auto* wave = static_cast<WavetableNode*>(gNodes[10].node.get());
         auto* midi = static_cast<MidiNotesNode*>(gNodes[11].node.get());
         auto* filter = static_cast<NoteFilterNode*>(gNodes[12].node.get());

         geo->shape = 4; geo->detail = 33; geo->posX = 1.25f;
         geo->color[0] = 0.11f; geo->color[1] = 0.22f; geo->color[2] = 0.33f;
         geo->emission = 2.5f;
         smooth->iterations = 7; smooth->amount = 0.66f;
         mat->metallic = 0.77f; mat->roughness = 0.11f;
         render->samples = 3; render->exposure = 1.8f; render->width = 512.0f;
         palette->swatchCount = 3;

         smooth->input = geo;
         mat->input = smooth;
         render->geometry[0] = mat;
         render->camera = static_cast<CameraNode*>(gNodes[3].node.get());
         render->lights[0] = static_cast<LightNode*>(gNodes[4].node.get());
         out->Input().Connect(render);                    // image cable
         out->AudioInput().Connect(wave);                 // audio cable
         if (NoteCable* noteIn = filter->NoteInputSlot(0))
            noteIn->Connect(midi);                        // note cable

         Modulation::Instance().Bind(gNodes[0].index, 6, gNodes[7].index, 2);
         Modulation::Instance().SetExpression(gNodes[0].index, 3, "sin(t)");
         PaletteBinding::Instance().Bind(gNodes[9].index, 0, gNodes[8].index, 0);
         ExprGlobals::All().push_back({ "myGlobal", "t*0.5", 0.0f, std::string() });
      }
      else if (getenv("INFINITE_DELETECRASHTEST") != nullptr)
      {
         // One source feeding every kind of geometry-ish pin at once - Render
         // 3D, all three Instance on Points pins, Metaballs' cloud, and both
         // of Path's pins - then deleted, to prove DisconnectAllTo's generic
         // GeometryInputSlot loop (Phase 2b) finds every one of them. Missing
         // a field here is exactly the bug class this refactor removes: a
         // dangling pointer to a freed node that crashes on the next cook.
         SpawnNode("Particle System", "3D", 40.0f, 40.0f);       // 0: the shared source
         SpawnNode("Render 3D", "3D", 320.0f, 40.0f);            // 1
         SpawnNode("Instance on Points", "3D", 320.0f, 260.0f);  // 2
         SpawnNode("Metaballs", "3D", 320.0f, 480.0f);           // 3
         SpawnNode("Path", "Modulators", 320.0f, 700.0f);        // 4

         auto* src = static_cast<ParticleSystemNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[2].node.get());
         auto* meta = static_cast<MetaBallNode*>(gNodes[3].node.get());
         auto* path = static_cast<PathNode*>(gNodes[4].node.get());

         render->geometry[0] = src;
         inst->pointSource = src;
         inst->instanceShape = src;
         inst->cloudSource = src;
         meta->cloudSource = src;
         path->curveSource = src;
         path->geometrySource = src;
      }
      else if (getenv("INFINITE_AUDIOGRAPHTEST") != nullptr)
      {
         // Wavetable -> Gain -> Audio Out, the exact three-node chain P2's
         // exit criterion names: exercises WireInputSlot's audio branch,
         // RebuildAudioTopology, save/load of the new "aud" patch tag, and
         // (later, at frameId checkpoints below) mid-chain delete survival.
         SpawnNode("Wavetable", "Synths", 40.0f, 40.0f);         // 0
         SpawnNode("Gain", "Synths", 320.0f, 40.0f);             // 1
         SpawnNode("Audio Out", "Utility", 600.0f, 40.0f); // 2

         auto* osc = static_cast<WavetableNode*>(gNodes[0].node.get());
         auto* gain = static_cast<GainNode*>(gNodes[1].node.get());
         auto* out = static_cast<AudioOutputNode*>(gNodes[2].node.get());
         gain->input.Connect(osc);
         out->input.Connect(gain);
         RebuildAudioTopology();
      }
      else if (getenv("INFINITE_NEWPATCHAUDIOTEST") != nullptr)
      {
         // Wavetable -> Gain -> Audio Out, rendered, then File > New. See the
         // frameId == 4 block that drives it.
         SpawnNode("Wavetable", "Synths", 40.0f, 40.0f);
         SpawnNode("Gain", "Synths", 320.0f, 40.0f);
         SpawnNode("Audio Out", "Utility", 600.0f, 40.0f);

         auto* osc = static_cast<WavetableNode*>(gNodes[0].node.get());
         auto* gain = static_cast<GainNode*>(gNodes[1].node.get());
         auto* out = static_cast<AudioOutputNode*>(gNodes[2].node.get());
         gain->input.Connect(osc);
         out->input.Connect(gain);
         RebuildAudioTopology();
      }
      else if (getenv("INFINITE_AUDIOLIFECYCLETEST") != nullptr)
      {
         // Repro + regression guard for
         // docs/plans/optimization/prompts/01-audio-lifecycle-correctness.md:
         //
         // Bug 1/2: RebuildAudioTopology only calls PrepareToPlay (which is
         // what pushes the real rate into a node's ParamMailbox) when
         // AudioEngine::SampleRate() is already > 0 - true on every cable
         // edit/patch load once the engine is running, but never true for
         // "load a patch, then press Start Audio", since the engine has no
         // rate at all until Start() returns. Every node's mailbox is left on
         // whatever it was built/last-rebuilt with until the next unrelated
         // graph edit. Bug 2 (stale AudioEngine::mSampleRate readers) shares
         // the same root cause and the same fix - see StartAudioEngine's
         // comment a few hundred lines up from RebuildAudioTopology.
         //
         // Bug 3: AudioEngine::Stop() didn't clear mLastCallbackMs, so the
         // first real callback after the next Start() compared its wall-
         // clock gap against a timestamp from before the device was closed -
         // reopening a device reliably takes far longer than one block
         // period, so this tripped kXrunGapMultiplier on essentially every
         // restart, a false positive rather than a real dropped block.
         //
         // This runs once at startup (like AUDIOGRAPHTEST just above), not
         // frameId-gated - every assertion below is synchronous main-thread
         // state, no cook-and-observe-next-frame step needed.
         bool overallOk = true;
         auto Check = [&](const char* name, bool ok)
         {
            printf("  [%s] %s\n", ok ? "pass" : "FAIL", name);
            overallOk = overallOk && ok;
         };

         // Start from a known Off state so "load patch, then press Start" is
         // the real first transition, not accidentally masked by whatever
         // ran before this block.
         if (AudioEngine::Instance().SampleRate() > 0.0)
            AudioEngine::Instance().Stop();

         GraphNode* wtGn = SpawnNode("Wavetable", "Synths", 40.0f, 40.0f);
         const int wtIndex = wtGn->index;
         GraphNode* outGn = SpawnNode("Audio Out", "Utility", 320.0f, 40.0f);
         const int outIndex = outGn->index;
         // Re-resolve - SpawnNode's push_back into gNodes can reallocate and
         // invalidate the pointer the earlier SpawnNode call returned.
         wtGn = FindNodeByIndex(wtIndex);
         outGn = FindNodeByIndex(outIndex);
         auto* wt = static_cast<WavetableNode*>(wtGn->node.get());
         auto* out = static_cast<AudioOutputNode*>(outGn->node.get());
         out->input.Connect(wt);

         // "Patch loaded, engine still off": a real patch load calls
         // RebuildAudioTopology exactly like this, and its sampleRate > 0.0
         // guard is expected to skip PrepareToPlay here - nothing has
         // negotiated a rate yet. Asserted as a sanity check on the test
         // itself, not the fix: if this ever fails, the rest of this test
         // proves nothing.
         RebuildAudioTopology();
         Check("sanity: mailbox unprepared before any Start",
               wt->DebugMailboxSampleRate() == 0.0);

         std::string startError;
         const bool started = StartAudioEngine(startError);
         Check("AudioEngine::Start succeeds", started);

         const double deviceRate = AudioEngine::Instance().SampleRate();
         Check("engine reports a real negotiated rate after Start", deviceRate > 0.0);
         // The bug-1 assertion: without StartAudioEngine's post-Start
         // RebuildAudioTopology, wt's mailbox would still read 0.0 here.
         Check("node's mailbox reports the device sample rate after Start",
               started && deviceRate > 0.0 && wt->DebugMailboxSampleRate() == deviceRate);

         // Bug 3: let a couple of real callbacks land so mLastCallbackMs
         // reflects live device state (not just whatever Start() left it as),
         // then cycle Stop/Start and confirm the cycle itself adds no xrun.
         std::this_thread::sleep_for(std::chrono::milliseconds(80));

         AudioEngine::Instance().Stop();
         std::string restartError;
         const bool restarted = StartAudioEngine(restartError);
         Check("AudioEngine::Start succeeds on restart", restarted);
         std::this_thread::sleep_for(std::chrono::milliseconds(80));
         // Total covers the deadline + OS counters; the callback-gap counter
         // is the one a stale timestamp would trip, so check it too.
         const AudioEngine::XrunCounts xrunAfterCycle = AudioEngine::Instance().Xruns();
         Check("Stop/Start cycle adds no xrun", restarted && xrunAfterCycle.Total() == 0);
         Check("Stop/Start cycle adds no callback gap", restarted && xrunAfterCycle.gaps == 0);

         // Leave the engine Off and the fixture graph gone, matching the
         // "audio starts off" contract the rest of this file's tests rely on.
         AudioEngine::Instance().Stop();
         RemoveNodeByIndex(outIndex);
         RemoveNodeByIndex(wtIndex);
         RebuildAudioTopology();

         printf("%s\n", overallOk ? "AUDIO LIFECYCLE TEST OK" : "AUDIO LIFECYCLE TEST FAIL");
      }
      else if (getenv("INFINITE_AUDIORECOVERYTEST") != nullptr)
      {
         // Mechanisable half of
         // docs/plans/optimization/prompts/02-device-change-and-wake-recovery.md's
         // exit criteria - real device unplug/sleep-wake still needs the
         // manual pass documented in STATUS.md, but PollAudioRecovery's
         // restart/rate-limit/give-up logic doesn't depend on which
         // notification set the flag, only on Platform::
         // AudioDeviceDebugSimulateConfigChange() setting the same one a
         // real AVAudioEngineConfigurationChangeNotification would.
         bool overallOk = true;
         auto Check = [&](const char* name, bool ok)
         {
            printf("  [%s] %s\n", ok ? "pass" : "FAIL", name);
            overallOk = overallOk && ok;
         };

         if (AudioEngine::Instance().SampleRate() > 0.0)
            AudioEngine::Instance().Stop();
         ResetAudioRecoveryState();

         GraphNode* wtGn = SpawnNode("Wavetable", "Synths", 40.0f, 40.0f);
         const int wtIndex = wtGn->index;
         GraphNode* outGn = SpawnNode("Audio Out", "Utility", 320.0f, 40.0f);
         const int outIndex = outGn->index;
         wtGn = FindNodeByIndex(wtIndex);
         outGn = FindNodeByIndex(outIndex);
         auto* wt = static_cast<WavetableNode*>(wtGn->node.get());
         auto* out = static_cast<AudioOutputNode*>(outGn->node.get());
         out->input.Connect(wt);
         RebuildAudioTopology();

         std::string startError;
         Check("AudioEngine::Start succeeds", StartAudioEngine(startError));
         const double runningRate = AudioEngine::Instance().SampleRate();
         Check("node's mailbox prepared at the running rate",
               runningRate > 0.0 && wt->DebugMailboxSampleRate() == runningRate);
         std::this_thread::sleep_for(std::chrono::milliseconds(80)); // let a real callback land

         // --- idempotency: a burst of flags in one instant collapses to one attempt ---
         Platform::AudioDeviceDebugSimulateConfigChange();
         PollAudioRecovery();
         const int attemptsAfterFirst = gAudioRecoveryAttemptsInWindow;
         Check("first config-change flag triggers exactly one attempt", attemptsAfterFirst == 1);

         Platform::AudioDeviceDebugSimulateConfigChange();
         PollAudioRecovery(); // fires within kAudioRecoveryMinIntervalMs of the first - must be swallowed
         Check("a second flag inside the min interval adds no attempt",
               gAudioRecoveryAttemptsInWindow == attemptsAfterFirst);

         std::this_thread::sleep_for(std::chrono::milliseconds(80));
         const double rateAfterRecovery = AudioEngine::Instance().SampleRate();
         Check("engine still running after a recovered restart", rateAfterRecovery > 0.0);
         Check("node's mailbox re-prepared at the (re-)negotiated rate after recovery",
               rateAfterRecovery > 0.0 && wt->DebugMailboxSampleRate() == rateAfterRecovery);

         // --- rate-limited give-up: N attempts spaced past the min interval, then stop trying ---
         ResetAudioRecoveryState();
         gAudioStartError.clear();
         for (int i = 0; i < kAudioRecoveryMaxAttemptsPerWindow; i++)
         {
            std::this_thread::sleep_for(std::chrono::milliseconds((long)kAudioRecoveryMinIntervalMs + 100));
            Platform::AudioDeviceDebugSimulateConfigChange();
            PollAudioRecovery();
         }
         Check("exactly kAudioRecoveryMaxAttemptsPerWindow attempts consumed",
               gAudioRecoveryAttemptsInWindow == kAudioRecoveryMaxAttemptsPerWindow);
         Check("engine still alive after the window's normal attempts", AudioEngine::Instance().SampleRate() > 0.0);

         std::this_thread::sleep_for(std::chrono::milliseconds((long)kAudioRecoveryMinIntervalMs + 100));
         Platform::AudioDeviceDebugSimulateConfigChange();
         PollAudioRecovery(); // the (N+1)th - past the window's cap, must give up rather than retry
         Check("gives up rather than retrying past the window cap",
               AudioEngine::Instance().SampleRate() == 0.0 && gAudioStartError.find("gave up") != std::string::npos);
         Check("honest UI: audioEngineOn reads false once recovery gives up",
               !(AudioEngine::Instance().SampleRate() > 0.0));

         // Leave the engine Off and the fixture graph gone, matching every
         // other audio test's contract.
         ResetAudioRecoveryState();
         gAudioStartError.clear();
         if (AudioEngine::Instance().SampleRate() > 0.0)
            AudioEngine::Instance().Stop();
         RemoveNodeByIndex(outIndex);
         RemoveNodeByIndex(wtIndex);
         RebuildAudioTopology();

         printf("%s\n", overallOk ? "AUDIO RECOVERY TEST OK" : "AUDIO RECOVERY TEST FAIL");
      }
      else if (getenv("INFINITE_MATFRAMETEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);     // 0
         SpawnNode("Material", "3D", 320.0f, 40.0f);    // 1
         SpawnNode("Render 3D", "3D", 620.0f, 40.0f);   // 2
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         geo->posX = 4.0f; geo->posY = 2.0f; geo->uniformScale = 2.0f;
         mat->input = geo;
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] = mat;
      }
      else if (getenv("INFINITE_ENVTEST") != nullptr)
      {
         // A tiny synthetic equirectangular HDR, one flat bright warm colour
         // across the whole sphere of directions - deterministic regardless
         // of which way the test camera happens to be looking - written to a
         // real .hdr file so this exercises the actual stb_image decode path
         // rather than poking EnvironmentNode's private texture upload
         // directly.
         const int ew = 16, eh = 8;
         std::vector<float> envPixels((size_t)ew * eh * 3);
         for (int i = 0; i < ew * eh; i++)
         {
            envPixels[i * 3 + 0] = 8.0f;
            envPixels[i * 3 + 1] = 5.0f;
            envPixels[i * 3 + 2] = 2.0f;
         }
         // One blown-out "sun" texel, far above half-float range, exactly like
         // the sun in a real HDRI. It survives the float32 file fine but turns
         // into +Inf when converted into the 16F texture unless Upload clamps
         // it - and glGenerateMipmap then averages that Inf up into every
         // higher mip as NaN, which is what used to render the lit geometry
         // solid black (the diffuse term always samples the topmost mip).
         envPixels[0] = envPixels[1] = envPixels[2] = 1.0e30f;
         const std::string envPath = TmpPath("infinite_envtest.hdr");
         stbi_write_hdr(envPath.c_str(), ew, eh, 3, envPixels.data());

         SpawnNode("Geometry", "3D", 40.0f, 40.0f);     // 0 - the reflective sphere
         SpawnNode("Material", "3D", 320.0f, 40.0f);    // 1
         SpawnNode("HDRI", "3D", 40.0f, 400.0f);        // 2
         SpawnNode("Render 3D", "3D", 620.0f, 40.0f);   // 3
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* mat = static_cast<MaterialNode*>(gNodes[1].node.get());
         auto* env = static_cast<EnvironmentNode*>(gNodes[2].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[3].node.get());
         geo->shape = 2; // sphere
         mat->input = geo;
         mat->metallic = 1.0f;
         mat->roughness = 0.03f; // near-mirror, so mip 0 dominates the reflection
         env->Load(envPath);
         render->geometry[0] = mat;
         render->envInput.Connect(env);
         render->width = 400.0f; render->height = 400.0f;
         render->samples = 0;
      }
      else if (getenv("INFINITE_PATHOCEANTEST") != nullptr)
      {
         SpawnNode("Path", "Modulators", 40.0f, 40.0f);   // 0
         SpawnNode("Ocean", "3D", 40.0f, 400.0f);         // 1
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);     // 2
         static_cast<Render3DNode*>(gNodes[2].node.get())->geometry[0] =
            static_cast<OceanNode*>(gNodes[1].node.get());
      }
      else if (getenv("INFINITE_UTILTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Null 3D", "3D", 300.0f, 40.0f);        // 1
         SpawnNode("Mesh to Points", "3D", 560.0f, 40.0f); // 2
         SpawnNode("Render 3D", "3D", 820.0f, 40.0f);      // 3
         SpawnNode("Shape", "Source", 40.0f, 500.0f);      // 4
         SpawnNode("Null", "Compositing", 300.0f, 500.0f); // 5
         SpawnNode("Output", "Utility", 560.0f, 500.0f);    // 6

         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* null3d = static_cast<Null3DNode*>(gNodes[1].node.get());
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[2].node.get());
         null3d->input = geo;
         m2p->input = null3d;
         static_cast<Render3DNode*>(gNodes[3].node.get())->geometry[0] = m2p;
         CableFor(gNodes[5], 0)->Connect(gNodes[4].node.get());
         CableFor(gNodes[6], 0)->Connect(gNodes[5].node.get());
      }
      else if (getenv("INFINITE_PALETTETEST") != nullptr)
      {
         // A Ramp makes a deterministic, strongly coloured reference without
         // needing an image file on disk, so the check runs anywhere.
         SpawnNode("Ramp", "Source", 40.0f, 40.0f);       // 0 reference
         SpawnNode("Palette", "Modulators", 340.0f, 40.0f); // 1
         SpawnNode("Ramp", "Source", 640.0f, 40.0f);      // 2 target, driven by 1
         SpawnNode("Palette", "Modulators", 40.0f, 560.0f);  // 3 same input, same seed

         auto* reference = static_cast<RampNode*>(gNodes[0].node.get());
         reference->stopCount = 3;
         reference->stopPos[0] = 0.0f;
         reference->stopPos[1] = 0.5f;
         reference->stopPos[2] = 1.0f;
         const float bands[3][3] = {
            { 0.90f, 0.10f, 0.10f }, { 0.10f, 0.85f, 0.15f }, { 0.15f, 0.20f, 0.95f }
         };
         for (int i = 0; i < 3; i++)
            for (int c = 0; c < 3; c++)
               reference->stopColor[i][c] = bands[i][c];

         for (int n : { 1, 3 })
         {
            auto* palette = static_cast<PaletteNode*>(gNodes[n].node.get());
            palette->swatchCount = 3;
            CableFor(gNodes[n], 0)->Connect(gNodes[0].node.get());
         }

         // Params have to be drawn for colour pins to register, which is what
         // the binding pass walks - and a Ramp only draws as many swatches as
         // it has stops.
         static_cast<RampNode*>(gNodes[2].node.get())->stopCount = 3;
         gNodes[2].showParams = true;
         gNodes[1].showParams = true;
      }
      else if (getenv("INFINITE_TEXT3DTEST") != nullptr)
      {
         SpawnNode("Text 3D", "3D", 40.0f, 40.0f);
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);
         static_cast<Render3DNode*>(gNodes[1].node.get())->geometry[0] =
            static_cast<Text3DNode*>(gNodes[0].node.get());
      }
      // Nothing is pre-spawned in slash mode: the point of that check is that
      // pressing "/" on an empty canvas is what produces the comment.
      else if (getenv("INFINITE_COMMENTTEST") != nullptr &&
               std::string(getenv("INFINITE_COMMENTTEST")) != "slash")
      {
         GraphNode* gn = SpawnNode("Comment", "Compositing", 60.0f, 60.0f);
         auto* c = static_cast<CommentNode*>(gn->node.get());
         // Middle line deliberately too long for the box, so the check covers
         // wrapping and clipping and not just three short lines.
         c->text = "Phase G\nfloating cubes instanced on the drift field, "
                   "then graded\nTODO: add fog";
         c->width = 260.0f; c->height = 140.0f;
         c->color[0] = 0.95f; c->color[1] = 0.85f; c->color[2] = 0.45f;
         gn->showParams = true; // exercise DrawCommentParams too, not just the preview
      }
      else if (getenv("INFINITE_GROUPTEST") != nullptr)
      {
         // Three nodes in a row for the group auto-fit check driven below.
         SpawnNode("Shape", "Source", 100.0f, 100.0f);  // 0
         SpawnNode("Shape", "Source", 400.0f, 100.0f);  // 1
         SpawnNode("Shape", "Source", 700.0f, 100.0f);  // 2
      }
      else if (const char* samplePath = getenv("INFINITE_BUILDSAMPLE"))
      {
         // A demo patch: a source cube continuously reshaped by Resynthesize
         // 3D, instanced onto a slow-drifting particle field so many
         // independent "floating cubes" share one animated source shape, then
         // graded through a three-stage 2D chain before Output. The grade used
         // to be five nodes: hsl, colorbalance and brightnesscontrast were
         // folded into the single "color adjustments" filter and no longer
         // exist as spawnable types, so spawning them by name silently added
         // nothing and every gNodes[] index from there on read past the end of
         // the vector. One graded node now does all three stages.
         SpawnNode("Cube", "3D", 40.0f, 40.0f);                    // 0 source shape
         SpawnNode("Resynthesize 3D", "3D", 320.0f, 40.0f);        // 1 continuous morph
         SpawnNode("Particle System", "3D", 40.0f, 420.0f);        // 2 drift field
         SpawnNode("Instance on Points", "3D", 320.0f, 420.0f);    // 3 stamp shape at each particle
         SpawnNode("Camera", "3D", 40.0f, 760.0f);                 // 4
         SpawnNode("Light", "3D", 40.0f, 920.0f);                  // 5 key
         SpawnNode("Light", "3D", 40.0f, 1080.0f);                 // 6 fill/rim
         SpawnNode("Render 3D", "3D", 620.0f, 420.0f);             // 7
         SpawnNode("color adjustments", "Compositing", 900.0f, 420.0f); // 8 the whole primaries grade
         SpawnNode("bloom", "Effects", 900.0f, 700.0f);            // 9
         SpawnNode("vignette", "Effects", 900.0f, 840.0f);         // 10
         SpawnNode("Output", "Utility", 1180.0f, 420.0f);          // 11

         auto* cube = static_cast<GeometryNode*>(gNodes[0].node.get());
         cube->detail = 32;

         auto* resynth = static_cast<MeshResynthNode*>(gNodes[1].node.get());
         resynth->input = cube;
         resynth->weight[MeshResynthNode::kDisplace] = 0.55f;
         resynth->weight[MeshResynthNode::kJitter] = 0.10f;
         resynth->weight[MeshResynthNode::kSmooth] = 0.40f;
         resynth->weight[MeshResynthNode::kTwist] = 0.30f;
         resynth->weight[MeshResynthNode::kBulge] = 0.40f;
         resynth->weight[MeshResynthNode::kExtrudeFaces] = 0.05f;
         resynth->weight[MeshResynthNode::kSubdivide] = 0.05f;
         resynth->weight[MeshResynthNode::kSquash] = 0.15f;
         resynth->chaos = 0.4f;
         resynth->autoStep = true;
         resynth->stepsPerBeat = 0.5f;
         resynth->seed = 17.0f;
         resynth->triangleBudget = 40000;

         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         particles->maxParticles = 10;
         particles->emitRate = 4.0f;
         particles->emitShape = ParticleSystemNode::kSphere;
         particles->emitRadius = 1.6f;
         particles->lifetime = 60.0f;
         particles->lifetimeRandom = 8.0f;
         particles->initialSpeed = 0.15f;
         particles->speedRandom = 0.10f;
         particles->spread = 1.0f; // omnidirectional - "floating", not "launched"
         particles->gravityX = 0.0f; particles->gravityY = -0.02f; particles->gravityZ = 0.0f;
         particles->drag = 0.15f;
         particles->turbulence = 0.35f;
         particles->turbulenceScale = 0.8f;
         particles->startSize = 1.0f;
         particles->endSize = 1.0f; // constant - reads as floating cubes, not dying particles
         particles->startColor[0] = 1.0f; particles->startColor[1] = 0.55f; particles->startColor[2] = 0.25f;
         particles->endColor[0] = 0.25f; particles->endColor[1] = 0.55f; particles->endColor[2] = 1.0f;
         particles->seed = 4.0f;

         auto* inst = static_cast<InstanceOnPointsNode*>(gNodes[3].node.get());
         inst->instanceShape = resynth;
         inst->cloudSource = particles;
         inst->instanceScale = 0.32f;
         inst->scaleRandom = 0.5f;
         inst->rotationRandom = 1.0f;
         inst->alignToNormal = true; // orients each cube along its drift direction
         inst->metallic = 0.35f;
         inst->roughness = 0.35f;
         inst->emissionColor[0] = 1.0f; inst->emissionColor[1] = 0.85f; inst->emissionColor[2] = 0.6f;
         inst->emission = 0.02f; // let lighting and bloom's own threshold define the highlights,
                                  // not a flat self-glow blowing out the whole surface
         inst->seed = 9.0f;

         auto* cam = static_cast<CameraNode*>(gNodes[4].node.get());
         cam->distance = 3.4f; cam->azimuth = 40.107f; cam->elevation = 20.0535f;
         cam->fov = 46.0f; cam->orbitPerBeat = 0.05f; // slow cinematic turntable, no keyframing needed

         auto* keyLight = static_cast<LightNode*>(gNodes[5].node.get());
         keyLight->azimuth = 51.5662f; keyLight->elevation = 57.2958f;
         keyLight->color[0] = 1.0f; keyLight->color[1] = 0.92f; keyLight->color[2] = 0.8f;
         keyLight->intensity = 1.4f; keyLight->orbitPerBeat = 0.02f;

         auto* fillLight = static_cast<LightNode*>(gNodes[6].node.get());
         fillLight->azimuth = -126.0507f; fillLight->elevation = 28.6479f;
         fillLight->color[0] = 0.55f; fillLight->color[1] = 0.7f; fillLight->color[2] = 1.0f;
         fillLight->intensity = 0.6f; fillLight->orbitPerBeat = -0.015f; // drifts the opposite way for parallax

         auto* render = static_cast<Render3DNode*>(gNodes[7].node.get());
         render->geometry[0] = inst;
         render->camera = cam;
         render->lights[0] = keyLight;
         render->lights[1] = fillLight;
         render->width = 1280.0f; render->height = 720.0f;
         render->samples = 2; render->tonemap = 1; render->exposure = 1.1f;
         // A dark teal, not pure black: the warm cube light/emission needs a cool
         // backdrop to read as graded contrast rather than glowing blobs on a void.
         render->bgColor[0] = 0.03f; render->bgColor[1] = 0.045f; render->bgColor[2] = 0.07f;
         render->envSky[0] = 0.14f; render->envSky[1] = 0.20f; render->envSky[2] = 0.32f;
         render->envHorizon[0] = 0.07f; render->envHorizon[1] = 0.09f; render->envHorizon[2] = 0.13f;
         render->envGround[0] = 0.02f; render->envGround[1] = 0.025f; render->envGround[2] = 0.035f;
         render->envIntensity = 0.9f;
         render->ambientColor[0] = 0.10f; render->ambientColor[1] = 0.16f; render->ambientColor[2] = 0.26f;
         render->rimIntensity = 0.5f;
         render->shadowsEnabled = true;
         render->shadowQuality = 1;
         render->shadowStrength = 0.5f;

         // Param indices follow the "color adjustments" def's flat order in
         // src/core/FilterDefs.cpp: 0 Brightness, 1 Contrast, 2 Black Point,
         // 3 White Point, 4 Gamma, 5 Cyan-Red, 6 Magenta-Green, 7 Yellow-Blue,
         // 8 Hue Shift, 9 Saturation, 10 Lightness - the section headers are
         // labels on the first param of each section, not params of their own.
         auto* grade = static_cast<FilterNode*>(gNodes[8].node.get());
         grade->Input().Connect(render);
         grade->SetParamValue(0, 0, 0.02f);  // Brightness
         grade->SetParamValue(1, 0, 0.15f);  // Contrast
         grade->SetParamValue(5, 0, 0.05f);  // Cyan-Red: push warm
         grade->SetParamValue(7, 0, -0.05f); // Yellow-Blue: push toward yellow
         grade->SetParamValue(9, 0, 1.5f);   // Saturation

         auto* bloom = static_cast<FilterNode*>(gNodes[9].node.get());
         bloom->Input().Connect(grade);
         bloom->SetParamValue(0, 0, 0.72f); // Threshold - only genuine highlights bloom
         bloom->SetParamValue(1, 0, 0.9f);  // Intensity
         bloom->SetParamValue(2, 0, 4.0f);  // Radius

         auto* vignette = static_cast<FilterNode*>(gNodes[10].node.get());
         vignette->Input().Connect(bloom);

         auto* out = static_cast<OutputNode*>(gNodes[11].node.get());
         out->Input().Connect(vignette);

         for (GraphNode& gn : gNodes)
            gn.showParams = false;

         // Fast-forward the transport so particles have already spread out and
         // the cube has already morphed a few generations before the first
         // frame anyone sees, rather than opening on an empty, unshaped scene.
         Transport::Instance().SetPlaying(true);
         Transport::Instance().Rewind();
         for (int i = 0; i < 240; i++)
         {
            out->CookIfNeeded(9600 + i);
            Transport::Instance().Tick(1.0f / 30.0f);
         }

         std::string error;
         if (!SavePatchTo(samplePath))
            fprintf(stderr, "sample patch: failed to write %s\n", samplePath);
         else
         {
            Patch::NoteRecent(samplePath);
            printf("sample patch written: %s\n", samplePath);
         }

         if (const char* pngPath = getenv("INFINITE_BUILDSAMPLE_PNG"))
         {
            ExportPng(out, pngPath);
            printf("sample preview written: %s\n", pngPath);
         }
      }
      else if (getenv("INFINITE_MODELTEST") != nullptr)
      {
         SpawnNode("Model 3D", "3D", 40.0f, 40.0f);
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);
         auto* model = static_cast<ModelSourceNode*>(gNodes[0].node.get());
         static_cast<Render3DNode*>(gNodes[1].node.get())->geometry[0] = model;
      }
      else if (getenv("INFINITE_MESHOPTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);
         SpawnNode("Render 3D", "3D", 700.0f, 40.0f);
      }
      else if (getenv("INFINITE_3DTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);
         SpawnNode("Geometry", "3D", 40.0f, 500.0f);
         SpawnNode("Render 3D", "3D", 360.0f, 40.0f);
         // A textured surface, so the mipmap/anisotropy path is exercised too.
         SpawnNode("Noise", "Source", 40.0f, 900.0f);
         auto* g0 = static_cast<GeometryNode*>(gNodes[0].node.get());
         g0->shape = 7; g0->color[0] = 1.0f; g0->color[1] = 0.45f; g0->color[2] = 0.2f;
         g0->uniformScale = 1.5f;
         auto* g1 = static_cast<GeometryNode*>(gNodes[1].node.get());
         g1->shape = 3; g1->posX = 0.9f; g1->posY = -0.35f; g1->uniformScale = 0.7f;
         g1->color[0] = 0.35f; g1->color[1] = 0.7f; g1->color[2] = 1.0f;
         auto* r = static_cast<Render3DNode*>(gNodes[2].node.get());
         r->geometry[0] = g0;
         r->geometry[1] = g1;
         g0->TextureInput().Connect(gNodes[3].node.get());
         r->width = 700.0f; r->height = 700.0f;
         r->samples = 0; // the antialias check below turns it on at frame 4
         if (getenv("INFINITE_NOCULL") != nullptr)
            r->backfaceCull = false;
         if (getenv("INFINITE_NODEPTH") != nullptr)
            r->depthTest = false;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[0].showParams = false;
         gNodes[1].showParams = false;
      }
      else if (getenv("INFINITE_PHASE1TEST") != nullptr)
      {
         // Points-render-as-points, phase 1: a point cloud reaches Render 3D
         // as points and draws as camera-facing sprites.
         //
         // Slot 0: Cube -> Mesh to Points -> Render 3D. A source with both a
         // mesh and a point cloud; the cloud must win the draw.
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);              // 0: cube
         SpawnNode("Mesh to Points", "3D", 300.0f, 40.0f);       // 1
         // Slot 1: Particle System -> Render 3D directly, a pure cloud with an
         // empty mesh - the render must not freeze on its first frame.
         SpawnNode("Particle System", "3D", 40.0f, 300.0f);      // 2
         // Slot 2: Image to Points -> Render 3D - still shows image colours
         // per point once drawn as sprites rather than swatch quads.
         SpawnNode("Noise", "Source", 40.0f, 560.0f);            // 3
         SpawnNode("Image to Points", "3D", 300.0f, 560.0f);     // 4
         SpawnNode("Render 3D", "3D", 560.0f, 300.0f);           // 5

         auto* cube = static_cast<GeometryNode*>(gNodes[0].node.get());
         cube->shape = 1; // Cube
         auto* m2p = static_cast<MeshToPointsNode*>(gNodes[1].node.get());
         m2p->input = cube;
         m2p->pointSize = 0.3f; // large enough to be visible at the default camera distance
         auto* particles = static_cast<ParticleSystemNode*>(gNodes[2].node.get());
         particles->emitRate = 400.0f;
         particles->startSize = 0.5f; particles->endSize = 0.5f;
         auto* i2p = static_cast<ImageToPointsNode*>(gNodes[4].node.get());
         i2p->Input().Connect(gNodes[3].node.get());
         i2p->pointSize = 0.3f;
         auto* r = static_cast<Render3DNode*>(gNodes[5].node.get());
         r->geometry[0] = m2p;
         r->geometry[1] = particles;
         r->geometry[2] = i2p;
         r->width = 400.0f; r->height = 400.0f;
         r->samples = 0;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (getenv("INFINITE_DEPTHTEST") != nullptr)
      {
         SpawnNode("Noise", "Source", 40.0f, 40.0f);                  // 0: depth image source
         SpawnNode("Ramp", "Source", 40.0f, 300.0f);                  // 1: color image source
         SpawnNode("Depth Projection", "3D", 300.0f, 40.0f);          // 2: depth projection
         SpawnNode("Render 3D", "3D", 560.0f, 40.0f);                 // 3: render 3D

         auto* depthMap = static_cast<NoiseNode*>(gNodes[0].node.get());
         depthMap->octaves = 3;
         auto* colorMap = static_cast<RampNode*>(gNodes[1].node.get());
         (void)colorMap;

         auto* dp = static_cast<DepthProjectionNode*>(gNodes[2].node.get());
         dp->DepthInput().Connect(gNodes[0].node.get());
         dp->ColorInput().Connect(gNodes[1].node.get());
         dp->density = 64;
         dp->projection = DepthProjectionNode::kPerspective;
         dp->outputType = DepthProjectionNode::kPoints;

         auto* r = static_cast<Render3DNode*>(gNodes[3].node.get());
         r->geometry[0] = dp;
         r->width = 400.0f;
         r->height = 400.0f;
         r->renderPass = 0; // beauty
      }
      else if (getenv("INFINITE_TEXTFIT") != nullptr)
      {
         SpawnNode("Text", "Source", 40.0f, 40.0f);
         auto* t = static_cast<TextNode*>(gNodes[0].node.get());
         t->text = "naman is a weirdo and this line is deliberately long enough to need several rows";
         t->fontName = "Verdana";
         t->fontSize = 300.0f;   // absurd on purpose: fitting must rein it in
         t->wordWrap = true;
         t->fitToBox = true;
         t->align = 3;
         t->scaleX = 1.4f;
         t->scaleY = 2.2f;
         gNodes[0].showParams = true;
         printf("TEXTFIT fixture: %zu nodes spawned\n", gNodes.size());
      }
      else if (getenv("INFINITE_SHOWCASE4") != nullptr)
      {
         SpawnNode("Reaction Diffusion", "Compositing", 40.0f, 40.0f);
         SpawnNode("Curves", "Compositing", 300.0f, 40.0f);
         SpawnNode("Shape", "Source", 560.0f, 40.0f);
         SpawnNode("Trails", "Compositing", 820.0f, 40.0f);
         SpawnNode("Resynthesize", "Effects", 1080.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[0].node.get());
         auto* cv = static_cast<CurvesNode*>(gNodes[1].node.get());
         cv->AddPoint(CurvesNode::kRGB, 0.35f, 0.75f);
         cv->AddPoint(CurvesNode::kRGB, 0.7f, 0.2f);
         auto* tr = static_cast<TrailsNode*>(gNodes[3].node.get());
         tr->zoom = 1.02f; tr->rotate = 0.5730f; tr->decay = 0.96f;
         auto* rd = static_cast<ReactionDiffusionNode*>(gNodes[0].node.get());
         rd->ApplyPreset(0);
         rd->stepsPerFrame = 24.0f;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
      }
      else if (getenv("INFINITE_AUDIOSPIKE") != nullptr)
      {
         // Same heavy visual load as INFINITE_SHOWCASE4 (Reaction Diffusion
         // at stepsPerFrame=24 feeding Curves/Shape/Trails) so the FPS-delta
         // measurement below has real GPU work to lose, not a synthetic one.
         SpawnNode("Reaction Diffusion", "Compositing", 40.0f, 40.0f);
         SpawnNode("Curves", "Compositing", 300.0f, 40.0f);
         SpawnNode("Shape", "Source", 560.0f, 40.0f);
         SpawnNode("Trails", "Compositing", 820.0f, 40.0f);
         SpawnNode("Resynthesize", "Effects", 1080.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[0].node.get());
         auto* cv = static_cast<CurvesNode*>(gNodes[1].node.get());
         cv->AddPoint(CurvesNode::kRGB, 0.35f, 0.75f);
         cv->AddPoint(CurvesNode::kRGB, 0.7f, 0.2f);
         auto* tr = static_cast<TrailsNode*>(gNodes[3].node.get());
         tr->zoom = 1.02f; tr->rotate = 0.5730f; tr->decay = 0.96f;
         auto* rd = static_cast<ReactionDiffusionNode*>(gNodes[0].node.get());
         rd->ApplyPreset(0);
         rd->stepsPerFrame = 24.0f;
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
      }
      else if (getenv("INFINITE_SHOWCASE3") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);
         SpawnNode("kaleidoscope", "Effects", 300.0f, 40.0f);
         SpawnNode("Macro Knob", "Modulators", 560.0f, 40.0f);
         SpawnNode("Macro XY", "Modulators", 820.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         gNodes[1].showParams = true;
         gNodes[2].showParams = true;
         gNodes[3].showParams = true;
         static_cast<MacroXYNode*>(gNodes[3].node.get())->padX = 0.32f;
         static_cast<MacroXYNode*>(gNodes[3].node.get())->padY = 0.68f;
      }
      else if (getenv("INFINITE_SHOWCASE2") != nullptr)
      {
         SpawnNode("Noise", "Source", 40.0f, 40.0f);
         SpawnNode("kaleidoscope", "Effects", 300.0f, 40.0f);
         SpawnNode("bloom", "Effects", 560.0f, 40.0f);
         SpawnNode("Switcher", "Compositing", 820.0f, 40.0f);
         SpawnNode("Pattern", "Modulators", 1080.0f, 40.0f);
         SpawnNode("Math", "Modulators", 1340.0f, 40.0f);
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[2], 0)->Connect(gNodes[1].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[2].node.get());
         CableFor(gNodes[3], 1)->Connect(gNodes[0].node.get());
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (getenv("INFINITE_SHOWCASE") != nullptr)
      {
         // dev-only: a representative patch, used to generate the README image
         SpawnNode("Shape", "Source", 40.0f, 40.0f);
         SpawnNode("glitch", "Effects", 320.0f, 40.0f);
         SpawnNode("Text", "Source", 600.0f, 40.0f);
         SpawnNode("Layer Stack", "Compositing", 880.0f, 40.0f);
         SpawnNode("Output", "Utility", 1160.0f, 40.0f);
         SpawnNode("LFO", "Modulators", 320.0f, 560.0f);

         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         shape->shapeType = 6;
         shape->sides = 7;
         shape->size = 0.36f;
         shape->fillColor[0] = 1.0f; shape->fillColor[1] = 0.42f; shape->fillColor[2] = 0.2f;
         auto* txt = static_cast<TextNode*>(gNodes[2].node.get());
         txt->text = "INFINITE";
         txt->fontSize = 150.0f;

         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         CableFor(gNodes[3], 0)->Connect(gNodes[1].node.get());
         CableFor(gNodes[3], 1)->Connect(gNodes[2].node.get());
         CableFor(gNodes[4], 0)->Connect(gNodes[3].node.get());

         for (GraphNode& gn : gNodes)
            gn.showParams = true;
         gNodes[2].showParams = false;
         gNodes[4].showParams = false;
      }
      else if (const char* geomDensityArg = getenv("INFINITE_GEOMDENSITYTEST"))
      {
         // 3D geometry density stress fixture: a UV sphere's triangle count is
         // exactly rings*sectors*2 (Mesh.cpp's Sphere(rings, sectors)), so
         // pinning sectors (sides*2 = 500) and driving rings (detail) from the
         // requested triangle target gives a clean tris = detail*1000 dial.
         // Fixed at 1920x1080 with AA/shadows off so the measurement (below,
         // near INFINITE_FPSTEST) isolates geometry cost alone - see
         // INFINITE_RESSWEEPTEST / INFINITE_QUALITYSWEEPTEST for those axes.
         SpawnNode("Sphere", "3D", 40.0f, 40.0f);      // 0
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);  // 1
         auto* sphere = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         const long triTarget = std::max(1L, atol(geomDensityArg));
         sphere->sides = 250;                                   // sectors = 500, fixed
         sphere->detail = (int)std::max(3L, triTarget / 1000L);  // rings; tris = rings*1000
         render->geometry[0] = sphere;
         render->width = 1920.0f; render->height = 1080.0f;
         render->samples = 0;         // AA off
         render->shadowsEnabled = false;
      }
      else if (const char* resSweepArg = getenv("INFINITE_RESSWEEPTEST"))
      {
         // Output-resolution stress fixture: fixed, moderate geometry (200
         // rings * 500 sectors = 200,000 triangles - the same rings*sectors*2
         // math as GEOMDENSITYTEST) so the measurement isolates resolution's
         // effect on frame time from geometry cost. Resolution is "WxH",
         // e.g. INFINITE_RESSWEEPTEST=3840x2160; defaults to 1920x1080 if
         // unparseable.
         SpawnNode("Sphere", "3D", 40.0f, 40.0f);      // 0
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);  // 1
         auto* sphere = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         sphere->sides = 250; sphere->detail = 200; // 200,000 tris
         render->geometry[0] = sphere;
         int resW = 1920, resH = 1080;
         sscanf(resSweepArg, "%dx%d", &resW, &resH);
         render->width = (float)std::max(16, resW);
         render->height = (float)std::max(16, resH);
         render->samples = 0;
         render->shadowsEnabled = false;
      }
      else if (const char* qualityArg = getenv("INFINITE_QUALITYSWEEPTEST"))
      {
         // Render quality-knob stress fixture. Render3DNode's only two
         // runtime-adjustable real-time quality controls are the MSAA sample
         // count (Render3DNode::SampleNames(): Off/2x/4x/8x, an index into
         // that list) and the shadow map size (ShadowQualityNames():
         // 1024/2048/4096) - both swept from here. Fixed at 1920x1080 / the
         // same 200,000-triangle sphere as RESSWEEPTEST so the measurement
         // isolates the quality knob's own cost. INFINITE_QUALITYSWEEPTEST is
         // the MSAA sample index (0-3); optional INFINITE_QUALITY_SHADOWQ
         // (0-2) also enables shadows at that map size.
         SpawnNode("Sphere", "3D", 40.0f, 40.0f);      // 0
         SpawnNode("Render 3D", "3D", 400.0f, 40.0f);  // 1
         auto* sphere = static_cast<GeometryNode*>(gNodes[0].node.get());
         auto* render = static_cast<Render3DNode*>(gNodes[1].node.get());
         sphere->sides = 250; sphere->detail = 200; // 200,000 tris
         render->geometry[0] = sphere;
         render->width = 1920.0f; render->height = 1080.0f;
         render->samples = std::max(0, std::min(3, atoi(qualityArg)));
         if (const char* shadowQArg = getenv("INFINITE_QUALITY_SHADOWQ"))
         {
            render->shadowsEnabled = true;
            render->shadowQuality = std::max(0, std::min(2, atoi(shadowQArg)));
         }
         else
         {
            render->shadowsEnabled = false;
         }
      }
      else if (getenv("INFINITE_NODECHAINPERFTEST") != nullptr)
      {
         // Node-chain cook-recursion stress fixture: Noise -> N x invert ->
         // Output, wired directly via SpawnNode/CableFor->Connect (bypassing
         // UI paste and FindFreeSpawnPosition's O(N) scan) so the
         // measurement below isolates FilterNode::CookIfNeeded's per-frame
         // recursive cook-walk cost from node-construction cost. Chain
         // length from INFINITE_PERFCHAINLEN, default 800 - see the
         // node-chain FPS investigation for why 800 (linear) vs. 1600
         // (hangs) are the interesting comparison points.
         const char* chainLenArg = getenv("INFINITE_PERFCHAINLEN");
         const long chainLen = std::max(1L, atol(chainLenArg ? chainLenArg : "800"));
         SpawnNode("Noise", "Source", 0.0f, 0.0f);
         if (const char* resArg = getenv("INFINITE_PERFCHAINRES"))
         {
            auto* noise = static_cast<NoiseNode*>(gNodes[0].node.get());
            noise->width = noise->height = (float)std::max(4L, atol(resArg));
         }
         for (long i = 0; i < chainLen; i++)
            SpawnNode("invert", "Compositing", 0.0f, 0.0f);
         SpawnNode("Output", "Utility", 0.0f, 0.0f);
         for (size_t i = 1; i < gNodes.size(); i++)
            CableFor(gNodes[i], 0)->Connect(gNodes[i - 1].node.get());
      }
      else if (const char* mixedArg = getenv("INFINITE_MIXEDSTRESSTEST"))
      {
         // Realistic-shape stress fixture, follow-up to the node-chain FPS
         // investigation: that investigation's straight 1600-node chain
         // isn't a shape any real patch takes (max ~100-150 nodes, heavily
         // interconnected with modulation) - this fixture instead builds a
         // fixed, moderately deep 3D geometry tree (16 primitives -> 4 join
         // nodes -> 1 root join -> Render 3D; triangle detail scales with
         // `scale`), a 12-voice audio rack (Oscillator -> N x Audio Filter
         // -> Mixer -> Delay -> Audio Out; per-voice filter count scales
         // with `scale`), and - at frameId==2 below, once params exist to
         // bind to - a modulation-heavy top layer (LFO count scales with
         // `scale`). Env: INFINITE_MIXEDSTRESSTEST=<scale>, default 1.0.
         const double scale = std::max(0.1, atof(mixedArg[0] != '\0' ? mixedArg : "1.0"));
         const int triDetail = std::max(3, (int)std::lround(scale * 40.0));
         const int filtersPerVoice = std::max(1, (int)std::lround(scale * 2.0));

         std::vector<GeometryNode*> leaves;
         for (int i = 0; i < 16; i++)
         {
            GraphNode* gn = SpawnNode((i % 2 == 0) ? "Sphere" : "Cube", "3D", 0.0f, 0.0f);
            auto* geo = static_cast<GeometryNode*>(gn->node.get());
            geo->sides = triDetail;
            geo->detail = triDetail;
            leaves.push_back(geo);
         }
         std::vector<JoinGeometryNode*> l1Joins;
         for (int j = 0; j < 4; j++)
         {
            GraphNode* gn = SpawnNode("Join Geometry", "3D", 0.0f, 0.0f);
            auto* join = static_cast<JoinGeometryNode*>(gn->node.get());
            for (int k = 0; k < 4; k++)
               join->inputs[k] = leaves[j * 4 + k];
            l1Joins.push_back(join);
         }
         GraphNode* rootJoinGn = SpawnNode("Join Geometry", "3D", 0.0f, 0.0f);
         auto* rootJoin = static_cast<JoinGeometryNode*>(rootJoinGn->node.get());
         for (int k = 0; k < 4; k++)
            rootJoin->inputs[k] = l1Joins[k];
         GraphNode* renderGn = SpawnNode("Render 3D", "3D", 0.0f, 0.0f);
         auto* render = static_cast<Render3DNode*>(renderGn->node.get());
         render->geometry[0] = rootJoin;
         render->width = 1280.0f;
         render->height = 720.0f;

         GraphNode* mixerGn = SpawnNode("Mixer", "Utility", 0.0f, 0.0f);
         auto* mixer = static_cast<MixerNode*>(mixerGn->node.get());
         mixer->numChannels = 12;
         for (int v = 0; v < 12; v++)
         {
            GraphNode* prev = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f);
            for (int f = 0; f < filtersPerVoice; f++)
            {
               GraphNode* filt = SpawnNode("Audio Filter", "AudioEffects", 0.0f, 0.0f);
               if (AudioCable* in = filt->node->AudioInputSlot(0))
                  in->Connect(prev->node.get());
               prev = filt;
            }
            if (AudioCable* in = mixer->AudioInputSlot(v))
               in->Connect(prev->node.get());
         }
         GraphNode* delayGn = SpawnNode("Delay", "AudioEffects", 0.0f, 0.0f);
         if (AudioCable* in = delayGn->node->AudioInputSlot(0))
            in->Connect(mixerGn->node.get());
         GraphNode* audioOutGn = SpawnNode("Audio Out", "Utility", 0.0f, 0.0f);
         if (AudioCable* in = audioOutGn->node->AudioInputSlot(0))
            in->Connect(delayGn->node.get());
         RebuildAudioTopology();

         // Params start collapsed (GraphNode::showParams) - force them open
         // so every spawned node's VisitParams actually runs during the
         // UI's per-frame draw pass and registers with Modulation's
         // FrameParams(), which the frameId==2 binder below depends on.
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (const char* bench5Arg = getenv("INFINITE_BENCH_B5NODES"))
      {
         // B5(b) node-count scaling fixture (docs/plans/perf/benchmark-suite.md
         // §4). Spawns a mixed set of representative node types laid out on a
         // grid - not stacked at the origin like INFINITE_MIXEDSTRESSTEST,
         // which is a known flaw of that fixture (benchmark-suite.md §2) -
         // and leaves them unconnected: the thing this isolates is per-node,
         // per-frame UI/bookkeeping overhead (header title building via
         // GetNodeInstanceIndex, param drawing) as node COUNT grows, not
         // cook-graph cost. INFINITE_BENCH_B5NODES=<n>.
         const long n = std::max(1L, atol(bench5Arg));
         static const char* kTypes[] = { "Shape", "Noise", "invert", "gaussianblur", "LFO" };
         static const char* kCats[]  = { "Source", "Source", "Compositing", "Effects", "Modulators" };
         const int cols = 12;
         for (long i = 0; i < n; i++)
         {
            const int t = (int)(i % 5);
            const float x = (float)(i % cols) * 260.0f;
            const float y = (float)(i / cols) * 200.0f;
            SpawnNode(kTypes[t], kCats[t], x, y);
         }
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (const char* bench5cArg = (getenv("INFINITE_BENCH_B5STAGES") ? getenv("INFINITE_BENCH_B5STAGES") : getenv("INFINITE_BENCH_B5C")))
      {
         // B5(c) per-stage CPU timing fixture (docs/plans/perf/benchmark-suite.md §4).
         // Spawns a mixed set of nodes on a grid (same shape as B5(b)) and
         // records per-stage CPU times across the sampled window.
         long n = atol(bench5cArg);
         if (n <= 1) n = 100;
         static const char* kTypes[] = { "Shape", "Noise", "invert", "gaussianblur", "LFO" };
         static const char* kCats[]  = { "Source", "Source", "Compositing", "Effects", "Modulators" };
         const int cols = 12;
         for (long i = 0; i < n; i++)
         {
            const int t = (int)(i % 5);
            const float x = (float)(i % cols) * 260.0f;
            const float y = (float)(i / cols) * 200.0f;
            SpawnNode(kTypes[t], kCats[t], x, y);
         }
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (const char* bench5fArg = getenv("INFINITE_BENCH_B5LOADSAVE"))
      {
         // B5(f) patch load/save time fixture (benchmark-suite.md §4). Same
         // mixed-node grid as B5(b)/B5(c) so the three are directly
         // comparable by node count. INFINITE_BENCH_B5LOADSAVE=<n>.
         long n = atol(bench5fArg);
         if (n <= 1) n = 100;
         static const char* kTypes[] = { "Shape", "Noise", "invert", "gaussianblur", "LFO" };
         static const char* kCats[]  = { "Source", "Source", "Compositing", "Effects", "Modulators" };
         const int cols = 12;
         for (long i = 0; i < n; i++)
         {
            const int t = (int)(i % 5);
            const float x = (float)(i % cols) * 260.0f;
            const float y = (float)(i / cols) * 200.0f;
            SpawnNode(kTypes[t], kCats[t], x, y);
         }
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (const char* bench5gArg = getenv("INFINITE_BENCH_B5UNDO"))
      {
         // B5(g) undo-snapshot time fixture (benchmark-suite.md §4). Same
         // mixed-node grid as B5(b)/B5(c)/B5(f). INFINITE_BENCH_B5UNDO=<n>.
         long n = atol(bench5gArg);
         if (n <= 1) n = 100;
         static const char* kTypes[] = { "Shape", "Noise", "invert", "gaussianblur", "LFO" };
         static const char* kCats[]  = { "Source", "Source", "Compositing", "Effects", "Modulators" };
         const int cols = 12;
         for (long i = 0; i < n; i++)
         {
            const int t = (int)(i % 5);
            const float x = (float)(i % cols) * 260.0f;
            const float y = (float)(i / cols) * 200.0f;
            SpawnNode(kTypes[t], kCats[t], x, y);
         }
         for (GraphNode& gn : gNodes)
            gn.showParams = true;
      }
      else if (const char* bench4Arg = getenv("INFINITE_BENCH_B4SCALE"))
      {
         // B4 Complex 3D fixture (docs/plans/perf/benchmark-suite.md §4).
         std::string scaleStr = bench4Arg;
         std::string shadowStr = getenv("INFINITE_BENCH_B4SHADOW") ? getenv("INFINITE_BENCH_B4SHADOW") : "2048";
         const char* animEnv = getenv("INFINITE_BENCH_B4ANIM");
         const bool isAnim = !(animEnv && (strcmp(animEnv, "0") == 0 || strcmp(animEnv, "static") == 0));
         const bool passSplit = getenv("INFINITE_BENCH_B4PASSES") != nullptr;
         Bench::Render3DPassSplit() = passSplit;

         sBenchB2Variant = "scale=" + scaleStr + ",shadow=" + shadowStr + ",anim=" + (isAnim ? "1" : "0");
         if (passSplit)
            sBenchB2Variant += ",passes=1";
         if (const char* t = getenv("INFINITE_BENCH_GPUTIMERS"); t && strcmp(t, "0") == 0)
            sBenchB2Variant += ",gputimers=0";

         BuildBenchB4Scene(scaleStr, shadowStr, isAnim, sBenchB2Render3DIdx, sBenchB2OutputIdx, sBenchB4CamIdx, sBenchB4LfoIdx);
      }
      else if (getenv("INFINITE_BENCH_B2") != nullptr ||
               getenv("INFINITE_BENCH_B2VISUALS") != nullptr ||
               getenv("INFINITE_BENCH_B2SCALE") != nullptr)
      {
         // B2 Heavy visuals fixture (docs/plans/perf/benchmark-suite.md §4).
         std::string scaleStr = "s";
         const char* bench2Arg = getenv("INFINITE_BENCH_B2SCALE");
         if (!bench2Arg) bench2Arg = getenv("INFINITE_BENCH_B2VISUALS");
         if (!bench2Arg) bench2Arg = getenv("INFINITE_BENCH_B2");
         if (bench2Arg) scaleStr = bench2Arg;

         bool isAnim = true;
         if (const char* animEnv = getenv("INFINITE_BENCH_B2ANIM"))
         {
            if (strcmp(animEnv, "0") == 0 || strcmp(animEnv, "false") == 0 || strcmp(animEnv, "static") == 0)
               isAnim = false;
         }

         sBenchB2Variant = std::string("scale=") + scaleStr + ",anim=" + (isAnim ? "1" : "0");
         if (getenv("INFINITE_BENCH_B2GPUNODES") != nullptr)
            sBenchB2Variant += ",gpunodes=1";
         if (const char* t = getenv("INFINITE_BENCH_GPUTIMERS"); t && strcmp(t, "0") == 0)
            sBenchB2Variant += ",gputimers=0";

         BuildBenchB2Scene(scaleStr, isAnim, sBenchB2Render3DIdx, sBenchB2OutputIdx);
      }
      else if (getenv("INFINITE_BENCH_B9SCENE") != nullptr ||
               getenv("INFINITE_BENCH_B9") != nullptr ||
               getenv("INFINITE_BENCH_B9MEMORY") != nullptr)
      {
         // B9 Memory footprint fixture (docs/plans/perf/benchmark-suite.md §4).
         // Builds B2 or B4 scene at scale l, animated.
         const char* sceneArg = getenv("INFINITE_BENCH_B9SCENE");
         if (!sceneArg) sceneArg = getenv("INFINITE_BENCH_B9");
         if (!sceneArg) sceneArg = getenv("INFINITE_BENCH_B9MEMORY");

         std::string sceneStr = (sceneArg && (strcmp(sceneArg, "b4") == 0 || strcmp(sceneArg, "B4") == 0)) ? "b4" : "b2";
         sBenchB9Scene = sceneStr;
         sBenchB9Variant = "scene=" + sceneStr + ",scale=l,anim=1";

         if (sceneStr == "b4")
         {
            BuildBenchB4Scene("l", "2048", true, sBenchB2Render3DIdx, sBenchB2OutputIdx, sBenchB4CamIdx, sBenchB4LfoIdx);
         }
         else
         {
            BuildBenchB2Scene("l", true, sBenchB2Render3DIdx, sBenchB2OutputIdx);
         }

         sBenchB9RssStartMb = sMainRssStartMb;
         sBenchB9RssBuiltMb = Bench::ProcessRssMb();
         sBenchB9FootBuiltMb = Bench::ProcessFootprintMb();
         sBenchB9RssPeakMb = std::max(sBenchB9RssStartMb, sBenchB9RssBuiltMb);
         sBenchB9FootPeakMb = std::max(sMainFootStartMb, sBenchB9FootBuiltMb);
      }
      else if (getenv("INFINITE_BENCH_B3") != nullptr ||
               getenv("INFINITE_BENCH_B3LIVE") != nullptr ||
               getenv("INFINITE_BENCH_B3SCALE") != nullptr ||
               getenv("INFINITE_BENCH_B7") != nullptr ||
               getenv("INFINITE_BENCH_B10") != nullptr)
      {
         // B7 soak and B10 offline-render/A-V-sync both run this same
         // B3-shaped fixture (docs/plans/perf/benchmark-suite.md §4).
         const bool isBenchB10 = getenv("INFINITE_BENCH_B10") != nullptr;
         std::string scaleStr = "s";
         const char* bench3Arg = getenv("INFINITE_BENCH_B3SCALE");
         if (!bench3Arg) bench3Arg = getenv("INFINITE_BENCH_B3LIVE");
         if (!bench3Arg) bench3Arg = getenv("INFINITE_BENCH_B3");
         if (bench3Arg && strlen(bench3Arg) > 0 && strcmp(bench3Arg, "1") != 0)
            scaleStr = bench3Arg;

         const int voices = (scaleStr == "m" || scaleStr == "medium" || scaleStr == "16") ? 16 : 8;
         const int bufFrames = getenv("INFINITE_BENCH_B3BUFFER") ? atoi(getenv("INFINITE_BENCH_B3BUFFER")) : 256;

         const bool b3EnableVisuals = (getenv("INFINITE_BENCH_B3VISUALS") == nullptr || strcmp(getenv("INFINITE_BENCH_B3VISUALS"), "0") != 0);
         // B10 renders offline, not through the live projector/MIDI loop B3's
         // own i2p measurement needs - simulated MIDI injection would just be
         // dead weight (and a spurious Platform::MidiStart) on a batch render.
         const bool b3EnableMidi = !isBenchB10 &&
            (getenv("INFINITE_BENCH_B3MIDI") == nullptr || strcmp(getenv("INFINITE_BENCH_B3MIDI"), "0") != 0);
         const bool b3EnablePred = (getenv("INFINITE_BENCH_B3PRED") == nullptr || strcmp(getenv("INFINITE_BENCH_B3PRED"), "0") != 0);
         const bool b3EnableGesture = (getenv("INFINITE_BENCH_B3GESTURE") == nullptr || strcmp(getenv("INFINITE_BENCH_B3GESTURE"), "0") != 0);
         sBenchB3I2PDryRun = (getenv("INFINITE_BENCH_B3I2PDRYRUN") != nullptr && strcmp(getenv("INFINITE_BENCH_B3I2PDRYRUN"), "0") != 0);

         sBenchB3Variant = std::string("scale=") + scaleStr;
         if (!b3EnableVisuals) sBenchB3Variant += ",visuals=0";
         if (!b3EnableMidi) sBenchB3Variant += ",midi=0";
         if (!b3EnablePred) sBenchB3Variant += ",pred=0";
         if (!b3EnableGesture) sBenchB3Variant += ",gesture=0";
         if (sBenchB3I2PDryRun) sBenchB3Variant += ",i2pdryrun=1";
         if (const char* t = getenv("INFINITE_BENCH_GPUTIMERS"); t && strcmp(t, "0") == 0)
            sBenchB3Variant += ",gputimers=0";

         // 1. Audio: B1-lite
         BuildBenchB1Audio(voices, bufFrames, /*yOffset=*/1200.0f);

         // 2. Visuals: B2-lite (scale s effects chain, animated by default).
         // B10 alone can ask for anim=0 (INFINITE_BENCH_B10ANIM=0), to check
         // output_hash stability the same way B2's anim=0 variant does -
         // B3/B7 keep the animated chain unconditionally, unchanged.
         const bool b3IsAnim = !isBenchB10 ||
            (getenv("INFINITE_BENCH_B10ANIM") == nullptr || strcmp(getenv("INFINITE_BENCH_B10ANIM"), "0") != 0);
         if (b3EnableVisuals)
         {
            BuildBenchB2Scene("s", b3IsAnim, sBenchB3Render3DIdx, sBenchB3OutputIdx,
                              &sBenchB3TwistIdx, &sBenchB3MatIdx, &sBenchB3CamIdx,
                              /*useEmbossForGlitch=*/true);

            // 3. Gesture/macro playback on Material metallic
            if (b3EnableGesture && sBenchB3MatIdx >= 0)
            {
               GestureRecorder::Playback pb;
               pb.samples = {
                  { 0.15f, 0.0, false },
                  { 0.85f, 1.0, false },
                  { 0.15f, 2.0, false }
               };
               pb.speed = 1.0f;
               pb.recordedMin = 0.15f;
               pb.recordedMax = 0.85f;
               GestureRecorder::Instance().SetPlayback(sBenchB3MatIdx, 1, pb);
            }

            // 4. 3 Prediction modulators bound to visual parameters
            if (b3EnablePred)
            {
               const int driftIdx = SpawnNode("Drift", "Prediction", 2600.0f, 600.0f)->index;
               const int movesIdx = SpawnNode("Moves", "Prediction", 2600.0f, 800.0f)->index;
               const int predModIdx = SpawnNode("Predictive Modulator", "Prediction", 2600.0f, 1000.0f)->index;

               if (sBenchB3MatIdx >= 0)
                  Modulation::Instance().Bind(sBenchB3MatIdx, 0, driftIdx, 0); // Material roughness
               if (sBenchB3TwistIdx >= 0)
                  Modulation::Instance().Bind(sBenchB3TwistIdx, 0, movesIdx, 0); // Twist amount
               if (sBenchB3CamIdx >= 0)
                  Modulation::Instance().Bind(sBenchB3CamIdx, 1, predModIdx, 0); // Camera elevation
            }
         }

         // 5. Start MIDI engine for simulated injection
         if (b3EnableMidi)
         {
            if (!Platform::MidiIsRunning())
            {
               std::string midiErr;
               Platform::MidiStart(midiErr);
            }
         }

         sBenchB3RssStartMb = sMainRssStartMb;
         sBenchB3FootStartMb = sMainFootStartMb;
         sBenchB3RssPeakMb = sBenchB3RssStartMb;
         sBenchB3FootPeakMb = sBenchB3FootStartMb;
      }
      else if (getenv("INFINITE_BENCH_B8") != nullptr)
      {
         // B8 Media I/O fixture (docs/plans/perf/benchmark-suite.md §4).
         auto envInt = [](const char* name, int fallback) {
            const char* v = getenv(name);
            return (v != nullptr && *v != '\0') ? std::atoi(v) : fallback;
         };
         auto envOn = [](const char* name) {
            const char* v = getenv(name);
            return v != nullptr && *v != '\0' && strcmp(v, "0") != 0;
         };
         sBenchB8Clips = std::clamp(envInt("INFINITE_BENCH_B8CLIPS", 2), 1, 4);
         sBenchB8Res = envInt("INFINITE_BENCH_B8RES", 1080) >= 2160 ? 2160 : 1080;
         sBenchB8Windows = std::clamp(envInt("INFINITE_BENCH_B8WINDOWS", 0), 0, 3);
         sBenchB8WantCamera = envOn("INFINITE_BENCH_B8CAMERA");
         sBenchB8WantSyphon = envOn("INFINITE_BENCH_B8SYPHON");
         sBenchB8ForceOverlap = envOn("INFINITE_BENCH_B8OVERLAP");
         sBenchB8TotalFrames = std::max(60, envInt("INFINITE_BENCH_B8FRAMES", 600));

         sBenchB8Variant = "clips=" + std::to_string(sBenchB8Clips) + ",res=" + std::to_string(sBenchB8Res) +
                           ",windows=" + std::to_string(sBenchB8Windows) +
                           ",camera=" + (sBenchB8WantCamera ? "1" : "0") +
                           ",syphon=" + (sBenchB8WantSyphon ? "1" : "0");
         // GPU timer queries stall the CPU on macOS (README "Found while
         // measuring"), so B8 times the GPU only when asked for with
         // INFINITE_BENCH_GPUTIMERS=1. The ring reads the env lazily on its
         // first use, which is after this.
         if (envOn("INFINITE_BENCH_GPUTIMERS"))
            sBenchB8Variant += ",gputimers=1";
         else
         {
#if defined(_WIN32)
            _putenv_s("INFINITE_BENCH_GPUTIMERS", "0");
#else
            setenv("INFINITE_BENCH_GPUTIMERS", "0", 1);
#endif
         }

         // Never raise the OS permission dialog: only an already-granted
         // camera is opened (CameraOpen itself asks when NotDetermined).
         bool withCamera = false;
         if (sBenchB8WantCamera)
         {
            switch (Platform::CameraAuthorizationStatus())
            {
               case Platform::CameraAuthorization::Denied: sBenchB8CameraSkipReason = "denied"; break;
               case Platform::CameraAuthorization::Restricted: sBenchB8CameraSkipReason = "restricted"; break;
               case Platform::CameraAuthorization::NotDetermined: sBenchB8CameraSkipReason = "not_determined"; break;
               case Platform::CameraAuthorization::Authorized:
                  if (Platform::CameraListDevices().empty())
                     sBenchB8CameraSkipReason = "no_device";
                  else
                     withCamera = true;
                  break;
            }
         }

         const char* mediaEnv = getenv("INFINITE_BENCH_B8MEDIA");
         const std::string mediaDir = (mediaEnv != nullptr && *mediaEnv != '\0') ? mediaEnv : "bench/media";
         std::vector<std::string> clipPaths;
         for (int i = 0; i < sBenchB8Clips; i++)
            clipPaths.push_back(mediaDir + "/b8_" + std::to_string(sBenchB8Res) + "p30_" + std::to_string(i) + ".mp4");

         // Before any clip opens: handles only grow stats if this is on.
         Bench::MediaIoEnabled().store(true);
         if (!BuildBenchB8Scene(clipPaths, sBenchB8WantSyphon, withCamera, sBenchB8Windows, sBenchB8ClipIdx, sBenchB8OutputIdx,
                                sBenchB8SyphonIdx, sBenchB8CameraIdx, sBenchB8SetupError))
            fprintf(stderr, "[bench B8] setup failed: %s (generate clips with scripts/bench/b8_make_clips.sh)\n",
                    sBenchB8SetupError.c_str());

         sBenchB8RssStartMb = sMainRssStartMb;
         sBenchB8FootStartMb = sMainFootStartMb;
         sBenchB8FootPeakMb = sBenchB8FootStartMb;
      }
      else if (getenv("INFINITE_BENCH_B6") != nullptr ||
               getenv("INFINITE_BENCH_B6NODES") != nullptr ||
               getenv("INFINITE_BENCH_B6MODE") != nullptr ||
               getenv("INFINITE_BENCH_B6COLLAPSED") != nullptr)
      {
         // B6 Canvas navigation fixture (docs/plans/perf/benchmark-suite.md §4).
         int nodeCount = 300;
         if (const char* nEnv = getenv("INFINITE_BENCH_B6NODES"))
            nodeCount = std::max(1, std::atoi(nEnv));
         else if (const char* b6Arg = getenv("INFINITE_BENCH_B6"))
         {
            if (strlen(b6Arg) > 0 && strcmp(b6Arg, "1") != 0 && std::atoi(b6Arg) > 0)
               nodeCount = std::atoi(b6Arg);
         }
         sBenchB6NodeCount = nodeCount;

         sBenchB6TotalFrames = getenv("INFINITE_BENCH_B6FRAMES") ? std::max(60, std::atoi(getenv("INFINITE_BENCH_B6FRAMES"))) : 600;

         const char* modeEnv = getenv("INFINITE_BENCH_B6MODE");
         sBenchB6Mode = (modeEnv && strlen(modeEnv) > 0) ? modeEnv : "all";

         sBenchB6Collapsed = (getenv("INFINITE_BENCH_B6COLLAPSED") != nullptr &&
                              strcmp(getenv("INFINITE_BENCH_B6COLLAPSED"), "0") != 0);

         sBenchB6Vsync = (getenv("INFINITE_BENCH_B6VSYNC") == nullptr ||
                          strcmp(getenv("INFINITE_BENCH_B6VSYNC"), "0") != 0);

         sBenchB6Variant = "n=" + std::to_string(nodeCount) + ",mode=" + sBenchB6Mode;
         if (sBenchB6Collapsed)
            sBenchB6Variant += ",collapsed=1";
         if (!sBenchB6Vsync)
            sBenchB6Variant += ",vsync=0";
         if (const char* t = getenv("INFINITE_BENCH_GPUTIMERS"); t && strcmp(t, "0") == 0)
            sBenchB6Variant += ",gputimers=0";

         BuildBenchB6Scene(nodeCount, sBenchB6Collapsed, sBenchB6GridMaxX, sBenchB6GridMaxY, sBenchB6DragNodeIndex);

         sBenchB6RssStartMb = sMainRssStartMb;
         sBenchB6FootStartMb = sMainFootStartMb;
         sBenchB6FootPeakMb = sBenchB6FootStartMb;
      }
      else if (const char* bench1Arg = getenv("INFINITE_BENCH_B1VOICES"))
      {
         const long numVoices = std::max(1L, std::min(64L, atol(bench1Arg)));
         const int bufFrames = getenv("INFINITE_BENCH_B1BUFFER") ? atoi(getenv("INFINITE_BENCH_B1BUFFER")) : 0;
         BuildBenchB1Audio(numVoices, bufFrames, 0.0f);
      }
      else if (getenv("INFINITE_BENCH_B5EMPTY") != nullptr)
      {
         // B5(a) empty-patch fixture (benchmark-suite.md §4): the floor -
         // frame cost with zero nodes, nothing to cook, nothing to draw.
         // Intentionally spawns nothing; the measurement half below is what
         // does the work.
      }
      else if (getenv("INFINITE_BENCH_B5STARTUP") != nullptr)
      {
         // B5(e) startup-breakdown fixture (benchmark-suite.md §4): measures
         // milestone timings from main entry to first frame completion.
         // Intentionally spawns nothing; the measurement half below is what
         // does the work.
      }
      else if (const char* audioAloneArg = getenv("INFINITE_BENCH_B5AUDIOALONE"))
      {
         // B5(d) audio-thread-alone fixture (benchmark-suite.md §4): the
         // audio callback's own per-block cost in isolation, at one buffer
         // size, with nothing else in the graph competing for the CPU -
         // one Oscillator straight into Audio Out, no effects chain (that's
         // what B1 stresses). INFINITE_BENCH_B5AUDIOALONE=<buffer frames>.
         const int oscIdx = SpawnNode("Oscillator", "Synths", 0.0f, 0.0f)->index;
         const int outIdx = SpawnNode("Audio Out", "Utility", 260.0f, 0.0f)->index;
         // Re-resolve by index rather than treating oscIdx/outIdx as gNodes[]
         // positions - GraphNode::index is a stable id from a monotonically
         // increasing counter, not a vector slot, and diverges from position
         // the moment any earlier node in the session was removed. Same
         // pattern as the Wavetable/Audio Out spawn above (main.cpp:66029).
         GraphNode* oscGn = FindNodeByIndex(oscIdx);
         GraphNode* outGn = FindNodeByIndex(outIdx);
         static_cast<AudioOutputNode*>(outGn->node.get())->input.Connect(oscGn->node.get());
         for (GraphNode& gn : gNodes)
            gn.showParams = true;

         if (AudioEngine::Instance().SampleRate() > 0.0)
            AudioEngine::Instance().Stop();
         AudioEngine::Instance().SetRequestedDevice(0); // system default - see BuildBenchB1Audio
         AudioEngine::Instance().SetRequestedBufferFrames(atoi(audioAloneArg));
         if (!StartAudioEngine(gAudioStartError))
            fprintf(stderr, "BENCH: audio engine did not start: %s\n", gAudioStartError.c_str());
         RebuildAudioTopology();
      }
      else if (const char* loadPatchPath = getenv("INFINITE_LOADPATCH"))
      {
         LoadPatchFrom(loadPatchPath);
         gRequestFitView = true;
      }
      else if (wantsFixture)
      {
         SpawnNode("Shape", "Source", 60.0f, 60.0f);
         // Default shapeType (0, circle) doesn't use the "sides" param, so it
         // never registers - several tests below look it up by name. Use a
         // polygon shape type so "sides" is always present in the fixture.
         static_cast<ShapeNode*>(gNodes[0].node.get())->shapeType = 5;
         if (getenv("INFINITE_RESYNTHTEST") != nullptr)
         {
            SpawnNode("Resynthesize", "Effects", 380.0f, 60.0f);
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
            gNodes[1].showParams = true;
         }
         else
            SpawnNode("Output", "Utility", 380.0f, 60.0f);
         if (getenv("INFINITE_RECTEST") != nullptr || getenv("INFINITE_RECTEARDOWNTEST") != nullptr)
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         if (getenv("INFINITE_HIDETEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
         if (getenv("INFINITE_MODTEST") != nullptr || getenv("INFINITE_OFFLINECLOCKTEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_MACROTEST") != nullptr)
         {
            SpawnNode("Macro XY", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
         if (getenv("INFINITE_MODBOUNDSTEST") != nullptr)
         {
            // clampOutput=false lets this modulator's Value01() genuinely
            // leave [0,1] on demand (see RangeToRangeNode::Value01) - the
            // exact mechanism of the original out-of-range bug, and the
            // simplest way to reproduce it without a real Random node.
            SpawnNode("Range to Range", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_MIDILEARNTEST") != nullptr)
         {
            SpawnNode("Range to Range", "Modulators", 60.0f, 500.0f);
            SpawnNode("MPC", "Synths", 400.0f, 500.0f);
            gNodes[3].showParams = true;
         }
         if (getenv("INFINITE_LOOPERTRIGTEST") != nullptr)
         {
            SpawnNode("Macro Trigger", "Macros", 60.0f, 500.0f);  // trigger A -> a Looper button
            SpawnNode("Macro Trigger", "Macros", 60.0f, 700.0f);  // trigger B -> a plain checkbox
            SpawnNode("Looper", "Synths", 400.0f, 500.0f);
            for (GraphNode& gn : gNodes)
               if (dynamic_cast<LooperNode*>(gn.node.get()) != nullptr)
                  gn.showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_MPCMODTEST") != nullptr)
         {
            SpawnNode("Range to Range", "Modulators", 60.0f, 500.0f); // gNodes[2]
            SpawnNode("MPC", "Synths", 400.0f, 500.0f);               // gNodes[3]
            gNodes[3].showParams = true;
         }
#ifndef NDEBUG
         if (getenv("INFINITE_PREDBINDTEST") != nullptr)
         {
            SpawnNode("Stub Predictor", "Modulators", 60.0f, 500.0f);         // gNodes[2]
            SpawnNode("Mixer", "Utility", 300.0f, 500.0f);                    // gNodes[3]: a tapered knob
            gNodes[0].showParams = true;
            gNodes[3].showParams = true;
         }
#endif
         if (getenv("INFINITE_MODMATRIXGEOM") != nullptr)
         {
            // A bound link is required: DrawModMatrixTable shows "No active
            // modulations" and never calls BeginTable at all when
            // mod.Links() is empty, which would make the geometry probe
            // below trivially (and meaninglessly) pass.
            SpawnNode("Range to Range", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
            gModMatrixOpen = true;
            // Defaults to the right dock (the orientation the scroll bug
            // showed up in); INFINITE_MODMATRIXDOCK overrides it so the
            // other three can be checked too.
            gModMatrixDock = getenv("INFINITE_MODMATRIXDOCK") != nullptr
                                ? atoi(getenv("INFINITE_MODMATRIXDOCK"))
                                : 1;
         }
         if (getenv("INFINITE_GESTUREUNDOTEST") != nullptr)
            gNodes[0].showParams = true; // params must be drawn for them to register
         if (getenv("INFINITE_CULLDRIVENTEST") != nullptr)
         {
            // Both start on screen with params open so frame 1 can resolve
            // them; the test then closes gNodes[0]'s eye and moves gNodes[2]
            // far outside the view, where the off-screen cull skips its body.
            gNodes[0].showParams = true;
            GraphNode* far = SpawnNode("Shape", "Source", 700.0f, 60.0f);
            static_cast<ShapeNode*>(far->node.get())->shapeType = 5;
            far->showParams = true;
         }
         if (getenv("INFINITE_MODMATRIXTEST") != nullptr || getenv("INFINITE_MODCURVETEST") != nullptr)
         {
            // Range to Range, not LFO: a deterministic constantIn (like
            // INFINITE_MODBOUNDSTEST's fixture) so the enable/disable test
            // can force a value change on demand instead of waiting on an
            // LFO's own period.
            SpawnNode("Range to Range", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_OSCTEST") != nullptr)
         {
            // Constant -> OSC Send -> (UDP, loopback) -> OSC Receive, proving a
            // round trip through the actual wire format rather than just
            // exercising the C++ classes in isolation.
            SpawnNode("Constant", "Modulators", 60.0f, 500.0f);   // gNodes[2]
            SpawnNode("OSC Send", "Utility", 300.0f, 500.0f);         // gNodes[3]
            SpawnNode("OSC Receive", "Utility", 540.0f, 500.0f);      // gNodes[4]
            gNodes[2].showParams = true;
            gNodes[3].showParams = true;
            gNodes[4].showParams = true;
            std::string wireErr;
            ConnectNodes(gNodes[2].index, 0, gNodes[3].index, 0, wireErr);
         }
      }
   }

   // Cocoa chdir's a bundled app to Contents/Resources, so a bare relative path
   // would silently write inside the .app. Default somewhere the user can find.
   const std::string desktopDir = AppPaths::DesktopDir();

   char exportPath[512] = "";
   snprintf(exportPath, sizeof(exportPath), "%s/infinite_output.png", desktopDir.c_str());

   char recordPath[512] = "";
   snprintf(recordPath, sizeof(recordPath), "%s/infinite_output.mp4", desktopDir.c_str());

   if (HeadlessJobActive())
   {
      Patch::Data probe;
      std::string readError;
      const bool loadsPatch = gHeadlessJob.mode == Headless::Mode::Render || gHeadlessJob.mode == Headless::Mode::Frame ||
                              gHeadlessJob.mode == Headless::Mode::Frames ||
                              gHeadlessJob.mode == Headless::Mode::Explain || gHeadlessJob.mode == Headless::Mode::AudioSummary;
      Headless::Status st;
      st.mode = gHeadlessJob.mode == Headless::Mode::Render ? "render"
                : gHeadlessJob.mode == Headless::Mode::Explain ? "explain"
                : gHeadlessJob.mode == Headless::Mode::AudioSummary ? "audio-summary"
                : gHeadlessJob.mode == Headless::Mode::Frames ? "frames"
                                                                    : "frame";
      st.patch = gHeadlessJob.patch;
      if (gHeadlessJob.mode == Headless::Mode::Canonicalize)
      {
         st.mode = "canonicalize";
         st.out = gHeadlessJob.out;
         if (!RunCanonicalize(gHeadlessJob, st))
         {
            st.ok = st.errors.empty();
            gHeadlessExitCode = Headless::Emit(gHeadlessJob, st);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
      else if (!loadsPatch)
      {
         // describe / validate do their work in HeadlessTick
      }
      else if (!Patch::Read(gHeadlessJob.patch, probe, readError))
      {
         st.errors.push_back({ "E_LOAD", readError, 0, -1 });
         gHeadlessExitCode = Headless::Emit(gHeadlessJob, st);
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
      else
      {
         // Strict pass before anything is applied: a broken authored file is
         // reported with its line numbers instead of loading half a graph.
         const PatchSchema::Env env = MakeSchemaEnv((gHeadlessJob.mode == Headless::Mode::Render ||
                                                     gHeadlessJob.mode == Headless::Mode::Frame ||
                                                     gHeadlessJob.mode == Headless::Mode::Frames) &&
                                                    gHeadlessJob.node.empty());
         PatchSchema::Resolve(probe, env, st.errors);
         if (st.errors.empty() && !gHeadlessJob.sets.empty())
            ApplyHeadlessSets(probe, &st.errors, &gHeadlessPreWarnings);
         if (!gHeadlessJob.node.empty())
         {
            // --node takes an `id <word>` as well as an index; the render phase wants the index.
            const size_t colon = gHeadlessJob.node.find(':');
            const std::string word = gHeadlessJob.node.substr(0, colon);
            for (const Patch::NodeRecord& n : probe.nodes)
               if (!n.id.empty() && n.id == word)
                  gHeadlessJob.node = std::to_string(n.index) + (colon == std::string::npos ? "" : gHeadlessJob.node.substr(colon));
         }
         if (st.errors.empty())
            PatchSchema::Validate(probe, env, st.errors, gHeadlessPreWarnings);
         // A tapped node needs no Output, and it is the thing being looked at, so it is not "unused".
         if (!gHeadlessJob.node.empty())
         {
            const int tapped = (int)std::strtol(gHeadlessJob.node.c_str(), nullptr, 10);
            gHeadlessPreWarnings.erase(std::remove_if(gHeadlessPreWarnings.begin(), gHeadlessPreWarnings.end(),
                                                      [tapped](const Headless::Issue& w)
                                                      { return w.code == "W_NO_OUTPUT" || (w.code == "W_UNUSED_NODE" && w.node == tapped); }),
                                       gHeadlessPreWarnings.end());
         }
         // An audio summary needs no picture Output; its own check (E_NO_AUDIO) covers the audio side.
         if (gHeadlessJob.mode == Headless::Mode::AudioSummary)
            gHeadlessPreWarnings.erase(std::remove_if(gHeadlessPreWarnings.begin(), gHeadlessPreWarnings.end(),
                                                      [](const Headless::Issue& w) { return w.code == "W_NO_OUTPUT"; }),
                                       gHeadlessPreWarnings.end());
         gHeadlessPatch = probe;
         if (!gHeadlessJob.lenient)
         {
            // Strict: a hard error stops the load, so everything is reported now;
            // otherwise the promoted warnings wait for the Warm phase (which adds
            // E_BAD_PARAM) and the graph is loaded only to be checked.
            if (!st.errors.empty())
               Headless::PromoteWarnings(gHeadlessPreWarnings, st.errors);
            else
               Headless::PromoteWarnings(gHeadlessPreWarnings, gHeadlessPromoted);
         }
         if (!st.errors.empty())
         {
            st.warnings = gHeadlessPreWarnings;
            gHeadlessExitCode = Headless::Emit(gHeadlessJob, st);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
         else if (probe.hasKeyRefs || gHeadlessJob.mode == Headless::Mode::Explain)
            gHeadlessNeedProbe = true; // HeadlessTick probes the types, resolves the keys, then loads
         else
            LoadPatchFrom(gHeadlessJob.patch);
      }
   }
   else if (argc > 1 && argv[1] != nullptr && argv[1][0] != '-')
   {
      const std::string argPath = argv[1];
      if (HasExtension(argPath, std::vector<std::string> { "inf", "infinite" }))
      {
         LoadPatchFrom(argPath);
         gRequestFitView = true;
      }
   }
   // Any launch-time Finder open is drained by the in-frame loop below, right
   // after the first glfwPollEvents() call.

   char searchBuf[128] = "";
   bool searchJustOpened = false;
   bool searchPopupCentered = false;
   // Shift+N is a toggle, so the keyboard block (which runs before the popup is
   // drawn) needs to know whether the picker is already up. ImGui::IsPopupOpen
   // can't be trusted from there - the other OpenPopup("search") call sites sit
   // inside ed::BeginCreate()/Suspend() scopes with their own ID stacks - so the
   // popup reports its own visibility here instead, one frame behind, and the
   // close is deferred to inside the popup where CloseCurrentPopup() is legal.
   bool searchPopupOpen = false;
   bool searchRequestClose = false;
   std::vector<std::string> clipboard;      // typeNames copied
   std::vector<INode*> clipboardSources;    // live sources to copy params from
   std::vector<int> clipboardOrigIndex;     // gNodes index each item had at copy time
   std::vector<int> clipboardOrigGroup;     // that item's owning group's index, or -1
   // Connections landing on the copied cluster, captured at Cmd+C time since
   // the graph can change before Cmd+V runs (see ApplyClusterLinks).
   ClusterClipboard clipboardCluster;
   const double tSettingsInit = Bench::ScopedStageTimer::NowMs();
   double sFirstFrameEndMs = 0.0;
   int frameId = 0;

   // Launcher screen over the first seconds (core/SplashScreen.h). Never in headless/test runs.
   // INFINITE_SPLASHTEST=<seconds> forces it on under the screenshot harness, starting that far in.
   const char* splashTest = getenv("INFINITE_SPLASHTEST");
   // Launcher card on every normal start (core/SplashScreen.h). INFINITE_NOSPLASH=1 suppresses it.
   const bool splashEnabled = splashTest != nullptr || (!gHeadlessTestWindow && !IsHeadlessProcess() && getenv("INFINITE_NOSPLASH") == nullptr);
   if (splashEnabled)
      Splash::Begin(splashTest ? (float)atof(splashTest) : 0.0f);

   while (!glfwWindowShouldClose(window))
   {
      static Bench::PercentileRing sStageModulation;
      static Bench::PercentileRing sStageCook;
      static Bench::PercentileRing sStageNodeBodies;
      static Bench::PercentileRing sStageLinks;
      static Bench::PercentileRing sStageCookAll;
      static Bench::PercentileRing sStageEditorEnd;
      static Bench::PercentileRing sStageImGuiRender;
      static Bench::PercentileRing sStageProjectors;
      static Bench::PercentileRing sStageSwap;
      static Bench::GpuTimerRing sGpuTimerRing;
      static Bench::PercentileRing sBenchB5cFrameMs;
      static double sBenchB5cRssStartMb = -1.0;
      static Bench::PercentileRing sBenchB2FrameMs;
      static double sBenchB2RssStartMb = -1.0;
      static unsigned long long sBenchB2FboAtF32 = 0; // FboAllocationCount at the start of the sample window

      const bool isBenchB5c = (getenv("INFINITE_BENCH_B5STAGES") != nullptr || getenv("INFINITE_BENCH_B5C") != nullptr);
      const bool isBenchB2 = (getenv("INFINITE_BENCH_B2") != nullptr || getenv("INFINITE_BENCH_B2VISUALS") != nullptr || getenv("INFINITE_BENCH_B2SCALE") != nullptr);
      const bool isBenchB7 = getenv("INFINITE_BENCH_B7") != nullptr;
      const bool isBenchB3 = (getenv("INFINITE_BENCH_B3") != nullptr || getenv("INFINITE_BENCH_B3LIVE") != nullptr || getenv("INFINITE_BENCH_B3SCALE") != nullptr || isBenchB7);
      const bool isBenchB4 = getenv("INFINITE_BENCH_B4SCALE") != nullptr;
      const bool isBenchB6 = (getenv("INFINITE_BENCH_B6") != nullptr || getenv("INFINITE_BENCH_B6NODES") != nullptr || getenv("INFINITE_BENCH_B6MODE") != nullptr || getenv("INFINITE_BENCH_B6COLLAPSED") != nullptr);
      const bool isBenchB9 = (getenv("INFINITE_BENCH_B9SCENE") != nullptr || getenv("INFINITE_BENCH_B9") != nullptr || getenv("INFINITE_BENCH_B9MEMORY") != nullptr);
      const bool isBenchB8 = getenv("INFINITE_BENCH_B8") != nullptr;
      // B8 samples the same span as B6 and uses B6's stage split (links and
      // cook_all get their own stages).
      const bool benchB6Stages = isBenchB6 || isBenchB8;
      sBenchB8Sampling = isBenchB8 && frameId >= 32 && frameId < sBenchB8TotalFrames;
      const bool benchStagesSample = ((isBenchB5c || isBenchB2 || isBenchB4) && (frameId >= 32 && frameId < 152)) ||
                                     (isBenchB6 && (frameId >= 32 && frameId < sBenchB6TotalFrames)) ||
                                     sBenchB8Sampling;
      const bool benchStagesCpuSample = ((isBenchB5c || isBenchB2 || isBenchB4 || isBenchB9 || isBenchB3) && (frameId >= 32 && frameId < 152)) ||
                                        (isBenchB6 && (frameId >= 32 && frameId < sBenchB6TotalFrames)) ||
                                        sBenchB8Sampling;
      // B2 per-node GPU split: time each Render 3D / filter draw by node type
      // instead of the enclosing "cook" stage (GL timer queries cannot nest).
      // B8 always splits: the clip and camera uploads run inside Output's
      // pull, i.e. inside the cook stage, so "media_upload"/"camera_upload"
      // can only be timed with the cook-stage query off.
      const bool benchGpuPerNode = (isBenchB2 && getenv("INFINITE_BENCH_B2GPUNODES") != nullptr) ||
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
      gDrawnColorPins.clear();
      gParamRightClickConsumedThisFrame = false;
      RefreshParamDriverFlags();

      // ---------------- node editor ----------------
      const ImGuiViewport* vp = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(vp->WorkPos);
      ImGui::SetNextWindowSize(vp->WorkSize);
      // NoScrollbar/NoScrollWithMouse: this window is a fixed full-screen
      // shell (also NoResize/NoMove) whose every region is meant to be
      // divided exactly among the menu bar, canvas and docked panels, never
      // scrolled as a whole. Without this flag, being even a few pixels over
      // budget in that division - e.g. the item spacing ImGui inserts between
      // a top/bottom-docked viewport panel and the canvas row below/above it,
      // easy to undercount by hand - grows a scrollbar on THIS window, which
      // reads as "a slider that moves the entire app" rather than as a
      // rounding error in one panel's layout.
      // Zero padding: this shell has no content of its own, only the canvas
      // and the docked panels, and each of those paints its own background
      // and carries its own inner padding. The default 8px WindowPadding just
      // put an 8px band of shell background around and between them - the same
      // "bar between the viewports" the ItemSpacing gaps were producing, at
      // the window edges and under the menu bar.
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::Begin("Infinite", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PopStyleVar();

      if (ImGui::BeginMenuBar())
      {
         if (ImGui::BeginMenu("File"))
         {
            if (ImGui::MenuItem("New", MODKEY "+N"))
               GuardUnsavedChanges([]() { NewPatch(); });
            if (ImGui::MenuItem("Open...", MODKEY "+O"))
            {
               const std::string path = Platform::OpenPatchDialog();
               if (!path.empty())
                  GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
            }

            if (ImGui::BeginMenu("Open Recent", !Patch::Recents().empty()))
            {
               // Copied before iterating: opening one calls NoteRecent, which
               // reorders the very list being walked.
               const std::vector<std::string> recents = Patch::Recents();
               for (const std::string& entry : recents)
               {
                  const size_t slash = entry.find_last_of('/');
                  const std::string name =
                     (slash == std::string::npos) ? entry : entry.substr(slash + 1);
                  if (ImGui::MenuItem(name.c_str()))
                     GuardUnsavedChanges([entry]() { LoadPatchFrom(entry); });
               }
               ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Save", MODKEY "+S"))
               SavePatchInteractive(false);
            if (ImGui::MenuItem("Save As...", MODKEY "+Shift+S"))
               SavePatchInteractive(true);

            if (!gPatchPath.empty() || !gPatchStatus.empty())
            {
               ImGui::Separator();
               if (!gPatchPath.empty())
               {
                  const size_t slash = gPatchPath.find_last_of('/');
                  ImGui::TextDisabled("%s", (slash == std::string::npos)
                                               ? gPatchPath.c_str()
                                               : gPatchPath.c_str() + slash + 1);
               }
               if (!gPatchStatus.empty())
                  ImGui::TextDisabled("%s", gPatchStatus.c_str());
            }
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("Edit"))
         {
            if (ImGui::MenuItem("Undo", MODKEY "+Z", false, !gUndoStack.empty()))
               Undo();
            if (ImGui::MenuItem("Redo", MODKEY "+Shift+Z", false, !gRedoStack.empty()))
               Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Cut / Copy", MODKEY "+C"))
               gRequestCopy = true;
            if (ImGui::MenuItem("Paste", MODKEY "+V"))
               gRequestPaste = true;
            if (ImGui::MenuItem("Duplicate", MODKEY "+D"))
               gRequestDuplicate = true;
            if (ImGui::MenuItem("Delete", "Backspace"))
               gRequestDelete = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Shift+A"))
               gRequestSelectAll = true;
            if (ImGui::MenuItem("Bypass selection", "B"))
               gRequestBypass = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Group selection", MODKEY "+G"))
               gRequestGroup = true;
            if (ImGui::MenuItem("Ungroup", MODKEY "+U"))
               gRequestUngroup = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Add Node...", "Shift+N"))
               gRequestAddNode = true;
            if (ImGui::MenuItem("Add Note", "/"))
               gRequestAddComment = true;
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("Menu"))
         {
            if (ImGui::MenuItem("Settings...", MODKEY "+0"))
               gSettingsOpen = true;

            ImGui::Separator();

            if (ImGui::BeginMenu("Viewport panel"))
            {
               ImGui::Checkbox("Show viewport panel", &gViewportPanelOpen);
               if (gViewportPanelOpen)
               {
                  ImGui::SetNextItemWidth(150);
                  ViewportPanelDockCombo();
                  ImGui::SetNextItemWidth(150);
                  if (gViewportPanelDock == 1 || gViewportPanelDock == 2)
                     ImGui::SliderFloat("Width", &gViewportPanelWidth,
                                        kViewportPanelMinWidth, 900.0f, "%.0f px");
                  else
                     ImGui::SliderFloat("Height", &gViewportPanelHeight,
                                        kViewportPanelMinHeight, 800.0f, "%.0f px");
                  ImGui::Separator();
                  if (!gViewportPanelNodes.empty() && ImGui::MenuItem("Clear cards"))
                     gViewportPanelNodes.clear();
                  if (ImGui::MenuItem("Close viewport panel"))
                     gViewportPanelOpen = false;
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Modulation matrix"))
            {
               // Plain themed widgets, same as "Viewport panel" above - this
               // is menu chrome, not a node body, so it takes the app's own
               // checkbox/slider colours (ApplyTheme) rather than the P10
               // dark-contrast-budget style meant for controls inside a node.
               // The two styles side by side in one menu (one purple/clean,
               // one flat blue) is what read as inconsistent.
               ImGui::Checkbox("Show modulation matrix", &gModMatrixOpen);
               if (gModMatrixOpen)
               {
                  ImGui::SetNextItemWidth(150);
                  ModMatrixDockCombo();
                  ImGui::SetNextItemWidth(150);
                  if (gModMatrixDock == 1 || gModMatrixDock == 2)
                     ImGui::SliderFloat("Width", &gModMatrixWidth,
                                        kModMatrixMinWidth, 900.0f, "%.0f px");
                  else
                     ImGui::SliderFloat("Height", &gModMatrixHeight,
                                        kModMatrixMinHeight, 800.0f, "%.0f px");
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Performance Matrix"))
            {
               // Same reasoning as "Modulation matrix" above: plain themed
               // widgets, not the node-body P10 style.
               ImGui::Checkbox("Show Performance Matrix", &gPerfPanelOpen);
               if (gPerfPanelOpen)
               {
                  ImGui::SetNextItemWidth(150);
                  PerfPanelDockCombo();
                  ImGui::SetNextItemWidth(150);
                  if (gPerfPanelDock == 1 || gPerfPanelDock == 2)
                     ImGui::SliderFloat("Width", &gPerfPanelWidth,
                                        kPerfPanelMinWidth, 900.0f, "%.0f px");
                  else
                     ImGui::SliderFloat("Height", &gPerfPanelHeight,
                                        kPerfPanelMinHeight, 800.0f, "%.0f px");
                  ImGui::Checkbox("Edit Mode", &gPerfEditMode);
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Arrangement Timeline"))
            {
               ImGui::Checkbox("Show Arrangement Timeline", &gArrangePanelOpen);
               if (gArrangePanelOpen)
               {
                  // Bottom or top only - a timeline reads left-to-right, so a
                  // side dock would fight the ruler's own horizontal axis.
                  // Saved with the document (Settings.dockSide); not undoable.
                  int dockSide = gArrange.settings.dockSide == 1 ? 1 : 0;
                  ImGui::SetNextItemWidth(150);
                  if (ImGui::Combo("Dock", &dockSide, "Bottom\0Top\0") && dockSide != gArrange.settings.dockSide)
                  {
                     gArrange.settings.dockSide = dockSide;
                     gArrange.revision++; // a model field like any other (WP5b)
                     gPatchDirty = true;
                  }
                  ImGui::SetNextItemWidth(150);
                  ImGui::SliderFloat("Height", &gArrangePanelHeight,
                                     kArrangePanelMinHeight, 800.0f, "%.0f px");
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Nodes"))
            {
               if (ImGui::MenuItem("Show all params"))
               {
                  for (GraphNode& gn : gNodes)
                     gn.showParams = true;
               }
               if (ImGui::MenuItem("Hide all params"))
               {
                  for (GraphNode& gn : gNodes)
                     gn.showParams = false;
               }
               ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("All shortcuts..."))
               gShortcutsOpen = true;
            if (ImGui::MenuItem("Help / module reference"))
               gHelpOpen = true;
#ifndef NDEBUG
            // ImGui's built-in inspectors, not a custom tool: the Debugger's
            // Tools > Item Picker names the exact ImGuiCol_*/style var and
            // rect behind whatever you click, and the Style Editor lists and
            // live-previews every one of those values - the fastest way to
            // hand back "this exact knob, this exact number" instead of a
            // screenshot and a guess. Dev-only: excluded from Release builds
            // (NDEBUG) so shipped/public builds never expose these.
            if (ImGui::MenuItem("UI Debugger / Item Picker"))
               gUiDebuggerOpen = true;
            if (ImGui::MenuItem("UI Style Editor"))
               gUiStyleEditorOpen = true;
#endif
            if (ImGui::MenuItem("Check for updates"))
            {
               UpdateCheck::Start();
               gShowUpdateCheckModal = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
               RequestClose(window);
            ImGui::EndMenu();
         }
         ImGui::Separator();

         Transport& transport = Transport::Instance();
         const bool isTransportPlaying = transport.IsPlaying();

         // Top bar controls styling: clean, symmetrical, unboxed with pixel-perfect alignment.
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.0f, 1.0f, 1.0f, 0.16f));
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.16f));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

         auto TopBarSameLine = [](float spacing = 4.0f) {
            ImGui::SameLine(0.0f, spacing);
         };
         auto TopBarLabel = [](const char* text, bool disabled = false) {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            window->DC.CurrLineTextBaseOffset = ImGui::GetStyle().FramePadding.y;
            if (disabled)
               ImGui::TextDisabled("%s", text);
            else
               ImGui::TextUnformatted(text);
         };
         const bool isLight = IsThemeLight();

         // 1. Transport (Play, Rewind, Audio On/Off)
         if (isTransportPlaying)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.63f, 0.31f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.70f, 0.36f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.55f, 0.26f, 1.0f));
         }
         if (ImGui::Button("##transportplay", ImVec2(34, 0)))
            transport.TogglePlay();
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.72f;
            const ImU32 col = isTransportPlaying
                                  ? IM_COL32(255, 255, 255, 255)
                                  : ImGui::GetColorU32(ImGuiCol_Text);
            if (isTransportPlaying)
               Tabler::DrawPlayerPause(dl, center, iconSize, col);
            else
               Tabler::DrawPlayerPlay(dl, center, iconSize, col, true);
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s (Space)", isTransportPlaying ? "Pause" : "Play");
         if (isTransportPlaying)
            ImGui::PopStyleColor(3);

         TopBarSameLine(2.0f);
         if (ImGui::Button("##transportrewind", ImVec2(34, 0)))
            transport.Rewind();
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.72f;
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            Tabler::DrawPlayerRewind(dl, center, iconSize, col);
         }
         if (ImGui::IsItemHovered())
            HelpTip("Rewind (Return)");

         TopBarSameLine(4.0f);

         // Audio engine power, nothing else: Start starts the device, Stop
         // stops it, and neither touches gAudioMode (which driver the engine
         // plays - the canvas or the Arrangement Timeline - is the panel's
         // "Enable Timeline Audio" toggle). While the timeline drives, a
         // "Timeline" badge sits next to the button so an engine that is on
         // but ignoring the canvas never looks broken.
         {
            const bool engineOn = AudioEngine::Instance().SampleRate() > 0.0;
            const bool audioOn = engineOn;
            const bool audioIsLight = isLight;
            ImGui::PushStyleColor(ImGuiCol_Button, audioOn
                                                       ? (audioIsLight ? ImVec4(0.20f, 0.62f, 0.34f, 1.0f) : ImVec4(0.16f, 0.52f, 0.28f, 1.0f))
                                                       : (audioIsLight ? ImVec4(0.80f, 0.82f, 0.87f, 1.0f) : ImVec4(0.30f, 0.30f, 0.34f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_Text, audioOn
                                                     ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                                     : (audioIsLight ? ImVec4(0.12f, 0.14f, 0.20f, 1.0f) : ImVec4(0.92f, 0.94f, 0.98f, 1.0f)));
            if (ImGui::Button(audioOn ? "Stop Audio" : "Start Audio"))
            {
               if (audioOn)
               {
                  AudioEngine::Instance().Stop();
               }
               else
               {
                  gAudioStartError.clear();
                  if (!StartAudioEngine(gAudioStartError))
                     fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
               }
            }
            ImGui::PopStyleColor(2);
            if (!audioOn && !gAudioStartError.empty() && ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", gAudioStartError.c_str());

            if (gAudioMode == AudioMode::Timeline)
            {
               TopBarSameLine(4.0f);
               const char* badge = "Timeline";
               const ImVec2 textSize = ImGui::CalcTextSize(badge);
               const ImVec2 pad(6.0f, ImGui::GetStyle().FramePadding.y);
               const ImVec2 bmin = ImGui::GetCursorScreenPos();
               const ImVec2 bmax(bmin.x + textSize.x + pad.x * 2.0f, bmin.y + ImGui::GetFrameHeight());
               ImGui::InvisibleButton("##timelineAudioBadge", ImVec2(bmax.x - bmin.x, bmax.y - bmin.y));
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImU32 edge = audioIsLight ? IM_COL32(40, 130, 72, 255) : IM_COL32(96, 200, 132, 255);
               dl->AddRect(bmin, bmax, edge, 3.0f, 0, 1.0f);
               dl->AddText(ImVec2(bmin.x + pad.x, bmin.y + pad.y), edge, badge);
               if (ImGui::IsItemHovered())
                  HelpTip(engineOn
                     ? "The Arrangement Timeline is driving audio. Hand it back to the canvas from the timeline panel."
                     : "The Arrangement Timeline will drive audio once the engine is started.");
            }
         }

         ImGui::Separator();

         static const int kDens[] = { 1, 2, 4, 8, 16 };
         auto SnapToValidDenominator = [](int val) -> int {
            int bestDen = kDens[0];
            int bestDist = std::abs(val - kDens[0]);
            for (int d : kDens)
            {
               int dist = std::abs(val - d);
               if (dist < bestDist)
               {
                  bestDist = dist;
                  bestDen = d;
               }
            }
            return bestDen;
         };
         auto DenToIdx = [](int d) -> int {
            for (int i = 0; i < 5; i++)
               if (kDens[i] == d) return i;
            return 2;
         };

         enum class TopBarField { None, Bpm, TsNum, TsDen };
         static TopBarField sActiveField = TopBarField::None;
         static char sFieldText[32] = "";
         static bool sFieldJustOpened = false;
         static float sDragAccumY = 0.0f;

         // 2. Tempo & Meter (BPM + Time Signature)
         {
            float bpm = transport.Tempo();
            TopBarLabel("BPM");
            TopBarSameLine(4.0f);

            if (sActiveField == TopBarField::Bpm)
            {
               ImGui::SetNextItemWidth(54.0f);
               if (sFieldJustOpened)
               {
                  ImGui::SetKeyboardFocusHere();
                  sFieldJustOpened = false;
               }
               const bool entered = ImGui::InputText("##bpmInput", sFieldText, sizeof(sFieldText),
                                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (entered || ImGui::IsItemDeactivated())
               {
                  char* end = nullptr;
                  float parsed = strtof(sFieldText, &end);
                  if (end != sFieldText && parsed > 0.0f)
                     transport.SetTempo(std::clamp(parsed, 20.0f, 300.0f));
                  sActiveField = TopBarField::None;
               }
            }
            else
            {
               char bpmBuf[32];
               snprintf(bpmBuf, sizeof(bpmBuf), "%.1f###bpmBtn", bpm);
               ImGui::Button(bpmBuf);
               if (!ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                  HelpTip("Tempo - drag, double-click or type to change.\n"
                                    "Arrangement Timeline clips keep their bar/beat positions:\n"
                                    "a tempo change moves their times in seconds, not their bars.");
               if (ImGui::IsItemHovered())
               {
                  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                  {
                     sActiveField = TopBarField::Bpm;
                     snprintf(sFieldText, sizeof(sFieldText), "%.1f", bpm);
                     sFieldJustOpened = true;
                  }
                  else
                  {
                     ImGuiIO& io = ImGui::GetIO();
                     for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
                     {
                        ImWchar ch = io.InputQueueCharacters[i];
                        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-')
                        {
                           sActiveField = TopBarField::Bpm;
                           sFieldText[0] = (char)ch;
                           sFieldText[1] = '\0';
                           sFieldJustOpened = true;
                           break;
                        }
                     }
                  }
               }
               if (ImGui::IsItemActive())
               {
                  const float dy = -ImGui::GetIO().MouseDelta.y;
                  const float speed = ImGui::GetIO().KeyShift ? 0.05f : 0.25f;
                  bpm = std::clamp(bpm + dy * speed, 20.0f, 300.0f);
                  transport.SetTempo(bpm);
               }
            }
         }

         TopBarSameLine(8.0f);

         // Time signature (numerator / denominator)
         {
            int tsNum = transport.TimeSigNumerator();
            const int tsDen = transport.TimeSigDenominator();

            // Numerator
            if (sActiveField == TopBarField::TsNum)
            {
               ImGui::SetNextItemWidth(30.0f);
               if (sFieldJustOpened)
               {
                  ImGui::SetKeyboardFocusHere();
                  sFieldJustOpened = false;
               }
               const bool entered = ImGui::InputText("##tsNumInput", sFieldText, sizeof(sFieldText),
                                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (entered || ImGui::IsItemDeactivated())
               {
                  int parsed = atoi(sFieldText);
                  if (parsed > 0)
                     transport.SetTimeSignature(std::clamp(parsed, 1, 99), tsDen);
                  sActiveField = TopBarField::None;
               }
            }
            else
            {
               char numBuf[16];
               snprintf(numBuf, sizeof(numBuf), "%d###tsNumBtn", tsNum);
               ImGui::Button(numBuf);
               if (ImGui::IsItemHovered())
               {
                  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                  {
                     sActiveField = TopBarField::TsNum;
                     snprintf(sFieldText, sizeof(sFieldText), "%d", tsNum);
                     sFieldJustOpened = true;
                  }
                  else
                  {
                     ImGuiIO& io = ImGui::GetIO();
                     for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
                     {
                        ImWchar ch = io.InputQueueCharacters[i];
                        if (ch >= '0' && ch <= '9')
                        {
                           sActiveField = TopBarField::TsNum;
                           sFieldText[0] = (char)ch;
                           sFieldText[1] = '\0';
                           sFieldJustOpened = true;
                           break;
                        }
                     }
                  }
               }
               if (ImGui::IsItemActive())
               {
                  const float dy = -ImGui::GetIO().MouseDelta.y;
                  sDragAccumY += dy;
                  const float kStep = 6.0f;
                  if (std::abs(sDragAccumY) >= kStep)
                  {
                     int steps = (int)(sDragAccumY / kStep);
                     sDragAccumY -= steps * kStep;
                     tsNum = std::clamp(tsNum + steps, 1, 99);
                     transport.SetTimeSignature(tsNum, tsDen);
                  }
               }
            }

            TopBarSameLine(3.0f);
            TopBarLabel("/", true);
            TopBarSameLine(3.0f);

            // Denominator
            if (sActiveField == TopBarField::TsDen)
            {
               ImGui::SetNextItemWidth(30.0f);
               if (sFieldJustOpened)
               {
                  ImGui::SetKeyboardFocusHere();
                  sFieldJustOpened = false;
               }
               const bool entered = ImGui::InputText("##tsDenInput", sFieldText, sizeof(sFieldText),
                                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (entered || ImGui::IsItemDeactivated())
               {
                  int parsed = atoi(sFieldText);
                  int snapped = SnapToValidDenominator(parsed);
                  transport.SetTimeSignature(tsNum, snapped);
                  sActiveField = TopBarField::None;
               }
            }
            else
            {
               char denBuf[16];
               snprintf(denBuf, sizeof(denBuf), "%d###tsDenBtn", tsDen);
               ImGui::Button(denBuf);
               if (ImGui::IsItemHovered())
               {
                  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                  {
                     sActiveField = TopBarField::TsDen;
                     snprintf(sFieldText, sizeof(sFieldText), "%d", tsDen);
                     sFieldJustOpened = true;
                  }
                  else
                  {
                     ImGuiIO& io = ImGui::GetIO();
                     for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
                     {
                        ImWchar ch = io.InputQueueCharacters[i];
                        if (ch >= '0' && ch <= '9')
                        {
                           sActiveField = TopBarField::TsDen;
                           sFieldText[0] = (char)ch;
                           sFieldText[1] = '\0';
                           sFieldJustOpened = true;
                           break;
                        }
                     }
                  }
               }
               if (ImGui::IsItemActive())
               {
                  const float dy = -ImGui::GetIO().MouseDelta.y;
                  sDragAccumY += dy;
                  const float kStep = 10.0f;
                  if (std::abs(sDragAccumY) >= kStep)
                  {
                     int steps = (int)(sDragAccumY / kStep);
                     sDragAccumY -= steps * kStep;
                     int curIdx = DenToIdx(tsDen);
                     int nextIdx = std::clamp(curIdx + steps, 0, 4);
                     if (nextIdx != curIdx)
                        transport.SetTimeSignature(tsNum, kDens[nextIdx]);
                  }
               }
            }
         }

         // Metronome: click toggles, right-click opens volume / accent (no hover
         // text, by design). Sits
         // in the Tempo & Meter group because it follows exactly those two.
         TopBarSameLine(8.0f);
         {
            // Read once: the click below flips gMetronomeOn, and the push/pop
            // pair must use the state it was pushed with.
            const bool metronomeWasOn = gMetronomeOn;
            if (metronomeWasOn)
               ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
            if (ImGui::Button("##metronomeBtn", ImVec2(32.0f, 0.0f)))
               gMetronomeOn = !gMetronomeOn;
            if (metronomeWasOn)
               ImGui::PopStyleColor();
            {
               ImVec4 iconCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
               if (!metronomeWasOn)
                  iconCol.w *= 0.78f;
               // The pendulum flips side on every beat - a hard 0/1, no easing -
               // so each click lands exactly as it snaps over. Upright when off
               // or while the transport is stopped.
               const float swing = (metronomeWasOn && isTransportPlaying)
                                      ? (((long long)std::floor(transport.Beats()) & 1) ? 1.0f : -1.0f)
                                      : 0.0f;
               const ImVec2 bmin = ImGui::GetItemRectMin();
               const ImVec2 bmax = ImGui::GetItemRectMax();
               Tabler::DrawMetronome(ImGui::GetWindowDrawList(),
                                     ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f),
                                     (bmax.y - bmin.y) * 0.84f, ImGui::GetColorU32(iconCol), swing);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
               ImGui::OpenPopup("##metronomePopup");

            if (ImGui::BeginPopup("##metronomePopup"))
            {
               // The top bar flattens every frame colour to transparent; a
               // slider needs its real theme frame back to be findable.
               const ImGuiStyle& base = ImGui::GetStyle();
               ImGui::PushStyleColor(ImGuiCol_FrameBg, base.Colors[ImGuiCol_FrameBg]);
               ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, base.Colors[ImGuiCol_FrameBgHovered]);
               ImGui::PushStyleColor(ImGuiCol_FrameBgActive, base.Colors[ImGuiCol_FrameBgActive]);
               ImGui::SetNextItemWidth(120.0f);
               const bool volChanged = ImGui::SliderFloat("volume##metronomeVol", &gMetronomeVolume, 0.0f, 1.0f, "%.2f");
               ImGui::PopStyleColor(3);
               if (volChanged)
                  gMetronomeDirty = true;
               if (ImGui::Selectable("accent first beat", gMetronomeAccent, ImGuiSelectableFlags_DontClosePopups))
               {
                  gMetronomeAccent = !gMetronomeAccent;
                  gMetronomeDirty = true;
               }
               ImGui::EndPopup();
            }
            if (gMetronomeDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               SaveGeneralSettings();
               gMetronomeDirty = false;
            }
            AudioEngine::Instance().SetMetronome(gMetronomeOn, gMetronomeVolume, gMetronomeAccent);
         }

         ImGui::Separator();

         // 3. Global Key & Scale
         static const char* const kKeyNames[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
         };
         auto FormatScaleDisplayName = [](const std::string& name) -> std::string {
            std::string out = name;
            bool capNext = true;
            for (size_t i = 0; i < out.size(); i++)
            {
               if (std::isalpha((unsigned char)out[i]))
               {
                  if (capNext)
                  {
                     out[i] = (char)std::toupper((unsigned char)out[i]);
                     capNext = false;
                  }
               }
               else
               {
                  capNext = true;
               }
            }
            return out;
         };

         int curKey = transport.Key();
         int curScale = transport.Scale();
         const auto& scaleList = MusicTime::ScaleTypeList();
         const char* curScaleName = (curScale >= 0 && curScale < (int)scaleList.size()) ? scaleList[curScale].c_str() : "major";
         const std::string capScaleName = FormatScaleDisplayName(curScaleName);

         {
            TopBarLabel("Key");
            TopBarSameLine(4.0f);

            if (ImGui::Button(kKeyNames[std::clamp(curKey, 0, 11)]))
               ImGui::OpenPopup("##globalKeyPopup");

            TopBarSameLine(4.0f);

            if (ImGui::Button(capScaleName.c_str()))
               ImGui::OpenPopup("##globalScalePopup");
         }

         if (ImGui::BeginPopup("##globalKeyPopup"))
         {
            for (int i = 0; i < 12; i++)
            {
               if (ImGui::Selectable(kKeyNames[i], i == curKey))
                  transport.SetKey(i);
            }
            ImGui::EndPopup();
         }
         if (ImGui::BeginPopup("##globalScalePopup"))
         {
            for (int i = 0; i < (int)scaleList.size(); i++)
            {
               const std::string capOpt = FormatScaleDisplayName(scaleList[i]);
               if (ImGui::Selectable(capOpt.c_str(), i == curScale))
                  transport.SetScale(i);
            }
            ImGui::EndPopup();
         }

         ImGui::Separator();

         // 4. Telemetry (Bar & beat, frame cost, CPU load)
         char barBeatBuf[64];
         snprintf(barBeatBuf, sizeof(barBeatBuf), "bar %d  beat %.2f",
                  1 + (int)transport.Bars(),
                  std::fmod(transport.Beats(), transport.BeatsPerBar()) + 1.0);

         // Frame cost
         static double sSmoothedMs = 0.0;
         sSmoothedMs = (sSmoothedMs <= 0.0)
                          ? gLastFrameMs
                          : sSmoothedMs * 0.9 + gLastFrameMs * 0.1;
         const double fps = sSmoothedMs > 0.0001 ? 1000.0 / sSmoothedMs : 0.0;

         char readout[80];
         snprintf(readout, sizeof(readout), "%.1f fps   %.1f ms", fps, sSmoothedMs);

         const bool audioEngineOn = AudioEngine::Instance().SampleRate() > 0.0;
         const double audioLoad = AudioEngine::Instance().LastBlockLoad();
         const AudioEngine::XrunCounts xrunParts = AudioEngine::Instance().Xruns();
         const uint64_t xruns = xrunParts.Total();
         const bool audioDead = audioEngineOn && !AudioEngine::Instance().IsAlive();
         char cpuReadout[32];
         if (audioDead)
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu lost");
         else if (audioEngineOn)
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu %.0f%%%s", audioLoad * 100.0, xruns > 0 ? " !" : "");
         else
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu --");

         TopBarLabel(barBeatBuf, true);
         TopBarSameLine(8.0f);
         TopBarLabel(readout, true);
         TopBarSameLine(8.0f);
         TopBarLabel(cpuReadout, true);

         if (audioEngineOn && xruns > 0 && ImGui::IsItemHovered())
            ImGui::SetTooltip("xruns=%llu this session\n%llu late block%s (render over the deadline)\n%llu reported by the audio device",
                              (unsigned long long)xruns,
                              (unsigned long long)xrunParts.deadline, xrunParts.deadline == 1 ? "" : "s",
                              (unsigned long long)xrunParts.os);

         // Left cluster's true rightmost extent (window-local X), used below
         // to crop the right cluster instead of letting it overlap the left
         // one when the window gets too narrow to fit both.
         const float leftClusterEndX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

         // Far right: an "Update" button (green, shown only while a newer
         // version is actually available), then icon buttons for the
         // Viewport panel, Modulation matrix and Performance mode, then a
         // search icon+label - all sharing the same transparent/hover-fill
         // button style as BPM/Key/Scale so they read as one family of
         // controls rather than the dimmed bar/beat/fps/cpu cluster.
         // On a narrow window these are dropped one at a time (icon toggles
         // first, then search, then Update) rather than drawn on top of the
         // left cluster - the bar crops instead of clutters.
         const float windowRight = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
         const float itemGap = ImGui::GetStyle().ItemSpacing.x * 3.0f;
         const float minGap = 12.0f;
         float cursorX = windowRight;

         if (UpdateCheck::UpdateAvailable())
         {
            const char* updateLabel = "Update";
            const float updateWidth = ImGui::CalcTextSize(updateLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (cursorX - updateWidth >= leftClusterEndX + minGap)
            {
               cursorX -= updateWidth;

               ImGui::SameLine(cursorX);
               ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.62f, 0.34f, 1.0f));
               ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.70f, 0.40f, 1.0f));
               ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.52f, 0.28f, 1.0f));
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
               if (ImGui::Button(updateLabel))
                  Platform::OpenExternalUrl("https://n1m21n.github.io/Infinite/#download");
               ImGui::PopStyleColor(4);
               if (ImGui::IsItemHovered())
               {
                  ImGui::SetTooltip("version %s is available (you have %s) - click to download",
                                     UpdateCheck::LatestVersion().c_str(), INFINITE_VERSION_STRING);
               }
               if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                  UpdateCheck::Dismiss();
               cursorX -= itemGap;
            }
         }

         {
            const char* searchLabel = "search";
            const float iconSize = ImGui::GetFrameHeight() * 0.9f;
            const float iconSlot = iconSize + 7.0f;
            const float textW = ImGui::CalcTextSize(searchLabel).x;
            const float totalW = iconSlot + textW + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (cursorX - totalW >= leftClusterEndX + minGap)
            {
               cursorX -= totalW;

               ImGui::SameLine(cursorX);
               const ImVec2 btnStart = ImGui::GetCursorScreenPos();
               const bool clicked = ImGui::Button("##searchhit", ImVec2(totalW, 0.0f));
               const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const float rowH = ImGui::GetItemRectSize().y;
               const ImVec2 iconCenter(btnStart.x + ImGui::GetStyle().FramePadding.x + iconSize * 0.5f, btnStart.y + rowH * 0.5f);
               Tabler::DrawSearch(dl, iconCenter, iconSize, col);
               const float textY = btnStart.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
               dl->AddText(ImVec2(btnStart.x + ImGui::GetStyle().FramePadding.x + iconSlot, textY), col, searchLabel);
               if (clicked)
                  gNodePanelOpen = !gNodePanelOpen;
               cursorX -= itemGap;
            }
         }

         // Icon-only toggle buttons for Viewport / Modulation matrix /
         // Performance mode - a little larger than the transport play/
         // rewind buttons (38px, icon at 88% of the row height) since these
         // carry no text label to help them read at a glance.
         // Returns false without drawing anything when there isn't room -
         // these are the first things dropped on a narrow window, since they
         // carry no text label and are the least essential of the cluster.
         auto TopBarIconToggle = [&](const char* id, bool isOpen, void (*draw)(ImDrawList*, ImVec2, float, ImU32, float), const char* tooltip)
         {
            const float btnW = 38.0f;
            if (cursorX - btnW < leftClusterEndX + minGap)
               return false;
            cursorX -= btnW;
            ImGui::SameLine(cursorX);

            if (isOpen)
               ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
            const bool clicked = ImGui::Button(id, ImVec2(btnW, 0.0f));
            if (isOpen)
               ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
               HelpTip("%s", tooltip);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.88f;
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            if (draw != nullptr)
               draw(dl, center, iconSize, col, 0.0f);
            else
               Tabler::DrawPlaceholder(dl, center, iconSize, col, 0.0f);

            cursorX -= itemGap;
            return clicked;
         };

         if (TopBarIconToggle("##arrangePanelToggle", gArrangePanelOpen, &Tabler::DrawBox3D, "Arrangement timeline"))
            gArrangePanelOpen = !gArrangePanelOpen;
         if (TopBarIconToggle("##perfPanelToggle", gPerfPanelOpen, &Tabler::DrawDisc, "Performance mode"))
            gPerfPanelOpen = !gPerfPanelOpen;
         if (TopBarIconToggle("##modMatrixToggle", gModMatrixOpen, &Tabler::DrawGridDots, "Modulation matrix"))
            gModMatrixOpen = !gModMatrixOpen;
         if (TopBarIconToggle("##viewportPanelToggle", gViewportPanelOpen, &Tabler::DrawLayoutSidebar, "Viewport panel"))
            gViewportPanelOpen = !gViewportPanelOpen;

         ImGui::PopStyleColor(6);
         ImGui::PopStyleVar(4);

         ImGui::EndMenuBar();
      }

      // The canvas zoomed wildly on a trackpad because it consumes raw wheel
      // deltas; damp them for the duration of the editor, then restore.
      ImGuiIO& io = ImGui::GetIO();
      const float savedWheel = io.MouseWheel;
      const float savedWheelH = io.MouseWheelH;
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
            ImGuiWindow* w = ImGui::FindWindowByName(wname);
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

      const float kNodePanelWidth = 300.0f;
      const bool viewportPanelOpen = gViewportPanelOpen;
      const bool viewportBottom = viewportPanelOpen && gViewportPanelDock == 0;
      const bool viewportRight = viewportPanelOpen && gViewportPanelDock == 1;
      const bool viewportLeft = viewportPanelOpen && gViewportPanelDock == 2;
      const bool viewportTop = viewportPanelOpen && gViewportPanelDock == 3;
      const bool matrixBottom = gModMatrixOpen && gModMatrixDock == 0;
      const bool matrixRight = gModMatrixOpen && gModMatrixDock == 1;
      const bool matrixLeft = gModMatrixOpen && gModMatrixDock == 2;
      const bool matrixTop = gModMatrixOpen && gModMatrixDock == 3;
      const bool perfBottom = gPerfPanelOpen && gPerfPanelDock == 0;
      const bool perfRight = gPerfPanelOpen && gPerfPanelDock == 1;
      const bool perfLeft = gPerfPanelOpen && gPerfPanelDock == 2;
      const bool perfTop = gPerfPanelOpen && gPerfPanelDock == 3;
      const bool arrangeBottom = gArrangePanelOpen && ArrangePanelDock() == 0;
      const bool arrangeRight = gArrangePanelOpen && ArrangePanelDock() == 1;
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
      const float graphHeight = std::max(150.0f,
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
      const bool dropInsideArrangePanel =
         gDropPos.x >= gArrangePanelRectMin.x && gDropPos.x < gArrangePanelRectMax.x &&
         gDropPos.y >= gArrangePanelRectMin.y && gDropPos.y < gArrangePanelRectMax.y;
      if (!gDroppedFiles.empty() && !dropInsideArrangePanel)
      {
         // Everything ModelIO reads. Checked before video because "usdz" and
         // "abc" would otherwise fall through to the image branch and fail.
         static const std::vector<std::string> kAudioExt = {
            "wav", "aif", "aiff", "mp3", "m4a", "aac", "caf", "flac", "ogg"
         };
         static const std::vector<std::string> kModelExt = {
            "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc"
         };
         // glTF/GLB are handled by their own branch (below, checked before
         // kModelExt) rather than folded into it: on a fresh drop they
         // auto-spawn a whole Material + Image Source rig, not just a bare
         // Model 3D node - see GltfImport.h.
         static const std::vector<std::string> kGltfExt = { "gltf", "glb" };
         // Plugin bundles, not files - see the branch that consumes this.
         static const std::vector<std::string> kPluginBundleExt = { "component", "vst3" };
         // Field build step 17: portable device files. Its own extension
         // check must run before the "inf"/"infinite" branch below - "inf"
         // is a strict-suffix match (HasExtension splits on the last dot),
         // so "field" and "infdev" never collide with it, but must still get
         // their own branch since they aren't in kAudioExt/kModelExt/etc.
         static const std::vector<std::string> kFieldExt = { "field", "infdev" };
         ImVec2 canvasPos = ed::ScreenToCanvas(gDropPos);
         DrumSequencerNode* dropTargetDrum = FindNodeUnderCanvasPoint<DrumSequencerNode>(canvasPos);
         int dropTargetLane =
            dropTargetDrum != nullptr ? DrumSequencerLaneForCanvasPos(dropTargetDrum, canvasPos.x, canvasPos.y) : 0;
         static const std::vector<std::string> kMidiExt = { "mid", "midi" };
         MidiFileNode* dropTargetMidi = FindNodeUnderCanvasPoint<MidiFileNode>(canvasPos);
         SamplerNode* dropTargetSampler = FindNodeUnderCanvasPoint<SamplerNode>(canvasPos);
         MpcNode* dropTargetMpc = FindNodeUnderCanvasPoint<MpcNode>(canvasPos);
         std::vector<std::string> mpcDropPaths;
         SlicerNode* dropTargetSlicer = FindNodeUnderCanvasPoint<SlicerNode>(canvasPos);
         PaulStretchNode* dropTargetPaul = FindNodeUnderCanvasPoint<PaulStretchNode>(canvasPos);
         GranularNode* dropTargetGran = FindNodeUnderCanvasPoint<GranularNode>(canvasPos);
         MolderNode* dropTargetMolder = FindNodeUnderCanvasPoint<MolderNode>(canvasPos);
         GrainMolderNode* dropTargetGrainMolder = FindNodeUnderCanvasPoint<GrainMolderNode>(canvasPos);
         AudioFileNode* dropTargetAudioFile = FindNodeUnderCanvasPoint<AudioFileNode>(canvasPos);
         AudioPluginNode* dropTargetPlugin = FindNodeUnderCanvasPoint<AudioPluginNode>(canvasPos);
         ModelSourceNode* dropTargetModel = FindNodeUnderCanvasPoint<ModelSourceNode>(canvasPos);
         VideoSourceNode* dropTargetVideo = FindNodeUnderCanvasPoint<VideoSourceNode>(canvasPos);
         ImageSourceNode* dropTargetImage = FindNodeUnderCanvasPoint<ImageSourceNode>(canvasPos);
         FieldElementNode* dropTargetFieldElement = FindNodeUnderCanvasPoint<FieldElementNode>(canvasPos);
         FieldPrimitiveNode* dropTargetFieldPrimitive = FindNodeUnderCanvasPoint<FieldPrimitiveNode>(canvasPos);
         FieldPixelNode* dropTargetFieldPixel = FindNodeUnderCanvasPoint<FieldPixelNode>(canvasPos);
         FieldSampleNode* dropTargetFieldSample = FindNodeUnderCanvasPoint<FieldSampleNode>(canvasPos);
         FieldSynthNode* dropTargetFieldSynth = FindNodeUnderCanvasPoint<FieldSynthNode>(canvasPos);
         FieldGraphNode* dropTargetFieldGraph = FindNodeUnderCanvasPoint<FieldGraphNode>(canvasPos);
         FormulaNode* dropTargetFormula = FindNodeUnderCanvasPoint<FormulaNode>(canvasPos);
         float offset = 0.0f;
         std::vector<std::string> pendingAudioDropPaths;
         bool droppedCheckpointPushed = false;
         auto ensureDroppedCheckpoint = [&]()
         {
            if (!droppedCheckpointPushed)
            {
               PushUndoCheckpoint();
               droppedCheckpointPushed = true;
            }
         };
         for (std::string path : gDroppedFiles)
         {
            while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
               path.pop_back();

            // Field build step 17: a dropped .field (or .infdev) hot-swaps whatever
            // matching-domain Field node sits under the drop point. Checked
            // before the "inf"/"infinite" patch-load branch below since
            // extensions are checked in order. No new node is spawned
            // when nothing matches under the cursor or the domain doesn't
            // match - same silent-no-op contract every other branch in this
            // dispatch already has for an unmatched drop.
            if (HasExtension(path, kFieldExt))
            {
               Field::DeviceFile device;
               std::string err;
               if (Field::LoadFromFieldFile(path, device, err))
               {
                   if (dropTargetFieldElement != nullptr && device.domain == "element")
                   {
                      ensureDroppedCheckpoint();
                      dropTargetFieldElement->LoadDeviceFile(device);
                      gPatchDirty = true;
                      continue;
                   }
                   if (dropTargetFieldPrimitive != nullptr && device.domain == "primitive")
                   {
                      ensureDroppedCheckpoint();
                      dropTargetFieldPrimitive->LoadDeviceFile(device);
                      gPatchDirty = true;
                      continue;
                   }
                  if (dropTargetFieldPixel != nullptr && device.domain == "pixel")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldPixel->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFieldSample != nullptr && device.domain == "sample")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldSample->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFieldSynth != nullptr && device.domain == "synth")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldSynth->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFieldGraph != nullptr && device.domain == "graph")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFieldGraph->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
                  if (dropTargetFormula != nullptr && device.domain == "formula")
                  {
                     ensureDroppedCheckpoint();
                     dropTargetFormula->LoadDeviceFile(device);
                     gPatchDirty = true;
                     continue;
                  }
               }
                  // If dropped on empty canvas, spawn a new Field node of that domain
                  GraphNode* spawned = nullptr;
                  if (device.domain == "element")
                     spawned = SpawnNode("Field Modifier", "3D", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "pixel")
                     spawned = SpawnNode("FieldPixel", "Source", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "sample")
                     spawned = SpawnNode("Field Effect", "AudioEffects", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "synth")
                     spawned = SpawnNode("Field Synth", "Synths", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "graph")
                     spawned = SpawnNode("Field Graph", "Utility", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "primitive")
                     spawned = SpawnNode("Field Primitive", "3D", canvasPos.x + offset, canvasPos.y);
                  else if (device.domain == "formula")
                     spawned = SpawnNode("Formula", "Modulators", canvasPos.x + offset, canvasPos.y);

                  if (spawned != nullptr)
                  {
                     ensureDroppedCheckpoint();
                     if (auto* fe = dynamic_cast<FieldElementNode*>(spawned->node.get()))
                        fe->LoadDeviceFile(device);
                     else if (auto* fpn = dynamic_cast<FieldPrimitiveNode*>(spawned->node.get()))
                        fpn->LoadDeviceFile(device);
                     else if (auto* fp = dynamic_cast<FieldPixelNode*>(spawned->node.get()))
                        fp->LoadDeviceFile(device);
                     else if (auto* fs = dynamic_cast<FieldSampleNode*>(spawned->node.get()))
                        fs->LoadDeviceFile(device);
                     else if (auto* fsynth = dynamic_cast<FieldSynthNode*>(spawned->node.get()))
                        fsynth->LoadDeviceFile(device);
                     else if (auto* fg = dynamic_cast<FieldGraphNode*>(spawned->node.get()))
                        fg->LoadDeviceFile(device);
                     else if (auto* form = dynamic_cast<FormulaNode*>(spawned->node.get()))
                        form->LoadDeviceFile(device);
                     spawned->showParams = true;
                     offset += 240.0f;
                     gPatchDirty = true;
                     continue;
                  }
               // Unreadable file or unspawnable domain: fall through silently.
               continue;
            }

            if (HasExtension(path, std::vector<std::string> { "inf", "infinite" }))
            {
               GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
               gRequestFitView = true;
               continue;
            }

            if (HasExtension(path, kAudioExt))
            {
               if (dropTargetDrum != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetDrum->LoadFileToLane(dropTargetLane, path);
                  dropTargetLane = (dropTargetLane + 1) % DrumSequencerNode::kNumLanes;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetMpc != nullptr)
               {
                  mpcDropPaths.push_back(path); // loaded together below, so a multi-file drop fills successive pads
                  continue;
               }
               if (dropTargetSampler != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetSampler->LoadFile(path);
                  dropTargetSampler = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetSlicer != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetSlicer->LoadFile(path);
                  dropTargetSlicer = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetPaul != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetPaul->LoadFile(path);
                  dropTargetPaul = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetGran != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetGran->LoadFile(path);
                  dropTargetGran = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetMolder != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetMolder->LoadFile(path);
                  dropTargetMolder = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetGrainMolder != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetGrainMolder->LoadFile(path);
                  dropTargetGrainMolder = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               if (dropTargetAudioFile != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetAudioFile->Open(path);
                  dropTargetAudioFile = nullptr;
                  gPatchDirty = true;
                  continue;
               }

               pendingAudioDropPaths.push_back(path);
               continue;
            }

            GraphNode* spawned = nullptr;
            if (HasExtension(path, kMidiExt))
            {
               if (dropTargetMidi != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetMidi->path = path;
                  dropTargetMidi = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("MIDI File", "Notes", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<MidiFileNode*>(spawned->node.get())->path = path;
            }
            else if (HasExtension(path, kPluginBundleExt))
            {
               const bool isVst3 = HasExtension(path, std::vector<std::string> { "vst3" });
#if !INFINITE_ENABLE_VST3
               if (isVst3)
               {
                  printf("VST3 support is not compiled into this build (build with "
                         "-DINFINITE_ENABLE_VST3=ON): %s\n", path.c_str());
                  continue;
               }
#endif
               std::vector<Platform::PluginDesc> found;
               const bool resolved = isVst3
                  ? (Platform::DescribeVST3Bundle(path, found) && !found.empty())
                  : (Platform::DescribeAudioUnitBundle(path, found) && !found.empty());
               if (!resolved)
               {
                  printf("dropped plugin bundle could not be resolved to a plugin: %s\n",
                         path.c_str());
                  continue;
               }
               // Prefer whatever the scanner already knows about this identity:
               // its display name came from the component registry, which is
               // better than the bundle's own Info.plist string.
               Platform::PluginDesc desc = found.front();
               if (const PluginScanner::Entry* known = gPluginScanner.FindByIdentifier(desc.identifier))
               {
                  const std::string origPath = desc.path;
                  desc = *known;
                  if (desc.path.empty() && !origPath.empty())
                     desc.path = origPath;
               }

               if (dropTargetPlugin != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetPlugin->LoadPlugin(desc);
                  dropTargetPlugin = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Plugin", "AudioEffects", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<AudioPluginNode*>(spawned->node.get())->LoadPlugin(desc);
            }
            else if (HasExtension(path, kGltfExt))
            {
               if (dropTargetModel != nullptr)
               {
                  // Reload in place - do NOT auto-spawn a duplicate
                  // Material/texture rig on top of whatever is already wired.
                  ensureDroppedCheckpoint();
                  dropTargetModel->Load(path);
                  dropTargetModel = nullptr;
                  gPatchDirty = true;
                  continue;
               }

               // Fresh drop: Model 3D + Material + one Image Source per
               // present texture map, already wired - the whole point of
               // this feature is zero manual cabling. Everything from here
               // to gSuppressUndoCheckpoints=false is one undo step.
               ensureDroppedCheckpoint();
               gSuppressUndoCheckpoints = true;

               // GraphNode* from SpawnNode points into gNodes' own storage,
               // which a LATER SpawnNode call can reallocate (std::vector
               // growth) - holding modelNode/materialNode across the several
               // more SpawnNode calls below would dangle. Capture the
               // stable `index` field immediately instead and re-resolve
               // via FindNodeByIndex() each time a node is actually needed.
               int modelIndex = -1;
               {
                  GraphNode* modelNode = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y);
                  if (modelNode != nullptr)
                  {
                     modelIndex = modelNode->index;
                     static_cast<ModelSourceNode*>(modelNode->node.get())->Load(path);
                  }
               }

               int materialIndex = -1;
               {
                  GraphNode* materialNode =
                     SpawnNode("Material", "3D", canvasPos.x + offset + 260.0f, canvasPos.y);
                  if (materialNode != nullptr)
                     materialIndex = materialNode->index;
               }

               if (modelIndex != -1 && materialIndex != -1)
               {
                  std::string wireErr;
                  ConnectNodes(modelIndex, 0, materialIndex, 0, wireErr);
               }

               std::string gltfErr;
               const GltfImport::GltfDecodePackage* pkg = GltfImport::DecodeCached(path, gltfErr);
               if (pkg != nullptr && materialIndex != -1)
               {
                  struct MapSlot
                  {
                     const GltfImport::GltfDecodedImage* img;
                     int mapIndex;
                     const char* slot;
                  };
                  const MapSlot maps[] = {
                     { &pkg->albedo, kMapAlbedo, "albedo" },
                     { &pkg->roughness, kMapRoughness, "roughness" },
                     { &pkg->metallic, kMapMetallic, "metallic" },
                     { &pkg->normalMap, kMapNormal, "normal" },
                     { &pkg->occlusion, kMapAmbientOcclusion, "ao" },
                     { &pkg->emissive, kMapEmission, "emission" },
                  };

                  const float texX = canvasPos.x + offset + 560.0f;
                  float texY = canvasPos.y;
                  for (const MapSlot& m : maps)
                  {
                     if (m.img->pixels.empty())
                        continue;

                     GraphNode* texNode = SpawnNode("Image Source", "Source", texX, texY);
                     if (texNode != nullptr)
                     {
                        auto* imgNode = static_cast<ImageSourceNode*>(texNode->node.get());
                        imgNode->LoadFromDecoded(m.img->pixels, m.img->width, m.img->height,
                                                 std::string("gltf://") + path + "#" + m.slot);
                        std::string wireErr;
                        ConnectNodes(texNode->index, 0, materialIndex, 1 + m.mapIndex, wireErr);
                     }
                     texY += 160.0f;
                  }
               }

               if (GraphNode* modelNode = (modelIndex != -1) ? FindNodeByIndex(modelIndex) : nullptr)
                  modelNode->showParams = true;
               if (GraphNode* materialNode = (materialIndex != -1) ? FindNodeByIndex(materialIndex) : nullptr)
                  materialNode->showParams = true;

               gSuppressUndoCheckpoints = false;
               gPatchDirty = true;
               offset += 240.0f;
               continue;
            }
            else if (HasExtension(path, kModelExt))
            {
               if (dropTargetModel != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetModel->Load(path);
                  dropTargetModel = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Model 3D", "3D", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ModelSourceNode*>(spawned->node.get())->Load(path);
            }
            else if (HasExtension(path, kVideoExt))
            {
               if (dropTargetVideo != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetVideo->Open(path);
                  dropTargetVideo = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Video", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<VideoSourceNode*>(spawned->node.get())->Open(path);
            }
            else
            {
               if (dropTargetImage != nullptr)
               {
                  ensureDroppedCheckpoint();
                  dropTargetImage->Load(path);
                  dropTargetImage = nullptr;
                  gPatchDirty = true;
                  continue;
               }
               spawned = SpawnNode("Image Source", "Source", canvasPos.x + offset, canvasPos.y);
               if (spawned != nullptr)
                  static_cast<ImageSourceNode*>(spawned->node.get())->Load(path);
            }
            if (spawned != nullptr)
               spawned->showParams = true;
            offset += 240.0f;
         }
         if (dropTargetMpc != nullptr && !mpcDropPaths.empty())
         {
            ensureDroppedCheckpoint();
            MpcDropFiles(dropTargetMpc, canvasPos.x, canvasPos.y, mpcDropPaths);
            gPatchDirty = true;
         }
         if (!pendingAudioDropPaths.empty())
         {
            gAudioDropPicker.justOpened = true;
            gAudioDropPicker.canvasPos = canvasPos;
            gAudioDropPicker.screenPos = gDropPos;
            gAudioDropPicker.paths = std::move(pendingAudioDropPaths);
         }
         gDroppedFiles.clear();
      }

      FrameTest_COLORTEST_2(frameId, window);

      FrameTest_RECTEST(frameId, window);

      FrameTest_RECTEARDOWNTEST(frameId, window);

      FrameTest_RESYNTHTEST(frameId, window);

      FrameTest_BYPASSTEST(frameId, window);

      FrameTest_CURVESLUTTEST(frameId, window);

      FrameTest_PHASEATEST(frameId, window);

      FrameTest_SELECTTEST(frameId, window);

      FrameTest_DISTRIBUTETEST(frameId, window);

      FrameTest_PHASE4TEST(frameId, window);

      FrameTest_INSTANCESELECTTEST(frameId, window);

      FrameTest_PADPATHTEST(frameId, window);

      FrameTest_UNDOTEST(frameId, window);

      // Modulation never creates an undo entry or dirties the patch, and one
      // user dropdown pick is exactly one undo entry holding the pre-pick
      // state (regression, docs/fix-briefs/modulated-dropdown-undo-spam.md).
      // A cable-driven discrete control writes its value back through the
      // caller's onSelect lambda / returns "changed" to the caller, and most
      // of those callers open with PushUndoCheckpoint() for the user-click
      // path - so every index boundary a modulator crossed used to serialize
      // the whole patch onto gUndoStack and set gPatchDirty. The existing
      // UNDOTEST never binds a modulator to anything, which is why this
      // shipped. Drives all five widget shapes over real drawn frames:
      //   AudioKnobRow::Dropdown      Audio Filter "type"
      //   AudioBareDropdown           Oscillator "oscWave"
      //   AudioKnobRow::DropdownKnob  Oscillator "oscFmMode"
      //   DropdownButton              Audio Color Ramp "mode"
      //   AudioKnobRow::Checkbox      Delay "sync to tempo"
      // from two Constants swept 0..1..0 several times. Then a second, unbound
      // Audio Filter's "type" dropdown is opened with its real onSelect lambda
      // and a row is committed through CommitDropdownPick - the code the popup
      // click runs - to prove the pick pushes one entry and Undo restores it.
      FrameTest_MODDROPDOWNUNDOTEST(frameId, window);

      FrameTest_ARRANGETEST(frameId, window);

      // Overhaul WP2 (docs/plans/arrangement/overhaul-prompt.md): the transport
      // clock's three new contracts - a tempo change doesn't move the
      // playhead, Beats() is seekable, and the loop wraps at a block boundary
      // rather than a UI frame.
      //
      // The audio clock is driven by hand here (NotifyAudioEngineStarted +
      // AdvanceAudioClock) with the real engine stopped first, so the fixture
      // is deterministic, needs no audio device, and never races a live audio
      // thread calling the same functions.
      FrameTest_TRANSPORTTEST(frameId, window);

      // Regression guard for the BuildPatchData() perf fix in
      // docs/plans/undo-delete-perf-prompt.md: it used to be O(N^2) with
      // dynamic_cast in the inner loop, which is what actually caused the
      // per-click stutter and the multi-second undo hang on a large patch.
      // The ceiling is deliberately generous - the fixed version should run
      // in low single-digit milliseconds even at this node count, so this is
      // here to catch a return to O(N^2) (or worse), not to chase a specific
      // number.
      // Arrangement timeline audio scheduling (overhaul WP3). Deterministic:
      // no device, no wall clock - the transport runs in offline mode and
      // AudioEngine::ProcessOffline is pumped by hand, so a block is a block
      // no matter how loaded the machine is.
      //
      // Everything here goes through the REAL RebuildAudioTopology over the
      // real gArrange model, not a hand-built topology: the bugs WP3 fixes
      // all lived in what that function decided to put in the topology, so a
      // fixture that built its own would test nothing.
      if (getenv("INFINITE_ARRANGEAUDIOTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;

         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kBpm = 120.0;            // 2 beats per second
         const double kSamplesPerBeat = kSr * 60.0 / kBpm;

         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         const AudioMode savedMode = gAudioMode;

         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 0.0f, 0.0f);
         const bool spawned = oscGn != nullptr && oscGn->node != nullptr;
         printf("arrange audio spawn oscillator: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {
            const uint64_t oscUid = oscGn->uid;
            const int oscIndex = oscGn->index;

            // One audio lane, clips filled in per section. Built straight into
            // gArrange (WP5b); revision keeps climbing across the reset so the
            // rebuild trigger never sees it rewind onto an old built value.
            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            Arrange::FindLane(gArrange, laneId)->name = "A1";
            (void)oscIndex;

            auto clearClips = [&]()
            {
               std::vector<uint64_t> ids;
               for (const Arrange::Clip& c : Arrange::FindLane(gArrange, laneId)->clips)
                  ids.push_back(c.id);
               Arrange::Delete(gArrange, ids);
            };
            auto addClip = [&](double startBeat, double lengthBeats, bool enabled)
            {
               Arrange::Clip c;
               c.start = Arrange::BeatsToTicks(startBeat);
               c.length = Arrange::BeatsToTicks(lengthBeats);
               c.srcUid = oscUid;
               c.enabled = enabled;
               Arrange::PlaceOverwrite(gArrange, laneId, c);
            };
            // Rebuilds the per-frame trigger fired while a render ran - it is
            // called once per block, so crossing a clip boundary with it
            // running is exactly the "a boundary must never rebuild" check.
            int staleRebuildsDuringRender = 0;

            gAudioMode = AudioMode::Timeline;
            tr.SetTempo((float)kBpm);
            tr.SetLoop(false, 0.0, 0.0);
            tr.SetPlaying(true);
            tr.SetOfflineMode(true, kSr);

            // Renders [startBeat, startBeat + numBlocks*kBlock samples) and
            // returns channel 0, concatenated. Rebuilds the topology first,
            // then prepares every node by hand: the PrepareToPlay loop inside
            // RebuildAudioTopology keys off a live device or an offline render
            // job, and this fixture has neither.
            // Params reach an AudioNode through its mailbox, which
            // CookIfNeeded fills - a node that has never been cooked runs on
            // its constructor defaults with an empty mailbox and produces
            // nothing. The main loop does this every frame; this fixture runs
            // its whole life inside one.
            int fixtureCookFrame = 1000000;
            auto cookAll = [&]()
            {
               fixtureCookFrame++;
               for (GraphNode& gn : gNodes)
                  gn.node->CookIfNeeded(fixtureCookFrame);
            };

            std::vector<float> lastChan1; // channel 1 of the most recent render()
            auto render = [&](double startBeat, int numBlocks)
            {
               RebuildAudioTopology();
               cookAll();
               for (GraphNode& gn : gNodes)
                  if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(startBeat);

               std::vector<float> chan0((size_t)kBlock), chan1((size_t)kBlock);
               float* chans[2] = { chan0.data(), chan1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;

               std::vector<float> out;
               out.reserve((size_t)kBlock * (size_t)numBlocks);
               lastChan1.clear();
               staleRebuildsDuringRender = 0;
               for (int b = 0; b < numBlocks; b++)
               {
                  if (ArrangeAudioRebuildIfStale())
                     staleRebuildsDuringRender++;
                  AudioEngine::Instance().ProcessOffline(buffer);
                  out.insert(out.end(), chan0.begin(), chan0.end());
                  lastChan1.insert(lastChan1.end(), chan1.begin(), chan1.end());
               }
               return out;
            };

            // Peak |x| over the samples covering [fromBeat, toBeat) of a
            // render that started at `originBeat`.
            auto peakOverBeats = [&](const std::vector<float>& x, double originBeat,
                                     double fromBeat, double toBeat)
            {
               const long long lo = std::max(0LL, (long long)((fromBeat - originBeat) * kSamplesPerBeat));
               const long long hi = std::min((long long)x.size(), (long long)((toBeat - originBeat) * kSamplesPerBeat));
               float peak = 0.0f;
               for (long long i = lo; i < hi; i++)
                  peak = std::max(peak, std::fabs(x[(size_t)i]));
               return peak;
            };

            // --- A. Two abutting clips of the same node are both audible ----
            // The original bug: the per-frame set of active srcIndex never
            // changed across the seam, so no rebuild happened and the stale
            // single-clip window silenced everything after the first clip.
            {
               clearClips();
               addClip(0.0, 2.0, true);
               addClip(2.0, 2.0, true);
               const std::vector<float> x = render(0.0, 800); // 800*256 = 204800 samples = 4.27 beats

               const float first = peakOverBeats(x, 0.0, 0.2, 1.8);
               const float second = peakOverBeats(x, 0.0, 2.2, 3.8);
               // The seam itself: abutting windows skip the declick, so the
               // signal must run straight through rather than dip to silence.
               const float seam = peakOverBeats(x, 0.0, 1.98, 2.02);
               // Two clip boundaries crossed (beat 2 and beat 4) with the
               // per-frame trigger polled every block: zero rebuilds.
               const bool aOk = first > 0.05f && second > 0.05f && seam > 0.05f &&
                                staleRebuildsDuringRender == 0;
               printf("arrange audio abutting clips: %s (first %.4f, second %.4f, seam %.4f, boundary rebuilds %d)\n",
                      aOk ? "OK" : "FAIL", first, second, seam, staleRebuildsDuringRender);
               allOk = allOk && aOk;
            }

            // --- B. Onset lands on the scheduled sample --------------------
            {
               clearClips();
               addClip(2.0, 2.0, true);
               const std::vector<float> x = render(0.0, 800);

               const long long expected = (long long)(2.0 * kSamplesPerBeat);
               long long firstAudible = -1;
               for (size_t i = 0; i < x.size(); i++)
                  if (std::fabs(x[i]) > 1e-5f) { firstAudible = (long long)i; break; }
               // The declick ramp is zero at exactly the onset sample and the
               // oscillator's own phase starts near zero, so the first sample
               // over the noise floor lands a hair after the scheduled one -
               // never before it, and never a UI frame later.
               const bool bOk = firstAudible >= expected && (firstAudible - expected) <= 8;
               printf("arrange audio onset: %s (scheduled %lld, first audible %lld, error %lld samples)\n",
                      bOk ? "OK" : "FAIL", expected, firstAudible, firstAudible - expected);
               allOk = allOk && bOk;
            }

            // --- C. A disabled clip is silent -------------------------------
            // `enabled` was not read by the audio path at all before WP3.
            {
               clearClips();
               addClip(0.0, 4.0, false);
               const std::vector<float> x = render(0.0, 400);
               const float peak = peakOverBeats(x, 0.0, 0.0, 2.0);
               const bool cOk = peak < 1e-6f;
               printf("arrange audio disabled clip: %s (peak %.8f)\n", cOk ? "OK" : "FAIL", peak);
               allOk = allOk && cOk;
            }

            // --- D. Paused in Timeline mode is silent -----------------------
            {
               clearClips();
               addClip(0.0, 4.0, true);
               tr.SetPlaying(false);
               const std::vector<float> x = render(0.0, 200);
               const float peak = peakOverBeats(x, 0.0, 0.0, 1.0);
               tr.SetPlaying(true);
               const bool dOk = peak < 1e-6f;
               printf("arrange audio paused: %s (peak %.8f)\n", dOk ? "OK" : "FAIL", peak);
               allOk = allOk && dOk;
            }

            // --- E. Seeking from clip A into clip B of the same node --------
            // The other half of the original bug: the set of active srcIndex
            // is identical on both sides of the seek, so nothing rebuilt and
            // clip B played silence.
            {
               clearClips();
               addClip(0.0, 2.0, true);
               addClip(4.0, 2.0, true);
               render(0.5, 100);                     // land inside clip A
               const std::vector<float> x = render(4.5, 200); // jump into clip B
               const float peak = peakOverBeats(x, 4.5, 4.6, 5.5);
               const bool eOk = peak > 0.05f;
               printf("arrange audio seek across clips: %s (peak %.4f)\n", eOk ? "OK" : "FAIL", peak);
               allOk = allOk && eOk;
            }

            // --- F. A rebuild mid-clip does not break the signal ------------
            // Editing an unrelated lane rebuilds the whole topology. With the
            // schedule carried on the terminal (and PDC state living outside
            // the topology), the block after the rebuild must continue the
            // same envelope rather than restart it.
            {
               clearClips();
               addClip(0.0, 8.0, true);
               RebuildAudioTopology();
               cookAll();
               for (GraphNode& gn : gNodes)
                  if (auto* an = dynamic_cast<AudioNode*>(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(1.0);

               std::vector<float> chan0((size_t)kBlock), chan1((size_t)kBlock);
               float* chans[2] = { chan0.data(), chan1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;

               // The rebuild goes through the same per-frame trigger the main
               // loop runs (WP5b: revision is the only change signal). It is
               // polled before every block, so the 39 blocks without an edit
               // are 39 no-op frames: exactly one rebuild over the whole run,
               // and the edit moves revision by exactly one.
               float beforePeak = 0.0f, afterPeak = 0.0f;
               const unsigned long long rebuildsBefore = gAudioTopologyRebuildCount;
               uint64_t otherLane = 0;
               bool bumpedOnce = false, noOpFrameQuiet = false;
               int triggered = 0;
               for (int b = 0; b < 40; b++)
               {
                  if (b == 20)
                  {
                     // An edit on a *different* lane - the clip under the
                     // playhead is untouched.
                     const uint64_t rev = gArrange.revision;
                     otherLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
                     bumpedOnce = gArrange.revision == rev + 1;
                  }
                  if (ArrangeAudioRebuildIfStale())
                     triggered++;
                  if (b == 20)
                     noOpFrameQuiet = !ArrangeAudioRebuildIfStale(); // same frame again: nothing changed
                  AudioEngine::Instance().ProcessOffline(buffer);
                  for (int i = 0; i < kBlock; i++)
                  {
                     if (b == 19) beforePeak = std::max(beforePeak, std::fabs(chan0[i]));
                     if (b == 20) afterPeak = std::max(afterPeak, std::fabs(chan0[i]));
                  }
               }
               const unsigned long long rebuilds = gAudioTopologyRebuildCount - rebuildsBefore;
               const bool fOk = beforePeak > 0.05f && afterPeak > 0.05f &&
                                std::fabs(afterPeak - beforePeak) < 0.25f * beforePeak &&
                                bumpedOnce && triggered == 1 && rebuilds == 1 && noOpFrameQuiet;
               printf("arrange audio rebuild mid-clip: %s (before %.4f, after %.4f, revision +1 %d, rebuilds %llu over 40 polled frames, no-op frame quiet %d)\n",
                      fOk ? "OK" : "FAIL", beforePeak, afterPeak, (int)bumpedOnce, rebuilds, (int)noOpFrameQuiet);
               allOk = allOk && fOk;
               Arrange::RemoveLane(gArrange, otherLane);
            }

            // --- H. Lane mix strip: mute, solo, pan, gain -------------------
            // The header's S / M / pan / gain, read by the schedule at
            // rebuild time. Mixer's rules: a muted lane is silent, and any
            // soloed audio lane silences every unsoloed one.
            {
               clearClips();
               addClip(0.0, 4.0, true);
               Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId);
               auto bump = [&]() { gArrange.revision++; };

               const std::vector<float> ref = render(0.0, 200);
               const float refL = peakOverBeats(ref, 0.0, 0.2, 1.0);
               const float refR = peakOverBeats(lastChan1, 0.0, 0.2, 1.0);

               ln->mute = true; bump();
               const float muted = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->mute = false; bump();

               const uint64_t soloLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
               Arrange::FindLane(gArrange, soloLane)->solo = true; bump();
               const float othersSoloed = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->solo = true; bump();
               const float bothSoloed = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               Arrange::RemoveLane(gArrange, soloLane);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->solo = false; bump();

               ln->pan = -1.0f; bump();
               const float hardL = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               const float hardLR = peakOverBeats(lastChan1, 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->pan = 0.0f;
               ln->gainDb = -6.0206f; bump();
               const float halfGain = peakOverBeats(render(0.0, 200), 0.0, 0.2, 1.0);
               ln = Arrange::FindLane(gArrange, laneId);
               ln->gainDb = 0.0f; bump();

               // Centre is unity per side (equal-power * sqrt2), hard left is
               // sqrt2 on L and nothing on R.
               const bool hOk = refL > 0.05f && std::fabs(refL - refR) < 0.01f * refL && muted < 1e-6f &&
                                othersSoloed < 1e-6f && bothSoloed > 0.9f * refL &&
                                std::fabs(hardL - refL * (float)M_SQRT2) < 0.02f * refL && hardLR < 1e-4f &&
                                std::fabs(halfGain - 0.5f * refL) < 0.02f * refL;
               printf("arrange audio lane mix: %s (centre L %.4f R %.4f, muted %.8f, other soloed %.8f, both soloed %.4f, "
                      "hard-left L %.4f R %.8f, -6dB %.4f)\n",
                      hOk ? "OK" : "FAIL", refL, refR, muted, othersSoloed, bothSoloed, hardL, hardLR, halfGain);
               allOk = allOk && hOk;
            }

            // --- G. Mode resets to Canvas on New and on Open ---------------
            {
               gAudioMode = AudioMode::Timeline;
               NewPatch();
               const bool afterNew = gAudioMode == AudioMode::Canvas;

               gAudioMode = AudioMode::Timeline;
               const bool afterOpen = !LoadPatchFrom("/nonexistent-arrangeaudiotest.ifp") ||
                                      gAudioMode == AudioMode::Canvas;
               // A failed open must NOT reset the mode - it never became a new
               // document - so re-check with a real round trip through a file
               // this fixture writes itself.
               bool afterRealOpen = true;
               {
                  const std::string path = "/tmp/infinite-arrangeaudiotest.ifp";
                  SpawnNode("Oscillator", "Synthesizers", 0.0f, 0.0f); // a patch with no nodes will not save
                  const bool saved = SavePatchTo(path);
                  if (saved)
                  {
                     gAudioMode = AudioMode::Timeline;
                     const bool loaded = LoadPatchFrom(path);
                     afterRealOpen = loaded && gAudioMode == AudioMode::Canvas;
                     if (!afterRealOpen)
                        printf("  [diag] saved %d loaded %d status '%s'\n", (int)saved, (int)loaded, gPatchStatus.c_str());
                     remove(path.c_str());
                  }
                  else
                  {
                     printf("  [diag] save failed: '%s'\n", gPatchStatus.c_str());
                  }
               }
               const bool gOk = afterNew && afterOpen && afterRealOpen;
               printf("arrange audio mode resets: %s (new %d, failed-open %d, open %d)\n",
                      gOk ? "OK" : "FAIL", (int)afterNew, (int)afterOpen, (int)afterRealOpen);
               allOk = allOk && gOk;
            }
         }

         tr.SetOfflineMode(false);
         tr.SetPlaying(true);
         gAudioMode = savedMode;
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         RebuildAudioTopology();

         printf("arrange audio test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Arrangement Timeline Audio Sample timing, measured on real rendered
      // audio. A synthetic 100 BPM click track (44.1 kHz stereo, so the
      // file/engine rate conversion is exercised too, and 10 s long so it is
      // not an exact loop length) goes through the real drop path
      // (ArrangeImportMediaFile + ArrangePollMediaImports), then every case
      // renders through RebuildAudioTopology + ProcessOffline and compares
      // each click's onset in the output against where the box says it
      // belongs. INFINITE_ARRANGESAMPLETEST=<dir> also writes the renders
      // there as WAVs for inspection.
      if (getenv("INFINITE_ARRANGESAMPLETEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         const std::string outDir = getenv("INFINITE_ARRANGESAMPLETEST");

         Transport& tr = Transport::Instance();
         const double kSr = 48000.0;
         const int kBlock = 256;
         const double kFileSr = 44100.0;
         const double kFileBpm = 100.0;
         const double kFileSeconds = 10.0;

         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();
         const AudioMode savedMode = gAudioMode;

         // Click track: a 6 ms 2 kHz burst on every beat, left and right
         // slightly different so a channel swap would show.
         const std::string wavPath = TmpPath("infinite_arrangesample_click.wav");
         {
            const int frames = (int)(kFileSeconds * kFileSr);
            std::vector<float> inter((size_t)frames * 2, 0.0f);
            const double beatSec = 60.0 / kFileBpm;
            for (int k = 0; k * beatSec < kFileSeconds; k++)
            {
               const int f0 = (int)std::llround(k * beatSec * kFileSr);
               for (int j = 0; j < (int)(0.006 * kFileSr) && f0 + j < frames; j++)
               {
                  const double env = std::exp(-(double)j / (0.0015 * kFileSr));
                  const float v = (float)(0.8 * env * std::sin(2.0 * 3.14159265358979 * 2000.0 * j / kFileSr));
                  inter[(size_t)(f0 + j) * 2] = v;
                  inter[(size_t)(f0 + j) * 2 + 1] = 0.9f * v;
               }
            }
            AudioRecordings::WriteWav(wavPath, inter.data(), frames, kFileSr, 2);
         }

         const uint64_t revBefore = gArrange.revision;
         gArrange = Arrange::Model();
         gArrange.revision = revBefore + 1;
         const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);

         gAudioMode = AudioMode::Timeline;
         tr.SetTempo(120.0f);
         Arrange::gSampleLiveTempoBpm = 120.0;
         tr.SetLoop(false, 0.0, 0.0);

         auto waitImports = [&]()
         {
            for (int i = 0; i < 500 && !gArrangePendingImports.empty(); i++)
            {
               ArrangePollMediaImports();
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return gArrangePendingImports.empty();
         };

         ArrangeImportMediaFile(wavPath, laneId, 0, Arrange::ImportMediaKind::Audio);
         const bool imported = waitImports();
         uint64_t clipId = 0;
         if (const Arrange::Lane* ln = Arrange::FindLane(gArrange, laneId))
            if (!ln->clips.empty())
               clipId = ln->clips.front().id;
         Arrange::Clip* c0 = Arrange::FindClip(gArrange, clipId);
         const bool importOk = imported && c0 != nullptr && !c0->importPending;
         printf("arrange sample import: %s\n", importOk ? "OK" : "FAIL");
         allOk = allOk && importOk;

         if (importOk)
         {
            // --- Estimate + drop defaults ------------------------------------
            {
               const double beats = Arrange::TicksToBeats(c0->length);
               const double expectBeats = kFileSeconds * kFileBpm / 60.0;
               const bool ok = std::fabs(c0->sampleBpm - kFileBpm) < 0.6 && c0->origBpm > 0.0f && c0->syncToTempo &&
                               std::fabs(beats - expectBeats) < 0.01;
               printf("arrange sample estimate: %s (sampleBpm %.2f, detected %.2f, sync %d, box %.3f beats, expected %.3f)\n",
                      ok ? "OK" : "FAIL", (double)c0->sampleBpm, (double)c0->origBpm, (int)c0->syncToTempo, beats, expectBeats);
               allOk = allOk && ok;
            }

            tr.SetPlaying(true);
            tr.SetOfflineMode(true, kSr);
            int cookFrame = 2000000;
            uint64_t lastDriftReseeks = 0;
            auto render = [&](double startBeat, double seconds, const char* name)
            {
               Arrange::gSampleLiveTempoBpm = (double)tr.Tempo();
               RebuildAudioTopology();
               cookFrame++;
               for (GraphNode& gn : gNodes)
                  gn.node->CookIfNeeded(cookFrame);
               // AudioNodeOfAny, not dynamic_cast: the sample player's
               // AudioNode lives inside its AudioFileNode, and a render that
               // skips preparing it runs at the wrong file/engine rate.
               for (GraphNode& gn : gNodes)
                  if (AudioNode* an = AudioNodeOfAny(gn.node.get()))
                     if (an->preparedForSampleRate != kSr)
                     {
                        an->PrepareToPlay(kSr, kAudioMaxBlockFrames);
                        an->preparedForSampleRate = kSr;
                     }
               tr.SeekBeats(startBeat);
               std::vector<float> ch0((size_t)kBlock), ch1((size_t)kBlock);
               float* chans[2] = { ch0.data(), ch1.data() };
               AudioBuffer buffer;
               buffer.channels = chans;
               buffer.numChannels = 2;
               buffer.numFrames = kBlock;
               std::vector<float> inter;
               const uint64_t reseeksBefore = gClipSampleDriftReseeks.load();
               const int blocks = (int)std::ceil(seconds * kSr / kBlock);
               for (int b = 0; b < blocks; b++)
               {
                  ArrangeAudioRebuildIfStale();
                  AudioEngine::Instance().ProcessOffline(buffer);
                  for (int i = 0; i < kBlock; i++)
                  {
                     inter.push_back(ch0[(size_t)i]);
                     inter.push_back(ch1[(size_t)i]);
                  }
               }
               lastDriftReseeks = gClipSampleDriftReseeks.load() - reseeksBefore;
               if (!outDir.empty())
                  AudioRecordings::WriteWav(outDir + "/" + name + ".wav", inter.data(),
                                            (int)(inter.size() / 2), kSr, 2);
               return inter;
            };

            // Click times (seconds from render start), left channel: each
            // click is a group of samples over 0.03 separated by >= 150 ms
            // under 0.01, timed at its loudest sample. The peak, not the
            // first sample over a threshold, so a stretcher's windowed
            // pre-echo or the 2 ms clip-edge declick ramp can't bias it.
            auto onsets = [&](const std::vector<float>& inter)
            {
               std::vector<double> out;
               const long long frames = (long long)(inter.size() / 2);
               long long i = 0;
               while (i < frames)
               {
                  if (std::fabs(inter[(size_t)i * 2]) < 0.03f) { i++; continue; }
                  long long peakAt = i, lastLoud = i;
                  float peak = 0.0f;
                  for (long long j = i; j < frames && j - lastLoud < (long long)(0.15 * kSr); j++)
                  {
                     const float v = std::fabs(inter[(size_t)j * 2]);
                     if (v >= 0.01f) lastLoud = j;
                     if (v > peak) { peak = v; peakAt = j; }
                  }
                  out.push_back((double)peakAt / kSr);
                  i = lastLoud + (long long)(0.15 * kSr);
               }
               return out;
            };

            // Compares measured onsets against the expected times inside
            // [0, until) seconds; reports count and worst error.
            auto check = [&](const char* label, const std::vector<float>& inter,
                             const std::vector<double>& expected, double tolMs)
            {
               // Same end cut-off expectedClicks applies.
               const double renderSeconds = (double)(inter.size() / 2) / kSr;
               std::vector<double> got;
               for (double g : onsets(inter))
                  if (g < renderSeconds - 0.02)
                     got.push_back(g);
               double worst = 0.0;
               int matched = 0;
               for (double e : expected)
               {
                  double best = 1e9;
                  for (double g : got)
                     best = std::min(best, std::fabs(g - e));
                  if (best < 0.05)
                     matched++;
                  // A click right on a clip's start edge goes through the
                  // engine's 2 ms declick ramp, which moves its peak later.
                  const bool onEdge = e < 0.003;
                  worst = std::max(worst, onEdge ? std::max(0.0, best - 0.002) : best);
               }
               const bool countOk = got.size() == expected.size();
               const bool ok = countOk && matched == (int)expected.size() && worst * 1000.0 <= tolMs &&
                               lastDriftReseeks == 0;
               printf("arrange sample %s: %s (expected %d clicks, got %d, worst error %.2f ms, tol %.1f, drift reseeks %llu)\n",
                      label, ok ? "OK" : "FAIL", (int)expected.size(), (int)got.size(), worst * 1000.0, tolMs,
                      (unsigned long long)lastDriftReseeks);
               if (!ok)
               {
                  printf("  [diag] got:");
                  for (size_t i = 0; i < got.size() && i < 40; i++) printf(" %.4f", got[i]);
                  printf("\n  [diag] expected:");
                  for (size_t i = 0; i < expected.size() && i < 40; i++) printf(" %.4f", expected[i]);
                  printf("\n");
               }
               allOk = allOk && ok;
            };

            // Expected click times for a render starting at `startBeat` of
            // `seconds`: clicks sit at source beats k (source second k*0.6),
            // placed on the timeline at clipStart + (srcSec - offset) *
            // effBpm / 60 beats, inside the box only.
            auto expectedClicks = [&](uint64_t id, double startBeat, double seconds)
            {
               std::vector<double> out;
               const Arrange::Clip* c = Arrange::FindClip(gArrange, id);
               const double tempo = (double)tr.Tempo();
               const double eff = Arrange::SampleSourceBpm(c->syncToTempo, c->sampleBpm, tempo);
               for (int k = 0; k * 60.0 / kFileBpm < kFileSeconds; k++)
               {
                  const double srcSec = k * 60.0 / kFileBpm - c->sourceOffsetSeconds;
                  if (srcSec < -1e-9) continue;
                  const double beat = Arrange::TicksToBeats(c->start) + srcSec * eff / 60.0;
                  if (beat < Arrange::TicksToBeats(c->start) - 1e-9 || beat >= Arrange::TicksToBeats(c->End()) - 1e-6)
                     continue;
                  const double t = (beat - startBeat) * 60.0 / tempo;
                  if (t >= -1e-9 && t < seconds - 0.02)
                     out.push_back(std::max(0.0, t));
               }
               return out;
            };

            // Click peak sits ~0.125 ms into the synthetic click. Direct
            // (unstretched) reads are interpolation-exact; WSOLA may move a
            // transient by up to its +/-128-frame alignment search.
            const double tolDirect = 0.5;
            const double tol = 6.0;

            // Synced at 120 (sample is 100): stretched to 1.2x, beat k at k*0.5 s.
            tr.SetTempo(120.0f);
            {
               auto x = render(0.0, 8.0, "synced_120");
               check("synced @120", x, expectedClicks(clipId, 0.0, 8.0), tol);
            }
            // Synced at 140: tempo change alone moves every click.
            tr.SetTempo(140.0f);
            {
               auto x = render(0.0, 7.0, "synced_140");
               check("synced @140", x, expectedClicks(clipId, 0.0, 7.0), tol);
            }
            // Play from mid-clip.
            {
               auto x = render(5.3, 4.0, "synced_140_from_5.3");
               check("synced @140 from beat 5.3", x, expectedClicks(clipId, 5.3, 4.0), tol);
            }
            // Sync off at 120: box rescales to keep the same audio, and the
            // audio plays at native speed (one click per 0.6 s).
            tr.SetTempo(120.0f);
            Arrange::gSampleLiveTempoBpm = 120.0;
            {
               const Arrange::Tick before = Arrange::FindClip(gArrange, clipId)->length;
               ArrangeSetSampleSync(clipId, false);
               const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
               const bool ok = !c->syncToTempo && std::llabs(c->length - (Arrange::Tick)std::llround(before * 120.0 / 100.0)) <= 1;
               printf("arrange sample sync off keeps audio in box: %s (%.3f -> %.3f beats)\n", ok ? "OK" : "FAIL",
                      Arrange::TicksToBeats(before), Arrange::TicksToBeats(c->length));
               allOk = allOk && ok;
               auto x = render(0.0, 11.0, "unsynced_120");
               check("unsynced @120 (native speed, whole file)", x, expectedClicks(clipId, 0.0, 11.0), tolDirect);
            }
            // Unsynced, Sample BPM typed: must change nothing audible.
            {
               const Arrange::Tick before = Arrange::FindClip(gArrange, clipId)->length;
               ArrangeSetSampleBpm(clipId, 50.0f);
               const bool ok = Arrange::FindClip(gArrange, clipId)->length == before;
               printf("arrange sample unsynced BPM edit leaves box: %s\n", ok ? "OK" : "FAIL");
               allOk = allOk && ok;
               auto x = render(0.0, 11.0, "unsynced_120_bpm50");
               check("unsynced @120 after Sample BPM edit", x, expectedClicks(clipId, 0.0, 11.0), tolDirect);
            }
            // Unsynced at 150: native speed still, box fixed at 20 beats =
            // 8 s, so the file's last 2 s are cut at the box end.
            tr.SetTempo(150.0f);
            {
               auto x = render(0.0, 11.0, "unsynced_150");
               check("unsynced @150 (tail cut at box end)", x, expectedClicks(clipId, 0.0, 11.0), tolDirect);
            }
            // Back to synced, then Sample BPM 50: the clip now claims to be
            // 50 BPM, so at 120 it plays 2.4x and the box halves to 8.33 beats.
            tr.SetTempo(120.0f);
            Arrange::gSampleLiveTempoBpm = 120.0;
            {
               ArrangeSetSampleSync(clipId, true);   // eff 120 -> 50
               ArrangeSetSampleBpm(clipId, 100.0f); // back to the real tempo
               const Arrange::Clip* c = Arrange::FindClip(gArrange, clipId);
               const double beats = Arrange::TicksToBeats(c->length);
               const bool ok = std::fabs(beats - 16.6667) < 0.02;
               printf("arrange sample sync round trip restores box: %s (%.3f beats)\n", ok ? "OK" : "FAIL", beats);
               allOk = allOk && ok;
               ArrangeSetSampleBpm(clipId, 50.0f);
               auto x = render(0.0, 8.0, "synced_120_bpm50");
               check("synced @120, Sample BPM 50 (2.4x, click per half beat)", x, expectedClicks(clipId, 0.0, 8.0), tol);
               ArrangeSetSampleBpm(clipId, 100.0f);
            }
            // Pitch +7 st while synced: time-preserving, clicks stay put.
            {
               Arrange::FindClip(gArrange, clipId)->pitch = 7.0f;
               gArrange.revision++;
               auto x = render(0.0, 8.0, "synced_120_pitch7");
               check("synced @120, pitch +7 (timing unchanged)", x, expectedClicks(clipId, 0.0, 8.0), tol);
               Arrange::FindClip(gArrange, clipId)->pitch = 0.0f;
               gArrange.revision++;
            }
            // Split at beat 6.5 (clone node for the right half, like the
            // blade tool), then a paste of the whole original at beat 20.
            {
               uint64_t rightId = 0;
               Arrange::Split(gArrange, clipId, Arrange::BeatsToTicks(6.5), &rightId);
               ArrangeRespawnCloneNode(rightId);
               Arrange::Clip pasted = *Arrange::FindClip(gArrange, clipId);
               pasted.id = 0;
               pasted.start = Arrange::BeatsToTicks(20.0);
               pasted.length = Arrange::BeatsToTicks(10.0);
               pasted.sourceOffsetSeconds = 0.0f;
               uint64_t pasteId = 0;
               Arrange::PlaceOverwrite(gArrange, laneId, pasted, &pasteId);
               ArrangeRespawnCloneNode(pasteId);
               // The crash path: rebuild repeatedly while the clones decode.
               for (int i = 0; i < 20; i++)
               {
                  gArrange.revision++;
                  ArrangeAudioRebuildIfStale();
               }
               const bool clonesOk = waitImports() && Arrange::FindClip(gArrange, rightId) != nullptr &&
                                     Arrange::FindClip(gArrange, pasteId) != nullptr;
               printf("arrange sample split + paste clones decoded: %s\n", clonesOk ? "OK" : "FAIL");
               allOk = allOk && clonesOk;
               if (clonesOk)
               {
                  auto x = render(0.0, 15.5, "split_and_paste");
                  std::vector<double> e = expectedClicks(clipId, 0.0, 15.5);
                  for (double t : expectedClicks(rightId, 0.0, 15.5)) e.push_back(t);
                  for (double t : expectedClicks(pasteId, 0.0, 15.5)) e.push_back(t);
                  std::sort(e.begin(), e.end());
                  check("split + paste @120", x, e, tol);
               }
            }

            // Static waveform lines up with the audio: the bucket holding
            // each click's box position has a peak, the buckets between do not.
            {
               ArrangeSyncClipVisuals();
               const auto it = gArrangeSampleStaticWaves.find(clipId);
               bool ok = it != gArrangeSampleStaticWaves.end();
               int hits = 0, clicks = 0;
               if (ok)
               {
                  const ArrangeClipWave& w = it->second;
                  for (double t : expectedClicks(clipId, 0.0, 1000.0))
                  {
                     const double beat = t * 120.0 / 60.0;
                     const int b = (int)(Arrange::BeatsToTicks(beat) / kArrangeWaveBucketTicks);
                     clicks++;
                     if (b >= 0 && b < (int)w.maxv.size() && w.maxv[(size_t)b] > 0.3f)
                        hits++;
                  }
                  ok = clicks > 0 && hits == clicks;
               }
               printf("arrange sample static waveform aligned: %s (%d/%d clicks in their bucket)\n", ok ? "OK" : "FAIL",
                      hits, clicks);
               allOk = allOk && ok;
            }
         }

         tr.SetOfflineMode(false);
         tr.SetPlaying(true);
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }
         // The paste crash: several topology publishes landing inside one
         // real audio callback used to free a ProcessList the callback was
         // still walking. Hammer SetTopology with the device running and the
         // Sample clips playing; a regression shows up as a crash (or an ASan
         // report), not as a FAIL line.
         if (AudioEngine::Instance().SampleRate() > 0.0 || StartAudioEngine(gAudioStartError))
         {
            gAudioMode = AudioMode::Timeline;
            RebuildAudioTopology();
            tr.SeekBeats(0.0);
            for (int i = 0; i < 400; i++)
            {
               RebuildAudioTopology();
               if (i % 8 == 0)
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AudioEngine::Instance().PumpMainThread();
            printf("arrange sample live rebuild stress: OK (400 publishes under a running device at %.0f Hz)\n",
                   AudioEngine::Instance().SampleRate());
            if (!hadEngine)
            {
               AudioEngine::Instance().Stop();
               tr.NotifyAudioEngineStopped();
            }
         }
         else
            printf("arrange sample live rebuild stress: SKIP (no audio device)\n");
         gAudioMode = savedMode;
         RebuildAudioTopology();
         printf("arrange sample test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // Arrangement Timeline Sample through the REAL export path (symptom
      // "offline render/export is untested"): the render queue, the WAV
      // writer, the MP4 take through gArrangeTimelineExportNode and
      // CompositeArrangeTimelineVideo, with audio gated by
      // ArrangeTimelineRoutingActive. A 100 BPM click track synced at 120
      // starts at beat 2 (1.0 s) on an audio lane and a Ramp clip starts at
      // the same beat on a video lane, so in both files the first click and
      // the first non-black frame belong at exactly 1.0 s and every click
      // after it on a 0.5 s grid. The fixture only produces the files
      // (INFINITE_ARRANGESAMPLEEXPORTTEST=<dir>); measuring them is ffmpeg's
      // job, outside the app, so the check can't share the app's arithmetic.
      FrameTest_ARRANGESAMPLEEXPORTTEST(frameId, window);

      // Overhaul WP4: the arrangement video compositor. Lane order (top lane
      // frontmost), skip rules (disabled, unassigned), model opacity, and the
      // geometry-clip cache (no FBO allocation in steady state, per-target
      // keying, eviction, gPanelViewports untouched). Runs whole inside one
      // main-loop frame on fixture-owned targets, so neither the panel nor a
      // render has to be open.
      FrameTest_ARRANGEVIDEOTEST(frameId, window);

      // Overhaul WP5a (docs/plans/arrangement/overhaul-prompt.md): the panel's
      // editing layer, driven through the same helpers the keys, clicks and
      // menus call (ArrangeClickSelect, ArrangeDrag*, ArrangeCopySelection,
      // AddNodeToArrangeTimeline, ...) - never by UI scripting. Checks that
      // selection is by id (survives a lane reorder, a node delete and its
      // undo; a vanished id clears rather than landing on another clip), that
      // group gestures keep the model valid, that `0` and every gesture leave
      // exactly one undo entry (a no-move click none), that the clipboard
      // belongs to its document, and where Add to Timeline puts a clip.
      if (getenv("INFINITE_ARRANGEEDITTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         const Arrange::Tick kBar = Arrange::kTicksPerBar;
         std::string why;

         // A clean model with `video` video lanes then `audio` audio lanes.
         auto freshModel = [&](int video, int audio)
         {
            ArrangeEdit([&]()
            {
               while (!gArrange.lanes.empty())
                  Arrange::RemoveLane(gArrange, gArrange.lanes.front().id);
               for (int i = 0; i < video; i++)
                  Arrange::AddLane(gArrange, Arrange::kLaneVideo);
               for (int i = 0; i < audio; i++)
                  Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            });
            gArrangeSel.clear();
            gArrangeSelAnchor = 0;
         };
         auto place = [&](int lane, Arrange::Tick start, Arrange::Tick len, uint64_t uid)
         {
            Arrange::Clip c;
            c.start = start;
            c.length = len;
            c.srcUid = uid;
            uint64_t id = 0;
            ArrangeEdit([&]() { Arrange::PlaceOverwrite(gArrange, gArrange.lanes[lane].id, c, &id); });
            return id;
         };
         auto selIs = [&](std::set<uint64_t> want)
         {
            const std::vector<uint64_t> ids = ArrangeSelectionIds();
            return std::set<uint64_t>(ids.begin(), ids.end()) == want;
         };

         // --- A. Selection by id: lane reorder, node delete, undo -----------
         {
            GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            const uint64_t cubeUid = cube ? cube->uid : 0;
            const int cubeIndex = cube ? cube->index : -1;
            GraphNode* sphere = SpawnNode("Sphere", "3D", 200.0f, 0.0f);
            const uint64_t sphereUid = sphere ? sphere->uid : 0;
            bool aOk = cubeUid != 0 && sphereUid != 0;
            if (aOk)
            {
               freshModel(2, 0);
               const uint64_t laneV1 = gArrange.lanes[0].id;
               const uint64_t laneV2 = gArrange.lanes[1].id;
               const uint64_t c1 = place(0, 0, kBar, cubeUid);
               const uint64_t c2 = place(1, 0, kBar, sphereUid);
               const uint64_t c3 = place(0, kBar * 2, kBar, sphereUid);
               ArrangeClickSelect(c1, false, false);
               ArrangeClickSelect(c2, true, false);
               aOk = selIs({ c1, c2 }) && gArrangeSelAnchor == c2;

               // Reorder: the ids follow their clips to the new lane indices.
               ArrangeEdit([&]() { Arrange::ReorderLane(gArrange, laneV2, 0); });
               aOk = aOk && gArrange.lanes[0].id == laneV2 && selIs({ c1, c2 }) &&
                     Arrange::Find(gArrange, c1).lane == Arrange::LaneIndex(gArrange, laneV1) &&
                     Arrange::Find(gArrange, c2).lane == Arrange::LaneIndex(gArrange, laneV2);

               // Node delete: c1 goes offline (srcUid 0), stays selected.
               RemoveNodeByIndex(cubeIndex);
               aOk = aOk && selIs({ c1, c2 }) && Arrange::FindClip(gArrange, c1) != nullptr &&
                     Arrange::FindClip(gArrange, c1)->srcUid == 0;

               // Undo (a graph entry): the same clips, the link restored.
               Undo();
               aOk = aOk && selIs({ c1, c2 }) && Arrange::FindClip(gArrange, c1) != nullptr &&
                     Arrange::FindClip(gArrange, c1)->srcUid == cubeUid && FindNodeByUid(cubeUid) != nullptr &&
                     Arrange::Find(gArrange, c3).Valid();

               // An id that stops resolving clears; it never retargets.
               ArrangeClickSelect(c3, false, false);
               ArrangeDuplicateSelection();
               const std::vector<uint64_t> dup = ArrangeSelectionIds();
               aOk = aOk && dup.size() == 1 && dup[0] != c3;
               Undo();
               aOk = aOk && ArrangeSelectionIds().empty() && gArrangeSelAnchor == 0;
               aOk = aOk && Arrange::Validate(gArrange, &why);
            }
            printf("arrange edit select by id: %s\n", aOk ? "OK" : "FAIL");
            allOk = allOk && aOk;
         }

         // --- B. Group move, duplicate, delete, edge trim keep Validate -----
         {
            freshModel(2, 1);
            const uint64_t g1 = place(0, 0, kBar, 0);
            const uint64_t g2 = place(1, kBar, kBar, 0);
            const uint64_t x = place(0, kBar * 8, kBar, 0);
            const uint64_t aud = place(2, 0, kBar, 0);
            ArrangeClickSelect(g1, false, false);
            ArrangeClickSelect(g2, true, false);
            bool bOk = ArrangeGroupSelection();
            const uint64_t gid = Arrange::FindClip(gArrange, g1)->groupId;
            bOk = bOk && gid != 0 && Arrange::FindClip(gArrange, g2)->groupId == gid;

            // A plain click on one member selects the whole group; Alt-click one.
            ArrangeClickSelect(g2, false, true);
            bOk = bOk && selIs({ g2 });
            ArrangeClickSelect(g1, false, false);
            bOk = bOk && selIs({ g1, g2 });

            // Move: the whole group, one undo entry.
            size_t undoBefore = gUndoStack.size();
            ArrangeDragBegin(kArrangeDragMove, g1, Arrange::kEdgeStart, 0);
            ArrangeDragUpdate(kBar, 0);
            ArrangeDragUpdate(kBar * 3, 0);
            const bool pushed = ArrangeDragEnd();
            bOk = bOk && pushed && gUndoStack.size() == undoBefore + 1 &&
                  Arrange::FindClip(gArrange, g1)->start == kBar * 3 &&
                  Arrange::FindClip(gArrange, g2)->start == kBar * 4 && Arrange::Validate(gArrange, &why);

            // A lane delta that would put a member on the audio lane (or off
            // the end) is refused as a whole; the time delta still applies.
            ArrangeDragBegin(kArrangeDragMove, g1, Arrange::kEdgeStart, 0);
            ArrangeDragUpdate(kBar, 1);
            ArrangeDragEnd();
            bOk = bOk && Arrange::Find(gArrange, g1).lane == 0 && Arrange::Find(gArrange, g2).lane == 1 &&
                  Arrange::FindClip(gArrange, g1)->start == kBar * 4 && Arrange::FindClip(gArrange, aud)->start == 0 &&
                  Arrange::Validate(gArrange, &why);

            // Group edge trim: only the member flush with the end moves.
            ArrangeDragBegin(kArrangeDragGroupEdge, g2, Arrange::kEdgeEnd, 0);
            ArrangeDragUpdate(kBar * 5 + kBar / 2, 0);
            ArrangeDragEnd();
            bOk = bOk && Arrange::FindClip(gArrange, g1)->length == kBar &&
                  Arrange::FindClip(gArrange, g2)->End() == kBar * 5 + kBar / 2 && Arrange::Validate(gArrange, &why);

            // Duplicate: a new block, its own new group.
            ArrangeClickSelect(g1, false, false);
            bOk = bOk && ArrangeDuplicateSelection();
            const std::vector<uint64_t> copies = ArrangeSelectionIds();
            bOk = bOk && copies.size() == 2 && Arrange::Validate(gArrange, &why);
            if (copies.size() == 2)
            {
               const uint64_t ng = Arrange::FindClip(gArrange, copies[0])->groupId;
               bOk = bOk && ng != 0 && ng != gid && Arrange::FindClip(gArrange, copies[1])->groupId == ng;
            }

            // Delete a group by clicking one member: both go.
            if (!copies.empty())
               ArrangeClickSelect(copies[0], false, false);
            bOk = bOk && ArrangeDeleteSelection() && Arrange::Validate(gArrange, &why);
            for (uint64_t id : copies)
               bOk = bOk && !Arrange::Find(gArrange, id).Valid();
            bOk = bOk && Arrange::Find(gArrange, g1).Valid() && Arrange::Find(gArrange, x).Valid();

            // Ungroup.
            ArrangeClickSelect(g1, false, false);
            bOk = bOk && ArrangeUngroupSelection() && Arrange::FindClip(gArrange, g1)->groupId == 0 &&
                  Arrange::FindClip(gArrange, g2)->groupId == 0 && Arrange::Validate(gArrange, &why);
            printf("arrange edit group ops: %s\n", bOk ? "OK" : "FAIL");
            allOk = allOk && bOk;
         }

         // --- C. `0` toggles enabled, undoably ------------------------------
         {
            freshModel(1, 0);
            const uint64_t a = place(0, 0, kBar, 0);
            const uint64_t b = place(0, kBar, kBar, 0);
            ArrangeClickSelect(a, false, false);
            ArrangeClickSelect(b, true, false);
            const size_t undoBefore = gUndoStack.size();
            bool cOk = ArrangeToggleEnabledSelection() && gUndoStack.size() == undoBefore + 1 &&
                       !Arrange::FindClip(gArrange, a)->enabled && !Arrange::FindClip(gArrange, b)->enabled;
            Undo();
            cOk = cOk && Arrange::FindClip(gArrange, a)->enabled && Arrange::FindClip(gArrange, b)->enabled;
            // Mixed selection: one press disables all of it.
            ArrangeEdit([&]() { Arrange::SetEnabled(gArrange, { a }, Arrange::kDisable); });
            cOk = cOk && ArrangeToggleEnabledSelection() && !Arrange::FindClip(gArrange, b)->enabled;
            cOk = cOk && ArrangeToggleEnabledSelection() && Arrange::FindClip(gArrange, a)->enabled &&
                  Arrange::FindClip(gArrange, b)->enabled && Arrange::Validate(gArrange, &why);
            printf("arrange edit enable toggle: %s\n", cOk ? "OK" : "FAIL");
            allOk = allOk && cOk;
         }

         // --- D. A gesture that changes nothing pushes nothing --------------
         {
            freshModel(1, 0);
            const uint64_t a = place(0, 0, kBar, 0);
            place(0, kBar * 2, kBar, 0);
            ArrangeClickSelect(a, false, false);
            const size_t undoBefore = gUndoStack.size();
            const uint64_t revBefore = gArrange.revision;
            // A click: begin, no mouse movement, release.
            ArrangeDragBegin(kArrangeDragMove, a, Arrange::kEdgeStart, 0);
            bool dOk = !ArrangeDragEnd();
            // A drag that goes away and comes back.
            ArrangeDragBegin(kArrangeDragMove, a, Arrange::kEdgeStart, 0);
            ArrangeDragUpdate(kBar / 2, 0);
            ArrangeDragUpdate(0, 0);
            dOk = dOk && !ArrangeDragEnd();
            // A trim that goes nowhere, and an empty popup-field gesture.
            ArrangeDragBegin(kArrangeDragTrimEnd, a, Arrange::kEdgeEnd, kBar);
            ArrangeDragUpdate(kBar + kBar / 4, 0);
            ArrangeDragUpdate(kBar, 0);
            dOk = dOk && !ArrangeDragEnd();
            ArrangeGestureBegin();
            dOk = dOk && !ArrangeGestureEnd();
            dOk = dOk && gUndoStack.size() == undoBefore && Arrange::FindClip(gArrange, a)->start == 0 &&
                  Arrange::FindClip(gArrange, a)->length == kBar && gArrange.revision >= revBefore;
            printf("arrange edit no-move click: %s (undo %zu -> %zu)\n", dOk ? "OK" : "FAIL", undoBefore,
                   gUndoStack.size());
            allOk = allOk && dOk;
         }

         // --- E. Clipboard: paste at the playhead; cleared on New and Open --
         {
            freshModel(2, 0);
            const uint64_t a = place(0, 0, kBar, 0);
            const uint64_t b = place(1, kBar, kBar, 0);
            ArrangeClickSelect(a, false, false);
            ArrangeClickSelect(b, true, false);
            ArrangeGroupSelection();
            ArrangeClickSelect(a, false, false);
            bool eOk = ArrangeCopySelection() && gArrangeClipboard.items.size() == 2;
            eOk = eOk && ArrangePasteAt(kBar * 4);
            const std::vector<uint64_t> pasted = ArrangeSelectionIds();
            eOk = eOk && pasted.size() == 2 && Arrange::Validate(gArrange, &why);
            if (pasted.size() == 2)
            {
               const Arrange::Clip* p0 = Arrange::FindClip(gArrange, pasted[0]);
               const Arrange::Clip* p1 = Arrange::FindClip(gArrange, pasted[1]);
               const Arrange::Tick lo = std::min(p0->start, p1->start);
               eOk = eOk && lo == kBar * 4 && p0->groupId != 0 && p0->groupId == p1->groupId &&
                     p0->groupId != Arrange::FindClip(gArrange, a)->groupId;
            }
            // An undo is not a new document: the clipboard stays.
            Undo();
            eOk = eOk && !gArrangeClipboard.items.empty();
            // Open is: save, reload, and the clipboard is gone.
            const std::string path = TmpPath("arrange_edittest_open.inf");
            SavePatchTo(path);
            LoadPatchFrom(path);
            std::remove(path.c_str());
            eOk = eOk && !ArrangePasteAt(0) && gArrangeClipboard.items.empty() && gArrangeSel.empty();
            // New is too.
            freshModel(1, 0);
            place(0, 0, kBar, 0);
            ArrangeClickSelect(gArrange.lanes[0].clips[0].id, false, false);
            eOk = eOk && ArrangeCopySelection() && !gArrangeClipboard.items.empty();
            NewPatch();
            eOk = eOk && !ArrangePasteAt(0) && gArrangeClipboard.items.empty() && gArrangeSel.empty();
            printf("arrange edit clipboard cleared on new: %s\n", eOk ? "OK" : "FAIL");
            allOk = allOk && eOk;
         }

         // --- F. Add to Timeline picks the lane (and output) ----------------
         {
            NewPatch();
            tr.Seek(0.0);
            freshModel(0, 0);
            GraphNode* video = SpawnNode("Video", "Source", 0.0f, 0.0f);
            const int videoIndex = video ? video->index : -1;
            const uint64_t videoUid = video ? video->uid : 0;
            GraphNode* osc = SpawnNode("Oscillator", "Synthesizers", 300.0f, 0.0f);
            const int oscIndex = osc ? osc->index : -1;
            const uint64_t oscUid = osc ? osc->uid : 0;
            GraphNode* cube = SpawnNode("Cube", "3D", 600.0f, 0.0f);
            const uint64_t cubeUid = cube ? cube->uid : 0;
            bool fOk = videoIndex >= 0 && oscIndex >= 0 && cubeUid != 0;
            if (fOk)
            {
               GraphNode* vgn = FindNodeByIndex(videoIndex);
               fOk = IsNodeVideoCompatible(*vgn) && IsNodeAudioCompatible(*vgn) &&
                     ArrangeLaneTypeForNode(*vgn) == Arrange::kLaneVideo;

               const uint64_t v = AddNodeToArrangeTimeline(videoIndex);                        // natural: video
               const uint64_t va = AddNodeToArrangeTimeline(videoIndex, Arrange::kLaneAudio);  // submenu: audio
               const uint64_t v2 = AddNodeToArrangeTimeline(videoIndex, Arrange::kLaneVideo);  // lands after v
               const uint64_t o = AddNodeToArrangeTimeline(oscIndex);                          // audio only
               const Arrange::Loc lv = Arrange::Find(gArrange, v);
               const Arrange::Loc lva = Arrange::Find(gArrange, va);
               const Arrange::Loc lo = Arrange::Find(gArrange, o);
               fOk = fOk && lv.Valid() && lva.Valid() && lo.Valid() && Arrange::Find(gArrange, v2).Valid();
               if (fOk)
               {
                  const Arrange::Clip* cv = Arrange::FindClip(gArrange, v);
                  const Arrange::Clip* cva = Arrange::FindClip(gArrange, va);
                  const Arrange::Clip* cv2 = Arrange::FindClip(gArrange, v2);
                  const Arrange::Clip* co = Arrange::FindClip(gArrange, o);
                  fOk = gArrange.lanes[lv.lane].type == Arrange::kLaneVideo && cv->srcOutput == 0 &&
                        cv->srcUid == videoUid && cv->length == kBar && cv->start == 0 &&
                        gArrange.lanes[lva.lane].type == Arrange::kLaneAudio && cva->srcOutput == 1 &&
                        cva->srcUid == videoUid &&
                        cv2->start == cv->End() && Arrange::Find(gArrange, v2).lane == lv.lane &&
                        gArrange.lanes[lo.lane].type == Arrange::kLaneAudio && co->srcOutput == 0 &&
                        co->srcUid == oscUid && co->start == cva->End();
               }
               // Canvas Assign picker: by uid, type-checked, no-op pushes nothing.
               const size_t undoBefore = gUndoStack.size();
               fOk = fOk && !ArrangeAssignClipSource(o, cubeUid);             // cube has no audio
               fOk = fOk && !ArrangeAssignClipSource(o, oscUid);              // already that source
               fOk = fOk && gUndoStack.size() == undoBefore;
               fOk = fOk && ArrangeAssignClipSource(v, cubeUid) && gUndoStack.size() == undoBefore + 1 &&
                     Arrange::FindClip(gArrange, v)->srcUid == cubeUid;
               fOk = fOk && Arrange::Validate(gArrange, &why);
            }
            printf("arrange edit add to timeline lane pick: %s\n", fOk ? "OK" : "FAIL");
            allOk = allOk && fOk;
         }

         // --- G. Whole-group Group/Ungroup, blade, add-at-playhead ----------
         {
            freshModel(3, 0);
            auto gidOf = [&](uint64_t id) { return Arrange::FindClip(gArrange, id)->groupId; };
            auto groupAll = [&](std::vector<uint64_t> ids)
            {
               ArrangePruneSelection(); // settle a patch-generation reset first
               gArrangeSel.clear();
               gArrangeSel.insert(ids.begin(), ids.end());
               return ArrangeGroupSelection();
            };
            const uint64_t a = place(0, 0, kBar, 0);
            const uint64_t b = place(0, kBar * 2, kBar, 0);
            const uint64_t c = place(1, 0, kBar, 0);
            const uint64_t d = place(1, kBar * 2, kBar, 0);
            const uint64_t e = place(2, 0, kBar, 0);
            bool gOk = groupAll({ a, b }) && groupAll({ c, d }) && gidOf(a) != gidOf(c);

            // Two groups merge into one; one undo entry.
            size_t undoBefore = gUndoStack.size();
            ArrangeClickSelect(a, false, false);
            ArrangeClickSelect(c, true, false);
            gOk = gOk && ArrangeCanGroupSelection() && ArrangeGroupSelection() &&
                  gUndoStack.size() == undoBefore + 1 && gidOf(a) != 0 && gidOf(a) == gidOf(b) &&
                  gidOf(a) == gidOf(c) && gidOf(a) == gidOf(d) && selIs({ a, b, c, d });
            const bool mergeOk = gOk;

            // Exactly one whole group: nothing to do, nothing pushed, same id.
            const uint64_t g4 = gidOf(a);
            undoBefore = gUndoStack.size();
            ArrangeClickSelect(b, false, false);
            gOk = gOk && !ArrangeCanGroupSelection() && !ArrangeGroupSelection() &&
                  gUndoStack.size() == undoBefore && gidOf(a) == g4;
            const bool noopOk = gOk;

            // A loose clip plus one member of a group: the whole group joins.
            ArrangeUngroupSelection();
            gOk = gOk && gidOf(a) == 0 && gidOf(d) == 0 && groupAll({ a, b, c });
            ArrangeClickSelect(a, false, true); // Alt: this member only
            ArrangeClickSelect(e, true, false);
            gOk = gOk && selIs({ a, e }) && ArrangeGroupSelection() && gidOf(e) != 0 &&
                  gidOf(e) == gidOf(a) && gidOf(e) == gidOf(b) && gidOf(e) == gidOf(c) && gidOf(d) == 0;
            const bool mixOk = gOk;

            // Ungroup from any one member dissolves the whole group.
            ArrangeClickSelect(c, false, true);
            gOk = gOk && ArrangeCanUngroupSelection() && ArrangeUngroupSelection() && gidOf(a) == 0 &&
                  gidOf(b) == 0 && gidOf(c) == 0 && gidOf(e) == 0 && !ArrangeCanUngroupSelection();
            const bool ungroupOk = gOk;

            // Blade on a grouped clip cuts every member at that tick, as one
            // undo entry; the right halves stay in the group.
            gOk = gOk && groupAll({ a, c });
            gArrangeSel.clear();
            undoBefore = gUndoStack.size();
            gOk = gOk && ArrangeBladeSplitAt(a, kBar / 4) && gUndoStack.size() == undoBefore + 1 &&
                  gArrange.lanes[0].clips.size() == 3 && gArrange.lanes[1].clips.size() == 3 &&
                  Arrange::FindClip(gArrange, a)->length == kBar / 4 &&
                  Arrange::FindClip(gArrange, c)->length == kBar / 4 && gArrangeSel.empty();
            if (gOk)
            {
               const Arrange::Clip& r0 = gArrange.lanes[0].clips[1];
               const Arrange::Clip& r1 = gArrange.lanes[1].clips[1];
               gOk = r0.start == kBar / 4 && r1.start == kBar / 4 && r0.groupId == gidOf(a) &&
                     r1.groupId == gidOf(a) && Arrange::Validate(gArrange, &why);
            }
            // Outside the clip (or on an edge) is not a cut.
            gOk = gOk && !ArrangeBladeSplitAt(b, kBar * 2) && !ArrangeBladeSplitAt(b, kBar * 5);
            const bool bladeOk = gOk;

            // Add to Timeline lands in the first one-bar gap at the playhead
            // and asks the panel to scroll to it.
            NewPatch();
            freshModel(1, 0);
            GraphNode* video = SpawnNode("Video", "Source", 0.0f, 0.0f);
            const int videoIndex = video ? video->index : -1;
            const uint64_t videoUid = video ? video->uid : 0;
            gOk = gOk && videoIndex >= 0;
            if (videoIndex >= 0)
            {
               place(0, 0, kBar, videoUid);
               place(0, kBar * 2, kBar, videoUid);
               tr.SeekBeats(2.0); // half a bar in, inside the first clip
               const uint64_t n1 = AddNodeToArrangeTimeline(videoIndex);
               tr.SeekBeats(0.0);
               const uint64_t n2 = AddNodeToArrangeTimeline(videoIndex);
               gOk = gOk && n1 != 0 && n2 != 0 && Arrange::FindClip(gArrange, n1)->start == kBar &&
                     Arrange::FindClip(gArrange, n2)->start == kBar * 3 && gArrangeRevealClipId == n2 &&
                     gArrangeFlashClipId == n2 && Arrange::Validate(gArrange, &why);
            }
            gArrangeRevealClipId = 0;
            gArrangeFlashClipId = 0;
            printf("arrange edit whole groups + blade + add at playhead: %s (merge %d noop %d mix %d ungroup %d blade %d)\n",
                   gOk ? "OK" : "FAIL", mergeOk, noopOk, mixOk, ungroupOk, bladeOk);
            allOk = allOk && gOk;
         }

         if (!why.empty())
            printf("arrange edit validate: %s\n", why.c_str());
         gArrangePanelOpen = false;
         NewPatch();
         printf("arrange edit test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // WP6: time display, snap grid, markers, playhead keys, scrub-on-release.
      // Drives the same functions the panel's keys and gestures call.
      FrameTest_ARRANGEMARKERTEST(frameId, window);

      // Overhaul WP7 (docs/plans/arrangement/overhaul-prompt.md): the export
      // queue. Checks the parts of a take that are decided before a single
      // frame or sample is written - the range a kind resolves to, the exact
      // frame and sample budgets that range implies (#2: a 2.4s range must
      // not round up to a 3s file), which terminals the audio comes out of
      // for each source combination, that a take never changes what the user
      // is monitoring, and the queue's own mechanics. Deliberately NOT a
      // full encode: a video take is pumped by the main loop a frame at a
      // time and this fixture lives inside one frame. The audio-only path
      // runs for real when a device can be opened, and says so when not.
      if (getenv("INFINITE_ARRANGERENDERTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetTempo(120.0f); // 1 beat = 0.5s, so every expected time below is exact
         tr.Seek(0.0);
         const AudioMode modeBefore = gAudioMode;

         // uid read right after each spawn, not after both: SpawnNode()
         // push_backs onto gNodes, which can reallocate and invalidate every
         // GraphNode* into it - including a pointer from an earlier spawn
         // still held when a later one runs.
         GraphNode* rampGn = SpawnNode("Ramp", "Source", 0.0f, 0.0f);
         const uint64_t rampUid = rampGn != nullptr ? rampGn->uid : 0;
         GraphNode* oscGn = SpawnNode("Oscillator", "Synthesizers", 200.0f, 0.0f);
         const uint64_t oscUid = oscGn != nullptr ? oscGn->uid : 0;
         const bool spawned = rampGn != nullptr && oscGn != nullptr;
         printf("arrange render spawn: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {

            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t vLane = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
            const uint64_t aLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
            auto place = [&](uint64_t lane, uint64_t uid, double startBeat, double lenBeats) {
               Arrange::Clip c;
               c.start = Arrange::BeatsToTicks(startBeat);
               c.length = Arrange::BeatsToTicks(lenBeats);
               c.srcUid = uid;
               Arrange::PlaceOverwrite(gArrange, lane, c);
            };
            // Video [0, 6) beats = [0, 3)s, audio [0, 8) beats = [0, 4)s.
            place(vLane, rampUid, 0.0, 6.0);
            place(aLane, oscUid, 0.0, 8.0);
            Arrange::AddMarker(gArrange, Arrange::BeatsToTicks(1.0), "A");
            Arrange::AddMarker(gArrange, Arrange::BeatsToTicks(5.8), "B"); // 2.4s after A
            gArrange.settings.loop.enabled = true;
            gArrange.settings.loop.start = Arrange::BeatsToTicks(2.0);
            gArrange.settings.loop.end = Arrange::BeatsToTicks(4.4); // 1.2s, fractional

            // --- A. Every range kind resolves to the span it names ---------
            struct RangeCase { int kind; double aBeat; double bBeat; const char* what; };
            const RangeCase cases[] = {
               { kArrangeRangeWhole,   0.0, 8.0, "whole" },      // the audio clip is the longest
               { kArrangeRangeLoop,    2.0, 4.4, "loop" },
               { kArrangeRangeMarkers, 1.0, 5.8, "markers" },
               { kArrangeRangeCustom,  3.0, 7.0, "custom" },
            };
            bool aOk = true;
            for (const RangeCase& rc : cases)
            {
               Arrange::Tick ra = 0, rb = 0;
               ArrangeRenderResolveRange(rc.kind, 0, 1, Arrange::BeatsToTicks(3.0),
                                         Arrange::BeatsToTicks(7.0), ra, rb);
               const bool ok = ra == Arrange::BeatsToTicks(rc.aBeat) && rb == Arrange::BeatsToTicks(rc.bBeat);
               if (!ok)
                  printf("arrange render range %s: FAIL (got %.3f..%.3f beats, want %.3f..%.3f)\n", rc.what,
                         Arrange::TicksToBeats(ra), Arrange::TicksToBeats(rb), rc.aBeat, rc.bBeat);
               aOk = aOk && ok;
            }
            // Reversed markers swap rather than producing a negative range.
            {
               Arrange::Tick ra = 0, rb = 0;
               ArrangeRenderResolveRange(kArrangeRangeMarkers, 1, 0, 0, 0, ra, rb);
               aOk = aOk && ra == Arrange::BeatsToTicks(1.0) && rb == Arrange::BeatsToTicks(5.8);
            }
            // An empty range is widened, never handed to the runner as-is.
            {
               Arrange::Tick ra = 0, rb = 0;
               ArrangeRenderResolveRange(kArrangeRangeCustom, 0, 0, Arrange::BeatsToTicks(2.0),
                                         Arrange::BeatsToTicks(2.0), ra, rb);
               aOk = aOk && rb > ra;
            }
            printf("arrange render range kinds: %s\n", aOk ? "OK" : "FAIL");
            allOk = allOk && aOk;

            // --- B. Frame budget is ceil, not whole seconds (#2) -----------
            // The marker range is 2.4s: 72 frames at 30fps, and the old
            // ceil(durationSeconds) * fps would have written 90 (a 3s file).
            const int f30 = ArrangeRenderFrameBudget(2.4, 30);
            const int f60 = ArrangeRenderFrameBudget(2.4, 60);
            const int fLoop = ArrangeRenderFrameBudget(1.2, 25);       // exact, no rounding
            const int fPartial = ArrangeRenderFrameBudget(1.201, 25);  // one frame more
            const int fTiny = ArrangeRenderFrameBudget(0.001, 1);      // never zero frames
            const bool bOk = f30 == 72 && f60 == 144 && fLoop == 30 && fPartial == 31 && fTiny == 1;
            printf("arrange render frame budget: %s (2.4s@30=%d 2.4s@60=%d 1.2s@25=%d 1.201s@25=%d 0.001s@1=%d)\n",
                   bOk ? "OK" : "FAIL", f30, f60, fLoop, fPartial, fTiny);
            allOk = allOk && bOk;

            // --- C. Sample budget rounds to the nearest whole sample -------
            const long long s48 = ArrangeRenderSampleBudget(2.4, 48000.0);
            const long long s441 = ArrangeRenderSampleBudget(2.4, 44100.0);
            const long long sTiny = ArrangeRenderSampleBudget(0.0, 48000.0);
            const bool cOk = s48 == 115200 && s441 == 105840 && sTiny == 1;
            printf("arrange render sample budget: %s (2.4s@48k=%lld 2.4s@44.1k=%lld)\n", cOk ? "OK" : "FAIL",
                   s48, s441);
            allOk = allOk && cOk;

            // --- D. The source matrix picks the right audio terminals ------
            // ArrangeTimelineRoutingActive() is the single gate: Timeline
            // audio must use the timeline's terminals even though the user is
            // monitoring the canvas, and canvas audio must not, even when the
            // take is compositing the timeline's video.
            gAudioMode = AudioMode::Canvas;
            struct MatrixCase { int audio; int video; bool wantTimeline; const char* what; };
            const MatrixCase matrix[] = {
               { kArrangeAudioTimeline, kArrangeVideoTimeline, true,  "timeline A + timeline V" },
               { kArrangeAudioCanvas,   kArrangeVideoTimeline, false, "canvas A + timeline V" },
               { kArrangeAudioTimeline, kArrangeVideoCanvas,   true,  "timeline A + canvas V" },
               { kArrangeAudioCanvas,   kArrangeVideoCanvas,   false, "canvas A + canvas V" },
               { kArrangeAudioNone,     kArrangeVideoTimeline, false, "no audio" },
            };
            bool dOk = true;
            for (const MatrixCase& mc : matrix)
            {
               gOfflineRender.active = true;
               gOfflineRender.arrangeDriven = true;
               gOfflineRender.timelineAudio = mc.audio == kArrangeAudioTimeline;
               gOfflineRender.timelineVideo = mc.video == kArrangeVideoTimeline;
               const bool got = ArrangeTimelineRoutingActive();
               if (got != mc.wantTimeline)
                  printf("arrange render routing %s: FAIL (got %d, want %d)\n", mc.what, got ? 1 : 0,
                         mc.wantTimeline ? 1 : 0);
               dOk = dOk && got == mc.wantTimeline;
            }
            // The audio-only path routes through its own flag, not gOfflineRender's.
            gOfflineRender.active = false;
            gOfflineRender.arrangeDriven = false;
            gOfflineRender.timelineAudio = false;
            gOfflineRender.timelineVideo = false;
            gArrangeWavRender.active = true;
            gArrangeWavRender.timelineAudio = true;
            dOk = dOk && ArrangeTimelineRoutingActive();
            gArrangeWavRender.timelineAudio = false;
            dOk = dOk && !ArrangeTimelineRoutingActive();
            gArrangeWavRender.active = false;
            // And with nothing rendering it is the monitoring mode alone.
            dOk = dOk && !ArrangeTimelineRoutingActive();
            gAudioMode = AudioMode::Timeline;
            dOk = dOk && ArrangeTimelineRoutingActive();
            gAudioMode = AudioMode::Canvas;
            printf("arrange render source matrix: %s\n", dOk ? "OK" : "FAIL");
            allOk = allOk && dOk;

            // --- E. The render dialog's sources --------------------------
            // The Audio/Video source dropdowns are gone: a timeline take is
            // always timeline audio, and it is a movie exactly when the range
            // covers video clips, otherwise a WAV. Calls the shipping helpers
            // rather than a local copy of their logic - the previous version
            // of this test asserted on a private lambda, so it kept passing
            // after the behaviour it described had been deleted.
            bool eOk = true;
            // Audio never follows the monitoring mode any more.
            gAudioMode = AudioMode::Canvas;
            eOk = eOk && ArrangeRenderEffectiveAudioSource() == kArrangeAudioTimeline;
            gAudioMode = AudioMode::Timeline;
            eOk = eOk && ArrangeRenderEffectiveAudioSource() == kArrangeAudioTimeline;
            gAudioMode = AudioMode::Canvas;
            // And the deprecated pinned setting no longer overrides it.
            gArrange.settings.renderAudioSource = kArrangeAudioCanvas;
            eOk = eOk && ArrangeRenderEffectiveAudioSource() == kArrangeAudioTimeline;
            gArrange.settings.renderAudioSource = -1;
            // Video follows the range. The fixture built above has no video
            // lane, so every range is audio-only; a range that covers nothing
            // is audio-only whatever the project holds.
            gArrange.settings.renderVideoSource = kArrangeVideoCanvas; // also ignored now
            eOk = eOk && ArrangeRenderEffectiveVideoSource(0, 0) == kArrangeVideoNone;
            eOk = eOk && ArrangeRenderEffectiveVideoSource(0, Arrange::kPPQ * 64) ==
                             (ArrangeRenderVideoClipsInRange(0, Arrange::kPPQ * 64) > 0
                                  ? kArrangeVideoTimeline
                                  : kArrangeVideoNone);
            gArrange.settings.renderVideoSource = -1;
            printf("arrange render effective sources: %s\n", eOk ? "OK" : "FAIL");
            allOk = allOk && eOk;

            // --- F. Queue mechanics ----------------------------------------
            const std::string tmpDir = std::filesystem::temp_directory_path().string();
            gArrangeRenderQueue.clear();
            gArrangeRenderActiveJobId = 0;
            gArrangeRenderQueueRunning = false;
            auto makeJob = [&](const char* name, int audio, int video, double aBeat, double bBeat) {
               ArrangeRenderJob j;
               j.id = gArrangeRenderNextJobId++;
               j.startTick = Arrange::BeatsToTicks(aBeat);
               j.endTick = Arrange::BeatsToTicks(bBeat);
               j.audioSource = audio;
               j.videoSource = video;
               j.path = tmpDir + "/infinite_wp7_" + name + (video == kArrangeVideoNone ? ".wav" : ".mp4");
               gArrangeRenderQueue.push_back(j);
               return gArrangeRenderQueue.back().id;
            };
            makeJob("whole", kArrangeAudioTimeline, kArrangeVideoTimeline, 0.0, 8.0);
            makeJob("loop", kArrangeAudioTimeline, kArrangeVideoTimeline, 2.0, 4.4);
            const uint64_t emptyId = makeJob("empty", kArrangeAudioTimeline, kArrangeVideoTimeline, 3.0, 3.0);
            const uint64_t bothNoneId = makeJob("none", kArrangeAudioNone, kArrangeVideoNone, 0.0, 4.0);

            // A job with nothing to do fails at the gate instead of arming a
            // take, and the runner moves on rather than stalling the queue.
            ArrangeRenderJob* emptyJob = ArrangeRenderFindJob(emptyId);
            ArrangeRenderJob* noneJob = ArrangeRenderFindJob(bothNoneId);
            const bool emptyRefused = emptyJob != nullptr && !ArrangeRenderBeginJob(*emptyJob) &&
                                      emptyJob->status == kArrangeJobFailed;
            const bool noneRefused = noneJob != nullptr && !ArrangeRenderBeginJob(*noneJob) &&
                                     noneJob->status == kArrangeJobFailed;
            gArrangeRenderActiveJobId = 0;
            printf("arrange render invalid jobs refused: %s (%s / %s)\n",
                   emptyRefused && noneRefused ? "OK" : "FAIL",
                   emptyJob != nullptr ? emptyJob->message.c_str() : "?",
                   noneJob != nullptr ? noneJob->message.c_str() : "?");
            allOk = allOk && emptyRefused && noneRefused;

            // Retry puts a failed job back in line with its counters reset.
            emptyJob->status = kArrangeJobFailed;
            emptyJob->framesDone = 17;
            emptyJob->status = kArrangeJobQueued;
            emptyJob->framesDone = 0;
            emptyJob->message.clear();

            // Cancel All stops the run and marks everything still waiting,
            // and leaves the finished ones alone.
            noneJob->status = kArrangeJobDone;
            gArrangeRenderQueueRunning = true;
            ArrangeRenderCancelAll();
            int cancelled = 0, done = 0, stillQueued = 0;
            for (const ArrangeRenderJob& j : gArrangeRenderQueue)
            {
               if (j.status == kArrangeJobCancelled) cancelled++;
               else if (j.status == kArrangeJobDone) done++;
               else if (j.status == kArrangeJobQueued) stillQueued++;
            }
            const bool fOk = !gArrangeRenderQueueRunning && cancelled == 3 && done == 1 && stillQueued == 0;
            printf("arrange render cancel all: %s (%d cancelled, %d done, %d still queued, running %d)\n",
                   fOk ? "OK" : "FAIL", cancelled, done, stillQueued, gArrangeRenderQueueRunning ? 1 : 0);
            allOk = allOk && fOk;

            // --- G. Two jobs never share an output file --------------------
            const std::string taken = gArrangeRenderQueue[0].path;
            gArrangeRenderQueue[0].status = kArrangeJobQueued; // back in the queue, so it owns its path
            const bool seen = ArrangeRenderPathQueued(taken, 0);
            const bool notMine = !ArrangeRenderPathQueued(taken, gArrangeRenderQueue[0].id);
            const std::string unique = ArrangeRenderUniquePath(taken);
            const bool gOk = seen && notMine && unique != taken && unique.size() > 4 &&
                             unique.compare(unique.size() - 4, 4, ".mp4") == 0 &&
                             !ArrangeRenderPathQueued(unique, 0);
            printf("arrange render unique path: %s (%s -> %s)\n", gOk ? "OK" : "FAIL", taken.c_str(),
                   unique.c_str());
            allOk = allOk && gOk;

            // --- H. A live source in the range refuses the take ------------
            // Off-range hardware must not refuse: that was the whole point of
            // scoping the check to the render range rather than the patch.
            GraphNode* camGn = SpawnNode("Video In", "Source", 400.0f, 0.0f);
            if (camGn != nullptr && camGn->node != nullptr && camGn->node->IsHardwareDriven())
            {
               Arrange::Clip cam;
               cam.start = Arrange::BeatsToTicks(10.0);
               cam.length = Arrange::BeatsToTicks(2.0);
               cam.srcUid = camGn->uid;
               Arrange::PlaceOverwrite(gArrange, vLane, cam);
               const bool inRange =
                  FindHardwareDrivenNodeInArrangeRange(Arrange::BeatsToTicks(10.0), Arrange::BeatsToTicks(12.0),
                                                       true, true) != nullptr;
               const bool outOfRange =
                  FindHardwareDrivenNodeInArrangeRange(Arrange::BeatsToTicks(0.0), Arrange::BeatsToTicks(6.0),
                                                       true, true) == nullptr;
               const bool hOk = inRange && outOfRange;
               printf("arrange render live source scoped to range: %s (in %d, out %d)\n", hOk ? "OK" : "FAIL",
                      inRange ? 1 : 0, outOfRange ? 1 : 0);
               allOk = allOk && hOk;
            }
            else
            {
               printf("arrange render live source scoped to range: SKIP (no hardware-driven node)\n");
            }

            // --- I. An audio-only take, for real, when a device exists -----
            // This is the only end-to-end path a single frame can run: no
            // encoder, no per-frame main-loop pump. Without a device there is
            // nothing to render at (every AudioNode is prepared at the device
            // rate), so it says so rather than failing.
            gArrangeRenderQueue.clear();
            gArrangeRenderActiveJobId = 0;
            gArrangeRenderQueueRunning = false;
            const std::string wavPath = tmpDir + "/infinite_wp7_audio_only.wav";
            std::error_code rmEc;
            std::filesystem::remove(wavPath, rmEc);
            if (AudioEngine::Instance().SampleRate() > 0.0 || StartAudioEngine(gAudioStartError))
            {
               const double devRate = AudioEngine::Instance().SampleRate();
               ArrangeRenderJob j;
               j.id = gArrangeRenderNextJobId++;
               j.startTick = Arrange::BeatsToTicks(1.0);
               j.endTick = Arrange::BeatsToTicks(5.8); // 2.4s
               j.audioSource = kArrangeAudioTimeline;
               j.videoSource = kArrangeVideoNone;
               j.format = 2;
               j.path = wavPath;
               gArrangeRenderQueue.push_back(j);
               gArrangeRenderQueueRunning = true;

               // What the main loop does, without the frames in between. The
               // cap is a hang guard: at a 0.1s budget per tick a 2.4s take
               // needs a handful.
               int ticks = 0;
               while (gArrangeRenderQueueRunning && ticks++ < 2000)
                  ArrangeRenderQueueTick();

               const ArrangeRenderJob& doneJob = gArrangeRenderQueue.back();
               const long long wantSamples = ArrangeRenderSampleBudget(2.4, devRate);
               long long gotSamples = -1;
               std::error_code szEc;
               const auto bytes = (long long)std::filesystem::file_size(wavPath, szEc);
               if (!szEc)
                  gotSamples = (bytes - 44) / 4; // 16-bit stereo after the canonical WAV header
               // +-1 block: the pump writes in whole blocks of
               // OfflineAudioBlockFrames() and the last one is clipped to the
               // budget, so the file is exact - the tolerance is for a writer
               // that pads, not for a pump that overruns.
               const bool iOk = doneJob.status == kArrangeJobDone && gotSamples > 0 &&
                                std::llabs(gotSamples - wantSamples) <= OfflineAudioBlockFrames() &&
                                !ArrangeRenderBusy() && gArrangeRenderActiveJobId == 0;
               printf("arrange render audio-only take: %s (%lld samples, want %lld at %.0f Hz, status %d, %d ticks)\n",
                      iOk ? "OK" : "FAIL", gotSamples, wantSamples, devRate, doneJob.status, ticks);
               allOk = allOk && iOk;
               std::filesystem::remove(wavPath, rmEc);
            }
            else
            {
               printf("arrange render audio-only take: SKIP (no audio device: %s)\n", gAudioStartError.c_str());
            }

            // --- K. A take parks the loop instead of wrapping inside it ----
            // A fractional loop used to make the take re-render the loop body
            // until the frame budget ran out (#3). WP2 moved the wrap into
            // Transport and suspends it for the duration of a take; the
            // user's own loop flag is left alone so it comes back after.
            tr.SetLoop(true, 2.0, 4.4);
            const bool loopOnBefore = tr.LoopEnabled();
            const bool suspendedBefore = tr.LoopSuspended();
            tr.SetOfflineMode(true, 48000.0);
            const bool suspendedDuring = tr.LoopSuspended();
            const bool flagKept = tr.LoopEnabled();
            tr.SetOfflineMode(false);
            const bool suspendedAfter = tr.LoopSuspended();
            const bool kOk = loopOnBefore && !suspendedBefore && suspendedDuring && flagKept &&
                             !suspendedAfter && tr.LoopEnabled();
            printf("arrange render loop parked during take: %s (before %d, during %d, after %d)\n",
                   kOk ? "OK" : "FAIL", suspendedBefore ? 1 : 0, suspendedDuring ? 1 : 0,
                   suspendedAfter ? 1 : 0);
            allOk = allOk && kOk;
            tr.SetLoop(false, 0.0, 0.0);

            // --- J. A take never changes what the user is monitoring -------
            const bool jOk = gAudioMode == modeBefore && !ArrangeRenderBusy() &&
                             !gOfflineRender.arrangeDriven && !gOfflineRender.timelineAudio &&
                             !gOfflineRender.timelineVideo && !gArrangeWavRender.active &&
                             !Transport::Instance().IsOfflineMode();
            printf("arrange render leaves live state alone: %s (mode %d, offline %d)\n", jOk ? "OK" : "FAIL",
                   (int)gAudioMode, Transport::Instance().IsOfflineMode() ? 1 : 0);
            allOk = allOk && jOk;

            // --- L. Renders follow the global audio settings ---------------
            // The popup offers no rate or buffer control; both are read off
            // the live engine, so a job can never ask for something the
            // prepared graph cannot generate.
            const double activeRate = ArrangeRenderActiveSampleRate();
            const double engineRate = AudioEngine::Instance().SampleRate();
            const bool rateFollows =
               activeRate > 0.0 &&
               (engineRate > 0.0 ? std::abs(activeRate - engineRate) < 1.0
                                 : std::abs(activeRate - (gAudioSampleRate > 0.0 ? gAudioSampleRate
                                                                                 : 48000.0)) < 1.0);
            const int blockNow = OfflineAudioBlockFrames();
            const uint32_t devPeriod = Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
            const int wantBlock =
               std::clamp(devPeriod > 0 ? (int)devPeriod
                                        : (gAudioBufferFrames > 0 ? gAudioBufferFrames : 512),
                          1, kAudioMaxBlockFrames);
            // The buffer setting has to actually reach the pump: rendering in
            // kAudioMaxBlockFrames slabs latches MixerNode's pan/mute/solo
            // once per 4096 frames instead of once per period.
            const bool blockFollows = blockNow == wantBlock && blockNow <= kAudioMaxBlockFrames &&
                                      blockNow >= 1;
            const bool lOk = rateFollows && blockFollows;
            printf("arrange render follows audio settings: %s (%.0f Hz, %d-frame blocks, "
                   "engine %.0f Hz, setting %d, device period %u)\n",
                   lOk ? "OK" : "FAIL", activeRate, blockNow, engineRate, gAudioBufferFrames,
                   devPeriod);
            allOk = allOk && lOk;

            // R478: a headless job ignores the device and the Settings value,
            // so the same patch pumps the same blocks on every machine.
            {
               const int savedSetting = gAudioBufferFrames;
               const Headless::Mode savedMode = gHeadlessJob.mode;
               gHeadlessJob.mode = Headless::Mode::Render;
               bool fixed = true;
               for (int setting : { 64, 128, 1024 })
               {
                  gAudioBufferFrames = setting;
                  fixed = fixed && OfflineAudioBlockFrames() == kHeadlessAudioBlockFrames;
               }
               gAudioBufferFrames = savedSetting;
               gHeadlessJob.mode = savedMode;
               printf("headless audio block is fixed at %d regardless of settings: %s\n",
                      kHeadlessAudioBlockFrames, fixed ? "OK" : "FAIL");
               allOk = allOk && fixed;
            }

            std::string why;
            if (!Arrange::Validate(gArrange, &why))
            {
               printf("arrange render validate: %s\n", why.c_str());
               allOk = false;
            }
         }

         gArrangeRenderQueue.clear();
         gArrangeRenderActiveJobId = 0;
         gArrangeRenderQueueRunning = false;
         gAudioMode = modeBefore;
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         NewPatch();
         printf("arrange render test: all  %s\n", allOk ? "OK" : "FAIL");
      }

      // ---- WP8: live clip waveforms and video thumbnails ----------------
      // What a frame-4 fixture can decide: the ring's SPSC discipline and its
      // drop-rather-than-overwrite rule, the bucket maths, the cache's
      // shaping / invalidation / eviction rules, and - where a device opens -
      // a real take actually filling a clip's buckets through the audio
      // thread. What it cannot: the drawing, which the owner eyeballs.
      FrameTest_ARRANGEWAVETEST(frameId, window);

      // Per-clip modulation bypass (Arrange::Clip::bypassedModParams): the
      // list the Clip Settings panel builds, and the playhead-driven gate the
      // modulation apply loop reads. Both go through the same two functions
      // the app itself calls, so a green run here means the panel really does
      // list that binding and the gate really does fire on that beat.
      FrameTest_CLIPMODBYPASSTEST(frameId, window);


      // Clip inspector field units and parsing. The widgets themselves need a
      // mouse, but everything that decides what a typed string MEANS is pure
      // and is exactly where this went wrong before: Start/Length read and
      // write bar.beat.sixteenth, fades read and write milliseconds, and the
      // shortcut suppression has to be armed by a hover a frame before the
      // digit that opens the field arrives.
      FrameTest_CLIPFIELDTEST(frameId, window);

      FrameTest_UNDOPERFTEST(frameId, window);

      // Bring the comment into view for the screenshot check. Frame 2 rather
      // than frame 0 only so the node has settled at its spawn position and
      // measured its own size first; the fit itself runs at the end of this
      // same frame and lands on the frame after.
      if (getenv("INFINITE_COMMENTTEST") != nullptr && frameId == 2)
         gRequestFitView = true;

      // Slash mode: one keystroke had to produce a comment already taking the
      // keyboard, with no click of any kind in between, and the "/" itself must
      // not have ended up in the note.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "slash" && frameId == 15)
         {
            CommentNode* c = gNodes.size() == 1
                                ? dynamic_cast<CommentNode*>(gNodes[0].node.get())
                                : nullptr;
            const bool spawned = c != nullptr;
            const bool typed = spawned && c->text == "lighting\nrim light too hot";
            printf("slash spawned a comment=%d, text=\"%s\"  %s\n", (int)spawned,
                   spawned ? c->text.c_str() : "(none)",
                   (spawned && typed) ? "SLASH COMMENT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // In edit mode (see the synthetic double-click and typing above) report
      // whether the note's only way in works end to end.
      {
         const char* mode = getenv("INFINITE_COMMENTTEST");
         if (mode != nullptr && std::string(mode) == "edit" && frameId == 19)
         {
            auto* c = static_cast<CommentNode*>(gNodes[0].node.get());
            const bool opened = gCommentEdit.target == c;
            const bool typed = c->text.find('!') != std::string::npos;
            const size_t lines = (size_t)std::count(c->text.begin(), c->text.end(), '\n') + 1;
            printf("double-click opens editor=%d, typing reaches the note=%d, %zu lines  %s\n",
                   (int)opened, (int)typed, lines,
                   (opened && typed && lines == 4) ? "COMMENT EDIT OK" : "FAIL");
            if (getenv("IMAGERESYNTH_SCREENSHOT") == nullptr)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // A note's line breaks are its content, and one param round trip backs
      // saving, undo/redo and copy/paste alike - so a comment that survives
      // being written to disk and read back survives all three. Checked here
      // rather than by eye because a screenshot cannot tell "kept the line
      // breaks" from "happens to be short enough to wrap the same way".
      //
      // Runs before the screenshot frame, and puts the note back the way it
      // found it, so the picture still shows the comment as authored. Skipped
      // in edit mode: reloading the patch would delete the node whose text is
      // being edited and close the popup under test.
      FrameTest_COMMENTTEST_4(frameId, window);

      // Group auto-fit: the box must track its members in BOTH directions, so
      // dragging one out stretches it and dragging that one back shrinks it
      // to the size it had before. Driven here rather than by hand because
      // the whole behaviour is a fixed point between our own fitting pass and
      // the editor's group geometry, and eyeballing a screenshot cannot tell
      // "shrank back exactly" from "shrank back nearly".
      FrameTest_GROUPTEST(frameId, window);

      FrameTest_LIVETEST(frameId, window);

      // Drives the real glTF/GLB drop handler (main.cpp's kGltfExt branch)
      // end to end by pushing real paths into gDroppedFiles/gDropPos - the
      // same internal queue a real OS file-drop populates - rather than any
      // OS-level UI automation of the ImGui canvas. A push at frame N is
      // consumed by the drop-handling code (above, unconditional every
      // frame) during frame N+1, before this block runs again that same
      // frame, so two-frame spacing between "push" and "verify" stages
      // gives a safety margin. Paths come from env vars so the fixture
      // doesn't depend on committing binary glTF assets into the repo.
      FrameTest_GLTFDROPTEST(frameId, window);

      // R571 slice 4: with the Shortcuts window open, Tab drives ImGui nav and Space must not reach the transport.
      FrameTest_NAVTEST(frameId, window);

      // R573: every theme's text and dim text clear 4.5:1 against its window and panel backgrounds.
      FrameTest_THEMECONTRASTTEST(frameId, window);

      // R575: on the frame a text field takes focus io.WantTextInput is still false, TextFocusClaimed() is not.
      FrameTest_TEXTFOCUSTEST(frameId, window);

      // R576: help tooltips default to off; Cmd/Ctrl+= and Cmd/Ctrl+- step the UI scale by 0.1.
      FrameTest_UXLEFTOVERSTEST(frameId, window);

      // R506: an anti-aliased edge keeps the shape's own colour and only alpha falls off (straight alpha).
      FrameTest_SHAPEEDGETEST(frameId, window);

      FrameTest_FIELDPIXELTEST(frameId, window);

      // Field step 17 (.field device file format): exercises the pure
      // (de)serialization layer (FieldDevice.h/.cpp), the per-node
      // ToDeviceFile/LoadDeviceFile round trip (plan §2/§7), and the
      // domain-match gate the drag-and-drop dispatch in this file applies
      // before calling LoadDeviceFile - driven via direct function calls,
      // never UI automation, per plan §8.
      FrameTest_FIELDDEVICETEST(frameId, window);

      // Field step 10 (graph domain): the reconciler diffs a fresh GraphPlan
      // against a persisted key->index ownership map (doc §5.3.3) rather than
      // delete-and-respawn. Structural actions (mount/remount/unmount) are
      // what the doc's exit criterion cares about; an unchanged live key
      // still emits an Update action every regenerate (params may need
      // reapplying) even when nothing about it changed, so "idempotent"
      // below is asserted as "zero structural actions", not "zero actions".
      FrameTest_FIELDGRAPHTEST(frameId, window);

      FrameTest_FIELDGRAPHRATETEST(frameId, window);

      if (getenv("INFINITE_RPCBATCHTEST") != nullptr && frameId == 4)
         RunRpcBatchTest();

      if (getenv("INFINITE_PATCHWATCHTEST") != nullptr && frameId == 4) // needs the ImGui context, so in-loop
         RunPatchWatchTest();

      FrameTest_FIELDGRAPHUNDOTEST(frameId, window);

      FrameTest_FIELDGRAPHBLASTTEST(frameId, window);

      // Build step 15 ("Instrument Mode"): exit criterion §10.
      FrameTest_FIELDGRAPHENCAPTEST(frameId, window);

      if (getenv("INFINITE_FIELDGRAPHLIVEPARAMTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHLIVEPARAMTEST] Running Field graph live-parameter-forwarding harness...\n");
         bool allOk = true;

         // A thin counting wrapper around the real MainGraphHost so the
         // assertions below can observe exactly how many Mount/Unmount/
         // SetParam calls each phase makes, without duplicating
         // MainGraphHost's own logic (composition, not inheritance -
         // MainGraphHost is `final`).
         struct CountingFieldGraphHost final : public Field::IFieldGraphHost
         {
            MainGraphHost inner;
            int mountCalls = 0, unmountCalls = 0, setParamCalls = 0;
            int Mount(const std::string& t) override { mountCalls++; return inner.Mount(t); }
            void Unmount(int id) override { unmountCalls++; inner.Unmount(id); }
            void SetParam(int id, const std::string& n, float v) override { setParamCalls++; inner.SetParam(id, n, v); }
            void Connect(int a, int b, int c, int d) override { inner.Connect(a, b, c, d); }
            void Place(int id, float x, float y) override { inner.Place(id, x, y); }
            bool Alive(int id) const override { return inner.Alive(id); }
            std::string TypeNameOf(int id) const override { return inner.TypeNameOf(id); }
            bool Spawnable(const std::string& t) const override { return inner.Spawnable(t); }
            int Remount(int existing, const std::string& t) override { return inner.Remount(existing, t); }
            int DroppedModCount() const override { return inner.DroppedModCount(); }
            int DetachedCableCount() const override { return inner.DetachedCableCount(); }
         };

         NewPatch();
         GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
         int kernelIdx = gn->index;
         auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
         fgn->code =
            "param float amount = 0 [0, 1]\n"
            "param float unused = 0 [0, 1]\n"
            "osc = emit(\"LFO\", 0)\n"
            "set(osc, \"rateBeats\", amount)\n"
            "set(osc, \"shape\", 2.0 * unused)\n";
         CountingFieldGraphHost host;
         fgn->Regenerate(host);

         gn = FindNodeByIndex(kernelIdx);
         fgn = static_cast<FieldGraphNode*>(gn->node.get());
         int oscIdx = fgn->Ownership().Get("osc#0");

         auto setParamValue = [&](const char* name, float v) {
            for (auto& p : fgn->GetParamTable().Params())
               if (p.name == name) p.value = v;
         };

         // Assertion 1: driving `amount`'s ParamTable entry directly (simulating
         // a modulation cable write) changes the mounted LFO's actual rateBeats
         // field within the same frame, with zero Mount/Unmount/Remount calls
         // by PushLiveParams (Regenerate's own Mount calls above are excluded
         // by resetting the counters first).
         {
            host.mountCalls = 0;
            host.unmountCalls = 0;
            host.setParamCalls = 0;
            setParamValue("amount", 0.75f);
            fgn->PushLiveParams(host);

            auto* lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            bool valueForwarded = lfo != nullptr && std::abs(lfo->rateBeats - 0.75f) < 1.0e-4f;
            bool noMountUnmount = (host.mountCalls == 0) && (host.unmountCalls == 0);
            bool exactlyOneSetParam = (host.setParamCalls == 1);

            bool pass1 = valueForwarded && noMountUnmount && exactlyOneSetParam;
            printf("[FIELDGRAPHLIVEPARAMTEST] Assertion 1 (Live-forward, no Mount/Unmount): forwarded=%d rate=%f noMountUnmount=%d setParamCalls=%d  %s\n",
                   (int)valueForwarded, lfo ? lfo->rateBeats : -1.0f, (int)noMountUnmount, host.setParamCalls, pass1 ? "OK" : "FAIL");
            allOk = allOk && pass1;
         }

         // Assertion 2: a set() whose value expression is not a bare param
         // reference (`2.0 * unused`) is not in mLiveForward - driving `unused`
         // does not change the mounted LFO's shape until an explicit
         // Regenerate() runs.
         {
            auto* lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            int shapeBefore = lfo ? lfo->shape : -1;

            setParamValue("unused", 1.0f);
            fgn->PushLiveParams(host);

            lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            bool unchangedByPush = lfo != nullptr && lfo->shape == shapeBefore;

            fgn->Regenerate(host);
            gn = FindNodeByIndex(kernelIdx);
            fgn = static_cast<FieldGraphNode*>(gn->node.get());
            oscIdx = fgn->Ownership().Get("osc#0");
            lfo = dynamic_cast<LFONode*>(FindNodeByIndex(oscIdx)->node.get());
            bool changedByRegenerate = lfo != nullptr && lfo->shape == 2;

            bool pass2 = unchangedByPush && changedByRegenerate;
            printf("[FIELDGRAPHLIVEPARAMTEST] Assertion 2 (Computed set() not live-forwarded): unchangedByPush=%d changedByRegen=%d  %s\n",
                   (int)unchangedByPush, (int)changedByRegenerate, pass2 ? "OK" : "FAIL");
            allOk = allOk && pass2;
         }

         // Assertion 3: PushLiveParams called with no changed param values
         // makes zero host.SetParam calls - a delta-only push, not a
         // re-push-everything-every-frame loop. Assertion 2's Regenerate()
         // call cleared mLastPushedValue (§4.2/§6: rebuilt wholesale every
         // successful Regenerate()), so one priming push is needed first -
         // otherwise this call would still see "amount" as never-pushed-since-
         // last-regenerate and push it once, which is correct behavior but
         // not what this assertion is testing.
         {
            fgn->PushLiveParams(host);
            host.setParamCalls = 0;
            fgn->PushLiveParams(host);
            bool pass3 = (host.setParamCalls == 0);
            printf("[FIELDGRAPHLIVEPARAMTEST] Assertion 3 (No-change push is a no-op): setParamCalls=%d  %s\n",
                   host.setParamCalls, pass3 ? "OK" : "FAIL");
            allOk = allOk && pass3;
         }

         NewPatch();
         printf("%s\n", allOk ? "FIELDGRAPHLIVEPARAM OK" : "SUSPECT");
      }

      // Build step 16 ("Unpack to Canvas") harness. Unlike the encapsulation/
      // live-param/undo harnesses above (which never touch a real ed::
      // node position/size - they only ever check FieldGraphNode/GraphNode
      // bookkeeping directly), this step's exit criterion needs real
      // post-layout bounding boxes, which only exist after the node editor
      // has actually drawn each revealed child at least a couple of times
      // (doc §3.3/trap 3). So this harness spans real frames rather than
      // doing everything inside one frameId gate: setup + triggering the
      // unpack happens at frameId==4 (same "runs before this frame's node
      // draw loop" position the other Field harnesses already use to call
      // RunFieldGraphRegenerate directly - safe for the same reason), then
      // the ordinary per-frame drains (RunFieldGraphUnpackPhase1's arm,
      // RunFieldGraphUnpackPhase2Tick's poll) run every subsequent real
      // frame exactly as they do for a real user click, and assertions run
      // at frameId==30 - comfortably past kMaxRetries (10) frames of
      // phase-2 polling even in the worst case, so gFieldGraphUnpackPhase2
      // is guaranteed inactive (finished, one way or the other) by then.
      static int sUnpackTestFgnIdx = -1;
      static int sUnpackTestConsumerIdx = -1;
      static std::vector<int> sUnpackTestMembers;
      static bool sUnpackTestAllOk = true;

      if (getenv("INFINITE_FIELDGRAPHUNPACKTEST") != nullptr && frameId == 4)
      {
         printf("[FIELDGRAPHUNPACKTEST] Running Field graph unpack-to-canvas harness...\n");
         bool setupOk = true;

         // Assertion 6 (disabled/no-op on an empty mMountedIndices): a
         // never-regenerated Field Graph node has nothing to unpack -
         // calling the operation directly (as if the disabled button were
         // driven programmatically) must be a documented no-op, not a
         // crash or a partial mutation. Self-contained, own NewPatch(), run
         // before the main multi-frame scenario below.
         {
            NewPatch();
            GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
            auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
            bool encBefore = fgn->encapsulated;
            RunFieldGraphUnpackPhase1(fgn);
            bool pass6 = (fgn->encapsulated == encBefore) && fgn->encapsulated &&
                         !gFieldGraphUnpackPhase2.active;
            printf("[FIELDGRAPHUNPACKTEST] Assertion 6 (No-op on empty mMountedIndices): "
                   "stillEncapsulated=%d phase2Armed=%d  %s\n",
                   (int)fgn->encapsulated, (int)gFieldGraphUnpackPhase2.active, pass6 ? "OK" : "FAIL");
            setupOk = setupOk && pass6;
         }

         // Main scenario: a 3-deep chain (Noise -> Curves -> Curves, wired
         // via connect()) so the topological-depth assertion has a real
         // depth-2 node with transitive depth-0/1 dependencies, plus an
         // outer cable wired into the FieldGraphNode's own derived boundary
         // output pin (the terminal, "c") to prove §4.3's no-op finding -
         // the outer cable's real target is the FieldGraphNode itself
         // (RunFieldGraphRegenerate's boundarySource), which never changes
         // identity or address across an unpack, so the cable must survive
         // untouched.
         NewPatch();
         GraphNode* gn = SpawnNode("Field Graph", "Utility", 0.0f, 0.0f);
         int fgnIdx = gn->index;
         auto* fgn = static_cast<FieldGraphNode*>(gn->node.get());
         fgn->code =
            "a = emit(\"Noise\", 0)\n"
            "b = emit(\"Curves\", 1)\n"
            "c = emit(\"Curves\", 2)\n"
            "connect(a, 0, b, 0)\n"
            "connect(b, 0, c, 0)\n";
         RunFieldGraphRegenerate(fgn);

         fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
         ApplyModulationAndPalette(4); // cook once so the terminal has a real texture
         fgn = static_cast<FieldGraphNode*>(FindNodeByIndex(fgnIdx)->node.get());
         fgn->SetBoundaryOutputTarget(ResolveFieldGraphBoundaryTerminal(fgn));

         // Outer cable onto the boundary output pin - positioned well clear
         // of where the unpacked cluster will land so AutoFitGroupToMembers
         // never mistakes it for a member.
         GraphNode* consumer = SpawnNode("Curves", "Compositing", 3000.0f, 0.0f);
         int consumerIdx = consumer->index;
         std::string connErr;
         bool connected = ConnectNodes(fgnIdx, 0, consumerIdx, 0, connErr);
         bool wiredToKernel = connected;
         if (wiredToKernel)
         {
            ImageCable* cable = CableFor(*FindNodeByIndex(consumerIdx), 0);
            wiredToKernel = cable != nullptr && cable->IsConnected() &&
                            cable->GetSource() == FindNodeByIndex(fgnIdx)->node.get();
         }
         setupOk = setupOk && wiredToKernel;
         printf("[FIELDGRAPHUNPACKTEST] Setup (outer cable wired to kernel before unpack): wired=%d  %s\n",
                (int)wiredToKernel, wiredToKernel ? "OK" : "FAIL");

         bool encBefore = fgn->encapsulated;
         bool nonEmptyBefore = !fgn->MountedIndices().empty();
         sUnpackTestMembers.assign(fgn->MountedIndices().begin(), fgn->MountedIndices().end());

         // Same call the "Unpack to Canvas" button makes (DrawFieldGraphParams
         // queues gFieldGraphPendingUnpack; this test calls the queued
         // function directly, at the same before-the-node-draw-loop point in
         // the frame every other Field harness in this file already calls
         // RunFieldGraphRegenerate directly from - safe for the same reason).
         RunFieldGraphUnpackPhase1(fgn);

         sUnpackTestFgnIdx = fgnIdx;
         sUnpackTestConsumerIdx = consumerIdx;

         bool pass0 = encBefore && nonEmptyBefore && !fgn->encapsulated && gFieldGraphUnpackPhase2.active;
         printf("[FIELDGRAPHUNPACKTEST] Setup (phase 1 armed): wasEncapsulated=%d nowUnencapsulated=%d phase2Armed=%d  %s\n",
                (int)encBefore, (int)(!fgn->encapsulated), (int)gFieldGraphUnpackPhase2.active, pass0 ? "OK" : "FAIL");
         setupOk = setupOk && pass0;

         if (!setupOk)
            printf("[FIELDGRAPHUNPACKTEST] setup FAIL - assertions at frameId==30 will not be meaningful\n");
      }

      if (getenv("INFINITE_FIELDGRAPHUNPACKTEST") != nullptr && frameId == 30)
      {
         bool allOk = true;

         GraphNode* fgnGn = FindNodeByIndex(sUnpackTestFgnIdx);
         auto* fgn = fgnGn != nullptr ? static_cast<FieldGraphNode*>(fgnGn->node.get()) : nullptr;

         // Assertion 1: encapsulated flipped false; every mounted child's
         // hiddenFromCanvas cleared; exactly one new GroupNode exists whose
         // gGroupMembers set equals the mounted children's indices exactly.
         bool pass1 = fgn != nullptr && !fgn->encapsulated && !gFieldGraphUnpackPhase2.active;
         for (int idx : sUnpackTestMembers)
         {
            GraphNode* child = FindNodeByIndex(idx);
            pass1 = pass1 && child != nullptr && !child->hiddenFromCanvas;
         }
         GroupNode* spawnedGroup = nullptr;
         int groupCount = 0;
         for (GraphNode& n : gNodes)
         {
            if (auto* g = dynamic_cast<GroupNode*>(n.node.get()))
            {
               groupCount++;
               spawnedGroup = g;
            }
         }
         bool membershipMatches = spawnedGroup != nullptr && gGroupMembers.count(spawnedGroup) != 0 &&
                                   gGroupMembers[spawnedGroup] ==
                                      std::set<int>(sUnpackTestMembers.begin(), sUnpackTestMembers.end());
         pass1 = pass1 && (groupCount == 1) && membershipMatches;
         printf("[FIELDGRAPHUNPACKTEST] Assertion 1 (encapsulated false, children unhidden, 1 group with exact membership): "
                "encFalse=%d groupCount=%d membershipMatches=%d  %s\n",
                (int)(fgn != nullptr && !fgn->encapsulated), groupCount, (int)membershipMatches, pass1 ? "OK" : "FAIL");
         allOk = allOk && pass1;

         // Editor-context save/restore for the position/size reads below -
         // this runs after this frame's ed::End() already cleared the
         // current editor (same shape as FindFreeSpawnPosition/
         // RunFieldGraphUnpackPhase2Tick).
         ed::EditorContext* prevEditor = ed::GetCurrentEditor();
         ed::SetCurrentEditor(gEditor);

         // Assertion 2: no two of the members' post-layout bounding boxes
         // overlap.
         struct Box { ImVec2 mn, mx; };
         std::vector<Box> boxes;
         for (int idx : sUnpackTestMembers)
         {
            GraphNode* gn = FindNodeByIndex(idx);
            if (gn == nullptr) continue;
            ImVec2 p = ed::GetNodePosition(gn->NodeId());
            ImVec2 s = ed::GetNodeSize(gn->NodeId());
            boxes.push_back({ p, ImVec2(p.x + s.x, p.y + s.y) });
         }
         bool noOverlap = true;
         for (size_t i = 0; i < boxes.size() && noOverlap; i++)
            for (size_t j = i + 1; j < boxes.size() && noOverlap; j++)
            {
               const Box& A = boxes[i]; const Box& B = boxes[j];
               bool overlap = A.mx.x > B.mn.x && A.mn.x < B.mx.x && A.mx.y > B.mn.y && A.mn.y < B.mx.y;
               if (overlap) noOverlap = false;
            }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 2 (No overlapping bounding boxes among %zu members): %s\n",
                boxes.size(), noOverlap ? "OK" : "FAIL");
         allOk = allOk && noOverlap;

         // Assertion 3: topological x-ordering - "c" (depth 2) sits strictly
         // right of both "a" (depth 0) and "b" (depth 1), which sits
         // strictly right of "a" - transitively via plan.connects.
         bool topoOk = false;
         if (fgn != nullptr)
         {
            int aIdx = fgn->Ownership().Get("a#0");
            int bIdx = fgn->Ownership().Get("b#1");
            int cIdx = fgn->Ownership().Get("c#2");
            GraphNode* ag = FindNodeByIndex(aIdx);
            GraphNode* bg = FindNodeByIndex(bIdx);
            GraphNode* cg = FindNodeByIndex(cIdx);
            if (ag != nullptr && bg != nullptr && cg != nullptr)
            {
               float ax = ed::GetNodePosition(ag->NodeId()).x;
               float bx = ed::GetNodePosition(bg->NodeId()).x;
               float cx = ed::GetNodePosition(cg->NodeId()).x;
               topoOk = (bx > ax) && (cx > bx) && (cx > ax);
            }
         }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 3 (Topological x-ordering across 3 depths): %s\n",
                topoOk ? "OK" : "FAIL");
         allOk = allOk && topoOk;

         ed::SetCurrentEditor(prevEditor);

         // Assertion 4 (§4.3's expected no-op, asserted rather than assumed):
         // the outer cable wired to the FieldGraphNode's boundary output pin
         // before unpacking is still connected, to the same INode*, after
         // unpacking - zero detached, because the outer cable's real target
         // was always the FieldGraphNode itself (never the terminal), and
         // the FieldGraphNode's identity/address never changes across an
         // unpack.
         bool boundaryPreserved = false;
         {
            GraphNode* consumer = FindNodeByIndex(sUnpackTestConsumerIdx);
            ImageCable* cable = consumer != nullptr ? CableFor(*consumer, 0) : nullptr;
            boundaryPreserved = fgnGn != nullptr && cable != nullptr && cable->IsConnected() &&
                                cable->GetSource() == fgnGn->node.get();
         }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 4 (Boundary cable identity preserved, zero detach): %s\n",
                boundaryPreserved ? "OK" : "FAIL");
         allOk = allOk && boundaryPreserved;

         // Assertion 5 (part 1): undo after unpack restores `encapsulated`
         // and removes the spawned GroupNode in one step. The children's
         // hiddenFromCanvas re-sync is driven by the per-frame loop in
         // ApplyModulationAndPalette (main.cpp, "Build step 15" comment
         // above `child->hiddenFromCanvas = fgn->encapsulated;"), which for
         // this frame has already run before this test block executes - so
         // Undo() here takes effect for that loop's *next* pass, at
         // frameId==31, same one-frame lag FIELDGRAPHENCAPTEST's own
         // assertion 4 already relies on for the opposite toggle direction.
         bool undoOk = false;
         {
            Undo();
            GraphNode* afterUndoGn = FindNodeByIndex(sUnpackTestFgnIdx);
            auto* afterUndoFgn = afterUndoGn != nullptr ? static_cast<FieldGraphNode*>(afterUndoGn->node.get()) : nullptr;
            bool encRestored = afterUndoFgn != nullptr && afterUndoFgn->encapsulated;
            bool groupGone = true;
            for (GraphNode& n : gNodes)
               if (dynamic_cast<GroupNode*>(n.node.get()) != nullptr) groupGone = false;
            undoOk = encRestored && groupGone;
            printf("[FIELDGRAPHUNPACKTEST] Assertion 5a (Undo restores encapsulated, removes group): "
                   "encRestored=%d groupGone=%d  %s\n",
                   (int)encRestored, (int)groupGone, undoOk ? "OK" : "FAIL");
         }
         allOk = allOk && undoOk;
         sUnpackTestAllOk = allOk;
      }

      if (getenv("INFINITE_FIELDGRAPHUNPACKTEST") != nullptr && frameId == 31)
      {
         // Assertion 5 (part 2): one real frame after Undo(), the sync loop
         // in ApplyModulationAndPalette has now run once with the restored
         // `encapsulated == true`, so every member should be hidden again.
         //
         // Undo() goes through ApplyPatchData, which - same as
         // FIELDGRAPHUNDOTEST's own assertions 3/6 - reassigns node indices
         // (see its `remap` out-param). sUnpackTestMembers holds the
         // pre-undo indices, which are no longer meaningful; re-resolve the
         // three emit keys through the (now-restored) FieldGraphNode's own
         // ownership map instead, exactly as FIELDGRAPHUNDOTEST does with
         // "osc#" + i.
         GraphNode* fgnGn = FindNodeByIndex(sUnpackTestFgnIdx);
         if (fgnGn == nullptr)
         {
            for (GraphNode& n : gNodes)
               if (dynamic_cast<FieldGraphNode*>(n.node.get()) != nullptr) fgnGn = &n;
         }
         auto* fgn = fgnGn != nullptr ? static_cast<FieldGraphNode*>(fgnGn->node.get()) : nullptr;
         bool childrenHidden = fgn != nullptr;
         for (const char* key : { "a#0", "b#1", "c#2" })
         {
            int idx = fgn != nullptr ? fgn->Ownership().Get(key) : -1;
            GraphNode* child = idx >= 0 ? FindNodeByIndex(idx) : nullptr;
            childrenHidden = childrenHidden && child != nullptr && child->hiddenFromCanvas;
         }
         printf("[FIELDGRAPHUNPACKTEST] Assertion 5b (Children re-hidden one frame after undo): childrenHidden=%d  %s\n",
                (int)childrenHidden, childrenHidden ? "OK" : "FAIL");
         bool allOk = sUnpackTestAllOk && childrenHidden;

         NewPatch();
         sUnpackTestFgnIdx = -1;
         sUnpackTestConsumerIdx = -1;
         sUnpackTestMembers.clear();
         sUnpackTestAllOk = true;
         printf("%s\n", allOk ? "FIELDGRAPHUNPACK OK" : "SUSPECT");
      }

      FrameTest_FIELDPINSTEST(frameId, window);

      // Build step 13 (docs/plans/field/step-13-dynamic-pins-node-wiring.md):
      // node/UI/save-format wiring for kernel `output`/`input` declarations
      // on top of step 12's compiler-level PinTable/IR work and step 11's
      // hardcoded toggle pins (both exercised above by FIELDPINSTEST). This
      // was originally structural-only - every declared output's
      // ModulatorOutput() read back a fixed 0.0 placeholder, regardless of
      // domain. The device-catalog simplification pass finished that
      // follow-up for the one case with a working name-keyed runtime
      // channel: a Frame-domain, non-structural declared output (`chime`,
      // `glow`, ...) on FieldElementNode now reads its real value via
      // ElementVM::ReadFrameVar (FieldIR.cpp's DeclOutput lowering emits a
      // synthetic frame-var assign for it), and FieldSampleNode's
      // Frame-domain declared output (`bass`, always `reduce.rms(...)`) now
      // reads the same live value as the "rms" toggle output. Every other
      // declared-output domain (element/pixel/sample) still reads the fixed
      // 0.0 placeholder - see each node's DeclaredOutputPlaceholder.
      FrameTest_FIELDPINNODETEST(frameId, window);

      // GetNodeInstanceIndex answers from a cache (see NodeTitleInstanceEntry).
      // Every node's "#N" title must match the reference linear scan, with no
      // frame boundary in between, after each kind of change the cache has to
      // notice: spawn, delete, a live shape change, and back to unique.
      FrameTest_NODETITLETEST(frameId, window);

      // Audio Filter's response-curve cache (FilterCurveCache): a settled
      // curve is bit-identical to a direct full recompute, a single step is
      // exact at once, and a modulation-style streak is throttled - then
      // back to the exact full-resolution curve as soon as it stops.
      FrameTest_FILTERCURVECACHETEST(frameId, window);

      FrameTest_BYPASSRULETEST(frameId, window);

      FrameTest_BYPASSSWEEPTEST(frameId, window);

      // Live half of PATCHLAYOUTTEST: a pos-less file opened through the real
      // loader must come out laid out from drawn sizes - nothing stacked at 0,0,
      // no overlapping boxes, the picture chain ordered by wiring depth.
      FrameTest_PATCHLAYOUTLIVETEST(frameId, window);

      FrameTest_ROUNDTRIPTEST(frameId, window);

      FrameTest_PHASEFTEST(frameId, window);

      FrameTest_PHASEETEST(frameId, window);

      FrameTest_GROUP3DTEST(frameId, window);

      FrameTest_WRAPTEST(frameId, window);

      FrameTest_PHASEDTEST(frameId, window);

      FrameTest_PHASECTEST(frameId, window);

      FrameTest_MAPTEST(frameId, window);

      FrameTest_SHADOWTEST(frameId, window);

      FrameTest_BUGTEST(frameId, window);

      // Sweeps every node type that consumes an IGeometrySource, checking one
      // thing: does moving/rotating/scaling its upstream source actually move
      // the final world-space result? BUGTEST above proves specific fixtures
      // stay fixed; this proves the same property for every node that takes a
      // geometry input, generically, so a newly added node type is covered
      // without anyone having to remember to hand-write a fixture for it.
      FrameTest_TRANSFORMSWEEPTEST(frameId, window);

      // Sibling of TRANSFORMSWEEPTEST, same generic-probe approach, checking a
      // different side-channel: does GetMappingTransform() reach a node's
      // output from its input? Found via a real bug: ClothNode, MeshResynthNode
      // and MeshToPointsNode forwarded every other side-channel (material,
      // textures, model matrix) from their single geo input but not this one,
      // so a Mapping node patched upstream of any of them had its space/
      // translate/rotate/scale silently dropped before Render 3D ever saw it.
      FrameTest_MAPPINGSWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): sibling of MAPPINGSWEEPTEST,
      // checking side channel A (Material, `GetMaterial()`) instead of F.
      // Same generic-probe shape - MAPPINGSWEEPTEST already proved the
      // pattern works for one side channel.
      FrameTest_MATERIALSWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): sibling of MATERIALSWEEPTEST,
      // checking side channel E (textures, `GetMaterialTexture`/
      // `GetSurfaceTexture()`) with the identical wiring - the 17 node types
      // below are the exact same set, so a node that forwards A but not E
      // (or vice versa) shows up as one sweep failing and the other passing.
      FrameTest_TEXTURESWEEPTEST(frameId, window);

      // GeometryOpNode used to drop point clouds and curves (its GetPointCloud/
      // GetCurve fell through to IGeometrySource's nullptr defaults), so
      // Mesh to Points -> Transform -> Render 3D drew nothing. Checks, without
      // a GL context: a cloud/curve survives every op, kTransform moves the
      // cloud by exactly the offset and rotates nothing it shouldn't, and
      // PointCloudRevision only moves when the points actually changed.
      FrameTest_POINTCLOUDSWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): channels B (`Mesh::vertexColor`)
      // and C (`Particle::r/g/b`/`hasColor`) - built from scratch (no prior
      // COLOURSWEEPTEST existed anywhere in the codebase; grepped twice to
      // confirm). Two invariants, checked per node: colourless in must stay
      // colourless out (D6's "don't manufacture colour" rule,
      // `Mesh.h:296-301`), and a distinct authored colour in must survive out
      // unchanged. Covers every node type where a B/C answer is meaningful;
      // excludes InstanceOnPointsNode (its colour path is channel D,
      // `InstanceColors()`, not B/C - no sweep covers D yet, a gap for a
      // future test, not silently dropped), CurveNode/ModelSourceNode (need
      // real file/curve data to produce anything in a headless run), and
      // ImageToPointsNode/ParticleSystemNode's own colour-origination (both
      // already covered end-to-end by this session's D6 follow-up audit,
      // commit 853c732 - re-driving ParticleSystemNode here would need its
      // real time-stepped emission, which is flaky in a single-frame test).
      FrameTest_COLOURSWEEPTEST(frameId, window);

      // The instancing side-channels, same generic-probe shape as the two
      // sweeps above. InstanceOnPointsNode's GetMesh() returns the single
      // stamp mesh and carries its N placements separately, so every consumer
      // that wants the scatter walks PassthroughSource() to find the
      // instancer (Render3DNode::FindInstancer, NodeViewport's copy) and
      // reads GetInstanceGroupMatrix() off the chain head to pick up a
      // wrapping Transform. A node that takes a geometry input, forwards
      // every other side-channel, and silently drops these two turns N
      // instances into one un-instanced stamp - with no error anywhere.
      // Found via a real bug: Null3DNode (a node whose entire job is to be a
      // no-op passthrough), DisplacementNode and WrapNode dropped
      // PassthroughSource, and MaterialNode/SetColorNode/MergeByDistanceNode/
      // Switcher3DNode forwarded PassthroughSource without the group matrix,
      // so `Instance on Points -> Transform -> Material -> Render 3D` drew
      // the scatter back at the origin.
      FrameTest_INSTANCESWEEPTEST(frameId, window);

      // Phase 5 (geometry-domains audit): a randomised chain fuzzer, rather
      // than enumerating orderings by hand - the only tractable answer to
      // the permutation problem. Roster is the 11 single-geometry-input,
      // mesh-forwarding operator types from COLOURSWEEPTEST's
      // checkMeshForwarding list (GeometryOpNode, DisplacementNode,
      // AudioDisplacementNode, SetColorNode, MeshResynthNode, Null3DNode,
      // MaterialNode, MappingNode, MergeByDistanceNode, ClothNode,
      // FieldElementNode) - deliberately excludes the multi-input types
      // (WrapNode, JoinGeometryNode, Switcher3DNode, InstanceOnPointsNode)
      // since a random *chain* only ever needs one upstream slot filled;
      // giving every link a second, unfilled input pin would just make each
      // step behave like whatever that node does with nothing patched into
      // its second slot, not exercise anything new. Fixed-seed PRNG so a
      // failure is reproducible across runs. Expect this to surface real,
      // unrelated pre-existing bugs - that's the point (per the plan).
      FrameTest_CHAINFUZZTEST(frameId, window);

      // A different bug class from the two sweeps above: not a dropped
      // side-channel, but a revision/generation stamp that bumps when nothing
      // actually changed. Found via a real bug: DisplacementNode bumped
      // mTexGeneration on every single cook while a texture was connected,
      // even when the texture's pixels were identical to last frame, which
      // made MeshRevision() change every frame and forced ClothNode
      // downstream to treat every frame as a topology change - the cloth sim
      // reset to rest pose continuously instead of ever draping.
      //
      // The check: cook the same node twice in a row with nothing about its
      // inputs changed, and assert MeshRevision() (or the equivalent stamp)
      // did not move between the two cooks. Any node with a texture input
      // gets a *connected, static* texture for the same reason the Displace
      // bug only showed up with one patched in - the bug is invisible with no
      // texture connected at all.
      FrameTest_REVISIONSWEEPTEST(frameId, window);

      // Regression test for Render3DNode's frame-cache signature missing the
      // instancing and per-source-transform stamps. Confirms neither a
      // Particle System -> Instance on Points chain (no MeshRevision/
      // PointCloudRevision/CurveStamp bump - all motion carried by
      // InstanceRevision(), which the signature didn't track) nor a
      // spinY-animated Geometry node (a live GetModelMatrix() with no
      // revision bump of its own) leaves Render 3D's output frozen across
      // advancing frames with nothing but the transport clock moving -
      // the same as a user just sitting there, not touching the viewport.
      FrameTest_RENDER3DLIVETEST(frameId, window);

      // A different bug class from RENDER3DLIVETEST above: not a live-updating
      // source with no revision bump of its own, but the opposite direction -
      // a real upstream mesh/cloud/curve change that DOES bump a revision,
      // but never reaches Render 3D's rendered pixels because
      // BuildSceneSignature's per-slot geomRev used to XOR-fold
      // MeshRevision()/PointCloudRevision()/CurveStamp() into one value.
      // Several IGeometrySource implementations deliberately return the same
      // counter from two of those three accessors (mesh cache and point/curve
      // cache rebuilt together, sharing one stamp), which made the XOR
      // cancel to a constant 0 forever - the cache never invalidated and the
      // viewport froze on the first frame. See the SceneSignature comment in
      // Geometry3DNodes.h for the full story.
      //
      // The check, per geometry-consuming node type: cook Render 3D once,
      // capture NodeWorkCounter() (bumped only on a real, non-cached render -
      // Geometry3DNodes.cpp's CookIfNeeded increments it right after the
      // cache-hit early-return check), change something upstream that
      // genuinely alters the geometry, cook Render 3D again, and assert
      // NodeWorkCounter() advanced - i.e. the cache early-return was NOT
      // taken.
      FrameTest_RENDER3DCACHESWEEPTEST(frameId, window);

      FrameTest_FIXTEST(frameId, window);

      FrameTest_CLOTHTEST(frameId, window);

      FrameTest_PARTICLETEST(frameId, window);

      FrameTest_AUDIORECTEST(frameId, window);

      FrameTest_VIDEOAUDIOTEST(frameId, window);

      FrameTest_VIDEOSPEEDTEST(frameId, window);

      FrameTest_OFFLINERENDERTEST(frameId, window);

      FrameTest_OFFLINERENDERREFUSETEST(frameId, window);

      FrameTest_PATCHTEST(frameId, window);

      FrameTest_AUTOSAVETEST(frameId, window);

      // R30: the live validate pass marks a half-wired Blend and an empty
      // Output, leaves a fully wired graph alone, and clears when it is fixed.
      FrameTest_LIVEISSUETEST(frameId, window);
      FrameTest_LIVEISSUETEST_2(frameId, window);

      // R617: Shape Resonator inside the real graph - eight instances on one source, save/load with the shape
      // pin wired, source deleted mid-ring, and the pin counted as an input slot.
      FrameTest_SHAPERESGRAPHTEST(frameId, window);
      auto ShapeResFixtureReport = [&](const char* stage, bool wantWired) {
         int resonators = 0, wired = 0, slotOk = 0;
         for (GraphNode& gn : gNodes)
         {
            auto* fx = dynamic_cast<AudioEffectNode*>(gn.node.get());
            if (!fx || gn.typeName != "Shape Resonator")
               continue;
            resonators++;
            fx->CookIfNeeded(frameId); // must not crash on a freed or swapped source
            wired += (fx->geometry != nullptr);
            slotOk += (InputCountFor(gn) >= 2);
         }
         const bool ok = resonators == 8 && (wantWired ? wired == 8 : wired == 0) && slotOk == 8;
         printf("SHAPERESGRAPHTEST %s: resonators=%d wired=%d shape-pin-counted=%d  %s\n", stage, resonators, wired,
                slotOk, ok ? "OK" : "FAIL");
      };
      if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 6)
         ShapeResFixtureReport("after load", true);
      FrameTest_SHAPERESGRAPHTEST_3(frameId, window);
      if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 10)
      {
         ShapeResFixtureReport("after save/load", true);
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Cube")
            {
               RemoveNodeByIndex(gn.index); // the source dies while eight resonators are ringing on it
               break;
            }
      }
      if (getenv("INFINITE_SHAPERESGRAPHTEST") != nullptr && frameId == 12)
         ShapeResFixtureReport("after source deleted", false);

      if (getenv("INFINITE_DELETECRASHTEST") != nullptr && frameId == 4)
      {
         RemoveNodeByIndex(gNodes[0].index);
      }
      FrameTest_DELETECRASHTEST_2(frameId, window);

      // DELETECRASHTEST for the audio graph (docs/plans/audio/README.md §4/§7):
      // every node type DiscoverAudioSweepCandidates finds touching the audio/
      // note cable graph at all (not just ones with an AudioNode to drive -
      // this is broader than AUDIOPARAMSWEEPTEST's filter, so it also covers
      // pure terminals like Audio Out) gets spawned into the real editor
      // graph, wired with whatever companion nodes its shape needs, rendered
      // a few blocks "mid-playback", deleted via the real RemoveNodeByIndex
      // (which is what exercises DisconnectAllTo's generic AudioInputSlot/
      // NoteInputSlot loop), then rendered a few more blocks. Surviving to the
      // final printf is most of the proof - a dangling pointer to the freed
      // node's AudioNode crashes the very next ProcessOffline, the same way
      // DELETECRASHTEST's geometry nodes crash on the next cook.
      FrameTest_AUDIOTEARDOWNSWEEPTEST(frameId, window);

      // NoteEventQueue multi-consumer fanout regression (docs/plans/
      // fm-and-note-fanout-fixes.md item 11): before the fix, a single-cursor
      // queue meant the FIRST consumer to Pop() in topology order drained
      // every event, starving every other consumer wired to the same
      // producer. The bug report's exact patch - one note source feeding two
      // synths, one of which also feeds the other's FM input, so the FM
      // modulator (a dependency) necessarily runs before the synth it
      // modulates - made the second synth silent ("output peak=0.00000").
      // Goes through the real graph and the real RebuildAudioTopology (not a
      // hand-rolled inbox), because the thing under test is the topology
      // builder's ResetConsumers()/RegisterConsumer() wiring itself, not the
      // queue's own Push/Pop (already covered by NoteEventQueue's unit-level
      // use in the other fixtures above).
      // Note generators must run with no audio device: Random Note Generator
      // -> Note to CV should light up without Start Audio (the device-less
      // note pump in AudioEngine::PumpNoteNodesWithoutDevice).
      FrameTest_NOTEPUMPTEST(frameId, window);

      FrameTest_NOTEFANOUTTEST(frameId, window);

      // Regression test for the stuck-note race fixed by AudioEngine::
      // ApplyNoteWiringIfNew (bugfix/audio-rt-note-wiring): a Note Sequencer
      // fanned out to two synths, with RebuildAudioTopology() forced every
      // single block for ~2 seconds' worth of blocks while notes are
      // flowing through it. Before the fix, RebuildAudioTopology mutated the
      // live AudioNode/NoteEventQueue cursors directly on the main thread
      // (ResetConsumers/RegisterConsumer/SetNoteInbox) - here that means
      // every one of these per-block rebuilds re-registering both synths'
      // read cursors while a note could be in flight, which used to be able
      // to skip or duplicate events (worst case: a lost note-off, a
      // permanently stuck voice). Now the rebuild only *describes* the
      // wiring (AudioTopology::noteOutboxes/noteWires) and
      // ApplyNoteWiringIfNoDevice applies it - carrying each cursor's old
      // read position forward via AudioNode::appliedInbox/appliedCursor - so
      // repeated rewiring mid-stream must not drop a single event.
      // Note events are pushed directly into the sequencer's own outbox
      // (AudioNode::NoteOutbox(), a fixed member - present whether or not
      // the sequencer is actually playing) rather than relying on real
      // tempo-driven playback, so the fixture is deterministic and doesn't
      // need to wait on wall-clock time.
      FrameTest_NOTEREWIRESTRESSTEST(frameId, window);

      // Audio Filter's "cutoff mod" sidechain input (slot 1) was removed as
      // part of the envAmount-becomes-internal-LFO redesign (EffectDefs.cpp,
      // AudioFilterKernel.h) - the sidechain-teardown fixture that used to
      // live here (spawn/wire/delete-mid-playback against that pin) went
      // with it, since the pin no longer exists to test.

      FrameTest_AUDIOGRAPHTEST(frameId, window);

      // File > New with audio rendering. NewPatch retires every node and
      // clears gNodes; the published audio topology must be republished with
      // it, or the engine keeps rendering the old nodes (audible after New,
      // and with no device the retire drain frees nodes the topology still
      // points at). frameId 4: render, New, render; frameId 12: the retired
      // nodes must have been drained.
      FrameTest_NEWPATCHAUDIOTEST(frameId, window);

      FrameTest_MATFRAMETEST(frameId, window);

      FrameTest_ENVTEST(frameId, window);

      FrameTest_PATHOCEANTEST(frameId, window);

      FrameTest_PALETTETEST(frameId, window);

      FrameTest_PALETTETEST_2(frameId, window);

      FrameTest_UTILTEST(frameId, window);

      FrameTest_TEXT3DTEST(frameId, window);

      FrameTest_MODELTEST(frameId, window);

      // Exercises every mesh operator directly, checking each produces a
      // non-degenerate mesh with finite coordinates and a sane bounding box.
      // A silently empty or NaN-riddled result is the failure mode that matters
      // here: the renderer draws nothing and says nothing.
      FrameTest_MESHOPTEST(frameId, window);

      // Frame limiter check: run uncapped, then at 30fps, and compare. Vsync is
      // off for this so the display refresh is not what is being measured.
      if (getenv("INFINITE_FPSTEST") != nullptr)
      {
         static double sUncapped = 0.0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); }
         if (frameId == 30) { sUncapped = gLastFrameMs; gTargetFps = 30; }
         if (frameId == 60)
         {
            printf("uncapped %.2f ms/frame -> capped %.2f ms/frame (target 30fps = 33.3 ms)\n",
                   sUncapped, gLastFrameMs);
            printf("%s\n", (gLastFrameMs > 30.0 && gLastFrameMs < 37.0)
                              ? "FRAME LIMITER OK" : "SUSPECT - limiter missed its budget");
         }
      }

      // 3D geometry density stress fixture, measurement half - see the setup
      // half above (INFINITE_GEOMDENSITYTEST, before the main loop starts).
      // Explicit CookIfNeeded is needed every frame because the main loop's
      // ordinary per-frame cook pass (above, "apply modulation and palette")
      // only walks Output/Syphon/OscSend nodes - a bare Render 3D with
      // nothing downstream would otherwise never cook at all. Uncapped +
      // vsync off so the frame limiter/display refresh don't mask the real
      // per-frame cost; 30 frames of warmup (mesh upload, driver
      // shader/FBO allocation) then 120 sampled frames, matching the
      // node-chain FPS investigation's sampling window.
      if (getenv("INFINITE_GEOMDENSITYTEST") != nullptr)
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
            printf("GEOMDENSITYTEST tris=%zu avg=%.3fms min=%.3fms max=%.3fms fps=%.2f\n",
                   render->LastTriangleCount(), avg, sMin, sMax, fps);
            printf("%s\n", sSampleCount > 0 ? "GEOMDENSITYTEST DONE" : "GEOMDENSITYTEST FAIL (no samples)");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(b) node-count scaling fixture, measurement half - see
      // INFINITE_BENCH_B5NODES's setup above. Same warmup/sample window as
      // the other perf fixtures in this file (32 warmup, 120 sampled,
      // uncapped/vsync off) so results are comparable. Uses the shared
      // Bench::BenchReport/PercentileRing infra (src/core/BenchReport.h)
      // rather than this file's older avg/min/max-only pattern, per
      // benchmark-suite.md §3 (p50/p95/p99 matter more than average for live
      // work) and §5 (one BENCH_JSON line, not printf-formatted text).
      if (getenv("INFINITE_BENCH_B5NODES") != nullptr)
      {
         static Bench::PercentileRing sFrameMs;
         static double sRssStartMb = -1.0;
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; sRssStartMb = Bench::ProcessRssMb(); }
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
            sFrameMs.Push(gLastFrameMs);
         if (frameId == 152)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_nodecount";
            report.variant = getenv("INFINITE_BENCH_B5NODES") ? getenv("INFINITE_BENCH_B5NODES") : "";
            report.frames = 152;
            report.nodes = (int)gNodes.size();
            report.frameMs = sFrameMs;
            report.memRssStartMb = sRssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5NODES DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // B5(c) per-stage CPU timing fixture, measurement half - see
      // INFINITE_BENCH_B5STAGES setup above. Same warmup/sample window as
      // B5(a)/B5(b). Uses ScopedStageTimer/ConditionalStageTimer wired into
      // each main-loop stage to report median ms per stage in stagesCpuMs.
      if (isBenchB5c)
      {
         if (frameId == 2) { gVsync = false; SetCanvasSwapInterval(0); gTargetFps = 0; sBenchB5cRssStartMb = Bench::ProcessRssMb(); }
         if (frameId >= 32 && frameId < 152 && gLastFrameMs > 0.0)
            sBenchB5cFrameMs.Push(gLastFrameMs);
         if (frameId == 152)
         {
            Bench::BenchReport report;
            report.bench = "B5_fundamentals_stages";
            const char* bArg = getenv("INFINITE_BENCH_B5STAGES") ? getenv("INFINITE_BENCH_B5STAGES") : getenv("INFINITE_BENCH_B5C");
            report.variant = (bArg && *bArg && atol(bArg) > 1) ? (std::string("n=") + bArg) : "n=100";
            report.frames = 152;
            report.nodes = (int)gNodes.size();
            report.frameMs = sBenchB5cFrameMs;
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
            report.memRssStartMb = sBenchB5cRssStartMb;
            report.memRssEndMb = Bench::ProcessRssMb();
            report.Emit();
            printf("B5STAGES DONE\n");
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

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

      ConditionalStageTimer timerNodeBodies(benchStagesCpuSample ? &sStageNodeBodies : nullptr, Bench::FrameTail::kNodeBodies);
      Bench::ConditionalGpuStageTimer timerNodeBodiesGpu(benchStagesSample ? &sGpuTimerRing : nullptr, "node_bodies", frameId);
      PruneDeadGroups();

      const bool b6TrackVis = isBenchB6 && (frameId >= 32 && frameId < sBenchB6TotalFrames);
      int b6FrameVisibleCount = 0;
      int b6FrameBodiesDrawnCount = 0;
      double b6FrameOffscreenMs = 0.0;

      RefreshLiveIssues();
      for (GraphNode& gn : gNodes)
      {
         // Build step 15 ("Instrument Mode"): a node mounted by an
         // encapsulated FieldGraphNode is a real gNodes entry - it still
         // cooks, still counts toward audio topology, and its own params
         // still resolve via VisitParams (see ApplyModulationAndPalette's
         // per-mounted-child loop and RebuildAudioTopology, both of which
         // walk gNodes unconditionally, hidden or not) - but it gets no
         // ed::BeginNode/EndNode this frame at all, so it is neither drawn
         // nor pickable/selectable/draggable in the node editor (doc §3.2).
         // Known, deliberately scoped gap (flagged, not silently built
         // partial): a hidden child's OWN param pins do not re-register
         // for direct modulation while hidden, because doing so safely
         // would require running the ~500-line per-node param dispatch
         // chain outside its normal ed::BeginNode/ImGui-window context,
         // which is a much larger refactor than this step's exit criterion
         // requires (nothing in the FIELDGRAPHENCAPTEST assertions tests
         // it) - a pre-existing direct binding on a child stays wired but
         // stops being driven for as long as that child is hidden. The
         // FieldGraphNode's OWN declared params (the normal way to modulate
         // an encapsulated instrument, §4) are unaffected - those register
         // normally since the FieldGraphNode box itself is never hidden.
         if (gn.hiddenFromCanvas)
            continue;

         bool b6NodeIsVisible = true;
         double b6NodeDrawStartMs = 0.0;
         const double tailNodeStartMs = Bench::Tail().active ? Bench::ScopedStageTimer::NowMs() : 0.0;
         if (b6TrackVis)
         {
            const ImVec2 np = ed::GetNodePosition(gn.NodeId());
            ImVec2 ns = ed::GetNodeSize(gn.NodeId());
            if (ns.x <= 0.0f || ns.y <= 0.0f)
               ns = ImVec2(200.0f, 150.0f);
            const ImVec2 pMinScreen = ed::CanvasToScreen(np);
            const ImVec2 pMaxScreen = ed::CanvasToScreen(ImVec2(np.x + ns.x, np.y + ns.y));
            const ImGuiIO& io = ImGui::GetIO();
            b6NodeIsVisible = !(pMaxScreen.x < 0.0f || pMinScreen.x > io.DisplaySize.x ||
                                pMaxScreen.y < 0.0f || pMinScreen.y > io.DisplaySize.y);
            if (b6NodeIsVisible)
               b6FrameVisibleCount++;
            b6FrameBodiesDrawnCount++;
            if (!b6NodeIsVisible)
               b6NodeDrawStartMs = Bench::ScopedStageTimer::NowMs();
         }

         if (gn.needsPosition)
         {
            ed::SetNodePosition(gn.NodeId(), ImVec2(gn.spawnX, gn.spawnY));
            gn.needsPosition = false;
         }

         // Cached here, inside the editor context, for anything that needs a
         // position later in the frame when the context is gone.
         {
            const ImVec2 live = ed::GetNodePosition(gn.NodeId());
            if (std::isfinite(live.x) && std::abs(live.x) <= 1e6f && live.x > -2e9f)
               gn.liveX = live.x;
            else if (!std::isfinite(gn.liveX) || std::abs(gn.liveX) > 1e6f || gn.liveX <= -2e9f)
               gn.liveX = gn.spawnX;

            if (std::isfinite(live.y) && std::abs(live.y) <= 1e6f && live.y > -2e9f)
               gn.liveY = live.y;
            else if (!std::isfinite(gn.liveY) || std::abs(gn.liveY) > 1e6f || gn.liveY <= -2e9f)
               gn.liveY = gn.spawnY;
         }

         if (auto* group = dynamic_cast<GroupNode*>(gn.node.get()))
         {
            DrawGroupNode(gn, group);
            continue;
         }

         // Off-screen culling: a node well outside the view keeps the box and
         // pins it had last time it was laid out (so cables to it still land,
         // see KeepOffscreenNodeAlive) and skips its body. Same gate as the
         // collapsed node's register-only pass below: a parameter only exists
         // for anything that writes it (GraphNode::IsParamDriven - modulation,
         // palette, expressions, the performance panel, gesture loops) in the
         // frames it draws, so a driven node is always drawn. So is anything while a popup is open (its contents are
         // submitted from the body), and every node once per kCullRefresh
         // frames, staggered, so size or pin changes made while it is away
         // (a new param, an input count) show up within half a second.
         {
            constexpr int kCullRefresh = 30;
            constexpr float kCullMargin = 64.0f;
            const bool mustDraw = gn.IsParamDriven() || gHeadlessProbeAll ||
                                  ImGui::GetCurrentContext()->OpenPopupStack.Size > 0 ||
                                  ((frameId + gn.index) % kCullRefresh) == 0;
            if (!mustDraw && ed::KeepOffscreenNodeAlive(gn.NodeId(), kCullMargin))
            {
               if (b6TrackVis)
               {
                  b6FrameBodiesDrawnCount--;
                  if (!b6NodeIsVisible)
                     b6FrameOffscreenMs += (Bench::ScopedStageTimer::NowMs() - b6NodeDrawStartMs);
               }
               continue;
            }
            if (gHeadlessProbeAll)
               gHeadlessDrawn.insert(gn.index);
         }

         // Category tint: same idea as DrawGroupNode's stored colour, but from
         // the static per-category table since categories are a fixed
         // vocabulary, not something a user repicks per node. Blended into the
         // library's own default NodeBg rather than replacing it outright, so
         // a node still reads as "the same kind of card", just tinted.
         const CategoryColors::UiTheme& t = CategoryColors::CurrentUiTheme();
         const CategoryColors::Color& catColor = CategoryColors::ColorFor(gn.category);
         const bool isLight = CategoryColors::IsThemeLight();
         const float kTintWeight = CategoryColors::GetTintWeight();
         const float nodeAlpha = CategoryColors::GetNodeOpacity();
         const bool isComment = dynamic_cast<CommentNode*>(gn.node.get()) != nullptr;
         // D1 (geometry-domains audit, Phase 4): a node whose geometry input
         // doesn't satisfy what it asked for (see DescribeGeometryMismatch,
         // Geometry3DNodes.h) gets a red border instead of its usual category
         // tint, with the message on hover - the "red node" the plan's D1
         // decision called for, in place of a connect-time refusal.
         const auto* warnSrc = dynamic_cast<ICookWarningSource*>(gn.node.get());
         const bool hasCookWarning = warnSrc != nullptr && !warnSrc->CookWarning().empty();
         // R30: schema warnings from the last live validate pass. A cook
         // warning is the louder claim (the node is doing the wrong thing now),
         // so it keeps the red border and the amber one shows only without it.
         const auto liveIt = gLiveIssues.find(gn.index);
         const bool hasLiveIssue = !hasCookWarning && liveIt != gLiveIssues.end();
         if (isComment)
         {
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
         }
         else
         {
            ed::PushStyleColor(ed::StyleColor_NodeBg,
                               ImColor(t.panelBg.r * (1.0f - kTintWeight) + catColor.r * kTintWeight,
                                       t.panelBg.g * (1.0f - kTintWeight) + catColor.g * kTintWeight,
                                       t.panelBg.b * (1.0f - kTintWeight) + catColor.b * kTintWeight,
                                       nodeAlpha));
            if (hasCookWarning)
            {
               ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0.95f, 0.25f, 0.2f, 0.9f));
               ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f);
            }
            else if (hasLiveIssue)
            {
               ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(0.95f, 0.65f, 0.15f, 0.9f));
               ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f);
            }
            else
            {
               ed::PushStyleColor(ed::StyleColor_NodeBorder,
                                  ImColor(catColor.r, catColor.g, catColor.b, isLight ? 0.75f : 0.55f));
            }
         }

         ed::BeginNode(gn.NodeId());
         gInsideNodeCanvas = true;
         ImGui::PushID(gn.index);
         const bool dimmed = gn.node->bypassed;
         if (dimmed)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);

         const bool isAudioBody = IsAudioBodyNode(gn.node.get());
         auto* mixerNode = dynamic_cast<MixerNode*>(gn.node.get());

         // --- inputs spread along the top edge ---
         const ImVec2 topRowPos = ImGui::GetCursorPos();
         int inputs = InputCountFor(gn);
         float maxInputY = topRowPos.y;

         if (mixerNode != nullptr && mixerNode->numChannels > 0)
         {
            const float expectedW = AudioNodeWidth(gn.node.get());
            const float cellW = expectedW / (float)mixerNode->numChannels;
            for (int slot = 0; slot < inputs; slot++)
            {
               char label[24];
               if (const char* named = gn.node->InputLabel(slot))
                  snprintf(label, sizeof(label), "%s", named);
               else
                  snprintf(label, sizeof(label), "%d", slot + 1);

               const float pinW = kPinHit + 4.0f + ImGui::CalcTextSize(label).x;
               const float pinX = topRowPos.x + ((float)slot + 0.5f) * cellW - pinW * 0.5f;
               ImGui::SetCursorPos(ImVec2(pinX, topRowPos.y));
               DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
            }
            maxInputY = std::max(maxInputY, ImGui::GetCursorPosY());
         }
         else
         {
            const float pinSpacing = (inputs > 4) ? 8.0f : 12.0f;
            for (int slot = 0; slot < inputs; slot++)
            {
               char label[24];
               if (const char* named = gn.node->InputLabel(slot))
                  snprintf(label, sizeof(label), "%s", named);
               else if (inputs == 1)
                  label[0] = '\0';
               else
                  snprintf(label, sizeof(label), "%c", 'A' + slot);
               DrawPin(gn.InputPinId(slot), ed::PinKind::Input, label);
               if (slot + 1 < inputs)
                  ImGui::SameLine(0.0f, pinSpacing);
            }
            if (inputs > 0)
               maxInputY = std::max(maxInputY, ImGui::GetCursorPosY());
         }

         if (isAudioBody && !isComment)
         {
            const float expectedW = AudioNodeWidth(gn.node.get());
            bool* globalScalePtr = GetNodeGlobalScaleFlag(gn.node.get());
            if (globalScalePtr != nullptr)
            {
               ImGui::SetCursorPos(ImVec2(topRowPos.x + expectedW - 44.0f, topRowPos.y));
               if (GlobalScaleToggle(*globalScalePtr))
               {
                  PushUndoCheckpoint();
                  *globalScalePtr = !(*globalScalePtr);
               }
            }

            ImGui::SetCursorPos(ImVec2(topRowPos.x + expectedW - 22.0f, topRowPos.y));
            if (CanBypass(gn) && BypassToggle(gn.node->bypassed))
            {
               PushUndoCheckpoint();
               gn.node->bypassed = !gn.node->bypassed;
               RebuildAudioTopology();
            }
            maxInputY = std::max(maxInputY, topRowPos.y + 18.0f);
         }

         if (isComment)
            ImGui::SetCursorPos(topRowPos);
         else if (inputs > 0 || isAudioBody)
            ImGui::SetCursorPos(ImVec2(topRowPos.x, maxInputY + 4.0f));

         // Group the body so its measured width can right-align the out pin.
         // ed::GetNodeSize() is scaled by the current zoom, so feeding it back
         // into padding inflated the node a little more every frame until it
         // covered the canvas and swallowed every click.
         ImGui::BeginGroup();

         if (!isComment)
         {
            ImGui::TextUnformatted(NodeTitle(gn).c_str());
            int instanceTotal = 0;
            const int instanceIdx = GetNodeInstanceIndex(gn, &instanceTotal);
            if (instanceTotal > 1)
            {
               ImGui::SameLine(0.0f, 4.0f);
               ImGui::TextDisabled("#%d", instanceIdx);
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
            if (isLight)
               ImGui::PushStyleColor(ImGuiCol_Text,
                                     ImVec4(catColor.r * 0.75f, catColor.g * 0.75f,
                                            catColor.b * 0.75f, 1.0f));
            else
               ImGui::PushStyleColor(ImGuiCol_Text,
                                     ImVec4(catColor.r * 0.6f + 0.4f, catColor.g * 0.6f + 0.4f,
                                            catColor.b * 0.6f + 0.4f, 1.0f));
            ImGui::TextUnformatted(gn.category.c_str());
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
         }

         // Moved ahead of the old call site (right before the showParams
         // dispatch below) so DrawAudioNodeBody's ModSlider calls - which
         // now run inside the preview/body section below, not the params
         // section - get correct pin ids/param registration. Harmless for
         // every other node type: nothing between here and the old call
         // site used gCurrentNodeIndex/gParamCounter before this moved.
         BeginNodeParams(gn.index);

         // --- preview: image for image nodes, a value meter for modulators ---
         const bool multiOutModulator =
            dynamic_cast<ImageAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioFileNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<AudioAnalyzeNode*>(gn.node.get()) != nullptr ||
            dynamic_cast<GeometryTableNode*>(gn.node.get()) != nullptr;
         IGeometrySource* geoSourceForViewport = dynamic_cast<IGeometrySource*>(gn.node.get());
         if (multiOutModulator)
            ; // these draw their own meters in the params panel
         else if (auto* macroKnob = dynamic_cast<MacroKnobNode*>(gn.node.get()))
            DrawMacroKnobBody(macroKnob);
         else if (auto* macroSlider = dynamic_cast<MacroSliderNode*>(gn.node.get()))
            DrawMacroSliderBody(macroSlider);
         else if (auto* macroBipolar = dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()))
            DrawMacroBipolarKnobBody(macroBipolar);
         else if (auto* macroXY = dynamic_cast<MacroXYNode*>(gn.node.get()))
            DrawMacroXYBody(macroXY);
         else if (auto* macroToggle = dynamic_cast<MacroToggleNode*>(gn.node.get()))
            DrawMacroToggleBody(macroToggle);
         else if (auto* macroTrigger = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
            DrawMacroTriggerBody(macroTrigger);
         else if (auto* macroNumBox = dynamic_cast<MacroNumBoxNode*>(gn.node.get()))
            DrawMacroNumBoxBody(macroNumBox);
         else if (auto* macroRadio = dynamic_cast<MacroRadioSelectorNode*>(gn.node.get()))
            DrawMacroRadioSelectorBody(macroRadio);
         else if (auto* macroStepGate = dynamic_cast<MacroStepGateNode*>(gn.node.get()))
            DrawMacroStepGateBody(macroStepGate);
         else if (!isAudioBody && dynamic_cast<DriftNode*>(gn.node.get()) != nullptr)
         {
            // Ahead of the generic IModulator branch: Drift drives one independent value per
            // destination, so the single-history meter below would have to pick one and present
            // it as the node's output. DrawDriftMeter draws the same box with every line in it.
            DrawDriftMeter(dynamic_cast<DriftNode*>(gn.node.get()), gn.index);
         }
         else if (!isAudioBody && dynamic_cast<IModulator*>(gn.node.get()) != nullptr)
         {
            // Audio/note nodes are excluded here even when they implement
            // IModulator: EnvelopeNode does (its output is a modulator
            // value, see audio-graph-semantics.md §6), and this branch used
            // to win the dispatch race against the IsAudioBodyNode branch
            // below - so Envelope silently rendered a generic modulator
            // meter and DrawEnvelopeBody never ran at all.
            DrawModulatorMeter(dynamic_cast<IModulator*>(gn.node.get()), gn.index);
         }
         else if (gn.showMiniViewport && geoSourceForViewport != nullptr &&
                  HasUsefulMiniViewport(gn.node.get()))
            DrawMiniViewport(gn, geoSourceForViewport);
         else if (dynamic_cast<GeometryOpNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<InstanceOnPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ModelSourceNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Text3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<OceanNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<AudioDisplacementNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<AudioRibbonNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<SetColorNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ParticleSystemNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<Group3DNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<DepthProjectionNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                  dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
         {
            const bool isWide = (dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
            const float boxW = isWide ? kWideNodeWidth : kPreviewSize;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            const float h = 52.0f;
            ImGui::Dummy(ImVec2(boxW, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + boxW, origin.y + h);
            dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
            dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
            char line[64] = "";
            if (auto* o = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            {
               // Same frame-of-reference call-out as DrawGeometryOpParams'
               // status line, just shorter - this is the one place a
               // collapsed node's stamp-vs-group behaviour is visible at all.
               if (o->ActsOnInstanceStamp())
               {
                  if (o->op == GeometryOpNode::kTransform)
                     snprintf(line, sizeof(line), "moving group (%zu copies)", o->UpstreamInstanceCount());
                  else
                     snprintf(line, sizeof(line), "%zu tris, stamped x%zu", o->TriangleCount(), o->UpstreamInstanceCount());
               }
               else
                  snprintf(line, sizeof(line), "%zu triangles", o->TriangleCount());
            }
            else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu instances", inst->InstanceCount());
            else if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", model->TriangleCount());
            else if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", t3d->TriangleCount());
            else if (auto* n3d = dynamic_cast<Null3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", n3d->TriangleCount());
            else if (auto* mapn = dynamic_cast<MappingNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", mapn->TriangleCount());
            else if (auto* m2p = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points", m2p->PointCount());
            else if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", oc->TriangleCount());
            else if (auto* mat = dynamic_cast<MaterialNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", mat->TriangleCount());
            else if (auto* disp = dynamic_cast<DisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", disp->TriangleCount());
            else if (auto* adisp = dynamic_cast<AudioDisplacementNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", adisp->TriangleCount());
            else if (auto* arib = dynamic_cast<AudioRibbonNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", arib->TriangleCount());
            else if (auto* setColor = dynamic_cast<SetColorNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu triangles", setColor->TriangleCount());
            else if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu particles", ps->AliveCount());
            else if (auto* cv = dynamic_cast<CurveNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points, %zu tris", cv->PointCount(), cv->TriangleCount());
            else if (auto* mb = dynamic_cast<MetaBallNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu balls, %zu tris", mb->BallCount(), mb->TriangleCount());
            else if (auto* jn = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%d inputs, %zu tris", jn->ConnectedCount(), jn->TriangleCount());
            else if (auto* wr = dynamic_cast<WrapNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu tris", wr->TriangleCount());
            else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "showing input %c", 'A' + sw3->ActiveSlot());
            else if (auto* grp = dynamic_cast<Group3DNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%d in group", grp->GroupChildCount());
            else if (auto* cl = dynamic_cast<ClothNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu tris, %zu links", cl->TriangleCount(), cl->ConstraintCount());
            else if (auto* mrs = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "gen %d, %zu tris", mrs->Generation(), mrs->TriangleCount());
            else if (auto* i2p = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               snprintf(line, sizeof(line), "%zu points", i2p->PointCount());
            else if (auto* dp = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
            {
               if (dp->outputType == DepthProjectionNode::kPoints)
                  snprintf(line, sizeof(line), "%zu points", dp->PointCount());
               else
                  snprintf(line, sizeof(line), "%zu triangles", dp->TriangleCount());
            }
            else
               snprintf(line, sizeof(line), "scene node");
            dl->AddText(ImVec2(origin.x + 12, origin.y + 10),
                        isLight ? IM_COL32(30, 36, 52, 255) : IM_COL32(200, 206, 226, 255),
                        NodeTitleWithInstance(gn).c_str());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 28),
                        isLight ? IM_COL32(95, 105, 125, 255) : IM_COL32(130, 136, 156, 255), line);
         }
         else if (dynamic_cast<GeometryNode*>(gn.node.get()) != nullptr)
         {
            // Geometry emits a mesh, not a picture: show what it is instead of
            // an empty preview box.
            auto* geo = static_cast<GeometryNode*>(gn.node.get());
            ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(kPreviewSize, kPreviewSize * 0.45f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 br(origin.x + kPreviewSize, origin.y + kPreviewSize * 0.45f);
            dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
            dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
            const std::string& name = GeometryNode::ShapeNames()[
               std::max(0, std::min(geo->shape, (int)GeometryNode::ShapeNames().size() - 1))];
            dl->AddText(ImVec2(origin.x + 12, origin.y + 14),
                        isLight ? IM_COL32(30, 36, 52, 255) : IM_COL32(200, 206, 226, 255), name.c_str());
            char tris[48];
            snprintf(tris, sizeof(tris), "%zu triangles", geo->TriangleCount());
            dl->AddText(ImVec2(origin.x + 12, origin.y + 34),
                        isLight ? IM_COL32(95, 105, 125, 255) : IM_COL32(130, 136, 156, 255), tris);
            dl->AddText(ImVec2(origin.x + 12, origin.y + 54),
                        isLight ? IM_COL32(95, 105, 125, 255) : IM_COL32(130, 136, 156, 255), "geometry -> Render 3D");
         }
         else if (auto* draw = dynamic_cast<DrawNode*>(gn.node.get()))
            DrawPaintablePreview(draw);
         else if (auto* comment = dynamic_cast<CommentNode*>(gn.node.get()))
            DrawCommentPreview(comment);
         else if (auto* palette = dynamic_cast<PaletteNode*>(gn.node.get()))
            DrawPalettePreview(palette);
         else if (auto* proj = dynamic_cast<ProjectionNode*>(gn.node.get()))
            DrawProjectionPreview(proj);
         else if (auto* fgnPreview = dynamic_cast<FieldGraphNode*>(gn.node.get()))
         {
            // Build step 15 §5.1/§5.3: an encapsulated FieldGraphNode's
            // boundary output is whatever its terminal emit()-ed node(s)
            // produce - resolve each terminal (in emits order) and preview
            // the first one that actually has a texture, via the same
            // DrawPreview every other image-producing node's body already
            // uses (doc trap 8: no signature change, no second widget).
            // A texture-less terminal falls through to §5.3.1's audio-domain
            // case (RequiresAudioProcessing(), no texture) below; a geometry-
            // only terminal (neither) falls through to "nothing to show",
            // same as an empty/uncompiled program.
            INode* previewTarget = nullptr;
            for (int idx : fgnPreview->TerminalIndices())
            {
               GraphNode* term = FindNodeByIndex(idx);
               if (term != nullptr && term->node && term->node->GetOutputTexture() != 0 &&
                   term->node->GetOutputWidth() > 0)
               {
                  previewTarget = term->node.get();
                  break;
               }
            }
            // §5.4: this is also the node's single (derived) boundary output
            // pin's target - refresh it every draw frame so an outer cable
            // plugged into that pin (main.cpp's ordinary ImageCable/cable-
            // record machinery, resolved generically by pin, not specially
            // for FieldGraphNode) reads whichever terminal currently backs
            // it, re-resolved fresh rather than held stale across a
            // Regenerate()'s own SpawnNode/RemoveNodeByIndex churn.
            fgnPreview->SetBoundaryOutputTarget(previewTarget);
            if (previewTarget != nullptr)
               DrawPreview(previewTarget);
            else
            {
               INode* audioTerminal = nullptr;
               for (int idx : fgnPreview->TerminalIndices())
               {
                  GraphNode* term = FindNodeByIndex(idx);
                  if (term != nullptr && term->node && term->node->RequiresAudioProcessing() &&
                      term->node->GetOutputTexture() == 0)
                  {
                     audioTerminal = term->node.get();
                     break;
                  }
               }
               if (audioTerminal != nullptr)
                  DrawFieldGraphWaveform(fgnPreview, audioTerminal);
            }
         }
         else if (auto* fsnPreview = dynamic_cast<FieldSampleNode*>(gn.node.get()))
            DrawFieldSampleScope(fsnPreview, 60.0f, kPreviewSize);
         else if (auto* fspPreview = dynamic_cast<FieldSynthNode*>(gn.node.get()))
            DrawFieldSynthScope(fspPreview, 60.0f, kPreviewSize);
         else if (isAudioBody)
            DrawAudioNodeBody(gn);
         else
            DrawPreview(gn.node.get());

         // --- params (eye) ---
         // Audio nodes skip the eye toggle entirely: DrawAudioNodeBody above
         // already shows every param, Tier 1 and Tier 2 alike, unconditionally
         // - see docs/plans/audio/audio-node-ui-system.md §1.
         // Audio nodes have nothing this row would add: Tier 1 is never
         // collapsed (so the "mod"/"pal" collapsed-tag affordance has
         // nothing to stand in for), and there is no mesh for the viewport
         // toggle - see the comment above isAudioBody.
         if (!isAudioBody && !isComment)
         {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            const bool isWide = (dynamic_cast<Render3DNode*>(gn.node.get()) != nullptr ||
                                 dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr);
            if (isWide)
            {
               const float offset = std::max(0.0f, (kWideNodeWidth - kViewportSize) * 0.5f);
               if (offset > 0.0f)
                  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            }
            if (EyeToggle(gn.showParams))
               gn.showParams = !gn.showParams;
            ImGui::SameLine();
            if (!CanBypass(gn))
            {
               // Same footprint as BypassToggle, so the toggles to its right
               // sit where they do on every other node.
               ImGui::Dummy(ImVec2(22.0f, 18.0f));
            }
            else if (BypassToggle(gn.node->bypassed))
            {
               PushUndoCheckpoint();
               gn.node->bypassed = !gn.node->bypassed;
               if (dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr ||
                   dynamic_cast<INoteSource*>(gn.node.get()) != nullptr)
               {
                  RebuildAudioTopology();
               }
            }
            // Mini viewport toggle, only for nodes that actually have a mesh to
            // show - excludes CameraNode/LightNode, which appear in the stat-box
            // branch above but implement no geometry interface, and
            // ParticleSystemNode, whose mini viewport never frames usefully
            // (see HasUsefulMiniViewport) - a toggle that does nothing is worse
            // than no toggle.
            if (geoSourceForViewport != nullptr && HasUsefulMiniViewport(gn.node.get()))
            {
               ImGui::SameLine();
               if (ViewportToggle(gn.showMiniViewport))
                  gn.showMiniViewport = !gn.showMiniViewport;
            }
            ImVec2 modTagMin(0.0f, 0.0f), modTagMax(0.0f, 0.0f);
            ImVec2 palTagMin(0.0f, 0.0f), palTagMax(0.0f, 0.0f);
            const bool modTag = !gn.showParams && gn.hasModulatedParams;
            const bool palTag = !gn.showParams && gn.hasPaletteColors;
            if (modTag)
            {
               // make it obvious a collapsed node still has live modulation,
               // and whether any of it is bipolar (swinging around the knob)
               // rather than absolute (overriding it) - see 00-modulation-
               // polarity.md.
               ImGui::SameLine();
               const char* tagText = gn.hasBipolarParams ? "mod\xc2\xb1" : "mod";
               const ImVec2 txtSz = ImGui::CalcTextSize(tagText);
               const ImVec2 p = ImGui::GetCursorScreenPos();
               const ImVec2 tagSz(txtSz.x + 8.0f, txtSz.y + 2.0f);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               dl->AddRectFilled(p, ImVec2(p.x + tagSz.x, p.y + tagSz.y),
                                 isLight ? IM_COL32(255, 235, 200, 200) : IM_COL32(70, 50, 20, 180), 3.0f);
               dl->AddText(ImVec2(p.x + 4.0f, p.y + 1.0f),
                           isLight ? IM_COL32(180, 100, 20, 255) : IM_COL32(255, 190, 90, 255), tagText);
               ImGui::Dummy(tagSz);
               modTagMin = p;
               modTagMax = ImVec2(p.x + tagSz.x, p.y + tagSz.y);
            }
            if (palTag)
            {
               ImGui::SameLine();
               const char* tagText = "pal";
               const ImVec2 txtSz = ImGui::CalcTextSize(tagText);
               const ImVec2 p = ImGui::GetCursorScreenPos();
               const ImVec2 tagSz(txtSz.x + 8.0f, txtSz.y + 2.0f);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               dl->AddRectFilled(p, ImVec2(p.x + tagSz.x, p.y + tagSz.y),
                                 isLight ? IM_COL32(200, 245, 235, 200) : IM_COL32(20, 60, 50, 180), 3.0f);
               dl->AddText(ImVec2(p.x + 4.0f, p.y + 1.0f),
                           isLight ? IM_COL32(20, 140, 110, 255) : IM_COL32(128, 220, 190, 255), tagText);
               ImGui::Dummy(tagSz);
               palTagMin = p;
               palTagMax = ImVec2(p.x + tagSz.x, p.y + tagSz.y);
            }
            // Only once the whole row is laid out: the stubs move the cursor.
            if (modTag)
               CollapsedBindingPins(gn.index, modTagMin, modTagMax, false);
            if (palTag)
               CollapsedBindingPins(gn.index, palTagMin, palTagMax, true);
         }

         // BeginNodeParams(gn.index) already ran above, ahead of the preview/
         // body dispatch - see the comment at that call site.
         // A parameter only exists, as far as modulation is concerned, for the
         // frames it draws in: ModSlider/ModKnob hand the apply pass a raw
         // float* through RegisterParam, and the pass writes through exactly
         // the pointers registered this frame. So closing a node's eye used to
         // silently freeze every modulator and expression patched into it -
         // the cable stayed, the binding stayed, the matrix still listed it,
         // and nothing moved.
         //
         // Fix: a collapsed node that actually has something driving it still
         // runs this whole dispatch, with the UI suppressed three ways -
         // gParamRegisterOnly makes every Mod* widget register its ParamRef
         // and return before it draws or declares a pin (the pins a collapsed
         // node needs come from CollapsedBindingPins instead), SkipItems makes
         // every plain ImGui widget a no-op so the node's size and layout are
         // untouched, and an empty clip rect swallows any raw draw-list work
         // the params body does around them. Gated on there being a binding at
         // all so the common collapsed node costs exactly what it did before.
         const bool registerOnlyParams = !isAudioBody && !isComment && !gn.showParams &&
                                         (gn.IsParamDriven() || gHeadlessProbeAll);
         ImGuiWindow* paramsWindow = ImGui::GetCurrentWindow();
         const bool savedSkipItems = paramsWindow->SkipItems;
         if (registerOnlyParams)
         {
            gParamRegisterOnly = true;
            paramsWindow->SkipItems = true;
            ImGui::GetWindowDrawList()->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f), false);
         }
         if (!isAudioBody && !isComment && (gn.showParams || registerOnlyParams))
         {
            if (auto* n = dynamic_cast<ImageSourceNode*>(gn.node.get()))
               DrawImageSourceParams(n);
            else if (auto* n = dynamic_cast<SlideshowNode*>(gn.node.get()))
               DrawSlideshowParams(n);
            else if (auto* n = dynamic_cast<SyphonInNode*>(gn.node.get()))
               DrawSyphonInParams(n);
            else if (auto* n = dynamic_cast<EnvironmentNode*>(gn.node.get()))
               DrawEnvironmentParams(n);
            else if (auto* n = dynamic_cast<VideoSourceNode*>(gn.node.get()))
               DrawVideoParams(n);
            else if (auto* n = dynamic_cast<VideoInNode*>(gn.node.get()))
               DrawVideoInParams(n);
            else if (auto* n = dynamic_cast<FitNode*>(gn.node.get()))
               DrawFitParams(n);
            else if (auto* n = dynamic_cast<ProjectionNode*>(gn.node.get()))
               DrawProjectionParams(n);
            else if (auto* n = dynamic_cast<LFONode*>(gn.node.get()))
               DrawLFOParams(n);
            else if (auto* n = dynamic_cast<RandomNode*>(gn.node.get()))
               DrawRandomParams(n);
            else if (auto* n = dynamic_cast<DriftNode*>(gn.node.get()))
               DrawDriftParams(gn, n);
            else if (auto* n = dynamic_cast<MovesNode*>(gn.node.get()))
               DrawMovesParams(n);
            else if (auto* n = dynamic_cast<PredictiveModulatorNode*>(gn.node.get()))
               DrawPredictiveModulatorParams(n);
            else if (auto* n = dynamic_cast<PatternNode*>(gn.node.get()))
               DrawPatternParams(n);
            else if (auto* n = dynamic_cast<MathNode*>(gn.node.get()))
               DrawMathParams(n);
            else if (auto* n = dynamic_cast<CompareNode*>(gn.node.get()))
               DrawCompareParams(n);
            else if (auto* n = dynamic_cast<RangeToRangeNode*>(gn.node.get()))
               DrawRangeToRangeParams(n);
            else if (auto* n = dynamic_cast<SmoothNode*>(gn.node.get()))
               DrawSmoothParams(n);
            else if (auto* n = dynamic_cast<InvertNode*>(gn.node.get()))
               DrawInvertParams(n);
            else if (auto* n = dynamic_cast<ModDepthNode*>(gn.node.get()))
               DrawModDepthParams(n);
            else if (auto* n = dynamic_cast<ModCurveNode*>(gn.node.get()))
               DrawModCurveParams(n);
            else if (auto* n = dynamic_cast<CVToPitchNode*>(gn.node.get()))
               DrawCVToPitchParams(n);
            else if (auto* n = dynamic_cast<NoteToCVNode*>(gn.node.get()))
               DrawNoteToCVParams(n);
            else if (auto* n = dynamic_cast<VelocityToCVNode*>(gn.node.get()))
               DrawVelocityToCVParams(n);
            else if (auto* n = dynamic_cast<CVRecorderNode*>(gn.node.get()))
               DrawCVRecorderParams(n);
            else if (auto* n = dynamic_cast<MacroKnobNode*>(gn.node.get()))
               DrawMacroKnobParams(n);
            else if (auto* n = dynamic_cast<MacroSliderNode*>(gn.node.get()))
               DrawMacroSliderParams(n);
            else if (auto* n = dynamic_cast<MacroBipolarKnobNode*>(gn.node.get()))
               DrawMacroBipolarKnobParams(n);
            else if (auto* n = dynamic_cast<MacroXYNode*>(gn.node.get()))
               DrawMacroXYParams(n);
            else if (auto* n = dynamic_cast<MacroToggleNode*>(gn.node.get()))
               DrawMacroToggleParams(n);
            else if (auto* n = dynamic_cast<MacroTriggerNode*>(gn.node.get()))
               DrawMacroTriggerParams(n);
            else if (auto* n = dynamic_cast<MacroNumBoxNode*>(gn.node.get()))
               DrawMacroNumBoxParams(n);
            else if (auto* n = dynamic_cast<MacroRadioSelectorNode*>(gn.node.get()))
               DrawMacroRadioSelectorParams(n);
            else if (auto* n = dynamic_cast<MacroStepGateNode*>(gn.node.get()))
               DrawMacroStepGateParams(n);
            else if (auto* n = dynamic_cast<MidiCCNode*>(gn.node.get()))
               DrawMidiCCParams(n);
            else if (auto* n = dynamic_cast<MidiTriggerNode*>(gn.node.get()))
               DrawMidiTriggerParams(n);
            else if (auto* n = dynamic_cast<NoiseNode*>(gn.node.get()))
               DrawNoiseParams(n);
            else if (auto* n = dynamic_cast<TextureNode*>(gn.node.get()))
               DrawTextureParams(n);
            else if (auto* n = dynamic_cast<RampNode*>(gn.node.get()))
               DrawRampParams(n);
            else if (auto* n = dynamic_cast<PaletteNode*>(gn.node.get()))
               DrawPaletteParams(n);
            else if (auto* n = dynamic_cast<GeometryNode*>(gn.node.get()))
               DrawGeometryParams(n);
            else if (auto* n = dynamic_cast<ModelSourceNode*>(gn.node.get()))
               DrawModelParams(n);
            else if (auto* n = dynamic_cast<Text3DNode*>(gn.node.get()))
               DrawText3DParams(n);
            else if (auto* n = dynamic_cast<MeshToPointsNode*>(gn.node.get()))
               DrawMeshToPointsParams(n);
            else if (auto* n = dynamic_cast<MeshResynthNode*>(gn.node.get()))
               DrawMeshResynthParams(n);
            else if (auto* n = dynamic_cast<ImageToPointsNode*>(gn.node.get()))
               DrawImageToPointsParams(n);
            else if (auto* n = dynamic_cast<DepthProjectionNode*>(gn.node.get()))
               DrawDepthProjectionParams(n);
            else if (auto* n = dynamic_cast<CommentNode*>(gn.node.get()))
               DrawCommentParams(n);
            else if (auto* n = dynamic_cast<PathNode*>(gn.node.get()))
               DrawPathParams(n);
            else if (auto* n = dynamic_cast<GeometryTableNode*>(gn.node.get()))
               DrawGeometryTableParams(n);
            else if (auto* n = dynamic_cast<ConstantNode*>(gn.node.get()))
            {
               ModSlider("value", &n->value, 0.0f, 1.0f);
            }
            else if (auto* n = dynamic_cast<OscReceiveNode*>(gn.node.get()))
               DrawOscReceiveParams(n);
            else if (auto* n = dynamic_cast<OscSendNode*>(gn.node.get()))
               DrawOscSendParams(n);
            else if (auto* n = dynamic_cast<MaterialNode*>(gn.node.get()))
               DrawMaterialParams(n);
            else if (auto* n = dynamic_cast<MappingNode*>(gn.node.get()))
               DrawMappingParams(n);
            else if (auto* n = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
               DrawParticleSystemParams(n);
            else if (auto* n = dynamic_cast<ClothNode*>(gn.node.get()))
               DrawClothParams(n);
            else if (auto* n = dynamic_cast<JoinGeometryNode*>(gn.node.get()))
               DrawJoinGeometryParams(n);
            else if (auto* n = dynamic_cast<MetaBallNode*>(gn.node.get()))
               DrawMetaBallParams(n);
            else if (auto* n = dynamic_cast<CurveNode*>(gn.node.get()))
               DrawCurveParams(n);
            else if (auto* n = dynamic_cast<OceanNode*>(gn.node.get()))
               DrawOceanParams(n);
            else if (dynamic_cast<NullNode*>(gn.node.get()) != nullptr ||
                     dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr)
               ImGui::TextDisabled("pass-through");
            else if (auto* n = dynamic_cast<GeometryOpNode*>(gn.node.get()))
               DrawGeometryOpParams(n);
            else if (auto* n = dynamic_cast<DisplacementNode*>(gn.node.get()))
               DrawDisplacementParams(n);
            else if (auto* n = dynamic_cast<AudioDisplacementNode*>(gn.node.get()))
               DrawAudioDisplacementParams(n);
            else if (auto* n = dynamic_cast<AudioRibbonNode*>(gn.node.get()))
               DrawAudioRibbonParams(n);
            else if (auto* n = dynamic_cast<AudioTextureNode*>(gn.node.get()))
               DrawAudioTextureParams(n);
            else if (auto* n = dynamic_cast<AudioColorRampNode*>(gn.node.get()))
               DrawAudioColorRampParams(n);
            else if (auto* n = dynamic_cast<SetColorNode*>(gn.node.get()))
               DrawSetColorParams(n);
            else if (auto* n = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
               DrawInstanceParams(n);
            else if (auto* n = dynamic_cast<WrapNode*>(gn.node.get()))
               DrawWrapParams(n);
            else if (auto* n = dynamic_cast<DistributePointsOnFacesNode*>(gn.node.get()))
               DrawDistributePointsOnFacesParams(n);
            else if (auto* n = dynamic_cast<PointsToVerticesNode*>(gn.node.get()))
               DrawPointsToVerticesParams(n);
            else if (auto* n = dynamic_cast<DistributePointsInGridNode*>(gn.node.get()))
               DrawDistributePointsInGridParams(n);
            else if (auto* n = dynamic_cast<MergeByDistanceNode*>(gn.node.get()))
               DrawMergeByDistanceParams(n);
            else if (auto* n = dynamic_cast<Switcher3DNode*>(gn.node.get()))
               DrawSwitcher3DParams(n);
            else if (auto* n = dynamic_cast<CameraNode*>(gn.node.get()))
               DrawCameraParams(n);
            else if (auto* n = dynamic_cast<LightNode*>(gn.node.get()))
               DrawLightParams(n);
            else if (auto* n = dynamic_cast<Render3DNode*>(gn.node.get()))
               DrawRender3DParams(n);
            else if (auto* n = dynamic_cast<ImageAnalyzeNode*>(gn.node.get()))
               DrawImageAnalyzeParams(n);
            else if (auto* n = dynamic_cast<NullModulatorNode*>(gn.node.get()))
               DrawNullModulatorParams(n);
            else if (auto* n = dynamic_cast<AudioFileNode*>(gn.node.get()))
               DrawAudioFileParams(n);
            else if (auto* n = dynamic_cast<AudioAnalyzeNode*>(gn.node.get()))
               DrawAudioAnalyzeParams(n);
            else if (auto* n = dynamic_cast<ResynthNode*>(gn.node.get()))
               DrawResynthParams(n);
            else if (auto* n = dynamic_cast<CurvesNode*>(gn.node.get()))
               DrawCurvesParams(n);
            else if (auto* n = dynamic_cast<PredictiveColoringNode*>(gn.node.get()))
               DrawPredictiveColoringParams(n);
            else if (auto* n = dynamic_cast<ColorRampNode*>(gn.node.get()))
               DrawColorRampParams(n);
            else if (auto* n = dynamic_cast<RemoveBgNode*>(gn.node.get()))
               DrawRemoveBgParams(n);
            else if (auto* n = dynamic_cast<DrawNode*>(gn.node.get()))
               DrawDrawParams(n);
            else if (auto* n = dynamic_cast<FeedbackNode*>(gn.node.get()))
               DrawFeedbackParams(n);
            else if (auto* n = dynamic_cast<TrailsNode*>(gn.node.get()))
               DrawTrailsParams(n);
            else if (auto* n = dynamic_cast<ReactionDiffusionNode*>(gn.node.get()))
               DrawReactionDiffusionParams(n);
            else if (auto* n = dynamic_cast<SwitcherNode*>(gn.node.get()))
               DrawSwitcherParams(n);
            else if (auto* n = dynamic_cast<ShapeNode*>(gn.node.get()))
               DrawShapeParams(n);
            else if (auto* n = dynamic_cast<FormulaNode*>(gn.node.get()))
               DrawFormulaParams(n);
            else if (auto* n = dynamic_cast<FieldElementNode*>(gn.node.get()))
               DrawFieldElementParams(n);
            else if (auto* n = dynamic_cast<FieldPrimitiveNode*>(gn.node.get()))
               DrawFieldPrimitiveParams(n);
            else if (auto* n = dynamic_cast<FieldPixelNode*>(gn.node.get()))
               DrawFieldPixelParams(n);
            else if (auto* n = dynamic_cast<FieldSampleNode*>(gn.node.get()))
               DrawFieldSampleParams(n);
            else if (auto* n = dynamic_cast<FieldSynthNode*>(gn.node.get()))
               DrawFieldSynthParams(n);
            else if (auto* n = dynamic_cast<FieldGraphNode*>(gn.node.get()))
               DrawFieldGraphParams(n);
            else if (auto* n = dynamic_cast<TextNode*>(gn.node.get()))
               DrawTextParams(n);
            else if (auto* n = dynamic_cast<LayerStackNode*>(gn.node.get()))
               DrawLayerStackParams(n);
            else if (auto* n = dynamic_cast<BlendNode*>(gn.node.get()))
               DrawBlendParams(n);
            else if (auto* n = dynamic_cast<FilterNode*>(gn.node.get()))
               DrawFilterParams(n);
            else if (auto* n = dynamic_cast<OutputNode*>(gn.node.get()))
            {
               if (n->exportImagePath.empty())
               {
                  n->exportImagePath = AppPaths::DesktopDir() + "/infinite_output." + (n->imageFormat == 1 ? "jpg" : "png");
               }
               if (n->recordVideoPath.empty())
               {
                  n->recordVideoPath = AppPaths::DesktopDir() + "/infinite_output." + (n->videoFormat == 1 ? "mov" : "mp4");
               }

               char imgBuf[512];
               snprintf(imgBuf, sizeof(imgBuf), "%s", n->exportImagePath.c_str());
               ImGui::SetNextItemWidth(kPreviewSize);
               if (ImGui::InputText("##imagePath", imgBuf, sizeof(imgBuf)))
               {
                  n->exportImagePath = imgBuf;
                  std::string low = n->exportImagePath;
                  for (char& c : low) c = (char)tolower((unsigned char)c);
                  if (low.length() >= 4 && (low.rfind(".jpg") == low.length() - 4 || low.rfind(".jpeg") == low.length() - 5))
                     n->imageFormat = 1;
                  else if (low.length() >= 4 && low.rfind(".png") == low.length() - 4)
                     n->imageFormat = 0;
                  gPatchDirty = true;
               }

               const float halfBtnW = (kPreviewSize - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
               const bool pngActive = (n->imageFormat == 0);
               if (pngActive)
                  ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
               if (ImGui::Button(".png##imgPng", ImVec2(halfBtnW, 0)))
               {
                  n->imageFormat = 0;
                  size_t dot = n->exportImagePath.rfind('.');
                  if (dot != std::string::npos)
                     n->exportImagePath = n->exportImagePath.substr(0, dot) + ".png";
                  else
                     n->exportImagePath += ".png";
                  gPatchDirty = true;
               }
               if (pngActive)
                  ImGui::PopStyleColor();
               ImGui::SameLine();

               const bool jpgActive = (n->imageFormat == 1);
               if (jpgActive)
                  ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
               if (ImGui::Button(".jpg##imgJpg", ImVec2(halfBtnW, 0)))
               {
                  n->imageFormat = 1;
                  size_t dot = n->exportImagePath.rfind('.');
                  if (dot != std::string::npos)
                     n->exportImagePath = n->exportImagePath.substr(0, dot) + ".jpg";
                  else
                     n->exportImagePath += ".jpg";
                  gPatchDirty = true;
               }
               if (jpgActive)
                  ImGui::PopStyleColor();

               if (ImGui::Button("Export Image", ImVec2(kPreviewSize, 0)))
                  ExportImage(n, n->exportImagePath);

               ImGui::Dummy(ImVec2(0, 4));

               char vidBuf[512];
               snprintf(vidBuf, sizeof(vidBuf), "%s", n->recordVideoPath.c_str());
               ImGui::SetNextItemWidth(kPreviewSize);
               if (ImGui::InputText("##videoPath", vidBuf, sizeof(vidBuf)))
               {
                  n->recordVideoPath = vidBuf;
                  std::string low = n->recordVideoPath;
                  for (char& c : low) c = (char)tolower((unsigned char)c);
                  if (low.length() >= 4 && low.rfind(".mov") == low.length() - 4)
                     n->videoFormat = 1;
                  else if (low.length() >= 4 && low.rfind(".mp4") == low.length() - 4)
                     n->videoFormat = 0;
                  gPatchDirty = true;
               }

               ImGui::BeginDisabled(n->IsRecording());
               const bool mp4Active = (n->videoFormat == 0);
               if (mp4Active)
                  ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
               if (ImGui::Button(".mp4##vidMp4", ImVec2(halfBtnW, 0)))
               {
                  n->videoFormat = 0;
                  size_t dot = n->recordVideoPath.rfind('.');
                  if (dot != std::string::npos)
                     n->recordVideoPath = n->recordVideoPath.substr(0, dot) + ".mp4";
                  else
                     n->recordVideoPath += ".mp4";
                  gPatchDirty = true;
               }
               if (mp4Active)
                  ImGui::PopStyleColor();
               ImGui::SameLine();

               const bool movActive = (n->videoFormat == 1);
               if (movActive)
                  ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
               if (ImGui::Button(".mov##vidMov", ImVec2(halfBtnW, 0)))
               {
                  n->videoFormat = 1;
                  size_t dot = n->recordVideoPath.rfind('.');
                  if (dot != std::string::npos)
                     n->recordVideoPath = n->recordVideoPath.substr(0, dot) + ".mov";
                  else
                     n->recordVideoPath += ".mov";
                  gPatchDirty = true;
               }
               if (movActive)
                  ImGui::PopStyleColor();
               ImGui::EndDisabled();

               // Both of these are read once, at StartRecording, and latched
               // for the take - the recorder fixes its frame rate and its
               // audio track up front and cannot change either mid-stream.
               // Leaving them live meant dragging fps during a take silently
               // did nothing; now it also has to not desync the pacing that
               // reads it, so say plainly that the take owns them.
               ImGui::BeginDisabled(n->IsRecording());
               ImGui::SetNextItemWidth(kParamWidth);
               ImGui::SliderInt("fps", &n->recordFps, 1, 60);

               ImGui::Checkbox("include audio", &n->includeAudio);
               ImGui::EndDisabled();
               if (n->IsRecording() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  ImGui::SetTooltip("locked for the current take");
               if (n->includeAudio && n->AudioInput().IsConnected())
               {
                  INode* src = n->AudioInput().GetSource();
                  std::string srcName = "connected audio";
                  if (auto* af = dynamic_cast<AudioFileNode*>(src))
                     srcName = af->FileName().empty() ? "Audio File" : af->FileName();
                  else
                  {
                     for (GraphNode& srcGn : gNodes)
                     {
                        if (srcGn.node.get() == src)
                        {
                           srcName = NodeTitleWithInstance(srcGn);
                           break;
                        }
                     }
                  }
                  ImGui::TextDisabled("from: %s", srcName.c_str());
               }

               if (n->StopRequested() || n->IsFinalizing())
               {
                  // StopRequested(): the actual StopRecordingAsync() call
                  // runs at the top of next frame, once this "finalizing"
                  // state has had a chance to reach the screen - see the
                  // pump next to glfwPollEvents(). IsFinalizing(): the
                  // encoder join + movie finalize is running on a background
                  // thread and can take a while on a long/backlogged take,
                  // but doesn't block this UI - a new take can't be started
                  // here (the button stays disabled) since StartRecording()
                  // would otherwise briefly block on WaitForFinalize().
                  ImGui::BeginDisabled();
                  ImGui::Button("Finalizing...", ImVec2(kPreviewSize, 0));
                  ImGui::EndDisabled();
                  // PendingFrames() reads the live handle, which has already
                  // been handed off to the background thread once
                  // IsFinalizing() is true - nothing left here to report.
                  const int pending = n->StopRequested() ? n->PendingFrames() : 0;
                  if (pending > 0)
                     ImGui::TextDisabled("finishing up, %d frames left", pending);
               }
               else if (n->IsRecording())
               {
                  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
                  if (ImGui::Button("Stop recording", ImVec2(kPreviewSize, 0)))
                     n->RequestStopRecording();
                  ImGui::PopStyleColor();
                  ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "REC  %d frames", n->RecordedFrames());
                  const int pending = n->PendingFrames();
                  const int dropped = n->DroppedFrames();
                  if (pending > 0)
                  {
                     ImGui::SameLine();
                     ImGui::TextDisabled("(%d pending)", pending);
                  }
                  if (dropped > 0)
                  {
                     // Same orange as the VST3 blocklist warning - "this is a
                     // problem, not an error": the encoder is losing frames,
                     // but recording is continuing.
                     ImGui::TextColored(ImVec4(0.9f, 0.55f, 0.25f, 1.0f), "%d frames dropped - encoder can't keep up", dropped);
                  }
               }
               else
               {
                  if (ImGui::Button("Record video", ImVec2(kPreviewSize, 0)))
                     n->StartRecording(n->recordVideoPath);
               }
               if (!n->RecordStatus().empty())
               {
                  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kPreviewSize);
                  ImGui::TextDisabled("%s", n->RecordStatus().c_str());
                  ImGui::PopTextWrapPos();
               }

               ImGui::Dummy(ImVec2(0, 8));
               NodeSeparator();
               ImGui::TextDisabled("Offline Render");

               // A take drives the whole patch's Transport/AudioEngine, not
               // just this node - only one can ever be in flight regardless
               // of which OutputNode started it, and it can't overlap this
               // node's own live recording either (both would fight over the
               // same recordVideoPath/EnsureFbo-sized mOut).
               const bool thisNodeRendering = gOfflineRender.node == n;
               const bool otherSessionActive = gOfflineRender.active && !thisNodeRendering;

               ImGui::BeginDisabled(n->IsRecording() || n->IsFinalizing() || gOfflineRender.active);
               ImGui::SetNextItemWidth(kParamWidth);
               ImGui::InputInt("render fps", &n->offlineFps);
               n->offlineFps = std::clamp(n->offlineFps, 1, 240);

               // Duration is typed, not dragged: a render queue's length is a
               // number the user knows ("give me 45 seconds"), and hitting an
               // exact value on a 1..600 slider is fiddly. The presets are
               // the common takes; the field takes anything up to an hour.
               ImGui::SetNextItemWidth(kParamWidth);
               ImGui::InputInt("duration (s)", &n->offlineDurationSeconds);
               n->offlineDurationSeconds = std::clamp(n->offlineDurationSeconds, 1, 3600);
               for (int preset : { 15, 30, 45, 60 })
               {
                  ImGui::PushID(preset);
                  const bool selected = n->offlineDurationSeconds == preset;
                  if (selected)
                     ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                  if (ImGui::Button((std::to_string(preset) + "s").c_str(), ImVec2(kParamWidth * 0.22f, 0)))
                     n->offlineDurationSeconds = preset;
                  if (selected)
                     ImGui::PopStyleColor();
                  ImGui::PopID();
                  if (preset != 60)
                     ImGui::SameLine();
               }

               ImGui::SetNextItemWidth(kParamWidth);
               ImGui::InputInt("preroll frames", &n->offlinePrerollFrames);
               n->offlinePrerollFrames = std::clamp(n->offlinePrerollFrames, 0, 600);
               ImGui::EndDisabled();

               ImGui::TextDisabled("%d frames @ %dfps", n->offlineDurationSeconds * n->offlineFps, n->offlineFps);

               if (thisNodeRendering)
               {
                  // The floating DrawOfflineRenderProgressWindow carries the
                  // live progress/Cancel button; this is just a disabled
                  // placeholder so the button doesn't visually disappear.
                  ImGui::BeginDisabled();
                  ImGui::Button("Rendering...", ImVec2(kPreviewSize, 0));
                  ImGui::EndDisabled();
               }
               else
               {
                  ImGui::BeginDisabled(n->IsRecording() || n->IsFinalizing() || otherSessionActive);
                  if (ImGui::Button("Render", ImVec2(kPreviewSize, 0)))
                     StartOfflineRenderSession(n);
                  ImGui::EndDisabled();
               }
            }
            else if (auto* n = dynamic_cast<SyphonOutNode*>(gn.node.get()))
               DrawSyphonOutParams(n);
         }
         if (registerOnlyParams)
         {
            ImGui::GetWindowDrawList()->PopClipRect();
            paramsWindow->SkipItems = savedSkipItems;
            gParamRegisterOnly = false;
         }

         ImGui::EndGroup();
         const float contentW = ImGui::GetItemRectSize().x;

         // --- output dots, bottom-right: cables start here ---
         // A comment is not in the signal graph, and an out pin on one is worse
         // than useless: link validation only asks whether a source is an image
         // node, so a comment would happily patch into any image input and feed
         // it a blank texture. No pin, no way to make that mistake.
         // FieldGraphNode is a meta-node - it spawns/wires other real nodes at
         // edit time and never produces a picture of its own (GetOutputTexture
         // always returns 0), so an out pin on it is exactly as misleading as
         // one on a comment would be.
         if (dynamic_cast<OutputNode*>(gn.node.get()) == nullptr && !isComment &&
             dynamic_cast<FieldGraphNode*>(gn.node.get()) == nullptr)
         {
            // GeometryTableNode draws its row pins (index 4 and up) itself,
            // inline in the table grid in its params panel - only the four
            // aggregates (cx/cy/cz/spread) go through the generic row here.
            // Drawing the same pin id through ed::BeginPin() twice in one
            // frame is not something imgui-node-editor supports.
            auto* geoTable = dynamic_cast<GeometryTableNode*>(gn.node.get());
            auto* drumSeq = dynamic_cast<DrumSequencerNode*>(gn.node.get());
            const int outputs = geoTable != nullptr ? 4 : (drumSeq != nullptr ? 1 : std::max(1, gn.node->OutputCount()));
            std::vector<float> pinW(outputs);
            float itemW = 0.0f;
            for (int o = 0; o < outputs; o++)
            {
               pinW[o] = kPinHit + 4.0f + ImGui::CalcTextSize(gn.node->OutputLabel(o)).x;
               itemW += pinW[o] + (o ? 10.0f : 0.0f);
            }
            if (itemW <= contentW)
            {
               float pad = std::max(0.0f, contentW - itemW);
               ImGui::Dummy(ImVec2(pad, 1.0f));
               for (int o = 0; o < outputs; o++)
               {
                  ImGui::SameLine(0.0f, o == 0 ? 0.0f : 10.0f);
                  DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
               }
            }
            else
            {
               // Too many pins to fit one row: wrap greedily, rows left-aligned.
               float rowW = 0.0f;
               bool firstInRow = true;
               for (int o = 0; o < outputs; o++)
               {
                  float w = pinW[o];
                  bool wouldOverflow = !firstInRow && (rowW + 10.0f + w > contentW);
                  if (wouldOverflow)
                  {
                     firstInRow = true;
                     rowW = 0.0f;
                  }
                  if (!firstInRow)
                  {
                     ImGui::SameLine(0.0f, 10.0f);
                     rowW += 10.0f;
                  }
                  DrawPin(gn.OutputPinId(o), ed::PinKind::Output, gn.node->OutputLabel(o), true);
                  rowW += w;
                  firstInRow = false;
               }
            }
         }

         if (dimmed)
            ImGui::PopStyleVar();
         ImGui::PopID();
         gInsideNodeCanvas = false;
         ed::EndNode();
         if (hasCookWarning && ed::GetHoveredNode() == ed::NodeId(gn.NodeId()))
            ImGui::SetTooltip("%s", warnSrc->CookWarning().c_str());
         else if (hasLiveIssue && ed::GetHoveredNode() == ed::NodeId(gn.NodeId()))
         {
            std::string tip;
            for (const Headless::Issue& w : liveIt->second)
            {
               if (!tip.empty())
                  tip += "\n";
               tip += w.message;
               if (!w.hint.empty())
                  tip += "\n  -> " + w.hint;
            }
            ImGui::SetTooltip("%s", tip.c_str());
         }
         ed::PopStyleColor(2);
         if (isComment)
            ed::PopStyleVar(3);
         else if (hasCookWarning || hasLiveIssue)
            ed::PopStyleVar();

         if (b6TrackVis && !b6NodeIsVisible)
         {
            b6FrameOffscreenMs += (Bench::ScopedStageTimer::NowMs() - b6NodeDrawStartMs);
         }
         if (Bench::Tail().active)
            Bench::Tail().AddNode(gn.typeName, Bench::ScopedStageTimer::NowMs() - tailNodeStartMs);
      }

      // B6 only: end node_bodies here so links get their own stage. Other
      // fixtures keep the old span (bodies through the arrange overlay) so
      // their recorded baselines stay comparable; a GL timer query can't
      // nest, so the links GPU timer must not start inside that span either.
      if (benchB6Stages)
      {
         timerNodeBodies.Stop();
         timerNodeBodiesGpu.Stop();
      }
      if (b6TrackVis)
      {
         sBenchB6VisibleNodesSum += (double)b6FrameVisibleCount;
         sBenchB6BodiesDrawnSum += (double)b6FrameBodiesDrawnCount;
         sBenchB6OffscreenBodyMsSum += b6FrameOffscreenMs;
         sBenchB6SampledFrames++;
      }

      ConditionalStageTimer timerLinks((benchB6Stages && benchStagesCpuSample) ? &sStageLinks : nullptr, Bench::FrameTail::kLinks);
      Bench::ConditionalGpuStageTimer timerLinksGpu((benchB6Stages && benchStagesSample) ? &sGpuTimerRing : nullptr, "links", frameId);

      // ---- draw existing links ----
      // Link ids are derived from the destination pin id (kLinkIdBase +
      // dstPin), not from this vector's insertion position. A pin can carry
      // at most one incoming cable, so dstPin is already unique per link and
      // - critically - stable across frames: deleting one cable no longer
      // shifts every link *after* it in gNodes/slot iteration order down to
      // a lower position and hence a different id. That shift used to hand a
      // just-deleted link's id to a completely unrelated surviving link on
      // the very next frame (e.g. deleting a Sampler's note cable could
      // reassign its id to the Sampler's own audio-out cable, which
      // imgui-node-editor - having just processed a deletion for that same
      // id - then also treated as deleted). Positional ids still fit in one
      // collision-free space across image/audio/note/modulation/palette
      // cables, same as before; only the offset changed.
      gLinks.clear();
      for (GraphNode& gn : gNodes)
      {
         int inputs = InputCountFor(gn);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(gn, slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;

            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(cable->GetSourceOutput()), gn.InputPinId(slot) });
                  break;
               }
            }
         }
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = gn.node->AudioInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(cable->GetOutputSlot()), gn.InputPinId(slot) });
                  break;
               }
            }
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = gn.node->NoteInputSlot(slot);
            if (cable == nullptr || !cable->IsConnected())
               continue;
            for (GraphNode& src : gNodes)
            {
               if (src.node.get() == cable->GetSource())
               {
                  gLinks.push_back({ kLinkIdBase + gn.InputPinId(slot),
                                     src.OutputPinId(cable->GetOutputSlot()), gn.InputPinId(slot) });
                  break;
               }
            }
         }
      }
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
            continue; // no pin declared this frame: emitting the link would kill it
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
                        rejectReason = "Cannot modulate a parameter driven by a Field Graph kernel";
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
                        rejectReason = "Cannot connect: this would create an audio feedback loop";
                     }
                     if (valid && srcIsNoteSource &&
                         WouldCreateNoteCycle(srcNode->node.get(), dstNode->node.get()))
                     {
                        valid = false;
                        rejectReason = "Cannot connect: this would create a note feedback loop";
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
                     rejectReason = "A node can't be connected to itself";
                  }
                  else if (!GraphNode::IsOutputPin(a) || GraphNode::IsOutputPin(b))
                  {
                     rejectReason = "Drag from an output pin on the right of a node to an input pin on the left of another";
                  }
                  else if (GraphNode::IsColorPin(b))
                  {
                     rejectReason = "This color slot only accepts a Palette node";
                  }
                  else if (GraphNode::IsParamPin(b))
                  {
                     if (srcIsAudioNode || srcIsNoteSource)
                        rejectReason = "Audio/note signals can't drive a parameter pin - only a modulator can";
                     else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
                        rejectReason = "3D objects cannot drive a parameter pin - only a modulator can";
                     else if (!srcIsModulator)
                        rejectReason = "Only modulator nodes (LFO, Envelope, Formula, etc.) can drive a parameter pin";
                  }
                  else if (srcIsPredictor && GraphNode::IsInputPin(b))
                  {
                     // Source-driven refusal: it applies to every slot on every
                     // node, so it is read before any of the destination-shaped
                     // messages in the branch below.
                     rejectReason = "Predictive LFO / Macro can only drive a parameter, knob or slider - not another node";
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
                           ? "A modulator can't drive an audio signal pin - only another audio source can"
                           : "This pin only accepts an audio source";
                     else if (dstWantsNote && !srcIsNoteSource)
                        rejectReason = "This pin only accepts a note source";
                     else if ((srcIsAudioNode || srcIsNoteSource) && !dstWantsAudio && !dstWantsNote)
                        rejectReason = "Audio/note signals only connect to a matching audio/note pin";
                     else if (dstRenderNode != nullptr)
                     {
                        if (slot < Render3DNode::kSlots)
                        {
                           if (srcCamera != nullptr)
                              rejectReason = "Camera connects to the Camera slot (slot 5), not geometry slots";
                           else if (srcLight != nullptr)
                              rejectReason = "Light connects to the Light slots (slots 6-8), not geometry slots";
                           else if (srcIsEnvironment)
                              rejectReason = "HDRI connects to the Environment slot (slot 9), not geometry slots";
                           else
                              rejectReason = "Render 3D geometry slots only accept 3D geometry sources";
                        }
                        else if (slot == Render3DNode::kSlots)
                           rejectReason = "This slot only accepts a Camera 3D node";
                        else if (slot == Render3DNode::kEnvSlot)
                           rejectReason = "Environment slot only accepts an HDRI Environment node";
                        else
                           rejectReason = "This slot only accepts a Light 3D node";
                     }
                     else if (dstMaterialNode != nullptr && slot >= 1 && slot <= kMapCount)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = "Material map slots accept 2D images or textures, not 3D geometry";
                        else if (srcIsModulator)
                           rejectReason = "Material map slots accept 2D images or textures, not modulators";
                        else
                           rejectReason = "Material map slots accept 2D images or textures";
                     }
                     else if (dstDispNode != nullptr && slot == 1)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = "Displacement height slot accepts a 2D image or texture map, not 3D geometry";
                        else
                           rejectReason = "Displacement height slot accepts a 2D image or texture map";
                     }
                     else if (dstSetColorNode != nullptr && slot == 2)
                     {
                        rejectReason = "Set Vertex Color palette slot only accepts a Palette node";
                     }
                     else if (dstSetColorNode != nullptr && slot == 1)
                     {
                        if (srcGeometry != nullptr)
                           rejectReason = "Set Vertex Color texture slot accepts a 2D image or texture map, not 3D geometry";
                        else
                           rejectReason = "Set Vertex Color texture slot accepts a 2D image or texture map";
                     }
                     else if (dstMappingNode != nullptr)
                     {
                        rejectReason = "Mapping transforms 3D surface coordinates. Wire 3D geometry into Mapping, then into Material or Render 3D.";
                     }
                     else if (dstNode->node->GeometryInputSlot(slot) != nullptr)
                     {
                        if (srcCamera != nullptr || srcLight != nullptr)
                           rejectReason = "Camera and Light nodes connect to Render 3D, not geometry operators";
                        else
                           rejectReason = "This pin requires a 3D geometry source, not a 2D image";
                     }
                     else if (srcGeometry != nullptr || srcCamera != nullptr || srcLight != nullptr)
                     {
                        rejectReason = "3D geometry cannot be connected directly to a 2D image node. Connect geometry into a Render 3D node first.";
                     }
                     else if (dynamic_cast<AudioAnalyzeNode*>(dstNode->node.get()) != nullptr)
                     {
                        rejectReason = "Audio Analyze accepts any audio source - Audio In, Audio File, an effect, a Mixer";
                     }
                     else if (dstNode->node->ModulatorInputSlot(slot) != nullptr && dynamic_cast<ImageAnalyzeNode*>(dstNode->node.get()) == nullptr)
                     {
                        rejectReason = "This pin only accepts a modulator source";
                     }
                     else if (srcIsModulator)
                     {
                        rejectReason = "Image inputs accept 2D image sources, not modulators";
                     }
                     else
                     {
                        rejectReason = "Incompatible connection";
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
      const bool typing = io.WantTextInput || gNavOwnsKeys;
      const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;

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
      }

      // "/" drops a comment under the pointer and puts the caret straight into
      // it, so annotating a patch is one keystroke and then typing. Not gated on
      // Shift, so "?" does not leave a stray comment behind, and not on a
      // modifier, so Cmd-/ stays free for a binding later.
      const bool doAddComment = gRequestAddComment || (!typing && gCommentEdit.target == nullptr && !cmdOrCtrl && !io.KeyShift &&
                                                      ImGui::IsKeyPressed(ImGuiKey_Slash, false));
      gRequestAddComment = false;
      if (doAddComment)
      {
         // The pointer is only meaningful over the canvas; anywhere else (the
         // node panel, off the window entirely) the middle of the view is the
         // only sensible place for it.
         const ImVec2 mouse = ImGui::GetMousePos();
         const bool overGraph = mouse.x >= gGraphScreenTL.x &&
                                mouse.y >= gGraphScreenTL.y &&
                                mouse.x <= gGraphScreenTL.x + gGraphScreenSize.x &&
                                mouse.y <= gGraphScreenTL.y + gGraphScreenSize.y;
         const ImVec2 at = overGraph ? ed::ScreenToCanvas(mouse) : gViewCenterCanvas;
         if (GraphNode* gn = SpawnNode("Comment", "Compositing", at.x, at.y))
         {
            gCommentEdit.target = static_cast<CommentNode*>(gn->node.get());
            gCommentEdit.justOpened = true;
            // The '/' itself is already in the queue for this frame; without
            // this it lands in the note that is about to take the keyboard and
            // every comment starts with a slash.
            io.InputQueueCharacters.resize(0);
         }
      }

      // Space toggles play/pause on the transport, mirroring the Play/Pause
      // button, so the timeline can be started or paused without reaching
      // for the mouse.
      if (!typing && !cmdOrCtrl && !io.KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_Space, false))
         Transport::Instance().TogglePlay();

      // ---- Shift+<letter> canvas shortcuts ----
      // One family, all reachable with the left hand while the right stays on
      // the trackpad: V/X act on the selection, M/N/H/K are canvas-wide
      // toggles. All of them are gated on Shift alone (never Cmd/Ctrl), so
      // they can't collide with the system/menu bindings above.
      const bool shiftOnly = !cmdOrCtrl && io.KeyShift;

      // Shift+V:
      // - When one or more eligible nodes are selected: opens the viewport panel
      //   with those nodes (adds missing cards and ensures panel is open; if the panel
      //   is already open and all selected cards are already docked, toggles them off).
      // - When no node is selected (e.g. after clicking on blank canvas): toggles the
      //   whole viewport panel view on / off.
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_V, false))
      {
         const int count = ed::GetSelectedObjectCount();
         std::vector<int> eligible;
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               // Same gate as the "Open in viewport panel" context-menu entry,
               // so the keyboard can never open a card the menu wouldn't -
               // modulators, cameras/lights, comments and audio nodes have no
               // texture to show and would render an empty box.
               if (gn != nullptr && CanShowInViewportPanel(*gn))
                  eligible.push_back(gn->index);
            }
         }

         if (!eligible.empty())
         {
            if (!gViewportPanelOpen)
            {
               for (int idx : eligible)
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx) ==
                      gViewportPanelNodes.end())
                     gViewportPanelNodes.push_back(idx);
               }
               gViewportPanelOpen = true;
            }
            else
            {
               bool allOpen = true;
               for (int idx : eligible)
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx) ==
                      gViewportPanelNodes.end())
                     allOpen = false;
               }

               if (allOpen)
               {
                  for (int idx : eligible)
                  {
                     auto it = std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx);
                     if (it != gViewportPanelNodes.end())
                        gViewportPanelNodes.erase(it);
                  }
                  if (gViewportPanelNodes.empty())
                     gViewportPanelOpen = false;
               }
               else
               {
                  for (int idx : eligible)
                  {
                     if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), idx) ==
                         gViewportPanelNodes.end())
                        gViewportPanelNodes.push_back(idx);
                  }
                  gViewportPanelOpen = true;
               }
            }
         }
         else
         {
            gViewportPanelOpen = !gViewportPanelOpen;
         }
      }

      // Shift+M: the docked modulation matrix, same panel the modulator
      // context menu's "Show modulation matrix" opens.
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_M, false))
         gModMatrixOpen = !gModMatrixOpen;

      // Shift+P: the docked performance matrix
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_P, false))
         gPerfPanelOpen = !gPerfPanelOpen;

      // Shift+T: the docked arrangement timeline
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_T, false))
         gArrangePanelOpen = !gArrangePanelOpen;

      // Shift+Y: fit view to content, replacing the old menu-only entry
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_Y, false))
         gRequestFitView = true;

      // Shift+N: the type-to-filter node picker, without having to find empty
      // canvas to double-click. Pressed again it closes, so the same key gets
      // you back out. The close has to be deferred into the popup body (see
      // searchRequestClose) and is deliberately NOT gated on `typing`, since
      // the picker's own text field owns the keyboard the whole time it's up.
      // Safe to claim: the picker lowercases both query and candidate names,
      // so a capital letter is never needed to find a node. Gated off while
      // the arrangement panel has focus - Shift+N there is "add track"
      // (DrawArrangePanelContent's own keyboard block), a different action
      // that must not also pop the canvas node picker open underneath it.
      const bool doAddNode = gRequestAddNode ||
         (!cmdOrCtrl && io.KeyShift && (!typing || searchPopupOpen) && gCommentEdit.target == nullptr &&
          ImGui::IsKeyPressed(ImGuiKey_N, false));
      gRequestAddNode = false;
      if (doAddNode)
      {
         if (searchPopupOpen)
         {
            searchRequestClose = true;
         }
         else
         {
            // Spawn node and search panel in the middle of the view/screen
            gSpawnPos = gViewCenterCanvas;
            gLinkDragSourcePin = -1;
            gLinkDragSuggestions.clear();
            searchBuf[0] = '\0';
            searchJustOpened = true;
            searchPopupCentered = true;
            ImGui::OpenPopup("search");
         }
         // The 'N' is already queued as a character for this frame; without
         // this every picker opened from the keyboard starts pre-filled with
         // it (and the closing press would type into whatever takes focus next).
         io.InputQueueCharacters.resize(0);
      }

      // Shift+H: hide/show node params. With a selection it toggles just those
      // nodes; with nothing selected it acts on the whole canvas, mirroring the
      // menu's "Show all params"/"Hide all params" pair as one key. Audio nodes
      // are skipped - their params are always visible by design (see the
      // context menu's identical carve-out), so they must not count toward
      // "is anything showing?" either, or a canvas of audio nodes would make
      // the first press a no-op.
      if (!typing && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_H, false))
      {
         std::vector<GraphNode*> targets;
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn != nullptr && !IsAudioBodyNode(gn->node.get()))
                  targets.push_back(gn);
            }
         }
         else
         {
            for (GraphNode& gn : gNodes)
               if (!IsAudioBodyNode(gn.node.get()))
                  targets.push_back(&gn);
         }

         if (!targets.empty())
         {
            // "Anything still showing" -> hide, else show. A mixed selection
            // therefore collapses first and expands second, which is the
            // behaviour that reads as a toggle rather than a shuffle.
            bool anyShown = false;
            for (GraphNode* gn : targets)
               anyShown = anyShown || gn->showParams;
            for (GraphNode* gn : targets)
               gn->showParams = !anyShown;
         }
      }

      // Shift+K: the audio engine, same on/off the toolbar's Start/Stop Audio
      // button drives - routed through StartAudioEngine rather than
      // AudioEngine::Start() so a patch loaded while the engine was off gets
      // re-prepared at the device's rate (see StartAudioEngine's comment).
      if (!typing && !gArrangeFocused && shiftOnly && ImGui::IsKeyPressed(ImGuiKey_K, false))
      {
         if (AudioEngine::Instance().SampleRate() > 0.0)
         {
            AudioEngine::Instance().Stop();
         }
         else
         {
            gAudioStartError.clear();
            if (!StartAudioEngine(gAudioStartError))
               fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
         }
      }

      // X on its own deletes selected cables and nothing else, so a mis-aimed
      // click on a node can't silently take the node with it. Shift+X is the
      // broader "delete what's selected", handled by the Delete/Backspace
      // block below.
      if (!typing && !cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false))
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::LinkId> selLinks(count);
            const int linkCount = ed::GetSelectedLinks(selLinks.data(), count);
            if (linkCount > 0)
            {
               // One checkpoint for the batch - DisconnectLinkById pushes its
               // own otherwise, so undoing a multi-cable delete would claw
               // back one cable per press (same fix as the block below).
               PushUndoCheckpoint();
               gSuppressUndoCheckpoints = true;
               for (int i = 0; i < linkCount; i++)
               {
                  DisconnectLinkById((int)selLinks[i].Get());
                  ed::DeleteLink(selLinks[i]);
               }
               gSuppressUndoCheckpoints = false;
               ed::ClearSelection();
            }
         }
      }

      // gPerfMatrixFocused: the performance matrix is in edit mode and has
      // keyboard focus, so these keys belong to its controls, not to the graph.
      // It is set from the matrix window, which draws later in the frame than
      // this block, so a closed matrix would otherwise keep the last value it
      // wrote.
      if (!gPerfPanelOpen)
      {
         gPerfMatrixClaimedKeys = false;
         gPerfMatrixFocused = false;
      }
      else
      {
         gPerfMatrixFocused = gPerfMatrixClaimedKeys && gPerfEditMode;
      }

      // Arrangement Timeline focus guard: when arrangement timeline owns the
      // keyboard focus, keystrokes (Delete, Backspace, Cmd+C, Cmd+V, Cmd+D)
      // belong strictly to the timeline clips, not the graph canvas.
      if (!gArrangePanelOpen)
      {
         gArrangeClaimedKeys = false;
         gArrangeFocused = false;
      }
      else
      {
         gArrangeFocused = gArrangeClaimedKeys;
      }

      const bool doDelete = gRequestDelete ||
         (!typing && !gPerfMatrixFocused && !gArrangeFocused && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
                      (shiftOnly && ImGui::IsKeyPressed(ImGuiKey_X, false))));
      gRequestDelete = false;
      if (doDelete)
      {
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            std::vector<ed::LinkId> selLinks(count);
            int linkCount = ed::GetSelectedLinks(selLinks.data(), count);

            // A selected group takes its members with it - otherwise "delete"
            // on a group would silently do no more than an ungroup.
            std::set<int> toDelete;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toDelete.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toDelete.insert(it->second.begin(), it->second.end());
               }
            }

            // One checkpoint for the whole batch: RemoveNodeByIndex (and the
            // per-link path below) each push their own by default, which
            // would otherwise turn "delete this group" into a checkpoint per
            // node - so a single Undo only clawed back the last one removed
            // instead of the whole cluster.
            if (linkCount > 0 || !toDelete.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            // One topology rebuild for the whole batch too - RemoveNodeByIndex
            // rebuilds on every call by default, which turned deleting an
            // N-node group into N full audio-graph rebuilds (each re-preparing
            // every audio node in the order, resetting reverb tails/delay
            // lines along the way).
            gDeferAudioRebuild = true;

            for (int i = 0; i < linkCount; i++)
            {
               DisconnectLinkById((int)selLinks[i].Get());
               ed::DeleteLink(selLinks[i]);
            }
            for (int index : toDelete)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }

            // After the loop, not before: RemoveNodeByIndex's own ordering
            // rule (rebuild only after the victim is erased from gNodes)
            // still has to hold for every victim, and deferring the rebuild
            // to here preserves that - every victim is already erased by now.
            gDeferAudioRebuild = false;
            RebuildAudioTopology();
            gSuppressUndoCheckpoints = false;
            ed::ClearSelection();
         }
      }

      // Shift+D (or Cmd/Ctrl+D) duplicates whatever is selected without
      // touching the clipboard.
      const bool doDuplicate = gRequestDuplicate ||
         (!typing && !gPerfMatrixFocused && !gArrangeFocused && (io.KeyShift || cmdOrCtrl) && ImGui::IsKeyPressed(ImGuiKey_D, false));
      gRequestDuplicate = false;
      if (doDuplicate)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // A selected group brings its members along, even if they are not
            // individually part of the editor's own selection set.
            std::set<int> toDup;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toDup.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toDup.insert(it->second.begin(), it->second.end());
               }
            }

            // Captured now, against the still-live selection, before any
            // SpawnNode call below can reallocate gNodes.
            ClusterClipboard dupCluster;
            CaptureClusterLinks(toDup, dupCluster);

            // Resolve everything first: SpawnNode can reallocate gNodes.
            const ImVec2 off = ClusterOffset(toDup);
            struct DupItem
            {
               std::string type; std::string category; INode* src; ImVec2 pos; bool params;
               bool miniViewport;
               bool advancedParams;
               int origIndex; int origGroup;
            };
            std::vector<DupItem> items;
            for (int index : toDup)
            {
               if (GraphNode* gn = FindNodeByIndex(index))
               {
                  const ImVec2 p = ed::GetNodePosition(gn->NodeId());
                  items.push_back({ gn->typeName, gn->category, gn->node.get(),
                                    ImVec2(p.x + off.x, p.y + off.y), gn->showParams,
                                    gn->showMiniViewport, gn->showAdvancedParams,
                                    gn->index, IndexOfGroupNode(GroupOwning(gn->index)) });
               }
            }

            ed::ClearSelection();
            // One checkpoint for the whole duplicate: SpawnNode pushes its
            // own by default, which would otherwise scatter a multi-node
            // duplicate across several undo steps instead of one.
            if (!items.empty())
               PushUndoCheckpoint();
            gSuppressUndoCheckpoints = true;
            std::map<int, GraphNode*> newByOrig;
            for (const DupItem& item : items)
            {
               if (GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y))
               {
                  CopyParams(copy->node.get(), item.src);
                  if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
                     rn->seed = RandomNode::NextSeed();
                  // A duplicate's ownership map, just copied verbatim by
                  // CopyParams, still points at the ORIGINAL's live nodes -
                  // the copy's identity must diverge (doc §5.7.3) so its
                  // next Regenerate treats every emit() as fresh (mount, not
                  // "I already own that node") instead of fighting the
                  // original over the same indices.
                  if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
                  {
                     fgn->SetUid(FieldGraphNode::NewUid());
                     fgn->Ownership() = Field::GraphOwnershipMap();
                     fgn->ownershipText.clear();
                  }
                  copy->showParams = item.params;
                  copy->showMiniViewport = item.miniViewport;
                  copy->showAdvancedParams = item.advancedParams;
                  newByOrig[item.origIndex] = copy;
                  gPendingSelect.push_back(copy->NodeId());
               }
            }
            // Re-establish group membership among the duplicates.
            for (const DupItem& item : items)
            {
               if (item.origGroup < 0)
                  continue;
               auto groupIt = newByOrig.find(item.origGroup);
               auto memberIt = newByOrig.find(item.origIndex);
               if (groupIt == newByOrig.end() || memberIt == newByOrig.end())
                  continue;
               if (auto* g = dynamic_cast<GroupNode*>(groupIt->second->node.get()))
                  gGroupMembers[g].insert(memberIt->second->index);
            }
            ApplyClusterLinks(newByOrig, dupCluster);
            gSuppressUndoCheckpoints = false;
         }
      }

      // !gArrangeFocused: Cmd+G / Cmd+Shift+G group timeline clips while the
      // Arrangement panel has focus, and must not also group canvas nodes.
      const bool doGroup =
         gRequestGroup ||
         (!typing && !gArrangeFocused && cmdOrCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      const bool doUngroup =
         gRequestUngroup ||
         (!typing && !gArrangeFocused && cmdOrCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false));
      gRequestGroup = false;
      gRequestUngroup = false;

      // Cmd/Ctrl+Shift+G is member-scoped: selecting a group's header
      // dissolves that whole group (nodes go away as members, the group node
      // itself goes away via RemoveNodeByIndex, freeing them for another
      // group to adopt), but selecting one or more ordinary member nodes
      // detaches only those nodes from their group - same as each member's
      // own right-click "Ungroup" - leaving the rest of the cluster intact.
      // A selection can mix both kinds at once (e.g. one whole group plus a
      // lone member of a different group), so both paths run in the same
      // pass.
      if (doUngroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // Resolved up front: RemoveNodeByIndex erases from gNodes, which
            // invalidates every GraphNode* taken before it.
            std::set<int> doomedGroups;
            std::vector<int> detachMembers;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* sel = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (sel == nullptr)
                  continue;
               if (dynamic_cast<GroupNode*>(sel->node.get()) != nullptr)
               {
                  doomedGroups.insert(sel->index);
                  continue;
               }
               if (GroupNode* owner = GroupOwning(sel->index))
               {
                  // A member whose owning group is also directly selected
                  // (and about to dissolve entirely) doesn't need its own
                  // detach - that would just nudge it right before the group
                  // node vanishes anyway.
                  bool ownerAlsoSelected = false;
                  for (GraphNode& g : gNodes)
                  {
                     if (g.node.get() == owner && doomedGroups.count(g.index))
                        ownerAlsoSelected = true;
                  }
                  if (!ownerAlsoSelected)
                     detachMembers.push_back(sel->index);
               }
            }

            const bool anyWork = !doomedGroups.empty() || !detachMembers.empty();
            // One checkpoint for the whole batch - see the Delete-key handler
            // above for why (several groups/members touched at once would
            // otherwise leave Undo only able to claw back the last one).
            if (anyWork)
               PushUndoCheckpoint();

            for (int memberIndex : detachMembers)
            {
               GraphNode* gn = FindNodeByIndex(memberIndex);
               if (gn == nullptr)
                  continue;
               GroupNode* owner = GroupOwning(memberIndex);
               if (owner == nullptr)
                  continue;
               gGroupMembers[owner].erase(memberIndex);
               // Membership here is purely geometric - anything fully inside
               // the group's box gets adopted right back in next frame.
               // Nudging the node just past the box's bottom edge is what
               // makes removing it actually stick (same as the per-node
               // right-click "Ungroup" above).
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

            gSuppressUndoCheckpoints = true;
            for (int index : doomedGroups)
            {
               ed::DeleteNode(ed::NodeId(index * GraphNode::kStride));
               RemoveNodeByIndex(index);
            }
            gSuppressUndoCheckpoints = false;
            if (anyWork)
               ed::ClearSelection();
         }
      }

      // Cmd/Ctrl+G wraps the current selection in a Group node sized to its
      // bounding box, so a cluster of existing nodes sticks together and
      // drags as one without having to hand-drag the group's edges around
      // them first.
      if (doGroup)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            bool any = false;
            ImVec2 bmin(0.0f, 0.0f), bmax(0.0f, 0.0f);
            std::set<int> picked;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* member = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (member == nullptr || dynamic_cast<GroupNode*>(member->node.get()) != nullptr)
                  continue; // grouping a group isn't supported
               // Membership is exclusive, so a node that already belongs
               // somewhere stays where it is rather than being pulled into a
               // second group that would then fight the first one for it.
               if (GroupOwning(member->index) != nullptr)
                  continue;
               picked.insert(member->index);
               const ImVec2 p = ed::GetNodePosition(member->NodeId());
               const ImVec2 s = ed::GetNodeSize(member->NodeId());
               if (!any)
               {
                  bmin = p;
                  bmax = ImVec2(p.x + s.x, p.y + s.y);
                  any = true;
               }
               else
               {
                  bmin.x = std::min(bmin.x, p.x);
                  bmin.y = std::min(bmin.y, p.y);
                  bmax.x = std::max(bmax.x, p.x + s.x);
                  bmax.y = std::max(bmax.y, p.y + s.y);
               }
            }

            if (any)
            {
               const float kPad = 32.0f;
               const float kHeader = 24.0f;
               const float gx = bmin.x - kPad;
               const float gy = bmin.y - kPad - kHeader;
               const float gw = (bmax.x - bmin.x) + kPad * 2.0f;
               const float gh = (bmax.y - bmin.y) + kPad * 2.0f + kHeader;

               PushUndoCheckpoint();
               gSuppressUndoCheckpoints = true;
               if (GraphNode* ggn = SpawnNode("Group", "Compositing", gx, gy))
               {
                  if (auto* grp = dynamic_cast<GroupNode*>(ggn->node.get()))
                  {
                     grp->width = gw;
                     grp->height = gh;
                     gGroupMembers[grp] = picked;
                  }
                  ed::ClearSelection();
                  gPendingSelect.push_back(ggn->NodeId());
               }
               gSuppressUndoCheckpoints = false;
            }
         }
      }

      // !gPerfMatrixFocused for the same reason Copy/Paste check it: while the
      // performance matrix owns the keyboard, a bare letter belongs to that
      // panel, not to the canvas selection behind it.
      const bool doBypass =
         gRequestBypass ||
         (!typing && !gPerfMatrixFocused && !gArrangeFocused && !cmdOrCtrl && !io.KeyShift && !io.KeyAlt &&
          ImGui::IsKeyPressed(ImGuiKey_B, false));
      gRequestBypass = false;

      if (doBypass)
      {
         const int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            const int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);
            if (nodeCount > 0)
            {
               PushUndoCheckpoint();
               bool needsAudioRebuild = false;
               for (int i = 0; i < nodeCount; i++)
               {
                  GraphNode* sel = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
                  if (sel == nullptr || !CanBypass(*sel))
                     continue;
                  sel->node->bypassed = !sel->node->bypassed;
                  if (dynamic_cast<IAudioSource*>(sel->node.get()) != nullptr ||
                      dynamic_cast<INoteSource*>(sel->node.get()) != nullptr)
                  {
                     needsAudioRebuild = true;
                  }
               }
               if (needsAudioRebuild)
                  RebuildAudioTopology();
            }
         }
      }

      const bool doCopy = gRequestCopy || (!typing && !gPerfMatrixFocused && !gArrangeFocused && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false));
      gRequestCopy = false;
      if (doCopy)
      {
         clipboard.clear();
         clipboardSources.clear();
         clipboardOrigIndex.clear();
         clipboardOrigGroup.clear();
         clipboardCluster = ClusterClipboard();
         int count = ed::GetSelectedObjectCount();
         if (count > 0)
         {
            std::vector<ed::NodeId> selNodes(count);
            int nodeCount = ed::GetSelectedNodes(selNodes.data(), count);

            // A selected group brings its members along, even if they are not
            // individually part of the editor's own selection set.
            std::set<int> toCopy;
            for (int i = 0; i < nodeCount; i++)
            {
               GraphNode* gn = FindNodeByIndex((int)selNodes[i].Get() / GraphNode::kStride);
               if (gn == nullptr)
                  continue;
               toCopy.insert(gn->index);
               if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
               {
                  auto it = gGroupMembers.find(g);
                  if (it != gGroupMembers.end())
                     toCopy.insert(it->second.begin(), it->second.end());
               }
            }

            for (int index : toCopy)
            {
               GraphNode* gn = FindNodeByIndex(index);
               if (gn == nullptr)
                  continue;
               clipboard.push_back(gn->typeName);
               clipboardSources.push_back(gn->node.get());
               clipboardOrigIndex.push_back(gn->index);
               clipboardOrigGroup.push_back(IndexOfGroupNode(GroupOwning(gn->index)));
            }
            CaptureClusterLinks(toCopy, clipboardCluster);
         }
      }

      const bool doPaste = (gRequestPaste || (!typing && !gPerfMatrixFocused && !gArrangeFocused && cmdOrCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))) && !clipboard.empty();
      gRequestPaste = false;
      if (doPaste)
      {
         // Recomputed fresh against the canvas as it stands right now, so a
         // second Cmd+V (which already sees the first paste sitting on the
         // canvas) lands clear of that too, not on top of it.
         const ImVec2 off =
            ClusterOffset(std::set<int>(clipboardOrigIndex.begin(), clipboardOrigIndex.end()));

         // resolve sources first: SpawnNode can reallocate gNodes and invalidate pointers
         struct PasteItem
         {
            std::string type; std::string category; INode* src; ImVec2 pos;
            int origIndex; int origGroup;
         };
         std::vector<PasteItem> items;
         for (size_t i = 0; i < clipboard.size(); i++)
         {
            for (GraphNode& gn : gNodes)
            {
               if (gn.node.get() == clipboardSources[i])
               {
                  ImVec2 p = ed::GetNodePosition(gn.NodeId());
                  items.push_back({ clipboard[i], gn.category, gn.node.get(),
                                    ImVec2(p.x + off.x, p.y + off.y),
                                    clipboardOrigIndex[i], clipboardOrigGroup[i] });
                  break;
               }
            }
         }
         // One checkpoint for the whole paste: SpawnNode pushes its own by
         // default, which would otherwise scatter a multi-node paste across
         // several undo steps instead of one.
         if (!items.empty())
            PushUndoCheckpoint();
         gSuppressUndoCheckpoints = true;
         ed::ClearSelection();
         std::map<int, GraphNode*> newByOrig;
         for (const PasteItem& item : items)
         {
            if (GraphNode* copy = SpawnNode(item.type, item.category, item.pos.x, item.pos.y))
            {
               CopyParams(copy->node.get(), item.src);
               if (auto* rn = dynamic_cast<RandomNode*>(copy->node.get()))
                  rn->seed = RandomNode::NextSeed();
               // See the duplicate path's identical comment above (doc
               // §5.7.3) - a paste needs the same fresh-identity treatment.
               if (auto* fgn = dynamic_cast<FieldGraphNode*>(copy->node.get()))
               {
                  fgn->SetUid(FieldGraphNode::NewUid());
                  fgn->Ownership() = Field::GraphOwnershipMap();
                  fgn->ownershipText.clear();
               }
               newByOrig[item.origIndex] = copy;
            }
         }
         // Re-establish group membership among the pasted copies.
         for (const PasteItem& item : items)
         {
            if (item.origGroup < 0)
               continue;
            auto groupIt = newByOrig.find(item.origGroup);
            auto memberIt = newByOrig.find(item.origIndex);
            if (groupIt == newByOrig.end() || memberIt == newByOrig.end())
               continue;
            if (auto* g = dynamic_cast<GroupNode*>(groupIt->second->node.get()))
               gGroupMembers[g].insert(memberIt->second->index);
         }
         ApplyClusterLinks(newByOrig, clipboardCluster);
         gSuppressUndoCheckpoints = false;
      }

      // ---- handle deletions raised by the editor itself ----
      if (ed::BeginDelete())
      {
         ed::LinkId linkId;
         while (ed::QueryDeletedLink(&linkId))
         {
            if (ed::AcceptDeletedItem())
            {
               PushUndoCheckpoint();
               DisconnectLinkById((int)linkId.Get());
            }
         }

         ed::NodeId nodeId;
         while (ed::QueryDeletedNode(&nodeId))
         {
            if (ed::AcceptDeletedItem())
               RemoveNodeByIndex((int)nodeId.Get() / GraphNode::kStride);
         }
      }
      ed::EndDelete();

      // ---- undo checkpoint for node drags ----
      // Only worth capturing a snapshot (BuildPatchData() walks every node
      // and cable) when this click could actually turn into a node drag -
      // not on every click anywhere, including knobs, buttons, menus and
      // empty canvas, which used to pay this cost on every single click.
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
         bool couldStartNodeDrag = (bool)ed::GetHoveredNode();
         if (!couldStartNodeDrag)
         {
            const int selCount = ed::GetSelectedObjectCount();
            if (selCount > 0)
            {
               std::vector<ed::NodeId> selNodes(selCount);
               couldStartNodeDrag = ed::GetSelectedNodes(selNodes.data(), selCount) > 0;
            }
         }
         if (couldStartNodeDrag)
         {
            gDragStartSnapshot = BuildPatchData();
            gDragSnapshotValid = true;
            gDragSnapshotPushed = false;
         }
      }
      if (gDragSnapshotValid && !gDragSnapshotPushed &&
          ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
      {
         bool moved = false;
         for (const Patch::NodeRecord& rec : gDragStartSnapshot.nodes)
         {
            GraphNode* gn = FindNodeByIndex(rec.index);
            if (gn != nullptr &&
                (std::fabs(gn->liveX - rec.x) > 0.5f || std::fabs(gn->liveY - rec.y) > 0.5f))
            {
               moved = true;
               break;
            }
         }
         if (moved)
         {
            PushUndoSnapshot(std::move(gDragStartSnapshot));
            gDragSnapshotPushed = true;
         }
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         gDragSnapshotValid = false;

      // ---- snap to grid once the drag finishes ----
      if (gSnapToGrid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         for (GraphNode& gn : gNodes)
         {
            ImVec2 p = ed::GetNodePosition(gn.NodeId());
            ImVec2 snapped(std::round(p.x / gGridSnap) * gGridSnap,
                           std::round(p.y / gGridSnap) * gGridSnap);
            if (std::fabs(snapped.x - p.x) > 0.01f || std::fabs(snapped.y - p.y) > 0.01f)
               ed::SetNodePosition(gn.NodeId(), snapped);
         }
      }

      // ---- popups: search, spawn menu, dropdown ----
      ed::Suspend();

      DrawMinimap();

      // Deferred from GlobalScaleToggle() above - see comment on
      // gGlobalScaleTooltipHovered. This is the first point after the
      // per-node draw loop where an ed::Suspend()'d tooltip is safe to draw.
      if (gGlobalScaleTooltipHovered)
      {
         ImGui::BeginTooltip();
         ImGui::SetWindowFontScale(0.85f);
         ImGui::TextUnformatted(gGlobalScaleTooltipEnabled ? "Global Scale: ON (click to disable)"
                                                            : "Global Scale: OFF (click to enable)");
         ImGui::SetWindowFontScale(1.0f);
         ImGui::EndTooltip();
      }

      // Right-click (two-finger click on a Mac trackpad) opens the same
      // type-to-filter picker as double-click, so the keyboard works either way.
      if (ed::ShowBackgroundContextMenu() && gCommentEdit.target == nullptr)
      {
         gSpawnPos = ed::ScreenToCanvas(ImGui::GetMousePos());
         searchBuf[0] = '\0';
         searchJustOpened = true;
         ImGui::OpenPopup("search");
      }

      // Right-click (two-finger click on a Mac trackpad) a node or group ->
      // a menu of actions specific to what got clicked, rather than only the
      // menu-bar/shortcut routes to the same operations.
      {
         ed::NodeId contextNodeId = 0;
         // A right-click already claimed by a param this frame (see
         // gParamRightClickConsumedThisFrame) opens that param's text field
         // instead - node editor still reports the click as a node context
         // menu request, so it has to be swallowed here rather than upstream.
         if (ed::ShowNodeContextMenu(&contextNodeId) && !gParamRightClickConsumedThisFrame)
         {
            gContextMenuNodeIndex = (int)contextNodeId.Get() / GraphNode::kStride;
            ImGui::OpenPopup("##nodecontext");
         }
      }
      if (ImGui::BeginPopup("##nodecontext"))
      {
         GraphNode* gn = FindNodeByIndex(gContextMenuNodeIndex);
         if (gn == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else if (auto* g = dynamic_cast<GroupNode*>(gn->node.get()))
         {
            if (ImGui::MenuItem("Rename"))
            {
               PushUndoCheckpoint();
               g->renaming = true;
               g->renameJustStarted = true;
            }
            if (ImGui::MenuItem("Ungroup"))
            {
               ed::ClearSelection();
               ed::SelectNode(gn->NodeId());
               gRequestUngroup = true;
            }
            if (ImGui::MenuItem("Duplicate", MODKEY "+D"))
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
            if (ImGui::MenuItem("Delete Group", "Backspace"))
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
            if (ImGui::MenuItem("Edit Note"))
            {
               PushUndoCheckpoint();
               gCommentEdit.target = c;
               gCommentEdit.justOpened = true;
            }
            if (ImGui::BeginMenu("Font Size"))
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
            if (ImGui::MenuItem("Change Colour..."))
            {
               PushUndoCheckpoint();
               gColor.target = c->color;
               gColor.owner = c;
               gColor.label = "colour";
               gColor.justOpened = true;
            }
            if (ImGui::MenuItem("Duplicate", MODKEY "+D"))
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
               if (ImGui::MenuItem("Ungroup"))
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
            if (ImGui::MenuItem("Delete Note", "Backspace"))
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
            if (ImGui::MenuItem("Duplicate", MODKEY "+D"))
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
                  if (ImGui::MenuItem("Hide params"))
                     gn->showParams = false;
               }
               else
               {
                  if (ImGui::MenuItem("Show params"))
                     gn->showParams = true;
               }
            }
            if (dynamic_cast<IGeometrySource*>(gn->node.get()) != nullptr)
            {
               if (gn->showMiniViewport)
               {
                  if (ImGui::MenuItem("Hide viewport"))
                     gn->showMiniViewport = false;
               }
               else
               {
                  if (ImGui::MenuItem("Show viewport"))
                     gn->showMiniViewport = true;
               }
            }
            if (CanShowInViewportPanel(*gn))
            {
               if (ImGui::MenuItem("Open in viewport panel"))
               {
                  if (std::find(gViewportPanelNodes.begin(), gViewportPanelNodes.end(), gn->index) ==
                      gViewportPanelNodes.end())
                     gViewportPanelNodes.push_back(gn->index);
                  gViewportPanelOpen = true;
               }
            }
            if (dynamic_cast<IModulator*>(gn->node.get()) != nullptr)
            {
               if (ImGui::MenuItem("Show modulation matrix"))
                  gModMatrixOpen = true;
            }
            if (IsNodeVideoCompatible(*gn) && IsNodeAudioCompatible(*gn))
            {
               // Picture and sound from one node (Video Source): the user
               // picks the lane; the audio clip reads its audio output.
               if (ImGui::BeginMenu("Add to Timeline"))
               {
                  if (ImGui::MenuItem("Video"))
                     AddNodeToArrangeTimeline(gn->index, Arrange::kLaneVideo);
                  if (ImGui::MenuItem("Audio"))
                     AddNodeToArrangeTimeline(gn->index, Arrange::kLaneAudio);
                  ImGui::EndMenu();
               }
            }
            else if (IsNodeVideoCompatible(*gn) || IsNodeAudioCompatible(*gn))
            {
               if (ImGui::MenuItem("Add to Timeline"))
                  AddNodeToArrangeTimeline(gn->index);
            }
            ProjectorWindow* projector = FindProjectorWindow(gn->index);
            if (CanShowInViewportPanel(*gn) && projector != nullptr)
            {
               if (ImGui::BeginMenu("Output window"))
               {
                  if (ImGui::MenuItem("Fullscreen", "F11", projector->fullscreen))
                     ToggleProjectorFullscreen(*projector);
                  if (ImGui::BeginMenu("Display"))
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
                  if (ImGui::MenuItem("Close window"))
                     CloseProjectorWindowFor(gn->index);
                  ImGui::EndMenu();
               }
            }
            else if (CanShowInViewportPanel(*gn))
            {
               if (ImGui::MenuItem("Open in new window"))
                  OpenProjectorWindow(window, *gn);
            }

            ImGui::Separator();

            // Information & Hierarchy
            if (ImGui::MenuItem("Help"))
            {
               gHelpPopupNodeIndex = gn->index;
               ImGui::CloseCurrentPopup();
               gOpenNodeHelpPopup = true;
            }
            if (GroupNode* owner = GroupOwning(gn->index))
            {
               if (ImGui::MenuItem("Ungroup"))
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
            if (ImGui::MenuItem("Delete Node", "Backspace"))
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
               ImGui::TextDisabled("(parameter not visible)");
            }
            else
            {
               const Modulation::Source src = mod.ResolvedSourceFor(*destRef);
               float lo = src.lo, hi = src.hi;
               // A bool wants a toggle and an enum wants a selector; a knob
               // is only the right default for a continuous param.
               const int perfKind = destRef->isBool ? 3 : (destRef->isEnum ? 7 : 0);
               if (ImGui::MenuItem("Add to Performance Matrix"))
                  AddToPerformanceMatrix(nodeIndex, paramIndex, perfKind);
               DrawParamMidiLearnMenuItem(nodeIndex, paramIndex);
               if (ImGui::MenuItem("View in Modulation Matrix"))
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
               if (ImGui::MenuItem("Full range"))
                  mod.SetRange(nodeIndex, paramIndex, destRef->minValue, destRef->maxValue);
               if (ImGui::MenuItem("Around current"))
               {
                  const float span = (destRef->maxValue - destRef->minValue) * 0.25f;
                  const float centre = destRef->value != nullptr ? *destRef->value : src.centre;
                  mod.SetRange(nodeIndex, paramIndex,
                              std::clamp(centre - span, destRef->minValue, destRef->maxValue),
                              std::clamp(centre + span, destRef->minValue, destRef->maxValue));
               }
               if (ImGui::MenuItem("Invert"))
                  mod.SetRange(nodeIndex, paramIndex, src.hi, src.lo);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Unbind"))
            {
               PushUndoCheckpoint();
               mod.Unbind(nodeIndex, paramIndex);
            }
         }
         else if (hasExpr)
         {
            if (destRef == nullptr)
            {
               ImGui::TextDisabled("(parameter not visible)");
            }
            else
            {
               if (ImGui::MenuItem("Edit Expression"))
                  BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, destRef->value, valueFmt, /*hasExpr=*/true);
               const int perfKind = destRef->isBool ? 3 : (destRef->isEnum ? 7 : 0);
               if (ImGui::MenuItem("Add to Performance Matrix"))
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
               if (ImGui::MenuItem("Full range"))
                  mod.ClearExpressionRange(nodeIndex, paramIndex);
               if (ImGui::MenuItem("Invert"))
                  mod.SetExpressionRange(nodeIndex, paramIndex, hi, lo);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Unbind"))
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
               ImGui::TextDisabled("Waiting for movement...");
               ImGui::Separator();
               if (ImGui::MenuItem("Cancel Recording"))
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
               if (ImGui::MenuItem("Add to Performance Matrix"))
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
               if (ImGui::MenuItem("Full range"))
                  rec.ClearPlaybackRange(nodeIndex, paramIndex);
               if (ImGui::MenuItem("Invert"))
                  rec.SetPlaybackRange(nodeIndex, paramIndex, hi, lo);
               ImGui::Separator();
               if (ImGui::MenuItem("Record Again"))
                  rec.ArmParam(nodeIndex, paramIndex);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Unbind"))
            {
               rec.CancelArm(nodeIndex, paramIndex);
               rec.StopPlayback(nodeIndex, paramIndex);
            }
         }
         else
         {
            if (destRef == nullptr)
            {
               ImGui::TextDisabled("(parameter not visible)");
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
                  if (ImGui::MenuItem("Enter Value"))
                     BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, destRef->value, valueFmt, /*hasExpr=*/false);
               }
               if (ImGui::MenuItem("Add to Performance Matrix"))
                  AddToPerformanceMatrix(nodeIndex, paramIndex, perfKind);
               DrawParamMidiLearnMenuItem(nodeIndex, paramIndex);
               if (!destRef->isBool)
               {
                  if (ImGui::MenuItem("Enter Expression"))
                     BeginTypedEditFromCurrent(editKey, nodeIndex, paramIndex, destRef->value, valueFmt, /*hasExpr=*/true);
                  if (ImGui::MenuItem("Start Recording"))
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
      }

      // dev-only: force the picker open (optionally with a query) to verify layout
      if (const char* pk = getenv("INFINITE_PICKERTEST"))
      {
         if (frameId == 6)
         {
            gSpawnPos = ImVec2(200.0f, 200.0f);
            snprintf(searchBuf, sizeof(searchBuf), "%s", std::string(pk) == "empty" ? "" : pk);
            searchJustOpened = (std::string(pk) == "empty");
            ImGui::OpenPopup("search");
         }
      }

      if (gDropdown.justOpened)
      {
         ImGui::OpenPopup("##dropdown");
         gDropdown.justOpened = false;
         gDropdown.filterBuf[0] = '\0'; // never reopen on the previous list's search text
      }

      FrameTest_COLORTEST_3(frameId, window);

      if (gCommentEdit.justOpened)
      {
         ImGui::OpenPopup("##commentedit");
         gCommentEdit.justOpened = false;
      }

      // the comment may have been deleted while its editor was open
      if (gCommentEdit.target != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gCommentEdit.target)
               alive = true;
         }
         if (!alive)
            gCommentEdit.target = nullptr;
      }

      // Pinned to the node's own on-screen box (refreshed every frame in
      // DrawCommentPreview) so the editor lands exactly over the note instead
      // of opening as a separate window elsewhere on screen - typing is meant
      // to read as happening straight into the box you double-clicked.
      if (gCommentEdit.target != nullptr && gCommentEditRect.z > 0.0f)
      {
         ImGui::SetNextWindowPos(ImVec2(gCommentEditRect.x, gCommentEditRect.y));
         ImGui::SetNextWindowSize(ImVec2(gCommentEditRect.z, gCommentEditRect.w));
      }
      const float commentZoom = std::max(0.1f, gCommentEditZoom > 0.0f ? gCommentEditZoom : ed::GetCurrentZoom());
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

      if (ImGui::BeginPopup("##commentedit", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar))
      {
         if (gCommentEdit.target != nullptr)
         {
            CommentNode* c = gCommentEdit.target;
            gCommentEdit.framesOpen++;
            if (gCommentEdit.framesOpen <= 2) // see CommentEditRequest::framesOpen
            {
               ImGui::SetWindowFocus();
               ImGui::SetKeyboardFocusHere();
            }

            const bool isLight = CategoryColors::IsThemeLight();
            const float* col = c->color;
            const ImVec4 textCol = isLight
               ? ImVec4(30.0f / 255.0f, 36.0f / 255.0f, 48.0f / 255.0f, 1.0f)
               : ImVec4(col[0] * 0.5f + 0.5f, col[1] * 0.5f + 0.5f, col[2] * 0.5f + 0.5f, 1.0f);

            const float fontScale = CommentFontScale(c->fontSize);
            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, isLight ? ImVec4(0.0f, 0.0f, 0.0f, 0.15f) : ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, isLight ? ImVec4(0.0f, 0.0f, 0.0f, 0.30f) : ImVec4(1.0f, 1.0f, 1.0f, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, isLight ? ImVec4(0.0f, 0.0f, 0.0f, 0.45f) : ImVec4(1.0f, 1.0f, 1.0f, 0.50f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * commentZoom, 8.0f * commentZoom));
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * commentZoom);
            ImGui::SetWindowFontScale(commentZoom * fontScale);

            // Filling the whole popup cleanly at 1:1 scale with the canvas card
            ImGui::InputTextMultiline("##commenttext", &c->text, ImVec2(gCommentEditRect.z, gCommentEditRect.w));

            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(6);

            // The checkpoint was pushed when the editor opened, so every
            // keystroke here is part of that one undo step; all that is left is
            // to keep the patch marked unsaved.
            if (ImGui::IsItemEdited())
               gPatchDirty = true;
            // Esc finishes the same way clicking outside does - no separate
            // "done" step needed for a note this small.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
               ImGui::CloseCurrentPopup();
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar(3);
      if (!ImGui::IsPopupOpen("##commentedit"))
      {
         gCommentEdit.target = nullptr;
         gCommentEdit.framesOpen = 0;
      }

      if (gColor.justOpened)
      {
         ImGui::OpenPopup("##colorpick");
         gColor.justOpened = false;
      }

      // the owning node may have been deleted while the picker was open
      if (gColor.owner != nullptr)
      {
         bool alive = false;
         for (const GraphNode& gn : gNodes)
         {
            if (gn.node.get() == gColor.owner)
               alive = true;
         }
         if (!alive)
         {
            gColor.owner = nullptr;
            gColor.target = nullptr;
         }
      }

      if (ImGui::BeginPopup("##colorpick"))
      {
         if (gColor.target != nullptr)
         {
            ImGui::TextDisabled("%s", gColor.label.c_str());
            gColorPickerRect = ImVec4(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y,
                                      0.0f, 0.0f);
            ImGui::ColorPicker3("##pick", gColor.target,
                                ImGuiColorEditFlags_PickerHueBar |
                                ImGuiColorEditFlags_DisplayRGB |
                                ImGuiColorEditFlags_DisplayHSV |
                                ImGuiColorEditFlags_DisplayHex);
            gColorPickerRect.z = ImGui::GetItemRectSize().x;
            gColorPickerRect.w = ImGui::GetItemRectSize().y;
         }
         else
         {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(320, 440));
      if (searchPopupCentered)
      {
         const ImVec2 center = ImVec2(gGraphScreenTL.x + gGraphScreenSize.x * 0.5f,
                                      gGraphScreenTL.y + gGraphScreenSize.y * 0.5f);
         ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      }
      if (ImGui::BeginPopup("search"))
      {
         searchPopupCentered = false;
         searchPopupOpen = true;
         // Shift+N pressed again while the picker was up - see searchRequestClose.
         if (searchRequestClose)
         {
            searchRequestClose = false;
            searchPopupOpen = false;
            searchJustOpened = false;
            ImGui::CloseCurrentPopup();
         }
         // Search glyph from the merged Lucide icon font (see main()'s font
         // setup / IconsLucide.h) ahead of the input box - the one obviously
         // net-positive icon spot from the Stage 5 iconography audit: this
         // is the app's most-used search field (Shift+N node picker), it was
         // plain text with no visual affordance before, and a leading icon
         // is exactly the ImGui idiom for it. SetKeyboardFocusHere() still
         // has to land on the InputText itself, so it moves down next to it
         // rather than firing on the icon Text widget in between.
         const float searchIconW = ImGui::CalcTextSize(IconsLucide::Search).x;
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted(IconsLucide::Search);
         ImGui::SameLine();
         if (searchJustOpened)
         {
            ImGui::SetKeyboardFocusHere();
            searchJustOpened = false;
         }
         ImGui::SetNextItemWidth(-FLT_MIN);
         ImGui::InputTextWithHint("##q", "search nodes...", searchBuf, sizeof(searchBuf));
         ImGui::Separator();

         std::string q(searchBuf);
         std::transform(q.begin(), q.end(), q.begin(), ::tolower);

         const bool pickFirst = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
         std::string spawnName, spawnCategory;
         int shown = 0;

         if (q.empty())
         {
            // Link-drag search: a cable was just dropped on empty canvas, so
            // lead with the node types compatible with the pin it came from
            // rather than making the user scroll every category to find one.
            if (!gLinkDragSuggestions.empty())
            {
               ImGui::SeparatorText("Suggested");
               for (const auto& t : gLinkDragSuggestions)
               {
                  ++shown;
                  const std::string title = DisplayName(t.first);
                  const std::string category = DisplayName(t.second);
                  if (ImGui::MenuItem(title.c_str(), category.c_str()))
                  {
                     spawnName = t.first;
                     spawnCategory = t.second;
                  }
               }
               ImGui::Separator();
            }

            // no query yet: browse by category submenu
            std::vector<std::string> cats = NodeFactory::Instance().GetCategories();
            std::stable_sort(cats.begin(), cats.end(), [](const std::string& a, const std::string& b) {
               return CategoryColors::SemanticRank(a) < CategoryColors::SemanticRank(b);
            });

            for (const std::string& category : cats)
            {
               ImGui::SetNextWindowSizeConstraints(ImVec2(180, 0), ImVec2(320, 440));
               if (ImGui::BeginMenu(DisplayName(category).c_str()))
               {
                  for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
                  {
                     if (!IsUserSpawnable(name))
                        continue;
                     ++shown;
                     if (ImGui::MenuItem(DisplayName(name).c_str()))
                     {
                        spawnName = name;
                        spawnCategory = category;
                     }
                  }
                  ImGui::EndMenu();
               }
            }
         }
         else
         {
            for (const auto& t : allTypes)
            {
               std::string hay = DisplayName(t.first) + " " + DisplayName(t.second);
               if (!NodeSearchMatches(hay, q))
                  continue;
               ++shown;
               const std::string title = DisplayName(t.first);
               const std::string category = DisplayName(t.second);
               bool activate = ImGui::MenuItem(title.c_str(), category.c_str());
               if (shown == 1 && pickFirst)
                  activate = true;
               if (activate)
               {
                  spawnName = t.first;
                  spawnCategory = t.second;
               }
            }
            if (shown == 0)
               ImGui::TextDisabled("no matches");
         }

         if (!spawnName.empty())
         {
            GraphNode* spawned = SpawnNode(spawnName, spawnCategory, gSpawnPos.x, gSpawnPos.y);
            if (gLinkDragSourcePin >= 0 && spawned != nullptr)
            {
               GraphNode* dragSrcNode = FindNodeByIndex(GraphNode::NodeIndexFromPin(gLinkDragSourcePin));
               if (dragSrcNode != nullptr)
               {
                  const int dragOutputSlot = GraphNode::OutputIndexFromPin(gLinkDragSourcePin);
                  const bool srcIsModulator = (dynamic_cast<IModulator*>(dragSrcNode->node.get()) != nullptr ||
                                               ModulatorForOutput(dragSrcNode->node.get(), dragOutputSlot) != nullptr);
                  auto* srcPalette = dynamic_cast<IPaletteSource*>(dragSrcNode->node.get());
                  auto* srcGeometry = dynamic_cast<IGeometrySource*>(dragSrcNode->node.get());
                  if (srcGeometry != nullptr && !srcGeometry->IsGeometryOutputIndex(dragOutputSlot))
                     srcGeometry = nullptr;
                  auto* srcCamera = dynamic_cast<CameraNode*>(dragSrcNode->node.get());
                  auto* srcLight = dynamic_cast<LightNode*>(dragSrcNode->node.get());
                  const bool srcIsEnvironment =
                     dynamic_cast<EnvironmentNode*>(dragSrcNode->node.get()) != nullptr;
                  auto* srcAudioSource = dynamic_cast<IAudioSource*>(dragSrcNode->node.get());
                  const bool srcIsAudioNode =
                     srcAudioSource != nullptr && srcAudioSource->IsAudioOutputIndex(dragOutputSlot);
                  const bool srcIsNoteSource =
                     dynamic_cast<INoteSource*>(dragSrcNode->node.get()) != nullptr;
                  const bool srcIsPredictor =
                     dynamic_cast<IPredictor*>(dragSrcNode->node.get()) != nullptr;

                  const int slotCount = InputCountFor(*spawned);
                  for (int slot = 0; slot < slotCount; ++slot)
                  {
                     if (IsInputSlotCompatible(spawned, slot, srcIsModulator, srcPalette, srcGeometry,
                                                srcCamera, srcLight, srcIsEnvironment,
                                                srcIsAudioNode, srcIsNoteSource, srcIsPredictor))
                     {
                        // No PushUndoCheckpoint() here: SpawnNode() above already
                        // pushed one capturing the state before the node existed,
                        // so a single Undo removes the spawn and the wire
                        // together, as one user action - not two separate steps.
                        WireInputSlot(*dragSrcNode, *spawned, slot,
                                      GraphNode::OutputIndexFromPin(gLinkDragSourcePin));
                        if (srcIsAudioNode || srcIsNoteSource ||
                            spawned->node->AudioInputSlot(slot) != nullptr ||
                            spawned->node->NoteInputSlot(slot) != nullptr)
                           RebuildAudioTopology();
                        break;
                     }
                  }
               }
            }
            gLinkDragSourcePin = -1;
            gLinkDragSuggestions.clear();
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
      else
      {
         // Popup closed without a pick (Escape, click-away, or the manual
         // spawn-menu paths below) - clear so a later unrelated spawn doesn't
         // inherit a stale suggestion list from an earlier drag.
         gLinkDragSourcePin = -1;
         gLinkDragSuggestions.clear();
         searchPopupOpen = false;
         searchRequestClose = false;
      }

      // Size the popup to its actual content rather than a fixed 240px
      // floor - a short list like the filter-mode options ("off", "lp 12",
      // ...) doesn't need anywhere near that width, and a fixed floor just
      // stretches the pills into a wide, empty-feeling bar.
      const float dropdownTextPadX = 8.0f;
      float dropdownMaxTextW = 0.0f;
      for (const std::string& opt : gDropdown.options)
         dropdownMaxTextW = ImMax(dropdownMaxTextW, ImGui::CalcTextSize(opt.c_str()).x);
      for (const std::string& cat : gDropdown.categories)
         dropdownMaxTextW = ImMax(dropdownMaxTextW, ImGui::CalcTextSize(cat.c_str()).x);
      const float dropdownMinWidth = ImClamp(dropdownMaxTextW + ImGui::GetStyle().WindowPadding.x * 2.0f
                                                 + dropdownTextPadX * 2.0f
                                                 + ImGui::GetStyle().ScrollbarSize,
                                              120.0f, 400.0f);
      ImGui::SetNextWindowSizeConstraints(ImVec2(dropdownMinWidth, 0), ImVec2(520, 480));
      if (ImGui::BeginPopup("##dropdown"))
      {
         const bool showSearch = gDropdown.focusSearch || gDropdown.options.size() >= kDropdownAutoSearchMin;
         if (showSearch)
         {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
            if (ImGui::IsWindowAppearing())
               ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##ddsearch", "Search...", gDropdown.filterBuf, sizeof(gDropdown.filterBuf));
            ImGui::PopStyleVar();
            ImGui::Separator();
         }

         const std::string q = showSearch ? FoldForSearch(gDropdown.filterBuf) : std::string();

         // The pill still spans the full row (NoPadWithHalfSpacing below
         // keeps the gap between rows real, and item spacing is tightened
         // slightly so that gap isn't oversized), but the label is drawn
         // with its own left inset instead of starting flush with the
         // pill's edge - otherwise the text reads as glued to the window
         // border with no breathing room.
         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImMax(2.0f, ImGui::GetStyle().ItemSpacing.y - 2.0f)));
         std::string lastCategory;
         for (int i = 0; i < (int)gDropdown.options.size(); i++)
         {
            if (!q.empty())
            {
               std::string hay = gDropdown.options[i];
               if (i < (int)gDropdown.categories.size() && !gDropdown.categories[i].empty())
                  hay += " " + gDropdown.categories[i];
               hay = FoldForSearch(hay);
               if (hay.find(q) == std::string::npos)
                  continue;
            }

            if (i < (int)gDropdown.categories.size() && !gDropdown.categories[i].empty())
            {
               if (gDropdown.categories[i] != lastCategory)
               {
                  if (i > 0)
                     ImGui::Separator();
                  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                  ImGui::TextUnformatted(gDropdown.categories[i].c_str());
                  ImGui::PopStyleColor();
                  lastCategory = gDropdown.categories[i];
               }
            }
            bool selected = (i == gDropdown.current);
            ImGui::PushID(i);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            // NoPadWithHalfSpacing: Selectable pads its hit/fill rect into
            // half of ItemSpacing on each side by default so a stack of rows
            // reads as one continuous menu - which is exactly what erased
            // the gap between the selected row's pill and a hovered
            // neighbour's pill (e.g. "oct +0" selected, "oct +1" hovered).
            // Opting out restores a real gap between rows, so the two
            // highlight states stay visually distinct.
            const bool clicked = ImGui::Selectable("##ddrow", selected,
                                                    ImGuiSelectableFlags_NoPadWithHalfSpacing, ImVec2(rowWidth, 0.0f));
            const float rowH = ImGui::GetItemRectSize().y;
            ImGui::GetWindowDrawList()->AddText(
               ImVec2(rowMin.x + dropdownTextPadX, rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
               ImGui::GetColorU32(ImGuiCol_Text), gDropdown.options[i].c_str());
            ImGui::PopID();
            if (clicked)
            {
               CommitDropdownPick(i); // one click, one undo entry
               ImGui::CloseCurrentPopup();
            }
            if (selected && ImGui::IsWindowAppearing() && !gDropdown.focusSearch)
               ImGui::SetScrollHereY(0.5f);
         }

         ImGui::PopStyleVar();
         ImGui::EndPopup();
      }

      // Field build step 17: .infdev device Save name-prompt - same
      // deferred-popup discipline as "##dropdown" just above (opened from
      // inside a node body, rendered out here where the canvas transform
      // doesn't apply).
      if (gFieldDeviceSave.justOpened)
      {
         ImGui::OpenPopup("##fielddevicesave");
         gFieldDeviceSave.justOpened = false;
      }
      if (ImGui::BeginPopup("##fielddevicesave"))
      {
         ImGui::TextUnformatted("Save device as:");
         ImGui::SetNextItemWidth(220.0f);
         bool enterPressed = ImGui::InputText("##fielddevicesavename", gFieldDeviceSave.nameBuf,
                                              sizeof(gFieldDeviceSave.nameBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
         ImGui::SameLine();
         bool doSave = enterPressed || ImGui::Button("Save##fielddevicesaveconfirm");
         if (doSave && gFieldDeviceSave.nameBuf[0] != '\0')
         {
            Field::DeviceFile device;
            if (gFieldDeviceSave.getDeviceFile && gFieldDeviceSave.getDeviceFile(device))
            {
               const std::string dir = AppPaths::AppSupportDir() + "/Devices/" + gFieldDeviceSave.domain + "/";
               if (AppPaths::EnsureDir(dir))
               {
                  Field::SaveToFieldFile(dir + gFieldDeviceSave.nameBuf + ".field", device);
                  InvalidateFieldDeviceLibrary(gFieldDeviceSave.domain);
               }
            }
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      if (gAudioDropPicker.justOpened)
      {
         ImGui::OpenPopup("##audiodroppicker");
         gAudioDropPicker.justOpened = false;
         const ImVec2 popupPos = (gAudioDropPicker.screenPos.x != 0.0f || gAudioDropPicker.screenPos.y != 0.0f)
            ? gAudioDropPicker.screenPos
            : ed::CanvasToScreen(gAudioDropPicker.canvasPos);
         ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
      }
      else
      {
         const ImVec2 popupPos = (gAudioDropPicker.screenPos.x != 0.0f || gAudioDropPicker.screenPos.y != 0.0f)
            ? gAudioDropPicker.screenPos
            : ed::CanvasToScreen(gAudioDropPicker.canvasPos);
         ImGui::SetNextWindowPos(popupPos, ImGuiCond_Appearing);
      }
      ImGui::SetNextWindowSizeConstraints(ImVec2(240, 0), ImVec2(360, 480));
      if (ImGui::BeginPopup("##audiodroppicker"))
      {
         if (gAudioDropPicker.paths.empty())
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            if (gAudioDropPicker.paths.size() == 1)
            {
               std::string filename = gAudioDropPicker.paths[0];
               const size_t lastSlash = filename.find_last_of("/\\");
               if (lastSlash != std::string::npos)
                  filename = filename.substr(lastSlash + 1);
               ImGui::TextDisabled("Load %s into:", filename.c_str());
            }
            else
            {
               ImGui::TextDisabled("Load %d samples into:", (int)gAudioDropPicker.paths.size());
            }
            ImGui::Separator();

            struct PickerOption
            {
               const char* name;
               const char* category;
               const char* desc;
            };
            static const PickerOption kOptions[] = {
               { "Sampler",        "Synths",     "Sample playback with pitch & envelope" },
               { "Audio File",     "Modulators", "Streaming playback & follower" },
               { "Slicer",         "Synths",     "Beat/transient slicer" },
               { "Drum Sequencer", "Synths",     "Step sequencer, grooves & drum kit" },
               { "MPC",            "Synths",     "16-pad sample player" },
               { "PaulStretch",    "Synths",     "Extreme time-stretch & wash" },
               { "Granular",       "Synths",     "Granular cloud synthesis" },
               { "Grain Molder",   "Synths",     "Granular morph & shape" },
               { "Molder",         "Synths",     "Spectral cross-synthesis" },
            };

            for (const auto& opt : kOptions)
            {
               if (ImGui::MenuItem(opt.name, opt.category))
               {
                  if (std::string(opt.name) == "MPC")
                  {
                     GraphNode* spawned = SpawnNode(opt.name, opt.category, gAudioDropPicker.canvasPos.x,
                                                    gAudioDropPicker.canvasPos.y);
                     if (spawned != nullptr)
                     {
                        if (auto* mpc = dynamic_cast<MpcNode*>(spawned->node.get()))
                           for (int i = 0; i < (int)gAudioDropPicker.paths.size() && i < MpcNode::kPads; ++i)
                              mpc->LoadPad(i, gAudioDropPicker.paths[i]);
                        spawned->showParams = true;
                        gPatchDirty = true;
                        RebuildAudioTopology();
                     }
                  }
                  else if (std::string(opt.name) == "Drum Sequencer")
                  {
                     GraphNode* spawned = SpawnNode(opt.name, opt.category,
                                                    gAudioDropPicker.canvasPos.x,
                                                    gAudioDropPicker.canvasPos.y);
                     if (spawned != nullptr)
                     {
                        auto* drum = dynamic_cast<DrumSequencerNode*>(spawned->node.get());
                        if (drum != nullptr)
                        {
                           for (int i = 0; i < (int)gAudioDropPicker.paths.size() && i < DrumSequencerNode::kNumLanes; ++i)
                              drum->LoadFileToLane(i, gAudioDropPicker.paths[i]);
                        }
                        spawned->showParams = true;
                        gPatchDirty = true;
                        RebuildAudioTopology();
                     }
                  }
                  else
                  {
                     float offset = 0.0f;
                     for (const std::string& path : gAudioDropPicker.paths)
                     {
                        GraphNode* spawned = SpawnNode(opt.name, opt.category,
                                                       gAudioDropPicker.canvasPos.x + offset,
                                                       gAudioDropPicker.canvasPos.y);
                        if (spawned != nullptr)
                        {
                           if (auto* s = dynamic_cast<SamplerNode*>(spawned->node.get()))
                              s->LoadFile(path);
                           else if (auto* af = dynamic_cast<AudioFileNode*>(spawned->node.get()))
                              af->Open(path);
                           else if (auto* sl = dynamic_cast<SlicerNode*>(spawned->node.get()))
                              sl->LoadFile(path);
                           else if (auto* ps = dynamic_cast<PaulStretchNode*>(spawned->node.get()))
                              ps->LoadFile(path);
                           else if (auto* gran = dynamic_cast<GranularNode*>(spawned->node.get()))
                              gran->LoadFile(path);
                           else if (auto* gm = dynamic_cast<GrainMolderNode*>(spawned->node.get()))
                              gm->LoadFile(path);
                           else if (auto* mol = dynamic_cast<MolderNode*>(spawned->node.get()))
                              mol->LoadFile(path);

                           spawned->showParams = true;
                           gPatchDirty = true;
                           RebuildAudioTopology();
                        }
                        offset += 240.0f;
                     }
                  }
                  gAudioDropPicker.paths.clear();
                  ImGui::CloseCurrentPopup();
                  break;
               }
               if (ImGui::IsItemHovered() && opt.desc != nullptr && opt.desc[0] != '\0')
               {
                  ImGui::SetTooltip("%s", opt.desc);
               }
            }
         }
         ImGui::EndPopup();
      }
      else
      {
         if (!gAudioDropPicker.paths.empty() && !gAudioDropPicker.justOpened)
            gAudioDropPicker.paths.clear();
      }

      gHoveringItem = ed::GetHoveredNode() || ed::GetHoveredPin() || ed::GetHoveredLink();

      ed::Resume();

      // Fit-to-content has to happen down here, after every node has been
      // submitted this frame. ed::Begin() marks all nodes not-live and only
      // drawing them marks them live again, and NavigateToContent() measures
      // live nodes only - so called up next to ed::Begin() it always fit an
      // empty rectangle and silently did nothing at all. The new view is
      // picked up by the next ed::Begin(), one frame later.
      if (gRequestFitView)
      {
         ed::NavigateToContent(0.0f);
         gRequestFitView = false;
      }
      if (gRequestFitViewNodeIndex >= 0)
      {
         if (GraphNode* fitNode = FindNodeByIndex(gRequestFitViewNodeIndex))
         {
            // NavigateToSelection reads the selection's bounds immediately
            // (see ax::NodeEditor::NavigateToSelection), so it's safe to clear
            // the selection right back out afterward - the node won't render
            // with a selected-highlight border on the frame that gets shot.
            ed::ClearSelection();
            ed::SelectNode(fitNode->NodeId());
            ed::NavigateToSelection(false, 0.0f);
            ed::ClearSelection();
         }
         gRequestFitViewNodeIndex = -1;
      }
      if (getenv("INFINITE_PALETTETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if (getenv("INFINITE_AUDIOUITEST") != nullptr && frameId == 3)
         gRequestFitView = true; // same, for the audio node UI fixture
      if (getenv("INFINITE_LOADPATCH") != nullptr && (frameId == 2 || frameId == 4))
         gRequestFitView = true;
      FrameTest_AUDIOUITEST_2(frameId, window);
      if (getenv("INFINITE_HIDETEST") != nullptr && frameId == 3)
         gRequestFitView = true; // dev screenshot: frame the whole fixture
      if ((getenv("INFINITE_WTDRAGTEST") != nullptr || getenv("INFINITE_EQDRAGTEST") != nullptr ||
          getenv("INFINITE_PREDBINDTEST") != nullptr) && frameId == 3)
         gRequestFitView = true;

      if (gPerfAssigningElemIdx >= 0 && gPerfAssigningElemIdx < (int)gPerfElements.size())
      {
         const auto& assignElem = gPerfElements[gPerfAssigningElemIdx];
         // imgui-node-editor remaps io.MousePos into its own local/zoomed
         // canvas space for the duration of ed::Begin()/ed::End() (see
         // ImGuiEx::Canvas::EnterLocalSpace), so both GetMousePos() and
         // GetIO().MousePos are canvas-space here, not real screen pixels.
         // gGraphScreenTL/gGraphScreenSize were captured before ed::Begin(),
         // in real screen space, so they have to be converted with
         // ed::ScreenToCanvas before comparing against the mouse - comparing
         // them directly (as this used to) left mouseOverGraph false at any
         // pan/zoom other than the coincidental default, which silently
         // broke every canvas-click parameter assignment.
         const ImVec2 mp = ImGui::GetMousePos();
         int hoveredPinIdx = -1;

         const ImVec2 graphTL = ed::ScreenToCanvas(gGraphScreenTL);
         const ImVec2 graphBR = ed::ScreenToCanvas(
            ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));
         const bool mouseOverGraph = (mp.x >= graphTL.x && mp.x <= graphBR.x &&
                                      mp.y >= graphTL.y && mp.y <= graphBR.y);

         if (mouseOverGraph)
         {
            for (size_t pi = 0; pi < gParamPinScreenList.size(); pi++)
            {
               const auto& pInfo = gParamPinScreenList[pi];
               bool inBounds = (mp.x >= pInfo.rowMin.x && mp.x <= pInfo.rowMax.x &&
                                mp.y >= pInfo.rowMin.y && mp.y <= pInfo.rowMax.y);
               float dx = mp.x - pInfo.screenPos.x;
               float dy = mp.y - pInfo.screenPos.y;
               if (inBounds || (dx * dx + dy * dy < 20.0f * 20.0f))
               {
                  hoveredPinIdx = (int)pi;
               }
            }
         }

         if (hoveredPinIdx >= 0)
         {
            const auto& pInfo = gParamPinScreenList[hoveredPinIdx];
            // Glowing box + ring around the hovered control, drawn while still
            // inside ed::Begin/End so it rides the same local-space vertex
            // transform as the row rect it's outlining - drawing it after
            // ed::Suspend() below would place it in real screen space and it
            // would land in the wrong spot at any pan/zoom.
            {
               ImDrawList* hoverDl = ImGui::GetWindowDrawList();
               const ImU32 glowCol = IM_COL32(0, 230, 255, 28);
               const ImU32 ringCol = IM_COL32(0, 230, 255, 160);
               if (pInfo.isCircle)
               {
                  hoverDl->AddCircleFilled(pInfo.shapeCenter, pInfo.shapeRadius, glowCol, 24);
                  hoverDl->AddCircle(pInfo.shapeCenter, pInfo.shapeRadius, ringCol, 24, 1.0f);
               }
               else
               {
                  hoverDl->AddRectFilled(pInfo.rowMin, pInfo.rowMax, glowCol, 4.0f);
                  hoverDl->AddRect(pInfo.rowMin, pInfo.rowMax, ringCol, 4.0f, 0, 1.0f);
               }
            }
            ed::Suspend();
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Assign to '%s' (%s) -> %s: %s",
                              assignElem.label.c_str(),
                              gPerfAssigningAxis == 1 ? "Y Axis" : (assignElem.kind == 4 ? "X Axis" : "Param"),
                              pInfo.nodeTitle.c_str(), pInfo.paramName.c_str());
            ed::Resume();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint();
               auto& el = gPerfElements[gPerfAssigningElemIdx];
               if (gPerfAssigningAxis == 0)
               {
                  el.dstIndex = pInfo.nodeIndex;
                  el.dstParam = pInfo.paramIndex;
                  el.targets.clear();
                  el.targets.push_back({ pInfo.nodeIndex, pInfo.paramIndex, "" });
                  if (el.label.empty() || el.label == "Knob" || el.label == "Fader" || el.label == "Slider" || el.label == "Toggle" || el.label == "XY Pad" || el.label == "Trigger" || el.label == "NumBox" || el.label == "Selector" || el.label == "Pan/Detent" || el.label == "Step Gate")
                     el.label = pInfo.paramName;
               }
               else if (gPerfAssigningAxis == 1)
               {
                  el.dstParam2 = pInfo.paramIndex;
                  el.targetsY.clear();
                  el.targetsY.push_back({ pInfo.nodeIndex, pInfo.paramIndex, "" });
               }
               gPerfAssigningElemIdx = -1;
            }
         }

         if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
         {
            gPerfAssigningElemIdx = -1;
         }
      }

      // Predictive Drift Follow leader picker - identical click-to-assign UX to the
      // performance-matrix picker just above, but resolves to a DriftNode's leaderNodeIndex/
      // leaderParamIndex instead of a perf element target. Deliberately stores the target's
      // *uid* (gNodes[nodeIndex].uid), not its index - an index is reused by
      // RemoveNodeByIndex and would silently repoint Follow at a different node after an
      // unrelated delete/undo, exactly the stability trap Modulation.h's own uid comment
      // (ParamRef::uid) warns about.
      if (gDriftFollowPickingUid != 0)
      {
         GraphNode* pickingGn = FindNodeByUid(gDriftFollowPickingUid);
         DriftNode* pickingDrift = pickingGn != nullptr ? dynamic_cast<DriftNode*>(pickingGn->node.get()) : nullptr;
         if (pickingDrift == nullptr)
         {
            gDriftFollowPickingUid = 0; // the node was deleted while picking
         }
         else
         {
            const ImVec2 mp = ImGui::GetMousePos();
            int hoveredPinIdx = -1;
            const ImVec2 graphTL = ed::ScreenToCanvas(gGraphScreenTL);
            const ImVec2 graphBR = ed::ScreenToCanvas(
               ImVec2(gGraphScreenTL.x + gGraphScreenSize.x, gGraphScreenTL.y + gGraphScreenSize.y));
            const bool mouseOverGraph = (mp.x >= graphTL.x && mp.x <= graphBR.x &&
                                         mp.y >= graphTL.y && mp.y <= graphBR.y);
            if (mouseOverGraph)
            {
               for (size_t pi = 0; pi < gParamPinScreenList.size(); pi++)
               {
                  const auto& pInfo = gParamPinScreenList[pi];
                  // Can't follow yourself - a self-target would drive the same slot
                  // it's reading, an infinite feedback loop with no way to break it.
                  if (pInfo.nodeIndex >= 0 && pInfo.nodeIndex < (int)gNodes.size() &&
                      gNodes[pInfo.nodeIndex].uid == gDriftFollowPickingUid)
                     continue;
                  const bool inBounds = (mp.x >= pInfo.rowMin.x && mp.x <= pInfo.rowMax.x &&
                                         mp.y >= pInfo.rowMin.y && mp.y <= pInfo.rowMax.y);
                  const float dx = mp.x - pInfo.screenPos.x;
                  const float dy = mp.y - pInfo.screenPos.y;
                  if (inBounds || (dx * dx + dy * dy < 20.0f * 20.0f))
                     hoveredPinIdx = (int)pi;
               }
            }
            if (hoveredPinIdx >= 0)
            {
               const auto& pInfo = gParamPinScreenList[hoveredPinIdx];
               {
                  ImDrawList* hoverDl = ImGui::GetWindowDrawList();
                  const ImU32 glowCol = IM_COL32(120, 200, 255, 28);
                  const ImU32 ringCol = IM_COL32(120, 200, 255, 170);
                  if (pInfo.isCircle)
                  {
                     hoverDl->AddCircleFilled(pInfo.shapeCenter, pInfo.shapeRadius, glowCol, 24);
                     hoverDl->AddCircle(pInfo.shapeCenter, pInfo.shapeRadius, ringCol, 24, 1.0f);
                  }
                  else
                  {
                     hoverDl->AddRectFilled(pInfo.rowMin, pInfo.rowMax, glowCol, 4.0f);
                     hoverDl->AddRect(pInfo.rowMin, pInfo.rowMax, ringCol, 4.0f, 0, 1.0f);
                  }
               }
               ed::Suspend();
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
               ImGui::SetTooltip("Follow leader -> %s: %s", pInfo.nodeTitle.c_str(), pInfo.paramName.c_str());
               ed::Resume();
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && pInfo.nodeIndex >= 0 &&
                   pInfo.nodeIndex < (int)gNodes.size())
               {
                  PushUndoCheckpoint();
                  pickingDrift->leaderNodeIndex = (int)gNodes[pInfo.nodeIndex].uid;
                  pickingDrift->leaderParamIndex = pInfo.paramIndex;
                  gDriftFollowPickingUid = 0;
               }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
               gDriftFollowPickingUid = 0;
         }
      }

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
            const ImU32 glowCol = IM_COL32(0, 230, 255, 40);
            const ImU32 ringCol = IM_COL32(0, 230, 255, 200);
            hoverDl->AddRectFilled(hoveredP, ImVec2(hoveredP.x + hoveredS.x, hoveredP.y + hoveredS.y), glowCol, 6.0f);
            hoverDl->AddRect(hoveredP, ImVec2(hoveredP.x + hoveredS.x, hoveredP.y + hoveredS.y), ringCol, 6.0f, 0, 2.0f);

            ed::Suspend();
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Assign clip source -> %s", NodeTitleWithInstance(*hoveredCompatible).c_str());
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
         ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
         ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
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
         extern int g_InfiniteDuplicatePinIds;
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
         // See DrawModMatrixDocked's inner-content child for why
         // AlwaysUseWindowPadding is needed alongside Border now.
         ImGui::BeginChild("##nodepanel", ImVec2(kNodePanelWidth, graphHeight),
                           ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
         PopDockedPanelStyle();
         // Same hairline as every other panel boundary. This panel has no
         // resize grip to hang it off, so it draws the seam on its own left
         // edge - the side that faces the canvas, since it is always the
         // rightmost panel.
         {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(wp.x + 0.5f, wp.y),
                                                ImVec2(wp.x + 0.5f, wp.y + ws.y),
                                                PanelSeamColor(), 1.0f);
         }

         // Mode switcher: Modules is the original, always-present catalogue;
         // Samples and Media extend it per docs/plans/audio/README.md P3e.
         // Kept as plain selectable-style buttons rather than an ImGui tab
         // bar so the active mode reads clearly against the panel's own
         // dark background.
         // Sized dynamically across the 5 browser modes (Modules, Field, Samples, Media, Plugins).
         // Styled with centered alignment and dedicated gaps so labels never collide or clip.
         // Selectable's highlight is rounded app-wide (see the FrameRounding
         // patch in imgui_widgets.cpp), so anything tighter than ~8px reads
         // as one unbroken block between adjacent tabs with no visible seam.
         const float tabGap = 8.0f;
         const float totalAvailW = ImGui::GetContentRegionAvail().x;
         const float tabW = std::max(40.0f, std::floor((totalAvailW - tabGap * 4.0f) / 5.0f));

         ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
         if (ImGui::Selectable("Modules", gSearchPanelMode == 0, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 0;
         ImGui::SameLine(0.0f, tabGap);
         if (ImGui::Selectable("Field", gSearchPanelMode == 4, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 4;
         ImGui::SameLine(0.0f, tabGap);
         if (ImGui::Selectable("Samples", gSearchPanelMode == 1, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 1;
         ImGui::SameLine(0.0f, tabGap);
         if (ImGui::Selectable("Media", gSearchPanelMode == 2, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 2;
         ImGui::SameLine(0.0f, tabGap);
         if (ImGui::Selectable("Plugins", gSearchPanelMode == 3, 0, ImVec2(tabW, 0)))
            gSearchPanelMode = 3;
         ImGui::PopStyleVar();
         ImGui::Separator();

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
            if (DrawBrowserFilterStrip(gModulesFilter, "search modules...", kModuleSortNames, categoryNames))
               SaveBrowserFilterPrefs();

            std::string q = gModulesFilter.query;
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            const bool sortByName = (gModulesFilter.sortMode == 1);
            const bool sortByFav = (gModulesFilter.sortMode == 2);
            const std::string categoryFilter =
               (gModulesFilter.typeFilter > 0 && gModulesFilter.typeFilter < (int)categoryIds.size())
                  ? categoryIds[gModulesFilter.typeFilter] : std::string();

            std::string spawnName, spawnCategory;
            ImGui::Separator();
            ImGui::BeginChild("##nodepanellist", ImVec2(0, 0), false);

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
                        std::string hay = name + " " + category;
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
                  const float badgeReserve = 20.0f;
                  const std::string rowLabel =
                     TruncateWithEllipsis(DisplayName(match.first), std::max(20.0f, availW - badgeReserve));
                  if (ImGui::Selectable(rowLabel.c_str(), false, 0, ImVec2(availW, 0)))
                  {
                     spawnName = match.first;
                     spawnCategory = match.second;
                  }
                  if (ImGui::IsItemHovered() && rowLabel != DisplayName(match.first))
                     ImGui::SetTooltip("%s", DisplayName(match.first).c_str());
                  const ImVec2 selMin = ImGui::GetItemRectMin();
                  const ImVec2 selMax = ImGui::GetItemRectMax();
                  DrawFavoriteBadge(selMin, selMax, isFav);
                  if (ImGui::BeginPopupContextItem("##mod_ctx"))
                  {
                     if (ImGui::MenuItem(isFav ? "Remove from favourites" : "Add to favourites"))
                        gBrowserFavorites.ToggleModule(match.first);
                     if (ImGui::MenuItem("Add to canvas"))
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
                     std::string hay = name + " " + category;
                     if (NodeSearchMatches(hay, q))
                        matches.push_back(name);
                  }
                  if (matches.empty())
                     continue;
                  if (gModulesFilter.descending)
                     std::reverse(matches.begin(), matches.end());

                  ImGui::SeparatorText(DisplayName(category).c_str());
                  for (const std::string& name : matches)
                  {
                     ImGui::PushID(name.c_str());
                     const bool isFav = gBrowserFavorites.IsFavoriteModule(name);
                     const float availW = ImGui::GetContentRegionAvail().x;
                     const float badgeReserve = 20.0f;
                     const std::string rowLabel =
                        TruncateWithEllipsis(DisplayName(name), std::max(20.0f, availW - badgeReserve));
                     if (ImGui::Selectable(rowLabel.c_str(), false, 0, ImVec2(availW, 0)))
                     {
                        spawnName = name;
                        spawnCategory = category;
                     }
                     if (ImGui::IsItemHovered() && rowLabel != DisplayName(name))
                        ImGui::SetTooltip("%s", DisplayName(name).c_str());
                     const ImVec2 selMin = ImGui::GetItemRectMin();
                     const ImVec2 selMax = ImGui::GetItemRectMax();
                     DrawFavoriteBadge(selMin, selMax, isFav);
                     if (ImGui::BeginPopupContextItem("##mod_cat_ctx"))
                     {
                        if (ImGui::MenuItem(isFav ? "Remove from favourites" : "Add to favourites"))
                           gBrowserFavorites.ToggleModule(name);
                        if (ImGui::MenuItem("Add to canvas"))
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
            DrawLibrarySearchPanel(gSampleScanner, "##samples", "search samples...", false);
         }
         else if (gSearchPanelMode == 2)
         {
            DrawLibrarySearchPanel(gMediaScanner, "##media", "search media...", true);
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
         if (ImGui::Begin("Formula editor", &gFormulaEditorOpen))
         {
            ImGui::TextDisabled("body of  vec4 shape(vec2 uv, vec2 p, float t)");
            ImGui::TextDisabled("p is centred (-0.5..0.5), t is transport seconds, uA-uD are the knobs");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFormulaEditor->formula = editBuf;
               gFormulaEditor->Apply();
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
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
         if (ImGui::Begin("Field element editor", &gFieldElementEditorOpen))
         {
            ImGui::TextDisabled("Field element-domain kernel (per-vertex). Reserved: P (vec3), N (vec3), uv (vec2), Cd (vec3), i, count, t");
            ImGui::TextDisabled("User attributes: 'attrib float heat = 0'. Frame rate expressions are automatically hoisted.");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFieldElementEditor->code = editBuf;
               gFieldElementEditor->Apply();
               lastKnownCode = gFieldElementEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
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
         if (ImGui::Begin("Field primitive editor", &gFieldPrimitiveEditorOpen))
         {
            ImGui::TextDisabled("Field primitive generator (from scratch). Reserved: P (vec3), N (vec3), uv (vec2), Cd (vec3), i, count, t");
            ImGui::TextDisabled("Pure 3D geometry generator. Frame rate expressions are automatically hoisted.");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFieldPrimitiveEditor->code = editBuf;
               gFieldPrimitiveEditor->Apply();
               lastKnownCode = gFieldPrimitiveEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
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
         if (ImGui::Begin("Field pixel editor", &gFieldPixelEditorOpen))
         {
            ImGui::TextDisabled("Field pixel-domain kernel (per-pixel fragment shader).");
            ImGui::TextDisabled("Reserved: uv (vec2), xy (vec2), res (vec2), aspect, col (vec3), alpha, t, dt, frame");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFieldPixelEditor->code = editBuf;
               gFieldPixelEditor->Apply();
               lastKnownCode = gFieldPixelEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
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
         if (ImGui::Begin("Field effect editor", &gFieldSampleEditorOpen))
         {
            ImGui::TextDisabled("Field effect kernel (per-sample, per-voice, audio thread). Reserved: in, sr, n, out");
            ImGui::TextDisabled("'state float x = 0' declares per-voice memory (resets on note-on/steal). 'param float p = 0..1' exposes a modulatable knob.");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFieldSampleEditor->code = editBuf;
               gFieldSampleEditor->Apply();
               lastKnownCode = gFieldSampleEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
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
         if (ImGui::Begin("Field synth editor", &gFieldSynthEditorOpen))
         {
            ImGui::TextDisabled("Field polyphonic synth kernel (per-sample, per-voice, audio thread). Reserved: in, sr, n, freq, gate, out");
            ImGui::TextDisabled("'state float x = 0' declares per-voice memory (resets on note-on/steal). 'param float p = 0..1' exposes a modulatable knob.");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
            {
               gFieldSynthEditor->code = editBuf;
               gFieldSynthEditor->Apply();
               lastKnownCode = gFieldSynthEditor->code;
            }
            PopPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(120, 0)))
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
         if (ImGui::Begin("Field graph editor", &gFieldGraphEditorOpen))
         {
            ImGui::TextDisabled("Field graph-domain kernel (edit-time, runs once). emit(\"Type Name\", k0, k1, ...) -> handle");
            ImGui::TextDisabled("connect(src, srcSlot, dst, dstSlot)   set(handle, \"paramName\", value)   place(handle, x, y)");
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
            if (ImGui::Button("Apply", ImVec2(120, 0)))
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
            if (ImGui::Button("Revert", ImVec2(120, 0)))
               snprintf(editBuf, sizeof(editBuf), "%s", gFieldGraphEditor->code.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Regenerate", ImVec2(120, 0)))
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
               ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", gFieldGraphEditor->Notice().c_str());
            }
         }
         ImGui::End();
         PopElevatedPanelStyle();
      }

      // Expression globals now live only in Settings > Expression Globals
      // (see DrawSettingsWindow) - the standalone window used to duplicate
      // that exact editor, plus a Presets button now merged into the tab.

      if (gHelpOpen)
         DrawHelpWindow(&gHelpOpen);

      if (gShortcutsOpen)
         DrawShortcutsWindow(&gShortcutsOpen);

#ifndef NDEBUG
      // Stock ImGui windows, deliberately left un-themed (PushElevatedPanelStyle
      // etc. skipped on purpose) - they're a diagnostic overlay for picking
      // apart the ACTIVE style, not app chrome, so they should look like
      // ImGui's own default rather than inherit the thing they're inspecting.
      // Dev-only: excluded from Release builds (see the menu item above).
      if (gUiDebuggerOpen)
         ImGui::ShowMetricsWindow(&gUiDebuggerOpen);
      if (gUiStyleEditorOpen)
      {
         // ImGui::ShowStyleEditor lives in imgui_demo.cpp, which this build
         // doesn't compile in - so this is the Colors-tab portion of it,
         // reimplemented directly against the live ImGuiStyle: every
         // ImGuiCol_* by name, its current RGBA, and a live ColorEdit4 to
         // try a value before asking for the change in code.
         if (ImGui::Begin("UI Style Editor", &gUiStyleEditorOpen))
         {
            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::TextDisabled("Live-editing this ImGuiStyle for inspection only - not saved.");
            static char colorFilter[64] = "";
            ImGui::InputTextWithHint("##colorfilter", "filter colors...", colorFilter, sizeof(colorFilter));
            if (ImGui::BeginChild("##colorlist"))
            {
               for (int i = 0; i < ImGuiCol_COUNT; i++)
               {
                  const char* name = ImGui::GetStyleColorName((ImGuiCol)i);
                  if (colorFilter[0] != '\0' && !strcasestr(name, colorFilter))
                     continue;
                  ImGui::PushID(i);
                  ImGui::ColorEdit4("##col", (float*)&style.Colors[i], ImGuiColorEditFlags_AlphaBar);
                  ImGui::SameLine();
                  ImGui::TextUnformatted(name);
                  ImGui::PopID();
               }
            }
            ImGui::EndChild();
         }
         ImGui::End();
      }
#endif

      if (gSettingsOpen)
         DrawSettingsWindow(&gSettingsOpen);

      PollPatchFileWatch();
      if (gPatchChangedOnDisk)
      {
         const ImGuiViewport* vp = ImGui::GetMainViewport();
         ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->Pos.y + 48.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
         ImGui::SetNextWindowBgAlpha(0.95f);
         if (ImGui::Begin("##patchchangedondisk", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
         {
            ImGui::TextUnformatted("File changed on disk.");
            ImGui::SameLine();
            if (ImGui::Button("Reload"))
            {
               if (LoadPatchFromImpl(gPatchWatchPath, true))
                  gPatchChangedOnDisk = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep mine"))
               gPatchChangedOnDisk = false;
         }
         ImGui::End();
      }

      if (gShowUnsavedChangesModal)
      {
         ImGui::OpenPopup("Unsaved Changes");
         gShowUnsavedChangesModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("This patch has unsaved changes.");
         ImGui::Text("Save before closing?");
         ImGui::Separator();
         const float btnW = 100.0f;
         const float spacing = ImGui::GetStyle().ItemSpacing.x;
         const float totalW = btnW * 3 + spacing * 2;
         const float avail = ImGui::GetContentRegionAvail().x;
         if (avail > totalW)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW);
         // Right-aligned, cancel-to-primary reading order (platform
         // convention: the recommended default action is rightmost and
         // the only one drawn with emphasis - matching the fix in the
         // Recover Autosave modal below, which had the identical defect).
         if (ImGui::Button("Cancel", ImVec2(btnW, 0)))
         {
            gPendingUnsavedAction = nullptr;
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button("Don't Save", ImVec2(btnW, 0)))
         {
            if (gPendingUnsavedAction)
               gPendingUnsavedAction();
            gPendingUnsavedAction = nullptr;
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         PushPrimaryButtonStyle();
         const bool doSave = ImGui::Button("Save", ImVec2(btnW, 0));
         PopPrimaryButtonStyle();
         if (doSave)
         {
            SavePatchInteractive(false);
            // Only proceed if the save actually went through - a cancelled
            // Save As dialog or a write failure leaves gPatchDirty set, and
            // the modal should stay up so the user can try again.
            if (!gPatchDirty)
            {
               if (gPendingUnsavedAction)
                  gPendingUnsavedAction();
               gPendingUnsavedAction = nullptr;
               ImGui::CloseCurrentPopup();
            }
         }
         ImGui::EndPopup();
      }

      // ---- check for updates modal ----
      if (gShowUpdateCheckModal)
      {
         ImGui::OpenPopup("Check for updates");
         gShowUpdateCheckModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
      bool isUpdateCheckOpen = true;
      if (ImGui::BeginPopupModal("Check for updates", &isUpdateCheckOpen, ImGuiWindowFlags_AlwaysAutoResize))
      {
         if (!isUpdateCheckOpen)
            ImGui::CloseCurrentPopup();

         UpdateCheck::Status status = UpdateCheck::GetStatus();
         switch (status)
         {
            case UpdateCheck::Status::Idle:
            case UpdateCheck::Status::Checking:
            {
               // Text-only "spinner" - a handful of dots cycling off the
               // clock, so the modal never looks frozen while the request
               // is in flight.
               int dots = ((int)(ImGui::GetTime() * 2.0) % 4);
               ImGui::Text("Checking for updates%.*s", dots, "...");
               break;
            }
            case UpdateCheck::Status::UpToDate:
               ImGui::Text("You're running the latest version (%s).", INFINITE_VERSION_STRING);
               break;
            case UpdateCheck::Status::UpdateAvailable:
               ImGui::Text("Version %s is available (you have %s).",
                           UpdateCheck::ResultVersion().c_str(), INFINITE_VERSION_STRING);
               break;
            case UpdateCheck::Status::Failed:
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.4f, 1.0f));
               ImGui::TextWrapped("%s", UpdateCheck::LastError().c_str());
               ImGui::PopStyleColor();
               break;
         }

         ImGui::Separator();

         if (status == UpdateCheck::Status::UpdateAvailable)
         {
            PushPrimaryButtonStyle();
            const bool doDownload = ImGui::Button("Download latest version");
            PopPrimaryButtonStyle();
            if (doDownload)
               Platform::OpenExternalUrl(UpdateCheck::DownloadUrl());
            ImGui::SameLine();
            if (ImGui::Button("Later"))
               ImGui::CloseCurrentPopup();
         }
         else if (status == UpdateCheck::Status::Failed)
         {
            PushPrimaryButtonStyle();
            const bool doRetry = ImGui::Button("Retry");
            PopPrimaryButtonStyle();
            if (doRetry)
               UpdateCheck::Start();
            ImGui::SameLine();
            if (ImGui::Button("Close"))
               ImGui::CloseCurrentPopup();
         }
         else if (status == UpdateCheck::Status::UpToDate)
         {
            if (ImGui::Button("Close"))
               ImGui::CloseCurrentPopup();
         }
         // Idle/Checking: no buttons yet, just wait for Poll() to land a result.

         ImGui::EndPopup();
      }

      if (gShowAutosaveRecoveryModal)
      {
         ImGui::OpenPopup("Recover Autosave");
         gShowAutosaveRecoveryModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal("Recover Autosave", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("Infinite closed unexpectedly.");
         if (!gAutosaveRecoveryTimestamp.empty())
            ImGui::Text("A recovered version of your work from %s is available.",
                        gAutosaveRecoveryTimestamp.c_str());
         else
            ImGui::Text("A recovered version of your work is available.");
         ImGui::Separator();
         {
            const float btnW = 100.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float totalW = btnW * 2 + spacing;
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail > totalW)
               ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW);
         }
         // Discard (destructive, plain) on the left, Recover (recommended,
         // emphasized) rightmost - same right-aligned/primary-emphasis
         // convention as the Unsaved Changes modal above.
         if (ImGui::Button("Discard", ImVec2(100, 0)))
         {
            DiscardAutosave();
            const std::string marker = AutosaveMarkerPath();
            if (!marker.empty())
            {
               std::error_code ec;
               std::filesystem::remove(marker, ec);
            }
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         PushPrimaryButtonStyle();
         const bool doRecover = ImGui::Button("Recover", ImVec2(100, 0));
         PopPrimaryButtonStyle();
         if (doRecover)
         {
            ApplyPatchData(gPendingRecoveryData);
            gArrangePatchGeneration++; // a new document, same as File > Open
            gUndoStack.clear();
            gRedoStack.clear();
            gPatchPath.clear();          // it is not the user's file - force Save As
            gPatchDirty = true;          // it is unsaved work, and should say so
            gPatchStatus = "Recovered autosave. Save the project to keep it.";
            // A recovery that leaves the file behind offers itself again on
            // the next launch.
            DiscardAutosave();
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      // Keep the title bar in sync with the open document. GLFW has no
      // native "dirty dot" hook, so the bullet is just part of the string -
      // the same convention every other non-native-document-model app uses.
      {
         static std::string sLastTitle;
         std::string base = gPatchPath.empty()
            ? std::string("Untitled")
            : gPatchPath.substr(gPatchPath.find_last_of('/') + 1);
         std::string title = (gPatchDirty ? std::string("\xE2\x80\xA2 ") : std::string()) +
            base + " \xE2\x80\x94 Infinite";
         if (title != sLastTitle)
         {
            glfwSetWindowTitle(window, title.c_str());
            sLastTitle = title;
         }
      }

      // A node (e.g. SamplerNode::StartRecording/StopRecording) asked for a
      // topology rebuild outside the usual connect/disconnect/spawn/delete
      // actions - see AudioTopologyRequest.h for why it can't just call
      // RebuildAudioTopology() itself.
      if (AudioTopologyRequest::PendingRebuild())
      {
         AudioTopologyRequest::PendingRebuild() = false;
         RebuildAudioTopology();
      }

      // ---- apply modulation and expressions, then cook ----
      // Deliberately after the UI: the parameter registry is rebuilt every frame
      // while nodes draw, so every pointer here belongs to a node that still
      // exists. Cooking before the UI would mean writing through last frame's
      // pointers, which dangle the moment a node is deleted.
      // A node that cannot be bypassed never stays bypassed: covers a patch,
      // paste or undo snapshot saved before the rule existed, and a node
      // whose pin count grew past one (Mixer channels, Field pixel inputs).
      // Only nodes already flagged pay for the pin count.
      {
         bool clearedAudio = false;
         for (GraphNode& gn : gNodes)
         {
            if (!gn.node->bypassed || CanBypass(gn))
               continue;
            gn.node->bypassed = false;
            if (dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr ||
                dynamic_cast<INoteSource*>(gn.node.get()) != nullptr)
               clearedAudio = true;
         }
         if (clearedAudio)
            RebuildAudioTopology();
      }

      {
         ConditionalStageTimer timerModulation(benchStagesCpuSample ? &sStageModulation : nullptr, Bench::FrameTail::kModulation);
         Bench::ConditionalGpuStageTimer timerModulationGpu(benchStagesSample ? &sGpuTimerRing : nullptr, "modulation", frameId);
         if (!sBenchB3ProbePaused)
         {
            UpdateParamMidiLearn();
            DrawParamMidiLearnBanner();
            ApplyModulationAndPalette(frameId, true);
         }
      }

      {
         ConditionalStageTimer timerCook(benchStagesCpuSample ? &sStageCook : nullptr, Bench::FrameTail::kCook);
         Bench::ConditionalGpuStageTimer timerCookGpu(benchStagesSample && !benchGpuPerNode ? &sGpuTimerRing : nullptr, "cook", frameId);
         Bench::NodeGpuRing() = benchStagesSample && benchGpuPerNode ? &sGpuTimerRing : nullptr;
         for (GraphNode& gn : gNodes)
         {
            if (gn.node->bypassed)
            {
               if (auto* syphonOut = dynamic_cast<SyphonOutNode*>(gn.node.get()))
                  syphonOut->Withdraw();
               continue;
            }
            if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<SyphonOutNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<OscSendNode*>(gn.node.get()) != nullptr)
               gn.node->CookIfNeeded(frameId);
         }
         Bench::NodeGpuRing() = nullptr;
      }

      if (isBenchB3 && sBenchB3PendingInputInjectFrame >= 0)
      {
         if (auto* outGn = FindNodeByIndex(sBenchB3OutputIdx))
         {
            if (auto* outNode = dynamic_cast<OutputNode*>(outGn->node.get()))
            {
               if (outNode->Input().Revision() != sBenchB3RevBeforeInject)
               {
                  const int latencyFrames = frameId - sBenchB3PendingInputInjectFrame + 1;
                  sBenchB3InputToPhotonFrames.Push((double)latencyFrames);
                  sBenchB3PendingInputInjectFrame = -1;
                  sBenchB3ProbePaused = false;
               }
               else if (frameId - sBenchB3PendingInputInjectFrame >= 5)
               {
                  sBenchB3PendingInputInjectFrame = -1;
                  sBenchB3ProbePaused = false;
               }
            }
         }
      }
      if (getenv("INFINITE_SHOWCASE") != nullptr && frameId == 1)
      {
         for (const ParamRef& ref : Modulation::Instance().FrameParams())
         {
            if (ref.nodeIndex == gNodes[1].index && ref.name == "Amount")
               Modulation::Instance().Bind(ref.nodeIndex, ref.paramIndex, gNodes[5].index);
         }
      }

      // Modulation-heavy binding half of INFINITE_MIXEDSTRESSTEST - see the
      // setup half above (before the main loop starts). Deferred to
      // frameId==1, after this frame's own UI draw has registered every
      // spawned node's params with Modulation's FrameParams(); binding here
      // takes effect starting next frame, same as INFINITE_SHOWCASE above.
      // LFO count scales with `scale`; "channels" is skipped so modulating
      // it can't shrink Mixer's slot count out from under the audio rack
      // wired above, and each LFO's own params are skipped so a later LFO
      // never gets bound to modulate an earlier LFO.
      FrameTest_MIXEDSTRESSTEST_2(frameId, window);

      if (getenv("INFINITE_MACROTEST") != nullptr)
      {
         auto& mod = Modulation::Instance();
         if (gNodes.size() < 3)
         {
            printf("MACROTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* xy = static_cast<MacroXYNode*>(gNodes[2].node.get());
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 2)
         {
            int sizeParam = -1, rotParam = -1;
            for (const ParamRef& ref : mod.FrameParams())
            {
               if (ref.nodeIndex != gNodes[0].index) continue;
               if (ref.name == "size x") sizeParam = ref.paramIndex;
               if (ref.name == "rotation") rotParam = ref.paramIndex;
            }
            // X drives size, Y drives rotation - one pad, two destinations
            printf("sizeParam=%d rotParam=%d node0=%d node2=%d frameParams=%zu\n",
                   sizeParam, rotParam, gNodes[0].index, gNodes[2].index, mod.FrameParams().size());
            mod.Bind(gNodes[0].index, sizeParam, gNodes[2].index, 0);
            mod.Bind(gNodes[0].index, rotParam, gNodes[2].index, 1);
            printf("isModulated(size)=%d isModulated(rot)=%d\n",
                   (int)mod.IsModulated(gNodes[0].index, sizeParam),
                   (int)mod.IsModulated(gNodes[0].index, rotParam));
            xy->padX = 0.25f;
            xy->padY = 0.75f;
            printf("bound X->size Y->rotation\n");
         }
         if (frameId == 5)
         {
            // "size x" is a ModSlider over 0.01f..1.0f (see DrawShapeBody);
            // the expectation must be derived from that same span, not from a
            // hardcoded max that silently goes stale when the slider widens.
            printf("padX=%.2f -> size=%.4f (expect %.4f)\n", xy->padX, sh->sizeX, 0.01f + 0.99f * 0.25f);
            printf("padY=%.2f -> rotation=%.4f (expect %.4f)\n", xy->padY, sh->rotation, -180.0f + 360.0f * 0.75f);
            xy->padX = 0.9f; xy->padY = 0.1f;
         }
         if (frameId == 8)
         {
            printf("after move: size=%.4f rotation=%.4f  %s\n", sh->sizeX, sh->rotation,
                   (std::fabs(sh->sizeX - (0.01f + 0.99f * 0.9f)) < 0.01f &&
                    std::fabs(sh->rotation - (-180.0f + 360.0f * 0.1f)) < 3.0f)
                      ? "INDEPENDENT OUTPUTS OK" : "MISMATCH");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      FrameTest_HIDETEST_2(frameId, window);

      FrameTest_MODTEST(frameId, window);

      FrameTest_MODBOUNDSTEST(frameId, window);

      // MPC modulation stability (spec 11): a binding on one pad's param must
      // keep driving THAT pad and only that pad when another pad is selected,
      // and must survive save/load. Every pad's every param has a fixed
      // address (MpcNode::ParamId / the per-pad mode label), independent of
      // the selected pad.
      FrameTest_MPCMODTEST(frameId, window);

      FrameTest_LOOPERTRIGTEST(frameId, window);

      FrameTest_MIDILEARNTEST(frameId, window);

      if (getenv("INFINITE_MODMATRIXGEOM") != nullptr)
      {
         // The matrix pads itself out with empty rows so its horizontal
         // grid lines reach the bottom of the panel. That padding must be
         // computed in scroll-invariant coordinates: a screen-space target
         // grew the table by one row per scroll step, so the scroll range
         // extended every time the user reached the bottom and the table
         // scrolled without end. The probe above pins scroll to the bottom
         // every frame, so an offset-dependent count shows up here as a
         // row count (and scroll range) that keeps climbing.
         //
         // Needs one bound link: DrawModMatrixTable shows "No active
         // modulations" and skips BeginTable entirely with none, which
         // would make the checks below trivially pass without ever
         // exercising the fill loop.
         static int sidesParam = -1;
         Modulation& mod = Modulation::Instance();
         if (frameId == 1)
         {
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == gNodes[0].index && ref.name == "sides")
                  sidesParam = ref.paramIndex;
            mod.Bind(gNodes[0].index, sidesParam, gNodes[2].index);
         }
         static int firstRows = -1;
         static bool drifted = false;
         if (gModMatrixFillRows >= 0)
         {
            if (firstRows < 0 && frameId > 3)
               firstRows = gModMatrixFillRows;
            else if (firstRows >= 0 && gModMatrixFillRows != firstRows)
               drifted = true;
         }
         if (frameId == 24)
         {
            const bool ok = !drifted && firstRows > 0 && gModMatrixScrollMax <= 0.0f;
            printf("modmatrix geom (fill stable under scroll) rows=%d firstRows=%d "
                   "drifted=%d scrollMax=%.1f  %s\n",
                   gModMatrixFillRows, firstRows, (int)drifted, gModMatrixScrollMax,
                   ok ? "OK" : "- BUG");
            printf("%s\n", ok ? "MOD MATRIX GEOM OK" : "SUSPECT");
         }
      }

      // A Shift-drag recording is session state, not patch content - so undo
      // has to carry it by hand (see UndoEntry/RemapGestures). Three things
      // can silently break at once and none of them is visible without a
      // fixture: the recording surviving an undo that predates it, the undo
      // *not* restoring it on redo, and - because ApplyPatchData respawns
      // every node with a fresh index - the restored recording landing on the
      // wrong index. This drives the real GestureRecorder API in the real
      // frame order and checks all three.
      FrameTest_GESTUREUNDOTEST(frameId, window);

      // Every time-based animation source must advance on offline Transport
      // time during a render, exactly once per rendered frame, whatever the
      // wall clock does. One Shape carries an LFO binding (size x), a time
      // expression (size y) and a gesture loop (rotation). The same range is
      // swept offline twice at 30 fps and once at 60 fps, the way the Render
      // Now pump does it (variable-size batches of frames per UI frame, the
      // live gesture clock advancing by a random wall dt between batches),
      // then once from a 2.5 s start. Before the gesture clock followed
      // Transport offline, rotation stair-stepped on the wall dt and the
      // two 30 fps runs disagreed.
      static bool offlineClockDone = false;
      if (getenv("INFINITE_OFFLINECLOCKTEST") != nullptr && frameId >= 1 && !offlineClockDone)
      {
         Modulation& mod = Modulation::Instance();
         GestureRecorder& rec = GestureRecorder::Instance();
         Transport& transport = Transport::Instance();
         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         int sizeXParam = -1, sizeYParam = -1, rotParam = -1;
         for (const ParamRef& ref : mod.FrameParams())
         {
            if (ref.nodeIndex != gNodes[0].index)
               continue;
            if (ref.name == "size x") sizeXParam = ref.paramIndex;
            if (ref.name == "size y") sizeYParam = ref.paramIndex;
            if (ref.name == "rotation") rotParam = ref.paramIndex;
         }
         const bool resolved = sizeXParam >= 0 && sizeYParam >= 0 && rotParam >= 0;
         // The Shape registers its controls only on a frame that draws its
         // params; under load that frame can slip past frame 1 (a full driver
         // run saw "params -1,-1,-1"). Retry each frame up to the last one the
         // driver gives us, and only then report the failure.
         if (!resolved && frameId < 6)
            ;
         else
         {
         offlineClockDone = true;
         if (resolved)
         {
            mod.Bind(gNodes[0].index, sizeXParam, gNodes[2].index);
            mod.SetExpression(gNodes[0].index, sizeYParam, "lerp(lo, hi, mod(t, 4) / 4)");
            // A one-second -90 -> 90 ramp, ended at once so it is already looping.
            const double t0 = rec.ClockNow();
            rec.BeginFrame(/*shiftHeld=*/true, t0);
            rec.NotifyMovement(gNodes[0].index, rotParam, -90.0f, t0, /*isNewGrab=*/true);
            rec.NotifyMovement(gNodes[0].index, rotParam, 0.0f, t0 + 0.5, /*isNewGrab=*/false);
            rec.NotifyMovement(gNodes[0].index, rotParam, 90.0f, t0 + 1.0, /*isNewGrab=*/false);
            rec.BeginFrame(/*shiftHeld=*/false, t0 + 1.0);
         }
         transport.SetPlaying(false); // every run enters offline from the same transport position

         struct Values { float sizeX, sizeY, rot; };
         int sweepFrameId = 1000000; // clear of the main loop's ids, so no modulator memo is reused
         uint32_t lcg = 12345u;
         auto sweep = [&](int fps, double start, int frames, uint32_t wallSeed) {
            std::vector<Values> out;
            lcg = wallSeed;
            transport.SetOfflineMode(true, 48000.0);
            int n = 0;
            while (n < frames)
            {
               lcg = lcg * 1664525u + 1013904223u;
               rec.AdvanceClock((double)(lcg >> 8) / (double)(1u << 24) * 0.2, true); // wall dt in [0, 0.2)
               const int batch = 1 + (int)((lcg >> 4) % 7u);
               for (int b = 0; b < batch && n < frames; ++b, ++n)
               {
                  transport.SetOfflineVideoTime(start + (double)n / (double)fps);
                  ApplyModulationAndPalette(++sweepFrameId);
                  out.push_back({ shape->sizeX, shape->sizeY, shape->rotation });
               }
            }
            transport.SetOfflineMode(false, 0.0);
            return out;
         };
         const std::vector<Values> a = sweep(30, 0.0, 90, 1u);
         const std::vector<Values> b = sweep(30, 0.0, 90, 777u);
         const std::vector<Values> c = sweep(60, 0.0, 180, 4242u);
         const std::vector<Values> d = sweep(30, 2.5, 60, 99u);

         const bool sameRuns = a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Values)) == 0;
         double worstRate = 0.0, worstRamp = 0.0, worstStart = 0.0;
         for (size_t k = 0; k < a.size() && 2 * k < c.size(); ++k)
         {
            worstRate = std::max(worstRate, (double)std::abs(a[k].sizeX - c[2 * k].sizeX));
            worstRate = std::max(worstRate, (double)std::abs(a[k].sizeY - c[2 * k].sizeY));
            worstRate = std::max(worstRate, (double)std::abs(a[k].rot - c[2 * k].rot));
         }
         auto ramp = [](double t) { return -90.0 + 180.0 * std::fmod(t, 1.0); };
         for (size_t k = 0; k < a.size(); ++k)
            worstRamp = std::max(worstRamp, std::abs((double)a[k].rot - ramp((double)k / 30.0)));
         for (size_t k = 0; k < d.size(); ++k)
            worstStart = std::max(worstStart, std::abs((double)d[k].rot - ramp(2.5 + (double)k / 30.0)));
         // The LFO and expression must actually move, or equal runs prove nothing.
         const bool moving = !a.empty() && a.front().sizeX != a[a.size() / 2].sizeX &&
                             a.front().sizeY != a[a.size() / 2].sizeY;

         const bool ok = resolved && moving && sameRuns && worstRate <= 1e-6 && worstRamp <= 1e-3 && worstStart <= 1e-3;
         printf("offline clock: params %d,%d,%d moving=%d 30==30 %s, 30 vs 60 worst %.3g, ramp worst %.3g, start 2.5 worst %.3g\n",
                sizeXParam, sizeYParam, rotParam, (int)moving, sameRuns ? "identical" : "DIFFERENT",
                worstRate, worstRamp, worstStart);
         printf("%s\n", ok ? "OFFLINE CLOCK OK" : "OFFLINE CLOCK FAIL");
         }
      }

      // A gesture loop (Shift-drag recording) on a node whose body is skipped -
      // off screen (culled) or eye closed (register-only pass) - must still
      // move every frame. Both skips are gated on GraphNode::IsParamDriven;
      // before gestures were in it, the culled node stepped once per
      // kCullRefresh frames and the collapsed one froze, in the canvas and in
      // any render/record taken while zoomed in.
      FrameTest_CULLDRIVENTEST(frameId, window);

#ifndef NDEBUG
      if (getenv("INFINITE_PREDBINDTEST") != nullptr)
      {
         static int sizeYParam = -1, rotParam = -1, freqParam = -1, discParam = -1;
         static int filterIdx = -1;
         static ParamRef sizeXRef, freqRef;
         static bool ok[8] = {};
         static uint64_t stubUid = 0;
         static int lastTicks = 0, tickCalls = 0, tickBad = 0;
         static float posAtHold = -1.0f, discBefore = 0.0f, holdBefore = 0.0f;
         static int ticksAtBypass = 0;
         Modulation& mod = Modulation::Instance();
         auto stubNode = [&]() -> StubPredictorNode* {
            for (GraphNode& gn : gNodes)
               if (auto* st = dynamic_cast<StubPredictorNode*>(gn.node.get())) return st;
            return nullptr;
         };
         auto frameRefFor = [&](int nodeIdx, int param) -> const ParamRef* {
            for (const ParamRef& r : mod.FrameParams())
               if (r.nodeIndex == nodeIdx && r.paramIndex == param) return &r;
            return nullptr;
         };
         auto sizeXPos = [&]() -> float {
            const ParamRef* r = frameRefFor(gNodes[0].index, gPredTestSizeXParam);
            return r != nullptr ? ParamToPos(*r, *r->value) : -1.0f;
         };
         StubPredictorNode* stub = stubNode();

         if (frameId == 1)
         {
            for (const ParamRef& r : mod.FrameParams())
            {
               if (r.nodeIndex == gNodes[0].index)
               {
                  if (r.name == "size x") { gPredTestSizeXParam = r.paramIndex; gPredTestNodeIndex = gNodes[0].index; sizeXRef = r; }
                  if (r.name == "size y") sizeYParam = r.paramIndex;
                  if (r.name == "rotation") rotParam = r.paramIndex;
                  if ((r.isEnum || r.isBool) && discParam < 0) discParam = r.paramIndex;
               }
               if (r.nodeIndex == gNodes[3].index && r.posToValue != nullptr && freqParam < 0)
               {
                  freqParam = r.paramIndex; filterIdx = r.nodeIndex; freqRef = r;
               }
            }
            printf("predbind: sizeX=%d sizeY=%d rot=%d discrete=%d filterFreq=%d\n", gPredTestSizeXParam, sizeYParam,
                   rotParam, discParam, freqParam);
            if (stub == nullptr || gPredTestSizeXParam < 0 || sizeYParam < 0 || rotParam < 0 || discParam < 0 || freqParam < 0)
            {
               printf("PREDBINDTEST FAIL (fixture: could not resolve params)\n");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
               return 1;
            }
            stubUid = UidForIndex(gNodes[2].index);
            stub->pos = 0.5f;
            mod.Bind(gNodes[0].index, gPredTestSizeXParam, gNodes[2].index);
            mod.Bind(gNodes[0].index, sizeYParam, gNodes[2].index);
            mod.Bind(gNodes[0].index, rotParam, gNodes[2].index);
            mod.Bind(filterIdx, freqParam, gNodes[2].index);
         }
         // 1. one stub drives four params: Tick runs once per frame
         if (frameId >= 3 && frameId <= 8 && stub != nullptr)
         {
            if (frameId > 3) { ++tickCalls; if (stub->tickCount - lastTicks != 1) ++tickBad; }
            lastTicks = stub->tickCount;
            if (frameId == 8)
            {
               ok[1] = tickCalls == 5 && tickBad == 0;
               printf("test1 (idempotent Tick, 4 bindings) frames=%d bad=%d  %s\n", tickCalls, tickBad, ok[1] ? "OK" : "- BUG");
               // 2. fader space: p = 0.5 on a log-tapered param lands at posToValue(0.5), not the linear midpoint
               const ParamRef* fr = frameRefFor(filterIdx, freqParam);
               if (fr != nullptr)
               {
                  const float expect = std::clamp(fr->posToValue(0.5f, fr->minValue, fr->maxValue), fr->minValue, fr->maxValue);
                  const float linMid = 0.5f * (fr->minValue + fr->maxValue);
                  ok[2] = std::fabs(*fr->value - expect) <= 1e-3f * std::max(1.0f, std::fabs(expect)) &&
                          std::fabs(expect - linMid) > 0.01f * (fr->maxValue - fr->minValue);
                  printf("test2 (fader-space map) value=%.3f expect=%.3f linearMid=%.3f  %s\n", *fr->value, expect, linMid,
                         ok[2] ? "OK" : "- BUG");
               }
            }
         }
         // 3a. plain grab: no Shift, so nothing changes
         if (frameId == 69 && stub != nullptr)
         {
            const float pos = sizeXPos();
            ok[3] = stub->grabCount == 0 && stub->releaseCount == 0 && std::fabs(pos - 0.5f) < 0.02f;
            printf("test3a (plain grab ignored) grabs=%d pos=%.3f  %s\n", stub->grabCount, pos, ok[3] ? "OK" : "- BUG");
         }
         // 3b. Shift-grab: held by hand mid-drag, OnRelease gets the new pos, then the predictor resumes
         if (frameId == 79 && stub != nullptr)
         {
            posAtHold = sizeXPos();
            holdBefore = posAtHold;
            ok[4] = stub->grabCount == 1 && posAtHold > 0.6f;
            printf("test3b (shift-grab holds) grabs=%d pos=%.3f  %s\n", stub->grabCount, posAtHold, ok[4] ? "OK" : "- BUG");
         }
         if (frameId == 85 && stub != nullptr)
         {
            const float pos = sizeXPos();
            ok[5] = stub->releaseCount == 1 && stub->releasePos > 0.6f && std::fabs(stub->releasePos - holdBefore) < 0.05f &&
                    stub->lastRelease.uid == UidForIndex(gNodes[0].index) && std::fabs(pos - 0.5f) < 0.02f;
            printf("test3c (release + resume) releases=%d releasePos=%.3f vel=%.2f resumedPos=%.3f  %s\n",
                   stub->releaseCount, stub->releasePos, stub->releaseVel, pos, ok[5] ? "OK" : "- BUG");
         }
         if (frameId == 90)
         {
            const bool clear = gPredictorGrabs.empty() && gPredictorGrabsPrev.empty();
            printf("test3d (grab set cleared) %s\n", clear ? "OK" : "- BUG");
            ok[5] = ok[5] && clear;
            // 4. undo keeps the slot key: unbind, undo, the binding is back and keyed by the same uid
            PushUndoCheckpoint();
            mod.Unbind(gNodes[0].index, sizeYParam);
            Undo();
         }
         if (frameId == 91)
         {
            stub = stubNode();
            if (stub != nullptr)
               stub->pos = 0.25f;
         }
         if (frameId == 94)
         {
            const Modulation::Source s = mod.ModulatorFor(gNodes[0].index, sizeYParam);
            const ParamRef* yr = frameRefFor(gNodes[0].index, sizeYParam);
            const float yPos = yr != nullptr ? ParamToPos(*yr, *yr->value) : -1.0f;
            ok[6] = stub != nullptr && s.nodeIndex >= 0 && UidForIndex(s.nodeIndex) == stubUid && std::fabs(yPos - 0.25f) < 0.02f &&
                    stub->readKeys.count(ParamKey{ UidForIndex(gNodes[0].index), sizeYParam }) > 0;
            printf("test4 (undo keeps uid key) bound=%d uidSame=%d yPos=%.3f  %s\n", (int)(s.nodeIndex >= 0),
                   (int)(s.nodeIndex >= 0 && UidForIndex(s.nodeIndex) == stubUid), yPos, ok[6] ? "OK" : "- BUG");
         }
         // 5. green never binds to a discrete param: refused at the cable drop, inert when it arrives via a patch
         if (frameId == 96 && stub != nullptr)
         {
            const ParamRef* dr = frameRefFor(gNodes[0].index, discParam);
            discBefore = dr != nullptr ? *dr->value : 0.0f;
            const bool refused = PredictorBindRefusal(stub, gNodes[0].index, discParam) != nullptr;
            const bool contRefused = PredictorBindRefusal(stub, gNodes[0].index, sizeYParam) != nullptr;
            Modulation::Source src;
            src.nodeIndex = gNodes[2].index; src.lo = 0.0f; src.hi = 1.0f; src.hasRange = true;
            mod.RestoreLink(gNodes[0].index, discParam, src); // what a hand-edited patch file would do
            ok[7] = refused && !contRefused;
            printf("test5a (cable drop refuses discrete) refused=%d continuousRefused=%d  %s\n", (int)refused, (int)contRefused,
                   ok[7] ? "OK" : "- BUG");
            stub->pos = 0.9f;
            stub->readKeys.clear();
         }
         if (frameId == 100 && stub != nullptr)
         {
            const ParamRef* dr = frameRefFor(gNodes[0].index, discParam);
            const bool held = dr != nullptr && *dr->value == discBefore;
            const bool neverRead = stub->readKeys.count(ParamKey{ UidForIndex(gNodes[0].index), discParam }) == 0;
            const bool inert = IsInertPredictorBinding(gNodes[0].index, discParam);
            const bool good = held && neverRead && inert;
            printf("test5b (loaded discrete binding inert) held=%d neverRead=%d matrixInert=%d  %s\n", (int)held, (int)neverRead,
                   (int)inert, good ? "OK" : "- BUG");
            ok[7] = ok[7] && good;
            mod.Unbind(gNodes[0].index, discParam);
         }
         // 6. bypass: Tick stops and the param holds; un-bypass resumes
         static float xAtBypass = -1.0f;
         if (frameId == 102 && stub != nullptr)
         {
            xAtBypass = sizeXPos();
            ticksAtBypass = stub->tickCount;
            stub->pos = 0.1f;
            gNodes[2].node->bypassed = true;
         }
         if (frameId == 106 && stub != nullptr)
         {
            const bool frozen = stub->tickCount == ticksAtBypass && std::fabs(sizeXPos() - xAtBypass) < 0.02f;
            gNodes[2].node->bypassed = false;
            ok[0] = frozen;
            printf("test6a (bypass freezes) ticks=%d/%d pos=%.3f held=%.3f  %s\n", stub->tickCount, ticksAtBypass, sizeXPos(),
                   xAtBypass, frozen ? "OK" : "- BUG");
         }
         if (frameId == 110 && stub != nullptr)
         {
            const bool resumed = stub->tickCount > ticksAtBypass && std::fabs(sizeXPos() - 0.1f) < 0.02f;
            ok[0] = ok[0] && resumed;
            printf("test6b (un-bypass resumes) ticks=%d pos=%.3f  %s\n", stub->tickCount, sizeXPos(), resumed ? "OK" : "- BUG");
            const bool all = ok[0] && ok[1] && ok[2] && ok[3] && ok[4] && ok[5] && ok[6] && ok[7];
            printf("%s\n", all ? "PREDBINDTEST OK" : "PREDBINDTEST FAIL");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
#endif

      if (getenv("INFINITE_MODMATRIXTEST") != nullptr)
      {
         static int sidesParam = -1;
         static bool test1Ok = false, test2Ok = false, test3Ok = false, test4Ok = false;
         static int sidesAtDisable = 0;
         static bool movedWhileEnabled = false;
         auto* shape = static_cast<ShapeNode*>(gNodes[0].node.get());
         auto* r2r = static_cast<RangeToRangeNode*>(gNodes[2].node.get());
         Modulation& mod = Modulation::Instance();

         if (frameId == 1)
         {
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == gNodes[0].index && ref.name == "sides")
                  sidesParam = ref.paramIndex;
            printf("sides param=%d\n", sidesParam);
            r2r->outLow = 3.0f;
            r2r->outHigh = 20.0f;
            r2r->clampOutput = true;
            r2r->constantIn = 0.0f; // -> sides = 3
            mod.Bind(gNodes[0].index, sidesParam, gNodes[2].index);
         }
         if (frameId == 3)
         {
            // Test 1: the binding exists and Links() reports it, matching
            // what the matrix table iterates.
            test1Ok = mod.Links().size() == 1 &&
                      mod.ModulatorFor(gNodes[0].index, sidesParam).nodeIndex == gNodes[2].index;
            printf("test1 (link exists) links=%zu  %s\n", mod.Links().size(),
                   test1Ok ? "OK" : "- BUG");
            gNodes[0].showParams = false; // collapse the modulated node
         }
         if (frameId == 5)
         {
            // Test 2: the matrix can still resolve a collapsed destination's
            // name/range. Two things have to hold at once: a collapsed node
            // that something is patched into keeps re-registering its params
            // (otherwise the apply pass has no pointer to write through and
            // the modulation silently freezes - see gParamRegisterOnly), and
            // KnownParam stays sticky for the cases that genuinely register
            // nothing (a docked matrix panel drawing before the canvas has,
            // a param behind a mode switch).
            bool registeredThisFrame = false;
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == gNodes[0].index && ref.paramIndex == sidesParam)
                  registeredThisFrame = true;
            const ParamRef* known = mod.KnownParam(gNodes[0].index, sidesParam);
            test2Ok = registeredThisFrame && known != nullptr && known->name == "sides";
            printf("test2 (collapsed node still registers, KnownParam sticky) registered=%d known=%p name=%s  %s\n",
                   registeredThisFrame, (const void*)known, known ? known->name.c_str() : "(null)",
                   test2Ok ? "OK" : "- BUG");
            gNodes[0].showParams = true; // reopen
            r2r->constantIn = 1.0f; // -> sides should move towards 20 while still enabled
         }
         if (frameId == 8)
         {
            // Positive control: prove the binding actually drives the
            // value while enabled, so freezing it after disable (below)
            // means something.
            movedWhileEnabled = shape->sides != 3;
            printf("moved while enabled: sides=%d %s\n", shape->sides,
                   movedWhileEnabled ? "MOVED" : "DID NOT MOVE");
            mod.SetEnabled(gNodes[0].index, sidesParam, false);
            sidesAtDisable = shape->sides;
            r2r->constantIn = 0.0f; // try to pull it back to 3 - must have no effect once disabled
         }
         if (frameId == 13)
         {
            // Test 3: a disabled binding stops being written - sides must
            // still be exactly what it was the frame it was disabled, even
            // though the modulator driving it (now aimed back at the other
            // end of its range) keeps changing.
            test3Ok = movedWhileEnabled && shape->sides == sidesAtDisable;
            printf("test3 (disabled freezes value) at=%d now=%d  %s\n",
                   sidesAtDisable, shape->sides, test3Ok ? "OK" : "- BUG");
            SavePatchTo(TmpPath("infinite_modmatrixtest.infinite"));
            // Also check the RemoteControl JSON export carries enabled=false
            // through PatchJson::ToJson - the text patch and the JSON view
            // must agree.
            nlohmann::json out = PatchJson::ToJson(BuildPatchData());
            bool jsonEnabledFalse = false;
            for (const auto& m : out["modulation"])
            {
               if (m["dstIndex"] == gNodes[0].index && m["dstParam"] == sidesParam)
                  jsonEnabledFalse = (m["enabled"] == false);
            }
            printf("test4a (json enabled=false) %s\n", jsonEnabledFalse ? "OK" : "- BUG");
            test4Ok = jsonEnabledFalse;
         }
         if (frameId == 15)
         {
            NewPatch();
            LoadPatchFrom(TmpPath("infinite_modmatrixtest.infinite"));
         }
         if (frameId == 17)
         {
            // Test 4 (continued): the text patch round-trip must reproduce
            // the same disabled binding.
            int reloadedNodeIdx = -1, reloadedSidesParam = -1;
            for (GraphNode& gn : gNodes)
               if (dynamic_cast<ShapeNode*>(gn.node.get()) != nullptr)
                  reloadedNodeIdx = gn.index;
            for (const ParamRef& ref : mod.FrameParams())
               if (ref.nodeIndex == reloadedNodeIdx && ref.name == "sides")
                  reloadedSidesParam = ref.paramIndex;
            const Modulation::Source reloaded = mod.ModulatorFor(reloadedNodeIdx, reloadedSidesParam);
            const bool textEnabledFalse = !reloaded.enabled;
            printf("test4b (text patch enabled=false) %s\n", textEnabledFalse ? "OK" : "- BUG");
            test4Ok = test4Ok && textEnabledFalse;

            // No self-close here: INFINITE_EXITAFTER owns closing the window
            // and flushing stdout - see the identical comment on
            // INFINITE_MODBOUNDSTEST above.
            printf("%s\n", (test1Ok && test2Ok && test3Ok && test4Ok) ? "MOD MATRIX TEST OK" : "SUSPECT");
         }
      }

      FrameTest_MODCURVETEST(frameId, window);

      if (getenv("INFINITE_OSCTEST") != nullptr)
      {
         if (gNodes.size() < 5)
         {
            printf("OSCTEST fixture missing (%zu nodes)\n", gNodes.size());
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return 1;
         }
         auto* constNode = static_cast<ConstantNode*>(gNodes[2].node.get());
         auto* send = static_cast<OscSendNode*>(gNodes[3].node.get());
         auto* recv = static_cast<OscReceiveNode*>(gNodes[4].node.get());
         if (frameId == 1)
         {
            // A dedicated port, not the 9000 default both nodes spawn with -
            // proves the port field itself (and the receive listener restart
            // it triggers) actually takes effect, not just the wiring.
            const int testPort = 9737;
            constNode->value = 0.73f;
            send->host = "127.0.0.1";
            send->port = testPort;
            send->address = "/infinite/osctest";
            recv->port = testPort;
            recv->address = "/infinite/osctest";
         }
         if (frameId == 60)
         {
            const float got = recv->Value01();
            const bool ok = std::fabs(got - constNode->value) < 0.02f;
            printf("osc sent=%.3f received=%.3f  %s\n", constNode->value, got,
                   ok ? "ROUND TRIP OK" : "BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      // Must run before the cook loop just below - see
      // ArrangeSeekVideoSampleSources's own comment for why.
      ArrangeSeekVideoSampleSources(Transport::Instance().Beats());

      // A bypassed node is out of the chain, so it does no work at all: a
      // sender (Syphon/Spout Out, OSC Send) stops sending, a receiver
      // (camera, Syphon In, video) stops pulling frames into the graph, and
      // a GPU effect stops rendering. Nothing reads its texture - every
      // image read resolves past it (ImageCable::Resolved).
      {
         // B6 only: the whole-graph cook is outside every other stage, so
         // without this the canvas bench cannot tell cook time from UI time.
         ConditionalStageTimer timerCookAll((benchB6Stages && benchStagesCpuSample) ? &sStageCookAll : nullptr, Bench::FrameTail::kCookAll);
         for (GraphNode& gn : gNodes)
            if (!gn.node->bypassed)
               gn.node->CookIfNeeded(frameId);
      }

      // Arrangement monitor (overhaul WP4): after the cook, so the clips it
      // selects and the textures it reads belong to the same frame.
      CompositeArrangeMonitorIfRequested();
      ReapArrangeGeomViewports();
      // Unconditional, panel open or not: the audio thread's peak ring has
      // to be drained every frame or it fills and starts dropping buckets
      // the waveform would never get back (WP8).
      ArrangeSyncClipVisuals();

      // The node cook loop above is the longest single stretch of the frame,
      // and it runs after glfwPollEvents() with the run loop otherwise
      // unserviced - see local-prompts/02-plugin-editor-lag.md. Pump it here
      // so a hosted plugin's editor window (the app's only real NSWindow)
      // doesn't sit starved for the length of a heavy cook. Called
      // unconditionally, not gated on AnyPluginEditorOpen(): on Linux (task
      // 4.3) a plugin can register IRunLoop timers through the
      // factory-context host object with no editor open at all, and those
      // still need servicing every frame. Cheap when idle on every
      // platform - macOS's CFRunLoopRunInMode(..., 0.0, ...) and Windows'
      // PeekMessageW(nullptr, ...) are both no-ops with nothing queued.
      Platform::PumpPluginEditorEvents();

      // Top-level idle gate: NodeWorkCounter() only advances when some node
      // actually redid real work this frame (FilterNode's RunShaderPass,
      // Render3DNode's draw passes, ...) - a cache hit leaves it alone. A
      // patch with nothing left to compute settles into idle==true every
      // frame within a couple frames of its last real edit, the same way a
      // static Blender viewport stops re-rendering. This only observes that;
      // it deliberately does not skip glfwSwapBuffers/ImGui's own redraw,
      // since those still have to run for UI responsiveness (hover states,
      // cursor, animated widgets) regardless of whether the node graph is idle.
      FrameTest_CACHETEST(frameId, window);

      if (getenv("INFINITE_TEXTFIT") != nullptr && frameId == 4 && !gNodes.empty())
      {
         if (auto* t = dynamic_cast<TextNode*>(gNodes[0].node.get()))
            printf("requested %.0f pt -> fitted %.1f pt (box %.0fx%.0f of %dx%d)\n",
                   t->fontSize, t->FittedSize(),
                   t->width * t->wrapWidth, t->height * t->wrapHeight,
                   t->GetOutputWidth(), t->GetOutputHeight());
      }

      if (selfTest && frameId >= 1)
      {
         int failures = 0;
         for (GraphNode& gn : gNodes)
         {
            // modulators emit a value, not a texture, so they are checked
            // differently - including nodes that expose taps rather than being
            // modulators themselves (Image/Audio Analyze).
            if (dynamic_cast<CameraNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<LightNode*>(gn.node.get()) != nullptr)
            {
               printf("%-22s [%-12s] scene node     OK\n", gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* op = dynamic_cast<GeometryOpNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), op->TriangleCount());
               continue;
            }
            if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu instances  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), inst->InstanceCount());
               continue;
            }
            if (auto* ps = dynamic_cast<ParticleSystemNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu particles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), ps->AliveCount());
               continue;
            }
            if (dynamic_cast<Null3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MappingNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MaterialNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DisplacementNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<ClothNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<JoinGeometryNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<WrapNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Switcher3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<Group3DNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MetaBallNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<CurveNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshToPointsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<MeshResynthNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<ImageToPointsNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<DepthProjectionNode*>(gn.node.get()) != nullptr)
            {
               // Pass-throughs and samplers with nothing patched in are empty
               // by definition, so that is not a failure.
               printf("%-22s [%-12s] geometry pass  OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }
            if (auto* oc = dynamic_cast<OceanNode*>(gn.node.get()))
            {
               const bool ok = oc->TriangleCount() > 0;
               if (!ok) ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), oc->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }
            if (auto* t3d = dynamic_cast<Text3DNode*>(gn.node.get()))
            {
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), t3d->TriangleCount());
               continue;
            }
            if (auto* model = dynamic_cast<ModelSourceNode*>(gn.node.get()))
            {
               // A freshly spawned Model 3D has no file yet, so an empty mesh
               // here is correct rather than a failure - same as a Geometry Op
               // with nothing patched into it.
               printf("%-22s [%-12s] %zu triangles  OK\n",
                      gn.typeName.c_str(), gn.category.c_str(), model->TriangleCount());
               continue;
            }
            if (auto* geo = dynamic_cast<GeometryNode*>(gn.node.get()))
            {
               // geometry emits a mesh, not a texture
               const bool ok = geo->TriangleCount() > 0;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] %zu triangles  %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), geo->TriangleCount(),
                      ok ? "OK" : "FAIL");
               continue;
            }

            if (dynamic_cast<IAudioSource*>(gn.node.get()) != nullptr ||
                dynamic_cast<AudioOutputNode*>(gn.node.get()) != nullptr)
            {
               // P2's audio-graph nodes (AudioFileNode included, now that
               // it's a real IAudioSource) emit real-time samples, not a
               // texture or a 0-1 modulator value.
               printf("%-22s [%-12s] audio node     OK\n",
                      gn.typeName.c_str(), gn.category.c_str());
               continue;
            }

            IModulator* mod = dynamic_cast<IModulator*>(gn.node.get());
            if (mod == nullptr)
               mod = gn.node->ModulatorOutput(0);
            if (mod != nullptr)
            {
               const float v = mod->Value01();
               const bool ok = v >= 0.0f && v <= 1.0f;
               if (!ok)
                  ++failures;
               printf("%-22s [%-12s] value=%.3f      %s\n",
                      gn.typeName.c_str(), gn.category.c_str(), v, ok ? "OK" : "FAIL");
               continue;
            }

            bool ok = gn.node->GetOutputWidth() > 0 && gn.node->GetOutputTexture() != 0;
            if (!ok)
               ++failures;
            printf("%-22s [%-12s] %dx%d tex=%-3u %s\n",
                   gn.typeName.c_str(), gn.category.c_str(),
                   gn.node->GetOutputWidth(), gn.node->GetOutputHeight(),
                   gn.node->GetOutputTexture(), ok ? "OK" : "FAIL");
         }
         printf("\n%zu node types, %d failures\n", gNodes.size(), failures);
         glfwSetWindowShouldClose(window, GLFW_TRUE);
      }

      if (gOfflineRender.active)
         DrawOfflineRenderProgressWindow();
      DrawArrangeWavRenderProgressWindow();
      DrawArrangeRenderFailNotice();
      if (splashEnabled)
         Splash::Draw();

      int fbW, fbH;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      {
         ConditionalStageTimer timerImGuiRender(benchStagesCpuSample ? &sStageImGuiRender : nullptr, Bench::FrameTail::kImGuiRender);
         Bench::ConditionalGpuStageTimer timerImGuiRenderGpu(benchStagesSample ? &sGpuTimerRing : nullptr, "imgui_render", frameId);
         ImGui::Render();
         glViewport(0, 0, fbW, fbH);
         // Backs every transparent ImGui child/window (ChildBg/WindowBg alpha 0
         // by default, see ApplyTheme) - any panel that skips
         // PushElevatedPanelStyle shows this colour through, so it has to track
         // the theme rather than stay a fixed dark constant or a light theme
         // renders that gap as near-black.
         const CategoryColors::UiTheme& clearTheme = CategoryColors::CurrentUiTheme();
         glClearColor(clearTheme.windowBg.r, clearTheme.windowBg.g, clearTheme.windowBg.b, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      }

      if (const char* shotPath = getenv("IMAGERESYNTH_SCREENSHOT"))
      {
         const int targetFrame = getenv("INFINITE_SCREENSHOT_FRAME") ? atoi(getenv("INFINITE_SCREENSHOT_FRAME")) : 12;
         if (frameId == targetFrame)
         {
            std::vector<unsigned char> px(fbW * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            // Report the real result. This printed "wrote ..." unconditionally,
            // so a failed write announced success - and on macOS it fails
            // easily, because Cocoa chdir's a bundled app to
            // Contents/Resources and a relative shotPath then names a
            // directory that does not exist there. A screenshot harness whose
            // only signal is this line was reading a lie.
            if (stbi_write_png(shotPath, fbW, fbH, 4, px.data(), fbW * 4))
               printf("wrote %s (%dx%d)\n", shotPath, fbW, fbH);
            else
               printf("FAILED to write %s (%dx%d)\n", shotPath, fbW, fbH);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }

      if (Bench::Tail().active)
      {
         PollTailFences();
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkCanvasSwap, Bench::ScopedStageTimer::NowMs());
      }
      {
         ConditionalStageTimer timerSwap(benchStagesCpuSample ? &sStageSwap : nullptr, Bench::FrameTail::kSwap);
         glfwSwapBuffers(window);
      }
      if (Bench::Tail().active)
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkSwapped, Bench::ScopedStageTimer::NowMs());
      gTailCanvasFence.Place(true);
      if (frameId == 0)
         sFirstFrameEndMs = Bench::ScopedStageTimer::NowMs();

      // glfwSwapBuffers blocks on vsync - dead time for AppKit to service a
      // hosted plugin's editor window. See local-prompts/02-plugin-editor-lag.md.
      // Unconditional for the same reason as the call above (Linux
      // factory-context IRunLoop timers with no editor open).
      Platform::PumpPluginEditorEvents();
      if (Bench::Tail().active)
         Bench::Tail().MarkAt(Bench::FrameTail::kMarkPumped, Bench::ScopedStageTimer::NowMs());

      // Projector output: blit this frame's cooked result of each open
      // window's node into that window. Runs after the editor's own swap so
      // it never shows last frame's texture a frame behind the graph.
      // Iterate backwards since a dead source erases its window mid-loop.
      //
      // Windows will demote a HWND_TOPMOST window in real situations
      // (another app going topmost, a UAC prompt, display hot-plug, a
      // resolution change), so a fullscreen projector's topmost state is
      // reasserted on a throttle - cheap and imperceptible at 0.5s, wasteful
      // to do every frame.
      static double sLastTopmostRefresh = 0.0;
      static double sTailProjIntervalMs = -1.0;
      const double now = glfwGetTime();
      const bool refreshTopmost = now - sLastTopmostRefresh >= 0.5;
      // Projectors present in two passes around the frame clock's wait (Block
      // 2 step 3a): every window renders into its back buffer and flushes
      // first, so the GPU works through the blits while the loop waits; only
      // the swaps run after the wait returns, so the presents land as soon
      // after the refresh as possible. A dead source's window is closed in the
      // first pass, so the second only sees windows that rendered.
      // `projectors` (stage timer and Bench::Tail) is both passes; the wait
      // between them is idle time, not work.
      double projWorkMs = 0.0;
      const bool timeProjectors = benchStagesCpuSample || Bench::Tail().active;
      {
         const double projRenderStartMs = timeProjectors ? Bench::ScopedStageTimer::NowMs() : 0.0;
         for (size_t i = gProjectorWindows.size(); i-- > 0; )
         {
            GraphNode* src = FindNodeByIndex(gProjectorWindows[i].nodeIndex);
            if (src == nullptr)
            {
               // Its node was deleted out from under it - close rather than sit
               // there showing a permanently blank window.
               CloseProjectorWindow(i);
               continue;
            }

            GLFWwindow* projWindow = gProjectorWindows[i].window;
#if defined(_WIN32)
            if (gProjectorWindows[i].fullscreen && refreshTopmost)
               Platform::ReassertOutputWindowTopmost(projWindow);
#endif
            glfwMakeContextCurrent(projWindow);
            int pw, ph;
            glfwGetFramebufferSize(projWindow, &pw, &ph);

            // A geometry node's own GetOutputTexture() isn't a real preview (it
            // produces a mesh, not pixels) - render it the same way its
            // mini-viewport/viewport-panel card does instead, sharing that
            // node's own orbit camera so all three agree on framing.
            unsigned int tex = 0;
            int texW = 0, texH = 0;
            if (auto* geo = dynamic_cast<IGeometrySource*>(src->node.get()))
            {
               NodeViewport& viewport = gProjectorViewports[src->index];
               SharedViewportCamera& cam = gNodeCameras[src->index];
               tex = viewport.Render(dynamic_cast<IGeometrySource*>(DisplayNode(src->node.get())), cam, pw, ph);
            }
            else if (INode* shown = DisplayNode(src->node.get()))
            {
               // A bypassed node projects what passes through it, and a bypassed
               // source projects nothing (the clear below), never a frozen frame.
               tex = shown->GetOutputTexture();
               texW = shown->GetOutputWidth();
               texH = shown->GetOutputHeight();
            }

            if (tex != 0)
            {
               if (dynamic_cast<ProjectionNode*>(DisplayNode(src->node.get())) != nullptr)
                  GLUtil::DrawTextureToScreen(tex, pw, ph, 0, 0, /*checkerBg=*/false);
               else
                  GLUtil::DrawTextureToScreen(tex, pw, ph, texW, texH, /*checkerBg=*/true,
                                              /*transparentWindow=*/!gProjectorWindows[i].fullscreen);
            }
            else
            {
               glViewport(0, 0, pw, ph);
               glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
               glClear(GL_COLOR_BUFFER_BIT);
            }
            glFlush();
         }
         if (timeProjectors)
            projWorkMs += Bench::ScopedStageTimer::NowMs() - projRenderStartMs;
      }
      // The frame clock's wait: the primary Output display's refresh with a
      // projector open, else the canvas's when Vsync is on, so the presents
      // below land on its grid whether or not the canvas (or the projector)
      // is covered or hidden.
      PaceProjectorPresent(window);
      {
         const double projSwapStartMs = timeProjectors ? Bench::ScopedStageTimer::NowMs() : 0.0;
         for (size_t i = gProjectorWindows.size(); i-- > 0; )
         {
            GLFWwindow* projWindow = gProjectorWindows[i].window;
            glfwMakeContextCurrent(projWindow);
            // B8: present cost and swap-to-swap interval per window. CPU only -
            // a GL timer query must never span this context switch.
            const double benchB8SwapStartMs = (sBenchB8Sampling && i < sBenchB8Win.size())
                                                 ? Bench::ScopedStageTimer::NowMs() : -1.0;
            glfwSwapBuffers(projWindow);
            if (i == 0 && Bench::Tail().active)
            {
               // Window 0 is the last to present (backwards loop): its fence
               // covers every projector's work this frame.
               static double sTailLastProjSwapMs = -1.0;
               const double swapMs = Bench::ScopedStageTimer::NowMs();
               sTailProjIntervalMs = sTailLastProjSwapMs >= 0.0 ? swapMs - sTailLastProjSwapMs : -1.0;
               sTailLastProjSwapMs = swapMs;
               gTailProjFence.Place(false);
            }
            if (benchB8SwapStartMs >= 0.0)
            {
               const double endMs = Bench::ScopedStageTimer::NowMs();
               BenchB8Window& bw = sBenchB8Win[i];
               bw.presentMs.Push(endMs - benchB8SwapStartMs);
               if (bw.lastSwapMs >= 0.0)
               {
                  const double iv = endMs - bw.lastSwapMs;
                  bw.intervalMs.Push(iv);
                  const double refreshMs = bw.refreshHz > 0 ? 1000.0 / (double)bw.refreshHz : sBenchB8RefreshMs;
                  if (std::fabs(iv - std::max(1.0, std::round(iv / refreshMs)) * refreshMs) <= 1.5)
                     bw.onVsync++;
               }
               bw.lastSwapMs = endMs;
            }
            if (isBenchB3 && frameId >= 32)
            {
               const double nowSwapMs = Bench::ScopedStageTimer::NowMs();
               if (sBenchB3LastProjSwapMs > 0.0)
               {
                  const double intervalMs = nowSwapMs - sBenchB3LastProjSwapMs;
                  sBenchB3ProjIntervalRing.Push(intervalMs);
                  const double expectedIntervalMs = 1000.0 / (sBenchB3MonitorRefreshHz > 0 ? (double)sBenchB3MonitorRefreshHz : 60.0);
                  if (intervalMs > 1.5 * expectedIntervalMs)
                     sBenchB3MissedVsyncCount++;
                  sBenchB3TotalVsyncCount++;
               }
               sBenchB3LastProjSwapMs = nowSwapMs;
            }
         }
         if (timeProjectors)
         {
            projWorkMs += Bench::ScopedStageTimer::NowMs() - projSwapStartMs;
            if (benchStagesCpuSample)
               sStageProjectors.Push(projWorkMs);
            Bench::Tail().AddStage(Bench::FrameTail::kProjectors, projWorkMs);
         }
         if (refreshTopmost && !gProjectorWindows.empty())
            sLastTopmostRefresh = now;
         if (!gProjectorWindows.empty())
            glfwMakeContextCurrent(window);
      }
      // The projectors timer above has closed, so this frame's record is complete.
      if (sTailProjIntervalMs > 0.0)
         Bench::Tail().EndFrame(sTailProjIntervalMs);
      sTailProjIntervalMs = -1.0;

      GLUtil::EndFrameScratchFbos();
      ++frameId;
      // The uid map is rebuilt at most once a frame on first use (WP5b), so a
      // gNodes mutation that did not report in is stale for one frame at most
      // - and FindNodeByUid's own storage/size check catches most of those.
      InvalidateNodeByUid();

      // Patch shortcuts. Handled outside the node editor so its own Cmd/Ctrl-key
      // bindings do not swallow them, and gated on no text field having focus
      // so typing an 'S' into a Text node does not save the patch. KeySuper
      // alone would miss Windows, where Cmd doesn't exist and Ctrl reports as
      // KeyCtrl, not KeySuper (that's the Windows key there).
      if (!ImGui::GetIO().WantTextInput && (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl))
      {
         if (ImGui::IsKeyPressed(ImGuiKey_0, false))
            gSettingsOpen = true;
         else if (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_Minus, true))
         {
            // UI scale in 0.1 steps, like a browser's zoom. Applied through the same rescale path as the Settings slider.
            const bool up = ImGui::IsKeyPressed(ImGuiKey_Equal, true);
            const float next = std::clamp(std::round((CategoryColors::GetUiScale() + (up ? 0.1f : -0.1f)) * 10.0f) / 10.0f, 0.5f, 2.0f);
            if (next != CategoryColors::GetUiScale())
            {
               CategoryColors::SetUiScale(next);
               UiScale::RequestRescale();
            }
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            SavePatchInteractive(ImGui::GetIO().KeyShift);
         else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
         {
            const std::string path = Platform::OpenPatchDialog();
            if (!path.empty())
               GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            GuardUnsavedChanges([]() { NewPatch(); });
      }

      // Frame limiter. Sleeping most of the way there and spinning the last
      // sliver keeps the cap accurate without burning a core: sleep_for is only
      // accurate to a millisecond or two, which at 120fps is most of the budget.
      // Skipped entirely when Vsync is on: glfwSwapBuffers above already paced
      // this frame to the display's refresh, and topping that up against a
      // second, unrelated budget just fights the vsync quantization instead of
      // capping anything more precisely (the Target FPS control is disabled in
      // the UI whenever Vsync is on, for the same reason). Skipped too while a
      // projector window paces the loop to its display's refresh.
      if (gTargetFps > 0 && !gVsync && !FrameClockActive())
      {
         const double budget = 1.0 / (double)gTargetFps;
         const double deadline = gFrameStart + budget;
         if (Platform::AnyPluginEditorOpen())
         {
            // Spend the idle slack servicing the editor's run loop instead of
            // sleeping through it - see local-prompts/02-plugin-editor-lag.md.
            while (glfwGetTime() < deadline)
               if (!Platform::PumpPluginEditorEvents())
                  std::this_thread::sleep_for(std::chrono::microseconds(200));
         }
         else
         {
            const double slack = deadline - glfwGetTime();
            if (slack > 0.002)
               std::this_thread::sleep_for(std::chrono::duration<double>(slack - 0.001));
            while (glfwGetTime() < deadline)
               std::this_thread::yield();
         }
      }

      {
         // Measured after the swap so the number includes GPU work the driver
         // blocks on there, which is where a heavy 3D render actually lands.
         static double sPrevTime = 0.0;
         const double now = glfwGetTime();
         if (sPrevTime > 0.0)
            gLastFrameMs = (now - sPrevTime) * 1000.0;
         sPrevTime = now;
      }

      // Dev harness: quit after N frames. The self-tests above printf their
      // verdict and then run forever, so a scripted run has to kill the app -
      // which throws away stdout still sitting in the block buffer when it is
      // redirected to a file. Exiting normally lets it flush.
      if (const char* exitAfter = getenv("INFINITE_EXITAFTER"))
      {
         if (frameId >= atoi(exitAfter))
         {
            // Drag-test fixtures print their verdict from inside their own
            // phase machine; if the run times out before ever finding a
            // usable row (sPhase stuck at 0), that machine never gets to
            // print anything and the driver silently reads "no FAIL" as a
            // pass. Say so explicitly instead.
            if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr && gSamplerDragTestPhase == 0)
               printf("SAMPLERDRAGTEST FAIL (timed out waiting for a usable row)\n");
            if (getenv("INFINITE_MEDIADRAGTEST") != nullptr && gMediaDragTestPhase == 0)
               printf("MEDIADRAGTEST FAIL (timed out waiting for a usable row)\n");
            if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr && gPluginDragTestPhase == 0)
            {
               // An empty plugin index isn't a failure of the drag
               // mechanism - it just means this host has nothing installed
               // to drag.
               if (gPluginScanner.Index().empty())
                  printf("PLUGINDRAGTEST SKIP (no plugins installed on this host)\n");
               else
                  printf("PLUGINDRAGTEST FAIL (timed out waiting for a usable row)\n");
            }
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
   }

   // Every real close path (Quit menu, red button, Cmd+Q) routes through
   // RequestClose -> glfwSetWindowShouldClose, and every dev-harness exit
   // sets the same flag directly - so this fall-through is the single place
   // every one of them converges, and the only place a clean exit needs to
   // delete the marker. Same INFINITE_EXITAFTER carve-out as the startup
   // check: a harness run never created the real marker, so it must not
   // delete it either - see UsingAutosaveTestPaths.
   if (getenv("INFINITE_EXITAFTER") == nullptr && !HeadlessJobActive())
   {
      const std::string marker = AutosaveMarkerPath();
      if (!marker.empty())
      {
         std::error_code ec;
         std::filesystem::remove(marker, ec);
      }
   }

   CloseAllProjectorWindows();
   Platform::StopDisplayRefreshClock();
   AudioEngine::Instance().Stop();
   UpdateCheck::Shutdown(); // joins the worker thread so the process doesn't exit mid-request
   MovementLog::Stop();
   if (ColorStats::Engine::Instance().HasLearnedData())
      ColorStats::Engine::Instance().Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveQuantizeProfile::HasLearnedData())
      PredictiveQuantizeProfile::Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveVelocityProfile::HasLearnedData())
      PredictiveVelocityProfile::Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveNotesStyle::HasLearnedData())
      PredictiveNotesStyle::Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveRhythmStyle::HasLearnedData())
      PredictiveRhythmStyle::Save(AppPaths::AppSupportDir() + "/prediction");
   gNodes.clear();
   if (getenv("INFINITE_RECTEARDOWNTEST") != nullptr && std::string(getenv("INFINITE_RECTEARDOWNTEST")) == "quit")
      printf("quit-mid-record: survived  OK\n");
   ed::DestroyEditor(gEditor);
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
   glfwDestroyWindow(window);
   glfwTerminate();

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
   //
   // _Exit also skips the normal atexit-driven stdio flush, unlike exit()/
   // return from main. The INFINITE_EXITAFTER harness path already knew this
   // and fflush(stdout)'d itself before requesting the close - but any
   // self-test that calls glfwSetWindowShouldClose() directly on its own
   // verdict (most of them do, e.g. INFINITE_MINIVIEWPORTTEST) reaches here
   // without ever going through that branch, so its buffered printf verdict
   // silently vanished whenever stdout was redirected to a file (i.e. every
   // driver.sh run). One flush here covers every self-test's exit path
   // instead of requiring each one to remember its own.
   fflush(stdout);
   std::_Exit(gHeadlessExitCode);
}

// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/Glyphs.gen.h"
#include "app/frame/FrameCtx.h"

namespace app
{
// Resolves the main window's DPI + the manual slider into UiScale::Current(), hands the point
// scale to the GLFW backend and (re)bakes the font atlas at the resulting physical size. Safe
// to call again between frames: the atlas is rebuilt from scratch and, when the GL renderer
// already exists, its font texture is recreated. Style metrics are never rescaled here -
// they are in points like everything else, so there is nothing to compound.
void ApplyUiScale(GLFWwindow* window, bool rendererReady)
{
   // A queued language switch lands here, between frames, so no label pointer fetched earlier in
   // a frame can outlive its table; the bake below then sees the new language's glyph needs.
   I18n::ApplyPending();
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
   // CJK: Inter has no Han/kana, so a subsetted Noto face is merged for exactly the glyphs the
   // active language table needs. Every language also gets the picker's native names (日本語,
   // 中文简体) so the Language list never shows '?'. Noto SC for zh and for names, JP for ja:
   // the two draw the same Han code points with different shapes. Absent file => '?' only.
   if (uiFont != nullptr)
   {
      const bool ja = I18n::CurrentLanguage() == "ja";
      const bool cjk = I18n::CurrentLanguageNeedsCjk();
      const std::string notoPath =
         BundledResourcePath(ja ? "fonts/NotoSansJP-Subset.otf" : "fonts/NotoSansSC-Subset.otf");
      if (!notoPath.empty())
      {
         static ImVector<ImWchar> notoRanges; // must outlive the atlas build
         ImFontGlyphRangesBuilder builder;
         const std::vector<uint32_t> glyphs = cjk ? I18n::GlyphsForCurrentLanguage() : I18n::NativeNameGlyphs();
         for (uint32_t cp : glyphs)
            if (cp >= 0x2E00 && cp <= 0xFFFF)
               builder.AddChar(static_cast<ImWchar>(cp));
         if (cjk)
         {
            for (uint32_t cp = 0x3000; cp <= 0x30FF; cp++) // CJK punctuation, hiragana, katakana
               builder.AddChar(static_cast<ImWchar>(cp));
            for (uint32_t cp = 0xFF00; cp <= 0xFFEF; cp++) // fullwidth forms
               builder.AddChar(static_cast<ImWchar>(cp));
         }
         notoRanges.clear();
         builder.BuildRanges(&notoRanges);
         ImFontConfig cjkCfg;
         cjkCfg.MergeMode = true;
         io.Fonts->AddFontFromFileTTF(notoPath.c_str(), bakedPx, &cjkCfg, notoRanges.Data);
      }
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
   // Infinite Glyphs (art/icons/src -> tools/design/build_glyphs.py): our own icon set, merged the
   // same way as Lucide above. Codepoints come from the generated Glyphs.gen.h.
   if (uiFont != nullptr)
   {
      const std::string bundledGlyphs = BundledResourcePath("icons/infinite-glyphs.ttf");
      if (!bundledGlyphs.empty())
      {
         static const ImWchar glyphRanges[] = { (ImWchar)IconsInfinite::kFirst, (ImWchar)IconsInfinite::kLast, 0 };
         ImFontConfig glyphCfg;
         glyphCfg.MergeMode = true;
         glyphCfg.PixelSnapH = true;
         glyphCfg.GlyphMinAdvanceX = bakedPx;
         io.Fonts->AddFontFromFileTTF(bundledGlyphs.c_str(), bakedPx, &glyphCfg, glyphRanges);
      }
   }
   if (rendererReady)
      ImGui_ImplOpenGL3_CreateFontsTexture();
}

int InitApp(FrameCtx& fc, int argc, char** argv)
{
   auto& sMainRssStartMb = fc.sMainRssStartMb;
   auto& sMainFootStartMb = fc.sMainFootStartMb;
   auto& tPreWindow = fc.tPreWindow;
   auto& window = fc.window;
   auto& tWindowGl = fc.tWindowGl;
   auto& tImGuiFonts = fc.tImGuiFonts;
   auto& tScanners = fc.tScanners;
   auto& allTypes = fc.allTypes;
   auto& selfTest = fc.selfTest;
   auto& searchBuf = fc.searchBuf;
   auto& searchJustOpened = fc.searchJustOpened;
   auto& searchPopupCentered = fc.searchPopupCentered;
   auto& searchPopupOpen = fc.searchPopupOpen;
   auto& searchRequestClose = fc.searchRequestClose;
   auto& clipboard = fc.clipboard;
   auto& clipboardSources = fc.clipboardSources;
   auto& clipboardOrigIndex = fc.clipboardOrigIndex;
   auto& clipboardOrigGroup = fc.clipboardOrigGroup;
   auto& clipboardCluster = fc.clipboardCluster;
   auto& tSettingsInit = fc.tSettingsInit;
   auto& sFirstFrameEndMs = fc.sFirstFrameEndMs;
   auto& frameId = fc.frameId;
   auto& splashEnabled = fc.splashEnabled;

   // Where the Drum Sequencer finds its bundled kit (Resources/drumkits/
   // infinite-basic); empty when the folder is absent, which the node treats as
   // "no kit". Set first so headless jobs that load patches see it too.
   DrumSequencerNode::SetKitDir(BundledResourcePath("drumkits/infinite-basic"));
   const double sMainStartMs = Bench::ScopedStageTimer::NowMs();
   sMainRssStartMb = Bench::ProcessRssMb();
   sMainFootStartMb = Bench::ProcessFootprintMb();
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   static int sBenchB3MatIdx = -1;
   static int sBenchB3CamIdx = -1;
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   // B7 soak (the B3 fixture, run on wall-clock time): one sample per 10 s
   // window, verdicts at the end (benchmark-suite.md §6).
   
   
   
   
   

   // B8 Media I/O fixture state (docs/plans/perf/benchmark-suite.md §4)
   
   
   
   static int sBenchB8Clips = 2;
   static int sBenchB8Res = 1080;
   
   
   
    // non-empty = "camera":"skipped"
   
   
   
   
   
   
   
   
   
   
   
   
   
   // Per projector window, indexed like gProjectorWindows (none close mid-run).
   
   
    // INFINITE_BENCH_B8OVERLAP=1: leave projectors on top of the canvas
   

   // B6 Canvas navigation fixture state (docs/plans/perf/benchmark-suite.md §4)
   
   
   static int sBenchB6NodeCount = 300;
   
   static bool sBenchB6Collapsed = false;
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   
   

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

   if (getenv("INFINITE_SPATIALTEST") != nullptr)
      return RunSpatialFixture() ? 0 : 1;

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

   if (getenv("INFINITE_I18NTEST") != nullptr)
   {
      RunI18nTest();
      return 0; // verdict is the printf line, not $?
   }

   if (getenv("INFINITE_AUDIOPCMTEST") != nullptr)
      return Platform::AudioPcmConversionSelfTest() ? 0 : 1;

   if (getenv("INFINITE_NDITEST") != nullptr)
      return RunNdiTest();
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

   tPreWindow = Bench::ScopedStageTimer::NowMs();
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
   // The launcher card plays in its own window first (core/LauncherCard.h); the main window stays
   // hidden until it ends.
   // INFINITE_LAUNCHERCARDTEST forces it on under the harness, with its sentinel in a temp dir.
   const bool launcherCardTest = getenv("INFINITE_LAUNCHERCARDTEST") != nullptr;
   const std::string launcherCardDir =
      launcherCardTest ? AppPaths::TempDir() + "/infinite-launchercard-test" : AppPaths::AppSupportDir();
   if (launcherCardTest)
      AppPaths::EnsureDir(launcherCardDir);
   LauncherCard::Enabled() = launcherCardTest || (!gHeadlessTestWindow && !IsHeadlessProcess() &&
                                                  getenv("INFINITE_NOSPLASH") == nullptr &&
                                                  getenv("INFINITE_SPLASHTEST") == nullptr);
   if (LauncherCard::Enabled() && !launcherCardTest && LauncherCard::ConsumeStaleSentinel(launcherCardDir))
   {
      Platform::AppendLogLine("[startup] launcher card skipped once: the previous start did not get past it");
      LauncherCard::Enabled() = false;
   }
   // What later windows (projectors) must inherit once the card is done with the sticky hints.
   LauncherCard::WindowHints mainWindowHints;
   mainWindowHints.visible = gHeadlessTestWindow ? GLFW_FALSE : GLFW_TRUE;
   mainWindowHints.scaleToMonitor = gHeadlessTestWindow ? GLFW_FALSE : GLFW_TRUE;
   if (LauncherCard::Enabled())
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
   window = glfwCreateWindow(1600, 1000, "Infinite", nullptr, nullptr);
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

   // The launcher card plays here, before ImGui::CreateContext() and ImGui_ImplGlfw_InitForOpenGL:
   // with no GLFW backend installed on the main window there are no ImGui callbacks or Windows
   // WndProc hook for its events to reach while the card's private context is current. Played
   // after them, it crashed v0.4.7 on Windows (see core/LauncherCard.h). The sentinel lets the
   // next start skip the card once if this one dies inside it.
   bool launcherCardPlayed = false;
   if (LauncherCard::Enabled())
   {
      int injected = 0;
      std::function<void()> inject;
      if (launcherCardTest)
      {
         // Stale-sentinel contract first: a leftover sentinel is reported once, then gone.
         LauncherCard::MarkRunning(launcherCardDir);
         const bool staleSeen = LauncherCard::ConsumeStaleSentinel(launcherCardDir);
         const bool staleGone = !LauncherCard::ConsumeStaleSentinel(launcherCardDir);
         printf("LAUNCHER CARD SENTINEL TEST: stale-seen=%d consumed=%d %s\n", (int)staleSeen, (int)staleGone,
                staleSeen && staleGone ? "OK" : "FAIL");
         inject = [&]() { injected += LauncherCard::InjectMainWindowEvents(window); };
      }
      ImGuiContext* ctxBefore = ImGui::GetCurrentContext();
      LauncherCard::MarkRunning(launcherCardDir);
      // The test starts the card near its end so it finishes in about half a second.
      launcherCardPlayed = LauncherCard::Run(window, BundledResourcePath("fonts/Inter-Regular.ttf"), mainWindowHints,
                                             launcherCardTest ? Splash::kMinShow - 0.1f : 0.0f, inject);
      LauncherCard::ClearRunning(launcherCardDir);
      if (launcherCardTest)
      {
         // Pass = no crash while whatever main-window callbacks exist fired every card frame,
         // the card played, the ImGui context is back, and the sentinel is cleared.
         const bool ok = launcherCardPlayed && ImGui::GetCurrentContext() == ctxBefore &&
                         !LauncherCard::ConsumeStaleSentinel(launcherCardDir);
         printf("LAUNCHER CARD TEST: played=%d callbacks-hit=%d %s\n", (int)launcherCardPlayed, injected,
                ok ? "OK" : "FAIL");
         fflush(stdout);
         glfwDestroyWindow(window);
         glfwTerminate();
         return ok ? 0 : 1;
      }
   }

   tWindowGl = Bench::ScopedStageTimer::NowMs();
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::StyleColorsDark();

   // Loaded here (rather than down with the other Load*Settings() calls)
   // because the font/DPI block right below needs gUiScale before it bakes
   // the font atlas - loading it after the atlas already exists is too late.
   CategoryColors::LoadPreference();

   // Interface language: an explicit choice wins, otherwise the first OS preference that is one of
   // our six, otherwise English. The first ApplyUiScale() below applies it before the font bake.
   I18n::SetResourceDir(BundledResourcePath("lang"));
   {
      std::string lang = CategoryColors::GetLanguage();
      if (!I18n::IsSupported(lang))
         lang = I18n::MatchSupported(Platform::PreferredLanguages());
      // Test hook: run any self-test under a chosen language (locale-leak checks use "de").
      if (const char* forced = getenv("INFINITE_LANG"); forced && I18n::IsSupported(forced))
         lang = forced;
      I18n::RequestLanguage(lang);
   }

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
   tImGuiFonts = Bench::ScopedStageTimer::NowMs();

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
   tScanners = Bench::ScopedStageTimer::NowMs();

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
    // (name, category)
   for (const std::string& category : NodeFactory::Instance().GetCategories())
   {
      for (const std::string& name : NodeFactory::Instance().GetNodesInCategory(category))
         if (IsUserSpawnable(name))
            allTypes.emplace_back(name, category);
   }

   selfTest = getenv("IMAGERESYNTH_SELFTEST") != nullptr;
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

   std::memset(searchBuf, 0, sizeof(searchBuf));
   searchJustOpened = false;
   searchPopupCentered = false;
   // Shift+N is a toggle, so the keyboard block (which runs before the popup is
   // drawn) needs to know whether the picker is already up. ImGui::IsPopupOpen
   // can't be trusted from there - the other OpenPopup("search") call sites sit
   // inside ed::BeginCreate()/Suspend() scopes with their own ID stacks - so the
   // popup reports its own visibility here instead, one frame behind, and the
   // close is deferred to inside the popup where CloseCurrentPopup() is legal.
   searchPopupOpen = false;
   searchRequestClose = false;
         // typeNames copied
       // live sources to copy params from
        // gNodes index each item had at copy time
        // that item's owning group's index, or -1
   // Connections landing on the copied cluster, captured at Cmd+C time since
   // the graph can change before Cmd+V runs (see ApplyClusterLinks).
   
   tSettingsInit = Bench::ScopedStageTimer::NowMs();
   sFirstFrameEndMs = 0.0;
   frameId = 0;

   // Launcher screen over the first seconds (core/SplashScreen.h). Never in headless/test runs.
   // INFINITE_SPLASHTEST=<seconds> forces it on under the screenshot harness, starting that far in.
   const char* splashTest = getenv("INFINITE_SPLASHTEST");
   // Launcher card on every normal start (core/SplashScreen.h). INFINITE_NOSPLASH=1 suppresses it.
   splashEnabled = splashTest != nullptr || (!gHeadlessTestWindow && !IsHeadlessProcess() && getenv("INFINITE_NOSPLASH") == nullptr);
   if (LauncherCard::Enabled())
   {
      glfwShowWindow(window);
      glfwFocusWindow(window);
      if (launcherCardPlayed)
         splashEnabled = false; // already played, in its own window
      else if (splashEnabled)
         Splash::Begin(0.0f); // the card window failed: fall back to the in-window splash
   }
   else if (splashEnabled)
      Splash::Begin(splashTest ? (float)atof(splashTest) : 0.0f);   return -1;
}
}

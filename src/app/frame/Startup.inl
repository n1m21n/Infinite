// Part of src/main.cpp, included there in order (one translation unit).
// startup: arguments, window, audio, editor, first patch

   // Turbo 0.45: `--mcp` = the MCP bridge for Claude & co. (stdio), no window.
   for (int i = 1; i < argc; i++)
      if (std::strcmp(argv[i], "--mcp") == 0)
         return RunMcpBridge();
      else if (std::strcmp(argv[i], "--mcp-install") == 0)
         return InstallMcpConfig();
      else if (std::strcmp(argv[i], "--mcp-http") == 0)
         // Turbo 0.59: Streamable HTTP MCP for remote clients (ChatGPT), see McpShare.h.
         return RunMcpHttp(i + 1 < argc ? std::atoi(argv[i + 1]) : 7780, i + 2 < argc ? std::string(argv[i + 2]) : std::string("local"));
   // Turbo 0.46 (upstream R500): one Infinite-Turbo at a time for opening
   // patches. A second launch with a .inf (double-click in Explorer) hands
   // the file to the running one and exits; without a file it opens a second
   // window as before.
   if (argc > 1 && argv[1] != nullptr && argv[1][0] != '-')
   {
      std::string argPath = Platform::CommandLineArgUtf8(1); // UTF-8, unlike argv
      if (argPath.empty())
         argPath = argv[1];
      const bool isPatch = argPath.size() > 4 &&
                           (_stricmp(argPath.c_str() + argPath.size() - 4, ".inf") == 0 ||
                            (argPath.size() > 9 && _stricmp(argPath.c_str() + argPath.size() - 9, ".infinite") == 0));
      if (isPatch && !Platform::BecomePrimaryInstance() && Platform::ForwardOpenToRunningInstance(argPath))
         return 0;
   }
   if (argc <= 1 || (argv[1] != nullptr && argv[1][0] != '-'))
      Platform::BecomePrimaryInstance();
   std::setvbuf(stdout, nullptr, _IONBF, 0);
   std::setvbuf(stderr, nullptr, _IONBF, 0);
   StartupTrace("startup: main entered");
   // Turbo: leave a minidump behind instead of a window that just vanishes.
   Platform::InstallCrashHandler();
   if (getenv("INFINITE_CRASHTEST") != nullptr)
      SysInfo::CrashTest();
   StartupTrace("startup: initializing JUCE");
   Platform::EnsureJuceInitialised();
   StartupTrace("startup: JUCE initialized");

   if (getenv("INFINITE_AUDIOPARAMSWEEPTEST") != nullptr)
   {
      // Needs the registry populated (DiscoverAudioSweepCandidates walks
      // NodeFactory), unlike DSPTEST above which constructs its DspTest::*
      // fixture classes directly - RegisterNodes() itself touches no GL, so
      // it is safe to call this early, before glfwInit().
      RegisterNodes();
      RunAudioParamSweepTest();
      RunFmModeDebugCheck();
      // Always 0, never the sweep's own pass/fail - the printf verdict line
      // is the only signal every other INFINITE_* fixture uses (see
      // run-infinite-hygiene/SKILL.md's "verdict lines aren't exit codes"
      // Gotcha); driver.sh greps for "AUDIO PARAM SWEEP FAIL", it doesn't
      // read $?. A real nonzero exit here would misreport a normal [FAIL] as
      // a [CRASH] in that harness's generic per-check loop.
      return HeadlessExit(0);
   }

   if (getenv("INFINITE_PLUGINSCANTEST") != nullptr)
      return HeadlessExit(RunPluginScanTest());

   if (getenv("INFINITE_DSPTEST") != nullptr)
      return HeadlessExit(RunDspTest());

   // Turbo 0.60: upstream's per-voice modulation fixture. Exit 0 always - the
   // printed VOICEMODTEST verdict is the driver's signal, as for AUDIOPDCTEST.
   if (getenv("INFINITE_VOICEMODTEST") != nullptr)
   {
      RunVoiceModFixture();
      return HeadlessExit(0);
   }

   if (getenv("INFINITE_AUDIOPDCTEST") != nullptr)
   {
      RunAudioPdcTest();
      // Always 0, same reasoning as AUDIOPARAMSWEEPTEST above: the printf
      // verdict line is driver.sh's only signal, not $? - a nonzero exit
      // here would misreport a normal [FAIL] as [CRASH].
      return HeadlessExit(0);
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
      Platform::SuppressAppUIForScanChild();

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
      return HeadlessExit(0);
   }

   StartupTrace("startup: initializing GLFW");
   Platform::InitDocumentHandlingPreGlfw();
   if (!glfwInit())
   {
      fprintf(stderr, "glfwInit failed\n");
      return 1;
   }
   Platform::InitDocumentHandlingPostGlfw();
   StartupTrace("startup: GLFW initialized");

   // Turbo: ask for the newest core profile the driver offers (4.6 on any
   // current NVIDIA/AMD/Intel driver) and fall back to the original 3.2. All
   // shaders stay #version 150, which every core profile accepts; the newer
   // context unlocks compute shaders, DSA, texture storage and KHR_debug for
   // future work. Output windows reuse the same version (shared context).
   GLFWwindow* window = nullptr;
   {
      const int kVersions[][2] = { {4, 6}, {4, 5}, {4, 3}, {3, 3}, {3, 2} };
      for (const auto& v : kVersions)
      {
         glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, v[0]);
         glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, v[1]);
         glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
         glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
         window = glfwCreateWindow(1600, 1000, "Untitled - " INFINITE_TURBO_TITLE, nullptr, nullptr);
         if (window != nullptr)
         {
            gGLContextMajor = v[0];
            gGLContextMinor = v[1];
            break;
         }
      }
   }
   if (!window)
   {
      fprintf(stderr, "glfwCreateWindow failed\n");
      glfwTerminate();
      return 1;
   }
   StartupTrace("startup: window created");
   gMainWindow = window;
   Platform::SetFileDialogOwner(window);

   glfwMakeContextCurrent(window);
   glewExperimental = GL_TRUE;
   const GLenum glewResult = glewInit();
   glGetError(); // GLEW may leave a benign GL_INVALID_ENUM on a core profile.
   if (glewResult != GLEW_OK)
   {
      fprintf(stderr, "GLEW initialization failed: %s\n", glewGetErrorString(glewResult));
      glfwDestroyWindow(window);
      glfwTerminate();
      return 1;
   }
   StartupTrace("startup: OpenGL initialized");
   if (getenv("INFINITE_SYSINFO") != nullptr)
      SysInfo::PrintAndExit(window, INFINITE_TURBO_VERSION);
   RuntimeLog::Write("OpenGL %d.%d core context: %s | %s", gGLContextMajor, gGLContextMinor,
                     (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
   glfwSwapInterval(1);
   Platform::PreventAppNap();
   // Turbo 0.34: MMCSS boost of the render thread removed - an MMCSS "Games"
   // thread at high priority can outrank JUCE's audio thread and cause
   // buffer underruns. Platform::BoostRenderThread() is kept but unused.

   // Covers both the red close button and Cmd+Q: GLFW's Cocoa backend routes
   // applicationShouldTerminate: through the same _glfwInputWindowCloseRequest
   // as the window's own close button, so one callback gates both.
   glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { RequestClose(w); });

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::StyleColorsDark();
   StartupTrace("startup: ImGui context created");

   // IBM Plex Sans is shipped with the application so typography is identical
   // on every Windows machine. System faces remain only as an emergency
   // fallback if the package was copied incompletely.
   // Turbo 0.46 (upstream R527): the UI follows the monitor's scale. ImGui is
   // laid out in points (the GLFW backend divides the window by the point
   // scale), the font is baked at its final pixel size and brought back to 15
   // units, so text and nodes grow together and stay sharp. See ApplyUiScale.
   ApplyUiScale(window);
   ImGuiStyle& style = ImGui::GetStyle();
   style.FrameRounding = 3.0f;
   style.GrabRounding = 3.0f;
   style.WindowRounding = 4.0f;
   style.ItemSpacing = ImVec2(6, 5);

   ImGui_ImplGlfw_InitForOpenGL(window, true);
   // Turbo 0.46: a monitor with another scale (or a Windows scale change).
   glfwSetWindowContentScaleCallback(window, [](GLFWwindow*, float, float) { gUiRescaleRequested = true; });
   // Installed after the backend so it chains rather than replacing ImGui's.
   glfwSetDropCallback(window, OnFilesDropped);
   ImGui_ImplOpenGL3_Init("#version 150");
   StartupTrace("startup: ImGui backends initialized");

   // Cocoa chdir's a bundled app into Contents/Resources, so anything written
   // with a relative path lands inside the .app - which breaks its code
   // signature on first launch and can get the app reported as damaged. Keep
   // all mutable state in Application Support instead.
   std::string settingsDir = InfiniteSettingsDirectory();
   PredictiveQuantizeProfile::Load(PredictionDir());
   PredictiveVelocityProfile::Load(PredictionDir());
   PredictiveNotesStyle::Load(PredictionDir());
   PredictiveRhythmStyle::Load(PredictionDir());
   // Restores whatever was indexed last run with no rescan - scanning only
   // ever happens from an explicit Refresh click in the Samples/Media panel.
   gSampleScanner.LoadFromDisk();
   gMediaScanner.LoadFromDisk();
   // Turbo 0.49: the bundled kits (assets/drumkits) join the Samples
   // library once; removing them from the panel is remembered. Adding them
   // starts a background scan.
   if (const std::string kit = TurboDrumKitDir(); !kit.empty())
   {
      const std::filesystem::path kitsRoot = std::filesystem::u8path(kit).parent_path();
      gSampleScanner.SeedDefaultFolders({ { kitsRoot.u8string(), "Turbo kits" } });
      // Turbo 0.54: the CC0 pack landed inside that root (one rescan), and the
      // full build adds SLOOP's 37 kits rendered to samples.
      gSampleScanner.SeedFolderOnce("cc0-pack-0.54", kitsRoot.u8string(), "Turbo kits");
      std::error_code gplEc;
      const std::filesystem::path gplKits = kitsRoot.parent_path() / "drumkits-gpl";
      if (std::filesystem::is_directory(gplKits, gplEc))
         gSampleScanner.SeedFolderOnce("sloop-kits-0.54", gplKits.u8string(), "SLOOP kits");
   }
   SeedBundledDrumPatterns(); // Turbo 0.54
   SeedBundledNotePatterns(); // Turbo 0.57
   // Never a scan at launch, for the same reason the two above aren't:
   // the cached index is shown instantly and rebuilding it is the user's
   // explicit Rescan.
   gPluginScanner.LoadFromDisk();
   StartupTrace("startup: scanner caches loaded");

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
   CategoryColors::LoadPreference();

   gEditor = ed::CreateEditor(&config);
   ed::SetCurrentEditor(gEditor); // ed::GetStyle() below needs a current editor
   StartupTrace("startup: node editor created");

   // Restore the last machine-local setup before the first frame. Patch loads
   // can replace it later with their own saved scene settings.
   {
      Patch::SceneSettings savedSettings;
      if (Patch::LoadAppSettings(savedSettings))
         ApplySceneSettings(savedSettings, false);
   }

   RuntimeLog::Initialize(gDiagnosticLogEnabled);
   {
      const std::filesystem::path marker = std::filesystem::u8path(SessionMarkerPath());
      const std::filesystem::path autosave = std::filesystem::u8path(RecoveryAutosavePath());
      gRecoveryAvailable = gAutosaveEnabled && std::filesystem::exists(marker) && std::filesystem::exists(autosave);
      if (!gAutosaveEnabled)
      {
         std::error_code ec;
         std::filesystem::remove(autosave, ec);
      }
      std::ofstream sessionMarker(SessionMarkerPath(), std::ios::trunc);
      sessionMarker << "active\n";
      RuntimeLog::Write("startup complete; prior recovery=%s; edition=%s", gRecoveryAvailable ? "available" : "none",
                        INFINITE_GPL ? "full (GPL modules)" : "MIT");
   }

   RegisterNodes();
   StartupTrace("startup: nodes registered");
   ApplyTheme();
   StartupTrace("startup: theme applied");

   // Embedded local control server (see docs/plans - RemoteControl) - lets an
   // external tool (the Infinite MCP server) drive this running instance.
   // Loopback-only; port overridable for running more than one instance.
   {
      int controlPort = 7777;
      if (const char* portEnv = getenv("INFINITE_CONTROL_PORT"))
         controlPort = atoi(portEnv);
      RemoteControl::Start(controlPort);
      // Turbo 0.46: one release check per launch (Settings turns it off).
      if (gUpdateCheckEnabled)
         TurboUpdate::Start();
   }

   // Turbo: opt-in auto-start (Settings > Audio > "Start audio when Infinite
   // opens"). Started from the main loop once the first frames are up, the
   // same StartAudioEngine path the toolbar button uses. Never in test runs.
   gAudioAutoStartPending = gAudioAutoStart && getenv("INFINITE_EXITAFTER") == nullptr;

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
         getenv("INFINITE_RESYNTHTEST") != nullptr ||
         getenv("INFINITE_HIDETEST") != nullptr ||
         getenv("INFINITE_MACROTEST") != nullptr ||
         getenv("INFINITE_RECTEST") != nullptr || getenv("INFINITE_MODTEST") != nullptr ||
         getenv("INFINITE_SIZETEST") != nullptr || getenv("INFINITE_INPUTTEST") != nullptr ||
         getenv("INFINITE_DRAGTEST") != nullptr || getenv("INFINITE_COLORTEST") != nullptr ||
         getenv("INFINITE_PICKERTEST") != nullptr || getenv("INFINITE_OSCTEST") != nullptr;

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
         SpawnNode("MIDI Notes", "Notes", 20.0f, 20.0f);            // 0
         SpawnNode("Wavetable", "Synths", 20.0f, 440.0f);           // 1
         SpawnNode("Envelope", "Modulators", 540.0f, 20.0f);        // 2
         SpawnNode("Gain", "AudioUtility", 1040.0f, 440.0f);        // 3
         SpawnNode("Mixer", "AudioUtility", 1040.0f, 760.0f);       // 4
         SpawnNode("Splitter", "AudioUtility", 1300.0f, 440.0f);    // 5
         SpawnNode("Audio Out", "AudioUtility", 1300.0f, 580.0f);   // 6
         SpawnNode("Audio Filter", "AudioEffects", 1650.0f, 20.0f); // 7
         SpawnNode("Dynamics", "AudioEffects", 1650.0f, 780.0f);    // 8
         SpawnNode("Delay", "AudioEffects", 2160.0f, 20.0f);        // 9
         SpawnNode("Reverb", "AudioEffects", 2160.0f, 780.0f);      // 10
         SpawnNode("Stutter", "AudioEffects", 2470.0f, 20.0f);      // 11
         SpawnNode("Arpeggiator", "Notes", 2470.0f, 500.0f);        // 12
         SpawnNode("Scale Notes", "Notes", 2470.0f, 900.0f);        // 13
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
         SpawnNode("Equation Synth", "Synths", 5400.0f, 20.0f);         // 30
         {
            // A tiny synthetic WAV, loaded immediately, so the visual smoke
            // test's screenshot shows the interactive waveform (bars, the
            // start/end handles, the loop-range dimming) rather than the
            // empty "no sample loaded" placeholder every other run of this
            // fixture would otherwise show forever.
            const std::string fixtureWav = "/tmp/infinite_audiouitest_sampler.wav";
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
            if (auto* samplerFixture = dynamic_cast<SamplerNode*>(gNodes[24].node.get()))
               samplerFixture->LoadFile(fixtureWav);
         }
         {
            // Same idea, for the Drum Sequencer's lane 0/1: a loaded sample
            // and an armed step or two so the fixture's screenshot shows the
            // grid's velocity fills and the strip's real filename instead of
            // the empty "no sample loaded" placeholder forever.
            const std::string drumWav = "/tmp/infinite_audiouitest_drum.wav";
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
            if (auto* drumFixture = dynamic_cast<DrumSequencerNode*>(gNodes[27].node.get()))
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
         static_cast<BouncingBallsNode*>(gNodes[14].node.get())->shape = BouncingBallsNode::kTriangle;

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
         auto* eq = static_cast<AudioEffectNode*>(gNodes[26].node.get());
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

         mkdir("/tmp/infinite_samplerdrag", 0755);
         const std::string wavPath = "/tmp/infinite_samplerdrag/sample.wav";
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

         gSampleScanner.AddFolder("/tmp/infinite_samplerdrag");
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
         // The check is that the drag resolves onto the node under the cursor
         // (the node's saved identity), not that a plugin instantiates, so one
         // fake row is enough. A real scan with no cache (this test uses a
         // throwaway settings dir) can outlast the 600-frame budget on a
         // machine with many VST3s; INFINITE_PLUGINDRAG_REALSCAN=1 keeps it.
         if (getenv("INFINITE_PLUGINDRAG_REALSCAN") != nullptr)
            gPluginScanner.StartScan();
         else
         {
            Platform::PluginDesc fake;
            fake.format = "vst3";
            fake.name = "Drag Test Plugin";
            fake.manufacturer = "Infinite self-test";
            fake.identifier = "vst3:selftest-plugindrag";
            fake.path = "C:/tmp/infinite_plugindrag/missing.vst3"; // never exists: load fails cleanly
            gPluginScanner.DebugSetIndex({ fake });
         }
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

         mkdir("/tmp/infinite_mediadrag", 0755);
         const std::string pngPath = "/tmp/infinite_mediadrag/fixture.png";
         const int side = 8;
         std::vector<unsigned char> pixels(side * side * 4, 255);
         stbi_write_png(pngPath.c_str(), side, side, 4, pixels.data(), side * 4);

         gMediaScanner.AddFolder("/tmp/infinite_mediadrag");
         gMediaScanner.StartScan();
      }
      else if (getenv("INFINITE_BYPASSTEST") != nullptr)
      {
         SpawnNode("Shape", "Source", 40.0f, 40.0f);   // 0 white circle
         SpawnNode("invert", "Color", 320.0f, 40.0f);  // 1
         SpawnNode("Output", "Output", 600.0f, 40.0f); // 2
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
         SpawnNode("Shape", "Source", 40.0f, 40.0f);       // 0
         SpawnNode("Output", "Output", 320.0f, 40.0f);     // 1
         SpawnNode("Audio File", "Modulators", 40.0f, 400.0f); // 2
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         auto* audio = static_cast<AudioFileNode*>(gNodes[2].node.get());
         audio->Open(getenv("INFINITE_AUDIORECTEST"));
         audio->monitor = false; // silent while the test runs
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         out->includeAudio = true;
         out->AudioInput().Connect(audio);
         out->recordFps = 30;
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
         SpawnNode("invert", "Color", 1160.0f, 40.0f);     // 6
         SpawnNode("Output", "Output", 1440.0f, 40.0f);    // 7
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
         audioFile->Open("/tmp/models/tone.wav");
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
         SpawnNode("Audio Out", "AudioUtility", 600.0f, 40.0f); // 2

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
         GraphNode* outGn = SpawnNode("Audio Out", "AudioUtility", 320.0f, 40.0f);
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
         const uint64_t xrunAfterCycle = AudioEngine::Instance().XrunCount();
         Check("Stop/Start cycle adds no xrun", restarted && xrunAfterCycle == 0);

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
         GraphNode* outGn = SpawnNode("Audio Out", "AudioUtility", 320.0f, 40.0f);
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
         const std::string envPath = "/tmp/infinite_envtest.hdr";
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
      else if (getenv("INFINITE_PORTEDTEST") != nullptr)
      {
         // Turbo 0.60: every node ported from upstream after v0.6 spawns,
         // takes its input and produces output. Checked at frame 8, below.
         const char* names[] = {
            "Formula", "Time Machine", "Optical Flow", "Fluid", "Volume", "NDI In", "NDI Out",       // 0..6
            "Curve", "Sweep", "Revolve", "L-System", "Distribute Points in Grid", "Proximity Lines", // 7..12
            "Geometry", "Rigid Bodies", "Bevel", "Inset",                                            // 13..16
            "MSEG", "Voice Spread", "Note Expression",                                               // 17..19
            "FM", "String", "Partials", "Saturator", "Convolution Reverb", "Shape Resonator", "dither" // 20..26
         };
         for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
            SpawnNode(names[i], "", 40.0f + 260.0f * (i % 7), 40.0f + 320.0f * (i / 7));
         auto* src = static_cast<FormulaNode*>(gNodes[0].node.get());
         src->formula = "   return vec4(uv.x, uv.y, 0.5 + 0.5 * sin(t * 3.0), 1.0);";
         src->width = 128.0f;
         src->height = 128.0f;
         src->Apply();
         for (int i : { 1, 2, 3, 4, 6, 26 })
            if (ImageCable* c = CableFor(gNodes[i], 0))
               c->Connect(gNodes[0].node.get());
         auto geo = [](int i) { return dynamic_cast<IGeometrySource*>(gNodes[i].node.get()); };
         static_cast<SweepNode*>(gNodes[8].node.get())->path = geo(7);
         *gNodes[9].node->GeometryInputSlot(0) = geo(7);
         static_cast<ProximityLinesNode*>(gNodes[12].node.get())->input = geo(11);
         static_cast<RigidBodyNode*>(gNodes[14].node.get())->input = geo(13);
         *gNodes[15].node->GeometryInputSlot(0) = geo(13);
         *gNodes[16].node->GeometryInputSlot(0) = geo(13);
      }
      else if (getenv("INFINITE_TOPPARITYTEST") != nullptr)
      {
         // Turbo (TOP parity): Common page, Crop/Transform/Blend/Switcher/
         // Formula changes and the wireless Send/Receive family. Checked at
         // frame 4, below.
         auto formula = [](int i, const char* body, float w, float h)
         {
            auto* f = static_cast<FormulaNode*>(gNodes[i].node.get());
            f->formula = body;
            f->width = w;
            f->height = h;
            f->Apply();
         };
         SpawnNode("Formula", "Source", 40.0f, 40.0f);              // 0 gradient 200x100
         SpawnNode("crop", "Effects", 300.0f, 40.0f);               // 1
         SpawnNode("transform", "Compositing", 560.0f, 40.0f);      // 2
         SpawnNode("invert", "Color", 820.0f, 40.0f);               // 3
         SpawnNode("Formula", "Source", 40.0f, 400.0f);             // 4 white 100x100
         SpawnNode("Formula", "Source", 40.0f, 700.0f);             // 5 transparent 200x100
         SpawnNode("Blend", "Compositing", 300.0f, 400.0f);         // 6
         SpawnNode("Switcher", "Compositing", 560.0f, 400.0f);      // 7
         SpawnNode("Formula", "Source", 820.0f, 400.0f);            // 8 reads uIn0
         SpawnNode("Send Image", "Utility", 300.0f, 700.0f);        // 9
         SpawnNode("Send Image", "Utility", 560.0f, 700.0f);        // 10
         SpawnNode("Receive Image", "Utility", 820.0f, 700.0f);     // 11
         SpawnNode("Constant", "Modulators", 40.0f, 1000.0f);       // 12
         SpawnNode("Send CV", "Utility", 300.0f, 1000.0f);          // 13
         SpawnNode("Receive CV", "Utility", 560.0f, 1000.0f);       // 14
         SpawnNode("Geometry", "3D", 40.0f, 1300.0f);               // 15
         SpawnNode("Send Geometry", "Utility", 300.0f, 1300.0f);    // 16
         SpawnNode("Receive Geometry", "Utility", 560.0f, 1300.0f); // 17
         SpawnNode("Null 3D", "3D", 820.0f, 1300.0f);               // 18
         SpawnNode("Send Geometry", "Utility", 1080.0f, 1300.0f);   // 19
         SpawnNode("Receive Geometry", "Utility", 1340.0f, 1300.0f);// 20
         SpawnNode("Oscillator", "Synths", 40.0f, 1600.0f);         // 21
         SpawnNode("Send Audio", "Utility", 300.0f, 1600.0f);       // 22
         SpawnNode("Receive Audio", "Utility", 560.0f, 1600.0f);    // 23
         SpawnNode("Note Sequencer", "Notes", 40.0f, 1900.0f);      // 24
         SpawnNode("Send Notes", "Utility", 300.0f, 1900.0f);       // 25
         SpawnNode("Receive Notes", "Utility", 560.0f, 1900.0f);    // 26

         formula(0, "   return vec4(uv.x, uv.y, 0.0, 1.0);", 200.0f, 100.0f);
         formula(4, "   return vec4(1.0);", 100.0f, 100.0f);
         formula(5, "   return vec4(0.0);", 200.0f, 100.0f);
         formula(8, "   return texture(uIn0, uv);", 64.0f, 64.0f);

         auto setFilter = [](int i, const char* uniform, float v)
         {
            auto* f = static_cast<FilterNode*>(gNodes[i].node.get());
            for (size_t p = 0; p < f->Def().params.size(); p++)
               if (f->Def().params[p].uniformName == uniform)
                  f->SetParamValue(p, 0, v);
         };
         CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         setFilter(1, "uLeft", 0.25f);
         setFilter(1, "uRight", 0.25f);
         setFilter(1, "uTop", 0.1f);
         setFilter(1, "uBottom", 0.1f);
         setFilter(1, "uMode", 3.0f);

         CableFor(gNodes[2], 0)->Connect(gNodes[0].node.get());
         setFilter(2, "uTranslateX", 0.5f);

         CableFor(gNodes[3], 0)->Connect(gNodes[0].node.get());
         auto* inv = static_cast<FilterNode*>(gNodes[3].node.get());
         inv->common.res = FilterNode::Common::kResHalf;
         inv->common.format = FilterNode::Common::kFmt16f;
         inv->common.chanG = false;
         inv->common.chanB = false;

         auto* blend = static_cast<BlendNode*>(gNodes[6].node.get());
         blend->InputA().Connect(gNodes[5].node.get());
         blend->InputB().Connect(gNodes[4].node.get());
         blend->preFit = BlendNode::kFit;

         auto* sw = static_cast<SwitcherNode*>(gNodes[7].node.get());
         sw->Input(0).Connect(gNodes[4].node.get());
         sw->Input(1).Connect(gNodes[5].node.get());
         sw->manual = true;
         sw->index = 0.5f;

         static_cast<FormulaNode*>(gNodes[8].node.get())->Input(0).Connect(gNodes[4].node.get());
         static_cast<FormulaNode*>(gNodes[8].node.get())->matchInput = true;

         static_cast<SendImageNode*>(gNodes[9].node.get())->Input().Connect(gNodes[4].node.get());
         static_cast<SendImageNode*>(gNodes[10].node.get())->Input().Connect(gNodes[0].node.get());
         static_cast<WirelessNode*>(gNodes[9].node.get())->channel = "img";
         static_cast<WirelessNode*>(gNodes[10].node.get())->channel = "img";
         static_cast<WirelessNode*>(gNodes[11].node.get())->channel = "img";

         static_cast<ConstantNode*>(gNodes[12].node.get())->value = 0.3f;
         ConnectGeometrySlot(gNodes[13], 0, gNodes[12]); // the patch-load path for a modulator pin
         static_cast<WirelessNode*>(gNodes[13].node.get())->channel = "cv";
         static_cast<WirelessNode*>(gNodes[14].node.get())->channel = "cv";

         static_cast<SendGeometryNode*>(gNodes[16].node.get())->input =
            dynamic_cast<IGeometrySource*>(gNodes[15].node.get());
         static_cast<WirelessNode*>(gNodes[16].node.get())->channel = "geo";
         static_cast<WirelessNode*>(gNodes[17].node.get())->channel = "geo";
         // A wireless geometry loop: Receive "loop" -> Null 3D -> Send "loop".
         static_cast<Null3DNode*>(gNodes[18].node.get())->input =
            dynamic_cast<IGeometrySource*>(gNodes[20].node.get());
         static_cast<SendGeometryNode*>(gNodes[19].node.get())->input =
            dynamic_cast<IGeometrySource*>(gNodes[18].node.get());
         static_cast<WirelessNode*>(gNodes[19].node.get())->channel = "loop";
         static_cast<WirelessNode*>(gNodes[20].node.get())->channel = "loop";

         static_cast<SendAudioNode*>(gNodes[22].node.get())->input.Connect(gNodes[21].node.get());
         static_cast<WirelessNode*>(gNodes[22].node.get())->channel = "aud";
         static_cast<WirelessNode*>(gNodes[23].node.get())->channel = "aud";
         static_cast<SendNotesNode*>(gNodes[25].node.get())->noteInput.Connect(gNodes[24].node.get());
         static_cast<WirelessNode*>(gNodes[25].node.get())->channel = "nt";
         static_cast<WirelessNode*>(gNodes[26].node.get())->channel = "nt";
      }
      else if (getenv("INFINITE_UTILTEST") != nullptr)
      {
         SpawnNode("Geometry", "3D", 40.0f, 40.0f);        // 0
         SpawnNode("Null 3D", "3D", 300.0f, 40.0f);        // 1
         SpawnNode("Mesh to Points", "3D", 560.0f, 40.0f); // 2
         SpawnNode("Render 3D", "3D", 820.0f, 40.0f);      // 3
         SpawnNode("Shape", "Source", 40.0f, 500.0f);      // 4
         SpawnNode("Null", "Compositing", 300.0f, 500.0f); // 5
         SpawnNode("Output", "Output", 560.0f, 500.0f);    // 6

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
         // graded through a five-stage 2D chain before Output.
         SpawnNode("Cube", "3D", 40.0f, 40.0f);                    // 0 source shape
         SpawnNode("Resynthesize 3D", "3D", 320.0f, 40.0f);        // 1 continuous morph
         SpawnNode("Particle System", "3D", 40.0f, 420.0f);        // 2 drift field
         SpawnNode("Instance on Points", "3D", 320.0f, 420.0f);    // 3 stamp shape at each particle
         SpawnNode("Camera", "3D", 40.0f, 760.0f);                 // 4
         SpawnNode("Light", "3D", 40.0f, 920.0f);                  // 5 key
         SpawnNode("Light", "3D", 40.0f, 1080.0f);                 // 6 fill/rim
         SpawnNode("Render 3D", "3D", 620.0f, 420.0f);             // 7
         SpawnNode("hsl", "Color", 900.0f, 420.0f);                // 8
         SpawnNode("colorbalance", "Color", 900.0f, 560.0f);       // 9
         SpawnNode("bloom", "Effects", 900.0f, 700.0f);            // 10
         SpawnNode("vignette", "Effects", 900.0f, 840.0f);         // 11
         SpawnNode("brightnesscontrast", "Color", 900.0f, 980.0f); // 12
         SpawnNode("Output", "Output", 1180.0f, 420.0f);           // 13

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

         auto* hsl = static_cast<FilterNode*>(gNodes[8].node.get());
         hsl->Input().Connect(render);
         hsl->SetParamValue(1, 0, 1.5f); // Saturation - stands in for the old standalone vibrance step

         auto* colorbalance = static_cast<FilterNode*>(gNodes[9].node.get());
         colorbalance->Input().Connect(hsl);
         colorbalance->SetParamValue(0, 0, 0.05f);  // Cyan-Red: push warm
         colorbalance->SetParamValue(1, 0, 0.0f);   // Magenta-Green
         colorbalance->SetParamValue(2, 0, -0.05f); // Yellow-Blue: push toward yellow

         auto* bloom = static_cast<FilterNode*>(gNodes[10].node.get());
         bloom->Input().Connect(colorbalance);
         bloom->SetParamValue(0, 0, 0.72f); // Threshold - only genuine highlights bloom
         bloom->SetParamValue(1, 0, 0.9f);  // Intensity
         bloom->SetParamValue(2, 0, 4.0f);  // Radius

         auto* vignette = static_cast<FilterNode*>(gNodes[11].node.get());
         vignette->Input().Connect(bloom);

         auto* bc = static_cast<FilterNode*>(gNodes[12].node.get());
         bc->Input().Connect(vignette);
         bc->SetParamValue(0, 0, 0.02f); // Brightness
         bc->SetParamValue(1, 0, 0.15f); // Contrast

         auto* out = static_cast<OutputNode*>(gNodes[13].node.get());
         out->Input().Connect(bc);

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
      else if (getenv("INFINITE_TEXTFIT") != nullptr)
      {
         SpawnNode("Text", "Text", 40.0f, 40.0f);
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
         SpawnNode("Reaction Diffusion", "Feedback", 40.0f, 40.0f);
         SpawnNode("Curves", "Color", 300.0f, 40.0f);
         SpawnNode("Shape", "Source", 560.0f, 40.0f);
         SpawnNode("Trails", "Feedback", 820.0f, 40.0f);
         SpawnNode("Resynthesize", "Resynth", 1080.0f, 40.0f);
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
         SpawnNode("Reaction Diffusion", "Feedback", 40.0f, 40.0f);
         SpawnNode("Curves", "Color", 300.0f, 40.0f);
         SpawnNode("Shape", "Source", 560.0f, 40.0f);
         SpawnNode("Trails", "Feedback", 820.0f, 40.0f);
         SpawnNode("Resynthesize", "Resynth", 1080.0f, 40.0f);
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
         SpawnNode("Text", "Text", 600.0f, 40.0f);
         SpawnNode("Layer Stack", "Compositing", 880.0f, 40.0f);
         SpawnNode("Output", "Output", 1160.0f, 40.0f);
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
      else if (const char* loadPatchPath = getenv("INFINITE_LOADPATCH"))
      {
         LoadPatchFrom(loadPatchPath);
         gRequestFitView = true;
      }
      else if (wantsFixture)
      {
         SpawnNode("Shape", "Source", 60.0f, 60.0f);
         if (getenv("INFINITE_RESYNTHTEST") != nullptr)
         {
            SpawnNode("Resynthesize", "Resynth", 380.0f, 60.0f);
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
            gNodes[1].showParams = true;
         }
         else
            SpawnNode("Output", "Output", 380.0f, 60.0f);
         if (getenv("INFINITE_RECTEST") != nullptr)
            CableFor(gNodes[1], 0)->Connect(gNodes[0].node.get());
         if (getenv("INFINITE_HIDETEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
         if (getenv("INFINITE_MODTEST") != nullptr)
         {
            SpawnNode("LFO", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true; // params must be drawn for them to register
         }
         if (getenv("INFINITE_MACROTEST") != nullptr)
         {
            SpawnNode("Macro XY", "Modulators", 60.0f, 500.0f);
            gNodes[0].showParams = true;
         }
         if (getenv("INFINITE_OSCTEST") != nullptr)
         {
            // Constant -> OSC Send -> (UDP, loopback) -> OSC Receive, proving a
            // round trip through the actual wire format rather than just
            // exercising the C++ classes in isolation.
            SpawnNode("Constant", "Modulators", 60.0f, 500.0f);   // gNodes[2]
            SpawnNode("OSC Send", "OSC", 300.0f, 500.0f);         // gNodes[3]
            SpawnNode("OSC Receive", "OSC", 540.0f, 500.0f);      // gNodes[4]
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
   char exportPath[512] = "";
   const std::string desktopDir = InfiniteDesktopDirectory();
   if (!desktopDir.empty())
      snprintf(exportPath, sizeof(exportPath), "%s/infinite_output.png", desktopDir.c_str());
   else
      snprintf(exportPath, sizeof(exportPath), "infinite_output.png");

   char recordPath[512] = "";
   if (!desktopDir.empty())
      snprintf(recordPath, sizeof(recordPath), "%s/infinite_output.mp4", desktopDir.c_str());
   else
      snprintf(recordPath, sizeof(recordPath), "infinite_output.mp4");

   if (argc > 1 && argv[1] != nullptr && argv[1][0] != '-')
   {
      std::string argPath = Platform::CommandLineArgUtf8(1); // Turbo 0.46: accented paths
      if (argPath.empty())
         argPath = argv[1];
      if (HasExtension(argPath, std::vector<std::string> { "inf", "infinite" }))
      {
         LoadPatchFrom(argPath);
         gRequestFitView = true;
      }
   }
   // Turbo 0.46: with no file to open (and nothing to recover), start on the
   // bundled example, playing. Settings > "Open the example at startup".
   // Never in a self-test run (test-windows.bat sets INFINITE_EXITAFTER): the
   // example would start audio, move the view and add nodes under the fixture.
   const bool selfTestRun = getenv("INFINITE_EXITAFTER") != nullptr || getenv("IMAGERESYNTH_SCREENSHOT") != nullptr;
   // Turbo 0.59: Setup > Startup decides (empty by default; audio follows
   // "Start audio when Infinite opens", off by default).
   if (!selfTestRun && gNodes.empty() && gPatchPath.empty() && !gRecoveryAvailable)
   {
      std::error_code startEc;
      if (gStartupMode == 2)
         LoadExamplePatch(gAudioAutoStart);
      else if (gStartupMode == 1 && !Patch::Recents().empty() &&
               std::filesystem::exists(std::filesystem::u8path(Patch::Recents().front()), startEc))
         LoadPatchFrom(Patch::Recents().front());
      else if (gStartupMode == 3 && !gStartupPatch.empty() &&
               std::filesystem::exists(std::filesystem::u8path(gStartupPatch), startEc))
         LoadPatchFrom(gStartupPatch);
      if (!gNodes.empty())
         gRequestFitView = true;
   }
   // Any launch-time Finder open is drained by the in-frame loop below, right
   // after the first glfwPollEvents() call.

   char searchBuf[128] = "";
   bool searchJustOpened = false;
   std::vector<std::string> clipboard;      // typeNames copied
   std::vector<INode*> clipboardSources;    // live sources to copy params from
   std::vector<int> clipboardOrigIndex;     // gNodes index each item had at copy time
   std::vector<int> clipboardOrigGroup;     // that item's owning group's index, or -1
   int frameId = 0;

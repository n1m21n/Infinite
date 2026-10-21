// Part of src/main.cpp, included there in order (one translation unit).
// main window, recovery prompt, menu bar

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
      ImGui::Begin("Infinite", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

      if (gRecoveryAvailable && !gRecoveryPromptShown)
      {
         ImGui::OpenPopup("Recover project");
         gRecoveryPromptShown = true;
      }
      if (ImGui::BeginPopupModal("Recover project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::TextWrapped("Infinite found an autosave from a session that did not close normally.");
         ImGui::TextWrapped("Recover it before continuing?");
         if (ImGui::Button("Recover", ImVec2(100, 0)))
         {
            RecoverAutosave();
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button("Discard", ImVec2(100, 0)))
         {
            DiscardRecoveryAutosave();
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button("Later", ImVec2(100, 0)))
            ImGui::CloseCurrentPopup();
         ImGui::EndPopup();
      }

      // Turbo 0.59: classic menus by what they act on. FILE the document,
      // EDIT the graph, VIEW what is on screen (windows, canvas, minimap),
      // SETUP the app's settings (it replaced MENU), HELP the reference.
      if (ImGui::BeginMenuBar())
      {
         if (ImGui::BeginMenu("FILE"))
         {
            ImGui::TextDisabled("%s  (build %s)", INFINITE_TURBO_TITLE, kTurboBuildDate);
            ImGui::TextDisabled("unofficial Windows mod of Infinite");
            ImGui::Separator();
            if (ImGui::MenuItem("New", kShortcutNew))
               GuardUnsavedChanges([]() { NewPatch(); });
            if (ImGui::MenuItem("Open...", kShortcutOpen))
            {
               StartFileDialog(Platform::OpenPatchDialog, [](const std::string& path) {
                  GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
               });
            }

            if (ImGui::BeginMenu("Open Recent", !Patch::Recents().empty()))
            {
               // Copied before iterating: opening one calls NoteRecent, which
               // reorders the very list being walked.
               const std::vector<std::string> recents = Patch::Recents();
               for (const std::string& entry : recents)
               {
                  const size_t slash = entry.find_last_of("/\\");
                  const std::string name =
                     (slash == std::string::npos) ? entry : entry.substr(slash + 1);
                  if (ImGui::MenuItem(name.c_str()))
                     GuardUnsavedChanges([entry]() { LoadPatchFrom(entry); });
                  if (ImGui::IsItemHovered())
                     ImGui::SetTooltip("%s", entry.c_str());
               }
               ImGui::EndMenu();
            }

            // Turbo 0.59: the patches in <app>/examples, opened untitled.
            if (ImGui::BeginMenu("Examples"))
            {
               static std::vector<std::pair<std::string, std::string>> sExamples;
               if (ImGui::IsWindowAppearing())
                  sExamples = ListExamplePatches();
               if (sExamples.empty())
                  ImGui::TextDisabled("no examples found next to the app");
               for (const auto& ex : sExamples)
               {
                  if (ImGui::MenuItem(ex.first.c_str()))
                  {
                     const std::string path = ex.second;
                     GuardUnsavedChanges([path]() { LoadPatchAsUntitled(path); });
                  }
                  if (ImGui::IsItemHovered())
                     ImGui::SetTooltip("%s", ex.second.c_str());
               }
               ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import patch as a block..."))
               StartFileDialog(Platform::OpenPatchDialog, [](const std::string& p) { ImportPatchAsBlock(p); });
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Adds a whole .inf to this patch inside a new group (a Set block) that can be\n"
                                 "switched on and off: several songs in one patch (VIEW > Set list).");

            ImGui::Separator();
            if (ImGui::MenuItem("Save", kShortcutSave))
               SavePatchInteractive(false);
            if (ImGui::MenuItem("Save As...", kShortcutSaveAs))
               SavePatchInteractive(true);

            ImGui::Separator();
            const bool recoveryOnDisk = gRecoveryAvailable ||
               std::filesystem::exists(std::filesystem::u8path(RecoveryAutosavePath()));
            if (ImGui::MenuItem("Recover autosave...", nullptr, false, recoveryOnDisk))
            {
               gRecoveryAvailable = true;
               gRecoveryPromptShown = false;
            }

            if (!gPatchPath.empty() || !gPatchStatus.empty())
            {
               ImGui::Separator();
               if (!gPatchPath.empty())
               {
                  const size_t slash = gPatchPath.find_last_of("/\\");
                  ImGui::TextDisabled("%s", (slash == std::string::npos)
                                               ? gPatchPath.c_str()
                                               : gPatchPath.c_str() + slash + 1);
               }
               if (!gPatchStatus.empty())
                  ImGui::TextDisabled("%s", gPatchStatus.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
               RequestClose(window);
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("EDIT"))
         {
            if (ImGui::MenuItem("Undo", kShortcutUndo, false, !gUndoStack.empty()))
               Undo();
            if (ImGui::MenuItem("Redo", kShortcutRedo, false, !gRedoStack.empty()))
               Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Group selection", kShortcutGroup))
               gRequestGroup = true;
            if (ImGui::MenuItem("Ungroup", kShortcutUngroup))
               gRequestUngroup = true;
            ImGui::Separator();
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
            ImGui::Separator();
            if (ImGui::MenuItem("Expression globals..."))
               gGlobalsOpen = true;
            if (ImGui::MenuItem("MIDI learn mode", "Ctrl+M", MidiMap::LearnMode()))
            {
               MidiMap::LearnMode() = !MidiMap::LearnMode();
               if (!MidiMap::LearnMode())
                  MidiMap::Disarm();
            }
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("VIEW"))
         {
            if (ImGui::MenuItem("Side panel", "Ctrl+B", gNodePanelOpen))
               gNodePanelOpen = !gNodePanelOpen;
            if (ImGui::MenuItem("Fullscreen UI", "F11", gMainFullscreen))
               SetMainWindowFullscreen(!gMainFullscreen);
            ImGui::SeparatorText("Windows");
            if (ImGui::MenuItem("AI chat (Claude / ChatGPT)", "Shift+C", gChatOpen))
            {
               gChatOpen = !gChatOpen;
               gChatFocusInput = gChatOpen;
            }
            if (ImGui::MenuItem("Set list", "Shift+L", gSetListOpen))
               gSetListOpen = !gSetListOpen;
            if (ImGui::MenuItem("Performance mode", "Shift+P", gPerfPanelOpen))
               gPerfPanelOpen = !gPerfPanelOpen;
            if (ImGui::MenuItem("Arrangement timeline", "Shift+T", gArrangePanelOpen))
               gArrangePanelOpen = !gArrangePanelOpen;
            if (ImGui::MenuItem("Modulation matrix", "Shift+M", gModMatrixOpen))
               gModMatrixOpen = !gModMatrixOpen;
            if (ImGui::MenuItem("MIDI map", nullptr, gMidiWindowOpen))
               gMidiWindowOpen = !gMidiWindowOpen;

            // Turbo 0.59: show safety (ShowSafety.inl).
            ImGui::SeparatorText("Show");
            if (ImGui::MenuItem("Panic", "Ctrl+.", gPanicActive))
               TogglePanic();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Mutes the output (5 ms fade), sends all-notes-off, releases plugin notes\n"
                                 "and stops the transport. Again to bring the sound back.");
            if (ImGui::MenuItem("Show lock", "Ctrl+Shift+L", gShowLock))
               SetShowLock(!gShowLock);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("No deleting, dragging, new cables or undo while it is on.\n"
                                 "Knobs, transport, Performance mode and the Set list keep working.");
            if (ImGui::MenuItem("Pre-show check...", nullptr, gPreShowOpen))
               gPreShowOpen = !gPreShowOpen;

            ImGui::SeparatorText("Canvas");
            if (ImGui::MenuItem("Fit view to content"))
               gRequestFitView = true;
            ImGui::Checkbox("Snap to grid", &gSnapToGrid);
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Grid size", &gGridSnap, 5.0f, 100.0f, "%.0f px");
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Zoom speed", &gZoomSensitivity, 0.05f, 1.5f, "%.2f");

            ImGui::SeparatorText("Minimap");
            ImGui::Checkbox("Show minimap", &gMinimapEnabled);
            if (gMinimapEnabled)
            {
               static const char* kCorners[] = {
                  "Top left", "Top right", "Bottom left", "Bottom right"
               };
               ImGui::SetNextItemWidth(170);
               if (ImGui::BeginCombo("Position", kCorners[gMinimapCorner]))
               {
                  for (int i = 0; i < 4; i++)
                     if (ImGui::Selectable(kCorners[i], i == gMinimapCorner))
                        gMinimapCorner = i;
                  ImGui::EndCombo();
               }
               ImGui::SetNextItemWidth(170);
               ImGui::SliderFloat("Size", &gMinimapSize, 120.0f, 360.0f, "%.0f px");
               ImGui::SetNextItemWidth(170);
               ImGui::SliderFloat("Opacity", &gMinimapOpacity, 0.2f, 1.0f, "%.2f");
            }

            // The whole section only exists once something is open - with
            // nothing open there is nothing here to configure.
            if (!gViewportPanelNodes.empty())
            {
               ImGui::SeparatorText("Viewport panel");
               ImGui::SetNextItemWidth(170);
               ViewportPanelDockCombo();
               ImGui::SetNextItemWidth(170);
               // Only the axis the current dock actually reserves - the other
               // one has no effect from here, and a dead slider reads as broken.
               if (gViewportPanelDock == 1 || gViewportPanelDock == 2)
                  ImGui::SliderFloat("Width", &gViewportPanelWidth,
                                     kViewportPanelMinWidth, 900.0f, "%.0f px");
               else
                  ImGui::SliderFloat("Height", &gViewportPanelHeight,
                                     kViewportPanelMinHeight, 800.0f, "%.0f px");
               if (ImGui::MenuItem("Close viewport panel"))
                  gViewportPanelNodes.clear();
            }

            ImGui::SeparatorText("Output behind the canvas");
            GraphNode* bgNode = gCanvasBgNodeIndex >= 0 ? FindNodeByIndex(gCanvasBgNodeIndex) : nullptr;
            if (bgNode != nullptr)
               ImGui::TextDisabled("showing: %s", NodeTitleWithInstance(*bgNode).c_str());
            else
               ImGui::TextDisabled("off");
            if (ImGui::MenuItem("Show selected node", "Ctrl+Shift+B"))
            {
               const int sel = FirstSelectedImageNodeIndex();
               if (sel >= 0)
                  gCanvasBgNodeIndex = sel;
            }
            if (ImGui::MenuItem("Turn off", nullptr, false, bgNode != nullptr))
               gCanvasBgNodeIndex = -1;
            DrawCanvasBackgroundSettings();
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("SETUP"))
         {
            // Turbo 0.59: what Infinite opens with, and whether audio starts.
            ImGui::SeparatorText("Startup");
            {
               static const char* kStartup[] = { "An empty patch", "The last patch", "The example (superSynthMCP)",
                                                 "A patch I choose" };
               gStartupMode = std::clamp(gStartupMode, 0, 3);
               ImGui::SetNextItemWidth(220);
               if (ImGui::BeginCombo("Open at startup", kStartup[gStartupMode]))
               {
                  for (int i = 0; i < 4; i++)
                     if (ImGui::Selectable(kStartup[i], i == gStartupMode))
                        gStartupMode = i;
                  ImGui::EndCombo();
               }
               gStartWithExample = gStartupMode == 2; // older builds read this one
               if (gStartupMode == 3)
               {
                  const std::string shown = gStartupPatch.empty()
                                               ? std::string("(none chosen)")
                                               : std::filesystem::u8path(gStartupPatch).filename().u8string();
                  ImGui::TextDisabled("%s", shown.c_str());
                  if (!gStartupPatch.empty() && ImGui::IsItemHovered())
                     ImGui::SetTooltip("%s", gStartupPatch.c_str());
                  if (ImGui::SmallButton("Choose..."))
                     StartFileDialog(Platform::OpenPatchDialog, [](const std::string& p) { gStartupPatch = p; });
                  if (!gPatchPath.empty())
                  {
                     ImGui::SameLine();
                     if (ImGui::SmallButton("Use the open patch"))
                        gStartupPatch = gPatchPath;
                  }
               }
               ImGui::Checkbox("Start audio when Infinite opens", &gAudioAutoStart);
               if (ImGui::MenuItem(gPatchDirty ? "Open the example now (drops unsaved edits)" : "Open the example now"))
                  LoadExamplePatch(true);
            }

            ImGui::SeparatorText("AI assistants");
            if (ImGui::MenuItem("AI connections...", nullptr, gAiConnectionsOpen))
               gAiConnectionsOpen = !gAiConnectionsOpen;
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Claude and ChatGPT in the chat window, ChatGPT on the web (connector URL),\n"
                                 "Claude Desktop, Claude Code, Codex and any other MCP client.");

            ImGui::SeparatorText("Performance");
            {
               // A cap is useful in both directions: it stops a light patch
               // spinning the GPU at 400fps for no reason, and it gives a
               // predictable frame budget to judge a heavy one against.
               static const char* kFpsLabels[] = { "Unlimited", "30", "60", "120" };
               static const int kFpsValues[] = { 0, 30, 60, 120 };
               int current = 0;
               for (int i = 0; i < 4; i++)
                  if (kFpsValues[i] == gTargetFps)
                     current = i;

               ImGui::SetNextItemWidth(170);
               if (ImGui::BeginCombo("Target FPS", kFpsLabels[current]))
               {
                  for (int i = 0; i < 4; i++)
                  {
                     if (ImGui::Selectable(kFpsLabels[i], current == i))
                        gTargetFps = kFpsValues[i];
                  }
                  ImGui::EndCombo();
               }

               if (ImGui::Checkbox("Vsync", &gVsync))
                  gMainSwapIntervalApplied = -1; // re-applied right before the next swap
            }

            // Turbo 0.46: interface scale (Auto follows the monitor's Windows scale).
            ImGui::SeparatorText("Interface scale");
            {
               static const float kScales[] = { 0.0f, 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f };
               char current[32];
               if (gUiScaleSetting <= 0.0f)
                  snprintf(current, sizeof(current), "Auto (Windows, %.0f%%)", gUiPointScale * 100.0f);
               else
                  snprintf(current, sizeof(current), "%.0f%%", gUiScaleSetting * 100.0f);
               ImGui::SetNextItemWidth(190);
               if (ImGui::BeginCombo("##uiscale", current))
               {
                  for (float sc : kScales)
                  {
                     char label[32];
                     if (sc <= 0.0f)
                        snprintf(label, sizeof(label), "Auto (Windows)");
                     else
                        snprintf(label, sizeof(label), "%.0f%%", sc * 100.0f);
                     if (ImGui::Selectable(label, sc == gUiScaleSetting))
                     {
                        gUiScaleSetting = sc;
                        gUiRescaleRequested = true;
                     }
                  }
                  ImGui::EndCombo();
               }
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("Size of the whole interface. Auto follows the scale Windows uses\n"
                                    "for the monitor the window is on (and changes when you move it).");
            }
            {
               const char* currentLabel = kInterfaceFonts[0].label;
               for (const InterfaceFont& f : kInterfaceFonts)
                  if (gUiFontId == f.id)
                     currentLabel = f.label;
               ImGui::SetNextItemWidth(190);
               if (ImGui::BeginCombo("Interface font", currentLabel))
               {
                  for (const InterfaceFont& f : kInterfaceFonts)
                     if (ImGui::Selectable(f.label, gUiFontId == f.id) && gUiFontId != f.id)
                     {
                        gUiFontId = f.id;
                        gUiRescaleRequested = true; // rebakes the atlas with the new face
                     }
                  ImGui::EndCombo();
               }
            }
            ImGui::SeparatorText("Theme");
            {
               const std::vector<std::string>& presets = CategoryColors::PresetNames();
               const int current = CategoryColors::CurrentPreset();
               // Panel / text / accent fingerprint swatches, so presets can be
               // told apart without switching to each one. Border polarity follows
               // the live theme so near-white and near-black swatches stay legible.
               const float kSw = 10.0f, kGap = 3.0f, kStripW = 3 * kSw + 2 * kGap;
               auto drawSwatches = [&](ImVec2 pos, const CategoryColors::UiTheme& t) {
                  ImDrawList* dl = ImGui::GetWindowDrawList();
                  const CategoryColors::Color cols[3] = { t.panelBg, t.text, t.accent };
                  const ImU32 border = IsThemeLight() ? IM_COL32(0, 0, 0, 115) : IM_COL32(255, 255, 255, 115);
                  for (int k = 0; k < 3; k++)
                  {
                     const ImVec2 p0(pos.x + k * (kSw + kGap), pos.y), p1(p0.x + kSw, p0.y + kSw);
                     dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(cols[k].r, cols[k].g, cols[k].b, 1.0f)), 2.0f);
                     dl->AddRect(p0, p1, border, 2.0f, 0, 1.0f);
                  }
               };
               {
                  const ImVec2 cur = ImGui::GetCursorScreenPos();
                  const float rowH = ImGui::GetFrameHeight();
                  drawSwatches(ImVec2(cur.x, cur.y + (rowH - kSw) * 0.5f), CategoryColors::UiThemeForPreset(current));
                  ImGui::Dummy(ImVec2(kStripW, rowH));
                  ImGui::SameLine();
               }
               ImGui::SetNextItemWidth(190);
               if (ImGui::BeginCombo("Theme", presets[current].c_str()))
               {
                  for (int i = 0; i < (int)presets.size(); i++)
                  {
                     if (ImGui::Selectable(presets[i].c_str(), current == i))
                     {
                        CategoryColors::SetPreset(i);
                        ApplyTheme();
                     }
                     const ImVec2 rMin = ImGui::GetItemRectMin(), rMax = ImGui::GetItemRectMax();
                     drawSwatches(ImVec2(rMax.x - 8.0f - kStripW, rMin.y + (rMax.y - rMin.y - kSw) * 0.5f),
                                  CategoryColors::UiThemeForPreset(i));
                  }
                  ImGui::EndCombo();
               }
            }

            ImGui::SeparatorText("Audio");
            {
               // Turbo: driver type. ASIO (the interface's own driver) or
               // WASAPI exclusive give the lowest live latency; shared WASAPI
               // goes through the Windows mixer and adds its buffering.
               {
                  const std::vector<int> drivers = Platform::AudioAvailableDrivers();
                  ImGui::SetNextItemWidth(220);
                  if (ImGui::BeginCombo("Driver", Platform::AudioDriverLabel(gAudioDriver).c_str()))
                  {
                     for (int d : drivers)
                        if (ImGui::Selectable(Platform::AudioDriverLabel(d).c_str(), d == gAudioDriver) && d != gAudioDriver)
                        {
                           gAudioDriver = d;
                           Platform::AudioSetRequestedDriver(d);
                           // device names differ per driver type
                           gAudioOutputDeviceId = 0;
                           gAudioInputDeviceId = 0;
                        }
                     ImGui::EndCombo();
                  }
                  if (std::find(drivers.begin(), drivers.end(), 1) == drivers.end())
                     ImGui::TextDisabled("ASIO not in this build");
               }
               const std::vector<Platform::AudioDeviceInfo> devices = Platform::AudioListDevices();
               const bool audioRunning = AudioEngine::Instance().SampleRate() > 0.0;

               std::string outputLabel = (gAudioOutputDeviceId == 0) ? "System default" : "Unknown device";
               for (const Platform::AudioDeviceInfo& d : devices)
                  if (d.isOutput && d.deviceId == gAudioOutputDeviceId)
                     outputLabel = d.name;
               ImGui::SetNextItemWidth(220);
               if (ImGui::BeginCombo("Output device", outputLabel.c_str()))
               {
                  if (ImGui::Selectable("System default", gAudioOutputDeviceId == 0))
                     gAudioOutputDeviceId = 0;
                  for (const Platform::AudioDeviceInfo& d : devices)
                  {
                     if (!d.isOutput)
                        continue;
                     if (ImGui::Selectable(d.name.c_str(), d.deviceId == gAudioOutputDeviceId))
                        gAudioOutputDeviceId = d.deviceId;
                  }
                  ImGui::EndCombo();
               }

               // Input and output are selected independently on Windows.
               // Audio In consumes this selection through AudioEngine's
               // requested device setup; System default remains the fallback.
               std::string inputLabel = (gAudioInputDeviceId == 0) ? "System default" : "Unknown device";
               for (const Platform::AudioDeviceInfo& d : devices)
                  if (d.isInput && d.deviceId == gAudioInputDeviceId)
                     inputLabel = d.name;
               ImGui::SetNextItemWidth(220);
               if (ImGui::BeginCombo("Input device", inputLabel.c_str()))
               {
                  if (ImGui::Selectable("System default", gAudioInputDeviceId == 0))
                     gAudioInputDeviceId = 0;
                  for (const Platform::AudioDeviceInfo& d : devices)
                  {
                     if (!d.isInput)
                        continue;
                     if (ImGui::Selectable(d.name.c_str(), d.deviceId == gAudioInputDeviceId))
                        gAudioInputDeviceId = d.deviceId;
                  }
                  ImGui::EndCombo();
               }
               static const double kSampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
               std::string rateLabel = (gAudioSampleRate == 0.0) ? "Device default" : "";
               if (gAudioSampleRate != 0.0)
               {
                  char buf[32];
                  snprintf(buf, sizeof(buf), "%.0f Hz", gAudioSampleRate);
                  rateLabel = buf;
               }
               ImGui::SetNextItemWidth(220);
               if (ImGui::BeginCombo("Sample rate", rateLabel.c_str()))
               {
                  if (ImGui::Selectable("Device default", gAudioSampleRate == 0.0))
                     gAudioSampleRate = 0.0;
                  for (double rate : kSampleRates)
                  {
                     char label[32];
                     snprintf(label, sizeof(label), "%.0f Hz", rate);
                     if (ImGui::Selectable(label, gAudioSampleRate == rate))
                        gAudioSampleRate = rate;
                  }
                  ImGui::EndCombo();
               }

               static const int kBufferSizes[] = { 32, 48, 64, 96, 128, 192, 256, 512, 1024, 2048 };
               char bufferLabel[16];
               snprintf(bufferLabel, sizeof(bufferLabel), "%d", gAudioBufferFrames);
               ImGui::SetNextItemWidth(220);
               if (ImGui::BeginCombo("Buffer size", bufferLabel))
               {
                  for (int frames : kBufferSizes)
                  {
                     char label[16];
                     snprintf(label, sizeof(label), "%d", frames);
                     if (ImGui::Selectable(label, gAudioBufferFrames == frames))
                        gAudioBufferFrames = frames;
                  }
                  ImGui::EndCombo();
               }
               static const float kOversampleValues[] = { 1.0f, 2.0f, 4.0f };
               static const char* kOversampleLabels[] = { "1x", "2x", "4x" };
               int oversampleIdx = 0;
               for (int i = 0; i < 3; i++)
                  if (kOversampleValues[i] == gAudioOversample)
                     oversampleIdx = i;
               ImGui::SetNextItemWidth(220);
               if (ImGui::BeginCombo("Oversampling", kOversampleLabels[oversampleIdx]))
               {
                  for (int i = 0; i < 3; i++)
                     if (ImGui::Selectable(kOversampleLabels[i], oversampleIdx == i))
                        gAudioOversample = kOversampleValues[i];
                  ImGui::EndCombo();
               }
               if (audioRunning)
               {
                  const uint32_t actualBufferFrames = Platform::AudioDeviceBufferFrames(gAudioOutputDeviceId);
                  ImGui::TextDisabled("Active: %.0f Hz, %u frames", AudioEngine::Instance().SampleRate(),
                                      actualBufferFrames);
                  // what the driver reports: input + output latency + one buffer
                  const int rtFrames = Platform::AudioRoundTripLatencyFrames();
                  const double sr = AudioEngine::Instance().SampleRate();
                  if (rtFrames > 0 && sr > 0.0)
                     ImGui::TextDisabled("%s, round trip ~%.1f ms (driver-reported, plus plugin latency)",
                                         Platform::AudioCurrentDriverName().c_str(), 1000.0 * rtFrames / sr);
               }

               // Turbo 0.51: second output on a Windows (WASAPI shared) device
               // that mirrors the master, so OBS can capture it while ASIO owns
               // the interface.
               ImGui::SeparatorText("Mirror output");
               ImGui::Checkbox("Mirror output to Windows device (for OBS / screen recorders)", &gAudioMirror);
               if (gAudioMirror)
               {
                  const std::vector<std::string> mirrorDevices = Platform::AudioMirrorListDevices();
                  ImGui::SetNextItemWidth(220);
                  if (ImGui::BeginCombo("Mirror device", gAudioMirrorDevice.empty() ? "Windows default" : gAudioMirrorDevice.c_str()))
                  {
                     if (ImGui::Selectable("Windows default", gAudioMirrorDevice.empty()))
                        gAudioMirrorDevice.clear();
                     for (const std::string& n : mirrorDevices)
                        if (ImGui::Selectable(n.c_str(), n == gAudioMirrorDevice))
                           gAudioMirrorDevice = n;
                     ImGui::EndCombo();
                  }
                  if (gAudioDriver != 1)
                     ImGui::TextDisabled("Meant for ASIO. With other drivers pick a different device than the main output.");
               }
               {
                  const std::string mirrorStatus = Platform::AudioMirrorStatus();
                  if (!mirrorStatus.empty())
                     ImGui::TextDisabled("%s", mirrorStatus.c_str());
               }

               if (ImGui::MenuItem("Apply audio settings"))
               {
                  {
                     std::string mirrorError;
                     Platform::AudioMirrorApply(gAudioMirror, gAudioMirrorDevice, mirrorError);
                  }
                  const bool wasRunning = AudioEngine::Instance().SampleRate() > 0.0;
                  if (wasRunning)
                     AudioEngine::Instance().Stop();

                  AudioEngine::Instance().SetRequestedDevice(gAudioOutputDeviceId);
                  AudioEngine::Instance().SetRequestedInputDevice(gAudioInputDeviceId);
                  AudioEngine::Instance().SetRequestedSampleRate(gAudioSampleRate);
                  AudioEngine::Instance().SetRequestedBufferFrames(gAudioBufferFrames);

                  if (wasRunning)
                  {
                     gAudioStartError.clear();
                     // StartAudioEngine, not a bare Start(): the new rate/
                     // buffer size just applied above may not be what the
                     // device actually negotiates, and every node's
                     // ParamMailbox needs PrepareToPlay'd with whatever it
                     // negotiates - see StartAudioEngine's comment (bugs 1/2).
                     if (!StartAudioEngine(gAudioStartError))
                        fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
                  }
               }
            }

            ImGui::SeparatorText("Updates");
            ImGui::Checkbox("Check for updates at startup", &gUpdateCheckEnabled);
            if (ImGui::MenuItem("Check for updates now"))
               TurboUpdate::Start();
            {
               const int st = TurboUpdate::gStatus.load();
               std::string latest, err;
               {
                  std::lock_guard<std::mutex> lock(TurboUpdate::gMutex);
                  latest = TurboUpdate::gLatest;
                  err = TurboUpdate::gError;
               }
               if (st == 1) ImGui::TextDisabled("checking...");
               else if (st == 2) ImGui::TextDisabled("up to date (latest %s)", latest.c_str());
               else if (st == 3) ImGui::TextDisabled("%s is available", latest.c_str());
               else if (st == 4) ImGui::TextDisabled("check failed: %s", err.c_str());
            }

            // Turbo 0.60 (upstream): the movement log Moves / Drift / Predictive
            // Modulator learn from - on/off, its folder and how much it may keep.
            ImGui::SeparatorText("Movement log");
            {
               bool moveLogEnabled = MovementLog::IsEnabled();
               if (ImGui::Checkbox("Record movement log", &moveLogEnabled))
                  MovementLog::SetEnabled(moveLogEnabled);
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s", "Every knob move and who made it (hand, modulator, gesture, prediction).\nMoves, Drift and Predictive Modulator learn from it. Stays on this machine.");
               if (ImGui::MenuItem("Open log folder"))
               {
                  const std::string dir = MovementLog::GetLogDirectory();
                  if (!dir.empty())
                     Platform::RevealInFileManager(dir);
               }
               const float folderMB = (float)MovementLog::GetLogFolderSizeBytes() / (1024.0f * 1024.0f);
               ImGui::TextDisabled("%.1f MB used", folderMB);
               static const char* kCapLabels[] = { "256 MB", "1 GB", "4 GB" };
               static const uint64_t kCapValues[] = { 256ULL * 1024 * 1024, 1024ULL * 1024 * 1024, 4096ULL * 1024 * 1024 };
               const uint64_t curCap = MovementLog::GetRetentionCapBytes();
               int capIdx = 1;
               for (int i = 0; i < 3; i++)
                  if (curCap == kCapValues[i])
                     capIdx = i;
               ImGui::SetNextItemWidth(120.0f);
               if (ImGui::BeginCombo("Keep at most", kCapLabels[capIdx]))
               {
                  for (int i = 0; i < 3; i++)
                     if (ImGui::Selectable(kCapLabels[i], capIdx == i))
                        MovementLog::SetRetentionCapBytes(kCapValues[i]);
                  ImGui::EndCombo();
               }
            }

            ImGui::SeparatorText("Safety and diagnostics");
            if (ImGui::Checkbox("Diagnostic log", &gDiagnosticLogEnabled))
            {
               RuntimeLog::SetEnabled(gDiagnosticLogEnabled);
               RuntimeLog::Write("diagnostic logging setting changed");
            }
            ImGui::TextDisabled("Log file: Infinite.log");
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", RuntimeLog::Path().c_str());
            if (ImGui::MenuItem("Clear diagnostic log"))
               RuntimeLog::Clear();
            if (ImGui::Checkbox("Autosave recovery", &gAutosaveEnabled) && !gAutosaveEnabled)
               DiscardRecoveryAutosave();
            static const int kAutosaveIntervals[] = { 15, 30, 60, 120, 300 };
            char autosaveLabel[32];
            snprintf(autosaveLabel, sizeof(autosaveLabel), "%d seconds", gAutosaveSeconds);
            ImGui::SetNextItemWidth(170);
            if (ImGui::BeginCombo("Autosave interval", autosaveLabel))
            {
               for (int seconds : kAutosaveIntervals)
               {
                  char label[32];
                  snprintf(label, sizeof(label), "%d seconds", seconds);
                  if (ImGui::Selectable(label, gAutosaveSeconds == seconds))
                     gAutosaveSeconds = seconds;
               }
               ImGui::EndCombo();
            }
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("HELP"))
         {
            if (ImGui::MenuItem("Module reference"))
               gHelpOpen = true;
            if (ImGui::MenuItem("AI connections (Claude, ChatGPT, MCP)..."))
               gAiConnectionsOpen = true;
            if (ImGui::BeginMenu("AI skill (patch-building guide)"))
            {
               if (ImGui::MenuItem("Install for Claude Code"))
                  gPatchStatus = InstallPatchSkill(std::string());
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("Writes the patch-building guide to %%USERPROFILE%%\\.claude\\skills\\infinite-turbo-patching");
               if (ImGui::MenuItem("Save to a folder..."))
               {
                  const std::string folder = Platform::OpenFolderDialog("Folder for the infinite-turbo-patching skill");
                  if (!folder.empty())
                     gPatchStatus = InstallPatchSkill(folder);
               }
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("Saves infinite-turbo-patching\\SKILL.md: zip that folder to upload it as a skill in\n"
                                    "Claude, add it to a GPT's knowledge, or hand it to any AI assistant.\n"
                                    "Over MCP the same guide is the authoring_guide tool.");
               ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Check for updates now"))
               TurboUpdate::Start();
            ImGui::TextDisabled("%s  (build %s)", INFINITE_TURBO_TITLE, kTurboBuildDate);
            ImGui::EndMenu();
         }
         ImGui::Separator();

         // ---- top bar, left: transport (Turbo 0.59: icons) -------------------------
         Transport& transport = Transport::Instance();
         const ImVec4 kOnGreen(0.16f, 0.52f, 0.28f, 1.0f);
         if (UiIconButton("##play", transport.IsPlaying() ? UiIcon::Pause : UiIcon::Play, transport.IsPlaying(),
                          transport.IsPlaying() ? "pause" : "play", kOnGreen))
            transport.TogglePlay();
         ImGui::SameLine(0.0f, 2.0f);
         if (UiIconButton("##rewind", UiIcon::Rewind, false, "back to the start"))
            transport.Rewind();
         ImGui::SameLine(0.0f, 2.0f);

         // Audio engine on/off, independent of Play/Pause above: Play just
         // drives the timeline (what modulators/visuals read), so someone
         // working on visuals shouldn't have audio hardware opened just to
         // scrub a patch around. Audio-graph nodes read silence while this is
         // off, same as a machine with no usable device.
         {
            const bool audioOn = AudioEngine::Instance().SampleRate() > 0.0;
            std::string tip = audioOn ? "audio on: click to stop the audio engine"
                                      : "audio off: click to start the audio engine";
            if (!audioOn && !gAudioStartError.empty())
               tip += "\n" + gAudioStartError;
            if (UiIconButton("##audiopower", UiIcon::Power, audioOn, tip.c_str(), kOnGreen))
            {
               if (audioOn)
                  AudioEngine::Instance().Stop();
               else
               {
                  gAudioStartError.clear();
                  // StartAudioEngine (bugs 1/2): a patch loaded while the
                  // engine was off has every node still prepared with
                  // whatever rate it was constructed/last-rebuilt with, not
                  // the device's. Route through the one choke point instead
                  // of AudioEngine::Instance().Start() directly.
                  if (!StartAudioEngine(gAudioStartError))
                     fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
               }
            }
         }

         ImGui::Separator();

         // Tempo: drag sideways, scroll the wheel over it (Shift: 0.1 steps)
         // or double-click to type an exact value.
         {
            float bpm = transport.Tempo();
            ImGui::SetNextItemWidth(78);
            if (ImGui::DragFloat("##bpm", &bpm, 0.1f, 20.0f, 300.0f, "%.1f BPM", ImGuiSliderFlags_AlwaysClamp))
               transport.SetTempo(std::clamp(bpm, 20.0f, 300.0f));
            if (ImGui::IsItemHovered())
            {
               const ImGuiIO& bio = ImGui::GetIO();
               if (bio.MouseWheel != 0.0f && !ImGui::IsItemActive())
                  transport.SetTempo(std::clamp(transport.Tempo() + bio.MouseWheel * (bio.KeyShift ? 0.1f : 1.0f), 20.0f, 300.0f));
               ImGui::SetTooltip("tempo: drag, scroll (Shift: fine) or double-click to type");
            }
         }

         // Time signature (docs/plans/audio/P3c-P3a2-design.md §0.2): every
         // bar-relative rate reads Transport::BeatsPerBar(), so this is the
         // one place that controls all of them at once. Turbo 0.59: one
         // button and a popup (the two tiny fields were hard to hit).
         {
            const int tsNum = transport.TimeSigNumerator();
            const int tsDen = transport.TimeSigDenominator();
            char tsLabel[16];
            snprintf(tsLabel, sizeof(tsLabel), "%d/%d", tsNum, tsDen);
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::Button(tsLabel, ImVec2(ImGui::CalcTextSize("12/16").x + ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f)))
               ImGui::OpenPopup("##timesig");
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("time signature");
            if (ImGui::BeginPopup("##timesig"))
            {
               ImGui::TextDisabled("Time signature");
               static const int kPresets[][2] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 },
                                                  { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
               for (int i = 0; i < 8; i++)
               {
                  if (i % 4 != 0)
                     ImGui::SameLine();
                  char pl[16];
                  snprintf(pl, sizeof(pl), "%d/%d", kPresets[i][0], kPresets[i][1]);
                  if (ImGui::Selectable(pl, kPresets[i][0] == tsNum && kPresets[i][1] == tsDen, 0, ImVec2(42, 0)))
                     transport.SetTimeSignature(kPresets[i][0], kPresets[i][1]);
               }
               ImGui::Separator();
               int num = tsNum;
               ImGui::SetNextItemWidth(96);
               if (ImGui::InputInt("beats", &num, 1, 1))
                  transport.SetTimeSignature(std::clamp(num, 1, 16), tsDen);
               static const int kDens[] = { 1, 2, 4, 8, 16 };
               ImGui::TextUnformatted("note");
               for (int d : kDens)
               {
                  ImGui::SameLine();
                  char dl[8];
                  snprintf(dl, sizeof(dl), "%d", d);
                  if (ImGui::RadioButton(dl, d == tsDen))
                     transport.SetTimeSignature(tsNum, d);
               }
               ImGui::EndPopup();
            }
         }

         ImGui::Separator();

         // Global Key and Scale
         {
            static const char* const kKeyNames[] = {
               "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            int curKey = transport.Key();
            ImGui::SetNextItemWidth(45);
            if (ImGui::BeginCombo("##globalKey", kKeyNames[std::clamp(curKey, 0, 11)]))
            {
               for (int i = 0; i < 12; i++)
               {
                  if (ImGui::Selectable(kKeyNames[i], i == curKey))
                     transport.SetKey(i);
               }
               ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("global key");

            ImGui::SameLine(0.0f, 2.0f);
            int curScale = transport.Scale();
            const auto& scaleList = MusicTime::ScaleTypeList();
            const std::string curScaleName = DisplayName(
               (curScale >= 0 && curScale < (int)scaleList.size()) ? scaleList[curScale] : "major");
            ImGui::SetNextItemWidth(104);
            if (ImGui::BeginCombo("##globalScale", curScaleName.c_str()))
            {
               for (int i = 0; i < (int)scaleList.size(); i++)
               {
                  const std::string scaleLabel = DisplayName(scaleList[i]) + "##GLOBAL_SCALE:" + std::to_string(i);
                  if (ImGui::Selectable(scaleLabel.c_str(), i == curScale))
                     transport.SetScale(i);
               }
               ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("global scale");
         }

         ImGui::Separator();

         // Turbo 0.59: position as the bar number, the beat number and one
         // cell per beat of the bar (in the signature's own note value), lit
         // up to the current one.
         {
            const int tsNum = std::max(1, transport.TimeSigNumerator());
            const int tsDen = std::max(1, transport.TimeSigDenominator());
            const double inBar = std::fmod(std::max(0.0, transport.Beats()), transport.BeatsPerBar());
            const int count = std::clamp((int)std::floor(inBar * (double)tsDen / 4.0), 0, tsNum - 1);
            const int bar = 1 + (int)transport.Bars();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d", bar);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("bar");
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text("%d", count + 1);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("beat");
            ImGui::SameLine(0.0f, 6.0f);
            const int cells = std::min(tsNum, 16);
            const float cellW = 5.0f, cellGap = 2.0f, cellH = ImGui::GetFrameHeight() * 0.5f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float y0 = p.y + (ImGui::GetFrameHeight() - cellH) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool playing = transport.IsPlaying();
            for (int i = 0; i < cells; i++)
            {
               const float x0 = p.x + i * (cellW + cellGap);
               ImU32 col;
               if (i == count)
                  col = i == 0 ? IM_COL32(242, 140, 50, 255) : (playing ? IM_COL32(80, 170, 245, 255) : IM_COL32(150, 150, 160, 255));
               else if (i < count)
                  col = IM_COL32(80, 170, 245, 120);
               else
                  col = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
               dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cellW, y0 + cellH), col, 1.5f);
            }
            ImGui::Dummy(ImVec2(cells * (cellW + cellGap), ImGui::GetFrameHeight()));
         }

         // Turbo 0.59: show safety states, impossible to miss while on.
         if (gPanicActive)
         {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("PANIC"))
               TogglePanic();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Output muted: click (or Ctrl+.) to bring the sound back");
         }
         if (gShowLock)
         {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.42f, 0.10f, 1.0f));
            if (ImGui::Button("LOCKED"))
               SetShowLock(false);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Show lock: editing blocked. Click (or Ctrl+Shift+L) to unlock");
         }

         // ---- top bar, right: system monitors, then icon toggles ----------------
         // Frame cost: a gentle EMA of the swap-to-swap wall clock rather than
         // ImGui's smoothed rate, so a heavy patch shows its real cost at once.
         static double sSmoothedMs = 0.0;
         sSmoothedMs = (sSmoothedMs <= 0.0) ? gLastFrameMs : sSmoothedMs * 0.9 + gLastFrameMs * 0.1;
         const double fps = sSmoothedMs > 0.0001 ? 1000.0 / sSmoothedMs : 0.0;

         const bool audioEngineOn = AudioEngine::Instance().SampleRate() > 0.0;
         const double audioLoad = AudioEngine::Instance().LastBlockLoad();
         const uint64_t xruns = AudioEngine::Instance().XrunCount();
         // "on but dead" is real, brief window: PollAudioRecovery polls once
         // a frame and either recovers or gives up and turns the engine off.
         const bool audioDead = audioEngineOn && !AudioEngine::Instance().IsAlive();
         Platform::GpuStats gpuStats;
         const bool gpuAvailable = Platform::ReadGpuStats(gpuStats);
         const float cpuPercent = Platform::ReadProcessCpuPercent();

         const ImGuiStyle& barStyle = ImGui::GetStyle();
         const float frameH = ImGui::GetFrameHeight();
         const float iconBtnW = frameH * 1.15f;
         const float smallGap = 2.0f;
         const float rightEdge = ImGui::GetWindowWidth() - barStyle.WindowPadding.x * 2.0f;
         const float panelX = rightEdge - iconBtnW;
         const float setX = panelX - smallGap - iconBtnW;
         const float perfX = setX - smallGap - iconBtnW;
         const float clickX = perfX - smallGap - iconBtnW;
         const float chatX = clickX - smallGap - iconBtnW;

         // The monitors: one framed group, an icon and a number each, coloured
         // green / amber / red against their budget. Hover for the details.
         struct Monitor
         {
            UiIcon icon;
            char text[16];
            ImVec4 color;
            std::string tip;
         };
         const auto level = [](double x) {
            return x < 0.7 ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                 : x < 0.9 ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                           : ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
         };
         const ImVec4 kIdle(0.55f, 0.55f, 0.58f, 1.0f);
         std::vector<Monitor> monitors;
         {
            Monitor m { UiIcon::Cpu, {}, kIdle, {} };
            if (cpuPercent >= 0.0f)
            {
               snprintf(m.text, sizeof(m.text), "%.0f%%", cpuPercent);
               m.color = level(cpuPercent / 100.0);
            }
            else
               snprintf(m.text, sizeof(m.text), "--");
            m.tip = "CPU: Infinite-Turbo's share of the whole processor (all cores)";
            monitors.push_back(m);
         }
         {
            Monitor m { UiIcon::Audio, {}, kIdle, {} };
            if (audioDead)
            {
               snprintf(m.text, sizeof(m.text), "LOST");
               m.color = ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
               m.tip = "Audio device lost: reconnecting";
            }
            else if (audioEngineOn)
            {
               snprintf(m.text, sizeof(m.text), "%.0f%%%s", audioLoad * 100.0, xruns > 0 ? "!" : "");
               m.color = level(audioLoad);
               char tip[160];
               snprintf(tip, sizeof(tip), "Audio: %.0f%% of each block's real-time budget\n%llu buffer underrun%s this session",
                        audioLoad * 100.0, (unsigned long long)xruns, xruns == 1 ? "" : "s");
               m.tip = tip;
            }
            else
            {
               snprintf(m.text, sizeof(m.text), "off");
               m.tip = "Audio engine off (the power button at the left starts it)";
            }
            monitors.push_back(m);
         }
         {
            Monitor m { UiIcon::Gpu, {}, kIdle, {} };
            if (gpuAvailable)
            {
               snprintf(m.text, sizeof(m.text), "%.0f%%", gpuStats.gpuPercent);
               m.color = level(gpuStats.gpuPercent / 100.0);
               m.tip = "GPU: " + (gpuStats.name.empty() ? std::string("NVIDIA GPU") : gpuStats.name);
            }
            else
            {
               snprintf(m.text, sizeof(m.text), "--");
               m.tip = "GPU load needs an NVIDIA driver (NVML)";
            }
            monitors.push_back(m);
         }
         if (gpuAvailable)
         {
            Monitor m { UiIcon::Vram, {}, kIdle, {} };
            snprintf(m.text, sizeof(m.text), "%.0f%%", gpuStats.memoryPercent);
            m.color = level(gpuStats.memoryPercent / 100.0);
            char tip[96];
            snprintf(tip, sizeof(tip), "Video memory: %.0f / %.0f MiB",
                     (double)gpuStats.memoryUsedBytes / (1024.0 * 1024.0), (double)gpuStats.memoryTotalBytes / (1024.0 * 1024.0));
            m.tip = tip;
            monitors.push_back(m);
         }
         {
            // Judged against the target when there is one, so hitting a
            // deliberate 30fps cap reads as green rather than as a problem.
            // Otherwise against 50/25, where dragging a slider stops feeling live.
            const double good = gTargetFps > 0 ? gTargetFps * 0.95 : 50.0;
            const double poor = gTargetFps > 0 ? gTargetFps * 0.5 : 25.0;
            Monitor m { UiIcon::Fps, {}, kIdle, {} };
            snprintf(m.text, sizeof(m.text), "%.0f", fps);
            m.color = (fps >= good) ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                    : (fps >= poor) ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                    : ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
            char tip[96];
            if (gTargetFps > 0)
               snprintf(tip, sizeof(tip), "Frames per second: %.1f of %d (%.1f ms a frame)", fps, gTargetFps, sSmoothedMs);
            else
               snprintf(tip, sizeof(tip), "Frames per second: %.1f (%.1f ms a frame)", fps, sSmoothedMs);
            m.tip = tip;
            monitors.push_back(m);
         }
         // Fixed widths (a worst-case "100%!"), so nothing jitters as digits change.
         const float monIconW = frameH * 0.62f;
         const float monTextW = ImGui::CalcTextSize("100%!").x;
         const float monSegW = monIconW + 4.0f + monTextW;
         const float monPad = 8.0f;
         const float monW = monPad * 2.0f + monSegW * (float)monitors.size() + 6.0f * (float)(monitors.size() - 1);
         const float monX = chatX - barStyle.ItemSpacing.x * 3.0f - monW;

         // Turbo 0.46: newer release badge, left of the monitors.
         if (TurboUpdate::gStatus.load() == 3)
         {
            std::string latest, url;
            {
               std::lock_guard<std::mutex> lock(TurboUpdate::gMutex);
               latest = TurboUpdate::gLatest;
               url = TurboUpdate::gUrl;
            }
            const std::string badge = "UPDATE " + latest;
            const float badgeW = ImGui::CalcTextSize(badge.c_str()).x + barStyle.FramePadding.x * 2.0f;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), monX - badgeW - barStyle.ItemSpacing.x * 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
            if (ImGui::Button(badge.c_str()))
               Platform::OpenUrl(url);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Infinite-Turbo %s is out (you have %s): opens the release page",
                                 latest.c_str(), INFINITE_TURBO_VERSION);
         }

         {
            ImGui::SameLine(monX);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(origin, ImVec2(origin.x + monW, origin.y + frameH), ImGui::GetColorU32(ImGuiCol_FrameBgHovered),
                              frameH * 0.5f);
            float x = origin.x + monPad;
            for (size_t i = 0; i < monitors.size(); i++)
            {
               const Monitor& m = monitors[i];
               if (i > 0)
                  ImGui::SameLine(0.0f, 0.0f);
               ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
               ImGui::PushID((int)i);
               ImGui::InvisibleButton("##mon", ImVec2(monSegW, frameH));
               ImGui::PopID();
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("%s", m.tip.c_str());
               DrawUiIcon(dl, m.icon, ImVec2(x + monIconW * 0.5f, origin.y + frameH * 0.5f), monIconW,
                          ImGui::GetColorU32(ImGuiCol_TextDisabled));
               dl->AddText(ImVec2(x + monIconW + 4.0f, origin.y + (frameH - ImGui::GetTextLineHeight()) * 0.5f),
                           ImGui::GetColorU32(m.color), m.text);
               x += monSegW + 6.0f;
            }
         }

         // Icon toggles: AI chat, metronome, Performance mode, Set list, side panel.
         ImGui::SameLine(chatX);
         if (UiIconButton("##chatbtn", UiIcon::Chat, gChatOpen, "AI chat: Claude or ChatGPT build and change this patch (Shift+C)"))
         {
            gChatOpen = !gChatOpen;
            gChatFocusInput = gChatOpen;
         }
         {
            // Metronome: lit on every beat while it plays (accent on the 1).
            const bool clickOn = AudioEngine::Instance().TopBarClick();
            const double beat = transport.Beats();
            const double frac = beat - std::floor(beat);
            const bool flash = clickOn && transport.IsPlaying() && frac < 0.18;
            const bool downbeat = flash && std::fmod(std::floor(beat), std::max(1.0, (double)transport.BeatsPerBar())) < 0.5;
            ImGui::SameLine(clickX);
            if (clickOn)
               ImGui::PushStyleColor(ImGuiCol_Button, flash ? (downbeat ? ImVec4(0.95f, 0.55f, 0.20f, 1.0f)
                                                                       : ImVec4(0.30f, 0.65f, 0.95f, 1.0f))
                                                            : barStyle.Colors[ImGuiCol_ButtonActive]);
            // Turbo 0.48: a drawn metronome instead of the "CLICK" text (as upstream).
            if (ImGui::Button("##clickBtn", ImVec2(iconBtnW, 0.0f)))
               AudioEngine::Instance().SetTopBarClick(!clickOn);
            if (clickOn)
               ImGui::PopStyleColor();
            {
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec2 bmin = ImGui::GetItemRectMin();
               const ImVec2 bmax = ImGui::GetItemRectMax();
               const float h = (bmax.y - bmin.y) * 0.74f;
               const float cx = (bmin.x + bmax.x) * 0.5f;
               const float top = (bmin.y + bmax.y) * 0.5f - h * 0.5f;
               const float bot = top + h;
               ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
               if (!clickOn)
                  col.w *= 0.6f;
               const ImU32 c = ImGui::GetColorU32(col);
               const float th = std::max(1.0f, h * 0.075f);
               // Body: a trapezoid, narrow at the top, plus the base.
               const ImVec2 body[4] = { ImVec2(cx - h * 0.14f, top), ImVec2(cx + h * 0.14f, top),
                                        ImVec2(cx + h * 0.36f, bot), ImVec2(cx - h * 0.36f, bot) };
               dl->AddPolyline(body, 4, c, ImDrawFlags_Closed, th);
               dl->AddLine(ImVec2(cx - h * 0.42f, bot), ImVec2(cx + h * 0.42f, bot), c, th);
               // Rod from a pivot near the base. Beat-locked: it reaches a
               // side exactly on each beat and crosses the middle between.
               const bool swinging = clickOn && transport.IsPlaying();
               const float angle = swinging ? 0.55f * (float)std::cos(3.14159265358979 * beat) : 0.35f;
               const ImVec2 pivot(cx, bot - h * 0.16f);
               const float len = h * 0.86f;
               const ImVec2 tip(pivot.x + std::sin(angle) * len, pivot.y - std::cos(angle) * len);
               dl->AddLine(pivot, tip, c, th);
               const ImVec2 weight(pivot.x + std::sin(angle) * len * 0.62f, pivot.y - std::cos(angle) * len * 0.62f);
               dl->AddCircleFilled(weight, std::max(1.5f, h * 0.09f), c);
            }
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("metronome on the audio output while the transport plays");
         }
         ImGui::SameLine(perfX);
         if (UiIconButton("##perfbtn", UiIcon::Perf, gPerfPanelOpen,
                          "Performance mode: a panel of controls bound to any parameter (Shift+P)"))
            gPerfPanelOpen = !gPerfPanelOpen;
         ImGui::SameLine(setX);
         if (UiIconButton("##setbtn", UiIcon::SetList, gSetListOpen,
                          "Set list: switch songs (groups) on and off for a long show (Shift+L)"))
            gSetListOpen = !gSetListOpen;
         ImGui::SameLine(panelX);
         if (UiIconButton("##panelbtn", UiIcon::Panel, gNodePanelOpen,
                          "side panel: modules, samples, media, plugins (Ctrl+B)"))
            gNodePanelOpen = !gNodePanelOpen;

         ImGui::EndMenuBar();
      }

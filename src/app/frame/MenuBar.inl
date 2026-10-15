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
            ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("VIEW"))
         {
            if (ImGui::MenuItem("Side panel", "Ctrl+B", gNodePanelOpen))
               gNodePanelOpen = !gNodePanelOpen;
            if (ImGui::MenuItem("Fullscreen UI", "F11", gMainFullscreen))
               SetMainWindowFullscreen(!gMainFullscreen);
            ImGui::SeparatorText("MIDI");
            if (ImGui::MenuItem("MIDI learn mode", "Ctrl+M", MidiMap::LearnMode()))
            {
               MidiMap::LearnMode() = !MidiMap::LearnMode();
               if (!MidiMap::LearnMode())
                  MidiMap::Disarm();
            }
            if (ImGui::MenuItem("MIDI map...", nullptr, gMidiWindowOpen))
               gMidiWindowOpen = !gMidiWindowOpen;
            if (ImGui::MenuItem("Modulation matrix...", "Shift+M", gModMatrixOpen))
               gModMatrixOpen = !gModMatrixOpen;
            if (ImGui::MenuItem("Arrangement timeline", "Shift+T", gArrangePanelOpen))
               gArrangePanelOpen = !gArrangePanelOpen;
            if (ImGui::MenuItem("Performance mode", "Shift+P", gPerfPanelOpen))
               gPerfPanelOpen = !gPerfPanelOpen;
            if (ImGui::MenuItem("Claude chat", "Shift+C", gChatOpen))
            {
               gChatOpen = !gChatOpen;
               gChatFocusInput = gChatOpen;
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

         if (ImGui::BeginMenu("MENU"))
         {
            ImGui::SeparatorText("Canvas");
            ImGui::Checkbox("Snap to grid", &gSnapToGrid);
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Grid size", &gGridSnap, 5.0f, 100.0f, "%.0f px");
            ImGui::SetNextItemWidth(170);
            ImGui::SliderFloat("Zoom speed", &gZoomSensitivity, 0.05f, 1.5f, "%.2f");
            if (ImGui::MenuItem("Fit view to content"))
               gRequestFitView = true;

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
            ImGui::Checkbox("Start audio when Infinite opens", &gAudioAutoStart);
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

            ImGui::SeparatorText("Nodes");
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
            if (ImGui::MenuItem("Help / module reference"))
               gHelpOpen = true;

            // Turbo 0.46: AI assistants (MCP server + patch-building skill).
            ImGui::SeparatorText("AI assistants (Claude / MCP)");
            if (ImGui::MenuItem("Claude chat in this window", "Shift+C", gChatOpen))
            {
               gChatOpen = !gChatOpen;
               gChatFocusInput = gChatOpen;
            }
            if (ImGui::MenuItem("Connect to Claude Desktop (MCP)"))
               InstallMcpConfig(); // reports in a message box
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Registers this Infinite-Turbo as an MCP server in Claude Desktop's config\n"
                                 "(same as setup-mcp.bat). Restart Claude Desktop afterwards.");
            if (ImGui::MenuItem("Install AI skill for Claude Code"))
               gPatchStatus = InstallPatchSkill(std::string());
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Writes the patch-building guide to %%USERPROFILE%%\\.claude\\skills\\infinite-turbo-patching");
            if (ImGui::MenuItem("Save AI skill to a folder..."))
            {
               const std::string folder = Platform::OpenFolderDialog("Folder for the infinite-turbo-patching skill");
               if (!folder.empty())
                  gPatchStatus = InstallPatchSkill(folder);
            }
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Saves infinite-turbo-patching\\SKILL.md: zip that folder to upload it as a skill in\n"
                                 "Claude (Settings > Capabilities > Skills), or hand the file to any AI assistant.\n"
                                 "Over MCP the same guide is the authoring_guide tool.");

            ImGui::SeparatorText("Startup");
            ImGui::Checkbox("Open the example at startup", &gStartWithExample);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("On: Infinite-Turbo opens with the superSynthMCP example playing.\nOff: it opens empty.");
            if (ImGui::MenuItem(gPatchDirty ? "Open the example now (drops unsaved edits)" : "Open the example now"))
               LoadExamplePatch(true);

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

            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
               RequestClose(window);
            ImGui::EndMenu();
         }
         ImGui::Separator();

         Transport& transport = Transport::Instance();
         ImGui::PushStyleColor(ImGuiCol_Button, transport.IsPlaying()
                                                   ? ImVec4(0.16f, 0.52f, 0.28f, 1.0f)
                                                   : ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
         if (ImGui::Button(transport.IsPlaying() ? "Pause" : "Play", ImVec2(64, 0)))
            transport.TogglePlay();
         ImGui::PopStyleColor();

         if (ImGui::Button("Rewind"))
            transport.Rewind();

         ImGui::Separator();

         // Audio engine on/off, independent of Play/Pause above: Play just
         // drives the timeline (what modulators/visuals read), so someone
         // working on visuals shouldn't have audio hardware opened just to
         // scrub a patch around - see the removed auto-start at startup.
         // Audio-graph nodes read silence while this is off, same as a
         // machine with no usable device.
         {
            const bool audioOn = AudioEngine::Instance().SampleRate() > 0.0;
            ImGui::PushStyleColor(ImGuiCol_Button, audioOn
                                                       ? ImVec4(0.16f, 0.52f, 0.28f, 1.0f)
                                                       : ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
            if (ImGui::Button(audioOn ? "Stop Audio" : "Start Audio"))
            {
               if (audioOn)
                  AudioEngine::Instance().Stop();
               else
               {
                  gAudioStartError.clear();
                  // StartAudioEngine (bugs 1/2): a patch loaded while the
                  // engine was off has every node still prepared with
                  // whatever rate it was constructed/last-rebuilt with, not
                  // the device's - this is the "press Start Audio" repro in
                  // 01-audio-lifecycle-correctness.md. Route through the one
                  // choke point instead of AudioEngine::Instance().Start()
                  // directly so this can't reopen at some future call site.
                  if (!StartAudioEngine(gAudioStartError))
                     fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
               }
            }
            ImGui::PopStyleColor();
            if (!audioOn && !gAudioStartError.empty() && ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", gAudioStartError.c_str());
         }

         ImGui::Separator();

         // Tempo: a plain text-entry field (not a slider) so typing an exact
         // BPM is a direct click-and-type rather than a click-drag gesture.
         float bpm = transport.Tempo();
         ImGui::AlignTextToFramePadding();
         ImGui::TextUnformatted("BPM");
         ImGui::SameLine(0.0f, 4.0f);
         ImGui::SetNextItemWidth(52);
         if (ImGui::InputFloat("##bpm", &bpm, 0.0f, 0.0f, "%.1f"))
            transport.SetTempo(std::clamp(bpm, 20.0f, 300.0f));

         ImGui::Separator();

         // Time signature (docs/plans/audio/P3c-P3a2-design.md §0.2): every
         // bar-relative rate (Note Sequencer length, arp retrigger-on-bar,
         // euclidean rotation) reads Transport::BeatsPerBar(), so this is the
         // one place that controls all of them at once.
         {
            int tsNum = transport.TimeSigNumerator();
            const int tsDen = transport.TimeSigDenominator();
            ImGui::SetNextItemWidth(36);
            if (ImGui::InputInt("##tsNum", &tsNum, 0, 0))
               transport.SetTimeSignature(tsNum, tsDen);
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextUnformatted("/");
            ImGui::SameLine(0.0f, 2.0f);
            static const int kDens[] = { 1, 2, 4, 8, 16 };
            int denIdx = 2;
            for (int i = 0; i < 5; i++)
               if (kDens[i] == tsDen)
                  denIdx = i;
            char denLabel[4];
            snprintf(denLabel, sizeof(denLabel), "%d", tsDen);
            ImGui::SetNextItemWidth(44);
            if (ImGui::BeginCombo("##tsDen", denLabel))
            {
               for (int i = 0; i < 5; i++)
               {
                  char opt[4];
                  snprintf(opt, sizeof(opt), "%d", kDens[i]);
                  if (ImGui::Selectable(opt, i == denIdx))
                     transport.SetTimeSignature(tsNum, kDens[i]);
               }
               ImGui::EndCombo();
            }
         }

         ImGui::Separator();

         // Global Key and Scale
         {
            static const char* const kKeyNames[] = {
               "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            int curKey = transport.Key();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("KEY");
            ImGui::SameLine(0.0f, 4.0f);
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

            ImGui::SameLine(0.0f, 6.0f);
            int curScale = transport.Scale();
            const auto& scaleList = MusicTime::ScaleTypeList();
            const std::string curScaleName = DisplayName(
               (curScale >= 0 && curScale < (int)scaleList.size()) ? scaleList[curScale] : "major");
            ImGui::SetNextItemWidth(110);
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
         }

         ImGui::Separator();

         ImGui::TextDisabled("BAR %d  BEAT %.2f",
                             1 + (int)transport.Bars(),
                             std::fmod(transport.Beats(), transport.BeatsPerBar()) + 1.0);

         // Frame cost, pinned right. Measured from the swap-to-swap wall clock
         // rather than ImGui's smoothed rate, so a heavy patch shows its real
         // cost immediately instead of easing into it over a second.
         static double sSmoothedMs = 0.0;
         // A gentle EMA: raw frame times jitter too much to read, but the
         // window is short enough that dragging a slider shows up at once.
         sSmoothedMs = (sSmoothedMs <= 0.0)
                          ? gLastFrameMs
                          : sSmoothedMs * 0.9 + gLastFrameMs * 0.1;
         const double fps = sSmoothedMs > 0.0001 ? 1000.0 / sSmoothedMs : 0.0;

         char readout[80];
         if (gTargetFps > 0)
            snprintf(readout, sizeof(readout), "%.1f / %d FPS   %.1f MS",
                     fps, gTargetFps, sSmoothedMs);
         else
            snprintf(readout, sizeof(readout), "%.1f FPS   %.1f MS", fps, sSmoothedMs);

         // Reserve space using a worst-case template rather than the live
         // string's width, so the search button doesn't jitter as the digit
         // count of the fps/ms readout changes frame to frame.
         char readoutTemplate[80];
         if (gTargetFps > 0)
            snprintf(readoutTemplate, sizeof(readoutTemplate), "888.8 / %d FPS   888.8 MS", gTargetFps);
         else
            snprintf(readoutTemplate, sizeof(readoutTemplate), "888.8 FPS   888.8 MS");
         const float reservedWidth = ImGui::CalcTextSize(readoutTemplate).x;
         const float readoutX = ImGui::GetWindowWidth() - reservedWidth - ImGui::GetStyle().WindowPadding.x * 2.0f;

         // Audio engine load, pinned left of the search button. Same
         // green/yellow/red judgment as fps below: under 70% of the block's
         // real-time budget is fine, past 90% is where xruns start. Distinct
         // from the fps number - a heavy patch can pin the render loop
         // without the audio thread being anywhere near its deadline, or
         // vice versa, since they run on separate threads.
         const bool audioEngineOn = AudioEngine::Instance().SampleRate() > 0.0;
         const double audioLoad = AudioEngine::Instance().LastBlockLoad();
         const uint64_t xruns = AudioEngine::Instance().XrunCount();
         // "on but dead" is real, brief window: PollAudioRecovery polls once
         // a frame and needs at least one attempt (rate-limited to
         // kAudioRecoveryMinIntervalMs apart) to either recover or give up
         // and flip audioEngineOn back to false itself - see its comment.
         // Reading IsAlive() here rather than adding a parallel "is
         // recovering" flag means this can't drift out of sync with what
         // PollAudioRecovery actually decided.
         const bool audioDead = audioEngineOn && !AudioEngine::Instance().IsAlive();
         char audioReadout[64];
         if (audioDead)
            snprintf(audioReadout, sizeof(audioReadout), "AUDIO LOST - RECONNECTING");
         else if (audioEngineOn)
            snprintf(audioReadout, sizeof(audioReadout), "AUDIO %.0f%%%s",
                     audioLoad * 100.0, xruns > 0 ? " !" : "");
         else
            snprintf(audioReadout, sizeof(audioReadout), "AUDIO OFF");
         const float audioWidth = ImGui::CalcTextSize("AUDIO LOST - RECONNECTING").x;

         Platform::GpuStats gpuStats;
         const bool gpuAvailable = Platform::ReadGpuStats(gpuStats);
         char gpuReadout[64];
         if (gpuAvailable)
            snprintf(gpuReadout, sizeof(gpuReadout), "GPU %.0f%%  VRAM %.0f%%",
                     gpuStats.gpuPercent, gpuStats.memoryPercent);
         else
            snprintf(gpuReadout, sizeof(gpuReadout), "GPU N/A");
         const float gpuWidth = ImGui::CalcTextSize("GPU 100%  VRAM 100%").x;

         const char* searchLabel = "PANEL";
         const float searchWidth = ImGui::CalcTextSize(searchLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
         const float itemGap = ImGui::GetStyle().ItemSpacing.x * 4.0f;
         const float searchX = readoutX - searchWidth - itemGap;
         // Turbo 0.46: PERF (Performance Mode) and CLICK (metronome) toggles.
         const float smallGap = ImGui::GetStyle().ItemSpacing.x;
         const float perfBtnWidth = ImGui::CalcTextSize("PERF").x + ImGui::GetStyle().FramePadding.x * 2.0f;
         const float clickBtnWidth = ImGui::CalcTextSize("CLICK").x + ImGui::GetStyle().FramePadding.x * 2.0f;
         const float perfBtnX = searchX - perfBtnWidth - smallGap;
         const float clickBtnX = perfBtnX - clickBtnWidth - smallGap;
         // Turbo 0.47: CLAUDE (chat) left of CLICK.
         const float chatBtnWidth = ImGui::CalcTextSize("CLAUDE").x + ImGui::GetStyle().FramePadding.x * 2.0f;
         const float chatBtnX = clickBtnX - chatBtnWidth - smallGap;
         float updateBadgeW = 0.0f; // Turbo 0.46: room for the UPDATE badge
         if (TurboUpdate::gStatus.load() == 3)
         {
            std::lock_guard<std::mutex> lock(TurboUpdate::gMutex);
            updateBadgeW = ImGui::CalcTextSize(("UPDATE " + TurboUpdate::gLatest).c_str()).x +
                           ImGui::GetStyle().FramePadding.x * 2.0f + smallGap;
         }
         const float gpuX = chatBtnX - updateBadgeW - gpuWidth - itemGap;
         const float audioX = gpuX - audioWidth - itemGap;

         {
            const ImVec4 audioColor = !audioEngineOn ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                     : audioDead ? ImVec4(0.95f, 0.45f, 0.4f, 1.0f)
                                     : (audioLoad < 0.7) ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                                     : (audioLoad < 0.9) ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                                          : ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
            ImGui::SameLine(audioX);
            ImGui::TextColored(audioColor, "%s", audioReadout);
            if (audioEngineOn && xruns > 0 && ImGui::IsItemHovered())
               ImGui::SetTooltip("%llu buffer underrun%s detected this session",
                                  (unsigned long long)xruns, xruns == 1 ? "" : "s");
         }

         {
            const float pressure = std::max(gpuStats.gpuPercent, gpuStats.memoryPercent) / 100.0f;
            const ImVec4 gpuColor = !gpuAvailable ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                    : pressure < 0.7f ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                                    : pressure < 0.9f ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                                      : ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
            ImGui::SameLine(gpuX);
            ImGui::TextColored(gpuColor, "%s", gpuReadout);
            if (gpuAvailable && ImGui::IsItemHovered())
            {
               const double usedMiB = (double)gpuStats.memoryUsedBytes / (1024.0 * 1024.0);
               const double totalMiB = (double)gpuStats.memoryTotalBytes / (1024.0 * 1024.0);
               ImGui::SetTooltip("%s\nVRAM %.0f / %.0f MiB",
                                 gpuStats.name.empty() ? "NVIDIA GPU" : gpuStats.name.c_str(), usedMiB, totalMiB);
            }
         }

         {
            // Sits left of the frame readout with a bit of breathing room,
            // so it reads as its own control rather than glued to the fps text.
            // Turbo 0.46: newer release badge, left of the toggles.
            if (TurboUpdate::gStatus.load() == 3)
            {
               std::string latest, url;
               {
                  std::lock_guard<std::mutex> lock(TurboUpdate::gMutex);
                  latest = TurboUpdate::gLatest;
                  url = TurboUpdate::gUrl;
               }
               const std::string badge = "UPDATE " + latest;
               const float badgeW = ImGui::CalcTextSize(badge.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
               ImGui::SameLine(chatBtnX - badgeW - smallGap);
               ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
               if (ImGui::Button(badge.c_str()))
                  Platform::OpenUrl(url);
               ImGui::PopStyleColor();
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("Infinite-Turbo %s is out (you have %s): opens the release page",
                                    latest.c_str(), INFINITE_TURBO_VERSION);
            }
            {
               // Metronome: lit on every beat while it plays (accent on the 1).
               const bool clickOn = AudioEngine::Instance().TopBarClick();
               Transport& tr = Transport::Instance();
               const double beat = tr.Beats();
               const double frac = beat - std::floor(beat);
               const bool flash = clickOn && tr.IsPlaying() && frac < 0.18;
               const bool downbeat = flash && std::fmod(std::floor(beat), std::max(1.0, (double)tr.BeatsPerBar())) < 0.5;
               ImGui::SameLine(chatBtnX);
               {
                  const bool chatWasOpen = gChatOpen;
                  if (chatWasOpen)
                     ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                  if (ImGui::Button("CLAUDE"))
                  {
                     gChatOpen = !gChatOpen;
                     gChatFocusInput = gChatOpen;
                  }
                  if (chatWasOpen)
                     ImGui::PopStyleColor();
                  if (ImGui::IsItemHovered())
                     ImGui::SetTooltip("chat with Claude about this patch (Shift+C); uses your Claude Code login");
               }
               ImGui::SameLine(clickBtnX);
               const int pushed = clickOn ? 1 : 0;
               if (clickOn)
                  ImGui::PushStyleColor(ImGuiCol_Button, flash ? (downbeat ? ImVec4(0.95f, 0.55f, 0.20f, 1.0f)
                                                                          : ImVec4(0.30f, 0.65f, 0.95f, 1.0f))
                                                               : ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
               // Turbo 0.48: a drawn metronome instead of the "CLICK" text (as
               // upstream), same width so the top-bar layout math is unchanged.
               if (ImGui::Button("##clickBtn", ImVec2(clickBtnWidth, 0.0f)))
                  AudioEngine::Instance().SetTopBarClick(!clickOn);
               if (pushed)
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
                  const bool swinging = clickOn && tr.IsPlaying();
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
               ImGui::SameLine(perfBtnX);
               if (gPerfPanelOpen)
                  ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
               const bool perfWasOpen = gPerfPanelOpen;
               if (ImGui::Button("PERF"))
                  gPerfPanelOpen = !gPerfPanelOpen;
               if (perfWasOpen)
                  ImGui::PopStyleColor();
               if (ImGui::IsItemHovered())
                  ImGui::SetTooltip("Performance mode: a panel of controls bound to any parameter (Shift+P)");
            }
            ImGui::SameLine(searchX);
            if (gNodePanelOpen)
               ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(searchLabel))
               gNodePanelOpen = !gNodePanelOpen;
            if (gNodePanelOpen)
               ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("show / hide the side panel: modules, samples, media, plugins (Ctrl+B)");
         }

         ImGui::SameLine(readoutX);

         // Judged against the target when there is one, so hitting a
         // deliberate 30fps cap reads as green rather than as a problem.
         // Otherwise against 50/25, where dragging a slider stops feeling live.
         const double good = gTargetFps > 0 ? gTargetFps * 0.95 : 50.0;
         const double poor = gTargetFps > 0 ? gTargetFps * 0.5 : 25.0;
         const ImVec4 color = (fps >= good) ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f)
                            : (fps >= poor) ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f)
                                            : ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
         ImGui::TextColored(color, "%s", readout);

         ImGui::EndMenuBar();
      }

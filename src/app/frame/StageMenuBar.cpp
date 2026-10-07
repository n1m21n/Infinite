// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawMenuBar(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& vp = fc.vp;


      // ---------------- node editor ----------------
      vp = ImGui::GetMainViewport();
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
      }}
}

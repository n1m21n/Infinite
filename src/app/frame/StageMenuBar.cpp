// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/GlyphDraw.h"
#include "app/ui/design/components/IconTile.h"
#include "app/ui/design/components/Readout.h"
#include "app/ui/design/components/TopBarParts.h"
#include "app/ui/design/components/Divider.h"
#include "app/ui/design/TokenColors.h"
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
      // The bar is tok::bar_h tall: ImGui sizes it from FramePadding.y at Begin.
      const float barPadY = (tok::bar_h - ImGui::GetFontSize()) * 0.5f;
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, barPadY));
      ImGui::Begin("Infinite", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PopStyleVar(2);

      // BeginMenuBar aligns text to FramePadding.y, so the bar's padding is what
      // centres the File/Edit/Menu labels; popped at once so no menu popup inherits it.
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, barPadY));
      const bool menuBarOpen = ImGui::BeginMenuBar();
      ImGui::PopStyleVar();
      if (menuBarOpen)
      {
         auto& MenuTile = TopBarParts::MenuTile;
         if (MenuTile(L("File")))
         {
            if (ImGui::MenuItem(L("New"), MODKEY "+N"))
               GuardUnsavedChanges([]() { NewPatch(); });
            if (ImGui::MenuItem(L("Open..."), MODKEY "+O"))
            {
               const std::string path = Platform::OpenPatchDialog();
               if (!path.empty())
                  GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
            }

            if (ImGui::BeginMenu(L("Open Recent"), !Patch::Recents().empty()))
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
            if (ImGui::MenuItem(L("Save"), MODKEY "+S"))
               SavePatchInteractive(false);
            if (ImGui::MenuItem(L("Save As..."), MODKEY "+Shift+S"))
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

         if (MenuTile(L("Edit")))
         {
            if (ImGui::MenuItem(L("Undo"), MODKEY "+Z", false, !gUndoStack.empty()))
               Undo();
            if (ImGui::MenuItem(L("Redo"), MODKEY "+Shift+Z", false, !gRedoStack.empty()))
               Redo();
            ImGui::Separator();
            if (ImGui::MenuItem(L("Cut / Copy"), MODKEY "+C"))
               gRequestCopy = true;
            if (ImGui::MenuItem(L("Paste"), MODKEY "+V"))
               gRequestPaste = true;
            if (ImGui::MenuItem(L("Duplicate"), MODKEY "+D"))
               gRequestDuplicate = true;
            if (ImGui::MenuItem(L("Delete"), "Backspace"))
               gRequestDelete = true;
            ImGui::Separator();
            if (ImGui::MenuItem(L("Select All"), "Shift+A"))
               gRequestSelectAll = true;
            if (ImGui::MenuItem(L("Bypass selection"), "B"))
               gRequestBypass = true;
            ImGui::Separator();
            if (ImGui::MenuItem(L("Group selection"), MODKEY "+G"))
               gRequestGroup = true;
            if (ImGui::MenuItem(L("Ungroup"), MODKEY "+U"))
               gRequestUngroup = true;
            ImGui::Separator();
            if (ImGui::MenuItem(L("Add Node..."), "Shift+N"))
               gRequestAddNode = true;
            if (ImGui::MenuItem(L("Add Note"), "/"))
               gRequestAddComment = true;
            ImGui::EndMenu();
         }

         if (MenuTile(L("Menu")))
         {
            if (ImGui::MenuItem(L("Settings..."), MODKEY "+0"))
               gSettingsOpen = true;

            ImGui::Separator();

            if (ImGui::BeginMenu(L("Viewport panel")))
            {
               ImGui::Checkbox(L("Show viewport panel"), &gViewportPanelOpen);
               if (gViewportPanelOpen)
               {
                  ImGui::SetNextItemWidth(150);
                  ViewportPanelDockCombo();
                  ImGui::SetNextItemWidth(150);
                  if (gViewportPanelDock == 1 || gViewportPanelDock == 2)
                     ImGui::SliderFloat(L("Width"), &gViewportPanelWidth,
                                        kViewportPanelMinWidth, 900.0f, "%.0f px");
                  else
                     ImGui::SliderFloat(L("Height"), &gViewportPanelHeight,
                                        kViewportPanelMinHeight, 800.0f, "%.0f px");
                  ImGui::Separator();
                  if (!gViewportPanelNodes.empty() && ImGui::MenuItem(L("Clear cards")))
                     gViewportPanelNodes.clear();
                  if (ImGui::MenuItem(L("Close viewport panel")))
                     gViewportPanelOpen = false;
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(L("Modulation matrix")))
            {
               // Plain themed widgets, same as "Viewport panel" above - this
               // is menu chrome, not a node body, so it takes the app's own
               // checkbox/slider colours (ApplyTheme) rather than the P10
               // dark-contrast-budget style meant for controls inside a node.
               // The two styles side by side in one menu (one purple/clean,
               // one flat blue) is what read as inconsistent.
               ImGui::Checkbox(L("Show modulation matrix"), &gModMatrixOpen);
               if (gModMatrixOpen)
               {
                  ImGui::SetNextItemWidth(150);
                  ModMatrixDockCombo();
                  ImGui::SetNextItemWidth(150);
                  if (gModMatrixDock == 1 || gModMatrixDock == 2)
                     ImGui::SliderFloat(L("Width"), &gModMatrixWidth,
                                        kModMatrixMinWidth, 900.0f, "%.0f px");
                  else
                     ImGui::SliderFloat(L("Height"), &gModMatrixHeight,
                                        kModMatrixMinHeight, 800.0f, "%.0f px");
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(L("Performance Matrix")))
            {
               // Same reasoning as "Modulation matrix" above: plain themed
               // widgets, not the node-body P10 style.
               ImGui::Checkbox(L("Show Performance Matrix"), &gPerfPanelOpen);
               if (gPerfPanelOpen)
               {
                  ImGui::SetNextItemWidth(150);
                  PerfPanelDockCombo();
                  ImGui::SetNextItemWidth(150);
                  if (gPerfPanelDock == 1 || gPerfPanelDock == 2)
                     ImGui::SliderFloat(L("Width"), &gPerfPanelWidth,
                                        kPerfPanelMinWidth, 900.0f, "%.0f px");
                  else
                     ImGui::SliderFloat(L("Height"), &gPerfPanelHeight,
                                        kPerfPanelMinHeight, 800.0f, "%.0f px");
                  ImGui::Checkbox(L("Edit Mode"), &gPerfEditMode);
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(L("Arrangement Timeline")))
            {
               ImGui::Checkbox(L("Show Arrangement Timeline"), &gArrangePanelOpen);
               if (gArrangePanelOpen)
               {
                  // Bottom or top only - a timeline reads left-to-right, so a
                  // side dock would fight the ruler's own horizontal axis.
                  // Saved with the document (Settings.dockSide); not undoable.
                  int dockSide = gArrange.settings.dockSide == 1 ? 1 : 0;
                  ImGui::SetNextItemWidth(150);
                  if (ImGui::Combo(L("Dock"), &dockSide, I18n::TList("Bottom\0Top\0")) && dockSide != gArrange.settings.dockSide)
                  {
                     gArrange.settings.dockSide = dockSide;
                     gArrange.revision++; // a model field like any other (WP5b)
                     gPatchDirty = true;
                  }
                  ImGui::SetNextItemWidth(150);
                  ImGui::SliderFloat(L("Height"), &gArrangePanelHeight,
                                     kArrangePanelMinHeight, 800.0f, "%.0f px");
               }
               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(L("Nodes")))
            {
               if (ImGui::MenuItem(L("Show all params")))
               {
                  for (GraphNode& gn : gNodes)
                     gn.showParams = true;
               }
               if (ImGui::MenuItem(L("Hide all params")))
               {
                  for (GraphNode& gn : gNodes)
                     gn.showParams = false;
               }
               ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(L("All shortcuts...")))
               gShortcutsOpen = true;
            if (ImGui::MenuItem(L("Help / module reference")))
               gHelpOpen = true;
#ifndef NDEBUG
            // ImGui's built-in inspectors, not a custom tool: the Debugger's
            // Tools > Item Picker names the exact ImGuiCol_*/style var and
            // rect behind whatever you click, and the Style Editor lists and
            // live-previews every one of those values - the fastest way to
            // hand back "this exact knob, this exact number" instead of a
            // screenshot and a guess. Dev-only: excluded from Release builds
            // (NDEBUG) so shipped/public builds never expose these.
            if (ImGui::MenuItem(L("UI Debugger / Item Picker"))) // i18n-ok (debug UI)
               gUiDebuggerOpen = true;
            if (ImGui::MenuItem(L("UI Style Editor"))) // i18n-ok (debug UI)
               gUiStyleEditorOpen = true;
#endif
            if (ImGui::MenuItem(L("Check for updates")))
            {
               UpdateCheck::Start();
               gShowUpdateCheckModal = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem(L("Quit")))
               RequestClose(window);
            ImGui::EndMenu();
         }

         Transport& transport = Transport::Instance();
         const bool isTransportPlaying = transport.IsPlaying();

         // Top bar controls styling: clean, symmetrical, unboxed with pixel-perfect alignment.
         ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tok::V4(tok::palf::v_1000_1000_1000_80));
         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tok::V4(tok::palf::v_1000_1000_1000_160));
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::V4(tok::palf::v_1000_1000_1000_80));
         ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::V4(tok::palf::v_1000_1000_1000_160));
         ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
         ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tok::radius_tile);
         // 28 pt controls (tok::tile) on one centre line; the cursor Y below pins them in the bar.
         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, (tok::tile - ImGui::GetFontSize()) * 0.5f));
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
         // A live number: tabular digits in a slot as wide as its widest value, so it never
         // shifts what sits beside it. `widest` is the longest text it can show.
         auto TabLabel = [](const char* key, const char* text, const char* widest, bool dim)
         {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            float w;
            {
               UiType::Scope ts(UiType::Size::Title, UiType::Weight::Regular);
               w = std::max(Readout::Measure(widest), Readout::Measure(text));
            }
            ImGui::Dummy(ImVec2(w, tok::tile));
            Readout::Draw(key, UiLayout::Rect { p.x, p.y, w, tok::tile }, text, text, UiType::Size::Title,
                          UiType::Weight::Regular, Readout::Align::Left, dim ? 0.5f : 1.0f);
         };

         auto& SectionBreak = TopBarParts::SectionBreak;
         // Everything after the menus sits on one centre line, tok::tile tall in a tok::bar_h bar.
         SectionBreak();

         // 1. Transport (Play, Rewind, Audio On/Off)
         if (isTransportPlaying)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, tok::V4(tok::palf::v_160_630_310_1000));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::V4(tok::palf::v_200_700_360_1000));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::V4(tok::palf::v_140_550_260_1000));
         }
         if (ImGui::Button("##transportplay", ImVec2(tok::tile + 4.0f, 0)))
            transport.TogglePlay();
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = tok::icon_md;
            const ImU32 col = isTransportPlaying
                                  ? tok::U32(tok::pal::c_FFFFFFFF)
                                  : ImGui::GetColorU32(ImGuiCol_Text);
            if (isTransportPlaying)
               glyph::DrawPlayerPause(dl, center, iconSize, col);
            else
               glyph::Draw(dl, center, iconSize, col, IconsInfinite::PlayFill);
         }
         if (ImGui::IsItemHovered())
            HelpTip(T("%s (Space)"), isTransportPlaying ? T("Pause") : T("Play"));
         if (isTransportPlaying)
            ImGui::PopStyleColor(3);

         TopBarSameLine(2.0f);
         if (ImGui::Button("##transportrewind", ImVec2(tok::tile + 4.0f, 0)))
            transport.Rewind();
         {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = tok::icon_md;
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            glyph::DrawPlayerRewind(dl, center, iconSize, col);
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Rewind (Return)"));

         // Transport group: play and rewind.

         SectionBreak();

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
                                                       ? (audioIsLight ? tok::V4(tok::palf::v_200_620_340_1000) : tok::V4(tok::palf::v_160_520_280_1000))
                                                       : (audioIsLight ? tok::V4(tok::palf::v_800_820_870_1000) : tok::V4(tok::palf::v_300_300_340_1000)));
            ImGui::PushStyleColor(ImGuiCol_Text, audioOn
                                                     ? tok::V4(tok::palf::v_1000_1000_1000_1000)
                                                     : (audioIsLight ? tok::V4(tok::palf::v_120_140_200_1000) : tok::V4(tok::palf::v_920_940_980_1000)));
            if (ImGui::Button(audioOn ? L("Stop Audio") : L("Start Audio")))
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
               const char* badge = T("Timeline");
               const ImVec2 textSize = ImGui::CalcTextSize(badge);
               const ImVec2 pad(6.0f, ImGui::GetStyle().FramePadding.y);
               const ImVec2 bmin = ImGui::GetCursorScreenPos();
               const ImVec2 bmax(bmin.x + textSize.x + pad.x * 2.0f, bmin.y + ImGui::GetFrameHeight());
               ImGui::InvisibleButton("##timelineAudioBadge", ImVec2(bmax.x - bmin.x, bmax.y - bmin.y));
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImU32 edge = audioIsLight ? tok::U32(tok::pal::c_288248FF) : tok::U32(tok::pal::c_60C884FF);
               dl->AddRect(bmin, bmax, edge, 3.0f, 0, 1.0f);
               dl->AddText(ImVec2(bmin.x + pad.x, bmin.y + pad.y), edge, badge);
               if (ImGui::IsItemHovered())
                  HelpTip(engineOn
                     ? T("The Arrangement Timeline is driving audio. Hand it back to the canvas from the timeline panel.")
                     : T("The Arrangement Timeline will drive audio once the engine is started."));
            }
         }


         SectionBreak();

         // Bar and beat lead the readout group: bar, beat, BPM, signature, key.
         {
            char barBeat[64];
            snprintf(barBeat, sizeof(barBeat), T("bar %d  beat %.2f"),
                     1 + (int)transport.Bars(),
                     std::fmod(transport.Beats(), transport.BeatsPerBar()) + 1.0);
            char barWidest[64];
            snprintf(barWidest, sizeof(barWidest), T("bar %d  beat %.2f"), 99, 9.99);
            TabLabel("topbar.barbeat", barBeat, barWidest, false);
            TopBarSameLine(12.0f);
         }

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
            TopBarLabel("BPM", true);
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
                  HelpTip("%s", T("Tempo - drag, double-click or type to change.\nArrangement Timeline clips keep their bar/beat positions:\na tempo change moves their times in seconds, not their bars."));
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

         // The click belongs with tempo and meter: BPM, signature, metronome.
         TopBarSameLine(tok::space_3);
         {
            // Same tile as the panel toggles: outline glyph off, accent tile + filled glyph on.
            if (IconTile::Draw("##metronomeBtn", IconsInfinite::Metronome, IconsInfinite::MetronomeFill, gMetronomeOn,
                               tok::tile, ImGui::GetFrameHeight()))
               gMetronomeOn = !gMetronomeOn;
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
               const bool volChanged = ImGui::SliderFloat(L("volume##metronomeVol"), &gMetronomeVolume, 0.0f, 1.0f, "%.2f");
               ImGui::PopStyleColor(3);
               if (volChanged)
                  gMetronomeDirty = true;
               if (ImGui::Selectable(L("accent first beat"), gMetronomeAccent, ImGuiSelectableFlags_DontClosePopups))
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

         SectionBreak();

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
            TopBarLabel("Key", true);
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

         SectionBreak();

         // 4. Telemetry (Bar & beat, frame cost, CPU load)
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
            snprintf(cpuReadout, sizeof(cpuReadout), "%s", T("cpu lost"));
         else if (audioEngineOn)
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu %.0f%%%s", audioLoad * 100.0, xruns > 0 ? " !" : "");
         else
            snprintf(cpuReadout, sizeof(cpuReadout), "cpu --");

         TabLabel("topbar.fps", readout, "99.9 fps   99.9 ms", true);
         TopBarSameLine(8.0f);
         TabLabel("topbar.cpu", cpuReadout, audioDead ? cpuReadout : "cpu 99%", true);

         if (audioEngineOn && xruns > 0 && ImGui::IsItemHovered())
            ImGui::SetTooltip(T("xruns=%llu this session\n%llu late block(s) (render over the deadline)\n%llu reported by the audio device"),
                              (unsigned long long)xruns,
                              (unsigned long long)xrunParts.deadline,
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
         const float itemGap = tok::space_2;
         const float minGap = 12.0f;
         float cursorX = windowRight;

         if (UpdateCheck::UpdateAvailable())
         {
            const char* updateLabel = T("Update");
            const float updateWidth = ImGui::CalcTextSize(updateLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (cursorX - updateWidth >= leftClusterEndX + minGap)
            {
               cursorX -= updateWidth;

               ImGui::SameLine(cursorX);
               ImGui::PushStyleColor(ImGuiCol_Button, tok::V4(tok::palf::v_200_620_340_1000));
               ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::V4(tok::palf::v_240_700_400_1000));
               ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::V4(tok::palf::v_160_520_280_1000));
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_1000_1000_1000_1000));
               if (ImGui::Button(updateLabel))
                  Platform::OpenExternalUrl("https://n1m21n.github.io/Infinite/#download");
               ImGui::PopStyleColor(4);
               if (ImGui::IsItemHovered())
               {
                  ImGui::SetTooltip(T("version %s is available (you have %s) - click to download"),
                                     UpdateCheck::LatestVersion().c_str(), INFINITE_VERSION_STRING);
               }
               if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                  UpdateCheck::Dismiss();
               cursorX -= itemGap;
            }
         }

         // Library: a labelled toggle, not a bare magnifier - it opens the panel that lists,
         // searches and adds every node, so it is named for what it holds. A hairline sets it
         // apart from the four icon-only panel toggles to its right.
         {
            const char* libLabel = T("Library");
            const float iconSize = tok::icon_md;
            const float iconSlot = iconSize + 6.0f;
            const float textW = ImGui::CalcTextSize(libLabel).x;
            const float totalW = iconSlot + textW + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (cursorX - totalW >= leftClusterEndX + minGap)
            {
               cursorX -= totalW;

               ImGui::SameLine(cursorX);
               const ImVec2 btnStart = ImGui::GetCursorScreenPos();
               const bool clicked = ImGui::InvisibleButton("##nodeLibrary", ImVec2(totalW, tok::tile));
               const bool hov = ImGui::IsItemHovered();
               const bool down = ImGui::IsItemActive();
               const ImGuiID aid = ImGui::GetItemID();
               const float hv = UiAnim::Hover(aid, hov, tok::motion_hover_in, tok::motion_hover_out);
               const float onv = UiAnim::Value(aid + 1, gNodePanelOpen ? 1.0f : 0.0f, gNodePanelOpen ? tok::motion_on : tok::motion_off);
               ImDrawList* dl = ImGui::GetWindowDrawList();
               const ImVec2 bmax(btnStart.x + totalW, btnStart.y + tok::tile);
               const ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
               const float overlay = (down ? 0.12f : 0.06f * hv) * (1.0f - onv);
               if (overlay > 0.001f)
                  dl->AddRectFilled(btnStart, bmax, ImGui::GetColorU32(ImVec4(tx.x, tx.y, tx.z, overlay)), tok::radius_tile);
               if (onv > 0.001f)
               {
                  ImVec4 acc = down ? AccentEmphasisPressed() : AccentEmphasisSelected();
                  acc.w = onv;
                  dl->AddRectFilled(btnStart, bmax, ImGui::GetColorU32(acc), tok::radius_tile);
               }
               const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
               glyph::Draw(dl, ImVec2(btnStart.x + ImGui::GetStyle().FramePadding.x + iconSize * 0.5f, btnStart.y + tok::tile * 0.5f),
                           iconSize, col, gNodePanelOpen ? IconsInfinite::LibraryFill : IconsInfinite::Library);
               dl->AddText(ImVec2(btnStart.x + ImGui::GetStyle().FramePadding.x + iconSlot, btnStart.y + (tok::tile - ImGui::GetTextLineHeight()) * 0.5f), col, libLabel);
               if (hov)
                  HelpTip("%s", T("Library - search modules, samples, media and plugins"));
               if (clicked)
                  gNodePanelOpen = !gNodePanelOpen;
               cursorX -= tok::space_3;
               Divider::Vertical(UiLayout::Rect { cursorX - 1.0f, btnStart.y, 1.0f, tok::tile }, 4.0f);
               cursorX -= tok::space_3;
            }
         }

         // Icon-only toggle buttons for Viewport / Modulation matrix /
         // Performance mode - a little larger than the transport play/
         // rewind buttons (38px, icon at 88% of the row height) since these
         // carry no text label to help them read at a glance.
         // Returns false without drawing anything when there isn't room -
         // these are the first things dropped on a narrow window, since they
         // carry no text label and are the least essential of the cluster.
         // The four toggles spread evenly across the Library panel's tab row: the span runs from the panel's
         // inner left edge (kNodePanelWidth minus its 16 pt gap and padding each side) to the hairline. Falls back to a
         // tight pack when the window is too narrow for that span.
         float iconGap = tok::space_1;
         {
            const float regionLeft = windowRight - (fc.kNodePanelWidth - 32.0f);
            const float even = (cursorX - regionLeft - 4.0f * tok::tile) / 3.0f;
            if (regionLeft >= leftClusterEndX + minGap && even > tok::space_1)
               iconGap = even;
         }
         auto TopBarIconToggle = [&](const char* id, bool isOpen, void (*draw)(ImDrawList*, ImVec2, float, ImU32, float), const char* tooltip, const char* glyphOff = nullptr, const char* glyphOn = nullptr)
         {
            const float btnW = tok::tile;
            if (cursorX - btnW < leftClusterEndX + minGap)
               return false;
            cursorX -= btnW;
            ImGui::SameLine(cursorX);

            if (glyphOff != nullptr)
            {
               const bool clicked = IconTile::Draw(id, glyphOff, glyphOn, isOpen, 28.0f, ImGui::GetFrameHeight());
               if (ImGui::IsItemHovered())
                  HelpTip("%s", tooltip);
               cursorX -= iconGap;
               return clicked;
            }
            if (isOpen)
               PushSelectedButtonColors();
            const bool clicked = ImGui::Button(id, ImVec2(btnW, 0.0f));
            if (isOpen)
               PopSelectedButtonColors();
            if (ImGui::IsItemHovered())
               HelpTip("%s", tooltip);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = tok::icon_md;
            const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            if (glyphOff != nullptr)
               glyph::Draw(dl, center, iconSize, col, (isOpen && glyphOn != nullptr) ? glyphOn : glyphOff);
            else if (draw != nullptr)
               draw(dl, center, iconSize, col, 0.0f);
            else
               glyph::DrawPlaceholder(dl, center, iconSize, col, 0.0f);

            cursorX -= iconGap;
            return clicked;
         };

         if (TopBarIconToggle("##arrangePanelToggle", gArrangePanelOpen, nullptr, T("Arrangement timeline"), IconsInfinite::Cube))
            gArrangePanelOpen = !gArrangePanelOpen;
         if (TopBarIconToggle("##perfPanelToggle", gPerfPanelOpen, &glyph::DrawDisc, T("Performance mode")))
            gPerfPanelOpen = !gPerfPanelOpen;
         if (TopBarIconToggle("##modMatrixToggle", gModMatrixOpen, nullptr, T("Modulation matrix"), IconsInfinite::GridDots))
            gModMatrixOpen = !gModMatrixOpen;
         if (TopBarIconToggle("##viewportPanelToggle", gViewportPanelOpen, nullptr, T("Viewport panel"), IconsInfinite::Viewport, IconsInfinite::ViewportFill))
            gViewportPanelOpen = !gViewportPanelOpen;

         ImGui::PopStyleColor(6);
         ImGui::PopStyleVar(4);

         ImGui::EndMenuBar();
      }}
}

// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/ui/design/TokenColors.h"
#include "app/frame/FrameCtx.h"

namespace app
{
int DrawFloating(FrameCtx& fc)
{
   ImGuiStyle& style = ImGui::GetStyle();
   auto& window = fc.window;
   auto& frameId = fc.frameId;
   auto& isBenchB3 = fc.isBenchB3;
   auto& benchStagesSample = fc.benchStagesSample;
   auto& benchStagesCpuSample = fc.benchStagesCpuSample;
   auto& benchGpuPerNode = fc.benchGpuPerNode;
   auto& vp = fc.vp;


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
            ImGui::TextDisabled("%s", T("Live-editing this ImGuiStyle for inspection only - not saved."));
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
            ImGui::TextUnformatted(T("File changed on disk."));
            ImGui::SameLine();
            if (ImGui::Button(L("Reload")))
            {
               if (LoadPatchFromImpl(gPatchWatchPath, true))
                  gPatchChangedOnDisk = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("Keep mine")))
               gPatchChangedOnDisk = false;
         }
         ImGui::End();
      }

      if (gShowUnsavedChangesModal)
      {
         ImGui::OpenPopup(L("Unsaved Changes"));
         gShowUnsavedChangesModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal(L("Unsaved Changes"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("%s", T("This patch has unsaved changes."));
         ImGui::Text("%s", T("Save before closing?"));
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
         if (ImGui::Button(L("Cancel"), ImVec2(btnW, 0)))
         {
            gPendingUnsavedAction = nullptr;
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button(L("Don't Save"), ImVec2(btnW, 0)))
         {
            if (gPendingUnsavedAction)
               gPendingUnsavedAction();
            gPendingUnsavedAction = nullptr;
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         PushPrimaryButtonStyle();
         const bool doSave = ImGui::Button(L("Save"), ImVec2(btnW, 0));
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
         ImGui::OpenPopup(L("Check for updates"));
         gShowUpdateCheckModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
      bool isUpdateCheckOpen = true;
      if (ImGui::BeginPopupModal(L("Check for updates"), &isUpdateCheckOpen, ImGuiWindowFlags_AlwaysAutoResize))
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
               ImGui::Text(T("Checking for updates%.*s"), dots, "...");
               break;
            }
            case UpdateCheck::Status::UpToDate:
               ImGui::Text(T("You're running the latest version (%s)."), INFINITE_VERSION_STRING);
               break;
            case UpdateCheck::Status::UpdateAvailable:
               ImGui::Text(T("Version %s is available (you have %s)."),
                           UpdateCheck::ResultVersion().c_str(), INFINITE_VERSION_STRING);
               break;
            case UpdateCheck::Status::Failed:
               ImGui::PushStyleColor(ImGuiCol_Text, tok::V4(tok::palf::v_950_450_400_1000));
               ImGui::TextWrapped("%s", UpdateCheck::LastError().c_str());
               ImGui::PopStyleColor();
               break;
         }

         ImGui::Separator();

         if (status == UpdateCheck::Status::UpdateAvailable)
         {
            PushPrimaryButtonStyle();
            const bool doDownload = ImGui::Button(L("Download latest version"));
            PopPrimaryButtonStyle();
            if (doDownload)
               Platform::OpenExternalUrl(UpdateCheck::DownloadUrl());
            ImGui::SameLine();
            if (ImGui::Button(L("Later")))
               ImGui::CloseCurrentPopup();
         }
         else if (status == UpdateCheck::Status::Failed)
         {
            PushPrimaryButtonStyle();
            const bool doRetry = ImGui::Button(L("Retry"));
            PopPrimaryButtonStyle();
            if (doRetry)
               UpdateCheck::Start();
            ImGui::SameLine();
            if (ImGui::Button(L("Close")))
               ImGui::CloseCurrentPopup();
         }
         else if (status == UpdateCheck::Status::UpToDate)
         {
            if (ImGui::Button(L("Close")))
               ImGui::CloseCurrentPopup();
         }
         // Idle/Checking: no buttons yet, just wait for Poll() to land a result.

         ImGui::EndPopup();
      }

      if (gShowAutosaveRecoveryModal)
      {
         ImGui::OpenPopup(L("Recover Autosave"));
         gShowAutosaveRecoveryModal = false;
      }
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal(L("Recover Autosave"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::Text("%s", T("Infinite closed unexpectedly."));
         if (!gAutosaveRecoveryTimestamp.empty())
            ImGui::Text(T("A recovered version of your work from %s is available."),
                        gAutosaveRecoveryTimestamp.c_str());
         else
            ImGui::Text("%s", T("A recovered version of your work is available."));
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
         if (ImGui::Button(L("Discard"), ImVec2(100, 0)))
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
         const bool doRecover = ImGui::Button(L("Recover"), ImVec2(100, 0));
         PopPrimaryButtonStyle();
         if (doRecover)
         {
            ApplyPatchData(gPendingRecoveryData);
            gArrangePatchGeneration++; // a new document, same as File > Open
            gUndoStack.clear();
            gRedoStack.clear();
            gPatchPath.clear();          // it is not the user's file - force Save As
            gPatchDirty = true;          // it is unsaved work, and should say so
            gPatchStatus = T("Recovered autosave. Save the project to keep it.");
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
               else if (auto* ndiOut = dynamic_cast<NdiOutNode*>(gn.node.get()))
                  ndiOut->Withdraw();
               continue;
            }
            if (dynamic_cast<OutputNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<SyphonOutNode*>(gn.node.get()) != nullptr ||
                dynamic_cast<NdiOutNode*>(gn.node.get()) != nullptr ||
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
         return -1;
}
}

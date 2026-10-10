// Per-frame self-test blocks moved verbatim out of the main loop in main.cpp.
#include "app/AppShared.h"

namespace app
{

void FrameTest_INPUTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_INPUTTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         auto key = [&tio](ImGuiKey k, bool down) { tio.AddKeyEvent(k, down); };
         switch (frameId)
         {
            case 3: key(ImGuiMod_Super, true); key(ImGuiKey_C, true); break;
            case 4: key(ImGuiKey_C, false); break;
            case 5: key(ImGuiKey_V, true); break;
            case 6: key(ImGuiKey_V, false); key(ImGuiMod_Super, false); break;
            case 8: key(ImGuiKey_Backspace, true); break;
            case 9: key(ImGuiKey_Backspace, false); break;
            default: break;
         }
      }
}

void FrameTest_COLORTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true); // headless runs are never OS-focused; ImGui drops input otherwise
         if (frameId >= 8 && gColorPickerRect.z > 0.0f)
            tio.AddMousePosEvent(gColorPickerRect.x + gColorPickerRect.z * 0.25f,
                                 gColorPickerRect.y + gColorPickerRect.w * 0.30f);
         if (frameId == 9)
            tio.AddMouseButtonEvent(0, true);
         if (frameId == 11)
            tio.AddMouseButtonEvent(0, false);
      }
}

void FrameTest_COMMENTTEST(int frameId, GLFWwindow* window)
{
   if (const char* cmode = getenv("INFINITE_COMMENTTEST"))
      {
         if (std::string(cmode) == "slash")
         {
            ImGuiIO& tio = ImGui::GetIO();
            tio.ConfigInputTrickleEventQueue = false;
            tio.AddFocusEvent(true); // headless runs are never OS-focused
            if (frameId == 4)
            {
               tio.AddKeyEvent(ImGuiKey_Slash, true);
               tio.AddInputCharacter('/'); // a real keyboard sends both
            }
            if (frameId == 5)
               tio.AddKeyEvent(ImGuiKey_Slash, false);
            // Typing starts immediately after: no click anywhere in between.
            if (frameId == 7)
            {
               for (char ch : std::string("lighting"))
                  tio.AddInputCharacter(ch);
            }
            if (frameId == 9)
               tio.AddKeyEvent(ImGuiKey_Enter, true);
            if (frameId == 10)
               tio.AddKeyEvent(ImGuiKey_Enter, false);
            if (frameId == 12)
            {
               for (char ch : std::string("rim light too hot"))
                  tio.AddInputCharacter(ch);
            }
         }
      }
}

void FrameTest_COMMENTTEST_2(int frameId, GLFWwindow* window)
{
   if (const char* cmode = getenv("INFINITE_COMMENTTEST"))
      {
         if (std::string(cmode) == "edit")
         {
            ImGuiIO& tio = ImGui::GetIO();
            tio.ConfigInputTrickleEventQueue = false;
            tio.AddFocusEvent(true); // headless runs are never OS-focused
            if (frameId >= 4 && gCommentBodyRect.z > 0.0f)
               tio.AddMousePosEvent(gCommentBodyRect.x + gCommentBodyRect.z * 0.5f,
                                    gCommentBodyRect.y + gCommentBodyRect.w * 0.5f);
            // Two clicks a few frames apart: at 60fps that is well inside
            // ImGui's double-click window.
            if (frameId == 5 || frameId == 7)
               tio.AddMouseButtonEvent(0, true);
            if (frameId == 6 || frameId == 8)
               tio.AddMouseButtonEvent(0, false);
            // Then type, including a Return: a note whose editor cannot add a
            // line is no better than the single-line field this replaced. Left
            // a few frames after the click so the mouse has finished with the
            // node - while the button is still down the editor holds the active
            // item and the text field cannot take the keyboard.
            if (frameId == 14)
               tio.AddInputCharacter('!');
            if (frameId == 16)
               tio.AddKeyEvent(ImGuiKey_Enter, true);
            if (frameId == 17)
               tio.AddKeyEvent(ImGuiKey_Enter, false);
         }
      }
}

void FrameTest_WTDRAGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_WTDRAGTEST") != nullptr && gWtTestScreen.size() >= 8)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true);
         static bool sFocused = false;
         if (!sFocused) { glfwFocusWindow(window); sFocused = true; }
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };
         auto* wt = static_cast<WavetableNode*>(gNodes[0].node.get());

         const ImVec4 framesA = gWtTestScreen[0];
         const ImVec4 ampA = gWtTestScreen[1];
         const ImVec4 filtB = gWtTestScreen[7]; // engine B's filter envelope
         const float framesCy = (framesA.y + framesA.w) * 0.5f;

         // Where DrawEditableADSR puts the handles, computed from ComputeADSRLayout
         ADSRLayout ampL = ComputeADSRLayout(ImVec2(ampA.x, ampA.y), ampA.z - ampA.x, ampA.w - ampA.y,
                                             wt->engines[0].ampAttack, wt->engines[0].ampDecay,
                                             wt->engines[0].ampSustain, wt->engines[0].ampRelease);
         const float susX = ampL.pD.x;
         const float susY = ampL.pD.y;
         const float ampSpan = ampL.spanY;

         ADSRLayout fbL = ComputeADSRLayout(ImVec2(filtB.x, filtB.y), filtB.z - filtB.x, filtB.w - filtB.y,
                                            wt->engines[1].filterAttack, wt->engines[1].filterDecay,
                                            wt->engines[1].filterSustain, wt->engines[1].filterRelease);
         const float fbAttackX = fbL.pA.x;
         const float fbTopY = fbL.pA.y;
         const float fbSeg = fbL.wA;

         auto Aim = [](float sx, float sy) { return ImVec2(sx, sy); }; // already screen space
         const float fw = framesA.z - framesA.x;
         switch (frameId)
         {
            // --- phase 1: scrub engine A's table position ---
            case 54: gTestMouse = Aim(framesA.x + fw * 0.15f, framesCy); break;
            case 55: btn(true); break;
            case 56: gTestMouse = Aim(framesA.x + fw * 0.50f, framesCy); break;
            case 57: gTestMouse = Aim(framesA.x + fw * 0.80f, framesCy); break;
            case 58: btn(false); break;
            // --- phase 2: drag engine A's amp sustain handle upward ---
            case 62: gTestMouse = Aim(susX, susY); break;
            case 63: btn(true); break;
            case 64: gTestMouse = Aim(susX, susY - ampSpan * 0.25f); break;
            case 65: gTestMouse = Aim(susX, susY - ampSpan * 0.45f); break;
            case 66: btn(false); break;
            // --- phase 3: drag engine B's *filter* envelope attack sideways ---
            case 70: gTestMouse = Aim(fbAttackX, fbTopY); break;
            case 71: btn(true); break;
            case 72: gTestMouse = Aim(fbAttackX + 30.0f, fbTopY); break;
            case 73: gTestMouse = Aim(fbAttackX + 60.0f, fbTopY); break;
            case 74: btn(false); break;
            default: break;
         }
         if (frameId >= 54)
         {
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
            glfwSetCursorPos(window, (double)(gTestMouse.x * ImGui_ImplGlfw_GetPointScale()),
                             (double)(gTestMouse.y * ImGui_ImplGlfw_GetPointScale()));
         }
      }
}

void FrameTest_EDPERFTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_EDPERFTEST") != nullptr)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.AddMousePosEvent(800.0f, 500.0f);
      }
}

void FrameTest_EQDRAGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_EQDRAGTEST") != nullptr && !gNodes.empty())
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true); // headless runs are never OS-focused; ImGui drops hover/click otherwise
         static bool sFocused = false;
         if (!sFocused) { glfwFocusWindow(window); sFocused = true; }
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };

         const float x0 = gEqTestScreen.x, y0 = gEqTestScreen.y;
         const float x1 = gEqTestScreen.z, y1 = gEqTestScreen.w;
         const float w = x1 - x0, h = y1 - y0;

         // Recomputed every frame from the node's LIVE params (not the spawn
         // defaults) - phase 1 moves band3Freq, so phase 2's diamond, aimed
         // at band3's dot position plus an offset, would otherwise still be
         // targeting where band 3 *used to be* and miss it entirely.
         auto* eqLive = static_cast<AudioEffectNode*>(gNodes[0].node.get());
         const float band3Freq = eqLive->Param("band3Freq");
         const float band3Gain = eqLive->Param("band3Gain");
         const float band3X = FilterVizFreqToX(band3Freq, x0, w);
         const float band3Y = FilterVizDbToY(band3Gain, y0, h); // band3 is a peak type - UsesGain
         const float band2X = FilterVizFreqToX(eqLive->Param("band2Freq"), x0, w);
         const float band2Y = FilterVizDbToY(eqLive->Param("band2Gain"), y0, h); // band2 is a peak type too

         // Frames 4-30ish are spent waiting for the fixture's initial
         // zoom-to-fit/frame-all camera animation to settle - a synthetic
         // click aimed any earlier lands using a rect that's still visibly
         // drifting frame to frame, not just the usual one-frame conversion
         // lag WTDRAGTEST's own comment accepts for a static node.
         switch (frameId)
         {
            // --- phase 1: drag band 3's dot right (raise band3Freq) ---
            case 54: gTestMouse = ImVec2(band3X, band3Y); break;
            case 55: btn(true); break;
            case 56: gTestMouse = ImVec2(band3X + 15.0f, band3Y); break;
            case 57: gTestMouse = ImVec2(band3X + 25.0f, band3Y); break;
            case 58: btn(false); break;
            // --- phase 2: Shift + drag band 3's dot up (raise band3Q) ---
            case 61:
            case 62: gTestMouse = ImVec2(band3X, band3Y); break;
            case 63: btn(true); break;
            case 64: gTestMouse = ImVec2(band3X, band3Y - 30.0f); break;
            case 65: gTestMouse = ImVec2(band3X, band3Y - 50.0f); break;
            case 66: btn(false); break;
            // --- phase 3: double-click band 2's dot (toggle band2On) ---
            case 70: gTestMouse = ImVec2(band2X, band2Y); btn(true); break;
            case 71: btn(false); break;
            case 72: btn(true); break;
            case 73: btn(false); break;
            default: break;
         }
         if (frameId >= 61 && frameId <= 66)
         {
            tio.AddKeyEvent(ImGuiKey_LeftShift, true);
            tio.AddKeyEvent(ImGuiMod_Shift, true);
            tio.KeyShift = true;
         }
         else if (frameId >= 67)
         {
            tio.AddKeyEvent(ImGuiKey_LeftShift, false);
            tio.AddKeyEvent(ImGuiMod_Shift, false);
            tio.KeyShift = false;
         }
         if (frameId >= 54)
         {
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
            // Also warp the real GLFW cursor: this fixture's headless
            // sandbox has been observed moving the OS-level cursor on its
            // own between frames (screen-capture tooling, most likely),
            // which a later glfwGetCursorPos() poll would otherwise pick up
            // and use to clobber the AddMousePosEvent target above.
            glfwSetCursorPos(window, (double)(gTestMouse.x * ImGui_ImplGlfw_GetPointScale()),
                             (double)(gTestMouse.y * ImGui_ImplGlfw_GetPointScale()));
         }
      }
}

void FrameTest_SAMPLERDRAGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr)
      {
         // Drives the exact real-world gesture the fix targets: press down
         // on the panel's sample row (a real Selectable going ImGui-active,
         // the same mechanism gSampleDragActive relies on to detect drag
         // start), drag across onto the Sampler's own canvas rect, release.
         // If the release handling still depended on ed::GetHoveredNode()/
         // ImGui::IsWindowHovered() the way it used to, this active item
         // held in a different window would make the drop silently miss -
         // see the drag-release comment by ed::ScreenToCanvas above.
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };

         int& sPhase = gSamplerDragTestPhase; // 0=waiting for the scan+row, 1=pressed, 2=dragging, 3=released
         static int sPhaseFrame = 0;
         static ImVec2 sRowCenter(-1.0f, -1.0f);
         static ImVec2 sTargetScreen(-1.0f, -1.0f);

         if (sPhase == 0 && gSamplerDragTestRowRect.x >= 0.0f &&
             (gSamplerDragTestRowRect.w - gSamplerDragTestRowRect.y) > 1.0f && gSamplerDragTestTargetValid)
         {
            sRowCenter = ImVec2((gSamplerDragTestRowRect.x + gSamplerDragTestRowRect.z) * 0.5f,
                                (gSamplerDragTestRowRect.y + gSamplerDragTestRowRect.w) * 0.5f);
            sTargetScreen = gSamplerDragTestTargetScreen;
            sPhase = 1;
            sPhaseFrame = frameId;
            gTestMouse = sRowCenter;
         }
         else if (sPhase == 1)
         {
            gTestMouse = sRowCenter;
            if (frameId >= sPhaseFrame + 2)
            {
               btn(true);
               sPhase = 2;
               sPhaseFrame = frameId;
            }
         }
         else if (sPhase == 2)
         {
            const float f = std::min(1.0f, (float)(frameId - sPhaseFrame) / 6.0f);
            gTestMouse = ImVec2(sRowCenter.x + (sTargetScreen.x - sRowCenter.x) * f,
                                sRowCenter.y + (sTargetScreen.y - sRowCenter.y) * f);
            if (frameId >= sPhaseFrame + 8)
            {
               btn(false);
               sPhase = 3;
               sPhaseFrame = frameId;
            }
         }
         else if (sPhase == 3)
         {
            gTestMouse = sTargetScreen;
            if (frameId >= sPhaseFrame + 3)
            {
               auto* samplerNode = static_cast<SamplerNode*>(gNodes[0].node.get());
               const bool ok = samplerNode->FilePath() == TmpPath("infinite_samplerdrag/sample.wav");
               printf("SAMPLERDRAG drop onto node: path='%s' %s\n", samplerNode->FilePath().c_str(),
                      ok ? "OK" : "FAIL");
               printf("%s\n", ok ? "SAMPLERDRAGTEST OK" : "SAMPLERDRAGTEST FAIL");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (sPhase != 0)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }
}

void FrameTest_PLUGINDRAGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr)
      {
         // Same gesture as INFINITE_SAMPLERDRAGTEST above, driven against the
         // Plugins panel's row rect and the empty Plugin node's canvas rect.
         // The assertion is on the node's saved identity, not on the plugin
         // having finished instantiating: instantiation is asynchronous and
         // its own coverage is INFINITE_PLUGINSCANTEST. What this proves is
         // that the drag resolves onto the node under the cursor.
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };

         int& sPhase = gPluginDragTestPhase;
         static int sPhaseFrame = 0;
         static ImVec2 sRowCenter(-1.0f, -1.0f);
         static ImVec2 sTargetScreen(-1.0f, -1.0f);
         static std::string sExpectedId;

         // Gated on the list having been the SAME list for a few frames, not
         // just on IsScanning() being false: the scanner reports "not scanning"
         // as soon as its worker finishes, but the index is only swapped in
         // when the panel next calls PollResults - and this driver runs before
         // the panel draws. So there is a window where the row rect and the
         // identifier captured with it belong to the previous (disk-cached)
         // list, and by the time the synthetic press lands the list underneath
         // has been replaced. Requiring the captured row to hold still rules
         // that out without having to reach into the scanner's internals.
         static std::string sSeenId;
         static int sStableFrames = 0;
         if (sPhase == 0)
         {
            if (!gPluginDragTestRowId.empty() && gPluginDragTestRowId == sSeenId)
               sStableFrames++;
            else
            {
               sSeenId = gPluginDragTestRowId;
               sStableFrames = 0;
            }
         }
         if (sPhase == 0 && !gPluginScanner.IsScanning() && sStableFrames >= 4 &&
             gPluginDragTestRowRect.x >= 0.0f &&
             (gPluginDragTestRowRect.w - gPluginDragTestRowRect.y) > 1.0f && gPluginDragTestTargetValid)
         {
            sRowCenter = ImVec2((gPluginDragTestRowRect.x + gPluginDragTestRowRect.z) * 0.5f,
                                (gPluginDragTestRowRect.y + gPluginDragTestRowRect.w) * 0.5f);
            sTargetScreen = gPluginDragTestTargetScreen;
            sExpectedId.clear();
            sPhase = 1;
            sPhaseFrame = frameId;
            gTestMouse = sRowCenter;
         }
         else if (sPhase == 1)
         {
            gTestMouse = sRowCenter;
            if (frameId >= sPhaseFrame + 2)
            {
               btn(true);
               sPhase = 2;
               sPhaseFrame = frameId;
            }
         }
         else if (sPhase == 2)
         {
            const float f = std::min(1.0f, (float)(frameId - sPhaseFrame) / 6.0f);
            gTestMouse = ImVec2(sRowCenter.x + (sTargetScreen.x - sRowCenter.x) * f,
                                sRowCenter.y + (sTargetScreen.y - sRowCenter.y) * f);
            // Latched here, at the moment the drag actually starts (the
            // panel sets gPluginDragDesc from IsMouseDragging, not from the
            // phase-0 row snapshot), rather than pre-latched before the
            // button press - this is what removes the swap window instead
            // of just narrowing it.
            if (sExpectedId.empty() && !gPluginDragDesc.identifier.empty())
               sExpectedId = gPluginDragDesc.identifier;
            if (frameId >= sPhaseFrame + 8)
            {
               btn(false);
               sPhase = 3;
               sPhaseFrame = frameId;
            }
         }
         else if (sPhase == 3)
         {
            gTestMouse = sTargetScreen;
            if (sExpectedId.empty() && !gPluginDragDesc.identifier.empty())
               sExpectedId = gPluginDragDesc.identifier;
            if (frameId >= sPhaseFrame + 3)
            {
               auto* pluginNode = static_cast<AudioPluginNode*>(gNodes[0].node.get());
               const bool ok = !sExpectedId.empty() && pluginNode->pluginId == sExpectedId;
               printf("PLUGINDRAG drop onto node: id='%s' expected='%s' %s\n",
                      pluginNode->pluginId.c_str(), sExpectedId.c_str(), ok ? "OK" : "FAIL");
               printf("%s\n", ok ? "PLUGINDRAGTEST OK" : "PLUGINDRAGTEST FAIL");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (sPhase != 0)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }
}

void FrameTest_MEDIADRAGTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MEDIADRAGTEST") != nullptr)
      {
         // Same gesture as INFINITE_SAMPLERDRAGTEST above, driven against
         // the Media panel's row rect and Image Source's canvas rect.
         static bool sUnbuffered = false;
         if (!sUnbuffered) { setvbuf(stdout, nullptr, _IONBF, 0); sUnbuffered = true; }
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };

         int& sPhase = gMediaDragTestPhase; // 0=waiting for the scan+row, 1=pressed, 2=dragging, 3=released
         static int sPhaseFrame = 0;
         static ImVec2 sRowCenter(-1.0f, -1.0f);
         static ImVec2 sTargetScreen(-1.0f, -1.0f);

         if (sPhase == 0 && gMediaDragTestRowRect.x >= 0.0f &&
             (gMediaDragTestRowRect.w - gMediaDragTestRowRect.y) > 1.0f && gMediaDragTestTargetValid)
         {
            sRowCenter = ImVec2((gMediaDragTestRowRect.x + gMediaDragTestRowRect.z) * 0.5f,
                                (gMediaDragTestRowRect.y + gMediaDragTestRowRect.w) * 0.5f);
            sTargetScreen = gMediaDragTestTargetScreen;
            sPhase = 1;
            sPhaseFrame = frameId;
            gTestMouse = sRowCenter;
         }
         else if (sPhase == 1)
         {
            gTestMouse = sRowCenter;
            if (frameId >= sPhaseFrame + 2)
            {
               btn(true);
               sPhase = 2;
               sPhaseFrame = frameId;
            }
         }
         else if (sPhase == 2)
         {
            const float f = std::min(1.0f, (float)(frameId - sPhaseFrame) / 6.0f);
            gTestMouse = ImVec2(sRowCenter.x + (sTargetScreen.x - sRowCenter.x) * f,
                                sRowCenter.y + (sTargetScreen.y - sRowCenter.y) * f);
            if (frameId >= sPhaseFrame + 8)
            {
               btn(false);
               sPhase = 3;
               sPhaseFrame = frameId;
            }
         }
         else if (sPhase == 3)
         {
            gTestMouse = sTargetScreen;
            if (frameId >= sPhaseFrame + 3)
            {
               auto* imageNode = static_cast<ImageSourceNode*>(gNodes[0].node.get());
               const bool ok = imageNode->LoadedPath() == TmpPath("infinite_mediadrag/fixture.png");
               printf("MEDIADRAG drop onto node: path='%s' %s\n", imageNode->LoadedPath().c_str(),
                      ok ? "OK" : "FAIL");
               printf("%s\n", ok ? "MEDIADRAGTEST OK" : "MEDIADRAGTEST FAIL");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (sPhase != 0)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }
}

void FrameTest_TRANSPORTCLOCKTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_TRANSPORTCLOCKTEST") != nullptr)
      {
         // P2.5: verifies Beats()/Seconds() are actually live off the audio
         // engine's sample counter once it's running, not frozen at
         // whatever Tick() last wrote. Requires a real audio device to have
         // opened at startup (see the Start() call a few hundred lines up) -
         // on a machine with none, this just confirms the fallback clock
         // still advances smoothly, which is also worth knowing.
         static double sBaselineSeconds = 0.0;
         static double sPauseSeconds = 0.0;
         static bool sSawAdvance = false;
         static bool sSawNonDecrease = true;
         static double sPrevSeconds = 0.0;

         const double sr = AudioEngine::Instance().SampleRate();
         const double seconds = Transport::Instance().Seconds();

         if (frameId == 2)
         {
            sBaselineSeconds = seconds;
            sPrevSeconds = seconds;
            printf("audio sample rate: %.0f (%s)\n", sr, sr > 0.0 ? "audio-driven" : "fallback");
         }
         if (frameId >= 3 && frameId <= 20)
         {
            if (seconds < sPrevSeconds)
               sSawNonDecrease = false;
            if (seconds > sBaselineSeconds)
               sSawAdvance = true;
            sPrevSeconds = seconds;
         }
         if (frameId == 20)
         {
            printf("seconds after 18 frames: %.4f -> %.4f  %s\n", sBaselineSeconds, seconds,
                   (sSawAdvance && sSawNonDecrease) ? "ADVANCING SMOOTHLY OK" : "STALLED OR JUMPED - BUG");
            Transport::Instance().SetPlaying(false);
         }
         if (frameId == 21)
            sPauseSeconds = Transport::Instance().Seconds();
         if (frameId == 30)
         {
            const double afterPause = Transport::Instance().Seconds();
            printf("paused at %.4f, still %.4f after 9 frames  %s\n", sPauseSeconds, afterPause,
                   std::fabs(afterPause - sPauseSeconds) < 1e-9 ? "FROZEN OK" : "ADVANCED WHILE PAUSED - BUG");
            Transport::Instance().SetPlaying(true);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_COLORTEST_2(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_COLORTEST") != nullptr)
      {
         auto* sh = static_cast<ShapeNode*>(gNodes[0].node.get());
         if (frameId == 7)
            printf("before click: fill=(%.2f, %.2f, %.2f)\n", sh->fillColor[0], sh->fillColor[1], sh->fillColor[2]);
         if (frameId == 11)
         {
            printf("after  click: fill=(%.2f, %.2f, %.2f)  %s\n",
                   sh->fillColor[0], sh->fillColor[1], sh->fillColor[2],
                   (std::fabs(sh->fillColor[0] - 0.9f) > 0.02f ||
                    std::fabs(sh->fillColor[1] - 0.35f) > 0.02f ||
                    std::fabs(sh->fillColor[2] - 0.2f) > 0.02f) ? "PICKER RESPONDED OK" : "NO CHANGE - BUG");
         }
      }
}

void FrameTest_RECTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_RECTEST") != nullptr)
      {
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            out->recordFps = 30;
            bool started = out->StartRecording(TmpPath("infinite_rectest.mov"));
            printf("start recording: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 5)
         {
            // pause mid-take: the clock must freeze, but frames keep encoding
            Transport::Instance().SetPlaying(false);
            printf("paused at beats=%.4f\n", Transport::Instance().Beats());
         }
         if (frameId == 12)
            printf("beats while paused=%.4f (should be unchanged)\n", Transport::Instance().Beats());
         if (frameId == 13)
            Transport::Instance().SetPlaying(true);
         if (frameId == 40)
         {
            printf("frames captured: %d\n", out->RecordedFrames());
            out->StopRecording();
            printf("stop: %s\n", out->RecordStatus().c_str());

            // Check what StopRecording *reported* against what actually
            // landed on disk, walking the real encoded sample stream rather
            // than trusting duration - this is the regression the async PBO
            // + encoder-queue rewrite is most likely to get wrong (a
            // readback that never got drained, or the reported count
            // silently drifting from the file).
            const int lastFrames = out->LastRecordedFrames();
            const int lastDropped = out->LastDroppedFrames();
            const Platform::MovieInfo info = Platform::InspectMovie(TmpPath("infinite_rectest.mov"));
            const bool framesOk = info.frameCount == lastFrames;
            printf("written frames: %d (reported %d, dropped %d)  %s\n",
                   info.frameCount, lastFrames, lastDropped,
                   framesOk ? "RECFRAMES OK" : "SUSPECT - frame count mismatch");

            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_RECTEARDOWNTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_RECTEARDOWNTEST") != nullptr)
      {
         // Tearing down mid-take - by deleting the Output node, or by the
         // window closing and its destructor running - must not crash or
         // hang on the encoder worker join, and must not leak the triple-
         // buffered PBOs. See local-prompts/13-async-video-readback.md's
         // teardown section. INFINITE_RECTEARDOWNTEST=quit exercises the
         // destructor path (StopRecording never called explicitly); any
         // other value (e.g. "delete") exercises RemoveNodeByIndex.
         const bool viaQuit = std::string(getenv("INFINITE_RECTEARDOWNTEST")) == "quit";
         auto* out = static_cast<OutputNode*>(gNodes[1].node.get());
         if (frameId == 2)
         {
            bool started = out->StartRecording(TmpPath("infinite_recteardown.mov"));
            printf("start recording: %d (%s)\n", (int)started, out->RecordStatus().c_str());
         }
         if (frameId == 10)
         {
            printf("tearing down mid-recording (still recording=%d) via %s\n",
                   (int)out->IsRecording(), viaQuit ? "quit" : "delete");
            if (viaQuit)
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            else
            {
               RemoveNodeByIndex(gNodes[1].index);
               printf("delete-mid-record: survived  OK\n");
            }
         }
         if (!viaQuit && frameId == 12)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_RESYNTHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_RESYNTHTEST") != nullptr)
      {
         auto* rs = static_cast<ResynthNode*>(gNodes[1].node.get());
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth() / 3, n->GetOutputHeight() / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         static unsigned char prev[4] = { 0, 0, 0, 0 };
         if (frameId == 2)
         {
            rs->chaos = 0.8f; rs->mutation = 0.8f; rs->sourcePull = 0.02f;
            rs->Randomise();
            sample(rs, prev);
            printf("gen %-3d pixel=(%d,%d,%d)\n", rs->Generation(), prev[0], prev[1], prev[2]);
         }
         if (frameId >= 3 && frameId <= 12)
         {
            rs->StepOnce();
            unsigned char now[4];
            sample(rs, now);
            int drift = abs(now[0]-prev[0]) + abs(now[1]-prev[1]) + abs(now[2]-prev[2]);
            printf("gen %-3d pixel=(%d,%d,%d) drift=%d\n", rs->Generation(), now[0], now[1], now[2], drift);
            memcpy(prev, now, 4);
         }
         if (frameId == 14)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
      }
}

void FrameTest_BYPASSTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_BYPASSTEST") != nullptr)
      {
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth()/2, n->GetOutputHeight()/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         unsigned char px[4];
         if (frameId == 3)
         {
            sample(gNodes[2].node.get(), px);
            printf("invert active:   output=(%d,%d,%d) %s\n", px[0], px[1], px[2],
                   px[0] < 40 ? "inverted OK" : "UNEXPECTED");
            gNodes[1].node->bypassed = true;
         }
         if (frameId == 6)
         {
            sample(gNodes[2].node.get(), px);
            printf("invert bypassed: output=(%d,%d,%d) %s\n", px[0], px[1], px[2],
                   px[0] > 200 ? "PASSED THROUGH OK" : "STILL INVERTED - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_CURVESLUTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_CURVESLUTTEST") != nullptr)
      {
         auto sample = [](INode* n, unsigned char* out)
         {
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, n->GetOutputTexture(), 0);
            glReadPixels(n->GetOutputWidth()/2, n->GetOutputHeight()/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
         };
         static unsigned char sBaselinePx[4];
         static int sRebuildsAfterEdit = -1;
         auto* cv = static_cast<CurvesNode*>(gNodes[1].node.get());
         unsigned char px[4];
         if (frameId == 2)
         {
            sample(gNodes[2].node.get(), sBaselinePx);
         }
         if (frameId == 3)
         {
            // The raw-reference path DrawCurveEditor actually uses: no
            // MarkDirty(), no VisitParams, no wrapper call.
            cv->Shape(CurvesNode::kRGB).MovePoint(1, 1.0f, 0.0f);
         }
         if (frameId == 4)
         {
            sample(gNodes[2].node.get(), px);
            const bool changed = px[0] != sBaselinePx[0] || px[1] != sBaselinePx[1] || px[2] != sBaselinePx[2];
            printf("curves lut after raw-reference edit: baseline=(%d,%d,%d) now=(%d,%d,%d) %s\n",
                   sBaselinePx[0], sBaselinePx[1], sBaselinePx[2], px[0], px[1], px[2],
                   changed ? "UPDATED OK" : "STALE - BUG");
            sRebuildsAfterEdit = cv->LutRebuildCount();
         }
         if (frameId == 6)
         {
            // No mutation happened between frame 4 and here - a correct
            // implementation must not have rebuilt the LUT again.
            const int rebuilds = cv->LutRebuildCount();
            printf("curves lut rebuild count: after-edit=%d idle=%d %s\n",
                   sRebuildsAfterEdit, rebuilds,
                   rebuilds == sRebuildsAfterEdit ? "NO SPURIOUS REBUILD OK" : "REBUILT WITH NO EDIT - BUG");
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }
}

void FrameTest_PHASEATEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PHASEATEST") != nullptr && frameId == 4)
      {
         // Every primitive and 2D shape must be spawnable by its own name and
         // come out preset to that shape, not to the class default.
         bool allShapes = true;
         for (int i = 0; i < (int)GeometryNode::ShapeNames().size(); i++)
         {
            const std::string& name = GeometryNode::ShapeNames()[i];
            INode* made = NodeFactory::Instance().MakeNode(name);
            auto* geo = dynamic_cast<GeometryNode*>(made);
            // GetMesh() first: the mesh is built lazily, so TriangleCount is 0
            // until something asks for it.
            const bool ok = geo != nullptr && geo->shape == i &&
                            !geo->GetMesh().Empty();
            if (!ok) { allShapes = false; printf("  failed: %s\n", name.c_str()); }
            delete made;
         }
         bool allShapes2D = true;
         for (int i = 0; i < (int)ShapeNode::ShapeNames().size(); i++)
         {
            INode* made = NodeFactory::Instance().MakeNode(ShapeNode::ShapeNames()[i]);
            auto* sh = dynamic_cast<ShapeNode*>(made);
            if (sh == nullptr || sh->shapeType != i) allShapes2D = false;
            delete made;
         }
         printf("primitives spawnable by name: 3D=%d 2D=%d\n", (int)allShapes, (int)allShapes2D);
         // Two nodes sharing a name is a silent bug: one of them becomes
         // unspawnable and any patch naming it loads the wrong node.
         const std::vector<std::string>& dupes = NodeFactory::Instance().DuplicateNames();
         for (const std::string& d : dupes)
            printf("  duplicate node name: %s\n", d.c_str());
         printf("unique node names: %s\n", dupes.empty() ? "OK" : "FAIL");
         allShapes = allShapes && dupes.empty();

         // Bevel must actually round a cube - more triangles, and a smaller
         // extent than the original since the corners get pulled in.
         const Mesh cube = Primitives::Cube(1);
         const Mesh rounded = MeshOps::Bevel(cube, 0.6f, 2);
         // Measured as the furthest vertex from the centre, not the bounding
         // box: a bevel rounds the corners while leaving the flat faces exactly
         // where they were, so the box does not shrink at all. The corners are
         // the only thing that moves, and they are what this catches.
         auto maxRadius = [](const Mesh& m) {
            float worst = 0.0f;
            for (const Vertex& v : m.vertices)
               worst = std::max(worst, std::sqrt(v.px*v.px + v.py*v.py + v.pz*v.pz));
            return worst;
         };
         const float r0 = maxRadius(cube), r1 = maxRadius(rounded);
         printf("bevel: %zu -> %zu tris, corner radius %.3f -> %.3f (faces stay put)\n",
                cube.indices.size() / 3, rounded.indices.size() / 3, r0, r1);
         const bool bevelled = rounded.indices.size() > cube.indices.size() &&
                               r1 < r0 - 0.02f && r1 > 0.3f;

         printf("%s\n", (allShapes && allShapes2D && bevelled) ? "PHASE A OK" : "SUSPECT");
      }
}

void FrameTest_SELECTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_SELECTTEST") != nullptr && frameId == 4)
      {
         const Mesh cube = Primitives::Cube(1);
         printf("cube: %zu faces, %zu selected by default\n",
                cube.FaceCount(), cube.SelectedCount());
         // An empty mask has to mean "everything", or every operator would need
         // to special-case a mesh that has never been through a Select.
         const bool defaultAll = cube.SelectedCount() == cube.FaceCount();

         // The top of a cube is two triangles: selecting by normal must find
         // exactly those, which is the check that the mode means what it says.
         const Mesh top = MeshOps::Select(cube, MeshOps::kSelectNormal,
                                          0.9f, 0.0f, 1.0f, 1, 0.0f, false, false);
         printf("faces pointing +Y: %zu of %zu  %s\n", top.SelectedCount(), top.FaceCount(),
                top.SelectedCount() == 2 ? "OK" : "FAIL");

         // Deleting them must remove exactly those two and nothing else.
         const Mesh cut = MeshOps::DeleteSelected(top, false);
         printf("delete the selection: %zu faces  %s\n", cut.FaceCount(),
                cut.FaceCount() == cube.FaceCount() - 2 ? "OK" : "FAIL");

         // Keeping instead of deleting is the exact complement.
         const Mesh kept = MeshOps::DeleteSelected(top, true);
         printf("keep instead: %zu faces  %s\n", kept.FaceCount(),
                kept.FaceCount() == 2 ? "OK" : "FAIL");

         // Moving the selection must move only the top, so the maximum y rises
         // while the minimum stays exactly where it was.
         auto rangeY = [](const Mesh& m) {
            float lo = 1e30f, hi = -1e30f;
            for (const Vertex& v : m.vertices) { lo = std::min(lo, v.py); hi = std::max(hi, v.py); }
            return std::pair<float,float>(lo, hi);
         };
         const Mesh moved = MeshOps::TransformSelected(top, Mat4::Identity(), true, 0.5f);
         const auto before = rangeY(cube);
         const auto after = rangeY(moved);
         printf("move top by 0.5: y %.2f..%.2f -> %.2f..%.2f  %s\n",
                before.first, before.second, after.first, after.second,
                (std::fabs(after.second - (before.second + 0.5f)) < 0.01f &&
                 std::fabs(after.first - before.first) < 0.01f) ? "OK" : "FAIL");
         const bool movedOk = std::fabs(after.second - (before.second + 0.5f)) < 0.01f &&
                              std::fabs(after.first - before.first) < 0.01f;

         // Extruding the selection adds a cap and walls but leaves the rest.
         const Mesh extruded = MeshOps::ExtrudeSelected(top, 0.4f, 0.2f);
         printf("extrude top: %zu -> %zu faces  %s\n", cube.FaceCount(), extruded.FaceCount(),
                extruded.FaceCount() > cube.FaceCount() ? "OK" : "FAIL");

         // Random selection has to be reproducible from its seed, or a patch
         // would look different every time it was opened.
         const Mesh r1 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 7.0f, false, false);
         const Mesh r2 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 7.0f, false, false);
         const Mesh r3 = MeshOps::Select(cube, MeshOps::kSelectRandom, 0.5f, 0, 0, 1, 9.0f, false, false);
         const bool reproducible = r1.faceMask == r2.faceMask && r1.faceMask != r3.faceMask;
         printf("random selection reproducible from seed: %d\n", (int)reproducible);

         // Mesh to Points billboards each point as two triangles. Selecting at
         // ~50% must choose or skip both triangles of a point together - never
         // one triangle selected and its partner not, which is what tore
         // points in half before points carried a selectionGroup tag.
         const std::vector<MeshPoint> cloud = MeshOps::ToPoints(cube, 0, 10000);
         const Mesh billboards = MeshOps::PointsToFaces(cloud, 0.2f);
         const Mesh pointSel = MeshOps::Select(billboards, MeshOps::kSelectRandom,
                                                0.5f, 0, 0, 1, 3.0f, false, false);
         bool quadsIntact = !pointSel.faceMask.empty();
         for (size_t f = 0; f + 1 < pointSel.FaceCount(); f += 2)
            if (pointSel.faceMask[f] != pointSel.faceMask[f + 1])
               quadsIntact = false;
         printf("point selection keeps quads whole: %zu points, %zu tris  %s\n",
                cloud.size(), pointSel.FaceCount(), quadsIntact ? "OK" : "FAIL");

         const bool ok = defaultAll && top.SelectedCount() == 2 &&
                         cut.FaceCount() == cube.FaceCount() - 2 && kept.FaceCount() == 2 &&
                         movedOk && extruded.FaceCount() > cube.FaceCount() && reproducible &&
                         quadsIntact;
         printf("%s\n", ok ? "SELECTION OK" : "SUSPECT");
      }
}

void FrameTest_DISTRIBUTETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_DISTRIBUTETEST") != nullptr && frameId == 4)
      {
         // Phase 6: point distribution. A UV sphere so pole clumping (the bug
         // Distribute Points on Faces exists to fix) has something to show up
         // on; a coarse one so the check runs fast.
         const Mesh sphere = Primitives::Sphere(24, 24);

         // Coverage should be roughly uniform per unit area: split the sphere
         // by hemisphere (near the pole vs. near the equator band) using the
         // sampled points' own y, and the two counts should be much closer to
         // each other than ToPoints' index-stride sampling would give, since
         // Primitives::Sphere bunches rings toward the poles.
         const std::vector<MeshPoint> scattered =
            MeshOps::DistributeOnFaces(sphere, 400.0f, 1.0f, MeshOps::kDistributeRandom, 0.0f);
         // Primitives::Sphere bakes radius 0.5 into its positions (py in
         // -0.5..0.5), so the pole/equator split is relative to that, not 1.0.
         int poleCount = 0, equatorCount = 0;
         for (const MeshPoint& p : scattered)
         {
            if (std::fabs(p.py) > 0.35f) poleCount++;
            else if (std::fabs(p.py) < 0.15f) equatorCount++;
         }
         // Both bands cover a comparable fraction of the sphere's surface
         // area, so a uniform scatter should land within roughly 2x of each
         // other; the old index-stride sampler leaves the poles nearly empty
         // by comparison.
         const bool uniformCoverage = poleCount > 0 && equatorCount > 0 &&
            (float)poleCount / (float)equatorCount > 0.4f &&
            (float)poleCount / (float)equatorCount < 2.5f;
         printf("distribute on faces: %zu points, pole=%d equator=%d  %s\n",
                scattered.size(), poleCount, equatorCount, uniformCoverage ? "OK" : "FAIL");

         // Same seed -> identical scatter (a reopened patch must not reshuffle
         // its points); a different seed -> a different one.
         const std::vector<MeshPoint> s1 =
            MeshOps::DistributeOnFaces(sphere, 200.0f, 5.0f, MeshOps::kDistributeRandom, 0.0f);
         const std::vector<MeshPoint> s2 =
            MeshOps::DistributeOnFaces(sphere, 200.0f, 5.0f, MeshOps::kDistributeRandom, 0.0f);
         const std::vector<MeshPoint> s3 =
            MeshOps::DistributeOnFaces(sphere, 200.0f, 6.0f, MeshOps::kDistributeRandom, 0.0f);
         bool sameSeedIdentical = s1.size() == s2.size() && !s1.empty();
         for (size_t i = 0; sameSeedIdentical && i < s1.size(); i++)
            if (s1[i].px != s2[i].px || s1[i].py != s2[i].py || s1[i].pz != s2[i].pz)
               sameSeedIdentical = false;
         bool diffSeedDiffers = s1.size() != s3.size();
         if (!diffSeedDiffers)
            for (size_t i = 0; i < s1.size(); i++)
               if (s1[i].px != s3[i].px || s1[i].py != s3[i].py || s1[i].pz != s3[i].pz)
                  { diffSeedDiffers = true; break; }
         printf("scatter reproducible from seed: same=%d diff=%d  %s\n",
                (int)sameSeedIdentical, (int)diffSeedDiffers,
                (sameSeedIdentical && diffSeedDiffers) ? "OK" : "FAIL");

         // Poisson disk must respect minDistance (no two accepted points
         // closer than it) and must stop adding points once spacing
         // saturates, rather than packing them arbitrarily tight.
         const std::vector<MeshPoint> poisson =
            MeshOps::DistributeOnFaces(sphere, 5000.0f, 2.0f, MeshOps::kDistributePoisson, 0.15f);
         bool minDistanceHeld = true;
         for (size_t i = 0; minDistanceHeld && i < poisson.size(); i++)
            for (size_t j = i + 1; minDistanceHeld && j < poisson.size(); j++)
            {
               const float dx = poisson[i].px - poisson[j].px;
               const float dy = poisson[i].py - poisson[j].py;
               const float dz = poisson[i].pz - poisson[j].pz;
               if (std::sqrt(dx*dx + dy*dy + dz*dz) < 0.15f - 1e-4f)
                  minDistanceHeld = false;
            }
         // A far higher density than the packing can actually hold - if
         // saturation didn't stop the search, this would either hang or spill
         // past what minDistance allows.
         const std::vector<MeshPoint> poissonSaturated =
            MeshOps::DistributeOnFaces(sphere, 200000.0f, 2.0f, MeshOps::kDistributePoisson, 0.15f);
         const bool saturates = poissonSaturated.size() < 5000;
         printf("poisson disk: %zu points, min-distance held=%d, saturates at high density=%d (%zu)  %s\n",
                poisson.size(), (int)minDistanceHeld, (int)saturates, poissonSaturated.size(),
                (minDistanceHeld && saturates) ? "OK" : "FAIL");

         // A saturated Poisson scatter is the case that used to tank the
         // frame rate (30x the requested count in wasted candidate draws,
         // each walking an unordered_map neighbour grid). It must now finish
         // in single-digit milliseconds so running it once a frame is cheap.
         const auto poissonTimingStart = std::chrono::steady_clock::now();
         const std::vector<MeshPoint> poissonTiming =
            MeshOps::DistributeOnFaces(sphere, 300.0f, 3.0f, MeshOps::kDistributePoisson, 0.1f);
         const double poissonTimingMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - poissonTimingStart).count();
         printf("poisson disk timing: %zu points in %.3f ms  %s\n",
                poissonTiming.size(), poissonTimingMs, poissonTimingMs < 10.0 ? "OK" : "FAIL");

         // Merge by Distance: zero threshold is a no-op; a large one collapses
         // a subdivided cube's duplicated seam vertices.
         const Mesh cube = Primitives::Cube(1);
         const Mesh subdivided = MeshOps::Subdivide(cube, 2, 0.0f);
         const Mesh noOp = MeshOps::MergeByDistance(subdivided, 0.0f);
         const Mesh merged = MeshOps::MergeByDistance(subdivided, 2.0f);
         const bool mergeNoOp = noOp.vertices.size() == subdivided.vertices.size() &&
                                noOp.indices.size() == subdivided.indices.size();
         const bool mergeCollapses = merged.vertices.size() < subdivided.vertices.size() &&
                                     merged.indices.size() < subdivided.indices.size();
         printf("merge by distance: %zu verts -> noop %zu, threshold=2 %zu  %s\n",
                subdivided.vertices.size(), noOp.vertices.size(), merged.vertices.size(),
                (mergeNoOp && mergeCollapses) ? "OK" : "FAIL");

         // Points to Vertices: a vertices-only mesh trips Mesh::Empty() (no
         // indices) but must still report HasGeometry(), carry every alive
         // particle's position/colour, and drop dead ones when asked to.
         struct CloudProbe : public IGeometrySource
         {
            std::vector<Particle> cloud;
            Mesh dummy;
            const Mesh& GetMesh() override { return dummy; }
            unsigned long long MeshRevision() override { return 1; }
            const std::vector<Particle>* GetPointCloud() override { return &cloud; }
            unsigned long long PointCloudRevision() override { return 1; }
            Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
            Material GetMaterial() const override { return Material(); }
            unsigned int GetSurfaceTexture() override { return 0; }
         };
         CloudProbe cloudProbe;
         for (int i = 0; i < 5; i++)
         {
            Particle p;
            p.px = (float)i; p.r = 0.25f * i;
            p.hasColor = true;
            p.alive = (i != 2); // one dead particle in the middle
            cloudProbe.cloud.push_back(p);
         }
         PointsToVerticesNode p2v;
         p2v.input = &cloudProbe;
         p2v.aliveOnly = true;
         p2v.CookIfNeeded(40000);
         const Mesh& p2vMesh = p2v.GetMesh();
         const bool p2vOk = p2vMesh.Empty() && p2vMesh.HasGeometry() &&
                            p2vMesh.vertices.size() == 4 && p2vMesh.HasVertexColor();
         printf("points to vertices: %zu verts (want 4, dead dropped), Empty=%d HasGeometry=%d  %s\n",
                p2vMesh.vertices.size(), (int)p2vMesh.Empty(), (int)p2vMesh.HasGeometry(),
                p2vOk ? "OK" : "FAIL");

         // Distribute Points in Grid: exact count, and row-major/cell-centre
         // ordering matching ImageToPointsNode's convention (see
         // GenerativeNodes.cpp) so the two correspond point-for-point.
         DistributePointsInGridNode grid;
         grid.countX = 4; grid.countY = 3;
         grid.spacingX = 0.5f; grid.spacingY = 0.5f;
         grid.jitter = 0.0f;
         grid.CookIfNeeded(41000);
         const std::vector<Particle>& gridPoints = grid.GetPoints();
         bool gridOrderOk = gridPoints.size() == 12;
         if (gridOrderOk)
         {
            // Index 1 is (gx=1, gy=0): one spacing right of index 0, same row.
            const float dx = gridPoints[1].px - gridPoints[0].px;
            const float dy = gridPoints[1].py - gridPoints[0].py;
            // Index 4 is (gx=0, gy=1): one spacing up from index 0, same column.
            const float dxRow = gridPoints[4].px - gridPoints[0].px;
            const float dyRow = gridPoints[4].py - gridPoints[0].py;
            gridOrderOk = std::fabs(dx - 0.5f) < 1e-3f && std::fabs(dy) < 1e-3f &&
                          std::fabs(dxRow) < 1e-3f && std::fabs(dyRow - 0.5f) < 1e-3f;
         }
         printf("distribute in grid: %zu points (want 12), row-major order=%d  %s\n",
                gridPoints.size(), (int)gridOrderOk, gridOrderOk ? "OK" : "FAIL");

         // Render3D's cloud sprite path scales its unit quad by Particle::scale
         // directly (drawCloudSlot), not by pointSize - a node that forgets to
         // derive scale from pointSize renders every sprite at a fixed size
         // regardless of the param, which the mini-viewport preview (built off
         // GetMesh(), a different code path) would not have caught either.
         const bool gridScaleOk = !gridPoints.empty() &&
            std::fabs(gridPoints[0].scale - grid.pointSize * 0.5f) < 1e-4f;
         printf("distribute in grid: particle scale %.4f (want %.4f from pointSize)  %s\n",
                gridPoints.empty() ? -1.0f : gridPoints[0].scale, grid.pointSize * 0.5f,
                gridScaleOk ? "OK" : "FAIL");

         // Same check for Distribute Points on Faces' own particle output, not
         // just the raw MeshOps::DistributeOnFaces samples checked above.
         GeometryNode probeMeshForGrid;
         probeMeshForGrid.shape = 2; // sphere
         probeMeshForGrid.detail = 4;
         DistributePointsOnFacesNode distFacesScaleNode;
         distFacesScaleNode.input = &probeMeshForGrid;
         distFacesScaleNode.pointSize = 0.2f;
         distFacesScaleNode.density = 20.0f;
         distFacesScaleNode.CookIfNeeded(42000);
         const std::vector<Particle>& facesPoints = distFacesScaleNode.GetPoints();
         const bool facesScaleOk = !facesPoints.empty() &&
            std::fabs(facesPoints[0].scale - distFacesScaleNode.pointSize * 0.5f) < 1e-4f;
         printf("distribute on faces: particle scale %.4f (want %.4f from pointSize)  %s\n",
                facesPoints.empty() ? -1.0f : facesPoints[0].scale,
                distFacesScaleNode.pointSize * 0.5f, facesScaleOk ? "OK" : "FAIL");

         const bool ok = uniformCoverage && sameSeedIdentical && diffSeedDiffers &&
                         minDistanceHeld && saturates && poissonTimingMs < 10.0 &&
                         mergeNoOp && mergeCollapses &&
                         p2vOk && gridOrderOk && gridScaleOk && facesScaleOk;
         printf("%s\n", ok ? "DISTRIBUTE OK" : "SUSPECT");
      }
}

void FrameTest_PHASE4TEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PHASE4TEST") != nullptr && frameId == 4)
      {
         // Phase 4 (selection as an input): GeometryOpNode::selectionOnly.
         GeometryNode cube;
         cube.shape = 1; cube.detail = 0; // plain 12-triangle cube
         cube.CookIfNeeded(30000);

         GeometryOpNode select;
         select.input = &cube;
         select.op = GeometryOpNode::kSelect;
         select.selectMode = MeshOps::kSelectNormal;
         select.axis = 1; select.selectA = 0.9f; select.selectC = 1.0f; // +Y top

         auto rangeY = [](const Mesh& m) {
            float lo = 1e30f, hi = -1e30f;
            for (const Vertex& v : m.vertices) { lo = std::min(lo, v.py); hi = std::max(hi, v.py); }
            return std::pair<float, float>(lo, hi);
         };

         // Done means #6: Cube -> Select(+Y) -> Transform, selectionOnly on
         // moves only the top; off moves the whole cube.
         GeometryOpNode moveOn;
         moveOn.input = &select; moveOn.op = GeometryOpNode::kTransform;
         moveOn.selectionOnly = true; moveOn.offsetY = 0.5f;
         // Isolate the matrix-only effect being checked below - the default
         // moveAlongNormals=true would add its own +0.2 on top of offsetY.
         moveOn.moveAlongNormals = false;
         moveOn.CookIfNeeded(30001);
         const auto onRange = rangeY(moveOn.GetMesh());
         const auto baseRange = rangeY(cube.GetMesh());
         const bool onOk = std::fabs(onRange.second - (baseRange.second + 0.5f)) < 0.01f &&
                           std::fabs(onRange.first - baseRange.first) < 0.01f;

         GeometryOpNode moveOff;
         moveOff.input = &select; moveOff.op = GeometryOpNode::kTransform;
         moveOff.selectionOnly = false; moveOff.offsetY = 0.5f;
         moveOff.CookIfNeeded(30002);
         const auto offRange = rangeY(moveOff.GetMesh());
         const bool offOk = std::fabs(offRange.second - (baseRange.second + 0.5f)) < 0.01f &&
                            std::fabs(offRange.first - (baseRange.first + 0.5f)) < 0.01f;
         printf("transform: selectionOnly on moves top only (%.2f..%.2f), off moves whole cube (%.2f..%.2f)  %s\n",
                onRange.first, onRange.second, offRange.first, offRange.second,
                (onOk && offOk) ? "OK" : "FAIL");

         // kDelete: on removes just the 2 selected faces, off removes all 12
         // (see the Op enum comment - "useless but consistent").
         GeometryOpNode delOn;
         delOn.input = &select; delOn.op = GeometryOpNode::kDelete; delOn.selectionOnly = true;
         delOn.CookIfNeeded(30003);
         const size_t delOnFaces = delOn.GetMesh().FaceCount();

         GeometryOpNode delOff;
         delOff.input = &select; delOff.op = GeometryOpNode::kDelete; delOff.selectionOnly = false;
         delOff.CookIfNeeded(30004);
         const size_t delOffFaces = delOff.GetMesh().FaceCount();
         const bool deleteOk = delOnFaces == cube.GetMesh().FaceCount() - 2 && delOffFaces == 0;
         printf("delete: selectionOnly on -> %zu faces, off -> %zu faces  %s\n",
                delOnFaces, delOffFaces, deleteOk ? "OK" : "FAIL");

         // kExtrude: on only extrudes the top (small triangle gain), off
         // extrudes the entire mesh as one region (bigger gain, and the two
         // must differ - both use MeshOps::Extrude, gated by whether the mask
         // reaches it or gets cleared first).
         GeometryOpNode extOn;
         extOn.input = &select; extOn.op = GeometryOpNode::kExtrude;
         extOn.selectionOnly = true; extOn.thickness = 0.3f;
         extOn.CookIfNeeded(30005);
         const size_t extOnFaces = extOn.GetMesh().FaceCount();

         GeometryOpNode extOff;
         extOff.input = &select; extOff.op = GeometryOpNode::kExtrude;
         extOff.selectionOnly = false; extOff.thickness = 0.3f;
         extOff.CookIfNeeded(30006);
         const size_t extOffFaces = extOff.GetMesh().FaceCount();
         const bool extrudeOk = extOnFaces > cube.GetMesh().FaceCount() &&
                                extOffFaces > cube.GetMesh().FaceCount() &&
                                extOnFaces != extOffFaces;
         printf("extrude: selectionOnly on -> %zu faces, off -> %zu faces  %s\n",
                extOnFaces, extOffFaces, extrudeOk ? "OK" : "FAIL");

         // Done means #5, the highest-risk check in the phase: a patch saved
         // before this change used the (now-deprecated) kTransformSelected/
         // kDeleteSelected/kExtrudeSelected ops with no `selectionOnly` key at
         // all - that param didn't exist yet. Simulate exactly that save,
         // load it the way ApplyPatchData does (LoadParams then
         // MigrateDeprecatedOp), and check both that the migration lands on
         // the right (op, selectionOnly) and that the geometry it produces
         // matches what the old deprecated op (still handled in GetMesh's
         // switch, for defence in depth) would have produced.
         auto stripSelectionOnly = [](std::vector<std::pair<std::string, std::string>>& params) {
            params.erase(std::remove_if(params.begin(), params.end(),
               [](const std::pair<std::string, std::string>& kv) { return kv.first == "selectionOnly"; }),
               params.end());
         };
         auto sameMesh = [](const Mesh& a, const Mesh& b) {
            if (a.vertices.size() != b.vertices.size() || a.indices.size() != b.indices.size())
               return false;
            for (size_t i = 0; i < a.vertices.size(); i++)
               if (std::fabs(a.vertices[i].px - b.vertices[i].px) > 1e-5f ||
                   std::fabs(a.vertices[i].py - b.vertices[i].py) > 1e-5f ||
                   std::fabs(a.vertices[i].pz - b.vertices[i].pz) > 1e-5f)
                  return false;
            return true;
         };

         bool migrationOk = true;
         {
            GeometryOpNode oldNode;
            oldNode.input = &select; oldNode.op = GeometryOpNode::kTransformSelected;
            oldNode.offsetY = 0.5f; oldNode.moveAlongNormals = true; oldNode.normalAmount = 0.3f;
            oldNode.CookIfNeeded(30010);
            const Mesh preMigration = oldNode.GetMesh();

            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&oldNode, params);
            stripSelectionOnly(params);

            GeometryOpNode migrated;
            Patch::LoadParams(&migrated, params);
            migrated.MigrateDeprecatedOp();
            migrated.input = &select;
            const bool fieldsOk = migrated.op == GeometryOpNode::kTransform && migrated.selectionOnly;
            migrated.CookIfNeeded(30011);
            const bool meshOk = sameMesh(preMigration, migrated.GetMesh());
            migrationOk = migrationOk && fieldsOk && meshOk;
            printf("migrate Transform Selected -> Transform+selectionOnly: fields=%d mesh=%d  %s\n",
                   fieldsOk, meshOk, (fieldsOk && meshOk) ? "OK" : "FAIL");
         }
         {
            GeometryOpNode oldNode;
            oldNode.input = &select; oldNode.op = GeometryOpNode::kDeleteSelected;
            oldNode.keepSelected = true;
            oldNode.CookIfNeeded(30012);
            const Mesh preMigration = oldNode.GetMesh();

            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&oldNode, params);
            stripSelectionOnly(params);

            GeometryOpNode migrated;
            Patch::LoadParams(&migrated, params);
            migrated.MigrateDeprecatedOp();
            migrated.input = &select;
            const bool fieldsOk = migrated.op == GeometryOpNode::kDelete && migrated.selectionOnly;
            migrated.CookIfNeeded(30013);
            const bool meshOk = sameMesh(preMigration, migrated.GetMesh());
            migrationOk = migrationOk && fieldsOk && meshOk;
            printf("migrate Delete Selected -> Delete+selectionOnly: fields=%d mesh=%d  %s\n",
                   fieldsOk, meshOk, (fieldsOk && meshOk) ? "OK" : "FAIL");
         }
         {
            GeometryOpNode oldNode;
            oldNode.input = &select; oldNode.op = GeometryOpNode::kExtrudeSelected;
            oldNode.thickness = 0.3f; oldNode.inset = 0.1f;
            oldNode.CookIfNeeded(30014);
            const Mesh preMigration = oldNode.GetMesh();

            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&oldNode, params);
            stripSelectionOnly(params);

            GeometryOpNode migrated;
            Patch::LoadParams(&migrated, params);
            migrated.MigrateDeprecatedOp();
            migrated.input = &select;
            const bool fieldsOk = migrated.op == GeometryOpNode::kExtrude && migrated.selectionOnly;
            migrated.CookIfNeeded(30015);
            // kExtrude (general) is region-merged while ExtrudeSelected is a
            // simpler per-face wall - not byte-identical, but must select the
            // same faces and add geometry rather than silently no-op.
            const Mesh& migratedMesh = migrated.GetMesh();
            const bool meshOk = migratedMesh.FaceCount() > cube.GetMesh().FaceCount() &&
                                preMigration.FaceCount() > cube.GetMesh().FaceCount();
            migrationOk = migrationOk && fieldsOk && meshOk;
            printf("migrate Extrude Selected -> Extrude+selectionOnly: fields=%d mesh=%d  %s\n",
                   fieldsOk, meshOk, (fieldsOk && meshOk) ? "OK" : "FAIL");
         }

         const bool phase4Ok = onOk && offOk && deleteOk && extrudeOk && migrationOk;
         printf("%s\n", phase4Ok ? "PHASE 4 OK" : "SUSPECT");
      }
}

void FrameTest_INSTANCESELECTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_INSTANCESELECTTEST") != nullptr && frameId == 4)
      {
         // Instance-domain selection (local-prompts/instance-selection.md Part
         // 1): a Select downstream of an InstanceOnPoints must mask instance
         // placements, not the shared stamp's faces. Scatter a cube stamp over
         // a sphere's vertices (spread roughly over a unit sphere), then
         // Select by radius around the origin - a real subset should come
         // back selected, not none/all, and it must stay put across frames
         // with nothing changed.
         GeometryNode sphere;
         sphere.shape = 2; sphere.detail = 3; // plenty of vertices, spread over ~unit radius
         sphere.CookIfNeeded(50000);

         GeometryNode stamp;
         stamp.shape = 1; stamp.detail = 0; // small cube stamp
         stamp.CookIfNeeded(50001);

         InstanceOnPointsNode instancer;
         instancer.pointSource = &sphere;
         instancer.instanceShape = &stamp;
         instancer.pointMode = 0; // vertices
         instancer.CookIfNeeded(50002);
         const size_t instanceCount = instancer.InstanceTransforms().size();

         GeometryOpNode select;
         select.input = &instancer;
         select.op = GeometryOpNode::kSelect;
         select.selectMode = MeshOps::kSelectRadius;
         // A UV/icosphere's vertices all sit at the same distance from its
         // own centre, so centring the radius test there would trivially
         // select all-or-nothing regardless of the threshold. Centring it on
         // a point on the sphere's surface instead makes the 3D chord
         // distance to every other vertex vary continuously, giving a real
         // partial split - radius 1.0 selects roughly the near hemisphere.
         select.selectA = 1.0f; select.selectB = 0.0f; select.selectC = 0.0f;
         select.selectSeed = 1.0f;
         select.CookIfNeeded(50003);
         select.GetMesh();

         auto countSelected = [](const std::vector<unsigned char>* mask) {
            size_t n = 0;
            if (mask)
               for (unsigned char v : *mask)
                  if (v) n++;
            return n;
         };

         const std::vector<unsigned char>* mask = select.InstanceSelection();
         const size_t selectedCount = countSelected(mask);
         const bool maskSizeOk = mask != nullptr && mask->size() == instanceCount;
         const bool maskRangeOk = maskSizeOk && selectedCount > 0 && selectedCount < instanceCount;
         // Leaving the stamp mesh alone is the whole point of the instance-
         // domain branch (GeometryOpNode::GetMesh's kSelect case) - a bug that
         // fell back to face-masking the stamp would still pass the range
         // check above by accident, so check this too.
         const bool stampUntouchedOk = select.GetMesh().faceMask.empty();

         // Stable across frames: re-cook at a later frameId with nothing
         // changed and confirm the mask is byte-identical.
         select.CookIfNeeded(50004);
         select.GetMesh();
         const std::vector<unsigned char>* mask2 = select.InstanceSelection();
         const bool stableOk = mask2 != nullptr && mask != nullptr && *mask2 == *mask;

         const bool ok = instanceCount > 0 && maskSizeOk && maskRangeOk && stampUntouchedOk && stableOk;
         printf("instance select: %zu instances, %zu selected (mask size %zu), "
                "stamp untouched=%d, stable=%d  %s\n",
                instanceCount, selectedCount, mask ? mask->size() : (size_t)0,
                (int)stampUntouchedOk, (int)stableOk, ok ? "INSTANCE SELECT OK" : "FAIL");
      }
}

void FrameTest_PADPATHTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_PADPATHTEST") != nullptr && frameId == 4)
      {
         // A real recorded performance, not the generic mutator's mangled
         // text, round-tripping through both save/load and copy/paste.
         ShapeNode src;
         src.shapeType = 0;
         src.width = 64; src.height = 64;
         src.CookIfNeeded(9500);

         ResynthNode a;
         a.Input().Connect(&src);
         Transport::Instance().SetPlaying(true);
         Transport::Instance().Rewind();
         a.StartRecording();
         const float xs[] = { 0.1f, 0.3f, 0.6f, 0.9f, 0.2f };
         for (int i = 0; i < 5; i++)
         {
            a.padX = xs[i];
            a.padY = 1.0f - xs[i];
            a.CookIfNeeded(9510 + i);
            Transport::Instance().Tick(0.3f);
         }
         a.StopRecording();
         const size_t recorded = a.Path().size();

         auto pathsMatch = [](const std::vector<ResynthNode::PadPoint>& p,
                              const std::vector<ResynthNode::PadPoint>& q) {
            if (p.size() != q.size())
               return false;
            for (size_t i = 0; i < p.size(); i++)
               if (std::fabs(p[i].x - q[i].x) > 1e-5f || std::fabs(p[i].y - q[i].y) > 1e-5f ||
                   std::fabs(p[i].beat - q[i].beat) > 1e-6)
                  return false;
            return true;
         };

         ResynthNode b;
         CopyParams(&b, &a);
         const bool copyOk = pathsMatch(a.Path(), b.Path());
         printf("resynth pad path: %zu points recorded, copy/paste preserved %zu  %s\n",
                recorded, b.Path().size(), copyOk && recorded > 0 ? "OK" : "FAIL");

         std::vector<std::pair<std::string, std::string>> params;
         Patch::SaveParams(&a, params);
         ResynthNode c;
         Patch::LoadParams(&c, params);
         const bool loadOk = pathsMatch(a.Path(), c.Path());
         printf("resynth pad path: save/load preserved %zu  %s\n",
                c.Path().size(), loadOk ? "OK" : "FAIL");

         // Same check for Macro XY's pad, which records identically.
         MacroXYNode m;
         Transport::Instance().Rewind();
         m.StartRecording();
         for (int i = 0; i < 5; i++)
         {
            m.padX = xs[i];
            m.padY = 1.0f - xs[i];
            m.CookIfNeeded(9520 + i);
            Transport::Instance().Tick(0.3f);
         }
         m.StopRecording();
         const size_t mRecorded = m.Path().size();

         auto macroPathsMatch = [](const std::vector<MacroXYNode::PadPoint>& p,
                                   const std::vector<MacroXYNode::PadPoint>& q) {
            if (p.size() != q.size())
               return false;
            for (size_t i = 0; i < p.size(); i++)
               if (std::fabs(p[i].x - q[i].x) > 1e-5f || std::fabs(p[i].y - q[i].y) > 1e-5f ||
                   std::fabs(p[i].beat - q[i].beat) > 1e-6)
                  return false;
            return true;
         };
         MacroXYNode m2;
         CopyParams(&m2, &m);
         const bool mCopyOk = macroPathsMatch(m.Path(), m2.Path());
         printf("macro xy pad path: %zu points recorded, copy/paste preserved %zu  %s\n",
                mRecorded, m2.Path().size(), mCopyOk && mRecorded > 0 ? "OK" : "FAIL");

         const bool ok = copyOk && loadOk && recorded > 0 && mCopyOk && mRecorded > 0;
         printf("%s\n", ok ? "PAD PATH OK" : "SUSPECT");
      }
}

void FrameTest_UNDOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_UNDOTEST") != nullptr && frameId == 4)
      {
         NewPatch(); // also clears the undo/redo stacks - a clean baseline

         bool ok = true;

         // --- spawn / undo / redo ---
         GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
         const bool spawnedOne = gNodes.size() == 1 && cube != nullptr;
         Undo();
         const bool undoRemovedIt = gNodes.empty();
         Redo();
         const bool redoBroughtItBack = gNodes.size() == 1;
         printf("spawn: 1 node -> undo -> %zu nodes -> redo -> %zu nodes  %s\n",
                (size_t)0, gNodes.size(),
                (spawnedOne && undoRemovedIt && redoBroughtItBack) ? "OK" : "FAIL");
         ok = ok && spawnedOne && undoRemovedIt && redoBroughtItBack;

         // --- param edit / undo / redo, via the same checkpoint the UI widgets use ---
         // Re-fetched after every Undo()/Redo(): each one runs ApplyPatchData,
         // which rebuilds the graph from scratch via NewPatch() + respawn, so
         // gNodes[0] is a genuinely new GeometryNode object each time - a
         // pointer captured before the call is stale afterward (NewPatch
         // retires rather than frees its old nodes immediately, precisely so
         // a stale pointer like that stays inert instead of dangling).
         auto* geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         geo->detail = 24; // known starting value
         PushUndoCheckpoint(); // what ModSlider does on IsItemActivated, before the edit
         geo->detail = 91;
         Undo();
         geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         const int afterUndo = geo->detail;
         const bool undoRestoredParam = afterUndo == 24;
         Redo();
         geo = static_cast<GeometryNode*>(gNodes[0].node.get());
         const int afterRedo = geo->detail;
         const bool redoReappliedParam = afterRedo == 91;
         printf("param edit: 24 -> 91 -> undo -> %d -> redo -> %d  %s\n",
                afterUndo, afterRedo,
                (undoRestoredParam && redoReappliedParam) ? "OK" : "FAIL");
         ok = ok && undoRestoredParam && redoReappliedParam;

         // --- delete (with a connection) / undo restores both the node and the wire ---
         GraphNode* smoothGn = SpawnNode("Smooth", "3D", 200.0f, 0.0f);
         auto* smooth = static_cast<GeometryOpNode*>(smoothGn->node.get());
         smooth->input = geo; // the live Cube, re-fetched just above
         const int smoothIndex = smoothGn->index;
         RemoveNodeByIndex(smoothIndex);
         const bool deleted = FindNodeByIndex(smoothIndex) == nullptr;
         Undo();
         GraphNode* restored = nullptr;
         for (GraphNode& gn : gNodes)
            if (gn.typeName == "Smooth")
               restored = &gn;
         const bool nodeRestored = restored != nullptr;
         const bool connectionRestored = nodeRestored &&
            static_cast<GeometryOpNode*>(restored->node.get())->input ==
               dynamic_cast<IGeometrySource*>(gNodes[0].node.get());
         printf("delete with connection: deleted=%d, undo restores node=%d and wire=%d  %s\n",
                (int)deleted, (int)nodeRestored, (int)connectionRestored,
                (deleted && nodeRestored && connectionRestored) ? "OK" : "FAIL");
         ok = ok && deleted && nodeRestored && connectionRestored;

         // --- a new action after an undo must drop the stale redo history ---
         Undo(); // back to just the cube, no Smooth
         const size_t redoDepthBeforeNewAction = gRedoStack.size();
         SpawnNode("Sphere", "3D", 400.0f, 0.0f);
         const bool redoClearedByNewAction = gRedoStack.empty();
         printf("redo stack: %zu entries before a new action -> %zu after  %s\n",
                redoDepthBeforeNewAction, gRedoStack.size(),
                (redoDepthBeforeNewAction > 0 && redoClearedByNewAction) ? "OK" : "FAIL");
         ok = ok && redoDepthBeforeNewAction > 0 && redoClearedByNewAction;

         // --- undo/redo themselves must not pollute the stacks they read from ---
         const size_t depthBefore = gUndoStack.size();
         Undo();
         Redo();
         const bool statOfSizeUnaffected = gUndoStack.size() == depthBefore;
         printf("undo/redo do not grow their own stacks: %zu -> %zu  %s\n",
                depthBefore, gUndoStack.size(), statOfSizeUnaffected ? "OK" : "FAIL");
         ok = ok && statOfSizeUnaffected;

         // --- every modulator input pin survives an undo (regression) ---
         // Modulator-into-modulator cables (Random -> Range to Range's `in`,
         // and every other node exposing a ModulatorInputSlot) are raw
         // IModulator* pointers rather than ImageCables, so they only come
         // back if ConnectGeometrySlot knows how to re-seat them. It used to
         // know about MathNode and nothing else, so every other such cable was
         // written into the snapshot and then silently dropped on the way back
         // in - one undo anywhere on the canvas detached the cable. Swept over
         // the whole registry rather than a hand-written list, so a new
         // modulator-input node cannot quietly reintroduce the hole.
         {
            int sweptTypes = 0;
            std::vector<std::string> lostTypes;
            for (const std::string& category : NodeFactory::Instance().GetCategories())
            {
               for (const std::string& typeName : NodeFactory::Instance().GetNodesInCategory(category))
               {
                  {
                     std::unique_ptr<INode> probe(NodeFactory::Instance().MakeNode(typeName));
                     if (probe == nullptr || probe->ModulatorInputCount() == 0)
                        continue;
                  }

                  NewPatch();
                  GraphNode* srcGn = SpawnNode("Random", "Modulators", 0.0f, 0.0f);
                  GraphNode* dstGn = SpawnNode(typeName, category, 200.0f, 0.0f);
                  if (srcGn == nullptr || dstGn == nullptr)
                     continue;
                  const int slotCount = dstGn->node->ModulatorInputCount();
                  IModulator* wired = ModulatorForOutput(srcGn->node.get(), 0);
                  for (int slot = 0; slot < slotCount; slot++)
                     *dstGn->node->ModulatorInputSlot(slot) = wired;
                  sweptTypes++;

                  PushUndoCheckpoint();
                  SpawnNode("Constant", "Modulators", 400.0f, 0.0f); // something to undo
                  Undo();

                  // Re-found by type name: Undo rebuilds the graph from
                  // scratch, so every pointer captured above is stale.
                  GraphNode* srcBack = nullptr;
                  GraphNode* dstBack = nullptr;
                  for (GraphNode& gn : gNodes)
                  {
                     if (gn.typeName == "Random")
                        srcBack = &gn;
                     else if (gn.typeName == typeName)
                        dstBack = &gn;
                  }
                  bool intact = srcBack != nullptr && dstBack != nullptr;
                  if (intact)
                  {
                     IModulator* expected = ModulatorForOutput(srcBack->node.get(), 0);
                     for (int slot = 0; slot < slotCount && intact; slot++)
                     {
                        IModulator** field = dstBack->node->ModulatorInputSlot(slot);
                        intact = field != nullptr && *field != nullptr && *field == expected;
                     }
                  }
                  if (!intact)
                     lostTypes.push_back(typeName);
               }
            }
            const bool modCablesOk = sweptTypes > 0 && lostTypes.empty();
            printf("modulator input cables survive undo: %d types swept, %zu lost", sweptTypes, lostTypes.size());
            for (const std::string& t : lostTypes)
               printf(" [%s]", t.c_str());
            printf("  %s\n", modCablesOk ? "OK" : "FAIL");
            ok = ok && modCablesOk;
         }

         // --- opening a patch from disk clears history; undoing past it is not a thing ---
         SavePatchTo(TmpPath("infinite_undotest.infinite"));
         LoadPatchFrom(TmpPath("infinite_undotest.infinite"));
         const bool loadClearsUndo = gUndoStack.empty() && gRedoStack.empty();
         printf("loading a patch clears undo history: %zu undo, %zu redo  %s\n",
                gUndoStack.size(), gRedoStack.size(), loadClearsUndo ? "OK" : "FAIL");
         ok = ok && loadClearsUndo;

         printf("%s\n", ok ? "UNDO REDO OK" : "SUSPECT");
      }
}

void FrameTest_MODDROPDOWNUNDOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_MODDROPDOWNUNDOTEST") != nullptr)
      {
         static int sFilterIdx = -1, sOscIdx = -1, sConstAIdx = -1, sConstBIdx = -1;
         static int sDelayIdx = -1, sRampIdx = -1, sPickFilterIdx = -1;
         static int sTypeParam = -1, sWaveParam = -1, sFmModeParam = -1, sSyncParam = -1,
                    sRampModeParam = -1, sPickTypeParam = -1;
         static size_t sUndoBefore = 0, sRedoBefore = 0;
         static std::set<int> sTypesSeen, sWavesSeen, sFmModesSeen, sSyncsSeen, sRampModesSeen;
         static bool sDrivenOk = false;
         static int sPickBefore = -1;
         constexpr int kBindFrame = 8, kSweepStart = 10, kSweepFrames = 48, kCheckFrame = 62,
                       kPickFrame = 64;

         auto findIdx = [](int idx) -> GraphNode* { return FindNodeByIndex(idx); };
         auto filterType = [](int idx) -> int
         {
            GraphNode* f = FindNodeByIndex(idx);
            return f != nullptr ? (int)(static_cast<AudioEffectNode*>(f->node.get())->Param("type") + 0.5f) : -1;
         };

         if (frameId == 4)
         {
            NewPatch();
            // Indices read right after each spawn - SpawnNode push_backs onto
            // gNodes, which can reallocate every GraphNode* taken before it.
            GraphNode* gn = SpawnNode("Audio Filter", "AudioEffects", 0.0f, 0.0f);
            sFilterIdx = gn != nullptr ? gn->index : -1;
            gn = SpawnNode("Oscillator", "Synthesizers", 0.0f, 420.0f);
            sOscIdx = gn != nullptr ? gn->index : -1;
            gn = SpawnNode("Constant", "Modulators", 700.0f, 0.0f);
            sConstAIdx = gn != nullptr ? gn->index : -1;
            gn = SpawnNode("Constant", "Modulators", 700.0f, 420.0f);
            sConstBIdx = gn != nullptr ? gn->index : -1;
            gn = SpawnNode("Delay", "AudioEffects", 1100.0f, 0.0f);
            sDelayIdx = gn != nullptr ? gn->index : -1;
            gn = SpawnNode("Audio Color Ramp", "Compositing", 1100.0f, 420.0f);
            sRampIdx = gn != nullptr ? gn->index : -1;
            // DropdownButton lives in a Draw*Params panel, which is only drawn
            // (and so only registers its discrete slot) while expanded.
            if (gn != nullptr)
               gn->showParams = true;
            gn = SpawnNode("Audio Filter", "AudioEffects", 0.0f, 840.0f);
            sPickFilterIdx = gn != nullptr ? gn->index : -1;
            gRequestFitView = true; // every fixture node on screen, so each body draws and registers
         }
         if (frameId == kBindFrame)
         {
            // The bodies have drawn by now, so every control has registered
            // its discrete slot. Resolved by label through the same
            // DiscreteParamSlot the widgets use, then confirmed as registered
            // with the right kind (enum for a dropdown, bool for a checkbox).
            auto resolve = [](int nodeIdx, const char* label, bool wantBool) -> int
            {
               if (nodeIdx < 0)
                  return -1;
               const int slot = DiscreteParamSlot(nodeIdx, label);
               const ParamRef* ref = Modulation::Instance().KnownParam(nodeIdx, slot);
               if (ref == nullptr || (wantBool ? !ref->isBool : !ref->isEnum))
                  return -1;
               return slot;
            };
            sTypeParam = resolve(sFilterIdx, "type", false);
            sWaveParam = resolve(sOscIdx, "oscWave", false);
            sFmModeParam = resolve(sOscIdx, "oscFmMode", false);
            sSyncParam = resolve(sDelayIdx, "sync to tempo##delaySync", true);
            sRampModeParam = resolve(sRampIdx, "mode", false);
            sPickTypeParam = resolve(sPickFilterIdx, "type", false);
            if (sConstAIdx >= 0 && sConstBIdx >= 0)
            {
               if (sTypeParam >= 0)
                  Modulation::Instance().Bind(sFilterIdx, sTypeParam, sConstAIdx, 0);
               if (sWaveParam >= 0)
                  Modulation::Instance().Bind(sOscIdx, sWaveParam, sConstBIdx, 0);
               if (sFmModeParam >= 0)
                  Modulation::Instance().Bind(sOscIdx, sFmModeParam, sConstAIdx, 0);
               if (sSyncParam >= 0)
                  Modulation::Instance().Bind(sDelayIdx, sSyncParam, sConstBIdx, 0);
               if (sRampModeParam >= 0)
                  Modulation::Instance().Bind(sRampIdx, sRampModeParam, sConstAIdx, 0);
            }
            // Baseline taken after the bind: binding is a user edit of its
            // own, and this check is only about what the cable does after.
            sUndoBefore = gUndoStack.size();
            sRedoBefore = gRedoStack.size();
            gPatchDirty = false;
         }
         if (frameId >= kSweepStart && frameId < kSweepStart + kSweepFrames)
         {
            // Triangle 0..1..0 with a 16-frame period: three full sweeps,
            // each crossing every index boundary of every control twice.
            const int t = (frameId - kSweepStart) % 16;
            const float v = (t < 8 ? (float)t : (float)(16 - t)) / 8.0f;
            if (GraphNode* a = findIdx(sConstAIdx))
               static_cast<ConstantNode*>(a->node.get())->value = v;
            if (GraphNode* b = findIdx(sConstBIdx))
               static_cast<ConstantNode*>(b->node.get())->value = 1.0f - v;
            sTypesSeen.insert(filterType(sFilterIdx));
            if (GraphNode* o = findIdx(sOscIdx))
            {
               sWavesSeen.insert(static_cast<OscillatorNode*>(o->node.get())->waveform);
               sFmModesSeen.insert(static_cast<OscillatorNode*>(o->node.get())->fmMode);
            }
            if (GraphNode* d = findIdx(sDelayIdx))
               sSyncsSeen.insert((int)(static_cast<AudioEffectNode*>(d->node.get())->Param("sync") + 0.5f));
            if (GraphNode* r = findIdx(sRampIdx))
               sRampModesSeen.insert(static_cast<AudioColorRampNode*>(r->node.get())->mode);
         }
         if (frameId == kCheckFrame)
         {
            const bool bound = sTypeParam >= 0 && sWaveParam >= 0 && sFmModeParam >= 0 &&
                               sSyncParam >= 0 && sRampModeParam >= 0 && sPickTypeParam >= 0;
            // Proves each driven path actually ran (onSelect / the caller's
            // setter still writes the param) - without this the stack check
            // below passes vacuously.
            const bool driven = sTypesSeen.size() >= 3 && sWavesSeen.size() >= 3 &&
                                sFmModesSeen.size() >= 2 && sSyncsSeen.size() >= 2 &&
                                sRampModesSeen.size() >= 3;
            printf("modulated controls bound: filter type=%d, oscWave=%d, oscFmMode=%d, delay sync=%d, "
                   "ramp mode=%d, pick filter type=%d  %s\n",
                   sTypeParam, sWaveParam, sFmModeParam, sSyncParam, sRampModeParam, sPickTypeParam,
                   bound ? "OK" : "FAIL");
            printf("modulated controls follow the cable: filter type (Dropdown) %zu values, osc wave "
                   "(BareDropdown) %zu, osc fm mode (DropdownKnob) %zu, ramp mode (DropdownButton) %zu, "
                   "delay sync (row Checkbox) %zu  %s\n",
                   sTypesSeen.size(), sWavesSeen.size(), sFmModesSeen.size(), sRampModesSeen.size(),
                   sSyncsSeen.size(), driven ? "OK" : "FAIL");
            const bool undoUnchanged = gUndoStack.size() == sUndoBefore && gRedoStack.size() == sRedoBefore;
            printf("modulated controls push no undo entries: undo %zu -> %zu, redo %zu -> %zu  %s\n",
                   sUndoBefore, gUndoStack.size(), sRedoBefore, gRedoStack.size(),
                   undoUnchanged ? "OK" : "FAIL");
            printf("modulated controls leave the patch clean: dirty=%d  %s\n", (int)gPatchDirty,
                   !gPatchDirty ? "OK" : "FAIL");
            sDrivenOk = bound && driven && undoUnchanged && !gPatchDirty;

            // Ask the unbound filter's "type" dropdown to act as if clicked on
            // its next draw, so gDropdown ends up holding its real lambda.
            sPickBefore = filterType(sPickFilterIdx);
            gDropdown.onSelect = nullptr;
            if (sPickTypeParam >= 0)
               gDropdownTestOpenKey = std::pair<int, int>(sPickFilterIdx, sPickTypeParam);
         }
         if (frameId == kPickFrame)
         {
            const bool opened = gDropdownTestOpenKey.first < 0 && gDropdown.onSelect != nullptr &&
                                gDropdown.current == sPickBefore && sPickBefore >= 0;
            const int optionCount = (int)AudioFilterDsp::TypeList().size();
            const int target = optionCount > 1 ? (sPickBefore + 1) % optionCount : sPickBefore;
            const size_t undoBefore = gUndoStack.size();
            gPatchDirty = false;
            if (opened)
               CommitDropdownPick(target);
            const size_t undoAfter = gUndoStack.size();
            const int picked = filterType(sPickFilterIdx);
            const bool dirtyAfterPick = gPatchDirty;
            // Close the popup ImGui opened for the forced click; nothing else
            // in this fixture draws after this frame.
            gDropdown.onSelect = nullptr;
            Undo();
            const int undone = filterType(sPickFilterIdx);
            printf("user dropdown pick: opened with the real onSelect=%d, type %d -> %d (want %d)  %s\n",
                   (int)opened, sPickBefore, picked, target,
                   (opened && picked == target && target != sPickBefore) ? "OK" : "FAIL");
            const bool oneEntry = undoAfter == undoBefore + 1;
            printf("user dropdown pick pushes exactly one undo entry: undo %zu -> %zu, dirty=%d  %s\n",
                   undoBefore, undoAfter, (int)dirtyAfterPick, (oneEntry && dirtyAfterPick) ? "OK" : "FAIL");
            const bool undoRestores = undone == sPickBefore;
            printf("one Undo restores the pre-pick value: type %d (want %d)  %s\n", undone, sPickBefore,
                   undoRestores ? "OK" : "FAIL");
            const bool ok = sDrivenOk && opened && picked == target && target != sPickBefore && oneEntry &&
                            dirtyAfterPick && undoRestores;
            printf("%s\n", ok ? "MOD DROPDOWN UNDO OK" : "SUSPECT");
         }
      }
}

void FrameTest_ARRANGETEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_ARRANGETEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;

         // Every section drives Arrange::Model directly - that is the point of
         // WP1. Resetting keeps revision climbing (WP5b: it is the only change
         // signal, so a model that rewinds it could land back on the value the
         // audio topology was last built at and never rebuild).
         auto seedModel = [](Arrange::Model& m, int videoLanes, int audioLanes)
         {
            const uint64_t rev = m.revision;
            m = Arrange::Model();
            m.revision = rev + 1;
            for (int i = 0; i < videoLanes; i++) Arrange::AddLane(m, Arrange::kLaneVideo);
            for (int i = 0; i < audioLanes; i++) Arrange::AddLane(m, Arrange::kLaneAudio);
         };

         // --- A. Model invariants under the edit ops -----------------------
         {
            Arrange::Model m;
            seedModel(m, 2, 1);
            bool aOk = true;
            std::string why;

            Arrange::Clip c;
            c.start = 0;
            c.length = Arrange::kTicksPerBar;
            c.srcUid = 7;
            uint64_t first = 0, second = 0;
            aOk = aOk && Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &first);
            c.start = Arrange::kTicksPerBar;
            aOk = aOk && Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &second);
            aOk = aOk && m.lanes[0].clips.size() == 2 && first != second;

            // Overwrite straddling both: the left one is truncated, the right
            // one has its head eaten - the exact case index-based editing kept
            // getting wrong.
            c.start = Arrange::kPPQ * 2;
            c.length = Arrange::kPPQ * 4;
            uint64_t third = 0;
            aOk = aOk && Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &third);
            aOk = aOk && m.lanes[0].clips.size() == 3;
            aOk = aOk && Arrange::Validate(m, &why);

            // Splitting inside a clip yields two, and the ids are distinct.
            uint64_t rightHalf = 0;
            aOk = aOk && Arrange::Split(m, third, Arrange::kPPQ * 4, &rightHalf);
            aOk = aOk && rightHalf != 0 && rightHalf != third;
            aOk = aOk && Arrange::Validate(m, &why);

            // A clip that strictly contains an overwrite splits into two, so
            // the lane grows by one rather than losing the tail.
            {
               Arrange::Model m2;
               seedModel(m2, 1, 0);
               Arrange::Clip big;
               big.start = 0;
               big.length = Arrange::kPPQ * 16;
               uint64_t bigId = 0;
               Arrange::PlaceOverwrite(m2, m2.lanes[0].id, big, &bigId);
               Arrange::Clip mid;
               mid.start = Arrange::kPPQ * 4;
               mid.length = Arrange::kPPQ * 4;
               Arrange::PlaceOverwrite(m2, m2.lanes[0].id, mid);
               aOk = aOk && m2.lanes[0].clips.size() == 3 && Arrange::Validate(m2, &why);
            }

            // Groups: two members live, one member deleted dissolves the group
            // rather than leaving the singleton Validate rejects.
            {
               uint64_t gid = 0;
               aOk = aOk && Arrange::Group(m, { first, second }, &gid) && gid != 0;
               aOk = aOk && Arrange::Validate(m, &why);
               aOk = aOk && Arrange::Delete(m, { first });
               const Arrange::Clip* survivor = Arrange::FindClip(m, second);
               aOk = aOk && survivor != nullptr && survivor->groupId == 0;
               aOk = aOk && Arrange::Validate(m, &why);
            }

            // MoveClips is all-or-nothing: onto a lane of the wrong type it
            // must refuse, leaving the model exactly as it was.
            {
               const uint64_t before = m.revision;
               const bool refused = !Arrange::MoveClips(m, { second }, 0, 2); // video -> audio
               aOk = aOk && refused && m.revision == before;
            }

            // Anything that reaches the model without an edit op - the legacy
            // UI bridge appends without sorting - must still come out sorted,
            // non-overlapping and valid.
            {
               Arrange::Model m3;
               seedModel(m3, 1, 0);
               Arrange::Clip a1, a2;
               a1.id = m3.NewId(); a1.start = Arrange::kPPQ * 4; a1.length = Arrange::kPPQ * 4;
               a2.id = m3.NewId(); a2.start = 0;                 a2.length = Arrange::kPPQ * 6;
               m3.lanes[0].clips.push_back(a1);   // deliberately out of order
               m3.lanes[0].clips.push_back(a2);   // and overlapping
               Arrange::Normalize(m3);
               aOk = aOk && m3.lanes[0].clips.size() == 2 &&
                     m3.lanes[0].clips[0].start == 0 &&
                     m3.lanes[0].clips[0].End() == Arrange::kPPQ * 4 &&
                     Arrange::Validate(m3, &why);
            }

            printf("arrange model ops: %s%s%s\n", aOk ? "OK" : "FAIL",
                   why.empty() ? "" : "  reason: ", why.c_str());
            allOk = allOk && aOk;
         }

         // --- B. Fuzz: 2000 seeded random ops, Validate after every one ----
         {
            Arrange::Model m;
            seedModel(m, 3, 2);
            std::mt19937 rng(0xA44A47E5u);
            auto rnd = [&rng](int lo, int hi) { return lo + (int)(rng() % (uint32_t)(hi - lo + 1)); };

            bool bOk = true;
            std::string why;
            int applied = 0;
            for (int i = 0; i < 2000 && bOk; i++)
            {
               // Collect the live ids fresh each iteration - an op may have
               // deleted or split anything from the previous one.
               std::vector<uint64_t> ids;
               for (const Arrange::Lane& l : m.lanes)
                  for (const Arrange::Clip& c : l.clips)
                     ids.push_back(c.id);

               // Group ids and marker ids, also refreshed every iteration.
               std::vector<uint64_t> groups;
               for (const Arrange::Lane& l : m.lanes)
                  for (const Arrange::Clip& c : l.clips)
                     if (c.groupId != 0 &&
                         std::find(groups.begin(), groups.end(), c.groupId) == groups.end())
                        groups.push_back(c.groupId);

               const int op = rnd(0, 14);
               bool changed = false;
               switch (op)
               {
                  case 0: case 1: case 2:
                  {
                     Arrange::Clip c;
                     c.start = (Arrange::Tick)rnd(0, 64) * (Arrange::kPPQ / 4);
                     c.length = (Arrange::Tick)rnd(1, 16) * (Arrange::kPPQ / 4);
                     c.srcUid = (uint64_t)rnd(1, 5);
                     c.fadeIn = (Arrange::Tick)rnd(0, 4) * (Arrange::kPPQ / 4);
                     c.fadeOut = (Arrange::Tick)rnd(0, 4) * (Arrange::kPPQ / 4);
                     c.enabled = rnd(0, 1) != 0;
                     changed = Arrange::PlaceOverwrite(m, m.lanes[rnd(0, (int)m.lanes.size() - 1)].id, c);
                     break;
                  }
                  case 3:
                     if (!ids.empty())
                        changed = Arrange::MoveClips(m, { ids[rnd(0, (int)ids.size() - 1)] },
                                                     (Arrange::Tick)rnd(-8, 8) * (Arrange::kPPQ / 4),
                                                     rnd(-1, 1));
                     break;
                  case 4:
                     if (!ids.empty())
                        changed = Arrange::TrimEdge(m, ids[rnd(0, (int)ids.size() - 1)], rnd(0, 1),
                                                    (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                     break;
                  case 5:
                     if (!ids.empty())
                        changed = Arrange::Split(m, ids[rnd(0, (int)ids.size() - 1)],
                                                 (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                     break;
                  case 6:
                     if (ids.size() >= 2)
                     {
                        std::vector<uint64_t> pick = { ids[rnd(0, (int)ids.size() - 1)],
                                                       ids[rnd(0, (int)ids.size() - 1)] };
                        changed = (rnd(0, 1) == 0) ? Arrange::Group(m, pick)
                                                   : Arrange::DuplicateBlock(m, pick);
                     }
                     break;
                  case 7:
                     if (!ids.empty())
                        changed = Arrange::Delete(m, { ids[rnd(0, (int)ids.size() - 1)] });
                     break;
                  case 8:
                     if (!ids.empty())
                        changed = Arrange::SetEnabled(m, { ids[rnd(0, (int)ids.size() - 1)] }, Arrange::kToggle);
                     break;
                  // The group ops. Ungroup/RemoveFromGroup are the two that
                  // can strand a singleton group, which is invariant 3.
                  case 9:
                     if (!groups.empty())
                     {
                        const uint64_t g = groups[rnd(0, (int)groups.size() - 1)];
                        const int which = rnd(0, 3);
                        if (which == 0)
                           changed = Arrange::Ungroup(m, { g });
                        else if (which == 1)
                        {
                           const std::vector<uint64_t> members = Arrange::ClipsInGroup(m, g);
                           if (!members.empty())
                              changed = Arrange::RemoveFromGroup(m, { members[rnd(0, (int)members.size() - 1)] });
                        }
                        else if (which == 2)
                           changed = Arrange::TrimGroupEdge(m, g, rnd(0, 1),
                                                            (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                        else
                           changed = Arrange::ScaleGroup(m, g, rnd(0, 1),
                                                         (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                     }
                     break;
                  case 10:
                     if (!ids.empty())
                     {
                        // ExpandSelectionToGroups has no side effect, but it
                        // must never return an id the model doesn't hold.
                        const std::vector<uint64_t> sel =
                            Arrange::ExpandSelectionToGroups(m, { ids[rnd(0, (int)ids.size() - 1)] });
                        for (uint64_t id : sel)
                           bOk = bOk && Arrange::FindClip(m, id) != nullptr;
                     }
                     break;
                  // Lane ops. Add is capped so the fuzz doesn't just grow
                  // lanes forever, and remove takes whole lanes of clips with
                  // it - the path most likely to strand a group.
                  case 11:
                     if (m.lanes.size() < 8)
                        changed = Arrange::AddLane(m, rnd(0, 1), rnd(-1, (int)m.lanes.size())) != 0;
                     break;
                  case 12:
                     if (m.lanes.size() > 2)
                        changed = Arrange::RemoveLane(m, m.lanes[rnd(0, (int)m.lanes.size() - 1)].id);
                     break;
                  case 13:
                     if (m.lanes.size() > 1)
                        changed = Arrange::ReorderLane(m, m.lanes[rnd(0, (int)m.lanes.size() - 1)].id,
                                                       rnd(0, (int)m.lanes.size() - 1));
                     break;
                  // Markers, and the "a node went away" path.
                  default:
                  {
                     const int which = rnd(0, 3);
                     if (which == 0)
                        changed = Arrange::AddMarker(m, (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4), "m") != 0;
                     else if (!m.markers.empty())
                     {
                        const uint64_t mk = m.markers[rnd(0, (int)m.markers.size() - 1)].id;
                        if (which == 1)
                           changed = Arrange::MoveMarker(m, mk, (Arrange::Tick)rnd(0, 80) * (Arrange::kPPQ / 4));
                        else if (which == 2)
                           changed = Arrange::DeleteMarker(m, mk);
                        else
                           changed = Arrange::ClearSource(m, (uint64_t)rnd(1, 5));
                     }
                     break;
                  }
               }
               applied += changed ? 1 : 0;
               if (!Arrange::Validate(m, &why))
               {
                  printf("arrange fuzz: invariant broken at op %d (%d): %s\n", i, op, why.c_str());
                  bOk = false;
               }
            }
            printf("arrange fuzz: 2000 ops, %d changed the model  %s\n", applied, bOk ? "OK" : "FAIL");
            allOk = allOk && bOk;
         }

         // --- C. Tick save/load round trip, including markers and settings --
         {
            NewPatch();
            // gNodes is a vector, so the second SpawnNode can reallocate and
            // invalidate the first pointer - read what is needed immediately.
            GraphNode* spawned = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            const uint64_t cubeUid = spawned ? spawned->uid : 0;
            spawned = SpawnNode("Sphere", "3D", 200.0f, 0.0f);
            const uint64_t sphereUid = spawned ? spawned->uid : 0;
            bool cOk = cubeUid != 0 && sphereUid != 0;
            if (cOk)
            {
               Arrange::Model& m = gArrange;
               seedModel(m, 1, 1);
               Arrange::Clip c;
               c.start = Arrange::kPPQ * 3;          // deliberately off the bar
               c.length = Arrange::kPPQ * 5;
               c.srcUid = cubeUid;
               c.fadeIn = Arrange::kPPQ / 3;         // a triplet, exact in ticks
               c.gainDb = -6.0f;
               c.enabled = false;
               c.name = "clip one";
               uint64_t idA = 0, idB = 0;
               Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &idA);
               c.start = Arrange::kPPQ * 9;
               c.srcUid = sphereUid;
               c.enabled = true;
               c.name.clear();
               Arrange::PlaceOverwrite(m, m.lanes[1].id, c, &idB);
               Arrange::Group(m, { idA, idB });
               Arrange::AddMarker(m, Arrange::kTicksPerBar * 2, "chorus", 0xFF00FF00u);
               m.settings.timeDisplay = 1;
               m.settings.snapDivision = 8;
               m.settings.loop.enabled = true;
               m.settings.loop.start = 0;
               m.settings.loop.end = Arrange::kTicksPerBar * 4;
               m.settings.renderFps = 30;
               const uint64_t savedNextId = m.nextId;

               const std::string path = TmpPath("arrange_selftest_tick.inf");
               SavePatchTo(path);
               LoadPatchFrom(path);
               std::remove(path.c_str());

               const Arrange::Model& r = gArrange;
               cOk = r.lanes.size() == 2 && r.lanes[0].clips.size() == 1 && r.lanes[1].clips.size() == 1;
               if (cOk)
               {
                  const Arrange::Clip& ra = r.lanes[0].clips[0];
                  const Arrange::Clip& rb = r.lanes[1].clips[0];
                  // Ticks are exact - no epsilon, which is the whole reason
                  // the time base moved off doubles.
                  cOk = ra.start == Arrange::kPPQ * 3 && ra.length == Arrange::kPPQ * 5 &&
                        ra.fadeIn == Arrange::kPPQ / 3 && ra.gainDb == -6.0f &&
                        !ra.enabled && ra.name == "clip one" && ra.id == idA &&
                        rb.id == idB && ra.groupId != 0 && ra.groupId == rb.groupId;
                  // srcUid survives the whole respawn, which srcIndex could not.
                  GraphNode* ca = FindNodeByUid(ra.srcUid);
                  GraphNode* cb = FindNodeByUid(rb.srcUid);
                  cOk = cOk && ca != nullptr && cb != nullptr && ca->typeName == "Cube" &&
                        cb->typeName == "Sphere";
                  cOk = cOk && r.markers.size() == 1 && r.markers[0].pos == Arrange::kTicksPerBar * 2 &&
                        r.markers[0].name == "chorus" && r.markers[0].color == 0xFF00FF00u;
                  cOk = cOk && r.settings.timeDisplay == 1 && r.settings.snapDivision == 8 &&
                        r.settings.loop.enabled && r.settings.loop.end == Arrange::kTicksPerBar * 4 &&
                        r.settings.renderFps == 30;
                  // nextId is persisted, not recomputed: a reload must not be
                  // able to hand out an id a deleted clip already used.
                  cOk = cOk && r.nextId >= savedNextId;
                  std::string why;
                  cOk = cOk && Arrange::Validate(r, &why);
               }
            }
            printf("arrange tick roundtrip: %s\n", cOk ? "OK" : "FAIL");
            allOk = allOk && cOk;
         }

         // --- D. Legacy seconds patch converts to ticks --------------------
         {
            // 120 bpm -> 1 beat = 0.5 s, so 1.0 s is exactly 2 beats and
            // 2.0 s is 4. The transport line deliberately sits AFTER the clip
            // to prove the conversion waits for the whole file.
            const std::string path = TmpPath("arrange_selftest_legacy.inf");
            {
               std::ofstream f(path);
               f << "infinite-patch 1\n";
               f << "node 1 3D Cube\n";
               f << "end\n";
               f << "stream 0 0 1 0 0 V\n";
               f << "clip 0 1 2 1 0 1 0.25 0 -3 0.5 1\n";
               f << "transport 120 4 4 0 0\n";
            }
            Patch::Data loaded;
            std::string err;
            const bool read = Patch::Read(path, loaded, err);
            std::remove(path.c_str());

            bool dOk = read && loaded.streams.size() == 1 && loaded.streams[0].clips.size() == 1;
            if (dOk)
            {
               const Patch::ClipRecord& c = loaded.streams[0].clips[0];
               dOk = c.startTick == Arrange::kPPQ * 2 && c.lengthTick == Arrange::kPPQ * 4 &&
                     c.fadeInTick == Arrange::kPPQ / 2 && c.gainDb == -3.0f &&
                     c.legacySrcIndex == 1 && c.srcUid == 0 && c.enabled;
            }
            printf("arrange legacy seconds -> ticks: %s\n", dOk ? "OK" : "FAIL");
            allOk = allOk && dOk;
         }

         // --- E. Undo/redo, interleaved with node add and delete -----------
         {
            NewPatch();
            GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            bool eOk = cube != nullptr;
            const uint64_t cubeUid = cube ? cube->uid : 0;
            const int cubeIndex = cube ? cube->index : -1;
            if (eOk)
            {
               seedModel(gArrange, 1, 0);
               Arrange::Clip c;
               c.length = Arrange::kTicksPerBar;
               c.srcUid = cubeUid;
               uint64_t clipId = 0;
               Arrange::PlaceOverwrite(gArrange, gArrange.lanes[0].id, c, &clipId);

               // A timeline-only gesture: the entry must not respawn the graph
               // on undo, so the node's pointer identity survives it.
               const GraphNode* before = FindNodeByUid(cubeUid);
               PushArrangeUndo();
               Arrange::MoveClips(gArrange, { clipId }, Arrange::kTicksPerBar, 0);
               eOk = eOk && gArrange.lanes[0].clips[0].start == Arrange::kTicksPerBar;

               Undo();
               eOk = eOk && gArrange.lanes[0].clips[0].start == 0;
               eOk = eOk && FindNodeByUid(cubeUid) == before;   // no respawn
               Redo();
               eOk = eOk && gArrange.lanes[0].clips[0].start == Arrange::kTicksPerBar;
               Undo();

               // A graph gesture: deleting the node must leave the clip in
               // place but offline, and undo must re-attach it by uid.
               const size_t clipsBefore = gArrange.lanes[0].clips.size();
               PushUndoCheckpoint();
               RemoveNodeByIndex(cubeIndex);
               // Offline (WP5): the clip stays, its source is cleared to 0
               // so it draws "Unassigned"; the link lives in the undo entry.
               eOk = eOk && gArrange.lanes.size() == 1 &&
                     gArrange.lanes[0].clips.size() == clipsBefore &&
                     gArrange.lanes[0].clips[0].srcUid == 0 &&
                     FindNodeByUid(cubeUid) == nullptr;

               Undo();
               eOk = eOk && gArrange.lanes.size() == 1 && gArrange.lanes[0].clips.size() == clipsBefore;
               if (eOk)
               {
                  const uint64_t restored = gArrange.lanes[0].clips[0].srcUid;
                  GraphNode* back = FindNodeByUid(restored);
                  eOk = restored == cubeUid && back != nullptr && back->typeName == "Cube";
               }
               std::string why;
               eOk = eOk && Arrange::Validate(gArrange, &why);
            }
            printf("arrange undo redo + node delete: %s\n", eOk ? "OK" : "FAIL");
            allOk = allOk && eOk;
         }

         // --- F. File->New clears the model --------------------------------
         {
            NewPatch();
            // New seeds the default four video + four audio lanes, empty.
            int videoLanes = 0, audioLanes = 0;
            size_t clips = 0;
            for (const Arrange::Lane& l : gArrange.lanes)
            {
               (l.type == Arrange::kLaneVideo ? videoLanes : audioLanes)++;
               clips += l.clips.size();
            }
            const bool fOk = gArrange.lanes.size() == 8 && videoLanes == 4 && audioLanes == 4 &&
                             clips == 0 && gArrange.markers.empty() &&
                             Arrange::ArrangementEnd(gArrange) == 0;
            printf("arrange new patch: %s\n", fOk ? "OK" : "FAIL");
            allOk = allOk && fOk;
         }

         // --- G. Unknown tags and malformed lines ---------------------------
         {
            const std::string path = TmpPath("arrange_selftest_malformed.inf");
            {
               std::ofstream f(path);
               f << "infinite-patch 1\n";
               f << "node 1 3D Cube\n";
               f << "end\n";
               f << "stream 1\n";                      // malformed, kept with defaults
               f << "stream 0 0 1 0 0 V\n";
               f << "arrangefuture 1 2 3\n";           // from a newer build
               f << "cliptick 7 0 0 960 0\n";          // out-of-range lane, dropped
               f << "cliptick 0 0 0 0 0\n";            // zero length, dropped
               f << "cliptick 0 0 -5 960 0\n";         // negative start, dropped
               f << "cliptick 0 0 0 960 0 0 99999 0\n"; // fade past the end, clamped
               f << "marker 0 -4 0 bad\n";             // negative position, dropped
               f << "marker 0 1920 4278190080 good\n";
            }
            Patch::Data loaded;
            std::string err;
            const bool read = Patch::Read(path, loaded, err);
            std::remove(path.c_str());

            bool gOk = read && loaded.streams.size() == 2;
            if (gOk)
            {
               // The malformed `stream 1` line is kept with defaults rather
               // than dropped: the clip lines below address their lane by
               // position, so dropping it would move every later clip onto the
               // wrong lane. All four cliptick lines name lane 0, which IS
               // that malformed line - only the last one survives its checks.
               gOk = loaded.streams[0].type == 1 && loaded.streams[0].opacity == 1.0f &&
                     loaded.streams[0].name.empty() &&
                     loaded.streams[0].clips.size() == 1 &&
                     loaded.streams[0].clips[0].fadeInTick == 960 &&
                     loaded.streams[1].clips.empty() &&
                     loaded.markers.size() == 1 && loaded.markers[0].name == "good";
            }
            printf("arrange malformed input: %s\n", gOk ? "OK" : "FAIL");
            allOk = allOk && gOk;
         }

         // --- H. JSON parity -----------------------------------------------
         {
            NewPatch();
            GraphNode* cube = SpawnNode("Cube", "3D", 0.0f, 0.0f);
            seedModel(gArrange, 1, 0);
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            c.srcUid = cube ? cube->uid : 0;
            Arrange::PlaceOverwrite(gArrange, gArrange.lanes[0].id, c);
            Arrange::AddMarker(gArrange, Arrange::kPPQ, "m");

            nlohmann::json j = PatchJson::ToJson(BuildPatchData());
            const bool hOk = j.contains("streams") && j["streams"].is_array() &&
                             j["streams"].size() == gArrange.lanes.size() &&
                             j["streams"][0]["clips"].is_array() &&
                             j["streams"][0]["clips"].size() == gArrange.lanes[0].clips.size() &&
                             j["streams"][0]["clips"][0].contains("startTick") &&
                             j.contains("markers") && j["markers"].size() == 1 &&
                             j.contains("arrange") && j["arrange"].contains("nextId") &&
                             j["nodes"][0].contains("uid");
            printf("arrange json parity: %s\n", hOk ? "OK" : "FAIL");
            allOk = allOk && hOk;
         }

         // --- I. Ids are unique and never reused ---------------------------
         // The two defects the WP1 review found, both of which reached disk:
         // the legacy UI's copy paths clone a clip record verbatim (id and
         // all), and undo used to restore nextId along with the snapshot.
         {
            NewPatch();
            seedModel(gArrange, 1, 0);
            const uint64_t laneId = gArrange.lanes[0].id;
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            uint64_t firstId = 0;
            Arrange::PlaceOverwrite(gArrange, laneId, c, &firstId);

            // (1) A duplicate id arriving from outside an edit op - exactly
            // what Cmd+D used to produce - must be re-minted, not accepted.
            Arrange::Clip clone = gArrange.lanes[0].clips[0];
            clone.start = Arrange::kTicksPerBar * 2;
            gArrange.lanes[0].clips.push_back(clone);   // same id, deliberately
            gArrange.revision++;                         // a direct edit is still a change
            Arrange::Normalize(gArrange);
            std::string why;
            bool iOk = gArrange.lanes[0].clips.size() == 2 &&
                       gArrange.lanes[0].clips[0].id != gArrange.lanes[0].clips[1].id &&
                       Arrange::Validate(gArrange, &why);

            // (2) nextId only ever climbs. Snapshot, spend an id, undo, and
            // the next id handed out must still be a fresh one.
            PushArrangeUndo();
            uint64_t spentId = 0;
            Arrange::Clip extra;
            extra.start = Arrange::kTicksPerBar * 8;
            extra.length = Arrange::kTicksPerBar;
            Arrange::PlaceOverwrite(gArrange, laneId, extra, &spentId);
            Undo();
            iOk = iOk && spentId != 0 && gArrange.nextId > spentId;

            Arrange::Clip after;
            after.start = Arrange::kTicksPerBar * 12;
            after.length = Arrange::kTicksPerBar;
            uint64_t afterId = 0;
            Arrange::PlaceOverwrite(gArrange, laneId, after, &afterId);
            iOk = iOk && afterId != spentId && Arrange::Validate(gArrange, &why);

            printf("arrange id uniqueness: %s\n", iOk ? "OK" : "FAIL");
            allOk = allOk && iOk;
         }

         // --- J. Lane mix + per-clip compositing: round trip, legacy, undo --
         {
            NewPatch();
            Arrange::Model& m = gArrange;
            seedModel(m, 1, 2);
            Arrange::Clip c;
            c.length = Arrange::kTicksPerBar;
            uint64_t idNormal = 0, idScreen = 0;
            Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &idNormal);
            c.start = Arrange::kTicksPerBar * 2;
            c.blendMode = 5;
            Arrange::PlaceOverwrite(m, m.lanes[0].id, c, &idScreen);
            m.lanes[0].opacity = 0.4f;
            m.lanes[1].mute = true;
            m.lanes[1].pan = -0.5f;
            m.lanes[1].gainDb = -3.0f;
            m.lanes[2].solo = true;

            const std::string path = TmpPath("arrange_selftest_mix.inf");
            SavePatchTo(path);
            LoadPatchFrom(path);
            std::remove(path.c_str());
            const Arrange::Model& r = gArrange;
            bool rtOk = r.lanes.size() == 3 && r.lanes[0].clips.size() == 2;
            if (rtOk)
            {
               const Arrange::Clip* a = Arrange::FindClip(r, idNormal);
               const Arrange::Clip* b = Arrange::FindClip(r, idScreen);
               rtOk = a != nullptr && b != nullptr && a->blendMode == 0 && b->blendMode == 5 &&
                      r.lanes[0].opacity == 0.4f && r.lanes[1].mute && !r.lanes[1].solo &&
                      r.lanes[1].pan == -0.5f && r.lanes[1].gainDb == -3.0f && r.lanes[2].solo &&
                      !r.lanes[2].mute && r.lanes[0].blendMode == 0;
            }

            // A file from before per-clip compositing: the stream line's
            // lane-wide mode lands on every clip that has no clipblend line
            // of its own, and the lane field is left at 0.
            const std::string legacyPath = TmpPath("arrange_selftest_laneblend.inf");
            {
               std::ofstream f(legacyPath);
               f << "infinite-patch 1\n";
               f << "node 1 3D Cube\n";
               f << "end\n";
               f << "stream 0 7 0.5 0 0 V\n";
               f << "streamid 0 50\n";
               f << "cliptick 0 51 0 960 0\n";
               f << "cliptick 0 52 1920 960 0\n";
               f << "clipblend 0 52 3\n";
               f << "clipblend 0 99 4\n"; // no such clip: ignored
               f << "stream 1 0 1 0 0 A\n";
               f << "streammix 1 1 0\n";
               f << "streammix 9 1 1\n"; // out-of-range stream: ignored
            }
            Patch::Data legacy;
            std::string err;
            const bool read = Patch::Read(legacyPath, legacy, err);
            std::remove(legacyPath.c_str());
            Arrange::Model lm;
            if (read)
               PatchDataToArrangeModel(legacy, lm);
            bool legOk = read && lm.lanes.size() == 2 && lm.lanes[0].clips.size() == 2;
            if (legOk)
            {
               const Arrange::Clip* l51 = Arrange::FindClip(lm, 51);
               const Arrange::Clip* l52 = Arrange::FindClip(lm, 52);
               legOk = l51 != nullptr && l52 != nullptr && l51->blendMode == 7 && l52->blendMode == 3 &&
                       lm.lanes[0].blendMode == 0 && lm.lanes[1].mute && !lm.lanes[1].solo;
               // And back out: the stream field is written as 0, each clip
               // carries its own mode.
               Patch::Data back;
               ArrangeModelToPatchData(lm, back);
               legOk = legOk && back.streams[0].blendMode == 0 && back.streams[0].clips[0].blendMode == 7 &&
                       back.streams[0].clips[1].blendMode == 3 && back.streams[1].mute;
            }

            // Mute and clip compositing are undo state; a gesture that ends
            // where it started leaves no entry.
            bool undoOk = true;
            {
               const uint64_t audioLane = gArrange.lanes[1].id;
               const bool wasMuted = Arrange::FindLane(gArrange, audioLane)->mute;
               ArrangeGestureBegin();
               Arrange::FindLane(gArrange, audioLane)->mute = !wasMuted;
               gArrange.revision++;
               ArrangeGestureEnd();
               Undo();
               undoOk = Arrange::FindLane(gArrange, audioLane)->mute == wasMuted;
               ArrangeEdit([&]()
               {
                  if (Arrange::Clip* cc = Arrange::FindClip(gArrange, idNormal))
                  {
                     cc->blendMode = 9;
                     gArrange.revision++;
                  }
               });
               const bool applied = Arrange::FindClip(gArrange, idNormal)->blendMode == 9;
               Undo();
               undoOk = undoOk && applied && Arrange::FindClip(gArrange, idNormal)->blendMode == 0;
               Redo();
               undoOk = undoOk && Arrange::FindClip(gArrange, idNormal)->blendMode == 9;
               const float panWas = Arrange::FindLane(gArrange, audioLane)->pan;
               ArrangeGestureBegin();
               Arrange::FindLane(gArrange, audioLane)->pan = 0.75f;
               gArrange.revision++;
               Arrange::FindLane(gArrange, audioLane)->pan = panWas; // dragged back
               gArrange.revision++;
               ArrangeGestureEnd();
               Undo(); // must undo the blend edit, not an empty pan gesture
               undoOk = undoOk && Arrange::FindClip(gArrange, idNormal)->blendMode == 0 &&
                        Arrange::FindLane(gArrange, audioLane)->pan == panWas;
            }
            const bool jOk = rtOk && legOk && undoOk;
            printf("arrange lane mix + clip compositing: %s (round trip %d, legacy lane blend %d, undo %d)\n",
                   jOk ? "OK" : "FAIL", (int)rtOk, (int)legOk, (int)undoOk);
            allOk = allOk && jOk;
         }

         printf("arrange test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}

void FrameTest_TRANSPORTTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_TRANSPORTTEST") != nullptr && frameId == 4)
      {
         Transport& tr = Transport::Instance();
         bool allOk = true;

         const bool hadEngine = AudioEngine::Instance().SampleRate() > 0.0;
         AudioEngine::Instance().Stop();
         tr.NotifyAudioEngineStopped();

         const double kSr = 48000.0;
         const int kBlock = 512;
         const double kBlockSec = (double)kBlock / kSr;

         auto startFakeEngine = [&]() {
            tr.NotifyAudioEngineStarted(kSr);
         };

         // --- A. Tempo change mid-play doesn't move the playhead ------------
         {
            tr.SetTempo(120.0f);
            tr.SetLoop(false, 0.0, 0.0);
            tr.SetPlaying(true);
            tr.Seek(0.0);
            startFakeEngine();

            for (int i = 0; i < 400; i++)      // ~4.3 s at 120 bpm
               tr.AdvanceAudioClock(kBlock);

            const double before = tr.Beats();
            tr.SetTempo(240.0f);
            tr.AdvanceAudioClock(kBlock);      // the block that applies it
            const double after = tr.Beats();

            // One block at the *new* tempo is the largest legitimate step.
            const double maxStep = kBlockSec * (240.0 / 60.0) * 1.001;
            const bool continuous = std::fabs(after - before) <= maxStep;

            // ...and the tempo really did change: the next block must advance
            // at twice the old rate.
            const double b0 = tr.Beats();
            tr.AdvanceAudioClock(kBlock);
            const double rate = (tr.Beats() - b0) / kBlockSec;
            const bool doubled = std::fabs(rate - 4.0) < 0.01;

            const bool aOk = continuous && doubled;
            printf("transport tempo continuity: %s (jump %.6f beats, max %.6f; rate %.3f)\n",
                   aOk ? "OK" : "FAIL", std::fabs(after - before), maxStep, rate);
            allOk = allOk && aOk;
         }

         // --- B. SeekBeats ---------------------------------------------------
         {
            tr.SetTempo(120.0f);
            tr.SeekBeats(8.0);
            tr.AdvanceAudioClock(0);           // consume the pending seek
            const bool bOk = std::fabs(tr.Beats() - 8.0) < 1e-6 &&
                             std::fabs(tr.Seconds() - 4.0) < 1e-6 &&
                             (tr.SeekBeats(-3.0), tr.AdvanceAudioClock(0), tr.Beats() >= 0.0);
            printf("transport seek beats: %s\n", bOk ? "OK" : "FAIL");
            allOk = allOk && bOk;
         }

         // --- C. Loop wraps within one block ---------------------------------
         {
            tr.SetTempo(120.0f);
            tr.SetPlaying(true);
            tr.SeekBeats(0.0);
            tr.SetLoop(true, 2.0, 6.0);        // a 4-beat / 2-second loop
            tr.AdvanceAudioClock(0);

            const double oneBlockBeats = kBlockSec * 2.0;   // 120 bpm
            double worstOvershoot = 0.0;
            int laps = 0;
            double prev = tr.Beats();
            for (int i = 0; i < 2000; i++)     // ~21 s, five laps' worth
            {
               tr.AdvanceAudioClock(kBlock);
               const double b = tr.Beats();
               worstOvershoot = std::max(worstOvershoot, b - 6.0);
               if (b < prev)
                  laps++;
               prev = b;
            }
            // Never past the end by more than one block, and it really looped
            // rather than simply stopping.
            const bool cOk = worstOvershoot <= oneBlockBeats * 1.001 && laps >= 4 &&
                             tr.Beats() >= 2.0 && tr.Beats() < 6.0;
            printf("transport loop wrap: %s (%d laps, worst overshoot %.6f beats, budget %.6f)\n",
                   cOk ? "OK" : "FAIL", laps, worstOvershoot, oneBlockBeats);
            allOk = allOk && cOk;
         }

         // --- D. Offline render suspends the loop ----------------------------
         {
            tr.SetTempo(120.0f);
            tr.SetPlaying(true);
            tr.SeekBeats(0.0);
            tr.SetLoop(true, 0.0, 4.0);
            tr.SetOfflineMode(true, kSr);
            // Read inside the block: offline, Seconds() outside a block is the
            // video clock, which only moves when the renderer calls
            // SetOfflineVideoTime. The audio clock is only live between
            // Begin/EndOfflineAudioBlock, which is where the wrap would have
            // fired if the loop weren't suspended.
            double offlineBeats = 0.0;
            for (int i = 0; i < 600; i++)      // well past four beats
            {
               tr.BeginOfflineAudioBlock(kBlock);
               offlineBeats = tr.Beats();
               tr.EndOfflineAudioBlock();
            }
            tr.SetOfflineMode(false, 0.0);
            // ...and the user's loop is still armed afterwards.
            const bool dOk = offlineBeats > 4.0 && tr.LoopEnabled() &&
                             std::fabs(tr.LoopEndBeats() - 4.0) < 1e-9;
            printf("transport offline suspends loop: %s (reached %.2f beats)\n",
                   dOk ? "OK" : "FAIL", offlineBeats);
            allOk = allOk && dOk;
         }

         // --- F. The block that laps keeps its own start on the pre-wrap axis
         // BlockStartBeats() is what RunTopology builds its per-sample beat
         // axis from. If it were derived as Beats() - numFrames*rate it would
         // land on the NEW lap for the block that crossed the loop end, and
         // every sample of that block would be re-placed at the top of the
         // loop - which silences the last few ms before the loop point on
         // every lap, with a declick ramp instead of continuity.
         {
            tr.SetTempo(120.0f);
            tr.SetPlaying(true);
            tr.Seek(0.0);
            tr.SetLoop(true, 0.0, 4.0);
            startFakeEngine();

            const double blockBeats = kBlockSec * 2.0; // 120 bpm = 2 beats/s
            bool sawLap = false;
            bool axisOk = true;
            double prevBeats = tr.Beats();
            for (int i = 0; i < 600; i++)
            {
               tr.AdvanceAudioClock(kBlock);
               const double now = tr.Beats();
               if (now < prevBeats) // this block crossed the loop end
               {
                  sawLap = true;
                  const double start = tr.BlockStartBeats();
                  // The start must sit in the last block before the loop end,
                  // not at the top of the new lap.
                  axisOk = axisOk && start <= 4.0 && start > 4.0 - blockBeats * 1.001;
               }
               prevBeats = now;
            }
            tr.SetLoop(false, 0.0, 0.0);
            const bool fOk = sawLap && axisOk;
            printf("transport block start across loop: %s (laps seen %d)\n",
                   fOk ? "OK" : "FAIL", (int)sawLap);
            allOk = allOk && fOk;
         }

         // --- E. Loop also wraps on the no-engine fallback clock -------------
         {
            tr.NotifyAudioEngineStopped();     // back to Tick()-driven
            tr.SetTempo(120.0f);
            tr.SetPlaying(true);
            tr.Seek(0.0);
            tr.SetLoop(true, 0.0, 2.0);        // 1 second at 120 bpm
            bool wrapped = false;
            double worst = 0.0;
            for (int i = 0; i < 300; i++)
            {
               tr.Tick(1.0f / 60.0f);
               const double b = tr.Beats();
               worst = std::max(worst, b - 2.0);
               if (b < 2.0 && i > 40)
                  wrapped = true;
            }
            const double frameBeats = (1.0 / 60.0) * 2.0;
            const bool eOk = wrapped && worst <= frameBeats * 1.001;
            printf("transport loop wrap (no engine): %s (worst overshoot %.6f beats)\n",
                   eOk ? "OK" : "FAIL", worst);
            allOk = allOk && eOk;
         }

         // Leave the transport the way the rest of the session expects it.
         tr.SetLoop(false, 0.0, 0.0);
         tr.SetTempo(120.0f);
         tr.Seek(0.0);
         tr.SetPlaying(true);
         if (hadEngine)
         {
            std::string startErr;
            if (AudioEngine::Instance().Start(startErr))
               tr.NotifyAudioEngineStarted(AudioEngine::Instance().SampleRate());
         }

         printf("transport test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}

void FrameTest_ARRANGESAMPLEEXPORTTEST(int frameId, GLFWwindow* window)
{
   if (const char* exportDir = getenv("INFINITE_ARRANGESAMPLEEXPORTTEST"))
      {
         static bool sExportQueued = false;
         static bool sExportReported = false;
         if (frameId == 4)
         {
            NewPatch();
            Transport::Instance().SetTempo(120.0f);
            Transport::Instance().SetLoop(false, 0.0, 0.0);
            Arrange::gSampleLiveTempoBpm = 120.0;
            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            const uint64_t vLane = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
            const uint64_t aLane = Arrange::AddLane(gArrange, Arrange::kLaneAudio);

            const std::string wavPath = TmpPath("infinite_arrangeexport_click.wav");
            {
               const double fileSr = 44100.0;
               const int frames = (int)(10.0 * fileSr);
               std::vector<float> inter((size_t)frames * 2, 0.0f);
               for (int k = 0; k * 0.6 < 10.0; k++)
               {
                  const int f0 = (int)std::llround(k * 0.6 * fileSr);
                  for (int j = 0; j < (int)(0.006 * fileSr) && f0 + j < frames; j++)
                  {
                     const float v = (float)(0.8 * std::exp(-(double)j / (0.0015 * fileSr)) *
                                             std::sin(2.0 * 3.14159265358979 * 2000.0 * j / fileSr));
                     inter[(size_t)(f0 + j) * 2] = v;
                     inter[(size_t)(f0 + j) * 2 + 1] = v;
                  }
               }
               AudioRecordings::WriteWav(wavPath, inter.data(), frames, fileSr, 2);
            }
            ArrangeImportMediaFile(wavPath, aLane, Arrange::BeatsToTicks(2.0), Arrange::ImportMediaKind::Audio);
            for (int i = 0; i < 500 && !gArrangePendingImports.empty(); i++)
            {
               ArrangePollMediaImports();
               std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const Arrange::Lane* al = Arrange::FindLane(gArrange, aLane);
            const bool imported = gArrangePendingImports.empty() && al != nullptr && !al->clips.empty() &&
                                  al->clips.front().syncToTempo;

            GraphNode* rampGn = SpawnNode("Ramp", "Source", 0.0f, 0.0f);
            if (rampGn != nullptr)
            {
               Arrange::Clip v;
               v.start = Arrange::BeatsToTicks(2.0);
               v.length = Arrange::BeatsToTicks(8.0);
               v.srcUid = rampGn->uid;
               Arrange::PlaceOverwrite(gArrange, vLane, v);
            }
            const bool haveDevice = AudioEngine::Instance().SampleRate() > 0.0 || StartAudioEngine(gAudioStartError);
            printf("arrange sample export setup: %s (imported %d, ramp %d, device %d)\n",
                   imported && rampGn != nullptr && haveDevice ? "OK" : "FAIL", (int)imported,
                   (int)(rampGn != nullptr), (int)haveDevice);

            gArrangeRenderQueue.clear();
            gArrangeRenderActiveJobId = 0;
            auto job = [&](int video, int format, const std::string& path)
            {
               ArrangeRenderJob j;
               j.id = gArrangeRenderNextJobId++;
               j.startTick = 0;
               j.endTick = Arrange::BeatsToTicks(12.0); // 6 s
               j.audioSource = kArrangeAudioTimeline;
               j.videoSource = video;
               j.width = 320;
               j.height = 180;
               j.fps = 30;
               j.format = format;
               j.path = path;
               std::error_code ec;
               std::filesystem::remove(path, ec);
               gArrangeRenderQueue.push_back(j);
            };
            job(kArrangeVideoNone, 2, std::string(exportDir) + "/export_audio.wav");
            job(kArrangeVideoTimeline, 0, std::string(exportDir) + "/export_av.mp4");
            gArrangeRenderQueueRunning = true;
            sExportQueued = true;
         }
         else if (sExportQueued && !sExportReported && !gArrangeRenderQueueRunning && !ArrangeRenderBusy())
         {
            sExportReported = true;
            for (const ArrangeRenderJob& j : gArrangeRenderQueue)
               printf("arrange sample export job %s: status %d (%s) %s\n", j.path.c_str(), j.status,
                      j.status == kArrangeJobDone ? "done" : "NOT DONE", j.message.c_str());
            printf("arrange sample export finished at frame %d\n", frameId);
         }
      }
}

void FrameTest_ARRANGEVIDEOTEST(int frameId, GLFWwindow* window)
{
   if (getenv("INFINITE_ARRANGEVIDEOTEST") != nullptr && frameId == 4)
      {
         NewPatch();
         bool allOk = true;
         Transport& tr = Transport::Instance();
         tr.SetTempo(120.0f);
         tr.Seek(0.0); // commits the tempo (clips are in ticks, so it only fixes the clock)
         const double kBeat = 1.0; // the instant every check composites at

         const int kSize = 64;
         auto spawnRamp = [&](float r, float g, float b) -> int
         {
            GraphNode* gn = SpawnNode("Ramp", "Source", 0.0f, 0.0f);
            auto* ramp = gn != nullptr ? dynamic_cast<RampNode*>(gn->node.get()) : nullptr;
            if (ramp == nullptr)
               return -1;
            ramp->width = (float)kSize;
            ramp->height = (float)kSize;
            for (int s = 0; s < 2; s++)
            {
               ramp->stopColor[s][0] = r;
               ramp->stopColor[s][1] = g;
               ramp->stopColor[s][2] = b;
            }
            return gn->index;
         };
         const int redIndex = spawnRamp(1.0f, 0.0f, 0.0f);
         const int blueIndex = spawnRamp(0.0f, 0.0f, 1.0f);
         GraphNode* cubeGn = SpawnNode("Cube", "3D", 0.0f, 0.0f);
         const int cubeIndex = cubeGn != nullptr ? cubeGn->index : -1;
         const uint64_t cubeUid = cubeGn != nullptr ? cubeGn->uid : 0;
         cubeGn = nullptr; // SpawnNode pointers dangle across later spawns
         const bool spawned = redIndex >= 0 && blueIndex >= 0 && cubeIndex >= 0 &&
                              dynamic_cast<IGeometrySource*>(FindNodeByIndex(cubeIndex)->node.get()) != nullptr;
         printf("arrange video spawn: %s\n", spawned ? "OK" : "FAIL");
         allOk = allOk && spawned;

         if (spawned)
         {
            // Lane 0 is the TOP lane in the panel, lane 1 the one below it.
            // Built straight into gArrange (WP5b); revision keeps climbing
            // across the reset so nothing watching it sees a rewind.
            const uint64_t revBefore = gArrange.revision;
            gArrange = Arrange::Model();
            gArrange.revision = revBefore + 1;
            uint64_t clipIds[2] = { 0, 0 };
            for (int l = 0; l < 2; l++)
            {
               const uint64_t laneId = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
               Arrange::FindLane(gArrange, laneId)->name = l == 0 ? "V1" : "V2";
               Arrange::Clip c;
               c.start = 0;
               c.length = Arrange::BeatsToTicks(4.0); // beats [0, 4)
               c.srcUid = FindNodeByIndex(l == 0 ? redIndex : blueIndex)->uid;
               Arrange::PlaceOverwrite(gArrange, laneId, c, &clipIds[l]);
            }
            const uint64_t topId = clipIds[0];
            const uint64_t bottomId = clipIds[1];
            auto setTopSource = [&](uint64_t uid)
            {
               Arrange::FindClip(gArrange, topId)->srcUid = uid;
               gArrange.revision++;
            };

            int cookFrame = 2000000;
            auto cookAll = [&]()
            {
               cookFrame++;
               for (GraphNode& gn : gNodes)
                  gn.node->CookIfNeeded(cookFrame);
            };
            auto readCenter = [&](const GLUtil::Fbo& f, unsigned char* px)
            {
               GLint prev = 0;
               glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
               glBindFramebuffer(GL_FRAMEBUFFER, f.fbo);
               glReadPixels(f.w / 2, f.h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
               glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
            };

            ArrangeCompositeTarget t{ 90 };
            auto compositeAndRead = [&](unsigned char* px)
            {
               cookAll();
               CompositeArrangeTimelineVideo(t, nullptr, kBeat, kSize, kSize);
               readCenter(t.result, px);
            };
            unsigned char px[4] = { 0, 0, 0, 0 };

            // --- A. Top lane is frontmost ---------------------------------
            compositeAndRead(px);
            const bool aOk = px[0] > 200 && px[2] < 40;
            printf("arrange video top lane frontmost: %s (rgb %d,%d,%d)\n", aOk ? "OK" : "FAIL", px[0], px[1], px[2]);
            allOk = allOk && aOk;

            // --- B. Model opacity is honoured (no UI) ---------------------
            gArrange.lanes[0].opacity = 0.5f;
            gArrange.revision++;
            compositeAndRead(px);
            const bool bOk = px[0] > 100 && px[0] < 155 && px[2] > 100 && px[2] < 155;
            printf("arrange video lane opacity: %s (rgb %d,%d,%d)\n", bOk ? "OK" : "FAIL", px[0], px[1], px[2]);
            allOk = allOk && bOk;
            gArrange.lanes[0].opacity = 1.0f;
            gArrange.revision++;

            // --- C. A disabled clip is skipped ----------------------------
            Arrange::SetEnabled(gArrange, { topId }, Arrange::kDisable);
            compositeAndRead(px);
            const int countDisabled = CountActiveArrangeVideoClips(kBeat);
            const bool cOk = px[2] > 200 && px[0] < 40 && countDisabled == 1;
            printf("arrange video disabled clip skipped: %s (rgb %d,%d,%d, active %d)\n",
                   cOk ? "OK" : "FAIL", px[0], px[1], px[2], countDisabled);
            allOk = allOk && cOk;
            Arrange::SetEnabled(gArrange, { topId }, Arrange::kEnable);

            // --- D. An unassigned clip is skipped -------------------------
            setTopSource(0);
            compositeAndRead(px);
            const bool dOk = px[2] > 200 && px[0] < 40 && CountActiveArrangeVideoClips(kBeat) == 1;
            printf("arrange video unassigned clip skipped: %s (rgb %d,%d,%d)\n", dOk ? "OK" : "FAIL", px[0], px[1], px[2]);
            allOk = allOk && dOk;

            // --- E. Nothing usable -> opaque black ------------------------
            Arrange::SetEnabled(gArrange, { bottomId }, Arrange::kDisable);
            compositeAndRead(px);
            const bool eOk = px[0] < 10 && px[1] < 10 && px[2] < 10 && px[3] > 245 &&
                             CountActiveArrangeVideoClips(kBeat) == 0;
            printf("arrange video nothing active clears black: %s (rgba %d,%d,%d,%d)\n",
                   eOk ? "OK" : "FAIL", px[0], px[1], px[2], px[3]);
            allOk = allOk && eOk;
            Arrange::SetEnabled(gArrange, { bottomId }, Arrange::kEnable);

            // --- F. Geometry clip: zero FBO allocations over 100 frames ---
            // Two targets at different sizes composite the same geometry
            // clip every frame, the shape of a render running under an open
            // monitor - per-target keying is what keeps them from thrashing.
            setTopSource(cubeUid);
            ArrangeCompositeTarget t2{ 91 };
            const size_t panelViewportsBefore = gPanelViewports.size();
            auto geomFrame = [&]()
            {
               cookAll();
               CompositeArrangeTimelineVideo(t, nullptr, kBeat, kSize, kSize);
               CompositeArrangeTimelineVideo(t2, nullptr, kBeat, 96, 54);
               ReapArrangeGeomViewports();
            };
            geomFrame(); // warm-up: first sight allocates each target's viewport + scratch
            const unsigned long long allocsBefore = GLUtil::FboAllocationCount();
            for (int f = 0; f < 100; f++)
               geomFrame();
            const unsigned long long allocs = GLUtil::FboAllocationCount() - allocsBefore;
            const bool cached = gArrangeGeomViewports.count({ cubeUid, 90 }) == 1 &&
                                gArrangeGeomViewports.count({ cubeUid, 91 }) == 1;
            const bool panelUntouched = gPanelViewports.size() == panelViewportsBefore &&
                                        gPanelViewports.count(cubeIndex) == 0;
            const bool fOk = allocs == 0 && cached && panelUntouched;
            printf("arrange video geometry clip steady state: %s (%llu FBO allocations in 100 frames, cached %d, panel viewports untouched %d)\n",
                   fOk ? "OK" : "FAIL", allocs, cached ? 1 : 0, panelUntouched ? 1 : 0);
            allOk = allOk && fOk;

            // --- G. Unused geometry viewports are evicted -----------------
            Arrange::SetEnabled(gArrange, { topId }, Arrange::kDisable);
            for (uint64_t f = 0; f < kArrangeGeomEvictFrames; f++)
               geomFrame();
            const bool stillThere = gArrangeGeomViewports.count({ cubeUid, 90 }) == 1;
            geomFrame();
            const bool evicted = gArrangeGeomViewports.count({ cubeUid, 90 }) == 0 &&
                                 gArrangeGeomViewports.count({ cubeUid, 91 }) == 0;
            const bool gOk = stillThere && evicted;
            printf("arrange video geometry cache eviction: %s (kept through %llu frames %d, then evicted %d)\n",
                   gOk ? "OK" : "FAIL", (unsigned long long)kArrangeGeomEvictFrames, stillThere ? 1 : 0, evicted ? 1 : 0);
            allOk = allOk && gOk;

            for (ArrangeCompositeTarget* ft : { &t, &t2 })
            {
               GLUtil::DestroyFbo(ft->scratch[0]);
               GLUtil::DestroyFbo(ft->scratch[1]);
               GLUtil::DestroyFbo(ft->result);
               GLUtil::DestroyFbo(ft->retiredResult);
            }
         }

         NewPatch();
         printf("arrange video test: all  %s\n", allOk ? "OK" : "FAIL");
      }
}
}

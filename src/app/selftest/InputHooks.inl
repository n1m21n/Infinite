// Part of src/main.cpp, included there in order (one translation unit).
// INFINITE_*TEST input injection hooks (dev runs only)

      // dev-only: click inside the colour picker to prove it is interactive now
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

      // dev-only: the whole "/" flow with nothing else touched - press slash on
      // an empty canvas and type. This is the path that has to stay one
      // keystroke, so it is driven exactly as a person would drive it.
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

      // dev-only: double-click an existing note, the way an edit (rather than a
      // fresh comment) starts.
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

      // dev-only: synthetic mouse drags to prove empty-canvas drag pans the view
      // while a drag that starts on a node still moves that node. The position is
      // re-asserted every frame, otherwise the GLFW backend's real cursor wins on
      // frames we don't touch and the gesture breaks up.
      // Drags the Wavetable's own visualizers and checks that each one moves
      // *its own* engine's parameter and nothing else. This is the check the
      // column-scoping bug would have caught: every engine-B widget was drawn
      // on top of engine A's, so A's picture showed B's table, A's drags were
      // swallowed by B's invisible buttons, and both looked simply dead.
      if (getenv("INFINITE_WTDRAGTEST") != nullptr && gWtTestScreen.size() >= 8)
      {
         ImGuiIO& tio = ImGui::GetIO();
         tio.ConfigInputTrickleEventQueue = false;
         tio.AddFocusEvent(true);
         static bool sFocused = false;
         if (!sFocused) { glfwFocusWindow(window); sFocused = true; }
         auto btn = [&tio](bool down) { tio.AddMouseButtonEvent(0, down); };
         auto* wt = static_cast<WavetableNode*>(gNodes[0].node.get());

         // gWtTestRects is (frames, amp, pitch, filter) per engine, in column
         // order - so engine A's table picture is [0] and its amp curve [1].
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
            case 4: gTestMouse = Aim(framesA.x + fw * 0.15f, framesCy); break;
            case 5: btn(true); break;
            case 6: gTestMouse = Aim(framesA.x + fw * 0.50f, framesCy); break;
            case 7: gTestMouse = Aim(framesA.x + fw * 0.80f, framesCy); break;
            case 8: btn(false); break;
            // --- phase 2: drag engine A's amp sustain handle upward ---
            case 12: gTestMouse = Aim(susX, susY); break;
            case 13: btn(true); break;
            case 14: gTestMouse = Aim(susX, susY - ampSpan * 0.25f); break;
            case 15: gTestMouse = Aim(susX, susY - ampSpan * 0.45f); break;
            case 16: btn(false); break;
            // --- phase 3: drag engine B's *filter* envelope attack sideways ---
            case 20: gTestMouse = Aim(fbAttackX, fbTopY); break;
            case 21: btn(true); break;
            case 22: gTestMouse = Aim(fbAttackX + 30.0f, fbTopY); break;
            case 23: gTestMouse = Aim(fbAttackX + 60.0f, fbTopY); break;
            case 24: btn(false); break;
            default: break;
         }
         if (frameId >= 4)
         {
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
            glfwSetCursorPos(window, (double)gTestMouse.x, (double)gTestMouse.y);
         }
      }

      // Drags EQ's own handles and checks each gesture moves only the param
      // it targets - the same "picture in the right place" vs "dragging it
      // moves the engine's param" split WTDRAGTEST exists to cover, for the
      // per-band dot/diamond/double-click surface DrawEqVisualizer adds.
      // gEqTestScreen (built in the post-editor block below) is the previous
      // frame's rect, same lag WTDRAGTEST accepts for a static node.
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
         const float band3Q = eqLive->Param("band3Q");
         const float band3X = FilterVizFreqToX(band3Freq, x0, w);
         const float band3Y = FilterVizDbToY(band3Gain, y0, h); // band3 is a peak type - UsesGain
         const float band3QX = std::clamp(band3X + 16.0f, x0 + 8.0f, x1 - 8.0f);
         const float qT3 = (logf(std::max(0.1f, band3Q)) - logf(0.1f)) / (logf(18.0f) - logf(0.1f));
         const float band3QY = y0 + h - std::clamp(qT3, 0.0f, 1.0f) * h;
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
            // --- phase 2: drag band 3's Q diamond up (raise band3Q) ---
            case 62: gTestMouse = ImVec2(band3QX, band3QY); break;
            case 63: btn(true); break;
            case 64: gTestMouse = ImVec2(band3QX, band3QY - 30.0f); break;
            case 65: gTestMouse = ImVec2(band3QX, band3QY - 50.0f); break;
            case 66: btn(false); break;
            // --- phase 3: double-click band 2's dot (toggle band2On) ---
            case 70: gTestMouse = ImVec2(band2X, band2Y); btn(true); break;
            case 71: btn(false); break;
            case 72: btn(true); break;
            case 73: btn(false); break;
            default: break;
         }
         if (frameId >= 54)
         {
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
            // Also warp the real GLFW cursor: this fixture's headless
            // sandbox has been observed moving the OS-level cursor on its
            // own between frames (screen-capture tooling, most likely),
            // which a later glfwGetCursorPos() poll would otherwise pick up
            // and use to clobber the AddMousePosEvent target above.
            glfwSetCursorPos(window, (double)gTestMouse.x, (double)gTestMouse.y);
         }
      }

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
               const bool ok = samplerNode->FilePath() == "/tmp/infinite_samplerdrag/sample.wav";
               printf("SAMPLERDRAG drop onto node: path='%s' %s\n", samplerNode->FilePath().c_str(),
                      ok ? "OK" : "FAIL");
               printf("%s\n", ok ? "SAMPLERDRAGTEST OK" : "SAMPLERDRAGTEST FAIL");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (sPhase != 0)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }

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
            sExpectedId = gPluginDragTestRowId;
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
               const bool ok = imageNode->LoadedPath() == "/tmp/infinite_mediadrag/fixture.png";
               printf("MEDIADRAG drop onto node: path='%s' %s\n", imageNode->LoadedPath().c_str(),
                      ok ? "OK" : "FAIL");
               printf("%s\n", ok ? "MEDIADRAGTEST OK" : "MEDIADRAGTEST FAIL");
               glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
         }

         if (sPhase != 0)
            tio.AddMousePosEvent(gTestMouse.x, gTestMouse.y);
      }

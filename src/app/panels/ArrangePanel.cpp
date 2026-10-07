// Arrange panel content and docking (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   void DrawArrangePanelContent()
   {
      // Every id this panel holds (selection, anchor, rename/context/assign
      // targets) is re-resolved here; one that no longer exists clears.
      ArrangePruneSelection();

      // Adopt any dropped-media decode that finished since last frame -
      // see ArrangePollMediaImports's own comment for why this is only
      // polled from here.
      ArrangePollMediaImports();

      // Clip labels, the offline test and the render popup's resolution probe
      // all look nodes up by uid - through the global per-frame map (WP5b),
      // which this panel used to build its own private copy of every draw.
      auto nodeForUid = [](uint64_t uid) -> GraphNode* { return FindNodeByUid(uid); };

      const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 panelSize = ImGui::GetContentRegionAvail();

      // The timeline is read-only for the duration of a take (WP7): the
      // compositor and the audio scheduler both read gArrange frame by frame
      // while a job runs, and an edit landing mid-render would change the
      // thing being rendered halfway through the file.
      //
      // Three layers, because no one of them covers everything: the progress
      // dialog's full-screen click-catcher stops hover-guarded clicks, the
      // shortcut block below is gated on ArrangeRenderBusy(), and this drops
      // any gesture that was somehow still in flight - a drag can only have
      // started before the take (the Render button needs a click of its own),
      // but a queue that starts its next job the frame after the previous one
      // finished reopens that window once per job.
      if (ArrangeRenderBusy())
      {
         if (gArrangeGestureOpen)
            ArrangeGestureEnd();
         gArrangeDrag.mode = kArrangeDragNone;
         gArrangeMarkerDragId = 0;
         if (gArrangeScrubbing)
            ArrangeScrubCancel();
      }

      // Mouse wheel horizontal zoom / pan across the timeline area
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const bool overPanel = mouse.x >= panelOrigin.x && mouse.x < panelOrigin.x + panelSize.x &&
                             mouse.y >= panelOrigin.y && mouse.y < panelOrigin.y + panelSize.y;

      // Trackpad pinch: GLFW's Cocoa backend never forwards magnifyWithEvent:
      // (only scrollWheel:), so a real two-finger pinch produces no ImGui
      // wheel event at all - Platform::PollTrackpadMagnificationDelta() is a
      // side-channel reading it straight from a gesture recognizer. Drained
      // every frame (not just while overPanel) so a gesture that starts
      // outside the panel doesn't dump its whole backlog as one jump the
      // moment the mouse crosses in.
      const double pinchDelta = Platform::PollTrackpadMagnificationDelta();

      // Zoom keeps the time under the mouse fixed (pinch and Cmd/Ctrl+wheel
      // alike). The ruler's left edge is only known further down, so the
      // previous frame's is used - it moves only when the panel does.
      static float sArrangeLastRulerStartX = 0.0f;
      const bool arrangeWheelZoomMod = overPanel && (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);
      auto zoomAroundMouse = [&](float factor)
      {
         const float oldPpb = gArrangePixelsPerBeat;
         const float newPpb = std::clamp(oldPpb * factor, kArrangeMinPixelsPerBeat, kArrangeMaxPixelsPerBeat);
         const float mx = mouse.x - sArrangeLastRulerStartX;
         if (mx > 0.0f)
         {
            const double mouseBeat = gArrangeScrollBeats + (double)mx / oldPpb;
            gArrangeScrollBeats = std::max(0.0, mouseBeat - (double)mx / newPpb);
         }
         gArrangePixelsPerBeat = newPpb;
      };

      if (overPanel)
      {
         const float wheel = ImGui::GetIO().MouseWheel;
         const float wheelH = ImGui::GetIO().MouseWheelH;
         if (std::abs(pinchDelta) > 0.0005)
         {
            zoomAroundMouse((float)(1.0 + pinchDelta));
         }
         else if (arrangeWheelZoomMod && std::abs(wheel) > 0.001f)
         {
            zoomAroundMouse(wheel > 0.0f ? 1.15f : 0.87f);
         }
         else if (std::abs(wheelH) > 0.01f && std::abs(wheelH) > std::abs(wheel) * 0.5f)
         {
            gArrangeScrollBeats = std::max(0.0, gArrangeScrollBeats - (double)(wheelH * 40.0f / gArrangePixelsPerBeat));
         }
      }

      // Hand Tool live dragging (pan timeline horizontally & scroll vertically)
      if (gArrangeHandDragging)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
         if (ImGui::GetIO().MouseDown[0])
         {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if (std::abs(delta.x) > 0.001f && gArrangePixelsPerBeat > 0.0f)
               gArrangeScrollBeats = std::max(0.0, gArrangeScrollBeats - (double)(delta.x / gArrangePixelsPerBeat));
            if (std::abs(delta.y) > 0.001f)
               ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
         }
         else
         {
            gArrangeHandDragging = false;
         }
      }

      // Zoom Tool live dragging (smooth horizontal zoom scrubbing around drag start)
      if (gArrangeZoomDragging)
      {
         if (ImGui::GetIO().MouseDown[0])
         {
            const float dx = ImGui::GetIO().MousePos.x - gArrangeZoomDragStart.x;
            if (std::abs(dx) > 3.0f)
            {
               const float factor = std::clamp(expf(dx * 0.012f), 0.05f, 20.0f);
               const float newPpb = std::clamp(gArrangeZoomDragStartPpb * factor, kArrangeMinPixelsPerBeat, kArrangeMaxPixelsPerBeat);
               const float mx = gArrangeZoomDragStart.x - sArrangeLastRulerStartX;
               if (mx > 0.0f)
               {
                  const double mBeat = gArrangeZoomDragStartBeats + (double)mx / gArrangeZoomDragStartPpb;
                  gArrangeScrollBeats = std::max(0.0, mBeat - (double)mx / newPpb);
               }
               gArrangePixelsPerBeat = newPpb;
            }
         }
         else
         {
            const float dx = ImGui::GetIO().MousePos.x - gArrangeZoomDragStart.x;
            const float dy = ImGui::GetIO().MousePos.y - gArrangeZoomDragStart.y;
            if (std::sqrt(dx * dx + dy * dy) < 4.0f)
            {
               const float factor = ImGui::GetIO().KeyAlt ? 0.75f : 1.35f;
               zoomAroundMouse(factor);
            }
            gArrangeZoomDragging = false;
         }
      }

      // Keyboard focus claim
      if (overPanel && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
         gArrangeClaimedKeys = true;
      gArrangeFocused = gArrangeClaimedKeys;

      Transport& tr = Transport::Instance();

      // Strict loop: once armed, playback never runs past the region end -
      // it snaps back to the region start instead, same as the loop toggle
      // on any DAW transport.
      //
      // The wrap itself lives in Transport (WP2). Doing it here meant it only
      // ran once per UI frame, so the playhead sailed past the loop end by a
      // whole frame - tens of ms of audio that shouldn't have been heard, and
      // unbounded during a stall. This block now only *publishes* the loop;
      // Transport wraps at its own block boundary, within one audio block.
      //
      // The panel edits gArrange.settings.loop in ticks (WP5b) and every edit
      // publishes through ArrangeSetLoop; this re-publish is an idempotent
      // backstop, never a model write.
      PublishArrangeLoop();

      // Timeline shortcuts, only while the panel owns the keyboard and no
      // text field is taking input. Each acts on gArrangeSel (ids; a grouped
      // clip's whole group is selected with it) through one Arrange:: op and
      // leaves one undo entry. The canvas's own Cmd+C/V/D/G and Delete are
      // gated off while gArrangeFocused, so nothing fires twice. Not during a
      // clip drag or an active popup field either: those rebuild gArrange
      // from their gesture snapshot every frame, which would silently undo
      // (and double-push) an edit made mid-gesture.
      // Escape abandons a ruler scrub without seeking.
      const bool scrubEscaped = gArrangeScrubbing && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
      if (scrubEscaped)
         ArrangeScrubCancel();
      // !ArrangeRenderBusy(): the timeline is read-only for the duration of a
      // take (WP7). The mouse is already blocked by the progress dialog's
      // click-catcher, but keys reach here regardless of what floats on top,
      // and an edit landing mid-render would change the model the compositor
      // is reading frame by frame.
      // !ArrangeFieldHot(): hovering an inspector value field arms
      // hover-and-type, and the digit that opens the field arrives a frame
      // BEFORE WantTextInput goes true - so without this, typing "0.3" into
      // Pan ran the '0' through ArrangeToggleEnabledSelection and bypassed
      // the clip. Suppressing every single-key shortcut while the pointer is
      // over a field is deliberate: the pointer being there is exactly the
      // statement that the next keystroke is a value, not a command.
      if (gArrangeFocused && !ImGui::GetIO().WantTextInput && !ArrangeFieldHot() &&
          !scrubEscaped && !ArrangeRenderBusy() &&
          gArrangeDrag.mode == kArrangeDragNone && !gArrangeGestureOpen && !gArrangeScrubbing &&
          gArrangeMarkerDragId == 0)
      {
         const ImGuiIO& kio = ImGui::GetIO();
         const bool cmd = kio.KeySuper || kio.KeyCtrl;
         const bool noMods = !cmd && !kio.KeyAlt && !kio.KeyShift;
         const Arrange::Tick playTick = ArrangePlayTick();

         if (!cmd && kio.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false))
         {
            gArrangeSel.clear();
            for (const Arrange::Lane& l : gArrange.lanes)
               for (const Arrange::Clip& c : l.clips)
                  gArrangeSel.insert(c.id);
            gArrangeSelAnchor = 0;
         }
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_C, false))
            ArrangeCopySelection();
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_V, false))
            ArrangePasteAt(playTick);
         // Shift+D / Delete / Cmd+G / Cmd+Shift+G: the header-row selection
         // (gArrangeRowSel - tracks and groups) wins over the clip selection
         // whenever it is non-empty, so selecting a track and hitting one of
         // these acts on the track, not on whatever clip happened to still
         // be selected. Row-selection ops each return false (falling through
         // to the clip-selection op below) when the row selection turned out
         // empty by the time they ran, so nothing silently no-ops.
         else if ((cmd || kio.KeyShift) && ImGui::IsKeyPressed(ImGuiKey_D, false))
         {
            if (gArrangeRowSel.empty() || !ArrangeDuplicateRowSelection())
               ArrangeDuplicateSelection();
         }
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_E, false))
            ArrangeSplitSelectionAt(playTick);
         else if (cmd && kio.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false))
         {
            if (gArrangeRowSel.empty() || !ArrangeUngroupRowSelection())
               ArrangeUngroupSelection();
         }
         else if (cmd && ImGui::IsKeyPressed(ImGuiKey_G, false))
         {
            if (gArrangeRowSel.empty() || !ArrangeGroupRowSelection())
               ArrangeGroupSelection();
         }
         else if (cmd && !kio.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false))
            ArrangeRenameSelection();
         else if (!cmd && kio.KeyShift && !kio.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_J, false))
            ArrangeAddTrackShortcut(true);
         else if (!cmd && kio.KeyShift && !kio.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_K, false))
            ArrangeAddTrackShortcut(false);
         else if (!cmd && !kio.KeyAlt &&
                  (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)))
            ArrangeToggleEnabledSelection();
         else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
         {
            if (gArrangeRowSel.empty() || !ArrangeDeleteRowSelection())
               ArrangeDeleteSelection();
         }
         // Markers and the playhead (WP6). The canvas binds none of these
         // keys (Shift+M is the mod matrix, hence noMods on M).
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_M, false))
            ArrangeAddMarkerAtPlayhead();
         // Tool selection hotkeys (A = Select, T = Trim, R = Range, B = Blade, Z = Zoom, H = Hand)
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_A, false))
         {
            gArrangeTool = ArrangeTool::Select;
            gArrangeBladeOn = false;
         }
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_T, false))
         {
            gArrangeTool = (gArrangeTool == ArrangeTool::Trim) ? ArrangeTool::Select : ArrangeTool::Trim;
            gArrangeBladeOn = false;
         }
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_R, false))
         {
            gArrangeTool = (gArrangeTool == ArrangeTool::Range) ? ArrangeTool::Select : ArrangeTool::Range;
            gArrangeBladeOn = false;
         }
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_B, false))
         {
            gArrangeTool = (gArrangeTool == ArrangeTool::Blade) ? ArrangeTool::Select : ArrangeTool::Blade;
            gArrangeBladeOn = (gArrangeTool == ArrangeTool::Blade);
         }
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_Z, false))
         {
            gArrangeTool = (gArrangeTool == ArrangeTool::Zoom) ? ArrangeTool::Select : ArrangeTool::Zoom;
            gArrangeBladeOn = false;
         }
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_H, false))
         {
            gArrangeTool = (gArrangeTool == ArrangeTool::Hand) ? ArrangeTool::Select : ArrangeTool::Hand;
            gArrangeBladeOn = false;
         }
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_P, false))
         {
            gArrangeTool = (gArrangeTool == ArrangeTool::Pencil) ? ArrangeTool::Select : ArrangeTool::Pencil;
            gArrangeBladeOn = false;
         }
         else if (kio.KeyAlt && !cmd && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
            ArrangeJumpToMarker(-1);
         else if (kio.KeyAlt && !cmd && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
            ArrangeJumpToMarker(1);
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            ArrangeNudge(-1);
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            ArrangeNudge(1);
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_Home, false))
            ArrangeSeekTick(0);
         else if (noMods && ImGui::IsKeyPressed(ImGuiKey_End, false))
            ArrangeSeekTick(ArrangeEndKeyTargetTick());
         else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && gArrangeDrag.mode == kArrangeDragNone)
         {
            if (gArrangeTool != ArrangeTool::Select)
            {
               gArrangeTool = ArrangeTool::Select;
               gArrangeBladeOn = false;
            }
            else
            {
               gArrangeSel.clear();
               gArrangeSelAnchor = 0;
               gArrangeAssigningClipId = 0;
               gArrangeBladeOn = false;
            }
         }
      }

      // ---- Toolbar ----
      const float arrangeToolbarTop = ImGui::GetCursorScreenPos().y;
      float arrangeToolbarBottom = arrangeToolbarTop;
      {
         const bool arrangeToolbarLight = IsThemeLight();
         const ImU32 arrangeIconCol = ImGui::GetColorU32(ImGuiCol_Text);

         // Timeline audio mode toggle, pinned top-right of the panel. It owns
         // the routing mode only (gAudioMode); the top bar's Start/Stop Audio
         // owns engine power. Turning the mode on also starts the engine if it
         // is off (asking for timeline audio with no engine would be silent);
         // turning it off hands audio back to the canvas and leaves the engine
         // running.
         {
            const bool engineOn = AudioEngine::Instance().SampleRate() > 0.0;
            const bool timelineMode = gAudioMode == AudioMode::Timeline;
            const char* audioLabel = timelineMode ? T("Timeline Audio On") : T("Enable Timeline Audio");
            const float audioBtnW = ImGui::CalcTextSize(audioLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f;
            const ImVec2 audioBtnPos(panelOrigin.x + panelSize.x - audioBtnW - 6.0f, panelOrigin.y + 2.0f);
            const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(audioBtnPos);
            ImGui::PushStyleColor(ImGuiCol_Button, timelineMode
               ? (arrangeToolbarLight ? ImVec4(0.20f, 0.62f, 0.34f, 1.0f) : ImVec4(0.16f, 0.52f, 0.28f, 1.0f))
               : (arrangeToolbarLight ? ImVec4(0.80f, 0.82f, 0.87f, 1.0f) : ImVec4(0.30f, 0.30f, 0.34f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_Text, timelineMode
               ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
               : (arrangeToolbarLight ? ImVec4(0.12f, 0.14f, 0.20f, 1.0f) : ImVec4(0.92f, 0.94f, 0.98f, 1.0f)));
            if (ImGui::Button(audioLabel, ImVec2(audioBtnW, 0.0f)))
            {
               if (timelineMode)
               {
                  gAudioMode = AudioMode::Canvas;
               }
               else
               {
                  gAudioMode = AudioMode::Timeline;
                  if (!engineOn)
                  {
                     gAudioStartError.clear();
                     if (!StartAudioEngine(gAudioStartError))
                        fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
                  }
               }
            }
            ImGui::PopStyleColor(2);
            // Only a failure explains itself on hover; the label says the rest.
            if (ImGui::IsItemHovered() && !gAudioStartError.empty() && !engineOn)
               ImGui::SetTooltip("%s", gAudioStartError.c_str());
            ImGui::SetCursorScreenPos(savedCursor);

            // Render, pinned just left of Start/Stop Audio - exports the
            // arrangement's own timeline (every track's clips, composited
            // and stacked, over a selected time range) to a movie file.
            // Operates as an internal timeline renderer (never spawns an Output
            // node on the canvas). Video tracks are composited bottom lane first
            // (top lane frontmost) with aspect-ratio preservation and blend modes/opacity; audio is
            // the live sum of every active audio clip.
            // ---- Render / export (WP7) ----
            // The settings themselves are patch state (Arrange::Settings,
            // persisted in the `arrange` line) rather than the statics they
            // used to be, so a patch reopens with the same output size, fps,
            // format, range kind, sources and folder it was last rendered at
            // (WP7 #7). Only the file's name and the marker picks are
            // session-local: a filename belongs to a take, not to a document.
            Arrange::Settings& rset = gArrange.settings;
            static std::string sArrangeRenderFileName = "infinite_timeline";
            static int sArrangeRenderMarkerA = 0;
            static int sArrangeRenderMarkerB = 1;
            static ArrangeRenderJob sArrangePendingJob;
            static bool sArrangePendingStartNow = false;
            static bool sArrangeOpenOverwrite = false;

            const Arrange::Tick renderableEnd = ArrangeRenderableEndTick();

            // Is there anything for a timeline *video* source to draw in a
            // given range? Decides the video source's default and whether
            // "Timeline clips" is offered at all.
            auto rangeHasVideoClips = [](Arrange::Tick a, Arrange::Tick b) -> int {
               return ArrangeRenderVideoClipsInRange(a, b);
            };

            // The range the current settings describe, in ticks.
            auto currentRange = [&](Arrange::Tick& outA, Arrange::Tick& outB) {
               ArrangeRenderResolveRange(rset.renderRangeKind, sArrangeRenderMarkerA, sArrangeRenderMarkerB,
                                         rset.renderRangeStart, rset.renderRangeEnd, outA, outB);
            };

            Arrange::Tick rangeA = 0, rangeB = 0;
            currentRange(rangeA, rangeB);

            // -1 means "decide at draw time", which is what makes the default
            // track the monitoring mode instead of freezing whatever it was
            // when the patch was saved ("render what you hear", WP7 #6b).
            auto effectiveAudioSource = []() -> int {
               return ArrangeRenderEffectiveAudioSource();
            };
            auto effectiveVideoSource = [&]() -> int {
               return ArrangeRenderEffectiveVideoSource(rangeA, rangeB);
            };

            auto renderExtension = [&]() -> const char* {
               if (effectiveVideoSource() == kArrangeVideoNone)
                  return ".wav";
               return rset.renderFormat == 1 ? ".mov" : ".mp4";
            };
            auto renderFullPath = [&]() -> std::string {
               std::string folder = rset.renderFolder;
               if (folder.empty())
               {
                  folder = AppPaths::DesktopDir();
               }
               while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
                  folder.pop_back();
               return folder + "/" + sArrangeRenderFileName + renderExtension();
            };

            const char* renderLabel = ArrangeRenderBusy() ? "Rendering..." : "Render";
            const float renderBtnW = ImGui::CalcTextSize(renderLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f;
            const ImVec2 renderBtnPos(audioBtnPos.x - renderBtnW - 8.0f, panelOrigin.y + 2.0f);
            ImGui::SetCursorScreenPos(renderBtnPos);
            ImGui::BeginDisabled(ArrangeRenderBusy());
            if (ImGui::Button(renderLabel, ImVec2(renderBtnW, 0.0f)))
            {
               if (rset.renderRangeKind == kArrangeRangeCustom && rset.renderRangeEnd <= rset.renderRangeStart)
               {
                  rset.renderRangeStart = 0;
                  rset.renderRangeEnd = std::max<Arrange::Tick>(renderableEnd, Arrange::kPPQ);
               }
               if (rset.renderFolder.empty())
               {
                  rset.renderFolder = AppPaths::DesktopDir();
               }
               ImGui::OpenPopup("##arrangeRenderPopup");
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(savedCursor);

            if (ImGui::BeginPopup("##arrangeRenderPopup"))
            {
               ImGui::Text("%s", T("Timeline Render Settings"));
               ImGui::Separator();

               // ---- Time range ----
               ImGui::TextDisabled("%s", T("Range:"));
               ImGui::SetNextItemWidth(150.0f);
               int rangeKind = std::clamp(rset.renderRangeKind, 0, 3);
               if (ImGui::Combo("##arrRangeKind", &rangeKind, I18n::TList("Whole arrangement\0Loop\0Marker A -> B\0Custom\0")))
               {
                  rset.renderRangeKind = rangeKind;
                  gPatchDirty = true;
               }

               if (rset.renderRangeKind == kArrangeRangeMarkers)
               {
                  const int markerCount = (int)gArrange.markers.size();
                  if (markerCount < 2)
                  {
                     ImGui::TextDisabled("%s", T("(needs two markers)"));
                  }
                  else
                  {
                     std::string markerItems;
                     for (const Arrange::Marker& mk : gArrange.markers)
                     {
                        markerItems += mk.name.empty() ? ArrangeFormatPos(mk.pos) : mk.name;
                        markerItems.push_back('\0');
                     }
                     markerItems.push_back('\0');
                     sArrangeRenderMarkerA = std::clamp(sArrangeRenderMarkerA, 0, markerCount - 1);
                     sArrangeRenderMarkerB = std::clamp(sArrangeRenderMarkerB, 0, markerCount - 1);
                     ImGui::SetNextItemWidth(110.0f);
                     ImGui::Combo("##arrMarkA", &sArrangeRenderMarkerA, markerItems.c_str());
                     ImGui::SameLine(0.0f, 6.0f);
                     ImGui::TextDisabled("->");
                     ImGui::SameLine(0.0f, 6.0f);
                     ImGui::SetNextItemWidth(110.0f);
                     ImGui::Combo("##arrMarkB", &sArrangeRenderMarkerB, markerItems.c_str());
                  }
               }
               else if (rset.renderRangeKind == kArrangeRangeCustom)
               {
                  // Typed in whichever unit the timeline is showing (WP6's
                  // ArrangeParsePos/ArrangeFormatPos), so a range reads the
                  // same way as every other position in the panel.
                  auto tickField = [&](const char* id, Arrange::Tick& t) {
                     char buf[48];
                     snprintf(buf, sizeof(buf), "%s", ArrangeFormatPos(t).c_str());
                     ImGui::SetNextItemWidth(90.0f);
                     if (ImGui::InputText(id, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                     {
                        const Arrange::Tick parsed = ArrangeParsePos(buf);
                        if (parsed >= 0)
                        {
                           t = parsed;
                           gPatchDirty = true;
                        }
                     }
                  };
                  tickField("##arrRangeStart", rset.renderRangeStart);
                  ImGui::SameLine(0.0f, 6.0f);
                  ImGui::TextDisabled("->");
                  ImGui::SameLine(0.0f, 6.0f);
                  tickField("##arrRangeEnd", rset.renderRangeEnd);
               }
               currentRange(rangeA, rangeB);

               ImGui::Separator();

               const bool audioOnly = effectiveVideoSource() == kArrangeVideoNone;

               // ---- Resolution / fps (video jobs only) ----
               if (!audioOnly)
               {
                  ImGui::TextDisabled("%s", T("Resolution:"));
                  static int sArrangeRenderResPreset = 0; // 0=Match Clips, 1..4 fixed, 5=Custom
                  int detectedClipW = 0, detectedClipH = 0;
                  ArrangeRenderDetectClipSize(detectedClipW, detectedClipH);
                  const char* kResPresets[] = { T("Match Clips"), "1080p", "4K", "720p", T("Vertical"), T("Custom") };
                  ImGui::SetNextItemWidth(120.0f);
                  if (ImGui::Combo("##arrResPreset", &sArrangeRenderResPreset, kResPresets, IM_ARRAYSIZE(kResPresets)))
                  {
                     if (sArrangeRenderResPreset == 0) { rset.renderWidth = detectedClipW; rset.renderHeight = detectedClipH; }
                     else if (sArrangeRenderResPreset == 1) { rset.renderWidth = 1920; rset.renderHeight = 1080; }
                     else if (sArrangeRenderResPreset == 2) { rset.renderWidth = 3840; rset.renderHeight = 2160; }
                     else if (sArrangeRenderResPreset == 3) { rset.renderWidth = 1280; rset.renderHeight = 720; }
                     else if (sArrangeRenderResPreset == 4) { rset.renderWidth = 1080; rset.renderHeight = 1920; }
                     gPatchDirty = true;
                  }
                  ImGui::SameLine();
                  ImGui::SetNextItemWidth(60.0f);
                  if (ImGui::InputInt("##arrResW", &rset.renderWidth, 0, 0))
                     gPatchDirty = true;
                  ImGui::SameLine(0.0f, 4.0f);
                  ImGui::TextDisabled("%s", T("x"));
                  ImGui::SameLine(0.0f, 4.0f);
                  ImGui::SetNextItemWidth(60.0f);
                  if (ImGui::InputInt("##arrResH", &rset.renderHeight, 0, 0))
                     gPatchDirty = true;
                  rset.renderWidth = std::clamp(rset.renderWidth, 16, 7680);
                  rset.renderHeight = std::clamp(rset.renderHeight, 16, 4320);

                  ImGui::SetNextItemWidth(90.0f);
                  if (ImGui::InputInt(L("fps##arrRenderFps"), &rset.renderFps))
                     gPatchDirty = true;
                  rset.renderFps = std::clamp(rset.renderFps, 1, 240);

                  ImGui::Separator();
               }

               ImGui::Separator();

               // ---- Output file ----
               ImGui::TextDisabled("%s", T("Output File:"));
               char renderNameBuf[256];
               snprintf(renderNameBuf, sizeof(renderNameBuf), "%s", sArrangeRenderFileName.c_str());
               ImGui::SetNextItemWidth(200.0f);
               if (ImGui::InputText("##arrangeRenderName", renderNameBuf, sizeof(renderNameBuf)))
                  sArrangeRenderFileName = renderNameBuf;
               ImGui::SameLine();
               ImGui::TextDisabled("%s", renderExtension());

               char renderFolderBuf[512];
               snprintf(renderFolderBuf, sizeof(renderFolderBuf), "%s", rset.renderFolder.c_str());
               ImGui::SetNextItemWidth(260.0f);
               if (ImGui::InputText("##arrangeRenderFolder", renderFolderBuf, sizeof(renderFolderBuf)))
               {
                  rset.renderFolder = renderFolderBuf;
                  gPatchDirty = true;
               }

               // Format follows the video source: an audio-only job is a WAV
               // by definition (the extension beside the name already says
               // so), so the container buttons stand down rather than
               // offering a choice that cannot apply (WP7 #1).
               if (!audioOnly)
               {
                  const int fmtActive = rset.renderFormat == 1 ? 1 : 0;
                  if (fmtActive == 0) ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
                  if (ImGui::Button(L(".mp4##arrRenderMp4"), ImVec2(56, 0)))
                  {
                     rset.renderFormat = 0;
                     gPatchDirty = true;
                  }
                  if (fmtActive == 0) ImGui::PopStyleColor();
                  ImGui::SameLine();
                  if (fmtActive == 1) ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
                  if (ImGui::Button(L(".mov##arrRenderMov"), ImVec2(56, 0)))
                  {
                     rset.renderFormat = 1;
                     gPatchDirty = true;
                  }
                  if (fmtActive == 1) ImGui::PopStyleColor();
               }

               ImGui::Separator();

               // Builds the job the two submit buttons share, so "Render Now"
               // and "Add to Queue" can never disagree about what was asked
               // for - they differ only in where the job is put.
               auto buildJob = [&]() -> ArrangeRenderJob {
                  ArrangeRenderJob job;
                  job.rangeKind = rset.renderRangeKind;
                  job.startTick = rangeA;
                  job.endTick = rangeB;
                  job.audioSource = effectiveAudioSource();
                  job.videoSource = effectiveVideoSource();
                  // Always 0: this modal renders the timeline. The canvas
                  // Output-node take is the Output node's own record button,
                  // not a source choice hidden inside the timeline's render
                  // dialog - which is what the removed dropdowns were.
                  job.canvasVideoUid = 0;
                  job.width = rset.renderWidth;
                  job.height = rset.renderHeight;
                  job.fps = rset.renderFps;
                  // Follows Settings -> Audio, not rset.renderSampleRate: the
                  // graph only knows how to generate at the rate its nodes
                  // were prepared at, so a per-arrangement rate would be a
                  // choice nothing downstream could honour.
                  job.sampleRate = (int)llround(ArrangeRenderActiveSampleRate());
                  job.format = job.videoSource == kArrangeVideoNone ? 2 : (rset.renderFormat == 1 ? 1 : 0);
                  job.path = renderFullPath();
                  return job;
               };
               auto commitJob = [](ArrangeRenderJob job, bool startNow) {
                  job.id = gArrangeRenderNextJobId++;
                  job.status = kArrangeJobQueued;
                  if (startNow)
                  {
                     // Ahead of anything already parked, so "Render Now"
                     // means this job, not "whatever is first in the queue".
                     gArrangeRenderQueue.insert(gArrangeRenderQueue.begin(), job);
                     gArrangeRenderQueueRunning = true;
                  }
                  else
                  {
                     gArrangeRenderQueue.push_back(job);
                  }
               };
               auto submitJob = [&](bool startNow) {
                  ArrangeRenderJob job = buildJob();
                  std::error_code ec;
                  const bool collides =
                     std::filesystem::exists(AppPaths::FsPath(job.path), ec) || ArrangeRenderPathQueued(job.path, 0);
                  if (collides)
                  {
                     // Asking before the queue gets there, not while it runs:
                     // a job that silently overwrote yesterday's export is
                     // only noticed once it is already gone (WP7 #8).
                     sArrangePendingJob = job;
                     sArrangePendingStartNow = startNow;
                     sArrangeOpenOverwrite = true;
                  }
                  else
                  {
                     commitJob(job, startNow);
                  }
               };

               // effectiveAudioSource() is always Timeline here, so there is
               // always an audio stream to write; only the filename can be
               // missing.
               const bool canRender = !sArrangeRenderFileName.empty();
               ImGui::BeginDisabled(!canRender || ArrangeRenderBusy());
               if (ImGui::Button(L("Render Now"), ImVec2(110, 0)))
               {
                  submitJob(true);
                  ImGui::CloseCurrentPopup();
               }
               ImGui::EndDisabled();
               ImGui::SameLine();
               if (ImGui::Button(L("Cancel"), ImVec2(70, 0)))
                  ImGui::CloseCurrentPopup();
               ImGui::EndPopup();
            }

            // ---- overwrite / duplicate-path prompt (WP7 #8) ----
            // Drawn at toolbar level rather than inside the render popup: the
            // popup closes on submit, and a modal owned by a closed popup
            // never opens.
            if (sArrangeOpenOverwrite)
            {
               ImGui::OpenPopup(L("Overwrite file?##arrangeOverwrite"));
               sArrangeOpenOverwrite = false;
            }
            if (ImGui::BeginPopupModal(L("Overwrite file?##arrangeOverwrite"), nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize))
            {
               const bool queuedClash = ArrangeRenderPathQueued(sArrangePendingJob.path, 0);
               ImGui::TextUnformatted(queuedClash ? T("Another queued job already writes:") : T("This file already exists:"));
               ImGui::TextDisabled("%s", sArrangePendingJob.path.c_str());
               ImGui::Dummy(ImVec2(0, 4));
               if (ImGui::Button(L("Overwrite"), ImVec2(100, 0)))
               {
                  ArrangeRenderJob job = sArrangePendingJob;
                  job.id = gArrangeRenderNextJobId++;
                  job.status = kArrangeJobQueued;
                  if (sArrangePendingStartNow)
                  {
                     gArrangeRenderQueue.insert(gArrangeRenderQueue.begin(), job);
                     gArrangeRenderQueueRunning = true;
                  }
                  else
                  {
                     gArrangeRenderQueue.push_back(job);
                  }
                  ImGui::CloseCurrentPopup();
               }
               ImGui::SameLine();
               if (ImGui::Button(L("Auto-rename"), ImVec2(100, 0)))
               {
                  ArrangeRenderJob job = sArrangePendingJob;
                  job.path = ArrangeRenderUniquePath(job.path);
                  job.id = gArrangeRenderNextJobId++;
                  job.status = kArrangeJobQueued;
                  if (sArrangePendingStartNow)
                  {
                     gArrangeRenderQueue.insert(gArrangeRenderQueue.begin(), job);
                     gArrangeRenderQueueRunning = true;
                  }
                  else
                  {
                     gArrangeRenderQueue.push_back(job);
                  }
                  ImGui::CloseCurrentPopup();
               }
               ImGui::SameLine();
               if (ImGui::Button(L("Cancel"), ImVec2(80, 0)))
                  ImGui::CloseCurrentPopup();
               ImGui::EndPopup();
            }
         }

         // Add/delete track now live in each row's right-click menu, and
         // zoom is ctrl/cmd+scroll or pinch only (the px/beat slider that
         // used to sit here was pure toolbar clutter - gArrangePixelsPerBeat
         // itself is still clamped wherever the scroll/pinch handlers write it).

         // Clip Settings toggle: opens the docked inspector panel for
         // whichever clip is currently selected (drawn once, after the
         // whole arrange panel, so it can float outside this window). No
         // SameLine here (matches the removed Zoom slider it replaces):
         // SameLine's X comes from the LAST item's rect regardless of any
         // SetCursorScreenPos in between, and the last item submitted in
         // this window is the far-right Queue button (Render/Queue draw at
         // absolute positions then restore the cursor via SetCursorScreenPos
         // above) - a SameLine here would anchor off Queue's position, not
         // off the restored cursor, dragging every button after it (Play,
         // Rewind, Bars/Time) over there too.
         {
            const bool viewportWasOn = gArrangeShowViewport;
            if (viewportWasOn)
            {
               ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
               ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentEmphasisPressed());
            }
            if (ImGui::Button("##arrangeshowviewport", ImVec2(30, 0)))
               gArrangeShowViewport = !gArrangeShowViewport;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen("##arrangeviewportctx"))
               ImGui::OpenPopup("##arrangeviewportctx");
            if (viewportWasOn)
               ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
               HelpTip(viewportWasOn ? T("Viewport Monitor: Visible (Right-click for Dock Position)") : T("Toggle Viewport Monitor (Right-click for Dock Position)"));
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = viewportWasOn ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol);
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            const float bw = 13.0f, bh = 9.0f;
            ImVec2 tl(center.x - bw * 0.5f, center.y - bh * 0.5f - 1.0f);
            ImVec2 br(center.x + bw * 0.5f, center.y + bh * 0.5f - 1.0f);
            if (viewportWasOn)
               tdl->AddRectFilled(tl, br, icol, 1.5f);
            else
               tdl->AddRect(tl, br, icol, 1.5f, 0, 1.4f);
            tdl->AddLine(ImVec2(center.x, br.y), ImVec2(center.x, br.y + 2.5f), icol, 1.4f);
            tdl->AddLine(ImVec2(center.x - 3.5f, br.y + 2.5f), ImVec2(center.x + 3.5f, br.y + 2.5f), icol, 1.4f);
         }

         // Play/Pause and Rewind as icon buttons, matching the main
         // Infinite toolbar's own transport controls (see the top toolbar's
         // "##transportrewind" a bit further down in this file) rather than
         // plain text labels.
         ImGui::SameLine(0.0f, 14.0f);
         const bool arrangeIsPlaying = tr.IsPlaying();
         if (arrangeIsPlaying)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(16, 185, 129, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(5, 150, 105, 255));
         }
         if (ImGui::Button("##arrangeplaybtn", ImVec2(30, 0)))
            tr.TogglePlay();
         if (arrangeIsPlaying)
            ImGui::PopStyleColor(2);
         if (ImGui::IsItemHovered())
            HelpTip(arrangeIsPlaying ? T("Pause (Space)") : T("Play (Space)"));
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.6f;
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = arrangeIsPlaying ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol);
            if (arrangeIsPlaying)
               Tabler::DrawPlayerPause(ImGui::GetWindowDrawList(), center, iconSize, icol);
            else
               Tabler::DrawPlayerPlay(ImGui::GetWindowDrawList(), center, iconSize, icol);
         }

         ImGui::SameLine();
         if (ImGui::Button("##arrangerewindbtn", ImVec2(30, 0)))
            tr.Rewind();
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Return to Start (Enter)"));
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.72f;
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol;
            Tabler::DrawPlayerRewind(ImGui::GetWindowDrawList(), center, iconSize, icol);
         }

         // Bars | Time: which unit the ruler, the clip popup and the loop
         // fields speak (Settings::timeDisplay, saved with the patch). A view
         // change only - every position stays in ticks. The selected half
         // takes the shared "selected" accent tint; the other stays quiet.
         ImGui::SameLine(0.0f, 14.0f);
         {
            const int shownUnit = gArrange.settings.timeDisplay; // pre-click, for the push/pop pairs
            const char* kUnitLabels[2] = { "Bars##arrunitbars", "Time##arrunittime" };
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
            for (int u = 0; u < 2; u++)
            {
               if (u == 1)
                  ImGui::SameLine();
               const bool on = shownUnit == u;
               if (on)
               {
                  ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
                  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentEmphasisPressed());
               }
               if (ImGui::Button(kUnitLabels[u], ImVec2(44.0f, 0.0f)))
                  ArrangeSetTimeDisplay(u);
               if (on)
                  ImGui::PopStyleColor(2);
               if (ImGui::IsItemHovered())
                  HelpTip(u == 0 ? T("Switch display to Bars / Beats (BBT)") : T("Switch display to Time (Minutes:Seconds)"));
            }
            ImGui::PopStyleVar();
         }

         // Snap: the magnet toggles the grid off <-> the last division that
         // was on; the dropdown beside it picks the division. The grid is
         // Settings::snapDivision / snapTriplet (0 = off), in ticks, so it is
         // the same musical grid at any zoom and any tempo. It covers clip
         // move/trim, marker drags, the ruler scrub, the loop drag and the
         // arrow-key nudge.
         ImGui::SameLine(0.0f, 14.0f);
         const bool snapWasOn = gArrange.settings.snapDivision > 0;
         if (snapWasOn)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(16, 185, 129, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(5, 150, 105, 255));
         }
         if (ImGui::Button("##arrangesnapbtn", ImVec2(30, 0)))
         {
            if (snapWasOn)
               ArrangeSetSnap(0, false);
            else
               ArrangeSetSnap(std::max(1, gArrangeLastSnapDivision), gArrange.settings.snapTriplet);
         }
         if (snapWasOn)
            ImGui::PopStyleColor(2);
         if (ImGui::IsItemHovered())
            HelpTip(snapWasOn ? T("Snap to Grid: On") : T("Snap to Grid: Off"));
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.62f;
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = snapWasOn ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol);
            Tabler::DrawMagnet(ImGui::GetWindowDrawList(), center, iconSize, icol);
         }

         // Grid division dropdown. The divisions are MusicTime's own
         // RateDivision entries (names and lengths from that one table -
         // rhythmic-quantization-standard); this list only says which of
         // them a timeline grid offers, mapped onto snapDivision/snapTriplet.
         ImGui::SameLine(0.0f, 4.0f);
         {
            using GridChoice = ArrangeGridChoice;
            const auto& kGridChoices = kArrangeGridChoices;
            auto choiceName = [](const GridChoice& c) { return c.rd < 0 ? "Off" : MusicTime::RateDivisionName(c.rd); };
            const Arrange::Settings& st = gArrange.settings;
            const char* curName = "Custom";
            for (const GridChoice& c : kGridChoices)
               if (c.division == st.snapDivision && (c.division <= 1 || c.triplet == st.snapTriplet))
                  curName = choiceName(c);
            char gridBtn[48];
            snprintf(gridBtn, sizeof(gridBtn), "%s##arrgriddiv", curName);
            PushDropdownStyle();
            if (ImGui::Button(gridBtn, ImVec2(58.0f, 0.0f)))
               ImGui::OpenPopup("##arrgridpopup");
            PopDropdownStyle();
            if (ImGui::IsItemHovered())
               HelpTip("%s", T("Snap Grid Division"));
            if (ImGui::BeginPopup("##arrgridpopup"))
            {
               for (const GridChoice& c : kGridChoices)
               {
                  const bool sel = c.division == st.snapDivision && (c.division <= 1 || c.triplet == st.snapTriplet);
                  if (ImGui::Selectable(choiceName(c), sel))
                     ArrangeSetSnap(c.division, c.triplet);
               }
               ImGui::EndPopup();
            }
         }

         // Loop region toggle. The region itself is a Shift+drag on the
         // ruler; right-clicking the ruler disarms it.
         ImGui::SameLine();
         const Arrange::LoopRange loopNow = gArrange.settings.loop; // the loop as of this frame
         const bool loopWasOn = loopNow.enabled; // see snapWasOn above
         if (loopWasOn)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(16, 185, 129, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(5, 150, 105, 255));
         }
         if (ImGui::Button("##arrangeloopbtn", ImVec2(30, 0)))
            ArrangeSetLoop(!loopNow.enabled, loopNow.start, loopNow.end);
         if (loopWasOn)
            ImGui::PopStyleColor(2);
         if (ImGui::IsItemHovered())
            HelpTip(loopWasOn ? T("Loop Region: Enabled") : T("Toggle Loop Region (Shift+drag on ruler)"));
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const float iconSize = (bmax.y - bmin.y) * 0.65f;
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = loopWasOn ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol);
            Tabler::DrawRepeat(ImGui::GetWindowDrawList(), center, iconSize, icol);
         }

         // Tool Selector (Select A, Trim T, Range R, Blade B, Zoom Z, Hand H)
         ImGui::SameLine();
         const bool toolNonDefault = (gArrangeTool != ArrangeTool::Select);
         if (toolNonDefault)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(16, 185, 129, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(5, 150, 105, 255));
         }
         const float toolBtnW = 34.0f;
         if (ImGui::Button("##arrangetoolmodepicker", ImVec2(toolBtnW, 0)))
         {
            ImGui::OpenPopup("##arrangetoolpopup");
         }
         if (toolNonDefault)
            ImGui::PopStyleColor(2);

         const char* toolTooltip = T("Tool: Select (A)");
         switch (gArrangeTool)
         {
            case ArrangeTool::Select: toolTooltip = T("Tool: Select (A) - Click to choose tool"); break;
            case ArrangeTool::Trim:   toolTooltip = T("Tool: Trim (T) - Click to choose tool"); break;
            case ArrangeTool::Range:  toolTooltip = T("Tool: Range Selection (R) - Click to choose tool"); break;
            case ArrangeTool::Blade:  toolTooltip = T("Tool: Blade / Cut (B) - Click to choose tool"); break;
            case ArrangeTool::Zoom:   toolTooltip = T("Tool: Zoom (Z) - Click to choose tool"); break;
            case ArrangeTool::Hand:   toolTooltip = T("Tool: Hand / Pan (H) - Click to choose tool"); break;
            case ArrangeTool::Pencil: toolTooltip = T("Tool: Pencil / Draw (P) - Click to choose tool"); break;
         }
         if (ImGui::IsItemHovered())
            HelpTip("%s", toolTooltip);

         // Draw current tool icon + chevron
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const float iconSize = (bmax.y - bmin.y) * 0.65f;
            const ImVec2 center(bmin.x + 12.5f, (bmin.y + bmax.y) * 0.5f);
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = toolNonDefault ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol);
            ImDrawList* tdl = ImGui::GetWindowDrawList();
            switch (gArrangeTool)
            {
               case ArrangeTool::Select: Tabler::DrawPointer(tdl, center, iconSize, icol, true); break;
               case ArrangeTool::Trim:   Tabler::DrawTrim(tdl, center, iconSize, icol); break;
               case ArrangeTool::Range:  Tabler::DrawRange(tdl, center, iconSize, icol); break;
               case ArrangeTool::Blade:  Tabler::DrawScissors(tdl, center, iconSize, icol); break;
               case ArrangeTool::Zoom:   Tabler::DrawZoom(tdl, center, iconSize, icol); break;
               case ArrangeTool::Hand:   Tabler::DrawHand(tdl, center, iconSize, icol); break;
               case ArrangeTool::Pencil: Tabler::DrawPencil(tdl, center, iconSize, icol); break;
            }
            // Small chevron down on the right edge
            const ImVec2 chevCenter(bmax.x - 6.5f, (bmin.y + bmax.y) * 0.5f);
            Tabler::DrawChevronDown(tdl, chevCenter, 8.0f, icol, 1.3f);
         }

         if (ImGui::BeginPopup("##arrangetoolpopup"))
         {
            if (ImGui::MenuItem(L("Select"), "A", gArrangeTool == ArrangeTool::Select))
            {
               gArrangeTool = ArrangeTool::Select;
               gArrangeBladeOn = false;
            }
            if (ImGui::MenuItem(L("Trim"), "T", gArrangeTool == ArrangeTool::Trim))
            {
               gArrangeTool = ArrangeTool::Trim;
               gArrangeBladeOn = false;
            }
            if (ImGui::MenuItem(L("Range Selection"), "R", gArrangeTool == ArrangeTool::Range))
            {
               gArrangeTool = ArrangeTool::Range;
               gArrangeBladeOn = false;
            }
            if (ImGui::MenuItem(L("Blade"), "B", gArrangeTool == ArrangeTool::Blade))
            {
               gArrangeTool = ArrangeTool::Blade;
               gArrangeBladeOn = true;
            }
            if (ImGui::MenuItem(L("Zoom"), "Z", gArrangeTool == ArrangeTool::Zoom))
            {
               gArrangeTool = ArrangeTool::Zoom;
               gArrangeBladeOn = false;
            }
            if (ImGui::MenuItem(L("Hand"), "H", gArrangeTool == ArrangeTool::Hand))
            {
               gArrangeTool = ArrangeTool::Hand;
               gArrangeBladeOn = false;
            }
            if (ImGui::MenuItem(L("Pencil (Draw Clip)"), "P", gArrangeTool == ArrangeTool::Pencil))
            {
               gArrangeTool = ArrangeTool::Pencil;
               gArrangeBladeOn = false;
            }
            ImGui::EndPopup();
         }

         // Add Marker (M): drops one at the playhead, on the snap grid.
         ImGui::SameLine();
         if (ImGui::Button("##arrangemarkerbtn", ImVec2(30, 0)))
            ArrangeAddMarkerAtPlayhead();
         if (ImGui::IsItemHovered())
            HelpTip("%s", T("Add Marker at Playhead (M)"));
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol;
            Tabler::DrawFlag(ImGui::GetWindowDrawList(), center, (bmax.y - bmin.y) * 0.65f, icol);
         }

         // Reset Row Heights: any track drag-resized off the default row
         // height (Lane::rowHeight != 0) snaps back to it. Placed right next
         // to the Inspector toggle below - both act on the header column/row
         // layout, and are the two icons most likely to be reached for
         // together.
         ImGui::SameLine();
         {
            bool anyResized = false;
            for (const Arrange::Lane& lane : gArrange.lanes)
               if (lane.rowHeight > 0.0f) { anyResized = true; break; }

            ImGui::BeginDisabled(!anyResized);
            if (ImGui::Button("##arrangeresetrowh", ImVec2(30, 0)))
            {
               ArrangeEdit([&]() {
                  for (Arrange::Lane& lane : gArrange.lanes)
                     lane.rowHeight = 0.0f;
               });
            }
            ImGui::EndDisabled();
            const ImVec2 rbmin = ImGui::GetItemRectMin();
            const ImVec2 rbmax = ImGui::GetItemRectMax();
            const ImVec2 rcenter((rbmin.x + rbmax.x) * 0.5f, (rbmin.y + rbmax.y) * 0.5f);
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            const ImU32 barCol = anyResized
               ? (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol)
               : (arrangeIconCol & 0x60FFFFFFu);
            Tabler::DrawLineHeight(ImGui::GetWindowDrawList(), rcenter, (rbmax.y - rbmin.y) * 0.65f, barCol);
            if (hovered)
               HelpTip(anyResized ? T("Reset all track heights to default") : T("All tracks already at default height"));
         }

         // Inspector / Clip Settings toggle. Icon is the edit/pencil
         // glyph for clip/track settings.
         ImGui::SameLine();
         const bool inspectorWasOpen = gArrangeClipSettingsPanelOpen;
         if (inspectorWasOpen)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentEmphasisPressed());
         }
         if (ImGui::Button("##clipsettingstoggle", ImVec2(30, 0)))
            gArrangeClipSettingsPanelOpen = !gArrangeClipSettingsPanelOpen;
         if (inspectorWasOpen)
            ImGui::PopStyleColor(2);
         if (ImGui::IsItemHovered())
            HelpTip(inspectorWasOpen ? T("Clip / Track Inspector: Open") : T("Toggle Clip / Track Inspector"));
         {
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 icol = inspectorWasOpen ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(255, 255, 255, 255) : arrangeIconCol);
            Tabler::DrawEdit(ImGui::GetWindowDrawList(), center, (bmax.y - bmin.y) * 0.65f, icol);
         }

         // The routing mode (gAudioMode) is owned by the "Enable Timeline
         // Audio" toggle pinned top-right above; engine power is the top
         // bar's Start/Stop Audio. Neither changes the other's state, except
         // that enabling timeline audio starts a stopped engine.
         arrangeToolbarBottom = ImGui::GetCursorScreenPos().y;
      }

      // Right-click anywhere on the toolbar row to reach the
      // panel's Dock Position menu (Timeline at Bottom / Timeline at Top).
      {
         const ImVec2 mouse = ImGui::GetIO().MousePos;
         const bool overToolbar = mouse.x >= panelOrigin.x && mouse.x < panelOrigin.x + panelSize.x &&
                                  mouse.y >= arrangeToolbarTop && mouse.y < arrangeToolbarBottom;
         if (overToolbar && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
             ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen("##arrangedockctx"))
            ImGui::OpenPopup("##arrangedockctx");
      }
      if (ImGui::BeginPopup("##arrangedockctx"))
      {
         if (ImGui::BeginMenu(L("Dock Position")))
         {
            const bool panelTop = gArrange.settings.dockSide == 1;
            if (ImGui::MenuItem(L("Timeline at Bottom"), nullptr, !panelTop) && panelTop)
            {
               gArrange.settings.dockSide = 0;
               gArrange.revision++; // a model field like any other (WP5b)
               gPatchDirty = true;
            }
            if (ImGui::MenuItem(L("Timeline at Top"), nullptr, panelTop) && !panelTop)
            {
               gArrange.settings.dockSide = 1;
               gArrange.revision++;
               gPatchDirty = true;
            }
            ImGui::EndMenu();
         }
         ImGui::Separator();
         if (ImGui::MenuItem(L("Close Arrangement Timeline")))
         {
            gArrangePanelOpen = false;
         }
         ImGui::EndPopup();
      }
      if (ImGui::BeginPopup("##arrangeviewportctx"))
      {
         if (ImGui::BeginMenu(L("Viewport Position")))
         {
            if (ImGui::MenuItem(L("Dock Left"), nullptr, !gArrangeViewportOnRight))
               gArrangeViewportOnRight = false;
            if (ImGui::MenuItem(L("Dock Right"), nullptr, gArrangeViewportOnRight))
               gArrangeViewportOnRight = true;
            ImGui::EndMenu();
         }
         ImGui::EndPopup();
      }

      ImGui::Separator();

      // Layout: Global Viewport Monitor alongside / above timeline lanes
      const bool isWide = panelSize.x >= 720.0f;
      const bool showVp = isWide && gArrangeShowViewport;
      const float kSettingsW = 210.0f;
      const float kViewportW = isWide ? std::clamp(panelSize.x * 0.28f, 180.0f, 320.0f) : panelSize.x;
      const float kViewportH = isWide ? std::max(120.0f, panelSize.y - 40.0f) : 140.0f;

      const float kHeaderWidth = 175.0f; // Mix strip (S M pan gain / opacity), name box, drag handle
      const float kMarkerStripH = 14.0f; // marker flags (WP6), above the tick/label strip
      const float kRulerHeight = 40.0f;  // marker strip + the 26 px tick/label strip
      const float kLaneHeight = 30.0f;  // default/group row height; a lane can be drag-resized off this via Lane::rowHeight
      const float kMinLaneHeight = kLaneHeight;  // can't shrink below the original size, only grow
      const float kMaxLaneHeight = 160.0f;
      const float kGroupIndent = 8.0f;  // per nesting depth, in the header column
      auto laneEffectiveHeight = [&](const Arrange::Lane& lane) -> float
      {
         return lane.rowHeight > 0.0f ? std::clamp(lane.rowHeight, kMinLaneHeight, kMaxLaneHeight) : kLaneHeight;
      };

      // Per-lane row layout, in coordinates relative to the top of the lane
      // area (not yet offset by any child's scrollTL). A depth-first walk of
      // the lane/group tree: root-level lanes and groups interleaved in
      // stored order (Arrange::TrackGroupChildren), each group recursing
      // into its own children before the walk moves to the next sibling.
      // There is no collapsing - every row always renders, so every lane's
      // height is always kLaneHeight.
      std::vector<float> arrangeLaneRelTop(gArrange.lanes.size(), 0.0f);
      std::vector<float> arrangeLaneRelH(gArrange.lanes.size(), 0.0f);
      std::vector<int> arrangeLaneDepth(gArrange.lanes.size(), 0);
      std::unordered_map<uint64_t, float> arrangeGroupHeaderRelTop;
      std::unordered_map<uint64_t, int> arrangeGroupDepth;
      float arrangeLanesRelBottom = 0.0f;
      // Flattened depth-first row order (lanes and group headers interleaved,
      // same order as the walk below) - used for shift+click range-select in
      // the header column, since selection is anchor-to-target in tree order.
      std::vector<Arrange::RowSlot> arrangeRowOrder;
      {
         float y = 0.0f;
         std::function<void(uint64_t, int)> walkGroupRows = [&](uint64_t parentGroupId, int depth)
         {
            for (const Arrange::RowSlot& slot : Arrange::TrackGroupChildren(gArrange, parentGroupId))
            {
               arrangeRowOrder.push_back(slot);
               if (slot.isGroup)
               {
                  arrangeGroupHeaderRelTop[slot.id] = y;
                  arrangeGroupDepth[slot.id] = depth;
                  y += kLaneHeight;
                  const Arrange::TrackGroup* grp = Arrange::FindTrackGroup(gArrange, slot.id);
                  if (grp && !grp->collapsed)
                     walkGroupRows(slot.id, depth + 1);
               }
               else
               {
                  const int li = Arrange::LaneIndex(gArrange, slot.id);
                  if (li >= 0)
                  {
                     const float rowH = laneEffectiveHeight(gArrange.lanes[(size_t)li]);
                     arrangeLaneRelTop[(size_t)li] = y;
                     arrangeLaneRelH[(size_t)li] = rowH;
                     arrangeLaneDepth[(size_t)li] = depth;
                     y += rowH;
                  }
               }
            }
         };
         walkGroupRows(0, 0);
         arrangeLanesRelBottom = y;
      }

      // Global viewport monitor - a self-contained child window, factored into
      // a lambda so it can be placed on either side of the timeline lanes
      // (gArrangeViewportOnRight, toggled via right-click on the monitor).
      auto drawViewportMonitor = [&]()
      {
         PushDockedPanelStyle(/*isChild=*/true);
         ImGui::BeginChild("##arrangeglobalmonitor", ImVec2(kViewportW, 0), true,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
         PopDockedPanelStyle();

         const ImVec2 monAvail = ImGui::GetContentRegionAvail();
         const int monW = std::max(16, (int)monAvail.x);
         const int monH = std::max(16, (int)monAvail.y);

         // Same clock and same skip rules as the composite itself.
         const int activeClipCount = CountActiveArrangeVideoClips(tr.Beats());

         const ImVec2 monOrigin = ImGui::GetCursorScreenPos();
         const ImVec2 monBR(monOrigin.x + monAvail.x, monOrigin.y + monAvail.y);
         ImDrawList* monDl = ImGui::GetWindowDrawList();
         DrawCheckerboardBackdrop(monDl, monOrigin, monBR);

         if (activeClipCount > 0)
         {
            // Only a request here: the composite runs after the cook loop
            // (CompositeArrangeMonitorIfRequested) into this same stable
            // texture, before ImGui renders the draw list recorded now.
            gArrangeMonitorTarget.requestW = monW;
            gArrangeMonitorTarget.requestH = monH;
            const unsigned int tex = gArrangeMonitorTarget.result.tex;
            if (tex != 0)
               monDl->AddImage((ImTextureID)(intptr_t)tex, monOrigin, monBR, ImVec2(0, 1), ImVec2(1, 0));
         }

         ImGui::Dummy(monAvail);

         // Right-click anywhere on the monitor to reach the Viewport Position menu
         // (Dock Left / Dock Right).
         if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
             !ImGui::IsPopupOpen("##arrangeviewportctx"))
            ImGui::OpenPopup("##arrangeviewportctx");

         if (ImGui::BeginPopup("##arrangeviewportctx"))
         {
            if (ImGui::BeginMenu(L("Viewport Position")))
            {
               if (ImGui::MenuItem(L("Dock Left"), nullptr, !gArrangeViewportOnRight))
                  gArrangeViewportOnRight = false;
               if (ImGui::MenuItem(L("Dock Right"), nullptr, gArrangeViewportOnRight))
                  gArrangeViewportOnRight = true;
               ImGui::EndMenu();
            }
            ImGui::EndPopup();
         }

         ImGui::EndChild();
      };

      if (showVp && !gArrangeViewportOnRight)
      {
         drawViewportMonitor();
         ImGui::SameLine();
      }

      // ---- Timeline body (ruler + lanes) ----
      // (kHeaderWidth/kMarkerStripH/kRulerHeight/kLaneHeight are declared above,
      // shared with the row-layout prepass.)

      // When docked right, reserve kViewportW (+ spacing) and/or kSettingsW (+ spacing)
      // up front so the scroll child doesn't eat the full remaining width before the
      // monitor / settings child get a chance to claim their share via SameLine() below.
      const float reservedRight = (showVp && gArrangeViewportOnRight ? (kViewportW + ImGui::GetStyle().ItemSpacing.x) : 0.0f)
                                + (gArrangeClipSettingsPanelOpen ? (kSettingsW + ImGui::GetStyle().ItemSpacing.x) : 0.0f);
      const float timelineChildWidth = reservedRight > 0.0f ? -reservedRight : 0.0f;
      PushDockedPanelStyle(/*isChild=*/true);
      // Cmd/Ctrl+wheel zooms (above); it must not also scroll the lanes.
      ImGui::BeginChild("##arrangetimelinescroll", ImVec2(timelineChildWidth, 0), false,
                        ImGuiWindowFlags_AlwaysUseWindowPadding |
                        (arrangeWheelZoomMod ? ImGuiWindowFlags_NoScrollWithMouse : 0));
      PopDockedPanelStyle();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 scrollTL = ImGui::GetCursorScreenPos();
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float headerStartX = scrollTL.x + ImGui::GetScrollX();
      const float rulerStartX = headerStartX + kHeaderWidth;
      sArrangeLastRulerStartX = rulerStartX;
      const float rulerWidth = std::max(10.0f, avail.x + ImGui::GetScrollX() - kHeaderWidth);
      // scrollTL.y already has the child's current vertical scroll baked in
      // (it moves up off-screen as the user scrolls down through many
      // tracks), which is exactly right for the lanes below but was also
      // being used for the ruler - pulling the ruler up out of view along
      // with them. pinnedTopY adds the scroll back out, giving the fixed
      // "top of the visible child" the ruler should stay glued to
      // regardless of how far down the track list is scrolled.
      const float pinnedTopY = scrollTL.y + ImGui::GetScrollY();
      // Scroll a clip Add to Timeline just made into view, once: in time if
      // any of it is off the visible width, and down/up to its lane if that
      // lane is under the pinned ruler or below the fold.
      if (gArrangeRevealClipId != 0)
      {
         const Arrange::Loc rl = Arrange::Find(gArrange, gArrangeRevealClipId);
         if (rl.Valid())
         {
            const Arrange::Clip& rc = gArrange.lanes[rl.lane].clips[rl.index];
            const double visBeats = std::max(1.0, (double)(avail.x - kHeaderWidth) / gArrangePixelsPerBeat);
            const double a = Arrange::TicksToBeats(rc.start);
            const double b = Arrange::TicksToBeats(rc.End());
            if (a < gArrangeScrollBeats || b > gArrangeScrollBeats + visBeats)
               gArrangeScrollBeats = std::max(0.0, a - visBeats * 0.1);
            const float laneScrollMax = arrangeLaneRelTop[(size_t)rl.lane];
            const float laneScrollMin = kRulerHeight + arrangeLaneRelTop[(size_t)rl.lane] + arrangeLaneRelH[(size_t)rl.lane] - avail.y;
            const float sy = ImGui::GetScrollY();
            if (sy > laneScrollMax)
               ImGui::SetScrollY(laneScrollMax);
            else if (sy < laneScrollMin)
               ImGui::SetScrollY(laneScrollMin);
         }
         // A few frames, not one: a lane added with the clip only counts
         // toward the child's scroll range from the frame after it is laid
         // out, so the first SetScrollY can be clamped short.
         if (!rl.Valid() || --gArrangeRevealFrames <= 0)
            gArrangeRevealClipId = 0;
      }

      // ---- view geometry (WP6: beats, not seconds) ----
      // The axis is laid out in quarter-note beats off Transport::Beats(), the
      // same clock the clips are scheduled on. A tempo change rescales what a
      // beat means in seconds and nothing on screen moves.
      const float ppb = gArrangePixelsPerBeat;
      const double arrBpm = std::max(1.0, (double)tr.Tempo());
      const double beatsPerBar = std::max(1.0, tr.BeatsPerBar());
      const Arrange::Tick barTicks = std::max<Arrange::Tick>(1, Arrange::BeatsToTicks(beatsPerBar));
      const double startBeat = gArrangeScrollBeats;
      const double endBeat = startBeat + (double)rulerWidth / ppb;
      const bool showTime = gArrange.settings.timeDisplay == 1;
      auto beatToX = [&](double b) { return rulerStartX + (float)((b - startBeat) * ppb); };
      auto tickToX = [&](Arrange::Tick t) { return beatToX(Arrange::TicksToBeats(t)); };
      auto xToTick = [&](float x) { return Arrange::BeatsToTicks(startBeat + (double)(x - rulerStartX) / ppb); };
      const double pxPerTick = (double)ppb / (double)Arrange::kPPQ;
      const Arrange::Tick startTick = std::max<Arrange::Tick>(0, Arrange::BeatsToTicks(startBeat));
      const Arrange::Tick endTick = Arrange::BeatsToTicks(endBeat);
      const Arrange::Tick snapThresholdTicks = std::max<Arrange::Tick>(1, (Arrange::Tick)(8.0 / std::max(1e-9, pxPerTick)));
      const Arrange::Tick gridTicks = ArrangeSnapGridTicks(); // 0 = snap off
      const Arrange::Tick playTick = ArrangePlayTick();
      // A lone point (scrub, marker, loop edge, Add Clip) lands on the grid
      // when snap is on, nothing else to weigh.
      auto gridSnap = [&](Arrange::Tick t)
      {
         t = std::clamp<Arrange::Tick>(t, 0, Arrange::kMaxTick);
         return gridTicks > 0 ? Arrange::SnapToGrid(t, gridTicks) : t;
      };

      // Ruler tick spacing in beats (minor) and bars (labelled major), each
      // kept legibly apart at any zoom. Bars mode labels the bars; Time mode
      // labels round seconds. The lane grid below reuses beatStep when snap
      // is off.
      static const double kStepMultiples[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0 };
      double beatStep = kStepMultiples[0];
      for (double st : kStepMultiples)
      {
         beatStep = st;
         if (ppb * st >= 10.0) break;
      }
      double barStep = 1.0;
      for (double st : kStepMultiples)
      {
         if (st < 1.0) continue;
         barStep = st;
         if (ppb * beatsPerBar * st >= 64.0) break;
      }

      // The ruler: a marker strip on top, the tick/label strip under it.
      const float kTickStripTop = pinnedTopY + kMarkerStripH;
      const ImVec2 rulerPos(rulerStartX, pinnedTopY);
      const ImVec2 rulerSize(rulerWidth, kRulerHeight);
      const bool isLight = IsThemeLight();
      const ImU32 rulerBg = isLight ? IM_COL32(238, 238, 242, 255) : IM_COL32(32, 32, 36, 255);
      const ImU32 markerStripBg = isLight ? IM_COL32(229, 229, 235, 255) : IM_COL32(26, 26, 30, 255);
      const ImU32 tickCol = isLight ? IM_COL32(140, 140, 150, 255) : IM_COL32(100, 100, 110, 255);
      const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text, 0.80f);
      const ImU32 subTextCol = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.80f);

      // Header-column corner, level with the ruler (above the first track
      // row) - otherwise blank/unstyled, reading as a hole next to the
      // filled ruler bar it sits beside.
      dl->AddRectFilled(ImVec2(headerStartX, rulerPos.y), ImVec2(rulerPos.x, kTickStripTop), markerStripBg);
      dl->AddRectFilled(ImVec2(headerStartX, kTickStripTop), ImVec2(rulerPos.x, rulerPos.y + rulerSize.y), rulerBg);

      dl->AddRectFilled(rulerPos, ImVec2(rulerPos.x + rulerSize.x, kTickStripTop), markerStripBg);
      dl->AddRectFilled(ImVec2(rulerPos.x, kTickStripTop), ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y), rulerBg);
      dl->AddLine(ImVec2(headerStartX, kTickStripTop), ImVec2(rulerPos.x + rulerSize.x, kTickStripTop), tickCol, 0.5f);
      dl->AddLine(ImVec2(headerStartX, rulerPos.y + rulerSize.y), ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y),
                  tickCol, 1.0f);

      // ---- ruler ticks and labels ----
      // Primary label in the chosen unit; the other unit follows it, dimmer,
      // only where it fits before the next label.
      {
         const float rulerBottom = rulerPos.y + rulerSize.y;
         const float labelY = kTickStripTop + 2.0f;
         dl->PushClipRect(rulerPos, ImVec2(rulerPos.x + rulerSize.x, rulerBottom), true);
         auto drawLabelPair = [&](float x, float nextX, const std::string& primary, const std::string& secondary)
         {
            dl->AddText(ImVec2(x + 3.0f, labelY), textCol, primary.c_str());
            const float pw = ImGui::CalcTextSize(primary.c_str()).x;
            const float sw = ImGui::CalcTextSize(secondary.c_str()).x;
            if (x + 3.0f + pw + 6.0f + sw + 4.0f < nextX)
               dl->AddText(ImVec2(x + 3.0f + pw + 6.0f, labelY), subTextCol, secondary.c_str());
         };
         if (!showTime)
         {
            // Minor ticks on every beatStep, skipping the bar lines.
            const double firstMinor = std::floor(startBeat / beatStep) * beatStep;
            for (double b = firstMinor; b <= endBeat + beatStep; b += beatStep)
            {
               if (b < 0.0) continue;
               const double inBar = std::fmod(b, beatsPerBar);
               if (inBar < 1e-6 || beatsPerBar - inBar < 1e-6) continue;
               const float x = beatToX(b);
               dl->AddLine(ImVec2(x, rulerBottom - 6.0f), ImVec2(x, rulerBottom), tickCol, 0.8f);
            }
            // Bars: every bar gets a mid tick, every barStep-th a label.
            const double barPx = ppb * beatsPerBar;
            const long long firstBar = std::max(0LL, (long long)std::floor(startBeat / beatsPerBar));
            const long long lastBar = (long long)std::ceil(endBeat / beatsPerBar) + 1;
            const long long labelEvery = std::max(1LL, (long long)barStep);
            for (long long bar = firstBar; bar <= lastBar; bar++)
            {
               const float x = beatToX((double)bar * beatsPerBar);
               const bool labelled = bar % labelEvery == 0;
               if (!labelled && barPx < 4.0)
                  continue;
               dl->AddLine(ImVec2(x, rulerBottom - (labelled ? 12.0f : 8.0f)), ImVec2(x, rulerBottom), tickCol, labelled ? 1.2f : 1.0f);
               if (labelled)
               {
                  const Arrange::Tick t = (Arrange::Tick)bar * barTicks;
                  drawLabelPair(x, x + (float)(barPx * (double)labelEvery), std::to_string(bar),
                                ArrangeFormatTickSeconds(t));
               }
            }
         }
         else
         {
            // Round-second steps; the minor ticks subdivide a major.
            static const double kSecSteps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1200.0, 3600.0 };
            const double pxPerSec = (double)ppb * arrBpm / 60.0;
            double majorSec = kSecSteps[0];
            for (double st : kSecSteps)
            {
               majorSec = st;
               if (pxPerSec * st >= 72.0) break;
            }
            static const int kSubdivs[] = { 10, 5, 4, 2, 1 };
            double minorSec = majorSec;
            for (int sd : kSubdivs)
               if (pxPerSec * majorSec / sd >= 8.0) { minorSec = majorSec / sd; break; }
            const double startSecV = startBeat * 60.0 / arrBpm;
            const double endSecV = endBeat * 60.0 / arrBpm;
            const long long firstIdx = std::max(0LL, (long long)std::floor(startSecV / minorSec));
            const long long lastIdx = (long long)std::ceil(endSecV / minorSec) + 1;
            const long long perMajor = std::max(1LL, (long long)std::llround(majorSec / minorSec));
            for (long long k = firstIdx; k <= lastIdx; k++)
            {
               const double sec = (double)k * minorSec;
               const float x = beatToX(sec * arrBpm / 60.0);
               if (k % perMajor == 0)
               {
                  dl->AddLine(ImVec2(x, rulerBottom - 12.0f), ImVec2(x, rulerBottom), tickCol, 1.2f);
                  drawLabelPair(x, x + (float)(pxPerSec * majorSec), ArrangeFormatSeconds(sec, majorSec < 1.0),
                                ArrangeFormatBBT(Arrange::SecondsToTicks(sec, arrBpm)));
               }
               else
               {
                  dl->AddLine(ImVec2(x, rulerBottom - 6.0f), ImVec2(x, rulerBottom), tickCol, 0.8f);
               }
            }
         }
         dl->PopClipRect();
      }

      // ---- marker flags (WP6) ----
      // Submitted before the ruler's own button: the first item submitted
      // under the mouse claims hover, so a flag always wins over the scrub.
      // Drag moves (one undo entry per drag, snapped), double-click renames,
      // right-click opens colour / delete. A copy is iterated: a drag re-sorts
      // gArrange.markers.
      bool markerHoveredAny = false;
      bool openMarkerCtx = false;
      {
         const std::vector<Arrange::Marker> markersNow = gArrange.markers;
         dl->PushClipRect(rulerPos, ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y), true);
         for (const Arrange::Marker& mk : markersNow)
         {
            const float fx = tickToX(mk.pos);
            const char* nm = mk.name.empty() ? "Marker" : mk.name.c_str();
            const float flagW = std::clamp(ImGui::CalcTextSize(nm).x + 10.0f, 12.0f, 120.0f);
            if (fx + flagW < rulerStartX || fx > rulerStartX + rulerWidth)
               continue;
            const ImVec2 f0(fx, pinnedTopY + 1.0f);
            const ImVec2 f1(fx + flagW, kTickStripTop - 1.0f);
            const ImU32 fcol = ArrangeMarkerColU32(mk.color);
            const float lum = 0.299f * (float)((mk.color >> 24) & 0xFF) + 0.587f * (float)((mk.color >> 16) & 0xFF) +
                              0.114f * (float)((mk.color >> 8) & 0xFF);
            const bool dragged = gArrangeMarkerDragId == mk.id;
            dl->AddRectFilled(f0, f1, fcol, 3.0f, ImDrawFlags_RoundCornersRight);
            // A hairline edge so a pale flag still reads on the light ruler.
            dl->AddRect(f0, f1, dragged ? IM_COL32(255, 255, 255, 230) : IM_COL32(0, 0, 0, isLight ? 110 : 150),
                        3.0f, ImDrawFlags_RoundCornersRight, dragged ? 1.5f : 1.0f);
            dl->AddLine(ImVec2(fx, pinnedTopY), ImVec2(fx, rulerPos.y + rulerSize.y), fcol, 1.5f);

            ImGui::PushID((int)(mk.id & 0x7fffffff));
            if (gArrangeRenamingMarkerId == mk.id)
            {
               static uint64_t sMarkerRenameFocusedId = 0;
               ImGui::SetCursorScreenPos(ImVec2(std::max(rulerStartX, fx), pinnedTopY - 2.0f));
               ImGui::SetNextItemWidth(120.0f);
               if (sMarkerRenameFocusedId != mk.id)
               {
                  ImGui::SetKeyboardFocusHere();
                  sMarkerRenameFocusedId = mk.id;
               }
               const bool commit = ImGui::InputText("##renamingmarker", gArrangeRenameMarkerBuffer,
                                                    sizeof(gArrangeRenameMarkerBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (commit || ImGui::IsItemDeactivated())
               {
                  const std::string newName = gArrangeRenameMarkerBuffer;
                  const uint64_t mid = mk.id;
                  if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                     ArrangeEdit([&]() { Arrange::RenameMarker(gArrange, mid, newName); });
                  gArrangeRenamingMarkerId = 0;
                  sMarkerRenameFocusedId = 0;
               }
            }
            else
            {
               dl->AddText(ImVec2(fx + 5.0f, pinnedTopY + (kMarkerStripH - ImGui::GetFontSize()) * 0.5f),
                           lum > 150.0f ? IM_COL32(20, 20, 24, 255) : IM_COL32(255, 255, 255, 255), nm);
            }

            const float hitX0 = std::max(rulerStartX, fx - 3.0f);
            const float hitX1 = std::min(rulerStartX + rulerWidth, fx + flagW);
            if (hitX1 - hitX0 >= 1.0f && gArrangeRenamingMarkerId != mk.id)
            {
               ImGui::SetCursorScreenPos(ImVec2(hitX0, pinnedTopY));
               ImGui::InvisibleButton("##markerflag", ImVec2(hitX1 - hitX0, kMarkerStripH));
               const bool hovered = ImGui::IsItemHovered();
               if (hovered)
               {
                  markerHoveredAny = true;
                  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
               }
               if (ImGui::IsItemActivated() && gArrangeDrag.mode == kArrangeDragNone)
               {
                  ArrangeGestureBegin();
                  gArrangeMarkerDragId = mk.id;
                  gArrangeMarkerDragGrabTick = xToTick(mouse.x);
                  gArrangeMarkerDragOrigPos = mk.pos;
               }
               if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
               {
                  gArrangeRenamingMarkerId = mk.id;
                  snprintf(gArrangeRenameMarkerBuffer, sizeof(gArrangeRenameMarkerBuffer), "%s", mk.name.c_str());
               }
               if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
               {
                  gArrangeCtxMarkerId = mk.id;
                  openMarkerCtx = true;
               }
            }
            ImGui::PopID();
         }
         dl->PopClipRect();

         // The drag itself, outside the loop so it survives the flag
         // scrolling out from under the mouse. MoveMarker is a no-op while
         // the target holds still; the release closes the gesture (one undo
         // entry, none if it landed where it started).
         if (gArrangeMarkerDragId != 0)
         {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               gArrangeMarkerDragId = 0;
               ArrangeGestureEnd();
            }
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            {
               const Arrange::Tick target =
                  gridSnap(gArrangeMarkerDragOrigPos + (xToTick(mouse.x) - gArrangeMarkerDragGrabTick));
               Arrange::MoveMarker(gArrange, gArrangeMarkerDragId, target);
            }
         }
      }

      // The ruler's own button: click-drag scrubs a ghost playhead (the
      // transport seeks once, on release - WP6), Shift+drag carves the loop.
      // In Range Selection mode (R), dragging on the ruler selects across ALL tracks.
      // Dragging along the loop borders ([start, end]) or loop header adjusts the loop.
      const float loopX0 = tickToX(gArrange.settings.loop.start);
      const float loopX1 = tickToX(gArrange.settings.loop.end);
      const float kBorderHitRadius = 7.0f;
      const bool mouseInRuler = mouse.x >= rulerPos.x && mouse.x < rulerPos.x + rulerSize.x &&
                                mouse.y >= rulerPos.y && mouse.y < rulerPos.y + rulerSize.y;
      const bool loopLeftBorderHovered = gArrange.settings.loop.enabled && mouseInRuler && !markerHoveredAny &&
                                         (std::abs(mouse.x - loopX0) <= kBorderHitRadius);
      const bool loopRightBorderHovered = gArrange.settings.loop.enabled && mouseInRuler && !markerHoveredAny &&
                                          (std::abs(mouse.x - loopX1) <= kBorderHitRadius);
      const bool loopHeaderHovered = gArrange.settings.loop.enabled && mouseInRuler && !markerHoveredAny &&
                                     !loopLeftBorderHovered && !loopRightBorderHovered &&
                                     (mouse.x > loopX0 && mouse.x < loopX1) &&
                                     (mouse.y >= kTickStripTop && mouse.y <= kTickStripTop + 8.0f);

      if (gArrangeLoopDragMode != kArrangeLoopDragNone)
      {
         if (gArrangeLoopDragMode == kArrangeLoopDragStart || gArrangeLoopDragMode == kArrangeLoopDragEnd)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
         else if (gArrangeLoopDragMode == kArrangeLoopDragMove)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      }
      else if (loopLeftBorderHovered || loopRightBorderHovered)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }
      else if (loopHeaderHovered)
      {
         ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      }

      ImGui::SetCursorScreenPos(rulerPos);
      ImGui::InvisibleButton("##arrangerulerbtn", rulerSize);
      const bool rulerShiftHeld = ImGui::GetIO().KeyShift;
      if (ImGui::IsItemActivated())
      {
         const Arrange::Tick at = gridSnap(xToTick(ImGui::GetIO().MousePos.x));
         if (loopLeftBorderHovered)
         {
            gArrangeLoopDragMode = kArrangeLoopDragStart;
            gArrangeLoopDragOrigStart = gArrange.settings.loop.start;
            gArrangeLoopDragOrigEnd = gArrange.settings.loop.end;
            gArrangeLoopDragGrabTick = xToTick(ImGui::GetIO().MousePos.x);
         }
         else if (loopRightBorderHovered)
         {
            gArrangeLoopDragMode = kArrangeLoopDragEnd;
            gArrangeLoopDragOrigStart = gArrange.settings.loop.start;
            gArrangeLoopDragOrigEnd = gArrange.settings.loop.end;
            gArrangeLoopDragGrabTick = xToTick(ImGui::GetIO().MousePos.x);
         }
         else if (loopHeaderHovered)
         {
            gArrangeLoopDragMode = kArrangeLoopDragMove;
            gArrangeLoopDragOrigStart = gArrange.settings.loop.start;
            gArrangeLoopDragOrigEnd = gArrange.settings.loop.end;
            gArrangeLoopDragGrabTick = xToTick(ImGui::GetIO().MousePos.x);
         }
         else if (gArrangeTool == ArrangeTool::Range)
         {
            gArrangeMarquee.active = true;
            gArrangeMarquee.allTracks = true;
            gArrangeMarquee.startLane = -1;
            gArrangeMarquee.anchor = ImGui::GetIO().MousePos;
            gArrangeMarquee.current = ImGui::GetIO().MousePos;
            gArrangeMarquee.baseSel = rulerShiftHeld ? gArrangeSel : std::set<uint64_t>();
         }
         else if (rulerShiftHeld)
         {
            gArrangeShiftDraggingLoop = true;
            gArrangeLoopDragAnchorTick = at;
         }
         else
         {
            ArrangeScrubBegin(at);
         }
      }
      if (gArrangeLoopDragMode != kArrangeLoopDragNone && ImGui::IsItemActive())
      {
         const Arrange::Tick curTick = xToTick(ImGui::GetIO().MousePos.x);
         if (gArrangeLoopDragMode == kArrangeLoopDragStart)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            Arrange::Tick newStart = gridSnap(curTick);
            if (newStart < 0) newStart = 0;
            if (newStart >= gArrange.settings.loop.end)
               newStart = std::max<Arrange::Tick>(0, gArrange.settings.loop.end - Arrange::kPPQ / 16);
            ArrangeSetLoop(true, newStart, gArrange.settings.loop.end);
         }
         else if (gArrangeLoopDragMode == kArrangeLoopDragEnd)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            Arrange::Tick newEnd = gridSnap(curTick);
            if (newEnd <= gArrange.settings.loop.start)
               newEnd = gArrange.settings.loop.start + Arrange::kPPQ / 16;
            ArrangeSetLoop(true, gArrange.settings.loop.start, newEnd);
         }
         else if (gArrangeLoopDragMode == kArrangeLoopDragMove)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            const Arrange::Tick tickDelta = gridSnap(curTick) - gridSnap(gArrangeLoopDragGrabTick);
            const Arrange::Tick loopLen = gArrangeLoopDragOrigEnd - gArrangeLoopDragOrigStart;
            Arrange::Tick newStart = gArrangeLoopDragOrigStart + tickDelta;
            if (newStart < 0) newStart = 0;
            Arrange::Tick newEnd = newStart + loopLen;
            ArrangeSetLoop(true, newStart, newEnd);
         }
      }
      if (gArrangeLoopDragMode != kArrangeLoopDragNone && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         gArrangeLoopDragMode = kArrangeLoopDragNone;
      }
      if (gArrangeTool == ArrangeTool::Range && gArrangeMarquee.active && gArrangeMarquee.allTracks)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            gArrangeMarquee.current = ImGui::GetIO().MousePos;
      }
      else if (gArrangeShiftDraggingLoop && ImGui::IsItemActive())
      {
         // Shift+drag on the ruler carves out [start,end) - dragging left of
         // the anchor extends the region backward instead of collapsing it.
         // Armed state is left alone mid-drag and settled on release below.
         // ArrangeSetLoop is a no-op while the mouse holds still.
         const Arrange::Tick curTick = gridSnap(xToTick(ImGui::GetIO().MousePos.x));
         ArrangeSetLoop(gArrange.settings.loop.enabled, std::min(gArrangeLoopDragAnchorTick, curTick),
                        std::max(gArrangeLoopDragAnchorTick, curTick));
      }
      else if (gArrangeScrubbing && ImGui::IsItemActive())
      {
         ArrangeScrubUpdate(gridSnap(xToTick(ImGui::GetIO().MousePos.x)));
      }
      // Released anywhere (or the button lost its active state): the one
      // seek. Reads gArrangeScrubButton rather than a hardcoded left button
      // so a Sample-body scrub (started on middle-click, see the per-clip
      // loop below) ends on ITS button releasing, not on left-mouse state
      // that was never down for it.
      if (gArrangeScrubbing && !ImGui::IsMouseDown(gArrangeScrubButton))
         ArrangeScrubEnd();
      // Sample-body scrub continuation: started from within the per-clip
      // loop below (middle-click on a Sample), so - unlike the ruler's own
      // drag above - it has no ImGui item to stay "active" against. Driven
      // here, once per frame, purely off the button state.
      if (gArrangeScrubbing && gArrangeScrubButton == ImGuiMouseButton_Middle &&
          ImGui::IsMouseDown(ImGuiMouseButton_Middle))
      {
         ArrangeScrubUpdate(gridSnap(xToTick(ImGui::GetIO().MousePos.x)));
      }
      if (gArrangeShiftDraggingLoop && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      {
         gArrangeShiftDraggingLoop = false;
         const Arrange::LoopRange& loop = gArrange.settings.loop;
         ArrangeSetLoop(loop.end - loop.start > Arrange::kPPQ / 16, loop.start, loop.end);
      }
      // Right-click the ruler while a loop region is armed to drop it (a
      // marker flag's own right-click opens its menu instead).
      if (mouseInRuler && !markerHoveredAny && gArrange.settings.loop.enabled && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         ArrangeSetLoop(false, gArrange.settings.loop.start, gArrange.settings.loop.end);

      if (openMarkerCtx)
         ImGui::OpenPopup("##arrangemarkerctx");
      if (ImGui::BeginPopup("##arrangemarkerctx"))
      {
         const Arrange::Marker* cm = nullptr;
         for (const Arrange::Marker& mk : gArrange.markers)
            if (mk.id == gArrangeCtxMarkerId) cm = &mk;
         if (cm == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            const uint64_t mid = cm->id;
            const uint32_t curColor = cm->color;
            ImGui::TextDisabled(T("Marker: %s"), cm->name.c_str());
            ImGui::TextDisabled("%s  |  %s", ArrangeFormatBBT(cm->pos).c_str(), ArrangeFormatTickSeconds(cm->pos).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(L("Rename")))
            {
               gArrangeRenamingMarkerId = mid;
               snprintf(gArrangeRenameMarkerBuffer, sizeof(gArrangeRenameMarkerBuffer), "%s", cm->name.c_str());
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", T("Colour"));
            for (int pi = 0; pi < 10; pi++)
            {
               if (pi % 5 != 0) ImGui::SameLine();
               ImGui::PushID(pi + 900);
               const uint32_t rgba = ArrangeMarkerRGBA(kArrangePalette[pi].col);
               const ImVec4 cVec = ImGui::ColorConvertU32ToFloat4(kArrangePalette[pi].col);
               if (rgba == curColor)
                  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
               if (ImGui::ColorButton(kArrangePalette[pi].name, cVec, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24)))
                  ArrangeEdit([&]() { Arrange::RecolorMarker(gArrange, mid, rgba); });
               if (rgba == curColor)
                  ImGui::PopStyleVar();
               ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("Delete Marker")))
               ArrangeEdit([&]() { Arrange::DeleteMarker(gArrange, mid); });
         }
         ImGui::EndPopup();
      }

      // Lane grid lines (every lane stripes its body with these): the snap
      // grid when snap is on, the ruler's beat ticks when it is off, doubled
      // until they sit at least 6 px apart; bar lines drawn stronger.
      struct ArrangeGridLine { float x; bool isMajor; };
      std::vector<ArrangeGridLine> arrangeGridLines;
      {
         Arrange::Tick g = gridTicks > 0 ? gridTicks : std::max<Arrange::Tick>(1, Arrange::BeatsToTicks(beatStep));
         while ((double)g * pxPerTick < 6.0 && g < Arrange::kMaxTick)
            g *= 2;
         for (Arrange::Tick t = Arrange::GridCeil(startTick, g); t <= endTick; t += g)
            if (t % barTicks != 0)
               arrangeGridLines.push_back({ tickToX(t), false });
         Arrange::Tick bg = barTicks;
         while ((double)bg * pxPerTick < 6.0 && bg < Arrange::kMaxTick)
            bg *= 2;
         for (Arrange::Tick t = Arrange::GridCeil(startTick, bg); t <= endTick; t += bg)
            arrangeGridLines.push_back({ tickToX(t), true });
      }

      const float lanesTopY = scrollTL.y + kRulerHeight;

      // Per-lane row layout: the shared relative computation above, offset
      // by this child's own top - every call site that needs a lane's
      // on-screen Y (hit-testing, drag preview, selection highlight,
      // drawing) reads this instead of `lanesTopY + index * kLaneHeight`.
      std::vector<float> laneRowTop(gArrange.lanes.size(), lanesTopY);
      for (size_t i = 0; i < gArrange.lanes.size(); i++)
         laneRowTop[i] = lanesTopY + arrangeLaneRelTop[i];
      const std::vector<float>& laneRowH = arrangeLaneRelH;
      std::unordered_map<uint64_t, float> groupHeaderRowTop;
      for (const auto& kv : arrangeGroupHeaderRelTop)
         groupHeaderRowTop[kv.first] = lanesTopY + kv.second;
      const float lanesContentBottom = lanesTopY + arrangeLanesRelBottom;
      // How far down the timeline reads as "filled" - past the real lanes
      // when the panel is taller than the track list, since that space is
      // now itself striped/gridded to look like more timeline rather than
      // dead space (see the empty-track-region fill below). The loop
      // highlight band and playhead line should reach exactly this far too,
      // not stop dead at the last real lane.
      const float arrangeFullBottom = std::max(lanesContentBottom, scrollTL.y + avail.y);
      // Reverse lookup: which lane row (if any) a screen Y falls in. Returns
      // -1 past the end, so callers must clamp/guard same as before.
      auto laneRowAt = [&](float y) -> int
      {
         for (size_t i = 0; i < laneRowTop.size(); i++)
         {
            if (laneRowH[i] > 0.0f && y >= laneRowTop[i] && y < laneRowTop[i] + laneRowH[i])
               return (int)i;
         }
         if (!laneRowTop.empty() && y < laneRowTop.front())
            return -1;
         // Past the last row: fall back to the last lane so drag targeting
         // still clamps sanely.
         return laneRowTop.empty() ? -1 : (int)laneRowTop.size() - 1;
      };

      // Continuous monotonic lane index lookup for Range / Marquee selections (no gaps between lanes)
      auto laneIndexForY = [&](float y) -> int
      {
         if (laneRowTop.empty()) return 0;
         if (y <= laneRowTop.front()) return 0;
         if (y >= laneRowTop.back()) return (int)laneRowTop.size() - 1;
         for (size_t li = 0; li < laneRowTop.size(); ++li)
         {
            const float nextTop = (li + 1 < laneRowTop.size()) ? laneRowTop[li + 1] : (laneRowTop[li] + laneRowH[li]);
            if (y >= laneRowTop[li] && y < nextTop)
               return (int)li;
         }
         return std::clamp((int)laneRowTop.size() - 1, 0, (int)laneRowTop.size() - 1);
      };

      // Clip-edge snap. With snap on the grid point is always taken (a hard
      // quantize); the playhead, 0 and `extra` (neighbour edges) are magnets
      // that win when within ~8 px and nearer than the grid point. With snap
      // off only the magnets apply. `*dist` gets the winning distance, or a
      // value past the threshold when nothing is in reach.
      auto snapTick = [&](Arrange::Tick t, const std::vector<Arrange::Tick>& extra, Arrange::Tick* dist) -> Arrange::Tick
      {
         Arrange::Tick best = t;
         Arrange::Tick bestDist = snapThresholdTicks + 1;
         if (gridTicks > 0)
         {
            best = Arrange::SnapToGrid(t, gridTicks);
            bestDist = best > t ? best - t : t - best;
         }
         auto consider = [&](Arrange::Tick c)
         {
            const Arrange::Tick d = c > t ? c - t : t - c;
            if (d <= snapThresholdTicks && d < bestDist) { bestDist = d; best = c; }
         };
         consider(playTick);
         consider(0);
         for (Arrange::Tick e : extra)
            consider(e);
         if (dist != nullptr)
            *dist = bestDist;
         return best;
      };

      // Both edges of every clip on `laneIdx` at gesture start, minus the
      // clips being dragged - the neighbours a drag snaps to. Bounds come from
      // the lane the clip is landing on, not the one it left.
      auto laneEdges = [&](int laneIdx, const std::vector<uint64_t>& exclude)
      {
         std::vector<Arrange::Tick> e;
         const Arrange::Model& src = gArrangeGestureOpen ? gArrangeGestureBefore : gArrange;
         if (laneIdx < 0 || laneIdx >= (int)src.lanes.size())
            return e;
         for (const Arrange::Clip& c : src.lanes[laneIdx].clips)
         {
            if (std::find(exclude.begin(), exclude.end(), c.id) != exclude.end())
               continue;
            e.push_back(c.start);
            e.push_back(c.End());
         }
         return e;
      };

      // ---- the live clip drag ----
      // Every frame the mouse moves, gArrange is rebuilt as (gesture snapshot
      // + this drag) through the same op the release commits, so the view is
      // exactly the drop. Mouse-up ends the gesture: one undo entry, or none
      // if nothing ended up different.
      {
         ArrangeDragState& drag = gArrangeDrag;
         if (drag.mode != kArrangeDragNone)
         {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               const bool wasClick = !drag.live && drag.collapseOnClick;
               const uint64_t clicked = drag.clipId;
               const bool single = drag.singleMember;
               ArrangeDragEnd();
               if (wasClick && Arrange::Find(gArrange, clicked).Valid())
               {
                  std::vector<uint64_t> ids{ clicked };
                  if (!single)
                     ids = Arrange::ExpandSelectionToGroups(gArrange, ids);
                  gArrangeSel.clear();
                  gArrangeSel.insert(ids.begin(), ids.end());
                  gArrangeSelAnchor = clicked;
               }
            }
            else
            {
               if (!drag.live && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                  drag.live = true;
               if (drag.live)
               {
                  const Arrange::Tick rawDelta = xToTick(mouse.x) - drag.grabTick;
                  if (drag.mode == kArrangeDragMove)
                  {
                     const int laneCount = (int)gArrange.lanes.size();
                     const int mouseLane = std::clamp(laneRowAt(mouse.y), 0, std::max(0, laneCount - 1));
                     const int laneDelta = mouseLane - drag.grabLane;
                     const Arrange::Tick len = drag.origEnd - drag.origStart;
                     // Snap against the lane the grabbed clip would land on
                     // (its own lane when the lane move is refused).
                     int targetLane = drag.grabLane + laneDelta;
                     if (targetLane < 0 || targetLane >= laneCount || drag.grabLane >= laneCount ||
                         gArrange.lanes[targetLane].type != gArrange.lanes[drag.grabLane].type)
                        targetLane = drag.grabLane;
                     const std::vector<Arrange::Tick> edges = laneEdges(targetLane, drag.ids);
                     const Arrange::Tick cand = drag.origStart + rawDelta;
                     Arrange::Tick dStart = 0, dEnd = 0;
                     const Arrange::Tick sStart = snapTick(cand, edges, &dStart);
                     const Arrange::Tick sEnd = snapTick(cand + len, edges, &dEnd) - len;
                     const Arrange::Tick chosen = dEnd < dStart ? sEnd : sStart;
                     ArrangeDragUpdate(chosen - drag.origStart, laneDelta);
                  }
                  else
                  {
                     const Arrange::Tick origEdge = drag.edge == Arrange::kEdgeStart ? drag.origStart : drag.origEnd;
                     std::vector<Arrange::Tick> edges;
                     if (drag.mode == kArrangeDragTrimStart || drag.mode == kArrangeDragTrimEnd)
                        edges = laneEdges(drag.grabLane, { drag.clipId });
                     const Arrange::Tick snapped = snapTick(origEdge + rawDelta, edges, nullptr);
                     ArrangeDragUpdate(std::max<Arrange::Tick>(0, snapped), 0);
                  }
               }
            }
         }
      }

      // Group bounds (tick span and lane rows), for the group-edge hit test
      // and the selected-group outline.
      struct ArrangeGroupSpan { Arrange::Tick start, end; int laneMin, laneMax; };
      std::map<uint64_t, ArrangeGroupSpan> arrangeGroupSpans;
      for (int li = 0; li < (int)gArrange.lanes.size(); li++)
      {
         for (const Arrange::Clip& c : gArrange.lanes[li].clips)
         {
            if (c.groupId == 0)
               continue;
            auto it = arrangeGroupSpans.find(c.groupId);
            if (it == arrangeGroupSpans.end())
               arrangeGroupSpans[c.groupId] = { c.start, c.End(), li, li };
            else
            {
               it->second.start = std::min(it->second.start, c.start);
               it->second.end = std::max(it->second.end, c.End());
               it->second.laneMin = std::min(it->second.laneMin, li);
               it->second.laneMax = std::max(it->second.laneMax, li);
            }
         }
      }

      // Draw Lanes
      uint64_t laneToDelete = 0;
      bool openClipCtx = false;
      bool openAddClip = false;
      // static: the right-click that opens "##arrangeaddclip" and the frame
      // its "Add Clip" is clicked are different frames.
      static uint64_t addClipToLaneId = 0;
      static Arrange::Tick addClipAtTick = 0;

      // Right-click on a track's drag handle: group membership menu.
      bool openLaneCtx = false;
      static uint64_t ctxLaneId = 0;

      // Right-click on a group header row: rename/recolor/duplicate/delete.
      bool openGroupCtx = false;
      static uint64_t ctxGroupId = 0;

      // Drag-to-reparent payload: a lane's drag badge and a group header
      // row's grip both publish this; a group header row is the only drop
      // target that accepts it. Dragging a group carries its whole subtree
      // implicitly - subtree membership is defined by parentGroupId links,
      // not physical array position, so reparenting the one group record is
      // all a "drag the folder" gesture needs to do.
      struct ArrangeRowRef { bool isGroup; uint64_t id; };

      // Deferred row move (reorder AND/OR regroup in one drag), set by
      // ArrangeRowDropTarget below and applied once after BOTH the group
      // header pass and the lane pass have finished this frame - applying
      // it mid-loop would reshape gArrange.lanes (and dangle `lane`/
      // laneRowTop/laneRowH for the rest of the loop) the same way a
      // same-frame Add Track does; see the loop-bound comment above.
      bool pendingRowMove = false;
      ArrangeRowRef pendingMoveSrc{ false, 0 };
      bool pendingMoveTargetIsGroup = false;
      uint64_t pendingMoveTargetId = 0;
      int pendingMoveZone = 0; // 0 = above (prev sibling), 1 = into (as child), 2 = below (next sibling)

      // Shared row drop target for both lane rows and group header rows -
      // the one mechanism for reordering AND regrouping a row via drag,
      // replacing the old "Group With"/"Move to Group" context-menu items.
      // A lane target has two zones (top/bottom half = become its previous/
      // next sibling, inheriting its groupId either way - this is how a
      // drag pulls a row out of a group, by dropping it next to a
      // top-level lane). A group target has three (top/bottom 25% = become
      // ITS sibling at its own level; the middle 50% = join it as a
      // child).
      auto ArrangeRowDropTarget = [&](bool targetIsGroup, uint64_t targetId, ImVec2 rowMin, ImVec2 rowMax)
      {
         if (!ImGui::BeginDragDropTarget())
            return;
         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ARRANGE_ROW_REF",
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
         {
            IM_ASSERT(payload->DataSize == sizeof(ArrangeRowRef));
            const ArrangeRowRef ref = *(const ArrangeRowRef*)payload->Data;
            const bool isSelfDrop = (ref.isGroup == targetIsGroup) && (ref.id == targetId);
            if (!isSelfDrop)
            {
               const float relY = (mouse.y - rowMin.y) / std::max(1.0f, rowMax.y - rowMin.y);
               const int zone = !targetIsGroup ? (relY < 0.5f ? 0 : 2)
                                                : (relY < 0.30f ? 0 : relY > 0.70f ? 2 : 1);
               if (zone == 1)
               {
                  dl->AddRect(rowMin, rowMax, IM_COL32(59, 130, 246, 255), 3.0f, 0, 2.0f);
               }
               else
               {
                  const float lineY = (zone == 0) ? rowMin.y : rowMax.y;
                  dl->AddLine(ImVec2(rowMin.x, lineY), ImVec2(rowMax.x, lineY), IM_COL32(59, 130, 246, 255), 2.5f);
                  dl->AddCircleFilled(ImVec2(rowMin.x + 3.0f, lineY), 4.0f, IM_COL32(59, 130, 246, 255));
                  dl->AddCircleFilled(ImVec2(rowMax.x - 3.0f, lineY), 4.0f, IM_COL32(59, 130, 246, 255));
               }
               if (payload->IsDelivery())
               {
                  pendingRowMove = true;
                  pendingMoveSrc = ref;
                  pendingMoveTargetIsGroup = targetIsGroup;
                  pendingMoveTargetId = targetId;
                  pendingMoveZone = zone;
               }
            }
         }
         ImGui::EndDragDropTarget();
      };

      // Multi-row selection over the header column (declared at file scope,
      // see gArrangeRowSel above). Click sets/replaces the selection and
      // moves the anchor; shift+click range-selects anchor..target inclusive
      // in flattened tree order (arrangeRowOrder); cmd/ctrl+click toggles one
      // row without disturbing the rest.
      auto SelectRowRange = [&](uint64_t fromId, uint64_t toId)
      {
         size_t aIdx = SIZE_MAX, bIdx = SIZE_MAX;
         for (size_t ri = 0; ri < arrangeRowOrder.size(); ri++)
         {
            if (arrangeRowOrder[ri].id == fromId) aIdx = ri;
            if (arrangeRowOrder[ri].id == toId) bIdx = ri;
         }
         if (aIdx == SIZE_MAX || bIdx == SIZE_MAX) return;
         if (aIdx > bIdx) std::swap(aIdx, bIdx);
         for (size_t ri = aIdx; ri <= bIdx; ri++)
            gArrangeRowSel.insert(arrangeRowOrder[ri].id);
      };
      auto HandleRowClick = [&](uint64_t rowId)
      {
         gArrangeSel.clear();
         gArrangeSelAnchor = 0;
         const ImGuiIO& rio = ImGui::GetIO();
         const bool toggleMod = rio.KeyCtrl || rio.KeySuper;
         if (rio.KeyShift && gArrangeRowSelAnchor != 0)
         {
            if (!toggleMod)
               gArrangeRowSel.clear();
            SelectRowRange(gArrangeRowSelAnchor, rowId);
         }
         else if (toggleMod)
         {
            if (gArrangeRowSel.count(rowId)) gArrangeRowSel.erase(rowId);
            else gArrangeRowSel.insert(rowId);
            gArrangeRowSelAnchor = rowId;
         }
         else
         {
            gArrangeRowSel.clear();
            gArrangeRowSelAnchor = rowId;
            gArrangeRowSel.insert(rowId);
         }
      };
      // Double-click a row: open its inspector; double-click the SAME row
      // again while its inspector is already open closes it (toggle).
      auto ToggleOrOpenRowSettings = [&](uint64_t rowId)
      {
         if (gArrangeClipSettingsPanelOpen && gArrangeSettingsPanelTarget == rowId)
         {
            gArrangeClipSettingsPanelOpen = false;
         }
         else
         {
            HandleRowClick(rowId);
            gArrangeClipSettingsPanelOpen = true;
            gArrangeSettingsPanelTarget = rowId;
         }
      };

      auto ArrangeGroupDisplayName = [](const Arrange::TrackGroup& g)
      {
         return g.name.empty() ? ("Group " + std::to_string(g.id)) : g.name;
      };

      // One header row per track group, drawn the first time it is reached
      // in the tree walk below (groupHeaderRowTop already has its Y). No
      // collapsing - a group row is strictly indent + folder icon + color
      // swatch + editable name, plus the enabled toggle and member count;
      // it never gets mixer controls (gain/pan/mute/solo/opacity), those are
      // leaf-track-only. rename/recolor/duplicate/delete are behind a
      // right-click (openGroupCtx/ctxGroupId, handled after the loop like
      // every other arrange context menu).
      auto DrawArrangeGroupHeaderRow = [&](uint64_t groupId, float rowTop)
      {
         const Arrange::TrackGroup* grp = Arrange::FindTrackGroup(gArrange, groupId);
         if (grp == nullptr)
            return;
         ImGui::PushID((int)(groupId & 0x7fffffff) | (1 << 30));

         const ImU32 tint = ArrangeMarkerColU32(grp->color);
         dl->AddRectFilled(ImVec2(headerStartX, rowTop), ImVec2(rulerStartX + rulerWidth, rowTop + kLaneHeight),
                            (tint & 0x00FFFFFFu) | 0x28000000u);
         dl->AddLine(ImVec2(headerStartX, rowTop + kLaneHeight), ImVec2(rulerStartX + rulerWidth, rowTop + kLaneHeight),
                     tickCol, 1.0f);
         dl->AddLine(ImVec2(rulerStartX, rowTop), ImVec2(rulerStartX, rowTop + kLaneHeight), tickCol, 1.0f);

         const auto depthIt = arrangeGroupDepth.find(groupId);
         const int depth = (depthIt != arrangeGroupDepth.end()) ? depthIt->second : 0;
         const float indentX = headerStartX + 4.0f + (float)depth * kGroupIndent;
         for (int d = 0; d < depth; d++)
         {
            const float lineX = headerStartX + 6.0f + (float)d * kGroupIndent;
            dl->AddLine(ImVec2(lineX, rowTop), ImVec2(lineX, rowTop + kLaneHeight), tint & 0x60FFFFFFu, 1.0f);
         }

         const bool grpRowSelected = gArrangeRowSel.count(groupId) != 0;
         if (grpRowSelected)
         {
            const ImU32 selFill = isLight ? IM_COL32(139, 92, 246, 35) : IM_COL32(139, 92, 246, 45);
            const ImU32 selBorder = IM_COL32(167, 139, 250, 180);
            dl->AddRectFilled(ImVec2(headerStartX + 1.0f, rowTop + 1.0f), ImVec2(rulerStartX - 1.0f, rowTop + kLaneHeight - 1.0f), selFill, 2.0f);
            dl->AddRect(ImVec2(headerStartX + 1.0f, rowTop + 1.0f), ImVec2(rulerStartX - 1.0f, rowTop + kLaneHeight - 1.0f), selBorder, 2.0f, 0, 1.5f);
         }

         ImGui::SetCursorScreenPos(ImVec2(indentX, rowTop + 4.0f));
         if (ImGui::InvisibleButton("##grpcollapsebtn", ImVec2(28.0f, 20.0f)))
         {
            ArrangeEdit([&]() {
               Arrange::SetTrackGroupCollapsed(gArrange, groupId, !grp->collapsed);
            });
         }
         const bool foldHovered = ImGui::IsItemHovered();
         const ImU32 chevronCol = foldHovered ? (isLight ? IM_COL32(50, 50, 60, 255) : IM_COL32(240, 240, 240, 255)) : (tint | 0xE0000000u);
         const ImVec2 chevCenter(indentX + 6.0f, rowTop + kLaneHeight * 0.5f);
         if (grp->collapsed)
            Tabler::DrawChevronRight(dl, chevCenter, 11.0f, chevronCol);
         else
            Tabler::DrawChevronDown(dl, chevCenter, 11.0f, chevronCol);

         Tabler::DrawFolder(dl, ImVec2(indentX + 20.0f, rowTop + kLaneHeight * 0.5f), 13.0f, tint);

         ImGui::SameLine(0.0f, 6.0f);

         const float grpNameW = std::max(40.0f, rulerStartX - ImGui::GetCursorScreenPos().x - 24.0f);
         if (gArrangeRenamingLaneId == groupId)
         {
            ImGui::SetNextItemWidth(grpNameW);
            char grpNameBuf[128];
            snprintf(grpNameBuf, sizeof(grpNameBuf), "%s", grp->name.c_str());
            if (gArrangeRenameJustStarted)
            {
               ImGui::SetKeyboardFocusHere();
               gArrangeRenameJustStarted = false;
            }
            const bool grpNameEdited = ImGui::InputText("##groupname", grpNameBuf, sizeof(grpNameBuf),
                                                        ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemActivated())
               ArrangeGestureBegin();
            if (grpNameEdited && grp->name != grpNameBuf)
            {
               if (!gArrangeGestureOpen)
                  ArrangeGestureBegin();
               Arrange::RenameTrackGroup(gArrange, groupId, grpNameBuf);
            }
            if (ImGui::IsItemDeactivated())
            {
               ArrangeGestureEnd();
               gArrangeRenamingLaneId = 0;
            }
         }
         else
         {
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
            ImGui::Selectable(ArrangeGroupDisplayName(*grp).c_str(), false, ImGuiSelectableFlags_None,
                              ImVec2(grpNameW, kLaneHeight - 8.0f));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               ToggleOrOpenRowSettings(groupId);
            }
            else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
               HandleRowClick(groupId);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
               if (!gArrangeRowSel.count(groupId))
                  HandleRowClick(groupId);
               ctxGroupId = groupId;
               openGroupCtx = true;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
               ArrangeRowRef ref{true, groupId};
               ImGui::SetDragDropPayload("ARRANGE_ROW_REF", &ref, sizeof(ref));
               ImGui::BeginTooltip();
               ImGui::Text(T("Move Group: %s"), ArrangeGroupDisplayName(*grp).c_str());
               ImGui::EndTooltip();
               ImGui::EndDragDropSource();
            }
         }

         ImGui::SameLine(0.0f, 2.0f);
         const int memberCount = (int)Arrange::LanesInTrackGroup(gArrange, groupId).size();
         ImGui::TextDisabled("(%d)", memberCount);

         ImGui::SetCursorScreenPos(ImVec2(headerStartX, rowTop));
         ImGui::InvisibleButton("##grprowctx", ImVec2(rulerStartX - headerStartX, kLaneHeight));
         if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         {
            ctxGroupId = groupId;
            openGroupCtx = true;
         }
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            ToggleOrOpenRowSettings(groupId);
         }
         else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            HandleRowClick(groupId);
         }

         ArrangeRowDropTarget(/*targetIsGroup=*/true, groupId,
                              ImVec2(headerStartX, rowTop), ImVec2(rulerStartX, rowTop + kLaneHeight));

         ImGui::PopID();
      };

      // Adds a one-bar unassigned clip and hands straight to the canvas
      // click-to-assign flow. Shared by the "Add Clip" popup item and a
      // double-click on empty lane space.
      auto AddUnassignedClipAt = [&](uint64_t laneIdArg, Arrange::Tick atTick)
      {
         uint64_t made = 0;
         ArrangeEdit([&]()
         {
            Arrange::Clip c;
            c.start = atTick;
            c.length = Arrange::kTicksPerBar;
            Arrange::PlaceOverwrite(gArrange, laneIdArg, c, &made);
         });
         if (made != 0)
         {
            gArrangeSel = { made };
            gArrangeSelAnchor = made;
            gArrangeAssigningClipId = made;
         }
      };

      // Shared "insert a new track" popup body - opened either from the
      // empty-state prompt below (no tracks yet) or from a per-row "+" next
      // to a track's drag handle further down, with gArrangeAddTrackInsertAfter
      // set beforehand to say where it lands (-1 = append at end).
      auto InsertArrangeTrack = [&](bool isVideo, uint64_t explicitGroupId = 0)
      {
         const int type = isVideo ? Arrange::kLaneVideo : Arrange::kLaneAudio;
         ArrangeEdit([&]()
         {
            const int at = (gArrangeAddTrackInsertAfter < 0 || gArrangeAddTrackInsertAfter >= (int)gArrange.lanes.size())
               ? -1 : gArrangeAddTrackInsertAfter + 1;
            const uint64_t parentGroup = (explicitGroupId != 0) ? explicitGroupId :
               ((gArrangeAddTrackInsertAfter >= 0 && gArrangeAddTrackInsertAfter < (int)gArrange.lanes.size())
                  ? gArrange.lanes[gArrangeAddTrackInsertAfter].groupId : 0);
            const uint64_t id = Arrange::AddLane(gArrange, type, at);
            if (Arrange::Lane* l = Arrange::FindLane(gArrange, id))
            {
               l->name = Arrange::UniqueLaneName(gArrange, isVideo ? "Video" : "Audio", type);
               l->groupId = parentGroup;
            }
         });
      };

      if (gArrange.lanes.empty())
      {
         ImGui::SetCursorScreenPos(ImVec2(scrollTL.x + 4.0f, pinnedTopY + kRulerHeight + 10.0f));
         if (ImGui::Button(L("+ Add Track"), ImVec2(120, 0)))
         {
            gArrangeAddTrackInsertAfter = -1;
            ImGui::OpenPopup("##arrangeaddtrackpopup");
         }
      }
      if (ImGui::BeginPopup("##arrangeaddtrackpopup"))
      {
         if (ImGui::MenuItem(L("Video Track")))
            InsertArrangeTrack(true);
         if (ImGui::MenuItem(L("Audio Track")))
            InsertArrangeTrack(false);
         ImGui::EndPopup();
      }

      // Clip lane drawing to below the (now pinned) ruler strip, so a track
      // scrolled up under it is cut off cleanly instead of painting over the
      // ruler - the ruler itself is drawn unclipped further up, at a fixed
      // pinnedTopY, so it always stays on top of/above whatever scrolled.
      dl->PushClipRect(ImVec2(scrollTL.x, pinnedTopY + kRulerHeight),
                        ImVec2(scrollTL.x + avail.x, pinnedTopY + std::max(avail.y, kRulerHeight)), true);

      // Shift+drag marquee-select: tracks the live rectangle here; each
      // clip below tests itself against it and lands in marqueeHits, then
      // the whole thing is unioned onto the selection captured at
      // mouse-down (baseSel) once the per-lane loop is done drawing.
      bool marqueeFinalizeNow = false;
      std::set<uint64_t> marqueeHits;
      Arrange::Tick marqueeMinTick = 0;
      Arrange::Tick marqueeMaxTick = 0;
      float marqueeX0 = 0.0f;
      float marqueeX1 = 0.0f;
      int marqueeMinLane = 0;
      int marqueeMaxLane = (int)laneRowTop.size() - 1;
      if (gArrangeMarquee.active)
      {
         if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            gArrangeMarquee.current = mouse;
         else
            marqueeFinalizeNow = true;

         const Arrange::Tick tAnchor = gridSnap(xToTick(gArrangeMarquee.anchor.x));
         const Arrange::Tick tCurrent = gridSnap(xToTick(gArrangeMarquee.current.x));
         marqueeMinTick = std::min(tAnchor, tCurrent);
         marqueeMaxTick = std::max(tAnchor, tCurrent);
         marqueeX0 = std::max(rulerStartX, tickToX(marqueeMinTick));
         marqueeX1 = std::min(rulerStartX + rulerWidth, tickToX(marqueeMaxTick));

         if (!gArrangeMarquee.allTracks && !laneRowTop.empty())
         {
            const int startL = (gArrangeMarquee.startLane >= 0) ? gArrangeMarquee.startLane : laneIndexForY(gArrangeMarquee.anchor.y);
            const int curL = laneIndexForY(gArrangeMarquee.current.y);
            marqueeMinLane = std::clamp(std::min(startL, curL), 0, (int)laneRowTop.size() - 1);
            marqueeMaxLane = std::clamp(std::max(startL, curL), 0, (int)laneRowTop.size() - 1);
         }
      }

      // Every group header draws once, up front - not lane-triggered. With
      // nesting a group's only content can be other groups (no direct lane
      // member at all), so nothing in the per-lane loop below would ever
      // reach it; groupHeaderRowTop already has every group at every depth
      // from the tree walk, and none of these rows' Y ranges overlap, so
      // draw order among them doesn't matter.
      for (const auto& kv : groupHeaderRowTop)
         DrawArrangeGroupHeaderRow(kv.first, kv.second);

      bool anyLaneSolo = false;
      for (const Arrange::Lane& l : gArrange.lanes)
         anyLaneSolo = anyLaneSolo || (l.type == Arrange::kLaneAudio && l.solo);

      // Bounded by laneRowTop's size, not gArrange.lanes.size(): both
      // laneRowTop and arrangeLaneRelH (aliased as laneRowH) were sized
      // earlier this same frame, before "+ Add Track" (InsertArrangeTrack)
      // or a drop-import could have synchronously grown gArrange.lanes -
      // indexing them by the now-larger live lane count reads past their
      // end. A lane added this frame just waits one frame to draw, once
      // the layout arrays are rebuilt from the grown list.
      for (size_t i = 0; i < gArrange.lanes.size() && i < laneRowTop.size(); i++)
      {
         // Only the header's name field and mix strip write through this
         // reference; every clip edit is deferred to an id-addressed op after the loop, so the
         // lane vector never reshapes under it.
         Arrange::Lane& lane = gArrange.lanes[i];
         const uint64_t laneId = lane.id;

         // Scoped by lane id, not row, so a reorder never hands one lane's
         // active name field to another.
         const int laneScope = (int)(laneId & 0x7fffffff);
         ImGui::PushID(laneScope);

         if (laneRowH[i] <= 0.0f)
         {
            ImGui::PopID();
            continue;
         }

         const float curY = laneRowTop[i];
         const float rowH = laneRowH[i];

         // Lane background
         const ImU32 laneBg = (i % 2 == 0)
            ? (isLight ? IM_COL32(245, 245, 248, 255) : IM_COL32(24, 24, 28, 255))
            : (isLight ? IM_COL32(250, 250, 252, 255) : IM_COL32(28, 28, 32, 255));
         dl->AddRectFilled(ImVec2(headerStartX, curY),
                           ImVec2(rulerStartX + rulerWidth, curY + rowH), laneBg);
         dl->AddLine(ImVec2(headerStartX, curY + rowH),
                     ImVec2(rulerStartX + rulerWidth, curY + rowH),
                     tickCol, 0.5f);

         // Header-column background drop target & selection click-catcher.
         // Submitted before the name box and mix strip below, so it MUST
         // allow overlap - otherwise, per ImGui's hover rule (first item
         // whose rect contains the mouse wins the frame's hover and blocks
         // everything submitted after it), this full-row button would
         // silently eat every click meant for the name box, S/M, pan and
         // gain controls, leaving only row selection (handled right here)
         // actually reachable.
         ImGui::SetCursorScreenPos(ImVec2(headerStartX, curY));
         ImGui::SetNextItemAllowOverlap();
         ImGui::InvisibleButton("##lanerowbg", ImVec2(rulerStartX - headerStartX, rowH));
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
         {
            ToggleOrOpenRowSettings(laneId);
         }
         else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         {
            HandleRowClick(laneId);
         }
         if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         {
            if (!gArrangeRowSel.count(laneId))
               HandleRowClick(laneId);
            ctxLaneId = laneId;
            openLaneCtx = true;
         }

         ArrangeRowDropTarget(/*targetIsGroup=*/false, laneId,
                              ImVec2(headerStartX, curY), ImVec2(rulerStartX, curY + rowH));

         if (gArrangeRowSel.count(laneId))
         {
            const ImU32 selFill = isLight ? IM_COL32(139, 92, 246, 35) : IM_COL32(139, 92, 246, 45);
            const ImU32 selBorder = IM_COL32(167, 139, 250, 180);
            dl->AddRectFilled(ImVec2(headerStartX + 1.0f, curY + 1.0f), ImVec2(rulerStartX - 1.0f, curY + rowH - 1.0f), selFill, 2.0f);
            dl->AddRect(ImVec2(headerStartX + 1.0f, curY + 1.0f), ImVec2(rulerStartX - 1.0f, curY + rowH - 1.0f), selBorder, 2.0f, 0, 1.5f);
         }

         // Beat/bar grid lines through the lane body
         for (const ArrangeGridLine& gl : arrangeGridLines)
         {
            const ImU32 gridCol = isLight
               ? IM_COL32(0, 0, 0, gl.isMajor ? 60 : 22)
               : IM_COL32(255, 255, 255, gl.isMajor ? 55 : 18);
            dl->AddLine(ImVec2(gl.x, curY), ImVec2(gl.x, curY + rowH), gridCol, 1.0f);
         }

         // Header separator vertical line
         dl->AddLine(ImVec2(rulerStartX, curY), ImVec2(rulerStartX, curY + rowH), tickCol, 1.0f);

         // ---- Header content ----
         const int laneDepth = (i < arrangeLaneDepth.size()) ? arrangeLaneDepth[i] : 0;
         for (int d = 0; d < laneDepth; d++)
         {
            const float lineX = headerStartX + 6.0f + (float)d * kGroupIndent;
            dl->AddLine(ImVec2(lineX, curY), ImVec2(lineX, curY + rowH), IM_COL32(255, 255, 255, 24), 1.0f);
         }

         // Old full sizes for mixer controls
         const float kMixCtl = 18.0f, kMixGap = 3.0f;
         const float kMixStripW = kMixCtl * 4.0f + kMixGap * 3.0f; // 81px
         const bool isVideoForName = lane.type == Arrange::kLaneVideo;

         const float contentStartX = headerStartX + 4.0f + (float)laneDepth * kGroupIndent;
         const float nameBoxW = std::max(35.0f, rulerStartX - contentStartX - kMixStripW - 6.0f);
         const ImVec2 nameBoxPos(contentStartX, curY + 4.0f);
         const ImVec2 nameBoxSize(nameBoxW, rowH - 8.0f);

         // Name box
         ImGui::SetCursorScreenPos(nameBoxPos);
         if (gArrangeRenamingLaneId == laneId)
         {
            ImGui::SetNextItemWidth(nameBoxW);
            char nameBuf[128];
            snprintf(nameBuf, sizeof(nameBuf), "%s", lane.name.c_str());
            if (gArrangeRenameJustStarted)
            {
               ImGui::SetKeyboardFocusHere();
               gArrangeRenameJustStarted = false;
            }
            const bool nameEdited = ImGui::InputText("##streamname", nameBuf, sizeof(nameBuf),
                                                     ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemActivated())
               ArrangeGestureBegin();
            if (nameEdited && lane.name != nameBuf)
            {
               if (!gArrangeGestureOpen)
                  ArrangeGestureBegin();
               lane.name = nameBuf;
               gArrange.revision++;
            }
            if (ImGui::IsItemDeactivated())
            {
               ArrangeGestureEnd();
               gArrangeRenamingLaneId = 0;
            }
         }
         else
         {
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            ImGui::Selectable(lane.name.c_str(), false, ImGuiSelectableFlags_None, nameBoxSize);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               ToggleOrOpenRowSettings(laneId);
            }
            else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
               HandleRowClick(laneId);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
               if (!gArrangeRowSel.count(laneId))
                  HandleRowClick(laneId);
               ctxLaneId = laneId;
               openLaneCtx = true;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
               ArrangeRowRef ref{ false, laneId };
               ImGui::SetDragDropPayload("ARRANGE_ROW_REF", &ref, sizeof(ref));
               ImGui::BeginTooltip();
               ImGui::Text(T("Move Track: %s"), lane.name.c_str());
               ImGui::EndTooltip();
               ImGui::EndDragDropSource();
            }
         }

         // Mix strip positioned at the right of the header column
         ImGui::SetCursorScreenPos(ImVec2(rulerStartX - kMixStripW - 4.0f, curY + (rowH - kMixCtl) * 0.5f));
         const bool isVideo = lane.type == Arrange::kLaneVideo;
         const bool laneSilenced = !isVideo && (lane.mute || (anyLaneSolo && !lane.solo));
         if (rowH >= kMixCtl + 4.0f)
         {
            const ImU32 mixFill = IM_COL32(16, 185, 129, 255);
            auto mixGesture = [&](bool changed, const std::function<void()>& apply)
            {
               if (ImGui::IsItemActivated())
               {
                  ArrangeGestureBegin();
                  gArrangeMixGestureLaneId = laneId;
               }
               if (changed)
               {
                  if (!gArrangeGestureOpen)
                     ArrangeGestureBegin();
                  apply();
                  gArrange.revision++;
               }
               if (ImGui::IsItemDeactivated() && gArrangeMixGestureLaneId == laneId)
               {
                  ArrangeGestureEnd();
                  gArrangeMixGestureLaneId = 0;
               }
            };
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            if (!isVideo)
            {
               bool solo = lane.solo;
               mixGesture(AudioSoloButton("S##lanesolo", &solo, kMixCtl, kMixCtl), [&] { lane.solo = solo; });
               ImGui::SameLine(0.0f, kMixGap);
               bool mute = lane.mute;
               mixGesture(AudioMuteButton("M##lanemute", &mute, kMixCtl, kMixCtl), [&] { lane.mute = mute; });
               ImGui::SameLine(0.0f, kMixGap);
               float pan = lane.pan;
               const bool panChanged = BipolarKnobFloat("##lanepan", &pan, -1.0f, 1.0f, "%.2f", kMixCtl, mixFill,
                                                        false, 0.0f, -1, -1, false, 0.0f, 0.0f, false,
                                                        /*resetOnDoubleClick=*/true);
               mixGesture(panChanged, [&] { lane.pan = pan; });
               if (ImGui::IsItemActive())
               {
                  if (std::fabs(lane.pan) < 0.005f)
                     ImGui::SetTooltip("%s", T("C"));
                  else
                     ImGui::SetTooltip("%s %d", lane.pan < 0.0f ? "L" : "R", (int)std::lround(std::fabs(lane.pan) * 100.0f));
               }
               ImGui::SameLine(0.0f, kMixGap);
               float gainDb = lane.gainDb;
               bool gainChanged = KnobFloat("##lanegain", &gainDb, -60.0f, 12.0f, "%.1f dB", kMixCtl, mixFill, false);
               if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && gainDb != 0.0f)
               {
                  gainDb = 0.0f;
                  gainChanged = true;
               }
               mixGesture(gainChanged, [&] { lane.gainDb = gainDb; });
               if (ImGui::IsItemActive())
                  ImGui::SetTooltip(T("%.1f dB"), lane.gainDb);
            }
            else
            {
               float pct = lane.opacity * 100.0f;
               ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, (kMixCtl - ImGui::GetFontSize()) * 0.5f));
               ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
               ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 6.0f);
               ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(139, 92, 246, 255));
               ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(160, 120, 250, 255));
               ImGui::SetNextItemWidth(kMixStripW);
               const bool opChanged = ImGui::SliderFloat("##laneopacity", &pct, 0.0f, 100.0f, "%.0f%%",
                                                         ImGuiSliderFlags_AlwaysClamp);
               ImGui::PopStyleColor(2);
               ImGui::PopStyleVar(3);
               mixGesture(opChanged, [&] { lane.opacity = std::clamp(pct / 100.0f, 0.0f, 1.0f); });
            }
            ImGui::PopStyleVar();
         }

         // Clips on this lane first, so clip buttons take priority over empty
         // lane clicks. Each clip is a copy: nothing in this loop reshapes
         // the model (drags were applied above, menu edits run after it).
         bool clipHoveredAny = false;
         for (size_t ci = 0; ci < lane.clips.size(); ci++)
         {
            const Arrange::Clip clip = lane.clips[ci];
            const float clipX0 = tickToX(clip.start);
            const float clipX1 = tickToX(clip.End());

            // Cull offscreen clips
            if (clipX1 < rulerStartX || clipX0 > rulerStartX + rulerWidth)
               continue;

            const float cLeft = std::max(rulerStartX, clipX0);
            const float cRight = std::min(rulerStartX + rulerWidth, clipX1);
            const float cTop = curY + 3.0f;
            const float cBottom = curY + rowH - 3.0f;
            const float cWidth = std::max(4.0f, cRight - cLeft);

            if (gArrangeMarquee.active)
            {
               if (gArrangeMarquee.allTracks)
               {
                  if (cRight >= marqueeX0 && cLeft <= marqueeX1)
                     marqueeHits.insert(clip.id);
               }
               else
               {
                  if ((int)i >= marqueeMinLane && (int)i <= marqueeMaxLane)
                  {
                     if (cRight >= marqueeX0 && cLeft <= marqueeX1)
                        marqueeHits.insert(clip.id);
                  }
               }
            }

            ImGui::PushID((int)(clip.id & 0x7fffffff));

            GraphNode* clipNode = nodeForUid(clip.srcUid);
            const bool offline = clipNode == nullptr;
            const std::string clipLabel = !clip.name.empty() ? clip.name
               : (clipNode != nullptr ? NodeTitle(*clipNode) : std::string("Unassigned"));

            ImGui::SetCursorScreenPos(ImVec2(cLeft, cTop));
            ImGui::InvisibleButton("##clipbtn", ImVec2(cWidth, cBottom - cTop));
            const bool clipActive = ImGui::IsItemActive();
            const bool clipHovered = ImGui::IsItemHovered();
            const bool clipActivated = ImGui::IsItemActivated();
            if (clipHovered) clipHoveredAny = true;

            const bool isSelected = gArrangeSel.count(clip.id) != 0;
            const ImGuiIO& cio = ImGui::GetIO();
            const ImVec2 mPos = cio.MousePos;

            // Hit zones: a trim handle is at most a quarter of the clip on
            // each side (2..6 px), so the middle half of any clip always moves.
            const float handleW = std::clamp((clipX1 - clipX0) * 0.25f, 2.0f, 6.0f);
            const bool onLeftEdge = clipHovered && clipX0 >= rulerStartX && (mPos.x - clipX0 <= handleW);
            const bool onRightEdge = clipHovered && !onLeftEdge && clipX1 <= rulerStartX + rulerWidth &&
                                     (clipX1 - mPos.x <= handleW);
            const int edgeHit = onLeftEdge ? Arrange::kEdgeStart : (onRightEdge ? Arrange::kEdgeEnd : -1);

            const Arrange::Tick bladeTick = gridSnap(xToTick(mPos.x));
            const bool bladeCuts = (gArrangeTool == ArrangeTool::Blade || gArrangeBladeOn) &&
                                   (bladeTick > clip.start && bladeTick < clip.End());

            // Hand Tool: drag canvas to pan & scroll
            if (gArrangeTool == ArrangeTool::Hand)
            {
               if (clipHovered)
                  ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
               if (clipActivated)
                  gArrangeHandDragging = true;
            }
            // Zoom Tool: click in, Alt-click / right-click out, drag scrub
            else if (gArrangeTool == ArrangeTool::Zoom)
            {
               if (clipActivated)
               {
                  gArrangeZoomDragging = true;
                  gArrangeZoomDragStart = mPos;
                  gArrangeZoomDragStartPpb = gArrangePixelsPerBeat;
                  gArrangeZoomDragStartBeats = gArrangeScrollBeats;
               }
               else if (clipHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
               {
                  zoomAroundMouse(0.75f);
               }
            }
            // Range Selection Tool: marquee select bounded to track(s)
            else if (gArrangeTool == ArrangeTool::Range)
            {
               if (clipActivated && !gArrangeMarquee.active)
               {
                  gArrangeMarquee.active = true;
                  gArrangeMarquee.allTracks = false;
                  gArrangeMarquee.startLane = (int)i;
                  gArrangeMarquee.anchor = mPos;
                  gArrangeMarquee.current = mPos;
                  gArrangeMarquee.baseSel = cio.KeyShift ? gArrangeSel : std::set<uint64_t>();
                  marqueeHits.insert(clip.id);
               }
            }
            // Trim Tool: dedicated slip/trim on clip edges
            else if (gArrangeTool == ArrangeTool::Trim)
            {
               if (clipHovered)
                  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
               if (clipActivated && gArrangeDrag.mode == kArrangeDragNone)
               {
                  const float midX = (clipX0 + clipX1) * 0.5f;
                  const bool isLeft = (mPos.x < midX);
                  const int edge = isLeft ? Arrange::kEdgeStart : Arrange::kEdgeEnd;
                  const int mode = isLeft ? kArrangeDragTrimStart : kArrangeDragTrimEnd;
                  ArrangeClickSelect(clip.id, false, cio.KeyAlt);
                  ArrangeDragBegin(mode, clip.id, edge, xToTick(mPos.x));
               }
               if (clipHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
               {
                  if (!isSelected)
                     ArrangeClickSelect(clip.id, false, cio.KeyAlt);
                  gArrangeCtxClipId = clip.id;
                  openClipCtx = true;
               }
            }
            // Blade Tool (B): slice clip at mouse/grid
            else if (gArrangeTool == ArrangeTool::Blade || gArrangeBladeOn)
            {
               if (clipActivated && bladeCuts)
               {
                  ArrangeBladeSplitAt(clip.id, bladeTick);
               }
            }
            // Pencil / Draw Tool (P): select/inspect existing clip or right click menu
            else if (gArrangeTool == ArrangeTool::Pencil)
            {
               if (clipHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
               {
                  if (!isSelected)
                     ArrangeClickSelect(clip.id, false, cio.KeyAlt);
                  gArrangeCtxClipId = clip.id;
                  openClipCtx = true;
               }
               else if (clipActivated && gArrangeDrag.mode == kArrangeDragNone)
               {
                  ArrangeClickSelect(clip.id, cio.KeyShift, cio.KeyAlt);
               }
            }
            // Select Tool (A): Standard DAW clip interaction
            else
            {
               // A grouped clip's edge that is also the group's edge drives the
               // group (trim the members on that edge; Shift scales). Alt works
               // on the one clip.
               bool groupEdge = false;
               if (edgeHit >= 0 && clip.groupId != 0 && !cio.KeyAlt)
               {
                  auto gs = arrangeGroupSpans.find(clip.groupId);
                  if (gs != arrangeGroupSpans.end())
                     groupEdge = edgeHit == Arrange::kEdgeStart ? clip.start == gs->second.start : clip.End() == gs->second.end;
               }

               if (edgeHit >= 0)
                  ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

               // Click: Alt = this member only; Cmd/Ctrl or Shift = toggle into
               // the selection (no drag) - except Shift on a group edge, which is
               // the proportional scale. A plain click keeps an existing
               // selection that contains the clip, so the drag moves all of it.
               if (clipActivated && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                   gArrangeDrag.mode == kArrangeDragNone && !cio.KeyShift && !cio.KeySuper && !cio.KeyCtrl)
               {
                  if (gArrangeClipSettingsPanelOpen && gArrangeSettingsPanelTarget == clip.id)
                  {
                     gArrangeClipSettingsPanelOpen = false;
                  }
                  else
                  {
                     gArrangeClipSettingsPanelOpen = true;
                     gArrangeSettingsPanelTarget = clip.id;
                     gArrangeRowSel.clear();
                     gArrangeRowSelAnchor = 0;
                  }
               }
               else if (clipActivated && gArrangeDrag.mode == kArrangeDragNone)
               {
                  const Arrange::Tick grabTick = xToTick(mPos.x);
                  const bool alt = cio.KeyAlt;
                  if (cio.KeyShift && groupEdge)
                  {
                     ArrangeClickSelect(clip.id, false, false);
                     ArrangeDragBegin(kArrangeDragGroupScale, clip.id, edgeHit, grabTick);
                  }
                  else if (cio.KeySuper || cio.KeyCtrl || cio.KeyShift)
                  {
                     ArrangeClickSelect(clip.id, true, alt);
                  }
                  else
                  {
                     const bool wasSelected = isSelected && !alt;
                     ArrangeClickSelect(clip.id, false, alt);
                     int mode = kArrangeDragMove;
                     if (groupEdge)
                        mode = kArrangeDragGroupEdge;
                     else if (edgeHit == Arrange::kEdgeStart)
                        mode = kArrangeDragTrimStart;
                     else if (edgeHit == Arrange::kEdgeEnd)
                        mode = kArrangeDragTrimEnd;
                     ArrangeDragBegin(mode, clip.id, edgeHit >= 0 ? edgeHit : Arrange::kEdgeStart, grabTick);
                     gArrangeDrag.collapseOnClick = wasSelected;
                     gArrangeDrag.singleMember = alt;
                  }
               }

               // Right-click: the menu acts on the selection, so a clip outside
               // it becomes the selection first.
               if (clipHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
               {
                  if (!isSelected)
                     ArrangeClickSelect(clip.id, false, cio.KeyAlt);
                  gArrangeCtxClipId = clip.id;
                  openClipCtx = true;
               }

               // Middle-click scrub on a Sample
               if (clip.sampleDropped && clipHovered &&
                   ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !gArrangeScrubbing)
               {
                  ArrangeScrubBegin(gridSnap(xToTick(mPos.x)), ImGuiMouseButton_Middle);
               }
            }

            // Styling. A Color Tint overrides the type palette; a disabled or
            // offline clip is drawn desaturated under a diagonal hatch - the
            // "this will not play" mark.
            const bool hasTint = clip.colorR > 0.001f || clip.colorG > 0.001f || clip.colorB > 0.001f;
            // A Color Tint is drawn at full opacity in both states so the clip always
            // shows exactly the palette swatch the user picked, not a blended-down shade.
            ImU32 clipBaseCol = hasTint
               ? IM_COL32((int)std::lround(clip.colorR * 255.0f), (int)std::lround(clip.colorG * 255.0f), (int)std::lround(clip.colorB * 255.0f), 255)
               : (isVideo ? IM_COL32(109, 40, 217, 210) : IM_COL32(5, 150, 105, 210));
            ImU32 clipActiveCol = hasTint
               ? IM_COL32((int)std::lround(clip.colorR * 255.0f), (int)std::lround(clip.colorG * 255.0f), (int)std::lround(clip.colorB * 255.0f), 255)
               : (isVideo ? IM_COL32(139, 92, 246, 255) : IM_COL32(16, 185, 129, 255));
            const bool muted = !clip.enabled || offline || laneSilenced;
            if (muted)
            {
               clipBaseCol = isLight ? IM_COL32(176, 178, 186, 220) : IM_COL32(72, 72, 80, 220);
               clipActiveCol = isLight ? IM_COL32(160, 162, 170, 255) : IM_COL32(88, 88, 96, 255);
            }
            // A grouped clip is edged in its group's colour (brighter when
            // the group is selected - the group frame drawn after the lanes
            // carries the selection); a loose selected clip stays gold.
            const bool grouped = clip.groupId != 0;
            const ImU32 clipBorderCol = grouped
               ? ArrangeGroupColor(clip.groupId, isSelected ? 255 : (clipHovered ? 235 : 190))
               : isSelected
               ? IM_COL32(250, 204, 21, 255) // gold for selected
               : (clipActive ? IM_COL32(255, 255, 255, 240) : (clipHovered ? IM_COL32(230, 230, 240, 220) : IM_COL32(20, 20, 24, 180)));

            dl->AddRectFilled(ImVec2(cLeft, cTop), ImVec2(cRight, cBottom),
                              clipActive ? clipActiveCol : clipBaseCol, 4.0f);
            if (muted)
               DrawArrangeHatch(dl, ImVec2(cLeft, cTop), ImVec2(cRight, cBottom),
                                isLight ? IM_COL32(0, 0, 0, 45) : IM_COL32(255, 255, 255, 38));
            if (grouped)
               dl->AddRectFilled(ImVec2(cLeft, cTop), ImVec2(cRight, cTop + 4.0f), ArrangeGroupColor(clip.groupId),
                                 4.0f, ImDrawFlags_RoundCornersTop);

            // Live waveform (WP8). Position-indexed, so a column shows what
            // actually came out of the clip's node at that point in the clip
            // - and a stretch that has never played stays on the centre
            // line rather than guessing. Nothing is drawn during a take: the
            // panel is locked anyway and the take owns the frame budget.
            if (!isVideo && cWidth > 6.0f && !ArrangeRenderBusy() && !clip.importPending)
            {
               const float midY = (cTop + cBottom) * 0.5f;
               const float halfH = std::max(2.0f, (cBottom - cTop) * 0.5f - 5.0f);
               const ImU32 waveCol = muted ? IM_COL32(255, 255, 255, 60) : IM_COL32(255, 255, 255, 115);
               dl->PushClipRect(ImVec2(cLeft + 1.0f, cTop + 1.0f), ImVec2(cRight - 1.0f, cBottom - 1.0f), true);
               dl->AddLine(ImVec2(cLeft + 1.0f, midY), ImVec2(cRight - 1.0f, midY),
                           IM_COL32(255, 255, 255, 45), 1.0f);
               // Audio Sample: a static peak array computed once at import
               // from the fully-decoded source file - see
               // ArrangePollMediaImports. Audio Clip: unchanged, the
               // live-fill cache the audio thread writes as it plays.
               const ArrangeClipWave* waveSrc = nullptr;
               if (clip.sampleDropped)
               {
                  auto it = gArrangeSampleStaticWaves.find(clip.id);
                  if (it != gArrangeSampleStaticWaves.end())
                     waveSrc = &it->second;
               }
               else
               {
                  auto it = gArrangeClipWaves.find(clip.id);
                  if (it != gArrangeClipWaves.end())
                     waveSrc = &it->second;
               }
               if (waveSrc != nullptr && !waveSrc->minv.empty() && clip.length > 0)
               {
                  const ArrangeClipWave& wv = *waveSrc;
                  const int nb = (int)wv.minv.size();
                  // x -> tick -> bucket, inverting the same tickToX the clip
                  // rect came from, so the waveform cannot drift from it at
                  // any zoom. One column may span many buckets when zoomed
                  // out; take the envelope over all of them.
                  const double ticksPerPx =
                     (double)clip.length / std::max(1.0, (double)(clipX1 - clipX0));
                  for (float x = cLeft; x < cRight; x += 1.0f)
                  {
                     const double tA = ((double)x - (double)clipX0) * ticksPerPx;
                     const double tB = tA + ticksPerPx;
                     int b0 = (int)std::floor(tA / (double)kArrangeWaveBucketTicks);
                     int b1 = (int)std::floor((tB - 1.0) / (double)kArrangeWaveBucketTicks);
                     b0 = std::clamp(b0, 0, nb - 1);
                     b1 = std::clamp(std::max(b1, b0), 0, nb - 1);
                     float lo = 0.0f, hi = 0.0f;
                     bool any = false;
                     for (int b = b0; b <= b1; b++)
                     {
                        if (wv.filled[(size_t)b] == 0)
                           continue;
                        lo = std::min(lo, wv.minv[(size_t)b]);
                        hi = std::max(hi, wv.maxv[(size_t)b]);
                        any = true;
                     }
                     if (!any)
                        continue;
                     const float yTop = midY - std::clamp(hi, -1.0f, 1.0f) * halfH;
                     const float yBot = midY - std::clamp(lo, -1.0f, 1.0f) * halfH;
                     dl->AddLine(ImVec2(x + 0.5f, yTop), ImVec2(x + 0.5f, std::max(yBot, yTop + 1.0f)),
                                 waveCol, 1.0f);
                  }
               }
               dl->PopClipRect();
            }

            // Thumbnail at the clip's left edge (WP8), only when the clip is
            // wide enough that it does not crowd out the label. Drawn from
            // the pooled FBO the composite filled; a clip that has never
            // been under the playhead has none yet and just shows its
            // colour.
            float thumbRight = cLeft;
            if (isVideo && cWidth > (float)kArrangeThumbW + 16.0f && !ArrangeRenderBusy() && !clip.importPending)
            {
               auto thumbIt = gArrangeClipThumbs.find(clip.id);
               if (thumbIt != gArrangeClipThumbs.end() && thumbIt->second.fbo.tex != 0 &&
                   thumbIt->second.lastCapture >= 0.0)
               {
                  const float avail = (cBottom - cTop) - 8.0f;
                  const float th = std::min((float)kArrangeThumbH, avail);
                  const float tw = th * ((float)kArrangeThumbW / (float)kArrangeThumbH);
                  const ImVec2 tl(cLeft + 4.0f, (cTop + cBottom) * 0.5f - th * 0.5f);
                  const ImVec2 br(tl.x + tw, tl.y + th);
                  dl->PushClipRect(ImVec2(cLeft + 1.0f, cTop + 1.0f), ImVec2(cRight - 1.0f, cBottom - 1.0f), true);
                  // Flipped V: an FBO's texture is bottom-up, the same
                  // convention every other AddImage of a node texture uses.
                  dl->AddImage((ImTextureID)(intptr_t)thumbIt->second.fbo.tex, tl, br,
                               ImVec2(0, 1), ImVec2(1, 0));
                  dl->AddRect(tl, br, IM_COL32(0, 0, 0, 140), 2.0f);
                  dl->PopClipRect();
                  thumbRight = br.x;
               }
            }

            // Fade-in / Fade-out visual overlay
            if (clip.fadeIn > 0 || clip.fadeOut > 0)
            {
               dl->PushClipRect(ImVec2(cLeft, cTop), ImVec2(cRight, cBottom), true);
               if (clip.fadeIn > 0)
               {
                  const float fInX = tickToX(clip.start + clip.fadeIn);
                  dl->AddTriangleFilled(ImVec2(clipX0, cTop), ImVec2(clipX0, cBottom), ImVec2(fInX, cTop),
                                        IM_COL32(0, 0, 0, 50));
                  dl->AddLine(ImVec2(clipX0, cBottom), ImVec2(fInX, cTop), IM_COL32(255, 255, 255, 150), 1.5f);
               }
               if (clip.fadeOut > 0)
               {
                  const float fOutX = tickToX(clip.End() - clip.fadeOut);
                  dl->AddTriangleFilled(ImVec2(fOutX, cTop), ImVec2(clipX1, cBottom), ImVec2(clipX1, cTop),
                                        IM_COL32(0, 0, 0, 50));
                  dl->AddLine(ImVec2(fOutX, cTop), ImVec2(clipX1, cBottom), IM_COL32(255, 255, 255, 150), 1.5f);
               }
               dl->PopClipRect();
            }

            dl->AddRect(ImVec2(cLeft, cTop), ImVec2(cRight, cBottom), clipBorderCol, 4.0f, 0,
                        isSelected ? 2.5f : (grouped ? 1.8f : 1.2f));

            // Loading state: the clip's real decode (ArrangeMediaImport.h)
            // hasn't landed yet - shown instead of a waveform/thumbnail,
            // which importPending already suppresses above.
            if (clip.importPending)
            {
               const char* loadingLabel = "Loading...";
               const ImVec2 ts = ImGui::CalcTextSize(loadingLabel);
               if (cWidth > ts.x + 8.0f)
               {
                  const float midY = (cTop + cBottom) * 0.5f;
                  dl->AddText(ImVec2(cLeft + (cWidth - ts.x) * 0.5f, midY - ts.y * 0.5f),
                              IM_COL32(255, 255, 255, 200), loadingLabel);
               }
            }

            // Just added from the canvas: a white outline that fades over a
            // second, so the new clip is found at a glance.
            if (gArrangeFlashClipId == clip.id)
            {
               const float t = (float)(ImGui::GetTime() - gArrangeFlashStart);
               if (t >= 1.2f)
                  gArrangeFlashClipId = 0;
               else
               {
                  const int a = (int)(255.0f * (1.0f - t / 1.2f));
                  dl->AddRect(ImVec2(cLeft - 2.0f, cTop - 2.0f), ImVec2(cRight + 2.0f, cBottom + 2.0f),
                              IM_COL32(255, 255, 255, a), 5.0f, 0, 2.5f);
               }
            }

            // Blade preview: the cut line where a click would split.
            if (clipHovered && bladeCuts)
            {
               const float bx = tickToX(bladeTick);
               dl->AddLine(ImVec2(bx, cTop), ImVec2(bx, cBottom), IM_COL32(255, 255, 255, 235), 1.5f);
            }

            // Trim handle marks
            if (cWidth > 20.0f)
            {
               const ImU32 handleCol = onLeftEdge ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 255, 255, 80);
               const ImU32 handleRCol = onRightEdge ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 255, 255, 80);
               dl->AddLine(ImVec2(cLeft + 3.0f, cTop + 6.0f), ImVec2(cLeft + 3.0f, cBottom - 6.0f), handleCol, 2.0f);
               dl->AddLine(ImVec2(cRight - 3.0f, cTop + 6.0f), ImVec2(cRight - 3.0f, cBottom - 6.0f), handleRCol, 2.0f);
            }

            // Label, or the inline rename field. A rename is one undo entry,
            // committed on Enter or focus loss if the name changed; Escape
            // abandons it.
            if (gArrangeRenamingClipId == clip.id)
            {
               static uint64_t sArrangeRenameFocusedId = 0;
               ImGui::SetCursorScreenPos(ImVec2(cLeft + 4.0f, cTop + 2.0f));
               ImGui::SetNextItemWidth(std::max(20.0f, cWidth - 8.0f));
               if (sArrangeRenameFocusedId != clip.id)
               {
                  ImGui::SetKeyboardFocusHere();
                  sArrangeRenameFocusedId = clip.id;
               }
               const bool commit = ImGui::InputText("##renamingclipfield", gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
               if (commit || ImGui::IsItemDeactivated())
               {
                  const std::string newName = gArrangeRenameClipBuffer;
                  std::vector<uint64_t> renameIds = gArrangeRenameTargetIds;
                  renameIds.push_back(clip.id);
                  if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                  {
                     ArrangeEdit([&]()
                     {
                        for (uint64_t renameId : renameIds)
                        {
                           if (Arrange::Clip* c = Arrange::FindClip(gArrange, renameId))
                           {
                              if (c->name == newName)
                                 continue;
                              c->name = newName;
                              gArrange.revision++;
                           }
                        }
                     });
                  }
                  gArrangeRenamingClipId = 0;
                  gArrangeRenameTargetIds.clear();
                  sArrangeRenameFocusedId = 0;
               }
            }
            else
            {
               const std::string fullLabel = clipLabel + " [" + ArrangeFormatLength(clip.length) + "]";
               const ImU32 labelCol = muted ? (isLight ? IM_COL32(60, 60, 70, 255) : IM_COL32(190, 190, 200, 255))
                                            : IM_COL32(255, 255, 255, 255);
               // Starts after the thumbnail when there is one, so the two
               // never overlap.
               const float labelX = (thumbRight > cLeft ? thumbRight + 6.0f : cLeft + 8.0f);
               dl->PushClipRect(ImVec2(cLeft + 2.0f, cTop), ImVec2(cRight - 2.0f, cBottom), true);
               dl->AddText(ImVec2(labelX, cTop + 8.0f), labelCol, fullLabel.c_str());
               dl->PopClipRect();
            }

            ImGui::PopID();
         }

         // Empty lane body: behavior depends on active tool
         const bool mouseInLane = mouse.x >= rulerStartX && mouse.x < rulerStartX + rulerWidth &&
                                  mouse.y >= curY && mouse.y < curY + rowH;
         if (mouseInLane && !clipHoveredAny && ImGui::IsWindowHovered())
         {
            const ImGuiIO& lio = ImGui::GetIO();
            if (gArrangeTool == ArrangeTool::Hand)
            {
               ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                  gArrangeHandDragging = true;
            }
            else if (gArrangeTool == ArrangeTool::Zoom)
            {
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
               {
                  gArrangeZoomDragging = true;
                  gArrangeZoomDragStart = mouse;
                  gArrangeZoomDragStartPpb = gArrangePixelsPerBeat;
                  gArrangeZoomDragStartBeats = gArrangeScrollBeats;
               }
               else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
               {
                  zoomAroundMouse(0.75f);
               }
            }
            else if (gArrangeTool == ArrangeTool::Pencil)
            {
               const Arrange::Tick ghostTick = gridSnap(xToTick(mouse.x));
               const float gx0 = std::max(rulerStartX, tickToX(ghostTick));
               const float gx1 = std::min(rulerStartX + rulerWidth, tickToX(ghostTick + Arrange::kTicksPerBar));
               if (gx1 > gx0)
               {
                  dl->AddRectFilled(ImVec2(gx0, curY + 2.0f), ImVec2(gx1, curY + rowH - 2.0f),
                                    IM_COL32(16, 185, 129, 50), 3.0f);
                  dl->AddRect(ImVec2(gx0, curY + 2.0f), ImVec2(gx1, curY + rowH - 2.0f),
                              IM_COL32(16, 185, 129, 200), 3.0f, 0, 1.5f);
               }
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gArrangeDrag.mode == kArrangeDragNone)
               {
                  AddUnassignedClipAt(laneId, ghostTick);
               }
               if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
               {
                  addClipAtTick = ghostTick;
                  addClipToLaneId = laneId;
                  openAddClip = true;
               }
            }
            else if (gArrangeTool == ArrangeTool::Range)
            {
               if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gArrangeMarquee.active && gArrangeDrag.mode == kArrangeDragNone)
               {
                  gArrangeMarquee.active = true;
                  gArrangeMarquee.allTracks = false;
                  gArrangeMarquee.startLane = (int)i;
                  gArrangeMarquee.anchor = mouse;
                  gArrangeMarquee.current = mouse;
                  gArrangeMarquee.baseSel = lio.KeyShift ? gArrangeSel : std::set<uint64_t>();
               }
            }
            else
            {
               if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && gArrangeDrag.mode == kArrangeDragNone)
               {
                  AddUnassignedClipAt(laneId, gridSnap(xToTick(mouse.x)));
               }
               else if (lio.KeyShift && !lio.KeySuper && !lio.KeyCtrl && !gArrangeMarquee.active &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gArrangeDrag.mode == kArrangeDragNone)
               {
                  gArrangeMarquee.active = true;
                  gArrangeMarquee.allTracks = false;
                  gArrangeMarquee.startLane = (int)i;
                  gArrangeMarquee.anchor = mouse;
                  gArrangeMarquee.current = mouse;
                  gArrangeMarquee.baseSel = gArrangeSel;
               }
               else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gArrangeDrag.mode == kArrangeDragNone &&
                   !gArrangeBladeOn && !lio.KeyShift && !lio.KeySuper && !lio.KeyCtrl)
               {
                  gArrangeSel.clear();
                  gArrangeSelAnchor = 0;
                  gArrangeRowSel.clear();
                  gArrangeRowSelAnchor = 0;
               }
               if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
               {
                  addClipAtTick = gridSnap(xToTick(mouse.x));
                  addClipToLaneId = laneId;
                  openAddClip = true;
               }
            }
         }

         // OS-level Finder drop landing on this lane row. gDropPos is a
         // plain screen-space point captured once at drop time
         // (OnFilesDropped), not live mouse state like `mouse` above, so
         // this is hit-tested against the row independently of hover. This
         // runs before the node-editor canvas's own gDroppedFiles handling
         // further down main.cpp, and consumes (clears) any path it
         // recognizes so that later handling never sees it - the two
         // dispatches only ever pull from one shared list.
         if (!gDroppedFiles.empty() && gDropPos.x >= rulerStartX && gDropPos.x < rulerStartX + rulerWidth &&
             gDropPos.y >= curY && gDropPos.y < curY + rowH)
         {
            // Multiple files dropped in one gesture all share the same
            // gDropPos (OnFilesDropped records one point per drop event,
            // not per path) - each successive one is placed one placeholder
            // length further along so they land as separate back-to-back
            // clips instead of fully overwriting each other in PlaceOverwrite.
            Arrange::Tick cursorTick = gridSnap(xToTick(gDropPos.x));
            std::vector<std::string> remaining;
            for (const std::string& p : gDroppedFiles)
            {
               Arrange::ImportMediaKind kindProbe;
               if (ArrangeMediaKindForPath(p, kindProbe))
               {
                  ArrangeImportMediaFile(p, laneId, cursorTick, kindProbe);
                  cursorTick += Arrange::kTicksPerBar;
               }
               else
               {
                  remaining.push_back(p);
               }
            }
            gDroppedFiles = std::move(remaining);
         }

         // A browser/search-panel sample or media drag released over this
         // lane row - see gArrangePendingBrowserDrop's own comment for why
         // this is resolved here instead of at the drag's mouse-release site.
         if (gArrangePendingBrowserDrop.pending &&
             gArrangePendingBrowserDrop.screenPos.x >= rulerStartX &&
             gArrangePendingBrowserDrop.screenPos.x < rulerStartX + rulerWidth &&
             gArrangePendingBrowserDrop.screenPos.y >= curY && gArrangePendingBrowserDrop.screenPos.y < curY + rowH)
         {
            Arrange::ImportMediaKind kindProbe;
            if (ArrangeMediaKindForPath(gArrangePendingBrowserDrop.path, kindProbe))
            {
               ArrangeImportMediaFile(gArrangePendingBrowserDrop.path, laneId,
                                      gridSnap(xToTick(gArrangePendingBrowserDrop.screenPos.x)),
                                      kindProbe);
            }
            gArrangePendingBrowserDrop.pending = false;
         }

         // Row-height resize grip: a thin strip straddling the row's bottom
         // border, header column only (the ruler/lane body below stays free
         // for normal clip interaction). Mirrors the panel-dock grip's own
         // drag-a-plain-float pattern, just wrapped in a gesture since
         // Lane::rowHeight is real model state (persisted, undoable).
         {
            const float kGripH = 5.0f;
            ImGui::SetCursorScreenPos(ImVec2(headerStartX, curY + rowH - kGripH * 0.5f));
            ImGui::InvisibleButton("##laneheightgrip", ImVec2(rulerStartX - headerStartX, kGripH));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
               ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActivated())
               ArrangeGestureBegin();
            if (ImGui::IsItemActive())
            {
               const float newH = std::clamp(rowH + ImGui::GetIO().MouseDelta.y, kMinLaneHeight, kMaxLaneHeight);
               if (newH != lane.rowHeight)
               {
                  lane.rowHeight = newH;
                  gArrange.revision++;
               }
            }
            if (ImGui::IsItemDeactivated())
               ArrangeGestureEnd();
         }

         ImGui::PopID();
      }

      // Didn't land inside any lane row (header, ruler, gap between lanes,
      // etc.) - drop it rather than let it apply to a future, unrelated drop.
      gArrangePendingBrowserDrop.pending = false;

      // Commit the marquee-select union onto the real selection - every
      // frame while dragging (so the highlight tracks live), and one last
      // time on release before clearing `active`.
      if (gArrangeMarquee.active)
      {
         gArrangeSel = gArrangeMarquee.baseSel;
         gArrangeSel.insert(marqueeHits.begin(), marqueeHits.end());
         gArrangeSelAnchor = 0;
         if (marqueeFinalizeNow)
            gArrangeMarquee.active = false;
         else
         {
            const float rMinX = marqueeX0;
            const float rMaxX = marqueeX1;
            float rMinY = 0.0f;
            float rMaxY = 0.0f;
            if (gArrangeMarquee.allTracks || laneRowTop.empty())
            {
               rMinY = lanesTopY;
               rMaxY = lanesContentBottom;
            }
            else
            {
               rMinY = laneRowTop[marqueeMinLane];
               rMaxY = laneRowTop[marqueeMaxLane] + laneRowH[marqueeMaxLane];
            }

            if (rMaxX > rMinX && rMaxY > rMinY)
            {
               const ImVec2 r0(rMinX, rMinY);
               const ImVec2 r1(rMaxX, rMaxY);
               dl->AddRectFilled(r0, r1, IM_COL32(59, 130, 246, 22));
               dl->AddRect(r0, r1, IM_COL32(96, 165, 250, 110), 0.0f, 0, 1.0f);
               // Subtle vertical boundary lines at x0 and x1
               dl->AddLine(ImVec2(r0.x, r0.y), ImVec2(r0.x, r1.y), IM_COL32(255, 255, 255, 120), 1.0f);
               dl->AddLine(ImVec2(r1.x, r0.y), ImVec2(r1.x, r1.y), IM_COL32(255, 255, 255, 120), 1.0f);
            }
         }
      }

      // Overwrite preview: the parts of stationary clips the moving block is
      // about to trim, in red over the target lane - read off the gesture
      // snapshot, since the live model has already trimmed them.
      if (gArrangeDrag.mode == kArrangeDragMove && gArrangeDrag.live && gArrangeGestureOpen)
      {
         const std::vector<uint64_t>& movers = gArrangeDrag.ids;
         for (uint64_t id : movers)
         {
            const Arrange::Loc loc = Arrange::Find(gArrange, id);
            if (!loc.Valid() || loc.lane >= (int)gArrangeGestureBefore.lanes.size())
               continue;
            const Arrange::Clip& mc = gArrange.lanes[loc.lane].clips[loc.index];
            const float y = laneRowTop[loc.lane];
            const float rowH = laneRowH[loc.lane];
            for (const Arrange::Clip& sc : gArrangeGestureBefore.lanes[loc.lane].clips)
            {
               if (std::find(movers.begin(), movers.end(), sc.id) != movers.end())
                  continue;
               const Arrange::Tick a = std::max(sc.start, mc.start);
               const Arrange::Tick b = std::min(sc.End(), mc.End());
               if (a >= b)
                  continue;
               const float x0 = std::max(rulerStartX, tickToX(a));
               const float x1 = std::min(rulerStartX + rulerWidth, tickToX(b));
               if (x1 <= x0)
                  continue;
               dl->AddRectFilled(ImVec2(x0, y + 3.0f), ImVec2(x1, y + rowH - 3.0f), IM_COL32(239, 68, 68, 105), 3.0f);
               dl->AddRect(ImVec2(x0, y + 3.0f), ImVec2(x1, y + rowH - 3.0f), IM_COL32(239, 68, 68, 235), 3.0f, 0, 1.5f);
            }
         }
      }

      // A selected group is framed as one object: a soft wash in its colour
      // plus a 2px frame just outside its clips' bounds.
      {
         std::set<uint64_t> selectedGroups;
         for (uint64_t id : gArrangeSel)
            if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
               if (c->groupId != 0)
                  selectedGroups.insert(c->groupId);
         for (uint64_t gid : selectedGroups)
         {
            auto it = arrangeGroupSpans.find(gid);
            if (it == arrangeGroupSpans.end())
               continue;
            const float x0 = std::max(rulerStartX, tickToX(it->second.start)) - 1.0f;
            const float x1 = std::min(rulerStartX + rulerWidth, tickToX(it->second.end)) + 1.0f;
            const int laneMin = std::clamp(it->second.laneMin, 0, (int)laneRowTop.size() - 1);
            const int laneMax = std::clamp(it->second.laneMax, 0, (int)laneRowTop.size() - 1);
            const float y0 = laneRowTop[laneMin] + 1.0f;
            const float y1 = laneRowTop[laneMax] + laneRowH[laneMax] - 1.0f;
            // Alpha and thickness toned down (was a filled 22/245 double
            // outline) - the highlight was reading louder than the
            // selection itself needed.
            if (x1 > x0 && y1 > y0)
               dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), ArrangeGroupColor(gid, 130), 5.0f, 0, 1.0f);
         }
      }

      // Tool cursors over the lanes
      if (ImGui::IsWindowHovered() && mouse.x >= rulerStartX &&
          mouse.x < rulerStartX + rulerWidth && mouse.y >= lanesTopY &&
          mouse.y < lanesContentBottom)
      {
         if (gArrangeBladeOn || gArrangeTool == ArrangeTool::Blade)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            Tabler::DrawScissors(fg, ImVec2(mouse.x + 1.0f, mouse.y + 1.0f), 20.0f, IM_COL32(0, 0, 0, 200), 3.2f);
            Tabler::DrawScissors(fg, mouse, 20.0f, IM_COL32(255, 255, 255, 255), 1.8f);
         }
         else if (gArrangeTool == ArrangeTool::Pencil)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            Tabler::DrawPencil(fg, ImVec2(mouse.x + 1.0f, mouse.y + 1.0f), 20.0f, IM_COL32(0, 0, 0, 200), 3.2f);
            Tabler::DrawPencil(fg, mouse, 20.0f, IM_COL32(255, 255, 255, 255), 1.8f);
         }
         else if (gArrangeTool == ArrangeTool::Zoom)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const bool zoomOut = ImGui::GetIO().KeyAlt || ImGui::IsMouseDown(ImGuiMouseButton_Right);
            Tabler::DrawZoom(fg, ImVec2(mouse.x + 1.0f, mouse.y + 1.0f), 20.0f, IM_COL32(0, 0, 0, 200), 3.2f);
            Tabler::DrawZoom(fg, mouse, 20.0f, IM_COL32(255, 255, 255, 255), 1.8f);
            // Draw + or - indicator inside the magnifying glass center
            const ImVec2 zc(mouse.x - 2.0f, mouse.y - 2.0f);
            fg->AddLine(ImVec2(zc.x - 2.5f, zc.y), ImVec2(zc.x + 2.5f, zc.y), IM_COL32(255, 255, 255, 255), 1.6f);
            if (!zoomOut)
               fg->AddLine(ImVec2(zc.x, zc.y - 2.5f), ImVec2(zc.x, zc.y + 2.5f), IM_COL32(255, 255, 255, 255), 1.6f);
         }
         else if (gArrangeTool == ArrangeTool::Hand)
         {
            ImGui::SetMouseCursor(gArrangeHandDragging ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
         }
         else if (gArrangeTool == ArrangeTool::Range)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
         }
         else if (gArrangeTool == ArrangeTool::Trim)
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
         }
      }
      dl->PopClipRect();

      // ---- clip context menu (acts on the selection; ids only) ----
      if (openClipCtx)
         ImGui::OpenPopup("##arrangeclipctx");
      if (ImGui::BeginPopup("##arrangeclipctx"))
      {
         Arrange::Clip* cp = Arrange::FindClip(gArrange, gArrangeCtxClipId);
         if (cp == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            const uint64_t cid = cp->id;
            const Arrange::Loc cloc = Arrange::Find(gArrange, cid);
            const int ctxLaneType = gArrange.lanes[cloc.lane].type;
            GraphNode* ctxNode = nodeForUid(cp->srcUid);
            // Fade In/Out and Compositing are per-clip-type (audio vs video)
            // settings - meaningless once the selection spans both, which
            // happens as soon as a mixed audio+video group (one video clip,
            // one audio clip) gets selected as a unit. Only offer them when
            // every selected clip shares the right-clicked clip's type.
            bool ctxSelectionSingleType = true;
            for (uint64_t selId : ArrangeSelectionIds())
            {
               const Arrange::Loc selLoc = Arrange::Find(gArrange, selId);
               if (selLoc.Valid() && gArrange.lanes[selLoc.lane].type != ctxLaneType)
               {
                  ctxSelectionSingleType = false;
                  break;
               }
            }

            const std::vector<uint64_t> ctxSelIds = ArrangeSelectionIds();
            if (ctxSelIds.size() > 1)
            {
               // Multi-clip selection: an intentionally minimal menu. Every
               // other item below (Fade, Sync/Sample BPM, Compositing, Color
               // Grade, Output, Group/Ungroup...) is single-clip-scoped and
               // would either apply nonsensically or silently do nothing to
               // the rest of the batch - only offer what unambiguously means
               // the same thing across every selected clip.
               if (ImGui::MenuItem(L("Rename"), MODKEY "+R"))
               {
                  const std::string label = !cp->name.empty() ? cp->name
                     : (ctxNode != nullptr ? NodeTitle(*ctxNode) : std::string("Unassigned"));
                  gArrangeRenamingClipId = cid;
                  gArrangeRenameTargetIds.clear();
                  for (uint64_t id : ctxSelIds)
                     if (id != cid)
                        gArrangeRenameTargetIds.push_back(id);
                  snprintf(gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer), "%s", label.c_str());
               }
               if (ImGui::BeginMenu(L("Color Tint")))
               {
                  const auto& kPaletteColors = kArrangePalette;
                  for (int ci2 = 0; ci2 < 10; ci2++)
                  {
                     if (ci2 % 5 != 0) ImGui::SameLine();
                     ImGui::PushID(ci2 + 700);
                     const ImVec4 cVec = ImGui::ColorConvertU32ToFloat4(kPaletteColors[ci2].col);
                     if (ImGui::ColorButton(kPaletteColors[ci2].name, cVec, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24)))
                     {
                        const float r = ci2 == 0 ? 0.0f : cVec.x;
                        const float g = ci2 == 0 ? 0.0f : cVec.y;
                        const float b = ci2 == 0 ? 0.0f : cVec.z;
                        ArrangeEdit([&]()
                        {
                           for (uint64_t id : ctxSelIds)
                           {
                              Arrange::Clip* c = Arrange::FindClip(gArrange, id);
                              if (c == nullptr || (c->colorR == r && c->colorG == g && c->colorB == b))
                                 continue;
                              c->colorR = r;
                              c->colorG = g;
                              c->colorB = b;
                              gArrange.revision++;
                           }
                        });
                     }
                     ImGui::PopID();
                  }
                  ImGui::EndMenu();
               }
               // Assign Node...: only when every selected clip shares one
               // lane type - a mixed audio+video batch has no single node
               // type that fits both. ArrangeAssignClipSource itself still
               // rejects any Sample clip in the batch (see its own comment),
               // so a mixed Sample/Clip selection just leaves the Samples
               // untouched rather than needing a separate check here.
               if (ctxSelectionSingleType && ImGui::MenuItem(L("Assign Node...")))
               {
                  gArrangeAssigningClipId = cid;
                  gArrangeAssignTargetIds.clear();
                  for (uint64_t id : ctxSelIds)
                     if (id != cid)
                        gArrangeAssignTargetIds.push_back(id);
                  ImGui::CloseCurrentPopup();
               }
            }
            else
            {
            // Rename and Active/Bypass: apply to every clip type, mirroring
            // the double-click-to-rename and '0'-key shortcuts this menu
            // just gives an explicit, discoverable entry point for.
            if (ImGui::MenuItem(L("Rename"), MODKEY "+R"))
            {
               const std::string label = !cp->name.empty() ? cp->name
                  : (ctxNode != nullptr ? NodeTitle(*ctxNode) : std::string("Unassigned"));
               gArrangeRenamingClipId = cid;
               gArrangeRenameTargetIds.clear();
               snprintf(gArrangeRenameClipBuffer, sizeof(gArrangeRenameClipBuffer), "%s", label.c_str());
            }
            if (ImGui::MenuItem(L("Active"), nullptr, cp->enabled))
               ArrangeToggleEnabledSelection();
            ImGui::Separator();
            // Fade fields: live on the model, one undo entry per drag of a
            // field (opened on the first change, pushed on deactivate, and
            // only if something changed).
            auto fieldGesture = [&](bool changed)
            {
               if (changed && !gArrangeGestureOpen)
                  ArrangeGestureBegin();
            };
            auto fieldGestureEnd = [&]()
            {
               if (ImGui::IsItemDeactivated())
                  ArrangeGestureEnd();
            };
            // Same fields, same units, same typing as the docked inspector -
            // one shared widget rather than a second copy here, which is how
            // the two drifted apart in the first place.
            auto tickField = [&](const char* label, Arrange::Tick cur, Arrange::Tick lo, Arrange::Tick hi,
                                 ArrangeTickUnit unit, Arrange::Tick* out) -> bool
            {
               return ArrangeTickField(label, cur, lo, hi, unit, 160.0f, out);
            };
            // Per clip-type settings only. Position and length are the
            // mouse's (drag, trim handles, blade); clip gain and pan are
            // deferred, the lane's own mix strip covers level for now.
            if (ctxSelectionSingleType && ctxLaneType == Arrange::kLaneAudio)
            {
               Arrange::Tick nt = 0;
               if (tickField("Fade In", cp->fadeIn, 0, cp->length, ArrangeTickUnit::FadeMs, &nt))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->fadeIn = std::clamp<Arrange::Tick>(nt, 0, cp->length);
                  gArrange.revision++;
               }
               fieldGestureEnd();
               cp = Arrange::FindClip(gArrange, cid);
               if (tickField("Fade Out", cp->fadeOut, 0, cp->length, ArrangeTickUnit::FadeMs, &nt))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->fadeOut = std::clamp<Arrange::Tick>(nt, 0, cp->length);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               float gainDb = cp->gainDb;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Gain", &gainDb, -60.0f, 12.0f, "%.1f dB"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->gainDb = gainDb;
                  gArrange.revision++;
               }
               fieldGestureEnd();

               cp = Arrange::FindClip(gArrange, cid);
               float pan = cp->pan;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Pan", &pan, -1.0f, 1.0f, "%.2f"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->pan = std::clamp(pan, -1.0f, 1.0f);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               cp = Arrange::FindClip(gArrange, cid);
               float pitch = cp->pitch;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Pitch", &pitch, -24.0f, 24.0f, "%.1f st"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  // Pushed per-block onto the terminal's own sourceNode by
                  // RunTopology's lookahead (ClipWindow::pitch), not written
                  // here directly - a node can be the source of more than one
                  // clip, and writing straight to it would make one clip's
                  // pitch edit audible on every other clip sharing that node.
                  cp->pitch = std::clamp(pitch, -24.0f, 24.0f);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               // Sync to Tempo / Sample BPM: an Audio Sample-only concept
               // (see Clip::sampleBpm's comment) - this menu used to show
               // the checkbox for any single-lane-type audio selection,
               // which meant it silently applied to a manually-patched
               // Audio Clip too (step 5's consistency-sweep gap). Gated on
               // sampleDropped here to match the docked Clip Settings panel.
               cp = Arrange::FindClip(gArrange, cid);
               if (cp->sampleDropped)
               {
                  bool syncToTempo = cp->syncToTempo;
                  if (ImGui::Checkbox(L("Sync to Tempo"), &syncToTempo))
                     ArrangeEdit([&]() { ArrangeSetSampleSync(cid, syncToTempo); });

                  // Sample BPM only means something while synced (unsynced
                  // plays at native speed), so it is locked while sync is off.
                  cp = Arrange::FindClip(gArrange, cid);
                  float sampleBpm = cp->sampleBpm;
                  ImGui::BeginDisabled(!cp->syncToTempo);
                  if (ArrangeDragFloat("Sample BPM", &sampleBpm, 0.1f, 20.0f, 999.0f, "%.2f", 160.0f))
                  {
                     fieldGesture(true);
                     ArrangeSetSampleBpm(cid, sampleBpm);
                  }
                  fieldGestureEnd();
                  if (const Arrange::Clip* ci = Arrange::FindClip(gArrange, cid))
                  {
                     if (ci->origBpm > 0.0f && ci->origBpm != ci->sampleBpm &&
                         ImGui::Selectable(L("Reset Sample BPM to Detected")))
                     {
                        const float detected = ci->origBpm;
                        ArrangeEdit([&]() { ArrangeSetSampleBpm(cid, detected); });
                     }
                  }
                  ImGui::EndDisabled();
                  if (const Arrange::Clip* ci = Arrange::FindClip(gArrange, cid))
                     ArrangeDrawSampleTempoInfo(*ci);
               }

               ImGui::Separator();
            }
            else if (ctxSelectionSingleType && ctxLaneType == Arrange::kLaneVideo)
            {
               Arrange::Tick nt = 0;
               if (tickField("Fade In", cp->fadeIn, 0, cp->length, ArrangeTickUnit::FadeMs, &nt))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->fadeIn = std::clamp<Arrange::Tick>(nt, 0, cp->length);
                  gArrange.revision++;
               }
               fieldGestureEnd();
               cp = Arrange::FindClip(gArrange, cid);
               if (tickField("Fade Out", cp->fadeOut, 0, cp->length, ArrangeTickUnit::FadeMs, &nt))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->fadeOut = std::clamp<Arrange::Tick>(nt, 0, cp->length);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               cp = Arrange::FindClip(gArrange, cid);
               float opacity = cp->opacity;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->opacity = std::clamp(opacity, 0.0f, 1.0f);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               ImGui::Separator();
            }

            if (ctxSelectionSingleType && ctxLaneType != Arrange::kLaneAudio && ImGui::BeginMenu(L("Compositing")))
            {
               // How this clip lays over the lanes below it. Applies to every
               // selected video clip, like Color Tint.
               const int curMode = Arrange::FindClip(gArrange, cid)->blendMode;
               const std::vector<std::string>& modes = BlendModes::Names();
               for (int m = 0; m < (int)modes.size(); m++)
               {
                  ImGui::PushID(m + 900);
                  if (ImGui::MenuItem(modes[m].c_str(), nullptr, curMode == m))
                  {
                     ArrangeEdit([&]()
                     {
                        for (uint64_t id : ArrangeSelectionIds())
                        {
                           const Arrange::Loc loc = Arrange::Find(gArrange, id);
                           if (!loc.Valid() || gArrange.lanes[loc.lane].type != Arrange::kLaneVideo)
                              continue;
                           Arrange::Clip& c = gArrange.lanes[loc.lane].clips[loc.index];
                           if (c.blendMode == m)
                              continue;
                           c.blendMode = m;
                           gArrange.revision++;
                        }
                     });
                  }
                  ImGui::PopID();
               }
               ImGui::EndMenu();
            }

            if (ctxLaneType == Arrange::kLaneVideo && ImGui::BeginMenu(L("Color Grade")))
            {
               // Basic grade only: brightness/contrast/saturation, consumed
               // by the compositor as a per-clip shader pass. Defaults are a
               // no-op (0/0/1) so this menu never needs a reset button.
               cp = Arrange::FindClip(gArrange, cid);
               float brightness = cp->colorBrightness;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Brightness", &brightness, -1.0f, 1.0f, "%.2f"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->colorBrightness = std::clamp(brightness, -1.0f, 1.0f);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               cp = Arrange::FindClip(gArrange, cid);
               float contrast = cp->colorContrast;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Contrast", &contrast, -1.0f, 1.0f, "%.2f"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->colorContrast = std::clamp(contrast, -1.0f, 1.0f);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               cp = Arrange::FindClip(gArrange, cid);
               float saturation = cp->colorSaturation;
               ImGui::SetNextItemWidth(160.0f);
               if (ArrangeSliderFloat("Saturation", &saturation, 0.0f, 2.0f, "%.2f"))
               {
                  fieldGesture(true);
                  cp = Arrange::FindClip(gArrange, cid);
                  cp->colorSaturation = std::clamp(saturation, 0.0f, 2.0f);
                  gArrange.revision++;
               }
               fieldGestureEnd();

               ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(L("Color Tint")))
            {
               const auto& kPaletteColors = kArrangePalette; // shared with the marker colours
               for (int ci2 = 0; ci2 < 10; ci2++)
               {
                  if (ci2 % 5 != 0) ImGui::SameLine();
                  ImGui::PushID(ci2 + 700);
                  const ImVec4 cVec = ImGui::ColorConvertU32ToFloat4(kPaletteColors[ci2].col);
                  if (ImGui::ColorButton(kPaletteColors[ci2].name, cVec, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24)))
                  {
                     const float r = ci2 == 0 ? 0.0f : cVec.x;
                     const float g = ci2 == 0 ? 0.0f : cVec.y;
                     const float b = ci2 == 0 ? 0.0f : cVec.z;
                     ArrangeEdit([&]()
                     {
                        for (uint64_t id : ArrangeSelectionIds())
                        {
                           Arrange::Clip* c = Arrange::FindClip(gArrange, id);
                           if (c == nullptr || (c->colorR == r && c->colorG == g && c->colorB == b))
                              continue;
                           c->colorR = r;
                           c->colorG = g;
                           c->colorB = b;
                           gArrange.revision++;
                        }
                     });
                  }
                  ImGui::PopID();
               }
               ImGui::EndMenu();
            }

            // Audio/Video Sample clips own a private node from the media
            // import that made them - repointing one at a different node
            // would defeat the whole point of a sample (see sampleDropped's
            // doc comment in ArrangeModel.h). Only Audio/Video Clip can be
            // reassigned.
            cp = Arrange::FindClip(gArrange, cid);
            if (cp != nullptr && !cp->sampleDropped && ImGui::MenuItem(L("Assign Node...")))
            {
               gArrangeAssigningClipId = cid;
               gArrangeAssignTargetIds.clear();
               ImGui::CloseCurrentPopup();
            }

            // Output: only for a source with more than one output of this
            // lane's type (VideoSourceNode has one of each, so it gets none).
            if (ctxNode != nullptr)
            {
               const std::vector<int> outs = ArrangeOutputsOfType(*ctxNode, ctxLaneType);
               if (outs.size() > 1 && ImGui::BeginMenu(L("Output")))
               {
                  const int curOut = Arrange::FindClip(gArrange, cid)->srcOutput;
                  for (int o : outs)
                  {
                     const char* outName = ctxNode->node->OutputLabel(o);
                     char outItem[96];
                     snprintf(outItem, sizeof(outItem), "%s##arrout%d", outName != nullptr ? outName : "out", o);
                     if (ImGui::MenuItem(outItem, nullptr, curOut == o) && curOut != o)
                     {
                        ArrangeEdit([&]()
                        {
                           if (Arrange::Clip* c = Arrange::FindClip(gArrange, cid))
                           {
                              c->srcOutput = o;
                              gArrange.revision++;
                           }
                        });
                     }
                  }
                  ImGui::EndMenu();
               }
            }

            } // end else (single-clip specific properties)

            // Group, Ungroup, and Delete: available for both multi-selection and single-clip / group-selection
            ImGui::Separator();
            if (ImGui::MenuItem(L("Group"), MODKEY "+G", false, ArrangeCanGroupSelection()))
               ArrangeGroupSelection();
            if (ImGui::MenuItem(L("Ungroup"), MODKEY "+Shift+G", false, ArrangeCanUngroupSelection()))
               ArrangeUngroupSelection();
            if (ImGui::MenuItem(L("Delete"), "Backspace", false, !ctxSelIds.empty()))
               ArrangeDeleteSelection();
         }
         ImGui::EndPopup();
      }
      else if (gArrangeMixGestureLaneId != 0 && !ImGui::IsAnyItemActive())
      {
         // A mix-strip control vanished mid-gesture (its lane was deleted or
         // scrolled away): its deactivate never ran.
         if (gArrangeGestureOpen)
            ArrangeGestureEnd();
         gArrangeMixGestureLaneId = 0;
      }
      else if (gArrangeGestureOpen && gArrangeDrag.mode == kArrangeDragNone && gArrangeRenamingLaneId == 0 &&
               gArrangeMarkerDragId == 0 && gArrangeMixGestureLaneId == 0)
      {
         // The popup closed with a field still mid-edit (click outside):
         // its deactivate never ran, so close the gesture here.
         ArrangeGestureEnd();
      }

      // ---- "Add Clip" on an empty lane spot ----
      // Adds a one-bar unassigned clip and hands straight to the canvas
      // click-to-assign flow.
      if (openAddClip)
         ImGui::OpenPopup("##arrangeaddclip");
      if (ImGui::BeginPopup("##arrangeaddclip"))
      {
         ImGui::TextDisabled(T("Add Clip at %s"), ArrangeFormatPos(addClipAtTick).c_str());
         ImGui::Separator();
         if (ImGui::MenuItem(L("Add Clip")))
            AddUnassignedClipAt(addClipToLaneId, addClipAtTick);
         ImGui::EndPopup();
      }

      // ---- lane group-membership context menu (from the drag-handle) ----
      if (openLaneCtx)
         ImGui::OpenPopup("##arrangelanectx");
      if (ImGui::BeginPopup("##arrangelanectx"))
      {
         Arrange::Lane* ctxLane = Arrange::FindLane(gArrange, ctxLaneId);
         if (ctxLane == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            int laneIdx = -1;
            for (size_t k = 0; k < gArrange.lanes.size(); k++)
               if (gArrange.lanes[k].id == ctxLaneId) { laneIdx = (int)k; break; }

            if (ImGui::MenuItem(L("Add Video Track")))
            {
               gArrangeAddTrackInsertAfter = laneIdx;
               InsertArrangeTrack(true);
            }
            if (ImGui::MenuItem(L("Add Audio Track")))
            {
               gArrangeAddTrackInsertAfter = laneIdx;
               InsertArrangeTrack(false);
            }
            if (ImGui::MenuItem(L("Rename Track"), MODKEY "+R"))
            {
               gArrangeRenamingLaneId = ctxLaneId;
               gArrangeRenameJustStarted = true;
            }
            if (ImGui::MenuItem(L("Delete Track")))
               laneToDelete = ctxLaneId;
            ImGui::Separator();

            const uint64_t curGroupId = ctxLane->groupId;
            if (curGroupId != 0 && ImGui::MenuItem(L("Remove from Group")))
               ArrangeEdit([&]() { Arrange::SetLaneTrackGroup(gArrange, ctxLaneId, 0); });

            // Wraps this one track as the sole child of a brand-new group at
            // the track's current position (same parent it already had) -
            // reversible via the group's own Ungroup.
            if (ImGui::MenuItem(L("Convert to Group")))
               ArrangeEdit([&]() { Arrange::AddTrackGroup(gArrange, { ctxLaneId }, std::string(), curGroupId); });

            // Right-clicked a track that's part of a larger multi-row
            // selection: offer to fold every selected track (group rows in
            // the selection are ignored - GroupSelectedLanes only takes
            // lanes) into one new group, nested at the shallowest group that
            // already contains all of them (the lowest common ancestor of
            // their current parent chains; 0 = top level).
            if (gArrangeRowSel.count(ctxLaneId) && gArrangeRowSel.size() > 1)
            {
               std::vector<uint64_t> selLaneIds;
               for (uint64_t rowId : gArrangeRowSel)
                  if (Arrange::FindLane(gArrange, rowId) != nullptr)
                     selLaneIds.push_back(rowId);
               if (selLaneIds.size() > 1 && ImGui::MenuItem(L("Group Selected")))
               {
                  ArrangeEdit([&]()
                  {
                     std::vector<uint64_t> lca;
                     bool first = true;
                     for (uint64_t lid : selLaneIds)
                     {
                        const Arrange::Lane* ln = Arrange::FindLane(gArrange, lid);
                        const uint64_t gid = ln ? ln->groupId : 0;
                        std::vector<uint64_t> anc = Arrange::GroupAncestors(gArrange, gid);
                        std::reverse(anc.begin(), anc.end());
                        anc.push_back(gid);
                        if (first) { lca = anc; first = false; }
                        else
                        {
                           size_t n = std::min(lca.size(), anc.size());
                           size_t common = 0;
                           while (common < n && lca[common] == anc[common]) common++;
                           lca.resize(common);
                        }
                     }
                     const uint64_t shallowestParent = lca.empty() ? 0 : lca.back();
                     Arrange::GroupSelectedLanes(gArrange, selLaneIds, shallowestParent);
                  });
               }
            }


            ImGui::Separator();
            if (ImGui::MenuItem(L("Render Track")) && !ArrangeRenderBusy())
            {
               ArrangeRenderJob job = ArrangeBuildLaneScopedRenderJob(
                  { ctxLaneId }, ctxLane->name.empty() ? ("Track " + std::to_string(ctxLaneId)) : ctxLane->name);
               ArrangeCommitLaneScopedRenderJob(job);
            }
         }
         ImGui::EndPopup();
      }

      // ---- track-group header context menu ----
      if (openGroupCtx)
         ImGui::OpenPopup("##arrangegroupctx");
      if (ImGui::BeginPopup("##arrangegroupctx"))
      {
         const Arrange::TrackGroup* ctxGrp = Arrange::FindTrackGroup(gArrange, ctxGroupId);
         if (ctxGrp == nullptr)
         {
            ImGui::CloseCurrentPopup();
         }
         else
         {
            // Snapshot everything this menu needs from *ctxGrp* up front:
            // "Duplicate Group" below push_backs into gArrange.trackGroups,
            // which can reallocate that vector and dangle ctxGrp for the
            // rest of this popup's draw - so nothing after this point may
            // dereference ctxGrp itself, only these already-captured locals.
            const bool grpWasEnabled = ctxGrp->enabled;
            const std::string ctxGrpLabel = ArrangeGroupDisplayName(*ctxGrp);
            const std::vector<uint64_t> subtreeLanes = Arrange::LanesInTrackGroupRecursive(gArrange, ctxGroupId);
            int lastLaneIdx = -1;
            if (!subtreeLanes.empty())
            {
               for (size_t k = 0; k < gArrange.lanes.size(); k++)
                  if (gArrange.lanes[k].id == subtreeLanes.back()) { lastLaneIdx = (int)k; break; }
            }
            if (ImGui::MenuItem(L("Add Video Track")))
            {
               gArrangeAddTrackInsertAfter = lastLaneIdx;
               InsertArrangeTrack(true, ctxGroupId);
            }
            if (ImGui::MenuItem(L("Add Audio Track")))
            {
               gArrangeAddTrackInsertAfter = lastLaneIdx;
               InsertArrangeTrack(false, ctxGroupId);
            }
            if (ImGui::MenuItem(L("Rename Group"), MODKEY "+R"))
            {
               gArrangeRenamingLaneId = ctxGroupId;
               gArrangeRenameJustStarted = true;
            }
            ImGui::Separator();

            if (ImGui::MenuItem(L("Duplicate Group")))
               ArrangeEdit([&]() { Arrange::DuplicateTrackGroup(gArrange, ctxGroupId); });
            if (ImGui::MenuItem(L("Toggle Enabled"), nullptr, grpWasEnabled))
               ArrangeEdit([&]() { Arrange::SetTrackGroupEnabled(gArrange, ctxGroupId, grpWasEnabled ? 0 : 1); });


            if (ImGui::MenuItem(L("Render Group")) && !ArrangeRenderBusy())
            {
               const std::vector<uint64_t> subtreeLanes = Arrange::LanesInTrackGroupRecursive(gArrange, ctxGroupId);
               if (!subtreeLanes.empty())
               {
                  ArrangeRenderJob job = ArrangeBuildLaneScopedRenderJob(subtreeLanes, ctxGrpLabel);
                  ArrangeCommitLaneScopedRenderJob(job);
               }
            }

            ImGui::Separator();
            if (ImGui::MenuItem(L("Ungroup (Keep Tracks)")))
               ArrangeEdit([&]() { Arrange::RemoveTrackGroup(gArrange, ctxGroupId, /*deleteLanes=*/false); });
            if (ImGui::MenuItem(L("Delete Group + Tracks")))
               ArrangeEdit([&]() { Arrange::RemoveTrackGroup(gArrange, ctxGroupId, /*deleteLanes=*/true); });
         }
         ImGui::EndPopup();
      }

      // Row drag-and-drop reorder/regroup, and lane delete - after the loop
      // so no reference above (lane&, laneRowTop/laneRowH indices) dangles.
      if (pendingRowMove)
      {
         pendingRowMove = false;
         const ArrangeRowRef ref = pendingMoveSrc;
         const bool targetIsGroup = pendingMoveTargetIsGroup;
         const uint64_t targetId = pendingMoveTargetId;
         const int zone = pendingMoveZone; // 0 above, 1 into, 2 below

         bool validTarget = true;
         uint64_t newParentId = 0;
         if (zone == 1)
         {
            newParentId = targetId; // "into" only offered on a group target
         }
         else if (targetIsGroup)
         {
            const Arrange::TrackGroup* tg = Arrange::FindTrackGroup(gArrange, targetId);
            if (!tg) validTarget = false; else newParentId = tg->parentGroupId;
         }
         else
         {
            const Arrange::Lane* tl = Arrange::FindLane(gArrange, targetId);
            if (!tl) validTarget = false; else newParentId = tl->groupId;
         }

         bool rejected = !validTarget;
         if (!rejected && ref.isGroup)
         {
            // Can't become its own parent, or move under its own descendant.
            if (newParentId == ref.id) rejected = true;
            for (uint64_t a : Arrange::GroupAncestors(gArrange, newParentId))
               if (a == ref.id) { rejected = true; break; }
         }

         if (!rejected)
         {
            ArrangeEdit([&]()
            {
               std::vector<uint64_t> movedLanes;
               if (ref.isGroup)
               {
                  const Arrange::TrackGroup* dg = Arrange::FindTrackGroup(gArrange, ref.id);
                  const uint64_t curParent = dg ? dg->parentGroupId : 0;
                  if (curParent != newParentId)
                     Arrange::SetTrackGroupParent(gArrange, ref.id, newParentId);
                  movedLanes = Arrange::LanesInTrackGroupRecursive(gArrange, ref.id);
               }
               else
               {
                  const Arrange::Lane* srcLn = Arrange::FindLane(gArrange, ref.id);
                  const uint64_t curGroup = srcLn ? srcLn->groupId : 0;
                  if (curGroup != newParentId)
                     Arrange::SetLaneTrackGroup(gArrange, ref.id, newParentId);
                  movedLanes = { ref.id };
               }

               size_t anchor;
               if (targetIsGroup)
               {
                  const std::vector<uint64_t> subtree = Arrange::LanesInTrackGroupRecursive(gArrange, targetId);
                  size_t minIdx = gArrange.lanes.size(), maxIdx = 0;
                  bool any = false;
                  for (uint64_t lid : subtree)
                  {
                     const int li = Arrange::LaneIndex(gArrange, lid);
                     if (li < 0) continue;
                     any = true;
                     minIdx = std::min(minIdx, (size_t)li);
                     maxIdx = std::max(maxIdx, (size_t)li + 1);
                  }
                  anchor = !any ? gArrange.lanes.size() : (zone == 0 ? minIdx : maxIdx);
               }
               else
               {
                  const int li = Arrange::LaneIndex(gArrange, targetId);
                  anchor = (li < 0) ? gArrange.lanes.size() : (size_t)li + (zone == 2 ? 1 : 0);
               }
               Arrange::MoveLanesBefore(gArrange, movedLanes, anchor);
            });
         }
      }
      else if (laneToDelete != 0)
      {
         ArrangeEdit([&]() { Arrange::RemoveLane(gArrange, laneToDelete); });
      }

      const float fullTimelineBottom = std::max(lanesContentBottom, pinnedTopY + avail.y);

      // Below the last track, keep the canvas reading as a continuous grid
      // instead of trailing off into dead blank space (WP: decently-sized
      // fixed canvas, independent of how few tracks exist) - faint row
      // guides at the same kLaneHeight spacing, all the way to the bottom
      // of the visible scroll area. Drawn before the loop band and playhead
      // so their highlights overlay this area continuously.
      {
         const float emptyGridBottom = fullTimelineBottom;
         if (emptyGridBottom > lanesContentBottom)
         {
            dl->PushClipRect(ImVec2(headerStartX, lanesContentBottom), ImVec2(rulerStartX + rulerWidth, emptyGridBottom), true);
            // Continue the same alternating row-background stripes the real
            // lanes use (main.cpp laneBg above) so this region reads as more
            // of the same timeline instead of a visually distinct flat-black
            // void - it's still empty, but no longer looks "cut off".
            {
               size_t rowIdx = gArrange.lanes.size();
               for (float gy = lanesContentBottom; gy < emptyGridBottom; gy += kLaneHeight, rowIdx++)
               {
                  const ImU32 stripeBg = (rowIdx % 2 == 0)
                     ? (isLight ? IM_COL32(245, 245, 248, 255) : IM_COL32(24, 24, 28, 255))
                     : (isLight ? IM_COL32(250, 250, 252, 255) : IM_COL32(28, 28, 32, 255));
                  dl->AddRectFilled(ImVec2(headerStartX, gy), ImVec2(rulerStartX + rulerWidth, std::min(gy + kLaneHeight, emptyGridBottom)), stripeBg);
               }
            }
            // Vertical beat/bar lines: the same ones drawn through every track
            // row above, continued down so the timeline still reads as a grid
            // instead of stopping dead at the last track.
            for (const ArrangeGridLine& gl : arrangeGridLines)
            {
               const ImU32 gridCol = isLight
                  ? IM_COL32(0, 0, 0, gl.isMajor ? 60 : 22)
                  : IM_COL32(255, 255, 255, gl.isMajor ? 55 : 18);
               dl->AddLine(ImVec2(gl.x, lanesContentBottom), ImVec2(gl.x, emptyGridBottom), gridCol, 1.0f);
            }
            const ImU32 emptyGridLine = isLight ? IM_COL32(0, 0, 0, 22) : IM_COL32(255, 255, 255, 18);
            for (float gy = lanesContentBottom + kLaneHeight; gy < emptyGridBottom; gy += kLaneHeight)
               dl->AddLine(ImVec2(rulerStartX, gy), ImVec2(rulerStartX + rulerWidth, gy), emptyGridLine, 1.0f);
            dl->PopClipRect();
         }
      }

      // Draw loop region band (armed or mid-Shift+drag) behind everything
      // else. gArrange.settings.loop already holds the live preview range
      // while gArrangeShiftDraggingLoop is true (set above, in the ruler-drag
      // handling), so there is nothing extra to compute here beyond the
      // ticks -> seconds step the panel geometry needs.
      if (gArrange.settings.loop.enabled || gArrangeShiftDraggingLoop)
      {
         const Arrange::LoopRange& bl = gArrange.settings.loop;
         if (bl.end > startTick && bl.start < endTick)
         {
            const float bx0 = std::max(rulerStartX, tickToX(bl.start));
            const float bx1 = std::min(rulerStartX + rulerWidth, tickToX(bl.end));
            const float bandBottom = fullTimelineBottom;
            const ImU32 bandCol = gArrangeShiftDraggingLoop ? IM_COL32(250, 204, 21, 45) : IM_COL32(250, 204, 21, 30);
            const ImU32 bandBorder = IM_COL32(250, 204, 21, 180);
            const bool isDraggingLeft = (gArrangeLoopDragMode == kArrangeLoopDragStart);
            const bool isDraggingRight = (gArrangeLoopDragMode == kArrangeLoopDragEnd);
            const bool isDraggingHeader = (gArrangeLoopDragMode == kArrangeLoopDragMove);

            // From the tick strip down: the marker strip above stays clear.
            dl->AddRectFilled(ImVec2(bx0, kTickStripTop), ImVec2(bx1, bandBottom), bandCol);

            // Loop brace header bar in ruler
            const ImU32 headerBarCol = isDraggingHeader ? IM_COL32(255, 235, 59, 255) : IM_COL32(250, 204, 21, 230);
            dl->AddRectFilled(ImVec2(bx0, kTickStripTop), ImVec2(bx1, kTickStripTop + 4.0f), headerBarCol, 2.0f);

            // Left boundary line & bracket handle [
            const ImU32 leftCol = isDraggingLeft ? IM_COL32(255, 255, 255, 255) : bandBorder;
            const float leftStroke = isDraggingLeft ? 2.5f : 1.5f;
            dl->AddLine(ImVec2(bx0, kTickStripTop), ImVec2(bx0, bandBottom), leftCol, leftStroke);
            dl->AddLine(ImVec2(bx0, kTickStripTop), ImVec2(bx0, kTickStripTop + 8.0f), leftCol, 3.0f);
            dl->AddLine(ImVec2(bx0, kTickStripTop + 8.0f), ImVec2(bx0 + 5.0f, kTickStripTop + 8.0f), leftCol, 2.0f);

            // Right boundary line & bracket handle ]
            const ImU32 rightCol = isDraggingRight ? IM_COL32(255, 255, 255, 255) : bandBorder;
            const float rightStroke = isDraggingRight ? 2.5f : 1.5f;
            dl->AddLine(ImVec2(bx1, kTickStripTop), ImVec2(bx1, bandBottom), rightCol, rightStroke);
            dl->AddLine(ImVec2(bx1, kTickStripTop), ImVec2(bx1, kTickStripTop + 8.0f), rightCol, 3.0f);
            dl->AddLine(ImVec2(bx1 - 5.0f, kTickStripTop + 8.0f), ImVec2(bx1, kTickStripTop + 8.0f), rightCol, 2.0f);
         }
      }

      // Playhead, drawn off Transport::Beats() - the clock clips are
      // scheduled on - so it sits exactly where they sound. While scrubbing
      // the real playhead stays put and a ghost follows the mouse; the
      // transport seeks once, on release (WP6).
      {
         const float lineBottom = fullTimelineBottom;
         const double playBeats = std::max(0.0, tr.Beats());
         if (playBeats >= startBeat && playBeats <= endBeat)
         {
            const float playheadX = beatToX(playBeats);
            const ImU32 playheadCol = IM_COL32(239, 68, 68, 255);
            dl->AddLine(ImVec2(playheadX, kTickStripTop), ImVec2(playheadX, lineBottom), playheadCol, 1.5f);
            const float triSize = 7.0f;
            dl->AddTriangleFilled(ImVec2(playheadX - triSize, kTickStripTop), ImVec2(playheadX + triSize, kTickStripTop),
                                  ImVec2(playheadX, kTickStripTop + triSize * 1.6f), playheadCol);
         }
         if (gArrangeScrubbing && gArrangeScrubTick >= startTick && gArrangeScrubTick <= endTick)
         {
            const float gx = tickToX(gArrangeScrubTick);
            const ImU32 ghostCol = IM_COL32(239, 68, 68, 120);
            dl->AddLine(ImVec2(gx, kTickStripTop), ImVec2(gx, lineBottom), ghostCol, 1.5f);
            const float triSize = 7.0f;
            dl->AddTriangleFilled(ImVec2(gx - triSize, kTickStripTop), ImVec2(gx + triSize, kTickStripTop),
                                  ImVec2(gx, kTickStripTop + triSize * 1.6f), ghostCol);
            const std::string ghostLabel = ArrangeFormatBBT(gArrangeScrubTick) + "  |  " + ArrangeFormatTickSeconds(gArrangeScrubTick);
            const ImVec2 ts = ImGui::CalcTextSize(ghostLabel.c_str());
            float lx = gx + 6.0f;
            if (lx + ts.x + 6.0f > rulerStartX + rulerWidth)
               lx = gx - 6.0f - ts.x;
            const ImVec2 l0(lx - 3.0f, kTickStripTop + kRulerHeight - kMarkerStripH + 2.0f);
            dl->AddRectFilled(l0, ImVec2(l0.x + ts.x + 6.0f, l0.y + ts.y + 2.0f),
                              isLight ? IM_COL32(255, 255, 255, 230) : IM_COL32(20, 20, 24, 230), 3.0f);
            dl->AddText(ImVec2(lx, l0.y + 1.0f), ImGui::GetColorU32(ImGuiCol_Text), ghostLabel.c_str());
         }
      }

      // + Add Track button below the last track header
      if (!gArrange.lanes.empty())
      {
         ImGui::SetCursorScreenPos(ImVec2(headerStartX + 4.0f, lanesContentBottom + 4.0f));
         if (ImGui::Button(L("+ Add Track"), ImVec2(kHeaderWidth - 8.0f, 22.0f)))
         {
            gArrangeAddTrackInsertAfter = -1;
            ImGui::OpenPopup("##arrangeaddtrackpopup");
         }
      }

      // Expand dummy to define scroll area
      const float totalH = lanesContentBottom - scrollTL.y + 40.0f;

      // Empty space below lanes: click clears selection, right click opens Add Track menu
      const bool mouseBelowLanes = mouse.x >= headerStartX && mouse.x < headerStartX + avail.x &&
                                   mouse.y >= lanesContentBottom && mouse.y < scrollTL.y + std::max(totalH, avail.y);
      if (mouseBelowLanes && ImGui::IsWindowHovered())
      {
         const ImGuiIO& lio = ImGui::GetIO();
         if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gArrangeDrag.mode == kArrangeDragNone &&
             !gArrangeBladeOn && !lio.KeyShift && !lio.KeySuper && !lio.KeyCtrl)
         {
            gArrangeSel.clear();
            gArrangeSelAnchor = 0;
         }
         if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
         {
            ImGui::OpenPopup("##arrangeemptyspacectx");
         }
      }
      if (ImGui::BeginPopup("##arrangeemptyspacectx"))
      {
         if (ImGui::MenuItem(L("Add Video Track")))
            ArrangeEdit([&]() { Arrange::AddLane(gArrange, Arrange::kLaneVideo); });
         if (ImGui::MenuItem(L("Add Audio Track")))
            ArrangeEdit([&]() { Arrange::AddLane(gArrange, Arrange::kLaneAudio); });
         if (ImGui::MenuItem(L("Add Track Group")))
            ArrangeEdit([&]() { Arrange::AddTrackGroup(gArrange, {}, "New Group"); });
         ImGui::EndPopup();
      }

      ImGui::SetCursorScreenPos(scrollTL);
      ImGui::Dummy(ImVec2(avail.x, totalH));

      ImGui::EndChild();

      // "Rendering - timeline locked" (WP7). The click-catcher the progress
      // dialog puts across the viewport already stops every mouse gesture in
      // here, and the shortcut block up top is gated on ArrangeRenderBusy();
      // this is the reason-why, so an inert timeline reads as locked rather
      // than as hung. On the panel's own draw list and clipped to the panel,
      // so it can never paint over the progress dialog.
      if (ArrangeRenderBusy())
      {
         ImDrawList* odl = ImGui::GetWindowDrawList();
         const ImVec2 a = gArrangePanelRectMin, b = gArrangePanelRectMax;
         odl->PushClipRect(a, b, true);
         odl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110));
         const char* lockText = T("Rendering - timeline locked");
         const ImVec2 ts = ImGui::CalcTextSize(lockText);
         const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
         const ImVec2 t0(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
         odl->AddRectFilled(ImVec2(t0.x - 12.0f, t0.y - 7.0f), ImVec2(t0.x + ts.x + 12.0f, t0.y + ts.y + 7.0f),
                            IM_COL32(18, 18, 22, 225), 5.0f);
         odl->AddText(t0, IM_COL32(235, 235, 240, 255), lockText);
         odl->PopClipRect();
      }

      if (showVp && gArrangeViewportOnRight)
      {
         ImGui::SameLine();
         drawViewportMonitor();
      }

      if (gArrangeClipSettingsPanelOpen)
      {
         ImGui::SameLine();
         DrawArrangeClipSettingsChild(kSettingsW);
      }
   }


   void DrawArrangePanelDocked(const char* id, const ImVec2& size)
   {
      const float kGrip = 6.0f;
      const int dock = ArrangePanelDock();
      const bool vertical = (dock == 1 || dock == 2);
      const bool gripFirst = (dock == 0 || dock == 1);

      PushDockedPanelStyle(/*isChild=*/true);
      ImGui::BeginChild(id, size, false);
      PopDockedPanelStyle();

      gArrangePanelRectMin = ImGui::GetWindowPos();
      gArrangePanelRectMax = ImVec2(gArrangePanelRectMin.x + ImGui::GetWindowSize().x,
                                 gArrangePanelRectMin.y + ImGui::GetWindowSize().y);

      const ImVec2 inner = ImGui::GetContentRegionAvail();

      auto grip = [&]()
      {
         ImGui::InvisibleButton("##arrangepanelgrip",
                                vertical ? ImVec2(kGrip, std::max(1.0f, inner.y))
                                         : ImVec2(std::max(1.0f, inner.x), kGrip));
         DrawPanelSeam(vertical, gripFirst);
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
         if (ImGui::IsItemActive())
         {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            switch (dock)
            {
               case 0: gArrangePanelHeight -= d.y; break;
               case 1: gArrangePanelWidth -= d.x; break;
               case 2: gArrangePanelWidth += d.x; break;
               default: gArrangePanelHeight += d.y; break;
            }
            gArrangePanelWidth = std::max(kArrangePanelMinWidth, gArrangePanelWidth);
            gArrangePanelHeight = std::max(kArrangePanelMinHeight, gArrangePanelHeight);
         }
      };

      if (gripFirst)
      {
         grip();
         if (vertical)
            ImGui::SameLine();
      }

      const ImVec2 gap = ImGui::GetStyle().ItemSpacing;
      PushDockedPanelStyle(/*isChild=*/true);
      ImGui::BeginChild("##arrangepanelinnercontent",
                        vertical ? ImVec2(std::max(1.0f, inner.x - kGrip - gap.x), inner.y)
                                 : ImVec2(0, std::max(1.0f, inner.y - kGrip - gap.y)),
                        ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
      DrawArrangePanelContent();
      ImGui::EndChild();
      PopDockedPanelStyle();

      if (!gripFirst)
      {
         if (vertical)
            ImGui::SameLine();
         grip();
      }

      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
      ImGui::EndChild();
      ImGui::PopStyleVar();
   }
}

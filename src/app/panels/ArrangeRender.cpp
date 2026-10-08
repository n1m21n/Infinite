// Offline render windows, arrange clip settings and render queue (moved verbatim from main.cpp).
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"

namespace app
{
   // Entry point for the "Render" button in an OutputNode's params (see the
   // node-params drawing code below). Refuses up front if the patch has a
   // hardware-driven source or another take (on this or any other OutputNode)
   // is already in progress; otherwise stops the live audio device (so its
   // real callback can't race this take's synchronous ProcessOffline calls -
   // see the main-loop pump next to glfwPollEvents()) and forces Transport
   // to play, restoring both once the take finishes or is cancelled.
   void StartOfflineRenderSession(OutputNode* n, int width, int height, bool isArrange)
   {
      if (gOfflineRender.active || n == nullptr)
         return;
      if (n->IsRecording() || n->IsFinalizing())
         return;

      // A live source can't be pre-synthesized for an offline take, so refuse
      // rather than write a file full of one frozen frame. An arrangement
      // take scopes the sweep to the clips inside its own range (WP7 #4);
      // any side of it that renders the canvas instead falls back to the
      // whole-patch sweep, which is what that side actually cooks.
      const bool arrangeScoped = isArrange && gOfflineRender.timelineVideo && gOfflineRender.timelineAudio;
      INode* hw = arrangeScoped
                     ? FindHardwareDrivenNodeInArrangeRange(gOfflineRender.rangeStartTick,
                                                            gOfflineRender.rangeEndTick, true, true)
                     : FindHardwareDrivenNode();
      if (hw != nullptr)
      {
         n->SetRecordStatus("refused: " + std::string(isArrange ? "the render range has" : "patch has") +
                            " a live source (camera/MIDI/Syphon In) "
                            "that can't be pre-synthesized for an offline take");
         return;
      }

      // The graph's AudioNodes are only PrepareToPlay'd once the engine has
      // opened a device at least once this session (RebuildAudioTopology
      // skips the PrepareToPlay loop while SampleRate() is 0), and
      // StartOfflineRender reads that same rate to budget/mux the take - so
      // for a synth-audio take with the device currently off, start it
      // briefly here first. If it can't start, the take still goes ahead as
      // video-only rather than being refused outright: an unprepared graph
      // would only write silence anyway.
      const bool deviceWasRunningBefore = AudioEngine::Instance().SampleRate() > 0.0;
      const bool wantsGraphAudio =
         n->includeAudio && (isArrange || (n->AudioInput().IsConnected() &&
         dynamic_cast<AudioFileNode*>(n->AudioInput().GetSource()) == nullptr));
      // A headless job never opens a device: the take runs at the job's own
      // rate (gHeadlessAudioRate), which is what makes it reproducible.
      if (wantsGraphAudio && AudioEngine::Instance().SampleRate() <= 0.0 && gHeadlessAudioRate <= 0.0)
      {
         if (!StartAudioEngine(gAudioStartError))
            n->SetRecordStatus("no audio device (" + gAudioStartError + ") - rendering video only");
      }

      // Detach the device BEFORE arming the take, not after. While the
      // device is open its real-time callback keeps writing into the same
      // capture ring the take reads from, and Platform::AudioDeviceClose is
      // not instant (hundreds of ms of AVAudioEngine teardown) - arming
      // first captured that whole live tail into the head of the take's
      // audio track, so the file came out with more audio than picture and
      // everything after the tail sat out of sync.
      double takeSampleRate = AudioEngine::Instance().SampleRate();
      if (takeSampleRate > 0.0)
         AudioEngine::Instance().Stop();
      else
         takeSampleRate = gHeadlessAudioRate; // 0 outside a headless job

      if (!n->StartOfflineRender(n->recordVideoPath, takeSampleRate, width, height, isArrange))
      {
         // Put the device back exactly as it was - the take never started.
         if (deviceWasRunningBefore)
            StartAudioEngine(gAudioStartError);
         return; // n->RecordStatus() already carries the reason
      }

      gOfflineRender.active = true;
      gOfflineRender.node = n;
      gOfflineRender.includeAudio = n->OfflineNeedsGraphAudio();
      // Deliberately the state from BEFORE the warmup start above, so a
      // device this function itself opened is left closed afterwards rather
      // than silently switching the user's audio on.
      gOfflineRender.deviceWasRunning = deviceWasRunningBefore;
      gOfflineRender.wasPlaying = Transport::Instance().IsPlaying();
      gOfflineRender.startTime = glfwGetTime();
      gOfflineRender.lastProgressTime = gOfflineRender.startTime;
      gOfflineRender.lastFramesDone = -1;
      gOfflineRender.waitingOnEncoder = false;
      gOfflineRender.arrangeDriven = isArrange;

      // An offline render is meant to run as fast as the hardware allows, and
      // with vsync on it cannot: the main loop blocks in glfwSwapBuffers for
      // the rest of every display refresh, so the pump gets a little over
      // half of each 16.7ms whatever budget it is given. Dropping the swap
      // interval for the duration of the take is the difference between "as
      // fast as it can go" and "a bit faster than realtime" on a heavy patch,
      // which is what makes a long render look like it has stalled.
      gOfflineRender.vsyncWasOn = gVsync;
      SetCanvasSwapInterval(0);

      gOfflineRender.startSeconds = Transport::Instance().Seconds();
      Transport::Instance().SetOfflineMode(true, takeSampleRate);
      Transport::Instance().SetPlaying(true);
      ForceAudioRepare();
      RebuildAudioTopology();
   }


   // Small floating progress dialog, drawn once a frame (right before
   // ImGui::Render()) for the whole duration of a take - matches the
   // TouchDesigner/After Effects "rendering..." modal rather than living
   // inline in the OutputNode's own params panel, since the node stays
   // wherever it is on the canvas but the take applies to the whole patch.
   void DrawOfflineRenderProgressWindow()
   {
      OutputNode* n = gOfflineRender.node;
      if (n == nullptr)
         return;

      ImGuiIO& io = ImGui::GetIO();

      // Full-screen dim + click-catcher, drawn first so it sits below the
      // dialog but above every other panel - "pauses everything" means the
      // canvas/graph/arrangement panel underneath shouldn't be clickable
      // while a take is in flight, not just that a dialog floats on top of
      // them. An invisible button spanning the whole viewport intercepts
      // clicks without needing per-panel disable flags scattered elsewhere.
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, tok::V4(tok::palf::v_0_0_0_350));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      ImGui::Begin("##OfflineRenderBlocker", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::InvisibleButton("##OfflineRenderBlockerCatch", io.DisplaySize);
      ImGui::End();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();

      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                               ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      PushElevatedPanelStyle(/*isChild=*/false);
      ImGui::Begin(L("Offline Render"), nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

      const int total = std::max(1, n->OfflineFramesTotal());
      const int done = std::min(n->OfflineFramesDone(), total);
      const float frac = (float)done / (float)total;

      if (n->IsOfflineCancelling())
      {
         ImGui::Text("%s", T("Cancelling..."));
      }
      else if (n->IsOfflineFinalizing())
      {
         ImGui::Text("%s", T("Finalizing..."));
      }
      else if (n->IsPrerolling())
      {
         ImGui::Text(T("Warming up... (%d frames left)"), n->PrerollFramesRemaining());
      }
      else
      {
         // Frame counter and bar only. A rate/ETA readout and an
         // encoder-wait notice lived here briefly; they were diagnostics for
         // a specific stall wearing a UI's clothes, and they read as noise
         // once the stall was fixed. gOfflineRender still tracks the timing
         // they came from - see its startTime/lastProgressTime - so a future
         // diagnostic can print rather than draw.
         ImGui::Text(T("Rendering... %d/%d"), done, total);
      }
      // "Job 2 of 4" while a run works through the queue; silent for a lone
      // take, where the count would only be noise.
      int qIdx = 0, qTotal = 0;
      ArrangeRenderQueuePosition(qIdx, qTotal);
      if (qTotal > 1 && qIdx > 0)
         ImGui::TextDisabled(T("Job %d of %d"), qIdx, qTotal);
      // A cancelled take's progress bar is meaningless - it would sit frozen
      // at whatever fraction the render reached, which is precisely what
      // made a slow cancel look like a hang.
      if (n->IsOfflineCancelling())
         ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(280, 0), "");
      else
         ImGui::ProgressBar(frac, ImVec2(280, 0));

      ImGui::BeginDisabled(n->IsOfflineFinalizing());
      if (ImGui::Button(L("Cancel"), ImVec2(120, 0)))
         n->RequestFinishOfflineRender(true);
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(qTotal <= 1);
      if (ImGui::Button(L("Cancel All"), ImVec2(120, 0)))
         ArrangeRenderCancelAll();
      ImGui::EndDisabled();

      ImGui::End();
      PopElevatedPanelStyle();
   }



   // The audio-only twin of DrawOfflineRenderProgressWindow. Same dim +
   // click-catcher discipline: a WAV take drives the graph synchronously from
   // the main loop, so editing underneath it while it runs would be editing
   // the thing being rendered.
   void DrawArrangeWavRenderProgressWindow()
   {
      if (!gArrangeWavRender.active)
         return;

      ImGuiIO& io = ImGui::GetIO();
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, tok::V4(tok::palf::v_0_0_0_350));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      ImGui::Begin("##ArrangeWavRenderBlocker", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::InvisibleButton("##ArrangeWavRenderBlockerCatch", io.DisplaySize);
      ImGui::End();
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor();

      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                              ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      PushElevatedPanelStyle(/*isChild=*/false);
      ImGui::Begin(L("Rendering Audio"), nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

      const long long total = std::max<long long>(1, gArrangeWavRender.framesTotal);
      const long long done = std::min(gArrangeWavRender.framesDone, total);
      const float frac = (float)((double)done / (double)total);
      if (gArrangeWavRender.cancelRequested)
         ImGui::Text("%s", T("Cancelling..."));
      else
         ImGui::Text(T("%.1fs of %.1fs at %d Hz"), (double)done / gArrangeWavRender.sampleRate,
                     (double)total / gArrangeWavRender.sampleRate,
                     (int)llround(gArrangeWavRender.sampleRate));
      // "Job 2 of 4" while a run works through the queue; silent for a lone
      // take, where the count would only be noise.
      int qIdx = 0, qTotal = 0;
      ArrangeRenderQueuePosition(qIdx, qTotal);
      if (qTotal > 1 && qIdx > 0)
         ImGui::TextDisabled(T("Job %d of %d"), qIdx, qTotal);
      ImGui::ProgressBar(frac, ImVec2(280, 0));
      ImGui::BeginDisabled(gArrangeWavRender.cancelRequested);
      if (ImGui::Button(L("Cancel"), ImVec2(120, 0)))
         ArrangeRenderCancelActive();
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(qTotal <= 1);
      if (ImGui::Button(L("Cancel All"), ImVec2(120, 0)))
         ArrangeRenderCancelAll();
      ImGui::EndDisabled();
      ImGui::End();
      PopElevatedPanelStyle();
   }


   const char* ArrangeRenderStatusText(int status)
   {
      switch (status)
      {
      case kArrangeJobQueued:     return "Queued";
      case kArrangeJobRendering:  return "Rendering";
      case kArrangeJobFinalizing: return "Finalizing";
      case kArrangeJobDone:       return "Done";
      case kArrangeJobFailed:     return "Failed";
      case kArrangeJobCancelled:  return "Cancelled";
      default:                    return "?";
      }
   }


   ImVec4 ArrangeRenderStatusColor(int status)
   {
      switch (status)
      {
      case kArrangeJobDone:       return tok::V4(tok::palf::v_450_800_500_1000);
      case kArrangeJobFailed:     return tok::V4(tok::palf::v_900_450_400_1000);
      case kArrangeJobCancelled:  return tok::V4(tok::palf::v_700_650_400_1000);
      case kArrangeJobRendering:
      case kArrangeJobFinalizing: return tok::V4(tok::palf::v_500_720_950_1000);
      default:                    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
      }
   }


   std::string ArrangeRenderFileName(const std::string& path)
   {
      const size_t slash = path.find_last_of("/\\");
      return slash == std::string::npos ? path : path.substr(slash + 1);
   }


   std::string ArrangeRenderJobSourceText(const ArrangeRenderJob& j)
   {
      const char* a = j.audioSource == kArrangeAudioTimeline ? "Timeline"
                      : j.audioSource == kArrangeAudioCanvas ? "Canvas"
                                                             : "-";
      const char* v = j.videoSource == kArrangeVideoTimeline ? "Timeline"
                      : j.videoSource == kArrangeVideoCanvas ? "Canvas"
                                                             : "-";
      return std::string("A:") + a + "  V:" + v;
   }


   // Seconds left on the running job, or -1 when there is nothing to go on
   // yet. Straight-line from the frames done so far, which is what every
   // encoder's ETA is: a long take settles within a few seconds.
   double ArrangeRenderJobEtaSeconds(const ArrangeRenderJob& j)
   {
      if (j.status != kArrangeJobRendering || j.framesDone <= 0 || j.framesTotal <= 0)
         return -1.0;
      const double elapsed = glfwGetTime() - j.startedTime;
      if (elapsed < 0.5)
         return -1.0;
      const double perFrame = elapsed / (double)j.framesDone;
      return perFrame * (double)std::max(0, j.framesTotal - j.framesDone);
   }


   // Stops the run and marks everything still waiting as cancelled. Shared by
   // the queue window and by both progress dialogs, which float above the
   // full-screen click-catcher and so are the only reachable UI mid-take.
   void ArrangeRenderCancelAll()
   {
      // Stop feeding first, then cancel what is in flight: the other order
      // lets ArrangeRenderQueueTick start the next job in the same frame the
      // current one was cancelled.
      gArrangeRenderQueueRunning = false;
      for (ArrangeRenderJob& j : gArrangeRenderQueue)
         if (j.status == kArrangeJobQueued)
         {
            j.status = kArrangeJobCancelled;
            j.message = "cancelled";
         }
      ArrangeRenderCancelActive();
   }


   // Docked inspector child panel for whatever is currently selected on the timeline -
   // a clip, a track, or a group. Pinned to the right side of the timeline panel.
   void DrawArrangeClipSettingsChild(float panelW)
   {
      PushDockedPanelStyle(/*isChild=*/true);
      ImGui::BeginChild("##arrangeclipsettings_child", ImVec2(panelW, 0), true,
                        ImGuiWindowFlags_AlwaysUseWindowPadding);
      PopDockedPanelStyle();

      const float availW = ImGui::GetContentRegionAvail().x;

      auto DrawCloseBtn = []() -> bool
      {
         const float sz = 16.0f;
         bool clicked = ImGui::InvisibleButton("##closeinspector", ImVec2(sz, sz));
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImVec2 bmin = ImGui::GetItemRectMin();
         const ImVec2 bmax = ImGui::GetItemRectMax();
         const ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
         const float iconSize = sz * 0.65f;
         const ImU32 col = ImGui::IsItemHovered() ? tok::U32(tok::pal::c_E63C3CFF) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
         Tabler::DrawX(dl, center, iconSize, col);
         return clicked;
      };

      auto DrawBypassButton = [](const char* id, bool enabled, const char* labelActive = "Active", const char* labelBypassed = "Bypassed") -> bool
      {
         bool toggled = false;
         if (enabled)
         {
            ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_10B9812D));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::U32(tok::pal::c_10B98150));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::U32(tok::pal::c_10B98178));
            ImGui::PushStyleColor(ImGuiCol_Text, tok::U32(tok::pal::c_34D399FF));
         }
         else
         {
            ImGui::PushStyleColor(ImGuiCol_Button, tok::U32(tok::pal::c_EF444423));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tok::U32(tok::pal::c_EF444446));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, tok::U32(tok::pal::c_EF444464));
            ImGui::PushStyleColor(ImGuiCol_Text, tok::U32(tok::pal::c_F87171FF));
         }
         char buf[96];
         snprintf(buf, sizeof(buf), "%s%s", enabled ? labelActive : labelBypassed, id);
         if (ImGui::Button(buf, ImVec2(-FLT_MIN, 22.0f)))
            toggled = true;
         ImGui::PopStyleColor(4);
         return toggled;
      };

      uint64_t clipId = 0;
      if (gArrangeSel.size() == 1)
         clipId = *gArrangeSel.begin();
      Arrange::Clip* clip = clipId != 0 ? Arrange::FindClip(gArrange, clipId) : nullptr;

      const float fieldW = std::max(70.0f, availW - 65.0f);

      auto tickField = [&](const char* label, Arrange::Tick cur, Arrange::Tick lo, Arrange::Tick hi,
                           ArrangeTickUnit unit, Arrange::Tick* out) -> bool
      {
         return ArrangeTickField(label, cur, lo, hi, unit, fieldW, out);
      };

      auto drawPaletteSwatches = [&](const std::function<void(uint32_t)>& onSelect)
      {
         for (int pi = 0; pi < IM_ARRAYSIZE(kArrangePalette); pi++)
         {
            if (pi % 5 != 0) ImGui::SameLine(0.0f, 4.0f);
            const ImVec4 cVec = ImGui::ColorConvertU32ToFloat4(kArrangePalette[pi].col);
            ImGui::PushID(pi);
            if (ImGui::ColorButton(kArrangePalette[pi].name, cVec, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18)))
               onSelect(ArrangeMarkerRGBA(kArrangePalette[pi].col));
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", kArrangePalette[pi].name);
            ImGui::PopID();
         }
      };

      if (clip != nullptr)
      {
         const Arrange::Loc cloc = Arrange::Find(gArrange, clipId);
         const Arrange::Lane* owningLane = cloc.Valid() ? &gArrange.lanes[cloc.lane] : nullptr;
         const bool isVideo = owningLane != nullptr && owningLane->type == Arrange::kLaneVideo;
         const bool isSample = clip->sampleDropped;

         // Header: Title + Close [X]
         const char* clipKindLabel = isVideo
            ? (isSample ? "Video Sample" : "Video Clip")
            : (isSample ? "Audio Sample" : "Audio Clip");
         ImGui::TextUnformatted(clipKindLabel);
         ImGui::SameLine(availW - 18.0f);
         if (DrawCloseBtn())
            gArrangeClipSettingsPanelOpen = false;
         ImGui::Separator();

         // Name
         char nameBuf[128];
         snprintf(nameBuf, sizeof(nameBuf), "%s", clip->name.c_str());
         ImGui::SetNextItemWidth(-FLT_MIN);
         if (ImGui::InputText("##clipinspname", nameBuf, sizeof(nameBuf)))
         {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  c->name = nameBuf;
                  gArrange.revision++;
               }
            });
         }

         // Bypass button below name
         ImGui::Spacing();
         if (DrawBypassButton("##clipbypassbtn", clip->enabled, "Active", "Bypassed"))
         {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  c->enabled = !c->enabled;
                  gArrange.revision++;
               }
            });
         }

         ImGui::Spacing();
         ImGui::TextDisabled("%s", T("Timing & Position"));
         Arrange::Tick newTick = 0;
         if (tickField("Start##clipstart", clip->start, 0, Arrange::kMaxTick, ArrangeTickUnit::Position, &newTick))
         {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  c->start = newTick;
                  gArrange.revision++;
               }
            });
         }
         if (tickField("Length##cliplength", clip->length, Arrange::kPPQ / 16, Arrange::kMaxTick, ArrangeTickUnit::Length, &newTick))
         {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  c->length = newTick;
                  gArrange.revision++;
               }
            });
         }
         if (tickField("Fade In##clipfadein", clip->fadeIn, 0, clip->length, ArrangeTickUnit::FadeMs, &newTick))
         {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  c->fadeIn = std::clamp<Arrange::Tick>(newTick, 0, c->length);
                  gArrange.revision++;
               }
            });
         }
         if (tickField("Fade Out##clipfadeout", clip->fadeOut, 0, clip->length, ArrangeTickUnit::FadeMs, &newTick))
         {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  c->fadeOut = std::clamp<Arrange::Tick>(newTick, 0, c->length);
                  gArrange.revision++;
               }
            });
         }

         if (isVideo && ArrangeVideoSourceConflictClips().count(clipId))
         {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", T("Source"));
            // One line, not a paragraph: the button beside it is the whole
            // fix, and the reasoning belongs in a tooltip the user opens
            // when they want it rather than in permanent panel text.
            ImGui::TextColored(tok::V4(tok::palf::v_1000_650_200_1000), "Shared with another video track.");
            ArrangeSharedSourceTooltip(
               "Both clips show the same frame - a video source reads the global transport, not "
               "the clip - and where they overlap the upper track hides this one's blend mode, "
               "opacity and grade.\n\nMake Unique gives this clip its own copy of the node, fed "
               "by the same inputs and carrying the same modulations.");
            if (ImGui::Button(L("Make Unique##clipuniquevideo"), ImVec2(-FLT_MIN, 0)))
               ArrangeMakeClipSourceUnique(clipId); // takes its own full checkpoint
         }

         // Every audio clip position-locks its source now (see RunTopology's
         // position lock), so a source shared across lanes is a conflict for
         // any audio clip, not only a dropped Sample. It is a fact about the
         // patch rather than a setting, so it stays informational.
         if (!isVideo && gArrangeRetriggerConflictClipIds.count(clipId))
         {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", T("Playback & Trigger"));
            ImGui::TextColored(tok::V4(tok::palf::v_1000_650_200_1000), "Shared with another track.");
            ArrangeSharedSourceTooltip(
               "The node holds one playback position and both tracks set it every block, so "
               "whichever is summed last wins and this clip can end up playing the other one's "
               "position.\n\nMake Unique gives this clip its own copy of the node, fed by the "
               "same inputs and carrying the same modulations.");
            if (ImGui::Button(L("Make Unique##clipuniqueaudio"), ImVec2(-FLT_MIN, 0)))
               ArrangeMakeClipSourceUnique(clipId); // takes its own full checkpoint
         }

         ImGui::Spacing();
         ImGui::Separator();

         if (isVideo)
         {
            ImGui::TextDisabled("%s", T("Compositing & Video"));
            const std::vector<std::string>& modes = BlendModes::Names();
            const char* curBlendName = (clip->blendMode >= 0 && clip->blendMode < (int)modes.size())
               ? modes[clip->blendMode].c_str() : "Normal";
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::BeginCombo(L("Blend##clipblend"), curBlendName))
            {
               for (int m = 0; m < (int)modes.size(); m++)
               {
                  const bool sel = (clip->blendMode == m);
                  if (ImGui::Selectable(modes[m].c_str(), sel))
                  {
                     ArrangeEdit([&]() {
                        if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                        {
                           c->blendMode = m;
                           gArrange.revision++;
                        }
                     });
                  }
               }
               ImGui::EndCombo();
            }

            float opacity = clip->opacity;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Opacity##clipop", &opacity, 0.0f, 1.0f, "%.2f"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     c->opacity = std::clamp(opacity, 0.0f, 1.0f);
                     gArrange.revision++;
                  }
               });
            }

            float brightness = clip->colorBrightness;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Bright##clipbright", &brightness, -1.0f, 1.0f, "%.2f"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     c->colorBrightness = std::clamp(brightness, -1.0f, 1.0f);
                     gArrange.revision++;
                  }
               });
            }

            float contrast = clip->colorContrast;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Contrast##clipcont", &contrast, -1.0f, 1.0f, "%.2f"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     c->colorContrast = std::clamp(contrast, -1.0f, 1.0f);
                     gArrange.revision++;
                  }
               });
            }

            float saturation = clip->colorSaturation;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Sat##clipsat", &saturation, 0.0f, 2.0f, "%.2f"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     c->colorSaturation = std::clamp(saturation, 0.0f, 2.0f);
                     gArrange.revision++;
                  }
               });
            }
         }
         else
         {
            ImGui::TextDisabled("%s", T("Audio Adjustments"));
            float gainDb = clip->gainDb;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Gain##clipgain", &gainDb, -60.0f, 12.0f, "%.1f dB"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     c->gainDb = gainDb;
                     gArrange.revision++;
                  }
               });
            }

            float pan = clip->pan;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Pan##clippan", &pan, -1.0f, 1.0f, "%.2f"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     c->pan = std::clamp(pan, -1.0f, 1.0f);
                     gArrange.revision++;
                  }
               });
            }

            float pitch = clip->pitch;
            ImGui::SetNextItemWidth(fieldW);
            if (ArrangeSliderFloat("Pitch##clippitch", &pitch, -24.0f, 24.0f, "%+.1f st"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                  {
                     // Pushed per-block onto the terminal's own sourceNode by
                     // RunTopology's lookahead (ClipWindow::pitch), not
                     // written here directly - a node can be the source of
                     // more than one clip, and writing straight to it would
                     // make one clip's pitch edit audible on every other
                     // clip sharing that node.
                     c->pitch = std::clamp(pitch, -24.0f, 24.0f);
                     gArrange.revision++;
                  }
               });
            }

            // Step 3: real, always-on BPM sync - Audio Sample only. An
            // Audio Clip (manually-patched node) has no "sample bpm"
            // concept at all, so this whole block is gated on isSample and
            // never appears for a Clip.
            if (isSample)
            {
               ImGui::Spacing();
               ImGui::TextDisabled("%s", T("Tempo Sync"));
               bool syncToTempo = clip->syncToTempo;
               if (ImGui::Checkbox(L("Sync to Tempo##clipsync"), &syncToTempo))
                  ArrangeEdit([&]() { ArrangeSetSampleSync(clipId, syncToTempo); });
               // Locked while sync is off: unsynced plays at native speed,
               // so Sample BPM would be a control that does nothing.
               if (const Arrange::Clip* ci = Arrange::FindClip(gArrange, clipId))
               {
                  const bool locked = !ci->syncToTempo;
                  ImGui::BeginDisabled(locked);
                  float sampleBpm = ci->sampleBpm;
                  if (ArrangeDragFloat("Sample BPM##clipbpm", &sampleBpm, 0.1f, 20.0f, 999.0f, "%.2f", fieldW))
                     ArrangeEdit([&]() { ArrangeSetSampleBpm(clipId, sampleBpm); });
                  if (const Arrange::Clip* cr = Arrange::FindClip(gArrange, clipId))
                     if (cr->origBpm > 0.0f && cr->origBpm != cr->sampleBpm &&
                         ImGui::SmallButton(L("Reset to Detected##clipbpmreset")))
                     {
                        const float detected = cr->origBpm;
                        ArrangeEdit([&]() { ArrangeSetSampleBpm(clipId, detected); });
                     }
                  ImGui::EndDisabled();
               }
               if (const Arrange::Clip* ci = Arrange::FindClip(gArrange, clipId))
                  ArrangeDrawSampleTempoInfo(*ci);
            }
         }

         ImGui::Spacing();
         ImGui::TextDisabled("%s", T("Color Tint"));
         drawPaletteSwatches([&](uint32_t col) {
            ArrangeEdit([&]() {
               if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
               {
                  const ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
                  c->colorR = cv.x;
                  c->colorG = cv.y;
                  c->colorB = cv.z;
                  gArrange.revision++;
               }
            });
         });

         ImGui::Spacing();
         ImGui::Separator();
         ImGui::TextDisabled("%s", T("Source Node"));
         GraphNode* srcNode = FindNodeByUid(clip->srcUid);
         if (srcNode != nullptr)
         {
            ImGui::Text(T("Node: %s"), NodeTitleWithInstance(*srcNode).c_str());
            ImGui::TextDisabled(T("Type: %s"), srcNode->typeName.c_str());
            // --- per-clip modulation bypass -----------------------------
            // Every modulation currently bound to this clip's source node,
            // each with a checkbox saying whether THIS clip wants it. The
            // node keeps the binding either way - it is the clip that opts
            // out, so the same node still arrives modulated under a clip
            // that leaves the box ticked.
            //
            // Only this node's OWN bindings are listed. A modulator sitting
            // on something upstream shapes what this node is fed, and that
            // feed is shared with every other consumer of the upstream node,
            // so a single clip cannot opt out of it without duplicating the
            // whole chain. Deliberately out of scope.
            {
               std::vector<std::pair<int, std::string>> bound; // paramIndex, label
               ArrangeCollectClipModBindings(*srcNode, bound);

               if (!bound.empty())
               {
                  ImGui::Spacing();
                  ImGui::Separator();
                  ImGui::TextDisabled("%s", T("Modulations"));
                  PushCheckboxStyle();
                  for (const auto& entry : bound)
                  {
                     const int paramIndex = entry.first;
                     // Ticked = this clip hears the modulation, which is the
                     // default and reads the right way round: an untouched
                     // clip shows every box ticked.
                     bool active = !clip->IsModBypassed(paramIndex);
                     ImGui::PushID(paramIndex);
                     if (ImGui::Checkbox(entry.second.c_str(), &active))
                     {
                        ArrangeEdit([&]() {
                           if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                           {
                              c->SetModBypassed(paramIndex, !active);
                              gArrange.revision++;
                           }
                        });
                     }
                     ImGui::PopID();
                  }
                  PopCheckboxStyle();
               }
            }

            // A Sample owns the private node its own drag-drop import
            // created - repointing or clearing that would defeat the whole
            // point of a sample (see sampleDropped's doc comment in
            // ArrangeModel.h). Only Audio/Video Clip can be reassigned.
            if (!isSample)
            {
               if (ImGui::Button(L("Assign Different Node..."), ImVec2(-FLT_MIN, 0)))
                  gArrangeAssigningClipId = clipId;
               if (ImGui::Button(L("Clear Source"), ImVec2(-FLT_MIN, 0)))
               {
                  ArrangeEdit([&]() {
                     if (Arrange::Clip* c = Arrange::FindClip(gArrange, clipId))
                     {
                        c->srcUid = 0;
                        c->srcOutput = 0;
                        gArrange.revision++;
                     }
                  });
               }
            }
         }
         else if (!isSample)
         {
            ImGui::TextDisabled("%s", T("(unassigned)"));
            if (ImGui::Button(L("Assign Node..."), ImVec2(-FLT_MIN, 0)))
               gArrangeAssigningClipId = clipId;
         }
         else
         {
            ImGui::TextDisabled("%s", T("(missing - sample's source node was deleted)"));
         }
      }
      else if (gArrangeSel.size() > 1)
      {
         ImGui::Text(T("Multiple Clips (%d)"), (int)gArrangeSel.size());
         ImGui::SameLine(availW - 18.0f);
         if (DrawCloseBtn())
            gArrangeClipSettingsPanelOpen = false;
         ImGui::Separator();

         // Multi-clip renaming
         static char sBulkRenameBuf[128] = "Clip";
         ImGui::TextDisabled("%s", T("Rename All Selected"));
         ImGui::SetNextItemWidth(availW - 55.0f);
         ImGui::InputText("##bulkrenametext", sBulkRenameBuf, sizeof(sBulkRenameBuf));
         ImGui::SameLine();
         if (ImGui::Button(L("Apply##bulkapplyrename")))
         {
            ArrangeEdit([&]() {
               int idx = 1;
               for (uint64_t id : gArrangeSel)
               {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, id))
                  {
                     c->name = std::string(sBulkRenameBuf) + " " + std::to_string(idx++);
                  }
               }
               gArrange.revision++;
            });
         }

         // Multi-clip Bypass toggle
         ImGui::Spacing();
         bool anyDisabled = false;
         for (uint64_t id : gArrangeSel)
         {
            if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
            {
               if (!c->enabled) { anyDisabled = true; break; }
            }
         }
         const bool allActive = !anyDisabled;
         if (DrawBypassButton("##bulkclipbypass", allActive, "Active (All Clips)", "Bypassed (Some/All)"))
         {
            ArrangeEdit([&]() {
               const bool setVal = anyDisabled; // if any disabled, make all active; else bypass all
               for (uint64_t id : gArrangeSel)
               {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, id))
                     c->enabled = setVal;
               }
               gArrange.revision++;
            });
         }

         ImGui::Spacing();
         ImGui::TextDisabled("%s", T("Color Tint"));
         drawPaletteSwatches([&](uint32_t col) {
            ArrangeEdit([&]() {
               const ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
               for (uint64_t id : gArrangeSel)
               {
                  if (Arrange::Clip* c = Arrange::FindClip(gArrange, id))
                  {
                     c->colorR = cv.x;
                     c->colorG = cv.y;
                     c->colorB = cv.z;
                  }
               }
               gArrange.revision++;
            });
         });

         // Ungroup, if any selected clip is actually part of a clip group -
         // same op as the clip context menu's Ungroup / Cmd+Shift+G, just
         // reachable from the panel too.
         bool anyGrouped = false;
         for (uint64_t id : gArrangeSel)
            if (const Arrange::Clip* c = Arrange::FindClip(gArrange, id))
               if (c->groupId != 0) { anyGrouped = true; break; }
         if (anyGrouped)
         {
            ImGui::Spacing();
            if (ImGui::Button(L("Ungroup"), ImVec2(-FLT_MIN, 0)))
               ArrangeUngroupSelection();
         }

         ImGui::Spacing();
         ImGui::Separator();
         if (ImGui::Button(L("Delete Selected Clips"), ImVec2(-FLT_MIN, 0)))
         {
            ArrangeEdit([&]() {
               Arrange::Delete(gArrange, std::vector<uint64_t>(gArrangeSel.begin(), gArrangeSel.end()));
            });
            gArrangeSel.clear();
         }
      }
      else if (gArrangeRowSel.size() == 1)
      {
         const uint64_t rowId = *gArrangeRowSel.begin();
         if (Arrange::Lane* lane = Arrange::FindLane(gArrange, rowId))
         {
            const bool isVideo = lane->type == Arrange::kLaneVideo;
            ImGui::TextUnformatted(isVideo ? "Video Track" : "Audio Track");
            ImGui::SameLine(availW - 18.0f);
            if (DrawCloseBtn())
               gArrangeClipSettingsPanelOpen = false;
            ImGui::Separator();

            char trackName[128];
            snprintf(trackName, sizeof(trackName), "%s", lane->name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##trackinspname", trackName, sizeof(trackName)))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                  {
                     l->name = trackName;
                     gArrange.revision++;
                  }
               });
            }

            // Dedicated Bypass button below track name
            ImGui::Spacing();
            const bool trackActive = isVideo ? (lane->opacity > 0.0f) : (!lane->mute);
            if (DrawBypassButton("##trackbypassbtn", trackActive, "Active", "Bypassed / Muted"))
            {
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                  {
                     if (isVideo)
                        l->opacity = (l->opacity > 0.0f) ? 0.0f : 1.0f;
                     else
                        l->mute = !l->mute;
                     gArrange.revision++;
                  }
               });
            }

            ImGui::Spacing();
            if (!isVideo)
            {
               ImGui::TextDisabled("%s", T("Audio Track Controls"));
               bool solo = lane->solo;
               if (ImGui::Checkbox(L("Solo##tracksolo"), &solo))
               {
                  ArrangeEdit([&]() {
                     if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                        l->solo = solo;
                  });
               }
               ImGui::SameLine();
               bool mute = lane->mute;
               if (ImGui::Checkbox(L("Mute##trackmute"), &mute))
               {
                  ArrangeEdit([&]() {
                     if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                        l->mute = mute;
                  });
               }

               float gainDb = lane->gainDb;
               ImGui::SetNextItemWidth(fieldW);
               if (ArrangeSliderFloat("Gain##trackgain", &gainDb, -60.0f, 12.0f, "%.1f dB"))
               {
                  ArrangeEdit([&]() {
                     if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                        l->gainDb = gainDb;
                  });
               }

               float pan = lane->pan;
               ImGui::SetNextItemWidth(fieldW);
               if (ArrangeSliderFloat("Pan##trackpan", &pan, -1.0f, 1.0f, "%.2f"))
               {
                  ArrangeEdit([&]() {
                     if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                        l->pan = pan;
                  });
               }
            }
            else
            {
               ImGui::TextDisabled("%s", T("Video Track Controls"));
               float opacity = lane->opacity;
               ImGui::SetNextItemWidth(fieldW);
               if (ArrangeSliderFloat("Opacity##trackop", &opacity, 0.0f, 1.0f, "%.2f"))
               {
                  ArrangeEdit([&]() {
                     if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                        l->opacity = opacity;
                  });
               }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("%s", T("Track Tint"));
            drawPaletteSwatches([&](uint32_t col) {
               ArrangeEdit([&]() {
                  if (Arrange::Lane* l = Arrange::FindLane(gArrange, rowId))
                  {
                     const ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
                     l->colorR = c.x;
                     l->colorG = c.y;
                     l->colorB = c.z;
                     gArrange.revision++;
                  }
               });
            });

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button(L("Duplicate Track"), ImVec2(-FLT_MIN, 0)))
            {
               ArrangeEdit([&]() {
                  Arrange::DuplicateLane(gArrange, rowId);
               });
            }
            if (ImGui::Button(L("Delete Track"), ImVec2(-FLT_MIN, 0)))
            {
               ArrangeEdit([&]() {
                  Arrange::RemoveLane(gArrange, rowId);
               });
               gArrangeRowSel.clear();
            }
         }
         else if (const Arrange::TrackGroup* grp = Arrange::FindTrackGroup(gArrange, rowId))
         {
            ImGui::TextUnformatted("Track Group");
            ImGui::SameLine(availW - 18.0f);
            if (DrawCloseBtn())
               gArrangeClipSettingsPanelOpen = false;
            ImGui::Separator();

            char grpName[128];
            snprintf(grpName, sizeof(grpName), "%s", grp->name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##grpinspname", grpName, sizeof(grpName)))
            {
               ArrangeEdit([&]() {
                  Arrange::RenameTrackGroup(gArrange, rowId, grpName);
               });
            }

            // Dedicated Bypass button below group name
            ImGui::Spacing();
            if (DrawBypassButton("##groupbypassbtn", grp->enabled, "Active", "Bypassed"))
            {
               ArrangeEdit([&]() {
                  Arrange::SetTrackGroupEnabled(gArrange, rowId, grp->enabled ? 0 : 1);
               });
            }

            ImGui::Spacing();
            ImGui::TextDisabled("%s", T("Group Color"));
            drawPaletteSwatches([&](uint32_t col) {
               ArrangeEdit([&]() {
                  Arrange::RecolorTrackGroup(gArrange, rowId, col);
               });
            });

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button(L("Add Video Track to Group"), ImVec2(-FLT_MIN, 0)))
            {
               ArrangeEdit([&]() {
                  const uint64_t newLaneId = Arrange::AddLane(gArrange, Arrange::kLaneVideo);
                  Arrange::SetLaneTrackGroup(gArrange, newLaneId, rowId);
               });
            }
            if (ImGui::Button(L("Add Audio Track to Group"), ImVec2(-FLT_MIN, 0)))
            {
               ArrangeEdit([&]() {
                  const uint64_t newLaneId = Arrange::AddLane(gArrange, Arrange::kLaneAudio);
                  Arrange::SetLaneTrackGroup(gArrange, newLaneId, rowId);
               });
            }
            if (ImGui::Button(L("Ungroup (Keep Tracks)"), ImVec2(-FLT_MIN, 0)))
            {
               ArrangeEdit([&]() {
                  Arrange::RemoveTrackGroup(gArrange, rowId, false);
               });
               gArrangeRowSel.clear();
            }
            if (ImGui::Button(L("Delete Group + Tracks"), ImVec2(-FLT_MIN, 0)))
            {
               ArrangeEdit([&]() {
                  Arrange::RemoveTrackGroup(gArrange, rowId, true);
               });
               gArrangeRowSel.clear();
            }
         }
         else
         {
            ImGui::TextDisabled("%s", T("No item selected."));
         }
      }
      else
      {
         ImGui::TextDisabled("%s", T("Inspector"));
         ImGui::SameLine(availW - 18.0f);
         if (DrawCloseBtn())
            gArrangeClipSettingsPanelOpen = false;
         ImGui::Separator();
         ImGui::TextDisabled("%s", T("Select a clip, track, or group to inspect its properties."));
      }

      ImGui::EndChild();
   }


   // "Job 2 of 5", for the progress dialogs. Counts every job that is not
   // already finished, so it reads as progress through the run rather than
   // through the list's history.
   void ArrangeRenderQueuePosition(int& outIndex, int& outTotal)
   {
      outIndex = 0;
      outTotal = 0;
      for (const ArrangeRenderJob& j : gArrangeRenderQueue)
      {
         const bool counts = j.status == kArrangeJobQueued || j.status == kArrangeJobRendering ||
                             j.status == kArrangeJobFinalizing;
         if (!counts)
            continue;
         outTotal++;
         if (j.id == gArrangeRenderActiveJobId)
            outIndex = outTotal;
      }
   }


   // ---- Arrangement render jobs: the runner (WP7) ---------------------------

   bool ArrangeRenderBusy()
   {
      return gOfflineRender.active || gArrangeWavRender.active;
   }


   ArrangeRenderJob* ArrangeRenderFindJob(uint64_t id)
   {
      for (ArrangeRenderJob& j : gArrangeRenderQueue)
         if (j.id == id)
            return &j;
      return nullptr;
   }


   // Seconds are derived from the live tempo at the moment the job starts, not
   // when it was queued: a job is a tick range, so re-tempoing the patch
   // between queueing and rendering changes its duration on purpose (same
   // rule the clips themselves follow, WP6).
   double ArrangeRenderTickSeconds(int64_t t)
   {
      return Arrange::TicksToSeconds((Arrange::Tick)t, std::max(1.0, (double)Transport::Instance().Tempo()));
   }


   void ArrangeRenderFailJob(ArrangeRenderJob& job, const std::string& why)
   {
      job.status = kArrangeJobFailed;
      job.message = why;
      gArrangeRenderActiveJobId = 0;
      gArrangeRenderFailNotice = "\"" + job.path + "\"\n\n" + why;
      gArrangeRenderFailNoticeOpen = true;
      fprintf(stderr, "timeline render failed: %s (%s)\n", why.c_str(), job.path.c_str());
   }


   void DrawArrangeRenderFailNotice()
   {
      if (gArrangeRenderFailNoticeOpen)
      {
         ImGui::OpenPopup(L("Render failed##arrangeRenderFail"));
         gArrangeRenderFailNoticeOpen = false;
      }
      ImGuiIO& io = ImGui::GetIO();
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                              ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f), ImVec2(560.0f, 400.0f));
      if (ImGui::BeginPopupModal(L("Render failed##arrangeRenderFail"), nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize))
      {
         ImGui::PushTextWrapPos(520.0f);
         ImGui::TextWrapped("%s", T("The timeline render did not start."));
         ImGui::Dummy(ImVec2(0, 4));
         ImGui::TextWrapped("%s", gArrangeRenderFailNotice.c_str());
         ImGui::PopTextWrapPos();
         ImGui::Dummy(ImVec2(0, 4));
         if (ImGui::Button(L("OK"), ImVec2(100, 0)))
            ImGui::CloseCurrentPopup();
         ImGui::EndPopup();
      }
   }


   // Restores everything a take borrowed. Shared by the WAV path's finish and
   // its failure exits so a half-started take can't leave the device detached
   // or the transport stuck in offline mode.
   void ArrangeWavRenderRestore()
   {
      Transport::Instance().SetOfflineMode(false);
      Transport::Instance().Seek(gArrangeWavRender.endSeconds);
      if (gArrangeWavRender.deviceWasRunning)
         StartAudioEngine(gAudioStartError);
      else
         Transport::Instance().NotifyAudioEngineStopped();
      Transport::Instance().SetPlaying(gArrangeWavRender.wasPlaying);
      SetCanvasSwapInterval(gArrangeWavRender.vsyncWasOn ? 1 : 0);
      gArrangeWavRender.active = false;
      gArrangeWavRender.timelineAudio = false;
      gArrangeWavRender.cancelRequested = false;
      gArrangeRenderActiveLaneScope.clear();
      RebuildAudioTopology();
   }


   // Audio-only take: no encoder, no frames, no OutputNode. Follows the same
   // order as StartOfflineRenderSession - warm the graph, detach the device,
   // then arm - because the live callback writes into the same graph this
   // take drives synchronously, and AudioDeviceClose is not instant.
   bool ArrangeWavRenderBegin(ArrangeRenderJob& job, double startSec, double endSec)
   {
      if (ArrangeRenderBusy())
         return false;

      const bool deviceWasRunningBefore = AudioEngine::Instance().SampleRate() > 0.0;
      if (!deviceWasRunningBefore)
      {
         // The graph's AudioNodes are only PrepareToPlay'd once a device has
         // opened at least once this session, so a cold start would render
         // silence at an unknown rate.
         if (!StartAudioEngine(gAudioStartError))
         {
            gArrangeRenderActiveLaneScope.clear();
            ArrangeRenderFailJob(job, "no audio device (" + gAudioStartError + ")");
            return false;
         }
      }

      // The file is written at the rate the graph was actually prepared at,
      // not at the job's requested rate: every AudioNode keeps generating as
      // if the device rate still applies, so muxing at anything else plays
      // back at the wrong speed (the same bug the video path's comment in
      // OutputNode::StartOfflineRender describes).
      const double rate = AudioEngine::Instance().SampleRate();
      if (!(rate > 0.0))
      {
         gArrangeRenderActiveLaneScope.clear();
         ArrangeRenderFailJob(job, "no audio device");
         return false;
      }
      AudioEngine::Instance().Stop();

      if (!gArrangeWavRender.writer.Open(job.path, rate, 2, AudioFileWriter::Format::Wav))
      {
         if (deviceWasRunningBefore)
            StartAudioEngine(gAudioStartError);
         gArrangeRenderActiveLaneScope.clear();
         ArrangeRenderFailJob(job, "could not create " + job.path);
         return false;
      }

      gArrangeWavRender.active = true;
      gArrangeWavRender.timelineAudio = job.audioSource == kArrangeAudioTimeline;
      gArrangeWavRender.cancelRequested = false;
      gArrangeWavRender.sampleRate = rate;
      gArrangeWavRender.framesTotal = ArrangeRenderSampleBudget(endSec - startSec, rate);
      gArrangeWavRender.framesDone = 0;
      gArrangeWavRender.startSeconds = startSec;
      gArrangeWavRender.endSeconds = endSec;
      gArrangeWavRender.deviceWasRunning = deviceWasRunningBefore;
      gArrangeWavRender.wasPlaying = Transport::Instance().IsPlaying();
      gArrangeWavRender.vsyncWasOn = gVsync;
      gArrangeWavRender.startedTime = glfwGetTime();
      SetCanvasSwapInterval(0);

      Transport::Instance().Seek(startSec);
      Transport::Instance().SetOfflineMode(true, rate);
      Transport::Instance().SetPlaying(true);
      RebuildAudioTopology(); // picks up timelineAudio through ArrangeTimelineRoutingActive

      job.status = kArrangeJobRendering;
      job.framesTotal = (int)std::min<long long>(gArrangeWavRender.framesTotal, (long long)2147483647);
      job.framesDone = 0;
      job.startedTime = gArrangeWavRender.startedTime;
      if (std::abs(rate - (double)job.sampleRate) > 1.0)
         job.message = "written at the device rate (" + std::to_string((int)llround(rate)) + " Hz)";
      return true;
   }


   // One main-loop slice of an audio-only take. Same ~10Hz budget as the video
   // pump, for the same reason: the progress window and its Cancel button
   // still have to repaint.
   void ArrangeWavRenderPump()
   {
      if (!gArrangeWavRender.active)
         return;

      static float sWavL[kAudioMaxBlockFrames];
      static float sWavR[kAudioMaxBlockFrames];
      static float* sWavChannels[2] = { sWavL, sWavR };
      static std::vector<float> sWavInterleave;

      const double budgetStart = glfwGetTime();
      // Same block size the live device runs at, so the take hears the graph
      // the way the user does (see OfflineAudioBlockFrames).
      const int blockCap = OfflineAudioBlockFrames();
      while (!gArrangeWavRender.cancelRequested &&
             gArrangeWavRender.framesDone < gArrangeWavRender.framesTotal)
      {
         const int blockFrames = (int)std::min<long long>(
            blockCap, gArrangeWavRender.framesTotal - gArrangeWavRender.framesDone);
         AudioBuffer buf;
         buf.channels = sWavChannels;
         buf.numChannels = 2;
         buf.numFrames = blockFrames;
         AudioEngine::Instance().ProcessOffline(buf);

         sWavInterleave.resize((size_t)blockFrames * 2);
         for (int i = 0; i < blockFrames; i++)
         {
            sWavInterleave[(size_t)i * 2 + 0] = sWavL[i];
            sWavInterleave[(size_t)i * 2 + 1] = sWavR[i];
         }
         gArrangeWavRender.writer.Append(sWavInterleave.data(), blockFrames);
         gArrangeWavRender.framesDone += blockFrames;

         // Keeps the drawn playhead and anything reading video time honest
         // while the take runs; the clip windows themselves are scheduled off
         // the audio clock ProcessOffline just advanced (WP3).
         Transport::Instance().SetOfflineVideoTime(
            gArrangeWavRender.startSeconds +
            (double)gArrangeWavRender.framesDone / gArrangeWavRender.sampleRate);

         if (glfwGetTime() - budgetStart > 0.1)
            break;
      }

      if (ArrangeRenderJob* job = ArrangeRenderFindJob(gArrangeRenderActiveJobId))
         job->framesDone = (int)std::min<long long>(gArrangeWavRender.framesDone, (long long)2147483647);

      if (!gArrangeWavRender.cancelRequested &&
          gArrangeWavRender.framesDone < gArrangeWavRender.framesTotal)
         return;

      const bool cancelled = gArrangeWavRender.cancelRequested;
      gArrangeWavRender.writer.Close();
      ArrangeWavRenderRestore();

      if (ArrangeRenderJob* job = ArrangeRenderFindJob(gArrangeRenderActiveJobId))
      {
         job->status = cancelled ? kArrangeJobCancelled : kArrangeJobDone;
         if (cancelled)
            job->message = "cancelled";
      }
      gArrangeRenderActiveJobId = 0;
   }


   bool ArrangeRenderBeginJob(ArrangeRenderJob& job)
   {
      if (ArrangeRenderBusy())
         return false;

      const double startSec = ArrangeRenderTickSeconds(job.startTick);
      const double endSec = ArrangeRenderTickSeconds(job.endTick);
      const double durSec = endSec - startSec;
      if (!(durSec > 0.0))
      {
         ArrangeRenderFailJob(job, "empty range");
         return false;
      }
      if (job.audioSource == kArrangeAudioNone && job.videoSource == kArrangeVideoNone)
      {
         ArrangeRenderFailJob(job, "nothing to render (both sources are None)");
         return false;
      }
      if (job.path.empty())
      {
         ArrangeRenderFailJob(job, "no output path");
         return false;
      }

      // The folder is typed by hand and the default (Desktop) may not exist
      // (redirected, renamed, a typo), so make it here: Media Foundation and
      // fopen both fail on a missing parent with an opaque error. FsPath keeps
      // a non-ASCII folder name intact on Windows.
      {
         std::error_code dirEc;
         const std::filesystem::path parent = AppPaths::FsPath(job.path).parent_path();
         if (!parent.empty() && !std::filesystem::is_directory(parent, dirEc))
         {
            std::filesystem::create_directories(parent, dirEc);
            if (!std::filesystem::is_directory(parent, dirEc))
            {
               ArrangeRenderFailJob(job, "could not create the folder " + parent.u8string());
               return false;
            }
         }
      }

      gArrangeRenderActiveJobId = job.id;

      // Empty scope (the common case: whole-project Render) leaves both
      // RebuildAudioTopology and CollectArrangeVideoLayers unfiltered.
      gArrangeRenderActiveLaneScope.clear();
      gArrangeRenderActiveLaneScope.insert(job.laneScope.begin(), job.laneScope.end());

      if (job.videoSource == kArrangeVideoNone)
         return ArrangeWavRenderBegin(job, startSec, endSec);

      OutputNode* rn = nullptr;
      if (job.videoSource == kArrangeVideoTimeline)
      {
         if (!gArrangeTimelineExportNode)
            gArrangeTimelineExportNode = std::make_unique<OutputNode>();
         rn = gArrangeTimelineExportNode.get();
      }
      else
      {
         GraphNode* gn = FindNodeByUid(job.canvasVideoUid);
         rn = gn != nullptr ? dynamic_cast<OutputNode*>(gn->node.get()) : nullptr;
         if (rn == nullptr)
         {
            ArrangeRenderFailJob(job, "the job's canvas Output node is gone");
            return false;
         }
      }

      rn->recordVideoPath = job.path;
      rn->videoFormat = job.format == 1 ? 1 : 0;
      rn->offlineFps = job.fps;
      // Both are set: the override is what the take actually uses (WP7 #2),
      // the seconds keep the node's own params readable if the user opens it.
      rn->offlineDurationSeconds = std::clamp((int)std::ceil(durSec), 1, 3600);
      rn->offlineTotalFramesOverride = ArrangeRenderFrameBudget(durSec, job.fps);
      rn->includeAudio = job.audioSource != kArrangeAudioNone;

      // Set before arming: StartOfflineRenderSession's refusal reads the range
      // and RebuildAudioTopology (its last line) reads the routing flags.
      Transport::Instance().Seek(startSec);
      gOfflineRender.arrangeDriven = true;
      gOfflineRender.timelineVideo = job.videoSource == kArrangeVideoTimeline;
      gOfflineRender.timelineAudio = job.audioSource == kArrangeAudioTimeline;
      gOfflineRender.rangeStartTick = job.startTick;
      gOfflineRender.rangeEndTick = job.endTick;
      gOfflineRender.endSeconds = endSec;
      StartOfflineRenderSession(rn, job.width, job.height, true /* isArrange */);

      if (!gOfflineRender.active)
      {
         // Refused (hardware source, nothing connected, another take). Put the
         // flags back so the next job starts from a clean slate.
         gOfflineRender.arrangeDriven = false;
         gOfflineRender.timelineVideo = false;
         gOfflineRender.timelineAudio = false;
         gArrangeRenderActiveLaneScope.clear();
         ArrangeRenderFailJob(job, rn->RecordStatus().empty() ? "could not start the take" : rn->RecordStatus());
         return false;
      }

      job.status = kArrangeJobRendering;
      job.framesTotal = rn->OfflineFramesTotal();
      job.framesDone = 0;
      job.startedTime = glfwGetTime();
      return true;
   }


   void ArrangeRenderCancelActive()
   {
      if (gArrangeWavRender.active)
      {
         gArrangeWavRender.cancelRequested = true;
         return;
      }
      if (gOfflineRender.active && gOfflineRender.node != nullptr)
         gOfflineRender.node->RequestFinishOfflineRender(true);
   }


   // Called once a frame from the main loop, after the offline pump has had
   // its slice. Notices a finished video take and starts the next queued job.
   void ArrangeRenderQueueTick()
   {
      ArrangeWavRenderPump();

      if (gArrangeRenderActiveJobId != 0 && !ArrangeRenderBusy())
      {
         // A video take just finished (the pump's teardown cleared .active).
         // The WAV path settles its own job inside ArrangeWavRenderPump.
         if (ArrangeRenderJob* job = ArrangeRenderFindJob(gArrangeRenderActiveJobId))
         {
            if (job->status == kArrangeJobRendering || job->status == kArrangeJobFinalizing)
            {
               const bool cancelled = job->framesDone < job->framesTotal;
               job->status = cancelled ? kArrangeJobCancelled : kArrangeJobDone;
               if (cancelled)
                  job->message = "cancelled";
               else
               {
                  // A take that ran to its last frame but left no file (the
                  // encoder rejected the stream, or finalizing failed) must
                  // not read as Done.
                  std::error_code sizeEc;
                  const std::filesystem::path outPath = AppPaths::FsPath(job->path);
                  const bool wrote = std::filesystem::exists(outPath, sizeEc) &&
                                     std::filesystem::file_size(outPath, sizeEc) > 0;
                  if (!wrote)
                     ArrangeRenderFailJob(*job, "the encoder finished but no file was written");
               }
            }
         }
         gArrangeRenderActiveJobId = 0;
      }
      else if (gArrangeRenderActiveJobId != 0 && gOfflineRender.active)
      {
         if (ArrangeRenderJob* job = ArrangeRenderFindJob(gArrangeRenderActiveJobId))
         {
            OutputNode* on = gOfflineRender.node;
            if (on != nullptr)
            {
               job->framesDone = on->OfflineFramesDone();
               job->framesTotal = on->OfflineFramesTotal();
               job->status = on->IsOfflineFinalizing() ? kArrangeJobFinalizing : kArrangeJobRendering;
            }
         }
      }

      if (!gArrangeRenderQueueRunning || ArrangeRenderBusy() || gArrangeRenderActiveJobId != 0)
         return;

      for (ArrangeRenderJob& j : gArrangeRenderQueue)
      {
         if (j.status != kArrangeJobQueued)
            continue;
         ArrangeRenderBeginJob(j); // failure marks the job and falls through to the next
         return;
      }
      gArrangeRenderQueueRunning = false;
   }
}

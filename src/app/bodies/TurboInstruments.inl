// Part of src/main.cpp, included there in order (one translation unit).
// Looper, MPC, Super Mixer, Analog, Slicer, Molders, Transport Control

   // ======================================================================
   // Infinite-Turbo nodes: Looper, MPC, MPC Out, Super Mixer
   // ======================================================================

   void DrawLooperStrip(LooperNode* n, float h)
   {
      const float w = gAudioBodyW;
      const ImVec2 o = ImGui::GetCursorScreenPos();
      const ImVec2 br(o.x + w, o.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(o, br, ScopeBgCol(), 4.0f);
      const int st = n->CurrentState();
      const ImU32 col = st == LooperNode::kRecording || st == LooperNode::kArmed ? IM_COL32(230, 70, 70, 255)
                      : st == LooperNode::kOverdubbing                             ? IM_COL32(240, 160, 60, 255)
                                                                                    : IM_COL32(90, 200, 140, 255);
      if (st == LooperNode::kRecording)
      {
         dl->AddRectFilled(ImVec2(o.x + 2.0f, o.y + 2.0f), ImVec2(br.x - 2.0f, br.y - 2.0f), IM_COL32(230, 70, 70, 60), 3.0f);
      }
      else if (st == LooperNode::kArmed)
      {
         char armed[96];
         snprintf(armed, sizeof(armed), "armed - starts on the next %s in %.2f s",
                  n->lengthMode == LooperNode::kLengthSubBar     ? "sub-bar"
                  : n->lengthMode == LooperNode::kLengthDivision ? "grid line"
                                                                 : "bar",
                  n->ArmedSeconds());
         dl->AddText(ImVec2(o.x + 10.0f, o.y + h * 0.5f - 7.0f), col, armed);
      }
      else if (n->HasLoop())
      {
         const float x = o.x + 2.0f + (w - 4.0f) * std::clamp(n->Position01(), 0.0f, 1.0f);
         dl->AddRectFilled(ImVec2(o.x + 2.0f, o.y + 2.0f), ImVec2(x, br.y - 2.0f),
                           (col & 0x00FFFFFF) | 0x50000000, 3.0f);
         dl->AddLine(ImVec2(x, o.y + 2.0f), ImVec2(x, br.y - 2.0f), col, 2.0f);
         // Turbo 0.49: same cursor head as the sample nodes' play cursors.
         dl->AddTriangleFilled(ImVec2(x - 5.0f, o.y + 2.0f), ImVec2(x + 5.0f, o.y + 2.0f), ImVec2(x, o.y + 8.0f), col);
      }
      else
      {
         dl->AddText(ImVec2(o.x + 10.0f, o.y + h * 0.5f - 7.0f), IM_COL32(130, 136, 156, 255),
                     "empty - press REC to record a loop");
      }
      dl->AddRect(o, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawLooperBody(GraphNode& gn, LooperNode* n)
   {
      char stat[96];
      if (n->CurrentState() == LooperNode::kRecording)
         snprintf(stat, sizeof(stat), "recording %.1f s", n->RecordedSeconds());
      else if (n->HasLoop())
         snprintf(stat, sizeof(stat), "%s - loop %.2f s", LooperNode::StateName(n->CurrentState()), n->LengthSeconds());
      else
         snprintf(stat, sizeof(stat), "%s - up to %.0f s", LooperNode::StateName(n->CurrentState()), LooperNode::MaxSeconds());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      DrawLooperStrip(n, 34.0f);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Transport. Every button is a modulatable widget: each gets a CV pin,
      // so a MIDI controller (via a MIDI CC / Note modulator) can drive it.
      {
         const float spacing = ImGui::GetStyle().ItemSpacing.x;
         const float bw = (gAudioContentW - 3.0f * spacing) / 4.0f;
         const ImVec2 size(bw, 30.0f);
         bool requested = false;

         ImGui::PushStyleColor(ImGuiCol_Button, n->IsRecordingOrArmed() ? ImVec4(0.75f, 0.18f, 0.18f, 1.0f)
                                                                         : ImVec4(0.30f, 0.12f, 0.12f, 1.0f));
         // Turbo 0.48: each press (click or CV rising edge) is resolved on
         // the audio thread, so repeated Macro Triggers fire every time.
         if (ModStateButton("REC##looperRec", n->IsRecordingOrArmed(), requested, size, /*toggleOnRise=*/true))
            n->PressRecord();
         ImGui::PopStyleColor();
         ImGui::SameLine();

         // A quantized PLAY waiting for its line blinks.
         const bool playWaiting = n->PlayWaitSeconds() > 0.0f && std::fmod(ImGui::GetTime(), 0.5) < 0.25;
         ImGui::PushStyleColor(ImGuiCol_Button, (n->IsPlaying() || playWaiting) ? ImVec4(0.20f, 0.55f, 0.32f, 1.0f)
                                                                                : ImVec4(0.12f, 0.24f, 0.16f, 1.0f));
         if (ModStateButton("PLAY##looperPlay", n->IsPlaying(), requested, size, /*toggleOnRise=*/true))
            n->PressPlay();
         ImGui::PopStyleColor();
         ImGui::SameLine();

         ImGui::PushStyleColor(ImGuiCol_Button, n->IsOverdubbing() ? ImVec4(0.80f, 0.50f, 0.15f, 1.0f)
                                                                   : ImVec4(0.30f, 0.20f, 0.10f, 1.0f));
         if (ModStateButton("DUB##looperDub", n->IsOverdubbing(), requested, size, /*toggleOnRise=*/true))
            n->PressOverdub();
         ImGui::PopStyleColor();
         ImGui::SameLine();

         if (ModTriggerButton("CLEAR##looperClear", size))
            n->Clear();

         // Layer history + export (Turbo). UNDO/REDO have CV pins too.
         const ImVec2 small(bw, 24.0f);
         const bool canUndo = n->CanUndoLayer();
         const bool canRedo = n->CanRedoLayer();
         if (!canUndo)
            ImGui::BeginDisabled();
         if (ModTriggerButton("UNDO##looperUndo", small))
            n->UndoLayer();
         if (!canUndo)
            ImGui::EndDisabled();
         ImGui::SameLine();
         if (!canRedo)
            ImGui::BeginDisabled();
         if (ModTriggerButton("REDO##looperRedo", small))
            n->RedoLayer();
         if (!canRedo)
            ImGui::EndDisabled();
         ImGui::SameLine();
         const bool canExport = n->HasLoop() && !n->IsRecordingOrArmed();
         if (!canExport)
            ImGui::BeginDisabled();
         if (ImGui::Button("EXPORT WAV##looperExport", ImVec2(2.0f * bw + spacing, small.y)))
         {
            StartNodeFileDialog(n, []() { return Platform::SaveAudioDialog("loop.wav"); },
                                [](LooperNode* m, const std::string& path) {
                                   std::string error;
                                   m->ExportWav(path, error);
                                });
         }
         if (!canExport)
            ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("write the loop (all layers summed) to a 32-bit float WAV");
         if (n->HistoryCount() > 1 || !n->StatusText().empty())
         {
            if (!n->StatusText().empty())
               ImGui::TextDisabled("layer %d/%d  -  %s", n->HistoryIndex(), n->HistoryCount() - 1, n->StatusText().c_str());
            else
               ImGui::TextDisabled("layer %d/%d", n->HistoryIndex(), n->HistoryCount() - 1);
         }
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Turbo 0.48: one "take length" menu instead of mode + bars + sub-bar +
      // division controls. It writes the same saved keys (lengthMode, bars,
      // subDivision); a Division take (RPC / older 0.48 patches) shows as the
      // last entry.
      BeginAudioSection("take length");
      {
         struct LenChoice { const char* name; int mode; int bars; int sub; };
         static const LenChoice kChoices[] = {
            { "free: press REC again to close", LooperNode::kLengthFree, 0, 0 },
            { "1/16 bar", LooperNode::kLengthSubBar, 0, 3 },
            { "1/8 bar", LooperNode::kLengthSubBar, 0, 2 },
            { "1/4 bar (1 beat)", LooperNode::kLengthSubBar, 0, 1 },
            { "1/2 bar", LooperNode::kLengthSubBar, 0, 0 },
            { "1 bar", LooperNode::kLengthBars, 1, 0 },
            { "2 bars", LooperNode::kLengthBars, 2, 0 },
            { "3 bars", LooperNode::kLengthBars, 3, 0 },
            { "4 bars", LooperNode::kLengthBars, 4, 0 },
            { "6 bars", LooperNode::kLengthBars, 6, 0 },
            { "8 bars", LooperNode::kLengthBars, 8, 0 },
            { "12 bars", LooperNode::kLengthBars, 12, 0 },
            { "16 bars", LooperNode::kLengthBars, 16, 0 },
            { "24 bars", LooperNode::kLengthBars, 24, 0 },
            { "32 bars", LooperNode::kLengthBars, 32, 0 },
         };
         constexpr int kNumChoices = (int)(sizeof(kChoices) / sizeof(kChoices[0]));
         int current = -1;
         for (int c = 0; c < kNumChoices && current < 0; c++)
         {
            const LenChoice& ch = kChoices[c];
            if (ch.mode != n->lengthMode)
               continue;
            if ((ch.mode == LooperNode::kLengthFree) || (ch.mode == LooperNode::kLengthBars && ch.bars == n->bars) ||
                (ch.mode == LooperNode::kLengthSubBar && ch.sub == n->subDivision))
               current = c;
         }
         // The last entry is whatever the menu has no row for (5 bars, a division).
         std::vector<std::string> names;
         for (const LenChoice& ch : kChoices)
            names.push_back(ch.name);
         char other[64];
         if (n->lengthMode == LooperNode::kLengthDivision)
            snprintf(other, sizeof(other), "%s (division)", MusicTime::RateDivisionName(n->takeDivision));
         else if (current < 0 && n->lengthMode == LooperNode::kLengthBars)
            snprintf(other, sizeof(other), "%d bars", n->bars);
         else
            snprintf(other, sizeof(other), "other (set from MCP)");
         names.push_back(other);
         if (current < 0)
            current = kNumChoices;
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         ImGui::TextDisabled("length");
         ImGui::SameLine();
         AudioBareDropdown("length##looperLen", names, current,
                           [n](int c) {
                              if (c < 0 || c >= (int)(sizeof(kChoices) / sizeof(kChoices[0])))
                                 return; // the "other" row keeps what is set
                              PushUndoCheckpoint();
                              n->lengthMode = kChoices[c].mode;
                              if (kChoices[c].mode == LooperNode::kLengthBars)
                                 n->bars = kChoices[c].bars;
                              if (kChoices[c].mode == LooperNode::kLengthSubBar)
                                 n->subDivision = kChoices[c].sub;
                           },
                           gAudioContentW - ImGui::CalcTextSize("length").x - gap, {}, true);
         // The old "bars" knob had a positional pin: keep its slot so the
         // knobs below keep their modulation pin numbers in saved patches.
         gParamCounter++;
      }
      if (AudioToggleButton("in time##looperSync", &n->syncStart, 90.0f))
         PushUndoCheckpoint();
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("REC, PLAY and DUB wait for the next bar line (or the take length, if shorter)\n"
                           "while the transport plays; pressed just after a line they start at once, in time");
      ImGui::SameLine();
      if (AudioToggleButton("loop##looperLoop", &n->loop, 70.0f))
         PushUndoCheckpoint();
      if (n->PlayWaitSeconds() > 0.0f)
      {
         ImGui::SameLine();
         ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "play in %.1f s", n->PlayWaitSeconds());
      }
      else if (n->CurrentState() == LooperNode::kArmed)
      {
         ImGui::SameLine();
         ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "rec in %.1f s", n->ArmedSeconds());
      }
      EndAudioSection();

      BeginAudioSection("playback");
      {
         static const std::vector<std::string> kDirs = { "Forward", "Reverse", "Ping-pong" };
         AudioKnobRow row(3, kKnobStd);
         row.Dropdown("direction##looperDir", kDirs, n->direction,
                      [n](int v) { PushUndoCheckpoint(); n->direction = v; });
         row.Knob("thru", &n->thru, 0.0f, 1.0f, "%.2f");
         row.Knob("level", &n->level, 0.0f, 1.5f, "%.2f");
         row.End();
      }
      EndAudioSection();

      BeginAudioSection("latency");
      {
         if (AudioToggleButton("auto##looperAutoLat", &n->autoLatency, 60.0f))
            PushUndoCheckpoint();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("shift each take by the round-trip latency the audio driver reports");
         ImGui::SameLine();
         ImGui::TextDisabled("comp %.1f ms", n->CompensationMs());
         AudioKnobRow row(1, kKnobStd);
         row.Knob("offset ms", &n->latencyOffsetMs, -100.0f, 300.0f, "%+.0f ms");
         row.End();
      }
      EndAudioSection();

      // Turbo 0.48 (from upstream): varispeed playback, per-pass fades and
      // output volume. Drawn last so the knobs above keep their pin numbers.
      BeginAudioSection(n->AtUnity() ? "speed / fades" : "speed / fades - drifts, overdub paused");
      {
         AudioKnobRow row(3, kKnobStd);
         row.Knob("speed", &n->speed, -2.0f, 2.0f, "%.2fx");
         row.Knob("pitch", &n->pitch, -24.0f, 24.0f, "%+.1f st");
         row.Knob("fine", &n->finetune, -50.0f, 50.0f, "%+.0f ct");
         row.End();
      }
      {
         AudioKnobRow row(3, kKnobStd);
         row.Knob("fade in", &n->fadeIn, 0.0f, 250.0f, "%.0f ms");
         row.Knob("fade out", &n->fadeOut, 0.0f, 250.0f, "%.0f ms");
         row.Knob("volume", &n->volume, 0.0f, 2.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }

   // Waveform with draggable trim handles: click/drag near a handle moves it
   // (the nearest one), the dimmed parts are outside the played range.
   void DrawMpcPadWave(MpcNode* n, int p, float w, float h)
   {
      const ImVec2 o = ImGui::GetCursorScreenPos();
      const ImVec2 br(o.x + w, o.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(o, br, ScopeBgCol(), 3.0f);
      const WavePeaks& peaks = n->padPeaks[p];
      if (n->padWaveCount[p] > 1 && !peaks.Empty())
      {
         // Turbo 0.49: shared view (zoom-to-trim, per-pixel peaks, cursor,
         // time label, Shift = fine). Click/drag anywhere still moves the
         // nearest handle, as before.
         WaveView view = WaveViewBegin("##mpcwave", o, w, h, n->padStart[p], n->padEnd[p], peaks.NumFrames());
         const bool isLight = IsThemeLight();
         dl->PushClipRect(o, br, true);
         DrawWavePeaks(dl, view, peaks, n->padStart[p], n->padEnd[p],
                       isLight ? IM_COL32(30, 110, 230, 230) : IM_COL32(120, 190, 255, 230));
         DrawPlayCursor(dl, view, n->PadPlayPosition(p), PlayCursorColor());
         const ImU32 startCol = TrimStartCol();
         const ImU32 endCol = TrimEndCol();
         const float xs = view.X(std::clamp(n->padStart[p], 0.0f, 1.0f));
         const float xe = view.X(std::clamp(n->padEnd[p], 0.0f, 1.0f));
         dl->AddLine(ImVec2(xs, o.y), ImVec2(xs, br.y), startCol, 2.0f);
         dl->AddLine(ImVec2(xe, o.y), ImVec2(xe, br.y), endCol, 2.0f);
         dl->AddTriangleFilled(ImVec2(xs, o.y), ImVec2(xs + 7.0f, o.y), ImVec2(xs, o.y + 7.0f), startCol);
         dl->AddTriangleFilled(ImVec2(xe, o.y), ImVec2(xe - 7.0f, o.y), ImVec2(xe, o.y + 7.0f), endCol);
         dl->PopClipRect();

         // Chip first: the full-strip trim button below has no overlap
         // opt-in, so the chip keeps its clicks.
         WaveZoomChip(view);
         ImGui::SetCursorScreenPos(o);
         ImGui::InvisibleButton("##mpcTrim", ImVec2(w, h));
         static int sDragHandle = -1; // 0 = start, 1 = end
         const bool activated = ImGui::IsItemActivated();
         if (activated)
         {
            PushUndoCheckpoint();
            const float mx = ImGui::GetIO().MousePos.x;
            sDragHandle = std::fabs(mx - xs) <= std::fabs(mx - xe) ? 0 : 1;
         }
         if (ImGui::IsItemActive() && sDragHandle >= 0)
         {
            float* val = sDragHandle == 0 ? &n->padStart[p] : &n->padEnd[p];
            const float t = std::clamp(TrimDragValue(ImGui::GetItemID(), view, *val, activated, true), 0.0f, 1.0f);
            if (sDragHandle == 0)
               n->padStart[p] = std::min(t, n->padEnd[p] - 0.001f);
            else
               n->padEnd[p] = std::max(t, n->padStart[p] + 0.001f);
            DrawTrimTimeLabel(dl, view, *val, &peaks, sDragHandle == 0 ? startCol : endCol);
         }
         ImGui::GetStateStorage()->SetBool(view.keyDrag, ImGui::IsItemActive() && sDragHandle >= 0);
         if (ImGui::IsItemDeactivated())
            sDragHandle = -1;
         if (ImGui::IsItemHovered() || ImGui::IsItemActive())
         {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            char t0[32], t1[32], line[96];
            FormatTrimTime(t0, sizeof(t0), n->padStart[p], &peaks);
            FormatTrimTime(t1, sizeof(t1), n->padEnd[p], &peaks);
            snprintf(line, sizeof(line), "trim %s..%s - drag moves the nearest edge, shift: fine", t0, t1);
            SetAudioReadout("#", line);
         }
         ImGui::SetCursorScreenPos(o);
         ImGui::Dummy(ImVec2(w, h));
      }
      else
      {
         dl->AddText(ImVec2(o.x + 8.0f, o.y + h * 0.5f - 7.0f), IM_COL32(130, 136, 156, 255), "no sample");
         ImGui::Dummy(ImVec2(w, h));
      }
      dl->AddRect(o, br, ScopeBorderCol(), 3.0f);
   }

   // Turbo 0.49: each MPC's pad grid in canvas space (origin, pitch), so a
   // sample dragged from the library panel lands on the pad under the
   // cursor. Each rect carries the frame it was drawn in: only a recent one
   // is trusted (a deleted node's address can be reused by a new MPC), and
   // stale entries are pruned once per frame.
   struct MpcPadGridRect
   {
      ImVec4 rect;   // origin x/y, column/row pitch
      int frame = 0; // ImGui::GetFrameCount() when drawn
   };
   std::unordered_map<const MpcNode*, MpcPadGridRect> gMpcPadGridCanvas;

   void PruneMpcPadGrids()
   {
      static int sLastPrune = -1;
      const int frame = ImGui::GetFrameCount();
      if (frame == sLastPrune)
         return;
      sLastPrune = frame;
      for (auto it = gMpcPadGridCanvas.begin(); it != gMpcPadGridCanvas.end();)
         it = (frame - it->second.frame > 2) ? gMpcPadGridCanvas.erase(it) : std::next(it);
   }

   int MpcPadForCanvasPos(const MpcNode* n, float x, float y)
   {
      auto it = gMpcPadGridCanvas.find(n);
      const ImVec4* r = nullptr;
      if (it != gMpcPadGridCanvas.end() && ImGui::GetFrameCount() - it->second.frame <= 2)
         r = &it->second.rect;
      if (r != nullptr && r->z > 0.0f && r->w > 0.0f)
      {
         const int col = (int)std::floor((x - r->x) / r->z);
         const int row = (int)std::floor((y - r->y) / r->w);
         if (col >= 0 && col < 4 && row >= 0 && row < 4)
            return (3 - row) * 4 + col;
      }
      return std::clamp(n->selectedPad, 0, MpcNode::kPads - 1);
   }

   void DrawMpcBody(GraphNode& gn, MpcNode* n)
   {
      int loaded = 0, playing = 0;
      for (int p = 0; p < MpcNode::kPads; p++)
      {
         loaded += n->PadLoaded(p) ? 1 : 0;
         playing += n->PadPlaying(p) ? 1 : 0;
      }
      char stat[96];
      snprintf(stat, sizeof(stat), "%d/16 loaded - %d playing - notes %d-%d", loaded, playing, n->baseNote,
               n->baseNote + MpcNode::kPads - 1);
      BeginAudioBody(gn.index, gn.category, 560.0f, stat);

      // 4x4 pads, pad 1 bottom-left like the hardware. Each pad is a gate
      // button with its own CV pin: gate high = pad held.
      {
         static const char* kModeTag[3] = { "one shot", "gate", "loop" };
         const float gap = 6.0f;
         const float padW = (gAudioContentW - gap * 3.0f) / 4.0f;
         const float padH = 58.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         PruneMpcPadGrids();
         gMpcPadGridCanvas[n] = { ImVec4(origin.x, origin.y, padW + gap, padH + gap), ImGui::GetFrameCount() }; // Turbo 0.49
         for (int row = 0; row < 4; row++)
         {
            for (int col = 0; col < 4; col++)
            {
               const int p = (3 - row) * 4 + col;
               ImGui::SetCursorScreenPos(ImVec2(origin.x + (float)col * (padW + gap), origin.y + (float)row * (padH + gap)));
               ImGui::PushID(p);
               char label[32];
               snprintf(label, sizeof(label), "%d##mpcPad%d", p + 1, p + 1);
               std::string sub = n->PadLoaded(p) ? n->PadName(p) : std::string("empty");
               if (sub.size() > 14)
                  sub = sub.substr(0, 13) + ".";
               sub += std::string("\n") + kModeTag[std::clamp(n->padMode[p], 0, 2)];
               bool clicked = false;
               const bool held = ModGateButton(label, ImVec2(padW, padH), n->PadPlaying(p), p == n->selectedPad,
                                               n->PadLoaded(p), sub.c_str(), clicked);
               if (clicked)
                  n->selectedPad = p;
               if (held != n->uiHeld[p])
               {
                  n->uiHeld[p] = held;
                  n->PadEvent(p, held, 1.0f);
               }
               ImGui::PopID();
            }
         }
         ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 4.0f * (padH + gap)));
         ImGui::Dummy(ImVec2(gAudioContentW, 2.0f));
      }

      // Selected pad editor. Plain widgets on purpose: they follow the
      // selection, and a CV binding that jumped between pads would be
      // confusing. The pads themselves carry the CV pins.
      const int p = std::clamp(n->selectedPad, 0, MpcNode::kPads - 1);
      char header[96];
      snprintf(header, sizeof(header), "pad %d - %s", p + 1, n->PadLoaded(p) ? n->PadName(p).c_str() : "empty");
      BeginAudioSection(header);
      ImGui::PushID(1000 + p);
      if (ImGui::Button("Load sample..."))
      {
         StartNodeFileDialog(n, Platform::OpenAudioDialog, [p](auto* m, const std::string& path) {
            PushUndoCheckpoint();
            if (m->LoadPad(p, path))
            {
               m->padStart[p] = 0.0f;
               m->padEnd[p] = 1.0f;
            }
         });
      }
      ImGui::SameLine();
      if (ImGui::Button("Load folder..."))
      {
         StartNodeFileDialog(n, [] { return Platform::OpenFolderDialog("Load a folder into the 16 pads"); },
                             [](auto* m, const std::string& folder) {
                                PushUndoCheckpoint();
                                m->LoadFolder(folder);
                             });
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear pad"))
      {
         PushUndoCheckpoint();
         n->ClearPad(p);
      }
      if (!n->PadStatus(p).empty() && n->PadStatus(p) != "loaded" && n->PadStatus(p) != "empty")
         ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", n->PadStatus(p).c_str());
      DrawMpcPadWave(n, p, gAudioContentW, 56.0f);

      int mode = std::clamp(n->padMode[p], 0, 2);
      if (ImGui::RadioButton("one shot", mode == MpcNode::kOneShot)) { PushUndoCheckpoint(); n->padMode[p] = MpcNode::kOneShot; }
      ImGui::SameLine();
      if (ImGui::RadioButton("gate (hold)", mode == MpcNode::kGate)) { PushUndoCheckpoint(); n->padMode[p] = MpcNode::kGate; }
      ImGui::SameLine();
      if (ImGui::RadioButton("loop (toggle)", mode == MpcNode::kLoopToggle)) { PushUndoCheckpoint(); n->padMode[p] = MpcNode::kLoopToggle; }

      // Drag fields: drag to change, double-click (or Ctrl+click) to type.
      const ImGuiSliderFlags clampFlags = ImGuiSliderFlags_AlwaysClamp;
      const float half = (gAudioContentW - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
      const float third = (gAudioContentW - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
      ImGui::SetNextItemWidth(half);
      ImGui::DragFloat("##mpcStart", &n->padStart[p], 0.001f, 0.0f, 1.0f, "trim in %.3f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(half);
      ImGui::DragFloat("##mpcEnd", &n->padEnd[p], 0.001f, 0.0f, 1.0f, "trim out %.3f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      n->padEnd[p] = std::max(n->padEnd[p], n->padStart[p] + 0.001f);
      ImGui::SetNextItemWidth(third);
      ImGui::DragFloat("##mpcVol", &n->padVolume[p], 0.005f, 0.0f, 1.5f, "vol %.2f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(third);
      ImGui::DragFloat("##mpcPitch", &n->padPitch[p], 0.05f, -24.0f, 24.0f, "pitch %.2f st", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(third);
      ImGui::DragFloat("##mpcPan", &n->padPan[p], 0.005f, -1.0f, 1.0f, "pan %.2f", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      // Turbo 0.48 (from upstream): speed, fine tune, per-pass fades, sync.
      const float quarter = (gAudioContentW - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
      ImGui::SetNextItemWidth(quarter);
      ImGui::DragFloat("##mpcSpeed", &n->padSpeed[p], 0.005f, -2.0f, 2.0f, "speed %.2fx", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(quarter);
      ImGui::DragFloat("##mpcFine", &n->padFine[p], 0.25f, -50.0f, 50.0f, "fine %+.0f ct", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(quarter);
      ImGui::DragFloat("##mpcFadeIn", &n->padFadeIn[p], 0.5f, 0.0f, 250.0f, "in %.0f ms", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::SetNextItemWidth(quarter);
      ImGui::DragFloat("##mpcFadeOut", &n->padFadeOut[p], 0.5f, 0.0f, 250.0f, "out %.0f ms", clampFlags);
      if (ImGui::IsItemActivated()) PushUndoCheckpoint();
      bool synced = n->padSync[p] == MpcNode::kSynced;
      if (ImGui::Checkbox("sync to##mpcSync", &synced))
      {
         PushUndoCheckpoint();
         n->padSync[p] = synced ? MpcNode::kSynced : MpcNode::kFree;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("a hit waits for the next line of this division (loop: one pass per division)");
      ImGui::SameLine();
      if (!synced)
         ImGui::BeginDisabled();
      const std::vector<std::string>& divs = MusicTime::RateDivisionList();
      const int div = std::clamp(n->padDiv[p], 0, (int)divs.size() - 1);
      NodeDropdown("mpcDiv", divs, div, [n, p](int d) {
         PushUndoCheckpoint();
         n->padDiv[p] = d;
      }, 90.0f);
      if (!synced)
         ImGui::EndDisabled();
      ImGui::PopID();
      EndAudioSection();

      BeginAudioSection("master");
      {
         AudioKnobRow row(2, kKnobStd);
         row.Knob("volume", &n->volume, 0.0f, 1.5f, "%.2f");
         row.KnobInt("base note", &n->baseNote, 0, 112);
         row.End();
      }
      if (AudioToggleButton("velocity##mpcVel", &n->velocitySensitive, 100.0f))
         PushUndoCheckpoint();
      ImGui::SameLine();
      ImGui::TextDisabled("pad outs: add an MPC Out node");
      EndAudioSection();

      EndAudioBody();
   }

   void DrawMpcOutBody(GraphNode& gn, MpcOutNode* n)
   {
      char stat[64];
      if (n->ConnectedToMpc())
         snprintf(stat, sizeof(stat), "pad %d", std::clamp(n->pad, 0, 15) + 1);
      else
         snprintf(stat, sizeof(stat), "wire an MPC into it");
      BeginAudioBody(gn.index, gn.category, 260.0f, stat);
      static const std::vector<std::string> kPads = { "pad 1", "pad 2", "pad 3", "pad 4", "pad 5", "pad 6",
                                                      "pad 7", "pad 8", "pad 9", "pad 10", "pad 11", "pad 12",
                                                      "pad 13", "pad 14", "pad 15", "pad 16" };
      AudioKnobRow row(2, kKnobStd);
      row.Dropdown("pad##mpcOutPad", kPads, n->pad, [n](int v) { PushUndoCheckpoint(); n->pad = v; });
      row.Knob("gain", &n->gainDb, -60.0f, 12.0f, "%.1f dB", kKnobStd, /*dbTaper=*/true);
      row.End();
      EndAudioBody();
   }

   // Turbo 0.50: the Super Mixer's master meter. Stereo bars on an IEC 60268-18
   // style scale (more room at the top, where mixing decisions happen): RMS
   // solid, peak lighter behind it, a 1.5 s peak-hold tick, and clip LEDs
   // that latch until clicked. Below: numeric readouts and the compressor
   // and limiter gain-reduction bars.
   void DrawSuperMixerMasterMeter(SuperMixerNode* n, float x, float y, float w, float h)
   {
      const SuperMixerNode::MasterMeter& m = n->Meter();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 dimCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const float lineH = ImGui::GetTextLineHeight();

      auto iecFrac = [](float db) {
         float pct;
         if (db < -70.0f)
            pct = 0.0f;
         else if (db < -60.0f)
            pct = (db + 70.0f) * 0.25f;
         else if (db < -50.0f)
            pct = (db + 60.0f) * 0.5f + 2.5f;
         else if (db < -40.0f)
            pct = (db + 50.0f) * 0.75f + 7.5f;
         else if (db < -30.0f)
            pct = (db + 40.0f) * 1.5f + 15.0f;
         else if (db < -20.0f)
            pct = (db + 30.0f) * 2.0f + 30.0f;
         else
            pct = (db + 20.0f) * 2.5f + 50.0f;
         return std::clamp(pct / 100.0f, 0.0f, 1.0f);
      };
      auto zoneCol = [](float db, int alpha) {
         return db >= -1.0f  ? IM_COL32(255, 80, 70, alpha)
              : db >= -6.0f  ? IM_COL32(255, 175, 70, alpha)
              : db >= -18.0f ? IM_COL32(220, 220, 90, alpha)
                             : IM_COL32(110, 205, 150, alpha);
      };

      const float labelW = 14.0f, ledW = 16.0f, barH = 13.0f, gap = 3.0f;
      const float bx0 = x + labelW, bx1 = x + w - ledW - 4.0f, bw = bx1 - bx0;
      for (int ch = 0; ch < 2; ch++)
      {
         const float by = y + (float)ch * (barH + gap);
         dl->AddText(ImVec2(x + 2.0f, by), dimCol, ch == 0 ? "L" : "R");
         dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + barH), ScopeBgCol(), 2.0f);
         // Colour zones are drawn segment by segment so a bar shows green up
         // to -18, yellow to -6, orange to -1, red above.
         auto bar = [&](float db, float inset, int alpha) {
            const float f = iecFrac(db);
            if (f <= 0.0f)
               return;
            static const float kEdges[5] = { -200.0f, -18.0f, -6.0f, -1.0f, 200.0f };
            static const float kZoneDb[4] = { -30.0f, -10.0f, -3.0f, 0.0f };
            for (int z = 0; z < 4; z++)
            {
               const float f0 = iecFrac(kEdges[z]), f1 = std::min(f, iecFrac(kEdges[z + 1]));
               if (f1 <= f0)
                  break;
               dl->AddRectFilled(ImVec2(bx0 + bw * f0, by + inset), ImVec2(bx0 + bw * f1, by + barH - inset),
                                 zoneCol(kZoneDb[z], alpha));
            }
         };
         bar(m.peakDb[ch], 1.0f, 95);
         bar(m.rmsDb[ch], 3.0f, 235);
         if (m.holdDb[ch] > -70.0f)
         {
            const float hx = bx0 + bw * iecFrac(m.holdDb[ch]);
            dl->AddLine(ImVec2(hx, by + 1.0f), ImVec2(hx, by + barH - 1.0f),
                        m.holdDb[ch] >= -1.0f ? IM_COL32(255, 80, 70, 255)
                                              : (isLight ? IM_COL32(30, 30, 40, 255) : IM_COL32(245, 245, 250, 255)),
                        2.0f);
         }
         dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + barH), ScopeBorderCol(), 2.0f);

         // Clip LED, latched; click either one to clear both.
         const ImVec2 l0(bx1 + 4.0f, by), l1(bx1 + 4.0f + ledW, by + barH);
         dl->AddRectFilled(l0, l1, m.clip[ch] ? IM_COL32(255, 40, 40, 255)
                                              : (isLight ? IM_COL32(200, 200, 205, 255) : IM_COL32(70, 40, 40, 255)), 2.0f);
         ImGui::SetCursorScreenPos(l0);
         ImGui::PushID(ch);
         if (ImGui::InvisibleButton("##smClip", ImVec2(ledW, barH)))
            n->ClearClip();
         if (ImGui::IsItemHovered())
            SetAudioReadout("clip", m.clip[ch] ? "output hit 0 dBFS - click to reset" : "no clip since last reset");
         ImGui::PopID();
      }

      // dB scale under the bars.
      const float scaleY = y + 2.0f * (barH + gap);
      static const float kTicks[] = { -60.0f, -40.0f, -30.0f, -20.0f, -12.0f, -6.0f, -3.0f, 0.0f };
      for (float db : kTicks)
      {
         const float tx = bx0 + bw * iecFrac(db);
         dl->AddLine(ImVec2(tx, scaleY - gap), ImVec2(tx, scaleY + 2.0f), dimCol, 1.0f);
         char t[8];
         snprintf(t, sizeof(t), "%.0f", db);
         const float tw = ImGui::CalcTextSize(t).x;
         dl->AddText(ImVec2(std::clamp(tx - tw * 0.5f, bx0, bx1 - tw), scaleY + 2.0f), dimCol, t);
      }

      // Numeric readouts.
      const float readY = scaleY + lineH + 4.0f;
      auto fmtDb = [](char* buf, size_t sz, float db) {
         if (db <= -99.0f)
            snprintf(buf, sz, " -inf");
         else
            snprintf(buf, sz, "%+5.1f", db);
      };
      char pl[12], pr[12], rl[12], rr[12], hl[12], hr[12];
      fmtDb(pl, sizeof(pl), m.peakDb[0]);
      fmtDb(pr, sizeof(pr), m.peakDb[1]);
      fmtDb(rl, sizeof(rl), m.rmsDb[0]);
      fmtDb(rr, sizeof(rr), m.rmsDb[1]);
      fmtDb(hl, sizeof(hl), m.holdDb[0]);
      fmtDb(hr, sizeof(hr), m.holdDb[1]);
      char line[160];
      snprintf(line, sizeof(line), "peak %s / %s   hold %s / %s   rms %s / %s dB%s", pl, pr, hl, hr, rl, rr,
               n->masterMute ? "   MUTED" : "");
      dl->AddText(ImVec2(bx0, readY), n->masterMute ? IM_COL32(235, 90, 80, 255) : textCol, line);

      // Gain reduction: 0..20 dB, growing from the left.
      auto grBar = [&](float gy, const char* label, float grDb, bool on, bool warn) {
         const float gx0 = bx0 + 52.0f, gx1 = bx1 - 64.0f;
         dl->AddText(ImVec2(bx0, gy - 2.0f), on ? textCol : dimCol, label);
         dl->AddRectFilled(ImVec2(gx0, gy), ImVec2(gx1, gy + 9.0f), ScopeBgCol(), 2.0f);
         if (on && grDb > 0.05f)
         {
            const float f = std::clamp(grDb / 20.0f, 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(gx0 + 1.0f, gy + 1.0f), ImVec2(gx0 + 1.0f + (gx1 - gx0 - 2.0f) * f, gy + 8.0f),
                              IM_COL32(255, 170, 60, 235), 1.0f);
         }
         for (float t : { 3.0f, 6.0f, 10.0f })
         {
            const float tx = gx0 + (gx1 - gx0) * (t / 20.0f);
            dl->AddLine(ImVec2(tx, gy), ImVec2(tx, gy + 9.0f), ScopeMidLineCol(), 1.0f);
         }
         dl->AddRect(ImVec2(gx0, gy), ImVec2(gx1, gy + 9.0f), ScopeBorderCol(), 2.0f);
         char v[24];
         if (on)
            snprintf(v, sizeof(v), "-%.1f dB", grDb);
         else
            snprintf(v, sizeof(v), "bypass");
         dl->AddText(ImVec2(gx1 + 6.0f, gy - 2.0f), on ? textCol : dimCol, v);
         // Turbo 0.50: the limiter's last-resort clamp caught something.
         if (warn)
            dl->AddCircleFilled(ImVec2(gx1 + 6.0f + ImGui::CalcTextSize(v).x + 7.0f, gy + 4.5f), 3.5f,
                                IM_COL32(255, 40, 40, 255));
      };
      const float grY = readY + lineH + 6.0f;
      grBar(grY, "comp GR", m.compGrDb, n->fxCompOn, false);
      grBar(grY + 14.0f, "limit GR", m.limGrDb, n->fxLimOn, m.limClamps > 0);
      (void)h;
   }

   void DrawSuperMixerBody(GraphNode& gn, SuperMixerNode* n)
   {
      constexpr int N = SuperMixerNode::kChannels;
      char stat[64];
      snprintf(stat, sizeof(stat), "16 in -> 1 out   sum %+.1f dB%s",
               DspMath::LinearToDb(std::max(n->Level(), 1e-5f)), n->masterMute ? "   MASTER MUTED" : "");
      BeginAudioBody(gn.index, gn.category, kAudioWideWidth + 80.0f, stat);
      const float dia = 34.0f;

      BeginAudioSection("gain / eq");
      {
         char label[16];
         AudioKnobRow trim(N, dia);
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "gain %d", i + 1);
            trim.Knob(label, &n->trimDb[i], -24.0f, 24.0f, "%+.1f dB", dia);
         }
         trim.End();
         AudioKnobRow hi(N, dia);
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "hi %d", i + 1);
            hi.Knob(label, &n->eqHigh[i], -15.0f, 15.0f, "%.1f dB", dia);
         }
         hi.End();
         AudioKnobRow mid(N, dia);
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "mid %d", i + 1);
            mid.Knob(label, &n->eqMid[i], -15.0f, 15.0f, "%.1f dB", dia);
         }
         mid.End();
         AudioKnobRow midF(N, dia);
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "mid f %d", i + 1);
            midF.Knob(label, &n->eqMidFreq[i], 200.0f, 8000.0f, "%.0f Hz", dia, false, /*freqTaper=*/true);
         }
         midF.End();
         AudioKnobRow low(N, dia);
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "low %d", i + 1);
            low.Knob(label, &n->eqLow[i], -15.0f, 15.0f, "%.1f dB", dia);
         }
         low.End();
      }
      EndAudioSection();

      const float cellW = gAudioContentW / (float)N;
      const float faderH = 130.0f;
      const float stripTop = ImGui::GetCursorScreenPos().y;
      for (int i = 0; i < N; i++)
      {
         const float cx = gAudioContentX + ((float)i + 0.5f) * cellW;
         DrawStripMeter(cx + 12.0f, stripTop + 6.0f, 6.0f, faderH - 12.0f, n->ChannelLevel(i));
      }
      {
         AudioKnobRow row(N, faderH);
         char label[8];
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "%d", i + 1);
            row.Fader(label, &n->gainDb[i], -60.0f, 12.0f, "%.1f dB", faderH, /*dbTaper=*/true);
         }
         row.End();
      }

      // Mute / solo rows, one cell per channel, each with its own CV pin.
      for (int pass = 0; pass < 2; pass++)
      {
         const float btnW = std::min(cellW - 4.0f, 50.0f);
         const float rowY = ImGui::GetCursorScreenPos().y;
         for (int i = 0; i < N; i++)
         {
            const float cx = gAudioContentX + ((float)i + 0.5f) * cellW;
            ImGui::SetCursorScreenPos(ImVec2(cx - btnW * 0.5f, rowY));
            ImGui::PushID(9400 + pass * 100 + i);
            char label[32];
            if (pass == 0)
            {
               snprintf(label, sizeof(label), "M##smMute%d", i + 1);
               bool v = n->mute[i];
               if (AudioToggleButton(label, &v, btnW))
               {
                  PushUndoCheckpoint();
                  n->mute[i] = v;
               }
            }
            else
            {
               snprintf(label, sizeof(label), "S##smSolo%d", i + 1);
               bool v = n->solo[i];
               if (AudioToggleButton(label, &v, btnW))
               {
                  PushUndoCheckpoint();
                  n->solo[i] = v;
               }
            }
            ImGui::PopID();
         }
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowY));
         ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight() + 3.0f));
      }

      {
         AudioKnobRow row(N, dia);
         char label[16];
         for (int i = 0; i < N; i++)
         {
            snprintf(label, sizeof(label), "pan %d", i + 1);
            row.Knob(label, &n->pan[i], -1.0f, 1.0f, "%.2f", dia);
         }
         row.End();
      }

      // Turbo 0.50: master bus. Positional order after "master": "master pan",
      // then the 18 FX knobs, then "lim lookahead" and "lim link" (always
      // registered, drawn only when expanded).
      BeginAudioSection("master");
      {
         AudioKnobRow row(6, 110.0f);
         row.Fader("master", &n->masterDb, -60.0f, 12.0f, "%.1f dB", 110.0f, /*dbTaper=*/true);
         const float cell1X = row.x0 + row.cellW;
         row.Knob("master pan", &n->masterPan, -1.0f, 1.0f, "%+.2f", kKnobStd);
         // Mute sits above the pan knob in the same cell (discrete pin, hashed
         // by its ## id, so it never shifts a positional pin).
         ImGui::SetCursorScreenPos(ImVec2(cell1X + 6.0f, row.y0 + 2.0f));
         {
            bool v = n->masterMute;
            if (AudioToggleButton("MUTE##smMasterMute", &v, row.cellW - 12.0f))
            {
               PushUndoCheckpoint();
               n->masterMute = v;
            }
            if (n->masterMute)
            {
               // Red wash over the lit button (its own colours are fixed):
               // a muted master must read from across the room. The button
               // starts after the 14 px pin + 4 px gap BeginDiscreteParam draws.
               const ImVec2 b0(cell1X + 6.0f + 18.0f, row.y0 + 2.0f);
               const ImVec2 b1(b0.x + std::max(8.0f, row.cellW - 12.0f - 18.0f), b0.y + ImGui::GetFrameHeight());
               ImGui::GetWindowDrawList()->AddRectFilled(b0, b1, IM_COL32(230, 50, 45, 150), 3.0f);
               ImGui::GetWindowDrawList()->AddText(ImVec2(b0.x + 6.0f, b0.y + ImGui::GetStyle().FramePadding.y),
                                                   IM_COL32(255, 255, 255, 255), "MUTE");
            }
         }
         DrawSuperMixerMasterMeter(n, row.x0 + 2.0f * row.cellW + 4.0f, row.y0 + 2.0f, row.cellW * 4.0f - 8.0f,
                                   106.0f);
         row.Skip();
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Stage switches (lit = in, dark = bypassed) plus the expander. The
      // switches stay visible while the knobs are folded away, so a stage can
      // be dropped in or out live with one click or one MIDI note.
      float expanderX = 0.0f, expanderY = 0.0f;
      {
         struct StageSwitch { const char* label; bool* on; };
         const StageSwitch switches[5] = {
            { "EQ##smFxEq", &n->fxEqOn },       { "COMP##smFxComp", &n->fxCompOn },
            { "WIDTH##smFxWidth", &n->fxWidthOn }, { "SAT##smFxSat", &n->fxSatOn },
            { "LIMIT##smFxLim", &n->fxLimOn },
         };
         const float cellW = gAudioContentW / 6.0f;
         const float rowY = ImGui::GetCursorScreenPos().y;
         for (int i = 0; i < 5; i++)
         {
            ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + (float)i * cellW + 4.0f, rowY));
            bool v = *switches[i].on;
            if (AudioToggleButton(switches[i].label, &v, cellW - 8.0f))
            {
               PushUndoCheckpoint();
               *switches[i].on = v;
            }
         }
         expanderX = gAudioContentX + 5.0f * cellW + 4.0f;
         expanderY = rowY;
         ImGui::SetCursorScreenPos(ImVec2(expanderX, rowY));
         if (ImGui::Button(n->fxOpen ? "master FX  -###smFxOpen" : "master FX  +###smFxOpen", ImVec2(cellW - 8.0f, 0)))
            n->fxOpen = !n->fxOpen;
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowY));
         ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight() + 3.0f));
      }
      EndAudioSection();

      // The FX knobs. Collapsed, each one still takes its paramIndex and stays
      // registered (same trick as DrawRateModeControls), so pins after it never
      // shift and a CV / MIDI / Performance binding keeps driving it.
      struct FxKnob { const char* label; float* v; float lo, hi; const char* fmt; bool freq; };
      const FxKnob eqKnobs[7] = {
         { "eq low f", &n->fxEqLowHz, 20.0f, 1000.0f, "%.0f Hz", true },
         { "eq low", &n->fxEqLowDb, -15.0f, 15.0f, "%+.1f dB", false },
         { "eq mid f", &n->fxEqMidHz, 40.0f, 16000.0f, "%.0f Hz", true },
         { "eq mid", &n->fxEqMidDb, -15.0f, 15.0f, "%+.1f dB", false },
         { "eq mid q", &n->fxEqMidQ, 0.2f, 10.0f, "%.2f", false },
         { "eq high f", &n->fxEqHighHz, 1000.0f, 20000.0f, "%.0f Hz", true },
         { "eq high", &n->fxEqHighDb, -15.0f, 15.0f, "%+.1f dB", false },
      };
      const FxKnob compKnobs[5] = {
         { "comp thresh", &n->fxCompThreshDb, -48.0f, 0.0f, "%.1f dB", false },
         { "comp ratio", &n->fxCompRatio, 1.0f, 20.0f, "%.1f:1", false },
         { "comp attack", &n->fxCompAttackMs, 0.1f, 100.0f, "%.1f ms", false },
         { "comp release", &n->fxCompReleaseMs, 20.0f, 2000.0f, "%.0f ms", false },
         { "comp makeup", &n->fxCompMakeupDb, 0.0f, 24.0f, "%+.1f dB", false },
      };
      const FxKnob tailKnobs[6] = {
         { "width", &n->fxWidth, 0.0f, 2.0f, "%.2f", false },
         { "sat drive", &n->fxSatDriveDb, 0.0f, 24.0f, "%.1f dB", false },
         { "sat mix", &n->fxSatMix, 0.0f, 1.0f, "%.2f", false },
         { "sat out", &n->fxSatOutDb, -12.0f, 6.0f, "%+.1f dB", false },
         { "lim ceiling", &n->fxLimCeilingDb, -24.0f, 0.0f, "%.1f dBFS", false },
         { "lim release", &n->fxLimReleaseMs, 1.0f, 2000.0f, "%.0f ms", false },
      };
      auto fxRow = [&](const FxKnob* k, int count) {
         if (n->fxOpen)
         {
            AudioKnobRow row(count, kKnobStd);
            for (int i = 0; i < count; i++)
               row.Knob(k[i].label, k[i].v, k[i].lo, k[i].hi, k[i].fmt, kKnobStd, false, k[i].freq);
            row.End();
            return;
         }
         for (int i = 0; i < count; i++)
         {
            ParamRef ref;
            ref.nodeIndex = gCurrentNodeIndex;
            ref.paramIndex = gParamCounter++;
            ref.value = k[i].v;
            ref.minValue = k[i].lo;
            ref.maxValue = k[i].hi;
            ref.taper = k[i].freq ? 2 : 0;
            ref.name = k[i].label;
            Modulation::Instance().RegisterParam(ref);
         }
      };
      const int firstFxParam = gParamCounter;
      if (n->fxOpen)
         BeginAudioSection("master FX  -  EQ / glue comp / width + saturation + true-peak limiter");
      fxRow(eqKnobs, 7);
      fxRow(compKnobs, 5);
      fxRow(tailKnobs, 6);

      // Turbo 0.50: limiter v2. The two knobs are positional and come after
      // every older positional control, so existing pin numbers never move;
      // style and the two switches are hashed discrete controls.
      const FxKnob limKnobs[2] = {
         { "lim lookahead", &n->fxLimLookaheadMs, 0.5f, 5.0f, "%.1f ms", false },
         { "lim link", &n->fxLimLink, 0.0f, 100.0f, "%.0f %%", false },
      };
      if (!n->fxOpen)
         fxRow(limKnobs, 2);
      else
      {
         AudioKnobRow row(7, kKnobStd);
         for (int i = 0; i < 2; i++)
            row.Knob(limKnobs[i].label, limKnobs[i].v, limKnobs[i].lo, limKnobs[i].hi, limKnobs[i].fmt, kKnobStd);
         static const std::vector<std::string> kLimStyles = { "transparent", "punchy", "loud" };
         row.Dropdown("lim style", kLimStyles, n->fxLimStyle, [n](int i) {
            PushUndoCheckpoint();
            n->fxLimStyle = i;
         });
         row.Checkbox("true peak##smLimTp", &n->fxLimTruePeak);
         row.Checkbox("auto rel##smLimAuto", &n->fxLimAutoRelease);

         // Last two cells: GR history (about 3 s, newest right, hanging
         // from the top like the GR bar), latency and the safety-clamp LED.
         const SuperMixerNode::MasterMeter& mm = n->Meter();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const ImU32 dimCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
         const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
         const float lineH = ImGui::GetTextLineHeight();
         const float hx0 = row.x0 + 5.0f * row.cellW + 6.0f, hx1 = row.x0 + 7.0f * row.cellW - 6.0f;
         const float hy0 = row.y0 + lineH + 4.0f, hy1 = row.y0 + kKnobStd;
         char info[64];
         snprintf(info, sizeof(info), "latency %.2f ms%s", n->LimLatencyMs(), n->fxLimOn ? "" : " (when on)");
         dl->AddText(ImVec2(hx0, row.y0), n->fxLimOn ? textCol : dimCol, info);
         // Clamp LED, top right: lit once the last-resort clamp has caught
         // an over (should never happen); click to reset.
         const ImVec2 led0(hx1 - 12.0f, row.y0 + 1.0f), led1(hx1, row.y0 + 13.0f);
         dl->AddRectFilled(led0, led1, mm.limClamps > 0 ? IM_COL32(255, 40, 40, 255)
                                                        : (IsThemeLight() ? IM_COL32(200, 200, 205, 255)
                                                                          : IM_COL32(70, 40, 40, 255)), 2.0f);
         ImGui::SetCursorScreenPos(led0);
         if (ImGui::InvisibleButton("##smLimClamp", ImVec2(12.0f, 12.0f)))
            n->ClearLimClamps();
         if (ImGui::IsItemHovered())
         {
            char tip[80];
            snprintf(tip, sizeof(tip), "%u safety clamps (ISP / clip) since reset - click to reset", mm.limClamps);
            SetAudioReadout("lim clamp", tip);
         }
         dl->AddRectFilled(ImVec2(hx0, hy0), ImVec2(hx1, hy1), ScopeBgCol(), 2.0f);
         const int K = SuperMixerNode::MasterMeter::kGrHist;
         const float colW = (hx1 - hx0 - 2.0f) / (float)K;
         for (int i = 0; i < K; i++)
         {
            const float gr = mm.grHist[(mm.grHistPos + i) % K];
            if (gr <= 0.05f)
               continue;
            const float f = std::clamp(gr / 12.0f, 0.0f, 1.0f);
            const float cx = hx0 + 1.0f + colW * (float)i;
            dl->AddRectFilled(ImVec2(cx, hy0 + 1.0f), ImVec2(cx + std::max(1.0f, colW), hy0 + 1.0f + (hy1 - hy0 - 2.0f) * f),
                              IM_COL32(255, 170, 60, 220));
         }
         for (float t : { 3.0f, 6.0f })
         {
            const float ty = hy0 + 1.0f + (hy1 - hy0 - 2.0f) * (t / 12.0f);
            dl->AddLine(ImVec2(hx0, ty), ImVec2(hx1, ty), ScopeMidLineCol(), 1.0f);
         }
         dl->AddRect(ImVec2(hx0, hy0), ImVec2(hx1, hy1), ScopeBorderCol(), 2.0f);
         dl->AddText(ImVec2(hx0, row.y0 + kKnobStd + 4.0f), dimCol, "limit GR, 3 s (0..12 dB)");
         row.Skip();
         row.Skip();
         row.End();
      }
      if (n->fxOpen)
         EndAudioSection();
      else
      {
         // Folded: give any cable bound to a hidden knob a 1px landing pin on
         // the expander, so the link is drawn instead of looking lost.
         const int lastFxParam = gParamCounter;
         const ImVec2 restore = ImGui::GetCursorScreenPos();
         for (const auto& link : Modulation::Instance().Links())
         {
            if (link.first.first != gCurrentNodeIndex || link.first.second < firstFxParam ||
                link.first.second >= lastFxParam)
               continue;
            const int pinId = gCurrentNodeIndex * GraphNode::kStride + GraphNode::kParamBase + link.first.second;
            if (gDrawnParamPins.count(pinId) > 0)
               continue;
            gDrawnParamPins.insert(pinId);
            ImGui::SetCursorScreenPos(ImVec2(expanderX, expanderY + ImGui::GetFrameHeight() * 0.5f));
            ed::BeginPin(pinId, ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ed::EndPin();
         }
         ImGui::SetCursorScreenPos(restore);
      }

      EndAudioBody();
   }


   void DrawAnalogBody(GraphNode& gn, AnalogNode* n)
   {
      const bool noteDriven = n->noteInput.GetSource() != nullptr;
      const int voices = n->ActiveVoices();
      const auto& filterNames = AnalogFilterTypeList();
      const char* filterName = (n->filterType >= 0 && n->filterType < (int)filterNames.size())
                                  ? filterNames[n->filterType].c_str() : "off";

      char stat[96];
      if (noteDriven)
         snprintf(stat, sizeof(stat), "%s  -  %d voice%s", filterName, voices, voices == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "%s  -  free run %.0f Hz", filterName, n->freq);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      const auto& waveList = AnalogWaveformList();

      // Oscillator section
      BeginAudioSection("oscillator");
      // Tuning header per oscillator, laid out like the Oscillator/Wavetable
      // header: wave dropdown, fine slider, octave and semitone dropdowns.
      // oct/semi stay floats in the node (saved patches, modulation, DSP), so
      // the dropdowns round on write; the semi list spans the full stored
      // -24..+24 so no saved value falls off the end of it.
      static const std::vector<std::string> semiNames24 = [] {
         std::vector<std::string> v;
         for (int i = -24; i <= 24; i++)
         {
            char b[16];
            snprintf(b, sizeof(b), "semi %+d", i);
            v.push_back(b);
         }
         return v;
      }();
      auto tuningHeader = [&](const char* idBase, int& wave, float& fine, float& semi, float& oct) {
         // Discrete pins are keyed by these id strings, not the ImGui ID
         // stack, so each oscillator needs its own.
         const std::string waveId = std::string("wave") + idBase, octId = std::string("oct") + idBase,
                           semiId = std::string("semi") + idBase;
         const float w = gAudioContentW;
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         const float gap = 5.0f;
         const float octW = 74.0f, semiW = 82.0f, fineW = 104.0f;
         const float waveW = std::max(70.0f, w - octW - semiW - fineW - gap * 3.0f);
         ImGui::PushID(idBase);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown(waveId.c_str(), waveList, wave, [&wave](int i) { PushUndoCheckpoint(); wave = i; }, waveW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + waveW + gap, y));
         AudioSlider("fine", &fine, -50.0f, 50.0f, "%.1f c", fineW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + w - octW - semiW - gap, y));
         AudioBareDropdown(octId.c_str(), OctaveNames(), std::clamp((int)std::lround(oct), -4, 4) + 4,
                           [&oct](int i) { PushUndoCheckpoint(); oct = (float)(i - 4); }, octW);
         ImGui::SetCursorScreenPos(ImVec2(x0 + w - semiW, y));
         AudioBareDropdown(semiId.c_str(), semiNames24, std::clamp((int)std::lround(semi), -24, 24) + 24,
                           [&semi](int i) { PushUndoCheckpoint(); semi = (float)(i - 24); }, semiW);
         ImGui::PopID();
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
         ImGui::Dummy(ImVec2(0.0f, 4.0f));
      };
      tuningHeader("1", n->wave1, n->fine1, n->semi1, n->oct1);
      tuningHeader("2", n->wave2, n->fine2, n->semi2, n->oct2);
      {
         // Row 3: Osc 1 Vol, Osc 2 Vol, Sync & Analog switches
         AudioKnobRow row(4);
         row.Knob("vol 1", &n->osc1Vol, 0.0f, 1.0f, "%.2f");
         row.Knob("vol 2", &n->osc2Vol, 0.0f, 1.0f, "%.2f");
         row.Checkbox("sync", &n->sync);
         row.Checkbox("analog", &n->analog);
         row.End();
      }
      {
         // Row 4: pw, voices, spread, fm
         AudioKnobRow row(4);
         row.Knob("pw", &n->pw1, 0.01f, 0.99f, "%.2f");
         row.Knob("voices", &n->voices, 1.0f, 7.0f, "%.0f");
         row.Knob("spread", &n->spread, 0.0f, 1.0f, "%.2f");
         row.Knob("fm", &n->fm, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         // Row 5: detune, mix (default 50%), sub, noise
         AudioKnobRow row(4);
         row.Knob("detune", &n->detune, 0.0f, 100.0f, "%.1f c");
         row.Knob("mix", &n->oscMix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("sub", &n->sub, 0.0f, 1.0f, "%.2f");
         row.Knob("noise", &n->noise, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Filter section
      BeginAudioSection("filter");
      {
         const bool filterOff = (n->filterType == kAFilterOff);
         AudioKnobRow row(4, kKnobLarge, ImGui::GetFrameHeight() + 5.0f);
         row.DropdownKnob("analogFlt", filterNames, n->filterType,
                          [n](int i) { PushUndoCheckpoint(); n->filterType = i; },
                          "filter", &n->cutoff, 20.0f, 18000.0f, "%.0f Hz", filterOff, kKnobLarge, true);
         row.Knob("reso", &n->resonance, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("drive", &n->drive, 0.0f, 1.0f, "%.2f");
         row.Knob("key track", &n->keyTrack, 0.0f, 1.0f, "%.2f");
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Envelope section (interactive ADSR curve panel with parameter sliders)
      ImGui::PushID("amp");
      DrawEnvelopePanel("amp envelope  -  drag the handles", "##analogAmpEnv", &n->attack,
                        &n->decay, &n->sustain, &n->release, nullptr, 0.0f, 0.0f, nullptr,
                        IM_COL32(150, 214, 255, 245));
      ImGui::PopID();

      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Output section
      BeginAudioSection("output");
      {
         AudioKnobRow row(4);
         if (noteDriven)
            ImGui::BeginDisabled();
         row.Knob("freq", &n->freq, 20.0f, 8000.0f, "%.0f Hz", kKnobLarge);
         if (noteDriven)
            ImGui::EndDisabled();
         row.Knob("volume", &n->volume, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s");
         row.Knob("bend", &n->pitchBend, -2.0f, 2.0f, "%+.2f st");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }

   // Waveform + slice markers for the Slicer. Clicking inside a slice's band
   // auditions that slice; in onsets mode each marker (except the pinned one
   // at 0) can be dragged. Grid boundaries are derived from the transport, so
   // dragging them is disabled rather than silently undone on the next
   // recompute.
   void DrawSlicerWaveform(SlicerNode* n, float h, float width)
   {
      static const char* kPitchClass[12] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };

      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      const std::vector<float>& slices = n->Slices();
      const int sliceCount = (int)slices.size();
      // Turbo 0.49: shared view (per-pixel peaks, cursors with heads). No
      // trim range here, so no zoom chip.
      const WaveView view = WaveViewBegin("##slicerwave", origin, w, h, -1.0f, -1.0f, n->peaks.NumFrames());

      // Body click-catcher first, so it owns hover/active by default; the
      // marker grab-zones are added afterwards at the same screen position.
      // ImGui overlap resolution is NOT "last submitted wins" - a later item
      // is blocked while an earlier one already holds g.HoveredId unless that
      // earlier item opted in via SetNextItemAllowOverlap() *before* it.
      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##slicerwavebody", ImVec2(w, h));
      if (hasSample && sliceCount > 0 && ImGui::IsItemActivated())
      {
         const float frac = view.MouseFrac();
         int hit = 0;
         for (int i = 0; i < sliceCount; i++)
         {
            if (slices[i] <= frac)
               hit = i;
         }
         n->TriggerSlicePreview(hit);
      }
      if (hasSample && sliceCount > 0 && ImGui::IsItemHovered())
      {
         const float frac = view.MouseFrac();
         int hit = 0;
         for (int i = 0; i < sliceCount; i++)
         {
            if (slices[i] <= frac)
               hit = i;
         }
         const int note = SlicerNode::kBaseNote + hit;
         char val[48];
         snprintf(val, sizeof(val), "slice %d - %s%d - click to audition", hit + 1,
                  kPitchClass[note % 12], note / 12 - 1);
         SetAudioReadout("#", val);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      if (!hasSample)
      {
         const float midY = origin.y + h * 0.5f;
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
      }

      if (hasSample)
      {
         // Alternating band shading behind the waveform, so "where does one
         // slice end" reads without counting marker lines.
         for (int i = 0; i < sliceCount; i += 2)
         {
            const float x0 = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            const float x1 = origin.x + w * ((i + 1 < sliceCount) ? std::clamp(slices[i + 1], 0.0f, 1.0f) : 1.0f);
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, br.y),
                              isLight ? IM_COL32(0, 0, 0, 14) : IM_COL32(255, 255, 255, 12));
         }

         DrawWavePeaks(dl, view, n->peaks, 0.0f, 1.0f,
                       isLight ? IM_COL32(30, 110, 230, 230) : IM_COL32(150, 214, 255, 220));

         // Voices in flight, faded by their own amplitude.
         const SlicerVoiceSnapshot& snap = n->VisualSnapshot();
         for (int v = 0; v < snap.count; v++)
         {
            if (snap.voices[v].amp < 0.002f)
               continue;
            const int alpha = (int)(std::clamp(snap.voices[v].amp, 0.0f, 1.0f) * 235.0f) + 20;
            DrawPlayCursor(dl, view, std::clamp(snap.voices[v].position, 0.0f, 1.0f), PlayCursorColor(alpha));
         }

         // Slice markers, plus the note each slice answers to when it fits.
         const ImU32 markerCol = isLight ? IM_COL32(20, 140, 90, 230) : IM_COL32(120, 230, 175, 220);
         for (int i = 0; i < sliceCount; i++)
         {
            const float x = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), markerCol, i == 0 ? 1.0f : 1.5f);

            const float nextX = origin.x + w * ((i + 1 < sliceCount) ? std::clamp(slices[i + 1], 0.0f, 1.0f) : 1.0f);
            const int note = SlicerNode::kBaseNote + i;
            char label[8];
            snprintf(label, sizeof(label), "%s%d", kPitchClass[note % 12], note / 12 - 1);
            const ImVec2 ts = ImGui::CalcTextSize(label);
            if (nextX - x > ts.x + 6.0f)
               dl->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), ScopeTextCol(), label);
         }
      }
      else
      {
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(), "no sample loaded");
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample && n->MarkersAreEditable())
      {
         // Narrow grab-zones centred on each marker, submitted after the body
         // button above. Marker 0 is pinned at the sample's start.
         const float handleW = 9.0f;
         for (int i = 1; i < sliceCount; i++)
         {
            const float x = origin.x + w * std::clamp(slices[i], 0.0f, 1.0f);
            const float btnX = std::clamp(x - handleW * 0.5f, origin.x, br.x - handleW);
            char id[32];
            snprintf(id, sizeof(id), "##slicermk%d", i);
            ImGui::SetCursorScreenPos(ImVec2(btnX, origin.y));
            ImGui::InvisibleButton(id, ImVec2(handleW, h));
            if (ImGui::IsItemActivated())
               PushUndoCheckpoint();
            if (ImGui::IsItemActive())
            {
               n->MoveSliceMarker(i, (ImGui::GetIO().MousePos.x - origin.x) / w);
               // Turbo 0.49: the marker's time while dragging.
               if (i < (int)n->Slices().size())
                  DrawTrimTimeLabel(dl, view, std::clamp(n->Slices()[(size_t)i], 0.0f, 1.0f), &n->peaks,
                                    isLight ? IM_COL32(20, 140, 90, 230) : IM_COL32(120, 230, 175, 220));
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
               ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
         }
      }

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   // Custom node body for the Slicer.
   //
   // Draw order below IS the modulation-pin numbering (ModSlider and
   // RegisterDiscreteParam both take paramIndex = gParamCounter++), so new
   // params are appended at the end forever - reordering silently rewires
   // every saved patch's modulation cables.
   //
   // Row order (all two-column, each cell exactly AudioHalfWidth()):
   //    1: slice by (dropdown) | onsets
   //    2: division (dropdown) | sensitivity
   //    3: pitch               | finetune
   //    4: attack              | decay        <- the envelope pair, adjacent
   //    5: speed               | volume
   //    6: crossthrough (cbox) | (deliberately empty)
   //
   // Pillars: P1 every control sits on the two-column grid (the button strip
   // is the one deliberate full-width row); P2 the crossthrough checkbox uses
   // ModCheckbox, which passes ImGui::GetFrameHeight() to
   // DrawDiscreteParamPin and so centres its modulation dot with no per-node
   // offset code (same idiom as DrawWavetableBody's engine on/off); P3 both
   // selectors and the checkbox occupy the left column of their rows, so the
   // left edge reads as one non-slider column; P5 the mode swap greys the
   // inactive control instead of removing its cell, so no cell ever moves;
   // P6 eleven controls is odd, so exactly one cell must be spare - it is the
   // far-right of the last row, P6's sanctioned position, rather than the
   // ragged full-width `volume` row this used to end on; P10 the dropdowns
   // and the checkbox go through the shared AudioBareDropdown /
   // PushCheckboxStyle theming, never a hand-rolled colour; P11 everything
   // numeric goes to the readout strip. P4 does not apply - a Synths node
   // has no `mix`.
   //
   // Discrete params (the two dropdowns and the checkbox) are allocated from
   // kDiscreteParamBase and keyed by label hash, NOT from gParamCounter, so
   // adding the checkbox shifts no float pin ordinal.
   void DrawSlicerBody(GraphNode& gn, SlicerNode* n)
   {
      static const std::vector<std::string> kSliceByNames = { "onsets", "grid" };
      static const std::vector<std::string> kDivisionNames = [] {
         std::vector<std::string> v;
         for (int i = 0; i < kSlicerNumDivisions; i++)
            v.push_back(kSlicerDivisionNames[i]);
         return v;
      }();

      const int sliceCount = n->SliceCount();
      char stat[192];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %d slice%s  -  %s", n->FileName().c_str(), sliceCount,
                  sliceCount == 1 ? "" : "s", n->sliceBy == 0 ? "onsets" : "grid");
      else
         snprintf(stat, sizeof(stat), "%s  -  %s", n->Status().c_str(),
                  n->sliceBy == 0 ? "onsets" : "grid");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Button strip - the one deliberate full-width row (P1's exception).
      if (ImGui::Button("Load...", ImVec2(90, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      // Turbo 0.49: mappable (CV / MIDI / Performance), like the Sampler's.
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##slicerRecord" : "Record##slicerRecord",
                         recording, requestedRecording, ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (requestedRecording)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedPlaying = playing;
      if (ModStateButton(playing ? "Stop##slicerAudition" : "Audition##slicerAudition",
                         playing, requestedPlaying, ImVec2(90, 0)))
      {
         if (requestedPlaying)
            n->TriggerSlicePreview(-1);
         else
            n->StopPreview();
      }
      if (playing)
         ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
         SetAudioReadout("#", "plays the whole sample on this node's own voice");

      ImGui::SameLine();
      ImGui::BeginDisabled(n->FileName().empty() || n->sliceBy != 0);
      if (ModTriggerButton("re-slice##slicerReslice", ImVec2(90, 0)))
      {
         PushUndoCheckpoint();
         n->ReSlice();
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered())
         SetAudioReadout("#", "re-runs transient detection at the current sensitivity");

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawSlicerWaveform(n, 140.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      const bool onsetMode = (n->sliceBy == 0);
      const float halfW = AudioHalfWidth();
      const float gap = ImGui::GetStyle().ItemSpacing.x;

      // Row 1: slice by | onsets. Row 2: division | sensitivity.
      // Both selectors sit in the left column of consecutive rows (P3), and
      // the two mode-specific controls keep their own permanent cells so
      // switching modes never moves the grid (P5).
      {
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         AudioBareDropdown("slice by##slicerSliceBy", kSliceByNames, n->sliceBy,
                           [n](int i) { PushUndoCheckpoint(); n->sliceBy = i; }, halfW);
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(halfW, ImGui::GetFrameHeight()));
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!onsetMode);
      if (AudioSliderInt("onsets", &n->onsets, 1, SlicerNode::kMaxSlices, halfW))
         n->onsets = std::clamp(n->onsets, 1, SlicerNode::kMaxSlices);
      ImGui::EndDisabled();

      {
         const float x0 = gAudioContentX;
         const float y = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::BeginDisabled(onsetMode);
         AudioBareDropdown("division##slicerDivision", kDivisionNames, n->division,
                           [n](int i) { PushUndoCheckpoint(); n->division = i; }, halfW);
         ImGui::EndDisabled();
         ImGui::SetCursorScreenPos(ImVec2(x0, y));
         ImGui::Dummy(ImVec2(halfW, ImGui::GetFrameHeight()));
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!onsetMode);
      AudioSlider("sensitivity", &n->sensitivity, 0.0f, 100.0f, "%.0f %%", halfW);
      ImGui::EndDisabled();

      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", halfW);
      ImGui::SameLine();
      AudioSlider("finetune", &n->finetune, -100.0f, 100.0f, "%.0f c", halfW);

      // attack extends the hidden 2 ms de-click ramp rather than adding a
      // second envelope, so 0 is exactly the old (instant) behaviour. The
      // skew puts 100 ms at 12 o'clock over the 0..500 throw.
      AudioSlider("attack", &n->attack, 0.0f, 500.0f, "%.1f ms", halfW);
      ImGui::SameLine();
      // decay owns the ENVELOPE only. The top of its throw is a no-decay
      // detent - the slice holds at full level after its attack. Where it
      // stops is `crossthrough`'s business, not decay's.
      const char* decayFmt = (n->decay >= SlicerNode::kDecayInfinite) ? "hold" : "%.0f ms";
      AudioSlider("decay", &n->decay, 5.0f, 5000.0f, decayFmt, halfW);

      // speed is exponential (linear in log2) so 1.0x sits at the middle of
      // the 0.25..4 throw instead of a quarter of the way along it.
      AudioSlider("speed", &n->speed, 0.25f, 4.0f, "%.2fx", halfW);
      ImGui::SameLine();
      AudioSlider("volume", &n->volume, 0.0f, 1.0f, "%.2f", halfW);

      // Row 6, left cell only: the right cell is the one deliberate spare
      // (P6). The `bool tmp` dance is required - ModCheckbox reports a
      // modulator-driven flip only through its return value.
      {
         const float y = ImGui::GetCursorScreenPos().y;
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, y));
         bool cross = n->crossthrough;
         bool crossUserChanged = false;
         if (ModCheckbox("crossthrough##slicerCrossthrough", &cross, &crossUserChanged))
         {
            if (crossUserChanged) // a cable flip writes the value but never checkpoints
               PushUndoCheckpoint();
            n->crossthrough = cross;
         }
         if (ImGui::IsItemHovered())
            SetAudioReadout("crossthrough",
                            n->crossthrough ? "slices run past their own boundary"
                                            : "each slice stops at the next onset");
      }
      (void)gap;

      EndAudioBody();
   }

   // Decimated waveform + partial-spectrum strip for Molder, cached on the
   // node (rebuilt only when a new render/analysis lands - see
   // MolderNode::RebuildWaveformCache/RebuildPartialCache), never
   // recomputed per frame.
   void DrawMolderWaveform(MolderNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      // Turbo 0.49: shared view (zoom-to-trim, per-pixel peaks, cursor).
      WaveView view = WaveViewBegin("##molderwave", origin, w, h, n->start, n->end, n->peaks.NumFrames());

      // Body click-catcher first (see DrawSamplerWaveform's comment on
      // overlap ordering) - the two range-handle grab-zones are added after,
      // at the same screen position, and only receive input because this
      // opts in via SetNextItemAllowOverlap() before being submitted.
      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##molderwavebody", ImVec2(w, h));
      if (hasSample && ImGui::IsItemActivated())
      {
         const float frac = view.MouseFrac();
         const float target = (frac < n->start || frac > n->end) ? n->start : frac;
         n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      if (hasSample)
      {
         DrawWavePeaks(dl, view, n->peaks, n->start, n->end,
                       isLight ? IM_COL32(40, 100, 230, 230) : IM_COL32(165, 180, 255, 225));
         DrawPlayCursor(dl, view, std::clamp(n->Playhead(), 0.0f, 1.0f), PlayCursorColor(n->IsPlaying() ? 255 : 120),
                        2.0f);
      }
      else
      {
         const float midY = origin.y + h * 0.5f;
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(),
                     n->IsAnalyzing() ? "analyzing..." : "no sample loaded");
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
         WaveTrimHandles(view, "##moldertrim", &n->start, &n->end, std::max(0.001f, 0.01f * view.Span()), &n->peaks,
                         TrimStartCol(), TrimEndCol());

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawMolderPartialBars(MolderNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();

      dl->AddRectFilled(origin, br, ScopeBgCol(), 3.0f);
      dl->PushClipRect(origin, br, true);

      const int count = n->partialBarCount;
      if (count > 0)
      {
         float maxAmp = 1e-6f;
         for (int i = 0; i < count; i++)
            maxAmp = std::max(maxAmp, n->partialBars[i]);
         for (int i = 0; i < count; i++)
         {
            const float x = origin.x + w * (float)i / (float)count;
            const float barW = std::max(1.0f, w / (float)count - 1.0f);
            const float frac = std::clamp(n->partialBars[i] / maxAmp, 0.0f, 1.0f);
            const float top = br.y - frac * h;
            dl->AddRectFilled(ImVec2(x, top), ImVec2(x + barW, br.y),
                              isLight ? IM_COL32(90, 70, 200, 200) : IM_COL32(180, 160, 255, 200));
         }
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 3.0f);

      // Draws into the draw list only, above - without this the cursor never
      // advances past `origin`, so everything drawn after this call (the
      // Dummy spacer and the seed/gen/f0/harm readout) renders on top of it
      // instead of below it.
      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawMolderBody(GraphNode& gn, MolderNode* n)
   {
      char stat[192];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...", ImVec2(80, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      // Turbo 0.49: transport and actions mappable (CV / MIDI / Performance).
      // Widths grew by the 18 px pin each so the captions still fit.
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##molderRec" : "Record##molderRec",
                         recording, requestedRecording, ImVec2(76, 0)))
      {
         PushUndoCheckpoint();
         if (requestedRecording)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      if (ModTriggerButton("Roll##molderRoll", ImVec2(64, 0)))
      {
         PushUndoCheckpoint();
         n->Roll();
      }
      ImGui::SameLine();
      if (ModTriggerButton("Iterate##molderIterate", ImVec2(80, 0)))
      {
         PushUndoCheckpoint();
         n->Iterate();
      }
      ImGui::SameLine();
      if (ModTriggerButton("Reset##molder", ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         n->Reset();
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      DrawMolderWaveform(n, 90.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      DrawMolderPartialBars(n, 36.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // loop/reverse/ping-pong right-aligned on their own row, anchored off
      // the fixed declared content width rather than GetContentRegionAvail()
      // - see DrawSamplerBody's comment on why (it overlaps the node-editor
      // resize hit-zone otherwise).
      {
         const float toggleW = 44.0f;
         const float modsW = toggleW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
         const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
         bool loopBool = n->loop;
         if (AudioToggleButton("loop##molderLoop", &loopBool))
         {
            PushUndoCheckpoint();
            n->loop = loopBool;
         }
         ImGui::SameLine();
         bool reverseBool = n->reverse;
         if (AudioToggleButton("rev##molderReverse", &reverseBool))
         {
            PushUndoCheckpoint();
            n->reverse = reverseBool;
         }
         ImGui::SameLine();
         bool pingpongBool = n->pingpong;
         if (AudioToggleButton("p-p##molderPingpong", &pingpongBool))
         {
            PushUndoCheckpoint();
            n->pingpong = pingpongBool;
         }
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      AudioSlider("chaos", &n->chaos, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());

      AudioSlider("tone", &n->tone, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("stretch", &n->stretch, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      AudioSlider("air", &n->air, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("snap", &n->snap, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      AudioSlider("time", &n->time, 0.0f, 1.0f, "%.2f", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("level", &n->level, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->start = std::min(n->start, n->end - 0.01f);
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->end = std::max(n->end, n->start + 0.01f);

      EndAudioBody();
   }

   // ----------------------------------------------------------- Grain Molder
   void DrawGrainMolderWaveform(GrainMolderNode* n, float h, float width)
   {
      const float w = width > 0.0f ? width : gAudioContentW;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool hasSample = n->waveformCacheCount > 0;
      // Turbo 0.49: shared view (zoom-to-trim, per-pixel peaks, cursors).
      WaveView view = WaveViewBegin("##grainmolderwave", origin, w, h, n->start, n->end, n->peaks.NumFrames());

      ImGui::SetNextItemAllowOverlap();
      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##grainmolderwavebody", ImVec2(w, h));
      if (hasSample && (ImGui::IsItemActivated() || ImGui::IsItemActive()))
      {
         const float frac = view.MouseFrac();
         const float target = std::clamp(frac, n->start, n->end);
         n->position = target;
         if (ImGui::IsItemActivated())
            n->TriggerPreview(target);
      }

      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      if (hasSample)
      {
         DrawWavePeaks(dl, view, n->peaks, n->start, n->end,
                       isLight ? IM_COL32(50, 160, 190, 230) : IM_COL32(110, 210, 240, 225));

         // Primary yellow playhead: stays between start and end musically and UI-wise
         const auto& snap = n->VisualSnapshot();
         const float posClamped = std::clamp(n->position, n->start, n->end);
         const float activeFrac = (snap.selfActive && snap.selfPos >= 0.0f) ? snap.selfPos : posClamped;
         DrawPlayCursor(dl, view, std::clamp(activeFrac, 0.0f, 1.0f), PlayCursorColor(), 2.0f);

         // Polyphonic white playheads moving at different speeds according to incoming pitch
         for (int v = 0; v < snap.count; v++)
         {
            const auto& voice = snap.voices[v];
            if (voice.amp < 0.002f)
               continue;
            const int alpha = std::clamp((int)(voice.amp * 255.0f), 0, 255);
            const ImU32 whiteCol = isLight ? IM_COL32(40, 45, 55, alpha) : IM_COL32(255, 255, 255, alpha);
            DrawPlayCursor(dl, view, std::clamp(voice.position, 0.0f, 1.0f), whiteCol);
         }
      }
      else
      {
         const float midY = origin.y + h * 0.5f;
         dl->AddLine(ImVec2(origin.x, midY), ImVec2(br.x, midY), ScopeMidLineCol(), 1.0f);
         dl->AddText(ImVec2(origin.x + 8.0f, origin.y + 4.0f), ScopeTextCol(),
                     n->IsRendering() ? "molding..." : "no sample loaded");
      }
      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);

      if (hasSample)
         WaveTrimHandles(view, "##gmtrim", &n->start, &n->end, std::max(0.001f, 0.01f * view.Span()), &n->peaks,
                         TrimStartCol(), TrimEndCol());

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawGrainMolderBody(GraphNode& gn, GrainMolderNode* n)
   {
      char stat[192];
      if (!n->FileName().empty())
         snprintf(stat, sizeof(stat), "%s  -  %s", n->FileName().c_str(), n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ImGui::Button("Load...##grainMolder", ImVec2(80, 0)))
      {
         const std::string path = Platform::OpenAudioDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->LoadFile(path);
         }
      }
      ImGui::SameLine();
      const bool recording = n->IsRecording();
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      // Turbo 0.49: mappable (CV / MIDI / Performance), like the Sampler's.
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop##gmRec" : "Record##gmRec",
                         recording, requestedRecording, ImVec2(70, 0)))
      {
         PushUndoCheckpoint();
         if (requestedRecording)
            n->StartRecording();
         else
            n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();

      ImGui::SameLine();
      const bool playing = n->IsPlaying();
      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(190, 60, 60, 255));
      bool requestedPlaying = playing;
      if (ModStateButton(playing ? "Stop##gmAud" : "Audition##gmAud",
                         playing, requestedPlaying, ImVec2(80, 0)))
      {
         if (requestedPlaying)
            n->TriggerPreview(n->start);
         else
            n->StopPreview();
      }
      if (playing)
         ImGui::PopStyleColor();

      // loop/rev/p-p toggles right-aligned on header row
      ImGui::SameLine();
      const float toggleW = 44.0f;
      const float modsW = toggleW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
      const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - modsW, rowScreenPos.y));
      bool loopBool = n->loop;
      if (AudioToggleButton("loop##gmLoop", &loopBool))
      {
         PushUndoCheckpoint();
         n->loop = loopBool;
      }
      ImGui::SameLine();
      bool reverseBool = n->reverse;
      if (AudioToggleButton("rev##gmReverse", &reverseBool))
      {
         PushUndoCheckpoint();
         n->reverse = reverseBool;
      }
      ImGui::SameLine();
      bool pingpongBool = n->pingpong;
      if (AudioToggleButton("p-p##gmPingpong", &pingpongBool))
      {
         PushUndoCheckpoint();
         n->pingpong = pingpongBool;
      }

      // Read-only status word right-aligned
      {
         const bool noteConnected = n->noteInput.IsConnected();
         char status[32];
         if (noteConnected)
            snprintf(status, sizeof(status), "notes \xc2\xb7 %d", n->ActiveNoteCount());
         else if (n->IsAuditioning())
            snprintf(status, sizeof(status), "auditioning");
         else
            snprintf(status, sizeof(status), "%s",
                     Transport::Instance().IsPlaying() ? "auto \xc2\xb7 running" : "auto \xc2\xb7 press space");

         const ImVec2 textSize = ImGui::CalcTextSize(status);
         const ImVec2 rowPos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - textSize.x, rowPos.y));
         ImGui::TextColored(ImVec4(0.6f, 0.63f, 0.72f, 1.0f), "%s", status);
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f));
      DrawGrainMolderWaveform(n, 130.0f, AudioFullWidth());
      ImGui::Dummy(ImVec2(0.0f, 6.0f));

      // Row 1: grain | amount
      AudioSlider("grain", &n->grain, 10.0f, 500.0f, "%.0f ms", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("amount", &n->amount, 0.0f, 1.0f, "%.2f", AudioHalfWidth());

      // Row 2: key | order (or seed if random)
      {
         const float pinIndent = 18.0f; // 14px pin + 4px spacing matching ModSlider
         const float halfTrackW = AudioHalfWidth() - pinIndent;

         ImGui::Dummy(ImVec2(14.0f, 14.0f));
         ImGui::SameLine(0.0f, 4.0f);
         static const std::vector<std::string> kKeyNames = { "Level", "Bright", "Random" };
         AudioBareDropdown("gmKey", kKeyNames, n->key, [n](int i) {
            PushUndoCheckpoint();
            n->key = i;
         }, halfTrackW);
         ImGui::SameLine();

         if (n->key == 2)
         {
            float seedFloat = (float)n->seed;
            if (AudioSlider("seed", &seedFloat, 1.0f, 9999.0f, "%.0f", AudioHalfWidth()))
            {
               PushUndoCheckpoint();
               n->seed = std::max(1, (int)seedFloat);
            }
         }
         else
         {
            ImGui::Dummy(ImVec2(14.0f, 14.0f));
            ImGui::SameLine(0.0f, 4.0f);
            static const std::vector<std::string> kOrderNames = { "Ascending", "Descending" };
            AudioBareDropdown("gmOrder", kOrderNames, n->descending ? 1 : 0, [n](int i) {
               PushUndoCheckpoint();
               n->descending = (i == 1);
            }, halfTrackW);
         }
      }

      // Row 3: start | end
      if (AudioSlider("start", &n->start, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->start = std::min(n->start, n->end - 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }
      ImGui::SameLine();
      if (AudioSlider("end", &n->end, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
      {
         n->end = std::max(n->end, n->start + 0.01f);
         n->position = std::clamp(n->position, n->start, n->end);
      }

      // Row 4: position | level
      if (AudioSlider("position", &n->position, 0.0f, 1.0f, "%.3f", AudioHalfWidth()))
         n->position = std::clamp(n->position, n->start, n->end);
      ImGui::SameLine();
      AudioSlider("level", &n->level, 0.0f, 2.0f, "%.2f", AudioHalfWidth());

      // Row 5: pitch | decay
      AudioSlider("pitch", &n->pitch, -24.0f, 24.0f, "%.1f st", AudioHalfWidth());
      ImGui::SameLine();
      AudioSlider("decay", &n->decay, 0.05f, 10.0f, "%.2f s", AudioHalfWidth());

      EndAudioBody();
   }

   // ---- Transport Control (Turbo) ------------------------------------------
   bool StartAudioEngine(std::string& outError); // defined with the audio lifecycle code below

   void DrawTransportControlBody(GraphNode& gn, TransportControlNode* n)
   {
      Transport& transport = Transport::Instance();
      char stat[128];
      const auto& scales = MusicTime::ScaleTypeList();
      const int scaleIdx = std::clamp(transport.Scale(), 0, (int)scales.size() - 1);
      snprintf(stat, sizeof(stat), "%.1f bpm  -  %s %s%s  -  %d/%d  -  %s", transport.Tempo(),
               NoteNameList()[((transport.Key() % 12) + 12) % 12].c_str(), scales[scaleIdx].c_str(),
               n->KeyPending() ? " (next bar)" : "", transport.TimeSigNumerator(), transport.TimeSigDenominator(),
               transport.IsPlaying() ? "playing" : "stopped");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Transport buttons: each is a trigger pin too (Macro Trigger, MIDI pad).
      {
         const float spacing = ImGui::GetStyle().ItemSpacing.x;
         const float bw = (gAudioContentW - 4.0f * spacing) / 5.0f;
         const ImVec2 size(bw, 28.0f);
         // The audio engine itself (same as the toolbar's Start/Stop Audio).
         {
            const bool audioOn = AudioEngine::Instance().SampleRate() > 0.0;
            bool requested = audioOn;
            ImGui::PushStyleColor(ImGuiCol_Button, audioOn ? ImVec4(0.16f, 0.52f, 0.28f, 1.0f)
                                                           : ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
            if (ModStateButton("AUDIO##tcAudio", audioOn, requested, size, /*toggleOnRise=*/true) && requested != audioOn)
            {
               if (!requested)
                  AudioEngine::Instance().Stop();
               else
               {
                  gAudioStartError.clear();
                  if (!StartAudioEngine(gAudioStartError))
                     fprintf(stderr, "audio device: %s\n", gAudioStartError.c_str());
               }
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
         }
         if (ModTriggerButton("PLAY##tcPlay", size))
            n->Play();
         ImGui::SameLine();
         if (ModTriggerButton("STOP##tcStop", size))
            n->Stop();
         ImGui::SameLine();
         if (ModTriggerButton("REWIND##tcRewind", size))
            n->Rewind();
         ImGui::SameLine();
         if (ModTriggerButton("TAP##tcTap", size))
            n->Tap();
      }
      // T taps while the pointer is over this node (no global hijack).
      {
         const ImGuiIO& io = ImGui::GetIO();
         const bool hovered = ed::GetHoveredNode() == ed::NodeId(gn.NodeId());
         if (hovered && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper &&
             ImGui::IsKeyPressed(ImGuiKey_T, false))
            n->Tap();
      }
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      BeginAudioSection("tempo");
      {
         AudioKnobRow row(4, kKnobLarge);
         ImGui::BeginDisabled(!n->driveTempo);
         row.Knob("bpm", &n->bpm, 20.0f, 300.0f, "%.1f", kKnobLarge);
         row.Knob("glide", &n->glide, 0.0f, 16.0f, "%.1f s");
         ImGui::EndDisabled();
         row.Checkbox("drive##tcTempo", &n->driveTempo);
         row.Checkbox("MIDI clock##tcClock", &n->followClock);
         row.End();
      }
      if (n->followClock)
         ImGui::TextDisabled(n->ClockPresent() ? "following MIDI clock" : "MIDI clock: no signal");
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      BeginAudioSection("key / scale");
      {
         AudioKnobRow row(4, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         ImGui::BeginDisabled(!n->driveKey);
         row.Dropdown("key##tcKey", NoteNameList(), std::clamp(n->key, 0, 11),
                      [n](int i) { PushUndoCheckpoint(); n->key = i; });
         row.Dropdown("scale##tcScale", MusicTime::ScaleTypeList(),
                      std::clamp(n->scale, 0, MusicTime::kNumScaleTypes - 1),
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         ImGui::EndDisabled();
         row.Checkbox("drive##tcKeyDrive", &n->driveKey);
         row.Checkbox("on bar##tcOnBar", &n->keyOnBar);
         row.End();
      }
      EndAudioSection();

      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      BeginAudioSection("meter");
      {
         static const std::vector<std::string> kDen = { "/2", "/4", "/8", "/16" };
         AudioKnobRow row(4, kKnobSmall, ImGui::GetFrameHeight() + 5.0f);
         ImGui::BeginDisabled(!n->driveMeter);
         row.KnobInt("beats", &n->meterNum, 1, 16);
         row.Dropdown("unit##tcDen", kDen, std::clamp(n->meterDenIndex, 0, 3),
                      [n](int i) { PushUndoCheckpoint(); n->meterDenIndex = i; });
         ImGui::EndDisabled();
         row.Checkbox("drive##tcMeterDrive", &n->driveMeter);
         row.Skip();
         row.End();
      }
      EndAudioSection();

      // Metronome: `direct` mixes it straight into the device output (no
      // cable, not in recordings); the `click` output pin sends it into the
      // graph for routing through a mixer / Audio Out.
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      BeginAudioSection("metronome");
      {
         AudioKnobRow row(4, kKnobSmall);
         row.Checkbox("click##tcClick", &n->click);
         ImGui::BeginDisabled(!n->click);
         row.Knob("volume", &n->clickVolume, 0.0f, 1.0f, "%.2f");
         ImGui::EndDisabled();
         row.Checkbox("direct##tcClickDirect", &n->clickDirect);
         row.Checkbox("accent##tcClickAccent", &n->clickAccent);
         row.End();
      }
      EndAudioSection();
      AudioToggleButton("audio on open", &n->audioOnOpen, 110.0f);
      ImGui::TextDisabled("T taps tempo while the pointer is over this node");

      EndAudioBody();
   }

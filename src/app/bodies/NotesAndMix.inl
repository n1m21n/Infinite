// Part of src/main.cpp, included there in order (one translation unit).
// audio in, mixer and note processing node bodies

   void DrawAudioInBody(GraphNode& gn, AudioInputNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%+.1f dB   %s",
               n->gainDb, Platform::AudioInputCaptureIsRunning() ? "mic in - live" : "mic in - idle");
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Turbo: which input(s) of the device. Pairs "1+2", "3+4"..., every
      // single channel as mono and (0.48) a mix of all inputs, named after the
      // driver's channel names. Encoded as channelMode (upstream meaning).
      {
         static std::vector<std::string> sNames;
         static std::string sDevice;
         static double sNextRefresh = 0.0;
         if (ImGui::GetTime() >= sNextRefresh)
         {
            sNames = Platform::AudioInputChannelNames();
            sDevice = Platform::AudioInputDeviceName();
            sNextRefresh = ImGui::GetTime() + 1.0;
         }
         n->SyncChannelKeys();
         auto chName = [](int ch) { return ch < (int)sNames.size() ? sNames[ch] : std::string(); };
         const int count = std::max(2, (int)sNames.size());
         std::vector<std::string> options;
         std::vector<std::pair<int, int>> choices; // (channelMode, firstChannel)
         for (int ch = 0; ch + 1 < count; ch += 2)
         {
            std::string label = "in " + std::to_string(ch + 1) + "+" + std::to_string(ch + 2);
            if (!chName(ch).empty())
               label += "  " + chName(ch) + " / " + chName(ch + 1);
            options.push_back(label);
            choices.push_back({ 0, ch });
         }
         for (int ch = 0; ch < count; ch++)
         {
            std::string label = "in " + std::to_string(ch + 1) + " (mono)";
            if (!chName(ch).empty())
               label += "  " + chName(ch);
            options.push_back(label);
            choices.push_back({ ch + 1, ch });
         }
         options.push_back("mix (all inputs)");
         choices.push_back({ AudioInputNode::kChannelModeMix, n->firstChannel });
         int current = -1;
         for (int i = 0; i < (int)choices.size(); i++)
            if (choices[i].first == n->channelMode && (n->channelMode != 0 || choices[i].second == n->firstChannel))
               current = i;
         bool missing = false;
         if (current < 0) // saved on a device with more inputs than this one
         {
            missing = true;
            options.push_back(n->channelMode > 0 ? "in " + std::to_string(n->channelMode) + " (mono)"
                                                 : "in " + std::to_string(n->firstChannel + 1) + "+" +
                                                       std::to_string(n->firstChannel + 2));
            choices.push_back({ n->channelMode, n->firstChannel });
            current = (int)choices.size() - 1;
         }
         DropdownButton("inputs", options, current, [n, choices](int i) {
            PushUndoCheckpoint();
            n->channelMode = choices[i].first;
            n->firstChannel = choices[i].second;
            n->mono = choices[i].first > 0;
            n->SyncChannelKeys();
            n->deviceName = sDevice; // remember which device the choice was made on
         }, gAudioContentW, false);
         ImGui::TextDisabled("%s", sDevice.empty() ? "default input (Settings > Audio)" : sDevice.c_str());
         if (missing && !sNames.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "not on this device");
         else if (!n->deviceName.empty() && !sDevice.empty() && n->deviceName != sDevice)
         {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "set on another device");
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Channels were chosen on \"%s\".\nTurbo reads the input device picked in Settings > Audio.",
                                 n->deviceName.c_str());
         }
      }

      const float faderH = 132.0f;
      const float stripTop = ImGui::GetCursorScreenPos().y;
      DrawStripMeter(gAudioContentX + gAudioContentW * 0.5f + 26.0f, stripTop + 6.0f, 10.0f, faderH - 12.0f,
                     n->Level());
      AudioKnobRow row(1, faderH);
      row.Fader("trim", &n->gainDb, -24.0f, 24.0f, "%.1f dB", faderH);
      row.End();
      EndAudioBody();
   }

   // Rule 2's one summing node (docs/plans/audio/audio-graph-semantics.md
   // §1), drawn the way a summing node is drawn everywhere else: eight
   // vertical channel strips, each with its own live meter, mute and pan,
   // sharing one baseline so the balance between channels is readable as a
   // shape.
   //
   // The knob grid this replaced could only be read one cell at a time - eight
   // identical dials with eight identical captions, which is precisely the
   // "no hierarchy, reads as a spreadsheet" failure §5 warns about. The wide
   // horizontal sum meter that briefly sat above the strips is gone too: every
   // channel already carries its own meter, so it was a duplicate reading of
   // the same signal drawn across the strips' own space.
   void DrawMixerBody(GraphNode& gn, MixerNode* n)
   {
      // Turbo 0.60 (upstream): 0..12 channels with solo. The strips keep
      // Turbo's 100 px each (800 at the default eight).
      const int count = std::clamp(n->numChannels, 0, (int)MixerNode::kMaxSlots);
      const float bodyW = std::max(280.0f, (float)count * 100.0f);
      char stat[64];
      if (count > 0)
         snprintf(stat, sizeof(stat), "%d in -> 1 out   sum %+.1f dB", count,
                  DspMath::LinearToDb(std::max(n->Level(), 1e-5f)));
      else
         snprintf(stat, sizeof(stat), "0 in -> 0 out   idle");
      BeginAudioBody(gn.index, gn.category, bodyW, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Channel count: a stepper, not a modulatable slider - it rebuilds the
      // audio topology, so a cable driving it would rebuild it every block.
      // The counter still advances (upstream's ordinals: channels 0, then the
      // faders and pans); Turbo patches saved before it are shifted on load.
      {
         int ch = n->numChannels;
         gParamCounter++;
         const float btnH = ImGui::GetFrameHeight();
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("channels");
         ImGui::SameLine();
         int next = ch;
         if (ActionButton::Draw("-##mixCh", ImVec2(btnH * 1.4f, btnH)))
            next = ch - 1;
         ImGui::SameLine(0.0f, gap);
         ImGui::AlignTextToFramePadding();
         ImGui::Text("%d", ch);
         ImGui::SameLine(0.0f, gap);
         if (ActionButton::Draw("+##mixCh", ImVec2(btnH * 1.4f, btnH)))
            next = ch + 1;
         next = std::clamp(next, 0, (int)MixerNode::kMaxSlots);
         if (next != ch)
         {
            PushUndoCheckpoint();
            n->numChannels = next;
            RebuildAudioTopology();
         }
         ImGui::Dummy(ImVec2(0.0f, 4.0f));
      }

      if (count > 0)
      {
         const float cellW = gAudioContentW / (float)count;
         const float faderH = 138.0f;
         const float stripTop = ImGui::GetCursorScreenPos().y;

         // Meters first, drawn straight to the draw list so they cost no layout
         // and sit inside each fader's own cell.
         for (int i = 0; i < count; i++)
         {
            const float cx = gAudioContentX + ((float)i + 0.5f) * cellW;
            DrawStripMeter(cx + 14.0f, stripTop + 6.0f, 8.0f, faderH - 12.0f, n->ChannelLevel(i));
         }

         {
            // See DrawGainBody: the row's maxDiameter has to be the fader height.
            AudioKnobRow row(count, faderH);
            for (int i = 0; i < count; i++)
            {
               char label[8];
               snprintf(label, sizeof(label), "%d", i + 1);
               row.Fader(label, &n->gainDb[i], -60.0f, 12.0f, "%.1f dB", faderH, /*dbTaper=*/true);
            }
            row.End();
         }

         // Solo and mute row, aligned to the same cells. A button that isn't
         // directly under its own fader belongs to no channel in particular.
         {
            const float btnW = std::min((cellW - 8.0f) * 0.5f, 30.0f);
            const float rowY = ImGui::GetCursorScreenPos().y;
            for (int i = 0; i < count; i++)
            {
               const float cx = gAudioContentX + ((float)i + 0.5f) * cellW;
               ImGui::SetCursorScreenPos(ImVec2(cx - btnW - 2.0f, rowY));
               ImGui::PushID(9100 + i);
               ImGui::PushStyleColor(ImGuiCol_Button, n->solo[i] ? ImVec4(0.62f, 0.52f, 0.12f, 1.0f)
                                                                 : ImVec4(0.13f, 0.14f, 0.18f, 1.0f));
               bool soloed = n->solo[i];
               if (AudioToggleButton("S##mixerSolo", &soloed, btnW))
               {
                  PushUndoCheckpoint();
                  n->solo[i] = soloed;
               }
               ImGui::PopStyleColor();
               ImGui::PopID();

               ImGui::SetCursorScreenPos(ImVec2(cx + 2.0f, rowY));
               ImGui::PushID(9200 + i);
               ImGui::PushStyleColor(ImGuiCol_Button, n->mute[i] ? ImVec4(0.52f, 0.16f, 0.16f, 1.0f)
                                                                 : ImVec4(0.13f, 0.14f, 0.18f, 1.0f));
               bool muted = n->mute[i];
               if (AudioToggleButton("M##mixerMute", &muted, btnW))
               {
                  PushUndoCheckpoint();
                  n->mute[i] = muted;
               }
               ImGui::PopStyleColor();
               ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowY));
            ImGui::Dummy(ImVec2(gAudioContentW, ImGui::GetFrameHeight() + 4.0f));
         }

         {
            AudioKnobRow row(count, kKnobStd);
            for (int i = 0; i < count; i++)
            {
               char label[8];
               snprintf(label, sizeof(label), "pan %d", i + 1);
               row.Knob(label, &n->pan[i], -1.0f, 1.0f, "%.2f", kKnobStd);
            }
            row.End();
         }
      }

      EndAudioBody();
   }

   // No params - Splitter is a passthrough fan-out point, not a mixing
   // decision (audio-graph-semantics.md §1). Narrow body: there is nothing
   // here to spend 440px on.
   void DrawSplitterBody(GraphNode& gn, SplitterNode*)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "1 in -> up to %d out", SplitterNode::kMaxFanout);
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      EndAudioBody();
   }

   // Two octaves of piano keyboard showing what is held right now. This is the
   // MIDI Notes node's visualizer for the same reason the sequencer's step
   // grid was its own: it is the single piece of feedback that tells you
   // whether the node is receiving anything at all, which is the first thing
   // you need to know when a patch is silent.
   void DrawMidiKeyboard(const bool held[128], int lowNote, int octaves)
   {
      const float w = gAudioBodyW, h = 58.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
      static const int kBlackOffsets[5] = { 1, 3, 6, 8, 10 };
      static const float kBlackSlot[5] = { 0.0f, 1.0f, 3.0f, 4.0f, 5.0f };

      const int whiteCount = 7 * octaves;
      const float keyW = (w - 4.0f) / (float)whiteCount;

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + (float)(o * 7 + k) * keyW;
            const ImU32 whiteCol = on ? (isLight ? IM_COL32(50, 130, 245, 255) : IM_COL32(120, 200, 255, 245))
                                      : (isLight ? IM_COL32(250, 250, 255, 255) : IM_COL32(206, 210, 222, 255));
            dl->AddRectFilled(ImVec2(x + 0.5f, origin.y + 3.0f), ImVec2(x + keyW - 0.5f, br.y - 3.0f),
                              whiteCol, 2.0f);
         }
      }
      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 5; k++)
         {
            const int note = lowNote + o * 12 + kBlackOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[k] + 1.0f) * keyW - keyW * 0.3f;
            const ImU32 blackCol = on ? (isLight ? IM_COL32(30, 100, 230, 255) : IM_COL32(90, 170, 235, 255))
                                      : (isLight ? IM_COL32(60, 65, 80, 255) : IM_COL32(26, 28, 36, 255));
            dl->AddRectFilled(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                              blackCol, 2.0f);
         }
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawMidiNotesBody(GraphNode& gn, MidiNotesNode* n)
   {
      bool held[128];
      n->HeldKeys(held);
      const int count = n->HeldCount();

      char stat[80];
      if (!Platform::MidiIsRunning())
         snprintf(stat, sizeof(stat), "no MIDI input");
      else if (count > 0)
         snprintf(stat, sizeof(stat), "%d key%s held", count, count == 1 ? "" : "s");
      else if (n->channel < 0)
         snprintf(stat, sizeof(stat), "listening  -  omni");
      else
         snprintf(stat, sizeof(stat), "listening  -  ch %d only", n->channel + 1);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // Centred on the last note played so a keyboard playing outside the
      // default range still shows something, rather than sitting dark.
      const int last = n->LastNote();
      const int lowNote = last >= 0 ? std::clamp((last / 12) * 12 - 12, 0, 108) : 48;
      DrawMidiKeyboard(held, lowNote, 3);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      {
         // Turbo 0.50: channel is a dropdown ("omni", "ch 1".."ch 16") instead
         // of a 0-based knob, plus "learn" (takes the next note-on's channel)
         // and a readout of the last channel heard, so a controller whose pads
         // and keys send on different channels can be split by two nodes.
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float learnW = 62.0f;
         MidiChannelDropdown("channel##midiNotesCh", &n->channel, true,
                             std::max(90.0f, gAudioContentW - learnW - gap - ImGui::CalcTextSize("channel").x - gap));
         ImGui::SameLine();
         if (n->IsLearningChannel())
         {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.1f, 1.0f));
            if (ImGui::Button("cancel##midiNotesLearn", ImVec2(learnW, 0)))
               n->CancelChannelLearn();
            ImGui::PopStyleColor();
         }
         else if (ImGui::Button("learn##midiNotesLearn", ImVec2(learnW, 0)))
         {
            PushUndoCheckpoint();
            n->StartChannelLearn();
         }
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("set the channel from the next note you play\n(hit a pad to listen only to the pads)");
         const int lastCh = n->LastChannel();
         if (n->IsLearningChannel())
            ImGui::TextDisabled("play a note on the channel to keep...");
         else if (lastCh >= 0)
            ImGui::TextDisabled("last note in: ch %d%s", lastCh + 1,
                                (n->channel >= 0 && n->channel != lastCh) ? "  (filtered out)" : "");
         else
            ImGui::TextDisabled("last note in: none yet");
         // The old channel knob took a positional modulation slot: keep it so
         // transpose/velocity keep their pin numbers in saved patches.
         gParamCounter++;
      }
      {
         AudioKnobRow row(2);
         row.KnobInt("transpose", &n->transpose, -24, 24, kKnobLarge);
         row.Knob("velocity", &n->velocityScale, 0.0f, 2.0f, "%.2f", kKnobLarge);
         row.End();
      }

      ModCheckbox("Snap to Key##midiSnap", &n->useGlobalScale);

      BeginAudioSection("input");
      if (!Platform::MidiIsRunning())
      {
         if (ImGui::Button("MIDI Learn", ImVec2(kPreviewSize, 0)))
            n->StartListening();
         ImGui::TextDisabled("click to start listening for a connected controller");
      }
      else
      {
         ImGui::TextUnformatted(Platform::MidiDeviceSummary().c_str());
      }
      EndAudioSection();

      EndAudioBody();
   }

   const std::vector<std::string>& NoteNameList()
   {
      static std::vector<std::string> list = { "C", "C#", "D", "D#", "E", "F",
                                                "F#", "G", "G#", "A", "A#", "B" };
      return list;
   }

   void DrawCVToPitchParams(CVToPitchNode* n)
   {
      // The semitone readout is the point of this node - make it the
      // visually dominant element, not a text line under six sliders.
      char st[16];
      snprintf(st, sizeof(st), "%+d st", n->LastSemitone());
      ImGui::SetWindowFontScale(1.8f);
      ImGui::TextUnformatted(st);
      ImGui::SetWindowFontScale(1.0f);

      if (n->input == nullptr)
         ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      else
         ImGui::TextDisabled("in: patched");

      if (ModSliderInt("range low", &n->rangeLow, -48, 48))
         n->rangeHigh = std::max(n->rangeHigh, n->rangeLow + 1);
      if (ModSliderInt("range high", &n->rangeHigh, -48, 48))
         n->rangeLow = std::min(n->rangeLow, n->rangeHigh - 1);

      DropdownButton("scale", MusicTime::ScaleTypeList(), n->scale,
                     [n](int i) { n->scale = i; });
      if (n->scale != MusicTime::kChromatic)
         DropdownButton("root", NoteNameList(), n->root, [n](int i) { n->root = i; });

      ModSlider("glide (ms)", &n->glideMs, 0.0f, 2000.0f);
   }

   // Two-octave keyboard centred on the filter's root, coloured by what the
   // gate would do to each pitch - computed purely from params (main thread
   // only, redraws every frame like DrawADSRVisualizer), plus one live marker
   // for the last note the audio thread actually saw: a ring around the key,
   // coloured by whether it passed. That marker is the only thing read from
   // the audio side, via the same two-atomic publish LastNote() uses.
   void DrawNoteFilterVisualizer(NoteFilterNode* n)
   {
      const float w = gAudioBodyW, h = 58.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
      static const int kBlackOffsets[5] = { 1, 3, 6, 8, 10 };
      static const float kBlackSlot[5] = { 0.0f, 1.0f, 3.0f, 4.0f, 5.0f };
      const int octaves = 2;
      const int lowNote = std::clamp(48 + n->root, 0, 108); // one octave below/above middle C, centred on root
      const int whiteCount = 7 * octaves;
      const float keyW = (w - 4.0f) / (float)whiteCount;
      const int lastIn = n->LastNoteIn();
      const bool lastPassed = n->LastPassed();

      auto KeyColor = [n, isLight](int note, bool isWhite) -> ImU32 {
         const bool inRange = note >= n->rangeLow && note <= n->rangeHigh;
         const int pc = ((note - n->root) % 12 + 12) % 12;
         const bool inScale = MusicTime::ScaleContainsPitchClass(n->scale, pc);
         const bool active = inRange && inScale;
         if (isWhite)
            return active ? (isLight ? IM_COL32(250, 250, 255, 255) : IM_COL32(206, 210, 222, 255))
                          : (isLight ? IM_COL32(200, 205, 218, 255) : IM_COL32(72, 76, 90, 255));
         return active ? (isLight ? IM_COL32(30, 100, 230, 255) : IM_COL32(90, 170, 235, 255))
                       : (isLight ? IM_COL32(110, 115, 130, 255) : IM_COL32(26, 28, 36, 255));
      };

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const float x = origin.x + 2.0f + (float)(o * 7 + k) * keyW;
            dl->AddRectFilled(ImVec2(x + 0.5f, origin.y + 3.0f), ImVec2(x + keyW - 0.5f, br.y - 3.0f),
                              KeyColor(note, true), 2.0f);
            if (note == lastIn)
               dl->AddRect(ImVec2(x + 1.0f, origin.y + 4.0f), ImVec2(x + keyW - 1.0f, br.y - 4.0f),
                          lastPassed ? IM_COL32(120, 200, 255, 255) : IM_COL32(255, 96, 86, 255), 2.0f, 0, 2.0f);
         }
      }
      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 5; k++)
         {
            const int note = lowNote + o * 12 + kBlackOffsets[k];
            const float x = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[k] + 1.0f) * keyW - keyW * 0.3f;
            dl->AddRectFilled(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                              KeyColor(note, false), 2.0f);
            if (note == lastIn)
               dl->AddRect(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                          lastPassed ? IM_COL32(120, 200, 255, 255) : IM_COL32(255, 96, 86, 255), 2.0f, 0, 2.0f);
         }
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawNoteFilterBody(GraphNode& gn, NoteFilterNode* n)
   {
      char stat[96];
      if (n->LastNoteIn() < 0)
         snprintf(stat, sizeof(stat), "%s in %s..%s",
                  MusicTime::ScaleTable(n->scale).name, NoteNameList()[n->rangeLow % 12].c_str(),
                  NoteNameList()[n->rangeHigh % 12].c_str());
      else
         snprintf(stat, sizeof(stat), "last: %s%d - %s", NoteNameList()[n->LastNoteIn() % 12].c_str(),
                  n->LastNoteIn() / 12 - 1, n->LastPassed() ? "passed" : "blocked");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawNoteFilterVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      {
         AudioKnobRow row(5);
         if (n->useGlobalScale)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), n->scale,
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         row.Dropdown("root", NoteNameList(), n->root,
                      [n](int i) { PushUndoCheckpoint(); n->root = i; });
         if (n->useGlobalScale)
            ImGui::EndDisabled();
         row.KnobInt("lo", &n->rangeLow, 0, 127, kKnobSmall);
         row.KnobInt("hi", &n->rangeHigh, 0, 127, kKnobSmall);
         row.Knob("chance", &n->chance, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

      ModCheckbox("Global Scale##filterGlobal", &n->useGlobalScale);

      EndAudioBody();
   }

   // Shared by every tempo-synced note generator (Arp, Note Sequencer,
   // Random Note Generator): a rate expressed either as a note division
   // (synced to tempo) or free seconds - see QuantizerNode::div's header
   // comment (NoteNodes.h) for the equivalent quantize-grid table.
   static const std::vector<std::string> kRateDivisionLabels = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
   static const float kRateDivisionBeats[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
   int NearestRateDivision(float beats)
   {
      int best = 2;
      float bestDiff = 1e9f;
      for (int i = 0; i < 6; i++)
      {
         const float d = std::fabs(kRateDivisionBeats[i] - beats);
         if (d < bestDiff) { bestDiff = d; best = i; }
      }
      return best;
   }

   // Draws a "rateMode" toggle plus whichever rate control it selects -
   // a division dropdown when synced, a seconds knob when free. `rowSlots`
   // must match the AudioKnobRow this is called inside (it draws exactly 2
   // controls: the mode toggle and the rate itself).
   void DrawRateModeControls(AudioKnobRow& row, int* rateMode, float* rateBeats, float* rateSeconds)
   {
      static const std::vector<std::string> kRateModes = { "Synced", "Free" };
      row.Dropdown("sync", kRateModes, *rateMode, [rateMode](int i) { PushUndoCheckpoint(); *rateMode = i; });
      if (*rateMode == 0)
      {
         int div = NearestRateDivision(*rateBeats);
         row.Dropdown("rate", kRateDivisionLabels, div, [rateBeats](int i) {
            PushUndoCheckpoint();
            *rateBeats = kRateDivisionBeats[std::clamp(i, 0, 5)];
         });
         // The rate knob isn't drawn in Synced mode, but it still must consume
         // a paramIndex ordinal (see ModKnob's comment on gParamCounter):
         // ordinals are assigned by draw order, so skipping this one would
         // shift every later knob (e.g. "gate") down into it, silently
         // repointing any cable that's patched into rate onto gate. Registering
         // the param without drawing it keeps the ordinal reserved and the
         // link alive-but-invisible - same trick as a collapsed node's hidden
         // knobs (CollapsedBindingPins) - so it comes right back once the user
         // flips back to Free instead of jumping to a different parameter.
         ParamRef rateSlot;
         rateSlot.nodeIndex = gCurrentNodeIndex;
         rateSlot.paramIndex = gParamCounter++;
         rateSlot.value = rateSeconds;
         rateSlot.minValue = 0.02f;
         rateSlot.maxValue = 2.0f;
         rateSlot.name = "rate";
         Modulation::Instance().RegisterParam(rateSlot);
      }
      else
      {
         row.Knob("rate", rateSeconds, 0.02f, 2.0f, "%.2fs", kKnobSmall);
      }
   }

   // Shared "quantize to grid" dropdown - Off, 1/4, 1/8, 1/16, 1/32 - used by
   // Quantizer (live note-on timing) and Note Capturer (recorded timing).
   static const std::vector<std::string> kQuantizeLabels = { "Off", "1/4", "1/8", "1/16", "1/32" };

   // Standard shape for a one-knob note node (Note Transpose, Pitch Bend,
   // Gate, Glide, Vibrato below) - narrow body, one big centred knob, no
   // second row. Standardised so every one-knob note node looks like these
   // rather than each inventing its own compact layout.
   void DrawSingleKnobAudioBody(GraphNode& gn, const char* stat, const char* label, float* v, float lo, float hi,
                                const char* fmt)
   {
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      AudioKnobRow row(1, kKnobLarge);
      row.Knob(label, v, lo, hi, fmt, kKnobLarge);
      row.End();
      EndAudioBody();
   }

   // Int counterpart of DrawSingleKnobAudioBody. A plain float local can't be
   // used here and then written back to an int param after the call, because
   // ModKnob registers the local's address with Modulation for the apply pass
   // that runs later in the frame - by then the local is a dangling stack
   // pointer. KnobInt's ModKnobInt keeps its persistent float in
   // gIntParamStore instead, so the registered pointer stays valid.
   void DrawSingleKnobIntAudioBody(GraphNode& gn, const char* stat, const char* label, int* v, int lo, int hi)
   {
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      AudioKnobRow row(1, kKnobLarge);
      row.KnobInt(label, v, lo, hi, kKnobLarge);
      row.End();
      EndAudioBody();
   }

   void DrawNoteTransposeBody(GraphNode& gn, NoteTransposeNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%+d st", n->semitones);
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      {
         AudioKnobRow row(1, kKnobLarge);
         row.KnobInt("transpose", &n->semitones, -48, 48, kKnobLarge);
         row.End();
      }
      ModCheckbox("Snap to Key##transposeSnap", &n->useGlobalScale);
      EndAudioBody();
   }

   void DrawPitchBendBody(GraphNode& gn, PitchBendNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%+.2f st", n->bendSemitones);
      DrawSingleKnobAudioBody(gn, stat, "bend", &n->bendSemitones, -PitchBendNode::kRange, PitchBendNode::kRange,
                               "%+.2f st");
   }

   void DrawGateBody(GraphNode& gn, GateNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", n->holdMs > 0.0f ? "gated" : "off (passthrough)");
      DrawSingleKnobAudioBody(gn, stat, "hold", &n->holdMs, 0.0f, 3000.0f, "%.0f ms");
   }

   void DrawGlideBody(GraphNode& gn, GlideNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", n->glideMs > 0.0f ? "gliding" : "off");
      DrawSingleKnobAudioBody(gn, stat, "glide", &n->glideMs, 0.0f, 2000.0f, "%.0f ms");
   }

   void DrawVibratoBody(GraphNode& gn, VibratoNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%.1f Hz", n->rateHz);
      DrawSingleKnobAudioBody(gn, stat, "rate", &n->rateHz, 0.5f, 12.0f, "%.1f Hz");
   }

   // Velocity Curve's chart: y = x^curve over 0..1, both axes normalized -
   // the same "show the transfer function" idea as Audio Filter's response
   // curve or Dynamics' operating-point dot, scaled to this node's narrow
   // body. Read-only (the curve is fully determined by the one knob below
   // it), but redrawn from the live curve value every frame like every
   // other params-only visualizer in this file.
   void DrawVelocityCurveChart(float curve)
   {
      const float w = gAudioBodyW;
      const float h = w; // square, like Bouncing Balls' canvas
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      for (int i = 1; i < 4; i++)
      {
         dl->AddLine(ImVec2(origin.x, origin.y + h * i / 4.0f), ImVec2(br.x, origin.y + h * i / 4.0f),
                     ScopeGridCol(), 1.0f);
         dl->AddLine(ImVec2(origin.x + w * i / 4.0f, origin.y), ImVec2(origin.x + w * i / 4.0f, br.y),
                     ScopeGridCol(), 1.0f);
      }
      // The linear reference (curve == 1) as a dim diagonal, so the knob's
      // effect reads relative to "unchanged" rather than in isolation.
      dl->AddLine(ImVec2(origin.x, br.y), ImVec2(br.x, origin.y), ScopeMidLineCol(), 1.0f);

      dl->PathClear();
      const int kSteps = 32;
      for (int i = 0; i <= kSteps; i++)
      {
         const float x = (float)i / (float)kSteps;
         const float y = std::pow(x, curve);
         dl->PathLineTo(ImVec2(origin.x + x * w, br.y - y * h));
      }
      dl->PathStroke(isLight ? IM_COL32(30, 110, 230, 255) : IM_COL32(150, 214, 255, 245), 0, 1.8f);

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::Dummy(ImVec2(w, h));
   }

   void DrawVelocityCurveBody(GraphNode& gn, VelocityCurveNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "in %.0f%% -> out %.0f%%", n->LastVelocityIn() * 100.0f,
               n->LastVelocityOut() * 100.0f);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      DrawVelocityCurveChart(n->curve);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      AudioKnobRow row(1, kKnobLarge);
      row.Knob("curve", &n->curve, 0.25f, 4.0f, "%.2f", kKnobLarge);
      row.End();
      EndAudioBody();
   }

   void DrawHumanizerBody(GraphNode& gn, HumanizerNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%.0f ms, %.0f%% vel", n->timingMs, n->velocityPct);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(2, kKnobLarge);
      row.Knob("hu.time", &n->timingMs, 0.0f, 100.0f, "%.0f ms", kKnobLarge);
      row.Knob("hu.vel", &n->velocityPct, 0.0f, 100.0f, "%.0f%%", kKnobLarge);
      row.End();
      EndAudioBody();
   }

   void DrawQuantizerBody(GraphNode& gn, QuantizerNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", kQuantizeLabels[std::clamp(n->div, 0, 4)].c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(1, kKnobLarge);
      row.Dropdown("quantize", kQuantizeLabels, n->div, [n](int i) {
         PushUndoCheckpoint();
         n->div = i;
      });
      row.End();
      EndAudioBody();
   }


   // Note names for dropdowns (C-1 .. G9), used by the ported Predictive nodes.
   const std::vector<std::string>& MidiNoteNameList()
   {
      static std::vector<std::string> names;
      if (names.empty())
      {
         static const char* kPc[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
         for (int n = 0; n < 128; n++)
            names.push_back(std::string(kPc[n % 12]) + std::to_string(n / 12 - 1));
      }
      return names;
   }

   // ---- Predictive nodes (ported from upstream Infinite) ----------------------
   void DrawPredictiveQuantizeBody(GraphNode& gn, PredictiveQuantizeNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      if (n->ModeCount() > 0)
         snprintf(stat, sizeof(stat), "%d spacings  -  %d onsets total", n->ModeCount(), n->TotalCaptured());
      else if (n->TotalCaptured() > 0)
         snprintf(stat, sizeof(stat), "listening  -  %d onsets", n->TotalCaptured());
      else
         snprintf(stat, sizeof(stat), "wire notes in - always adapting");

      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%d%% conf", (int)std::round(conf * 100.0f));
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNarrowWidth - bsz.x - 7.0f, statusPos.y + 3.0f),
                     IM_COL32(34, 197, 94, 255), confBadge);
      }

      {
         AudioKnobRow row(1);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   void DrawPredictiveVelocityBody(GraphNode& gn, PredictiveVelocityNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      if (n->HasCurve())
         snprintf(stat, sizeof(stat), "dynamic range learned  -  %d notes total", n->TotalCaptured());
      else if (n->TotalCaptured() > 0)
         snprintf(stat, sizeof(stat), "listening  -  %d notes", n->TotalCaptured());
      else
         snprintf(stat, sizeof(stat), "wire notes in - always adapting");

      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%d%% conf", (int)std::round(conf * 100.0f));
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNarrowWidth - bsz.x - 7.0f, statusPos.y + 3.0f),
                     IM_COL32(34, 197, 94, 255), confBadge);
      }

      {
         AudioKnobRow row(1);
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   void DrawPredictiveRhythmBody(GraphNode& gn, PredictiveRhythmNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      const std::string rootName = MidiNoteNameList()[std::clamp(n->root, 0, 127)];
      if (n->IsLearning() && n->Dropped() > 0)
         snprintf(stat, sizeof(stat), "learning  -  %d notes (%d dropped)", n->NotesCaptured(), n->Dropped());
      else if (n->IsLearning())
         snprintf(stat, sizeof(stat), "learning  -  %d notes", n->NotesCaptured());
      else if (n->Building())
         snprintf(stat, sizeof(stat), "building model...");
      else if (n->LastLearnTooShort() && n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "too short, kept %d notes learned", n->LearnedNotes());
      else if (n->LastLearnTooShort())
         snprintf(stat, sizeof(stat), "too short to learn, wire notes in");
      else if (n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "%d notes learned, root %s", n->LearnedNotes(), rootName.c_str());
      else
         snprintf(stat, sizeof(stat), "wire notes in, press Learn");

      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%d%% conf", (int)std::round(conf * 100.0f));
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNarrowWidth - bsz.x - 7.0f, statusPos.y + 3.0f),
                     IM_COL32(34, 197, 94, 255), confBadge);
      }

      {
         const float w = gAudioContentW;
         const float h = ImGui::GetFrameHeight();
         const bool learning = n->IsLearning();
         // Turbo 0.49: mappable (CV / MIDI / Performance).
         bool requestedLearning = learning;
         if (ModStateButton(learning ? "Stop##prLearn" : "Learn##prLearn", learning, requestedLearning, ImVec2(w, h)))
         {
            PushUndoCheckpoint();
            n->SetLearning(requestedLearning);
         }
      }
      {
         AudioKnobRow row(2);
         row.Dropdown("root", MidiNoteNameList(), n->root, [n](int i) { n->root = i; });
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   void DrawPredictiveNotesBody(GraphNode& gn, PredictiveNotesNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      if (n->IsLearning() && n->Dropped() > 0)
         snprintf(stat, sizeof(stat), "learning  -  %d notes, %d bars (%d dropped)", n->NotesCaptured(), n->BarsCaptured(), n->Dropped());
      else if (n->IsLearning())
         snprintf(stat, sizeof(stat), "learning  -  %d notes, %d bars", n->NotesCaptured(), n->BarsCaptured());
      else if (n->Building())
         snprintf(stat, sizeof(stat), "building model...");
      else if (n->LastLearnTooShort() && n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "too short, kept %d notes learned", n->LearnedNotes());
      else if (n->LastLearnTooShort())
         snprintf(stat, sizeof(stat), "too short to learn, wire more notes in");
      else if (n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "%d notes learned", n->LearnedNotes());
      else
         snprintf(stat, sizeof(stat), "wire notes in, press Learn");

      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%d%% conf", (int)std::round(conf * 100.0f));
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNodeWidth - bsz.x - 7.0f, statusPos.y + 3.0f),
                     IM_COL32(34, 197, 94, 255), confBadge);
      }

      {
         const float w = gAudioContentW;
         const float h = ImGui::GetFrameHeight();
         const bool learning = n->IsLearning();
         // Turbo 0.49: mappable (CV / MIDI / Performance).
         bool requestedLearning = learning;
         if (ModStateButton(learning ? "Stop##predLearn" : "Learn##predLearn", learning, requestedLearning,
                            ImVec2(w * 0.3f, h)))
         {
            PushUndoCheckpoint();
            n->SetLearning(requestedLearning);
         }
         ImGui::SameLine();
         // Learning meter: how much the model beats a memoryless one, per captured bar.
         const ImVec2 p0 = ImGui::GetCursorScreenPos();
         const float mw = w - w * 0.3f - ImGui::GetStyle().ItemSpacing.x;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(p0, ImVec2(p0.x + mw, p0.y + h), IM_COL32(255, 255, 255, 14), 3.0f);
         const auto& c = n->Curve();
         if (c.size() >= 2)
         {
            float hi = 0.1f;
            for (float v : c)
               hi = std::max(hi, v);
            for (size_t i = 1; i < c.size(); i++)
            {
               const float x0 = p0.x + mw * (float)(i - 1) / (float)(c.size() - 1);
               const float x1 = p0.x + mw * (float)i / (float)(c.size() - 1);
               const float y0 = p0.y + h - 3.0f - (h - 6.0f) * std::clamp(c[i - 1] / hi, 0.0f, 1.0f);
               const float y1 = p0.y + h - 3.0f - (h - 6.0f) * std::clamp(c[i] / hi, 0.0f, 1.0f);
               dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(120, 200, 140, 230), 1.5f);
            }
         }
         if (conf > 0.0f)
         {
            char confStr[32];
            snprintf(confStr, sizeof(confStr), "%d%%", (int)std::round(conf * 100.0f));
            const ImVec2 csz = ImGui::CalcTextSize(confStr);
            dl->AddText(ImVec2(p0.x + mw - csz.x - 6.0f, p0.y + (h - csz.y) * 0.5f),
                        IM_COL32(34, 197, 94, 255), confStr);
         }
         ImGui::Dummy(ImVec2(mw, h));
      }
      {
         AudioKnobRow row(4);
         row.Knob("stray", &n->stray, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.KnobInt("memory", &n->memory, 0, 8);
         row.Knob("length", &n->lengthSpread, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("velocity", &n->velocitySpread, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.KnobInt("low", &n->rangeLow, 0, 127);
         row.KnobInt("high", &n->rangeHigh, 0, 127);
         row.End();
      }

      EndAudioBody();
   }

   void DrawNoteEchoBody(GraphNode& gn, NoteEchoNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%d repeats, %d pending", n->repeats, n->PendingCount());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(2);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.End();
      }
      {
         AudioKnobRow row(2);
         row.KnobInt("repeats", &n->repeats, 1, 8);
         row.Knob("decay", &n->decay, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }
      {
         AudioKnobRow row(2);
         row.KnobInt("transpose", &n->transposePerRepeat, -12, 12);
         row.End();
      }

      ModCheckbox("Mute Dry##echoMuteDry", &n->muteDry);

      EndAudioBody();
   }

   void DrawNoteMergeBody(GraphNode& gn, NoteMergeNode* n)
   {
      int active = 0;
      for (int i = 0; i < NoteMergeNode::kSlots; i++)
         if (n->noteInputs[i].IsConnected())
            active++;

      char stat[32];
      snprintf(stat, sizeof(stat), "%d active", active);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      // Four lit dots showing which inputs are currently connected - the
      // same visual language as Note Router's output dots, mirrored to
      // inputs since Merge is fan-in rather than fan-out.
      {
         const float w = gAudioBodyW;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float dia = 14.0f;
         const float gap = (w - (float)NoteMergeNode::kSlots * dia) / (float)(NoteMergeNode::kSlots + 1);
         for (int i = 0; i < NoteMergeNode::kSlots; i++)
         {
            const float cx = origin.x + gap * (float)(i + 1) + dia * (float)i + dia * 0.5f;
            const bool lit = n->noteInputs[i].IsConnected();
            dl->AddCircleFilled(ImVec2(cx, origin.y + dia * 0.5f), dia * 0.5f,
                                lit ? IM_COL32(120, 200, 255, 255) : IM_COL32(50, 53, 64, 255));
         }
         ImGui::Dummy(ImVec2(w, dia + 6.0f));
      }

      EndAudioBody();
   }

   void DrawNoteRouterBody(GraphNode& gn, NoteRouterNode* n)
   {
      static const std::vector<std::string> kModes = { "Round Robin", "Random", "Probability", "Chain" };
      const int mask = n->LastRoutedMask();
      char stat[64];
      snprintf(stat, sizeof(stat), "%s%s", kModes[std::clamp(n->mode, 0, 3)].c_str(),
               mask == 0 ? "" : " - active");

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      // Four lit dots showing which outputs are currently carrying a held note.
      {
         const float w = gAudioBodyW;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float dia = 14.0f;
         const float gap = (w - 4.0f * dia) / 5.0f;
         for (int o = 0; o < 4; o++)
         {
            const float cx = origin.x + gap * (float)(o + 1) + dia * (float)o + dia * 0.5f;
            const bool lit = (mask & (1 << o)) != 0;
            dl->AddCircleFilled(ImVec2(cx, origin.y + dia * 0.5f), dia * 0.5f,
                                lit ? IM_COL32(120, 200, 255, 255) : IM_COL32(50, 53, 64, 255));
         }
         ImGui::Dummy(ImVec2(w, dia + 6.0f));
      }

      {
         const bool showProb = n->mode == NoteRouterNode::kProbability;
         AudioKnobRow row(showProb ? 2 : 1);
         row.Dropdown("mode", kModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; });
         if (showProb)
            row.Knob("prob", &n->probability, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

      EndAudioBody();
   }

   // Drag state for the gate grid's paint gesture - file-scope like
   // gDrumGridDrag, since a drag spans many frames and many per-cell
   // InvisibleButton calls. A distinct struct from the drum grid's (rather
   // than reused) since this one has no lane dimension, just a step origin.
   struct ArpGateDragState
   {
      bool active = false;
      bool paintOn = false;
      int originStep = -1;
   };
   ArpGateDragState gArpGateDrag;

   // The 8-step gate grid: both the visualizer and the primary editable
   // control (replaces the old per-mode decorative shape, which couldn't
   // scale to 15 modes and wasn't interactive). Click/drag-paint toggles a
   // step's gate; the currently-sounding step lights up as the playhead.
   void DrawArpGateGrid(ArpeggiatorNode* n)
   {
      const int steps = ArpeggiatorNode::kGateSteps;
      const float gap = 3.0f;
      const float w = gAudioBodyW;
      const float cellW = (w - gap * (float)(steps - 1)) / (float)steps;
      const float h = std::min(cellW, 46.0f);
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const int playStep = n->CurrentGridStep();

      for (int s = 0; s < steps; s++)
      {
         const float x0 = origin.x + (float)s * (cellW + gap);
         const ImVec2 cellMin(x0, origin.y);
         const ImVec2 cellMax(x0 + cellW, origin.y + h);

         ImGui::PushID(s);
         ImGui::SetCursorScreenPos(cellMin);
         ImGui::InvisibleButton("gatecell", ImVec2(cellW, h));

         const bool wasOn = ((n->stepGates >> s) & 1) != 0;
         if (ImGui::IsItemActivated())
         {
            PushUndoCheckpoint();
            if (wasOn)
               n->stepGates &= ~(1 << s);
            else
               n->stepGates |= (1 << s);
            gArpGateDrag.active = true;
            gArpGateDrag.paintOn = !wasOn;
            gArpGateDrag.originStep = s;
         }
         else if (gArpGateDrag.active && s != gArpGateDrag.originStep && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                  ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
         {
            if (gArpGateDrag.paintOn)
               n->stepGates |= (1 << s);
            else
               n->stepGates &= ~(1 << s);
         }

         const bool on = ((n->stepGates >> s) & 1) != 0;
         const bool isPlayhead = (playStep == s);

         const bool isLight = IsThemeLight();
         dl->AddRectFilled(cellMin, cellMax, isLight ? IM_COL32(220, 225, 235, 255) : IM_COL32(11, 12, 16, 255), 4.0f);
         if (on)
            dl->AddRectFilled(cellMin, cellMax,
                               isPlayhead ? (isLight ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 242, 255, 255))
                                          : (isLight ? IM_COL32(40, 120, 235, 240) : IM_COL32(150, 214, 255, 200)), 4.0f);
         dl->AddRect(cellMin, cellMax, isPlayhead ? (isLight ? IM_COL32(30, 100, 230, 255) : IM_COL32(255, 255, 255, 255))
                                                  : (isLight ? IM_COL32(180, 188, 204, 255) : IM_COL32(64, 68, 84, 255)), 4.0f,
                     0, isPlayhead ? 2.0f : 1.0f);

         ImGui::PopID();
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         gArpGateDrag.active = false;

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
   }

   // Turbo 0.50: the shared quantized-restart row of the sequencers (Chord
   // Progression, Note Sequencer, Arpeggiator, MIDI File). "restart" and
   // "restart q" are discrete controls keyed by label, so they get stable
   // pins for MIDI learn, CV and Performance Mode; a modulator's rising edge
   // on the "restart" pin fires it too. Returns true when restart fired.
   bool DrawRestartControls(int* quant, bool armed)
   {
      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      const bool fired = ModTriggerButton("restart", ImVec2(92.0f, 0.0f));
      if (ImGui::IsItemHovered())
         SetAudioReadout("restart", "starts again from the top at the next grid line");
      ImGui::SameLine();
      DropdownButton("restart q", QuantizedRestart::Names(),
                     std::clamp(*quant, 0, QuantizedRestart::kNumQuants - 1),
                     [quant](int i) {
                        if (!gApplyingDiscreteCv)
                           PushUndoCheckpoint();
                        *quant = i;
                        gPatchDirty = true;
                     },
                     118.0f, true);
      if (armed)
      {
         // Blinking dot + caption while the restart waits for its grid line.
         ImGui::SameLine();
         const ImVec2 p = ImGui::GetCursorScreenPos();
         const float h = ImGui::GetFrameHeight();
         const bool blink = std::fmod(ImGui::GetTime(), 0.5) < 0.25;
         ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + h * 0.5f), 4.5f,
                                                     blink ? IM_COL32(255, 170, 60, 255) : IM_COL32(150, 95, 30, 255));
         ImGui::Dummy(ImVec2(14.0f, h));
         ImGui::SameLine(0.0f, 2.0f);
         ImGui::TextUnformatted("armed");
      }
      return fired;
   }

   void DrawArpeggiatorBody(GraphNode& gn, ArpeggiatorNode* n)
   {
      // Display order per the design ask; enum values (mode's serialized
      // int) must not move, so this is a separate indirection rather than
      // a reorder of the enum itself - see ArpeggiatorNode::Mode's comment.
      static const std::vector<std::string> kModeLabels = {
         "Up", "Down", "Up/Down", "Down/Up", "Random", "As Played", "Repeat x2", "Repeat x4",
         "Join", "Spread", "Join/Spread", "Stairs Up", "Stairs Down", "Diverge", "Converge",
      };
      static const int kModeValues[] = {
         ArpeggiatorNode::kUp,        ArpeggiatorNode::kDown,       ArpeggiatorNode::kUpDown,
         ArpeggiatorNode::kDownUp,    ArpeggiatorNode::kRandom,     ArpeggiatorNode::kAsPlayed,
         ArpeggiatorNode::kRepeat2,   ArpeggiatorNode::kRepeat4,    ArpeggiatorNode::kJoin,
         ArpeggiatorNode::kSpread,    ArpeggiatorNode::kJoinSpread, ArpeggiatorNode::kStairsUp,
         ArpeggiatorNode::kStairsDown, ArpeggiatorNode::kDiverge,   ArpeggiatorNode::kConverge,
      };
      int modeUiIndex = 0;
      for (int i = 0; i < (int)kModeLabels.size(); i++)
         if (kModeValues[i] == n->mode) { modeUiIndex = i; break; }

      char stat[64];
      const int cur = n->CurrentNote();
      if (cur < 0)
         snprintf(stat, sizeof(stat), "%d held", n->HeldCount());
      else
         snprintf(stat, sizeof(stat), "%s%d", NoteNameList()[cur % 12].c_str(), cur / 12 - 1);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      DrawArpGateGrid(n);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      {
         AudioKnobRow row(3);
         row.Dropdown("preset", ArpeggiatorNode::PresetNames(), n->mLastPresetUiIndex,
                      [n](int i) { PushUndoCheckpoint(); n->mLastPresetUiIndex = i; n->ApplyPreset(i); });
         row.Dropdown("mode", kModeLabels, modeUiIndex,
                      [n](int i) { PushUndoCheckpoint(); n->mode = kModeValues[std::clamp(i, 0, (int)kModeLabels.size() - 1)]; });
         row.KnobInt("octaves", &n->octaves, 1, 4);
         row.End();
      }
      {
         AudioKnobRow row(3);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.Knob("gate", &n->gatePercent, 1.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

      ModCheckbox("Snap to Key##arpSnap", &n->useGlobalScale);
      if (DrawRestartControls(&n->restartQuant, n->RestartArmed())) // Turbo 0.50
         n->RequestRestart();

      EndAudioBody();
   }

   void DrawNoteSequencerBody(GraphNode& gn, NoteSequencerNode* n)
   {
      char stat[64];
      const int cur = n->CurrentStep();
      snprintf(stat, sizeof(stat), "step %d/%d", cur < 0 ? 0 : cur + 1, n->steps);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      {
         const int steps = std::clamp(n->steps, 1, NoteSequencerNode::kMaxSteps);
         const int pageCount = std::max(1, (steps + 15) / 16);
         n->editPage = std::clamp(n->editPage, 0, pageCount - 1);
         if (ImGui::SmallButton("<##seqPage"))
            n->editPage = std::max(0, n->editPage - 1);
         ImGui::SameLine();
         ImGui::TextDisabled("steps %d-%d / %d", n->editPage * 16 + 1,
                             std::min(steps, n->editPage * 16 + 16), steps);
         ImGui::SameLine();
         if (ImGui::SmallButton(">##seqPage"))
            n->editPage = std::min(pageCount - 1, n->editPage + 1);
         const int firstStep = n->editPage * 16;
         const int visibleSteps = std::max(1, std::min(16, steps - firstStep));
         const float w = gAudioBodyW;
         const float pitchH = 70.0f;
         const float velH = 16.0f;
         const float gap = 2.0f;
         const float barW = (w - gap * (float)(visibleSteps - 1)) / (float)visibleSteps;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         // Full MIDI range on the bar itself. Exact typed entry below remains
         // available, but C-1 through G9 no longer require leaving the mouse
         // workflow. MIDI C0 is note 12.
         const int kLow = 0, kHigh = 127;

         for (int visible = 0; visible < visibleSteps; visible++)
         {
            const int i = firstStep + visible;
            const float x0 = origin.x + (float)visible * (barW + gap);
            ImGui::PushID(i);

            ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
            ImGui::InvisibleButton("pitch", ImVec2(barW, pitchH));
            if (ImGui::IsItemClicked()) n->editStep = i;
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
               const float my = ImGui::GetIO().MousePos.y;
               const float t = 1.0f - std::clamp((my - origin.y) / pitchH, 0.0f, 1.0f);
               n->stepNote[i] = kLow + (int)(t * (float)(kHigh - kLow) + 0.5f);
            }
            if (ImGui::IsItemHovered())
            {
               const float my = ImGui::GetIO().MousePos.y;
               const float hoverT = 1.0f - std::clamp((my - origin.y) / pitchH, 0.0f, 1.0f);
               const int hoverNote = kLow + (int)(hoverT * (float)(kHigh - kLow) + 0.5f);
               ImGui::SetTooltip("%s%d - MIDI %d", NoteNameList()[hoverNote % 12].c_str(),
                                 hoverNote / 12 - 1, hoverNote);
            }
            const bool isCurrent = (i == cur);
            const float t = std::clamp((float)(n->stepNote[i] - kLow) / (float)(kHigh - kLow), 0.0f, 1.0f);
            const float barTop = origin.y + (1.0f - t) * pitchH;
            const ImU32 col = n->stepEnabled[i]
               ? (isCurrent ? IM_COL32(255, 200, 100, 255) : IM_COL32(120, 200, 255, 255))
               : IM_COL32(50, 53, 64, 255);
            dl->AddRectFilled(ImVec2(x0 + 1.0f, origin.y), ImVec2(x0 + barW - 1.0f, origin.y + pitchH),
                              IM_COL32(20, 21, 26, 255), 2.0f);
            dl->AddRectFilled(ImVec2(x0 + 1.0f, barTop), ImVec2(x0 + barW - 1.0f, origin.y + pitchH), col, 2.0f);

            const float vy0 = origin.y + pitchH + 2.0f;
            ImGui::SetCursorScreenPos(ImVec2(x0, vy0));
            if (ImGui::InvisibleButton("en", ImVec2(barW, velH)))
               n->stepEnabled[i] = !n->stepEnabled[i];
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            {
               const float my = ImGui::GetIO().MousePos.y;
               const float vt = 1.0f - std::clamp((my - vy0) / velH, 0.0f, 1.0f);
               n->stepVelocity[i] = std::clamp(vt, 0.0f, 1.0f);
            }
            dl->AddRectFilled(ImVec2(x0 + 1.0f, vy0), ImVec2(x0 + barW - 1.0f, vy0 + velH),
                              IM_COL32(20, 21, 26, 255), 2.0f);
            const float vFillTop = vy0 + (1.0f - n->stepVelocity[i]) * velH;
            dl->AddRectFilled(ImVec2(x0 + 1.0f, vFillTop), ImVec2(x0 + barW - 1.0f, vy0 + velH),
                              n->stepEnabled[i] ? IM_COL32(90, 170, 120, 255) : IM_COL32(50, 53, 64, 255), 2.0f);

            ImGui::PopID();
         }
         ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + pitchH + velH + 6.0f));
         ImGui::Dummy(ImVec2(w, 1.0f));
      }

      {
         n->editStep = std::clamp(n->editStep, 0, std::max(0, n->steps - 1));
         int note = std::clamp(n->stepNote[n->editStep], 0, 127);
         char noteLabel[64];
         snprintf(noteLabel, sizeof(noteLabel), "step %d: %s%d (MIDI %d)", n->editStep + 1,
                  NoteNameList()[note % 12].c_str(), note / 12 - 1, note);
         ImGui::TextDisabled("%s", noteLabel);
         ImGui::SetNextItemWidth(gAudioBodyW);
         if (ImGui::InputInt("##exactNote", &note, 1, 12))
            n->stepNote[n->editStep] = std::clamp(note, 0, 127);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enter an exact MIDI note number from 0 to 127");
      }

      {
         AudioKnobRow row(4);
         row.KnobInt("steps", &n->steps, 1, NoteSequencerNode::kMaxSteps);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.Knob("gate", &n->gatePercent, 1.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

      ModCheckbox("Snap to Key##seqSnap", &n->useGlobalScale);
      if (DrawRestartControls(&n->restartQuant, n->RestartArmed())) // Turbo 0.50
         n->RequestRestart();

      EndAudioBody();
   }

   void DrawRandomNoteGeneratorBody(GraphNode& gn, RandomNoteGeneratorNode* n)
   {
      char stat[64];
      const int last = n->LastNote();
      if (last < 0)
         snprintf(stat, sizeof(stat), "%s in %s..%s", MusicTime::ScaleTable(n->scale).name,
                  NoteNameList()[n->rangeLow % 12].c_str(), NoteNameList()[n->rangeHigh % 12].c_str());
      else
         snprintf(stat, sizeof(stat), "last: %s%d", NoteNameList()[last % 12].c_str(), last / 12 - 1);
      if (n->groove > 0.001f) // Turbo 0.48: show swing amount
      {
         const size_t len = strlen(stat);
         snprintf(stat + len, sizeof(stat) - len, " - %.0f%% groove", n->groove * 100.0f);
      }

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      {
         AudioKnobRow row(4);
         row.KnobInt("lo", &n->rangeLow, 0, 127, kKnobSmall);
         row.KnobInt("hi", &n->rangeHigh, 0, 127, kKnobSmall);
         if (n->useGlobalScale)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), n->scale,
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         row.Dropdown("root", NoteNameList(), n->root,
                      [n](int i) { PushUndoCheckpoint(); n->root = i; });
         if (n->useGlobalScale)
            ImGui::EndDisabled();
         row.End();
      }
      {
         AudioKnobRow row(3);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.KnobInt("wander", &n->maxStep, 1, 12);
         row.End();
      }

      ModCheckbox("Global Scale##rngGlobal", &n->useGlobalScale);
      // Turbo 0.48 (upstream port): swing. Drawn last so existing modulation pin
      // ordinals (draw order) stay put.
      AudioSlider("groove", &n->groove, 0.0f, 1.0f, "%.2f", AudioFullWidth());
      // Turbo 0.49 (upstream): note-picking style, drawn last for the same reason.
      {
         static const std::vector<std::string> kRandomNoteStyles = { "walk", "melodic" };
         AudioKnobRow row(4);
         row.Dropdown("style", kRandomNoteStyles, std::clamp(n->style, 0, 1),
                      [n](int i) { PushUndoCheckpoint(); n->style = i; });
         row.End();
      }

      EndAudioBody();
   }

   void DrawChorderBody(GraphNode& gn, ChorderNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%s %s, %d-note chord", NoteNameList()[n->root].c_str(),
               MusicTime::ScaleTable(n->scale).name, n->LastChordSize());

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      {
         AudioKnobRow row(4);
         if (n->useGlobalScale)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), n->scale,
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         row.Dropdown("root", NoteNameList(), n->root,
                      [n](int i) { PushUndoCheckpoint(); n->root = i; });
         if (n->useGlobalScale)
            ImGui::EndDisabled();
         row.KnobInt("chord", &n->chordSize, 2, 6);
         row.Knob("groove", &n->rateBeats, 0.0625f, 4.0f, "%.3f beats", kKnobSmall);
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("strum", &n->strumMs, 0.0f, 200.0f, "%.0fms", kKnobSmall);
         row.Knob("hu.time", &n->humanizeTimingMs, 0.0f, 100.0f, "%.0fms", kKnobSmall);
         row.Knob("hu.vel", &n->humanizeVelocity, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.Knob("harmonics", &n->upperHarmonics, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

      ModCheckbox("Global Scale##chorderGlobal", &n->useGlobalScale);

      EndAudioBody();
   }

   // A row of 4 pill toggles, aligned under an AudioKnobRow(4)'s cells -
   // caption is the voice number, fixed text; the pill's fill colour is what
   // shows on/off, same convention as the Sampler's loop/rev/p-p toggles.
   void DrawNoteStackToggleRow(NoteStackNode* n, int startVoice)
   {
      const float x0 = gAudioContentX;
      const float y0 = ImGui::GetCursorScreenPos().y;
      const float cellW = gAudioContentW / 4.0f;
      const float btnW = 36.0f;
      ImGui::SetWindowFontScale(0.85f);
      const float btnH = ImGui::GetFrameHeight();
      for (int i = 0; i < 4; i++)
      {
         const int v = startVoice + i;
         const float cx = x0 + (float)i * cellW + cellW * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(cx - btnW * 0.5f, y0));
         char label[24];
         snprintf(label, sizeof(label), "%d##stackEn%d", v + 1, v);
         bool en = n->enabled[v];
         if (AudioToggleButton(label, &en, btnW))
         {
            PushUndoCheckpoint();
            n->enabled[v] = en;
         }
      }
      ImGui::SetWindowFontScale(1.0f);
      ImGui::SetCursorScreenPos(ImVec2(x0, y0 + btnH + 6.0f));
   }

   void DrawNoteStackBody(GraphNode& gn, NoteStackNode* n)
   {
      int onCount = 0;
      for (int i = 0; i < NoteStackNode::kVoices; i++)
         if (n->enabled[i])
            onCount++;

      char stat[64];
      const int lastStack = n->LastStackSize();
      if (lastStack > 0)
         snprintf(stat, sizeof(stat), "%d notes", lastStack);
      else
         snprintf(stat, sizeof(stat), "%d voice%s on", onCount, onCount == 1 ? "" : "s");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Row 1: Voices 1..4 (knobs on top, small toggle buttons just below)
      {
         AudioKnobRow row(4, kKnobSmall, 0.0f, false);
         for (int v = 0; v < 4; v++)
         {
            char label[24];
            snprintf(label, sizeof(label), "##stackSemi%d", v);
            if (!n->enabled[v])
               ImGui::BeginDisabled();
            row.KnobInt(label, &n->semitones[v], -24, 24);
            if (!n->enabled[v])
               ImGui::EndDisabled();
         }
         row.End();
      }
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, ImGui::GetCursorScreenPos().y + 5.0f));
      DrawNoteStackToggleRow(n, 0);

      {
         const float w = ImGui::CalcTextSize("in - always on").x;
         const ImVec2 pos = ImGui::GetCursorScreenPos();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + (gAudioContentW - w) * 0.5f, pos.y + 2.0f));
         ImGui::TextColored(ImVec4(0.69f, 0.71f, 0.78f, 1.0f), "in - always on");
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, pos.y + ImGui::GetTextLineHeightWithSpacing() + 6.0f));
      }

      // Row 2: Voices 5..8 (knobs on top, small toggle buttons just below)
      {
         AudioKnobRow row(4, kKnobSmall, 0.0f, false);
         for (int v = 4; v < 8; v++)
         {
            char label[24];
            snprintf(label, sizeof(label), "##stackSemi%d", v);
            if (!n->enabled[v])
               ImGui::BeginDisabled();
            row.KnobInt(label, &n->semitones[v], -24, 24);
            if (!n->enabled[v])
               ImGui::EndDisabled();
         }
         row.End();
      }
      ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, ImGui::GetCursorScreenPos().y + 5.0f));
      DrawNoteStackToggleRow(n, 4);

      ModCheckbox("Snap to Key##stackSnap", &n->useGlobalScale);

      EndAudioBody();
   }


   void DrawNoteCapturerVisualizer(NoteCapturerNode* n)
   {
      bool held[128];
      n->HeldKeys(held);
      DrawMidiKeyboard(held, 48, 3);
   }

   void DrawNoteCapturerBody(GraphNode& gn, NoteCapturerNode* n)
   {
      const bool recording = n->IsRecording();
      const bool playing = n->IsPlaying();
      char stat[64];
      if (recording)
         snprintf(stat, sizeof(stat), "REC  %.1f beats", n->RecordedLengthBeats());
      else if (playing)
         snprintf(stat, sizeof(stat), "playing, %d notes", n->RecordedCount());
      else
         snprintf(stat, sizeof(stat), "%d notes recorded", n->RecordedCount());

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawNoteCapturerVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      const float btnW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
      bool requestedRecording = recording;
      if (ModStateButton(recording ? "Stop record##noteCaptureRecord" : "Record##noteCaptureRecord",
                         recording, requestedRecording, ImVec2(btnW, 0)))
      {
         if (requestedRecording) n->StartRecording();
         else n->StopRecording();
      }
      if (recording)
         ImGui::PopStyleColor();
      ImGui::SameLine();

      if (playing)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.5f, 0.2f, 1.0f));
      ImGui::BeginDisabled(n->RecordedCount() == 0 && !playing);
      bool requestedPlaying = playing;
      if (ModStateButton(playing ? "Stop play##noteCapturePlay" : "Play##noteCapturePlay",
                         playing, requestedPlaying, ImVec2(btnW, 0)))
      {
         if (requestedPlaying && n->RecordedCount() > 0) n->StartPlayback();
         else n->StopPlayback();
      }
      ImGui::EndDisabled();
      if (playing)
         ImGui::PopStyleColor();
      ImGui::SameLine();

      if (ModTriggerButton("Stop##noteCaptureStop", ImVec2(btnW, 0)))
      {
         n->StopRecording();
         n->StopPlayback();
      }
      ImGui::SameLine();

      if (ModTriggerButton("Clear##noteCaptureClear", ImVec2(btnW, 0)))
         n->ClearRecording();

      ModCheckbox("loop", &n->loop);
      ImGui::SameLine();
      {
         // Applied once, on StopRecording - see NoteCapturerNode::quantizeDiv.
         AudioKnobRow row(1);
         row.Dropdown("quantize", kQuantizeLabels, n->quantizeDiv,
                      [n](int i) { PushUndoCheckpoint(); n->quantizeDiv = i; });
         row.End();
      }

      EndAudioBody();
   }

   void DrawBouncingBallsVisualizer(BouncingBallsNode* n)
   {
      const float fullW = gAudioBodyW;
      // Shrunk well below the full card width (>=33% smaller than before)
      // and centered in the row, rather than filling it edge to edge.
      const float w = fullW * 0.6f;
      const float h = w; // square canvas - the shape itself needs equal aspect
      const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 origin(rowOrigin.x + (fullW - w) * 0.5f, rowOrigin.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      dl->AddRectFilled(origin, br, ScopeBgCol(), 4.0f);
      dl->PushClipRect(origin, br, true);

      const ImVec2 center((origin.x + br.x) * 0.5f, (origin.y + br.y) * 0.5f);
      const float scale = (w * 0.5f - 6.0f) / BouncingBallsNode::kBound;
      auto ToScreen = [&](float x, float y) { return ImVec2(center.x + x * scale, center.y - y * scale); };

      const ImU32 outlineCol = isLight ? IM_COL32(140, 150, 175, 255) : IM_COL32(150, 165, 190, 255);
      const float outlineThickness = 5.0f;
      const float b = BouncingBallsNode::kBound;
      const float triBboxCenterY = (b + -0.5f * b) * 0.5f;
      if (n->shape == BouncingBallsNode::kSquare)
      {
         dl->AddRect(ToScreen(-b, b), ToScreen(b, -b), outlineCol, 2.0f, 0, outlineThickness);
      }
      else if (n->shape == BouncingBallsNode::kTriangle)
      {
         const ImVec2 p0 = ToScreen(0.0f, b - triBboxCenterY);
         const ImVec2 p1 = ToScreen(-0.8660254f * b, -0.5f * b - triBboxCenterY);
         const ImVec2 p2 = ToScreen(0.8660254f * b, -0.5f * b - triBboxCenterY);
         dl->AddTriangle(p0, p1, p2, outlineCol, outlineThickness);
      }
      else
      {
         dl->AddCircle(center, b * scale, outlineCol, 48, outlineThickness);
      }

      float bx[BouncingBallsNode::kMaxBalls], by[BouncingBallsNode::kMaxBalls], bf[BouncingBallsNode::kMaxBalls];
      const int count = n->BallPositions(bx, by, bf);
      const float ballShiftY = (n->shape == BouncingBallsNode::kTriangle) ? triBboxCenterY : 0.0f;
      for (int i = 0; i < count; i++)
      {
         const ImVec2 p = ToScreen(bx[i], by[i] - ballShiftY);
         const float flash = std::clamp(bf[i], 0.0f, 1.0f);
         const ImU32 restCol = isLight ? IM_COL32(30, 110, 230, 245) : IM_COL32(120, 200, 255, 235);
         const ImU32 hitCol = isLight ? IM_COL32(230, 120, 20, 255) : IM_COL32(255, 190, 90, 255);
         const ImU32 col = ImGui::ColorConvertFloat4ToU32(
            ImLerp(ImGui::ColorConvertU32ToFloat4(restCol), ImGui::ColorConvertU32ToFloat4(hitCol), flash));
         dl->AddCircleFilled(p, n->ballSize * scale, col);
      }

      dl->PopClipRect();
      dl->AddRect(origin, br, ScopeBorderCol(), 4.0f);
      ImGui::SetCursorScreenPos(rowOrigin);
      ImGui::Dummy(ImVec2(fullW, h));
   }

   void DrawBouncingBallsBody(GraphNode& gn, BouncingBallsNode* n)
   {
      static const std::vector<std::string> kShapes = { "Circle", "Square", "Triangle" };
      char stat[64];
      snprintf(stat, sizeof(stat), "%s, %d balls", kShapes[std::clamp(n->shape, 0, 2)].c_str(), n->numBalls);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawBouncingBallsVisualizer(n);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      {
         AudioKnobRow row(3);
         row.Dropdown("shape", kShapes, n->shape, [n](int i) { PushUndoCheckpoint(); n->shape = i; });
         row.KnobInt("balls", &n->numBalls, 1, BouncingBallsNode::kMaxBalls);
         row.Knob("speed", &n->ballSpeed, 0.05f, 2.0f, "%.2f", kKnobSmall);
         row.End();
      }
      {
         AudioKnobRow row(3);
         row.Knob("size", &n->ballSize, 0.02f, 0.15f, "%.2f", kKnobSmall);
         row.KnobInt("lo", &n->rangeLow, 0, 127, kKnobSmall);
         row.KnobInt("hi", &n->rangeHigh, 0, 127, kKnobSmall);
         row.End();
      }

      ModCheckbox("Global Scale##ballsGlobal", &n->useGlobalScale);

      EndAudioBody();
   }

   void DrawStrumBody(GraphNode& gn, NoteStrumNode* n)
   {
      char stat[64];
      if (n->strumMs <= 0.0f)
         snprintf(stat, sizeof(stat), "off");
      else
         snprintf(stat, sizeof(stat), "%.1f ms", n->strumMs);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(1);
         row.Knob("strum", &n->strumMs, 0.0f, 100.0f, "%.1f ms", kKnobLarge);
         row.End();
      }

      EndAudioBody();
   }

   void DrawAudioToCVBody(GraphNode& gn, AudioToCVNode* n)
   {
      const float lvl = n->CurrentLevel();
      char stat[64];
      snprintf(stat, sizeof(stat), "mod %.2f", lvl);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(2);
         row.Knob("gain", &n->gain, 0.0f, 5.0f, "%.2fx", kKnobSmall);
         static const std::vector<std::string> kModes = { "Peak", "RMS" };
         row.Dropdown("mode", kModes, n->mode,
                      [n](int i) { PushUndoCheckpoint(); n->mode = i; });
         row.End();
      }
      {
         AudioKnobRow row(2);
         row.Knob("att", &n->attackMs, 0.1f, 200.0f, "%.1f ms", kKnobSmall);
         row.Knob("rel", &n->releaseMs, 1.0f, 1000.0f, "%.0f ms", kKnobSmall);
         row.End();
      }

      EndAudioBody();
   }

   void DrawEnvelopeBody(GraphNode& gn, EnvelopeNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "level %.2f", n->Value01());
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // Same editable-ADSR panel the wavetable engines use (DrawEditableADSR /
      // DrawEnvelopePanel) rather than a bespoke read-only shape - one
      // envelope-editing look across the app instead of two that behave
      // differently. No `amount`: this node's whole output *is* the depth,
      // there's nothing else for a depth control to scale here.
      DrawEnvelopePanel("envelope  -  drag the handles", "##envAdsr", &n->attackMs, &n->decayMs,
                        &n->sustainLevel, &n->releaseMs, nullptr, 0.0f, 0.0f, "%.2f",
                        IM_COL32(150, 214, 255, 245));
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // "in" is used only while nothing is patched into the modulator pin;
      // "threshold" is the gate line on the raw 0..1 input - rising through
      // it starts attack, falling back below it starts release (see the
      // class comment on EnvelopeNode, ModulatorNodes.h).
      AudioKnobRow row2(2);
      row2.Knob("in", &n->constantIn, 0.0f, 1.0f, "%.2f", kKnobSmall);
      row2.Knob("threshold", &n->threshold, 0.0f, 1.0f, "%.2f", kKnobSmall);
      row2.End();

      EndAudioBody();
   }

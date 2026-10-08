// Plugin, audio-out, analog and audio-node dispatch bodies (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // The hosted-plugin body. Its own controls stay minimal on purpose (the
   // node is a shell around someone else's plugin, not an instrument of its
   // own): the plugin's name, an open-editor button, bypass, and configure.
   // Everything below the header is mapped plugin parameters.
   //
   // Two structural rules this body has to keep:
   //
   //  - No ModSlider/ModKnob call may come BEFORE the mapping grid, and none
   //    may come AFTER it. Modulation bindings are keyed by (nodeIndex,
   //    paramIndex) where paramIndex is this body's ModSlider call order
   //    (gParamCounter), so anything drawn around the grid would shift every
   //    row's pin identity as rows appear and disappear. That is also why
   //    mapping slots are positional and never compacted, and why hiding
   //    trailing unassigned rows is safe when hiding a middle one would not be.
   //  - The open-editor button's label is ASCII. The UI font is loaded with no
   //    glyph range (only Basic Latin is guaranteed), so a Unicode arrow would
   //    render as a literal '?'.
   void DrawPluginBody(GraphNode& gn, AudioPluginNode* n)
   {
      char stat[200];
      if (!n->HasPlugin())
         snprintf(stat, sizeof(stat), "no plugin - drag one from the Plugins panel");
      else if (n->IsLoading())
         snprintf(stat, sizeof(stat), "loading %s...", n->PluginDisplayName().c_str());
      else if (!n->IsReady())
         snprintf(stat, sizeof(stat), "%s - %s", n->PluginDisplayName().c_str(), n->Status().c_str());
      else
         // "au" was the plugin *format*; this is the kind (effect vs.
         // instrument/music-effect), which is what a user picking a node
         // apart actually wants to know at a glance.
         snprintf(stat, sizeof(stat), "%s - %s - %d param%s mapped", n->PluginDisplayName().c_str(),
                  n->AcceptsNotes() ? "instrument" : "effect", n->AssignedCount(),
                  n->AssignedCount() == 1 ? "" : "s");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // --- header row ---
      const bool ready = n->IsReady();
      const bool editorOpen = n->EditorIsOpen();
      if (!ready)
         ImGui::BeginDisabled();
      // "open" / "close", not a glyph - see the note above about the font.
      if (ImGui::Button(editorOpen ? "close" : "open", ImVec2(64.0f, 0)))
         n->ToggleEditor();
      if (!ready)
         ImGui::EndDisabled();
      if (ImGui::IsItemHovered())
         SetAudioReadout("editor", editorOpen ? "open" : "closed");

      ImGui::SameLine();
      bool bypass = n->bypass;
      if (AudioToggleButton("bypass", &bypass, 66.0f))
      {
         PushUndoCheckpoint();
         n->bypass = bypass;
      }

      ImGui::SameLine();
      bool configuring = n->Configuring();
      if (!ready)
         ImGui::BeginDisabled();
      if (AudioToggleButton("configure", &configuring, 84.0f))
         n->SetConfiguring(configuring);
      if (!ready)
         ImGui::EndDisabled();
      if (ImGui::IsItemHovered())
         SetAudioReadout("configure", configuring ? "touch a control in the plugin" : "off");

      // Right-aligned unload, anchored off the declared body width rather than
      // GetContentRegionAvail - see DrawSamplerBody's comment on why the live
      // width collides with the node's resize hit-zone.
      if (n->HasPlugin())
      {
         const float unloadW = 60.0f;
         ImGui::SameLine();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - unloadW,
                                          ImGui::GetCursorScreenPos().y));
         if (ImGui::Button("unload", ImVec2(unloadW, 0)))
         {
            PushUndoCheckpoint();
            n->Unload();
         }
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // --- fallback parameter picker ---
      // Learn alone is not enough: plenty of plugin editors don't notify the
      // host on every control, and a user whose plugin is one of those would
      // otherwise have no way to map anything at all.
      if (ready && !n->AvailableParams().empty())
      {
         BeginAudioSection("map a parameter");
         static int sPickIndex = 0;
         const std::vector<Platform::PluginParamInfo>& params = n->AvailableParams();
         sPickIndex = std::clamp(sPickIndex, 0, (int)params.size() - 1);
         std::vector<std::string> names;
         names.reserve(params.size());
         for (const Platform::PluginParamInfo& p : params)
            names.push_back(p.displayName);
         // Not a plain BeginCombo: this body is drawn inside ed::Begin()/End(),
         // whose canvas transform ImGui popups don't participate in - a combo
         // opened directly here lands unscaled at the wrong screen position.
         // AudioBareDropdown defers the popup to the once-per-frame draw at
         // canvas scale instead (see its own comment).
         AudioBareDropdown("##pluginparampick", names, sPickIndex, [](int i) { sPickIndex = i; },
                           gAudioContentW - 70.0f - ImGui::GetStyle().ItemSpacing.x);
         ImGui::SameLine();
         if (ImGui::Button("map", ImVec2(70.0f, 0)))
         {
            PushUndoCheckpoint();
            n->MapParameter(params[sPickIndex].address);
         }
         EndAudioSection();
      }

      // --- the mapping grid ---
      // Two columns, growing downward as the user maps more. Every row is a
      // ModSlider, which is what makes these real params: each gets its own
      // modulation pin, typed edit, expression support and undo, exactly like
      // any other param in the app.
      const int highest = n->HighestAssignedSlot();
      // One spare row while configuring, so there is somewhere visible for the
      // next learned param to land. Safe only because nothing is drawn after
      // the grid - see this function's header comment.
      int rows = highest + 1;
      if (n->Configuring() && rows < AudioPluginNode::kMaxMappedParams)
         rows++;

      if (rows <= 0)
      {
         ImGui::TextDisabled(ready ? "turn on configure, then touch a control in the plugin"
                                   : "no parameters mapped");
      }

      const float cellW = AudioHalfWidth();
      for (int i = 0; i < rows; i++)
      {
         AudioPluginNode::Mapping& m = n->mappings[i];
         if ((i % 2) == 1)
            ImGui::SameLine();

         if (m.assigned)
         {
            // The label carries the slot index so two identically-named plugin
            // params (common: "Gain" on each of four bands) stay tellable
            // apart, and so ModSlider's own ImGui id is unique per row.
            char label[96];
            snprintf(label, sizeof(label), "%s##map%d", m.displayName.c_str(), i);
            if (n->Configuring())
            {
               const float unmapBtnW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f + 2.0f;
               ModSlider(label, &m.value, m.minValue, m.maxValue, "%.3f", cellW - unmapBtnW - 4.0f, /*audioStyle=*/true);
               ImGui::SameLine(0.0f, 2.0f);
               ImGui::PushID(i + 40000);
               ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
               ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
               ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
               if (ImGui::SmallButton("x"))
               {
                  PushUndoCheckpoint();
                  Modulation::Instance().Unbind(gn.index, i);
                  Modulation::Instance().ClearExpression(gn.index, i);
                  n->UnmapSlot(i);
               }
               ImGui::PopStyleColor(3);
               ImGui::PopID();
            }
            else
            {
               ModSlider(label, &m.value, m.minValue, m.maxValue, "%.3f", cellW, /*audioStyle=*/true);
            }
         }
         else
         {
            // An unassigned row still consumes a ModSlider call (and therefore
            // a paramIndex) so that assigning it later doesn't renumber every
            // row after it. It is drawn read-only against a dead scratch value.
            static float sUnassigned = 0.0f;
            sUnassigned = 0.0f;
            char label[32];
            snprintf(label, sizeof(label), "unassigned##map%d", i);
            ImGui::BeginDisabled();
            ModSlider(label, &sUnassigned, 0.0f, 1.0f, "%.0f", cellW, /*audioStyle=*/true);
            ImGui::EndDisabled();
         }
      }

      EndAudioBody();
   }


   // Filename for a fresh recording that doesn't collide with anything
   // already in `dir`: Infinite-<patchname>-001.ext, -002, ... <patchname>
   // falls back to "Untitled" for an unsaved patch, same fallback the title
   // bar uses. `dir` may be "" (only used to probe collisions, e.g. for the
   // Choose... dialog's suggested name), in which case every candidate is
   // reported free.
   std::string DefaultRecordingFileName(const std::string& dir, const std::string& ext = "wav")
   {
      std::string base = gPatchPath.empty() ? "Untitled" : gPatchPath.substr(gPatchPath.find_last_of('/') + 1);
      const size_t dot = base.find_last_of('.');
      if (dot != std::string::npos)
         base = base.substr(0, dot);

      for (int i = 1; i < 1000; i++)
      {
         char name[256];
         snprintf(name, sizeof(name), "Infinite-%s-%03d.%s", base.c_str(), i, ext.c_str());
         if (dir.empty())
            return name;
         struct stat st;
         if (stat((dir + "/" + name).c_str(), &st) != 0) // ENOENT: nothing there yet
            return name;
      }
      char fallback[256];
      snprintf(fallback, sizeof(fallback), "Infinite-%s.%s", base.c_str(), ext.c_str()); // search exhausted, overwrite
      return fallback;
   }


   // Where the next recording lands: the folder last picked via Choose...,
   // or the user's Desktop when nothing has been picked yet.
   std::string RecordingDirFor(const AudioOutputNode* n)
   {
      if (!n->recordDirectory.empty())
         return n->recordDirectory;
      return AppPaths::DesktopDir();
   }


   void DrawAudioOutBody(GraphNode& gn, AudioOutputNode* n)
   {
      const bool recording = n->IsRecording();
      const bool audioOn = AudioEngine::Instance().SampleRate() > 0.0;
      char stat[96];
      if (recording)
      {
         const char* fmtStr = (n->formatIndex == 1) ? "FLAC" : (n->formatIndex == 2 ? "MP3" : "WAV");
         snprintf(stat, sizeof(stat), "REC %s  %.1fs  %.0f KB", fmtStr, n->ElapsedSeconds(), (double)n->FileSizeBytes() / 1024.0);
      }
      else
      {
         snprintf(stat, sizeof(stat), "%s", audioOn ? "device output - running" : "device output");
      }

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Format selector: WAV | FLAC | MP3
      ImGui::BeginDisabled(recording);
      const float fmtBtnW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
      const char* fmtNames[] = { "WAV", "FLAC", "MP3" };
      for (int i = 0; i < 3; i++)
      {
         const bool active = (n->formatIndex == i);
         if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
         if (ImGui::Button(fmtNames[i], ImVec2(fmtBtnW, 0)))
         {
            n->formatIndex = i;
            gPatchDirty = true;
         }
         if (active)
            ImGui::PopStyleColor();
         if (i < 2)
            ImGui::SameLine();
      }
      ImGui::EndDisabled();
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Live: skip alignment against other Audio Outs (see AudioOutputNode::live).
      {
         if (n->live)
            ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
         if (ImGui::Button("live##audioOutLive", ImVec2(AudioFullWidth(), 0)))
         {
            PushUndoCheckpoint();
            n->live = !n->live;
            gPatchDirty = true;
            RebuildAudioTopology();
         }
         if (n->live)
            ImGui::PopStyleColor();
         if (ImGui::IsItemHovered())
            SetAudioReadout("live", n->live ? "not delayed to match other outputs" : "aligned with other outputs");
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      // Record/Stop is the only control that ever starts or stops a
      // recording - Choose... below only ever changes where the *next*
      // recording will land, never triggers one itself, so picking a folder
      // can't be mistaken for pressing Record.
      const float btnW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
      ImGui::BeginDisabled(!recording && !audioOn);
      if (ImGui::Button(recording ? "Stop##audioOutRec" : "Record##audioOutRec", ImVec2(btnW, 0)))
      {
         if (recording)
         {
            n->StopRecording();
         }
         else
         {
            const std::string ext = (n->formatIndex == 1) ? "flac" : (n->formatIndex == 2 ? "mp3" : "wav");
            const std::string dir = RecordingDirFor(n);
            n->StartRecording(dir + "/" + DefaultRecordingFileName(dir, ext));
         }
      }
      if (!recording && !audioOn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("press Start Audio in the toolbar first");
      ImGui::EndDisabled();
      if (recording)
         ImGui::PopStyleColor();
      ImGui::SameLine();

      ImGui::BeginDisabled(recording);
      if (ImGui::Button("Choose...##audioOutChoose", ImVec2(btnW, 0)))
      {
         const std::string chosen = Platform::OpenFolderDialog("Choose recording folder", RecordingDirFor(n));
         if (!chosen.empty())
         {
            n->recordDirectory = chosen;
            gPatchDirty = true;
         }
      }
      ImGui::EndDisabled();

      const std::string destLabel = n->recordDirectory.empty() ? std::string("~/Desktop") : n->recordDirectory;
      ImGui::TextColored(ImVec4(0.6f, 0.62f, 0.68f, 1.0f), "%s", destLabel.c_str());

      if (n->DroppedSampleCount() > 0)
         ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f), "dropped samples - device can't keep up");

      EndAudioBody();
   }


   // Export panel of the Spatial Mixer: record the binaural render (head facing
   // front) as WAV/FLAC, plus the terminal-output `live` switch. Same
   // Record/Choose behaviour as Audio Out.
   void DrawSpatialExportPanel(SpatialMixerNode* n)
   {
      const bool recording = n->IsRecording();
      const bool audioOn = AudioEngine::Instance().SampleRate() > 0.0;
      ImGui::BeginDisabled(recording);
      const float half = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
      const char* fmtNames[] = { "WAV", "FLAC" };
      for (int i = 0; i < 2; i++)
      {
         const bool active = (n->formatIndex == i);
         if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
         if (ImGui::Button(fmtNames[i], ImVec2(half, 0)))
         {
            n->formatIndex = i;
            gPatchDirty = true;
         }
         if (active)
            ImGui::PopStyleColor();
         if (i == 0)
            ImGui::SameLine();
      }
      ImGui::EndDisabled();

      if (n->live)
         ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
      if (ImGui::Button("live##spatialLive", ImVec2(half, 0)))
      {
         PushUndoCheckpoint();
         n->live = !n->live;
         gPatchDirty = true;
         RebuildAudioTopology();
      }
      if (n->live)
         ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
         SetAudioReadout("live", n->live ? "not delayed to match other outputs" : "aligned with other outputs");
      ImGui::SameLine();
      if (n->renderMode == 1)
         ImGui::PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected());
      if (ImGui::Button(n->renderMode == 1 ? "stereo##spatialMode" : "binaural##spatialMode", ImVec2(half, 0)))
      {
         PushUndoCheckpoint();
         n->renderMode = 1 - n->renderMode;
         gPatchDirty = true;
      }
      if (n->renderMode == 1)
         ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
         SetAudioReadout("render", n->renderMode == 1 ? "speaker-safe pan, no ear filtering" : "headphones: ear filtering on");

      const float btnW = half;
      if (recording)
         ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
      ImGui::BeginDisabled(!recording && !audioOn);
      if (ImGui::Button(recording ? "Stop##spatialRec" : "Record##spatialRec", ImVec2(btnW, 0)))
      {
         if (recording)
            n->StopRecording();
         else
         {
            const std::string ext = (n->formatIndex == 1) ? "flac" : "wav";
            const std::string dir = n->recordDirectory.empty() ? AppPaths::DesktopDir() : n->recordDirectory;
            n->StartRecording(dir + "/" + DefaultRecordingFileName(dir, ext));
         }
      }
      ImGui::EndDisabled();
      if (recording)
         ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::BeginDisabled(recording);
      if (ImGui::Button("Choose...##spatialChoose", ImVec2(btnW, 0)))
      {
         const std::string chosen = Platform::OpenFolderDialog("Choose recording folder",
            n->recordDirectory.empty() ? AppPaths::DesktopDir() : n->recordDirectory);
         if (!chosen.empty())
         {
            n->recordDirectory = chosen;
            gPatchDirty = true;
         }
      }
      ImGui::EndDisabled();
      if (recording)
         ImGui::TextColored(ImVec4(0.6f, 0.62f, 0.68f, 1.0f), "REC %.1fs  %.0f KB  (head facing front)",
                            n->ElapsedSeconds(), (double)n->FileSizeBytes() / 1024.0);
      else
         ImGui::TextColored(ImVec4(0.6f, 0.62f, 0.68f, 1.0f), "%s",
                            n->recordDirectory.empty() ? "~/Desktop" : n->recordDirectory.c_str());
      if (n->DroppedSampleCount() > 0)
         ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f), "dropped samples - device can't keep up");
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
         row.Knob("glide", &n->glide, 0.0f, 2.0f, "%.2f s", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewGlide150);
         row.Knob("bend", &n->pitchBend, -2.0f, 2.0f, "%+.2f st");
         row.End();
      }
      EndAudioSection();

      EndAudioBody();
   }


   // Dispatch for anything IsAudioBodyNode() accepts. Called from the
   // node-body loop's DrawPreview-replacement chain; audio nodes have no
   // image to preview, so this entirely replaces DrawPreview +
   // DrawXxxParams for them rather than sitting alongside either.
   void DrawAudioNodeBody(GraphNode& gn)
   {
      if (auto* n = dynamic_cast<OscillatorNode*>(gn.node.get()))
         DrawOscillatorBody(gn, n);
      else if (auto* n = dynamic_cast<WavetableNode*>(gn.node.get()))
         DrawWavetableBody(gn, n);
      else if (auto* n = dynamic_cast<AnalogNode*>(gn.node.get()))
         DrawAnalogBody(gn, n);
      else if (auto* n = dynamic_cast<WaveTerrainNode*>(gn.node.get()))
         DrawWaveTerrainBody(gn, n);
      else if (auto* n = dynamic_cast<EquationNode*>(gn.node.get()))
         DrawEquationBody(gn, n);
      else if (auto* n = dynamic_cast<ImageSpectralSynthNode*>(gn.node.get()))
         DrawImageSpectralSynthBody(gn, n);
      else if (auto* n = dynamic_cast<MetallicNode*>(gn.node.get()))
         DrawMetallicBody(gn, n);
      else if (auto* n = dynamic_cast<AudioPluginNode*>(gn.node.get()))
         DrawPluginBody(gn, n);
      else if (auto* n = dynamic_cast<SamplerNode*>(gn.node.get()))
         DrawSamplerBody(gn, n);
      else if (auto* n = dynamic_cast<SlicerNode*>(gn.node.get()))
         DrawSlicerBody(gn, n);
      else if (auto* n = dynamic_cast<PaulStretchNode*>(gn.node.get()))
         DrawPaulStretchBody(gn, n);
      else if (auto* n = dynamic_cast<MolderNode*>(gn.node.get()))
         DrawMolderBody(gn, n);
      else if (auto* n = dynamic_cast<GrainMolderNode*>(gn.node.get()))
         DrawGrainMolderBody(gn, n);
      else if (auto* n = dynamic_cast<GranularNode*>(gn.node.get()))
         DrawGranularBody(gn, n);
      else if (auto* n = dynamic_cast<DrumSequencerNode*>(gn.node.get()))
         DrawDrumSequencerBody(gn, n);
      else if (auto* n = dynamic_cast<MpcNode*>(gn.node.get()))
         DrawMpcBody(gn, n);
      else if (auto* n = dynamic_cast<LooperNode*>(gn.node.get()))
         DrawLooperBody(gn, n);
      else if (auto* n = dynamic_cast<GainNode*>(gn.node.get()))
         DrawGainBody(gn, n);
      else if (auto* n = dynamic_cast<AudioMeterNode*>(gn.node.get()))
         DrawAudioMeterBody(gn, n);
      else if (auto* n = dynamic_cast<MixerNode*>(gn.node.get()))
         DrawMixerBody(gn, n);
      else if (auto* n = dynamic_cast<SpatialMixerNode*>(gn.node.get()))
         DrawSpatialMixerBody(gn, n);
      else if (auto* n = dynamic_cast<SplitterNode*>(gn.node.get()))
         DrawSplitterBody(gn, n);
      else if (auto* n = dynamic_cast<BlendAudioNode*>(gn.node.get()))
         DrawBlendAudioBody(gn, n);
      else if (auto* n = dynamic_cast<AudioInputNode*>(gn.node.get()))
         DrawAudioInBody(gn, n);
      else if (auto* n = dynamic_cast<AudioOutputNode*>(gn.node.get()))
         DrawAudioOutBody(gn, n);
      else if (auto* n = dynamic_cast<MidiNotesNode*>(gn.node.get()))
         DrawMidiNotesBody(gn, n);
      else if (auto* n = dynamic_cast<KeyboardNode*>(gn.node.get()))
         DrawKeyboardBody(gn, n);
      else if (auto* n = dynamic_cast<NoteFilterNode*>(gn.node.get()))
         DrawNoteFilterBody(gn, n);
      else if (auto* n = dynamic_cast<NoteTransposeNode*>(gn.node.get()))
         DrawNoteTransposeBody(gn, n);
      else if (auto* n = dynamic_cast<PitchBendNode*>(gn.node.get()))
         DrawPitchBendBody(gn, n);
      else if (auto* n = dynamic_cast<VelocityCurveNode*>(gn.node.get()))
         DrawVelocityCurveBody(gn, n);
      else if (auto* n = dynamic_cast<GateNode*>(gn.node.get()))
         DrawGateBody(gn, n);
      else if (auto* n = dynamic_cast<HumanizerNode*>(gn.node.get()))
         DrawHumanizerBody(gn, n);
      else if (auto* n = dynamic_cast<QuantizerNode*>(gn.node.get()))
         DrawQuantizerBody(gn, n);
      else if (auto* n = dynamic_cast<GlideNode*>(gn.node.get()))
         DrawGlideBody(gn, n);
      else if (auto* n = dynamic_cast<VibratoNode*>(gn.node.get()))
         DrawVibratoBody(gn, n);
      else if (auto* n = dynamic_cast<NoteEchoNode*>(gn.node.get()))
         DrawNoteEchoBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveNotesNode*>(gn.node.get()))
         DrawPredictiveNotesBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveQuantizeNode*>(gn.node.get()))
         DrawPredictiveQuantizeBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveVelocityNode*>(gn.node.get()))
         DrawPredictiveVelocityBody(gn, n);
      else if (auto* n = dynamic_cast<PredictiveRhythmNode*>(gn.node.get()))
         DrawPredictiveRhythmBody(gn, n);
      else if (auto* n = dynamic_cast<NoteRouterNode*>(gn.node.get()))
         DrawNoteRouterBody(gn, n);
      else if (auto* n = dynamic_cast<NoteMergeNode*>(gn.node.get()))
         DrawNoteMergeBody(gn, n);
      else if (auto* n = dynamic_cast<NoteSwitcherNode*>(gn.node.get()))
         DrawNoteSwitcherBody(gn, n);
      else if (auto* n = dynamic_cast<ArpeggiatorNode*>(gn.node.get()))
         DrawArpeggiatorBody(gn, n);
      else if (auto* n = dynamic_cast<NoteSequencerNode*>(gn.node.get()))
         DrawNoteSequencerBody(gn, n);
      else if (auto* n = dynamic_cast<RandomNoteGeneratorNode*>(gn.node.get()))
         DrawRandomNoteGeneratorBody(gn, n);
      else if (auto* n = dynamic_cast<MidiFileNode*>(gn.node.get()))
         DrawMidiFileBody(gn, n);
      else if (auto* n = dynamic_cast<ChorderNode*>(gn.node.get()))
         DrawChorderBody(gn, n);
      else if (auto* n = dynamic_cast<NoteStackNode*>(gn.node.get()))
         DrawNoteStackBody(gn, n);
      else if (auto* n = dynamic_cast<NoteCapturerNode*>(gn.node.get()))
         DrawNoteCapturerBody(gn, n);
      else if (auto* n = dynamic_cast<BouncingBallsNode*>(gn.node.get()))
         DrawBouncingBallsBody(gn, n);
      else if (auto* n = dynamic_cast<NoteStrumNode*>(gn.node.get()))
         DrawStrumBody(gn, n);
      else if (auto* n = dynamic_cast<AudioToCVNode*>(gn.node.get()))
         DrawAudioToCVBody(gn, n);
      else if (auto* n = dynamic_cast<EnvelopeNode*>(gn.node.get()))
         DrawEnvelopeBody(gn, n);
      else if (auto* n = dynamic_cast<AudioEffectNode*>(gn.node.get()))
      {
         // Every EffectDefs.cpp entry shares this one C++ class (§0.4), so
         // this dispatches a second time on which effect it actually is -
         // the same reason DrawAudioNodeBody itself exists one level up.
         // Dispatched by EffectDef::visualizerId (set once per table entry
         // in EffectDefs.cpp), not by a second string comparison against
         // Def().name - a name ladder is exactly what would turn into a
         // seven-way chain as the remaining P3c effects land.
         switch (n->Def().visualizerId)
         {
         case EffectVisualizerId::kFilterResponse:
            DrawAudioFilterBody(gn, n);
            break;
         case EffectVisualizerId::kEqCurve:
            DrawEqBody(gn, n);
            break;
         case EffectVisualizerId::kDynamicsTransfer:
            DrawDynamicsBody(gn, n);
            break;
         case EffectVisualizerId::kLimiterMeter:
            DrawLimiterBody(gn, n);
            break;
         case EffectVisualizerId::kDelayTaps:
            DrawDelayBody(gn, n);
            break;
         case EffectVisualizerId::kReverbDecay:
            DrawReverbBody(gn, n);
            break;
         case EffectVisualizerId::kDriveCurve:
            DrawDriveBody(gn, n);
            break;
         case EffectVisualizerId::kStereoGoniometer:
            DrawStereoBody(gn, n);
            break;
         case EffectVisualizerId::kPitchShiftDisplay:
            DrawPitchShiftBody(gn, n);
            break;
         case EffectVisualizerId::kChorusScatter:
            DrawChorusBody(gn, n);
            break;
         case EffectVisualizerId::kFlangerScatter:
            DrawFlangerBody(gn, n);
            break;
         case EffectVisualizerId::kPhaserScatter:
            DrawPhaserBody(gn, n);
            break;
         case EffectVisualizerId::kBitcrushWave:
            DrawBitcrushBody(gn, n);
            break;
         case EffectVisualizerId::kTransientEnvelope:
            DrawTransientShaperBody(gn, n);
            break;
         case EffectVisualizerId::kStutterGrid:
            DrawStutterBody(gn, n);
            break;
         case EffectVisualizerId::kRingModWave:
            DrawRingModBody(gn, n);
            break;
         case EffectVisualizerId::kFrequencyShiftSpectrum:
            DrawFrequencyShifterBody(gn, n);
            break;
         case EffectVisualizerId::kTremoloWave:
            DrawTremoloBody(gn, n);
            break;
         case EffectVisualizerId::kFormantVowel:
            DrawFormantFilterBody(gn, n);
            break;
         case EffectVisualizerId::kWavetableShaperCurve:
            DrawWavetableShaperBody(gn, n);
            break;
         case EffectVisualizerId::kResonatorBankSpectrum:
            DrawResonatorBankBody(gn, n);
            break;
         case EffectVisualizerId::kCycleShaperWave:
            DrawCycleShaperBody(gn, n);
            break;
         case EffectVisualizerId::kSpecBlurSpectrum:
            DrawSpecBlurBody(gn, n);
            break;
         case EffectVisualizerId::kKeySnapScale:
            DrawKeySnapBody(gn, n);
            break;
         case EffectVisualizerId::kSpectrumSlide:
            DrawSpectrumSlideBody(gn, n);
            break;
         case EffectVisualizerId::kShapeResonator:
            DrawShapeResonatorBody(gn, n);
            break;
         default:
            break;
         }
      }
   }
}

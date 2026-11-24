// Audio-in, mixer, keyboard and note node bodies (moved verbatim from main.cpp).
#include "arrange/NoteSchedule.h"
#include "app/graph/NodeParams.h"
#include "app/ui/design/components/MenuParts.h"
#include "app/ui/design/UiInteract.h"
#include "app/ui/design/Keymap.h"
#include "app/ui/design/UiType.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/DrawParam.h"
#include "core/ModShapes.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/ChipButton.h"
#include "app/ui/design/components/FormParts.h"
#include "app/ui/design/components/StepCell.h"
#include "audio/NoteTheory.h"
#include "app/ui/design/UiFmt.h"
#include "app/ui/design/UiScope.h"

namespace app
{
   struct AudioInChoice
   {
      std::string label;       // e.g. "Stereo (1+2)", "Input 1 (Mono)"
      std::string shortName;   // e.g. "Stereo", "In 1"
      std::string category;    // Device name or "System Default"
      uint32_t deviceId = 0;
      int channelMode = 0;     // 0 = Stereo (1+2), 1 = In 1, 2 = In 2, 3 = In 3...
      int source = AudioInputNode::kSourceDefault;
   };


   // Mirror image of Audio Out's body (a terminal with no controls at all) -
   // Audio In has an input channel/device selector and a trim fader.
   // The stat line reports the active input and whether the mic tap is
   // actually live (Platform::AudioInputCaptureIsRunning).
   void DrawAudioInBody(GraphNode& gn, AudioInputNode* n)
   {
      std::vector<AudioInChoice> choices;
      const bool host = n->ReadsHost();
      const std::vector<Platform::AudioDeviceInfo> devices = host ? std::vector<Platform::AudioDeviceInfo>() : Platform::AudioListDevices();

      if (host)
      {
         // Inside the plugin: the host's buses, never a device.
         const int kMain = AudioInputNode::kSourceHostMain, kSide = AudioInputNode::kSourceHostSidechain;
         choices.push_back({ "Host: Stereo (1+2)", "Host: Stereo", "Host", 0, 0, kMain });
         choices.push_back({ "Host: Input 1 (Mono)", "Host: In 1", "Host", 0, 1, kMain });
         choices.push_back({ "Host: Input 2 (Mono)", "Host: In 2", "Host", 0, 2, kMain });
         choices.push_back({ "Sidechain: Stereo (1+2)", "Sidechain: Stereo", "Sidechain", 0, 0, kSide });
         choices.push_back({ "Sidechain: Input 1 (Mono)", "Sidechain: In 1", "Sidechain", 0, 1, kSide });
         choices.push_back({ "Sidechain: Input 2 (Mono)", "Sidechain: In 2", "Sidechain", 0, 2, kSide });
      }
      else
      {
         // System Default options
         choices.push_back({ "Default: Stereo (1+2)", "Default: Stereo", "System Default", 0, 0 });
         choices.push_back({ "Default: Input 1 (Mono)", "Default: In 1", "System Default", 0, 1 });
         choices.push_back({ "Default: Input 2 (Mono)", "Default: In 2", "System Default", 0, 2 });
      }

      for (const Platform::AudioDeviceInfo& d : devices)
      {
         if (!d.isInput)
            continue;

         const int chCount = std::max(1, d.inputChannels > 0 ? d.inputChannels : 2);
         const std::string devPrefix = d.name + ": ";

         if (chCount >= 2)
         {
            choices.push_back({ devPrefix + "Stereo (1+2)", devPrefix + "Stereo", d.name, d.deviceId, 0 });
            choices.push_back({ devPrefix + "Input 1 (Mono)", devPrefix + "In 1", d.name, d.deviceId, 1 });
            choices.push_back({ devPrefix + "Input 2 (Mono)", devPrefix + "In 2", d.name, d.deviceId, 2 });
            for (int ch = 3; ch <= chCount; ch++)
            {
               char lbl[64], sname[64];
               snprintf(lbl, sizeof(lbl), "%sInput %d (Mono)", devPrefix.c_str(), ch);
               snprintf(sname, sizeof(sname), "%sIn %d", devPrefix.c_str(), ch);
               choices.push_back({ lbl, sname, d.name, d.deviceId, ch });
            }
         }
         else
         {
            choices.push_back({ devPrefix + "Input 1 (Mono)", devPrefix + "In 1", d.name, d.deviceId, 1 });
         }
      }

      int currentIdx = 0;
      const int wantSource = host ? (n->source == AudioInputNode::kSourceHostSidechain ? AudioInputNode::kSourceHostSidechain
                                                                                         : AudioInputNode::kSourceHostMain)
                                  : AudioInputNode::kSourceDefault;
      for (int i = 0; i < (int)choices.size(); i++)
      {
         if (host)
         {
            if (choices[i].source == wantSource && choices[i].channelMode == n->channelMode)
            {
               currentIdx = i;
               break;
            }
            continue;
         }
         if (choices[i].channelMode == n->channelMode &&
             ((n->deviceId == 0 && choices[i].deviceId == 0) ||
              (n->deviceId != 0 && (choices[i].deviceId == (uint32_t)n->deviceId || choices[i].category == n->deviceName))))
         {
            currentIdx = i;
            break;
         }
      }

      std::vector<std::string> options;
      std::vector<std::string> categories;
      options.reserve(choices.size());
      categories.reserve(choices.size());
      for (const auto& c : choices)
      {
         options.push_back(c.label);
         categories.push_back(c.category);
      }

      const std::string& inputDesc = choices[currentIdx].shortName;
      char stat[96];
      if (host)
         snprintf(stat, sizeof(stat), "%s   %s", UiFmt::DbOffset(n->gainDb).c, inputDesc.c_str());
      else if (Platform::AudioInputCaptureIsRunning())
         snprintf(stat, sizeof(stat), "%s   %s - live", UiFmt::DbOffset(n->gainDb).c, inputDesc.c_str());
      else if (!n->Status().empty())
         snprintf(stat, sizeof(stat), "%s   %s", UiFmt::DbOffset(n->gainDb).c, n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%s   %s - idle", UiFmt::DbOffset(n->gainDb).c, inputDesc.c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      Ui::Spacer(Ui::Space::S0_5);

      {
         PushDropdownStyle();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + tok::space_1, ImGui::GetCursorScreenPos().y));
         const std::string caption = options[currentIdx] + "##audioin_input";
         if (NodeDropdownField(caption.c_str(), ImVec2(gAudioContentW - tok::space_2, 0)))
         {
            gDropdown.options = options;
            gDropdown.categories = categories;
            gDropdown.onSelect = [n, choices](int idx) {
               if (idx >= 0 && idx < (int)choices.size())
               {
                  PushUndoCheckpoint();
                  n->channelMode = choices[idx].channelMode;
                  if (choices[idx].source != AudioInputNode::kSourceDefault)
                  {
                     n->source = choices[idx].source; // host bus: the device choice stays as it was
                     return;
                  }
                  n->source = AudioInputNode::kSourceDefault;
                  n->deviceId = (int)choices[idx].deviceId;
                  n->deviceName = choices[idx].category;
                  Platform::AudioInputCaptureSetDevice(choices[idx].deviceId);
               }
            };
            gDropdown.current = currentIdx;
            gDropdown.justOpened = true;
            gDropdown.focusSearch = false;
         }
         PopDropdownStyle();
      }

      Ui::Spacer(Ui::Space::S1);

      const float faderH = kFaderH;
      const float stripTop = ImGui::GetCursorScreenPos().y;
      const float cx = gAudioContentX + 0.5f * gAudioContentW;
      DrawStripMeter(cx + kFaderMeterDx, stripTop + kFaderMeterPad, kFaderMeterW, faderH - tok::note_audio_in_body_ratio * kFaderMeterPad, n->Level());
      AudioKnobRow row(1, faderH);
      row.Fader("trim", &n->gainDb, -60.0f, 12.0f, "%.1f dB", faderH, /*dbTaper=*/true);
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
   // channel already carries its own meter, so it was the same signal drawn across the strips' own space.
   void DrawMixerBody(GraphNode& gn, MixerNode* n)
   {
      const int count = std::clamp(n->numChannels, 0, (int)MixerNode::kMaxSlots);
      const float bodyW = std::max(tok::note_w_min, (float)count * tok::note_mixer_body_ratio);
      char stat[64];
      if (count > 0)
      {
         if (n->Level() > 1e-4f)
            snprintf(stat, sizeof(stat), "%d in \xe2\x86\x92 1 out \xc2\xb7 sum %s", count, UiFmt::DbOffset(DspMath::LinearToDb(n->Level())).c);
         else
            snprintf(stat, sizeof(stat), "%d in \xe2\x86\x92 1 out \xc2\xb7 silent", count);
      }
      else
         snprintf(stat, sizeof(stat), "0 in \xe2\x86\x92 0 out \xc2\xb7 idle");

      BeginAudioBody(gn.index, gn.category, bodyW, stat);
      Ui::Spacer(Ui::Space::S1);

      // Channels param control
      {
         int ch = n->numChannels;
         // A stepper, not a modulatable slider: the channel count rebuilds the audio topology, so
         // a cable driving it would rebuild it every block. The counter still advances so every
         // later param keeps its modulation index.
         gParamCounter++;
         const float btnH = ImGui::GetFrameHeight();
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         ImGui::AlignTextToFramePadding();
         ImGui::TextDisabled("channels");
         FormParts::Inline();
         int next = ch;
         if (ActionButton::Draw("-##mixCh", ImVec2(btnH * tok::note_mixer_x, btnH)))
            next = ch - 1;
         FormParts::Inline(0.0f, gap);
         ImGui::AlignTextToFramePadding();
         ImGui::Text("%d", ch);
         FormParts::Inline(0.0f, gap);
         if (ActionButton::Draw("+##mixCh", ImVec2(btnH * tok::note_mixer_x, btnH)))
            next = ch + 1;
         // The body's "+" stops at kBodySlots; a console the panel widened past it keeps its count.
         next = std::clamp(next, 0, std::max(ch, (int)MixerNode::kBodySlots));
         if (next != ch)
         {
            PushUndoCheckpoint();
            n->numChannels = next;
            RebuildAudioTopology();
         }
         Ui::Spacer(Ui::Space::S1);
      }

      if (count > 0)
      {
         const float cellW = gAudioContentW / (float)count;
         const float faderH = kFaderH;
         const float stripTop = ImGui::GetCursorScreenPos().y;

         // Meters first, drawn straight to the draw list so they cost no layout
         // and sit inside each fader's own cell.
         for (int i = 0; i < count; i++)
         {
            const float cx = gAudioContentX + ((float)i + tok::note_cx_inset) * cellW;
            DrawStripMeter(cx + kFaderMeterDx, stripTop + kFaderMeterPad, kFaderMeterW, faderH - tok::note_strip_meter_h * kFaderMeterPad, n->ChannelLevel(i));
         }

         {
            AudioKnobRow row(count, faderH);
            for (int i = 0; i < count; i++)
            {
               char label[16];
               snprintf(label, sizeof(label), "%d", i + 1);
               row.Fader(label, &n->gainDb[i], -60.0f, 12.0f, "%.1f dB", faderH, /*dbTaper=*/true);
            }
            row.End();
         }

         // Solo (S) and Mute (M) row
         {
            const float btnW = std::min((cellW - tok::space_2) * 0.5f, tok::note_btn_w_max);
            const float rowY = ImGui::GetCursorScreenPos().y;
            for (int i = 0; i < count; i++)
            {
               const float cx = gAudioContentX + ((float)i + tok::note_cx_inset) * cellW;
               const float sX = cx - btnW - tok::space_0_5;
               const float mX = cx + tok::space_0_5;

               // Solo (S) button
               ImGui::SetCursorScreenPos(ImVec2(sX, rowY));
               ImGui::PushID(9100 + i);
               bool soloVal = n->solo[i];
               if (AudioSoloButton("S", &soloVal, btnW))
               {
                  PushUndoCheckpoint();
                  n->solo[i] = soloVal;
               }
               ImGui::PopID();

               // Mute (M) button
               ImGui::SetCursorScreenPos(ImVec2(mX, rowY));
               ImGui::PushID(9200 + i);
               bool muteVal = n->mute[i];
               if (AudioMuteButton("M", &muteVal, btnW))
               {
                  PushUndoCheckpoint();
                  n->mute[i] = muteVal;
               }
               ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowY));
            Ui::Claim(ImVec2(gAudioContentW, ImGui::GetFrameHeight() + tok::space_1));
         }

         {
            // A knob a size down: at kKnobStd the input dot sat on the ring in an 80 px cell.
            const float panDia = std::min(kKnobStd, cellW - tok::note_mixer_body_gap);
            AudioKnobRow row(count, panDia);
            for (int i = 0; i < count; i++)
            {
               char label[16];
               snprintf(label, sizeof(label), "pan %d", i + 1);
               row.Knob(label, &n->pan[i], -1.0f, 1.0f, "%.2f", panDia);
            }
            row.End();
         }
      }

      EndAudioBody();
   }


   // A cable on a band pin that the new band count no longer has would point
   // at a pin nobody draws, and the node editor drops links to undrawn pins.
   // Disconnect those here, once per change, in the same undo step.
   void DropSplitterCablesPast(SplitterNode* n, int pins)
   {
      bool dropped = false;
      for (GraphNode& other : gNodes)
      {
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = other.node->AudioInputSlot(slot);
            if (cable != nullptr && cable->GetSource() == n && cable->GetOutputSlot() >= pins)
            {
               cable->Disconnect();
               dropped = true;
            }
         }
      }
      if (dropped)
         RebuildAudioTopology();
   }


   // Copy mode is the plain fan-out point (audio-graph-semantics.md §1).
   // Crossover mode turns the pins into bands; the split knobs past the band
   // count grey rather than vanish, so no cell moves (P5).
   void DrawSplitterBody(GraphNode& gn, SplitterNode* n)
   {
      const int bands = n->Bands();
      char stat[64];
      if (bands <= 1)
         snprintf(stat, sizeof(stat), "1 in \xE2\x86\x92 up to %d out", SplitterNode::kMaxFanout);
      else
         snprintf(stat, sizeof(stat), "%d bands \xC2\xB7 LR4, sum flat", bands);
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      Ui::Spacer(Ui::Space::S1);
      {
         static const std::vector<std::string> kModes = { "copy", "2 bands", "3 bands", "4 bands" };
         AudioKnobRow row(tok::note_splitter_body_audio_knob_row_layout);
         row.Dropdown("bands", kModes, bands - 1, [n](int i) {
            PushUndoCheckpoint();
            n->bands = i + 1;
            RebuildAudioTopology();
         });
         float* splits[3] = { &n->split1, &n->split2, &n->split3 };
         static const char* kNames[3] = { "split1", "split2", "split3" };
         for (int s = 0; s < 3; s++)
         {
            const bool grey = s + 2 > bands && !gParamRegisterOnly;
            if (grey)
               ImGui::BeginDisabled();
            row.Knob(kNames[s], splits[s], params::note::splitter_body_lo, params::note::splitter_body_hi, "%.0f Hz", kKnobSmall, /*dbTaper=*/false, /*freqTaper=*/true);
            if (grey)
               ImGui::EndDisabled();
         }
         row.End();
      }
      // A modulated or loaded band count lands here too, not just the dropdown.
      DropSplitterCablesPast(n, n->OutputCount());
      EndAudioBody();
   }


   // Two octaves of piano keyboard showing what is held right now. This is the
   // MIDI Notes node's visualizer for the same reason the sequencer's step
   // grid was its own: it is the single piece of feedback that tells you
   // whether the node is receiving anything at all, which is the first thing
   // you need to know when a patch is silent.
   // `ringNote` (-1 = none) draws a coloured outline around one key on top
   // of its lit/unlit fill - Note Filter's live last-note-passed/blocked
   // marker (node-ui-pillars P9's "one live marker" requirement). MIDI Notes
   // doesn't pass it, so its two call sites are unaffected by the default.
   // ui-content-begin: instrument picture: keys/strings drawn as the instrument
   void DrawMidiKeyboard(const bool held[128], int lowNote, int octaves, int ringNote = -1, bool ringGood = true)
   {
      const float w = gAudioBodyW, h = tok::note_h;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
      static const int kBlackOffsets[5] = { 1, 3, 6, 8, 10 };
      static const float kBlackSlot[5] = { 0.0f, 1.0f, 3.0f, 4.0f, 5.0f };

      const int whiteCount = 7 * octaves;
      const float keyW = (w - tok::space_1) / (float)whiteCount;
      const ImU32 ringCol = ringGood ? tok::Role(tok::col::accent_pale_muted_warm) : tok::Role(tok::col::text_err);

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + tok::space_0_5 + (float)(o * tok::note_midi_keyboard_ratio + k) * keyW;
            const ImU32 whiteCol = on ? (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_warm, tok::note_midi_keyboard_accent_pale_muted_warm_alpha))
                                      : (isLight ? tok::Base(tok::col::ink_white) : tok::Base(tok::col::surface_hi));
            dl->AddRectFilled(ImVec2(x + tok::px_align, origin.y + tok::space_0_75), ImVec2(x + keyW - tok::px_align, br.y - tok::space_0_75),
                              whiteCol, tok::radius_cell);
            if (note == ringNote)
               dl->AddRect(ImVec2(x + tok::space_0_25, origin.y + tok::space_1), ImVec2(x + keyW - tok::space_0_25, br.y - tok::space_1),
                          ringCol, tok::note_midi_keyboard_radius, 0, tok::note_midi_keyboard_stroke);
         }
      }
      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 5; k++)
         {
            const int note = lowNote + o * 12 + kBlackOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + tok::space_0_5 + ((float)(o * tok::note_midi_keyboard_ratio) + kBlackSlot[k] + 1.0f) * keyW - keyW * tok::note_x_ratio;
            const ImU32 blackCol = on ? (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_mid))
                                      : (isLight ? tok::Base(tok::col::surface_lo_light) : tok::Base(tok::col::edge_deep));
            dl->AddRectFilled(ImVec2(x, origin.y + tok::space_0_75), ImVec2(x + keyW * tok::note_fill_x, origin.y + h * tok::note_fill_y),
                              blackCol, tok::radius_cell);
            if (note == ringNote)
               dl->AddRect(ImVec2(x, origin.y + tok::space_0_75), ImVec2(x + keyW * tok::note_fill_x, origin.y + h * tok::note_fill_y),
                          ringCol, tok::note_midi_keyboard_radius, 0, tok::note_midi_keyboard_stroke);
         }
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using MidiNotesBodyRow = params::Row<MidiNotesNode>;
      const MidiNotesBodyRow kMidiNotesBodyRows[] = {
         MidiNotesBodyRow::KnobInt<&MidiNotesNode::channel>("channel", params::MidiNotes::channel_lo, params::MidiNotes::channel_hi).Size(kKnobLarge),
         MidiNotesBodyRow::KnobInt<&MidiNotesNode::transpose>("transpose", params::MidiNotes::transpose_lo, params::MidiNotes::transpose_hi).Size(kKnobLarge),
         MidiNotesBodyRow::Knob<&MidiNotesNode::velocityScale>("velocity", params::MidiNotes::velocity_scale_lo, params::MidiNotes::velocity_scale_hi).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawMidiNotesBody(GraphNode& gn, MidiNotesNode* n)
   {
      bool held[128];
      n->HeldKeys(held);
      const int count = n->HeldCount();
      n->PollLearn();

      char stat[80];
      if (!Platform::MidiIsRunning())
         snprintf(stat, sizeof(stat), "no MIDI input");
      else if (count > 0)
         snprintf(stat, sizeof(stat), "%d key%s held", count, count == 1 ? "" : "s");
      else
         snprintf(stat, sizeof(stat), "listening  -  %s",
                  n->channel < 0 ? "omni" : "one channel");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // Centred on the last note played so a keyboard playing outside the
      // default range still shows something, rather than sitting dark.
      const int last = n->LastNote();
      const int lowNote = last >= 0 ? std::clamp((last / 12) * 12 - 12, 0, 108) : 48;
      DrawMidiKeyboard(held, lowNote, 3);
      Ui::Spacer(Ui::Space::S1_25);

      {
         AudioKnobRow row(tok::note_midi_notes_body_audio_knob_row_layout, kKnobLarge);
         // -1 is omni, so the knob's low end is a mode rather than a channel
         // number - spelled out in the readout format rather than hidden.
         Ui::DrawKnobs(row, n, kMidiNotesBodyRows);
         row.End();
      }
      // MPE: each held note follows its own member channel's pitch bend.
      BeginAudioSection("mpe");
      {
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float checkW = tok::note_check_w;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         Ui::DrawParam(n, MidiNotesBodyRow::Toggle<&MidiNotesNode::mpe>("mpe##midiMpe"));
         ImGui::SetCursorScreenPos(ImVec2(at.x + checkW + gap, at.y));
         AudioSlider("bend st", &n->mpeBendRange, params::MidiNotes::mpe_bend_range_lo, params::MidiNotes::mpe_bend_range_hi, "%.0f", gAudioContentW - checkW - gap);
      }
      EndAudioSection();

      BeginAudioSection("input");
      if (!Platform::MidiIsRunning())
      {
         if (ActionButton::Draw("Start MIDI", ImVec2(kPreviewSize, 0)))
            n->StartListening();
         ImGui::TextDisabled("click to start listening for a connected controller");
      }
      else
      {
         // Device: every input by default, or one connected controller by name.
         const auto sources = Platform::MidiListSources();
         std::vector<std::string> options { "All MIDI inputs" };
         int currentIdx = 0;
         for (const auto& src : sources)
         {
            if (src.name == n->deviceName)
               currentIdx = (int)options.size();
            options.push_back(src.name);
         }
         std::string caption = n->deviceName.empty() ? options[0] : n->deviceName;
         if (n->DeviceMissing())
            caption += " (not connected)";
         // One row, edge to edge in the section: device on the left, Learn on
         // the right, the same height.
         const float gap = ImGui::GetStyle().ItemSpacing.x;
         const float learnW = tok::note_learn_w;
         const ImVec2 rowAt = ImGui::GetCursorScreenPos();
         PushDropdownStyle();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, rowAt.y));
         if (NodeDropdownField((caption + "##midiDevice").c_str(), ImVec2(gAudioContentW - learnW - gap, 0)))
         {
            gDropdown.options = options;
            gDropdown.categories.assign(options.size(), "");
            gDropdown.onSelect = [n, options](int idx) {
               if (idx < 0 || idx >= (int)options.size())
                  return;
               PushUndoCheckpoint();
               n->deviceName = idx == 0 ? std::string() : options[idx];
            };
            gDropdown.current = currentIdx;
            gDropdown.justOpened = true;
            gDropdown.focusSearch = false;
         }
         const float rowH = ImGui::GetItemRectSize().y;
         PopDropdownStyle();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + gAudioContentW - learnW, rowAt.y));
         const bool learning = n->IsLearning();
         if (ActionButton::Draw(learning ? "Play a key" : "Learn", ImVec2(learnW, rowH),
                                learning ? ActionButton::Kind::Learn : ActionButton::Kind::Plain))
         {
            if (learning)
               n->CancelLearn();
            else
            {
               PushUndoCheckpoint();
               n->BeginLearn();
            }
         }
         if (n->DeviceMissing())
            ImGui::TextDisabled("not connected - listening to all inputs");
      }
      EndAudioSection();

      EndAudioBody();
   }


   // Dedicated interactive piano for KeyboardNode - deliberately not shared
   // with DrawMidiKeyboard, whose caller (DrawMidiNotesBody above) recentres
   // lowNote from the last played note every frame. That recentering is
   // exactly why clicking used to feel misaligned: the widget moved out from
   // under an active click on the very next frame. This one takes a fixed
   // lowNote and never moves, so what you see is always what you're pressing.
   // Returns the note the mouse is currently over while the button is held
   // (drag included), or -1 when nothing is pressed.
   // ui-content-begin: instrument picture: keys/strings drawn as the instrument
   int DrawInteractiveKeyboard(const bool held[128], int lowNote, int octaves)
   {
      const float w = gAudioBodyW, h = tok::note_interactive_keyboard_h;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      static const int kWhiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
      static const int kBlackOffsets[5] = { 1, 3, 6, 8, 10 };
      static const float kBlackSlot[5] = { 0.0f, 1.0f, 3.0f, 4.0f, 5.0f };

      const int whiteCount = 7 * octaves;
      const float keyW = (w - tok::space_1) / (float)whiteCount;

      ImGui::SetCursorScreenPos(origin);
      UiInteract::Hit("##keyboardHit", ImVec2(w, h));
      const bool active = ImGui::IsItemActive();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      int hitNote = -1;
      if (active && mouse.x >= origin.x && mouse.x < br.x && mouse.y >= origin.y && mouse.y < br.y)
      {
         // Black keys sit visually on top, so test them first.
         const float relY = mouse.y - origin.y;
         if (relY < h * tok::note_interactive_keyboard_ratio)
         {
            for (int o = 0; o < octaves && hitNote < 0; o++)
            {
               for (int k = 0; k < 5; k++)
               {
                  const int note = lowNote + o * 12 + kBlackOffsets[k];
                  const float x = origin.x + tok::space_0_5 + ((float)(o * tok::note_interactive_keyboard_scale) + kBlackSlot[k] + 1.0f) * keyW - keyW * tok::note_x_ratio;
                  if (mouse.x >= x && mouse.x < x + keyW * tok::note_fill_scale)
                  {
                     hitNote = note;
                     break;
                  }
               }
            }
         }
         if (hitNote < 0)
         {
            const int col = std::clamp((int)((mouse.x - origin.x - tok::space_0_5) / keyW), 0, whiteCount - 1);
            hitNote = lowNote + (col / 7) * 12 + kWhiteOffsets[col % 7];  // ui-audit-ok: index (white-key column to note: 7 white keys, 12 semitones per octave)
         }
      }

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + tok::space_0_5 + (float)(o * tok::note_interactive_keyboard_scale + k) * keyW;
            const ImU32 whiteCol = on ? (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_warm, tok::note_interactive_keyboard_accent_pale_muted_warm_alpha))
                                      : (isLight ? tok::Base(tok::col::ink_white) : tok::Base(tok::col::surface_hi));
            dl->AddRectFilled(ImVec2(x + tok::px_align, origin.y + tok::space_0_75), ImVec2(x + keyW - tok::px_align, br.y - tok::space_0_75),
                              whiteCol, tok::radius_cell);
         }
      }
      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 5; k++)
         {
            const int note = lowNote + o * 12 + kBlackOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + tok::space_0_5 + ((float)(o * tok::note_interactive_keyboard_scale) + kBlackSlot[k] + 1.0f) * keyW - keyW * tok::note_x_ratio;
            const ImU32 blackCol = on ? (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_mid))
                                      : (isLight ? tok::Base(tok::col::surface_lo_light) : tok::Base(tok::col::edge_deep));
            dl->AddRectFilled(ImVec2(x, origin.y + tok::space_0_75), ImVec2(x + keyW * tok::note_fill_scale, origin.y + h * tok::note_interactive_keyboard_ratio),
                              blackCol, tok::radius_cell);
         }
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      return hitNote;
   }
   // ui-content-end


   struct TypingKey { ImGuiKey key; int semitone; };


   // Logic Pro / GarageBand's "Musical Typing" layout - the de facto standard
   // for playing a software instrument from a QWERTY keyboard. Two rows, each
   // shaped like one octave of a real piano (letter row = white keys, row
   // above = black keys sitting between the white keys they sharp), with the
   // upper row exactly one octave above the lower.
   static const TypingKey kTypingKeys[] = {
      { ImGuiKey_Z, 0 }, { ImGuiKey_S, 1 }, { ImGuiKey_X, 2 }, { ImGuiKey_D, 3 },
      { ImGuiKey_C, 4 }, { ImGuiKey_V, 5 }, { ImGuiKey_G, 6 }, { ImGuiKey_B, 7 },
      { ImGuiKey_H, 8 }, { ImGuiKey_N, 9 }, { ImGuiKey_J, 10 }, { ImGuiKey_M, 11 },
      { ImGuiKey_Comma, 12 }, { ImGuiKey_L, 13 }, { ImGuiKey_Period, 14 },
      { ImGuiKey_Semicolon, 15 }, { ImGuiKey_Slash, 16 },
      { ImGuiKey_Q, 12 }, { ImGuiKey_2, 13 }, { ImGuiKey_W, 14 }, { ImGuiKey_3, 15 },
      { ImGuiKey_E, 16 }, { ImGuiKey_R, 17 }, { ImGuiKey_5, 18 }, { ImGuiKey_T, 19 },
      { ImGuiKey_6, 20 }, { ImGuiKey_Y, 21 }, { ImGuiKey_7, 22 }, { ImGuiKey_U, 23 },
      { ImGuiKey_I, 24 }, { ImGuiKey_9, 25 }, { ImGuiKey_O, 26 }, { ImGuiKey_0, 27 },
      { ImGuiKey_P, 28 },
   };


   namespace
   {
      using KeyboardBodyRow = params::Row<KeyboardNode>;
      const KeyboardBodyRow kKeyboardBodyRows[] = {
         KeyboardBodyRow::KnobInt<&KeyboardNode::baseOctave>("octave", params::Keyboard::base_octave_lo, params::Keyboard::base_octave_hi).Size(kKnobLarge),
         KeyboardBodyRow::KnobInt<&KeyboardNode::transpose>("transpose", params::Keyboard::transpose_lo, params::Keyboard::transpose_hi).Size(kKnobLarge),
         KeyboardBodyRow::Knob<&KeyboardNode::velocityScale>("velocity", params::Keyboard::velocity_scale_lo, params::Keyboard::velocity_scale_hi).Fmt("%.2f").Size(kKnobLarge),
      };
   }

   void DrawKeyboardBody(GraphNode& gn, KeyboardNode* n)
   {
      bool held[128];
      n->HeldKeys(held);
      const int last = n->LastNote();

      static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
      char stat[64];
      if (last >= 0)
         snprintf(stat, sizeof(stat), "%s%d", kNoteNames[((last % 12) + 12) % 12], last / 12 - 1);
      else
         snprintf(stat, sizeof(stat), "click or type to play \xc2\xb7 -/= octave");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      const int baseNote = (n->baseOctave + 1) * 12;
      const int lowNote = std::clamp(baseNote, 0, 103);   // fixed - never recentres under an active click
      const int hit = DrawInteractiveKeyboard(held, lowNote, 3);  // ui-audit-ok: model (octave count)

      if (hit != n->mMouseNote)
      {
         if (n->mMouseNote >= 0)
            n->SetKeyState(n->mMouseNote, false);
         if (hit >= 0)
            n->SetKeyState(hit, true);
         n->mMouseNote = hit;
      }

      // The letter keys play the Keyboard under the mouse, or the one Keyboard that is the whole
      // selection (the armed-track rule: New Synth selects its Keyboard, so it plays with no hover).
      const bool isHovered = ed::GetHoveredNode() == ed::NodeId(gn.NodeId()) ||
                             (ed::GetSelectedObjectCount() == 1 && ed::IsNodeSelected(gn.NodeId()));
      if (n->computerKeyboardEnabled && isHovered)
         gComputerKeyboardHot = true; // letter keys belong to this node; canvas WASD/F/H/U stand down
      // No Cmd/Ctrl/Alt: Cmd+Z, Cmd+S, Cmd+C... are shortcuts, not notes (Z, S and C are piano keys). No Shift either:
      // Shift+letter is a canvas chord, and a capital typed by mistake should not sound. !gArrangeFocused: the
      // timeline owns the keyboard (its M marker key would otherwise also play a note here).
      const ImGuiIO& io = ImGui::GetIO();
      const bool canPlay = n->computerKeyboardEnabled && isHovered && !TextFocusClaimed() && !gArrangeFocused &&
                           !io.KeyCtrl && !io.KeySuper && !io.KeyAlt && !io.KeyShift;
      static_assert(sizeof(kTypingKeys) / sizeof(kTypingKeys[0]) <= sizeof(n->mTypedNotes) / sizeof(int), "mTypedNotes");
      for (int i = 0; i < (int)(sizeof(kTypingKeys) / sizeof(kTypingKeys[0])); i++)
      {
         const TypingKey& tk = kTypingKeys[i];
         // Release always processed, even off-hover, so a key held while the
         // mouse leaves the node can't leave a note stuck on. Press requires
         // hover + the toggle, so typing here doesn't hijack shortcuts
         // elsewhere in the app.
         if (ImGui::IsKeyReleased(tk.key))
         {
            if (n->mTypedNotes[i] > 0)
               n->SetKeyState(n->mTypedNotes[i] - 1, false);
            n->mTypedNotes[i] = 0;
         }
         else if (canPlay && n->mTypedNotes[i] == 0 && ImGui::IsKeyPressed(tk.key, false))
         {
            const int note = std::clamp(baseNote + tk.semitone, 0, 127);
            n->SetKeyState(note, true);
            n->mTypedNotes[i] = note + 1;
         }
      }
      // - and = step the octave (Logic's Musical Typing keys). Held keys keep the note they pressed.
      if (canPlay && (Keymap::KeyPressed(Keymap::Act::OctaveDown) || Keymap::KeyPressed(Keymap::Act::OctaveUp)))
      {
         const int next = std::clamp(n->baseOctave + (Keymap::KeyPressed(Keymap::Act::OctaveUp) ? 1 : -1), -1, 7);
         if (next != n->baseOctave)
         {
            PushUndoCheckpoint();
            n->baseOctave = next;
         }
      }

      Ui::Spacer(Ui::Space::S1_25);

      {
         AudioKnobRow row(tok::note_keyboard_body_audio_knob_row_layout, kKnobLarge);
         Ui::DrawKnobs(row, n, kKeyboardBodyRows);
         row.End();
      }
      {
         // Checkbox in the leftmost cell (P3), matching the 3-cell knob row
         // above it (P1 corollary) with the rest deliberately skipped (P6) -
         // the same "bottom left" shape as the other AudioEffects checkbox
         // rows (e.g. DrawChorusBody's "analog" row).
         AudioKnobRow row(tok::note_keyboard_body_audio_knob_row_layout, tok::note_row_layout_lg, 0.0f, false);
         Ui::DrawKnob(row, n, KeyboardBodyRow::Toggle<&KeyboardNode::computerKeyboardEnabled>("numpad##kbToggle"));
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   const std::vector<std::string>& NoteNameList()
   {
      static std::vector<std::string> list = { "C", "C#", "D", "D#", "E", "F",
                                                "F#", "G", "G#", "A", "A#", "B" };
      return list;
   }


   // Full MIDI note range (0..127) as "C-1".."G9", for a dropdown that picks one specific note
   // rather than just a pitch class - same naming convention as the "%s%d" readouts elsewhere
   // (NoteNameList()[n % 12], n / 12 - 1).
   const std::vector<std::string>& MidiNoteNameList()
   {
      static std::vector<std::string> list = [] {
         std::vector<std::string> l;
         l.reserve(128);
         for (int n = 0; n < 128; n++)
            l.push_back(NoteNameList()[n % 12] + std::to_string(n / 12 - 1));
         return l;
      }();
      return list;
   }


   namespace
   {
      using CVToPitchParamsRow = params::Row<CVToPitchNode>;
   }

   void DrawCVToPitchParams(CVToPitchNode* n)
   {
      // The semitone readout is the point of this node - make it the
      // visually dominant element, not a text line under six sliders.
      char st[16];
      snprintf(st, sizeof(st), "%s", UiFmt::Semi(n->LastSemitone()).c);
      {
         UiType::Scope readout(UiType::Size::Display, UiType::Weight::Medium);
         ImGui::TextUnformatted(st);
      }

      if (n->input == nullptr)
         Ui::DrawParam(n, CVToPitchParamsRow::Slider<&CVToPitchNode::constantIn>("in (no cable)", 0.0f, 1.0f));
      else
         ImGui::TextDisabled("in: patched");

      if (ModSliderInt("range low", &n->rangeLow, params::CVToPitch::range_low_lo, params::CVToPitch::range_low_hi))
         n->rangeHigh = std::max(n->rangeHigh, n->rangeLow + 1);
      if (ModSliderInt("range high", &n->rangeHigh, params::CVToPitch::range_high_lo, params::CVToPitch::range_high_hi))
         n->rangeLow = std::min(n->rangeLow, n->rangeHigh - 1);

      DropdownButton("scale", MusicTime::ScaleTypeList(), n->scale,
                     [n](int i) { n->scale = i; });
      if (n->scale != MusicTime::kChromatic)
         DropdownButton("root", NoteNameList(), n->root, [n](int i) { n->root = i; });

      Ui::DrawParam(n, CVToPitchParamsRow::Slider<&CVToPitchNode::glideMs>("glide (ms)", params::CVToPitch::glide_ms_lo, params::CVToPitch::glide_ms_hi));
   }


   namespace
   {
      using NoteToCVParamsRow = params::Row<NoteToCVNode>;
   }

   void DrawNoteToCVParams(NoteToCVNode* n)
   {
      const int last = n->LastNote();
      if (last >= 0)
         ImGui::TextDisabled("last: %s%d (MIDI %d)", NoteNameList()[last % 12].c_str(), last / 12 - 1, last);
      else
         ImGui::TextDisabled("last: none");

      if (ModSliderInt("range low", &n->rangeLow, params::NoteToCV::range_low_lo, params::NoteToCV::range_low_hi))
         n->rangeHigh = std::max(n->rangeHigh, n->rangeLow + 1);
      if (ModSliderInt("range high", &n->rangeHigh, params::NoteToCV::range_high_lo, params::NoteToCV::range_high_hi))
         n->rangeLow = std::min(n->rangeLow, n->rangeHigh - 1);

      Ui::DrawParam(n, NoteToCVParamsRow::Slider<&NoteToCVNode::glideMs>("glide (ms)", params::NoteToCV::glide_ms_lo, params::NoteToCV::glide_ms_hi));
   }


   namespace
   {
      using VelocityToCVParamsRow = params::Row<VelocityToCVNode>;
      const VelocityToCVParamsRow kVelocityToCVParamsRows[] = {
         VelocityToCVParamsRow::Slider<&VelocityToCVNode::rangeLow>("range low", 0.0f, 1.0f),
         VelocityToCVParamsRow::Slider<&VelocityToCVNode::rangeHigh>("range high", 0.0f, 1.0f),
      };
   }

   void DrawVelocityToCVParams(VelocityToCVNode* n)
   {
      ImGui::TextDisabled("velocity: %s", UiFmt::F2(n->Value01()).c);
      Ui::DrawParams(n, kVelocityToCVParamsRows);
   }


   namespace
   {
      using MidiOutParamsRow = params::Row<MidiOutNode>;
      const MidiOutParamsRow kMidiOutParamsRows[] = {
         MidiOutParamsRow::SliderInt<&MidiOutNode::channel>("channel", params::MidiOut::channel_lo, params::MidiOut::channel_hi),
         MidiOutParamsRow::Slider<&MidiOutNode::offsetMs>("offset (ms)", params::MidiOut::offset_ms_lo, params::MidiOut::offset_ms_hi).Fmt("%.1f"),
      };
   }

   void DrawMidiOutParams(MidiOutNode* n)
   {
      // The device list is a live OS query, so it is refreshed on a slow timer, not every frame.
      if (ImGui::GetTime() - n->devicesListedAt > tok::motion_s_rescan_slow)
      {
         n->RefreshDeviceList();
         n->devicesListedAt = ImGui::GetTime();
      }

      std::vector<std::string> options { "none" };
      int current = 0;
      for (const std::string& d : n->Devices())
      {
         options.push_back(d);
         if (d == n->device) current = (int)options.size() - 1;
      }
      // A saved device that is not plugged in stays selectable so the choice survives a restart.
      if (!n->device.empty() && current == 0)
      {
         options.push_back(n->device + " (not connected)");
         current = (int)options.size() - 1;
      }
      DropdownButton("device", options, current, [n](int i) {
         PushUndoCheckpoint();
         if (i <= 0)
            n->device.clear();
         else if ((size_t)i <= n->Devices().size()) // the "(not connected)" row keeps the saved name
            n->device = n->Devices()[i - 1];
      });

      Ui::DrawParams(n, kMidiOutParamsRows);
      NodeCheckbox("clock##midiOutClock", &n->clock);

      // Stable labels, so a modulation cable stays bound when the cc number changes.
      static const char* const kOnLbl[MidiOutNode::kCcRows] = { "cc1##midiOutCc1", "cc2##midiOutCc2", "cc3##midiOutCc3", "cc4##midiOutCc4" };
      static const char* const kNumLbl[MidiOutNode::kCcRows] = { "cc1 #", "cc2 #", "cc3 #", "cc4 #" };
      static const char* const kValLbl[MidiOutNode::kCcRows] = { "cc1 value", "cc2 value", "cc3 value", "cc4 value" };
      for (int i = 0; i < MidiOutNode::kCcRows; i++)
      {
         NodeCheckbox(kOnLbl[i], &n->ccOn[i]);
         ModSliderInt(kNumLbl[i], &n->ccNum[i], params::MidiOut::cc_num_lo, params::MidiOut::cc_num_hi);
         ModSlider(kValLbl[i], &n->ccVal[i], 0.0f, 1.0f);
      }

      if (ActionButton::Draw("panic##midiOutPanic", ImVec2(kPreviewSize, 0)))
         n->Panic();

      if (!n->DeviceError().empty())
         ImGui::TextDisabled("%s", n->DeviceError().c_str());
      else if (n->DeviceOpen())
         ImGui::TextDisabled("sent: %llu", n->SentCount());
      else
         ImGui::TextDisabled("no device");
   }


   namespace
   {
      using CVRecorderParamsRow = params::Row<CVRecorderNode>;
      const CVRecorderParamsRow kCVRecorderParamsRows[] = {
         CVRecorderParamsRow::Slider<&CVRecorderNode::speed>("speed", params::CVRecorder::speed_lo, params::CVRecorder::speed_hi),
         CVRecorderParamsRow::Slider<&CVRecorderNode::low>("low", 0.0f, 1.0f),
         CVRecorderParamsRow::Slider<&CVRecorderNode::high>("high", 0.0f, 1.0f),
      };
   }

   void DrawCVRecorderParams(CVRecorderNode* n)
   {
      // One button: rec -> stop, and stopping starts the loop immediately.
      // Fixed width (the preview's), never derived from
      // GetContentRegionAvail: inside an auto-sizing node that feeds back on
      // itself and collapsed the old three buttons into slivers.
      const bool recording = n->IsRecording();
      char label[48];
      if (recording)
         snprintf(label, sizeof(label), "stop  (%s)###cvRec", UiFmt::Beats(n->LengthBeats()).c);
      else
         snprintf(label, sizeof(label), "record###cvRec");
      if (ActionButton::Draw(label, ImVec2(kPreviewSize, 0), (recording) ? ActionButton::Kind::Record : ActionButton::Kind::Plain))
      {
         if (recording)
            n->StopRecording();
         else
         {
            PushUndoCheckpoint();
            // Recording runs on the beat clock; a stopped transport would
            // capture a single sample.
            if (!Transport::Instance().IsPlaying())
               Transport::Instance().SetPlaying(true);
            n->StartRecording();
         }
      }

      Ui::DrawParam(n, CVRecorderParamsRow::Slider<&CVRecorderNode::constantIn>("in (no cable)", 0.0f, 1.0f), n->input == nullptr);

      Ui::DrawParams(n, kCVRecorderParamsRows);
   }


   // Effective scale/root - the values AudioNoteFilterNode::ProcessBlock
   // (NoteNodes.cpp) actually gates against. When useGlobalScale is on, the
   // node's own scale/root fields are disabled in the UI and ignored on the
   // audio thread in favour of the transport's; the visualizer has to read
   // the same source or it lies about what's being applied.
   void NoteFilterEffectiveScale(NoteFilterNode* n, int& outScale, int& outRoot)
   {
      if (n->useGlobalScale)
      {
         outScale = Transport::Instance().Scale();
         outRoot = Transport::Instance().Key();
      }
      else
      {
         outScale = n->scale;
         outRoot = n->root;
      }
   }


   // Reuses DrawMidiKeyboard's real black/white piano look (node-ui-pillars
   // P9/P3 feedback: this used to be an all-blue-tinted-rectangle strip with
   // clip-arrow clutter and a root tick that read as a stray mark). A key is
   // lit iff a note there would actually pass the gate right now - in range
   // and in the effective scale - exactly like DrawMidiKeyboard's "held"
   // meaning, just computed from params instead of MIDI state. The one thing
   // kept from the old visualizer is the live last-note ring - useful, not
   // part of the complaint.
   void DrawNoteFilterVisualizer(NoteFilterNode* n)
   {
      int scale, root;
      NoteFilterEffectiveScale(n, scale, root);

      const int rangeLow = std::clamp(n->rangeLow, 0, 127);
      const int rangeHigh = std::clamp(n->rangeHigh, rangeLow, 127);

      // Size a 2-4 octave window to contain the whole range, centred on its
      // midpoint, anchored to a C (node-ui-pillars P9) - never anchored to
      // root. Keyboard layout stays a real instrument regardless of what
      // scale/root is selected; only which keys light up changes with those.
      const int octaves = std::clamp((rangeHigh - rangeLow) / 12 + 2, 2, 4);
      const int windowSemitones = octaves * 12;
      const int mid = (rangeLow + rangeHigh) / 2;
      const int maxLowNote = std::max(0, ((128 - windowSemitones) / 12) * 12);
      const int lowNote = std::clamp(((mid - windowSemitones / 2) / 12) * 12, 0, maxLowNote);

      bool lit[128];
      for (int note = 0; note < 128; note++)
      {
         const int pc = ((note - root) % 12 + 12) % 12;
         lit[note] = note >= rangeLow && note <= rangeHigh && MusicTime::ScaleContainsPitchClass(scale, pc);
      }

      DrawMidiKeyboard(lit, lowNote, octaves, n->LastNoteIn(), n->LastPassed());
   }


   namespace
   {
      using NoteFilterBodyRow = params::Row<NoteFilterNode>;
      const NoteFilterBodyRow kNoteFilterBodyRows[] = {
         NoteFilterBodyRow::KnobInt<&NoteFilterNode::rangeLow>("lo", params::NoteFilter::range_low_lo, params::NoteFilter::range_low_hi),
         NoteFilterBodyRow::KnobInt<&NoteFilterNode::rangeHigh>("hi", params::NoteFilter::range_high_lo, params::NoteFilter::range_high_hi),
         NoteFilterBodyRow::Knob<&NoteFilterNode::chance>("chance", params::NoteFilter::chance_lo, params::NoteFilter::chance_hi).Fmt("%.0f%%"),
      };
   }

   void DrawNoteFilterBody(GraphNode& gn, NoteFilterNode* n)
   {
      char stat[96];
      if (n->LastNoteIn() < 0)
      {
         // Truthful in both modes (node-ui-pillars P9): when useGlobalScale
         // is on, n->scale is the disabled/greyed field, not what's actually
         // gating - the status text has to name the effective scale instead.
         int effScale, effRoot;
         NoteFilterEffectiveScale(n, effScale, effRoot);
         snprintf(stat, sizeof(stat), "%s in %s%d..%s%d",
                  MusicTime::ScaleTable(effScale).name, NoteNameList()[n->rangeLow % 12].c_str(),
                  n->rangeLow / 12 - 1, NoteNameList()[n->rangeHigh % 12].c_str(), n->rangeHigh / 12 - 1);
      }
      else
         snprintf(stat, sizeof(stat), "last: %s%d - %s", NoteNameList()[n->LastNoteIn() % 12].c_str(),
                  n->LastNoteIn() / 12 - 1, n->LastPassed() ? "passed" : "blocked");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawNoteFilterVisualizer(n);
      Ui::Spacer(Ui::Space::S1_25);

      {
         // Selectors on their own row, left-aligned on the same 3-cell grid as the knobs below (P1, P3): a dropdown
         // is at most 112 wide in any cell, so the scale name is no more clipped here than in a 2-cell row.
         AudioKnobRow sel(tok::note_filter_body_audio_knob_row_layout, tok::note_sel_layout_lg, tok::note_sel_layout_sm, false);
         if (n->useGlobalScale)
            ImGui::BeginDisabled();
         sel.Dropdown("scale", MusicTime::ScaleTypeList(), n->scale,
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         sel.Dropdown("root", NoteNameList(), n->root,
                      [n](int i) { PushUndoCheckpoint(); n->root = i; });
         if (n->useGlobalScale)
            ImGui::EndDisabled();
         sel.Skip();
         sel.End();
      }
      {
         AudioKnobRow row(tok::note_filter_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kNoteFilterBodyRows);
         row.End();
      }

      EndAudioBody();
   }


   // Shared by every tempo-synced note generator (Arp, Note Sequencer,
   // Random Note Generator, Note Echo, Note Switcher): a rate expressed either
   // as a note division (synced to tempo) or free seconds.
   inline int NearestRateDivision(float beats)
   {
      return (int)MusicTime::NearestRateDivision((double)beats);
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
         row.Dropdown("rate", MusicTime::RateDivisionList(), div, [rateBeats](int i) {
            PushUndoCheckpoint();
            *rateBeats = (float)MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(i, 0, (int)MusicTime::kNumRateDivisions - 1));
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
         // A cable into the hidden rate shows dotted, ending on the division dropdown.
         NoteHiddenParamAnchorAtLastItem(rateSlot.paramIndex);
      }
      else
      {
         row.Knob("rate", rateSeconds, params::note::rate_lo, params::note::rate_hi, "%.2fs", kKnobSmall);
      }
   }


   // Shared "quantize to grid" dropdown - Off, 4 bars down to 1/32 (with dotted and triplets), 1/64 - used by
   // Quantizer (live note-on timing) and Note Capturer (recorded timing).
   #define kQuantizeLabels MusicTime::QuantizeGridList()


   // Standard shape for a one-knob note node (Note Transpose, Pitch Bend,
   // Gate, Glide, Vibrato below) - narrow body, one big centred knob, no
   // second row. Standardised so every one-knob note node looks like these
   // rather than each inventing its own compact layout.
   void DrawSingleKnobAudioBody(GraphNode& gn, const char* stat, const char* label, float* v, float lo, float hi,
                                const char* fmt, AudioWidgetStyle explicitStyle = AudioWidgetStyle::Knob,
                                FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr)
   {
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(1, kKnobLarge);
      row.Knob(label, v, lo, hi, fmt, kKnobLarge, false, false, explicitStyle, posToValue, valueToPos);
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
      AudioKnobRow row(1, kKnobLarge);
      row.KnobInt(label, v, lo, hi, kKnobLarge);
      row.End();
      EndAudioBody();
   }


   void DrawNoteTransposeBody(GraphNode& gn, NoteTransposeNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", UiFmt::Semi(n->semitones).c);
      DrawSingleKnobIntAudioBody(gn, stat, "transpose", &n->semitones, -48, 48);
   }


   void DrawPitchBendBody(GraphNode& gn, PitchBendNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", UiFmt::Semi(n->bendSemitones).c);
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
      DrawSingleKnobAudioBody(gn, stat, "glide", &n->glideMs, 0.0f, 2000.0f, "%.0f ms", AudioWidgetStyle::KnobSkewGlide150);
   }


   void DrawVibratoBody(GraphNode& gn, VibratoNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", UiFmt::Hz(n->rateHz).c);
      DrawSingleKnobAudioBody(gn, stat, "rate", &n->rateHz, 0.5f, 12.0f, "%.1f Hz");
   }


   // Velocity Curve's chart: y = x^curve over 0..1, both axes normalized -
   // the same "show the transfer function" idea as Audio Filter's response
   // curve or Dynamics' operating-point dot, scaled to this node's narrow
   // body. Read-only (the curve is fully determined by the one knob below
   // it), but redrawn from the live curve value every frame like every
   // other params-only visualizer in this file.
   // ui-content-begin: editable graph: the curve, ramp or grid the user drags
   void DrawVelocityCurveChart(float curve)
   {
      const float w = gAudioBodyW;
      const float h = w; // square, like Bouncing Balls' canvas
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const ImVec2 br(origin.x + w, origin.y + h);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      for (int i = 1; i < 4; i++)
      {
         dl->AddLine(ImVec2(origin.x, origin.y + h * i / tok::note_velocity_curve_chart_ratio), ImVec2(br.x, origin.y + h * i / tok::note_velocity_curve_chart_ratio),
                     ScopeGridCol(), tok::note_velocity_curve_chart_stroke);
         dl->AddLine(ImVec2(origin.x + w * i / tok::note_velocity_curve_chart_ratio, origin.y), ImVec2(origin.x + w * i / tok::note_velocity_curve_chart_ratio, br.y),
                     ScopeGridCol(), tok::note_velocity_curve_chart_stroke);
      }
      // The linear reference (curve == 1) as a dim diagonal, so the knob's
      // effect reads relative to "unchanged" rather than in isolation.
      dl->AddLine(ImVec2(origin.x, br.y), ImVec2(br.x, origin.y), ScopeMidLineCol(), tok::note_velocity_curve_chart_stroke);

      dl->PathClear();
      const int kSteps = 32;
      for (int i = 0; i <= kSteps; i++)
      {
         const float x = (float)i / (float)kSteps;
         const float y = std::pow(x, curve);
         dl->PathLineTo(ImVec2(origin.x + x * w, br.y - y * h));
      }
      dl->PathStroke(isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_alt, tok::note_path_alpha), 0, tok::note_path_stroke);

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      Ui::Claim(ImVec2(w, h));
   }
   // ui-content-end


   namespace
   {
      using VelocityCurveBodyRow = params::Row<VelocityCurveNode>;
   }

   void DrawVelocityCurveBody(GraphNode& gn, VelocityCurveNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "in %s -> out %s", UiFmt::PctOf(n->LastVelocityIn()).c,
               UiFmt::PctOf(n->LastVelocityOut()).c);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      DrawVelocityCurveChart(n->curve);
      Ui::Spacer(Ui::Space::S1);
      AudioKnobRow row(1, kKnobLarge);
      Ui::DrawKnob(row, n, VelocityCurveBodyRow::Knob<&VelocityCurveNode::curve>("curve", params::VelocityCurve::curve_lo, params::VelocityCurve::curve_hi).Fmt("%.2f").Size(kKnobLarge));
      row.End();
      EndAudioBody();
   }


   namespace
   {
      using HumanizerBodyRow = params::Row<HumanizerNode>;
      const HumanizerBodyRow kHumanizerBodyRows[] = {
         HumanizerBodyRow::Knob<&HumanizerNode::timingMs>("hu.time", params::Humanizer::timing_ms_lo, params::Humanizer::timing_ms_hi).Fmt("%.0f ms").Size(kKnobLarge),
         HumanizerBodyRow::Knob<&HumanizerNode::velocityPct>("hu.vel", params::Humanizer::velocity_pct_lo, params::Humanizer::velocity_pct_hi).Fmt("%.0f%%").Size(kKnobLarge),
      };
   }

   void DrawHumanizerBody(GraphNode& gn, HumanizerNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%s, %s vel", UiFmt::Ms(n->timingMs).c, UiFmt::Pct(n->velocityPct).c);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(tok::note_humanizer_body_audio_knob_row_layout, kKnobLarge);
      Ui::DrawKnobs(row, n, kHumanizerBodyRows);
      row.End();
      EndAudioBody();
   }


   void DrawQuantizerBody(GraphNode& gn, QuantizerNode* n)
   {
      const auto& options = MusicTime::QuantizeGridList();
      const int safeDiv = std::clamp(n->div, 0, (int)options.size() - 1);
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", options[safeDiv].c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(1, tok::note_quantizer_body_audio_knob_row_layout, tok::note_row_layout_sm, false);
      row.Dropdown("grid", options, safeDiv, [n](int i) {
         PushUndoCheckpoint();
         n->div = i;
      });
      row.End();
      EndAudioBody();
   }


   // The "NN% conf" badge sits on the right of the readout strip; shorten the idle
   // stat so the two never print on top of each other (200 pt nodes).
   void FitStatBesideConf(char* stat, size_t cap, float conf, float bodyW)
   {
      if (conf <= 0.0f)
         return;
      char badge[32];
      snprintf(badge, sizeof(badge), "%s conf", UiFmt::Pct((int)std::round(conf * 100.0f)).c);
      const float maxW = bodyW - ImGui::CalcTextSize(badge).x - tok::note_max_w_gap;
      std::string out = stat;
      if (ImGui::CalcTextSize(out.c_str()).x <= maxW)
         return;
      while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > maxW)
         out.pop_back();
      snprintf(stat, cap, "%s...", out.c_str());
   }


   namespace
   {
      using PredictiveQuantizeBodyRow = params::Row<PredictiveQuantizeNode>;
   }

   void DrawPredictiveQuantizeBody(GraphNode& gn, PredictiveQuantizeNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      if (n->ModeCount() > 0)
         snprintf(stat, sizeof(stat), "%d spacings  -  %s onsets total", n->ModeCount(), UiFmt::Count((long long)(n->TotalCaptured())).c);
      else if (n->TotalCaptured() > 0)
         snprintf(stat, sizeof(stat), "listening  -  %s onsets", UiFmt::Count((long long)(n->TotalCaptured())).c);
      else
         snprintf(stat, sizeof(stat), "wire notes in - always adapting");

      FitStatBesideConf(stat, sizeof(stat), conf, kAudioNarrowWidth);
      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%s conf", UiFmt::Pct((int)std::round(conf * 100.0f)).c);
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNarrowWidth - bsz.x - tok::note_predictive_quantize_body_gap, statusPos.y + tok::space_0_75),
                     tok::Role(tok::col::state_ok_dark_vivid_hi), confBadge);
      }

      {
         AudioKnobRow row(1);
         Ui::DrawKnob(row, n, PredictiveQuantizeBodyRow::Knob<&PredictiveQuantizeNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge));
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using PredictiveVelocityBodyRow = params::Row<PredictiveVelocityNode>;
   }

   void DrawPredictiveVelocityBody(GraphNode& gn, PredictiveVelocityNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      if (n->HasCurve())
         snprintf(stat, sizeof(stat), "dynamic range learned  -  %s notes total", UiFmt::Count((long long)(n->TotalCaptured())).c);
      else if (n->TotalCaptured() > 0)
         snprintf(stat, sizeof(stat), "listening  -  %s notes", UiFmt::Count((long long)(n->TotalCaptured())).c);
      else
         snprintf(stat, sizeof(stat), "wire notes in - always adapting");

      FitStatBesideConf(stat, sizeof(stat), conf, kAudioNarrowWidth);
      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%s conf", UiFmt::Pct((int)std::round(conf * 100.0f)).c);
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNarrowWidth - bsz.x - tok::note_predictive_velocity_body_gap, statusPos.y + tok::space_0_75),
                     tok::Role(tok::col::state_ok_dark_vivid_hi), confBadge);
      }

      {
         AudioKnobRow row(1);
         Ui::DrawKnob(row, n, PredictiveVelocityBodyRow::Knob<&PredictiveVelocityNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge));
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using PredictiveRhythmBodyRow = params::Row<PredictiveRhythmNode>;
   }

   void DrawPredictiveRhythmBody(GraphNode& gn, PredictiveRhythmNode* n)
   {
      const float conf = n->Confidence01();
      char stat[64];
      const std::string rootName = MidiNoteNameList()[std::clamp(n->root, 0, 127)];
      if (n->IsLearning() && n->Dropped() > 0)
         snprintf(stat, sizeof(stat), "learning  -  %s notes (%d dropped)", UiFmt::Count((long long)(n->NotesCaptured())).c, n->Dropped());
      else if (n->IsLearning())
         snprintf(stat, sizeof(stat), "learning  -  %s notes", UiFmt::Count((long long)(n->NotesCaptured())).c);
      else if (n->Building())
         snprintf(stat, sizeof(stat), "building model...");
      else if (n->LastLearnTooShort() && n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "too short, kept %s notes learned", UiFmt::Count((long long)(n->LearnedNotes())).c);
      else if (n->LastLearnTooShort())
         snprintf(stat, sizeof(stat), "too short to learn, wire notes in");
      else if (n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "%s notes learned, root %s", UiFmt::Count((long long)(n->LearnedNotes())).c, rootName.c_str());
      else
         snprintf(stat, sizeof(stat), "wire notes in, press Learn");

      FitStatBesideConf(stat, sizeof(stat), conf, kAudioNarrowWidth);
      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%s conf", UiFmt::Pct((int)std::round(conf * 100.0f)).c);
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNarrowWidth - bsz.x - tok::note_predictive_rhythm_body_gap, statusPos.y + tok::space_0_75),
                     tok::Role(tok::col::state_ok_dark_vivid_hi), confBadge);
      }

      {
         const float w = gAudioContentW;
         const float h = ImGui::GetFrameHeight();
         const bool learning = n->IsLearning();
         if (ActionButton::Draw(learning ? "Stop##prLearn" : "Learn##prLearn", ImVec2(w, h)))
         {
            PushUndoCheckpoint();
            n->SetLearning(!learning);
         }
      }
      {
         AudioKnobRow row(tok::note_predictive_rhythm_body_audio_knob_row_layout);
         row.Dropdown("root", MidiNoteNameList(), n->root, [n](int i) { n->root = i; });
         Ui::DrawKnob(row, n, PredictiveRhythmBodyRow::Knob<&PredictiveRhythmNode::mix>("mix", 0.0f, 1.0f).Fmt("%.2f").Size(kKnobLarge));
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using NoteEchoBodyRow = params::Row<NoteEchoNode>;
      const NoteEchoBodyRow kNoteEchoBodyRows[] = {
         NoteEchoBodyRow::KnobInt<&NoteEchoNode::repeats>("repeats", 1, NoteEchoNode::kMaxRepeats),
         NoteEchoBodyRow::Knob<&NoteEchoNode::decay>("decay", params::NoteEcho::decay_lo, params::NoteEcho::decay_hi).Fmt("%.0f%%"),
      };
   }

   void DrawNoteEchoBody(GraphNode& gn, NoteEchoNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%d repeats, %d pending", n->repeats, n->PendingCount());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(tok::note_echo_body_audio_knob_row_layout);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.End();
      }
      {
         AudioKnobRow row(tok::note_echo_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kNoteEchoBodyRows);
         row.End();
      }
      {
         AudioKnobRow row(tok::note_echo_body_audio_knob_row_layout);
         Ui::DrawKnob(row, n, NoteEchoBodyRow::KnobInt<&NoteEchoNode::transposePerRepeat>("transpose", -NoteEchoNode::kMaxTransposePerRepeat, NoteEchoNode::kMaxTransposePerRepeat));
         bool muteDryBool = n->muteDry;
         if (row.Checkbox("Mute Dry##echoMuteDry", &muteDryBool))
            n->muteDry = muteDryBool;
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using PredictiveNotesBodyRow = params::Row<PredictiveNotesNode>;
      const PredictiveNotesBodyRow kPredictiveNotesBodyRows1[] = {
         PredictiveNotesBodyRow::Knob<&PredictiveNotesNode::stray>("stray", 0.0f, 1.0f).Fmt("%.2f"),
         PredictiveNotesBodyRow::KnobInt<&PredictiveNotesNode::memory>("memory", params::PredictiveNotes::memory_lo, params::PredictiveNotes::memory_hi),
         PredictiveNotesBodyRow::Knob<&PredictiveNotesNode::lengthSpread>("length", 0.0f, 1.0f).Fmt("%.2f"),
         PredictiveNotesBodyRow::Knob<&PredictiveNotesNode::velocitySpread>("velocity", 0.0f, 1.0f).Fmt("%.2f"),
      };
      const PredictiveNotesBodyRow kPredictiveNotesBodyRows2[] = {
         PredictiveNotesBodyRow::KnobInt<&PredictiveNotesNode::rangeLow>("low", params::PredictiveNotes::range_low_lo, params::PredictiveNotes::range_low_hi),
         PredictiveNotesBodyRow::KnobInt<&PredictiveNotesNode::rangeHigh>("high", params::PredictiveNotes::range_high_lo, params::PredictiveNotes::range_high_hi),
      };
   }

   void DrawPredictiveNotesBody(GraphNode& gn, PredictiveNotesNode* n)
   {
      const float conf = n->Confidence01();
      char stat[128];
      if (n->IsLearning() && n->Dropped() > 0)
         snprintf(stat, sizeof(stat), "learning  -  %s notes, %d bars (%d dropped)", UiFmt::Count((long long)(n->NotesCaptured())).c, n->BarsCaptured(), n->Dropped());
      else if (n->IsLearning())
         snprintf(stat, sizeof(stat), "learning  -  %s notes, %s", UiFmt::Count((long long)(n->NotesCaptured())).c, UiFmt::Bars(n->BarsCaptured()).c);
      else if (n->Building())
         snprintf(stat, sizeof(stat), "building model...");
      else if (n->LastLearnTooShort() && n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "too short, kept %s notes learned", UiFmt::Count((long long)(n->LearnedNotes())).c);
      else if (n->LastLearnTooShort())
         snprintf(stat, sizeof(stat), "too short to learn, wire more notes in");
      else if (n->LearnedNotes() > 0)
         snprintf(stat, sizeof(stat), "%s notes learned", UiFmt::Count((long long)(n->LearnedNotes())).c);
      else
         snprintf(stat, sizeof(stat), "wire notes in, press Learn");

      FitStatBesideConf(stat, sizeof(stat), conf, kAudioNodeWidth);
      const ImVec2 statusPos = ImGui::GetCursorScreenPos();
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (conf > 0.0f && gAudioReadout.find(gn.index) == gAudioReadout.end())
      {
         char confBadge[32];
         snprintf(confBadge, sizeof(confBadge), "%s conf", UiFmt::Pct((int)std::round(conf * 100.0f)).c);
         const ImVec2 bsz = ImGui::CalcTextSize(confBadge);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddText(ImVec2(statusPos.x + kAudioNodeWidth - bsz.x - tok::note_predictive_notes_body_gap, statusPos.y + tok::space_0_75),
                     tok::Role(tok::col::state_ok_dark_vivid_hi), confBadge);
      }

      {
         const float w = gAudioContentW;
         const float h = ImGui::GetFrameHeight();
         const bool learning = n->IsLearning();
         if (ActionButton::Draw(learning ? "Stop##predLearn" : "Learn##predLearn", ImVec2(w * tok::note_predictive_notes_body_ratio - tok::space_0_5, h)))
         {
            PushUndoCheckpoint();
            n->SetLearning(!learning);
         }
         Ui::SameRow();
         // Learning meter: how much the model beats a memoryless one, per captured bar.
         const ImVec2 p0 = ImGui::GetCursorScreenPos();
         const float mw = w - (w * tok::note_predictive_notes_body_ratio - 2.0f) - ImGui::GetStyle().ItemSpacing.x;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(p0, ImVec2(p0.x + mw, p0.y + h), tok::Role(tok::col::ink_white, tok::note_fill_alpha_lg), tok::note_predictive_notes_body_radius);
         const auto& c = n->Curve();
         if (c.size() >= tok::note_predictive_notes_body_limit)
         {
            float hi = 0.1f;
            for (float v : c)
               hi = std::max(hi, v);
            for (size_t i = 1; i < c.size(); i++)
            {
               const float x0 = p0.x + mw * (float)(i - 1) / (float)(c.size() - 1);
               const float x1 = p0.x + mw * (float)i / (float)(c.size() - 1);
               const float y0 = p0.y + h - tok::space_0_75 - (h - tok::space_1_5) * std::clamp(c[i - 1] / hi, 0.0f, 1.0f);
               const float y1 = p0.y + h - tok::space_0_75 - (h - tok::space_1_5) * std::clamp(c[i] / hi, 0.0f, 1.0f);
               dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), tok::Role(tok::col::state_ok, tok::note_line_alpha), tok::note_predictive_notes_body_stroke);
            }
         }
         if (c.size() < tok::note_predictive_notes_body_limit)
            AudioViz::IdleLabel(AudioViz::Frame{dl, p0, ImVec2(p0.x + mw, p0.y + h)}, "learning meter");
         if (conf > 0.0f)
         {
            char confStr[32];
            snprintf(confStr, sizeof(confStr), "%s", UiFmt::Pct((int)std::round(conf * 100.0f)).c);
            const ImVec2 csz = ImGui::CalcTextSize(confStr);
            dl->AddText(ImVec2(p0.x + mw - csz.x - tok::space_1_5, p0.y + (h - csz.y) * 0.5f),
                        tok::Role(tok::col::state_ok_dark_vivid_hi), confStr);
         }
         Ui::Claim(ImVec2(mw, h));
      }
      Ui::Spacer(Ui::Space::S1);
      {
         AudioKnobRow row(tok::note_predictive_notes_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kPredictiveNotesBodyRows1);
         row.End();
      }
      {
         AudioKnobRow row(tok::note_predictive_notes_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kPredictiveNotesBodyRows2);
         row.End();
      }

      EndAudioBody();
   }


   void DrawNoteMergeBody(GraphNode& gn, NoteMergeNode* n)
   {
      int active = 0;
      for (int i = 0; i < NoteMergeNode::kSlots; i++)
         if (n->noteInputs[i].IsConnected())
            active++;

      char stat[64];
      if (active == 0)
         snprintf(stat, sizeof(stat), "no active inputs");
      else
         snprintf(stat, sizeof(stat), "%d of %s inputs active", active, UiFmt::Count((long long)(NoteMergeNode::kSlots)).c);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      ImGui::TextDisabled("merges all connected inputs");
      ImGui::TextDisabled("to a single output stream");

      EndAudioBody();
   }


   namespace
   {
      using NoteSwitcherBodyRow = params::Row<NoteSwitcherNode>;
   }

   void DrawNoteSwitcherBody(GraphNode& gn, NoteSwitcherNode* n)
   {
      const int active = n->ActiveSlot();
      char stat[64];
      if (n->manual)
         snprintf(stat, sizeof(stat), "manual  -  slot %d", active + 1);
      else if (n->rateMode == 0)
      {
         int div = NearestRateDivision(n->rateBeats);
         snprintf(stat, sizeof(stat), "every %s  -  slot %d", MusicTime::RateDivisionName(div), active + 1);
      }
      else
         snprintf(stat, sizeof(stat), "every %s  -  slot %d", UiFmt::Sec(n->rateSeconds).c, active + 1);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      // Four LED dots: active slot lit, connected dim, unconnected darkest
      {
         const float w = gAudioBodyW;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float dia = tok::note_dia;
         const float gap = (w - (float)NoteSwitcherNode::kSlots * dia) / (float)(NoteSwitcherNode::kSlots + 1);
         for (int i = 0; i < NoteSwitcherNode::kSlots; i++)
         {
            const float cx = origin.x + gap * (float)(i + 1) + dia * (float)i + dia * 0.5f;
            const bool isConnected = n->noteInputs[i].IsConnected();
            const bool isActive = (i == active);
            ImU32 col = tok::Base(tok::col::edge_lo); // unconnected darkest
            if (isActive)
               col = tok::Role(tok::col::accent_pale_muted_warm); // active lit
            else if (isConnected)
               col = tok::Role(tok::col::accent_muted_lo); // connected dim
            dl->AddCircleFilled(ImVec2(cx, origin.y + dia * 0.5f), dia * 0.5f, col);
         }
         Ui::Claim(ImVec2(w, dia + tok::space_1_5));
      }

      {
         // Synced mode is two dropdowns that carry their names inside the
         // face, so no caption strip is reserved under them (it left a gap
         // above the manual checkbox); Free mode has a captioned knob.
         AudioKnobRow row(tok::note_switcher_body_audio_knob_row_layout, kKnobSmall, 0.0f, n->rateMode != 0);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.End();
      }

      {
         AudioKnobRow row(tok::note_switcher_body_audio_knob_row_layout, tok::note_row_layout_lg, 0.0f, false);
         Ui::DrawKnob(row, n, NoteSwitcherBodyRow::Toggle<&NoteSwitcherNode::manual>("manual##switcherManual"));
         row.End();
      }
      if (n->manual)
      {
         Ui::DrawParam(n, NoteSwitcherBodyRow::SliderInt<&NoteSwitcherNode::manualSlot>("slot", 0, NoteSwitcherNode::kSlots - 1));
      }

      EndAudioBody();
   }


   namespace
   {
      using NoteRouterBodyRow = params::Row<NoteRouterNode>;
   }

   void DrawNoteRouterBody(GraphNode& gn, NoteRouterNode* n)
   {
      static const std::vector<std::string> kModes = { "Round Robin", "Random", "Probability", "Chain" };
      const int mask = n->LastRoutedMask();
      char stat[64];
      if (mask == 0)
         snprintf(stat, sizeof(stat), "idle");
      else
      {
         char outs[16] = "";
         int o = 0;
         for (int i = 0; i < 4 && o < 12; ++i)
            if (mask & (1 << i))
               o += snprintf(outs + o, sizeof(outs) - (size_t)o, o > 0 ? " %d" : "%d", i + 1);
         snprintf(stat, sizeof(stat), "holding out %s", outs);
      }
      (void)kModes;

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      // Four lit dots showing which outputs are currently carrying a held note.
      {
         const float w = gAudioBodyW;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float dia = tok::note_dia;
         const float gap = (w - tok::note_router_body_ratio * dia) / tok::note_gap_ratio;
         for (int o = 0; o < 4; o++)
         {
            const float cx = origin.x + gap * (float)(o + 1) + dia * (float)o + dia * 0.5f;
            const bool lit = (mask & (1 << o)) != 0;
            dl->AddCircleFilled(ImVec2(cx, origin.y + dia * 0.5f), dia * 0.5f,
                                lit ? tok::Role(tok::col::accent_pale_muted_warm) : tok::Role(tok::col::edge_lo));
         }
         Ui::Claim(ImVec2(w, dia + tok::space_1_5));
      }

      {
         const bool showProb = n->mode == NoteRouterNode::kProbability;
         AudioKnobRow row(showProb ? tok::note_router_body_audio_knob_row_layout : 1);
         row.Dropdown("mode", kModes, n->mode, [n](int i) { PushUndoCheckpoint(); n->mode = i; });
         if (showProb)
            Ui::DrawKnob(row, n, NoteRouterBodyRow::Knob<&NoteRouterNode::probability>("prob", params::NoteRouter::probability_lo, params::NoteRouter::probability_hi).Fmt("%.0f%%"));
         row.End();
      }

      EndAudioBody();
   }


   // The 8-step gate grid: both the visualizer and the primary editable
   // control (replaces the old per-mode decorative shape, which couldn't
   // scale to 15 modes and wasn't interactive). Click/drag-paint toggles a
   // step's gate; the currently-sounding step lights up as the playhead.
   void DrawArpGateGrid(ArpeggiatorNode* n)
   {
      const int steps = ArpeggiatorNode::kGateSteps;
      const float gap = tok::space_0_75;
      const float w = gAudioBodyW;
      const float cellW = (w - gap * (float)(steps - 1)) / (float)steps;
      const float h = std::min(cellW, tok::note_arp_gate_grid_max);
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
         UiInteract::Hit("gatecell", ImVec2(cellW, h));

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

         StepCell::Draw(dl, cellMin, cellMax, on, isPlayhead, ImGui::IsItemHovered());

         ImGui::PopID();
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         gArpGateDrag.active = false;

      ImGui::SetCursorScreenPos(origin);
      Ui::Claim(ImVec2(w, h));
   }


   namespace
   {
      using ArpeggiatorBodyRow = params::Row<ArpeggiatorNode>;
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
      Ui::Spacer(Ui::Space::S1);

      {
         AudioKnobRow row(tok::note_arpeggiator_body_audio_knob_row_layout);
         row.Dropdown("preset", ArpeggiatorNode::PresetNames(), n->mLastPresetUiIndex,
                      [n](int i) { PushUndoCheckpoint(); n->mLastPresetUiIndex = i; n->ApplyPreset(i); });
         row.Dropdown("mode", kModeLabels, modeUiIndex,
                      [n](int i) { PushUndoCheckpoint(); n->mode = kModeValues[std::clamp(i, 0, (int)kModeLabels.size() - 1)]; });
         Ui::DrawKnob(row, n, ArpeggiatorBodyRow::KnobInt<&ArpeggiatorNode::octaves>("octaves", params::Arpeggiator::octaves_lo, params::Arpeggiator::octaves_hi));
         row.End();
      }
      {
         AudioKnobRow row(tok::note_arpeggiator_body_audio_knob_row_layout);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         Ui::DrawKnob(row, n, ArpeggiatorBodyRow::Knob<&ArpeggiatorNode::gatePercent>("gate", params::Arpeggiator::gate_percent_lo, params::Arpeggiator::gate_percent_hi).Fmt("%.0f%%"));
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using NoteSequencerBodyRow = params::Row<NoteSequencerNode>;
      const NoteSequencerBodyRow kNoteSequencerBodyRows[] = {
         NoteSequencerBodyRow::KnobInt<&NoteSequencerNode::steps>("steps", 1, NoteSequencerNode::kMaxSteps),
         NoteSequencerBodyRow::Knob<&NoteSequencerNode::gatePercent>("gate", params::NoteSequencer::gate_percent_lo, params::NoteSequencer::gate_percent_hi).Fmt("%.0f%%"),
      };
   }

   void DrawNoteSequencerBody(GraphNode& gn, NoteSequencerNode* n)
   {
      // No playhead while the transport is stopped: it would read as a position that is not moving.
      const int cur = Transport::Instance().IsPlaying() ? n->CurrentStep() : -1;
      const int steps = std::clamp(n->steps, 1, NoteSequencerNode::kMaxSteps);
      char stat[80];
      if (cur >= 0 && cur < steps)
      {
         int playNote = n->stepNote[cur];
         if (n->useGlobalScale)
            playNote = MusicTime::SnapToScale(playNote, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
         playNote = std::clamp(playNote, 0, 127);
         snprintf(stat, sizeof(stat), "step %d/%d \xC2\xB7 %s%d (%s)%s",
                  cur + 1, steps,
                  NoteNameList()[playNote % 12].c_str(), playNote / 12 - 1,
                  UiFmt::Semi(playNote - 60).c,
                  n->stepEnabled[cur] ? "" : " [muted]");
      }
      else
      {
         snprintf(stat, sizeof(stat), "%s steps, looped", UiFmt::Count((long long)(steps)).c);
      }

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      int hoverStep = -1;
      {
         const float w = gAudioBodyW;
         const float pitchH = tok::note_pitch_h;
         const float velH = tok::note_vel_h;
         const float totalGridH = pitchH + velH;
         const float gap = tok::space_0_5;
         const float barW = (w - gap * (float)(steps - 1)) / (float)steps;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + w, origin.y + totalGridH);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         ImFont* font = ImGui::GetFont();
         const bool isLight = IsThemeLight();

         const int kLow = tok::note_low, kHigh = tok::note_high; // C2..C6 (-24..+24 st, 49 notes)
         const int numNotes = kHigh - kLow + 1;
         const float noteH = pitchH / (float)numNotes;

         auto DrawTextCentered = [&](const ImVec2& minP, const ImVec2& maxP, ImU32 col, const char* text, float fSz) {
            const ImVec2 sz = font->CalcTextSizeA(fSz, FLT_MAX, 0.0f, text);
            const float tx = minP.x + std::max(0.0f, (maxP.x - minP.x - sz.x) * 0.5f);
            const float ty = minP.y + std::max(0.0f, (maxP.y - minP.y - sz.y) * 0.5f);
            dl->AddText(font, fSz, ImVec2(tx, ty), col, text);
         };

         // ---- 1) Container chassis ----
         dl->AddRectFilled(origin, br, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), tok::radius_control);

         // ---- 2) Octave guide lines across pitch area ----
         const int octaveNotes[] = { 48, 60, 72 }; // C3, C4, C5
         for (int octNote : octaveNotes)
         {
            const float lineY = origin.y + (float)(kHigh - octNote) * noteH;
            const bool isMiddleC = (octNote == 60);
            const ImU32 lineCol = isMiddleC
               ? (isLight ? tok::Base(tok::col::edge_mid_light_fixed, tok::note_sequencer_body_edge_mid_light_fixed_alpha) : tok::Base(tok::col::edge_lo_light, tok::note_sequencer_body_edge_lo_light_alpha))
               : (isLight ? tok::Base(tok::col::surface_hi, tok::note_sequencer_body_surface_hi_alpha) : tok::Base(tok::col::surface_lo, tok::note_sequencer_body_surface_lo_alpha));
            dl->AddLine(ImVec2(origin.x + tok::space_0_5, lineY), ImVec2(br.x - tok::space_0_5, lineY), lineCol, isMiddleC ? tok::note_sequencer_body_stroke : tok::note_line_stroke);
         }

         // Divider line between pitch matrix and velocity lane
         dl->AddLine(ImVec2(origin.x, origin.y + pitchH), ImVec2(br.x, origin.y + pitchH),
                     isLight ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::edge_lo), tok::note_line_stroke);

         // ---- 3) Step Columns ----
         for (int i = 0; i < steps; i++)
         {
            const float x0 = origin.x + (float)i * (barW + gap);
            const float x1 = x0 + barW;
            const bool isCurrent = (i == cur);
            const bool isGroupStart = (i % 4) == 0;
            ImGui::PushID(i);

            // Column track background
            const ImU32 trackCol = isLight
               ? (isGroupStart ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::edge_hi))
               : (isGroupStart ? tok::Role(tok::col::surface_lo) : tok::Role(tok::col::edge_deep));
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + pitchH), trackCol, tok::radius_cell);

            // ---- Pitch interactive area ----
            ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
            UiInteract::Hit("pitch", ImVec2(barW, pitchH));
            const bool pitchHovered = ImGui::IsItemHovered();
            const bool pitchActive = ImGui::IsItemActive();
            if (pitchHovered)
               hoverStep = i;

            // Double click or right click to toggle gate / mute
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint();
               n->stepEnabled[i] = !n->stepEnabled[i];
            }
            else if (MenuParts::ContextClicked())
            {
               PushUndoCheckpoint();
               n->stepEnabled[i] = !n->stepEnabled[i];
            }
            else if (pitchActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsItemClicked(ImGuiMouseButton_Left)))
            {
               const float my = ImGui::GetIO().MousePos.y;
               const float t = 1.0f - std::clamp((my - origin.y) / pitchH, 0.0f, 1.0f);
               n->stepNote[i] = std::clamp(kLow + (int)std::floor(t * (float)numNotes), kLow, kHigh);
            }

            // Calculate display note
            const int rawNote = n->stepNote[i];
            int dispNote = rawNote;
            if (n->useGlobalScale)
               dispNote = MusicTime::SnapToScale(rawNote, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
            dispNote = std::clamp(dispNote, kLow, kHigh);
            const int dispPc = ((dispNote % 12) + 12) % 12;
            const int dispOct = dispNote / 12 - 1;

            const float noteTopY = origin.y + (float)(kHigh - dispNote) * noteH;
            const float capH = std::max(tok::note_cap_h_min, noteH + tok::note_sequencer_body_gap);
            const float capTop = std::clamp(noteTopY - (capH - noteH) * 0.5f, origin.y, origin.y + pitchH - capH);
            const float capBot = capTop + capH;

            // Hover highlight (the playhead is the outline + pill + number, not a wash)
            if (pitchHovered)
            {
               dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + pitchH),
                                 isLight ? tok::Base(tok::col::ink_black, tok::note_fill_alpha_sm) : tok::Base(tok::col::ink_white, tok::note_fill_alpha_md), tok::radius_cell);
            }

            // Pitch bar stem (from cap to bottom of pitch area)
            if (capBot < origin.y + pitchH)
            {
               const ImU32 stemCol = n->stepEnabled[i]
                  ? (isLight ? tok::Base(tok::col::accent, tok::note_sequencer_body_accent_alpha) : tok::Base(tok::col::accent, tok::note_stem_col_alpha_lg))
                  : (isLight ? tok::Base(tok::col::surface_hi, tok::note_stem_col_alpha_sm) : tok::Base(tok::col::edge_lo, tok::note_sequencer_body_edge_lo_alpha));
               dl->AddRectFilled(ImVec2(x0 + 1.0f, capBot), ImVec2(x1 - 1.0f, origin.y + pitchH),
                                 stemCol, tok::radius_cell);
            }

            // Note pill cap
            const float fontCapSz = steps <= 8 ? 10.5f : (steps <= 12 ? 9.5f : 8.5f);
            if (n->stepEnabled[i])
            {
               const ImU32 capCol = isCurrent
                  ? (isLight ? tok::Base(tok::col::text_warn) : tok::Base(tok::col::role_modulation))
                  : (pitchActive
                        ? (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_warm))
                        : (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_xlo)));
               const ImU32 capBorder = isCurrent
                  ? (isLight ? tok::Base(tok::col::role_solo) : tok::Base(tok::col::role_solo_alt))
                  : (isLight ? tok::Base(tok::col::accent_alt, tok::note_sequencer_body_accent_alt_alpha) : tok::Base(tok::col::accent_alt, tok::note_cap_border_alpha));

               dl->AddRectFilled(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), capCol, tok::note_sequencer_body_radius);
               dl->AddRect(ImVec2(x0 + tok::space_0_25, capTop), ImVec2(x1 - tok::space_0_25, capBot), capBorder, tok::note_sequencer_body_radius);

               char noteStr[16];
               snprintf(noteStr, sizeof(noteStr), "%s%d", NoteNameList()[dispPc].c_str(), dispOct);
               const ImU32 txtCol = isCurrent
                  ? tok::Base(tok::col::surface_deep)
                  : (isLight ? tok::Base(tok::col::ink_white) : tok::Base(tok::col::surface_deep));
               DrawTextCentered(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), txtCol, noteStr, fontCapSz);
            }
            else
            {
               const ImU32 capCol = isLight ? tok::Base(tok::col::surface_hi, tok::note_cap_col_else_alpha) : tok::Base(tok::col::edge_lo, tok::note_cap_col_else_alpha);
               const ImU32 capBorder = isLight ? tok::Base(tok::col::edge_mid_light_fixed, tok::note_cap_border_else_alpha) : tok::Base(tok::col::edge_lo_light, tok::note_cap_border_else_alpha);
               dl->AddRectFilled(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), capCol, tok::note_sequencer_body_radius);
               dl->AddRect(ImVec2(x0 + tok::space_0_25, capTop), ImVec2(x1 - tok::space_0_25, capBot), capBorder, tok::note_sequencer_body_radius);

               char noteStr[16];
               snprintf(noteStr, sizeof(noteStr), "%s%d", NoteNameList()[dispPc].c_str(), dispOct);
               const ImU32 txtCol = isLight ? tok::Base(tok::col::edge_mid, tok::note_sequencer_body_edge_mid_alpha) : tok::Base(tok::col::edge_lo_pale, tok::note_sequencer_body_edge_lo_pale_alpha);
               DrawTextCentered(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), txtCol, noteStr, fontCapSz);
            }

            // ---- Velocity lane ----
            const float vy0 = origin.y + pitchH;
            const float vy1 = vy0 + velH;
            ImGui::SetCursorScreenPos(ImVec2(x0, vy0));
            UiInteract::Hit("vel", ImVec2(barW, velH));
            const bool velHovered = ImGui::IsItemHovered();
            const bool velActive = ImGui::IsItemActive();
            if (velHovered)
               hoverStep = i;

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint();
               n->stepEnabled[i] = !n->stepEnabled[i];
            }
            else if (MenuParts::ContextClicked())
            {
               PushUndoCheckpoint();
               n->stepEnabled[i] = !n->stepEnabled[i];
            }
            else if (velActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsItemClicked(ImGuiMouseButton_Left)))
            {
               const float my = ImGui::GetIO().MousePos.y;
               const float vt = 1.0f - std::clamp((my - vy0) / velH, 0.0f, 1.0f);
               n->stepVelocity[i] = std::clamp(vt, 0.0f, 1.0f);
               n->stepEnabled[i] = true;
            }

            // Velocity track background
            const ImU32 velTrackCol = isLight ? tok::Base(tok::col::surface_hi) : tok::Base(tok::col::surface_deep);
            dl->AddRectFilled(ImVec2(x0 + 1.0f, vy0 + 1.0f), ImVec2(x1 - 1.0f, vy1 - 1.0f), velTrackCol, tok::radius_cell);

            const float fontVelSz = steps <= 8 ? 9.5f : 8.5f;
            if (n->stepEnabled[i])
            {
               const float vFillTop = vy1 - n->stepVelocity[i] * (velH - tok::space_0_5) - 1.0f;
               const ImU32 vCol = isCurrent
                  ? (isLight ? tok::Base(tok::col::text_warn) : tok::Base(tok::col::role_modulation))
                  : (isLight ? tok::Base(tok::col::accent) : tok::Base(tok::col::accent_pale_muted_xlo));
               dl->AddRectFilled(ImVec2(x0 + 1.0f, vFillTop), ImVec2(x1 - 1.0f, vy1 - 1.0f), vCol, tok::radius_cell);

               char vStr[16];
               snprintf(vStr, sizeof(vStr), "%s", UiFmt::F0(n->stepVelocity[i] * 100.0f).c);
               const ImU32 txtVCol = (isLight && !isCurrent) ? tok::Base(tok::col::ink_white) : tok::Base(tok::col::surface_deep);
               DrawTextCentered(ImVec2(x0 + 1.0f, vy0), ImVec2(x1 - 1.0f, vy1), txtVCol, vStr, fontVelSz);
            }
            else
            {
               DrawTextCentered(ImVec2(x0 + 1.0f, vy0), ImVec2(x1 - 1.0f, vy1),
                                isLight ? tok::Base(tok::col::edge_mid, tok::note_text_centered_alpha) : tok::Base(tok::col::surface_lo_pale_fixed, tok::note_text_centered_alpha), "OFF", fontVelSz);
            }

            // Playhead column outline
            if (isCurrent)
            {
               StepCell::PlayheadOutline(dl, ImVec2(x0, origin.y), ImVec2(x1, vy1));
            }

            // ---- Step number label below each column (Pattern-style) ----
            char label[16];
            snprintf(label, sizeof(label), "%d", i + 1);
            // Cap at the ambient UI font size and only shrink below it once a
            // column gets too narrow to fit a number at that size - the old
            // `steps > 8 ? barW * 0.95f` branch scaled font size UP with
            // column width instead of down, so going from 8 to 9+ steps (still
            // ~45px-wide columns at this node width) jumped the step numbers
            // to ~3x the normal text size instead of shrinking them.
            const float labelFontSize = std::min(ImGui::GetFontSize(), std::max(tok::type_min, barW * tok::type_fit_frac));
            const ImVec2 textSize = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
            const ImU32 stepNumCol = isCurrent
               ? (isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation))
               : (isLight
                     ? (isGroupStart ? tok::Base(tok::col::edge_lo) : tok::Base(tok::col::edge_mid))
                     : (isGroupStart ? tok::Role(tok::col::surface_mid) : tok::Role(tok::col::edge_mid)));
            dl->AddText(font, labelFontSize, ImVec2(x0 + (barW - textSize.x) * 0.5f, br.y + tok::space_0_75),
                        stepNumCol, label);

            ImGui::PopID();
         }

         // Grid outer border
         AudioViz::Border(dl, origin, br);

         const float labelRowH = ImGui::GetFontSize() + tok::note_label_row_h_gap;
         ImGui::SetCursorScreenPos(ImVec2(origin.x, br.y + labelRowH));
         if (hoverStep >= 0)
         {
            const int rawNote = n->stepNote[hoverStep];
            int dispNote = rawNote;
            if (n->useGlobalScale)
               dispNote = MusicTime::SnapToScale(rawNote, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
            dispNote = std::clamp(dispNote, kLow, kHigh);
            const int dispPc = ((dispNote % 12) + 12) % 12;
            const int dispOct = dispNote / 12 - 1;
            const int stFromC4 = dispNote - 60;
            if (n->stepEnabled[hoverStep])
               ImGui::TextDisabled("step %d: %s%d (%s) \xC2\xB7 vel %s (dbl-click to mute)",
                                   hoverStep + 1, NoteNameList()[dispPc].c_str(), dispOct, UiFmt::Semi(stFromC4).c,
                                   UiFmt::PctOf(n->stepVelocity[hoverStep]).c);
            else
               ImGui::TextDisabled("step %d: %s%d (%s) [MUTED] (dbl-click to unmute)",
                                   hoverStep + 1, NoteNameList()[dispPc].c_str(), dispOct, UiFmt::Semi(stFromC4).c);
         }
         else
         {
            Ui::Claim(ImVec2(0.0f, ImGui::GetTextLineHeight())); // hover line keeps its row; the idle text lives in the status line
         }
      }

      {
         Ui::Spacer(Ui::Space::S1);
         AudioKnobRow row(tok::note_sequencer_body_audio_knob_row_layout);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         Ui::DrawKnobs(row, n, kNoteSequencerBodyRows);
         row.End();
      }

      EndAudioBody();
   }


   // The Piano Roll node's card: the linked page's notes as a mini roll over
   // the lane's span, the transport playhead, and the node's four controls.
   namespace
   {
      using PianoRollBodyRow = params::Row<PianoRollNode>;
      const PianoRollBodyRow kPianoRollBodyRows[] = {
         PianoRollBodyRow::KnobInt<&PianoRollNode::transpose>("transpose", params::PianoRoll::transpose_lo, params::PianoRoll::transpose_hi),
         PianoRollBodyRow::Knob<&PianoRollNode::velocity>("vel", params::PianoRoll::velocity_lo, params::PianoRoll::velocity_hi).Fmt("%.2f"),
      };
   }

   void DrawPianoRollBody(GraphNode& gn, PianoRollNode* n)
   {
      const Arrange::NoteSchedule* s = n->DisplaySchedule();
      char stat[96];
      if (n->laneId == 0)
         snprintf(stat, sizeof(stat), "no page linked");
      else
         snprintf(stat, sizeof(stat), "%s - %s notes", n->laneName.c_str(), UiFmt::Count((long long)(s ? (int)s->notes.size() : 0)).c);
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      {
         const float w = gAudioBodyW;
         const float h = tok::note_piano_roll_h;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + w, origin.y + h);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool isLight = IsThemeLight();
         dl->AddRectFilled(origin, br, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), tok::radius_control);
         if (s && !s->notes.empty())
         {
            // Span: whole bars from the first note to the last off.
            double t0 = s->notes.front().on, t1 = t0;
            int lo = 127, hi = 0;
            for (const Arrange::SchedNote& e : s->notes)
            {
               t1 = std::max(t1, e.off);
               lo = std::min(lo, (int)e.pitch);
               hi = std::max(hi, (int)e.pitch);
            }
            t0 = std::floor(t0 / 4.0) * 4.0;
            t1 = std::max(t0 + 4.0, std::ceil(t1 / 4.0) * 4.0);
            lo = std::max(0, lo - 1);
            hi = std::min(127, hi + 1);
            const double len = t1 - t0;
            const float noteH = std::max(tok::note_h_min, (h - tok::space_1_5) / (float)(hi - lo + 1));
            const ImU32 on = IM_COL32((int)(gAudioTint.r * tok::note_piano_roll_body_ratio), (int)(gAudioTint.g * 255.0f),
                                      (int)(gAudioTint.b * 255.0f), tok::note_on_alpha);
            dl->PushClipRect(origin, br, true);
            // A dense page draws at most ~2 notes per pixel column's worth.
            const size_t step = std::max<size_t>(1, s->notes.size() / 4096);  // ui-audit-ok: bytes (byte / buffer arithmetic, not a screen quantity)
            for (size_t i = 0; i < s->notes.size(); i += step)
            {
               const Arrange::SchedNote& e = s->notes[i];
               const float x0 = origin.x + (float)((e.on - t0) / len) * w;
               const float x1 = std::max(x0 + 1.0f, origin.x + (float)((e.off - t0) / len) * w);
               const float y = br.y - tok::space_0_75 - (float)(e.pitch - lo + 1) * noteH;
               dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + noteH - tok::px_align), on);
            }
            const double ph = n->PlayheadBeats();
            if (ph >= t0 && ph <= t1)
            {
               const float x = origin.x + (float)((ph - t0) / len) * w;
               dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), isLight ? tok::Base(tok::col::edge_lo, tok::note_line_alpha) : tok::Base(tok::col::edge_hi, tok::note_line_alpha), tok::note_piano_roll_body_stroke);
            }
            dl->PopClipRect();
         }
         else
            AudioViz::IdleLabel(AudioViz::Frame{dl, origin, br}, n->laneId == 0 ? "link a Piano Roll page" : "empty page");
         Ui::Claim(ImVec2(w, h));
         // Double-click the preview: the panel, on this node's page.
         if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            PianoRollOpenPage(n->laneId);
      }

      Ui::Spacer(Ui::Space::S1); // the skeleton's gap under the visualizer

      {
         AudioKnobRow row(tok::note_piano_roll_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kPianoRollBodyRows);
         row.Skip();
         row.Skip();
         row.End();
      }
      {
         AudioKnobRow row(tok::note_piano_roll_body_audio_knob_row_layout, tok::note_row_layout_lg, 0.0f, false);
         Ui::DrawKnob(row, n, PianoRollBodyRow::Toggle<&PianoRollNode::thru>("thru"));
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }
      if (ChipButton::Draw("Open##pianoOpen", false, ChipButton::Height(), gAudioBodyW))
         PianoRollOpenPage(n->laneId);

      EndAudioBody();
   }


   namespace
   {
      using MidiFileBodyRow = params::Row<MidiFileNode>;
      const MidiFileBodyRow kMidiFileBodyRows[] = {
         MidiFileBodyRow::KnobInt<&MidiFileNode::transpose>("transpose", params::MidiFile::transpose_lo, params::MidiFile::transpose_hi),
         MidiFileBodyRow::Knob<&MidiFileNode::position>("position", 0.0f, 1.0f).Fmt("%.2f"),
         MidiFileBodyRow::Knob<&MidiFileNode::speed>("speed", params::MidiFile::speed_lo, params::MidiFile::speed_hi).Fmt("%.2fx"),
         MidiFileBodyRow::Knob<&MidiFileNode::velocity>("vel", params::MidiFile::velocity_lo, params::MidiFile::velocity_hi).Fmt("%.2f"),
      };
   }

   void DrawMidiFileBody(GraphNode& gn, MidiFileNode* n)
   {
      const MidiFile::Song* song = n->GetSong();
      char stat[96];
      if (!n->Status().empty())
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      else if (song)
         snprintf(stat, sizeof(stat), "%s notes - %d track%s - %s", UiFmt::Count((long long)(song->noteCount)).c, song->trackCount,
                  song->trackCount == 1 ? "" : "s", UiFmt::Beats(n->LoopBeats()).c);
      else
         snprintf(stat, sizeof(stat), "no file");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ActionButton::Draw("Load...", ImVec2(tok::note_load_x, 0)))
      {
         const std::string path = Platform::OpenMidiDialog();
         if (!path.empty())
         {
            PushUndoCheckpoint();
            n->path = path;
            gPatchDirty = true;
         }
      }
      if (!n->path.empty())
      {
         Ui::SameRow();
         ImGui::TextDisabled("%s", n->path.substr(n->path.find_last_of("/\\") == std::string::npos ? 0 : n->path.find_last_of("/\\") + 1).c_str());
      }

      {
         const float w = gAudioBodyW;
         const float h = tok::note_piano_roll_h;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + w, origin.y + h);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool isLight = IsThemeLight();
         dl->AddRectFilled(origin, br, isLight ? tok::Base(tok::col::edge_hi) : tok::Base(tok::col::surface_deep), tok::radius_control);
         if (song && !song->events.empty())
         {
            const double len = n->LoopBeats();
            const int lo = std::max(0, (int)song->lowNote - 1);
            const int hi = std::min(127, (int)song->highNote + 1);
            const float noteH = std::max(tok::note_h_min, (h - tok::space_1_5) / (float)(hi - lo + 1));
            const ImU32 on = IM_COL32((int)(gAudioTint.r * tok::note_midi_file_body_ratio), (int)(gAudioTint.g * 255.0f),
                                      (int)(gAudioTint.b * 255.0f), tok::note_on_alpha);
            // Pair each on with the next off of the same note and track.
            std::vector<std::pair<uint8_t, double>> open[128];
            dl->PushClipRect(origin, br, true);
            for (const MidiFile::Event& e : song->events)
            {
               const int key = e.note;
               if (e.on)
                  open[key].push_back({ e.track, e.beat });
               else
                  for (size_t i = 0; i < open[key].size(); i++)
                     if (open[key][i].first == e.track)
                     {
                                                const float x0 = origin.x + (float)(open[key][i].second / len) * w;
                        const float x1 = std::max(x0 + 1.0f, origin.x + (float)((e.beat) / len) * w);
                        const float y = br.y - tok::space_0_75 - (float)(key - lo + 1) * noteH;
                        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + noteH - tok::px_align), on);
                        open[key].erase(open[key].begin() + (long)i);
                        break;
                     }
            }
            const double ph = n->PlayheadBeats();
            if (ph >= 0.0)
            {
               const float x = origin.x + (float)(std::min(ph, len) / len) * w;
               dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), isLight ? tok::Base(tok::col::edge_lo, tok::note_line_alpha) : tok::Base(tok::col::edge_hi, tok::note_line_alpha), tok::note_midi_file_body_stroke);
            }
            dl->PopClipRect();
         }
         else
            AudioViz::IdleLabel(AudioViz::Frame{dl, origin, br}, "drop a .mid file");
         Ui::Claim(ImVec2(w, h));
      }

      Ui::Spacer(Ui::Space::S1); // the skeleton's gap under the visualizer

      {
         // Four knobs a row (five cells put each mod pin against its neighbour's knob), the checkbox bottom-left
         // under them - the Keyboard's shape (P3, P6).
         AudioKnobRow row(tok::note_midi_file_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kMidiFileBodyRows);
         row.End();
      }
      {
         AudioKnobRow row(tok::note_midi_file_body_audio_knob_row_layout, tok::note_row_layout_lg, 0.0f, false);
         Ui::DrawKnob(row, n, MidiFileBodyRow::Toggle<&MidiFileNode::loop>("loop"));
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using RandomNoteGeneratorBodyRow = params::Row<RandomNoteGeneratorNode>;
      const RandomNoteGeneratorBodyRow kRandomNoteGeneratorBodyRows1[] = {
         RandomNoteGeneratorBodyRow::KnobInt<&RandomNoteGeneratorNode::rangeLow>("lo", params::RandomNoteGenerator::range_low_lo, params::RandomNoteGenerator::range_low_hi),
         RandomNoteGeneratorBodyRow::KnobInt<&RandomNoteGeneratorNode::rangeHigh>("hi", params::RandomNoteGenerator::range_high_lo, params::RandomNoteGenerator::range_high_hi),
      };
      const RandomNoteGeneratorBodyRow kRandomNoteGeneratorBodyRows2[] = {
         RandomNoteGeneratorBodyRow::KnobInt<&RandomNoteGeneratorNode::maxStep>("wander", params::RandomNoteGenerator::max_step_lo, params::RandomNoteGenerator::max_step_hi),
         RandomNoteGeneratorBodyRow::Knob<&RandomNoteGeneratorNode::groove>("groove", 0.0f, 1.0f).Fmt("%.2f"),
      };
   }

   void DrawRandomNoteGeneratorBody(GraphNode& gn, RandomNoteGeneratorNode* n)
   {
      char stat[64];
      const int last = n->LastNote();
      if (last < 0)
      {
         if (n->groove > 0.001f)
            snprintf(stat, sizeof(stat), "%s in %s..%s - %s groove", MusicTime::ScaleTable(n->scale).name,
                     NoteNameList()[n->rangeLow % 12].c_str(), NoteNameList()[n->rangeHigh % 12].c_str(),
                     UiFmt::PctOf(n->groove).c);
         else
            snprintf(stat, sizeof(stat), "%s in %s..%s", MusicTime::ScaleTable(n->scale).name,
                     NoteNameList()[n->rangeLow % 12].c_str(), NoteNameList()[n->rangeHigh % 12].c_str());
      }
      else
      {
         if (n->groove > 0.001f)
            snprintf(stat, sizeof(stat), "last: %s%d - %s groove", NoteNameList()[last % 12].c_str(),
                     last / 12 - 1, UiFmt::PctOf(n->groove).c);
         else
            snprintf(stat, sizeof(stat), "last: %s%d", NoteNameList()[last % 12].c_str(), last / 12 - 1);
      }

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Row 1 (4): selector column left (scale, root), knobs right (lo, hi).
      {
         AudioKnobRow row(tok::note_random_generator_body_audio_knob_row_layout);
         if (n->useGlobalScale)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), n->scale,
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         row.Dropdown("root", NoteNameList(), n->root,
                      [n](int i) { PushUndoCheckpoint(); n->root = i; });
         if (n->useGlobalScale)
            ImGui::EndDisabled();
         Ui::DrawKnobs(row, n, kRandomNoteGeneratorBodyRows1);
         row.End();
      }
      // Row 2 (4): same grid - mode/rate selector column left, knobs right.
      // DrawRateModeControls emits the mode dropdown + rate cell as two cells
      // with stable ordinals (see its header comment), so cell indices never
      // move when rate mode flips between Synced and Free.
      {
         AudioKnobRow row(tok::note_random_generator_body_audio_knob_row_layout);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         Ui::DrawKnobs(row, n, kRandomNoteGeneratorBodyRows2);
         row.End();
      }

      EndAudioBody();
   }


   // Eight chord cells (roman numeral in the current key, "-" = hold the chord before) over eight
   // thin chord-type cells. Click = next value, right-click = previous; either switches the node to
   // progression mode. Cells past `steps` are dimmed; the playing cell is lit.
   void DrawChorderProgressionStrip(ChorderNode* n)
   {
      constexpr int kCells = ChorderNode::kProgCells;
      const bool useGlobal = n->useGlobalScale;
      const int scale = useGlobal ? Transport::Instance().Scale() : n->scale;
      const int degreeCount = std::clamp(MusicTime::ScaleTable(scale).count, 1, 12);
      const bool progression = n->mode == ChorderNode::kProgression;
      const int playing = progression ? n->CurrentProgCell() : -1;

      const float gap = tok::space_0_5;
      const float w = gAudioContentW;
      const float cellW = (w - gap * (float)(kCells - 1)) / (float)kCells;
      const float chordH = tok::note_chord_h;
      const float typeH = tok::note_type_h;
      const ImVec2 origin(gAudioContentX, ImGui::GetCursorScreenPos().y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const bool light = IsThemeLight();
      const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
      const ImU32 dimCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const ImU32 litText = tok::Role(tok::col::surface_deep);
      static const char* const kTypeLabels[NoteTheory::kNumChordTypes] = { "\xC2\xB7", "7", "sus2", "sus4" };

      UiType::Push(UiType::Size::Caption);
      for (int c = 0; c < kCells; c++)
      {
         const float x0 = origin.x + (float)c * (cellW + gap);
         const bool used = c < n->progSteps;
         ImGui::PushID(c);

         // chord cell
         const ImVec2 cMin(x0, origin.y), cMax(x0 + cellW, origin.y + chordH);
         ImGui::SetCursorScreenPos(cMin);
         UiInteract::Hit("chordcell", ImVec2(cellW, chordH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool chordHovered = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || MenuParts::ContextClicked())
         {
            // values: 0 .. degreeCount-1, then -1 (hold)
            const int span = degreeCount + 1;
            const int idx = n->progDegree[c] < 0 ? degreeCount : std::min(n->progDegree[c], degreeCount - 1);
            const int next = (idx + (ImGui::IsItemClicked(ImGuiMouseButton_Left) ? 1 : span - 1)) % span;
            PushUndoCheckpoint();
            n->progDegree[c] = next == degreeCount ? -1 : next;
            n->mode = ChorderNode::kProgression;
         }
         if (chordHovered)
            gNodeHoverTip = "Chord: click for the next, right-click for the previous; - holds the chord before";
         const bool lit = playing == c;
         StepCell::Draw(dl, cMin, cMax, lit, lit, chordHovered);
         char label[16];
         if (n->progDegree[c] < 0)
            snprintf(label, sizeof(label), "-");
         else
            NoteTheory::RomanNumeral(n->progDegree[c], scale, label, sizeof(label));
         const ImVec2 ts = ImGui::CalcTextSize(label);
         ImU32 col = lit ? litText : (n->progDegree[c] < 0 ? dimCol : textCol);
         if (!used || !progression)
            col = (col & ~IM_COL32_A_MASK) | ((ImU32)((float)((col >> IM_COL32_A_SHIFT) & 0xFF) * (used ? tok::note_chorder_progression_strip_lg : tok::note_chorder_progression_strip_sm)) << IM_COL32_A_SHIFT);
         dl->AddText(ImVec2(x0 + (cellW - ts.x) * 0.5f, origin.y + (chordH - ts.y) * 0.5f), col, label);

         // type cell
         const float ty = origin.y + chordH + gap;
         const ImVec2 tMin(x0, ty), tMax(x0 + cellW, ty + typeH);
         ImGui::SetCursorScreenPos(tMin);
         UiInteract::Hit("typecell", ImVec2(cellW, typeH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
         const bool typeHovered = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || MenuParts::ContextClicked())
         {
            const int span = NoteTheory::kNumChordTypes;
            PushUndoCheckpoint();
            n->progType[c] = (n->progType[c] + (ImGui::IsItemClicked(ImGuiMouseButton_Left) ? 1 : span - 1)) % span;
            n->mode = ChorderNode::kProgression;
         }
         if (typeHovered)
            gNodeHoverTip = "Chord type: \xC2\xB7 = chord knob, 7th, sus2, sus4 (click / right-click)";
         StepCell::Draw(dl, tMin, tMax, false, false, typeHovered);
         const char* tl = kTypeLabels[std::clamp(n->progType[c], 0, NoteTheory::kNumChordTypes - 1)];
         const ImVec2 tts = ImGui::CalcTextSize(tl);
         ImU32 tcol = n->progType[c] == 0 ? dimCol : textCol;
         if (!used || !progression)
            tcol = (tcol & ~IM_COL32_A_MASK) | ((ImU32)((float)((tcol >> IM_COL32_A_SHIFT) & 0xFF) * (used ? 0.6f : 0.3f)) << IM_COL32_A_SHIFT);
         dl->AddText(ImVec2(x0 + (cellW - tts.x) * 0.5f, ty + (typeH - tts.y) * 0.5f), tcol, tl);

         ImGui::PopID();
      }
      UiType::Pop();
      (void)light;
      ImGui::SetCursorScreenPos(origin);
      Ui::Claim(ImVec2(w, chordH + gap + typeH + tok::space_1));
   }


   namespace
   {
      using ChorderBodyRow = params::Row<ChorderNode>;
      const ChorderBodyRow kChorderBodyRows[] = {
         ChorderBodyRow::Knob<&ChorderNode::humanizeTimingMs>("hu.time", params::Chorder::humanize_timing_ms_lo, params::Chorder::humanize_timing_ms_hi).Fmt("%.0fms"),
         ChorderBodyRow::Knob<&ChorderNode::humanizeVelocity>("hu.vel", params::Chorder::humanize_velocity_lo, params::Chorder::humanize_velocity_hi).Fmt("%.0f%%"),
      };
   }

   void DrawChorderBody(GraphNode& gn, ChorderNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "%s %s, %d-note chord", NoteNameList()[n->root].c_str(),
               MusicTime::ScaleTable(n->scale).name, n->LastChordSize());

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Top row: every selector. Middle: what is played. Bottom: how it is played.
      {
         // "random" walks functional harmony; a preset name (or "custom": the cells as edited)
         // plays the cells below. Picking "custom" keeps the cells and switches to them.
         static std::vector<std::string> progNames, progCats;
         if (progNames.empty())
         {
            int count = 0;
            const NoteTheory::Progression* list = NoteTheory::Progressions(count);
            progNames.push_back("random");
            progCats.push_back("");
            for (int i = 0; i < count; i++)
            {
               progNames.push_back(list[i].name);
               progCats.push_back(list[i].category);
            }
            progNames.push_back("custom");
            progCats.push_back("");
         }
         const int customIdx = (int)progNames.size() - 1;
         const int match = n->MatchingProgression();
         const int progIdx = n->mode != ChorderNode::kProgression ? 0 : (match >= 0 ? match + 1 : customIdx);

         AudioKnobRow row(tok::note_chorder_body_audio_knob_row_layout, tok::note_row_layout_lg, 0.0f, false); // selectors only: a dropdown's height, no caption line
         if (n->useGlobalScale)
            ImGui::BeginDisabled();
         row.Dropdown("scale", MusicTime::ScaleTypeList(), n->scale,
                      [n](int i) { PushUndoCheckpoint(); n->scale = i; });
         row.Dropdown("root", NoteNameList(), n->root,
                      [n](int i) { PushUndoCheckpoint(); n->root = i; });
         if (n->useGlobalScale)
            ImGui::EndDisabled();
         // Chord rate is a step of the global rhythmic table (MusicTime::RateDivisionList: 4 bars ..
         // 1/64, dotted and triplet), not a free beats knob. rateBeats stays the saved float.
         int div = NearestRateDivision(n->rateBeats);
         row.Dropdown("rate", MusicTime::RateDivisionList(), div, [n](int i) {
            PushUndoCheckpoint();
            n->rateBeats = (float)MusicTime::BeatsFor(
               (MusicTime::RateDivision)std::clamp(i, 0, (int)MusicTime::kNumRateDivisions - 1));
         });
         row.Dropdown("chords", progNames, progIdx,
                      [n, customIdx](int i) {
                         PushUndoCheckpoint();
                         if (i <= 0)
                            n->mode = ChorderNode::kRandom;
                         else if (i >= customIdx)
                            n->mode = ChorderNode::kProgression;
                         else
                            n->ApplyProgression(i - 1);
                      },
                      progCats);
         row.End();
         Ui::Spacer(Ui::Space::S1);
      }
      {
         AudioKnobRow row(tok::note_row_layout);
         // notes = how many notes stack into each chord (3 = triad, 4 = seventh, 5+ = ninth, 11th).
         // length = how many chords the progression strip below plays before it loops.
         // oct up = chance a chord gets one extra tone doubled an octave higher (airier voicing).
         Ui::DrawKnob(row, n, ChorderBodyRow::KnobInt<&ChorderNode::chordSize>("notes", params::Chorder::chord_size_lo, params::Chorder::chord_size_hi));
         if (ImGui::IsItemHovered())
            gNodeHoverTip = "Notes per chord: 3 = triad, 4 = with a 7th, 5-6 = 9th and 11th colour";
         Ui::DrawKnob(row, n, ChorderBodyRow::KnobInt<&ChorderNode::progSteps>("length", 1, ChorderNode::kProgCells));
         if (ImGui::IsItemHovered())
            gNodeHoverTip = "Chords before the progression loops (uses the cells below; random mode ignores it)";
         Ui::DrawKnob(row, n, ChorderBodyRow::Knob<&ChorderNode::upperHarmonics>("oct up", params::Chorder::upper_harmonics_lo, params::Chorder::upper_harmonics_hi).Fmt("%.0f%%"));
         if (ImGui::IsItemHovered())
            gNodeHoverTip = "Chance a chord gets an extra note an octave above one of its tones";
         row.End();
      }
      {
         AudioKnobRow row(tok::note_row_layout);
         row.Knob("strum", &n->strumMs, params::Chorder::strum_ms_lo, params::Chorder::strum_ms_hi, "%.0fms", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewStrum30);
         Ui::DrawKnobs(row, n, kChorderBodyRows);
         row.End();
      }
      DrawChorderProgressionStrip(n);

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
      UiType::Push(UiType::Size::Caption);
      const float btnH = ImGui::GetFrameHeight();
      for (int i = 0; i < 4; i++)
      {
         const int v = startVoice + i;
         const float cx = x0 + (float)i * cellW + cellW * 0.5f;
         ImGui::SetCursorScreenPos(ImVec2(cx - btnW * 0.5f, y0));
         char label[32];
         snprintf(label, sizeof(label), "%d##stackEn%d", v + 1, v);
         bool en = n->enabled[v];
         if (AudioToggleButton(label, &en, btnW))
         {
            PushUndoCheckpoint();
            n->enabled[v] = en;
         }
      }
      UiType::Pop();
      ImGui::SetCursorScreenPos(ImVec2(x0, y0 + btnH + 6.0f));
      Ui::Settle(); // the moved cursor must end on an item or ImGui flags the window growing
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
         snprintf(stat, sizeof(stat), "%s notes", UiFmt::Count((long long)(lastStack)).c);
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
         ImGui::TextColored(tok::RoleV4(tok::col::edge_mid_light_fixed), "in - always on");
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

      EndAudioBody();
   }



   void DrawNoteCapturerVisualizer(NoteCapturerNode* n)
   {
      bool held[128];
      n->HeldKeys(held);
      DrawMidiKeyboard(held, 48, 3);
   }


   namespace
   {
      using NoteCapturerBodyRow = params::Row<NoteCapturerNode>;
   }

   void DrawNoteCapturerBody(GraphNode& gn, NoteCapturerNode* n)
   {
      const bool recording = n->IsRecording();
      const bool playing = n->IsPlaying();
      char stat[64];
      if (recording)
         snprintf(stat, sizeof(stat), "REC  %s", UiFmt::Beats(n->RecordedLengthBeats()).c);
      else if (playing)
         snprintf(stat, sizeof(stat), "playing, %s notes", UiFmt::Count((long long)(n->RecordedCount())).c);
      else
         snprintf(stat, sizeof(stat), "%s notes recorded", UiFmt::Count((long long)(n->RecordedCount())).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawNoteCapturerVisualizer(n);
      Ui::Spacer(Ui::Space::S1);

      const float btnW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x * tok::note_capturer_body_ratio) / tok::note_btn_w_ratio;
      if (ActionButton::Draw("Record", ImVec2(btnW, 0), (recording) ? ActionButton::Kind::Record : ActionButton::Kind::Plain))
      {
         if (recording) n->StopRecording();
         else n->StartRecording();
      }
      Ui::SameRow();

      ImGui::BeginDisabled(n->RecordedCount() == 0 && !playing);
      if (ActionButton::Draw("Play", ImVec2(btnW, 0), (playing) ? ActionButton::Kind::Go : ActionButton::Kind::Plain))
      {
         if (playing) n->StopPlayback();
         else n->StartPlayback();
      }
      ImGui::EndDisabled();
      Ui::SameRow();

      if (ActionButton::Draw("Stop", ImVec2(btnW, 0)))
      {
         n->StopRecording();
         n->StopPlayback();
      }
      Ui::SameRow();

      if (ActionButton::Draw("Clear", ImVec2(btnW, 0)))
         n->ClearRecording();

      Ui::Spacer(Ui::Space::S0_5);
      {
         // Same 4-cell grid as the Record/Play/Stop/Clear strip above, so
         // `loop` lands directly under Record instead of floating at the
         // body's left edge under an unrelated cell count. Small explicit
         // height (matches the Flanger/Phaser trailing-checkbox-row
         // precedent) since a lone checkbox doesn't need full knob height
         // plus a caption-line reservation.
         AudioKnobRow row(tok::note_capturer_body_audio_knob_row_layout, tok::note_row_layout_lg, tok::note_row_layout_sm, false);
         Ui::DrawKnob(row, n, NoteCapturerBodyRow::Toggle<&NoteCapturerNode::loop>("loop##capturerLoop"));
         row.Skip();
         row.Skip();
         row.Skip();
         row.End();
      }

      EndAudioBody();
   }


   // ui-content-begin: node visualizer: the signal picture (trace, spectrum, meter, grid)
   void DrawBouncingBallsVisualizer(BouncingBallsNode* n)
   {
      // The frame is the full body width like every other viewer (shared left/right edges); the square
      // arena the physics needs sits centred inside it.
      const float fullW = gAudioBodyW;
      const float w = fullW;
      const float h = fullW * tok::note_h_ratio;
      const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 origin = rowOrigin;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 br(origin.x + w, origin.y + h);
      const bool isLight = IsThemeLight();
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const ImVec2 center((origin.x + br.x) * 0.5f, (origin.y + br.y) * 0.5f);
      const float scale = (std::min(w, h) * 0.5f - 6.0f) / BouncingBallsNode::kBound;
      auto ToScreen = [&](float x, float y) { return ImVec2(center.x + x * scale, center.y - y * scale); };

      const ImU32 outlineCol = isLight ? tok::Base(tok::col::text_secondary) : tok::Base(tok::col::text_secondary);
      const float outlineThickness = tok::note_outline_thickness;
      const float b = BouncingBallsNode::kBound;
      const float triBboxCenterY = (b + -tok::note_bouncing_balls_visualizer_ratio * b) * 0.5f;
      if (n->shape == BouncingBallsNode::kSquare)
      {
         dl->AddRect(ToScreen(-b, b), ToScreen(b, -b), outlineCol, tok::note_bouncing_balls_visualizer_radius, 0, outlineThickness);
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
         dl->AddCircle(center, b * scale, outlineCol, tok::note_bouncing_balls_visualizer_segments, outlineThickness);
      }

      float bx[BouncingBallsNode::kMaxBalls], by[BouncingBallsNode::kMaxBalls], bf[BouncingBallsNode::kMaxBalls];
      const int count = n->BallPositions(bx, by, bf);
      const float ballShiftY = (n->shape == BouncingBallsNode::kTriangle) ? triBboxCenterY : 0.0f;
      for (int i = 0; i < count; i++)
      {
         const ImVec2 p = ToScreen(bx[i], by[i] - ballShiftY);
         const float flash = std::clamp(bf[i], 0.0f, 1.0f);
         const ImU32 restCol = isLight ? tok::Base(tok::col::accent, tok::note_bouncing_balls_visualizer_accent_alpha) : tok::Base(tok::col::accent_pale_muted_warm, tok::note_bouncing_balls_visualizer_accent_pale_muted_warm_alpha);
         const ImU32 hitCol = isLight ? tok::Base(tok::col::state_warn) : tok::Base(tok::col::role_modulation);
         const ImU32 col = ImGui::ColorConvertFloat4ToU32(
            ImLerp(ImGui::ColorConvertU32ToFloat4(restCol), ImGui::ColorConvertU32ToFloat4(hitCol), flash));
         dl->AddCircleFilled(p, n->ballSize * scale, col);
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      ImGui::SetCursorScreenPos(rowOrigin);
      Ui::Claim(ImVec2(fullW, h));
   }
   // ui-content-end


   namespace
   {
      using BouncingBallsBodyRow = params::Row<BouncingBallsNode>;
      const BouncingBallsBodyRow kBouncingBallsBodyRows1[] = {
         BouncingBallsBodyRow::KnobInt<&BouncingBallsNode::numBalls>("balls", 1, BouncingBallsNode::kMaxBalls),
         BouncingBallsBodyRow::Knob<&BouncingBallsNode::ballSpeed>("speed", params::BouncingBalls::ball_speed_lo, params::BouncingBalls::ball_speed_hi).Fmt("%.2f"),
      };
      const BouncingBallsBodyRow kBouncingBallsBodyRows2[] = {
         BouncingBallsBodyRow::Knob<&BouncingBallsNode::ballSize>("size", params::BouncingBalls::ball_size_lo, params::BouncingBalls::ball_size_hi).Fmt("%.2f"),
         BouncingBallsBodyRow::KnobInt<&BouncingBallsNode::rangeLow>("lo", params::BouncingBalls::range_low_lo, params::BouncingBalls::range_low_hi),
         BouncingBallsBodyRow::KnobInt<&BouncingBallsNode::rangeHigh>("hi", params::BouncingBalls::range_high_lo, params::BouncingBalls::range_high_hi),
      };
   }

   void DrawBouncingBallsBody(GraphNode& gn, BouncingBallsNode* n)
   {
      static const std::vector<std::string> kShapes = { "Circle", "Square", "Triangle" };
      char stat[64];
      snprintf(stat, sizeof(stat), "%s, %s balls", kShapes[std::clamp(n->shape, 0, 2)].c_str(), UiFmt::Count((long long)(n->numBalls)).c);

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      DrawBouncingBallsVisualizer(n);
      Ui::Spacer(Ui::Space::S1_25);

      {
         AudioKnobRow row(tok::note_bouncing_balls_body_audio_knob_row_layout);
         row.Dropdown("shape", kShapes, n->shape, [n](int i) { PushUndoCheckpoint(); n->shape = i; });
         Ui::DrawKnobs(row, n, kBouncingBallsBodyRows1);
         row.End();
      }
      {
         AudioKnobRow row(tok::note_bouncing_balls_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kBouncingBallsBodyRows2);
         row.End();
      }

      EndAudioBody();
   }


   // Single-macro bodies below: identical standard rhythm (narrow body, one
   // centred large knob, no second row) as Note Transpose / Pitch Bend / Gate
   // / Glide / Vibrato.
   void DrawStrumBody(GraphNode& gn, NoteStrumNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", UiFmt::Ms(n->strumMs).c);
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(1, kKnobLarge);
         row.Knob("strum", &n->strumMs, params::NoteStrum::strum_ms_lo, params::NoteStrum::strum_ms_hi, "%.1f ms", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewStrum20);
         row.End();
      }

      EndAudioBody();
   }


   namespace
   {
      using AudioToCVBodyRow = params::Row<AudioToCVNode>;
      const AudioToCVBodyRow kAudioToCVBodyRows[] = {
         AudioToCVBodyRow::Knob<&AudioToCVNode::attackMs>("att", params::AudioToCV::attack_ms_lo, params::AudioToCV::attack_ms_hi).Fmt("%.1f ms"),
         AudioToCVBodyRow::Knob<&AudioToCVNode::releaseMs>("rel", params::AudioToCV::release_ms_lo, params::AudioToCV::release_ms_hi).Fmt("%.0f ms"),
      };
   }

   void DrawAudioToCVBody(GraphNode& gn, AudioToCVNode* n)
   {
      const float lvl = n->CurrentLevel();
      char stat[64];
      snprintf(stat, sizeof(stat), "mod %s", UiFmt::F2(lvl).c);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(tok::note_audio_to_cv_body_audio_knob_row_layout);
         static const std::vector<std::string> kModes = { "Peak", "RMS" };
         row.Dropdown("mode", kModes, n->mode,
                      [n](int i) { PushUndoCheckpoint(); n->mode = i; });
         Ui::DrawKnob(row, n, AudioToCVBodyRow::Knob<&AudioToCVNode::gain>("gain", params::AudioToCV::gain_lo, params::AudioToCV::gain_hi).Fmt("%.2fx"));
         row.End();
      }
      {
         AudioKnobRow row(tok::note_audio_to_cv_body_audio_knob_row_layout);
         Ui::DrawKnobs(row, n, kAudioToCVBodyRows);
         row.End();
      }

      EndAudioBody();
   }


   void DrawEnvelopeBody(GraphNode& gn, EnvelopeNode* n)
   {
      char stat[64];
      snprintf(stat, sizeof(stat), "level %s", UiFmt::F2(n->Value01()).c);
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // Same editable-ADSR panel the wavetable engines use (DrawEditableADSR /
      // DrawEnvelopePanel) rather than a bespoke read-only shape - one
      // envelope-editing look across the app instead of two that behave
      // differently. No `amount`: this node's whole output *is* the depth,
      // there's nothing else for a depth control to scale here.
      const ::ModShapes::Ahdsr shape = n->Shape();
      DrawEnvelopePanel("envelope  -  drag the handles", "##envAdsr", &n->attackMs, &n->decayMs,
                        &n->sustainLevel, &n->releaseMs, nullptr, 0.0f, 0.0f, "%.2f",
                        tok::Role(tok::col::accent_alt, tok::note_envelope_alpha), &shape);
      Ui::Spacer(Ui::Space::S1);

      // Hold at the peak, then each moving stage's bend: 0 straight,
      // positive fast first (a falling decay/release), negative slow first.
      // Sliders, not knobs: the bends read as numbers to set, two to a row.
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float rowH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
      auto pair = [&](auto&& left, auto&& right) {
         const float halfW = (gAudioContentW - gap) * 0.5f;
         const ImVec2 at = ImGui::GetCursorScreenPos();
         left(halfW);
         ImGui::SetCursorScreenPos(ImVec2(at.x + halfW + gap, at.y));
         right(halfW);
         ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH));
      };
      BeginAudioSection("shape");
      pair([&](float w) { AudioSlider("hold", &n->holdMs, params::Envelope::hold_ms_lo, params::Envelope::hold_ms_hi, "%.0f ms", w); },
           [&](float w) { AudioSlider("attack curve", &n->attackCurve, -1.0f, 1.0f, "%.2f", w); });
      pair([&](float w) { AudioSlider("decay curve", &n->decayCurve, -1.0f, 1.0f, "%.2f", w); },
           [&](float w) { AudioSlider("release curve", &n->releaseCurve, -1.0f, 1.0f, "%.2f", w); });
      EndAudioSection();

      // "in" is used only while nothing is patched into the modulator pin;
      // "threshold" is the gate line on the raw 0..1 input - rising through
      // it starts attack, falling back below it starts release (see the
      // class comment on EnvelopeNode, ModulatorNodes.h).
      // "output": ADSR is the plain 0..1 envelope (rest 0, peak 1, sustain =
      // level); centred is the pre-v0.5 bipolar offset around 0.5 that
      // patches saved before it load with.
      static const std::vector<std::string> kOutputs = { "centred", "ADSR" };
      // "per voice": cabled straight into an Analog or Wavetable knob, each
      // note gets its own envelope, gated by its own note-on and note-off
      // (ADSR output only; centred needs the shared input).
      BeginAudioSection("trigger");
      pair([&](float w) {
              DropdownButton("output", kOutputs, n->output,
                             [n](int i) { PushUndoCheckpoint(); n->output = i; }, w);
           },
           [&](float w) { AudioSlider("input", &n->constantIn, 0.0f, 1.0f, "%.2f", w); });
      pair([&](float w) { AudioSlider("threshold", &n->threshold, 0.0f, 1.0f, "%.2f", w); },
           [&](float) { ModCheckbox("per voice##envVoice", &n->voice); });
      EndAudioSection();

      EndAudioBody();
   }
}

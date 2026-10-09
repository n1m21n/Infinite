// Audio-in, mixer, keyboard and note node bodies (moved verbatim from main.cpp).
#include "app/ui/design/UiType.h"
#include "app/ui/design/TokenColors.h"
#include "app/AppShared.h"
#include "app/ui/design/components/AudioViz.h"
#include "app/ui/design/components/StepCell.h"

namespace app
{
   struct AudioInChoice
   {
      std::string label;       // e.g. "Stereo (1+2)", "Input 1 (Mono)"
      std::string shortName;   // e.g. "Stereo", "In 1"
      std::string category;    // Device name or "System Default"
      uint32_t deviceId = 0;
      int channelMode = 0;     // 0 = Stereo (1+2), 1 = In 1, 2 = In 2, 3 = In 3...
   };


   // Mirror image of Audio Out's body (a terminal with no controls at all) -
   // Audio In has an input channel/device selector and a trim fader.
   // The stat line reports the active input and whether the mic tap is
   // actually live (Platform::AudioInputCaptureIsRunning).
   void DrawAudioInBody(GraphNode& gn, AudioInputNode* n)
   {
      std::vector<AudioInChoice> choices;
      const std::vector<Platform::AudioDeviceInfo> devices = Platform::AudioListDevices();

      // System Default options
      choices.push_back({ "Default: Stereo (1+2)", "Default: Stereo", "System Default", 0, 0 });
      choices.push_back({ "Default: Input 1 (Mono)", "Default: In 1", "System Default", 0, 1 });
      choices.push_back({ "Default: Input 2 (Mono)", "Default: In 2", "System Default", 0, 2 });

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
      for (int i = 0; i < (int)choices.size(); i++)
      {
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
      if (Platform::AudioInputCaptureIsRunning())
         snprintf(stat, sizeof(stat), "%+.1f dB   %s - live", n->gainDb, inputDesc.c_str());
      else if (!n->Status().empty())
         snprintf(stat, sizeof(stat), "%+.1f dB   %s", n->gainDb, n->Status().c_str());
      else
         snprintf(stat, sizeof(stat), "%+.1f dB   %s - idle", n->gainDb, inputDesc.c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      ImGui::Dummy(ImVec2(0.0f, 2.0f));

      {
         PushDropdownStyle();
         ImGui::SetCursorScreenPos(ImVec2(gAudioContentX + 4.0f, ImGui::GetCursorScreenPos().y));
         const std::string caption = options[currentIdx] + "##audioin_input";
         if (NodeDropdownField(caption.c_str(), ImVec2(gAudioContentW - 8.0f, 0)))
         {
            gDropdown.options = options;
            gDropdown.categories = categories;
            gDropdown.onSelect = [n, choices](int idx) {
               if (idx >= 0 && idx < (int)choices.size())
               {
                  PushUndoCheckpoint();
                  n->channelMode = choices[idx].channelMode;
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

      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      const float faderH = 138.0f;
      const float stripTop = ImGui::GetCursorScreenPos().y;
      const float cx = gAudioContentX + 0.5f * gAudioContentW;
      DrawStripMeter(cx + 14.0f, stripTop + 6.0f, 8.0f, faderH - 12.0f, n->Level());
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
      const float bodyW = std::max(280.0f, (float)count * 80.0f);
      char stat[64];
      if (count > 0)
         snprintf(stat, sizeof(stat), "%d in -> 1 out   sum %+.1f dB",
                  count, DspMath::LinearToDb(std::max(n->Level(), 1e-5f)));
      else
         snprintf(stat, sizeof(stat), "0 in -> 0 out (idle)");

      BeginAudioBody(gn.index, gn.category, bodyW, stat);
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      // Channels param control
      {
         ImGui::PushItemWidth(140.0f);
         int ch = n->numChannels;
         if (ImGui::SliderInt("channels", &ch, 0, MixerNode::kMaxSlots))
         {
            PushUndoCheckpoint();
            n->numChannels = std::clamp(ch, 0, (int)MixerNode::kMaxSlots);
            RebuildAudioTopology();
         }
         ImGui::PopItemWidth();
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
            AudioKnobRow row(count, faderH);
            for (int i = 0; i < count; i++)
            {
               char label[8];
               snprintf(label, sizeof(label), "%d", i + 1);
               row.Fader(label, &n->gainDb[i], -60.0f, 12.0f, "%.1f dB", faderH, /*dbTaper=*/true);
            }
            row.End();
         }

         // Solo (S) and Mute (M) row
         {
            const float btnW = std::min((cellW - 8.0f) * 0.5f, 26.0f);
            const float rowY = ImGui::GetCursorScreenPos().y;
            for (int i = 0; i < count; i++)
            {
               const float cx = gAudioContentX + ((float)i + 0.5f) * cellW;
               const float sX = cx - btnW - 2.0f;
               const float mX = cx + 2.0f;

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
   // `ringNote` (-1 = none) draws a coloured outline around one key on top
   // of its lit/unlit fill - Note Filter's live last-note-passed/blocked
   // marker (node-ui-pillars P9's "one live marker" requirement). MIDI Notes
   // doesn't pass it, so its two call sites are unaffected by the default.
   void DrawMidiKeyboard(const bool held[128], int lowNote, int octaves, int ringNote = -1, bool ringGood = true)
   {
      const float w = gAudioBodyW, h = 58.0f;
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
      const float keyW = (w - 4.0f) / (float)whiteCount;
      const ImU32 ringCol = ringGood ? tok::U32(tok::pal::c_78C8FFFF) : tok::U32(tok::pal::c_FF6056FF);

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + (float)(o * 7 + k) * keyW;
            const ImU32 whiteCol = on ? (isLight ? tok::U32(tok::pal::c_3282F5FF) : tok::U32(tok::pal::c_78C8FFF5))
                                      : (isLight ? tok::U32(tok::pal::c_FAFAFFFF) : tok::U32(tok::pal::c_CED2DEFF));
            dl->AddRectFilled(ImVec2(x + 0.5f, origin.y + 3.0f), ImVec2(x + keyW - 0.5f, br.y - 3.0f),
                              whiteCol, 2.0f);
            if (note == ringNote)
               dl->AddRect(ImVec2(x + 1.0f, origin.y + 4.0f), ImVec2(x + keyW - 1.0f, br.y - 4.0f),
                          ringCol, 2.0f, 0, 2.0f);
         }
      }
      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 5; k++)
         {
            const int note = lowNote + o * 12 + kBlackOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[k] + 1.0f) * keyW - keyW * 0.3f;
            const ImU32 blackCol = on ? (isLight ? tok::U32(tok::pal::c_1E64E6FF) : tok::U32(tok::pal::c_5AAAEBFF))
                                      : (isLight ? tok::U32(tok::pal::c_3C4150FF) : tok::U32(tok::pal::c_1A1C24FF));
            dl->AddRectFilled(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                              blackCol, 2.0f);
            if (note == ringNote)
               dl->AddRect(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                          ringCol, 2.0f, 0, 2.0f);
         }
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
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
      else
         snprintf(stat, sizeof(stat), "listening  -  %s",
                  n->channel < 0 ? "omni" : "one channel");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);
      // Centred on the last note played so a keyboard playing outside the
      // default range still shows something, rather than sitting dark.
      const int last = n->LastNote();
      const int lowNote = last >= 0 ? std::clamp((last / 12) * 12 - 12, 0, 108) : 48;
      DrawMidiKeyboard(held, lowNote, 3);
      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      {
         AudioKnobRow row(3, kKnobLarge);
         // -1 is omni, so the knob's low end is a mode rather than a channel
         // number - spelled out in the readout format rather than hidden.
         row.KnobInt("channel", &n->channel, -1, 15, kKnobLarge);
         row.KnobInt("transpose", &n->transpose, -24, 24, kKnobLarge);
         row.Knob("velocity", &n->velocityScale, 0.0f, 2.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         // MPE: each held note follows its own member channel's pitch bend.
         AudioKnobRow row(3, kKnobLarge);
         row.Checkbox("mpe##midiMpe", &n->mpe);
         row.Knob("bend st", &n->mpeBendRange, 1.0f, 96.0f, "%.0f", kKnobLarge);
         row.Skip();
         row.End();
      }

      BeginAudioSection("input");
      if (!Platform::MidiIsRunning())
      {
         if (ActionButton::Draw("MIDI Learn", ImVec2(kPreviewSize, 0)))
            n->StartListening();
         ImGui::TextDisabled("click to start listening for a connected controller");
      }
      else
      {
         ImGui::TextUnformatted(Platform::MidiDeviceSummary().c_str());
      }
      ImGui::TextDisabled("%s", n->channel < 0 ? "channel: omni (-1)" : "channel: filtered");
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
   int DrawInteractiveKeyboard(const bool held[128], int lowNote, int octaves)
   {
      const float w = gAudioBodyW, h = 64.0f;
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
      const float keyW = (w - 4.0f) / (float)whiteCount;

      ImGui::SetCursorScreenPos(origin);
      ImGui::InvisibleButton("##keyboardHit", ImVec2(w, h));
      const bool active = ImGui::IsItemActive();
      const ImVec2 mouse = ImGui::GetIO().MousePos;

      int hitNote = -1;
      if (active && mouse.x >= origin.x && mouse.x < br.x && mouse.y >= origin.y && mouse.y < br.y)
      {
         // Black keys sit visually on top, so test them first.
         const float relY = mouse.y - origin.y;
         if (relY < h * 0.62f)
         {
            for (int o = 0; o < octaves && hitNote < 0; o++)
            {
               for (int k = 0; k < 5; k++)
               {
                  const int note = lowNote + o * 12 + kBlackOffsets[k];
                  const float x = origin.x + 2.0f + ((float)(o * 7) + kBlackSlot[k] + 1.0f) * keyW - keyW * 0.3f;
                  if (mouse.x >= x && mouse.x < x + keyW * 0.6f)
                  {
                     hitNote = note;
                     break;
                  }
               }
            }
         }
         if (hitNote < 0)
         {
            const int col = std::clamp((int)((mouse.x - origin.x - 2.0f) / keyW), 0, whiteCount - 1);
            hitNote = lowNote + (col / 7) * 12 + kWhiteOffsets[col % 7];
         }
      }

      for (int o = 0; o < octaves; o++)
      {
         for (int k = 0; k < 7; k++)
         {
            const int note = lowNote + o * 12 + kWhiteOffsets[k];
            const bool on = note >= 0 && note < 128 && held[note];
            const float x = origin.x + 2.0f + (float)(o * 7 + k) * keyW;
            const ImU32 whiteCol = on ? (isLight ? tok::U32(tok::pal::c_3282F5FF) : tok::U32(tok::pal::c_78C8FFF5))
                                      : (isLight ? tok::U32(tok::pal::c_FAFAFFFF) : tok::U32(tok::pal::c_CED2DEFF));
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
            const ImU32 blackCol = on ? (isLight ? tok::U32(tok::pal::c_1E64E6FF) : tok::U32(tok::pal::c_5AAAEBFF))
                                      : (isLight ? tok::U32(tok::pal::c_3C4150FF) : tok::U32(tok::pal::c_1A1C24FF));
            dl->AddRectFilled(ImVec2(x, origin.y + 3.0f), ImVec2(x + keyW * 0.6f, origin.y + h * 0.62f),
                              blackCol, 2.0f);
         }
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
      return hitNote;
   }


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
         snprintf(stat, sizeof(stat), "click or type to play");

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      const int baseNote = (n->baseOctave + 1) * 12;
      const int lowNote = std::clamp(baseNote, 0, 103);   // fixed - never recentres under an active click
      const int hit = DrawInteractiveKeyboard(held, lowNote, 3);

      if (hit != n->mMouseNote)
      {
         if (n->mMouseNote >= 0)
            n->SetKeyState(n->mMouseNote, false);
         if (hit >= 0)
            n->SetKeyState(hit, true);
         n->mMouseNote = hit;
      }

      const bool isHovered = ed::GetHoveredNode() == ed::NodeId(gn.NodeId());
      if (n->computerKeyboardEnabled && isHovered)
         gComputerKeyboardHot = true; // letter keys belong to this node; canvas WASD/F/H/U stand down
      for (const TypingKey& tk : kTypingKeys)
      {
         const int note = std::clamp(baseNote + tk.semitone, 0, 127);
         // Release always processed, even off-hover, so a key held while the
         // mouse leaves the node can't leave a note stuck on. Press requires
         // hover + the toggle, so typing here doesn't hijack shortcuts
         // elsewhere in the app.
         if (ImGui::IsKeyReleased(tk.key))
            n->SetKeyState(note, false);
         // !gArrangeFocused: the timeline owns the keyboard (its M marker
         // key would otherwise also play a note here).
         else if (n->computerKeyboardEnabled && isHovered && !TextFocusClaimed() && !gArrangeFocused
                  && ImGui::IsKeyPressed(tk.key, false))
            n->SetKeyState(note, true);
      }

      ImGui::Dummy(ImVec2(0.0f, 5.0f));

      {
         AudioKnobRow row(3, kKnobLarge);
         row.KnobInt("octave", &n->baseOctave, -1, 7, kKnobLarge);
         row.KnobInt("transpose", &n->transpose, -24, 24, kKnobLarge);
         row.Knob("velocity", &n->velocityScale, 0.0f, 2.0f, "%.2f", kKnobLarge);
         row.End();
      }
      {
         // Checkbox in the leftmost cell (P3), matching the 3-cell knob row
         // above it (P1 corollary) with the rest deliberately skipped (P6) -
         // the same "bottom left" shape as the other AudioEffects checkbox
         // rows (e.g. DrawChorusBody's "analog" row).
         AudioKnobRow row(3, 20.0f, 0.0f, false);
         row.Checkbox("numpad##kbToggle", &n->computerKeyboardEnabled);
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


   void DrawCVToPitchParams(CVToPitchNode* n)
   {
      // The semitone readout is the point of this node - make it the
      // visually dominant element, not a text line under six sliders.
      char st[16];
      snprintf(st, sizeof(st), "%+d st", n->LastSemitone());
      {
         UiType::Scope readout(UiType::Size::Display, UiType::Weight::Medium);
         ImGui::TextUnformatted(st);
      }

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


   void DrawNoteToCVParams(NoteToCVNode* n)
   {
      const int last = n->LastNote();
      if (last >= 0)
         ImGui::TextDisabled("last: %s%d (MIDI %d)", NoteNameList()[last % 12].c_str(), last / 12 - 1, last);
      else
         ImGui::TextDisabled("last: none");

      if (ModSliderInt("range low", &n->rangeLow, 0, 126))
         n->rangeHigh = std::max(n->rangeHigh, n->rangeLow + 1);
      if (ModSliderInt("range high", &n->rangeHigh, 1, 127))
         n->rangeLow = std::min(n->rangeLow, n->rangeHigh - 1);

      ModSlider("glide (ms)", &n->glideMs, 0.0f, 500.0f);
   }


   void DrawVelocityToCVParams(VelocityToCVNode* n)
   {
      ImGui::TextDisabled("velocity: %.2f", n->Value01());
      ModSlider("range low", &n->rangeLow, 0.0f, 1.0f);
      ModSlider("range high", &n->rangeHigh, 0.0f, 1.0f);
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
         snprintf(label, sizeof(label), "stop  (%.1f beats)###cvRec", n->LengthBeats());
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

      ImGui::BeginDisabled(n->input != nullptr);
      ModSlider("in (no cable)", &n->constantIn, 0.0f, 1.0f);
      ImGui::EndDisabled();

      ModSlider("speed", &n->speed, 0.05f, 4.0f);
      ModSlider("low", &n->low, 0.0f, 1.0f);
      ModSlider("high", &n->high, 0.0f, 1.0f);
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
      }
      else
      {
         row.Knob("rate", rateSeconds, 0.02f, 2.0f, "%.2fs", kKnobSmall);
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
      snprintf(stat, sizeof(stat), "%+d st", n->semitones);
      DrawSingleKnobIntAudioBody(gn, stat, "transpose", &n->semitones, -48, 48);
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
      DrawSingleKnobAudioBody(gn, stat, "glide", &n->glideMs, 0.0f, 2000.0f, "%.0f ms", AudioWidgetStyle::KnobSkewGlide150);
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
      AudioViz::Fill(dl, origin, br);
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
      dl->PathStroke(isLight ? tok::U32(tok::pal::c_1E6EE6FF) : tok::U32(tok::pal::c_96D6FFF5), 0, 1.8f);

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
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
      const auto& options = MusicTime::QuantizeGridList();
      const int safeDiv = std::clamp(n->div, 0, (int)options.size() - 1);
      char stat[48];
      snprintf(stat, sizeof(stat), "%s", options[safeDiv].c_str());

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);
      AudioKnobRow row(1);
      row.Dropdown("grid", options, safeDiv, [n](int i) {
         PushUndoCheckpoint();
         n->div = i;
      });
      row.End();
      EndAudioBody();
   }


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
                     tok::U32(tok::pal::c_22C55EFF), confBadge);
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
                     tok::U32(tok::pal::c_22C55EFF), confBadge);
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
                     tok::U32(tok::pal::c_22C55EFF), confBadge);
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
         AudioKnobRow row(2);
         row.Dropdown("root", MidiNoteNameList(), n->root, [n](int i) { n->root = i; });
         row.Knob("mix", &n->mix, 0.0f, 1.0f, "%.2f", kKnobLarge);
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
         bool muteDryBool = n->muteDry;
         if (row.Checkbox("Mute Dry##echoMuteDry", &muteDryBool))
            n->muteDry = muteDryBool;
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
                     tok::U32(tok::pal::c_22C55EFF), confBadge);
      }

      {
         const float w = gAudioContentW;
         const float h = ImGui::GetFrameHeight();
         const bool learning = n->IsLearning();
         if (ActionButton::Draw(learning ? "Stop##predLearn" : "Learn##predLearn", ImVec2(w * 0.3f, h)))
         {
            PushUndoCheckpoint();
            n->SetLearning(!learning);
         }
         ImGui::SameLine();
         // Learning meter: how much the model beats a memoryless one, per captured bar.
         const ImVec2 p0 = ImGui::GetCursorScreenPos();
         const float mw = w - w * 0.3f - ImGui::GetStyle().ItemSpacing.x;
         ImDrawList* dl = ImGui::GetWindowDrawList();
         dl->AddRectFilled(p0, ImVec2(p0.x + mw, p0.y + h), tok::U32(tok::pal::c_FFFFFF0E), 3.0f);
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
               dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), tok::U32(tok::pal::c_78C88CE6), 1.5f);
            }
         }
         if (conf > 0.0f)
         {
            char confStr[32];
            snprintf(confStr, sizeof(confStr), "%d%%", (int)std::round(conf * 100.0f));
            const ImVec2 csz = ImGui::CalcTextSize(confStr);
            dl->AddText(ImVec2(p0.x + mw - csz.x - 6.0f, p0.y + (h - csz.y) * 0.5f),
                        tok::U32(tok::pal::c_22C55EFF), confStr);
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
         snprintf(stat, sizeof(stat), "%d of %d inputs active", active, NoteMergeNode::kSlots);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      ImGui::TextDisabled("merges all connected inputs");
      ImGui::TextDisabled("to a single output stream");

      EndAudioBody();
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
         snprintf(stat, sizeof(stat), "every %.2fs  -  slot %d", n->rateSeconds, active + 1);

      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      // Four LED dots: active slot lit, connected dim, unconnected darkest
      {
         const float w = gAudioBodyW;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const float dia = 14.0f;
         const float gap = (w - (float)NoteSwitcherNode::kSlots * dia) / (float)(NoteSwitcherNode::kSlots + 1);
         for (int i = 0; i < NoteSwitcherNode::kSlots; i++)
         {
            const float cx = origin.x + gap * (float)(i + 1) + dia * (float)i + dia * 0.5f;
            const bool isConnected = n->noteInputs[i].IsConnected();
            const bool isActive = (i == active);
            ImU32 col = tok::U32(tok::pal::c_323540FF); // unconnected darkest
            if (isActive)
               col = tok::U32(tok::pal::c_78C8FFFF); // active lit
            else if (isConnected)
               col = tok::U32(tok::pal::c_4678AAFF); // connected dim
            dl->AddCircleFilled(ImVec2(cx, origin.y + dia * 0.5f), dia * 0.5f, col);
         }
         ImGui::Dummy(ImVec2(w, dia + 6.0f));
      }

      {
         AudioKnobRow row(2);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.End();
      }

      {
         AudioKnobRow row(2, 20.0f, 0.0f, false);
         row.Checkbox("manual##switcherManual", &n->manual);
         row.End();
      }
      if (n->manual)
      {
         ModSliderInt("slot", &n->manualSlot, 0, NoteSwitcherNode::kSlots - 1);
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
                                lit ? tok::U32(tok::pal::c_78C8FFFF) : tok::U32(tok::pal::c_323540FF));
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

         StepCell::Draw(dl, cellMin, cellMax, on, isPlayhead, ImGui::IsItemHovered());

         ImGui::PopID();
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
         gArpGateDrag.active = false;

      ImGui::SetCursorScreenPos(origin);
      ImGui::Dummy(ImVec2(w, h));
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

      EndAudioBody();
   }


   void DrawNoteSequencerBody(GraphNode& gn, NoteSequencerNode* n)
   {
      const int cur = n->CurrentStep();
      const int steps = std::clamp(n->steps, 1, NoteSequencerNode::kMaxSteps);
      char stat[80];
      if (cur >= 0 && cur < steps)
      {
         int playNote = n->stepNote[cur];
         if (n->useGlobalScale)
            playNote = MusicTime::SnapToScale(playNote, Transport::Instance().Key(), Transport::Instance().Scale(), MusicTime::kSnapNearest);
         playNote = std::clamp(playNote, 0, 127);
         snprintf(stat, sizeof(stat), "step %d/%d - %s%d (%+dst)%s",
                  cur + 1, steps,
                  NoteNameList()[playNote % 12].c_str(), playNote / 12 - 1,
                  playNote - 60,
                  n->stepEnabled[cur] ? "" : " [muted]");
      }
      else
      {
         snprintf(stat, sizeof(stat), "step 0/%d", steps);
      }

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      int hoverStep = -1;
      {
         const float w = gAudioBodyW;
         const float pitchH = 170.0f;
         const float velH = 22.0f;
         const float totalGridH = pitchH + velH;
         const float gap = 2.0f;
         const float barW = (w - gap * (float)(steps - 1)) / (float)steps;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + w, origin.y + totalGridH);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         ImFont* font = ImGui::GetFont();
         const bool isLight = IsThemeLight();

         const int kLow = 36, kHigh = 84; // C2..C6 (-24..+24 st, 49 notes)
         const int numNotes = kHigh - kLow + 1;
         const float noteH = pitchH / (float)numNotes;

         auto DrawTextCentered = [&](const ImVec2& minP, const ImVec2& maxP, ImU32 col, const char* text, float fSz) {
            const ImVec2 sz = font->CalcTextSizeA(fSz, FLT_MAX, 0.0f, text);
            const float tx = minP.x + std::max(0.0f, (maxP.x - minP.x - sz.x) * 0.5f);
            const float ty = minP.y + std::max(0.0f, (maxP.y - minP.y - sz.y) * 0.5f);
            dl->AddText(font, fSz, ImVec2(tx, ty), col, text);
         };

         // ---- 1) Container chassis ----
         dl->AddRectFilled(origin, br, isLight ? tok::U32(tok::pal::c_ECF0F8FF) : tok::U32(tok::pal::c_101016FF), 4.0f);

         // ---- 2) Octave guide lines across pitch area ----
         const int octaveNotes[] = { 48, 60, 72 }; // C3, C4, C5
         for (int octNote : octaveNotes)
         {
            const float lineY = origin.y + (float)(kHigh - octNote) * noteH;
            const bool isMiddleC = (octNote == 60);
            const ImU32 lineCol = isMiddleC
               ? (isLight ? tok::U32(tok::pal::c_AFB6C6DC) : tok::U32(tok::pal::c_414658DC))
               : (isLight ? tok::U32(tok::pal::c_D2D7E296) : tok::U32(tok::pal::c_20232E96));
            dl->AddLine(ImVec2(origin.x + 2.0f, lineY), ImVec2(br.x - 2.0f, lineY), lineCol, isMiddleC ? 1.5f : 1.0f);
         }

         // Divider line between pitch matrix and velocity lane
         dl->AddLine(ImVec2(origin.x, origin.y + pitchH), ImVec2(br.x, origin.y + pitchH),
                     isLight ? tok::U32(tok::pal::c_C8CEDAFF) : tok::U32(tok::pal::c_282C38FF), 1.0f);

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
               ? (isGroupStart ? tok::U32(tok::pal::c_D6DCE8FF) : tok::U32(tok::pal::c_E2E6F0FF))
               : (isGroupStart ? tok::U32(tok::pal::c_20232EFF) : tok::U32(tok::pal::c_161820FF));
            dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + pitchH), trackCol, 2.0f);

            // ---- Pitch interactive area ----
            ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
            ImGui::InvisibleButton("pitch", ImVec2(barW, pitchH));
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
            else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
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
            const float capH = std::max(15.0f, noteH + 2.5f);
            const float capTop = std::clamp(noteTopY - (capH - noteH) * 0.5f, origin.y, origin.y + pitchH - capH);
            const float capBot = capTop + capH;

            // Playhead column wash or hover highlight
            if (isCurrent)
            {
               dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + pitchH),
                                 isLight ? tok::U32(tok::pal::c_FFC85032) : tok::U32(tok::pal::c_FFBE5023), 2.0f);
            }
            else if (pitchHovered)
            {
               dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + pitchH),
                                 isLight ? tok::U32(tok::pal::c_0000000A) : tok::U32(tok::pal::c_FFFFFF0C), 2.0f);
            }

            // Pitch bar stem (from cap to bottom of pitch area)
            if (capBot < origin.y + pitchH)
            {
               const ImU32 stemCol = n->stepEnabled[i]
                  ? (isCurrent
                        ? (isLight ? tok::U32(tok::pal::c_EB911EA0) : tok::U32(tok::pal::c_E1AA3296))
                        : (isLight ? tok::U32(tok::pal::c_2D7DE696) : tok::U32(tok::pal::c_3C8CEB8C)))
                  : (isLight ? tok::U32(tok::pal::c_C3C8D25A) : tok::U32(tok::pal::c_262A365A));
               dl->AddRectFilled(ImVec2(x0 + 1.0f, capBot), ImVec2(x1 - 1.0f, origin.y + pitchH),
                                 stemCol, 2.0f);
            }

            // Note pill cap
            const float fontCapSz = steps <= 8 ? 10.5f : (steps <= 12 ? 9.5f : 8.5f);
            if (n->stepEnabled[i])
            {
               const ImU32 capCol = isCurrent
                  ? (isLight ? tok::U32(tok::pal::c_F59B19FF) : tok::U32(tok::pal::c_FFC850FF))
                  : (pitchActive
                        ? (isLight ? tok::U32(tok::pal::c_1469DCFF) : tok::U32(tok::pal::c_78C3FFFF))
                        : (isLight ? tok::U32(tok::pal::c_2378EBFF) : tok::U32(tok::pal::c_50AAFFFF)));
               const ImU32 capBorder = isCurrent
                  ? (isLight ? tok::U32(tok::pal::c_FFE68CFF) : tok::U32(tok::pal::c_FFF5B4FF))
                  : (isLight ? tok::U32(tok::pal::c_A0CDFFC8) : tok::U32(tok::pal::c_B4E6FFB4));

               dl->AddRectFilled(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), capCol, 3.0f);
               dl->AddRect(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), capBorder, 3.0f);

               char noteStr[16];
               snprintf(noteStr, sizeof(noteStr), "%s%d", NoteNameList()[dispPc].c_str(), dispOct);
               const ImU32 txtCol = isCurrent
                  ? tok::U32(tok::pal::c_140F05FF)
                  : (isLight ? tok::U32(tok::pal::c_FFFFFFFF) : tok::U32(tok::pal::c_0A1423FF));
               DrawTextCentered(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), txtCol, noteStr, fontCapSz);
            }
            else
            {
               const ImU32 capCol = isLight ? tok::U32(tok::pal::c_D0D5E0C8) : tok::U32(tok::pal::c_282C38C8);
               const ImU32 capBorder = isLight ? tok::U32(tok::pal::c_AFB6C4B4) : tok::U32(tok::pal::c_414655B4);
               dl->AddRectFilled(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), capCol, 3.0f);
               dl->AddRect(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), capBorder, 3.0f);

               char noteStr[16];
               snprintf(noteStr, sizeof(noteStr), "%s%d", NoteNameList()[dispPc].c_str(), dispOct);
               const ImU32 txtCol = isLight ? tok::U32(tok::pal::c_828898C8) : tok::U32(tok::pal::c_696E7DB4);
               DrawTextCentered(ImVec2(x0 + 1.0f, capTop), ImVec2(x1 - 1.0f, capBot), txtCol, noteStr, fontCapSz);
            }

            // ---- Velocity lane ----
            const float vy0 = origin.y + pitchH;
            const float vy1 = vy0 + velH;
            ImGui::SetCursorScreenPos(ImVec2(x0, vy0));
            ImGui::InvisibleButton("vel", ImVec2(barW, velH));
            const bool velHovered = ImGui::IsItemHovered();
            const bool velActive = ImGui::IsItemActive();
            if (velHovered)
               hoverStep = i;

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
               PushUndoCheckpoint();
               n->stepEnabled[i] = !n->stepEnabled[i];
            }
            else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
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
            const ImU32 velTrackCol = isLight ? tok::U32(tok::pal::c_DCE1ECFF) : tok::U32(tok::pal::c_12141AFF);
            dl->AddRectFilled(ImVec2(x0 + 1.0f, vy0 + 1.0f), ImVec2(x1 - 1.0f, vy1 - 1.0f), velTrackCol, 2.0f);

            const float fontVelSz = steps <= 8 ? 9.5f : 8.5f;
            if (n->stepEnabled[i])
            {
               const float vFillTop = vy1 - n->stepVelocity[i] * (velH - 2.0f) - 1.0f;
               const ImU32 vCol = isCurrent
                  ? (isLight ? tok::U32(tok::pal::c_EB911EF0) : tok::U32(tok::pal::c_FFCD5AFF))
                  : (isLight ? tok::U32(tok::pal::c_28A564E6) : tok::U32(tok::pal::c_41CD87F0));
               dl->AddRectFilled(ImVec2(x0 + 1.0f, vFillTop), ImVec2(x1 - 1.0f, vy1 - 1.0f), vCol, 2.0f);

               char vStr[16];
               snprintf(vStr, sizeof(vStr), "%.0f", n->stepVelocity[i] * 100.0f);
               const ImU32 txtVCol = isLight ? tok::U32(tok::pal::c_0A190FE6) : tok::U32(tok::pal::c_0A190FE6);
               DrawTextCentered(ImVec2(x0 + 1.0f, vy0), ImVec2(x1 - 1.0f, vy1), txtVCol, vStr, fontVelSz);
            }
            else
            {
               DrawTextCentered(ImVec2(x0 + 1.0f, vy0), ImVec2(x1 - 1.0f, vy1),
                                isLight ? tok::U32(tok::pal::c_8C91A0B4) : tok::U32(tok::pal::c_555A69B4), "OFF", fontVelSz);
            }

            // Playhead column outline
            if (isCurrent)
            {
               dl->AddRect(ImVec2(x0, origin.y), ImVec2(x1, vy1),
                           isLight ? tok::U32(tok::pal::c_E18214F0) : tok::U32(tok::pal::c_FFD250E6), 2.0f, 0, 1.5f);
            }

            // ---- Step number label below each column (Pattern-style) ----
            char label[8];
            snprintf(label, sizeof(label), "%d", i + 1);
            // Cap at the ambient UI font size and only shrink below it once a
            // column gets too narrow to fit a number at that size - the old
            // `steps > 8 ? barW * 0.95f` branch scaled font size UP with
            // column width instead of down, so going from 8 to 9+ steps (still
            // ~45px-wide columns at this node width) jumped the step numbers
            // to ~3x the normal text size instead of shrinking them.
            const float labelFontSize = std::min(ImGui::GetFontSize(), std::max(8.0f, barW * 0.95f));
            const ImVec2 textSize = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
            const ImU32 stepNumCol = isCurrent
               ? (isLight ? tok::U32(tok::pal::c_E18214FF) : tok::U32(tok::pal::c_FFC850FF))
               : (isLight
                     ? (isGroupStart ? tok::U32(tok::pal::c_283041FF) : tok::U32(tok::pal::c_6E7484FF))
                     : (isGroupStart ? tok::U32(tok::pal::c_BEC2D2FF) : tok::U32(tok::pal::c_6E7282FF)));
            dl->AddText(font, labelFontSize, ImVec2(x0 + (barW - textSize.x) * 0.5f, br.y + 3.0f),
                        stepNumCol, label);

            ImGui::PopID();
         }

         // Grid outer border
         dl->AddRect(origin, br, isLight ? tok::U32(tok::pal::c_B9C0D0FF) : tok::U32(tok::pal::c_414655FF), 4.0f);

         const float labelRowH = ImGui::GetFontSize() + 5.0f;
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
               ImGui::TextDisabled("step %d: %s%d (%+dst) | vel %.0f%% (dbl-click to mute)",
                                   hoverStep + 1, NoteNameList()[dispPc].c_str(), dispOct, stFromC4,
                                   n->stepVelocity[hoverStep] * 100.0f);
            else
               ImGui::TextDisabled("step %d: %s%d (%+dst) [MUTED] (dbl-click to unmute)",
                                   hoverStep + 1, NoteNameList()[dispPc].c_str(), dispOct, stFromC4);
         }
         else
         {
            ImGui::TextDisabled("%d steps, looped", steps);
         }
      }

      {
         AudioKnobRow row(4);
         row.KnobInt("steps", &n->steps, 1, NoteSequencerNode::kMaxSteps);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.Knob("gate", &n->gatePercent, 1.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

      EndAudioBody();
   }


   void DrawMidiFileBody(GraphNode& gn, MidiFileNode* n)
   {
      const MidiFile::Song* song = n->GetSong();
      char stat[96];
      if (!n->Status().empty())
         snprintf(stat, sizeof(stat), "%s", n->Status().c_str());
      else if (song)
         snprintf(stat, sizeof(stat), "%d notes - %d track%s - %.0f beats", song->noteCount, song->trackCount,
                  song->trackCount == 1 ? "" : "s", n->LoopBeats());
      else
         snprintf(stat, sizeof(stat), "no file");
      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      if (ActionButton::Draw("Load...", ImVec2(90, 0)))
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
         ImGui::SameLine();
         ImGui::TextDisabled("%s", n->path.substr(n->path.find_last_of("/\\") == std::string::npos ? 0 : n->path.find_last_of("/\\") + 1).c_str());
      }
      else
         ImGui::TextDisabled("Load a .mid file, or drop one on the canvas");

      {
         const float w = gAudioBodyW;
         const float h = 96.0f;
         const ImVec2 origin = ImGui::GetCursorScreenPos();
         const ImVec2 br(origin.x + w, origin.y + h);
         ImDrawList* dl = ImGui::GetWindowDrawList();
         const bool isLight = IsThemeLight();
         dl->AddRectFilled(origin, br, isLight ? tok::U32(tok::pal::c_ECF0F8FF) : tok::U32(tok::pal::c_101016FF), 4.0f);
         if (song && !song->events.empty())
         {
            const double len = n->LoopBeats();
            const int lo = std::max(0, (int)song->lowNote - 1);
            const int hi = std::min(127, (int)song->highNote + 1);
            const float noteH = std::max(1.5f, (h - 6.0f) / (float)(hi - lo + 1));
            const ImU32 on = IM_COL32((int)(gAudioTint.r * 255.0f), (int)(gAudioTint.g * 255.0f),
                                      (int)(gAudioTint.b * 255.0f), 220);
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
                        const float y = br.y - 3.0f - (float)(key - lo + 1) * noteH;
                        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + noteH - 0.5f), on);
                        open[key].erase(open[key].begin() + (long)i);
                        break;
                     }
            }
            const double ph = n->PlayheadBeats();
            if (ph >= 0.0)
            {
               const float x = origin.x + (float)(std::min(ph, len) / len) * w;
               dl->AddLine(ImVec2(x, origin.y), ImVec2(x, br.y), isLight ? tok::U32(tok::pal::c_28283CE6) : tok::U32(tok::pal::c_F0F0FFE6), 1.5f);
            }
            dl->PopClipRect();
         }
         ImGui::Dummy(ImVec2(w, h));
      }

      {
         AudioKnobRow row(5);
         row.KnobInt("transpose", &n->transpose, -48, 48);
         row.Knob("position", &n->position, 0.0f, 1.0f, "%.2f", kKnobSmall);
         row.Knob("speed", &n->speed, 0.25f, 4.0f, "%.2fx", kKnobSmall);
         row.Knob("vel", &n->velocity, 0.0f, 2.0f, "%.2f", kKnobSmall);
         row.Checkbox("loop", &n->loop);
         row.End();
      }

      EndAudioBody();
   }


   void DrawRandomNoteGeneratorBody(GraphNode& gn, RandomNoteGeneratorNode* n)
   {
      char stat[64];
      const int last = n->LastNote();
      if (last < 0)
      {
         if (n->groove > 0.001f)
            snprintf(stat, sizeof(stat), "%s in %s..%s - %.0f%% groove", MusicTime::ScaleTable(n->scale).name,
                     NoteNameList()[n->rangeLow % 12].c_str(), NoteNameList()[n->rangeHigh % 12].c_str(),
                     n->groove * 100.0f);
         else
            snprintf(stat, sizeof(stat), "%s in %s..%s", MusicTime::ScaleTable(n->scale).name,
                     NoteNameList()[n->rangeLow % 12].c_str(), NoteNameList()[n->rangeHigh % 12].c_str());
      }
      else
      {
         if (n->groove > 0.001f)
            snprintf(stat, sizeof(stat), "last: %s%d - %.0f%% groove", NoteNameList()[last % 12].c_str(),
                     last / 12 - 1, n->groove * 100.0f);
         else
            snprintf(stat, sizeof(stat), "last: %s%d", NoteNameList()[last % 12].c_str(), last / 12 - 1);
      }

      BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

      // Row 1 (4): selector column left (scale, root), knobs right (lo, hi).
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
         row.KnobInt("lo", &n->rangeLow, 0, 127, kKnobSmall);
         row.KnobInt("hi", &n->rangeHigh, 0, 127, kKnobSmall);
         row.End();
      }
      // Row 2 (4): same grid - mode/rate selector column left, knobs right.
      // DrawRateModeControls emits the mode dropdown + rate cell as two cells
      // with stable ordinals (see its header comment), so cell indices never
      // move when rate mode flips between Synced and Free.
      {
         AudioKnobRow row(4);
         DrawRateModeControls(row, &n->rateMode, &n->rateBeats, &n->rateSeconds);
         row.KnobInt("wander", &n->maxStep, 1, 12);
         row.Knob("groove", &n->groove, 0.0f, 1.0f, "%.2f", kKnobSmall);
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
         // Chord rate is a step of the global rhythmic table (MusicTime::RateDivisionList: 4 bars ..
         // 1/64, dotted and triplet), not a free beats knob. rateBeats stays the saved float.
         int div = NearestRateDivision(n->rateBeats);
         row.Dropdown("rate", MusicTime::RateDivisionList(), div, [n](int i) {
            PushUndoCheckpoint();
            n->rateBeats = (float)MusicTime::BeatsFor(
               (MusicTime::RateDivision)std::clamp(i, 0, (int)MusicTime::kNumRateDivisions - 1));
         });
         row.KnobInt("chord", &n->chordSize, 2, 6);
         row.End();
      }
      {
         AudioKnobRow row(4);
         row.Knob("strum", &n->strumMs, 0.0f, 200.0f, "%.0fms", kKnobSmall, false, false, AudioWidgetStyle::KnobSkewStrum30);
         row.Knob("hu.time", &n->humanizeTimingMs, 0.0f, 100.0f, "%.0fms", kKnobSmall);
         row.Knob("hu.vel", &n->humanizeVelocity, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.Knob("harmonics", &n->upperHarmonics, 0.0f, 100.0f, "%.0f%%", kKnobSmall);
         row.End();
      }

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
      ImGui::SetWindowFontScale(tok::type_body / ImGui::GetFontSize());
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
      ImGui::Dummy(ImVec2(0.0f, 0.0f)); // the moved cursor must end on an item or ImGui flags the window growing
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
         ImGui::TextColored(tok::V4(tok::palf::v_690_710_780_1000), "in - always on");
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
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      const float btnW = (AudioFullWidth() - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
      if (ActionButton::Draw("Record", ImVec2(btnW, 0), (recording) ? ActionButton::Kind::Record : ActionButton::Kind::Plain))
      {
         if (recording) n->StopRecording();
         else n->StartRecording();
      }
      ImGui::SameLine();

      ImGui::BeginDisabled(n->RecordedCount() == 0 && !playing);
      if (ActionButton::Draw("Play", ImVec2(btnW, 0), (playing) ? ActionButton::Kind::Go : ActionButton::Kind::Plain))
      {
         if (playing) n->StopPlayback();
         else n->StartPlayback();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();

      if (ActionButton::Draw("Stop", ImVec2(btnW, 0)))
      {
         n->StopRecording();
         n->StopPlayback();
      }
      ImGui::SameLine();

      if (ActionButton::Draw("Clear", ImVec2(btnW, 0)))
         n->ClearRecording();

      ImGui::Dummy(ImVec2(0.0f, 2.0f));
      {
         // Same 4-cell grid as the Record/Play/Stop/Clear strip above, so
         // `loop` lands directly under Record instead of floating at the
         // body's left edge under an unrelated cell count. Small explicit
         // height (matches the Flanger/Phaser trailing-checkbox-row
         // precedent) since a lone checkbox doesn't need full knob height
         // plus a caption-line reservation.
         AudioKnobRow row(4, 20.0f, 8.0f, false);
         row.Checkbox("loop##capturerLoop", &n->loop);
         row.Skip();
         row.Skip();
         row.Skip();
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
      AudioViz::Fill(dl, origin, br);
      dl->PushClipRect(origin, br, true);

      const ImVec2 center((origin.x + br.x) * 0.5f, (origin.y + br.y) * 0.5f);
      const float scale = (w * 0.5f - 6.0f) / BouncingBallsNode::kBound;
      auto ToScreen = [&](float x, float y) { return ImVec2(center.x + x * scale, center.y - y * scale); };

      const ImU32 outlineCol = isLight ? tok::U32(tok::pal::c_8C96AFFF) : tok::U32(tok::pal::c_96A5BEFF);
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
         const ImU32 restCol = isLight ? tok::U32(tok::pal::c_1E6EE6F5) : tok::U32(tok::pal::c_78C8FFEB);
         const ImU32 hitCol = isLight ? tok::U32(tok::pal::c_E67814FF) : tok::U32(tok::pal::c_FFBE5AFF);
         const ImU32 col = ImGui::ColorConvertFloat4ToU32(
            ImLerp(ImGui::ColorConvertU32ToFloat4(restCol), ImGui::ColorConvertU32ToFloat4(hitCol), flash));
         dl->AddCircleFilled(p, n->ballSize * scale, col);
      }

      dl->PopClipRect();
      AudioViz::Border(dl, origin, br);
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

      EndAudioBody();
   }


   // Single-macro bodies below: identical standard rhythm (narrow body, one
   // centred large knob, no second row) as Note Transpose / Pitch Bend / Gate
   // / Glide / Vibrato.
   void DrawStrumBody(GraphNode& gn, NoteStrumNode* n)
   {
      char stat[48];
      snprintf(stat, sizeof(stat), "%.1f ms", n->strumMs);
      BeginAudioBody(gn.index, gn.category, kAudioNarrowWidth, stat);

      {
         AudioKnobRow row(1, kKnobLarge);
         row.Knob("strum", &n->strumMs, 0.0f, 100.0f, "%.1f ms", kKnobLarge, false, false, AudioWidgetStyle::KnobSkewStrum20);
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
                        tok::U32(tok::pal::c_96D6FFF5));
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
}
